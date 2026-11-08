/**
 * Figures for maths and physics classes: segments, arrows, polygons, curves, arcs, angles, dimension lines and wireframe
 * solids (box, pyramid, cylinder, cone, sphere). A figure is made of ordinary pen strokes and text boxes (all with
 * `xlPlane="<id>"`, so they select, move and resize as one), rebuilt from its control points when they are edited.
 *
 * The control points are the figure's anchors. They are stored in the `xlFig` attribute of an invisible "witness" stroke
 * together with three reference points; after the pieces have been moved, scaled, rotated or flipped, the affine map
 * from the stored witnesses to where they are now is applied to the stored control points, so the figure can always be
 * edited again without anything having to tell it that it moved.
 */
import { attrValue, bboxOf, cssColor, setGlow, type Glow, type PageElement, type Stroke, type TextBox } from "./model";
import { textBBox } from "./geometry";
import { affineFrom, head, mapPoints, strokeEl, textEl as genTextEl } from "./genobj";
const textEl = (text: string, at: V2, size: number, color: string, id: string) => genTextEl(text, at, size, color, id, "figlabel");
import { planeIdOf, roleOf, type PlaneStyle } from "./plane";

export type V2 = [number, number];

export type FigKind =
  | "segment" | "arrow" | "polyline" | "polygon" | "regular"
  | "bezier2" | "bezier3" | "spline" | "arc" | "angle" | "dim"
  | "box" | "pyramid" | "cylinder" | "cone" | "sphere";

export interface FigureSpec {
  kind: FigKind;
  /** Control points (x, y, ...): the anchors. See each kind in `defaultSpec`. */
  pts: number[];
  /** Arrow heads at the ends of lines and curves. */
  heads: "none" | "end" | "both";
  dash: "solid" | "dash" | "dot";
  /** Sides of a regular polygon, or meridians of a sphere. */
  n: number;
  /** Parallels of a sphere. */
  m: number;
  /** Edges hidden behind a solid: dashed, not drawn, or drawn like the rest. */
  hidden: "dash" | "none" | "solid";
  /** A text near the figure (plain text). */
  label: string;
}

export interface FigureCatalogItem { kind: FigKind; name: string; group: "Líneas" | "Curvas" | "Polígonos" | "3D"; clicks: boolean }

export const FIGURES: FigureCatalogItem[] = [
  { kind: "segment", name: "Segmento", group: "Líneas", clicks: false },
  { kind: "arrow", name: "Flecha / vector", group: "Líneas", clicks: false },
  { kind: "dim", name: "Cota", group: "Líneas", clicks: false },
  { kind: "bezier2", name: "Curva cuadrática", group: "Curvas", clicks: false },
  { kind: "bezier3", name: "Curva cúbica", group: "Curvas", clicks: false },
  { kind: "spline", name: "Curva por puntos", group: "Curvas", clicks: true },
  { kind: "arc", name: "Arco", group: "Curvas", clicks: false },
  { kind: "polyline", name: "Poligonal", group: "Polígonos", clicks: true },
  { kind: "polygon", name: "Polígono", group: "Polígonos", clicks: true },
  { kind: "regular", name: "Polígono regular", group: "Polígonos", clicks: false },
  { kind: "angle", name: "Ángulo", group: "Polígonos", clicks: false },
  { kind: "box", name: "Cubo / prisma", group: "3D", clicks: false },
  { kind: "pyramid", name: "Pirámide", group: "3D", clicks: false },
  { kind: "cylinder", name: "Cilindro", group: "3D", clicks: false },
  { kind: "cone", name: "Cono", group: "3D", clicks: false },
  { kind: "sphere", name: "Esfera", group: "3D", clicks: false },
];

export const isSolid = (k: FigKind) => k === "box" || k === "pyramid" || k === "cylinder" || k === "cone" || k === "sphere";
export const clicksKind = (k: FigKind) => k === "polyline" || k === "polygon" || k === "spline";

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const finite = (v: unknown, d: number) => (typeof v === "number" && Number.isFinite(v) ? v : d);
const round3 = (v: number) => Math.round(v * 1000) / 1000;

/** Number of control points a kind needs (min, max). */
export function pointRange(k: FigKind): [number, number] {
  switch (k) {
    case "segment": case "arrow": case "regular": return [2, 2];
    case "dim": case "bezier2": case "arc": case "angle": return [3, 3];
    case "bezier3": return [4, 4];
    case "polyline": case "spline": return [2, 200];
    case "polygon": return [3, 200];
    default: return [4, 4]; // solids: origin and the three edge tips
  }
}

