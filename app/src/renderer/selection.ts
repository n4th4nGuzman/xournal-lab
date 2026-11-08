import type { Editor, Gesture } from "./editor";
import {
  applyAff, cloneElement, elementBBox, flipAff, hitElement, inPolygon, inRect, pointInRect, restore, rotateAff, scaleAff, snapshot,
  transformElement, translateAff, unionRect, type Aff, type Rect, type Snap,
} from "./geometry";
import { activeLayer, setAttr, type Layer, type PageElement } from "./model";
import { drawElement } from "./render";
import { newPlaneId, planeIdOf, planeMembers } from "./plane";
import { type FigureSpec } from "./figures";
import type { MathSpec } from "./mathobj";
import { withAnchor, type AnchorSide, type Space3Spec } from "./space3";

interface Item { layer: Layer; el: PageElement }

/** Pieces of the same plane are one object: selecting any of them selects them all. */
function withPlaneMembers(found: Item[]): Item[] {
  const out = found.slice();
  const have = new Set(found.map((i) => i.el));
  for (const it of found) {
    const id = planeIdOf(it.el);
    if (!id) continue;
    for (const m of planeMembers(it.layer, id)) if (!have.has(m)) { have.add(m); out.push({ layer: it.layer, el: m }); }
  }
  return out;
}
interface Slot extends Item { index: number }

export type Handle = "nw" | "n" | "ne" | "e" | "se" | "s" | "sw" | "w" | "rot";
const SIZE_HANDLES: Handle[] = ["nw", "n", "ne", "e", "se", "s", "sw", "w"];
const CURSORS: Record<Handle, string> = {
  nw: "nwse-resize", se: "nwse-resize", ne: "nesw-resize", sw: "nesw-resize",
  n: "ns-resize", s: "ns-resize", e: "ew-resize", w: "ew-resize", rot: "grab",
};
export type QuickTransform = "rotL" | "rotR" | "flipH" | "flipV";

const MAX_BITMAP_PX = 4096;
const CLICK_SLOP = 3; // page units: smaller drags are clicks
const HIT_PAD = 4;
const HANDLE_PX = 8; // drawn size and grab tolerance, in screen pixels
const ROT_OFFSET_PX = 26; // rotation handle distance above the frame
const MIN_SCALE = 0.05;
const SNAP_STEP = Math.PI / 12; // 15 degrees with Shift
/** Protractor shown while rotating (exported for tests). */
export const PROTRACTOR = { minRadiusPx: 70, arcFactor: 0.55 };

function handlePoint(h: Handle, b: Rect, zoom: number): [number, number] {
  const mx = (b[0] + b[2]) / 2, my = (b[1] + b[3]) / 2;
  if (h === "rot") return [mx, b[1] - ROT_OFFSET_PX / zoom];
  const x = h.includes("w") ? b[0] : h.includes("e") ? b[2] : mx;
  const y = h.includes("n") ? b[1] : h.includes("s") ? b[3] : my;
  return [x, y];
}
/** The fixed point opposite a handle: the far corner, or the far edge's midpoint. */
function anchorOf(h: Handle, b: Rect): [number, number] {
  const mx = (b[0] + b[2]) / 2, my = (b[1] + b[3]) / 2;
  const x = h.includes("w") ? b[2] : h.includes("e") ? b[0] : mx;
  const y = h.includes("n") ? b[3] : h.includes("s") ? b[1] : my;
  return [x, y];
}
const clampMag = (v: number) => (Math.abs(v) < MIN_SCALE ? (v < 0 ? -MIN_SCALE : MIN_SCALE) : v);

interface Lifted { bbox: Rect; bitmap: HTMLCanvasElement; slots: Slot[]; snaps: Snap[] }
type Drag =
  | { mode: "anchor"; axis: 0 | 1 | 2; side: AnchorSide; spec: Space3Spec; cur?: Space3Spec }
  | { mode: "figanchor"; i: number; spec: FigureSpec; cur?: FigureSpec }
  | { mode: "mathanchor"; i: number; spec: MathSpec; cur?: MathSpec }
  | { mode: "rect"; x0: number; y0: number; x1: number; y1: number }
  | { mode: "lasso"; poly: number[] }
  | ({ mode: "move"; x0: number; y0: number; m: Aff; vspace?: { y: number; minDy: number; maxDy: number } } & Lifted)
  | ({ mode: "resize"; handle: Handle; ax: number; ay: number; uniform: boolean; m: Aff } & Lifted)
  | ({ mode: "rotate"; cx: number; cy: number; a0: number; prev: number; total: number; shown: number; snapped: boolean; r0px: number; m: Aff } & Lifted);

