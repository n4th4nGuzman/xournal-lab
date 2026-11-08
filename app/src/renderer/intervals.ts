/**
 * Sets of real numbers as lists of intervals, with the usual operations. An interval has two ends, each open or closed; a single point
 * is the closed interval [a, a]; infinite ends are open. A set is always kept normal: sorted, with no empty or touching pieces.
 */

export interface Piece { lo: number; hi: number; loClosed: boolean; hiClosed: boolean }
export type IntervalSet = Piece[];

const EPS = 1e-9;
const same = (a: number, b: number) => a === b || (Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= EPS * Math.max(1, Math.abs(a), Math.abs(b)));

export const EMPTY: IntervalSet = [];
export const FULL: IntervalSet = [{ lo: -Infinity, hi: Infinity, loClosed: false, hiClosed: false }];

export function piece(lo: number, hi: number, loClosed: boolean, hiClosed: boolean): Piece {
  return { lo, hi, loClosed: loClosed && Number.isFinite(lo), hiClosed: hiClosed && Number.isFinite(hi) };
}

const isEmpty = (p: Piece) => p.lo > p.hi + EPS || (same(p.lo, p.hi) && !(p.loClosed && p.hiClosed));

/** Sorted, merged, no empty pieces; touching pieces merge when the shared end is in at least one of them. */
export function normalize(list: Piece[]): IntervalSet {
  const ps = list.filter((p) => !isEmpty(p)).map((p) => ({ ...p })).sort((a, b) => a.lo - b.lo || (a.loClosed === b.loClosed ? 0 : a.loClosed ? -1 : 1));
  const out: Piece[] = [];
  for (const p of ps) {
    const last = out[out.length - 1];
    if (last && (p.lo < last.hi - EPS || (same(p.lo, last.hi) && (p.loClosed || last.hiClosed)))) {
      if (p.hi > last.hi + EPS) { last.hi = p.hi; last.hiClosed = p.hiClosed; }
      else if (same(p.hi, last.hi)) last.hiClosed = last.hiClosed || p.hiClosed;
    } else out.push(p);
  }
  return out;
}

export const union = (a: IntervalSet, b: IntervalSet): IntervalSet => normalize([...a, ...b]);

export function intersect(a: IntervalSet, b: IntervalSet): IntervalSet {
  const out: Piece[] = [];
  for (const p of a) {
    for (const q of b) {
      const lo = Math.max(p.lo, q.lo), hi = Math.min(p.hi, q.hi);
      const loClosed = same(p.lo, q.lo) ? p.loClosed && q.loClosed : p.lo > q.lo ? p.loClosed : q.loClosed;
      const hiClosed = same(p.hi, q.hi) ? p.hiClosed && q.hiClosed : p.hi < q.hi ? p.hiClosed : q.hiClosed;
      out.push(piece(lo, hi, loClosed, hiClosed));
    }
  }
  return normalize(out);
}

export function complement(a: IntervalSet): IntervalSet {
  const out: Piece[] = [];
  let lo = -Infinity, loClosed = false;
  for (const p of a) {
    out.push(piece(lo, p.lo, loClosed, !p.loClosed));
    lo = p.hi; loClosed = !p.hiClosed;
  }
  out.push(piece(lo, Infinity, loClosed, false));
  return normalize(out);
}

export const difference = (a: IntervalSet, b: IntervalSet): IntervalSet => intersect(a, complement(b));
export const symmetric = (a: IntervalSet, b: IntervalSet): IntervalSet => union(difference(a, b), difference(b, a));

export function contains(a: IntervalSet, x: number): boolean {
  return a.some((p) => (x > p.lo + EPS || (same(x, p.lo) && p.loClosed)) && (x < p.hi - EPS || (same(x, p.hi) && p.hiClosed)));
}

export const equals = (a: IntervalSet, b: IntervalSet): boolean =>
  a.length === b.length && a.every((p, i) => same(p.lo, b[i]!.lo) && same(p.hi, b[i]!.hi) && p.loClosed === b[i]!.loClosed && p.hiClosed === b[i]!.hiClosed);

/** Numbers as the teacher writes them: "π/2", "1/3", "2.5", "−∞". */
export function formatNumber(v: number): string {
  if (v === Infinity) return "∞";
  if (v === -Infinity) return "−∞";
  if (same(v, 0)) return "0";
  const k = v / Math.PI;
  for (const d of [1, 2, 3, 4, 6]) {
    const n = k * d;
    if (Math.abs(n) > 1e-9 && same(n, Math.round(n)) && Math.abs(Math.round(n)) <= 12 && !Number.isInteger(v)) {
      const num = Math.round(n);
      const top = Math.abs(num) === 1 ? "π" : `${Math.abs(num)}π`;
      return `${num < 0 ? "−" : ""}${top}${d === 1 ? "" : `/${d}`}`;
    }
  }
  const s = String(+v.toFixed(6));
  return s.replace("-", "−");
}

/** A set as text, in the usual notation: (−2, 3] ∪ {5} ∪ [7, ∞). */
export function formatSet(a: IntervalSet): string {
  if (!a.length) return "∅";
  return a.map((p) => (same(p.lo, p.hi) ? `{${formatNumber(p.lo)}}` : `${p.loClosed ? "[" : "("}${formatNumber(p.lo)}, ${formatNumber(p.hi)}${p.hiClosed ? "]" : ")"}`)).join(" ∪ ");
}
