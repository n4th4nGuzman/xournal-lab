/**
 * Diagrams of two or three sets (Venn): a universe, circles, the elements written in each set, and the region an expression shades.
 * The shading is a diagonal hatching computed exactly: along each line of the hatching, the stretches inside each circle are found,
 * and the stretches that belong to a shaded region are the ones in the circles of its mask and outside the others. No fills are
 * needed, so the drawing is ordinary pen strokes and texts.
 */
import { attrValue, type PageElement, type Stroke } from "./model";
import { complement, intersect, normalize, piece, union, type IntervalSet } from "./intervals";
import { evalRegions, parseSetExpr } from "./setexpr";
import { SET_ATTR } from "./numberline";
import { affineFrom, findWitness, makeWitness, mapPoints, strokeEl, textEl, type GenStyle, type V2 } from "./genobj";
import { roleOf } from "./plane";

export interface VennSpec {
  kind: "venn";
  /** Top left and bottom right corners of the universe, then for each set its centre and a point on its circle. */
  pts: number[];
  k: 2 | 3;
  names: string[];
  /** Which region is shaded, written as an expression of the names (A ∩ B', A ∖ B, (A ∪ B)'…). */
  expr: string;
  universe: boolean;
  hatch: "light" | "dense";
  /** The elements written in each set ("1, 2, 3"), then the ones outside every set. An element in two lists is in both sets. */
  lists: string[];
}

const MAX_LINES = 400;

export function defaultVenn(k: 2 | 3 = 2, a: V2 = [100, 100], b: V2 = [500, 360]): VennSpec {
  const w = b[0] - a[0], h = b[1] - a[1];
  const s = Math.min(w * 0.5, h * (k === 2 ? 0.8 : 0.62));
  const r = k === 2 ? s * 0.5 : s * 0.5;
  const mid: V2 = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
  const centres: V2[] = k === 2
    ? [[mid[0] - r * 0.62, mid[1]], [mid[0] + r * 0.62, mid[1]]]
    : [[mid[0] - r * 0.6, mid[1] - r * 0.4], [mid[0] + r * 0.6, mid[1] - r * 0.4], [mid[0], mid[1] + r * 0.62]];
  const pts = [a[0], a[1], b[0], b[1]];
  for (const c of centres) {
    const d = Math.hypot(c[0] - mid[0], c[1] - mid[1]) || 1;
    const dir: V2 = d < 1e-6 ? [0.7071, -0.7071] : [(c[0] - mid[0]) / d, (c[1] - mid[1]) / d];
    pts.push(c[0], c[1], c[0] + dir[0] * r, c[1] + dir[1] * r);
  }
  return { kind: "venn", pts, k, names: ["A", "B", "C"], expr: "", universe: true, hatch: "light", lists: ["", "", "", ""].slice(0, k + 1) };
}

export function sanitizeVenn(raw: unknown): VennSpec {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const k: 2 | 3 = o.k === 3 ? 3 : 2;
  const d = defaultVenn(k);
  const need = 4 + 4 * k;
  const pts = Array.isArray(o.pts) && o.pts.length >= need && (o.pts as unknown[]).slice(0, need).every((v) => typeof v === "number" && Number.isFinite(v)) ? (o.pts as number[]).slice(0, need) : d.pts;
  const names = Array.isArray(o.names) ? (o.names as unknown[]).slice(0, 3).map((n, i) => (typeof n === "string" && /^[\p{L}][\p{L}\d_]{0,3}$/u.test(n.trim()) ? n.trim() : d.names[i]!)) : d.names;
  while (names.length < 3) names.push(d.names[names.length]!);
  const lists = Array.isArray(o.lists) ? (o.lists as unknown[]).slice(0, k + 1).map((l) => (typeof l === "string" ? l.slice(0, 300) : "")) : [];
  while (lists.length < k + 1) lists.push("");
  return { kind: "venn", pts, k, names, expr: typeof o.expr === "string" ? o.expr.slice(0, 200) : "", universe: o.universe !== false, hatch: o.hatch === "dense" ? "dense" : "light", lists };
}

