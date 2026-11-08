import { isMarkdown, markdownSize } from "./markdown";
import { bboxOf, setTexPose, texPose, type ImageEl, type PageElement, type Stroke, type TexImageEl, type TexPose, type TextBox } from "./model";

export type Rect = [x0: number, y0: number, x1: number, y1: number];

const LINE_HEIGHT = 1.2; // keep in sync with render.ts drawText

function segDist2(x: number, y: number, ax: number, ay: number, bx: number, by: number): number {
  const dx = bx - ax, dy = by - ay;
  const len2 = dx * dx + dy * dy;
  let t = len2 ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const cx = ax + t * dx - x, cy = ay + t * dy - y;
  return cx * cx + cy * cy;
}

/** True when (x, y) is within `pad` of any segment (or the dot) of the stroke. */
export function strokeHit(s: Stroke, x: number, y: number, pad: number): boolean {
  const p = s.pts;
  const n = p.length >> 1;
  if (n === 1) return Math.hypot(x - p[0]!, y - p[1]!) <= pad;
  const pad2 = pad * pad;
  for (let i = 1; i < n; i++) {
    if (segDist2(x, y, p[2 * i - 2]!, p[2 * i - 1]!, p[2 * i]!, p[2 * i + 1]!) <= pad2) return true;
  }
  return false;
}

/**
 * Cut the parts of a stroke within `r` of (x, y). Returns null when nothing is
 * hit, otherwise the surviving pieces (possibly none). Per-segment widths follow
 * their segments: widths[k] (k >= 1) belongs to the segment points k-1 -> k.
 */
export function splitStroke(s: Stroke, x: number, y: number, r: number): Stroke[] | null {
  const p = s.pts;
  const n = p.length >> 1;
  const pad = r + s.widths[0]! / 2;
  if (n === 1) return Math.hypot(x - p[0]!, y - p[1]!) <= pad ? [] : null;
  const pad2 = pad * pad;
  const hit = new Array<boolean>(n - 1);
  let any = false;
  for (let i = 0; i < n - 1; i++) {
    hit[i] = segDist2(x, y, p[2 * i]!, p[2 * i + 1]!, p[2 * i + 2]!, p[2 * i + 3]!) <= pad2;
    any ||= hit[i]!;
  }
  if (!any) return null;
  const variable = s.widths.length > 1;
  const pieces: Stroke[] = [];
  let i = 0;
  while (i < n - 1) {
    if (hit[i]) { i++; continue; }
    let j = i;
    while (j < n - 1 && !hit[j]) j++; // kept segments i..j-1, points i..j
    const pts = p.slice(2 * i, 2 * j + 2);
    const widths = [s.widths[0]!];
    if (variable) for (let k = i + 1; k <= j; k++) widths.push(s.widths[k] ?? s.widths[0]!);
    pieces.push({ ...s, pts, widths, bbox: bboxOf(pts) });
    i = j;
  }
  return pieces;
}

let measureCtx: CanvasRenderingContext2D | null = null;

export function textBBox(t: TextBox): Rect {
  if (isMarkdown(t)) { const [w, h] = markdownSize(t); return [t.x, t.y, t.x + w, t.y + h]; }
  measureCtx ??= document.createElement("canvas").getContext("2d")!;
  const family = t.font === "Sans" ? "system-ui, sans-serif" : `"${t.font}", sans-serif`;
  measureCtx.font = `${t.size}px ${family}`;
  const lines = t.text.split("\n");
  let w = 0;
  for (const l of lines) w = Math.max(w, measureCtx.measureText(l).width);
  return [t.x, t.y, t.x + Math.max(w, t.size), t.y + lines.length * t.size * LINE_HEIGHT];
}

/** Bounding box in page units, or null for elements this editor cannot place. */
/** The largest number of a list (a loop: spreading a long array into Math.max is slow and can overflow the stack). */
export function maxOf(a: number[]): number {
  let m = -Infinity;
  for (let i = 0; i < a.length; i++) if (a[i]! > m) m = a[i]!;
  return m;
}

