import { recognizeShape, shapePoints, splitStroke, strokeHit, type ShapeKind } from "./geometry";
import { setGlow, type Glow, activeLayer, attrValue, bboxOf, cssColor, type ImageEl, type Layer, type PageElement, type Stroke, type XoppDocument } from "./model";
import { drawElement, glowFilter } from "./render";
import { anyFloatingOpen } from "./floating";
import { BrushEngine, PressureMap, type BrushKind } from "./brush";
import { DEFAULT_TABLET, penButtonOf, type PenAction } from "./tablet";
import { buildMath, isMath, mathAnchors, moveMathAnchor, readMath, sanitizeMath, type MathSpec } from "./mathobj";
import { defaultLine, type LineSpec } from "./numberline";
import { defaultVenn, type VennSpec } from "./venn";
import { buildFigure, clicksKind, defaultSpec as defaultFigure, figureSpecOf, isFigure, moveFigureAnchor, sanitizeFigure, type FigKind, type FigureSpec } from "./figures";
import { DEFAULT_SPACE3, anchorsOf, buildSpace3, extentOf, isSpace3, placedSpec, project, sanitizeSpace3, space3SpecOf, space3StyleOf, type AnchorSide, type Space3Spec, type V2 } from "./space3";
import { DEFAULT_PLANE, buildPlane, newPlaneId, planeFrameOf, planeIdOf, planeMembers, planeSpecOf, planeStyleOf, sanitizePlane, squareFrame, type Frame, type PlaneSpec, type PlaneStyle } from "./plane";
import { DEFAULT_FILL, computeFill, fillImage, isGenerated, fillScale, isClosed, colCorners, measureRiemann, pathLength, rectsMask, riemannBases, riemannColumns, simplifyPath, snapPathEnds, type FillOptions, type FillMask, type RiemannResult } from "./fill";
import { Selection } from "./selection";

// Low-latency presentation path for the live overlay; see the frame-time measurements.
export const DESYNC = !location.search.includes("sync");

/** Tools that make an object and then leave it selected. */
const INSERTERS: ReadonlySet<string> = new Set(["plane", "space3", "figure", "numline", "venn", "shape", "text", "latex"]);

export type Tool = "pen" | "highlighter" | "eraser" | "eraserPart" | "select" | "lasso" | "vspace" | "laser" | "shape" | "text" | "latex" | "hand" | "fill" | "plane" | "space3" | "figure" | "numline" | "venn";

/** How many steps can be undone. */
const MAX_UNDO = 500;
export const PALETTE = ["#000000", "#3333cc", "#ff0000", "#008000", "#ff8000"];
export const PEN_WIDTHS = [0.85, 1.41, 2.26];
export const HIGHLIGHTER_WIDTHS = [2.83, 5.66, 11.32];
export const TEXT_SIZES = [10, 12, 14, 18, 24];
const HIGHLIGHTER_ALPHA_HEX = "7f"; // observed in the sample files
export const ERASER_SIZES = [4, 8, 16, 32]; // radius, page units

export interface Host {
  doc(): XoppDocument | null;
  zoom(): number;
  /** Viewport-sized overlay used for the stroke in progress (size independent of zoom). */
  liveCanvas(): HTMLCanvasElement;
  /** Static (committed) canvas of a visible page, or null when not rendered. */
  staticCanvas(i: number): HTMLCanvasElement | null;
  pageEl(i: number): HTMLElement;
  /** First page currently in view. */
  currentPage(): number;
  /** Draw one newly committed stroke onto the page's static canvas. */
  commitPaint(i: number, s: Stroke): void;
  /** Repaint a page's static canvas from the model. */
  repaint(i: number): void;
  /** Redraw only the part of a page inside `rect` (page units: x0, y0, x1, y1). */
  repaintRect(i: number, rect: [number, number, number, number]): void;
  /** Repaint every page currently rendered. */
  repaintVisible(): void;
  /** The page structure or sizes changed: rebuild the page views and go to `focus`. */
  rebuildPages(focus?: number): void;
  /** Something changed: `page` is the only page whose content did, when that is known (else all may have). */
  changed(page?: number): void;
}

export interface Action {
  page: number;
  /** Pages were added, removed, moved or resized. */
  structural?: boolean;
  /** Something that affects every rendered page changed (e.g. paper for all pages). */
  repaintAll?: boolean;
  undo(): void;
  redo(): void;
}

interface EraseEntry { layer: Layer; index: number; removed: Stroke; added: Stroke[] }

export interface Gesture {
  page: number;
  /** Union of what the eraser touched since the last repaint (page units). */
  dirty?: [number, number, number, number];
  previewQueued?: boolean;
  /** Shift was held at the last move (fill tool: snap the base line's angle). */
  shift?: boolean;
  el: HTMLElement;
  pointerId: number;
  tool: Tool;
  rect: DOMRect;
  scroll0: [number, number];
  pts: number[];
  widths: number[];
  pen: boolean;
  brush?: BrushEngine;
  shapeStart?: [number, number];
  live?: { canvas: HTMLCanvasElement; ctx: CanvasRenderingContext2D; ox: number; oy: number };
  removed: EraseEntry[];
  repaintQueued: boolean;
}

export interface MoveStats { moveMs: number[]; commitMs: number[]; eraseRepaintMs: number[] }

export class Editor {
  tool: Tool = "pen";
  color = PALETTE[0]!;
  /** New pen and shape strokes get a glow when this is on (highlighter strokes never do). */
  /** Settings for the next Cartesian plane (and the shown ones of the selected plane while it is edited). */
  planeSpec: PlaneSpec = { ...DEFAULT_PLANE };
  /** Settings for the next 3D space (its page geometry is the default view, scaled when it is placed). */
  space3Spec: Space3Spec = { ...DEFAULT_SPACE3 };
  /** The figure tool: what kind it makes and its options (heads, dash, sides, hidden edges, label). */
  figKind: FigKind = "segment";
  figOpts: Pick<FigureSpec, "heads" | "dash" | "n" | "m" | "hidden" | "label"> = { heads: "none", dash: "solid", n: 5, m: 3, hidden: "dash", label: "" };
  /** Points clicked so far for a figure made by successive clicks (polyline, polygon, curve by points). */
  figPending: number[] = [];
  private figPage = -1;
  /** Options of the paint-bucket tool. */
  fill: FillOptions = { ...DEFAULT_FILL };
  glowOn = false;
  glowSize = 8;
  glowIntensity = 0.8;
  penWidth = PEN_WIDTHS[1]!;
  /** The pen's tip, how pressure is read, and what the pen's buttons do (see tablet.ts). */
  brush: BrushKind = "pen";
  pressure = new PressureMap();
  /** Whether new texts are markdown, and who to tell when that changes. */
  textMarkdown = true;
  onTextMd?: (on: boolean) => void;
  /** Asked to show the tool box where a pen button was pressed. */
  onToolbox?: (x: number, y: number) => void;
  penMap: Record<"barrel" | "barrel2" | "eraser", PenAction> = { barrel: DEFAULT_TABLET.barrel, barrel2: DEFAULT_TABLET.barrel2, eraser: DEFAULT_TABLET.eraser };
  highlighterWidth = HIGHLIGHTER_WIDTHS[0]!;
  textSize = TEXT_SIZES[1]!;
  eraserRadius = ERASER_SIZES[1]!;
  /** Turn a hand-drawn pen stroke that is clearly a line, ellipse, rectangle or triangle into the clean shape. */
  recognize = false;
  /** Let touch input draw (preference); by default only pen and mouse do. */
  fingerDraw = false;
  shape: ShapeKind = "rect";
  stats: MoveStats | null = null;
  readonly selection = new Selection(this);
  /** Set by the app: reflect a tool change in the toolbar. */
  onToolChange: ((t: Tool) => void) | null = null;
  /** Set by the app: a click with the text tool. */
  onTextClick: ((page: number, x: number, y: number) => void) | null = null;
  /** Set by the app: a click with the LaTeX tool. */
  onLatexClick: ((page: number, x: number, y: number) => void) | null = null;
  /** Set by the app: show a short message to the user. */
  onNotice: ((msg: string) => void) | null = null;
  private undoStack: Action[] = [];
  private redoStack: Action[] = [];
  private g: Gesture | null = null;
  private version = 0;
  /** Counts every change, undo and redo: never goes back (`version` does), so it tells whether anything happened since a given moment. */
  private mutations = 0;
  get changeCount(): number { return this.mutations; }
  private savedVersion = 0;
  private liveCtx: CanvasRenderingContext2D | null = null;

  constructor(readonly host: Host, readonly scroller: HTMLElement) {}

  get dirty(): boolean { return this.version !== this.savedVersion; }
  get canUndo(): boolean { return this.undoStack.length > 0; }
  get canRedo(): boolean { return this.redoStack.length > 0; }
  /** Number of undoable actions (used by tests to tell whether a gesture changed anything). */
  get undoDepth(): number { return this.undoStack.length; }
  markSaved(): void { this.savedVersion = this.version; }
  reset(): void {
    this.undoStack = []; this.redoStack = []; this.g = null; this.version = 0; this.savedVersion = 0;
    this.selection.clear();
  }

  /**
   * The tool to go back to when what was just made is let go (Esc, or a click on the empty page): tools that make an object switch to
   * Select so the object can be adjusted, but that should not leave the user to pick the pen again. Null when Select was chosen on purpose.
   */
  returnTo: Tool | null = null;

