import { drawMarkdown, isMarkdown } from "./markdown";
import { texPose, type Glow, type ImageEl, Page, PageElement, Stroke, TexImageEl, TextBox } from "./model";
import { displayPoints, elementBBox, maxOf } from "./geometry";
import { drawPaper } from "./paper";
import { rasterizeAuto } from "./pdfraster";

// Background geometry (page units = points). Spacing is approximate for
// lined/ruled/graph/dotted; unknown styles fall back to plain.
const LINE = "rgba(150,150,150,1)";
const GRID = 14.17;

function drawBackground(ctx: CanvasRenderingContext2D, page: Page): void {
  const { width: w, height: h, background: bg } = page;
  ctx.fillStyle = bg.color;
  ctx.fillRect(0, 0, w, h);
  if (bg.paper) { drawPaper(ctx, w, h, bg.color, bg.paper); return; }
  ctx.strokeStyle = LINE;
  ctx.lineWidth = 0.5;
  ctx.beginPath();
  switch (bg.style) {
    case "lined":
    case "ruled": {
      for (let y = 80; y < h; y += 24) { ctx.moveTo(0, y); ctx.lineTo(w, y); }
      ctx.stroke();
      if (bg.style === "lined") { // only "lined" has the red margin
        ctx.strokeStyle = "rgba(220,80,80,1)";
        ctx.beginPath();
        ctx.moveTo(72, 0);
        ctx.lineTo(72, h);
        ctx.stroke();
      }
      break;
    }
    case "graph": {
      for (let x = 0; x <= w; x += GRID) { ctx.moveTo(x, 0); ctx.lineTo(x, h); }
      for (let y = 0; y <= h; y += GRID) { ctx.moveTo(0, y); ctx.lineTo(w, y); }
      ctx.stroke();
      break;
    }
    case "dotted": {
      ctx.fillStyle = LINE;
      for (let y = GRID; y < h; y += GRID) for (let x = GRID; x < w; x += GRID) ctx.fillRect(x - 0.5, y - 0.5, 1, 1);
      break;
    }
    default:
      break;
  }
}

// Dash patterns are approximate (units scale with nothing; observed style: "dash").
const DASHES: Record<string, number[]> = { dash: [6, 3], dashdot: [6, 3, 0.5, 3], dot: [0.5, 3] };

/** `rgba(r,g,b,a)` of a CSS colour with another alpha. */
function withAlpha(css: string, a: number): string {
  const m = /^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/.exec(css);
  if (m) return `rgba(${m[1]},${m[2]},${m[3]},${a})`;
  const h = /^#([0-9a-f]+)$/i.exec(css); // the live overlay is given the pen's colour as hex: it needs the intensity too
  if (!h || ![3, 6, 8].includes(h[1]!.length)) return css;
  const x = h[1]!.length === 3 ? [...h[1]!].map((c) => c + c).join("") : h[1]!;
  return `rgba(${parseInt(x.slice(0, 2), 16)},${parseInt(x.slice(2, 4), 16)},${parseInt(x.slice(4, 6), 16)},${a})`;
}

/**
 * The CSS/canvas filter that paints a glow: three stacked soft shadows (wide and faint, middle, tight and bright),
 * which gives a thin line a visible neon halo. `scale` is how many pixels of the surface the filter runs on one
 * page unit covers. The live overlay (a CSS filter) and the page canvas (a canvas filter) build it the same way,
 * so a stroke does not change when the pen is lifted.
 */
export function glowFilter(color: string, g: Glow, scale: number): string {
  const sigma = Math.max(0.3, g.size * scale * 0.5);
  const c = withAlpha(color, g.intensity);
  return [1, 0.45, 0.16].map((k) => `drop-shadow(0 0 ${(sigma * k).toFixed(2)}px ${c})`).join(" ");
}

const MAX_GLOW_PIXELS = 24_000_000;

export function drawStroke(ctx: CanvasRenderingContext2D, s: Stroke, lite = false): void {
  if (s.tool === "eraser") return; // erasing strokes are not replayed (known limitation)
  if (!lite && s.glow && s.tool !== "highlighter" && drawGlowing(ctx, s, s.glow)) return; // thumbnails skip the glow: invisible at that size
  drawStrokeCore(ctx, s);
}

interface GlowBitmap {
  pts: number[]; widths: number[]; color: string; size: number; intensity: number; k: number;
  canvas: HTMLCanvasElement;
  /** Where the bitmap's corner is, in pixels, relative to the page origin of the transform it was made for. */
  ox: number; oy: number;
  bytes: number;
}
/** Glow bitmaps of strokes, most recently used last. The glow only changes with the stroke or the zoom, never between repaints. */
const glowBitmaps = new Map<Stroke, GlowBitmap>();
let glowBytes = 0;
const GLOW_CACHE_BYTES = 96 * 1024 * 1024;
const GLOW_CACHE_PIXELS = 2_500_000; // bigger than this is drawn the direct way (and not kept)

function dropGlowBitmap(s: Stroke): void {
  const c = glowBitmaps.get(s);
  if (!c) return;
  glowBytes -= c.bytes;
  c.canvas.width = 0;
  c.canvas.height = 0;
  glowBitmaps.delete(s);
}

