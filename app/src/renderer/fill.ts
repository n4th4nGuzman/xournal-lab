/**
 * Paint-bucket fill. Two ways to start it:
 *  - a click: classic flood fill from that point, stopped by the strokes of the active layer;
 *  - a dragged line or curve: the fill radiates from that line toward its left or right side, and stops at any
 *    stroke of the active layer. The side is a constraint (a half-plane next to the line; the inside or outside of
 *    a closed curve), so the fill never wraps around the end of the line to the other side.
 *
 * The result is a raster mask in page space. `fillImage` paints it (solid colour or gradient) into a canvas that
 * the editor stores as an ordinary image element, below the strokes, so the file stays readable by Xournal++.
 */
import { maxOf } from "./geometry";
import { drawStroke } from "./render";
import type { Stroke } from "./model";

export interface FillOptions {
  /** Which side of the dragged line is filled (the line's travel direction defines left and right). */
  side: "left" | "right";
  /** Paint: one colour, a gradient, or Riemann rectangles standing on the dragged line and reaching the curve. */
  kind: "solid" | "linear" | "radial" | "riemann";
  /** Second colour of a gradient; the first is the pen colour. */
  color2: string;
  /** Direction of a linear gradient in degrees: 0 left to right, 90 top to bottom. */
  angle: number;
  opacity: number;
  /** Gaps (openings) up to this width, in page units, do not let the fill through. */
  gap: number;
  /** Keep the dragged line as a pen stroke. */
  keepLine: boolean;
  /** Riemann rectangles stand on a perfectly straight base (start to end of the drag), or follow the curve that was drawn. */
  base: "line" | "curve";
  /** Riemann rectangles: how tall each one is (upper = covers the curve, lower = stays under it, ...). */
  rule: RiemannRule;
  /** How the number of rectangles is given: their count, or the width of each. */
  by: "count" | "width";
  count: number;
  width: number;
  /** Draw each rectangle's outline as a pen stroke. */
  outline: boolean;
  /** Magnet: close bigger gaps on its own, stick to the nearest closed shape and pull a dragged line onto the curve. */
  magnet: boolean;
  /** Widest gap (page units) the magnet may close. */
  maxGap: number;
}

export const DEFAULT_FILL: FillOptions = { side: "left", kind: "solid", color2: "#05e1ff", angle: 90, opacity: 1, gap: 2, keepLine: false, rule: "upper", base: "line", by: "count", count: 12, width: 20, outline: true, magnet: true, maxGap: 14 };

const num = (v: unknown, lo: number, hi: number, d: number) => (typeof v === "number" && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : d);

/** Parse stored options (preferences are text); anything invalid falls back to its default. */
export function sanitizeFill(raw: unknown): FillOptions {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  return {
    side: o.side === "right" ? "right" : "left",
    kind: o.kind === "linear" || o.kind === "radial" || o.kind === "riemann" ? o.kind : "solid",
    color2: typeof o.color2 === "string" && /^#[0-9a-fA-F]{6}$/.test(o.color2) ? o.color2.toLowerCase() : DEFAULT_FILL.color2,
    angle: num(o.angle, 0, 360, DEFAULT_FILL.angle),
    opacity: num(o.opacity, 0.05, 1, 1),
    gap: num(o.gap, 0, 16, DEFAULT_FILL.gap),
    keepLine: o.keepLine === true,
    rule: o.rule === "lower" || o.rule === "mid" || o.rule === "left" || o.rule === "right" ? o.rule : "upper",
    base: o.base === "curve" ? "curve" : "line",
    by: o.by === "width" ? "width" : "count",
    count: Math.round(num(o.count, 1, 400, DEFAULT_FILL.count)),
    width: num(o.width, 1, 200, DEFAULT_FILL.width),
    outline: o.outline !== false,
    magnet: o.magnet !== false,
    maxGap: num(o.maxGap, 2, 40, DEFAULT_FILL.maxGap),
  };
}

export type FillSeed =
  | { kind: "point"; x: number; y: number }
  | { kind: "path"; pts: number[]; side: "left" | "right" };

