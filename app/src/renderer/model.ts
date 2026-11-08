import { parseSpec, type PaperSpec } from "./paper";
// In-memory model of a .xopp document. Parsed once from the XML text. The
// renderer and editor consume these typed facts; `attrs` keep the original
// attribute order/values so unchanged content serializes back verbatim.

export type Attrs = Array<[string, string]>;

/** A soft halo around a stroke, in the colour of the stroke. `size` is page units; `intensity` is 0..1. Saved as the `glow` / `glowAlpha` attributes. */
export interface Glow { size: number; intensity: number }

export interface Stroke {
  kind: "stroke";
  attrs: Attrs;
  tool: string; // pen | highlighter | eraser (others kept raw)
  color: string; // CSS rgba()
  widths: number[]; // [base, per-segment widths...]
  pts: number[]; // x0,y0,x1,y1,...
  cap: CanvasLineCap;
  dash: string | null; // "dash" | "dashdot" | "dot" | null
  bbox: [number, number, number, number];
  glow?: Glow;
  /** Runtime only (never saved): input-grid detection, valid while `src` is this stroke's `pts` array. */
  view?: { src: number[]; step: number; smooth: number[] | null };
}
export interface TextBox {
  kind: "text";
  attrs: Attrs;
  text: string;
  font: string;
  size: number;
  x: number;
  y: number;
  color: string;
}
export interface ImageEl {
  kind: "image";
  attrs: Attrs;
  left: number;
  top: number;
  right: number;
  bottom: number;
  b64: string;
  bitmap?: ImageBitmap | HTMLCanvasElement;
  loading?: boolean;
}
/** A LaTeX formula: the source text plus a vector PDF of it (the same layout Xournal++ stores). */
export interface TexImageEl {
  kind: "teximage";
  attrs: Attrs;
  latex: string;
  left: number;
  top: number;
  right: number;
  bottom: number;
  pdfB64: string;
  bitmap?: HTMLCanvasElement;
  loading?: boolean;
  failed?: boolean;
  /** Making the bitmap sharper for a closer zoom failed: it stays as it is. */
  sharpFailed?: boolean;
}
/**
 * How a formula is turned. The file format keeps a formula as an upright PDF in a box, so the turn lives in two attributes of
 * ours (`xlRot`, radians, and `xlFlip`) that other programs ignore: they show the formula upright. The box is the upright one;
 * the formula is drawn mirrored (if `flip`) and then turned by `rot` around the centre of the box.
 */
export interface TexPose { rot: number; flip: boolean }

export function texPose(t: TexImageEl): TexPose {
  let rot = 0, flip = false;
  for (const a of t.attrs) {
    if (a[0] === "xlRot") { const v = Number(a[1]); if (Number.isFinite(v)) rot = v; }
    else if (a[0] === "xlFlip") flip = a[1] === "1";
  }
  return { rot, flip };
}

export function setTexPose(t: TexImageEl, pose: TexPose): void {
  t.attrs = t.attrs.filter((a) => a[0] !== "xlRot" && a[0] !== "xlFlip");
  let rot = pose.rot % (2 * Math.PI);
  if (rot > Math.PI) rot -= 2 * Math.PI;
  if (rot <= -Math.PI) rot += 2 * Math.PI;
  if (Math.abs(rot) > 1e-6) t.attrs.push(["xlRot", String(+rot.toFixed(8))]);
  if (pose.flip) t.attrs.push(["xlFlip", "1"]);
}

export interface UnsupportedEl {
  kind: "unsupported";
  tag: string;
  xml: string; // serialized verbatim by the browser's XMLSerializer
}
export type PageElement = Stroke | TextBox | ImageEl | TexImageEl | UnsupportedEl;

