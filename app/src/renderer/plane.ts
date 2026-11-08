/**
 * Cartesian planes drawn on demand: axes (with ticks, numbers and arrows), an optional grid and subgrid, on a frame
 * the user drags or resizes. A plane is made of ordinary pen strokes and text boxes, so it prints, exports and opens in
 * Xournal++ like any ink; every piece carries `xlPlane="<id>"` (and `xlRole`) so this editor can select the pieces as
 * one object and rebuild them when the scale, the range or the frame change. The axis strokes also carry the plane's
 * settings (`xlPlaneSpec`). The style (colour, width, glow) is the pen's at the time.
 */
import { bboxOf, cssColor, setGlow, attrValue, type Glow, type Layer, type PageElement, type Stroke, type TextBox } from "./model";
import { textBBox } from "./geometry";
import { stampText } from "./serialize";

export interface PlaneSpec {
  xmin: number; xmax: number; ymin: number; ymax: number;
  /** Distance between numbered ticks and major grid lines, in axis units. */
  xstep: number; ystep: number;
  grid: boolean;
  /** Subdivisions of each cell drawn lighter (0 = none); only with the grid. */
  sub: number;
  ticks: boolean;
  numbers: boolean;
  arrows: boolean;
  xlabel: string;
  ylabel: string;
  /** Keep the same number of points per unit on both axes when the frame is created or the range changes. */
  square: boolean;
}

export const DEFAULT_PLANE: PlaneSpec = { xmin: -5, xmax: 5, ymin: -5, ymax: 5, xstep: 1, ystep: 1, grid: true, sub: 0, ticks: true, numbers: true, arrows: true, xlabel: "x", ylabel: "y", square: true };

export interface Frame { x0: number; y0: number; x1: number; y1: number }
export interface PlaneStyle { color: string; width: number; glow?: Glow }

const MAX_LINES = 120;
const SUBS = [0, 2, 4, 5, 10];

const finite = (v: unknown, d: number) => (typeof v === "number" && Number.isFinite(v) ? v : d);
const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const round6 = (v: number) => Math.round(v * 1e6) / 1e6;

/** Make a spec usable: ranges that are whole numbers of steps (the grid then ends exactly on the frame), few enough lines. */
export function sanitizePlane(raw: unknown): PlaneSpec {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const d = DEFAULT_PLANE;
  const xstep = clamp(finite(o.xstep, d.xstep), 1e-6, 1e6) || d.xstep;
  const ystep = clamp(finite(o.ystep, d.ystep), 1e-6, 1e6) || d.ystep;
  let xmin = finite(o.xmin, d.xmin), xmax = finite(o.xmax, d.xmax), ymin = finite(o.ymin, d.ymin), ymax = finite(o.ymax, d.ymax);
  xmin = Math.floor(xmin / xstep + 1e-9) * xstep; xmax = Math.ceil(xmax / xstep - 1e-9) * xstep;
  ymin = Math.floor(ymin / ystep + 1e-9) * ystep; ymax = Math.ceil(ymax / ystep - 1e-9) * ystep;
  if (xmax <= xmin) xmax = xmin + xstep;
  if (ymax <= ymin) ymax = ymin + ystep;
  const sub = SUBS.includes(Number(o.sub)) ? Number(o.sub) : d.sub;
  const text = (v: unknown, def: string) => (typeof v === "string" ? v.slice(0, 12) : def);
  const spec: PlaneSpec = {
    xmin: round6(xmin), xmax: round6(xmax), ymin: round6(ymin), ymax: round6(ymax), xstep: round6(xstep), ystep: round6(ystep),
    grid: o.grid !== false, sub, ticks: o.ticks !== false, numbers: o.numbers !== false, arrows: o.arrows !== false,
    xlabel: text(o.xlabel, d.xlabel), ylabel: text(o.ylabel, d.ylabel), square: o.square !== false,
  };
  // too many lines is unreadable and slow: widen the step
  while ((spec.xmax - spec.xmin) / spec.xstep > MAX_LINES) { spec.xstep = round6(spec.xstep * 2); spec.xmin = Math.floor(spec.xmin / spec.xstep) * spec.xstep; spec.xmax = Math.ceil(spec.xmax / spec.xstep) * spec.xstep; }
  while ((spec.ymax - spec.ymin) / spec.ystep > MAX_LINES) { spec.ystep = round6(spec.ystep * 2); spec.ymin = Math.floor(spec.ymin / spec.ystep) * spec.ystep; spec.ymax = Math.ceil(spec.ymax / spec.ystep) * spec.ystep; }
  while (spec.sub && ((spec.xmax - spec.xmin) / spec.xstep + (spec.ymax - spec.ymin) / spec.ystep) * spec.sub > 600) spec.sub = SUBS[Math.max(0, SUBS.indexOf(spec.sub) - 1)]!;
  return spec;
}