interface Circle { c: V2; r: number }
function circlesOf(spec: VennSpec): Circle[] {
  const out: Circle[] = [];
  for (let i = 0; i < spec.k; i++) {
    const cx = spec.pts[4 + 4 * i]!, cy = spec.pts[5 + 4 * i]!, rx = spec.pts[6 + 4 * i]!, ry = spec.pts[7 + 4 * i]!;
    out.push({ c: [cx, cy], r: Math.max(8, Math.hypot(rx - cx, ry - cy)) });
  }
  return out;
}
function rectOf(spec: VennSpec): [number, number, number, number] {
  const [ax, ay, bx, by] = spec.pts as [number, number, number, number];
  return [Math.min(ax, bx), Math.min(ay, by), Math.max(ax, bx), Math.max(ay, by)];
}

/** Which circles a point is inside, as bits (0 is outside all of them). */
export function maskAt(spec: VennSpec, p: V2): number {
  let m = 0;
  circlesOf(spec).forEach((ci, i) => { if (Math.hypot(p[0] - ci.c[0], p[1] - ci.c[1]) < ci.r) m |= 1 << i; });
  return m;
}

/** The regions the expression shades (or null when there is none or it has a mistake, with the mistake). */
export function shadedRegions(spec: VennSpec): { shaded: boolean[] | null; error: string | null } {
  if (!spec.expr.trim()) return { shaded: null, error: null };
  const p = parseSetExpr(spec.expr);
  if (!p.ok) return { shaded: null, error: p.msg };
  const e = evalRegions(p.expr, spec.names.slice(0, spec.k));
  return e.ok ? { shaded: e.value, error: null } : { shaded: null, error: e.msg };
}

const splitList = (s: string) => s.split(/[,;\n]/).map((x) => x.trim()).filter(Boolean);

/** The elements with the region they belong to: the lists of the sets they are in (an element in two lists is in both). */
export function elementsOf(spec: VennSpec): Array<{ label: string; mask: number }> {
  const masks = new Map<string, number>();
  for (let i = 0; i < spec.k; i++) for (const e of splitList(spec.lists[i] ?? "")) masks.set(e, (masks.get(e) ?? 0) | (1 << i));
  for (const e of splitList(spec.lists[spec.k] ?? "")) if (!masks.has(e)) masks.set(e, 0);
  return [...masks].map(([label, mask]) => ({ label, mask }));
}

/** The interval of t where the line P(t) = base + t·v is inside the circle. */
function chord(base: V2, v: V2, c: Circle): IntervalSet {
  const bx = base[0] - c.c[0], by = base[1] - c.c[1];
  const b = bx * v[0] + by * v[1], cc = bx * bx + by * by - c.r * c.r, disc = b * b - cc;
  return disc > 0 ? normalize([piece(-b - Math.sqrt(disc), -b + Math.sqrt(disc), true, true)]) : [];
}

/** The strokes of the hatching: [t0, t1] of each stretch on each line, as pairs of points. */
export function hatchSegments(spec: VennSpec, shaded: boolean[]): number[][] {
  const [x0, y0, x1, y1] = rectOf(spec);
  const circles = circlesOf(spec);
  const gap = spec.hatch === "dense" ? 5 : 9;
  let d = gap;
  if ((x1 - x0 + (y1 - y0)) / d > MAX_LINES) d = (x1 - x0 + (y1 - y0)) / MAX_LINES;
  const v: V2 = [Math.SQRT1_2, -Math.SQRT1_2]; // lines go up to the right, x + y = c
  const out: number[][] = [];
  const masks = shaded.map((s, m) => (s ? m : -1)).filter((m) => m >= 0);
  if (!masks.length) return out;
  const cMin = x0 + y0;
  for (let c = cMin + d / 2; c < x1 + y1; c += d) {
    const base: V2 = [c / 2, c / 2];
    // the stretch of the line inside the universe
    let tLo = -Infinity, tHi = Infinity;
    for (const [axis, lo, hi] of [[0, x0, x1], [1, y0, y1]] as Array<[0 | 1, number, number]>) {
      const bv = base[axis], vv = v[axis];
      const a = (lo - bv) / vv, b = (hi - bv) / vv;
      tLo = Math.max(tLo, Math.min(a, b)); tHi = Math.min(tHi, Math.max(a, b));
    }
    if (!(tHi > tLo)) continue;
    const universe = normalize([piece(tLo, tHi, true, true)]);
    const inside = circles.map((ci) => chord(base, v, ci));
    let total: IntervalSet = [];
    for (const m of masks) {
      let part = universe;
      circles.forEach((_, i) => { part = intersect(part, (m >> i & 1) === 1 ? inside[i]! : complement(inside[i]!)); });
      total = union(total, part);
    }
    for (const p of total) if (p.hi - p.lo > 0.5) out.push([base[0] + v[0] * p.lo, base[1] + v[1] * p.lo, base[0] + v[0] * p.hi, base[1] + v[1] * p.hi]);
  }
  return out;
}

