/**
 * Cartesian space in 3D, drawn as a projection onto the page: an origin and, for each axis, the page vector that one
 * unit along it covers, separately for its positive and its negative half. Dragging the anchor at the end of a half changes
 * that half's angle and length on the page, so any oblique or axonometric view can be made by hand, one side at a time. Like the 2D plane, it is made of ordinary pen strokes and
 * text boxes (all with `xlPlane="<id>"`, so they select, move and resize as one), rebuilt when its settings or its
 * geometry change. The geometry is read back from the axis strokes themselves, so moving, resizing or rotating the
 * selection never leaves stale numbers behind.
 */
import { attrValue, bboxOf, cssColor, setGlow, type Layer, type PageElement, type Stroke, type TextBox } from "./model";
import { textBBox } from "./geometry";
import { stampText } from "./serialize";
import { planeIdOf, planeMembers, roleOf, type PlaneStyle } from "./plane";

export type V2 = [number, number];

export interface Space3Spec {
  xmin: number; xmax: number; ymin: number; ymax: number; zmin: number; zmax: number;
  xstep: number; ystep: number; zstep: number;
  /** Grid on the coordinate planes xy (z = 0), xz (y = 0) and yz (x = 0). */
  gridXY: boolean; gridXZ: boolean; gridYZ: boolean;
  ticks: boolean; numbers: boolean; arrows: boolean;
  names: [string, string, string];
  /** Where the origin is on the page, the page vector of one unit toward the positive end of x, y and z... */
  o: V2;
  u: [V2, V2, V2];
  /** ...and toward the negative end (the same as `u` while an axis is straight). */
  n: [V2, V2, V2];
}

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const finite = (v: unknown, d: number) => (typeof v === "number" && Number.isFinite(v) ? v : d);
const round6 = (v: number) => Math.round(v * 1e6) / 1e6;
const MAX_LINES = 40;

/** Page vector for an axis `deg` degrees counter-clockwise from the right, `len` page units per unit. */
export function axisVector(deg: number, len: number): V2 {
  const a = (deg * Math.PI) / 180;
  return [Math.cos(a) * len, -Math.sin(a) * len];
}

/** Angle (degrees, counter-clockwise from the right, 0 to 360) and length of a page vector. */
export function vectorAngle(v: V2): { deg: number; len: number } {
  const deg = (Math.atan2(-v[1], v[0]) * 180) / Math.PI;
  return { deg: (deg + 360) % 360, len: Math.hypot(v[0], v[1]) };
}

/** The usual hand-drawn view: z up, y to the right, x toward the viewer (down-left, foreshortened). */
export function defaultVectors(scale = 1): [V2, V2, V2] {
  return [axisVector(215, 24 * scale), axisVector(0, 34 * scale), axisVector(90, 34 * scale)];
}

export const DEFAULT_SPACE3: Space3Spec = {
  xmin: -3, xmax: 3, ymin: -3, ymax: 3, zmin: -3, zmax: 3, xstep: 1, ystep: 1, zstep: 1,
  gridXY: true, gridXZ: false, gridYZ: false, ticks: true, numbers: true, arrows: true, names: ["x", "y", "z"],
  o: [0, 0], u: defaultVectors(), n: defaultVectors(),
};

const vec = (v: unknown, d: V2): V2 => (Array.isArray(v) && v.length === 2 && v.every((n) => typeof n === "number" && Number.isFinite(n)) ? [v[0] as number, v[1] as number] : [...d]);