export interface FillMask {
  /** Position and size of the mask in pixels of the fill raster (`scale` pixels per page unit). */
  x0: number; y0: number; w: number; h: number;
  /** Size of the whole fill raster the mask was cut from. */
  W: number; H: number;
  scale: number;
  mask: Uint8Array;
  /** How many pixels are filled. */
  count: number;
  /** Total raster size, to tell "filled the whole page" (an open shape) from a bounded area. */
  total: number;
}

export type FillOutcome = { ok: true; mask: FillMask } | { ok: false; reason: string };

const MAX_PIXELS = 4_500_000;

/** Pixels per page unit for a fill of `w`x`h` page units, wanting `want` but never more than the pixel budget. */
export function fillScale(w: number, h: number, want: number): number {
  return Math.max(0.5, Math.min(want, Math.sqrt(MAX_PIXELS / (w * h))));
}

function newCanvas(w: number, h: number): HTMLCanvasElement {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  return c;
}

/** Rasterise something and return which pixels it covers (alpha at least `min`). */
function coverage(W: number, H: number, scale: number, draw: (ctx: CanvasRenderingContext2D) => void, min = 100): Uint8Array {
  const ctx = newCanvas(W, H).getContext("2d", { willReadFrequently: true })!;
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  draw(ctx);
  const d = ctx.getImageData(0, 0, W, H).data;
  const out = new Uint8Array(W * H);
  for (let i = 0; i < out.length; i++) out[i] = d[4 * i + 3]! >= min ? 1 : 0;
  return out;
}

/** Grow a mask by `r` pixels in every direction (square neighbourhood, two linear passes). */
export function dilate(src: Uint8Array, W: number, H: number, r: number): Uint8Array {
  if (r <= 0) return src;
  const tmp = new Uint8Array(src.length);
  for (let y = 0; y < H; y++) {
    const row = y * W;
    let last = -1e9;
    for (let x = 0; x < W; x++) { if (src[row + x]) last = x; if (x - last <= r) tmp[row + x] = 1; }
    last = 1e9;
    for (let x = W - 1; x >= 0; x--) { if (src[row + x]) last = x; if (last - x <= r) tmp[row + x] = 1; }
  }
  const out = new Uint8Array(src.length);
  for (let x = 0; x < W; x++) {
    let last = -1e9;
    for (let y = 0; y < H; y++) { if (tmp[y * W + x]) last = y; if (y - last <= r) out[y * W + x] = 1; }
    last = 1e9;
    for (let y = H - 1; y >= 0; y--) { if (tmp[y * W + x]) last = y; if (last - y <= r) out[y * W + x] = 1; }
  }
  return out;
}

/** Remove near-duplicate points so a wobbly drag becomes a clean polyline (Ramer-Douglas-Peucker). */
export function simplifyPath(p: number[], eps: number): number[] {
  const n = p.length >> 1;
  if (n < 3) return p.slice();
  const keep = new Uint8Array(n);
  keep[0] = keep[n - 1] = 1;
  const stack: Array<[number, number]> = [[0, n - 1]];
  while (stack.length) {
    const [a, b] = stack.pop()!;
    const ax = p[2 * a]!, ay = p[2 * a + 1]!, dx = p[2 * b]! - ax, dy = p[2 * b + 1]! - ay;
    const len = Math.hypot(dx, dy);
    let worst = -1, wi = -1;
    for (let i = a + 1; i < b; i++) {
      const px = p[2 * i]! - ax, py = p[2 * i + 1]! - ay;
      const d = len < 1e-9 ? Math.hypot(px, py) : Math.abs(px * dy - py * dx) / len;
      if (d > worst) { worst = d; wi = i; }
    }
    if (worst > eps) { keep[wi] = 1; stack.push([a, wi], [wi, b]); }
  }
  const out: number[] = [];
  for (let i = 0; i < n; i++) if (keep[i]) out.push(p[2 * i]!, p[2 * i + 1]!);
  return out;
}