export function sanitizeFigure(raw: unknown): FigureSpec {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const kind = FIGURES.some((f) => f.kind === o.kind) ? (o.kind as FigKind) : "segment";
  const [lo, hi] = pointRange(kind);
  const src = Array.isArray(o.pts) ? (o.pts as unknown[]).map((v) => finite(v, 0)) : [];
  const pts: number[] = [];
  for (let i = 0; i + 1 < src.length && pts.length / 2 < hi; i += 2) pts.push(src[i]!, src[i + 1]!);
  while (pts.length / 2 < lo) pts.push(pts[pts.length - 2] ?? 0, (pts[pts.length - 1] ?? 0) + 30 * (pts.length / 2));
  return {
    kind, pts,
    heads: o.heads === "both" || o.heads === "end" ? o.heads : kind === "arrow" ? "end" : "none",
    dash: o.dash === "dash" || o.dash === "dot" ? o.dash : "solid",
    n: Math.round(clamp(finite(o.n, kind === "regular" ? 5 : kind === "sphere" ? 8 : 4), 3, 12)),
    m: Math.round(clamp(finite(o.m, 3), 0, 9)),
    hidden: o.hidden === "none" || o.hidden === "solid" ? o.hidden : "dash",
    label: typeof o.label === "string" ? o.label.slice(0, 40) : "",
  };
}

/** The usual hand-drawn view of a solid: edge vectors for depth (down-left), width (right) and height (up). */
function viewVectors(scale: number): [V2, V2, V2] {
  const a = (deg: number, len: number): V2 => [Math.cos((deg * Math.PI) / 180) * len, -Math.sin((deg * Math.PI) / 180) * len];
  return [a(215, 30 * scale), a(0, 48 * scale), a(90, 48 * scale)];
}

/** Size (width, height) of the default figure of a kind, to fit a dragged box. */
export function defaultSize(kind: FigKind): [number, number] {
  if (isSolid(kind)) { const b = bboxOf(solidCorners(defaultSpec(kind, [0, 0], [1, 1]))); return [b[2] - b[0], b[3] - b[1]]; }
  return [120, 80];
}

/** The figure a drag from `a` to `b` (page units) makes. */
export function defaultSpec(kind: FigKind, a: V2, b: V2, shift = false): FigureSpec {
  const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.hypot(dx, dy) || 1;
  let end: V2 = b;
  if (shift) { const ang = Math.round(Math.atan2(dy, dx) / (Math.PI / 12)) * (Math.PI / 12); end = [a[0] + Math.cos(ang) * len, a[1] + Math.sin(ang) * len]; }
  const px = -(end[1] - a[1]) / len, py = (end[0] - a[0]) / len; // a unit normal of the drag
  let pts: number[] = [];
  switch (kind) {
    case "segment": case "arrow": pts = [a[0], a[1], end[0], end[1]]; break;
    case "regular": pts = [a[0], a[1], end[0], end[1]]; break;
    case "dim": pts = [a[0], a[1], end[0], end[1], (a[0] + end[0]) / 2 + px * 22, (a[1] + end[1]) / 2 + py * 22]; break;
    case "bezier2": pts = [a[0], a[1], (a[0] + end[0]) / 2 + px * len * 0.4, (a[1] + end[1]) / 2 + py * len * 0.4, end[0], end[1]]; break;
    case "bezier3": pts = [a[0], a[1], a[0] + (end[0] - a[0]) / 3 + px * len * 0.4, a[1] + (end[1] - a[1]) / 3 + py * len * 0.4, a[0] + (2 * (end[0] - a[0])) / 3 + px * len * 0.4, a[1] + (2 * (end[1] - a[1])) / 3 + py * len * 0.4, end[0], end[1]]; break;
    case "arc": pts = [a[0], a[1], end[0], end[1], a[0] - (end[1] - a[1]), a[1] + (end[0] - a[0])]; break; // 90 degrees, counter-clockwise on the page
    case "angle": { const c = Math.cos(-Math.PI / 3), s = Math.sin(-Math.PI / 3); pts = [a[0], a[1], end[0], end[1], a[0] + (end[0] - a[0]) * c - (end[1] - a[1]) * s, a[1] + (end[0] - a[0]) * s + (end[1] - a[1]) * c]; break; }
    case "polyline": case "spline": pts = [a[0], a[1], end[0], end[1]]; break;
    case "polygon": pts = [a[0], a[1], end[0], end[1], a[0] + (end[0] - a[0]) / 2 + px * len * 0.7, a[1] + (end[1] - a[1]) / 2 + py * len * 0.7]; break;
    default: { // solids: the view vectors scaled by the size of the drag
      const [e1, e2, e3] = viewVectors(Math.max(0.2, len / 90));
      pts = [a[0], a[1], a[0] + e1[0], a[1] + e1[1], a[0] + e2[0], a[1] + e2[1], a[0] + e3[0], a[1] + e3[1]];
    }
  }
  return sanitizeFigure({ kind, pts });
}