export function sanitizeSpace3(raw: unknown): Space3Spec {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const d = DEFAULT_SPACE3;
  const axis = (min: unknown, max: unknown, step: unknown, dmin: number, dmax: number, dstep: number) => {
    const st = clamp(finite(step, dstep), 1e-6, 1e6) || dstep;
    let lo = Math.floor(finite(min, dmin) / st + 1e-9) * st, hi = Math.ceil(finite(max, dmax) / st - 1e-9) * st;
    if (hi <= lo) hi = lo + st;
    let s = st;
    while ((hi - lo) / s > MAX_LINES) { s *= 2; lo = Math.floor(lo / s) * s; hi = Math.ceil(hi / s) * s; }
    return { lo: round6(lo), hi: round6(hi), st: round6(s) };
  };
  const x = axis(o.xmin, o.xmax, o.xstep, d.xmin, d.xmax, d.xstep), y = axis(o.ymin, o.ymax, o.ystep, d.ymin, d.ymax, d.ystep), z = axis(o.zmin, o.zmax, o.zstep, d.zmin, d.zmax, d.zstep);
  const names = Array.isArray(o.names) ? (o.names as unknown[]).map((n, i) => (typeof n === "string" ? n.slice(0, 12) : d.names[i]!)) : [...d.names];
  const uu = Array.isArray(o.u) ? (o.u as unknown[]) : [];
  const nn = Array.isArray(o.n) ? (o.n as unknown[]) : [];
  const u: [V2, V2, V2] = [vec(uu[0], d.u[0]), vec(uu[1], d.u[1]), vec(uu[2], d.u[2])];
  return {
    xmin: x.lo, xmax: x.hi, xstep: x.st, ymin: y.lo, ymax: y.hi, ystep: y.st, zmin: z.lo, zmax: z.hi, zstep: z.st,
    gridXY: o.gridXY !== false && o.gridXY !== 0, gridXZ: o.gridXZ === true || o.gridXZ === 1, gridYZ: o.gridYZ === true || o.gridYZ === 1,
    ticks: o.ticks !== false && o.ticks !== 0, numbers: o.numbers !== false && o.numbers !== 0, arrows: o.arrows !== false && o.arrows !== 0,
    names: [names[0] ?? "x", names[1] ?? "y", names[2] ?? "z"],
    o: vec(o.o, d.o),
    u,
    n: [vec(nn[0], u[0]), vec(nn[1], u[1]), vec(nn[2], u[2])], // a space saved before the halves were independent has straight axes
  };
}

export const encodeSpace3 = (s: Space3Spec): string => JSON.stringify(s);
export function decodeSpace3(text: string | null): Space3Spec | null {
  if (!text) return null;
  try { return sanitizeSpace3(JSON.parse(text)); } catch { return null; }
}

/** Page position of the point (x, y, z) of the space. */
export function project(s: Space3Spec, x: number, y: number, z: number): V2 {
  const c = [x, y, z];
  let px = s.o[0], py = s.o[1];
  for (let i = 0; i < 3; i++) {
    const v = c[i]!, w = v >= 0 ? s.u[i]! : s.n[i]!; // each half of an axis has its own vector
    px += v * w[0];
    py += v * w[1];
  }
  return [px, py];
}

const range = (s: Space3Spec, i: 0 | 1 | 2): [number, number, number] => (i === 0 ? [s.xmin, s.xmax, s.xstep] : i === 1 ? [s.ymin, s.ymax, s.ystep] : [s.zmin, s.zmax, s.zstep]);
/** Where the other axes cross an axis: at 0 when the range has it, else at the range's nearer end. */
const crossing = (s: Space3Spec, i: 0 | 1 | 2) => { const [lo, hi] = range(s, i); return clamp(0, lo, hi); };
function axisPoint(s: Space3Spec, i: 0 | 1 | 2, v: number): V2 {
  const c = [crossing(s, 0), crossing(s, 1), crossing(s, 2)];
  c[i] = v;
  return project(s, c[0]!, c[1]!, c[2]!);
}

/** The anchors: the end of each half of each axis (a half that the range does not have gets none). */
export type AnchorSide = "+" | "-";
export function anchorsOf(s: Space3Spec): Array<{ axis: 0 | 1 | 2; side: AnchorSide; at: V2; value: number }> {
  const out: Array<{ axis: 0 | 1 | 2; side: AnchorSide; at: V2; value: number }> = [];
  for (const i of [0, 1, 2] as const) {
    const [lo, hi] = range(s, i), c = crossing(s, i);
    if (hi > c) out.push({ axis: i, side: "+", at: axisPoint(s, i, hi), value: hi });
    if (lo < c) out.push({ axis: i, side: "-", at: axisPoint(s, i, lo), value: lo });
  }
  return out;
}