export interface Background {
  attrs: Attrs;
  /** Special paper (millimetre, polar, logarithmic...) from the `xlPaper` attribute; `style` then holds its built-in look-alike. */
  paper?: PaperSpec;
  type: string; // solid | pixmap | pdf
  color: string;
  style: string; // plain | lined | ruled | graph | dotted | ...
}
export interface Layer {
  attrs: Attrs;
  elements: PageElement[];
  /** Session-only view state: the file format has no attribute for it, so it is not saved. */
  hidden?: boolean;
}
export interface Page {
  /** Session-only: the layer new elements go to (defaults to the top one). */
  active?: Layer;
  attrs: Attrs;
  width: number;
  height: number;
  background: Background;
  layers: Layer[];
}
export interface XoppDocument {
  rootAttrs: Attrs;
  title: string | null;
  previewB64: string | null;
  pages: Page[];
  elementCount: number;
  /** Document/page-level children this model does not carry (dropped on save). */
  droppedChildren: number;
}

/** The layer that receives new elements: the chosen one, else the top layer (created if the page has none). */
export function activeLayer(page: Page): Layer {
  if (!page.layers.length) page.layers.push({ attrs: [], elements: [] });
  return page.active && page.layers.includes(page.active) ? page.active : page.layers[page.layers.length - 1]!;
}

/** Display name of a layer: its saved name, or "Capa N" counted from the bottom. */
export function layerName(page: Page, layer: Layer): string {
  return attrValue(layer.attrs, "name") || `Capa ${page.layers.indexOf(layer) + 1}`;
}

export function attrValue(attrs: Attrs, name: string): string | null {
  return attrs.find((a) => a[0] === name)?.[1] ?? null;
}
/** Set an attribute in place, keeping its position, or append it when missing. */
export function setAttr(attrs: Attrs, name: string, value: string): void {
  const a = attrs.find((x) => x[0] === name);
  if (a) a[1] = value;
  else attrs.push([name, value]);
}