export class Selection {
  private page = -1;
  private items: Item[] = [];
  private drag: Drag | null = null;
  private clip: PageElement[] = [];
  private clipPage = -1;
  private pasteCount = 0;

  constructor(private ed: Editor) {}

  get active(): boolean { return this.items.length > 0; }
  get count(): number { return this.items.length; }
  get pageIndex(): number { return this.page; }
  get elements(): PageElement[] { return this.items.map((i) => i.el); }
  /** The selected elements with the layer each one is in. */
  get entries(): Array<{ layer: Layer; el: PageElement }> { return this.items.slice(); }

  clear(): void {
    const had = this.items.length > 0 || this.drag !== null;
    this.items = [];
    this.page = -1;
    this.drag = null;
    if (had) this.ed.clearOverlay();
  }

  selectOnly(page: number, layer: Layer, el: PageElement): void {
    this.page = page;
    this.items = withPlaneMembers([{ layer, el }]);
    this.redraw();
    this.ed.host.changed();
  }

  /** Select these elements (all in `layer`). */
  selectMany(page: number, layer: Layer, els: PageElement[]): void {
    this.page = page;
    this.items = withPlaneMembers(els.map((el) => ({ layer, el })));
    this.redraw();
    this.ed.host.changed();
  }

  private bbox(): Rect | null {
    let r: Rect | null = null;
    for (const it of this.items) {
      const b = elementBBox(it.el);
      if (b) r = unionRect(r, b);
    }
    return r;
  }

  /** Drop items that are no longer in the model (after undo/redo) and redraw. */
  refresh(): void {
    // A plane that was rebuilt (undo or redo of a change to it) stays selected as the plane it is.
    const planes = new Map<string, Layer>();
    for (const it of this.items) { const id = planeIdOf(it.el); if (id) planes.set(id, it.layer); }
    const inLayer = new Map<Layer, Set<PageElement>>();
    this.items = this.items.filter((it) => { let set = inLayer.get(it.layer); if (!set) { set = new Set(it.layer.elements); inLayer.set(it.layer, set); } return set.has(it.el); });
    for (const [id, layer] of planes) for (const m of planeMembers(layer, id)) if (!this.items.some((i) => i.el === m)) this.items.push({ layer, el: m });
    if (!this.items.length) this.clear();
    else this.redraw();
  }

  // ---- hit-testing the selection frame -----------------------------------------
  private handleAt(x: number, y: number, b: Rect): Handle | null {
    const z = this.ed.host.zoom();
    const tol = HANDLE_PX / z;
    for (const h of [...SIZE_HANDLES, "rot" as Handle]) {
      const [hx, hy] = handlePoint(h, b, z);
      if (Math.abs(x - hx) <= tol && Math.abs(y - hy) <= tol) return h;
    }
    return null;
  }

  /** CSS cursor for a pointer over page `page` at page coordinates (x, y). */
  cursorAt(page: number, x: number, y: number): string {
    const b = this.items.length && page === this.page ? this.bbox() : null;
    if (!b) return "";
    const h = this.handleAt(x, y, b);
    if (h) return CURSORS[h];
    return pointInRect(x, y, b) ? "move" : "";
  }

  // ---- gestures ---------------------------------------------------------------
  /** Whether a press at (x, y) would grab the selection (an anchor, a handle or its body) instead of starting a new selection. */
  wouldHit(g: Gesture, x: number, y: number): boolean {
    const b = this.items.length && g.page === this.page ? this.bbox() : null;
    if (!b) return false;
    return !!this.ed.space3AnchorAt(x, y) || !!this.ed.figureAnchorAt(x, y) || !!this.ed.mathAnchorAt(x, y) || !!this.handleAt(x, y, b) || pointInRect(x, y, [b[0] - 2, b[1] - 2, b[2] + 2, b[3] + 2]);
  }