export function encodeSpec(s: PlaneSpec): string {
  return JSON.stringify({ ...s, grid: s.grid ? 1 : 0, ticks: s.ticks ? 1 : 0, numbers: s.numbers ? 1 : 0, arrows: s.arrows ? 1 : 0, square: s.square ? 1 : 0 });
}

export function decodeSpec(text: string | null): PlaneSpec | null {
  if (!text) return null;
  try {
    const o = JSON.parse(text) as Record<string, unknown>;
    return sanitizePlane({ ...o, grid: o.grid !== 0, ticks: o.ticks !== 0, numbers: o.numbers !== 0, arrows: o.arrows !== 0, square: o.square !== 0 });
  } catch { return null; }
}

const attrsOfEl = (el: PageElement): Array<[string, string]> => ("attrs" in el ? el.attrs : []);
export const planeIdOf = (el: PageElement): string | null => attrValue(attrsOfEl(el), "xlPlane");
export const roleOf = (el: PageElement): string | null => attrValue(attrsOfEl(el), "xlRole");

/** Every piece of plane `id` in the layer. */
export function planeMembers(layer: Layer, id: string): PageElement[] {
  return layer.elements.filter((e) => planeIdOf(e) === id);
}

/** The settings stored in a plane's pieces. */
export function planeSpecOf(members: PageElement[]): PlaneSpec | null {
  for (const m of members) { const s = decodeSpec(attrValue(attrsOfEl(m), "xlPlaneSpec")); if (s) return s; }
  return null;
}

/** The frame a plane occupies now: the ends of its two axes (they always span the whole frame, so resizing keeps this true). */
export function planeFrameOf(members: PageElement[]): Frame | null {
  const ax = members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "axisx");
  const ay = members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "axisy");
  if (!ax || !ay) return null;
  const bx = bboxOf(ax.pts), by = bboxOf(ay.pts);
  return { x0: bx[0], x1: bx[2], y0: by[1], y1: by[3] };
}

/** The style of the plane's axes: their colour, width and glow. */
export function planeStyleOf(members: PageElement[]): PlaneStyle | null {
  const ax = members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "axisx");
  if (!ax) return null;
  const hex = (attrValue(ax.attrs, "color") ?? "#000000ff").slice(0, 7);
  const w = Number(attrValue(ax.attrs, "xlPlaneW")); // the width chosen for the plane, not what resizing made of the strokes
  return { color: hex, width: Number.isFinite(w) && w > 0 ? w : ax.widths[0] ?? 1.41, ...(ax.glow ? { glow: { ...ax.glow } } : {}) };
}

/** Frame that keeps equal points per unit on both axes, fitted inside `f` and centred in it. */
export function squareFrame(spec: PlaneSpec, f: Frame): Frame {
  const rx = spec.xmax - spec.xmin, ry = spec.ymax - spec.ymin;
  const k = Math.min((f.x1 - f.x0) / rx, (f.y1 - f.y0) / ry);
  const w = rx * k, h = ry * k;
  const cx = (f.x0 + f.x1) / 2, cy = (f.y0 + f.y1) / 2;
  return { x0: cx - w / 2, x1: cx + w / 2, y0: cy - h / 2, y1: cy + h / 2 };
}

