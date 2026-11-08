// Bundled into the Electron renderer by build_math_bundle.mjs: the Electron app's own readers/builders of generated objects (planes, 3D spaces, figures,
// number lines, set diagrams) exposed to the interchange checks (tools/interchange/run_math_interchange.mjs).
import { parseXopp, attrValue } from "../../../app/src/renderer/model";
import { serializeXopp } from "../../../app/src/renderer/serialize";
import type { PageElement, XoppDocument } from "../../../app/src/renderer/model";
import { DEFAULT_PLANE, buildPlane, planeFrameOf, planeIdOf, planeSpecOf, planeStyleOf } from "../../../app/src/renderer/plane";
import { DEFAULT_SPACE3, anchorsOf, buildSpace3, isSpace3, placedSpec, space3SpecOf } from "../../../app/src/renderer/space3";
import { FIGURES, buildFigure, defaultSpec, figureSpecOf, isFigure, sanitizeFigure } from "../../../app/src/renderer/figures";
import { buildMath, isMath, mathAnchors, readMath, sanitizeMath } from "../../../app/src/renderer/mathobj";
import { defaultLine } from "../../../app/src/renderer/numberline";
import { defaultVenn } from "../../../app/src/renderer/venn";
import { repairGenerated } from "../../../app/src/renderer/repair";

type Probe = { page: number; id: string; kind: string; pts: number[]; pieces: number; texts: number; textsWithoutPlace: number };

function kindAndPts(m: PageElement[]): { kind: string; pts: number[] } {
  if (isSpace3(m)) {
    const s = space3SpecOf(m);
    if (!s) return { kind: "space3?", pts: [] };
    return { kind: "space3", pts: [s.o[0], s.o[1], ...anchorsOf(s).flatMap((a) => [a.at[0], a.at[1]])] };
  }
  if (isFigure(m)) { const r = figureSpecOf(m); return { kind: r ? "figure" : "figure?", pts: r ? r.spec.pts : [] }; }
  if (isMath(m)) { const r = readMath(m); return r ? { kind: r.spec.kind, pts: mathAnchors(r.spec).flatMap((p) => [p[0], p[1]]) } : { kind: "math?", pts: [] }; }
  const spec = planeSpecOf(m), f = planeFrameOf(m), st = planeStyleOf(m);
  if (spec && f && st) return { kind: "plane", pts: [f.x0, f.y0, f.x1, f.y0, f.x1, f.y1, f.x0, f.y1] };
  return { kind: "none", pts: [] };
}

function probeDoc(doc: XoppDocument): Probe[] {
  const out: Probe[] = [];
  doc.pages.forEach((p, pi) => {
    for (const layer of p.layers) {
      const ids: string[] = [];
      for (const e of layer.elements) { const id = planeIdOf(e); if (id && !ids.includes(id)) ids.push(id); }
      for (const id of ids) {
        const m = layer.elements.filter((e) => planeIdOf(e) === id);
        const kp = kindAndPts(m);
        const texts = m.filter((e) => e.kind === "text");
        out.push({ page: pi, id, ...kp, pieces: m.length, texts: texts.length, textsWithoutPlace: texts.filter((t) => attrValue(t.attrs, "x") === null && attrValue(t.attrs, "y") === null).length });
      }
    }
  });
  return out;
}

export function probe(xml: string): Probe[] { return probeDoc(parseXopp(xml)); }

/** Strip the label positions (the old bug), run Electron's repair, and probe again. */
export function repairProbe(xml: string): { repaired: number; before: Probe[]; after: Probe[]; againZero: boolean } {
  const doc = parseXopp(xml);
  for (const p of doc.pages) for (const l of p.layers) {
    const kinds = new Map<string, string>();
    for (const e of l.elements) { const id = planeIdOf(e); if (id && !kinds.has(id)) kinds.set(id, kindAndPts(l.elements.filter((x) => planeIdOf(x) === id)).kind); }
    for (const e of l.elements) {
      const id = planeIdOf(e);
      if (e.kind === "text" && id && ["plane", "space3", "figure"].includes(kinds.get(id) ?? "")) e.attrs = e.attrs.filter(([k]) => k !== "x" && k !== "y" && k !== "font" && k !== "size" && k !== "color");
    }
  }
  const before = probeDoc(doc);
  const repaired = repairGenerated(doc);
  return { repaired, before, after: probeDoc(doc), againZero: repairGenerated(doc) === 0 };
}

const TEMPLATE = `<?xml version="1.0" standalone="no"?><xournal creator="xl-interchange" fileversion="4"><title>Xournal++ document - see https://github.com/xournalpp/xournalpp</title><page width="595.27559100" height="841.88976400"><background type="solid" color="#ffffffff" style="plain"/><layer></layer></page></xournal>`;

/** A document made by the Electron app's builders: one object per page, every kind. */
export function make(): { xml: string; probe: Probe[] } {
  const doc = parseXopp(TEMPLATE);
  const style = { color: "#0044cc", width: 1.41 };
  const objs: Array<{ name: string; build: (id: string) => PageElement[] }> = [];
  objs.push({ name: "plane", build: (id) => buildPlane(id, DEFAULT_PLANE, { x0: 150, y0: 150, x1: 450, y1: 400 }, style) });
  objs.push({ name: "space3", build: (id) => buildSpace3(id, placedSpec(DEFAULT_SPACE3, 1, [150, 160]), style) });
  for (const f of FIGURES) objs.push({ name: "figure " + f.kind, build: (id) => buildFigure(id, sanitizeFigure({ ...defaultSpec(f.kind, [160, 170], [420, 360]), label: f.kind === "segment" ? "AB" : "" }), style) });
  objs.push({ name: "line", build: (id) => buildMath(id, sanitizeMath({ ...defaultLine([150, 250], [450, 250]), rows: [{ expr: "(-2, 3] U {4}", label: "A" }, { expr: "[1, inf)", label: "B" }] }), style) });
  objs.push({ name: "venn2", build: (id) => buildMath(id, sanitizeMath({ ...defaultVenn(2, [150, 150], [450, 360]), expr: "A ∩ B", lists: ["1, 2", "2, 3", "", "9"] }), style) });
  objs.push({ name: "venn3", build: (id) => buildMath(id, sanitizeMath({ ...defaultVenn(3, [150, 150], [450, 400]), expr: "(A ∪ B)'" }), style) });
  const tmpl = doc.pages[0]!;
  doc.pages = [];
  objs.forEach((o, i) => {
    const page = JSON.parse(JSON.stringify(tmpl)) as typeof tmpl;
    page.layers[0]!.elements = o.build("plE" + String(i).padStart(3, "0"));
    doc.pages.push(page);
  });
  const xml = serializeXopp(doc);
  return { xml, probe: probeDoc(parseXopp(xml)).map((p) => ({ ...p, id: objs[p.page]!.name })) };
}

(window as any).xlm = { probe, repairProbe, make };