  down(g: Gesture, x: number, y: number, newSelection = false): void {
    const b = this.items.length && g.page === this.page ? this.bbox() : null;
    if (b && !newSelection) {
      const anchor = this.ed.space3AnchorAt(x, y); // the ends of a 3D space's axes turn that axis
      if (anchor) { this.drag = { mode: "anchor", axis: anchor.axis, side: anchor.side, spec: anchor.spec }; return; }
      const fa = this.ed.figureAnchorAt(x, y); // the control points of a figure
      if (fa) { this.drag = { mode: "figanchor", i: fa.i, spec: fa.spec }; return; }
      const ma = this.ed.mathAnchorAt(x, y); // the ends of a number line, the circles of a diagram
      if (ma) { this.drag = { mode: "mathanchor", i: ma.i, spec: ma.spec }; return; }
      const h = this.handleAt(x, y, b);
      if (h === "rot") { this.startRotate(g, x, y, b); return; }
      if (h) { this.startResize(g, h, b); return; }
      if (pointInRect(x, y, [b[0] - 2, b[1] - 2, b[2] + 2, b[3] + 2])) { this.startMove(g, x, y, b); return; }
    }
    this.items = [];
    this.page = g.page;
    this.drag = g.tool === "lasso" ? { mode: "lasso", poly: [x, y] } : { mode: "rect", x0: x, y0: y, x1: x, y1: y };
    this.ed.clearOverlay();
  }

  move(g: Gesture, x: number, y: number, shift = false): void {
    const d = this.drag;
    if (!d) return;
    const { canvas, ctx } = this.ed.overlay();
    const z = this.ed.host.zoom();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    this.ed.overlayTransformFor(g.el);
    if (d.mode === "anchor") {
      d.cur = withAnchor(d.spec, d.axis, d.side, [x, y], shift); // only that half of the axis moves; Shift: it keeps its length and only turns
      this.ed.drawSpace3Drag(ctx, z, d.cur);
      return;
    }
    if (d.mode === "mathanchor") {
      d.cur = this.ed.moveMathAnchor(d.spec, d.i, [x, y], shift);
      this.ed.drawMathDrag(ctx, z, d.cur);
      return;
    }
    if (d.mode === "figanchor") {
      d.cur = this.ed.moveFigureAnchor(d.spec, d.i, [x, y], shift);
      this.ed.drawFigureDrag(ctx, z, d.cur);
      return;
    }
    switch (d.mode) {
      case "rect":
        d.x1 = x; d.y1 = y;
        this.strokeBox(ctx, [Math.min(d.x0, d.x1), Math.min(d.y0, d.y1), Math.max(d.x0, d.x1), Math.max(d.y0, d.y1)], z, true);
        return;
      case "lasso": {
        const n = d.poly.length;
        const dx = x - d.poly[n - 2]!, dy = y - d.poly[n - 1]!;
        if (dx * dx + dy * dy >= 0.25) d.poly.push(x, y);
        ctx.lineWidth = 1 / z;
        ctx.setLineDash([5 / z, 3 / z]);
        ctx.strokeStyle = "#2f6fd0";
        ctx.fillStyle = "rgba(47,111,208,0.12)";
        ctx.beginPath();
        ctx.moveTo(d.poly[0]!, d.poly[1]!);
        for (let i = 2; i + 1 < d.poly.length; i += 2) ctx.lineTo(d.poly[i]!, d.poly[i + 1]!);
        ctx.fill();
        ctx.stroke();
        ctx.setLineDash([]);
        return;
      }
      case "move":
        d.m = d.vspace
          ? translateAff(0, Math.min(d.vspace.maxDy, Math.max(d.vspace.minDy, y - d.y0)))
          : translateAff(x - d.x0, y - d.y0);
        break;
      case "resize": {
        const [hx, hy] = handlePoint(d.handle, d.bbox, z);
        const moveX = d.handle.includes("e") || d.handle.includes("w");
        const moveY = d.handle.includes("n") || d.handle.includes("s");
        // Dragging past the opposite side gives a negative factor: a mirror.
        let sx = moveX && hx !== d.ax ? clampMag((x - d.ax) / (hx - d.ax)) : 1;
        let sy = moveY && hy !== d.ay ? clampMag((y - d.ay) / (hy - d.ay)) : 1;
        if (d.uniform) {
          const s = moveX && moveY ? Math.max(Math.abs(sx), Math.abs(sy)) : Math.abs(moveX ? sx : sy);
          sx = (moveX ? Math.sign(sx) || 1 : 1) * s;
          sy = (moveY ? Math.sign(sy) || 1 : 1) * s;
        }
        d.m = scaleAff(sx, sy, d.ax, d.ay);
        break;
      }
      case "rotate": {
        // Accumulate the angle so turns beyond 180 degrees keep counting.
        const ang = Math.atan2(y - d.cy, x - d.cx);
        let delta = ang - d.prev;
        while (delta > Math.PI) delta -= 2 * Math.PI;
        while (delta <= -Math.PI) delta += 2 * Math.PI;
        d.total += delta;
        d.prev = ang;
        const th = shift ? Math.round(d.total / SNAP_STEP) * SNAP_STEP : d.total;
        d.shown = th;
        d.snapped = shift;
        d.m = rotateAff(th, d.cx, d.cy);
        break;
      }
    }
    // Move / resize / rotate: blit the pre-rendered selection through the map.
    const [bx, by, bx1, by1] = d.bbox;
    ctx.save();
    ctx.transform(d.m.a, d.m.b, d.m.c, d.m.d, d.m.e, d.m.f);
    ctx.drawImage(d.bitmap, bx, by, bx1 - bx, by1 - by);
    ctx.restore();
    if (d.mode === "move" && d.vspace) {
      // The line the space opens from.
      const page = this.pageOf(g)!;
      ctx.lineWidth = 1.5 / z;
      ctx.setLineDash([6 / z, 4 / z]);
      ctx.strokeStyle = "#2f6fd0";
      ctx.beginPath(); ctx.moveTo(0, d.vspace.y); ctx.lineTo(page.width, d.vspace.y); ctx.stroke();
      ctx.setLineDash([]);
    } else this.strokePolygon(ctx, d.bbox, d.m, z);
    if (d.mode === "rotate") this.drawProtractor(ctx, d, z);
  }