/** Where each element goes: the point of its region farthest from the borders, then the next ones farthest from both borders and each other. */
export function placeElements(spec: VennSpec): Array<{ label: string; at: V2 }> {
  const els = elementsOf(spec);
  const [x0, y0, x1, y1] = rectOf(spec);
  const circles = circlesOf(spec);
  const step = Math.max(3, Math.min(x1 - x0, y1 - y0) / 60);
  const dist = (p: V2) => {
    let d = Math.min(p[0] - x0, x1 - p[0], p[1] - y0, y1 - p[1]);
    for (const ci of circles) d = Math.min(d, Math.abs(Math.hypot(p[0] - ci.c[0], p[1] - ci.c[1]) - ci.r));
    return d;
  };
  const samples = new Map<number, Array<{ p: V2; d: number }>>();
  for (let x = x0 + step / 2; x < x1; x += step) for (let y = y0 + step / 2; y < y1; y += step) {
    const p: V2 = [x, y], m = maskAt(spec, p);
    const list = samples.get(m) ?? [];
    list.push({ p, d: dist(p) });
    samples.set(m, list);
  }
  const placed = new Map<number, V2[]>();
  const out: Array<{ label: string; at: V2 }> = [];
  for (const e of els) {
    const cands = samples.get(e.mask) ?? [];
    const mine = placed.get(e.mask) ?? [];
    let best: V2 | null = null, bestScore = -1;
    for (const s of cands) {
      const near = mine.length ? Math.min(...mine.map((q) => Math.hypot(q[0] - s.p[0], q[1] - s.p[1]))) : Infinity;
      const score = Math.min(s.d, near * 0.55);
      if (score > bestScore) { bestScore = score; best = s.p; }
    }
    if (!best) best = [(x0 + x1) / 2, (y0 + y1) / 2]; // a region that is not on the page: the middle
    mine.push(best);
    placed.set(e.mask, mine);
    out.push({ label: e.label, at: best });
  }
  return out;
}

function polygon(c: V2, r: number): number[] {
  const out: number[] = [];
  const n = Math.max(36, Math.min(120, Math.round(r / 2)));
  for (let i = 0; i <= n; i++) out.push(c[0] + Math.cos((i / n) * Math.PI * 2) * r, c[1] + Math.sin((i / n) * Math.PI * 2) * r);
  return out;
}