/** The part of a polyline between `trim` and `1 - trim` of its length (the whole path when it is short). */
export function middleOf(p: number[], trim: number): number[] {
  const total = pathLength(p);
  if (total < 30) return p;
  const lo = total * trim, hi = total * (1 - trim);
  const out: number[] = [];
  let acc = 0;
  for (let i = 0; i + 3 < p.length; i += 2) {
    const ax = p[i]!, ay = p[i + 1]!, bx = p[i + 2]!, by = p[i + 3]!, len = Math.hypot(bx - ax, by - ay);
    const t0 = Math.max(0, (lo - acc) / (len || 1)), t1 = Math.min(1, (hi - acc) / (len || 1));
    if (t1 > t0) {
      if (!out.length) out.push(ax + (bx - ax) * t0, ay + (by - ay) * t0);
      out.push(ax + (bx - ax) * t1, ay + (by - ay) * t1);
    }
    acc += len;
  }
  return out.length >= 4 ? out : p;
}

export function pathLength(p: number[]): number {
  let s = 0;
  for (let i = 2; i < p.length; i += 2) s += Math.hypot(p[i]! - p[i - 2]!, p[i + 1]! - p[i - 1]!);
  return s;
}

/** Does a dragged path come back to where it started (a closed curve)? */
export function isClosed(p: number[]): boolean {
  const n = p.length >> 1;
  if (n < 4) return false;
  const len = pathLength(p);
  return Math.hypot(p[0]! - p[2 * n - 2]!, p[1]! - p[2 * n - 1]!) <= Math.max(8, 0.06 * len);
}

/** Orientation of a closed path in screen coordinates (y down): positive is clockwise as seen. */
function signedArea(p: number[]): number {
  let a = 0;
  const n = p.length >> 1;
  for (let i = 0; i < n; i++) { const j = (i + 1) % n; a += p[2 * i]! * p[2 * j + 1]! - p[2 * j]! * p[2 * i + 1]!; }
  return a / 2;
}

/** The region a path radiates into: a union of strips (open path) or the inside / outside (closed path). */
function sideRegion(W: number, H: number, scale: number, w: number, h: number, path: number[], side: "left" | "right"): Uint8Array {
  const sgn = side === "left" ? 1 : -1;
  if (isClosed(path)) {
    const inside = (side === "left") === (signedArea(path) < 0);
    const poly = coverage(W, H, scale, (ctx) => {
      ctx.beginPath();
      ctx.moveTo(path[0]!, path[1]!);
      for (let i = 2; i < path.length; i += 2) ctx.lineTo(path[i]!, path[i + 1]!);
      ctx.closePath();
      ctx.fillStyle = "#000";
      ctx.fill();
    }, 128);
    if (inside) return poly;
    for (let i = 0; i < poly.length; i++) poly[i] = poly[i] ? 0 : 1;
    return poly;
  }
  const D = Math.hypot(w, h) + 10;
  const n = path.length >> 1;
  const nx: number[] = [], ny: number[] = [], tx: number[] = [], ty: number[] = [];
  for (let i = 0; i < n - 1; i++) {
    const dx = path[2 * i + 2]! - path[2 * i]!, dy = path[2 * i + 3]! - path[2 * i + 1]!;
    const len = Math.hypot(dx, dy) || 1;
    tx.push(dx / len); ty.push(dy / len);
    nx.push((dy / len) * sgn); ny.push((-dx / len) * sgn); // left of travel in y-down coordinates is (dy, -dx)
  }
  return coverage(W, H, scale, (ctx) => {
    ctx.fillStyle = "#000";
    for (let i = 0; i < n - 1; i++) {
      let ax = path[2 * i]!, ay = path[2 * i + 1]!, bx = path[2 * i + 2]!, by = path[2 * i + 3]!;
      if (i === 0) { ax -= tx[i]! * D; ay -= ty[i]! * D; } // the half-plane goes on past the ends
      if (i === n - 2) { bx += tx[i]! * D; by += ty[i]! * D; }
      ctx.beginPath();
      ctx.moveTo(ax, ay); ctx.lineTo(bx, by);
      ctx.lineTo(bx + nx[i]! * D, by + ny[i]! * D); ctx.lineTo(ax + nx[i]! * D, ay + ny[i]! * D);
      ctx.closePath();
      ctx.fill();
      if (i > 0) { // join the strips of two segments at their shared vertex
        const px = path[2 * i]!, py = path[2 * i + 1]!;
        ctx.beginPath();
        ctx.moveTo(px, py);
        ctx.lineTo(px + nx[i - 1]! * D, py + ny[i - 1]! * D);
        ctx.lineTo(px + nx[i]! * D, py + ny[i]! * D);
        ctx.closePath();
        ctx.fill();
      }
    }
  }, 128);
}