  up(g: Gesture, x: number, y: number): void {
    const d = this.drag;
    this.drag = null;
    if (!d) return;
    if (d.mode === "anchor") { if (d.cur) this.ed.updateSpace3(d.cur); else this.redraw(); }
    else if (d.mode === "figanchor") { if (d.cur) this.ed.updateFigure(d.cur); else this.redraw(); }
    else if (d.mode === "mathanchor") { if (d.cur) this.ed.updateMath(d.cur); else this.redraw(); }
    else if (d.mode === "rect") this.finishRect(g, d, x, y);
    else if (d.mode === "lasso") this.finishLasso(g, d);
    else this.finishTransform(g, d);
  }

  private pageOf(g: Gesture) { return this.ed.host.doc()?.pages[g.page]; }

  private finishRect(g: Gesture, d: Extract<Drag, { mode: "rect" }>, x: number, y: number): void {
    const page = this.pageOf(g);
    if (!page) return;
    const r: Rect = [Math.min(d.x0, x), Math.min(d.y0, y), Math.max(d.x0, x), Math.max(d.y0, y)];
    const found: Item[] = [];
    if (r[2] - r[0] < CLICK_SLOP && r[3] - r[1] < CLICK_SLOP) {
      // A click selects the topmost element under the pointer.
      outer: for (let l = page.layers.length - 1; l >= 0; l--) {
        const layer = page.layers[l]!;
        if (layer.hidden) continue;
        for (let i = layer.elements.length - 1; i >= 0; i--) {
          if (hitElement(layer.elements[i]!, x, y, HIT_PAD)) { found.push({ layer, el: layer.elements[i]! }); break outer; }
        }
      }
    } else {
      for (const layer of page.layers) if (!layer.hidden) for (const el of layer.elements) if (inRect(el, r)) found.push({ layer, el });
    }
    this.setFound(g, found);
  }

  private finishLasso(g: Gesture, d: Extract<Drag, { mode: "lasso" }>): void {
    const page = this.pageOf(g);
    if (!page) return;
    const found: Item[] = [];
    if (d.poly.length >= 6) for (const layer of page.layers) if (!layer.hidden) for (const el of layer.elements) if (inPolygon(el, d.poly)) found.push({ layer, el });
    this.setFound(g, found);
  }

  private setFound(g: Gesture, found: Item[]): void {
    this.items = withPlaneMembers(found);
    found = this.items;
    this.page = g.page;
    if (found.length) this.redraw(); else this.ed.clearOverlay();
    this.ed.host.changed();
  }

  // ---- shared by move / resize / rotate / delete --------------------------------
  private slots(): Slot[] {
    const where = new Map<Layer, Map<PageElement, number>>(); // one pass per layer instead of a search per element
    return this.items
      .map((it) => {
        let m = where.get(it.layer);
        if (!m) { m = new Map(); it.layer.elements.forEach((e, i) => m!.set(e, i)); where.set(it.layer, m); }
        return { ...it, index: m.get(it.el) ?? -1 };
      })
      .filter((s) => s.index >= 0)
      .sort((a, b) => a.index - b.index);
  }
  private remove(slots: Slot[]): void {
    for (let k = slots.length - 1; k >= 0; k--) slots[k]!.layer.elements.splice(slots[k]!.index, 1);
  }
  private reinsert(slots: Slot[]): void {
    for (const s of slots) s.layer.elements.splice(s.index, 0, s.el);
  }