  /** Select what was just made, remembering the tool that made it. */
  afterCreate(from: Tool): void {
    // back to what was used before the inserting tool; the pen when that was another inserter, Select, or unknown (never the inserter itself: a click would make another object)
    const before = this.toolBefore;
    const back: Tool = before && !INSERTERS.has(before) && before !== "select" && before !== "lasso" ? before : "pen";
    this.setTool("select");
    this.returnTo = from === "select" || from === "lasso" ? null : back;
  }

  /** The tool that was active before the current one (updated by setTool). */
  private toolBefore: Tool | null = null;

  /** Esc: back to the tool used before making the object, or else clear the selection. */
  escape(): void {
    if (this.returnTo && this.tool === "select") { const t = this.returnTo; this.setTool(t); return; }
    this.selection.clear();
  }

  setTool(t: Tool): void {
    this.returnTo = null;
    if (t !== "select" && t !== "lasso") this.selection.clear();
    if (this.figPending.length && t !== "figure") this.endFigure(true, false); // leaving the tool finishes the figure being clicked
    if (t !== this.tool) this.toolBefore = this.tool;
    this.tool = t;
    this.stopLaser();
    this.clearOverlay(); // drops a leftover eraser cursor
    this.selection.redraw();
    this.onToolChange?.(t);
  }

  attach(container: HTMLElement): void {
    container.addEventListener("pointerdown", (e) => { this.down(e); this.laserMove(e); });
    container.addEventListener("pointermove", (e) => { this.move(e); this.hover(e); this.hoverSelect(e); this.laserMove(e); });
    container.addEventListener("pointerleave", () => { if (this.isEraser() && !this.g) this.clearOverlay(); });
    container.addEventListener("pointerup", (e) => this.up(e));
    container.addEventListener("pointercancel", (e) => this.up(e));
  }

  // ---- history ----------------------------------------------------------------
  undo(): void {
    const a = this.undoStack.pop();
    if (!a) return;
    a.undo();
    this.redoStack.push(a);
    this.version--;
    this.mutations++;
    this.afterHistory(a);
  }
  redo(): void {
    const a = this.redoStack.pop();
    if (!a) return;
    a.redo();
    this.undoStack.push(a);
    this.version++;
    this.mutations++;
    this.afterHistory(a);
  }
  private afterHistory(a: Action): void {
    if (a.structural) this.host.rebuildPages(a.page);
    else if (a.repaintAll) this.host.repaintVisible();
    else this.host.repaint(a.page);
    this.selection.refresh();
    this.host.changed(a.structural || a.repaintAll ? undefined : a.page);
  }
  pushAction(a: Action): void {
    this.undoStack.push(a);
    if (this.undoStack.length > MAX_UNDO) this.undoStack.shift(); // each step keeps copies of what it changed (and of rotated pictures): the oldest are let go
    this.redoStack = [];
    this.version++;
    this.mutations++;
    this.host.changed(a.structural || a.repaintAll ? undefined : a.page);
  }

  // ---- shared overlay ---------------------------------------------------------
  overlay(): { canvas: HTMLCanvasElement; ctx: CanvasRenderingContext2D } {
    const canvas = this.host.liveCanvas();
    this.liveCtx ??= canvas.getContext("2d", { desynchronized: DESYNC })!;
    return { canvas, ctx: this.liveCtx };
  }
  /**
   * The overlay is a low-latency (desynchronized) surface that presents on its own schedule, so clearing it in
   * the same task that paints the committed stroke can show a frame without the stroke (a blink). An opaque
   * stroke looks the same on both surfaces, so keep the overlay for two frames and clear it once the page canvas
   * has certainly been shown. A new gesture clears it itself, so it is never cleared under a stroke in progress.
   */
  private clearOverlayAfterPresent(): void {
    const token = (this.clearToken = (this.clearToken ?? 0) + 1);
    requestAnimationFrame(() => requestAnimationFrame(() => { if (!this.g && token === this.clearToken) this.clearOverlay(); }));
  }
  private clearToken = 0;
  /** The glow a new stroke of this tool would get, if any. */
  glowFor(tool: Tool): Glow | undefined {
    return this.glowOn && (tool === "pen" || tool === "shape" || tool === "figure" || tool === "numline" || tool === "venn") ? { size: this.glowSize, intensity: this.glowIntensity } : undefined;
  }
  /** Add (or with `undefined`, remove) a glow on the selected strokes. One undo step. */
  setSelectionGlow(g: Glow | undefined): number {
    const strokes = this.selection.elements.filter((e): e is Stroke => e.kind === "stroke" && e.tool !== "highlighter");
    if (!strokes.length) return 0;
    const before = strokes.map((s) => (s.glow ? { ...s.glow } : undefined));
    const page = this.selection.pageIndex;
    const apply = (vals: Array<Glow | undefined>) => strokes.forEach((s, i) => setGlow(s, vals[i]));
    apply(strokes.map(() => g));
    this.host.repaint(page);
    this.pushAction({ page, undo: () => apply(before), redo: () => apply(strokes.map(() => g)) });
    return strokes.length;
  }
  /** The line being dragged for a fill: a green arrow with ticks pointing to the side that will be filled. */
  /** Draw a preview at most once per frame (a pen reports far more often than the screen refreshes). */
  private queuePreview(g: Gesture, draw: () => void): void {
    if (g.previewQueued) return;
    g.previewQueued = true;
    requestAnimationFrame(() => { g.previewQueued = false; if (this.g === g) draw(); });
  }

  private drawFillPreview(g: Gesture): void {
    const live = g.live!;
    const ctx = live.ctx, z = this.host.zoom();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, live.canvas.width, live.canvas.height);
    this.setLiveTransform(g);
    let p = g.pts;
    if (this.fill.kind === "riemann" && this.fill.base === "line" && p.length >= 4) { // the base will be a perfectly straight line: show that one
      const end = this.straightEnd([p[0]!, p[1]!], [p[p.length - 2]!, p[p.length - 1]!], !!g.shift);
      p = [p[0]!, p[1]!, end[0], end[1]];
    }
    const n = p.length >> 1;
    ctx.save();
    ctx.strokeStyle = ctx.fillStyle = "#39d353";
    ctx.lineWidth = 2 / z;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    if (n < 2) {
      ctx.beginPath();
      ctx.arc(p[0]!, p[1]!, 4 / z, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
      return;
    }
    ctx.beginPath();
    ctx.moveTo(p[0]!, p[1]!);
    for (let i = 1; i < n; i++) ctx.lineTo(p[2 * i]!, p[2 * i + 1]!);
    ctx.stroke();
    const sgn = this.fill.side === "left" ? 1 : -1;
    let acc = 0;
    const gapPx = 22 / z;
    for (let i = 1; i < n; i++) { // ticks on the side that will be filled
      const dx = p[2 * i]! - p[2 * i - 2]!, dy = p[2 * i + 1]! - p[2 * i - 1]!, len = Math.hypot(dx, dy);
      if (len < 1e-9) continue;
      acc += len;
      if (acc < gapPx) continue;
      acc = 0;
      const nx = (dy / len) * sgn, ny = (-dx / len) * sgn, x = p[2 * i]!, y = p[2 * i + 1]!;
      ctx.beginPath();
      ctx.moveTo(x, y);
      ctx.lineTo(x + (nx * 9) / z, y + (ny * 9) / z);
      ctx.stroke();
    }
    let k = n - 2; // arrow head along the last stretch of any real length
    while (k > 0 && Math.hypot(p[2 * n - 2]! - p[2 * k]!, p[2 * n - 1]! - p[2 * k + 1]!) < 6 / z) k--;
    const ex = p[2 * n - 2]!, ey = p[2 * n - 1]!, ang = Math.atan2(ey - p[2 * k + 1]!, ex - p[2 * k]!);
    ctx.beginPath();
    ctx.moveTo(ex, ey);
    ctx.lineTo(ex - (Math.cos(ang - 0.45) * 11) / z, ey - (Math.sin(ang - 0.45) * 11) / z);
    ctx.lineTo(ex - (Math.cos(ang + 0.45) * 11) / z, ey - (Math.sin(ang + 0.45) * 11) / z);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
  }

  /**
   * The end of a straight base: Shift steps its angle by 15 degrees; with the magnet, a base that is within 3 degrees
   * of horizontal or vertical becomes exactly so (what you see while dragging is what you get).
   */
  private straightEnd(a: [number, number], b: [number, number], shift: boolean): [number, number] {
    const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.hypot(dx, dy);
    if (len < 1e-6) return b;
    let ang = Math.atan2(dy, dx);
    if (shift) { const step = Math.PI / 12; ang = Math.round(ang / step) * step; return [a[0] + Math.cos(ang) * len, a[1] + Math.sin(ang) * len]; }
    if (this.fill.magnet) {
      for (const target of [0, Math.PI / 2, Math.PI, -Math.PI / 2]) {
        let d = Math.abs(ang - target) % (2 * Math.PI);
        if (d > Math.PI) d = 2 * Math.PI - d;
        if (d < (3 * Math.PI) / 180) return [a[0] + Math.cos(target) * len, a[1] + Math.sin(target) * len];
      }
    }
    return b;
  }

  // ---- Cartesian planes ------------------------------------------------------------
  /** The pen's colour, width and glow: the style a plane is drawn with. */
  planeStyleFromPen(): PlaneStyle {
    const glow = this.glowFor("pen");
    return { color: this.color, width: this.penWidth, ...(glow ? { glow } : {}) };
  }

