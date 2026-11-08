/**
 * Files saved before the labels of planes, 3D spaces and figures were written with their position, size and colour: those texts
 * come back at the top left corner of the page, all on top of each other, and no longer belong to what they described. The
 * strokes of such an object still hold its definition, so the whole object is built again from them (in the same place of
 * the layer) and its labels are where they belong.
 */
import { buildFigure, figureSpecOf, isFigure } from "./figures";
import { attrValue, type Layer, type PageElement, type XoppDocument } from "./model";
import { buildPlane, planeFrameOf, planeIdOf, planeSpecOf, planeStyleOf } from "./plane";
import { buildSpace3, isSpace3, space3SpecOf, space3StyleOf } from "./space3";

const lacksPlace = (e: PageElement) => e.kind === "text" && attrValue(e.attrs, "x") === null && attrValue(e.attrs, "y") === null;

/** Build again the objects whose labels have no position. Returns how many objects were repaired. */
export function repairGenerated(doc: XoppDocument): number {
  let fixed = 0;
  for (const page of doc.pages) {
    for (const layer of page.layers) fixed += repairLayer(doc, layer);
  }
  return fixed;
}

function repairLayer(doc: XoppDocument, layer: Layer): number {
  const broken = new Set<string>();
  for (const e of layer.elements) {
    const id = planeIdOf(e);
    if (id && lacksPlace(e)) broken.add(id);
  }
  let fixed = 0;
  for (const id of broken) {
    const members = layer.elements.filter((e) => planeIdOf(e) === id);
    let fresh: PageElement[] | null = null;
    const spec = planeSpecOf(members), frame = planeFrameOf(members), style = planeStyleOf(members);
    if (spec && frame && style) fresh = buildPlane(id, spec, frame, style);
    else if (isSpace3(members)) {
      const s3 = space3SpecOf(members), st = space3StyleOf(members);
      if (s3 && st) fresh = buildSpace3(id, s3, st);
    } else if (isFigure(members)) {
      const f = figureSpecOf(members);
      if (f) fresh = buildFigure(id, f.spec, f.style);
    }
    if (!fresh) continue;
    const at = layer.elements.findIndex((e) => planeIdOf(e) === id);
    layer.elements = layer.elements.filter((e) => planeIdOf(e) !== id);
    layer.elements.splice(at, 0, ...fresh);
    doc.elementCount += fresh.length - members.length;
    fixed++;
  }
  return fixed;
}