// ---------------------------------------------------------------------------------------------------------- geometry helpers
const P = (s: FigureSpec, i: number): V2 => [s.pts[2 * i]!, s.pts[2 * i + 1]!];
const flat = (a: V2[]): number[] => a.flatMap((p) => [p[0], p[1]]);

function sampleBezier(p: V2[], steps: number): V2[] {
  const out: V2[] = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps, u = 1 - t;
    if (p.length === 3) out.push([u * u * p[0]![0] + 2 * u * t * p[1]![0] + t * t * p[2]![0], u * u * p[0]![1] + 2 * u * t * p[1]![1] + t * t * p[2]![1]]);
    else out.push([u * u * u * p[0]![0] + 3 * u * u * t * p[1]![0] + 3 * u * t * t * p[2]![0] + t * t * t * p[3]![0], u * u * u * p[0]![1] + 3 * u * u * t * p[1]![1] + 3 * u * t * t * p[2]![1] + t * t * t * p[3]![1]]);
  }
  return out;
}

/** Catmull-Rom through the points (the curve passes through every one). */
function sampleSpline(p: V2[]): V2[] {
  if (p.length < 3) return p.slice();
  const out: V2[] = [];
  for (let i = 0; i < p.length - 1; i++) {
    const p0 = p[Math.max(0, i - 1)]!, p1 = p[i]!, p2 = p[i + 1]!, p3 = p[Math.min(p.length - 1, i + 2)]!;
    const steps = clamp(Math.round(Math.hypot(p2[0] - p1[0], p2[1] - p1[1]) / 4), 6, 40);
    for (let s = 0; s < steps; s++) {
      const t = s / steps, t2 = t * t, t3 = t2 * t;
      out.push([
        0.5 * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3),
        0.5 * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3),
      ]);
    }
  }
  out.push(p[p.length - 1]!);
  return out;
}

/** Angles in the figure's own sense: counter-clockwise as seen on the page (the page's y axis points down). */
const angleOf = (c: V2, p: V2) => Math.atan2(-(p[1] - c[1]), p[0] - c[0]);
const onCircle = (c: V2, r: number, a: number): V2 => [c[0] + Math.cos(a) * r, c[1] - Math.sin(a) * r];

function arcPoints(c: V2, r: number, a0: number, sweep: number): V2[] {
  const steps = clamp(Math.round((Math.abs(sweep) * 180) / Math.PI / 3), 8, 120);
  const out: V2[] = [];
  for (let i = 0; i <= steps; i++) out.push(onCircle(c, r, a0 + (sweep * i) / steps));
  return out;
}

const ccw = (from: number, to: number) => { let d = to - from; while (d <= 0) d += Math.PI * 2; return d; };

// ----------------------------------------------------------------------------------------------------------------- 3D solids
/** The page position of the point (a, b, c) of the solid's own space: the unit cube on its origin and three edge tips. */
export function solidPoint(s: FigureSpec, a: number, b: number, c: number): V2 {
  const o = P(s, 0), e1 = P(s, 1), e2 = P(s, 2), e3 = P(s, 3);
  return [o[0] + a * (e1[0] - o[0]) + b * (e2[0] - o[0]) + c * (e3[0] - o[0]), o[1] + a * (e1[1] - o[1]) + b * (e2[1] - o[1]) + c * (e3[1] - o[1])];
}

/** The direction (in the solid's own space) the solid is looked at along: faces whose outward normal has a positive dot with it are in front. */
function viewDir(s: FigureSpec): [number, number, number] | null {
  const o = P(s, 0), e1 = P(s, 1), e2 = P(s, 2), e3 = P(s, 3);
  const r1: [number, number, number] = [e1[0] - o[0], e2[0] - o[0], e3[0] - o[0]], r2: [number, number, number] = [e1[1] - o[1], e2[1] - o[1], e3[1] - o[1]];
  let d: [number, number, number] = [r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]];
  const l = Math.hypot(...d);
  if (l < 1e-9) return null;
  d = [d[0] / l, d[1] / l, d[2] / l];
  return d[0] < 0 ? [-d[0], -d[1], -d[2]] : d; // the first axis points toward the viewer, as in the default view
}