  /** Pre-render the selection once; dragging then only blits one bitmap. */
  private renderBitmap(slots: Slot[], bbox: Rect): HTMLCanvasElement {
    const w = bbox[2] - bbox[0], h = bbox[3] - bbox[1];
    const k = Math.min(this.ed.host.zoom() * (window.devicePixelRatio || 1), MAX_BITMAP_PX / Math.max(w, h, 1));
    const bitmap = document.createElement("canvas");
    bitmap.width = Math.max(1, Math.ceil(w * k));
    bitmap.height = Math.max(1, Math.ceil(h * k));
    const bctx = bitmap.getContext("2d")!;
    bctx.setTransform(k, 0, 0, k, -bbox[0] * k, -bbox[1] * k);
    for (const s of slots) drawElement(bctx, s.el, () => {});
    return bitmap;
  }

  /** Take the selection out of the model while it is being dragged. */
  private lift(g: Gesture, bbox: Rect): Lifted {
    const doc = this.ed.host.doc()!;
    const slots = this.slots();
    const bitmap = this.renderBitmap(slots, bbox);
    const snaps = slots.map((s) => snapshot(s.el));
    this.remove(slots);
    doc.elementCount -= slots.length;
    this.ed.host.repaint(g.page);
    return { bbox, bitmap, slots, snaps };
  }

  private startMove(g: Gesture, x: number, y: number, bbox: Rect): void {
    this.drag = { mode: "move", x0: x, y0: y, m: translateAff(0, 0), ...this.lift(g, bbox) };
    this.move(g, x, y);
  }

  private startResize(g: Gesture, handle: Handle, bbox: Rect): void {
    const [ax, ay] = anchorOf(handle, bbox);
    // Text keeps its shape, so a selection containing text scales uniformly.
    const uniform = handle.length === 2 || this.items.some((i) => i.el.kind === "text" || i.el.kind === "teximage");
    this.drag = { mode: "resize", handle, ax, ay, uniform, m: scaleAff(1, 1, ax, ay), ...this.lift(g, bbox) };
    const [hx, hy] = handlePoint(handle, bbox, this.ed.host.zoom());
    this.move(g, hx, hy);
  }

  private startRotate(g: Gesture, x: number, y: number, bbox: Rect): void {
    const cx = (bbox[0] + bbox[2]) / 2, cy = (bbox[1] + bbox[3]) / 2;
    const a0 = Math.atan2(y - cy, x - cx);
    const r0px = Math.max(PROTRACTOR.minRadiusPx, Math.hypot(x - cx, y - cy) * this.ed.host.zoom());
    this.drag = { mode: "rotate", cx, cy, a0, prev: a0, total: 0, shown: 0, snapped: false, r0px, m: rotateAff(0, cx, cy), ...this.lift(g, bbox) };
    this.move(g, x, y);
  }

  private isIdentity(m: Aff): boolean {
    const e = 1e-9;
    return Math.abs(m.a - 1) < e && Math.abs(m.d - 1) < e && Math.abs(m.b) < e && Math.abs(m.c) < e && Math.abs(m.e) < 1e-4 && Math.abs(m.f) < 1e-4;
  }

  private finishTransform(g: Gesture, d: Extract<Drag, { mode: "move" | "resize" | "rotate" }>): void {
    const doc = this.ed.host.doc()!;
    const changed = !this.isIdentity(d.m);
    if (changed) for (const s of d.snaps) transformElement(s.el, d.m);
    this.reinsert(d.slots);
    doc.elementCount += d.slots.length;
    // A resized plane is rebuilt for its new size (ticks, numbers and arrows keep their proper size), in the same undo step.
    const regen = changed && d.mode === "resize" ? this.ed.rebuildSelectedPlane(this.items) : null;
    if (changed) this.pushSnapshots(g.page, d.snaps, regen);
    if (regen) this.items = regen.fresh.map((el) => ({ layer: regen.layer, el }));
    this.ed.host.repaint(g.page);
    if (d.mode === "move" && d.vspace) { this.items = []; this.page = -1; this.ed.clearOverlay(); return; } // a space edit leaves no selection
    this.redraw();
  }