export function cssColor(raw: string | null): string {
  const m = /^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/.exec(raw ?? "");
  if (!m) return "#000";
  const n = parseInt(m[1]!, 16);
  const a = m[2] ? parseInt(m[2], 16) / 255 : 1;
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a.toFixed(3)})`;
}

function nums(s: string): number[] {
  const t = s.trim();
  if (!t) return [];
  const parts = t.split(/\s+/);
  const out = new Array<number>(parts.length);
  for (let i = 0; i < parts.length; i++) out[i] = Number(parts[i]);
  return out;
}

function attrsOf(e: Element): Attrs {
  return Array.from(e.attributes, (a): [string, string] => [a.name, a.value]);
}

/** A string without whitespace; a string that has none is returned as it is (no copy of a multi-megabyte image). */
function stripWs(t: string): string { return /\s/.test(t) ? t.replace(/\s+/g, "") : t; }

export function bboxOf(pts: number[]): [number, number, number, number] {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (let i = 0; i + 1 < pts.length; i += 2) {
    const x = pts[i]!, y = pts[i + 1]!;
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  return [x0, y0, x1, y1];
}

export const GLOW_MAX_SIZE = 40;

/** The glow described by a stroke's attributes, or undefined when it has none. */
export function readGlow(attrs: Attrs): Glow | undefined {
  const size = Number(attrValue(attrs, "glow"));
  if (!(size > 0)) return undefined;
  const a = Number(attrValue(attrs, "glowAlpha") ?? "0.8");
  return { size: Math.min(GLOW_MAX_SIZE, size), intensity: Number.isFinite(a) ? Math.min(1, Math.max(0.05, a)) : 0.8 };
}

/** Give a stroke a glow (or remove it): the typed field and the saved attributes change together. */
export function setGlow(s: Stroke, g: Glow | undefined): void {
  s.attrs = s.attrs.filter((a) => a[0] !== "glow" && a[0] !== "glowAlpha");
  if (g) {
    s.attrs.push(["glow", String(+g.size.toFixed(2))], ["glowAlpha", String(+g.intensity.toFixed(2))]);
    s.glow = { size: g.size, intensity: g.intensity };
  } else delete s.glow;
}

function readStroke(e: Element): Stroke {
  const widths = nums(e.getAttribute("width") ?? "1.41");
  const pts = nums(e.textContent ?? "");
  if (pts.length & 1) pts.pop();
  const cap = e.getAttribute("capStyle");
  const attrs = attrsOf(e);
  const glow = readGlow(attrs);
  return {
    kind: "stroke",
    attrs,
    ...(glow ? { glow } : {}),
    tool: e.getAttribute("tool") ?? "pen",
    color: cssColor(e.getAttribute("color")),
    widths: widths.length ? widths : [1.41],
    pts,
    cap: cap === "butt" || cap === "square" ? cap : "round",
    dash: e.getAttribute("style"),
    bbox: bboxOf(pts),
  };
}

function readLayerChild(e: Element): PageElement {
  switch (e.tagName) {
    case "stroke":
      return readStroke(e);
    case "text":
      return {
        kind: "text",
        attrs: attrsOf(e),
        text: e.textContent ?? "",
        font: e.getAttribute("font") ?? "Sans",
        size: Number(e.getAttribute("size") ?? 12),
        x: Number(e.getAttribute("x") ?? 0),
        y: Number(e.getAttribute("y") ?? 0),
        color: cssColor(e.getAttribute("color")),
      };
    case "image":
      return {
        kind: "image",
        attrs: attrsOf(e),
        left: Number(e.getAttribute("left")),
        top: Number(e.getAttribute("top")),
        right: Number(e.getAttribute("right")),
        bottom: Number(e.getAttribute("bottom")),
        b64: stripWs(e.textContent ?? ""),
      };
    case "teximage":
      return {
        kind: "teximage",
        attrs: attrsOf(e),
        latex: e.getAttribute("text") ?? "",
        left: Number(e.getAttribute("left")),
        top: Number(e.getAttribute("top")),
        right: Number(e.getAttribute("right")),
        bottom: Number(e.getAttribute("bottom")),
        pdfB64: stripWs(e.textContent ?? ""),
      };
    default:
      return { kind: "unsupported", tag: e.tagName, xml: new XMLSerializer().serializeToString(e) };
  }
}

export function parseXopp(xml: string): XoppDocument {
  const dom = new DOMParser().parseFromString(xml, "application/xml");
  const err = dom.querySelector("parsererror");
  if (err) throw new Error("XML inválido: " + (err.textContent ?? "").slice(0, 200));
  const root = dom.documentElement;
  if (root.tagName !== "xournal") throw new Error(`Raíz inesperada <${root.tagName}> (se esperaba <xournal>)`);

  let elementCount = 0;
  let dropped = 0;
  let title: string | null = null;
  let previewB64: string | null = null;
  const pages: Page[] = [];
  for (const p of Array.from(root.children)) {
    if (p.tagName === "title") { title = p.textContent ?? ""; continue; }
    if (p.tagName === "preview") { previewB64 = stripWs(p.textContent ?? ""); continue; }
    if (p.tagName !== "page") { dropped++; continue; }
    let bg: Background | null = null;
    const layers: Layer[] = [];
    for (const c of Array.from(p.children)) {
      if (c.tagName === "background" && !bg) {
        const spec = parseSpec(c.getAttribute("xlPaper"));
        bg = {
          attrs: attrsOf(c),
          ...(spec ? { paper: spec } : {}),
          type: c.getAttribute("type") ?? "solid",
          color: cssColor(c.getAttribute("color") ?? "#ffffffff"),
          style: c.getAttribute("style") ?? "plain",
        };
      } else if (c.tagName === "layer") {
        const elements = Array.from(c.children).map(readLayerChild);
        elementCount += elements.length;
        layers.push({ attrs: attrsOf(c), elements });
      } else dropped++;
    }
    pages.push({
      attrs: attrsOf(p),
      width: Number(p.getAttribute("width") ?? 595.275591),
      height: Number(p.getAttribute("height") ?? 841.889764),
      background: bg ?? { attrs: [["type", "solid"], ["color", "#ffffffff"], ["style", "plain"]], type: "solid", color: "#fff", style: "plain" },
      layers,
    });
  }
  return { rootAttrs: attrsOf(root), title, previewB64, pages, elementCount, droppedChildren: dropped };
}
