/** The objects of the real line and of set diagrams as one family: what is selected, built, read back and moved by its anchors. */
import { attrValue, type PageElement } from "./model";
import { roleOf } from "./plane";
import { SET_ATTR, buildLine, lineAnchors, moveLineAnchor, readLine, sanitizeLine, type LineSpec } from "./numberline";
import { buildVenn, moveVennAnchor, readVenn, sanitizeVenn, vennAnchors, type VennSpec } from "./venn";
import type { GenStyle, V2 } from "./genobj";

export type MathSpec = LineSpec | VennSpec;

export function isMath(members: PageElement[]): boolean {
  return members.some((m) => m.kind === "stroke" && roleOf(m) === "setwitness" && attrValue(m.attrs, SET_ATTR) !== null);
}

export function readMath(members: PageElement[]): { spec: MathSpec; style: GenStyle } | null {
  return readLine(members) ?? readVenn(members);
}

export function buildMath(id: string, spec: MathSpec, style: GenStyle): PageElement[] {
  return spec.kind === "line" ? buildLine(id, spec, style) : buildVenn(id, spec, style);
}

export const sanitizeMath = (spec: MathSpec): MathSpec => (spec.kind === "line" ? sanitizeLine(spec) : sanitizeVenn(spec));

export const mathAnchors = (spec: MathSpec): V2[] => (spec.kind === "line" ? lineAnchors(spec) : vennAnchors(spec));

export function moveMathAnchor(spec: MathSpec, i: number, p: V2, shift: boolean): MathSpec {
  return spec.kind === "line" ? moveLineAnchor(spec, i, p, shift) : moveVennAnchor(spec, i, p, shift);
}