/** The spec with the end of one half of axis `i` moved to page point `p`: that half's angle and length follow; with `keepLength` only the angle does. */
export function withAnchor(s: Space3Spec, i: 0 | 1 | 2, side: AnchorSide, p: V2, keepLength: boolean): Space3Spec {
  const a = anchorsOf(s).find((x) => x.axis === i && x.side === side);
  if (!a) return s;
  // where the crossing is, without this axis's own share: the end of the half is that point plus value * (the half's vector)
  const c = [crossing(s, 0), crossing(s, 1), crossing(s, 2)];
  let bx = s.o[0], by = s.o[1];
  for (const j of [0, 1, 2] as const) {
    if (j === i) continue;
    const w = c[j]! >= 0 ? s.u[j]! : s.n[j]!;
    bx += c[j]! * w[0];
    by += c[j]! * w[1];
  }
  const own = side === "+" ? s.u[i]! : s.n[i]!;
  let ux = (p[0] - bx) / a.value, uy = (p[1] - by) / a.value;
  if (keepLength) { const old = Math.hypot(own[0], own[1]), now = Math.hypot(ux, uy) || 1; ux = (ux / now) * old; uy = (uy / now) * old; }
  if (Math.hypot(ux, uy) < 2) return s; // not shorter than two points per unit: it would vanish
  const u: [V2, V2, V2] = [s.u[0], s.u[1], s.u[2]], n: [V2, V2, V2] = [s.n[0], s.n[1], s.n[2]];
  if (side === "+") u[i] = [ux, uy]; else n[i] = [ux, uy];
  return { ...s, u, n };
}

function fmtNum(v: number): string { const t = String(round6(v)); return t === "-0" ? "0" : t.replace("-", "−"); }

function strokeEl(pts: number[], color: string, alpha: string, width: number, role: string, id: string, glow?: PlaneStyle["glow"]): Stroke {
  const colorAttr = color + alpha;
  const st: Stroke = {
    kind: "stroke",
    attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(+width.toFixed(3))], ["capStyle", "round"], ["xlPlane", id], ["xlRole", role]],
    tool: "pen", color: cssColor(colorAttr), widths: [width], pts, cap: "round", dash: null, bbox: bboxOf(pts),
  };
  if (glow) setGlow(st, glow);
  return st;
}

function textEl(text: string, at: V2, size: number, color: string, id: string): TextBox {
  const t: TextBox = { kind: "text", attrs: [["xlPlane", id], ["xlRole", "label"]], text, font: "Sans", size, x: 0, y: 0, color: cssColor(color + "ff") };
  const b = textBBox(t);
  t.x = at[0] - (b[2] - b[0]) / 2;
  t.y = at[1] - (b[3] - b[1]) / 2;
  stampText(t);
  return t;
}

function multiples(min: number, max: number, step: number): number[] {
  const out: number[] = [];
  for (let n = Math.round(min / step); n <= Math.round(max / step); n++) out.push(round6(n * step));
  return out;
}