/** Bounding box of a formula as it is drawn: its upright box turned around its centre. */
function texBounds(el: TexImageEl): Rect {
  const { rot } = texPose(el);
  if (rot === 0) return [el.left, el.top, el.right, el.bottom];
  const cx = (el.left + el.right) / 2, cy = (el.top + el.bottom) / 2, hw = (el.right - el.left) / 2, hh = (el.bottom - el.top) / 2;
  const c = Math.abs(Math.cos(rot)), s = Math.abs(Math.sin(rot));
  const w = hw * c + hh * s, h = hw * s + hh * c;
  return [cx - w, cy - h, cx + w, cy + h];
}

/** Whether a point is on a formula, in the way it is turned. */
export function texContains(el: TexImageEl, x: number, y: number): boolean {
  const { rot } = texPose(el);
  const cx = (el.left + el.right) / 2, cy = (el.top + el.bottom) / 2;
  const dx = x - cx, dy = y - cy, c = Math.cos(-rot), s = Math.sin(-rot);
  const lx = dx * c - dy * s, ly = dx * s + dy * c;
  return Math.abs(lx) <= (el.right - el.left) / 2 && Math.abs(ly) <= (el.bottom - el.top) / 2;
}

export function elementBBox(el: PageElement): Rect | null {
  switch (el.kind) {
    case "stroke": {
      const h = maxOf(el.widths) / 2;
      return [el.bbox[0] - h, el.bbox[1] - h, el.bbox[2] + h, el.bbox[3] + h];
    }
    case "text": return textBBox(el);
    case "image": return [el.left, el.top, el.right, el.bottom];
    case "teximage": return texBounds(el);
    case "unsupported": return null;
  }
}

export function unionRect(a: Rect | null, b: Rect): Rect {
  return a ? [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[2], b[2]), Math.max(a[3], b[3])] : b;
}

export function rectsIntersect(a: Rect, b: Rect): boolean {
  return a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1];
}