export function buildVenn(id: string, rawSpec: VennSpec, style: GenStyle): PageElement[] {
  const spec = sanitizeVenn(rawSpec);
  const w = Math.max(0.3, style.width), color = style.color;
  const out: PageElement[] = [];
  const [x0, y0, x1, y1] = rectOf(spec);
  const circles = circlesOf(spec);
  const line = (pts: number[], role: string, width = w, alpha = "ff") => out.push(strokeEl(pts, color, alpha, width, role, id, null, style.glow));

  // the shading goes first, so the outlines are over it
  const { shaded } = shadedRegions(spec);
  if (shaded) for (const seg of hatchSegments(spec, shaded)) line(seg, "sethatch", Math.max(0.5, w * 0.5), "b0");

  if (spec.universe) { line([x0, y0, x1, y0, x1, y1, x0, y1, x0, y0], "setbody"); out.push(textEl("U", [x0 + 12, y0 + 12], 13, color, id, "setlabel")); }
  const mid: V2 = [(x0 + x1) / 2, (y0 + y1) / 2];
  circles.forEach((ci, i) => {
    line(polygon(ci.c, ci.r), "setbody");
    const dx = ci.c[0] - mid[0], dy = ci.c[1] - mid[1], d = Math.hypot(dx, dy);
    const dir: V2 = d < 1e-6 ? [0, -1] : [dx / d, dy / d];
    out.push(textEl(spec.names[i]!, [ci.c[0] + dir[0] * (ci.r + 12), ci.c[1] + dir[1] * (ci.r + 12)], 14, color, id, "setlabel"));
  });
  for (const e of placeElements(spec)) {
    line([e.at[0], e.at[1], e.at[0] + 0.01, e.at[1]], "setmark", 3.2);
    out.push(textEl(e.label, [e.at[0] + 5 + e.label.length * 3, e.at[1]], 11, color, id, "setlabel"));
  }

  const w3 = witnessOf(spec);
  out.push(makeWitness(w3, "setwitness", SET_ATTR, { kind: "venn", spec, w: w3, width: +w.toFixed(3) }, color, id));
  return out;
}

/** Three points that stand for the diagram's place: three corners of the universe. */
export function witnessOf(spec: VennSpec): number[] {
  const [x0, y0, x1, y1] = rectOf(spec);
  return [x0, y0, x1, y0, x0, y1];
}

export function vennAnchors(spec: VennSpec): V2[] {
  const out: V2[] = [];
  for (let i = 0; i + 1 < 4 + 4 * spec.k; i += 2) out.push([spec.pts[i]!, spec.pts[i + 1]!]);
  return out;
}

/** The spec with anchor `i` moved: a corner of the universe; the centre of a circle (it carries its circle); or the point that sets its radius. */
export function moveVennAnchor(spec: VennSpec, i: number, p: V2, _shift: boolean): VennSpec {
  const pts = spec.pts.slice();
  if (i < 2) {
    pts[2 * i] = p[0]; pts[2 * i + 1] = p[1];
    const [ax, ay, bx, by] = pts as [number, number, number, number];
    if (Math.abs(bx - ax) < 80 || Math.abs(by - ay) < 60) return spec;
  } else if ((i - 2) % 2 === 0) {
    const dx = p[0] - pts[2 * i]!, dy = p[1] - pts[2 * i + 1]!;
    pts[2 * i] = p[0]; pts[2 * i + 1] = p[1];
    pts[2 * i + 2] = pts[2 * i + 2]! + dx; pts[2 * i + 3] = pts[2 * i + 3]! + dy;
  } else {
    const c: V2 = [pts[2 * (i - 1)]!, pts[2 * (i - 1) + 1]!];
    if (Math.hypot(p[0] - c[0], p[1] - c[1]) < 12) return spec;
    pts[2 * i] = p[0]; pts[2 * i + 1] = p[1];
  }
  return { ...spec, pts };
}

export function readVenn(members: PageElement[]): { spec: VennSpec; style: GenStyle } | null {
  const wit = findWitness(members, "setwitness", SET_ATTR, roleOf);
  if (!wit) return null;
  let stored: { kind?: string; spec: unknown; w: number[]; width: number };
  try { stored = JSON.parse(attrValue(wit.attrs, SET_ATTR)!) as typeof stored; } catch { return null; }
  if (stored.kind !== "venn") return null;
  const spec = sanitizeVenn(stored.spec);
  const T = affineFrom(stored.w, wit.pts);
  if (!T) return null;
  spec.pts = mapPoints(T, spec.pts);
  const body = members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "setbody");
  const hex = (body ? attrValue(body.attrs, "color") ?? "#000000ff" : "#000000ff").slice(0, 7);
  return { spec, style: { color: hex, width: Number.isFinite(stored.width) && stored.width > 0 ? stored.width : 1.41, ...(body?.glow ? { glow: { ...body.glow } } : {}) } };
}
