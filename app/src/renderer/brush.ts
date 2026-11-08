/**
 * Pen tips. A tip decides how wide each part of a stroke is (from pressure, speed and direction); the result is the stroke's
 * ordinary per-point width list, so the file format and the renderer do not change and strokes look the same everywhere.
 * Everything here is pure: the editor feeds it samples and gets widths back.
 */

export type BrushKind = "pen" | "ink" | "fountain" | "brush";

export interface BrushInfo { kind: BrushKind; name: string; hint: string }

export const BRUSHES: BrushInfo[] = [
  { kind: "pen", name: "Bolígrafo", hint: "Grosor fijo (con lápiz óptico sigue la presión)" },
  { kind: "ink", name: "Tinta", hint: "Entra y sale en punta; con presión engorda, y al ir rápido adelgaza" },
  { kind: "fountain", name: "Pluma", hint: "Punta de caligrafía: fino al trazar a lo largo de la punta y grueso en el sentido contrario" },
  { kind: "brush", name: "Pincel", hint: "Muy sensible a la presión, con entrada y salida largas" },
];

export const isBrushKind = (v: unknown): v is BrushKind => v === "pen" || v === "ink" || v === "fountain" || v === "brush";

// ------------------------------------------------------------------------------------------------------ pressure curve
/** A pressure curve: a cubic Bézier from (0,0) to (1,1) with two control points, plus a dead zone below `min`. */
export interface PressureCurve { x1: number; y1: number; x2: number; y2: number; min: number }

export const LINEAR_CURVE: PressureCurve = { x1: 0.25, y1: 0.25, x2: 0.75, y2: 0.75, min: 0 };
export const CURVE_PRESETS: Array<{ name: string; curve: PressureCurve }> = [
  { name: "Lineal", curve: LINEAR_CURVE },
  { name: "Suave (más fácil engrosar)", curve: { x1: 0.15, y1: 0.4, x2: 0.5, y2: 0.9, min: 0 } },
  { name: "Firme (hay que apretar más)", curve: { x1: 0.5, y1: 0.1, x2: 0.85, y2: 0.6, min: 0.02 } },
];

const num = (v: unknown, d: number, lo: number, hi: number) => (typeof v === "number" && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : d);

export function sanitizeCurve(raw: unknown): PressureCurve {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  return { x1: num(o.x1, 0.25, 0, 1), y1: num(o.y1, 0.25, 0, 1), x2: num(o.x2, 0.75, 0, 1), y2: num(o.y2, 0.75, 0, 1), min: num(o.min, 0, 0, 0.5) };
}

const LUT_SIZE = 256;

/** The curve as a table of 256 values (pressure in, pressure out). Monotonic by construction (x control points are in [0,1]). */
export function curveLut(c: PressureCurve): Float32Array {
  const lut = new Float32Array(LUT_SIZE);
  const bez = (t: number, a: number, b: number) => 3 * (1 - t) * (1 - t) * t * a + 3 * (1 - t) * t * t * b + t * t * t;
  // sample the curve densely by t, then resample by x
  const N = 512, xs = new Float32Array(N + 1), ys = new Float32Array(N + 1);
  let prev = 0;
  for (let i = 0; i <= N; i++) { const t = i / N; xs[i] = Math.max(prev, bez(t, c.x1, c.x2)); prev = xs[i]!; ys[i] = bez(t, c.y1, c.y2); }
  let j = 0, maxY = 0;
  for (let k = 0; k < LUT_SIZE; k++) {
    const x = k / (LUT_SIZE - 1);
    while (j < N && xs[j + 1]! < x) j++;
    const x0 = xs[j]!, x1 = xs[Math.min(N, j + 1)]!, y0 = ys[j]!, y1 = ys[Math.min(N, j + 1)]!;
    const y = x1 > x0 ? y0 + ((y1 - y0) * (x - x0)) / (x1 - x0) : y1;
    maxY = Math.max(maxY, y);
    lut[k] = maxY; // never decreases
  }
  return lut;
}

export class PressureMap {
  private lut: Float32Array;
  constructor(readonly curve: PressureCurve = LINEAR_CURVE) { this.lut = curveLut(curve); }
  /** Raw pressure (0..1) to the pressure the tip sees (0..1). */
  apply(p: number): number {
    const min = this.curve.min;
    const x = min >= 1 ? 0 : Math.min(1, Math.max(0, (p - min) / (1 - min)));
    const f = x * (LUT_SIZE - 1), i = Math.floor(f), k = f - i;
    return this.lut[i]! + ((this.lut[Math.min(LUT_SIZE - 1, i + 1)]! - this.lut[i]!) * k);
  }
}

// -------------------------------------------------------------------------------------------------------------- engine
const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const NIB_ANGLE = -Math.PI / 4.5; // the pen's nib, 40 degrees above the horizontal as seen on the page (y points down)