  /**
   * Vertical space: everything whose top is below the clicked line moves up or down with the pointer.
   * It cannot squeeze into the line or be pushed off the bottom of the page.
   */
  startVspace(g: Gesture, x: number, y: number): void {
    const page = this.pageOf(g);
    if (!page) return;
    const items: Item[] = [];
    let box: Rect | null = null;
    for (const layer of page.layers) {
      if (layer.hidden) continue;
      for (const el of layer.elements) {
        const b = elementBBox(el);
        if (b && b[1] >= y) { items.push({ layer, el }); box = unionRect(box, b); }
      }
    }
    if (!box) { this.ed.onNotice?.("No hay nada debajo de esa línea"); return; }
    this.items = items;
    this.page = g.page;
    const vspace = { y, minDy: y - box[1], maxDy: page.height - box[3] };
    this.drag = { mode: "move", x0: x, y0: y, m: translateAff(0, 0), vspace, ...this.lift(g, box) };
    this.move(g, x, y);
  }

  /** Record undo/redo for elements already transformed; `before` are their old states. */
  private pushSnapshots(page: number, before: Snap[], regen: { undo(): void; redo(): void } | null = null): void {
    const after = before.map((s) => snapshot(s.el));
    this.ed.pushAction({
      page,
      undo: () => { regen?.undo(); for (const s of before) restore(s); },
      redo: () => { for (const s of after) restore(s); regen?.redo(); },
    });
  }

  /** One-click rotate by 90 degrees or mirror the whole selection about its centre. */
  transformSelection(kind: QuickTransform): void {
    const b = this.items.length ? this.bbox() : null;
    if (!b) return;
    const cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
    const m = kind === "rotR" ? rotateAff(Math.PI / 2, cx, cy)
      : kind === "rotL" ? rotateAff(-Math.PI / 2, cx, cy)
      : flipAff(kind === "flipH", cx, cy);
    const before = this.items.map((i) => snapshot(i.el));
    for (const it of this.items) transformElement(it.el, m);
    this.pushSnapshots(this.page, before);
    if (this.items.some((i) => i.el.kind === "text")) {
      this.ed.onNotice?.("El texto no se puede rotar ni espejar (el formato .xopp no lo permite): se reubica pero sigue derecho. Las fórmulas LaTeX sí se giran.");
    }
    this.ed.host.repaint(this.page);
    this.redraw();
  }

  /** Select everything on a page. */
  selectAll(page: number): void {
    const p = this.ed.host.doc()?.pages[page];
    if (!p) return;
    this.items = p.layers.filter((l) => !l.hidden).flatMap((layer) => layer.elements.filter((el) => elementBBox(el)).map((el) => ({ layer, el })));
    this.page = page;
    if (this.items.length) this.redraw(); else this.ed.clearOverlay();
    this.ed.host.changed();
  }

  // ---- clipboard (inside the app) ---------------------------------------------
  copy(): boolean {
    const copies = this.items.map((i) => cloneElement(i.el)).filter((e): e is PageElement => e !== null);
    if (!copies.length) return false;
    this.clip = copies;
    this.clipPage = this.page;
    this.pasteCount = 0;
    this.ed.onNotice?.(`Copiado: ${copies.length} elemento${copies.length === 1 ? "" : "s"}`);
    return true;
  }

  cut(): void {
    if (this.copy()) this.deleteSelected();
  }

  /** Paste on the page in view. Repeated pastes on the copy's page shift diagonally so they do not overlap. */
  paste(): boolean {
    const doc = this.ed.host.doc();
    if (!doc || !this.clip.length) return false;
    const page = this.ed.host.currentPage();
    const target = doc.pages[page]!;
    const n = ++this.pasteCount;
    const off = 12 * (page === this.clipPage ? n : n - 1);
    const clones = this.clip.map((e) => cloneElement(e)!);
    // a pasted plane, figure, line or diagram is a new object: its pieces must not share the group of the original
    const fresh = new Map<string, string>();
    for (const c of clones) {
      const id = planeIdOf(c);
      if (!id || !("attrs" in c)) continue;
      if (!fresh.has(id)) fresh.set(id, newPlaneId());
      setAttr(c.attrs, "xlPlane", fresh.get(id)!);
    }
    if (off) for (const c of clones) transformElement(c, translateAff(off, off));
    const layer = activeLayer(target);
    layer.elements.push(...clones);
    doc.elementCount += clones.length;
    this.ed.pushAction({
      page,
      undo: () => { for (const c of clones) { const i = layer.elements.indexOf(c); if (i >= 0) layer.elements.splice(i, 1); } doc.elementCount -= clones.length; },
      redo: () => { layer.elements.push(...clones); doc.elementCount += clones.length; },
    });
    this.ed.setTool("select");
    this.page = page;
    this.items = clones.map((el) => ({ layer, el }));
    this.ed.host.repaint(page);
    this.redraw();
    return true;
  }