type V3 = [number, number, number];
interface Poly { verts: V3[]; faces: number[][] }

function polyhedron(kind: "box" | "pyramid"): Poly {
  if (kind === "box") {
    const verts: V3[] = [];
    for (let c = 0; c < 2; c++) for (let b = 0; b < 2; b++) for (let a = 0; a < 2; a++) verts.push([a, b, c]);
    const v = (a: number, b: number, c: number) => a + 2 * b + 4 * c;
    return { verts, faces: [[v(0, 0, 0), v(0, 1, 0), v(0, 1, 1), v(0, 0, 1)], [v(1, 0, 0), v(1, 1, 0), v(1, 1, 1), v(1, 0, 1)], [v(0, 0, 0), v(1, 0, 0), v(1, 0, 1), v(0, 0, 1)], [v(0, 1, 0), v(1, 1, 0), v(1, 1, 1), v(0, 1, 1)], [v(0, 0, 0), v(1, 0, 0), v(1, 1, 0), v(0, 1, 0)], [v(0, 0, 1), v(1, 0, 1), v(1, 1, 1), v(0, 1, 1)]] };
  }
  return { verts: [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0.5, 0.5, 1]], faces: [[0, 1, 2, 3], [0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]] };
}

/** Faces' outward normals (the solid is convex, so outward is away from its centroid). */
function normals(p: Poly): V3[] {
  const cen: V3 = [0, 0, 0];
  for (const v of p.verts) { cen[0] += v[0] / p.verts.length; cen[1] += v[1] / p.verts.length; cen[2] += v[2] / p.verts.length; }
  return p.faces.map((f) => {
    const a = p.verts[f[0]!]!, b = p.verts[f[1]!]!, c = p.verts[f[2]!]!;
    const u: V3 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], w: V3 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    let n: V3 = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
    const fc: V3 = [0, 0, 0];
    for (const i of f) { fc[0] += p.verts[i]![0] / f.length; fc[1] += p.verts[i]![1] / f.length; fc[2] += p.verts[i]![2] / f.length; }
    if (n[0] * (fc[0] - cen[0]) + n[1] * (fc[1] - cen[1]) + n[2] * (fc[2] - cen[2]) < 0) n = [-n[0], -n[1], -n[2]];
    const l = Math.hypot(...n) || 1;
    return [n[0] / l, n[1] / l, n[2] / l];
  });
}

/** Corners of the solid's bounding box on the page (for sizing). */
export function solidCorners(s: FigureSpec): number[] {
  const out: number[] = [];
  for (const a of [0, 1]) for (const b of [0, 1]) for (const c of [0, 1]) out.push(...solidPoint(s, a, b, c));
  return out;
}

interface Lines { solid: number[][]; hidden: number[][] }

/** Cut a path of 3D points into runs in front of and behind the silhouette (by the sign of `depth`); runs meet at the crossing. */
function splitByDepth(path: V3[], depth: (p: V3) => number, toPage: (p: V3) => V2): { front: number[][]; back: number[][] } {
  const front: number[][] = [], back: number[][] = [];
  let run: number[] = [], inFront = depth(path[0]!) >= 0;
  const flush = () => { if (run.length >= 4) (inFront ? front : back).push(run); run = []; };
  path.forEach((p, i) => {
    const dp = depth(p), f = dp >= 0;
    if (i > 0 && f !== inFront) {
      const q = path[i - 1]!, dq = depth(q), t = dq / (dq - dp);
      const x = toPage([q[0] + (p[0] - q[0]) * t, q[1] + (p[1] - q[1]) * t, q[2] + (p[2] - q[2]) * t]);
      run.push(x[0], x[1]);
      flush();
      inFront = f;
      run = [x[0], x[1]];
    }
    const pg = toPage(p);
    run.push(pg[0], pg[1]);
  });
  flush();
  return { front, back };
}