/**
 * Computes the widths of one stroke as its points arrive. `widths[0]` is the width of the first point (a dot is drawn with it)
 * and `widths[i]` the width of the segment that ends at point i, exactly as strokes already store them.
 */
export class BrushEngine {
  private last: { x: number; y: number; t: number } | null = null;
  private len = 0;
  private w: number;
  private angle: number | null = null;
  /** Width the tip would have without the entry taper, smoothed. */
  private smooth = -1;

  constructor(readonly kind: BrushKind, readonly size: number, private readonly map: PressureMap, private readonly pressure: boolean) {
    this.w = kind === "pen" ? size : this.width(0.5, 0, null, 0);
  }

  /** Width of the first point. */
  first(): number { return this.w; }

  /** Width of the segment that ends at (x, y), sampled at time t (ms) with raw pressure p. */
  next(x: number, y: number, p: number, t: number): number {
    const prev = this.last;
    let v = 0, theta: number | null = null, d = 0;
    if (prev) {
      d = Math.hypot(x - prev.x, y - prev.y);
      const dt = Math.max(1, t - prev.t);
      v = d / dt;
      if (d > 0.05) theta = Math.atan2(y - prev.y, x - prev.x);
    }
    this.last = { x, y, t };
    this.len += d;
    if (theta !== null) this.angle = theta; // direction of the latest move
    return (this.w = this.width(this.pressure ? this.map.apply(p) : 0.5, v, theta ?? this.angle, this.len));
  }

  private width(p: number, v: number, theta: number | null, len: number): number {
    const S = this.size;
    let target: number, alpha: number, taperLen: number;
    switch (this.kind) {
      case "pen":
        return this.pressure ? Math.max(0.05, p * 1.5) * S : S;
      case "ink": {
        const pf = this.pressure ? 0.3 + 1.25 * p : 1;
        const sf = this.pressure ? 1 - 0.25 * clamp(v / 3, 0, 1) : 1.15 - 0.5 * clamp(v / 2.5, 0, 1);
        target = S * pf * sf; alpha = 0.35; taperLen = 3.5 * S;
        break;
      }
      case "fountain": {
        const across = theta === null ? 0.6 : Math.abs(Math.sin(theta - NIB_ANGLE)); // thin along the nib, thick across it
        target = S * 2.2 * (0.16 + 0.84 * across) * (this.pressure ? 0.7 + 0.6 * p : 1); alpha = 0.4; taperLen = 1.2 * S;
        break;
      }
      case "brush": {
        const pf = this.pressure ? 0.12 + 2.3 * Math.pow(p, 1.1) : 0.45 + 1.4 * (1 - clamp(v / 3, 0, 1));
        target = S * pf; alpha = 0.3; taperLen = 3 * S;
        break;
      }
    }
    this.smooth = this.smooth < 0 ? target : this.smooth + (target - this.smooth) * alpha;
    const entry = len >= taperLen ? 1 : 0.35 + 0.65 * (len / taperLen); // the stroke starts in a point
    return Math.max(0.05, this.smooth * entry);
  }

  /** Close a finished stroke: its last part runs out to a point (the tips that taper). Returns new widths (the input is not changed). */
  finish(pts: number[], widths: number[]): number[] {
    const out = widths.slice();
    if (out.length > 1) out[0] = out[1]!; // the first point is as wide as the first segment: the stroke does not begin with a dot of another size
    const tail = this.kind === "ink" ? 5 * this.size : this.kind === "brush" ? 10 * this.size : this.kind === "fountain" ? 1.5 * this.size : 0;
    const n = pts.length >> 1;
    if (!tail || n < 3 || widths.length < n) return out;
    let acc = 0;
    for (let i = n - 1; i >= 1; i--) {
      if (i < n - 1) acc += Math.hypot(pts[2 * (i + 1)]! - pts[2 * i]!, pts[2 * (i + 1) + 1]! - pts[2 * i + 1]!);
      if (acc >= tail) break;
      const k = 0.2 + 0.8 * (acc / tail);
      out[i] = Math.max(0.05, out[i]! * k);
    }
    return out;
  }
}

/** A made-up stroke through a sine wave, to show what a tip looks like: [x, y, pressure, ms] samples. */
export function demoStroke(w: number, h: number): Array<[number, number, number, number]> {
  const out: Array<[number, number, number, number]> = [];
  const N = 60;
  for (let i = 0; i <= N; i++) {
    const u = i / N;
    out.push([8 + u * (w - 16), h / 2 + Math.sin(u * Math.PI * 2) * h * 0.28, Math.sin(u * Math.PI) * 0.9 + 0.05, i * 8]);
  }
  return out;
}