  duplicate(): void {
    if (this.copy()) this.paste();
  }

  /** Move the selection to the top or bottom of its layer's stacking order. */
  reorder(where: "front" | "back"): void {
    const byLayer = new Map<Layer, Set<PageElement>>();
    for (const it of this.items) (byLayer.get(it.layer) ?? byLayer.set(it.layer, new Set()).get(it.layer)!).add(it.el);
    const changes: Array<{ layer: Layer; before: PageElement[]; after: PageElement[] }> = [];
    for (const [layer, chosen] of byLayer) {
      const picked = layer.elements.filter((e) => chosen.has(e));
      const rest = layer.elements.filter((e) => !chosen.has(e));
      const after = where === "front" ? [...rest, ...picked] : [...picked, ...rest];
      if (after.some((e, i) => e !== layer.elements[i])) changes.push({ layer, before: layer.elements.slice(), after });
    }
    if (!changes.length) return;
    const set = (pick: "before" | "after") => { for (const c of changes) c.layer.elements.splice(0, c.layer.elements.length, ...c[pick]); };
    set("after");
    this.ed.pushAction({ page: this.page, undo: () => set("before"), redo: () => set("after") });
    this.ed.host.repaint(this.page);
    this.redraw();
  }

  deleteSelected(): void {
    const doc = this.ed.host.doc();
    if (!doc || !this.items.length) return;
    const page = this.page;
    const slots = this.slots();
    this.remove(slots);
    doc.elementCount -= slots.length;
    this.ed.host.repaint(page);
    this.ed.pushAction({
      page,
      undo: () => { this.reinsert(slots); doc.elementCount += slots.length; },
      redo: () => { this.remove(slots); doc.elementCount -= slots.length; },
    });
    this.items = [];
    this.page = -1;
    this.ed.clearOverlay();
  }