/**
 * Flood fill from several seeds over the free pixels (4-connected scanline fill). Returns the filled mask.
 * With `abortOnBorder` the fill stops as soon as it reaches the edge of the page raster and reports `leaked`:
 * a shape that is really closed never gets there.
 */
export function floodFill(free: Uint8Array, W: number, H: number, seeds: number[], abortOnBorder = false): { mask: Uint8Array; count: number; leaked: boolean } {
  const mask = new Uint8Array(W * H);
  const stack = seeds.slice();
  let count = 0;
  while (stack.length) {
    const i = stack.pop()!;
    if (!free[i] || mask[i]) continue;
    const y = (i / W) | 0, row = y * W;
    let xl = i - row, xr = xl;
    while (xl > 0 && free[row + xl - 1] && !mask[row + xl - 1]) xl--;
    while (xr < W - 1 && free[row + xr + 1] && !mask[row + xr + 1]) xr++;
    for (let x = xl; x <= xr; x++) mask[row + x] = 1;
    count += xr - xl + 1;
    if (abortOnBorder && (xl === 0 || xr === W - 1 || y === 0 || y === H - 1)) return { mask, count, leaked: true };
    for (const ny of [y - 1, y + 1]) {
      if (ny < 0 || ny >= H) continue;
      const nr = ny * W;
      let inRun = false;
      for (let x = xl; x <= xr; x++) {
        const ok = free[nr + x] && !mask[nr + x];
        if (ok && !inRun) stack.push(nr + x);
        inRun = !!ok;
      }
    }
  }
  return { mask, count, leaked: false };
}

/** Closest point of any stroke within `radius` (page units) of (x, y), or null. */
export function nearestOnStrokes(strokes: Stroke[], x: number, y: number, radius: number): [number, number] | null {
  let best: [number, number] | null = null;
  let bd = radius * radius;
  for (const s of strokes) {
    if (s.tool === "eraser") continue;
    if (isGenerated(s) && !s.attrs.some((a) => a[0] === "xlRole" && (a[1] === "axisx" || a[1] === "axisy"))) continue; // axes attract, grid lines do not
    const b = s.bbox, pad = radius + maxOf(s.widths) / 2;
    if (x < b[0] - pad || x > b[2] + pad || y < b[1] - pad || y > b[3] + pad) continue;
    const p = s.pts;
    for (let i = 0; i + 3 < p.length; i += 2) {
      const ax = p[i]!, ay = p[i + 1]!, dx = p[i + 2]! - ax, dy = p[i + 3]! - ay;
      const l2 = dx * dx + dy * dy;
      const t = l2 ? Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / l2)) : 0;
      const qx = ax + t * dx, qy = ay + t * dy, d = (x - qx) ** 2 + (y - qy) ** 2;
      if (d < bd) { bd = d; best = [qx, qy]; }
    }
    if (p.length === 2) { const d = (x - p[0]!) ** 2 + (y - p[1]!) ** 2; if (d < bd) { bd = d; best = [p[0]!, p[1]!]; } }
  }
  return best;
}

/** Magnet for a dragged line: each end that lands near a stroke is pulled onto it, so the line really touches the curve. */
export function snapPathEnds(path: number[], strokes: Stroke[], radius: number): number[] {
  const out = path.slice();
  const n = out.length;
  const a = nearestOnStrokes(strokes, out[0]!, out[1]!, radius);
  if (a) { out[0] = a[0]; out[1] = a[1]; }
  const b = nearestOnStrokes(strokes, out[n - 2]!, out[n - 1]!, radius);
  if (b) { out[n - 2] = b[0]; out[n - 1] = b[1]; }
  return out;
}

export interface FillMagnet {
  /** Widest gap (page units) the magnet may close on its own. */
  maxGap: number;
  /** How close (raster pixels) a click can be to a stroke for the magnet to look at the other side of it. */
  snapPx: number;
}

export interface FillInfo {
  /** Gap actually used (page units): larger than the requested one when the magnet had to close a hole. */
  gapUsed: number;
  /** The click was moved to the closed region next to it because the spot under it leaked. */
  moved: boolean;
}

