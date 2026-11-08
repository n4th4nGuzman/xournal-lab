/**
 * The real line: an axis with marks and numbers, and on it (or on copies of it, one under the other) the sets of numbers that are
 * written as text: points, intervals open or closed or half open, rays to infinity, unions, and holes (a point left out). Everything
 * is drawn with ordinary pen strokes and texts. The text of each expression is the definition; the drawing is built from it.
 */
import { attrValue, type PageElement, type Stroke } from "./model";
import { formatNumber, type IntervalSet, type Piece } from "./intervals";
import { evalReal, parseSetExpr } from "./setexpr";
import { affineFrom, findWitness, head, makeWitness, mapPoints, strokeEl, textEl, type GenStyle, type V2 } from "./genobj";
import { roleOf } from "./plane";

export const SET_ATTR = "xlSet";

export interface LineRow { expr: string; label: string }
export interface LineSpec {
  kind: "line";
  /** The two ends of the axis: where `min` and `max` are. */
  pts: number[];
  min: number;
  max: number;
  step: number;
  /** A number is written every this many marks. */
  every: number;
  ticks: boolean;
  numbers: boolean;
  /** Arrows at both ends of the axis. */
  arrows: boolean;
  /** How an end of an interval is drawn: a circle (open) or a dot (closed); or a parenthesis and a bracket. */
  marks: "circles" | "brackets";
  rows: LineRow[];
}

export const MAX_ROWS = 3;
const MAX_TICKS = 120;
const PAD = 14;
const ROW_GAP = 40;

export const defaultLine = (a: V2 = [100, 200], b: V2 = [500, 200]): LineSpec => ({
  kind: "line", pts: [a[0], a[1], b[0], b[1]], min: -5, max: 5, step: 1, every: 1, ticks: true, numbers: true, arrows: true, marks: "circles", rows: [{ expr: "", label: "" }],
});

const num = (v: unknown, d: number, lo = -1e9, hi = 1e9) => (typeof v === "number" && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : d);

export function sanitizeLine(raw: unknown): LineSpec {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const d = defaultLine();
  const pts = Array.isArray(o.pts) && o.pts.length >= 4 ? (o.pts as unknown[]).slice(0, 4).map((v, i) => num(v, d.pts[i]!, -1e6, 1e6)) : d.pts;
  let min = num(o.min, d.min), max = num(o.max, d.max);
  if (!(max > min)) max = min + 1;
  const rowsIn = Array.isArray(o.rows) ? (o.rows as unknown[]).slice(0, MAX_ROWS) : [];
  const rows: LineRow[] = rowsIn.map((r) => {
    const q = typeof r === "object" && r !== null ? (r as Record<string, unknown>) : {};
    return { expr: typeof q.expr === "string" ? q.expr.slice(0, 200) : "", label: typeof q.label === "string" ? q.label.slice(0, 40) : "" };
  });
  if (!rows.length) rows.push({ expr: "", label: "" });
  return {
    kind: "line", pts, min, max, step: Math.max((max - min) / 400, num(o.step, d.step, 1e-6, 1e9)), every: Math.round(num(o.every, 1, 1, 50)),
    ticks: o.ticks !== false, numbers: o.numbers !== false, arrows: o.arrows !== false, marks: o.marks === "brackets" ? "brackets" : "circles", rows,
  };
}

/** The sets of the rows: a mistake in a row leaves it empty and is reported apart. */
export function lineSets(spec: LineSpec): Array<{ set: IntervalSet | null; error: string | null }> {
  return spec.rows.map((r) => {
    if (!r.expr.trim()) return { set: null, error: null };
    const p = parseSetExpr(r.expr);
    if (!p.ok) return { set: null, error: p.msg };
    const e = evalReal(p.expr);
    return e.ok ? { set: e.value, error: null } : { set: null, error: e.msg };
  });
}

interface Frame { p0: V2; u: V2; up: V2; len: number; scale: number }
function frameOf(spec: LineSpec): Frame {
  const [x0, y0, x1, y1] = spec.pts as [number, number, number, number];
  const len = Math.hypot(x1 - x0, y1 - y0) || 1;
  const u: V2 = [(x1 - x0) / len, (y1 - y0) / len];
  return { p0: [x0, y0], u, up: [u[1], -u[0]], len, scale: len / (spec.max - spec.min) };
}

const at = (f: Frame, s: number, off = 0): V2 => [f.p0[0] + f.u[0] * s + f.up[0] * off, f.p0[1] + f.u[1] * s + f.up[1] * off];