export function pointInRect(x: number, y: number, r: Rect): boolean {
  return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

/** Rectangle selection: strokes by vertex, text and images by box overlap. */
export function inRect(el: PageElement, r: Rect): boolean {
  const b = elementBBox(el);
  if (!b || !rectsIntersect(b, r)) return false;
  if (el.kind !== "stroke") return true;
  const p = el.pts;
  for (let i = 0; i + 1 < p.length; i += 2) if (pointInRect(p[i]!, p[i + 1]!, r)) return true;
  return false;
}

/** Point selection: nearest-stroke hit, or containing text/image box. */
export function hitElement(el: PageElement, x: number, y: number, pad: number): boolean {
  if (el.kind === "stroke") return strokeHit(el, x, y, pad + el.widths[0]! / 2);
  const b = elementBBox(el);
  return !!b && pointInRect(x, y, [b[0] - pad, b[1] - pad, b[2] + pad, b[3] + pad]);
}

/** Even-odd point-in-polygon; `poly` is x0,y0,x1,y1,... */
export function pointInPolygon(x: number, y: number, poly: number[]): boolean {
  let inside = false;
  const n = poly.length >> 1;
  for (let i = 0, j = n - 1; i < n; j = i++) {
    const xi = poly[2 * i]!, yi = poly[2 * i + 1]!, xj = poly[2 * j]!, yj = poly[2 * j + 1]!;
    if (yi > y !== yj > y && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

/** Lasso selection: strokes by vertex, text and images by box centre. */
export function inPolygon(el: PageElement, poly: number[]): boolean {
  if (el.kind === "stroke") {
    const p = el.pts;
    for (let i = 0; i + 1 < p.length; i += 2) if (pointInPolygon(p[i]!, p[i + 1]!, poly)) return true;
    return false;
  }
  const b = elementBBox(el);
  return !!b && pointInPolygon((b[0] + b[2]) / 2, (b[1] + b[3]) / 2, poly);
}

/** Affine map x' = a*x + c*y + e, y' = b*x + d*y + f (page units). */
export interface Aff { a: number; b: number; c: number; d: number; e: number; f: number }

export const scaleAff = (sx: number, sy: number, ax: number, ay: number): Aff =>
  ({ a: sx, b: 0, c: 0, d: sy, e: ax * (1 - sx), f: ay * (1 - sy) });
export const translateAff = (dx: number, dy: number): Aff => ({ a: 1, b: 0, c: 0, d: 1, e: dx, f: dy });
export function rotateAff(theta: number, cx: number, cy: number): Aff {
  const co = Math.cos(theta), si = Math.sin(theta);
  return { a: co, b: si, c: -si, d: co, e: cx - co * cx + si * cy, f: cy - si * cx - co * cy };
}
export const flipAff = (horizontal: boolean, cx: number, cy: number): Aff =>
  horizontal ? { a: -1, b: 0, c: 0, d: 1, e: 2 * cx, f: 0 } : { a: 1, b: 0, c: 0, d: -1, e: 0, f: 2 * cy };
export const applyAff = (m: Aff, x: number, y: number): [number, number] => [m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f];

/** Axis-aligned bounds of a rectangle after the map. */
export function transformRect(r: Rect, m: Aff): Rect {
  const c = [applyAff(m, r[0], r[1]), applyAff(m, r[2], r[1]), applyAff(m, r[2], r[3]), applyAff(m, r[0], r[3])];
  return [Math.min(...c.map((p) => p[0])), Math.min(...c.map((p) => p[1])), Math.max(...c.map((p) => p[0])), Math.max(...c.map((p) => p[1]))];
}

/** Copy of the state an element can be restored to (used for undo/redo of transforms). */
export interface Snap {
  el: PageElement;
  pts?: number[];
  widths?: number[];
  bbox?: Rect;
  x?: number; y?: number; size?: number;
  box?: Rect;
  b64?: string;
  bitmap?: ImageEl["bitmap"];
  latex?: string;
  pose?: TexPose;
}

export function snapshot(el: PageElement): Snap {
  switch (el.kind) {
    case "stroke": return { el, pts: el.pts.slice(), widths: el.widths.slice(), bbox: [...el.bbox] };
    case "text": return { el, x: el.x, y: el.y, size: el.size };
    case "image": return { el, box: [el.left, el.top, el.right, el.bottom], b64: el.b64, bitmap: el.bitmap };
    case "teximage": return { el, box: [el.left, el.top, el.right, el.bottom], b64: el.pdfB64, bitmap: el.bitmap, latex: el.latex, pose: texPose(el) };
    case "unsupported": return { el };
  }
}

export function restore(s: Snap): void {
  const el = s.el;
  if (el.kind === "stroke") { el.pts = s.pts!.slice(); el.widths = s.widths!.slice(); el.bbox = [...s.bbox!]; }
  else if (el.kind === "text") { el.x = s.x!; el.y = s.y!; el.size = s.size!; }
  else if (el.kind === "image") { [el.left, el.top, el.right, el.bottom] = s.box!; el.b64 = s.b64!; el.bitmap = s.bitmap; }
  else if (el.kind === "teximage") { [el.left, el.top, el.right, el.bottom] = s.box!; el.pdfB64 = s.b64!; el.bitmap = s.bitmap as HTMLCanvasElement | undefined; el.latex = s.latex!; setTexPose(el, s.pose ?? { rot: 0, flip: false }); }
}

const MAX_BAKED_SIDE = 2000;

/** Re-encode the image under `m` into a new PNG (the format has no rotation attribute). */
function bakeImage(el: ImageEl, m: Aff, nb: Rect): void {
  const src = el.bitmap;
  if (!src) return; // not decoded yet: keep the pixels, only the box moves
  const w0 = el.right - el.left, h0 = el.bottom - el.top;
  let ppu = src.width / Math.max(w0, 1e-6); // source pixels per page unit
  const nw = nb[2] - nb[0], nh = nb[3] - nb[1];
  ppu = Math.min(ppu, MAX_BAKED_SIDE / Math.max(nw, nh, 1e-6));
  const cv = document.createElement("canvas");
  cv.width = Math.max(1, Math.round(nw * ppu));
  cv.height = Math.max(1, Math.round(nh * ppu));
  const ctx = cv.getContext("2d")!;
  ctx.imageSmoothingQuality = "high";
  ctx.setTransform(ppu, 0, 0, ppu, -nb[0] * ppu, -nb[1] * ppu);
  ctx.transform(m.a, m.b, m.c, m.d, m.e, m.f);
  ctx.drawImage(src, el.left, el.top, w0, h0);
  el.b64 = cv.toDataURL("image/png").split(",")[1] ?? "";
  el.bitmap = cv;
}

/**
 * Apply the map to an element in place. Line widths and font sizes follow
 * sqrt(|det|), so rotations and flips keep them and uniform scales scale them.
 * Text stays upright (the format cannot rotate it): its box centre is mapped.
 */
export function transformElement(el: PageElement, m: Aff): void {
  const k = Math.sqrt(Math.abs(m.a * m.d - m.b * m.c));
  if (el.kind === "stroke") {
    const p = el.pts;
    const q = new Array<number>(p.length);
    for (let i = 0; i + 1 < p.length; i += 2) { const [x, y] = applyAff(m, p[i]!, p[i + 1]!); q[i] = x; q[i + 1] = y; }
    el.pts = q;
    el.widths = el.widths.map((w) => w * k);
    el.bbox = bboxOf(q);
  } else if (el.kind === "text") {
    const b = textBBox(el);
    const [cx, cy] = applyAff(m, (b[0] + b[2]) / 2, (b[1] + b[3]) / 2);
    el.size *= k;
    el.x = cx - ((b[2] - b[0]) * k) / 2;
    el.y = cy - ((b[3] - b[1]) * k) / 2;
  } else if (el.kind === "teximage") {
    // A formula is an upright PDF in a box; its turn and mirror are kept beside it (see TexPose). The box keeps its shape and size
    // (scaled), moves with the centre, and the pose absorbs the rotation and the mirror of the transform.
    const box: Rect = [el.left, el.top, el.right, el.bottom];
    const pose = texPose(el);
    const plainScale = m.b === 0 && m.c === 0 && m.a > 0 && m.d > 0;
    if (plainScale && pose.rot === 0 && !pose.flip) {
      [el.left, el.top, el.right, el.bottom] = transformRect(box, m);
    } else {
      const [cx, cy] = applyAff(m, (box[0] + box[2]) / 2, (box[1] + box[3]) / 2);
      const det = m.a * m.d - m.b * m.c;
      const k = Math.sqrt(Math.abs(det)) || 1;
      const w = (box[2] - box[0]) * k, h = (box[3] - box[1]) * k;
      [el.left, el.top, el.right, el.bottom] = [cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2];
      // linear part of (transform after the formula's own pose) = turn(phi) * mirror * k
      const c0 = Math.cos(pose.rot), s0 = Math.sin(pose.rot), fx = pose.flip ? -1 : 1;
      const n = { a: (m.a * c0 + m.c * s0) * fx, b: (m.b * c0 + m.d * s0) * fx, c: -m.a * s0 + m.c * c0, d: -m.b * s0 + m.d * c0 };
      const mirrored = n.a * n.d - n.b * n.c < 0;
      setTexPose(el, { rot: Math.atan2(-n.c, n.d), flip: mirrored });
    }
  } else if (el.kind === "image") {
    const nb = transformRect([el.left, el.top, el.right, el.bottom], m);
    const plainScale = m.b === 0 && m.c === 0 && m.a > 0 && m.d > 0;
    if (!plainScale) bakeImage(el, m, nb);
    [el.left, el.top, el.right, el.bottom] = nb;
  }
}

/** Independent copy of an element (for paste/duplicate); null for kinds this editor cannot place. */
export function cloneElement(el: PageElement): PageElement | null {
  const attrs = (a: Array<[string, string]>): Array<[string, string]> => a.map(([k, v]) => [k, v]);
  switch (el.kind) {
    case "stroke": return { ...el, attrs: attrs(el.attrs), pts: el.pts.slice(), widths: el.widths.slice(), bbox: [...el.bbox] };
    case "text": return { ...el, attrs: attrs(el.attrs) };
    case "image": return { ...el, attrs: attrs(el.attrs) };
    case "teximage": return { ...el, attrs: attrs(el.attrs) };
    case "unsupported": return null;
  }
}

export type ShapeKind = "line" | "rect" | "ellipse" | "arrow";
const SNAP15 = Math.PI / 12;

/**
 * Polyline for a shape dragged from (x0, y0) to (x1, y1). With `constrain`
 * lines and arrows snap to 15-degree steps, rectangles and ellipses become
 * squares and circles.
 */
export function shapePoints(kind: ShapeKind, x0: number, y0: number, x1: number, y1: number, constrain: boolean, width: number): number[] {
  let dx = x1 - x0, dy = y1 - y0;
  if (kind === "line" || kind === "arrow") {
    if (constrain) {
      const len = Math.hypot(dx, dy);
      const a = Math.round(Math.atan2(dy, dx) / SNAP15) * SNAP15;
      dx = Math.cos(a) * len; dy = Math.sin(a) * len;
    }
    const ex = x0 + dx, ey = y0 + dy;
    if (kind === "line") return [x0, y0, ex, ey];
    const len = Math.hypot(dx, dy);
    const head = Math.min(Math.max(10, width * 6), len * 0.4);
    const ang = Math.atan2(dy, dx);
    const b1 = ang + Math.PI - 0.45, b2 = ang + Math.PI + 0.45;
    return [x0, y0, ex, ey, ex + Math.cos(b1) * head, ey + Math.sin(b1) * head, ex, ey, ex + Math.cos(b2) * head, ey + Math.sin(b2) * head];
  }
  if (constrain) {
    const m = Math.max(Math.abs(dx), Math.abs(dy));
    dx = Math.sign(dx || 1) * m; dy = Math.sign(dy || 1) * m;
  }
  const ex = x0 + dx, ey = y0 + dy;
  if (kind === "rect") return [x0, y0, ex, y0, ex, ey, x0, ey, x0, y0];
  const cx = x0 + dx / 2, cy = y0 + dy / 2, rx = Math.abs(dx) / 2, ry = Math.abs(dy) / 2;
  const pts: number[] = [];
  const N = 72;
  for (let i = 0; i <= N; i++) { const t = (i / N) * Math.PI * 2; pts.push(cx + Math.cos(t) * rx, cy + Math.sin(t) * ry); }
  return pts;
}

/** Douglas-Peucker simplification of an open polyline (x,y,x,y,...). */
function simplify(p: number[], eps: number): number[] {
  const n = p.length >> 1;
  if (n < 3) return p.slice();
  const keep = new Uint8Array(n);
  keep[0] = 1;
  keep[n - 1] = 1;
  const stack: Array<[number, number]> = [[0, n - 1]];
  while (stack.length) {
    const [a, b] = stack.pop()!;
    let maxD = 0, idx = -1;
    for (let i = a + 1; i < b; i++) {
      const d = Math.sqrt(segDist2(p[2 * i]!, p[2 * i + 1]!, p[2 * a]!, p[2 * a + 1]!, p[2 * b]!, p[2 * b + 1]!));
      if (d > maxD) { maxD = d; idx = i; }
    }
    if (maxD > eps && idx >= 0) { keep[idx] = 1; stack.push([a, idx], [idx, b]); }
  }
  const out: number[] = [];
  for (let i = 0; i < n; i++) if (keep[i]) out.push(p[2 * i]!, p[2 * i + 1]!);
  return out;
}

const cornerCos = (ax: number, ay: number, bx: number, by: number, cx: number, cy: number) => {
  const ux = ax - bx, uy = ay - by, vx = cx - bx, vy = cy - by;
  return (ux * vx + uy * vy) / ((Math.hypot(ux, uy) * Math.hypot(vx, vy)) || 1);
};

/**
 * Recognise a hand-drawn line, ellipse, rectangle or triangle and return its clean polyline,
 * or null when the stroke is not clearly one of them (then it is kept as drawn).
 */
export function recognizeShape(pts: number[]): number[] | null {
  const n = pts.length >> 1;
  if (n < 8) return null;
  let len = 0;
  for (let i = 1; i < n; i++) len += Math.hypot(pts[2 * i]! - pts[2 * i - 2]!, pts[2 * i + 1]! - pts[2 * i - 1]!);
  const [x0, y0, x1, y1] = bboxOf(pts);
  const w = x1 - x0, h = y1 - y0, diag = Math.hypot(w, h);
  if (diag < 18 || len < 25) return null;
  const sx = pts[0]!, sy = pts[1]!, ex = pts[2 * n - 2]!, ey = pts[2 * n - 1]!;
  const gap = Math.hypot(ex - sx, ey - sy);

  if (gap >= 0.22 * len) {
    // Open stroke: a line when it hugs its chord.
    if (gap < 0.88 * len) return null;
    let worst = 0;
    for (let i = 0; i < n; i++) worst = Math.max(worst, Math.sqrt(segDist2(pts[2 * i]!, pts[2 * i + 1]!, sx, sy, ex, ey)));
    if (worst > 0.045 * gap) return null;
    // Nearly horizontal or vertical lines snap to the axis.
    let tx = ex, ty = ey;
    const ang = (Math.atan2(ey - sy, ex - sx) * 180) / Math.PI;
    const off = (a: number) => Math.min(Math.abs(a % 90), 90 - Math.abs(a % 90));
    if (off(ang) < 4) { if (Math.abs(ex - sx) > Math.abs(ey - sy)) ty = sy; else tx = sx; }
    return [sx, sy, tx, ty];
  }

  // Closed stroke: ellipse when every point is about the same normalised distance from the centre.
  const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = Math.max(w / 2, 1), ry = Math.max(h / 2, 1);
  let sum = 0, sum2 = 0;
  for (let i = 0; i < n; i++) {
    const rho = Math.hypot((pts[2 * i]! - cx) / rx, (pts[2 * i + 1]! - cy) / ry);
    sum += rho; sum2 += rho * rho;
  }
  const mean = sum / n, std = Math.sqrt(Math.max(0, sum2 / n - mean * mean));
  if (mean > 0.9 && mean < 1.1 && std < 0.09) return shapePoints("ellipse", x0, y0, x1, y1, false, 0);

  // Otherwise a polygon: rectangle (4 right angles) or triangle (3 corners).
  const simple = simplify(pts, 0.08 * diag);
  let m = simple.length >> 1;
  if (m >= 2 && Math.hypot(simple[2 * m - 2]! - simple[0]!, simple[2 * m - 1]! - simple[1]!) < 0.25 * diag) m -= 1; // closing point
  const v = (i: number): [number, number] => [simple[2 * (i % m)]!, simple[2 * (i % m) + 1]!];
  if (m === 4) {
    const rightAngles = [0, 1, 2, 3].every((i) => Math.abs(cornerCos(...v(i + 3), ...v(i), ...v(i + 1))) < 0.4);
    const e = [0, 1, 2, 3].map((i) => Math.hypot(v(i + 1)[0] - v(i)[0], v(i + 1)[1] - v(i)[1]));
    const opposite = e[0]! / e[2]! > 0.6 && e[0]! / e[2]! < 1.67 && e[1]! / e[3]! > 0.6 && e[1]! / e[3]! < 1.67;
    if (!rightAngles || !opposite) return null;
    const a = (Math.atan2(v(1)[1] - v(0)[1], v(1)[0] - v(0)[0]) * 180) / Math.PI;
    const aligned = Math.min(Math.abs(a % 90), 90 - Math.abs(a % 90)) < 12;
    if (aligned) return [x0, y0, x1, y0, x1, y1, x0, y1, x0, y0];
    return [...v(0), ...v(1), ...v(2), ...v(3), ...v(0)];
  }
  if (m === 3) return [...v(0), ...v(1), ...v(2), ...v(0)];
  return null;
}

/**
 * Local quadratic regression along the stroke: around every point, fit a parabola to x(s) and y(s)
 * (Gaussian weights by arc length) and take its value at that point. Unlike a plain average, a
 * parabola follows an arc without pulling it inward, so a wide window does not shrink curves.
 */
function localQuadratic(p: number[], s: Float64Array, sigma: number): number[] {
  const n = p.length >> 1;
  const R = 3 * sigma;
  const inv2 = 1 / 2;
  const out = new Array<number>(2 * n);
  let lo = 0, hi = 0;
  for (let i = 0; i < n; i++) {
    const si = s[i]!;
    while (s[lo]! < si - R) lo++;
    while (hi < n - 1 && s[hi + 1]! <= si + R) hi++;
    let S0 = 0, S1 = 0, S2 = 0, S3 = 0, S4 = 0, X0 = 0, X1 = 0, X2 = 0, Y0 = 0, Y1 = 0, Y2 = 0;
    for (let j = lo; j <= hi; j++) {
      const u = (s[j]! - si) / sigma;
      const ds = ((j < n - 1 ? s[j + 1]! : s[j]!) - (j > 0 ? s[j - 1]! : s[j]!)) / 2; // uneven sampling does not bias the fit
      const w = Math.exp(-u * u * inv2) * ds;
      const u2 = u * u;
      const x = p[2 * j]!, y = p[2 * j + 1]!;
      S0 += w; S1 += w * u; S2 += w * u2; S3 += w * u2 * u; S4 += w * u2 * u2;
      X0 += w * x; X1 += w * u * x; X2 += w * u2 * x;
      Y0 += w * y; Y1 += w * u * y; Y2 += w * u2 * y;
    }
    // Value at u = 0 of the weighted least-squares parabola (Cramer's rule on the 3x3 normal equations).
    const c0 = S2 * S4 - S3 * S3;
    const det = S0 * c0 - S1 * (S1 * S4 - S2 * S3) + S2 * (S1 * S3 - S2 * S2);
    if (S0 > 0 && Math.abs(det) > 1e-9 * S0 * S0 * S0) {
      out[2 * i] = (X0 * c0 - S1 * (X1 * S4 - S3 * X2) + S2 * (X1 * S3 - S2 * X2)) / det;
      out[2 * i + 1] = (Y0 * c0 - S1 * (Y1 * S4 - S3 * Y2) + S2 * (Y1 * S3 - S2 * Y2)) / det;
    } else {
      out[2 * i] = S0 > 0 ? X0 / S0 : p[2 * i]!;
      out[2 * i + 1] = S0 > 0 ? Y0 / S0 : p[2 * i + 1]!;
    }
  }
  return out;
}

/**
 * Remove the rounding noise of pixel-quantised input from a freehand stroke.
 *
 * Pointer events report whole screen pixels, so at low zoom every sample is rounded to a grid of
 * `step` = 1/zoom page units; zooming in later shows that grid as stair steps. The true position of
 * each sample lies within half a step of what was reported, so any smooth curve that stays inside
 * those cells is as consistent with the input as the raw points. This finds one: a wide local
 * parabola fit along the stroke, with every point kept inside its cell. Nothing moves by more than
 * half a step per axis, so corners and detail the rounding already hid survive, while shallow
 * curves, where a stair step is long, are fully smoothed. The number of points, and so any per-point
 * pressure widths, is unchanged, and the ends stay where the pen touched and lifted.
 */
export function smoothFreehand(p: number[], step: number): number[] {
  const n = p.length >> 1;
  if (n < 4 || step <= 0) return p.slice();
  const s = new Float64Array(n);
  for (let i = 1; i < n; i++) s[i] = s[i - 1]! + Math.hypot(p[2 * i]! - p[2 * i - 2]!, p[2 * i + 1]! - p[2 * i - 1]!);
  const sigma = 8 * step;
  if (s[n - 1]! < 2 * sigma) return p.slice();
  const half = step / 2;
  let cur = p.slice();
  for (let iter = 0; iter < 3; iter++) {
    cur = localQuadratic(cur, s, sigma);
    for (let k = 0; k < cur.length; k++) cur[k] = Math.min(p[k]! + half, Math.max(p[k]! - half, cur[k]!)); // back inside the cell
    cur[0] = p[0]!; cur[1] = p[1]!;
    cur[2 * n - 2] = p[2 * n - 2]!; cur[2 * n - 1] = p[2 * n - 1]!;
  }
  return cur;
}

/** Greatest common step of two lengths, tolerating rounding noise below `tol`. */
function fgcd(a: number, b: number, tol: number): number {
  let x = Math.max(a, b), y = Math.min(a, b);
  while (y > tol) {
    let r = x % y;
    if (r > y - tol) r = 0; // a whole multiple, up to noise
    x = y;
    y = r;
  }
  return x;
}

/**
 * The grid spacing (page units) the points of a stroke sit on, or 0 when they do not sit on one.
 *
 * Chromium reports the pen in whole screen pixels, so a stroke drawn at zoom Z has every coordinate
 * difference equal to a whole multiple of 1/Z. The greatest common step of those differences recovers
 * 1/Z without knowing the zoom. Strokes with sub-pixel input (those made by Xournal++ itself: 0 of 33,483
 * in the sample files) have no such step and are left alone.
 */
export function quantizationStep(p: number[]): number {
  const n = p.length >> 1;
  if (n < 13) return 0;
  const ds: number[] = [];
  for (let i = 1; i < n; i++) {
    for (let a = 0; a < 2; a++) {
      const d = Math.abs(p[2 * i + a]! - p[2 * i - 2 + a]!);
      if (d > 1e-3) ds.push(d);
    }
  }
  if (ds.length < 12) return 0;
  let g = ds[0]!;
  for (const d of ds) {
    g = fgcd(g, d, 2e-3); // saved coordinates keep 8 significant digits: far below this
    if (g < 0.05) return 0;
  }
  return g > 12 ? 0 : g; // coarser than a pixel at 8% zoom cannot be pen input: it is a lattice somebody drew
}

/** A grid step shows as a stair when it spans this many screen pixels. */
const STAIR_PX = 1.5;

/**
 * The points to draw a stroke with at `scale` screen pixels per page unit.
 *
 * The stored points are never changed. When the stroke sits on an input grid whose step would be
 * visible at this scale, a smoothed copy (kept inside each point's grid cell) is drawn instead; at the
 * scale it was drawn at, the grid is one pixel and the stored points are drawn as they are.
 */
export function displayPoints(s: Stroke, scale: number): number[] {
  let v = s.view;
  if (!v || v.src !== s.pts) {
    // Strokes the editor generated (planes, rectangle outlines) have exact coordinates that merely look like a grid: never smooth them.
    const exact = s.attrs.some((a) => a[0] === "xlPlane" || a[0] === "xlRiemann");
    v = { src: s.pts, step: exact ? 0 : quantizationStep(s.pts), smooth: null };
    s.view = v;
  }
  if (v.step * scale < STAIR_PX) return s.pts;
  return (v.smooth ??= smoothFreehand(s.pts, v.step));
}