  /**
   * The frame a drag describes. With square cells the plane keeps the proportions of its range and grows from the
   * point where the drag began, toward the pointer, by as much as the longer side of the drag asks; otherwise it is
   * the dragged rectangle. A click makes a default plane around the point.
   */
  private planeFrame(g: Gesture): Frame {
    const [ax, ay, bx, by] = g.pts as [number, number, number, number];
    const z = this.host.zoom();
    const dx = bx - ax, dy = by - ay;
    if (Math.abs(dx) * z < 12 && Math.abs(dy) * z < 12) return squareFrame(this.planeSpec.square ? this.planeSpec : { ...this.planeSpec, xmin: 0, xmax: 1, ymin: 0, ymax: 1 }, { x0: ax - 150, y0: ay - 150, x1: ax + 150, y1: ay + 150 }); // a click: a plane of 300
    if (!this.planeSpec.square) return { x0: Math.min(ax, bx), y0: Math.min(ay, by), x1: Math.max(ax, bx), y1: Math.max(ay, by) };
    const rx = this.planeSpec.xmax - this.planeSpec.xmin, ry = this.planeSpec.ymax - this.planeSpec.ymin;
    const k = Math.max(Math.abs(dx) / rx, Math.abs(dy) / ry);
    const w = rx * k, h = ry * k, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    return { x0: Math.min(ax, ax + sx * w), x1: Math.max(ax, ax + sx * w), y0: Math.min(ay, ay + sy * h), y1: Math.max(ay, ay + sy * h) };
  }