/** Forget every cached glow (a document was closed). */
export function clearGlowCache(): void { for (const s of [...glowBitmaps.keys()]) dropGlowBitmap(s); }

/** Draw the stroke with its glow: from the cached bitmap when nothing changed, else render it (padded) through the filter and keep it. */
function drawGlowing(ctx: CanvasRenderingContext2D, s: Stroke, g: Glow): boolean {
  const t = ctx.getTransform();
  const k = t.a;
  const hit = glowBitmaps.get(s);
  if (hit && hit.pts === s.pts && hit.widths === s.widths && hit.color === s.color && hit.size === g.size && hit.intensity === g.intensity && Math.abs(hit.k - k) < 1e-9) {
    glowBitmaps.delete(s); // most recently used goes last
    glowBitmaps.set(s, hit);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.drawImage(hit.canvas, Math.round(t.e) + hit.ox, Math.round(t.f) + hit.oy);
    ctx.restore();
    return true;
  }
  if (hit) dropGlowBitmap(s);
  const reach = Math.ceil(g.size * k * 1.6 + maxOf(s.widths) * k + 2);
  const ox = Math.floor(s.bbox[0] * k) - reach, oy = Math.floor(s.bbox[1] * k) - reach; // relative to the page origin, so it does not depend on where the canvas is
  const w = Math.ceil((s.bbox[2] - s.bbox[0]) * k) + 2 * reach, h = Math.ceil((s.bbox[3] - s.bbox[1]) * k) + 2 * reach;
  if (!(w > 0 && h > 0) || w * h > MAX_GLOW_PIXELS) return false;
  const scratch = document.createElement("canvas");
  scratch.width = w;
  scratch.height = h;
  const sc = scratch.getContext("2d")!;
  sc.setTransform(k, 0, 0, k, -ox, -oy);
  drawStrokeCore(sc, s);
  if (w * h > GLOW_CACHE_PIXELS) { // too big to keep: the direct way
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.filter = glowFilter(s.color, g, k);
    ctx.drawImage(scratch, Math.round(t.e) + ox, Math.round(t.f) + oy);
    ctx.restore();
    scratch.width = 0;
    return true;
  }
  const out = document.createElement("canvas");
  out.width = w;
  out.height = h;
  const oc = out.getContext("2d")!;
  oc.filter = glowFilter(s.color, g, k);
  oc.drawImage(scratch, 0, 0);
  scratch.width = 0;
  const bytes = w * h * 4;
  glowBitmaps.set(s, { pts: s.pts, widths: s.widths, color: s.color, size: g.size, intensity: g.intensity, k, canvas: out, ox, oy, bytes });
  glowBytes += bytes;
  while (glowBytes > GLOW_CACHE_BYTES && glowBitmaps.size > 1) dropGlowBitmap(glowBitmaps.keys().next().value!); // the least recently used
  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.drawImage(out, Math.round(t.e) + ox, Math.round(t.f) + oy);
  ctx.restore();
  return true;
}

function drawStrokeCore(ctx: CanvasRenderingContext2D, s: Stroke): void {
  ctx.setLineDash(s.dash ? DASHES[s.dash] ?? [] : []);
  const p = displayPoints(s, ctx.getTransform().a); // pixels per page unit: hides the input grid only when it would show
  const n = p.length >> 1;
  if (n === 0) return;
  ctx.strokeStyle = s.color;
  ctx.fillStyle = s.color;
  ctx.lineCap = s.cap;
  ctx.lineJoin = "round";
  const base = s.widths[0]!;
  if (n === 1) {
    ctx.beginPath();
    ctx.arc(p[0]!, p[1]!, Math.max(base, s.widths[1] ?? base) / 2, 0, Math.PI * 2);
    ctx.fill();
    return;
  }
  if (s.widths.length <= 1) {
    ctx.lineWidth = base;
    ctx.beginPath();
    ctx.moveTo(p[0]!, p[1]!);
    for (let i = 1; i < n; i++) ctx.lineTo(p[2 * i]!, p[2 * i + 1]!);
    ctx.stroke();
    return;
  }
  // Per-segment widths: batch runs of segments whose width is equal after
  // quantisation into one path to keep draw calls low.
  // Widths are rounded to a third of a device pixel (never finer than 0.05): a difference nobody can see, and long runs of equal width are
  // one draw call each (a pressure stroke used to need hundreds).
  const step = Math.max(0.05, 0.33 / ctx.getTransform().a);
  const q = (v: number) => Math.max(0.05, Math.round(v / step) * step); // never 0: a canvas ignores a line width of 0 and would keep the previous one
  let runW = q(s.widths[1] ?? base);
  ctx.lineWidth = runW;
  ctx.beginPath();
  ctx.moveTo(p[0]!, p[1]!);
  for (let i = 1; i < n; i++) {
    const w = q(s.widths[i] ?? base);
    if (w !== runW) {
      ctx.stroke();
      runW = w;
      ctx.lineWidth = runW;
      ctx.beginPath();
      ctx.moveTo(p[2 * (i - 1)]!, p[2 * (i - 1) + 1]!);
    }
    ctx.lineTo(p[2 * i]!, p[2 * i + 1]!);
  }
  ctx.stroke();
}