  // ---- drawing ----------------------------------------------------------------
  /** Draw the selection frame, its handles and the rotation handle. */
  redraw(): void {
    if (!this.items.length || this.page < 0 || this.drag) return;
    const host = this.ed.host;
    if (!host.staticCanvas(this.page)) { this.ed.clearOverlay(); return; }
    const b = this.bbox();
    if (!b) return;
    this.ed.clearOverlay();
    this.ed.overlayTransformFor(host.pageEl(this.page));
    const ctx = this.ed.overlay().ctx;
    const z = host.zoom();
    this.strokeBox(ctx, b, z, false);
    const s = HANDLE_PX / z;
    ctx.fillStyle = "#fff";
    ctx.strokeStyle = "#2f6fd0";
    ctx.lineWidth = 1.5 / z;
    ctx.beginPath();
    ctx.moveTo((b[0] + b[2]) / 2, b[1]);
    ctx.lineTo(...handlePoint("rot", b, z));
    ctx.stroke();
    for (const h of SIZE_HANDLES) {
      const [hx, hy] = handlePoint(h, b, z);
      ctx.fillRect(hx - s / 2, hy - s / 2, s, s);
      ctx.strokeRect(hx - s / 2, hy - s / 2, s, s);
    }
    const [rx, ry] = handlePoint("rot", b, z);
    ctx.beginPath();
    ctx.arc(rx, ry, s * 0.65, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    this.ed.drawSpace3Anchors(ctx, z);
    this.ed.drawFigureAnchors(ctx, z);
    this.ed.drawMathAnchors(ctx, z);
  }

  private strokeBox(ctx: CanvasRenderingContext2D, r: Rect, zoom: number, fill: boolean): void {
    ctx.lineWidth = 1 / zoom;
    ctx.setLineDash([5 / zoom, 3 / zoom]);
    ctx.strokeStyle = "#2f6fd0";
    if (fill) { ctx.fillStyle = "rgba(47,111,208,0.12)"; ctx.fillRect(r[0], r[1], r[2] - r[0], r[3] - r[1]); }
    ctx.strokeRect(r[0], r[1], r[2] - r[0], r[3] - r[1]);
    ctx.setLineDash([]);
  }

  /** Protractor: dial with 15-degree ticks measured from the start direction, both rays, and the swept arc with its value. */
  private drawProtractor(ctx: CanvasRenderingContext2D, d: Extract<Drag, { mode: "rotate" }>, zoom: number): void {
    const px = 1 / zoom;
    const { cx, cy, a0, shown: th } = d;
    const R = d.r0px * px;
    const blue = d.snapped ? "#ff8000" : "#2f6fd0";
    const pt = (a: number, r: number): [number, number] => [cx + Math.cos(a) * r, cy + Math.sin(a) * r];
    ctx.save();
    ctx.setLineDash([]);
    // dial
    ctx.beginPath();
    ctx.arc(cx, cy, R, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(47,111,208,0.05)";
    ctx.fill();
    ctx.lineWidth = px;
    ctx.strokeStyle = "rgba(47,111,208,0.6)";
    ctx.stroke();
    for (let k = 0; k < 24; k++) {
      const a = a0 + k * SNAP_STEP;
      const len = (k % 6 === 0 ? 14 : k % 3 === 0 ? 10 : 6) * px;
      ctx.lineWidth = (k % 6 === 0 ? 1.6 : 1) * px;
      ctx.beginPath();
      ctx.moveTo(...pt(a, R));
      ctx.lineTo(...pt(a, R - len));
      ctx.stroke();
    }
    ctx.font = `${11 * px}px system-ui, sans-serif`;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillStyle = "rgba(30,60,120,0.9)";
    [0, 90, 180, 270].forEach((deg) => ctx.fillText(`${deg}°`, ...pt(a0 + (deg * Math.PI) / 180, R + 13 * px)));
    // start ray (dashed grey) and current ray
    ctx.setLineDash([4 * px, 3 * px]);
    ctx.lineWidth = px;
    ctx.strokeStyle = "rgba(90,90,90,0.85)";
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(...pt(a0, R)); ctx.stroke();
    ctx.setLineDash([]);
    ctx.lineWidth = 2 * px;
    ctx.strokeStyle = blue;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(...pt(a0 + th, R)); ctx.stroke();
    // swept sector and arc
    const ra = R * PROTRACTOR.arcFactor;
    const sweep = Math.abs(th) > 2 * Math.PI ? Math.sign(th) * 2 * Math.PI : th;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.arc(cx, cy, ra, a0, a0 + sweep, sweep < 0);
    ctx.closePath();
    ctx.fillStyle = d.snapped ? "rgba(255,128,0,0.22)" : "rgba(47,111,208,0.22)";
    ctx.fill();
    ctx.lineWidth = 2.5 * px;
    ctx.beginPath();
    ctx.arc(cx, cy, ra, a0, a0 + sweep, sweep < 0);
    ctx.stroke();
    // value
    const deg = (th * 180) / Math.PI;
    const txt = Math.abs(deg) < 0.05 ? "0°" : `${d.snapped ? Math.abs(Math.round(deg)) : Math.abs(deg).toFixed(1)}° ${deg > 0 ? "↻" : "↺"}`;
    ctx.font = `bold ${15 * px}px system-ui, sans-serif`;
    const [tx, ty] = pt(a0 + sweep / 2, R * 0.8);
    ctx.lineWidth = 4 * px;
    ctx.strokeStyle = "rgba(255,255,255,0.95)";
    ctx.strokeText(txt, tx, ty);
    ctx.fillStyle = d.snapped ? "#b35900" : "#173f8a";
    ctx.fillText(txt, tx, ty);
    // centre
    ctx.beginPath();
    ctx.arc(cx, cy, 3 * px, 0, Math.PI * 2);
    ctx.fillStyle = blue;
    ctx.fill();
    ctx.restore();
  }

  /** Dashed outline of a rectangle after the map (stays crisp, unlike drawing inside the transform). */
  private strokePolygon(ctx: CanvasRenderingContext2D, r: Rect, m: Aff, zoom: number): void {
    const pts = [applyAff(m, r[0], r[1]), applyAff(m, r[2], r[1]), applyAff(m, r[2], r[3]), applyAff(m, r[0], r[3])];
    ctx.lineWidth = 1 / zoom;
    ctx.setLineDash([5 / zoom, 3 / zoom]);
    ctx.strokeStyle = "#2f6fd0";
    ctx.beginPath();
    ctx.moveTo(pts[0]![0], pts[0]![1]);
    for (const p of pts.slice(1)) ctx.lineTo(p[0], p[1]);
    ctx.closePath();
    ctx.stroke();
    ctx.setLineDash([]);
  }
}