function solidLines(s: FigureSpec): Lines {
  const solid: number[][] = [], hidden: number[][] = [];
  const d = viewDir(s);
  const pp = (p: V3): V2 => solidPoint(s, p[0], p[1], p[2]);
  if (s.kind === "box" || s.kind === "pyramid") {
    const poly = polyhedron(s.kind), nrm = normals(poly);
    const vis = nrm.map((n) => (d ? n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > 1e-9 : true));
    const edges = new Map<string, { a: number; b: number; faces: number[] }>();
    poly.faces.forEach((f, fi) => f.forEach((v, k) => {
      const w = f[(k + 1) % f.length]!;
      const key = v < w ? `${v}-${w}` : `${w}-${v}`;
      const e = edges.get(key) ?? { a: Math.min(v, w), b: Math.max(v, w), faces: [] };
      e.faces.push(fi);
      edges.set(key, e);
    }));
    for (const e of edges.values()) {
      const line = flat([pp(poly.verts[e.a]!), pp(poly.verts[e.b]!)]);
      (e.faces.some((fi) => vis[fi]) ? solid : hidden).push(line);
    }
    return { solid, hidden };
  }
  const dd: V3 = d ?? [1, 0, 0];
  // Round solids live in the unit box, like the cube: a circle of radius 1/2 centred on (1/2, 1/2); the sphere's centre is (1/2, 1/2, 1/2).
  // Their own coordinates are centred (x, y in the unit circle), and `depth` is the signed distance toward the viewer from the centre.
  const side = (p: V3) => p[0] * dd[0] + p[1] * dd[1];
  const circle = (h: number, steps = 72): V3[] => {
    const out: V3[] = [];
    for (let i = 0; i <= steps; i++) { const t = (Math.PI * 2 * i) / steps; out.push([Math.cos(t), Math.sin(t), h]); }
    return out;
  };
  if (s.kind === "cylinder" || s.kind === "cone") {
    const pc = (p: V3): V2 => solidPoint(s, 0.5 + 0.5 * p[0], 0.5 + 0.5 * p[1], p[2]);
    const cone = s.kind === "cone";
    const topFaces = dd[2] > 1e-9; // the top cap faces the viewer (otherwise the base does)
    const addCircle = (h: number, capFacesViewer: boolean) => {
      const path = circle(h);
      if (capFacesViewer) { solid.push(flat(path.map(pc))); return; }
      const { front, back } = splitByDepth(path, side, pc);
      solid.push(...front);
      hidden.push(...back);
    };
    addCircle(0, !topFaces);
    if (!cone) addCircle(1, topFaces);
    if (!cone) {
      const th = Math.atan2(-dd[0], dd[1]); // where the side is edge-on: depth zero
      for (const t of [th, th + Math.PI]) solid.push(flat([pc([Math.cos(t), Math.sin(t), 0]), pc([Math.cos(t), Math.sin(t), 1])]));
    } else {
      // the two lines from the apex tangent to the base ellipse (when the apex is outside it, as seen on the page)
      const apex = pc([0, 0, 1]), c0 = pc([0, 0, 0]), ex = pc([1, 0, 0]), ey = pc([0, 1, 0]);
      const m = [[ex[0] - c0[0], ey[0] - c0[0]], [ex[1] - c0[1], ey[1] - c0[1]]];
      const det = m[0]![0]! * m[1]![1]! - m[0]![1]! * m[1]![0]!;
      if (Math.abs(det) > 1e-9) {
        const rx = apex[0] - c0[0], ry = apex[1] - c0[1];
        const ax = (m[1]![1]! * rx - m[0]![1]! * ry) / det, ay = (-m[1]![0]! * rx + m[0]![0]! * ry) / det; // the apex in the base circle's own plane
        const dist = Math.hypot(ax, ay);
        if (dist > 1.0001) {
          const al = Math.atan2(ay, ax), dl = Math.acos(1 / dist);
          for (const t of [al - dl, al + dl]) solid.push(flat([pc([Math.cos(t), Math.sin(t), 0]), apex]));
        }
      }
    }
    return { solid, hidden };
  }
  // sphere: meridians (great circles through the poles), parallels and the outline
  const ps = (p: V3): V2 => solidPoint(s, 0.5 + 0.5 * p[0], 0.5 + 0.5 * p[1], 0.5 + 0.5 * p[2]);
  const depth3 = (p: V3) => p[0] * dd[0] + p[1] * dd[1] + p[2] * dd[2];
  const addPath = (path: V3[]) => { const { front, back } = splitByDepth(path, depth3, ps); solid.push(...front); hidden.push(...back); };
  addPath(circle(0)); // the equator
  for (let j = 1; j <= s.m; j++) { const phi = -Math.PI / 2 + (Math.PI * j) / (s.m + 1); if (Math.abs(phi) < 1e-6) continue; const r = Math.cos(phi), z = Math.sin(phi); addPath(circle(0, 72).map((p): V3 => [p[0] * r, p[1] * r, z])); }
  for (let k = 0; k < s.n; k++) {
    const th = (Math.PI * k) / s.n;
    const path: V3[] = [];
    for (let i = 0; i <= 72; i++) { const t = (Math.PI * 2 * i) / 72; path.push([Math.cos(t) * Math.cos(th), Math.cos(t) * Math.sin(th), Math.sin(t)]); }
    addPath(path);
  }
  // the outline: the shadow of the ball, an ellipse (the 2x3 map applied to the unit ball)
  const c = ps([0, 0, 0]), x1 = ps([1, 0, 0]), y1 = ps([0, 1, 0]), z1 = ps([0, 0, 1]);
  const a = [[x1[0] - c[0], y1[0] - c[0], z1[0] - c[0]], [x1[1] - c[1], y1[1] - c[1], z1[1] - c[1]]] as const;
  const sxx = a[0][0] ** 2 + a[0][1] ** 2 + a[0][2] ** 2, syy = a[1][0] ** 2 + a[1][1] ** 2 + a[1][2] ** 2, sxy = a[0][0] * a[1][0] + a[0][1] * a[1][1] + a[0][2] * a[1][2];
  const tr = sxx + syy, det = sxx * syy - sxy * sxy, disc = Math.sqrt(Math.max(0, (tr * tr) / 4 - det));
  const l1 = tr / 2 + disc, l2 = Math.max(0, tr / 2 - disc);
  const ang = 0.5 * Math.atan2(2 * sxy, sxx - syy);
  const outline: number[] = [];
  for (let i = 0; i <= 96; i++) {
    const t = (Math.PI * 2 * i) / 96;
    const u = Math.cos(t) * Math.sqrt(l1), v = Math.sin(t) * Math.sqrt(l2);
    outline.push(c[0] + u * Math.cos(ang) - v * Math.sin(ang), c[1] + u * Math.sin(ang) + v * Math.cos(ang));
  }
  solid.push(outline);
  return { solid, hidden };
}

