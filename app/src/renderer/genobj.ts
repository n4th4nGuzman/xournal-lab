/**
 * What the objects the editor builds out of ordinary pen strokes and texts (figures, number lines, set diagrams) have in common:
 * how a piece is made, and how an object is read back after its pieces were moved, turned or scaled. An object keeps its definition in
 * an invisible "witness" stroke together with three reference points; the map from where those points were to where they are now is
 * applied to every point of the stored definition.
 */
import { attrValue, bboxOf, cssColor, setGlow, type Glow, type PageElement, type Stroke, type TextBox } from "./model";
import { textBBox } from "./geometry";
import { stampText } from "./serialize";

export type V2 = [number, number];
export type Dash = "solid" | "dash" | "dot";

export interface GenStyle { color: string; width: number; glow?: Glow }

/** A piece of line: an ordinary pen stroke that belongs to object `id` with a role. `alpha` is two hex digits. */
export function strokeEl(pts: number[], color: string, alpha: string, width: number, role: string, id: string, dash: Dash | null, glow?: Glow): Stroke {
  const colorAttr = color + alpha;
  const st: Stroke = {
    kind: "stroke",
    attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(+width.toFixed(3))], ["capStyle", "round"], ["xlPlane", id], ["xlRole", role]],
    tool: "pen", color: cssColor(colorAttr), widths: [width], pts, cap: "round", dash: null, bbox: bboxOf(pts),
  };
  if (dash && dash !== "solid") { st.dash = dash; st.attrs.push(["style", dash]); }
  if (glow) setGlow(st, glow);
  return st;
}

/** A text centred on `at`, as a piece of object `id`. */
export function textEl(text: string, at: V2, size: number, color: string, id: string, role: string): TextBox {
  const t: TextBox = { kind: "text", attrs: [["xlPlane", id], ["xlRole", role]], text, font: "Sans", size, x: 0, y: 0, color: cssColor(color + "ff") };
  const b = textBBox(t);
  t.x = at[0] - (b[2] - b[0]) / 2;
  t.y = at[1] - (b[3] - b[1]) / 2;
  stampText(t);
  return t;
}

/** The three points of an arrow head with its tip at `tip`, coming from the direction of `from`. */
export function head(tip: V2, from: V2, len: number): number[] {
  const a = Math.atan2(tip[1] - from[1], tip[0] - from[0]);
  const b1 = a + Math.PI - 0.42, b2 = a + Math.PI + 0.42;
  return [tip[0] + Math.cos(b1) * len, tip[1] + Math.sin(b1) * len, tip[0], tip[1], tip[0] + Math.cos(b2) * len, tip[1] + Math.sin(b2) * len];
}

export type Affine = [number, number, number, number, number, number];

/** The affine map (a, b, c, d, e, f) taking three points to three others, or null when the first three are on a line. */
export function affineFrom(src: number[], dst: number[]): Affine | null {
  const [x0, y0, x1, y1, x2, y2] = src as [number, number, number, number, number, number];
  const det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (Math.abs(det) < 1e-9) return null;
  const solve = (v0: number, v1: number, v2: number): [number, number, number] => {
    const a = ((v1 - v0) * (y2 - y0) - (v2 - v0) * (y1 - y0)) / det, b = ((v2 - v0) * (x1 - x0) - (v1 - v0) * (x2 - x0)) / det;
    return [a, b, v0 - a * x0 - b * y0];
  };
  const [a, c, e] = solve(dst[0]!, dst[2]!, dst[4]!), [b, d, f] = solve(dst[1]!, dst[3]!, dst[5]!);
  return [a, b, c, d, e, f];
}

/** Every (x, y) pair of a flat list taken through the map. */
export function mapPoints(T: Affine, pts: number[]): number[] {
  const out: number[] = [];
  for (let i = 0; i + 1 < pts.length; i += 2) out.push(T[0] * pts[i]! + T[2] * pts[i + 1]! + T[4], T[1] * pts[i]! + T[3] * pts[i + 1]! + T[5]);
  return out;
}

/** The invisible stroke of an object (`role`) that holds its definition in attribute `attr`, if the members have one. */
export function findWitness(members: PageElement[], role: string, attr: string, roleOf: (e: PageElement) => string | null): Stroke | null {
  return members.find((m): m is Stroke => m.kind === "stroke" && roleOf(m) === role && attrValue(m.attrs, attr) !== null) ?? null;
}

/** Make the witness stroke: invisible, at three reference points, with the definition (any JSON) in attribute `attr`. */
export function makeWitness(w: number[], role: string, attr: string, payload: unknown, color: string, id: string): Stroke {
  const wit = strokeEl(w, color, "00", 0.3, role, id, null);
  wit.attrs.push([attr, JSON.stringify(payload)]);
  return wit;
}
