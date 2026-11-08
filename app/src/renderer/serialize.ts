import { setAttr } from "./model";
import type { Attrs, Background, ImageEl, Layer, Page, PageElement, Stroke, TexImageEl, TextBox, XoppDocument } from "./model";

// Mirrors Xournal++'s writer layout (observed via the source-track port, which
// round-trips the sample files byte for byte): numbers use C's "%.8g", empty
// nodes self-close, and every page has at least one layer.

/** C-style "%.8g" formatting, independent of locale. */
export function fmt8(v: number): string {
  if (!Number.isFinite(v)) return String(v);
  if (v === 0) return "0";
  const a = Math.abs(v);
  if (a >= 1e-4 && a < 1e8) return String(+v.toPrecision(8)); // the same digits as %.8g, without taking the number apart
  const [mant, expS] = v.toExponential(7).split("e") as [string, string];
  const exp = Number(expS);
  const strip = (s: string) => (s.includes(".") ? s.replace(/\.?0+$/, "") : s);
  if (exp < -4 || exp >= 8) {
    const sign = exp < 0 ? "-" : "+";
    return `${strip(mant)}e${sign}${String(Math.abs(exp)).padStart(2, "0")}`;
  }
  return strip(v.toFixed(7 - exp));
}

const escAttr = (s: string) =>
  s.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "&#10;").replace(/\r/g, "&#13;");
const escText = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

/**
 * The attributes of an element as text. `override` gives the values that always win (the numbers of the element itself) and are
 * written even when the element has no such attribute; `fallback` only fills in what is missing.
 */
function attrString(attrs: Attrs, override?: Record<string, string>, fallback?: Record<string, string>): string {
  let out = "";
  const seen = new Set<string>();
  for (const [k, v] of attrs) { seen.add(k); out += ` ${k}="${escAttr(override && k in override ? override[k]! : v)}"`; }
  if (override) for (const k of Object.keys(override)) if (!seen.has(k)) { seen.add(k); out += ` ${k}="${escAttr(override[k]!)}"`; }
  if (fallback) for (const k of Object.keys(fallback)) if (!seen.has(k)) out += ` ${k}="${escAttr(fallback[k]!)}"`;
  return out;
}

/** Give a text built by the editor the attributes a saved text has (font, size, position, colour), so it is the same thing as one read from a file. */
export function stampText(t: TextBox): void {
  setAttr(t.attrs, "font", t.font);
  setAttr(t.attrs, "size", fmt8(t.size));
  setAttr(t.attrs, "x", fmt8(t.x));
  setAttr(t.attrs, "y", fmt8(t.y));
  setAttr(t.attrs, "color", hexAlpha(t.color));
}

/** `#rrggbbaa` of a CSS colour as the model keeps it (`rgba(r,g,b,a)`), for texts that were built by the editor and have no colour attribute. */
export function hexAlpha(css: string): string {
  const m = /^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*([\d.]+))?/.exec(css);
  if (!m) return "#000000ff";
  const h = (v: number) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0");
  return `#${h(+m[1]!)}${h(+m[2]!)}${h(+m[3]!)}${h((m[4] === undefined ? 1 : +m[4]) * 255)}`;
}

function ptsText(pts: number[]): string {
  const parts = new Array<string>(pts.length);
  for (let i = 0; i < pts.length; i++) parts[i] = fmt8(pts[i]!);
  return parts.join(" ");
}

function stroke(s: Stroke): string {
  const width = s.widths.map(fmt8).join(" ");
  return `<stroke${attrString(s.attrs, { width })}>${ptsText(s.pts)}</stroke>\n`;
}
function text(t: TextBox): string {
  return `<text${attrString(t.attrs, { size: fmt8(t.size), x: fmt8(t.x), y: fmt8(t.y) }, { font: t.font, color: hexAlpha(t.color) })}>${escText(t.text)}</text>\n`;
}
function image(i: ImageEl): string {
  return `<image${attrString(i.attrs, { left: fmt8(i.left), top: fmt8(i.top), right: fmt8(i.right), bottom: fmt8(i.bottom) })}>${i.b64}</image>\n`;
}
function texImage(t: TexImageEl): string {
  return `<teximage${attrString(t.attrs, { text: t.latex, left: fmt8(t.left), top: fmt8(t.top), right: fmt8(t.right), bottom: fmt8(t.bottom) })}>${t.pdfB64}</teximage>\n`;
}
function element(e: PageElement): string {
  switch (e.kind) {
    case "stroke": return stroke(e);
    case "text": return text(e);
    case "image": return image(e);
    case "teximage": return texImage(e);
    case "unsupported": return e.xml + "\n";
  }
}
function layer(l: Layer): string {
  if (l.elements.length === 0) return `<layer${attrString(l.attrs)}/>\n`;
  return `<layer${attrString(l.attrs)}>\n${l.elements.map(element).join("")}</layer>\n`;
}
function background(b: Background): string {
  return `<background${attrString(b.attrs)}/>\n`;
}
function page(p: Page): string {
  const layers = p.layers.length ? p.layers : [{ attrs: [], elements: [] } as Layer];
  return `<page${attrString(p.attrs, { width: fmt8(p.width), height: fmt8(p.height) })}>\n${background(p.background)}${layers.map(layer).join("")}</page>\n`;
}

export function serializeXopp(doc: XoppDocument): string {
  let out = `<?xml version="1.0" standalone="no"?>\n<xournal${attrString(doc.rootAttrs)}>\n`;
  if (doc.title !== null) out += `<title>${escText(doc.title)}</title>\n`;
  if (doc.previewB64 !== null) out += `<preview>${doc.previewB64}</preview>\n`;
  for (const p of doc.pages) out += page(p);
  return out + "</xournal>\n";
}