// --------------------------------------------------------------------------------------------------------------- building
/** The three points that stand for the figure's position: its first control points, moved off a line if they are on one. */
export function witnessOf(s: FigureSpec): number[] {
  const a = P(s, 0), b = P(s, 1);
  let c: V2 = s.pts.length >= 6 ? P(s, 2) : [a[0], a[1]];
  const area = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
  const span = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
  if (area < span * 3) c = [a[0] - ((b[1] - a[1]) / span) * 4, a[1] + ((b[0] - a[0]) / span) * 4]; // a point just off the line through the first two (so the selection box does not grow)
  return [a[0], a[1], b[0], b[1], c[0], c[1]];
}

export function buildFigure(id: string, rawSpec: FigureSpec, style: PlaneStyle): PageElement[] {
  const s = sanitizeFigure(rawSpec);
  const w = Math.max(0.3, style.width), color = style.color;
  const out: PageElement[] = [];
  const dash = s.dash;
  const body = (pts: number[], role = "figbody") => out.push(strokeEl(pts, color, "ff", w, role, id, dash, style.glow));
  const arrowW = Math.max(0.3, w * 0.9);
  const headLen = clamp(w * 3 + 5, 6, 14);
  const addHeads = (path: V2[]) => {
    if (s.heads === "none" || path.length < 2) return;
    const last = path[path.length - 1]!, prev = path[Math.max(0, path.length - 4)]!;
    if (s.heads === "end" || s.heads === "both") out.push(strokeEl(head(last, prev, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow));
    if (s.heads === "both") out.push(strokeEl(head(path[0]!, path[Math.min(path.length - 1, 3)]!, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow));
  };
  const labelAt = (at: V2) => { if (s.label) out.push(textEl(s.label, at, 12, color, id)); };
  const pts: V2[] = Array.from({ length: s.pts.length / 2 }, (_, i) => P(s, i));

  switch (s.kind) {
    case "segment": case "arrow": {
      body(flat(pts));
      addHeads(pts);
      labelAt([(pts[0]![0] + pts[1]![0]) / 2 + 10, (pts[0]![1] + pts[1]![1]) / 2 - 10]);
      break;
    }
    case "polyline": case "polygon": {
      const path = s.kind === "polygon" ? [...pts, pts[0]!] : pts;
      body(flat(path));
      if (s.kind === "polyline") addHeads(path);
      labelAt([pts.reduce((a, p) => a + p[0], 0) / pts.length, pts.reduce((a, p) => a + p[1], 0) / pts.length]);
      break;
    }
    case "regular": {
      const c = pts[0]!, v0 = pts[1]!;
      const verts: V2[] = [];
      for (let k = 0; k < s.n; k++) { const t = (2 * Math.PI * k) / s.n; verts.push([c[0] + (v0[0] - c[0]) * Math.cos(t) - (v0[1] - c[1]) * Math.sin(t), c[1] + (v0[0] - c[0]) * Math.sin(t) + (v0[1] - c[1]) * Math.cos(t)]); }
      body(flat([...verts, verts[0]!]));
      labelAt([c[0], c[1]]);
      break;
    }
    case "bezier2": case "bezier3": {
      const chord = Math.hypot(pts[pts.length - 1]![0] - pts[0]![0], pts[pts.length - 1]![1] - pts[0]![1]);
      const path = sampleBezier(pts, clamp(Math.round(chord / 4), 24, 160));
      body(flat(path));
      addHeads(path);
      labelAt([path[Math.floor(path.length / 2)]![0] + 10, path[Math.floor(path.length / 2)]![1] - 10]);
      break;
    }
    case "spline": {
      const path = sampleSpline(pts);
      body(flat(path));
      addHeads(path);
      labelAt([pts[Math.floor(pts.length / 2)]![0] + 10, pts[Math.floor(pts.length / 2)]![1] - 10]);
      break;
    }
    case "arc": {
      const c = pts[0]!, r = Math.hypot(pts[1]![0] - c[0], pts[1]![1] - c[1]);
      const a0 = angleOf(c, pts[1]!), sweep = ccw(a0, angleOf(c, pts[2]!));
      const path = arcPoints(c, r, a0, sweep);
      body(flat(path));
      addHeads(path);
      const mid = onCircle(c, r + 12, a0 + sweep / 2);
      labelAt(mid);
      break;
    }
    case "angle": {
      const v = pts[0]!, a = pts[1]!, b = pts[2]!;
      body(flat([a, v, b]));
      const la = Math.hypot(a[0] - v[0], a[1] - v[1]), lb = Math.hypot(b[0] - v[0], b[1] - v[1]);
      const r = Math.max(8, Math.min(la, lb) * 0.28);
      const a0 = angleOf(v, a), a1 = angleOf(v, b);
      let sweep = ccw(a0, a1);
      let from = a0;
      if (sweep > Math.PI) { from = a1; sweep = Math.PI * 2 - sweep; }
      if (Math.abs(sweep - Math.PI / 2) < 0.03) { // a right angle gets a square
        const u: V2 = [Math.cos(from), -Math.sin(from)], t: V2 = [Math.cos(from + sweep), -Math.sin(from + sweep)];
        const k = r * 0.8;
        out.push(strokeEl([v[0] + u[0] * k, v[1] + u[1] * k, v[0] + (u[0] + t[0]) * k, v[1] + (u[1] + t[1]) * k, v[0] + t[0] * k, v[1] + t[1] * k], color, "ff", Math.max(0.3, w * 0.7), "figmark", id, null, style.glow));
      } else {
        out.push(strokeEl(flat(arcPoints(v, r, from, sweep)), color, "ff", Math.max(0.3, w * 0.7), "figmark", id, null, style.glow));
      }
      labelAt(onCircle(v, r * 1.9 + 4, from + sweep / 2));
      break;
    }
    case "dim": {
      const a = pts[0]!, b = pts[1]!, c = pts[2]!;
      const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.hypot(dx, dy) || 1;
      const nx = -dy / len, ny = dx / len;
      const off = (c[0] - a[0]) * nx + (c[1] - a[1]) * ny; // how far the dimension line stands from the measured segment
      const a2: V2 = [a[0] + nx * off, a[1] + ny * off], b2: V2 = [b[0] + nx * off, b[1] + ny * off];
      const ext = (p: V2, q: V2) => strokeEl([p[0] + (q[0] - p[0]) * 0.1, p[1] + (q[1] - p[1]) * 0.1, q[0] + nx * Math.sign(off) * 4, q[1] + ny * Math.sign(off) * 4], color, "ff", Math.max(0.3, w * 0.6), "figmark", id, null, style.glow);
      out.push(ext(a, a2), ext(b, b2));
      body(flat([a2, b2]));
      out.push(strokeEl(head(b2, a2, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow), strokeEl(head(a2, b2, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow));
      labelAt([(a2[0] + b2[0]) / 2 + nx * Math.sign(off || 1) * 10, (a2[1] + b2[1]) / 2 + ny * Math.sign(off || 1) * 10]);
      break;
    }
    default: {
      const { solid, hidden } = solidLines(s);
      for (const p of solid) body(p);
      if (s.hidden !== "none") for (const p of hidden) out.push(strokeEl(p, color, s.hidden === "dash" ? "99" : "ff", Math.max(0.3, w * 0.8), "fighidden", id, s.hidden === "dash" ? "dash" : s.dash, style.glow));
      labelAt([P(s, 0)[0] - 14, P(s, 0)[1] + 14]);
    }
  }
  // the invisible witness: where the figure was built, and what it is
  const wit = strokeEl(witnessOf(s), color, "00", 0.3, "figwitness", id, null);
  wit.attrs.push(["xlFig", JSON.stringify({ spec: s, w: witnessOf(s), width: +w.toFixed(3) })]);
  out.push(wit);
  return out;
}

// -------------------------------------------------------------------------------------------------- reading it back
const attrsOfEl = (el: PageElement): Array<[string, string]> => ("attrs" in el ? el.attrs : []);

export function figureWitness(members: PageElement[]): Stroke | null {
  return members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "figwitness" && attrValue(m.attrs, "xlFig") !== null) ?? null;
}

export function isFigure(members: PageElement[]): boolean { return figureWitness(members) !== null; }

/** The figure as it is now: the stored control points carried through whatever happened to the witness since. */
export function figureSpecOf(members: PageElement[]): { spec: FigureSpec; style: PlaneStyle } | null {
  const wit = figureWitness(members);
  if (!wit) return null;
  let stored: { spec: unknown; w: number[]; width: number };
  try { stored = JSON.parse(attrValue(wit.attrs, "xlFig")!) as typeof stored; } catch { return null; }
  const spec = sanitizeFigure(stored.spec);
  const T = affineFrom(stored.w, wit.pts);
  if (!T) return null;
  const pts = mapPoints(T, spec.pts);
  const body = members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "figbody");
  const hex = (body ? attrValue(body.attrs, "color") ?? "#000000ff" : "#000000ff").slice(0, 7);
  return { spec: { ...spec, pts }, style: { color: hex, width: Number.isFinite(stored.width) && stored.width > 0 ? stored.width : 1.41, ...(body?.glow ? { glow: { ...body.glow } } : {}) } };
}

/** The spec with control point `i` moved to `p`. Shift: angles in steps of 15 degrees (or, for a solid, the edge keeps its length). */
export function moveFigureAnchor(spec: FigureSpec, i: number, p: V2, shift: boolean): FigureSpec {
  const pts = spec.pts.slice();
  const at = (k: number): V2 => [pts[2 * k]!, pts[2 * k + 1]!];
  const set = (k: number, q: V2) => { pts[2 * k] = q[0]; pts[2 * k + 1] = q[1]; };
  const move = (dx: number, dy: number) => { for (let k = 0; k < pts.length; k += 2) { pts[k] = pts[k]! + dx; pts[k + 1] = pts[k + 1]! + dy; } };
  const snap = (from: V2, q: V2): V2 => {
    if (!shift) return q;
    const dx = q[0] - from[0], dy = q[1] - from[1], len = Math.hypot(dx, dy);
    const a = Math.round(Math.atan2(dy, dx) / (Math.PI / 12)) * (Math.PI / 12);
    return [from[0] + Math.cos(a) * len, from[1] + Math.sin(a) * len];
  };
  const o = at(0);
  if (isSolid(spec.kind)) {
    if (i === 0) { const old = at(0); move(p[0] - old[0], p[1] - old[1]); return { ...spec, pts }; } // the origin carries the whole solid
    const tip = at(i);
    let v: V2 = [p[0] - o[0], p[1] - o[1]];
    if (shift) { const old = Math.hypot(tip[0] - o[0], tip[1] - o[1]), now = Math.hypot(v[0], v[1]) || 1; v = [(v[0] / now) * old, (v[1] / now) * old]; }
    if (Math.hypot(v[0], v[1]) < 3) return spec;
    set(i, [o[0] + v[0], o[1] + v[1]]);
    return { ...spec, pts };
  }
  if (spec.kind === "regular" || spec.kind === "arc") {
    if (i === 0) { const old = at(0); move(p[0] - old[0], p[1] - old[1]); return { ...spec, pts }; }
    if (spec.kind === "arc" && i === 2) { // the end only sets the angle
      set(2, snap(o, p));
      return { ...spec, pts };
    }
    set(i, snap(o, p));
    return { ...spec, pts };
  }
  if (spec.kind === "segment" || spec.kind === "arrow") { set(i, snap(at(1 - i), p)); return { ...spec, pts }; }
  if (spec.kind === "angle") { set(i, i === 0 ? p : snap(at(0), p)); return { ...spec, pts }; }
  set(i, p);
  return { ...spec, pts };
}