/** Axis values at multiples of `step` between min and max. */
function multiples(min: number, max: number, step: number): number[] {
  const out: number[] = [];
  const n0 = Math.round(min / step), n1 = Math.round(max / step);
  for (let n = n0; n <= n1; n++) out.push(round6(n * step));
  return out;
}

const fmtNum = (v: number) => { const s = String(round6(v)); return s === "-0" ? "0" : s.replace("-", "−"); };

function strokeEl(pts: number[], color: string, alpha: string, width: number, role: string, id: string, glow?: Glow): Stroke {
  const colorAttr = color + alpha;
  const s: Stroke = {
    kind: "stroke",
    attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(+width.toFixed(3))], ["capStyle", "round"], ["xlPlane", id], ["xlRole", role]],
    tool: "pen", color: cssColor(colorAttr), widths: [width], pts, cap: "round", dash: null, bbox: bboxOf(pts),
  };
  if (glow) setGlow(s, glow);
  return s;
}

function textEl(text: string, x: number, y: number, size: number, color: string, id: string, align: "center" | "right" | "left", vcenter: boolean): TextBox {
  const t: TextBox = { kind: "text", attrs: [["xlPlane", id], ["xlRole", "label"]], text, font: "Sans", size, x, y, color: cssColor(color + "ff") };
  const b = textBBox(t);
  const w = b[2] - b[0], h = b[3] - b[1];
  t.x = align === "center" ? x - w / 2 : align === "right" ? x - w : x;
  t.y = vcenter ? y - h / 2 : y;
  stampText(t);
  return t;
}