/** The ranges of [a, b] that are left when the gaps are taken out. */
function without(a: number, b: number, gaps: Array<[number, number]>): Array<[number, number]> {
  let out: Array<[number, number]> = [[a, b]];
  for (const [g0, g1] of gaps) {
    out = out.flatMap(([s, e]): Array<[number, number]> => {
      if (g1 <= s || g0 >= e) return [[s, e]];
      const r: Array<[number, number]> = [];
      if (g0 > s) r.push([s, g0]);
      if (g1 < e) r.push([g1, e]);
      return r;
    });
  }
  return out.filter(([s, e]) => e - s > 0.05);
}

interface Marker { v: number; closed: boolean; side: 1 | -1 } // side: +1 when the interval goes toward larger values from here

/** The marks of a set inside the visible range, one per value (a point both pieces leave out is one open circle). */
function markersOf(set: IntervalSet, min: number, max: number): Marker[] {
  const byValue = new Map<string, Marker>();
  const add = (v: number, closed: boolean, side: 1 | -1) => {
    if (!Number.isFinite(v) || v < min - 1e-9 || v > max + 1e-9) return;
    const k = v.toFixed(9);
    const old = byValue.get(k);
    if (!old) byValue.set(k, { v, closed, side });
    else byValue.set(k, { v, closed: old.closed || closed, side: old.side });
  };
  for (const p of set) {
    if (p.lo === p.hi) add(p.lo, true, 1);
    else { add(p.lo, p.loClosed, 1); add(p.hi, p.hiClosed, -1); }
  }
  return [...byValue.values()].sort((a, b) => a.v - b.v);
}

function circle(c: V2, r: number): number[] {
  const out: number[] = [];
  for (let i = 0; i <= 24; i++) out.push(c[0] + Math.cos((i / 24) * Math.PI * 2) * r, c[1] + Math.sin((i / 24) * Math.PI * 2) * r);
  return out;
}

export function buildLine(id: string, rawSpec: LineSpec, style: GenStyle): PageElement[] {
  const spec = sanitizeLine(rawSpec);
  const f = frameOf(spec);
  const w = Math.max(0.3, style.width), color = style.color;
  const hiW = w * 2.6;
  const R = Math.min(7, Math.max(3.6, 3 + w * 1.6));
  const out: PageElement[] = [];
  const line = (pts: number[], role: string, width = w) => out.push(strokeEl(pts, color, "ff", width, role, id, null, style.glow));
  const sets = lineSets(spec);

  // marks every `step`, as many as can be told apart
  let step = spec.step;
  while ((spec.max - spec.min) / step > MAX_TICKS) step *= 2;
  const k0 = Math.ceil(spec.min / step - 1e-9), k1 = Math.floor(spec.max / step + 1e-9);

  spec.rows.forEach((row, ri) => {
    const off = ri * ROW_GAP;
    const set = sets[ri]?.set ?? null;
    const circles = spec.marks === "circles";
    const markers = set ? markersOf(set, spec.min, spec.max) : [];
    const gaps: Array<[number, number]> = circles ? markers.filter((m) => !m.closed).map((m) => { const s = (m.v - spec.min) * f.scale; return [s - R, s + R] as [number, number]; }) : [];

    // the line: in pieces, so an open circle has nothing inside it
    for (const [s0, s1] of without(-PAD, f.len + PAD, gaps)) line([...at(f, s0, off), ...at(f, s1, off)], "setbody", ri === 0 ? w : w * 0.7);
    if (spec.arrows) {
      line(head(at(f, f.len + PAD, off), at(f, 0, off), 8), "setarrow", w);
      line(head(at(f, -PAD, off), at(f, f.len, off), 8), "setarrow", w);
    }
    // marks and numbers
    if (spec.ticks) {
      const tl = ri === 0 ? 5 : 3;
      for (let k = k0; k <= k1; k++) {
        const s = (k * step - spec.min) * f.scale;
        if (gaps.some(([g0, g1]) => s > g0 && s < g1)) continue;
        line([...at(f, s, off + tl), ...at(f, s, off - tl)], "setmark", w * 0.8);
        if (ri === 0 && spec.numbers && (k - k0) % spec.every === 0) out.push(textEl(formatNumber(k * step), at(f, s, off - tl - 9), 10, color, id, "setlabel"));
      }
    }
    // what the set is: thick pieces over the line, with the ends marked
    if (set) {
      for (const p of set) drawPiece(p);
      for (const m of markers) drawMarker(m);
    }
    if (row.label) out.push(textEl(row.label, at(f, -PAD - 4, off + 16), 12, color, id, "setlabel"));

    function drawPiece(p: Piece): void {
      if (p.hi < spec.min - 1e-9 || p.lo > spec.max + 1e-9 || p.lo === p.hi) return;
      const leftOpenEnd = p.lo < spec.min - 1e-9, rightOpenEnd = p.hi > spec.max + 1e-9; // runs off the page (or to infinity)
      const s0 = leftOpenEnd ? -PAD : (p.lo - spec.min) * f.scale, s1 = rightOpenEnd ? f.len + PAD : (p.hi - spec.min) * f.scale;
      const trim = (m: boolean, closed: boolean) => (circles && m && !closed ? R : 0);
      const a = s0 + trim(!leftOpenEnd, p.loClosed), b = s1 - trim(!rightOpenEnd, p.hiClosed);
      if (b > a) line([...at(f, a, off), ...at(f, b, off)], "setbody", hiW);
      if (!spec.arrows) {
        if (rightOpenEnd) line(head(at(f, f.len + PAD, off), at(f, f.len - 30, off), 9), "setarrow", w);
        if (leftOpenEnd) line(head(at(f, -PAD, off), at(f, 30, off), 9), "setarrow", w);
      }
    }

    function drawMarker(m: Marker): void {
      const s = (m.v - spec.min) * f.scale, c = at(f, s, off);
      if (circles) {
        if (m.closed) line([c[0], c[1], c[0] + 0.01, c[1]], "setbody", R * 2);
        else line(circle(c, R), "setbody", w);
        return;
      }
      // a bracket where the end belongs to the set, a parenthesis where it does not; both open toward the piece
      const d: V2 = [f.u[0] * m.side, f.u[1] * m.side];
      const H = 9, hook = 5;
      if (m.closed) {
        const top = at(f, s, off + H), bot = at(f, s, off - H);
        line([top[0] + d[0] * hook, top[1] + d[1] * hook, ...top, ...bot, bot[0] + d[0] * hook, bot[1] + d[1] * hook], "setbody", w * 1.3);
      } else {
        const top = at(f, s, off + H), bot = at(f, s, off - H), ctl: V2 = [c[0] - d[0] * 7, c[1] - d[1] * 7];
        const pts: number[] = [];
        for (let i = 0; i <= 10; i++) { const t = i / 10, q = 1 - t; pts.push(q * q * top[0] + 2 * q * t * ctl[0] + t * t * bot[0], q * q * top[1] + 2 * q * t * ctl[1] + t * t * bot[1]); }
        line(pts, "setbody", w * 1.3);
      }
    }
  });

  out.push(makeWitness(witnessOf(spec), "setwitness", SET_ATTR, { kind: "line", spec, w: witnessOf(spec), width: +w.toFixed(3) }, color, id));
  return out;
}