/** Gap sizes the magnet tries, smallest first, up to `max`. */
function gapSteps(first: number, max: number): number[] {
  const out = [first];
  for (const g of [3, 6, 10, 16, 24, 32, 40]) if (g > first && g < max) out.push(g);
  if (max > first) out.push(max);
  return out;
}

/**
 * Work out which pixels a fill covers. `strokes` are the strokes of the active layer: they stop the fill.
 * `gap` is the widest opening (page units) that does not let the fill through. With a `magnet`, a click that leaks
 * out of the page is retried with ever larger gaps, and a click that lands just outside a closed shape (a pixel
 * off) falls back to the closed region next to it; if nothing closes, nothing is filled rather than the whole page.
 */
export function computeFill(strokes: Stroke[], w: number, h: number, scale: number, seed: FillSeed, gap: number, magnet: FillMagnet | null = null): FillOutcome & { info?: FillInfo } {
  const W = Math.ceil(w * scale), H = Math.ceil(h * scale);
  const raw = coverage(W, H, scale, (ctx) => {
    for (const s of strokes) {
      if (s.tool === "eraser" || isGenerated(s)) continue; // a plane under the drawing is not a wall
      drawStroke(ctx, { ...s, color: "#000", glow: undefined, widths: s.widths.map((x) => Math.max(x, 1.6 / scale)) });
    }
  }, 40);
  const radius = (g: number) => Math.round((g / 2) * scale); // a gap of width g is closed by growing both walls by g/2

  const freeFor = (g: number): Uint8Array => {
    const wall = dilate(raw, W, H, radius(g));
    const free = new Uint8Array(W * H);
    for (let i = 0; i < free.length; i++) free[i] = wall[i] ? 0 : 1;
    return free;
  };
  const nearestFree = (free: Uint8Array, sx: number, sy: number, reach: number): number => {
    let best = -1, bd = Infinity;
    for (let dy = -reach; dy <= reach; dy++) for (let dx = -reach; dx <= reach; dx++) {
      const x = sx + dx, y = sy + dy;
      if (x < 0 || y < 0 || x >= W || y >= H || !free[y * W + x]) continue;
      const d = dx * dx + dy * dy;
      if (d < bd) { bd = d; best = y * W + x; }
    }
    return best;
  };
  const finish = (mask: Uint8Array, g: number, info: FillInfo): FillOutcome & { info?: FillInfo } => {
    // Tuck the fill under the strokes that stopped it, so no white seam shows between ink and colour. Only the margin
    // within reach of the filled area is taken, so colour never crosses to the far side of a thin stroke.
    const r = radius(g);
    const wall = dilate(raw, W, H, r);
    const grown = dilate(mask, W, H, r + 1);
    let x0 = W, y0 = H, x1 = -1, y1 = -1, total = 0;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const i = y * W + x;
      if (!mask[i] && grown[i] && wall[i]) mask[i] = 1; // the margin kept free around strokes, and the strokes themselves
      if (mask[i]) { total++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    const bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    const crop = new Uint8Array(bw * bh);
    for (let y = 0; y < bh; y++) crop.set(mask.subarray((y + y0) * W + x0, (y + y0) * W + x0 + bw), y * bw);
    return { ok: true, mask: { x0, y0, w: bw, h: bh, W, H, scale, mask: crop, count: total, total: W * H }, info };
  };

  if (seed.kind === "path") {
    const free = freeFor(gap);
    const side = sideRegion(W, H, scale, w, h, seed.pts, seed.side);
    for (let i = 0; i < free.length; i++) if (!side[i]) free[i] = 0;
    const seeds: number[] = [];
    // Seed from the middle of the line only: near its ends the line meets the curve, and a seed there could land on
    // the wrong side of that curve.
    const core = middleOf(seed.pts, 0.15);
    for (const width of [5, 12]) { // a band of pixels along the line, on the chosen side
      const band = coverage(W, H, scale, (ctx) => {
        ctx.beginPath();
        ctx.moveTo(core[0]!, core[1]!);
        for (let i = 2; i < core.length; i += 2) ctx.lineTo(core[i]!, core[i + 1]!);
        ctx.lineWidth = width / scale;
        ctx.lineJoin = "round";
        ctx.lineCap = "butt";
        ctx.strokeStyle = "#000";
        ctx.stroke();
      }, 100);
      for (let i = 0; i < band.length; i++) if (band[i] && free[i]) seeds.push(i);
      if (seeds.length) break;
    }
    if (!seeds.length) return { ok: false, reason: "No hay espacio libre junto a la línea: ya hay trazos encima" };
    const { mask, count } = floodFill(free, W, H, seeds);
    if (!count) return { ok: false, reason: "No hay nada que rellenar ahí" };
    return finish(mask, gap, { gapUsed: gap, moved: false });
  }

  // Click mode.
  const sx = Math.min(W - 1, Math.max(0, Math.round(seed.x * scale))), sy = Math.min(H - 1, Math.max(0, Math.round(seed.y * scale)));
  if (!magnet || strokes.length === 0) {
    const free = freeFor(gap);
    const at = nearestFree(free, sx, sy, Math.ceil(Math.max(radius(gap) + 2, 6 * scale)));
    if (at < 0) return { ok: false, reason: "El clic cayó sobre un trazo: haz clic en un hueco libre" };
    const { mask } = floodFill(free, W, H, [at]);
    return finish(mask, gap, { gapUsed: gap, moved: false });
  }
  let sawFree = false;
  for (const g of gapSteps(gap, magnet.maxGap)) {
    const free = freeFor(g);
    const reach = Math.ceil(Math.max(radius(g) + 2, 6 * scale));
    const primary = nearestFree(free, sx, sy, reach);
    if (primary >= 0) {
      sawFree = true;
      const f = floodFill(free, W, H, [primary], true);
      if (!f.leaked) return finish(f.mask, g, { gapUsed: g, moved: false });
    }
    // The spot under the click leaks. If it sits within a few pixels of a stroke, the closed region on the other
    // side of that stroke (the click was a pixel off) is the one meant.
    let best: { mask: Uint8Array; count: number } | null = null;
    const ring = Math.max(magnet.snapPx, radius(g) + 2);
    for (let k = 0; k < 16; k++) {
      const a = (k / 16) * Math.PI * 2;
      const x = Math.round(sx + Math.cos(a) * ring), y = Math.round(sy + Math.sin(a) * ring);
      if (x < 0 || y < 0 || x >= W || y >= H) continue;
      const at = nearestFree(free, x, y, 2);
      if (at < 0 || at === primary) continue;
      sawFree = true;
      const f = floodFill(free, W, H, [at], true);
      if (!f.leaked && (!best || f.count < best.count)) best = f;
    }
    if (best) return finish(best.mask, g, { gapUsed: g, moved: true });
  }
  if (!sawFree) return { ok: false, reason: "El clic cayó sobre un trazo: haz clic en un hueco libre" };
  return { ok: false, reason: `La forma no se cierra ni cerrando huecos de hasta ${magnet.maxGap}: ciérrala, sube el máximo del imán o mantén Mayús para rellenar de todos modos` };
}

// ---------------------------------------------------------------- Riemann rectangles
export type RiemannRule = "upper" | "lower" | "mid" | "left" | "right";

/** One rectangle: it stands on the segment a→b of the base and reaches `h` along `normal`. */
export interface RiemannCol {
  a: [number, number];
  b: [number, number];
  normal: [number, number];
  h: number;
}

export interface RiemannResult {
  cols: RiemannCol[];
  /** Columns left out because nothing stops them (no curve there) or the curve touches the base. */
  skipped: number;
  n: number;
  /** Length of the base per column (page units). */
  width: number;
}

/** Number of columns for a base of length `len`: asked for directly, or from a width (equal columns). */
export function riemannColumns(len: number, by: "count" | "width", count: number, width: number): number {
  const n = by === "count" ? count : Math.round(len / Math.max(0.5, width));
  return Math.max(1, Math.min(400, Math.round(n)));
}

/**
 * Cut a base into `n` columns of equal length along it. A straight base (two points) gives equal parts of the line;
 * a curve is followed: every column stands on the chord of its stretch of the curve and looks to the chosen side of
 * the direction of travel.
 */
export function riemannBases(path: number[], n: number, side: "left" | "right"): Array<{ a: [number, number]; b: [number, number]; normal: [number, number] }> {
  const cum = [0];
  for (let i = 2; i < path.length; i += 2) cum.push(cum[cum.length - 1]! + Math.hypot(path[i]! - path[i - 2]!, path[i + 1]! - path[i - 1]!));
  const total = cum[cum.length - 1]!;
  const at = (s: number): [number, number] => {
    let i = 1;
    while (i < cum.length - 1 && cum[i]! < s) i++;
    const seg = cum[i]! - cum[i - 1]!, t = seg > 0 ? (s - cum[i - 1]!) / seg : 0;
    return [path[2 * i - 2]! + (path[2 * i]! - path[2 * i - 2]!) * t, path[2 * i - 1]! + (path[2 * i + 1]! - path[2 * i - 1]!) * t];
  };
  const out: Array<{ a: [number, number]; b: [number, number]; normal: [number, number] }> = [];
  for (let c = 0; c < n; c++) {
    const a = at((c / n) * total), b = at(((c + 1) / n) * total);
    const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    const left: [number, number] = [(b[1] - a[1]) / len, -(b[0] - a[0]) / len];
    out.push({ a, b, normal: side === "left" ? left : [-left[0], -left[1]] });
  }
  return out;
}

/** Strokes that Riemann rectangles stop against: every stroke of the layer except the outlines of earlier rectangles. */
export function riemannBarriers(strokes: Stroke[]): Stroke[] {
  return strokes.filter((s) => !isGenerated(s));
}

/**
 * Ink the editor drew as a backdrop (the axes and grid of a Cartesian plane, the outlines of earlier rectangles):
 * fills and rectangles go over it instead of stopping at it.
 */
export function isGenerated(s: Stroke): boolean {
  if (s.attrs.some((a) => a[0] === "xlRiemann")) return true;
  const plane = s.attrs.some((a) => a[0] === "xlPlane");
  if (!plane) return false;
  const role = s.attrs.find((a) => a[0] === "xlRole")?.[1];
  return !(role === "figbody" || role === "figarrow" || role === "figmark" || role === "setbody" || role === "setarrow" || role === "setmark"); // the lines of a figure are ordinary ink for a fill
}

/**
 * Heights of Riemann rectangles standing on `bases`. From each point of a column's base a ray goes until it meets a
 * stroke of the layer (the curve); a column is as tall as that distance. Upper: the highest the curve gets in the
 * column (rectangles cover the curve); lower: the lowest (they stay under it); mid, left and right use the curve's
 * height at that point of the column. Gaps in the curve up to `gap` do not let a ray through.
 */
export function measureRiemann(strokes: Stroke[], w: number, h: number, scale: number, bases: Array<{ a: [number, number]; b: [number, number]; normal: [number, number] }>, rule: RiemannRule, gap: number): RiemannResult {
  const W = Math.ceil(w * scale), H = Math.ceil(h * scale);
  const r = Math.max(0, Math.round((gap / 2) * scale));
  const wall = dilate(coverage(W, H, scale, (ctx) => {
    for (const s of riemannBarriers(strokes)) {
      if (s.tool === "eraser") continue;
      drawStroke(ctx, { ...s, color: "#000", glow: undefined, widths: s.widths.map((x) => Math.max(x, 1.6 / scale)) });
    }
  }, 40), W, H, r);
  const step = 1 / scale;
  const reachMax = (W + H) / scale;
  const cols: RiemannCol[] = [];
  let skipped = 0, totalLen = 0;
  for (const base of bases) {
    const len = Math.hypot(base.b[0] - base.a[0], base.b[1] - base.a[1]);
    totalLen += len;
    if (len < 1e-6) { skipped++; continue; }
    const tx = (base.b[0] - base.a[0]) / len, ty = (base.b[1] - base.a[1]) / len;
    /** Height of the curve above the base at distance u along the column, or Infinity when the ray leaves the page without meeting one. */
    const heightAt = (u: number): number => {
      const x0 = base.a[0] + tx * u, y0 = base.a[1] + ty * u;
      for (let v = 0; v < reachMax; v += step) {
        const ix = Math.floor((x0 + base.normal[0] * v) * scale), iy = Math.floor((y0 + base.normal[1] * v) * scale);
        if (ix < 0 || iy < 0 || ix >= W || iy >= H) return Infinity;
        if (wall[iy * W + ix]) return v === 0 ? 0 : v + r * step; // the margin grown around strokes is not part of the height; a base that starts on the curve has none
      }
      return Infinity;
    };
    let height: number;
    if (rule === "left") height = heightAt(step);
    else if (rule === "right") height = heightAt(len - step);
    else if (rule === "mid") height = heightAt(len / 2);
    else {
      const samples = Math.max(5, Math.ceil(len * scale));
      height = rule === "upper" ? 0 : Infinity;
      for (let j = 0; j < samples; j++) {
        const v = heightAt(((j + 0.5) / samples) * len);
        height = rule === "upper" ? Math.max(height, v) : Math.min(height, v);
      }
    }
    if (!Number.isFinite(height) || height < 0.5) skipped++;
    else cols.push({ a: base.a, b: base.b, normal: base.normal, h: height });
  }
  return { cols, skipped, n: bases.length, width: totalLen / Math.max(1, bases.length) };
}

/** The four corners of a column, from the base start counter-clockwise on the page. */
export function colCorners(c: RiemannCol): Array<[number, number]> {
  return [c.a, c.b, [c.b[0] + c.normal[0] * c.h, c.b[1] + c.normal[1] * c.h], [c.a[0] + c.normal[0] * c.h, c.a[1] + c.normal[1] * c.h]];
}

/** The pixels covered by Riemann rectangles (all as one shape, so neighbours leave no seam), as a fill mask. */
export function rectsMask(W: number, H: number, scale: number, cols: RiemannCol[]): FillMask | null {
  if (!cols.length) return null;
  const full = coverage(W, H, scale, (ctx) => {
    ctx.beginPath();
    for (const c of cols) {
      const q = colCorners(c);
      ctx.moveTo(q[0]![0], q[0]![1]);
      for (let i = 1; i < 4; i++) ctx.lineTo(q[i]![0], q[i]![1]);
      ctx.closePath();
    }
    ctx.fillStyle = "#000";
    ctx.fill("nonzero");
  }, 128);
  let x0 = W, y0 = H, x1 = -1, y1 = -1, count = 0;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (full[y * W + x]) { count++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
  if (!count) return null;
  const bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  const crop = new Uint8Array(bw * bh);
  for (let y = 0; y < bh; y++) crop.set(full.subarray((y + y0) * W + x0, (y + y0) * W + x0 + bw), y * bw);
  return { x0, y0, w: bw, h: bh, W, H, scale, mask: crop, count, total: W * H };
}

/** Paint a mask with a colour or a gradient: a canvas the size of the mask's bounding box. */
export function fillImage(m: FillMask, color1: string, o: FillOptions): HTMLCanvasElement {
  const cv = newCanvas(m.w, m.h);
  const ctx = cv.getContext("2d")!;
  const w = m.w, h = m.h;
  if (o.kind === "solid") ctx.fillStyle = color1;
  else if (o.kind === "linear") {
    const a = (o.angle * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
    const L = (Math.abs(w * c) + Math.abs(h * s)) / 2;
    const g = ctx.createLinearGradient(w / 2 - c * L, h / 2 - s * L, w / 2 + c * L, h / 2 + s * L);
    g.addColorStop(0, color1);
    g.addColorStop(1, o.color2);
    ctx.fillStyle = g;
  } else {
    const g = ctx.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, Math.hypot(w, h) / 2);
    g.addColorStop(0, color1);
    g.addColorStop(1, o.color2);
    ctx.fillStyle = g;
  }
  ctx.fillRect(0, 0, w, h);
  const maskCv = newCanvas(w, h);
  const mctx = maskCv.getContext("2d")!;
  const img = mctx.createImageData(w, h);
  for (let i = 0; i < m.mask.length; i++) img.data[4 * i + 3] = m.mask[i] ? 255 : 0;
  mctx.putImageData(img, 0, 0);
  ctx.globalCompositeOperation = "destination-in";
  ctx.drawImage(maskCv, 0, 0);
  if (o.opacity < 1) { // apply the opacity last, once, so a gradient keeps its own transparency
    const out = newCanvas(w, h);
    const octx = out.getContext("2d")!;
    octx.globalAlpha = o.opacity;
    octx.drawImage(cv, 0, 0);
    return out;
  }
  return cv;
}