/** Build the pieces of a plane inside `frame` (page units). */
export function buildPlane(id: string, rawSpec: PlaneSpec, frame: Frame, style: PlaneStyle): PageElement[] {
  const spec = sanitizePlane(rawSpec);
  const { x0, y0, x1, y1 } = frame;
  const rx = spec.xmax - spec.xmin, ry = spec.ymax - spec.ymin;
  const px = (x: number) => x0 + ((x - spec.xmin) / rx) * (x1 - x0);
  const py = (y: number) => y1 - ((y - spec.ymin) / ry) * (y1 - y0);
  const w = Math.max(0.3, style.width);
  const out: PageElement[] = [];
  const color = style.color;
  const xs = multiples(spec.xmin, spec.xmax, spec.xstep), ys = multiples(spec.ymin, spec.ymax, spec.ystep);

  // grid: one serpentine stroke per direction (its connectors run along the frame edge, which is a grid line too)
  const serpentine = (vals: number[], vertical: boolean): number[] => {
    const pts: number[] = [];
    vals.forEach((v, i) => {
      const a = vertical ? [px(v), i % 2 ? y0 : y1] : [i % 2 ? x1 : x0, py(v)];
      const b = vertical ? [px(v), i % 2 ? y1 : y0] : [i % 2 ? x0 : x1, py(v)];
      pts.push(a[0]!, a[1]!, b[0]!, b[1]!);
    });
    return pts;
  };
  if (spec.grid) {
    if (spec.sub > 1) {
      const mx = multiples(spec.xmin, spec.xmax, spec.xstep / spec.sub).filter((v) => Math.abs(v / spec.xstep - Math.round(v / spec.xstep)) > 1e-6);
      const my = multiples(spec.ymin, spec.ymax, spec.ystep / spec.sub).filter((v) => Math.abs(v / spec.ystep - Math.round(v / spec.ystep)) > 1e-6);
      if (mx.length > 1) out.push(strokeEl(serpentine(mx, true), color, "33", Math.max(0.25, w * 0.3), "subgrid", id));
      if (my.length > 1) out.push(strokeEl(serpentine(my, false), color, "33", Math.max(0.25, w * 0.3), "subgrid", id));
    }
    if (xs.length > 1) out.push(strokeEl(serpentine(xs, true), color, "66", Math.max(0.3, w * 0.5), "grid", id));
    if (ys.length > 1) out.push(strokeEl(serpentine(ys, false), color, "66", Math.max(0.3, w * 0.5), "grid", id));
  }

  // axes cross at 0 when it is inside the range, otherwise they sit on the frame edge
  const ya = spec.ymin <= 0 && spec.ymax >= 0 ? py(0) : py(spec.ymin);
  const xa = spec.xmin <= 0 && spec.xmax >= 0 ? px(0) : px(spec.xmin);
  const tl = clamp(w * 0.9 + 1.2, 2, 3.6); // half of a tick
  const head = clamp(w * 1.4 + 3.5, 4.5, 7.5); // length of an arrow head
  const tickW = Math.max(0.3, w * 0.6), arrowW = Math.max(0.3, w * 0.8);

  // the axes are plain lines across the whole frame (resizing reads the frame from them); ticks and arrows are thinner strokes of their own
  const ax = strokeEl([x0, ya, x1, ya], color, "ff", w, "axisx", id, style.glow);
  ax.attrs.push(["xlPlaneSpec", encodeSpec(spec)], ["xlPlaneW", String(+w.toFixed(3))]);
  out.push(ax, strokeEl([xa, y1, xa, y0], color, "ff", w, "axisy", id, style.glow));
  if (spec.ticks) {
    const tx: number[] = [], ty: number[] = [];
    for (const v of xs) {
      const X = px(v);
      if (Math.abs(X - xa) < 0.01 || X <= x0 + 0.01 || X >= x1 - 0.01) continue;
      tx.push(X, ya, X, ya - tl, X, ya + tl, X, ya); // between ticks the pen travels along the axis, under its thicker line
    }
    for (const v of [...ys].reverse()) {
      const Y = py(v);
      if (Math.abs(Y - ya) < 0.01 || Y >= y1 - 0.01 || Y <= y0 + 0.01) continue;
      ty.push(xa, Y, xa - tl, Y, xa + tl, Y, xa, Y);
    }
    if (tx.length) out.push(strokeEl(tx, color, "ff", tickW, "ticks", id, style.glow));
    if (ty.length) out.push(strokeEl(ty, color, "ff", tickW, "ticks", id, style.glow));
  }
  if (spec.arrows) {
    out.push(strokeEl([x1 - head, ya - head * 0.38, x1, ya, x1 - head, ya + head * 0.38], color, "ff", arrowW, "arrow", id, style.glow));
    out.push(strokeEl([xa - head * 0.38, y0 + head, xa, y0, xa + head * 0.38, y0 + head], color, "ff", arrowW, "arrow", id, style.glow));
  }

  // numbers and axis names
  const cellX = Math.abs((x1 - x0) * spec.xstep / rx), cellY = Math.abs((y1 - y0) * spec.ystep / ry);
  const size = clamp(Math.min(cellX, cellY) * 0.36, 6.5, 11);
  if (spec.numbers) {
    const widest = Math.max(...xs.map((v) => fmtNum(v).length)) * size * 0.62;
    const everyX = Math.max(1, Math.ceil((widest * 1.4) / cellX)), everyY = Math.max(1, Math.ceil((size * 1.7) / cellY));
    xs.forEach((v, i) => {
      if (Math.abs(px(v) - xa) < 0.01 || i % everyX) return;
      out.push(textEl(fmtNum(v), px(v), ya + tl + 1.5, size, color, id, "center", false));
    });
    ys.forEach((v, i) => {
      if (Math.abs(py(v) - ya) < 0.01 || i % everyY) return;
      out.push(textEl(fmtNum(v), xa - tl - 2, py(v), size, color, id, "right", true));
    });
  }
  const nameSize = clamp(size * 1.15, 8, 13);
  if (spec.xlabel) out.push(textEl(spec.xlabel, x1 + 3, ya, nameSize, color, id, "left", true));
  if (spec.ylabel) out.push(textEl(spec.ylabel, xa, y0 - nameSize * 1.25, nameSize, color, id, "center", false));
  return out;
}

/** A new random id for a plane. */
export function newPlaneId(): string { return "pl" + Math.random().toString(36).slice(2, 8); }
