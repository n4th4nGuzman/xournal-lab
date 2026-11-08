// Bundled into the Electron renderer by electron_dump.mjs: the Electron app's own parser, exposed as a canonical dump.
import { parseXopp } from "../../../app/src/renderer/model";
import { serializeXopp } from "../../../app/src/renderer/serialize";

const sha = async (s: string) => {
  const t = s.replace(/\s+/g, "");
  let h = 2166136261;
  for (let i = 0; i < t.length; i++) { h ^= t.charCodeAt(i) & 255; h = Math.imul(h, 16777619) >>> 0; }
  return h.toString(16);
};
const attrs = (a: [string, string][], dropWidth = false) => a.filter((kv) => !(dropWidth && kv[0] === "width"));

export async function dump(xml: string) {
  const d = parseXopp(xml);
  const pages = [];
  for (const p of d.pages) {
    const layers = [];
    for (const l of p.layers) {
      const els: unknown[] = [];
      for (const e of l.elements) {
        if (e.kind === "stroke") els.push({ k: "stroke", attrs: attrs(e.attrs, true), widths: e.widths, pts: e.pts });
        else if (e.kind === "text") els.push({ k: "text", attrs: attrs(e.attrs), text: e.text });
        else if (e.kind === "image") els.push({ k: "image", attrs: attrs(e.attrs), sha: await sha(e.b64), len: e.b64.length });
        else if (e.kind === "teximage") els.push({ k: "teximage", attrs: attrs(e.attrs), sha: await sha(e.pdfB64), len: e.pdfB64.length });
        else els.push({ k: "UNSUPPORTED:" + e.tag });
      }
      layers.push({ attrs: attrs(l.attrs), els });
    }
    pages.push({ attrs: attrs(p.attrs), bg: attrs(p.background.attrs), layers });
  }
  return { root: attrs(d.rootAttrs), title: d.title ?? "", preview: d.previewB64 ? await sha(d.previewB64) : await sha(""), pages, dropped: d.droppedChildren };
}
// Parse, serialize with Electron's writer, and return the XML (so C++ can load what Electron would write).
export function reserialize(xml: string): string { return serializeXopp(parseXopp(xml)); }
(window as any).xl = { dump, reserialize };