  private drawPlanePreview(g: Gesture): void {
    const live = g.live!;
    const ctx = live.ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, live.canvas.width, live.canvas.height);
    this.setLiveTransform(g);
    const f = this.planeFrame(g);
    const style = { ...this.planeStyleFromPen() };
    delete style.glow; // the preview is plain; the glow shows when the plane is made
    ctx.save();
    ctx.setLineDash([]);
    for (const el of buildPlane("preview", this.planeSpec, f, style)) drawElement(ctx, el, () => undefined);
    ctx.strokeStyle = "#39d353";
    ctx.lineWidth = 1 / this.host.zoom();
    ctx.setLineDash([4 / this.host.zoom(), 3 / this.host.zoom()]);
    ctx.strokeRect(f.x0, f.y0, f.x1 - f.x0, f.y1 - f.y0);
    ctx.restore();
  }

  /** Swap the pieces of a plane for newly built ones, at the same place in the layer. Returns how to undo and redo it. */
  private swapPlane(layer: Layer, id: string, fresh: PageElement[]): { undo(): void; redo(): void; layer: Layer; fresh: PageElement[] } {
    const doc = this.host.doc()!;
    const old = layer.elements.map((el, index) => ({ el, index })).filter((x) => planeIdOf(x.el) === id);
    const at = old.length ? old[0]!.index : layer.elements.length;
    const redo = () => {
      layer.elements = layer.elements.filter((e) => planeIdOf(e) !== id);
      layer.elements.splice(Math.min(at, layer.elements.length), 0, ...fresh);
      doc.elementCount += fresh.length - old.length;
    };
    const undo = () => {
      layer.elements = layer.elements.filter((e) => !fresh.includes(e));
      for (const o of old) layer.elements.splice(Math.min(o.index, layer.elements.length), 0, o.el);
      doc.elementCount += old.length - fresh.length;
    };
    redo();
    return { undo, redo, layer, fresh };
  }

  private finishPlane(g: Gesture): void {
    const doc = this.host.doc();
    if (!doc) return;
    const page = doc.pages[g.page]!;
    const layer = activeLayer(page);
    if (layer.hidden) { this.onNotice?.("La capa activa está oculta: muéstrala para dibujar el plano"); return; }
    const spec = sanitizePlane(this.planeSpec);
    const fresh = buildPlane(newPlaneId(), spec, this.planeFrame(g), this.planeStyleFromPen());
    const act = this.swapPlane(layer, "none", fresh);
    this.host.repaint(g.page);
    this.pushAction({ page: g.page, undo: () => act.undo(), redo: () => act.redo() });
    this.afterCreate(g.tool);
    this.selection.selectMany(g.page, layer, fresh);
  }

  /** The plane that is selected as a whole (and nothing else), with what is needed to rebuild it. */
  selectedPlane(items: Array<{ layer: Layer; el: PageElement }> = this.selection.entries): { id: string; layer: Layer; members: PageElement[]; spec: PlaneSpec; frame: Frame; style: PlaneStyle } | null {
    if (!items.length) return null;
    const id = planeIdOf(items[0]!.el);
    if (!id || items.some((i) => planeIdOf(i.el) !== id || i.layer !== items[0]!.layer)) return null;
    const layer = items[0]!.layer;
    const members = planeMembers(layer, id);
    const spec = planeSpecOf(members), frame = planeFrameOf(members), style = planeStyleOf(members);
    return spec && frame && style ? { id, layer, members, spec, frame, style } : null;
  }

  /** Rebuild a plane after the selection frame resized it: same settings, the new frame. Null when this is not a plane. */
  rebuildSelectedPlane(items: Array<{ layer: Layer; el: PageElement }>): { undo(): void; redo(): void; layer: Layer; fresh: PageElement[] } | null {
    const p = this.selectedPlane(items);
    if (p) return this.swapPlane(p.layer, p.id, buildPlane(p.id, p.spec, p.frame, p.style));
    const q = this.selectedSpace3(items);
    if (q) return this.swapPlane(q.layer, q.id, buildSpace3(q.id, q.spec, q.style));
    const mo = this.selectedMath(items);
    if (mo) return this.swapPlane(mo.layer, mo.id, buildMath(mo.id, mo.spec, mo.style));
    const f = this.selectedFigure(items);
    return f ? this.swapPlane(f.layer, f.id, buildFigure(f.id, f.spec, f.style)) : null;
  }

  /** Change the settings of the selected plane (and, with `restyle`, give it the pen's style). */
  updatePlane(spec: PlaneSpec, restyle = false): boolean {
    const p = this.selectedPlane();
    if (!p) return false;
    const clean = sanitizePlane(spec);
    const frame = clean.square ? squareFrame(clean, p.frame) : p.frame;
    const act = this.swapPlane(p.layer, p.id, buildPlane(p.id, clean, frame, restyle ? this.planeStyleFromPen() : p.style));
    const page = this.selection.pageIndex;
    this.host.repaint(page);
    this.pushAction({ page, undo: () => act.undo(), redo: () => act.redo() });
    this.selection.selectMany(page, p.layer, act.fresh);
    return true;
  }

  // ---- Figures (lines, curves, polygons, solids) -------------------------------------
  /** The figure that is selected as a whole (and nothing else), as it is now. */
  selectedFigure(items: Array<{ layer: Layer; el: PageElement }> = this.selection.entries): { id: string; layer: Layer; members: PageElement[]; spec: FigureSpec; style: PlaneStyle } | null {
    if (!items.length) return null;
    const id = planeIdOf(items[0]!.el);
    if (!id || items.some((i) => planeIdOf(i.el) !== id || i.layer !== items[0]!.layer)) return null;
    const layer = items[0]!.layer;
    const members = planeMembers(layer, id);
    if (!isFigure(members)) return null;
    const read = figureSpecOf(members);
    return read ? { id, layer, members, spec: read.spec, style: read.style } : null;
  }

  /** The spec a gesture describes: a drag for most kinds; the clicks so far plus the pointer for the point-by-point ones. */
  private figureOfGesture(g: Gesture): FigureSpec | null {
    const [ax, ay, bx, by] = g.pts as [number, number, number, number];
    const kind = this.figKind, o = this.figOpts;
    if (clicksKind(kind)) {
      const pts = [...this.figPending, bx, by];
      if (pts.length < 4) return null;
      const k: FigKind = kind === "polygon" && pts.length < 6 ? "polyline" : kind;
      return sanitizeFigure({ ...o, kind: k, pts });
    }
    const tiny = Math.abs(bx - ax) * this.host.zoom() < 12 && Math.abs(by - ay) * this.host.zoom() < 12;
    const solid = kind === "box" || kind === "pyramid" || kind === "cylinder" || kind === "cone" || kind === "sphere";
    const b: [number, number] = tiny ? [ax + (solid ? 0 : 100), ay - (solid ? 90 : 0)] : [bx, by]; // a click makes a figure of the default size
    const base = defaultFigure(kind, [ax, ay], b, !!g.shift);
    return sanitizeFigure({ ...base, ...o, kind, pts: base.pts, heads: kind === "arrow" && o.heads === "none" ? "end" : o.heads });
  }

  private drawFigurePreview(g: Gesture): void {
    const live = g.live!, ctx = live.ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, live.canvas.width, live.canvas.height);
    this.setLiveTransform(g);
    const spec = this.figureOfGesture(g);
    const style = { ...this.planeStyleFromPen() };
    delete style.glow;
    ctx.save();
    ctx.setLineDash([]);
    if (spec) for (const el of buildFigure("preview", spec, style)) drawElement(ctx, el, () => undefined);
    ctx.restore();
    if (clicksKind(this.figKind) && this.figPending.length) this.drawFigureAnchors(ctx, this.host.zoom(), { ...sanitizeFigure({ kind: "polyline", pts: this.figPending }), pts: this.figPending });
  }

  private commitFigure(page: number, spec: FigureSpec, reselect = true): void {
    const doc = this.host.doc();
    if (!doc) return;
    const layer = activeLayer(doc.pages[page]!);
    if (layer.hidden) { this.onNotice?.("La capa activa está oculta: muéstrala para dibujar la figura"); return; }
    const fresh = buildFigure(newPlaneId(), spec, this.planeStyleFromPen());
    const act = this.swapPlane(layer, "none", fresh);
    this.host.repaint(page);
    this.pushAction({ page, undo: () => act.undo(), redo: () => act.redo() });
    if (!reselect) return; // the tool is already being changed by the user
    this.afterCreate("figure");
    this.selection.selectMany(page, layer, fresh);
  }

  private finishFigure(g: Gesture): void {
    if (clicksKind(this.figKind)) { // a click adds a point; a second click on the same place (double click) finishes
      const bx = g.pts[2]!, by = g.pts[3]!;
      const n = this.figPending.length;
      if (n && Math.hypot(this.figPending[n - 2]! - bx, this.figPending[n - 1]! - by) * this.host.zoom() < 8) { this.endFigure(true); return; }
      this.figPending.push(bx, by);
      this.figPage = g.page;
      g.pts = [bx, by, bx, by];
      this.drawFigurePreview(g);
      return;
    }
    this.clearOverlay();
    const spec = this.figureOfGesture(g);
    if (spec) this.commitFigure(g.page, spec);
  }

  /** Finish (with `make`) or drop the figure being made point by point. */
  endFigure(make: boolean, reselect = true): boolean {
    const pts = this.figPending, page = this.figPage;
    this.figPending = [];
    this.figPage = -1;
    this.clearOverlay();
    if (!pts.length) return false;
    if (make) {
      const kind = this.figKind;
      if (pts.length >= (kind === "polygon" ? 6 : 4)) this.commitFigure(page, sanitizeFigure({ ...this.figOpts, kind, pts }), reselect);
      else this.onNotice?.("Faltan puntos: haz clic en al menos " + (kind === "polygon" ? "3" : "2") + " sitios");
    }
    return true;
  }

  /** Remove the last point clicked (Backspace). */
  undoFigurePoint(): boolean {
    if (!this.figPending.length) return false;
    this.figPending.length -= 2;
    if (!this.figPending.length) this.figPage = -1;
    this.clearOverlay();
    return true;
  }

  /** Change the settings of the selected figure (and, with `restyle`, give it the pen's style). */
  updateFigure(spec: FigureSpec, restyle = false): boolean {
    const f = this.selectedFigure();
    if (!f) return false;
    const act = this.swapPlane(f.layer, f.id, buildFigure(f.id, sanitizeFigure(spec), restyle ? this.planeStyleFromPen() : f.style));
    const page = this.selection.pageIndex;
    this.host.repaint(page);
    this.pushAction({ page, undo: () => act.undo(), redo: () => act.redo() });
    this.selection.selectMany(page, f.layer, act.fresh);
    return true;
  }

  /** The anchor under (x, y) of the selected figure, if any. */
  figureAnchorAt(x: number, y: number): { i: number; spec: FigureSpec } | null {
    const f = this.selectedFigure();
    if (!f) return null;
    let best: { i: number; spec: FigureSpec } | null = null, bd = 10 / this.host.zoom();
    for (let i = 0; i < f.spec.pts.length / 2; i++) { const d = Math.hypot(f.spec.pts[2 * i]! - x, f.spec.pts[2 * i + 1]! - y); if (d <= bd) { bd = d; best = { i, spec: f.spec }; } }
    return best;
  }

  /** Anchors of a figure: a circle on each control point (the overlay's transform is the page's). */
  drawFigureAnchors(ctx: CanvasRenderingContext2D, zoom: number, spec: FigureSpec | null = this.selectedFigure()?.spec ?? null): void {
    if (!spec) return;
    const r = 5.5 / zoom;
    ctx.save();
    ctx.setLineDash([]);
    ctx.lineWidth = 1.6 / zoom;
    ctx.strokeStyle = "#2f6fd0";
    ctx.fillStyle = "#fff";
    for (let i = 0; i + 1 < spec.pts.length; i += 2) { ctx.beginPath(); ctx.arc(spec.pts[i]!, spec.pts[i + 1]!, r, 0, Math.PI * 2); ctx.fill(); ctx.stroke(); }
    ctx.restore();
  }

  /** While an anchor is dragged: the figure as it would be, with its anchors. */
  drawFigureDrag(ctx: CanvasRenderingContext2D, zoom: number, spec: FigureSpec): void {
    const style = { ...(this.selectedFigure()?.style ?? this.planeStyleFromPen()) };
    delete style.glow;
    ctx.save();
    ctx.setLineDash([]);
    for (const el of buildFigure("preview", spec, style)) drawElement(ctx, el, () => undefined);
    ctx.restore();
    this.drawFigureAnchors(ctx, zoom, spec);
  }

  /** The spec with anchor `i` moved to `p` (Shift: angles in steps of 15 degrees; lengths kept for solids). */
  moveFigureAnchor(spec: FigureSpec, i: number, p: [number, number], shift: boolean): FigureSpec { return moveFigureAnchor(spec, i, p, shift); }

  // ---- Number line and set diagrams ------------------------------------------------
  /** What the next line / diagram is made with (the panel edits these when nothing is selected), and who to tell when one is made. */
  lineSpec: LineSpec = defaultLine();
  vennSpec: VennSpec = defaultVenn(2);
  onMathCreated?: (kind: "line" | "venn") => void;

  /** The line or diagram that is selected as a whole (and nothing else), as it is now. */
  selectedMath(items: Array<{ layer: Layer; el: PageElement }> = this.selection.entries): { id: string; layer: Layer; members: PageElement[]; spec: MathSpec; style: PlaneStyle } | null {
    if (!items.length) return null;
    const id = planeIdOf(items[0]!.el);
    if (!id || items.some((i) => planeIdOf(i.el) !== id || i.layer !== items[0]!.layer)) return null;
    const layer = items[0]!.layer;
    const members = planeMembers(layer, id);
    if (!isMath(members)) return null;
    const read = readMath(members);
    return read ? { id, layer, members, spec: read.spec, style: read.style } : null;
  }

  /** The line or diagram a drag describes: a click makes one of the default size around the point. */
  private mathOfGesture(g: Gesture): MathSpec {
    const [ax, ay, bx, by] = g.pts as [number, number, number, number];
    const z = this.host.zoom();
    const tiny = Math.abs(bx - ax) * z < 12 && Math.abs(by - ay) * z < 12;
    if (g.tool === "numline") {
      let a: V2 = [ax, ay], b: V2 = [bx, by];
      if (tiny || Math.hypot(bx - ax, by - ay) < 40) { a = [ax - 200, ay]; b = [ax + 200, ay]; }
      else if (g.shift) { const len = Math.hypot(bx - ax, by - ay), ang = Math.round(Math.atan2(by - ay, bx - ax) / (Math.PI / 12)) * (Math.PI / 12); b = [ax + Math.cos(ang) * len, ay + Math.sin(ang) * len]; }
      return { ...this.lineSpec, pts: [a[0], a[1], b[0], b[1]] };
    }
    let a: V2 = [Math.min(ax, bx), Math.min(ay, by)], b: V2 = [Math.max(ax, bx), Math.max(ay, by)];
    if (tiny || b[0] - a[0] < 120 || b[1] - a[1] < 90) { a = [ax - 200, ay - 130]; b = [ax + 200, ay + 130]; }
    const base = defaultVenn(this.vennSpec.k, a, b);
    return { ...base, names: this.vennSpec.names.slice(), expr: this.vennSpec.expr, universe: this.vennSpec.universe, hatch: this.vennSpec.hatch, lists: this.vennSpec.lists.slice() };
  }

  private drawMathPreview(g: Gesture): void {
    const live = g.live!, ctx = live.ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, live.canvas.width, live.canvas.height);
    this.setLiveTransform(g);
    const style = { ...this.planeStyleFromPen() };
    delete style.glow;
    ctx.save();
    ctx.setLineDash([]);
    for (const el of buildMath("preview", this.mathOfGesture(g), style)) drawElement(ctx, el, () => undefined);
    ctx.restore();
  }

  private finishMath(g: Gesture): void {
    const doc = this.host.doc();
    if (!doc) return;
    const layer = activeLayer(doc.pages[g.page]!);
    if (layer.hidden) { this.onNotice?.("La capa activa está oculta: muéstrala para dibujar"); return; }
    const spec = this.mathOfGesture(g);
    const fresh = buildMath(newPlaneId(), spec, this.planeStyleFromPen());
    const act = this.swapPlane(layer, "none", fresh);
    this.host.repaint(g.page);
    this.pushAction({ page: g.page, undo: () => act.undo(), redo: () => act.redo() });
    this.afterCreate(g.tool);
    this.selection.selectMany(g.page, layer, fresh);
    this.onMathCreated?.(spec.kind);
  }

  /** Change the selected line or diagram (and, with `restyle`, give it the pen's style). */
  updateMath(spec: MathSpec, restyle = false): boolean {
    const m = this.selectedMath();
    if (!m) return false;
    const act = this.swapPlane(m.layer, m.id, buildMath(m.id, sanitizeMath(spec), restyle ? this.planeStyleFromPen() : m.style));
    const page = this.selection.pageIndex;
    this.host.repaint(page);
    this.pushAction({ page, undo: () => act.undo(), redo: () => act.redo() });
    this.selection.selectMany(page, m.layer, act.fresh);
    return true;
  }

  /** The anchor under (x, y) of the selected line or diagram, if any. */
  mathAnchorAt(x: number, y: number): { i: number; spec: MathSpec } | null {
    const m = this.selectedMath();
    if (!m) return null;
    let best: { i: number; spec: MathSpec } | null = null, bd = 10 / this.host.zoom();
    mathAnchors(m.spec).forEach((p, i) => { const d = Math.hypot(p[0] - x, p[1] - y); if (d <= bd) { bd = d; best = { i, spec: m.spec }; } });
    return best;
  }

  drawMathAnchors(ctx: CanvasRenderingContext2D, zoom: number, spec: MathSpec | null = this.selectedMath()?.spec ?? null): void {
    if (!spec) return;
    const r = 5.5 / zoom;
    ctx.save();
    ctx.setLineDash([]);
    ctx.lineWidth = 1.6 / zoom;
    ctx.strokeStyle = "#2f6fd0";
    ctx.fillStyle = "#fff";
    for (const p of mathAnchors(spec)) { ctx.beginPath(); ctx.arc(p[0], p[1], r, 0, Math.PI * 2); ctx.fill(); ctx.stroke(); }
    ctx.restore();
  }

  drawMathDrag(ctx: CanvasRenderingContext2D, zoom: number, spec: MathSpec): void {
    const style = { ...(this.selectedMath()?.style ?? this.planeStyleFromPen()) };
    delete style.glow;
    ctx.save();
    ctx.setLineDash([]);
    for (const el of buildMath("preview", spec, style)) drawElement(ctx, el, () => undefined);
    ctx.restore();
    this.drawMathAnchors(ctx, zoom, spec);
  }

  moveMathAnchor(spec: MathSpec, i: number, p: [number, number], shift: boolean): MathSpec { return moveMathAnchor(spec, i, p, shift); }

  // ---- 3D Cartesian spaces ----------------------------------------------------------
  /** The 3D space that is selected as a whole (and nothing else). */
  selectedSpace3(items: Array<{ layer: Layer; el: PageElement }> = this.selection.entries): { id: string; layer: Layer; members: PageElement[]; spec: Space3Spec; style: PlaneStyle } | null {
    if (!items.length) return null;
    const id = planeIdOf(items[0]!.el);
    if (!id || items.some((i) => planeIdOf(i.el) !== id || i.layer !== items[0]!.layer)) return null;
    const layer = items[0]!.layer;
    const members = planeMembers(layer, id);
    if (!isSpace3(members)) return null;
    const spec = space3SpecOf(members), style = space3StyleOf(members);
    return spec && style ? { id, layer, members, spec, style } : null;
  }

  private space3Frame(g: Gesture): Space3Spec {
    const [ax, ay, bx, by] = g.pts as [number, number, number, number];
    const z = this.host.zoom();
    const base = sanitizeSpace3(this.space3Spec);
    const [x0, y0, x1, y1] = extentOf({ ...base, o: [0, 0] });
    const w0 = x1 - x0, h0 = y1 - y0;
    const dx = bx - ax, dy = by - ay;
    if (Math.abs(dx) * z < 12 && Math.abs(dy) * z < 12) return placedSpec(base, 1, [ax - w0 / 2, ay - h0 / 2]); // a click: the default size around the point
    const k = Math.min(8, Math.max(0.2, Math.max(Math.abs(dx) / w0, Math.abs(dy) / h0)));
    return placedSpec(base, k, [dx < 0 ? ax - w0 * k : ax, dy < 0 ? ay - h0 * k : ay]);
  }

  private drawSpace3Preview(g: Gesture): void {
    const live = g.live!;
    const ctx = live.ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, live.canvas.width, live.canvas.height);
    this.setLiveTransform(g);
    const style = { ...this.planeStyleFromPen() };
    delete style.glow;
    ctx.save();
    ctx.setLineDash([]);
    for (const el of buildSpace3("preview", this.space3Frame(g), style)) drawElement(ctx, el, () => undefined);
    ctx.restore();
  }

  private finishSpace3(g: Gesture): void {
    const doc = this.host.doc();
    if (!doc) return;
    const layer = activeLayer(doc.pages[g.page]!);
    if (layer.hidden) { this.onNotice?.("La capa activa está oculta: muéstrala para dibujar el espacio"); return; }
    const fresh = buildSpace3(newPlaneId(), this.space3Frame(g), this.planeStyleFromPen());
    const act = this.swapPlane(layer, "none", fresh);
    this.host.repaint(g.page);
    this.pushAction({ page: g.page, undo: () => act.undo(), redo: () => act.redo() });
    this.afterCreate(g.tool);
    this.selection.selectMany(g.page, layer, fresh);
  }

  /** Change the settings (or the view) of the selected 3D space; with `restyle` it takes the pen's style. */
  updateSpace3(spec: Space3Spec, restyle = false): boolean {
    const q = this.selectedSpace3();
    if (!q) return false;
    const act = this.swapPlane(q.layer, q.id, buildSpace3(q.id, sanitizeSpace3(spec), restyle ? this.planeStyleFromPen() : q.style));
    const page = this.selection.pageIndex;
    this.host.repaint(page);
    this.pushAction({ page, undo: () => act.undo(), redo: () => act.redo() });
    this.selection.selectMany(page, q.layer, act.fresh);
    return true;
  }

  /** The anchor under (x, y) of the selected 3D space, if any: dragging it turns that axis. */
  space3AnchorAt(x: number, y: number): { axis: 0 | 1 | 2; side: AnchorSide; spec: Space3Spec } | null {
    const q = this.selectedSpace3();
    if (!q) return null;
    const tol = 10 / this.host.zoom();
    let best: { axis: 0 | 1 | 2; side: AnchorSide; spec: Space3Spec } | null = null, bd = tol;
    for (const a of anchorsOf(q.spec)) { const d = Math.hypot(a.at[0] - x, a.at[1] - y); if (d <= bd) { bd = d; best = { axis: a.axis, side: a.side, spec: q.spec }; } }
    return best;
  }

  /** Draw the anchors of a 3D space (circles at the end of each axis) on the overlay, whose transform is the page's. */
  drawSpace3Anchors(ctx: CanvasRenderingContext2D, zoom: number, spec: Space3Spec | null = this.selectedSpace3()?.spec ?? null): void {
    if (!spec) return;
    const r = 6 / zoom;
    ctx.save();
    ctx.setLineDash([]);
    ctx.lineWidth = 1.6 / zoom;
    ctx.strokeStyle = "#2f6fd0";
    ctx.fillStyle = "#fff";
    ctx.font = `${11 / zoom}px system-ui, sans-serif`;
    ctx.textBaseline = "middle";
    for (const a of anchorsOf(spec)) {
      ctx.beginPath();
      ctx.arc(a.at[0], a.at[1], r, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
      ctx.fillStyle = "#2f6fd0";
      ctx.fillText((a.side === "-" ? "−" : "") + (spec.names[a.axis] || ""), a.at[0] + r + 3 / zoom, a.at[1] - r);
      ctx.fillStyle = "#fff";
    }
    ctx.restore();
  }

  /** While an anchor is dragged: the space as it would be, with its anchors. */
  drawSpace3Drag(ctx: CanvasRenderingContext2D, zoom: number, spec: Space3Spec): void {
    const q = this.selectedSpace3();
    const style = { ...(q?.style ?? this.planeStyleFromPen()) };
    delete style.glow;
    ctx.save();
    ctx.setLineDash([]);
    for (const el of buildSpace3("preview", spec, style)) drawElement(ctx, el, () => undefined);
    ctx.restore();
    this.drawSpace3Anchors(ctx, zoom, spec);
  }

  /** Last Riemann fill: how many columns were asked for, drawn, and their heights. */
  lastRiemann: { n: number; width: number; drawn: number; skipped: number; rule: string; base: string; heights: number[]; cols: Array<{ a: [number, number]; b: [number, number]; normal: [number, number]; h: number }> } | null = null;
  /** Last fill made, for measurements and tests. */
  lastFill: { ms: number; pixels: number; mode: "point" | "path"; wholePage: boolean; gapUsed: number; moved: boolean } | null = null;

  /** Fill from a click or a dragged line: an image under the strokes of the active layer. One undo step. */
  private runFill(g: Gesture, force: boolean): void {
    const doc = this.host.doc();
    if (!doc) return;
    const page = doc.pages[g.page]!;
    const layer = activeLayer(page);
    if (layer.hidden) { this.onNotice?.("La capa activa está oculta: muéstrala para rellenar"); return; }
    const t0 = performance.now();
    const z = this.host.zoom();
    const drag = pathLength(g.pts) * z >= 8;
    const riemann = this.fill.kind === "riemann";
    if (riemann && !drag) { this.onNotice?.("Rectángulos de Riemann: arrastra una línea base (por ejemplo el eje x) para marcar dónde se apoyan"); return; }
    const allStrokes = layer.elements.filter((e): e is Stroke => e.kind === "stroke");
    const strokes = allStrokes.filter((s) => !isGenerated(s)); // the walls: not the axes and grid of a plane, nor earlier rectangle outlines
    const magnet = this.fill.magnet && !force;
    let path = drag ? simplifyPath(g.pts, 1.2 / Math.max(z, 0.5)) : [];
    if (drag && magnet) path = snapPathEnds(path, allStrokes, 10 / z); // the ends of the line stick to a curve within 10 screen pixels
    if (riemann && this.fill.base === "line") {
      // a perfectly straight base from where the drag began to where it ended: angle steps with Shift, horizontal or vertical when almost so
      const start: [number, number] = [g.pts[0]!, g.pts[1]!];
      const end = this.straightEnd(start, [g.pts[g.pts.length - 2]!, g.pts[g.pts.length - 1]!], force);
      path = [start[0], start[1], end[0], end[1]];
      if (magnet) path = snapPathEnds(path, allStrokes, 10 / z);
    }
    const seed = drag ? ({ kind: "path", pts: path, side: this.fill.side } as const) : ({ kind: "point", x: g.pts[0]!, y: g.pts[1]! } as const);
    const scale = fillScale(page.width, page.height, Math.min(4, Math.max(2, z * (window.devicePixelRatio || 1))));
    const W = Math.ceil(page.width * scale), H = Math.ceil(page.height * scale);
    const out = riemann ? ({ ok: true } as const) : computeFill(strokes, page.width, page.height, scale, seed, this.fill.gap, magnet ? { maxGap: this.fill.maxGap, snapPx: Math.max(3, Math.round((8 / z) * scale)) } : null);
    if (!out.ok) { this.onNotice?.(out.reason); return; }
    const info = "info" in out ? out.info : undefined;
    if (info && (info.moved || info.gapUsed > this.fill.gap)) {
      this.onNotice?.(info.moved ? "Imán: se rellenó la zona cerrada junto al clic" : `Imán: se cerró un hueco de hasta ${info.gapUsed}`);
    }
    let m: FillMask | null = "mask" in out ? out.mask : null;
    let rects: RiemannResult | null = null;
    const a: [number, number] = [path[0]!, path[1]!], b: [number, number] = [path[path.length - 2]!, path[path.length - 1]!];
    if (riemann) {
      const n = riemannColumns(pathLength(path), this.fill.by, this.fill.count, this.fill.width);
      rects = measureRiemann(strokes, page.width, page.height, scale, riemannBases(path, n, this.fill.side), this.fill.rule, this.fill.gap);
      const rm = rectsMask(W, H, scale, rects.cols);
      if (!rm) { this.onNotice?.("No hay curva sobre la línea que detenga los rectángulos: dibuja la curva o cambia de lado"); return; }
      m = rm;
      this.lastRiemann = { n: rects.n, width: rects.width, drawn: rects.cols.length, skipped: rects.skipped, rule: this.fill.rule, base: this.fill.base, heights: rects.cols.map((c) => c.h), cols: rects.cols };
      this.onNotice?.(`${rects.cols.length} rectángulos de ancho ${+rects.width.toFixed(1)}${rects.skipped ? ` (${rects.skipped} sin altura: la curva toca la línea o no hay curva)` : ""}`);
    }
    if (!m) return;
    const cv = fillImage(m, this.color, riemann ? { ...this.fill, kind: "solid" } : this.fill);
    const left = m.x0 / scale, top = m.y0 / scale, right = (m.x0 + m.w) / scale, bottom = (m.y0 + m.h) / scale;
    const img: ImageEl = {
      kind: "image",
      attrs: [["left", String(+left.toFixed(4))], ["top", String(+top.toFixed(4))], ["right", String(+right.toFixed(4))], ["bottom", String(+bottom.toFixed(4))], ["xlFill", "1"]],
      left, top, right, bottom, b64: cv.toDataURL("image/png").split(",")[1]!, bitmap: cv,
    };
    // Under the ink, above earlier fills (a later fill over the same area wins).
    let at = 0;
    for (let i = 0; i < layer.elements.length; i++) { const e = layer.elements[i]!; if (e.kind === "image" && attrValue(e.attrs, "xlFill") !== null) at = i + 1; }
    const added: PageElement[] = [img];
    if (rects && this.fill.outline) {
      const colorAttr = this.color + "ff";
      for (const c of rects.cols) {
        const q = colCorners(c);
        const pts = [...q[0]!, ...q[1]!, ...q[2]!, ...q[3]!, ...q[0]!];
        added.push({ kind: "stroke", attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(this.penWidth)], ["capStyle", "round"], ["xlRiemann", "1"]], tool: "pen", color: cssColor(colorAttr), widths: [this.penWidth], pts, cap: "round", dash: null, bbox: bboxOf(pts) });
      }
    }
    if (drag && this.fill.keepLine && closedOrLong(path)) {
      const colorAttr = this.color + "ff";
      added.push({ kind: "stroke", attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(this.penWidth)], ["capStyle", "round"]], tool: "pen", color: cssColor(colorAttr), widths: [this.penWidth], pts: path.slice(), cap: "round", dash: null, bbox: bboxOf(path) });
    }
    const apply = () => { layer.elements.splice(at, 0, img); layer.elements.push(...added.slice(1)); doc.elementCount += added.length; };
    const unapply = () => { for (const el of added) { const i = layer.elements.lastIndexOf(el); if (i >= 0) layer.elements.splice(i, 1); } doc.elementCount -= added.length; };
    apply();
    this.host.repaint(g.page);
    this.pushAction({ page: g.page, undo: () => unapply(), redo: () => apply() });
    this.lastFill = { ms: performance.now() - t0, pixels: m.count, mode: drag ? "path" : "point", wholePage: m.count > 0.6 * m.total, gapUsed: info?.gapUsed ?? this.fill.gap, moved: info?.moved ?? false };
    if (this.lastFill.wholePage && !drag) this.onNotice?.("La forma tiene una abertura: el relleno salió y cubrió toda la zona. Deshaz y sube «Cerrar huecos»");
  }

  clearOverlay(): void {
    const { canvas, ctx } = this.overlay();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    canvas.style.opacity = "1";
    canvas.style.filter = "";
    this.ring = null;
  }
  /** Map page units of `pageEl` to overlay pixels using the current layout. */
  overlayTransformFor(pageEl: HTMLElement): void {
    const { canvas, ctx } = this.overlay();
    const cr = canvas.getBoundingClientRect();
    const pr = pageEl.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const k = this.host.zoom() * dpr;
    ctx.setTransform(k, 0, 0, k, (pr.left - cr.left) * dpr, (pr.top - cr.top) * dpr);
  }
  /** Resize/move cursors while hovering the selection frame. */
  private hoverSelect(e: PointerEvent): void {
    if ((this.tool !== "select" && this.tool !== "lasso") || this.g || e.pointerType === "touch") return;
    const el = (e.target as HTMLElement).closest<HTMLElement>(".page");
    if (!el) return;
    const z = this.host.zoom();
    const r = el.getBoundingClientRect();
    el.style.cursor = this.selection.cursorAt(Number(el.dataset.i), (e.clientX - r.left) / z, (e.clientY - r.top) / z);
  }

  // ---- laser pointer: a red trail that fades; it never touches the document ----------------
  private laserPts: Array<{ x: number; y: number; t: number }> = [];
  private laserRaf = 0;
  static readonly LASER_TTL_MS = 1400;

  private laserMove(e: PointerEvent): void {
    // Only while pressed: a pen touching the screen or a mouse button held. Hovering shows nothing.
    if (this.tool !== "laser" || e.pointerType === "touch" || e.buttons === 0) return;
    const cr = this.overlay().canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    this.laserPts.push({ x: (e.clientX - cr.left) * dpr, y: (e.clientY - cr.top) * dpr, t: performance.now() });
    if (!this.laserRaf) this.laserRaf = requestAnimationFrame(this.laserFrame);
  }

  private laserFrame = (): void => {
    this.laserRaf = 0;
    const now = performance.now();
    const ttl = Editor.LASER_TTL_MS;
    while (this.laserPts.length && now - this.laserPts[0]!.t > ttl) this.laserPts.shift();
    const { canvas, ctx } = this.overlay();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    canvas.style.opacity = "1";
    const pts = this.laserPts;
    if (!pts.length) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.shadowColor = "rgba(255,40,40,0.9)";
    ctx.shadowBlur = 10 * dpr;
    for (let i = 1; i < pts.length; i++) {
      const a = 1 - (now - pts[i]!.t) / ttl;
      ctx.strokeStyle = `rgba(255,40,40,${Math.max(0, a).toFixed(3)})`;
      ctx.lineWidth = (2 + 4 * a) * dpr;
      ctx.beginPath();
      ctx.moveTo(pts[i - 1]!.x, pts[i - 1]!.y);
      ctx.lineTo(pts[i]!.x, pts[i]!.y);
      ctx.stroke();
    }
    const last = pts[pts.length - 1]!;
    const a = 1 - (now - last.t) / ttl;
    ctx.fillStyle = `rgba(255,40,40,${Math.max(0, a).toFixed(3)})`;
    ctx.beginPath();
    ctx.arc(last.x, last.y, 6 * dpr, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;
    this.laserRaf = requestAnimationFrame(this.laserFrame);
  };

  private stopLaser(): void {
    cancelAnimationFrame(this.laserRaf);
    this.laserRaf = 0;
    this.laserPts = [];
  }

  private isEraser(): boolean { return this.tool === "eraser" || this.tool === "eraserPart"; }

  /** Where the eraser ring is on the overlay now (overlay pixels), so only its box is cleared next time. */
  private ring: { x: number; y: number; r: number } | null = null;
  private ringPos: { clientX: number; clientY: number } | null = null;
  private ringQueued = false;

  /** Eraser cursor: a ring with the real erasing radius, shown also while hovering. Drawn once per frame, at the last position. */
  private hover(e: PointerEvent): void {
    if (!this.isEraser() || e.pointerType === "touch" || (this.g && this.g.tool !== this.tool)) return;
    this.ringPos = { clientX: e.clientX, clientY: e.clientY };
    if (this.ringQueued) return;
    this.ringQueued = true;
    requestAnimationFrame(() => { this.ringQueued = false; this.drawRing(); });
  }

  private drawRing(): void {
    const at = this.ringPos;
    if (!at || !this.isEraser()) return;
    this.clearToken++; // a clearing still pending from the last stroke must not wipe the ring (the whole overlay is cleared below when the ring is new)
    const { canvas, ctx } = this.overlay();
    const cr = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    const old = this.ring;
    if (old) { const pad = 4 * dpr; ctx.clearRect(old.x - old.r - pad, old.y - old.r - pad, 2 * (old.r + pad), 2 * (old.r + pad)); } // only where the ring was
    else ctx.clearRect(0, 0, canvas.width, canvas.height);
    canvas.style.opacity = "1";
    const x = (at.clientX - cr.left) * dpr, y = (at.clientY - cr.top) * dpr;
    const r = this.eraserRadius * this.host.zoom() * dpr;
    this.ring = { x, y, r };
    ctx.setLineDash([]);
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(47,111,208,0.10)";
    ctx.fill();
    ctx.lineWidth = 2.5 * dpr;
    ctx.strokeStyle = "rgba(255,255,255,0.9)";
    ctx.stroke();
    ctx.lineWidth = 1 * dpr;
    ctx.strokeStyle = "rgba(0,0,0,0.8)";
    ctx.stroke();
  }

  /** Redraw anything persistent on the overlay (selection box) after scroll/zoom/resize. */
  refreshOverlay(): void { if (!this.g) this.selection.redraw(); }

  toPage(g: Gesture, e: { clientX: number; clientY: number }): [number, number] {
    const z = this.host.zoom();
    return [
      (e.clientX - g.rect.left - (this.scroller.scrollLeft - g.scroll0[0])) / z,
      (e.clientY - g.rect.top - (this.scroller.scrollTop - g.scroll0[1])) / z,
    ];
  }

  /** Map page units to overlay pixels, following any scroll since pointer-down. */
  setLiveTransform(g: Gesture): void {
    const l = g.live!;
    const dpr = window.devicePixelRatio || 1;
    const k = this.host.zoom() * dpr;
    l.ctx.setTransform(k, 0, 0, k, (l.ox - (this.scroller.scrollLeft - g.scroll0[0])) * dpr, (l.oy - (this.scroller.scrollTop - g.scroll0[1])) * dpr);
  }

  // ---- pointer dispatch -------------------------------------------------------
  private down(e: PointerEvent): void {
    const button = penButtonOf(e); // the pen's side buttons and eraser end arrive as other buttons
    if (this.g || this.pan || this.tool === "laser" || (e.button !== 0 && !button)) return;
    if (this.tool === "hand" && !button && e.pointerType === "touch") return; // a finger scrolls the page by itself
    if (anyFloatingOpen()) return; // a dialog floating over the page (or in a window of its own) is for reading it: the page ignores clicks
    let tool: Tool = this.tool;
    if (button) {
      const act = this.penMap[button];
      if (act === "undo" || act === "redo") { e.preventDefault(); if (act === "undo") this.undo(); else this.redo(); return; }
      if (act === "none") return;
      if (act === "toolbox") { e.preventDefault(); this.onToolbox?.(e.clientX, e.clientY); return; }
      tool = act;
    }
    if (tool === "hand") { // the hand (the tool, or a pen button that works as it): dragging moves the page
      if (anyFloatingOpen()) return;
      e.preventDefault();
      this.pan = { id: e.pointerId, x: e.clientX, y: e.clientY, left: this.scroller.scrollLeft, top: this.scroller.scrollTop, el: e.target as HTMLElement };
      try { this.pan.el.setPointerCapture(e.pointerId); } catch { /* synthetic events have no active pointer */ }
      return;
    }
    if (e.pointerType === "touch" && !this.fingerDraw) return; // fingers scroll / palm rejection; use the hand tool to pan
    if ((e.target as HTMLElement).tagName === "TEXTAREA") return;
    const el = (e.target as HTMLElement).closest<HTMLElement>(".page");
    const doc = this.host.doc();
    if (!el || !doc) return;
    const page = Number(el.dataset.i);
    if (!this.host.staticCanvas(page)) return;
    const g: Gesture = {
      page, el, pointerId: e.pointerId, tool, rect: el.getBoundingClientRect(),
      scroll0: [this.scroller.scrollLeft, this.scroller.scrollTop],
      pts: [], widths: [], pen: e.pointerType === "pen", removed: [], repaintQueued: false,
    };
    e.preventDefault();
    const [x, y] = this.toPage(g, e);
    if (g.tool === "select" && this.returnTo && !button && !this.selection.wouldHit(g, x, y)) { const back = this.returnTo; this.setTool(back); g.tool = back; } // a click on the empty page: the gesture goes on with the tool before
    if (g.tool === "text") { this.onTextClick?.(page, x, y); return; }
    if (g.tool === "latex") { this.onLatexClick?.(page, x, y); return; }
    this.g = g;
    try { el.setPointerCapture(e.pointerId); } catch { /* synthetic events have no active pointer */ }
    if (g.tool === "eraser" || g.tool === "eraserPart") { this.erase(g, x, y); return; }
    if (g.tool === "select" || g.tool === "lasso") { this.selection.down(g, x, y, e.shiftKey); return; }
    if (g.tool === "vspace") { this.selection.startVspace(g, x, y); return; }

    if (g.tool === "plane" || g.tool === "space3" || g.tool === "figure" || g.tool === "numline" || g.tool === "venn") {
      g.pts = [x, y, x, y];
      g.shift = e.shiftKey;
      if (g.tool === "figure" && this.figPending.length && this.figPage !== g.page) this.endFigure(false); // clicks on another page start over
      this.beginLive(g);
      return;
    }
    if (g.tool === "fill") {
      g.pts = [x, y];
      this.beginLive(g);
      this.drawFillPreview(g);
      return;
    }
    if (g.tool === "shape") {
      g.widths = [this.penWidth];
      g.shapeStart = [x, y];
      this.beginLive(g);
      return;
    }
    const base = g.tool === "pen" ? this.penWidth : this.highlighterWidth;
    if (g.tool === "pen") {
      if (this.brush !== "pen" || g.pen) g.brush = new BrushEngine(this.brush, this.penWidth, this.pressure, g.pen); // a plain pen under a mouse keeps one width for the whole stroke
      g.widths = [g.brush ? g.brush.first() : base];
    }
    else g.widths = [base];
    const ctx = this.beginLive(g);
    this.addPoint(g, x, y, e);
    if (!g.pen) { // a mouse or a finger shows its point at once; a pen's start is drawn by its first segment, with the width it really has
      ctx.beginPath();
      ctx.arc(x, y, g.widths[0]! / 2, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  /** Prepare the shared viewport-sized overlay for a stroke in progress; its cost does not grow with zoom. */
  private beginLive(g: Gesture): CanvasRenderingContext2D {
    const { canvas, ctx } = this.overlay();
    this.clearOverlay();
    const cr = canvas.getBoundingClientRect();
    g.live = { canvas, ctx, ox: g.rect.left - cr.left, oy: g.rect.top - cr.top };
    this.setLiveTransform(g);
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.setLineDash([]);
    ctx.strokeStyle = this.color;
    ctx.fillStyle = this.color;
    // Highlighter: draw opaque and fade the whole canvas, so overlapping
    // segments never darken each other.
    canvas.style.filter = this.glowFor(g.tool) ? glowFilter(this.color, this.glowFor(g.tool)!, this.host.zoom()) : "";
    canvas.style.opacity = g.tool === "highlighter" ? String(parseInt(HIGHLIGHTER_ALPHA_HEX, 16) / 255) : "1";
    return ctx;
  }

  private pressureWidth(g: Gesture, e: PointerEvent): number {
    return Math.max(0.05, e.pressure * 1.5) * g.widths[0]!; // approximate, pen only
  }

  private addPoint(g: Gesture, x: number, y: number, e: PointerEvent): boolean {
    const n = g.pts.length;
    if (n >= 2) {
      const dx = x - g.pts[n - 2]!, dy = y - g.pts[n - 1]!;
      if (dx * dx + dy * dy < 0.04) return false; // < 0.2 units: noise
    }
    g.pts.push(x, y);
    if (g.brush) { if (n >= 2) g.widths.push(g.brush.next(x, y, e.pressure, e.timeStamp)); else g.brush.next(x, y, e.pressure, e.timeStamp); } // the first point only starts the clock
    else if (g.pen && n >= 2) g.widths.push(this.pressureWidth(g, e));
    return true;
  }

  /** The page being dragged with the hand, from where the pointer started. */
  private pan: { id: number; x: number; y: number; left: number; top: number; el: HTMLElement } | null = null;

  private move(e: PointerEvent): void {
    const pan = this.pan;
    if (pan) {
      if (e.pointerId === pan.id) { this.scroller.scrollLeft = pan.left - (e.clientX - pan.x); this.scroller.scrollTop = pan.top - (e.clientY - pan.y); }
      return;
    }
    const g = this.g;
    if (!g || e.pointerId !== g.pointerId) return;
    const t0 = this.stats ? performance.now() : 0;
    const coalesced = typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
    const evs = coalesced.length ? coalesced : [e];
    if (g.tool === "eraser" || g.tool === "eraserPart") {
      for (const ev of evs) { const [x, y] = this.toPage(g, ev); this.erase(g, x, y); }
    } else if (g.tool === "plane" || g.tool === "space3" || g.tool === "figure" || g.tool === "numline" || g.tool === "venn") {
      const [x, y] = this.toPage(g, evs[evs.length - 1]!);
      g.pts[2] = x;
      g.pts[3] = y;
      g.shift = e.shiftKey;
      this.queuePreview(g, () => { if (g.tool === "plane") this.drawPlanePreview(g); else if (g.tool === "space3") this.drawSpace3Preview(g); else if (g.tool === "numline" || g.tool === "venn") this.drawMathPreview(g); else this.drawFigurePreview(g); });
    } else if (g.tool === "fill") {
      const min = 1.5 / this.host.zoom();
      g.shift = e.shiftKey;
      for (const ev of evs) {
        const [x, y] = this.toPage(g, ev);
        const n = g.pts.length;
        if (Math.hypot(x - g.pts[n - 2]!, y - g.pts[n - 1]!) >= min) g.pts.push(x, y);
      }
      this.queuePreview(g, () => this.drawFillPreview(g));
    } else if (g.tool === "shape") {
      const live = g.live!;
      const [x, y] = this.toPage(g, evs[evs.length - 1]!);
      const [x0, y0] = g.shapeStart!;
      g.pts = shapePoints(this.shape, x0, y0, x, y, e.shiftKey, this.penWidth);
      live.ctx.setTransform(1, 0, 0, 1, 0, 0);
      live.ctx.clearRect(0, 0, live.canvas.width, live.canvas.height);
      this.setLiveTransform(g);
      live.ctx.lineWidth = this.penWidth;
      live.ctx.beginPath();
      live.ctx.moveTo(g.pts[0]!, g.pts[1]!);
      for (let i = 2; i + 1 < g.pts.length; i += 2) live.ctx.lineTo(g.pts[i]!, g.pts[i + 1]!);
      live.ctx.stroke();
    } else if (g.tool === "select" || g.tool === "lasso" || g.tool === "vspace") {
      const [x, y] = this.toPage(g, evs[evs.length - 1]!);
      this.selection.move(g, x, y, e.shiftKey);
    } else {
      const live = g.live!;
      const ctx = live.ctx;
      this.setLiveTransform(g);
      for (const ev of evs) {
        const [x, y] = this.toPage(g, ev);
        const n = g.pts.length;
        const px = g.pts[n - 2]!, py = g.pts[n - 1]!;
        if (!this.addPoint(g, x, y, ev)) continue;
        ctx.lineWidth = g.widths[g.pen || g.brush ? g.widths.length - 1 : 0]!;
        ctx.beginPath();
        ctx.moveTo(px, py);
        ctx.lineTo(x, y);
        ctx.stroke();
      }
    }
    this.stats?.moveMs.push(performance.now() - t0);
  }

  private up(e: PointerEvent): void {
    const pan = this.pan;
    if (pan) {
      if (e.pointerId === pan.id) { this.pan = null; try { pan.el.releasePointerCapture(pan.id); } catch { /* not captured */ } }
      return;
    }
    const g = this.g;
    if (!g || e.pointerId !== g.pointerId) return;
    this.g = null;
    try { g.el.releasePointerCapture(g.pointerId); } catch { /* not captured */ }
    const doc = this.host.doc();
    if (g.tool === "eraser" || g.tool === "eraserPart") {
      if (g.removed.length && doc) this.pushErase(g);
      return;
    }
    if (g.tool === "select" || g.tool === "lasso" || g.tool === "vspace") { const [x, y] = this.toPage(g, e); this.selection.up(g, x, y); return; }
    if (g.tool === "fill") { this.clearOverlay(); this.runFill(g, e.shiftKey); return; }
    if (g.tool === "plane" || g.tool === "space3" || g.tool === "numline" || g.tool === "venn") {
      const [x, y] = this.toPage(g, e);
      if (g.tool === "numline" || g.tool === "venn") { g.pts[2] = x; g.pts[3] = y; g.shift = e.shiftKey; }
      this.clearOverlay();
      // a click only is not an insertion (it would drop objects by accident): it takes a drag to say where and how big
      const z = this.host.zoom();
      if (Math.abs(x - g.pts[0]!) * z < 12 && Math.abs(y - g.pts[1]!) * z < 12) { this.onNotice?.("Arrastra para dibujarlo (un clic solo no inserta nada)"); return; }
      if (g.tool === "plane") this.finishPlane(g); else if (g.tool === "space3") this.finishSpace3(g); else this.finishMath(g);
      return;
    }
    if (g.tool === "figure") { const [x, y] = this.toPage(g, e); g.pts[2] = x; g.pts[3] = y; g.shift = e.shiftKey; this.finishFigure(g); return; }
    if (g.tool === "shape" && g.shapeStart) {
      const [x, y] = this.toPage(g, e);
      g.pts = shapePoints(this.shape, g.shapeStart[0], g.shapeStart[1], x, y, e.shiftKey, this.penWidth);
      const b = bboxOf(g.pts);
      if (Math.max(b[2] - b[0], b[3] - b[1]) < 1) g.pts = []; // a click, not a drag
    }
    if (!doc || g.pts.length < 2) { this.clearOverlay(); return; }
    if (this.recognize && g.tool === "pen") {
      const shape = recognizeShape(g.pts);
      if (shape) { g.pts = shape; g.widths = [g.widths[0]!]; this.onNotice?.("Forma reconocida"); }
    }
    const t0 = this.stats ? performance.now() : 0;
    const page = doc.pages[g.page]!;
    const layer = activeLayer(page);
    if (layer.hidden) { layer.hidden = false; this.onNotice?.("La capa activa estaba oculta: se ha vuelto a mostrar"); }
    if (g.brush && g.widths.length > 1 && g.widths.length === g.pts.length / 2) g.widths = g.brush.finish(g.pts, g.widths); // the tip runs out
    const toolName = g.tool === "highlighter" ? "highlighter" : "pen"; // shapes are stored as ordinary pen strokes
    const alpha = g.tool === "highlighter" ? HIGHLIGHTER_ALPHA_HEX : "ff";
    const colorAttr = this.color + alpha;
    const s: Stroke = {
      kind: "stroke",
      attrs: [["tool", toolName], ["color", colorAttr], ["width", g.widths.join(" ")], ["capStyle", "round"]],
      tool: toolName,
      color: cssColor(colorAttr),
      widths: g.widths,
      pts: g.pts,
      cap: "round",
      dash: null,
      bbox: bboxOf(g.pts),
    };
    const glow = this.glowFor(g.tool);
    if (glow) setGlow(s, glow);
    layer.elements.push(s);
    doc.elementCount++;
    this.host.commitPaint(g.page, s);
    if (g.tool === "highlighter" || s.glow) this.clearOverlay(); // translucent or glowing: showing both would brighten it
    else this.clearOverlayAfterPresent();
    this.stats?.commitMs.push(performance.now() - t0);
    this.pushAction({
      page: g.page,
      undo: () => { const i = layer.elements.lastIndexOf(s); if (i >= 0) layer.elements.splice(i, 1); doc.elementCount--; },
      redo: () => { layer.elements.push(s); doc.elementCount++; },
    });
  }

  // ---- erasers ----------------------------------------------------------------
  private erase(g: Gesture, x: number, y: number): void {
    const doc = this.host.doc();
    if (!doc) return;
    const partial = g.tool === "eraserPart";
    const page = doc.pages[g.page]!;
    const r = this.eraserRadius;
    let hit = false;
    for (const layer of page.layers) {
      if (layer.hidden) continue;
      for (let i = layer.elements.length - 1; i >= 0; i--) {
        const el = layer.elements[i]!;
        if (el.kind !== "stroke") continue;
        const pad = r + el.widths[0]! * 0.75;
        const [x0, y0, x1, y1] = el.bbox;
        if (x < x0 - pad || x > x1 + pad || y < y0 - pad || y > y1 + pad) continue;
        let added: Stroke[] = [];
        if (partial) {
          const pieces = splitStroke(el, x, y, r);
          if (!pieces) continue;
          added = pieces;
        } else if (!strokeHit(el, x, y, r + el.widths[0]! / 2)) continue;
        layer.elements.splice(i, 1, ...added);
        doc.elementCount += added.length - 1;
        g.removed.push({ layer, index: i, removed: el, added });
        hit = true;
        const gp = 4 + (el.glow ? el.glow.size * 2 : 0) + el.widths[0]!; // what the stroke could have painted around its box
        const d = g.dirty;
        g.dirty = d ? [Math.min(d[0], x0 - gp), Math.min(d[1], y0 - gp), Math.max(d[2], x1 + gp), Math.max(d[3], y1 + gp)] : [x0 - gp, y0 - gp, x1 + gp, y1 + gp];
      }
    }
    if (hit && !g.repaintQueued) {
      g.repaintQueued = true;
      requestAnimationFrame(() => {
        g.repaintQueued = false;
        const t0 = this.stats ? performance.now() : 0;
        const rect = g.dirty;
        g.dirty = undefined;
        if (rect) this.host.repaintRect(g.page, rect); else this.host.repaint(g.page); // the page is repainted whole when the stroke ends
        this.stats?.eraseRepaintMs.push(performance.now() - t0);
      });
    }
  }

  private pushErase(g: Gesture): void {
    const doc = this.host.doc()!;
    const entries = g.removed;
    this.host.repaint(g.page);
    this.pushAction({
      page: g.page,
      undo: () => {
        for (let k = entries.length - 1; k >= 0; k--) {
          const e = entries[k]!;
          if (e.added.length) {
            const i = e.layer.elements.indexOf(e.added[0]!);
            if (i >= 0) e.layer.elements.splice(i, e.added.length, e.removed);
          } else e.layer.elements.splice(e.index, 0, e.removed);
          doc.elementCount += 1 - e.added.length;
        }
      },
      redo: () => {
        for (const e of entries) {
          const i = e.layer.elements.indexOf(e.removed);
          if (i >= 0) { e.layer.elements.splice(i, 1, ...e.added); doc.elementCount += e.added.length - 1; }
        }
      },
    });
  }
}

const closedOrLong = (p: number[]) => p.length >= 4 && (isClosed(p) || pathLength(p) > 2);