function drawText(ctx: CanvasRenderingContext2D, t: TextBox): void {
  if (isMarkdown(t)) { drawMarkdown(ctx, t); return; }
  const family = t.font === "Sans" ? "system-ui, sans-serif" : `"${t.font}", sans-serif`;
  ctx.font = `${t.size}px ${family}`;
  ctx.fillStyle = t.color;
  ctx.textBaseline = "top";
  const lh = t.size * 1.2;
  t.text.split("\n").forEach((line, i) => ctx.fillText(line, t.x, t.y + i * lh));
}

function drawImage(ctx: CanvasRenderingContext2D, im: ImageEl, onLoaded: () => void): void {
  if (im.bitmap) {
    ctx.drawImage(im.bitmap, im.left, im.top, im.right - im.left, im.bottom - im.top);
    return;
  }
  if (im.loading) return;
  im.loading = true;
  const bin = atob(im.b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  void createImageBitmap(new Blob([bytes]))
    .then((b) => {
      im.bitmap = b;
      im.loading = false;
      onLoaded();
    })
    .catch(() => {
      im.loading = false;
    });
}

const TEX_PX_PER_PT = 4; // resolution of a formula's cached bitmap
const TEX_MAX_PX = 2048;

const TEX_SHARP_MAX = 4096; // widest bitmap a formula is sharpened to when zoomed in

function drawTexImage(ctx: CanvasRenderingContext2D, t: TexImageEl, onLoaded: () => void): void {
  const w = t.right - t.left, h = t.bottom - t.top;
  if (t.bitmap) {
    // Zoomed in beyond what the bitmap holds: it is made again from the formula's PDF at the size the screen needs (the old one is drawn meanwhile).
    const need = Math.ceil(w * ctx.getTransform().a);
    if (need > t.bitmap.width * 1.25 && t.bitmap.width < TEX_SHARP_MAX && !t.loading && !t.sharpFailed) {
      t.loading = true;
      const target = Math.min(TEX_SHARP_MAX, Math.max(need, Math.ceil(t.bitmap.width * 2)));
      void rasterizeAuto(t.pdfB64, target)
        .then((cv) => { if (cv.width > 0) t.bitmap = cv; else t.sharpFailed = true; t.loading = false; onLoaded(); })
        .catch(() => { t.loading = false; t.sharpFailed = true; });
    }
    const pose = texPose(t);
    if (pose.rot === 0 && !pose.flip) { ctx.drawImage(t.bitmap, t.left, t.top, w, h); return; }
    ctx.save();
    ctx.translate(t.left + w / 2, t.top + h / 2);
    ctx.rotate(pose.rot);
    if (pose.flip) ctx.scale(-1, 1);
    ctx.drawImage(t.bitmap, -w / 2, -h / 2, w, h);
    ctx.restore();
    return;
  }
  // Placeholder while the PDF is rasterised (or if it cannot be).
  ctx.save();
  ctx.strokeStyle = t.failed ? "rgba(200,60,60,0.8)" : "rgba(90,90,90,0.6)";
  ctx.setLineDash([3, 3]);
  ctx.lineWidth = 0.7;
  ctx.strokeRect(t.left, t.top, w, h);
  ctx.restore();
  if (t.loading || t.failed) return;
  t.loading = true;
  void rasterizeAuto(t.pdfB64, Math.min(TEX_MAX_PX, Math.max(64, Math.round(w * TEX_PX_PER_PT))))
    .then((cv) => { t.bitmap = cv; t.loading = false; onLoaded(); })
    .catch(() => { t.loading = false; t.failed = true; onLoaded(); });
}

export function drawElement(ctx: CanvasRenderingContext2D, el: PageElement, onImageLoaded: () => void, lite = false): void {
  if (el.kind === "stroke") drawStroke(ctx, el, lite);
  else if (el.kind === "text") drawText(ctx, el);
  else if (el.kind === "image") drawImage(ctx, el, onImageLoaded);
  else if (el.kind === "teximage") drawTexImage(ctx, el, onImageLoaded);
}

/**
 * Draw a page onto ctx whose transform already maps page units to pixels. With `region` (page units) elements that
 * cannot touch it are skipped, so a canvas that covers only the visible part of a page does not pay for the rest.
 */
export function renderPage(ctx: CanvasRenderingContext2D, page: Page, onImageLoaded: () => void, region?: [number, number, number, number], lite = false): void {
  drawBackground(ctx, page);
  for (const layer of page.layers) {
    if (layer.hidden) continue;
    for (const el of layer.elements) {
      if (region) {
        const b = elementBBox(el);
        const pad = 6 + (el.kind === "stroke" && el.glow ? el.glow.size * 2 : 0);
        if (b && (b[2] + pad < region[0] || b[0] - pad > region[2] || b[3] + pad < region[1] || b[1] - pad > region[3])) continue;
      }
      drawElement(ctx, el, onImageLoaded, lite);
    }
  }
}
