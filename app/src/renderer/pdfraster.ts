import type * as PdfJs from "pdfjs-dist";

// pdf.js ships as a module plus a worker; both are copied next to the page by the build
// and loaded on first use, so documents without PDFs or formulas never pay for them.
let lib: Promise<typeof PdfJs> | null = null;

function load(): Promise<typeof PdfJs> {
  lib ??= (async () => {
    const url = new URL("pdf.mjs", location.href).href;
    const m = (await import(/* @vite-ignore */ url)) as typeof PdfJs;
    m.GlobalWorkerOptions.workerSrc = new URL("pdf.worker.mjs", location.href).href;
    return m;
  })();
  return lib;
}

const toBytes = (b64: string) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));

export interface PdfInfo { pages: number; widthPt: number; heightPt: number }

/** Page count and first-page size (points) of a PDF. */
export async function pdfInfo(b64: string): Promise<PdfInfo> {
  const pdfjs = await load();
  const task = pdfjs.getDocument({ data: toBytes(b64) });
  const pdf = await task.promise;
  try {
    const v = (await pdf.getPage(1)).getViewport({ scale: 1 });
    return { pages: pdf.numPages, widthPt: v.width, heightPt: v.height };
  } finally {
    await task.destroy();
  }
}

/** Draw one page of a PDF on a transparent canvas `widthPx` wide. */
export async function rasterizePdf(b64: string, widthPx: number, pageNo = 1, background?: string): Promise<HTMLCanvasElement> {
  const pdfjs = await load();
  const task = pdfjs.getDocument({ data: toBytes(b64) });
  const pdf = await task.promise;
  try {
    const page = await pdf.getPage(pageNo);
    const viewport = page.getViewport({ scale: widthPx / page.getViewport({ scale: 1 }).width });
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.ceil(viewport.width));
    canvas.height = Math.max(1, Math.ceil(viewport.height));
    await page.render(background === undefined ? { canvas, viewport } : { canvas, viewport, background }).promise;
    return canvas;
  } finally {
    await task.destroy();
  }
}

const hexRgb = (hex: string): [number, number, number] => {
  const n = parseInt(hex.replace("#", ""), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
};

/**
 * Turn a page drawn on a known background into a transparent one, given the ink colour.
 * Per pixel: P = a*ink + (1-a)*background, so alpha comes from the channel where ink and
 * background differ most. pdf.js paints a "transparent" background black, hence this.
 */
function unblend(cv: HTMLCanvasElement, ink: [number, number, number], onBlack: boolean): HTMLCanvasElement {
  const ctx = cv.getContext("2d")!;
  const img = ctx.getImageData(0, 0, cv.width, cv.height);
  const d = img.data;
  const dist = ink.map((v) => (onBlack ? v : 255 - v));
  const ch = dist.indexOf(Math.max(...dist));
  const den = Math.max(1, dist[ch]!);
  for (let i = 0; i < d.length; i += 4) {
    const p = d[i + ch]!;
    const a = Math.min(1, Math.max(0, (onBlack ? p : 255 - p) / den));
    const al = Math.round(a * 255);
    d[i] = al ? ink[0] : 0; d[i + 1] = al ? ink[1] : 0; d[i + 2] = al ? ink[2] : 0; d[i + 3] = al; // no colour where there is no ink (a pixel with colour and no alpha would add to what is under it)
  }
  ctx.putImageData(img, 0, 0);
  return cv;
}

/**
 * Draw a one-colour page (a formula) with a transparent background, given its ink colour.
 */
export async function rasterizeInk(b64: string, widthPx: number, ink: string): Promise<HTMLCanvasElement> {
  const rgb = hexRgb(ink);
  const onBlack = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2] > 150; // light ink: white would hide it
  const cv = await rasterizePdf(b64, widthPx, 1, onBlack ? "rgb(0,0,0)" : "rgb(255,255,255)");
  return unblend(cv, rgb, onBlack);
}

/**
 * Like rasterizeInk, but the ink colour is found in the page itself: the pixels that differ
 * most from the white background are fully covered, so they show the true colour. This is what
 * lets a coloured formula keep its colour when the file is opened again.
 */
export async function rasterizeAuto(b64: string, widthPx: number): Promise<HTMLCanvasElement> {
  const cv = await rasterizePdf(b64, widthPx); // white background
  const d = cv.getContext("2d")!.getImageData(0, 0, cv.width, cv.height).data;
  let maxD = 0;
  for (let i = 0; i < d.length; i += 4) maxD = Math.max(maxD, 765 - d[i]! - d[i + 1]! - d[i + 2]!);
  if (maxD < 30) {
    // Nearly white ink (or an empty page): detect it against black instead.
    const dark = await rasterizePdf(b64, widthPx, 1, "rgb(0,0,0)");
    const dd = dark.getContext("2d")!.getImageData(0, 0, dark.width, dark.height).data;
    let best = 0;
    for (let i = 0; i < dd.length; i += 4) best = Math.max(best, dd[i]! + dd[i + 1]! + dd[i + 2]!);
    if (best < 30) return document.createElement("canvas"); // nothing drawn
    const sum = [0, 0, 0]; let n = 0;
    for (let i = 0; i < dd.length; i += 4) if (dd[i]! + dd[i + 1]! + dd[i + 2]! >= 0.9 * best) { sum[0]! += dd[i]!; sum[1]! += dd[i + 1]!; sum[2]! += dd[i + 2]!; n++; }
    return unblend(dark, [Math.round(sum[0]! / n), Math.round(sum[1]! / n), Math.round(sum[2]! / n)], true);
  }
  const sum = [0, 0, 0]; let n = 0;
  for (let i = 0; i < d.length; i += 4) {
    if (765 - d[i]! - d[i + 1]! - d[i + 2]! >= 0.9 * maxD) { sum[0]! += d[i]!; sum[1]! += d[i + 1]!; sum[2]! += d[i + 2]!; n++; }
  }
  return unblend(cv, [Math.round(sum[0]! / n), Math.round(sum[1]! / n), Math.round(sum[2]! / n)], false);
}