/** Build the pieces of a 3D space (all of them, labelled as a plane's pieces so they stay together). */
export function buildSpace3(id: string, rawSpec: Space3Spec, style: PlaneStyle): PageElement[] {
  const s = sanitizeSpace3(rawSpec);
  const w = Math.max(0.3, style.width), color = style.color;
  const out: PageElement[] = [];
  const cross = [crossing(s, 0), crossing(s, 1), crossing(s, 2)];

  // grids on the coordinate planes: one zig-zag stroke per direction, its connectors run along the plane's edge (also a grid line)
  const planes: Array<[boolean, 0 | 1 | 2, 0 | 1 | 2]> = [[s.gridXY, 0, 1], [s.gridXZ, 0, 2], [s.gridYZ, 1, 2]];
  for (const [on, a, b] of planes) {
    if (!on) continue;
    const third = ([0, 1, 2] as const).find((k) => k !== a && k !== b)!;
    const [alo, ahi, ast] = range(s, a), [blo, bhi, bst] = range(s, b);
    const pt = (va: number, vb: number): V2 => { const c = [...cross]; c[a] = va; c[b] = vb; c[third] = cross[third]!; return project(s, c[0]!, c[1]!, c[2]!); };
    const along = (vals: number[], fixedIsA: boolean): number[] => {
      const pts: number[] = [];
      vals.forEach((v, i) => {
        const from = fixedIsA ? (i % 2 ? bhi : blo) : (i % 2 ? ahi : alo), to = fixedIsA ? (i % 2 ? blo : bhi) : (i % 2 ? alo : ahi);
        const p0 = fixedIsA ? pt(v, from) : pt(from, v), p1 = fixedIsA ? pt(v, to) : pt(to, v);
        pts.push(p0[0], p0[1], p1[0], p1[1]);
      });
      return pts;
    };
    const as = multiples(alo, ahi, ast), bs = multiples(blo, bhi, bst);
    if (as.length > 1) out.push(strokeEl(along(as, true), color, "55", Math.max(0.3, w * 0.45), "grid3", id));
    if (bs.length > 1) out.push(strokeEl(along(bs, false), color, "55", Math.max(0.3, w * 0.45), "grid3", id));
  }

  const tl = clamp(w * 0.9 + 1.2, 2, 3.6), head = clamp(w * 1.4 + 3.5, 4.5, 7.5);
  const tickW = Math.max(0.3, w * 0.6), arrowW = Math.max(0.3, w * 0.8);
  const roles = ["axis3x", "axis3y", "axis3z"];
  const labels: PageElement[] = [];
  const ticks: PageElement[] = [], arrows: PageElement[] = [];
  const letters = s.names;
  const normalOf = (d: V2): V2 => { // a unit normal of the axis on the page, pointing down or to the left
    const l = Math.hypot(d[0], d[1]) || 1;
    let n: V2 = [-d[1] / l, d[0] / l];
    if (n[1] < -0.3 || (Math.abs(n[1]) <= 0.3 && n[0] > 0)) n = [-n[0], -n[1]];
    return n;
  };
  ([0, 1, 2] as const).forEach((i) => {
    const [lo, hi, st] = range(s, i);
    const p0 = axisPoint(s, i, lo), p1 = axisPoint(s, i, hi), pc = axisPoint(s, i, cross[i]!);
    const axis = strokeEl([p0[0], p0[1], pc[0], pc[1], p1[0], p1[1]], color, "ff", w, roles[i]!, id, style.glow); // through the crossing: the halves can have different directions
    if (i === 0) axis.attrs.push(["xlSpace3", encodeSpace3(s)], ["xlPlaneW", String(+w.toFixed(3))]);
    out.push(axis);
    const dir: V2 = hi > cross[i]! ? [p1[0] - pc[0], p1[1] - pc[1]] : [p1[0] - p0[0], p1[1] - p0[1]]; // the way the arrow points: along the positive half
    const dl = Math.hypot(dir[0], dir[1]) || 1;
    const nrm = normalOf(dir);
    const vals = multiples(lo, hi, st).filter((v) => v !== cross[i] && v !== lo && v !== hi);
    if (s.ticks && vals.length) {
      const pts: number[] = [];
      for (const v of vals) { const p = axisPoint(s, i, v); pts.push(p[0], p[1], p[0] - nrm[0] * tl, p[1] - nrm[1] * tl, p[0] + nrm[0] * tl, p[1] + nrm[1] * tl, p[0], p[1]); }
      ticks.push(strokeEl(pts, color, "ff", tickW, "ticks", id, style.glow));
    }
    if (s.arrows) {
      const ux = dir[0] / dl, uy = dir[1] / dl;
      arrows.push(strokeEl([p1[0] - ux * head - uy * head * 0.38, p1[1] - uy * head + ux * head * 0.38, p1[0], p1[1], p1[0] - ux * head + uy * head * 0.38, p1[1] - uy * head - ux * head * 0.38], color, "ff", arrowW, "arrow", id, style.glow));
    }
    const stepPx = Math.hypot(s.u[i][0], s.u[i][1]) * st;
    const size = clamp(stepPx * 0.34, 6.5, 11);
    if (s.numbers) {
      const every = Math.max(1, Math.ceil((size * 2.2) / Math.max(stepPx, 1)));
      multiples(lo, hi, st).forEach((v, k) => {
        if (v === cross[i] || k % every) return;
        const p = axisPoint(s, i, v);
        labels.push(textEl(fmtNum(v), [p[0] + nrm[0] * (tl + size * 0.7), p[1] + nrm[1] * (tl + size * 0.7)], size, color, id));
      });
    }
    if (letters[i]) labels.push(textEl(letters[i]!, [p1[0] + (dir[0] / dl) * (head + 7), p1[1] + (dir[1] / dl) * (head + 7)], clamp(size * 1.2, 8, 13), color, id));
  });
  return [...out, ...ticks, ...arrows, ...labels];
}