/** Three points that stand for the line's place: its two ends and a point off to the side. */
export function witnessOf(spec: LineSpec): number[] {
  const f = frameOf(spec);
  const p = at(f, 0, 30);
  return [...spec.pts, p[0], p[1]];
}

/** The anchors: the two ends of the axis. */
export function lineAnchors(spec: LineSpec): V2[] {
  return [[spec.pts[0]!, spec.pts[1]!], [spec.pts[2]!, spec.pts[3]!]];
}

/** The spec with an end of the axis moved. Shift: the angle goes in steps of 15° (and an end stays level with the other by default). */
export function moveLineAnchor(spec: LineSpec, i: number, p: V2, shift: boolean): LineSpec {
  const other: V2 = i === 0 ? [spec.pts[2]!, spec.pts[3]!] : [spec.pts[0]!, spec.pts[1]!];
  let q = p;
  if (shift) {
    const dx = p[0] - other[0], dy = p[1] - other[1], len = Math.hypot(dx, dy);
    const a = Math.round(Math.atan2(dy, dx) / (Math.PI / 12)) * (Math.PI / 12);
    q = [other[0] + Math.cos(a) * len, other[1] + Math.sin(a) * len];
  }
  if (Math.hypot(q[0] - other[0], q[1] - other[1]) < 20) return spec;
  const pts = spec.pts.slice();
  pts[2 * i] = q[0]; pts[2 * i + 1] = q[1];
  return { ...spec, pts };
}

/** The line as it is now: the stored points carried through whatever happened to the witness since. */
export function readLine(members: PageElement[]): { spec: LineSpec; style: GenStyle } | null {
  const wit = findWitness(members, "setwitness", SET_ATTR, roleOf);
  if (!wit) return null;
  let stored: { kind?: string; spec: unknown; w: number[]; width: number };
  try { stored = JSON.parse(attrValue(wit.attrs, SET_ATTR)!) as typeof stored; } catch { return null; }
  if (stored.kind !== "line") return null;
  const spec = sanitizeLine(stored.spec);
  const T = affineFrom(stored.w, wit.pts);
  if (!T) return null;
  spec.pts = mapPoints(T, spec.pts);
  const body = members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === "setbody");
  const hex = (body ? attrValue(body.attrs, "color") ?? "#000000ff" : "#000000ff").slice(0, 7);
  return { spec, style: { color: hex, width: Number.isFinite(stored.width) && stored.width > 0 ? stored.width : 1.41, ...(body?.glow ? { glow: { ...body.glow } } : {}) } };
}