/** The members of space `id` that are axes, in x, y, z order. */
function axesOf(members: PageElement[]): Array<Stroke | undefined> {
  return ["axis3x", "axis3y", "axis3z"].map((r) => members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === r));
}

export function isSpace3(members: PageElement[]): boolean { return axesOf(members).every(Boolean); }

/** The settings stored in a space's axis, with the geometry read back from where the axes are now. */
export function space3SpecOf(members: PageElement[]): Space3Spec | null {
  const axes = axesOf(members);
  const ax = axes[0];
  if (!axes.every(Boolean) || !ax) return null;
  const stored = decodeSpace3(attrValue(ax.attrs, "xlSpace3"));
  if (!stored) return null;
  const u: V2[] = [], n: V2[] = [];
  let cross: V2 = [0, 0];
  ([0, 1, 2] as const).forEach((i) => {
    const p = axes[i]!.pts; // lowest end, crossing, highest end
    const [lo, hi] = range(stored, i), c = crossing(stored, i);
    cross = [p[2]!, p[3]!];
    u.push(hi > c ? [(p[4]! - p[2]!) / (hi - c), (p[5]! - p[3]!) / (hi - c)] : stored.u[i]!);
    n.push(lo < c ? [(p[0]! - p[2]!) / (lo - c), (p[1]! - p[3]!) / (lo - c)] : stored.n[i]!);
  });
  const geo: Space3Spec = { ...stored, u: [u[0]!, u[1]!, u[2]!], n: [n[0]!, n[1]!, n[2]!], o: [0, 0] };
  // the origin: the crossing point minus the share of each axis at its crossing value
  let ox = cross[0], oy = cross[1];
  for (const i of [0, 1, 2] as const) {
    const c = crossing(stored, i), w = c >= 0 ? geo.u[i]! : geo.n[i]!;
    ox -= c * w[0];
    oy -= c * w[1];
  }
  geo.o = [ox, oy];
  return geo;
}

export function space3StyleOf(members: PageElement[]): PlaneStyle | null {
  const ax = axesOf(members)[0];
  if (!ax) return null;
  const hex = (attrValue(ax.attrs, "color") ?? "#000000ff").slice(0, 7);
  const w = Number(attrValue(ax.attrs, "xlPlaneW"));
  return { color: hex, width: Number.isFinite(w) && w > 0 ? w : ax.widths[0] ?? 1.41, ...(ax.glow ? { glow: { ...ax.glow } } : {}) };
}

export function space3Members(layer: Layer, id: string): PageElement[] { return planeMembers(layer, id); }
export const space3IdOf = planeIdOf;

/** A spec whose drawing fits `scale` times the default size, with its bounding box's top-left corner at (x, y). */
export function placedSpec(base: Space3Spec, scale: number, corner: V2): Space3Spec {
  const grow = (a: [V2, V2, V2]) => a.map((v) => [v[0] * scale, v[1] * scale] as V2) as [V2, V2, V2];
  const probe = { ...base, o: [0, 0] as V2, u: grow(base.u), n: grow(base.n) };
  const b = extentOf(probe);
  return { ...probe, o: [corner[0] - b[0], corner[1] - b[1]] };
}

/** Page bounding box (x0, y0, x1, y1) of the axes and grids of a spec. */
export function extentOf(s: Space3Spec): [number, number, number, number] {
  const pts: number[] = [];
  for (const x of [s.xmin, s.xmax]) for (const y of [s.ymin, s.ymax]) for (const z of [s.zmin, s.zmax]) pts.push(...project(s, x, y, z));
  return bboxOf(pts);
}
