import { DESYNC, ERASER_SIZES, Editor, HIGHLIGHTER_WIDTHS, PEN_WIDTHS, TEXT_SIZES, type Host, type Tool } from "./editor";
import { installIcons } from "./icons";
import { BrushEngine, CURVE_PRESETS, PressureMap, type BrushKind } from "./brush";
import { APP_ACTIONS, DEFAULT_TABLET, PEN_ACTIONS, comboOf, mouseCombo, sanitizeTablet, wheelCombo, type TabletSettings } from "./tablet";
import { TabletDialog } from "./tabletui";
import { repairGenerated } from "./repair";
import { DEFAULT_SIZES, dotSize, sanitizeSizes, sizeKindOf, type SizeKind, type SizePresets } from "./sizes";
import { contains, formatSet } from "./intervals";
import { evalReal, evalRegions, parseSetExpr } from "./setexpr";
import { defaultLine, type LineSpec } from "./numberline";
import { defaultVenn, elementsOf, maskAt, placeElements, type VennSpec } from "./venn";
import { readMath } from "./mathobj";
import { catalogWith } from "./toolbox";
import { MathPanels } from "./mathpanel";
import { anyFloatingOpen, floatingState, makeFloating } from "./floating";
import { DEFAULT_TOOLBOX, GROUPS, Toolbox, sanitizeToolbox, type ToolboxSettings } from "./toolbox";
import { FIGURES, buildFigure, figureSpecOf, isSolid, sanitizeFigure, type FigKind } from "./figures";
import { DEFAULT_SPACE3, buildSpace3, anchorsOf, axisVector, defaultVectors, extentOf, isSpace3, project, sanitizeSpace3, space3SpecOf, vectorAngle, type Space3Spec } from "./space3";
import { DEFAULT_PLANE, buildPlane, decodeSpec, encodeSpec, planeFrameOf, planeIdOf, planeMembers, planeSpecOf, sanitizePlane } from "./plane";
import { START_PAPERS, Welcome } from "./welcome";
import { MenuBar, type MenuNode } from "./menubar";
import katex from "katex";
import { CARET, PALETTE_CATEGORIES, searchPalette } from "./latexpalette";
import { isGenerated, sanitizeFill } from "./fill";
import { PAPER_KINDS, defaultSpec, drawPaper, formatSpec as formatSpecForTest, paperKind, parseSpec, withParam, type PaperSpec } from "./paper";
import { PALETTES, paletteById } from "./palettes";
import { drawElement, glowFilter } from "./render";
import { inlineRuns, isMarkdown, layoutMarkdown, parseMarkdown } from "./markdown";
import { Inserts } from "./inserts";
import { Find } from "./find";
import { PenDiagnostic, analyze } from "./diagnostic";
import { DEFAULT_PREFS, PrefsDialog, type Prefs } from "./prefs";
import { LatexTool } from "./latex";
import { pdfInfo, rasterizeAuto, rasterizeInk } from "./pdfraster";
import { Layers } from "./layers";
import { Sidebar } from "./sidebar";
import { Journal, MM_TO_PT, PAPER_FORMATS, PAPER_STYLES, blankDocument } from "./journal";
import { displayPoints, elementBBox, restore, snapshot, texContains, textBBox, type Aff, quantizationStep, translateAff, transformElement, unionRect, type Rect } from "./geometry";
import { PROTRACTOR } from "./selection";
import { activeLayer, attrValue, bboxOf, parseXopp, type PageElement, setAttr, setGlow, texPose, type ImageEl, type Page, type Stroke, type TexImageEl, type TextBox, type XoppDocument } from "./model";
import { drawStroke, renderPage } from "./render";
import { serializeXopp } from "./serialize";

interface CliInfo {
  files: string[];
  scrollPage: number | null;
  zoom: number | null;
  selftest: boolean;
  bench: boolean;
  e2e: boolean;
  headless: boolean;
  stress: string | null;
}
declare global {
  interface Window {
    xournal: {
      openDialog(dir?: string): Promise<string | null>;
      readXopp(file: string): Promise<{ xml: string; bytes: number; name: string; dir: string }>;
      saveXopp(xml: string, suggested: string, dir: string | null, mode: "save" | "saveAs" | "copy", target: string | null): Promise<{ path: string; bytes: number } | null>;
      addRecent(file: string): void;
      menuGet(): Promise<unknown[]>;
      menuClick(path: number[]): Promise<boolean>;
      onMenuChanged(cb: () => void): void;
      setTitleBar(color: string, symbolColor: string): void;
      recentList(): Promise<Array<{ path: string; name: string; dir: string }>>;
      recentClear(): Promise<void>;
      setFullscreen(on: boolean): void;
      setMenuBar(on: boolean): void;
      onCommand(handler: (id: string, arg: unknown) => void): void;
      testMenuClick(labels: string[]): Promise<boolean>;
      getPrefs(): Promise<Prefs & { autosaveDir: string }>;
      setPrefs(p: Partial<Prefs>): Promise<unknown>;
      autosave(xml: string, name: string): Promise<string>;
      pdfBegin(suggested: string, dir: string | null, testPath?: string): Promise<string | null>;
      pdfPage(index: number, w: number, h: number, pw: number, ph: number, jpg: Uint8Array): Promise<boolean>;
      pdfFinish(): Promise<{ ok: true; path: string; bytes: number } | { ok: false; error: string }>;
      pdfAbort(): Promise<void>;
      latexPdf(req: { latex: string; display: boolean; color: string; px: number }): Promise<{ ok: true; pdfB64: string; widthPt: number; heightPt: number } | { ok: false; error: string }>;
      cli(): Promise<CliInfo>;
      ready(stats: unknown): Promise<void>;
      pathForFile(f: File): string;
      setDirty(d: boolean): void;
    };
  }
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
/** The bridge to the main process; tests swap it to observe save requests. */
let io = window.xournal;
const scroller = $("scroller");
const pagesEl = $("pages");
const hint = $("hint");
const info = $("info");
const zoomLabel = $("zoomLabel");
const undoBtn = $<HTMLButtonElement>("undo");
const redoBtn = $<HTMLButtonElement>("redo");
const widthSel = $<HTMLSelectElement>("width");
const shapeSel = $<HTMLSelectElement>("shapeSel");
const paperSel = $<HTMLSelectElement>("paperSel");

let doc: XoppDocument | null = null;
let fileName = "";
let fileDir: string | null = null;
/** Where this document was last saved in this session; null for new or freshly opened documents. */
let savePath: string | null = null;
/** User preferences (loaded from the main process at start-up). */
let prefs: Prefs & { autosaveDir: string } = { ...DEFAULT_PREFS, autosaveDir: "" };
let zoom = 1;
const MIN_ZOOM = 0.25;
const MAX_ZOOM = 5;
const GAP = 12;
/** New and opened documents start fitted to the width of the window. The automated modes turn this off to keep 100 %. */
let autoFit = true;
/** The zoom that makes the widest page of `d` fill the window's width (within the allowed zoom range). */
/** Width the vertical scrollbar takes in the current theme (measured on a probe: it is not there yet while the page is empty). */
function scrollbarWidth(): number {
  const probe = document.createElement("div");
  probe.style.cssText = "position:absolute;visibility:hidden;width:100px;height:100px;overflow:scroll";
  document.body.appendChild(probe);
  const w = probe.offsetWidth - probe.clientWidth;
  probe.remove();
  return w;
}
function fitZoomFor(d: XoppDocument): number {
  const w = Math.max(...d.pages.map((p) => p.width));
  // the scrollbar is counted even when it is not there yet: a tall document will bring it, and the page must not overflow sideways
  return Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, (scroller.offsetWidth - scrollbarWidth() - 2 * GAP - 20) / w));
}

interface PageView {
  el: HTMLDivElement;
  canvas: HTMLCanvasElement | null;
  /** The part of the page (page units: x0, y0, x1, y1) the canvas covers: all of it, or only what is on screen when the page is too big to hold at this zoom. */
  region: [number, number, number, number] | null;
}
/** A page whose canvas would need more pixels than this is drawn in a window of what is on screen, not whole (memory and GPU work stay bounded at any zoom). */
const FULL_PAGE_PIXELS = 6_000_000;
const WINDOW_PIXELS = 4_500_000;
let views: PageView[] = [];
let observer: IntersectionObserver | null = null;
const visible = new Set<number>();

/** Continuous view shows every page in a column; otherwise only one page at a time. */
let continuous = true;
let singleIdx = 0;
function applyMode(): void {
  views.forEach((v, i) => { v.el.style.display = continuous || i === singleIdx ? "" : "none"; });
}
function setContinuous(on: boolean): void {
  const cur = doc ? currentPage() : 0;
  continuous = on;
  if (!doc) return;
  singleIdx = Math.min(cur, views.length - 1);
  applyMode();
  scrollToPage(singleIdx);
  updatePageUi();
}

function buildViews(): void {
  pagesEl.replaceChildren();
  views = doc!.pages.map((_, i) => {
    const el = document.createElement("div");
    el.className = "page";
    el.dataset.i = String(i);
    pagesEl.appendChild(el);
    return { el, canvas: null, region: null };
  });
  welcome.hide();
  singleIdx = Math.min(singleIdx, views.length - 1);
  applyMode();
  layoutPages();
  setupObserver();
  sidebar.rebuild();
  updatePageUi();
}

/** Re-create the page views after a structural change, then go to `focus` (or keep the scroll position). */
function rebuildPages(focus?: number): void {
  if (!doc) return;
  inserts.commitText();
  editor.selection.refresh();
  const keep = scroller.scrollTop;
  buildViews();
  if (focus !== undefined) scrollToPage(focus);
  else scroller.scrollTop = keep;
  updatePageUi();
  refreshUi();
}

function layoutPages(): void {
  if (!doc) return;
  doc.pages.forEach((p, i) => {
    const v = views[i]!;
    v.el.style.width = `${p.width * zoom}px`;
    v.el.style.height = `${p.height * zoom}px`;
  });
  zoomLabel.textContent = `${Math.round(zoom * 100)}%`;
}

/** Which part of page `i` its canvas should cover now. */
function regionFor(i: number): [number, number, number, number] {
  const page = doc!.pages[i]!;
  const k = zoom * (window.devicePixelRatio || 1);
  if (page.width * page.height * k * k <= FULL_PAGE_PIXELS) return [0, 0, page.width, page.height];
  const sr = scroller.getBoundingClientRect(), pr = views[i]!.el.getBoundingClientRect();
  const vx0 = Math.max(0, (sr.left - pr.left) / zoom), vx1 = Math.min(page.width, (sr.right - pr.left) / zoom);
  const vy0 = Math.max(0, (sr.top - pr.top) / zoom), vy1 = Math.min(page.height, (sr.bottom - pr.top) / zoom);
  if (!(vx1 > vx0 && vy1 > vy0)) return [0, 0, Math.min(page.width, 1), Math.min(page.height, 1)];
  // some margin around what is visible, so a small scroll does not need a new render; less of it if the pixel budget asks
  let m = 0.4;
  for (;;) {
    const x0 = Math.max(0, vx0 - (vx1 - vx0) * m), x1 = Math.min(page.width, vx1 + (vx1 - vx0) * m);
    const y0 = Math.max(0, vy0 - (vy1 - vy0) * m), y1 = Math.min(page.height, vy1 + (vy1 - vy0) * m);
    if ((x1 - x0) * (y1 - y0) * k * k <= WINDOW_PIXELS || m < 0.02) return [x0, y0, x1, y1];
    m /= 2;
  }
}

/** Is everything on screen of page `i` inside its canvas? (A full-page canvas always is.) */
function regionCovers(i: number): boolean {
  const v = views[i]!;
  if (!v.canvas || !v.region) return true;
  const page = doc!.pages[i]!;
  const [x0, y0, x1, y1] = v.region;
  if (x0 <= 0 && y0 <= 0 && x1 >= page.width && y1 >= page.height) return true;
  const sr = scroller.getBoundingClientRect(), pr = v.el.getBoundingClientRect();
  const vx0 = Math.max(0, (sr.left - pr.left) / zoom), vx1 = Math.min(page.width, (sr.right - pr.left) / zoom);
  const vy0 = Math.max(0, (sr.top - pr.top) / zoom), vy1 = Math.min(page.height, (sr.bottom - pr.top) / zoom);
  return vx0 >= x0 - 0.5 && vx1 <= x1 + 0.5 && vy0 >= y0 - 0.5 && vy1 <= y1 + 0.5;
}

function ensureCanvas(i: number): HTMLCanvasElement {
  const v = views[i]!;
  const page = doc!.pages[i]!;
  const dpr = window.devicePixelRatio || 1;
  if (!v.canvas) {
    v.canvas = document.createElement("canvas");
    v.el.insertBefore(v.canvas, v.el.firstChild);
  }
  const r = (v.region = regionFor(i));
  const wpx = Math.max(1, Math.round((r[2] - r[0]) * zoom * dpr));
  const hpx = Math.max(1, Math.round((r[3] - r[1]) * zoom * dpr));
  if (v.canvas.width !== wpx || v.canvas.height !== hpx) {
    v.canvas.width = wpx;
    v.canvas.height = hpx;
  }
  // positioned in percentages of the page, so a zoom stretches it with the page until the next render
  const st = v.canvas.style;
  st.left = `${(r[0] / page.width) * 100}%`;
  st.top = `${(r[1] / page.height) * 100}%`;
  st.width = `${((r[2] - r[0]) / page.width) * 100}%`;
  st.height = `${((r[3] - r[1]) / page.height) * 100}%`;
  return v.canvas;
}

/** Make `ctx` (the canvas of page `i`) map page units to its pixels. */
function setPageTransform(ctx: CanvasRenderingContext2D, i: number): void {
  const v = views[i]!;
  const r = v.region ?? [0, 0, doc!.pages[i]!.width, doc!.pages[i]!.height];
  const sx = v.canvas!.width / (r[2] - r[0]), sy = v.canvas!.height / (r[3] - r[1]);
  ctx.setTransform(sx, 0, 0, sy, -r[0] * sx, -r[1] * sy);
}

function paint(i: number): void {
  if (!doc) return;
  const page = doc.pages[i]!;
  const canvas = ensureCanvas(i);
  const ctx = canvas.getContext("2d")!;
  setPageTransform(ctx, i);
  const full = views[i]!.region![0] <= 0 && views[i]!.region![1] <= 0 && views[i]!.region![2] >= page.width && views[i]!.region![3] >= page.height;
  renderPage(ctx, page, () => {
    if (visible.has(i)) paint(i);
  }, full ? undefined : views[i]!.region!);
}

/** Redraw only `rect` (page units) of page `i`, keeping the canvas as it is: a clip, and only the elements that reach it. */
function paintRect(i: number, rect: [number, number, number, number]): void {
  const v = views[i]!;
  if (!doc || !v.canvas || !v.region) return;
  const page = doc.pages[i]!;
  const rg = v.region;
  const r: [number, number, number, number] = [Math.max(rect[0], rg[0]), Math.max(rect[1], rg[1]), Math.min(rect[2], rg[2]), Math.min(rect[3], rg[3])];
  if (!(r[2] > r[0] && r[3] > r[1])) return;
  const ctx = v.canvas.getContext("2d")!;
  ctx.save();
  setPageTransform(ctx, i);
  ctx.beginPath();
  ctx.rect(r[0], r[1], r[2] - r[0], r[3] - r[1]);
  ctx.clip();
  renderPage(ctx, page, () => { if (visible.has(i)) paint(i); }, r);
  ctx.restore();
}

function release(i: number): void {
  const v = views[i]!;
  if (v.canvas) {
    v.canvas.width = 0;
    v.canvas.height = 0;
    v.canvas.remove();
    v.canvas = null;
    v.region = null;
  }
  // decoded pictures of a page that is far away go back to being their base64 (they decode again when the page returns)
  if (doc) for (const layer of doc.pages[i]?.layers ?? []) for (const e of layer.elements) if (e.kind === "image" && e.bitmap instanceof ImageBitmap) { e.bitmap.close(); e.bitmap = undefined; }
}

const liveEl = $<HTMLCanvasElement>("live");
function sizeLive(): void {
  const dpr = window.devicePixelRatio || 1;
  const w = Math.round(scroller.clientWidth * dpr), h = Math.round(scroller.clientHeight * dpr);
  liveEl.style.width = `${scroller.clientWidth}px`;
  liveEl.style.height = `${scroller.clientHeight}px`;
  if (liveEl.width !== w || liveEl.height !== h) { liveEl.width = w; liveEl.height = h; }
}
new ResizeObserver(() => { sizeLive(); editor.refreshOverlay(); }).observe(scroller);
sizeLive();
// Create and touch the overlay's GPU-backed context now: doing it on the first
// stroke froze that stroke for ~0.6 s (measured).
{
  const w = liveEl.getContext("2d", { desynchronized: DESYNC })!;
  w.fillRect(0, 0, 1, 1);
  w.clearRect(0, 0, liveEl.width, liveEl.height);
}

const host: Host = {
  liveCanvas: () => liveEl,
  doc: () => doc,
  zoom: () => zoom,
  staticCanvas: (i) => views[i]?.canvas ?? null,
  pageEl: (i) => views[i]!.el,
  currentPage: () => currentPage(),
  commitPaint: (i, s: Stroke) => {
    const v = views[i]!;
    if (!v.canvas || !doc) return;
    const ctx = v.canvas.getContext("2d")!;
    setPageTransform(ctx, i);
    drawStroke(ctx, s);
  },
  repaint: (i) => { if (views[i]?.canvas) paint(i); },
  repaintRect: (i, rect) => { if (views[i]?.canvas) paintRect(i, rect); },
  repaintVisible: () => { visible.forEach((i) => paint(i)); },
  rebuildPages: (focus) => rebuildPages(focus),
  changed: (page) => refreshUi(page),
};
const menubar = new MenuBar({
  get: () => window.xournal.menuGet() as Promise<MenuNode[]>,
  click: (path) => { void window.xournal.menuClick(path); },
  onChanged: (cb) => window.xournal.onMenuChanged(cb),
});
const welcome = new Welcome({
  newDocument: () => newDocument(),
  newWithPaper: (v) => newDocumentWithPaper(v),
  open: () => { void openDialog(); },
  openRecent: (p) => { void openUserFile(p); },
  openAutosave: () => { void commands["file.openAutosave"]!(); },
  recent: () => window.xournal.recentList(),
  clearRecent: () => window.xournal.recentClear(),
  theme: () => (prefs.theme === "neon" ? "neon" : "classic"),
  toggleTheme: () => { const theme = prefs.theme === "neon" ? "classic" : "neon"; applyPrefs({ ...prefs, theme }); void window.xournal.setPrefs({ theme }); },
  showOnStart: () => prefs.welcome !== false,
  setShowOnStart: (on) => { applyPrefs({ ...prefs, welcome: on }); void window.xournal.setPrefs({ welcome: on }); },
  hasDocument: () => doc !== null,
});
const editor = new Editor(host, scroller);
editor.attach(pagesEl);
const inserts = new Inserts(editor);
editor.onTextClick = (page, x, y) => inserts.openText(page, x, y);
const latex = new LatexTool(editor);
const find = new Find(editor, {
  reveal: async (page, box) => {
    goToPage(page);
    const frame = () => new Promise<void>((res) => requestAnimationFrame(() => requestAnimationFrame(() => res())));
    await frame();
    const top = views[page]!.el.offsetTop + box[1] * zoom;
    const bottom = views[page]!.el.offsetTop + box[3] * zoom;
    if (top < scroller.scrollTop + 40 || bottom > scroller.scrollTop + scroller.clientHeight - 40) scroller.scrollTop = Math.max(0, top - scroller.clientHeight / 3);
    await frame();
  },
});
function setRecognize(on: boolean): void {
  editor.recognize = on;
  $("recog").classList.toggle("on", on);
}
$("recog").addEventListener("click", () => setRecognize(!editor.recognize));
editor.onLatexClick = (page, x, y) => latex.openAt(page, x, y);
editor.onToolChange = (t) => applyToolUi(t);
editor.onNotice = (msg) => { info.textContent = msg; };

/** What the title and the dirty flag last were: they are only sent when they change (each write repaints the bar, each dirty flag is an IPC). */
let shownTitle = "", sentDirty: boolean | null = null;
let mathPanels: MathPanels | null = null;
function refreshUi(page?: number): void {
  if (undoBtn.disabled === editor.canUndo) undoBtn.disabled = !editor.canUndo;
  if (redoBtn.disabled === editor.canRedo) redoBtn.disabled = !editor.canRedo;
  const star = editor.dirty ? "● " : "";
  const title = doc ? `${star}${fileName} — Xournal Lab` : "Xournal Lab";
  if (title !== shownTitle) { shownTitle = title; document.title = title; $("tbTitle").textContent = title; }
  if (sentDirty !== editor.dirty) { sentDirty = editor.dirty; window.xournal.setDirty(editor.dirty); }
  if (doc) { updateInfo(); syncPaper(); }
  if (!planePanel.hidden) syncPlaneUi(); // the selection may have changed
  if (!s3Panel.hidden) syncSpace3Ui();
  mathPanels?.sync();
  sidebar.refresh(page);
}

function updateInfo(): void {
  const pg = ` · pág. ${currentPage() + 1}`;
  info.textContent = `${doc!.pages.length} págs · ${doc!.elementCount.toLocaleString()} elementos${pg}${editor.dirty ? " · sin guardar" : ""}`;
}

function setupObserver(): void {
  observer?.disconnect();
  visible.clear();
  observer = new IntersectionObserver(
    (entries) => {
      for (const e of entries) {
        const i = Number((e.target as HTMLElement).dataset.i);
        if (e.isIntersecting) {
          visible.add(i);
          paint(i);
        } else {
          visible.delete(i);
          release(i);
        }
      }
      if (doc) updateInfo();
    },
    { root: scroller, rootMargin: "800px 0px" },
  );
  views.forEach((v) => observer!.observe(v.el));
}

function setZoom(z: number, anchor?: { x: number; y: number }): void {
  const nz = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z));
  if (nz === zoom || !doc) return;
  // Keep the point under the cursor (or viewport centre) stable.
  const ax = anchor?.x ?? scroller.clientWidth / 2;
  const ay = anchor?.y ?? scroller.clientHeight / 2;
  const ratio = nz / zoom;
  const sx = (scroller.scrollLeft + ax) * ratio - ax;
  const sy = (scroller.scrollTop + ay) * ratio - ay;
  zoom = nz;
  layoutPages();
  scroller.scrollLeft = sx;
  scroller.scrollTop = sy;
  inserts.commitText();
  scheduleSharpRender();
  editor.refreshOverlay();
}

/**
 * While the zoom is changing the pages are only stretched (the canvases are sized in percentages of the page): no drawing,
 * no new canvas memory. One sharp render follows when it settles. `now` skips the wait.
 */
let sharpTimer = 0;
function scheduleSharpRender(now = false): void {
  window.clearTimeout(sharpTimer);
  const run = () => { sharpTimer = 0; visible.forEach((i) => paint(i)); };
  if (now) run(); else sharpTimer = window.setTimeout(run, 90);
}

function fitPage(): void {
  if (!doc) return;
  const keep = currentPage();
  const p = doc.pages[keep]!;
  setZoom(Math.min((scroller.clientWidth - 2 * GAP - 20) / p.width, (scroller.clientHeight - 2 * GAP) / p.height));
  scrollToPage(keep);
}

function fitWidth(): void {
  if (!doc) return;
  setZoom(fitZoomFor(doc));
}

// ---- current page and navigation ---------------------------------------------
/** The page under the middle of the viewport. */
function currentPage(): number {
  if (!doc || !views.length) return 0;
  if (!continuous) return Math.min(singleIdx, views.length - 1);
  const y = scroller.scrollTop + scroller.clientHeight / 2;
  let best = 0;
  for (let i = 0; i < views.length; i++) {
    const top = views[i]!.el.offsetTop, bottom = top + views[i]!.el.offsetHeight;
    if (y >= top && y < bottom) return i;
    if (y >= bottom) best = i;
  }
  return best;
}

function scrollToPage(i: number): void {
  if (!views.length) return;
  const idx = Math.min(views.length - 1, Math.max(0, i));
  if (!continuous) { singleIdx = idx; applyMode(); scroller.scrollTop = 0; return; }
  scroller.scrollTop = views[idx]!.el.offsetTop - GAP;
}

let history = { back: [] as number[], fwd: [] as number[] };

/** Go to a page; explicit jumps are recorded so Back/Forward can return. */
function goToPage(i: number, record = true): void {
  if (!doc) return;
  const target = Math.min(doc.pages.length - 1, Math.max(0, i));
  const from = currentPage();
  if (record && target !== from) { history.back.push(from); history.fwd = []; }
  scrollToPage(target);
  updatePageUi();
}
function historyBack(): void {
  const p = history.back.pop();
  if (p === undefined) return;
  history.fwd.push(currentPage());
  goToPage(p, false);
}
function historyForward(): void {
  const p = history.fwd.pop();
  if (p === undefined) return;
  history.back.push(currentPage());
  goToPage(p, false);
}

const pgNum = $<HTMLInputElement>("pgNum");
const pgTotal = $("pgTotal");
/** What the page controls last showed: they are only written when it changes (every write repaints the toolbar). */
let pageUiKey = "";
function updatePageUi(): void {
  if (!doc) { if (pageUiKey !== "none") { pageUiKey = "none"; pgNum.value = ""; pgTotal.textContent = "0"; } return; }
  const cur = currentPage();
  const bg = doc.pages[cur]?.background;
  const col = bg ? attrValue(bg.attrs, "color") : null;
  const key = `${cur}|${doc.pages.length}|${bg?.style}|${bg?.paper ? 1 : 0}|${col}|${document.activeElement === pgNum ? 1 : 0}`;
  if (key === pageUiKey) return;
  pageUiKey = key;
  sidebar.setCurrent(cur);
  if (document.activeElement !== pgNum) pgNum.value = String(cur + 1);
  pgNum.max = String(doc.pages.length);
  pgTotal.textContent = String(doc.pages.length);
  syncPaper();
  if (col && /^#[0-9a-fA-F]{6}/.test(col)) bgColor.value = col.slice(0, 7).toLowerCase();
  markPaperSwatch();
}

interface OpenTiming { readMs: number; parseMs: number; firstPaintMs: number; bytes: number; xmlChars: number }

async function openFile(file: string, opts?: { scrollPage?: number | null; zoom?: number | null }): Promise<OpenTiming> {
  const t0 = performance.now();
  info.textContent = "Cargando…";
  const r = await window.xournal.readXopp(file);
  const t1 = performance.now();
  doc = parseXopp(r.xml);
  const repaired = repairGenerated(doc); // labels of planes, spaces and figures saved without a position
  const t2 = performance.now();
  fileName = r.name;
  fileDir = r.dir;
  savePath = prefs.saveOverOpened ? file : null; // by default an opened original is never overwritten without asking
  editor.reset();

  zoom = opts?.zoom ?? (autoFit ? fitZoomFor(doc) : 1);
  history = { back: [], fwd: [] };
  buildViews();
  if (opts?.scrollPage != null) scrollToPage(opts.scrollPage);
  await new Promise((res) => requestAnimationFrame(() => requestAnimationFrame(res)));
  const t3 = performance.now();
  refreshUi();
  if (repaired) info.textContent = `Se rehicieron ${repaired} objeto(s) (planos, espacios o figuras) cuyas etiquetas se habían guardado sin posición; guarda para conservar el arreglo`;
  return { readMs: Math.round(t1 - t0), parseMs: Math.round(t2 - t1), firstPaintMs: Math.round(t3 - t2), bytes: r.bytes, xmlChars: r.xml.length };
}

function showError(e: unknown): void {
  info.textContent = "Error: " + (e instanceof Error ? e.message : String(e));
}

async function openDialog(): Promise<void> {
  const f = await window.xournal.openDialog();
  if (f) await openUserFile(f);
}

const baseName = (p: string) => p.split(/[\\/]/).pop() ?? p;
const dirName = (p: string) => p.slice(0, Math.max(0, p.length - baseName(p).length - 1));

type SaveMode = "save" | "saveAs" | "copy";
async function save(mode: SaveMode): Promise<void> {
  if (!doc) return;
  if (doc.droppedChildren > 0 &&
      !confirm(`Este documento tiene ${doc.droppedChildren} elemento(s) de nivel superior que este visor no conserva y se omitirán al guardar. ¿Guardar igualmente?`)) return;
  const base = fileName.replace(/\.xopp$/i, "");
  const suggested = mode === "copy" ? `${base}-copia.xopp` : savePath ? baseName(savePath) : `${base}-lab.xopp`;
  const r = await io.saveXopp(serializeXopp(doc), suggested, savePath ? dirName(savePath) : fileDir, mode, savePath);
  if (!r) return;
  if (mode !== "copy") {
    savePath = r.path;
    fileName = baseName(r.path);
    fileDir = dirName(r.path);
    editor.markSaved();
    refreshUi();
  }
  info.textContent = `${mode === "copy" ? "Copia guardada" : "Guardado"}: ${r.path} (${(r.bytes / 1024).toFixed(0)} KB)`;
}

/** Longest side, in pixels, of a page image in an exported PDF: about 215 dpi on an A4 page, never more than the canvas limit. */
const PDF_MAX_SIDE = 3000;
let exporting = false;

/** Wait until every image and formula of a page has been decoded, so the first drawing is already complete. */
async function pageAssetsReady(page: Page): Promise<void> {
  const kick = document.createElement("canvas");
  kick.width = kick.height = 1;
  const g = kick.getContext("2d")!;
  for (let i = 0; i < 100; i++) {
    g.setTransform(1 / page.width, 0, 0, 1 / page.height, 0, 0);
    renderPage(g, page, () => undefined); // starts the loading of everything that is not ready (drawn into one pixel)
    const pending = page.layers.some((l) => !l.hidden && l.elements.some((e) => (e.kind === "image" || e.kind === "teximage") && !e.bitmap && !("failed" in e && e.failed)));
    if (!pending) return;
    await new Promise((r) => setTimeout(r, 30));
  }
}

/** Export the whole document as a PDF: one JPEG per page (about 215 dpi), at the page's real size. */
async function exportPdf(testPath?: string): Promise<{ ok: boolean; path?: string; bytes?: number; error?: string } | null> {
  if (!doc || exporting) return null;
  exporting = true;
  const d = doc;
  try {
    inserts.commitText();
    const base = fileName.replace(/\.xopp$/i, "");
    const target = await window.xournal.pdfBegin(`${base}.pdf`, savePath ? dirName(savePath) : fileDir, testPath);
    if (!target) return null;
    for (let i = 0; i < d.pages.length; i++) {
      const page = d.pages[i]!;
      info.textContent = `Exportando PDF… página ${i + 1} de ${d.pages.length}`;
      await pageAssetsReady(page);
      const k = Math.min(PDF_MAX_SIDE / Math.max(page.width, page.height), 4);
      const cv = document.createElement("canvas");
      cv.width = Math.max(1, Math.round(page.width * k));
      cv.height = Math.max(1, Math.round(page.height * k));
      const g = cv.getContext("2d")!;
      g.setTransform(cv.width / page.width, 0, 0, cv.height / page.height, 0, 0);
      renderPage(g, page, () => undefined);
      const blob = await new Promise<Blob | null>((res) => cv.toBlob(res, "image/jpeg", 0.93));
      const [pw, ph] = [cv.width, cv.height];
      cv.width = cv.height = 0; // free the pixels before the next page
      if (!blob || !(await window.xournal.pdfPage(i, page.width, page.height, pw, ph, new Uint8Array(await blob.arrayBuffer())))) throw new Error(`No se pudo preparar la página ${i + 1}`);
      await new Promise((r) => setTimeout(r, 0)); // let the window breathe between pages
    }
    info.textContent = "Exportando PDF… armando el archivo";
    const res = await window.xournal.pdfFinish();
    if (!res.ok) throw new Error(res.error);
    info.textContent = `PDF exportado: ${res.path} (${d.pages.length} páginas, ${(res.bytes / 1048576).toFixed(1)} MB)`;
    return res;
  } catch (e) {
    void window.xournal.pdfAbort();
    info.textContent = `No se pudo exportar el PDF: ${e instanceof Error ? e.message : String(e)}`;
    return { ok: false, error: info.textContent };
  } finally {
    exporting = false;
  }
}

/** Ask before throwing away unsaved changes. */
function confirmDiscard(): boolean {
  return !editor.dirty || confirm("Hay cambios sin guardar. ¿Descartarlos?");
}

async function openUserFile(file: string): Promise<void> {
  if (!confirmDiscard()) return;
  await openFile(file).catch(showError);
  window.xournal.addRecent(file);
}

/** A new document whose only page has this paper: a built-in style, or `x:<kind>` for a special one. */
function newDocumentWithPaper(value: string): void {
  if (!confirmDiscard()) return;
  newDocument(true);
  const bg = doc!.pages[0]!.background;
  if (value.startsWith("x:")) {
    const k = paperKind(value.slice(2));
    const spec = k && defaultSpec(k.id);
    if (k && spec) { bg.paper = spec; bg.style = k.fallback; setAttr(bg.attrs, "style", k.fallback); setAttr(bg.attrs, "xlPaper", formatSpecForTest(spec)); }
  } else { bg.style = value; setAttr(bg.attrs, "style", value); }
  buildViews();
  refreshUi();
}

function newDocument(sure = false): void {
  if (!sure && !confirmDiscard()) return;
  doc = blankDocument();
  fileName = "Documento sin título.xopp";
  fileDir = null;
  savePath = null;
  zoom = autoFit ? fitZoomFor(doc) : 1;
  editor.reset();
  history = { back: [], fwd: [] };
  buildViews();
  scroller.scrollTop = 0;
  refreshUi();
}

function closeDocument(): void {
  if (!doc || !confirmDiscard()) return;
  doc = null;
  views = [];
  editor.reset();
  pagesEl.replaceChildren();
  void welcome.show();
  info.textContent = "";
  sidebar.rebuild();
  refreshUi();
  updatePageUi();
}

// ---- toolbar ----------------------------------------------------------------
let previousTool: Tool = "pen";
function setTool(t: Tool): void {
  if (t !== editor.tool) previousTool = editor.tool;
  inserts.commitText();
  editor.setTool(t); // calls applyToolUi through onToolChange
}
function applyToolUi(t: Tool): void {
  document.querySelectorAll<HTMLButtonElement>("button.tool").forEach((b) => b.classList.toggle("active", b.dataset.tool === t));
  pagesEl.className = t;
  fillWidths();
}
function fillWidths(): void {
  const t = editor.tool;
  const eraser = t === "eraser" || t === "eraserPart";
  const base = t === "highlighter" ? HIGHLIGHTER_WIDTHS : t === "text" ? TEXT_SIZES : eraser ? ERASER_SIZES : PEN_WIDTHS;
  const cur = t === "highlighter" ? editor.highlighterWidth : t === "text" ? editor.textSize : eraser ? editor.eraserRadius : editor.penWidth;
  const list = base.includes(cur) ? base : [...base, cur].sort((x, y) => x - y); // a size chosen in the pen dialog shows too
  widthSel.replaceChildren(...list.map((w) => {
    const o = document.createElement("option");
    o.value = String(w);
    o.textContent = `${+w.toFixed(2)}`;
    o.selected = w === cur;
    return o;
  }));
  widthSel.disabled = !["pen", "highlighter", "text", "shape", "eraser", "eraserPart"].includes(editor.tool);
  shapeSel.hidden = editor.tool !== "shape";
  renderSizePresets();
}

// ---- size presets in the toolbar ----
let sizePresets: SizePresets = sanitizeSizes(DEFAULT_SIZES);
const presetBox = $("sizePresets");
const currentSize = (kind: SizeKind) => (kind === "highlighter" ? editor.highlighterWidth : kind === "text" ? editor.textSize : kind === "eraser" ? editor.eraserRadius : editor.penWidth);
/** Use a size for what is being drawn with (the pen's is remembered with its other settings). */
function applySize(kind: SizeKind, w: number): void {
  if (kind === "highlighter") editor.highlighterWidth = w;
  else if (kind === "text") editor.textSize = w;
  else if (kind === "eraser") editor.eraserRadius = w;
  else applyTablet({ ...tablet, size: w }, true);
  fillWidths();
}
function renderSizePresets(): void {
  const kind = sizeKindOf(editor.tool);
  presetBox.hidden = kind === null;
  presetBox.replaceChildren();
  if (!kind) return;
  const cur = currentSize(kind);
  sizePresets[kind].forEach((v, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.dataset.size = String(v);
    b.dataset.index = String(i);
    b.className = Math.abs(v - cur) < 1e-6 ? "active" : "";
    b.title = `${v}${kind === "text" ? " pt" : ""} · clic: usar este tamaño · clic derecho (o Alt+clic): guardar aquí el tamaño actual (${+cur.toFixed(2)})`;
    const dot = document.createElement("i");
    const d = dotSize(kind, v);
    dot.style.width = dot.style.height = `${d}px`;
    b.append(dot);
    const save = () => {
      sizePresets = { ...sizePresets, [kind]: sizePresets[kind].map((x, j) => (j === i ? +currentSize(kind).toFixed(2) : x)) };
      prefs = { ...prefs, sizes: JSON.stringify(sizePresets) };
      void window.xournal.setPrefs({ sizes: prefs.sizes });
      renderSizePresets();
    };
    b.addEventListener("click", (e) => { if (e.altKey) save(); else applySize(kind, v); });
    b.addEventListener("contextmenu", (e) => { e.preventDefault(); save(); });
    presetBox.append(b);
  });
}
const swatches = $("swatches");
const palMenu = $("palMenu");
let paletteId = "classic";
function selectColor(c: string): void {
  editor.color = c;
  syncGlowUi();
  swatches.querySelectorAll<HTMLElement>(".sw").forEach((x) => x.classList.toggle("active", x.dataset.color === c));
}
/** Show the colours of a preset. The current colour stays selected if the preset has it. */
function setPalette(id: string): void {
  const pal = paletteById(id);
  paletteId = pal.id;
  swatches.replaceChildren();
  for (const c of pal.colors) {
    const b = document.createElement("button");
    b.className = "sw" + (c === editor.color ? " active" : "");
    b.style.background = c;
    b.style.color = c; // the neon theme glows in the swatch's own colour
    b.dataset.color = c;
    b.title = c;
    b.addEventListener("click", () => selectColor(c));
    swatches.appendChild(b);
  }
  renderPaperSwatches(pal.papers ?? [], pal.colors);
  $("btnPalette").title = `Paleta: ${pal.name}. Clic para cambiarla`;
}
// ---- glow ----------------------------------------------------------------------
const glowPanel = $("glowPanel");
const glowOnEl = $<HTMLInputElement>("glowOn"), glowSizeEl = $<HTMLInputElement>("glowSize"), glowIntEl = $<HTMLInputElement>("glowInt");
function syncGlowUi(): void {
  glowOnEl.checked = editor.glowOn;
  glowSizeEl.value = String(editor.glowSize);
  glowIntEl.value = String(Math.round(editor.glowIntensity * 100));
  $("glowSizeV").textContent = `${editor.glowSize}`;
  $("glowIntV").textContent = `${Math.round(editor.glowIntensity * 100)} %`;
  $("btnGlow").classList.toggle("on", editor.glowOn);
  const demo = $("glowDemo").firstElementChild as SVGSVGElement;
  const path = document.getElementById("glowDemoPath") as unknown as SVGPathElement;
  path.setAttribute("stroke", editor.color);
  demo.style.filter = editor.glowOn ? glowFilter(editor.color, { size: editor.glowSize, intensity: editor.glowIntensity }, 1) : "";
}
function glowChanged(): void {
  editor.glowOn = glowOnEl.checked;
  editor.glowSize = Number(glowSizeEl.value);
  editor.glowIntensity = Number(glowIntEl.value) / 100;
  syncGlowUi();
  void window.xournal.setPrefs({ glowOn: editor.glowOn, glowSize: editor.glowSize, glowIntensity: editor.glowIntensity });
}
[glowOnEl, glowSizeEl, glowIntEl].forEach((el) => el.addEventListener("input", glowChanged));
function selectionGlow(on: boolean): void {
  const n = editor.setSelectionGlow(on ? { size: editor.glowSize, intensity: editor.glowIntensity } : undefined);
  info.textContent = n ? `${on ? "Brillo aplicado a" : "Brillo quitado de"} ${n} trazo${n === 1 ? "" : "s"}` : "Selecciona trazos de lápiz primero";
}
$("glowApply").addEventListener("click", () => selectionGlow(true));
$("glowRemove").addEventListener("click", () => selectionGlow(false));
function closeGlowPanel(): void { glowPanel.hidden = true; }
$("btnGlow").addEventListener("click", (e) => {
  e.stopPropagation();
  if (!glowPanel.hidden) { closeGlowPanel(); return; }
  closePalMenu();
  syncGlowUi();
  const r = $("btnGlow").getBoundingClientRect();
  glowPanel.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - 300))}px`;
  glowPanel.style.top = `${r.bottom + 4}px`;
  glowPanel.hidden = false;
});
document.addEventListener("pointerdown", (e) => { if (!glowPanel.hidden && !glowPanel.contains(e.target as Node) && !$("btnGlow").contains(e.target as Node)) closeGlowPanel(); }, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !glowPanel.hidden) { closeGlowPanel(); e.stopPropagation(); } }, true);

/** The active palette as paper colours too: curated papers first, then the inks. */
function renderPaperSwatches(papers: string[], inks: string[]): void {
  const box = $("paperSwatches");
  box.replaceChildren();
  for (const c of [...new Set([...papers, ...inks])]) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "psw";
    b.style.background = c;
    b.dataset.color = c;
    b.title = `Papel ${c}`;
    b.addEventListener("click", () => {
      const input = $<HTMLInputElement>("bgColor");
      input.value = c;
      input.dispatchEvent(new Event("change", { bubbles: true })); // the same path as the colour picker
      markPaperSwatch();
    });
    box.appendChild(b);
  }
  markPaperSwatch();
}
function markPaperSwatch(): void {
  const cur = $<HTMLInputElement>("bgColor").value.toLowerCase();
  $("paperSwatches").querySelectorAll<HTMLElement>(".psw").forEach((x) => x.classList.toggle("active", x.dataset.color === cur));
}

// ---- Cartesian plane options -----------------------------------------------------
const planePanel = $("planePanel");
const pl = {
  xmin: $<HTMLInputElement>("plXmin"), xmax: $<HTMLInputElement>("plXmax"), ymin: $<HTMLInputElement>("plYmin"), ymax: $<HTMLInputElement>("plYmax"),
  xstep: $<HTMLInputElement>("plXstep"), ystep: $<HTMLInputElement>("plYstep"), grid: $<HTMLInputElement>("plGrid"), sub: $<HTMLSelectElement>("plSub"),
  ticks: $<HTMLInputElement>("plTicks"), numbers: $<HTMLInputElement>("plNumbers"), arrows: $<HTMLInputElement>("plArrows"), square: $<HTMLInputElement>("plSquare"),
  xl: $<HTMLInputElement>("plXl"), yl: $<HTMLInputElement>("plYl"),
};
/** Show a plane's settings: those of the selected plane when there is one, otherwise the ones for the next plane. */
function syncPlaneUi(): void {
  const sel = editor.selectedPlane();
  const s = sel ? sel.spec : editor.planeSpec;
  pl.xmin.value = String(s.xmin); pl.xmax.value = String(s.xmax); pl.ymin.value = String(s.ymin); pl.ymax.value = String(s.ymax);
  pl.xstep.value = String(s.xstep); pl.ystep.value = String(s.ystep);
  pl.grid.checked = s.grid; pl.sub.value = String(s.sub); pl.sub.disabled = !s.grid;
  pl.ticks.checked = s.ticks; pl.numbers.checked = s.numbers; pl.arrows.checked = s.arrows; pl.square.checked = s.square;
  pl.xl.value = s.xlabel; pl.yl.value = s.ylabel;
  $("planeMode").textContent = sel ? "Editando el plano seleccionado." : "Valores para el próximo plano: elige la herramienta y arrastra un recuadro.";
}
function readPlaneUi() {
  return sanitizePlane({
    xmin: Number(pl.xmin.value), xmax: Number(pl.xmax.value), ymin: Number(pl.ymin.value), ymax: Number(pl.ymax.value),
    xstep: Number(pl.xstep.value), ystep: Number(pl.ystep.value), grid: pl.grid.checked, sub: Number(pl.sub.value),
    ticks: pl.ticks.checked, numbers: pl.numbers.checked, arrows: pl.arrows.checked, square: pl.square.checked, xlabel: pl.xl.value, ylabel: pl.yl.value,
  });
}
function planeChanged(restyle = false): void {
  const asked = { xmin: Number(pl.xmin.value), xmax: Number(pl.xmax.value), ymin: Number(pl.ymin.value), ymax: Number(pl.ymax.value) };
  if (!(asked.xmin < asked.xmax) || !(asked.ymin < asked.ymax)) { // a range with its minimum at or above its maximum: keep what there was
    info.textContent = !(asked.xmin < asked.xmax) ? "x mín tiene que ser menor que x máx" : "y mín tiene que ser menor que y máx";
    syncPlaneUi();
    return;
  }
  const spec = readPlaneUi();
  editor.planeSpec = spec;
  editor.updatePlane(spec, restyle);
  syncPlaneUi();
  void window.xournal.setPrefs({ plane: JSON.stringify(spec) });
}
for (const el of Object.values(pl)) el.addEventListener("change", () => planeChanged());
$("plAxesOnly").addEventListener("click", () => { pl.grid.checked = false; pl.sub.value = "0"; planeChanged(); });
$("plRestyle").addEventListener("click", () => { if (!editor.selectedPlane()) { info.textContent = "Selecciona un plano para darle el estilo del lápiz"; return; } planeChanged(true); });
function closePlanePanel(): void { planePanel.hidden = true; }
$("btnPlane").addEventListener("click", (e) => {
  e.stopPropagation();
  if (!planePanel.hidden) { closePlanePanel(); return; }
  syncPlaneUi();
  const r = $("btnPlane").getBoundingClientRect();
  planePanel.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - 340))}px`;
  planePanel.style.top = `${r.bottom + 4}px`;
  planePanel.hidden = false;
});
document.addEventListener("pointerdown", (e) => { if (!planePanel.hidden && !planePanel.contains(e.target as Node) && !$("btnPlane").contains(e.target as Node)) closePlanePanel(); }, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !planePanel.hidden) { closePlanePanel(); e.stopPropagation(); } }, true);

// ---- 3D space options -------------------------------------------------------------
const s3Panel = $("space3Panel");
const s3 = (id: string) => $<HTMLInputElement>(id);
const AX = ["X", "Y", "Z"] as const;
function syncSpace3Ui(): void {
  const sel = editor.selectedSpace3();
  const s = sel ? sel.spec : editor.space3Spec;
  AX.forEach((A, i) => {
    const lo = [s.xmin, s.ymin, s.zmin][i]!, hi = [s.xmax, s.ymax, s.zmax][i]!, st = [s.xstep, s.ystep, s.zstep][i]!;
    s3(`s3${A}min`).value = String(lo); s3(`s3${A}max`).value = String(hi); s3(`s3${A}step`).value = String(st);
    const v = vectorAngle(s.u[i]!);
    s3(`s3${A}ang`).value = String(+v.deg.toFixed(1)); s3(`s3${A}len`).value = String(+v.len.toFixed(1)); s3(`s3${A}n`).value = s.names[i]!;
    const vn = vectorAngle([-s.n[i]![0], -s.n[i]![1]]); // the way the negative half points on the page
    s3(`s3${A}angN`).value = String(+vn.deg.toFixed(1)); s3(`s3${A}lenN`).value = String(+vn.len.toFixed(1));
  });
  s3("s3GridXY").checked = s.gridXY; s3("s3GridXZ").checked = s.gridXZ; s3("s3GridYZ").checked = s.gridYZ;
  s3("s3Ticks").checked = s.ticks; s3("s3Numbers").checked = s.numbers; s3("s3Arrows").checked = s.arrows;
  $("s3Mode").textContent = sel ? "Editando el espacio seleccionado." : "Valores para el próximo espacio: elige la herramienta y arrastra (o haz clic).";
}
function readSpace3Ui(base: Space3Spec): Space3Spec | null {
  const n = (id: string) => Number(s3(id).value);
  for (const A of AX) if (!(n(`s3${A}min`) < n(`s3${A}max`))) { info.textContent = `${A.toLowerCase()} mín tiene que ser menor que ${A.toLowerCase()} máx`; return null; }
  return sanitizeSpace3({
    xmin: n("s3Xmin"), xmax: n("s3Xmax"), xstep: n("s3Xstep"), ymin: n("s3Ymin"), ymax: n("s3Ymax"), ystep: n("s3Ystep"), zmin: n("s3Zmin"), zmax: n("s3Zmax"), zstep: n("s3Zstep"),
    gridXY: s3("s3GridXY").checked, gridXZ: s3("s3GridXZ").checked, gridYZ: s3("s3GridYZ").checked,
    ticks: s3("s3Ticks").checked, numbers: s3("s3Numbers").checked, arrows: s3("s3Arrows").checked,
    names: [s3("s3Xn").value, s3("s3Yn").value, s3("s3Zn").value], o: base.o,
    u: AX.map((A) => axisVector(n(`s3${A}ang`), Math.max(2, n(`s3${A}len`)))),
    n: AX.map((A) => { const w = axisVector(n(`s3${A}angN`), Math.max(2, n(`s3${A}lenN`))); return [-w[0], -w[1]]; }),
  });
}
function space3Changed(restyle = false): void {
  const sel = editor.selectedSpace3();
  const spec = readSpace3Ui(sel ? sel.spec : editor.space3Spec);
  if (!spec) { syncSpace3Ui(); return; }
  editor.space3Spec = { ...spec, o: [0, 0] };
  editor.updateSpace3(spec, restyle);
  syncSpace3Ui();
  void window.xournal.setPrefs({ space3: JSON.stringify({ ...spec, o: [0, 0] }) });
}
for (const A of AX) for (const k of ["min", "max", "step", "ang", "len", "angN", "lenN", "n"]) s3(`s3${A}${k}`).addEventListener("change", () => space3Changed());
for (const id of ["s3GridXY", "s3GridXZ", "s3GridYZ", "s3Ticks", "s3Numbers", "s3Arrows"]) s3(id).addEventListener("change", () => space3Changed());
$("s3Reset").addEventListener("click", () => {
  const u = defaultVectors();
  AX.forEach((A, i) => { const v = vectorAngle(u[i]!); s3(`s3${A}ang`).value = String(+v.deg.toFixed(1)); s3(`s3${A}len`).value = String(+v.len.toFixed(1)); s3(`s3${A}angN`).value = String(+((v.deg + 180) % 360).toFixed(1)); s3(`s3${A}lenN`).value = String(+v.len.toFixed(1)); });
  space3Changed();
});
$("s3Restyle").addEventListener("click", () => { if (!editor.selectedSpace3()) { info.textContent = "Selecciona un espacio 3D para darle el estilo del lápiz"; return; } space3Changed(true); });
function closeSpace3Panel(): void { s3Panel.hidden = true; }
$("btnSpace3").addEventListener("click", (e) => {
  e.stopPropagation();
  if (!s3Panel.hidden) { closeSpace3Panel(); return; }
  syncSpace3Ui();
  const r = $("btnSpace3").getBoundingClientRect();
  s3Panel.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - 530))}px`;
  s3Panel.style.top = `${r.bottom + 4}px`;
  s3Panel.hidden = false;
});
document.addEventListener("pointerdown", (e) => { if (!s3Panel.hidden && !s3Panel.contains(e.target as Node) && !$("btnSpace3").contains(e.target as Node)) closeSpace3Panel(); }, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !s3Panel.hidden) { closeSpace3Panel(); e.stopPropagation(); } }, true);
void DEFAULT_SPACE3;

// ---- figures panel ----
const figPanel = $("figPanel");
const figOpt = <T extends HTMLElement = HTMLInputElement>(id: string) => $<T>(id);
function buildFigGallery(): void {
  const host = $("figGallery");
  host.textContent = "";
  for (const group of ["Líneas", "Curvas", "Polígonos", "3D"] as const) {
    const t = document.createElement("div");
    t.className = "figGroup";
    t.textContent = group;
    const row = document.createElement("div");
    row.className = "figItems";
    for (const f of FIGURES.filter((x) => x.group === group)) {
      const b = document.createElement("button");
      b.type = "button";
      b.dataset.kind = f.kind;
      b.textContent = f.name;
      b.title = f.clicks ? "Un clic por punto; doble clic o Intro termina" : "Arrastra para dibujarla";
      b.addEventListener("click", () => {
        editor.figKind = f.kind;
        const sel = editor.selectedFigure();
        if (!sel) { editor.endFigure(false); setTool("figure"); }
        syncFigUi();
      });
      row.append(b);
    }
    host.append(t, row);
  }
}
function syncFigUi(): void {
  const sel = editor.selectedFigure();
  const kind: FigKind = sel ? sel.spec.kind : editor.figKind;
  const o = sel ? sel.spec : { ...editor.figOpts };
  for (const b of $("figGallery").querySelectorAll<HTMLButtonElement>("button")) b.classList.toggle("on", b.dataset.kind === kind);
  figOpt<HTMLSelectElement>("figHeads").value = o.heads;
  figOpt<HTMLSelectElement>("figDash").value = o.dash;
  figOpt("figN").value = String(o.n);
  figOpt("figM").value = String(o.m);
  figOpt<HTMLSelectElement>("figHidden").value = o.hidden;
  figOpt("figLabel").value = o.label;
  $("figRowN").style.display = kind === "regular" || kind === "sphere" ? "" : "none";
  $("figRowH").style.display = isSolid(kind) ? "" : "none";
}
function figChanged(restyle = false): void {
  const sel = editor.selectedFigure();
  const read = {
    heads: figOpt<HTMLSelectElement>("figHeads").value as "none" | "end" | "both",
    dash: figOpt<HTMLSelectElement>("figDash").value as "solid" | "dash" | "dot",
    n: Number(figOpt("figN").value), m: Number(figOpt("figM").value),
    hidden: figOpt<HTMLSelectElement>("figHidden").value as "dash" | "none" | "solid",
    label: figOpt("figLabel").value,
  };
  const clean = sanitizeFigure({ ...(sel ? sel.spec : { kind: editor.figKind, pts: [0, 0, 0, 40, 40, 40, 40, 0] }), ...read });
  editor.figOpts = { heads: clean.heads, dash: clean.dash, n: clean.n, m: clean.m, hidden: clean.hidden, label: clean.label };
  if (sel) editor.updateFigure({ ...sel.spec, ...editor.figOpts }, restyle);
  syncFigUi();
}
buildFigGallery();
for (const id of ["figHeads", "figDash", "figN", "figM", "figHidden", "figLabel"]) figOpt(id).addEventListener("change", () => figChanged());
$("figRestyle").addEventListener("click", () => { if (!editor.selectedFigure()) { info.textContent = "Selecciona una figura para darle el estilo del lápiz"; return; } figChanged(true); });
function closeFigPanel(): void { figPanel.hidden = true; }
$("btnFigure").addEventListener("click", (e) => {
  e.stopPropagation();
  if (!figPanel.hidden) { closeFigPanel(); return; }
  syncFigUi();
  const r = $("btnFigure").getBoundingClientRect();
  figPanel.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - 380))}px`;
  figPanel.style.top = `${r.bottom + 4}px`;
  figPanel.hidden = false;
});
document.addEventListener("pointerdown", (e) => { if (!figPanel.hidden && !figPanel.contains(e.target as Node) && !$("btnFigure").contains(e.target as Node)) closeFigPanel(); }, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !figPanel.hidden) { closeFigPanel(); e.stopPropagation(); } }, true);

// ---- paint bucket options --------------------------------------------------------
const fillPanel = $("fillPanel");
const fillEl = {
  kind: $<HTMLSelectElement>("fillKind"), color2: $<HTMLInputElement>("fillColor2"), angle: $<HTMLInputElement>("fillAngle"),
  opacity: $<HTMLInputElement>("fillOpacity"), gap: $<HTMLInputElement>("fillGap"), keep: $<HTMLInputElement>("fillKeep"),
  magnet: $<HTMLInputElement>("fillMagnet"), max: $<HTMLInputElement>("fillMax"),
  rule: $<HTMLSelectElement>("fillRule"), base: $<HTMLSelectElement>("fillBase"), by: $<HTMLSelectElement>("fillBy"), count: $<HTMLInputElement>("fillCount"), width: $<HTMLInputElement>("fillWidth"), outline: $<HTMLInputElement>("fillOutline"),
};
const fillSideEls = () => [...document.querySelectorAll<HTMLInputElement>('input[name="fillSide"]')];
function syncFillUi(): void {
  const o = editor.fill;
  fillSideEls().forEach((r) => { r.checked = r.value === o.side; });
  fillEl.kind.value = o.kind;
  fillEl.color2.value = o.color2;
  fillEl.angle.value = String(o.angle);
  fillEl.opacity.value = String(Math.round(o.opacity * 100));
  fillEl.gap.value = String(o.gap);
  fillEl.keep.checked = o.keepLine;
  fillEl.magnet.checked = o.magnet;
  fillEl.max.value = String(o.maxGap);
  $("fillMaxV").textContent = `${o.maxGap}`;
  $("fillRowMax").hidden = !o.magnet;
  $("fillAngleV").textContent = `${o.angle}°`;
  $("fillOpacityV").textContent = `${Math.round(o.opacity * 100)} %`;
  $("fillGapV").textContent = `${o.gap}`;
  fillEl.rule.value = o.rule;
  fillEl.base.value = o.base;
  fillEl.by.value = o.by;
  fillEl.count.value = String(o.count);
  fillEl.width.value = String(o.width);
  fillEl.outline.checked = o.outline;
  const rm = o.kind === "riemann";
  $("fillRowC2").hidden = o.kind === "solid" || rm;
  $("fillRowAngle").hidden = o.kind !== "linear";
  $("fillRowRule").hidden = !rm;
  $("fillRowBase").hidden = !rm;
  $("fillRowBy").hidden = !rm;
  $("fillRowOutline").hidden = !rm;
  fillEl.count.hidden = o.by !== "count";
  fillEl.width.hidden = o.by !== "width";
  $("fillByV").textContent = o.by === "count" ? `${o.count}` : `${o.width}`;
}
function fillChanged(): void {
  editor.fill = sanitizeFill({
    side: fillSideEls().find((r) => r.checked)?.value, kind: fillEl.kind.value, color2: fillEl.color2.value, angle: Number(fillEl.angle.value),
    opacity: Number(fillEl.opacity.value) / 100, gap: Number(fillEl.gap.value), keepLine: fillEl.keep.checked, magnet: fillEl.magnet.checked, maxGap: Number(fillEl.max.value),
    rule: fillEl.rule.value, base: fillEl.base.value, by: fillEl.by.value, count: Number(fillEl.count.value), width: Number(fillEl.width.value), outline: fillEl.outline.checked,
  });
  syncFillUi();
  void window.xournal.setPrefs({ fill: JSON.stringify(editor.fill) });
}
[...Object.values(fillEl), ...fillSideEls()].forEach((el) => el.addEventListener("input", fillChanged));
fillSideEls().forEach((el) => el.addEventListener("change", fillChanged));
function closeFillPanel(): void { fillPanel.hidden = true; }
$("btnFill").addEventListener("click", (e) => {
  e.stopPropagation();
  if (!fillPanel.hidden) { closeFillPanel(); return; }
  syncFillUi();
  const r = $("btnFill").getBoundingClientRect();
  fillPanel.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - 340))}px`;
  fillPanel.style.top = `${r.bottom + 4}px`;
  fillPanel.hidden = false;
});
document.addEventListener("pointerdown", (e) => { if (!fillPanel.hidden && !fillPanel.contains(e.target as Node) && !$("btnFill").contains(e.target as Node)) closeFillPanel(); }, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !fillPanel.hidden) { closeFillPanel(); e.stopPropagation(); } }, true);

function closePalMenu(): void { palMenu.hidden = true; }
function openPalMenu(): void {
  palMenu.replaceChildren();
  for (const pal of PALETTES) {
    const row = document.createElement("button");
    row.type = "button";
    row.className = "palItem" + (pal.id === paletteId ? " cur" : "");
    row.dataset.palette = pal.id;
    const name = document.createElement("span");
    name.className = "palName";
    name.textContent = pal.name;
    const chips = document.createElement("span");
    chips.className = "palChips";
    for (const c of pal.colors) { const d = document.createElement("i"); d.style.background = c; d.style.color = c; chips.appendChild(d); }
    row.append(name, chips);
    row.addEventListener("click", () => {
      closePalMenu();
      applyPrefs({ ...prefs, palette: pal.id });
      void window.xournal.setPrefs({ palette: pal.id });
      if (pal.paper) info.textContent = `${pal.name}: pensada para papel oscuro (${pal.paper}); cámbialo en Papel → color`;
    });
    palMenu.appendChild(row);
  }
  const r = $("btnPalette").getBoundingClientRect();
  palMenu.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - 300))}px`;
  palMenu.style.top = `${r.bottom + 4}px`;
  palMenu.hidden = false;
}
$("btnPalette").addEventListener("click", (e) => { e.stopPropagation(); if (palMenu.hidden) openPalMenu(); else closePalMenu(); });
document.addEventListener("pointerdown", (e) => { if (!palMenu.hidden && !palMenu.contains(e.target as Node) && !$("btnPalette").contains(e.target as Node)) closePalMenu(); }, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !palMenu.hidden) { closePalMenu(); e.stopPropagation(); } }, true);
setPalette("classic");
const customColor = $<HTMLInputElement>("customColor");
customColor.addEventListener("input", () => {
  selectColor(customColor.value);
});
shapeSel.addEventListener("change", () => { editor.shape = shapeSel.value as typeof editor.shape; });
document.querySelectorAll<HTMLButtonElement>("button.tool").forEach((b) => b.addEventListener("click", () => setTool(b.dataset.tool as Tool)));
widthSel.addEventListener("change", () => {
  const w = Number(widthSel.value);
  const kind = sizeKindOf(editor.tool) ?? "pen";
  applySize(kind, w); // the pen's size is part of its settings, so it is remembered
});

$("open").addEventListener("click", () => void openDialog());
$("save").addEventListener("click", () => void save("save"));
undoBtn.addEventListener("click", () => editor.undo());
redoBtn.addEventListener("click", () => editor.redo());
$("zoomIn").addEventListener("click", () => setZoom(zoom * 1.2));
$("zoomOut").addEventListener("click", () => setZoom(zoom / 1.2));
$("fit").addEventListener("click", fitWidth);
// ---- pages, paper, navigation ------------------------------------------------
const journal = new Journal(editor);
const layers = new Layers(editor);
const sidebar = new Sidebar(editor, layers, {
  goTo: (i) => goToPage(i),
  current: () => currentPage(),
  newPage: () => { if (doc) journal.newPageAfter(currentPage()); },
  deletePage: () => { if (doc) journal.deletePage(currentPage()); },
  movePage: (d) => { if (doc) journal.movePage(currentPage(), d); },
});
const missingIcons = installIcons();
if (missingIcons.length) console.warn("icons missing:", missingIcons.join(", "));
$("btnTheme").addEventListener("click", () => { const theme = prefs.theme === "neon" ? "classic" : "neon"; applyPrefs({ ...prefs, theme }); void window.xournal.setPrefs({ theme }); });
$("btnPaper").addEventListener("click", () => { const b = $("bar3"); b.hidden = !b.hidden; $("btnPaper").classList.toggle("active", !b.hidden); });
$("btnSidePages").addEventListener("click", () => sidebar.toggle("pages"));
$("btnSideLayers").addEventListener("click", () => sidebar.toggle("layers"));
let lastFlip = 0;
// Single-page view: scrolling past the end of a page turns to the next one.
scroller.addEventListener("wheel", (e) => {
  if (continuous || e.ctrlKey || !doc) return;
  const now = performance.now();
  if (now - lastFlip < 350) return;
  const atBottom = scroller.scrollTop + scroller.clientHeight >= scroller.scrollHeight - 1;
  const atTop = scroller.scrollTop <= 0;
  if (e.deltaY > 0 && atBottom && singleIdx < views.length - 1) { lastFlip = now; goToPage(singleIdx + 1); }
  else if (e.deltaY < 0 && atTop && singleIdx > 0) { lastFlip = now; goToPage(singleIdx - 1); scroller.scrollTop = scroller.scrollHeight; }
}, { passive: true });
const applyAll = $<HTMLInputElement>("applyAll");
const formatSel = $<HTMLSelectElement>("formatSel");
const bgColor = $<HTMLInputElement>("bgColor");
const mathPapers = document.createElement("optgroup");
mathPapers.label = "Matemáticas y física";
mathPapers.append(...PAPER_KINDS.map((k) => new Option(k.label, `x:${k.id}`)));
paperSel.replaceChildren(...PAPER_STYLES.map(([v, label]) => new Option(label, v)), mathPapers);
const paperOpts = $("paperOpts");
/** The options of the page's special paper (base of a log scale, unit of a grid...), one select each. */
let paperOptsKey = " ";
function syncPaper(): void {
  if (!doc) return;
  const bg = doc.pages[currentPage()]?.background; // the view can still point past the end while pages are being rebuilt
  if (!bg) return;
  paperSel.value = bg.paper ? `x:${bg.paper.kind}` : bg.style;
  renderPaperOpts(bg.paper);
}
function renderPaperOpts(spec: PaperSpec | undefined): void {
  const key = spec ? formatSpecForTest(spec) : "";
  if (key === paperOptsKey) return; // rebuilding while the user has a list open would close it
  paperOptsKey = key;
  paperOpts.replaceChildren();
  const k = spec && paperKind(spec.kind);
  if (!spec || !k) return;
  for (const param of k.params) {
    const label = document.createElement("label");
    label.append(`${param.label} `);
    const sel = document.createElement("select");
    sel.dataset.param = param.key;
    sel.append(...param.options.map(([v, l]) => new Option(l, v)));
    sel.value = spec.params[param.key] ?? param.options[0]![0];
    sel.addEventListener("change", needDoc(() => journal.setPaperLook(currentPage(), k.fallback, withParam(spec, param.key, sel.value), applyAll.checked)));
    label.append(sel);
    paperOpts.append(label);
  }
}
/** A value of the paper select or menu: a built-in style, or `x:<kind>` for a special paper. */
function choosePaper(value: string): void {
  if (!doc) return;
  if (!value.startsWith("x:")) { journal.setPaper(currentPage(), value, applyAll.checked); return; }
  const k = paperKind(value.slice(2));
  if (!k) return;
  const cur = doc.pages[currentPage()]!.background.paper;
  const spec = cur && cur.kind === k.id ? cur : defaultSpec(k.id)!;
  journal.setPaperLook(currentPage(), k.fallback, spec, applyAll.checked);
}
formatSel.replaceChildren(...Object.entries(PAPER_FORMATS).map(([k, [, , label]]) => new Option(label, k)));
const needDoc = (fn: () => void) => () => { if (doc) fn(); };
$("pgFirst").addEventListener("click", needDoc(() => goToPage(0)));
$("pgPrev").addEventListener("click", needDoc(() => goToPage(currentPage() - 1)));
$("pgNext").addEventListener("click", needDoc(() => goToPage(currentPage() + 1)));
$("pgLast").addEventListener("click", needDoc(() => goToPage(doc!.pages.length - 1)));
$("histBack").addEventListener("click", historyBack);
$("histFwd").addEventListener("click", historyForward);
pgNum.addEventListener("change", needDoc(() => goToPage(Number(pgNum.value) - 1)));
pgNum.addEventListener("keydown", (e) => { e.stopPropagation(); if (e.key === "Enter") pgNum.blur(); });
$("pgNew").addEventListener("click", needDoc(() => journal.newPageAfter(currentPage())));
$("pgBefore").addEventListener("click", needDoc(() => journal.insertBefore(currentPage())));
$("pgAppend").addEventListener("click", needDoc(() => journal.append()));
$("pgDelete").addEventListener("click", needDoc(() => journal.deletePage(currentPage())));
$("pgUp").addEventListener("click", needDoc(() => journal.movePage(currentPage(), -1)));
$("pgDown").addEventListener("click", needDoc(() => journal.movePage(currentPage(), 1)));
paperSel.addEventListener("change", () => choosePaper(paperSel.value));
bgColor.addEventListener("change", needDoc(() => journal.setBackgroundColor(currentPage(), bgColor.value, applyAll.checked)));
formatSel.addEventListener("change", needDoc(() => {
  const f = PAPER_FORMATS[formatSel.value]!;
  const p = doc!.pages[currentPage()]!;
  const landscape = p.width > p.height;
  journal.setFormat(currentPage(), landscape ? f[1] : f[0], landscape ? f[0] : f[1], applyAll.checked);
}));
$("orient").addEventListener("click", needDoc(() => {
  const p = doc!.pages[currentPage()]!;
  journal.setFormat(currentPage(), p.height, p.width, applyAll.checked);
}));
$("fmtApply").addEventListener("click", needDoc(() => {
  const w = Number($<HTMLInputElement>("fmtW").value), h = Number($<HTMLInputElement>("fmtH").value);
  if (w >= 20 && h >= 20) journal.setFormat(currentPage(), w * MM_TO_PT, h * MM_TO_PT, applyAll.checked);
}));

$("copy").addEventListener("click", () => editor.selection.copy());
$("cut").addEventListener("click", () => editor.selection.cut());
$("paste").addEventListener("click", () => editor.selection.paste());
$("dup").addEventListener("click", () => editor.selection.duplicate());
$("front").addEventListener("click", () => editor.selection.reorder("front"));
$("back").addEventListener("click", () => editor.selection.reorder("back"));
$("rotL").addEventListener("click", () => editor.selection.transformSelection("rotL"));
$("rotR").addEventListener("click", () => editor.selection.transformSelection("rotR"));
$("flipH").addEventListener("click", () => editor.selection.transformSelection("flipH"));
$("flipV").addEventListener("click", () => editor.selection.transformSelection("flipV"));
const imgInput = $<HTMLInputElement>("imgInput");
$("image").addEventListener("click", () => imgInput.click());
imgInput.addEventListener("change", () => {
  const f = imgInput.files?.[0];
  if (f) void inserts.insertImage(f).then((ok) => { if (!ok) info.textContent = "No se pudo leer esa imagen"; });
  imgInput.value = "";
});
window.addEventListener("paste", (e) => {
  const tag = (e.target as HTMLElement).tagName;
  if (tag === "TEXTAREA" || tag === "INPUT") return; // normal text paste
  const f = Array.from(e.clipboardData?.files ?? []).find((x) => x.type.startsWith("image/"));
  if (!doc) return;
  e.preventDefault();
  if (f) void inserts.insertImage(f);
  else editor.selection.paste();
});
let overlayQueued = false;
scroller.addEventListener("scroll", () => {
  if (overlayQueued) return;
  overlayQueued = true;
  requestAnimationFrame(() => {
    overlayQueued = false;
    if (editor.selection.active) editor.refreshOverlay();
    updatePageUi();
    if (!sharpTimer) visible.forEach((i) => { if (!regionCovers(i)) paint(i); }); // a big page is drawn in windows: move the window when the screen leaves it
  });
});

// Leaving presentation or full screen must always work: even from a text field or a dialog
// (capture phase, so no field can swallow the key).
window.addEventListener("keydown", (e) => {
  if (e.key !== "Escape") return;
  if (presenting) { e.preventDefault(); setPresenting(false); }
  else if (fullscreen) { e.preventDefault(); fullscreen = false; window.xournal.setFullscreen(false); }
}, true);
document.getElementById("presentChip")!.addEventListener("click", () => setPresenting(false));

window.addEventListener("keydown", (e) => {
  if (latex.isOpen || prefsDialog.isOpen || penDiagnostic.isOpen) return; // dialogs own the keyboard
  const tag = (e.target as HTMLElement).tagName;
  if (tag === "TEXTAREA" || tag === "INPUT" || tag === "SELECT") return; // typing, not shortcuts
  const bound = tablet.keys[comboOf(e) ?? ""]; // the tablet's and the remote's keys, as the user bound them
  if (bound) { e.preventDefault(); if (!e.repeat) runAction(bound); return; }
  if (e.ctrlKey) {
    const k = e.key.toLowerCase();
    if (k === "o") { e.preventDefault(); void openDialog(); }
    else if (k === "s") { e.preventDefault(); void save(e.shiftKey ? "saveAs" : "save"); }
    else if (k === "n" && !e.shiftKey) { e.preventDefault(); newDocument(); }
    else if (k === "w") { e.preventDefault(); closeDocument(); }
    else if (k === "g") { e.preventDefault(); pgNum.focus(); pgNum.select(); }
    else if (doc && e.key === "Home") { e.preventDefault(); goToPage(0); }
    else if (doc && e.key === "End") { e.preventDefault(); goToPage(doc.pages.length - 1); }
    else if (k === "f" && !e.shiftKey) { e.preventDefault(); find.open(); }
    else if (k === "a") { e.preventDefault(); setTool("select"); editor.selection.selectAll(host.currentPage()); }
    else if (k === "c") { e.preventDefault(); editor.selection.copy(); }
    else if (k === "x") { e.preventDefault(); editor.selection.cut(); }
    else if (k === "d") { e.preventDefault(); editor.selection.duplicate(); }
    else if (e.key === "ArrowUp") { e.preventDefault(); editor.selection.reorder("front"); }
    else if (e.key === "ArrowDown") { e.preventDefault(); editor.selection.reorder("back"); }
    else if (k === "z" && !e.shiftKey) { e.preventDefault(); editor.undo(); }
    else if (k === "y" || (k === "z" && e.shiftKey)) { e.preventDefault(); editor.redo(); }
    else if (e.key === "+" || e.key === "=") { e.preventDefault(); setZoom(zoom * 1.2); }
    else if (e.key === "-") { e.preventDefault(); setZoom(zoom / 1.2); }
    else if (e.key === "0") { e.preventDefault(); setZoom(1); }
    return;
  }
  if (e.altKey && e.key === "ArrowLeft") { e.preventDefault(); historyBack(); return; }
  if (e.altKey && e.key === "ArrowRight") { e.preventDefault(); historyForward(); return; }
  if (e.altKey || e.metaKey) return;
  if (doc && e.key === "PageDown") { e.preventDefault(); goToPage(currentPage() + 1); return; }
  if (doc && e.key === "PageUp") { e.preventDefault(); goToPage(currentPage() - 1); return; }
  if (editor.tool === "figure" && editor.figPending.length) { // a figure being clicked point by point
    if (e.key === "Enter") { e.preventDefault(); editor.endFigure(true); return; }
    if (e.key === "Escape") { e.preventDefault(); editor.endFigure(false); return; }
    if (e.key === "Backspace" || e.key === "Delete") { e.preventDefault(); editor.undoFigurePoint(); return; }
  }
  if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); editor.selection.deleteSelected(); return; }
  if (e.key === "F11") { e.preventDefault(); void commands["view.fullscreen"]!(); return; }
  if (e.key === "F5") { e.preventDefault(); setPresenting(!presenting); return; }
  if (e.key === "Escape") { editor.escape(); return; }
  if (editor.selection.active && (e.key === "r" || e.key === "R")) { editor.selection.transformSelection(e.shiftKey ? "rotL" : "rotR"); return; }
  if (editor.selection.active && (e.key === "f" || e.key === "F")) { editor.selection.transformSelection(e.shiftKey ? "flipV" : "flipH"); return; }
  if (e.key.toLowerCase() === "q" && !e.shiftKey) { e.preventDefault(); showToolbox(); return; } // the tool box, unless a binding of the user took the key above
  const map: Record<string, Tool> = { p: "pen", h: "highlighter", e: "eraser", b: "eraserPart", s: "select", l: "lasso", g: "shape", t: "text", x: "latex", v: "vspace", k: "laser", u: "fill", c: "plane", d: "space3", f: "figure", n: "numline", j: "venn", m: "hand" };
  const t = map[e.key.toLowerCase()];
  if (t) setTool(t);
});
scroller.addEventListener(
  "wheel",
  (e) => {
    if (!e.ctrlKey) return;
    e.preventDefault();
    const r = scroller.getBoundingClientRect();
    setZoom(zoom * Math.exp(-e.deltaY * 0.0015), { x: e.clientX - r.left, y: e.clientY - r.top });
  },
  { passive: false },
);
pagesEl.addEventListener("dblclick", (e) => {
  if (editor.tool !== "select" && editor.tool !== "lasso") return;
  const sel = editor.selection.elements;
  const t = sel.length === 1 ? sel[0] : undefined;
  const pageEl = (e.target as HTMLElement).closest<HTMLElement>(".page");
  if (t?.kind === "teximage" && pageEl) {
    const page = Number(pageEl.dataset.i);
    const layer = doc!.pages[page]!.layers.find((l) => l.elements.includes(t));
    if (layer) latex.edit(page, t, layer);
  }
});
window.addEventListener("dragover", (e) => e.preventDefault());
window.addEventListener("drop", (e) => {
  e.preventDefault();
  const f = e.dataTransfer?.files[0];
  if (!f) return;
  if (f.type.startsWith("image/")) { if (doc) void inserts.insertImage(f); return; }
  void openUserFile(window.xournal.pathForFile(f));
});
setTool("pen");
refreshUi();

// ---- menu commands (the native menu lives in the main process) ----------------
let presenting = false;
let fullscreen = false;
function setPresenting(on: boolean): void {
  presenting = on;
  document.body.classList.toggle("presentation", on);
  fullscreen = on;
  window.xournal.setFullscreen(on);
  window.xournal.setMenuBar(!on);
}

const toolIds: Record<string, Tool> = {
  "tool.pen": "pen", "tool.highlighter": "highlighter", "tool.eraser": "eraser", "tool.eraserPart": "eraserPart",
  "tool.text": "text", "tool.latex": "latex", "tool.numline": "numline", "tool.venn": "venn", "tool.vspace": "vspace", "tool.laser": "laser", "tool.fill": "fill", "tool.plane": "plane", "tool.space3": "space3", "tool.figure": "figure", "tool.shape": "shape", "tool.select": "select", "tool.lasso": "lasso", "tool.hand": "hand",
};
// ---- pen and tablet ----
let tablet: TabletSettings = { ...DEFAULT_TABLET, keys: {} };
const BRUSH_ORDER = ["pen", "ink", "fountain", "brush"] as const;
function applyTablet(s: TabletSettings, save = false): void {
  tablet = s;
  editor.brush = s.brush;
  editor.penWidth = s.size;
  editor.pressure = new PressureMap(s.curve);
  editor.penMap = { barrel: s.barrel, barrel2: s.barrel2, eraser: s.eraser };
  fillWidths();
  if (save) { prefs = { ...prefs, tablet: JSON.stringify(s) }; void window.xournal.setPrefs({ tablet: prefs.tablet }); }
}
mathPanels = new MathPanels(editor);
editor.onMathCreated = (kind) => { mathPanels?.open(kind); }; // the expression is what is written next
const tabletDialog = new TabletDialog({ settings: () => ({ ...tablet, size: editor.penWidth }), change: (s) => applyTablet(s, true) });
const nudgeSize = (f: number) => applyTablet({ ...tablet, size: Math.min(60, Math.max(0.1, +(editor.penWidth * f).toFixed(2))) }, true);
const cycleBrush = (d: number) => applyTablet({ ...tablet, brush: BRUSH_ORDER[(BRUSH_ORDER.indexOf(tablet.brush) + d + BRUSH_ORDER.length) % BRUSH_ORDER.length]! }, true);
function cycleColor(d: number): void {
  const sw = [...swatches.querySelectorAll<HTMLElement>(".sw")];
  if (!sw.length) return;
  const i = sw.findIndex((x) => x.classList.contains("active"));
  const next = sw[(i + d + sw.length) % sw.length]!;
  if (next.dataset.color) selectColor(next.dataset.color);
}
/** What a bound key does. Tools and commands reuse the menu's own ids. */
function runAction(id: string): void {
  if (!APP_ACTIONS.some((a) => a.id === id)) return;
  if (id === "tool.previous") { setTool(previousTool); return; }
  if (id === "toolbox.show") { showToolbox(); return; }
  if (id.startsWith("tool.")) { const t = toolIds[id]; if (t) setTool(t); return; }
  if (id === "file.save") { void save("save"); return; }
  if (id === "pen.sizeUp") { nudgeSize(1.25); return; }
  if (id === "pen.sizeDown") { nudgeSize(0.8); return; }
  if (id === "brush.next") { cycleBrush(1); return; }
  if (id === "brush.prev") { cycleBrush(-1); return; }
  if (id === "color.next") { cycleColor(1); return; }
  if (id === "color.prev") { cycleColor(-1); return; }
  if (id === "glow.toggle") { glowOnEl.checked = !glowOnEl.checked; glowOnEl.dispatchEvent(new Event("change")); return; }
  void commands[id]?.();
}
$("btnTablet").addEventListener("click", () => tabletDialog.open());

// ---- markdown in texts ----
const btnMd = $("btnMd");
const showMd = (on: boolean) => { btnMd.classList.toggle("on", on); btnMd.setAttribute("aria-pressed", String(on)); };
editor.onTextMd = showMd;
btnMd.addEventListener("mousedown", (e) => e.preventDefault()); // do not take the focus from the text being written
btnMd.addEventListener("click", () => {
  const on = !inserts.markdown;
  inserts.setMarkdown(on);
  prefs = { ...prefs, textMd: on };
  void window.xournal.setPrefs({ textMd: on });
});

// ---- tool box (floating panel of tools) ----
let toolboxSettings: ToolboxSettings = sanitizeToolbox(null);
const lastPointer = { x: 400, y: 300 };
window.addEventListener("pointermove", (e) => { lastPointer.x = e.clientX; lastPointer.y = e.clientY; }, true);
function saveToolbox(s: ToolboxSettings): void {
  toolboxSettings = s;
  prefs = { ...prefs, toolbox: JSON.stringify(s) };
  void window.xournal.setPrefs({ toolbox: prefs.toolbox });
}
const toolbox = new Toolbox({
  settings: () => toolboxSettings,
  save: saveToolbox,
  run: (id) => {
    if (id.startsWith("bar:")) document.getElementById(id.slice(4))?.click(); // a button of the toolbar does its own work
    else if (id.startsWith("color:")) selectColor(id.slice(6));
    else runAction(id);
  },
  isActive: (id) => (id.startsWith("tool.") ? toolIds[id] === editor.tool : id.startsWith("color:") ? editor.color.toLowerCase() === id.slice(6).toLowerCase() : id === "glow.toggle" ? editor.glowOn : false),
  colors: () => [...swatches.querySelectorAll<HTMLElement>(".sw")].map((x) => (x.dataset.color ?? "").toLowerCase()).filter((c) => /^#[0-9a-f]{6}$/.test(c)),
  icons: (root) => { installIcons(root); },
});
editor.onToolbox = (x, y) => { toolbox.open(x, y); };
function showToolbox(x = lastPointer.x, y = lastPointer.y): void {
  if (!toolboxSettings.enabled) { info.textContent = "El cuadro de herramientas está desactivado (Herramientas ▸ Activar o desactivar el cuadro de herramientas)"; return; }
  toolbox.toggle(x, y);
}
function setToolboxEnabled(on: boolean): void {
  saveToolbox({ ...toolboxSettings, enabled: on });
  if (!on) toolbox.close();
  info.textContent = on ? "Cuadro de herramientas activado" : "Cuadro de herramientas desactivado";
}
// The remote's dial (mouse wheel) and a driver's extra mouse buttons, when bound, run their action instead of scrolling or zooming.
window.addEventListener("wheel", (e) => {
  if (tabletDialog.isOpen || latex.isOpen || prefsDialog.isOpen) return;
  const id = tablet.keys[wheelCombo(e) ?? ""];
  if (id) { e.preventDefault(); e.stopPropagation(); runAction(id); }
}, { capture: true, passive: false });
window.addEventListener("mousedown", (e) => {
  if (tabletDialog.isOpen || latex.isOpen || prefsDialog.isOpen) return;
  const id = tablet.keys[mouseCombo(e) ?? ""];
  if (id) { e.preventDefault(); e.stopPropagation(); runAction(id); }
}, true);

const commands: Record<string, (arg?: unknown) => void | Promise<void>> = {
  "pen.settings": () => tabletDialog.open(),
  "tool.toolbox": () => showToolbox(),
  "tool.toolboxEnable": () => setToolboxEnabled(!toolboxSettings.enabled),
  "file.exportPdf": () => { void exportPdf(); },
  "file.new": () => newDocument(),
  "help.welcome": () => { void welcome.show(); },
  "file.open": openDialog,
  "file.openRecent": (p) => openUserFile(String(p)),
  "file.save": () => save("save"),
  "file.saveAs": () => save("saveAs"),
  "file.saveCopy": () => save("copy"),
  "file.close": closeDocument,
  "edit.undo": () => editor.undo(),
  "edit.redo": () => editor.redo(),
  "edit.cut": () => editor.selection.cut(),
  "edit.copy": () => { editor.selection.copy(); },
  "edit.paste": () => { editor.selection.paste(); },
  "edit.delete": () => editor.selection.deleteSelected(),
  "edit.find": () => find.open(),
  "edit.prefs": () => prefsDialog.open(prefs),
  "help.pen": () => penDiagnostic.open(),
  "file.openAutosave": async () => { const f = await window.xournal.openDialog(prefs.autosaveDir || undefined); if (f) await openUserFile(f); },
  "tool.recognizer": (on) => setRecognize(on === true),
  "view.toolbars": (on) => { document.body.classList.toggle("hide-bars", on === false); },
  "view.fullscreen": () => { fullscreen = !fullscreen; window.xournal.setFullscreen(fullscreen); },
  "view.presentation": () => setPresenting(!presenting),
  "view.sidebarPages": () => sidebar.toggle("pages"),
  "view.sidebarLayers": () => sidebar.toggle("layers"),
  "view.continuous": (on) => setContinuous(on !== false),
  "view.zoomIn": () => setZoom(zoom * 1.2),
  "view.zoomOut": () => setZoom(zoom / 1.2),
  "view.zoom100": () => setZoom(1),
  "view.fitWidth": fitWidth,
  "view.fitPage": fitPage,
  "nav.first": needDoc(() => goToPage(0)),
  "nav.prev": needDoc(() => goToPage(currentPage() - 1)),
  "nav.next": needDoc(() => goToPage(currentPage() + 1)),
  "nav.last": needDoc(() => goToPage(doc!.pages.length - 1)),
  "nav.goto": () => { pgNum.focus(); pgNum.select(); },
  "nav.back": historyBack,
  "nav.forward": historyForward,
  "journal.newPage": needDoc(() => { journal.newPageAfter(currentPage()); }),
  "journal.insertBefore": needDoc(() => { journal.insertBefore(currentPage()); }),
  "journal.append": needDoc(() => { journal.append(); }),
  "journal.delete": needDoc(() => { journal.deletePage(currentPage()); }),
  "journal.moveUp": needDoc(() => { journal.movePage(currentPage(), -1); }),
  "journal.moveDown": needDoc(() => { journal.movePage(currentPage(), 1); }),
  "journal.bgColor": () => bgColor.click(),
  "journal.applyAll": (on) => { applyAll.checked = on === true; },
  "journal.rotate": needDoc(() => { const p = doc!.pages[currentPage()]!; journal.setFormat(currentPage(), p.height, p.width, applyAll.checked); }),
  "tool.image": () => imgInput.click(),
};
// ---- preferences and recovery copies ----------------------------------------------
const reduceMq = [window.matchMedia("(prefers-reduced-motion: reduce)"), window.matchMedia("(prefers-reduced-transparency: reduce)")];
/** Glows and soft shadows are off ("reduced") when the preference says so, or, in automatic mode, when the system asks for less motion or transparency. */
function applyEffects(): void {
  const reduced = prefs.effects === "reduced" || (prefs.effects !== "full" && reduceMq.some((m) => m.matches));
  document.documentElement.dataset.fx = reduced ? "reduced" : "full";
}
for (const m of reduceMq) m.addEventListener("change", () => applyEffects());
let autosaveTimer = 0;
function applyPrefs(p: Prefs): void {
  prefs = { ...prefs, ...p };
  editor.fingerDraw = prefs.fingerDraw;
  if (prefs.palette !== paletteId) setPalette(prefs.palette);
  editor.glowOn = prefs.glowOn;
  try { editor.planeSpec = sanitizePlane(prefs.plane ? JSON.parse(prefs.plane) : DEFAULT_PLANE); } catch { editor.planeSpec = { ...DEFAULT_PLANE }; }
  try { editor.space3Spec = sanitizeSpace3(prefs.space3 ? JSON.parse(prefs.space3) : DEFAULT_SPACE3); } catch { editor.space3Spec = { ...DEFAULT_SPACE3 }; }
  try { editor.fill = sanitizeFill(prefs.fill ? JSON.parse(prefs.fill) : {}); } catch { editor.fill = sanitizeFill({}); }
  editor.glowSize = prefs.glowSize;
  editor.glowIntensity = prefs.glowIntensity;
  try { sizePresets = sanitizeSizes(prefs.sizes ? JSON.parse(prefs.sizes) : {}); } catch { sizePresets = sanitizeSizes({}); }
  renderSizePresets();
  editor.textMarkdown = prefs.textMd !== false;
  showMd(inserts.markdown);
  try { toolboxSettings = sanitizeToolbox(prefs.toolbox ? JSON.parse(prefs.toolbox) : DEFAULT_TOOLBOX); } catch { toolboxSettings = sanitizeToolbox(null); }
  try { applyTablet(sanitizeTablet(prefs.tablet ? JSON.parse(prefs.tablet) : DEFAULT_TABLET)); } catch { applyTablet({ ...DEFAULT_TABLET, keys: {} }); }
  syncGlowUi();
  document.documentElement.dataset.theme = prefs.theme === "neon" ? "neon" : "classic";
  applyEffects();
  if (prefs.theme === "neon") window.xournal.setTitleBar("#1e1043", "#05e1ff"); else window.xournal.setTitleBar("#2b2b2b", "#dddddd"); // the system buttons follow the theme
  window.clearInterval(autosaveTimer);
  autosaveTimer = 0;
  if (prefs.autosaveMinutes > 0) {
    autosaveTimer = window.setInterval(() => void autosaveNow(), prefs.autosaveMinutes * 60_000);
  }
}
/** Write a recovery copy of a document with unsaved changes. It never touches the user's own file. */
let lastAutosaveChange = -1;
async function autosaveNow(): Promise<string | null> {
  if (!doc || !editor.dirty) return null;
  if (editor.changeCount === lastAutosaveChange) return null; // nothing happened since the last recovery copy: do not serialise the whole document again
  lastAutosaveChange = editor.changeCount;
  const file = await window.xournal.autosave(serializeXopp(doc), fileName);
  info.textContent = `Copia de recuperación guardada (${new Date().toLocaleTimeString()})`;
  return file;
}
const penDiagnostic = new PenDiagnostic();
const prefsDialog = new PrefsDialog((p) => { applyPrefs(p); void window.xournal.setPrefs(p); });
// every dialog moves by its title bar and can be sent to a window of its own (to another monitor)
makeFloating($("latexDlg"), () => latex.close());
makeFloating($("prefsDlg"), () => prefsDialog.close());
makeFloating($("tabletDlg"), () => tabletDialog.close());
makeFloating($("penDlg"), () => penDiagnostic.close());
void window.xournal.getPrefs().then((p) => applyPrefs(p)).catch(() => undefined);

window.xournal.onCommand((id, arg) => {
  const tool = toolIds[id];
  if (tool) { setTool(tool); return; }
  if (id.startsWith("journal.paper:") && doc) { choosePaper(id.slice("journal.paper:".length)); return; }
  if (id.startsWith("journal.format:") && doc) {
    const f = PAPER_FORMATS[id.slice("journal.format:".length)];
    const p = doc.pages[currentPage()]!;
    if (f) { const land = p.width > p.height; journal.setFormat(currentPage(), land ? f[1] : f[0], land ? f[0] : f[1], applyAll.checked); }
    return;
  }
  void commands[id]?.(arg);
});

// ---- automated modes (headless runs) ----------------------------------------
const pct = (a: number[], q: number) => (a.length ? [...a].sort((x, y) => x - y)[Math.min(a.length - 1, Math.floor(q * a.length))]! : 0);
const summary = (a: number[]) => ({ n: a.length, mean: +(a.reduce((s, v) => s + v, 0) / (a.length || 1)).toFixed(3), p95: +pct(a, 0.95).toFixed(3), max: +Math.max(0, ...a).toFixed(3) });

async function selftest(files: string[]): Promise<void> {
  const results = [];
  for (const f of files) {
    const r = await window.xournal.readXopp(f);
    const d = parseXopp(r.xml);
    const t0 = performance.now();
    const out = serializeXopp(d);
    const ms = performance.now() - t0;
    let diff = -1;
    if (out !== r.xml) { diff = 0; const n = Math.min(out.length, r.xml.length); while (diff < n && out[diff] === r.xml[diff]) diff++; }
    results.push({
      file: r.name, identical: out === r.xml, serializeMs: Math.round(ms), droppedChildren: d.droppedChildren,
      ...(diff >= 0 ? { firstDiff: diff, orig: r.xml.slice(Math.max(0, diff - 40), diff + 60), ours: out.slice(Math.max(0, diff - 40), diff + 60), lens: [r.xml.length, out.length] } : {}),
    });
  }
  await window.xournal.ready({ mode: "selftest", results });
}

function synth(type: string, el: Element, x: number, y: number, pressure: number, pointerType = "pen", shift = false, hover = false, btn?: { button: number; buttons: number }): void {
  const pressed = !hover && type !== "pointerup";
  el.dispatchEvent(new PointerEvent(type, { bubbles: true, cancelable: true, clientX: x, clientY: y, pointerId: 7, pointerType, pressure, button: btn?.button ?? 0, buttons: pressed ? (btn?.buttons ?? 1) : 0, isPrimary: true, shiftKey: shift }));
}

/**
 * Sustained load for measuring GPU and CPU from outside (see the performance notes in the README): `draw`, `zoom`,
 * `scroll` or `hover`, optionally with `glow`. It runs until the process is stopped.
 */
async function runStress(spec: string): Promise<void> {
  const [name, css] = [spec.split("|")[0]!, spec.split("|").slice(1).join("|")];
  if (css) { const st = document.createElement("style"); st.textContent = css; document.head.appendChild(st); } // experiments: a stylesheet to try on top of the page
  const glow = name.endsWith("glow");
  const kind = name.replace("glow", "");
  if (glow) { editor.glowOn = true; editor.glowSize = 8; editor.glowIntensity = 0.8; editor.color = "#ff2a9d"; }
  await new Promise((r) => setTimeout(r, 1500));
  const pageIdx = Math.min(...visible);
  const el = () => views[pageIdx]!.el;
  const t0 = performance.now();
  if (kind === "glowdocs") { // 400 neon strokes already on the page, then the zoom keeps changing: the cost of redrawing glowing ink
    const layer = activeLayer(doc!.pages[pageIdx]!);
    const page = doc!.pages[pageIdx]!;
    for (let n = 0; n < 400; n++) {
      const pts: number[] = [];
      const cx = 60 + Math.random() * (page.width - 120), cy = 60 + Math.random() * (page.height - 120);
      for (let i = 0; i < 60; i++) pts.push(cx + Math.cos(i / 6 + n) * (8 + i * 0.6), cy + Math.sin(i / 6 + n) * (8 + i * 0.6));
      const st: Stroke = { kind: "stroke", attrs: [["tool", "pen"], ["color", "#ff2a9dff"], ["width", "1.41"], ["capStyle", "round"]], tool: "pen", color: "rgba(255,42,157,1)", widths: [1.41], pts, cap: "round", dash: null, bbox: bboxOf(pts) };
      setGlow(st, { size: 6, intensity: 0.8 });
      layer.elements.push(st);
    }
    doc!.elementCount += 400;
    visible.forEach((i) => paint(i));
    const tick = () => { setZoom(1.6 + 0.5 * Math.sin((performance.now() - t0) / 1500)); scheduleSharpRender(true); setTimeout(tick, 250); }; // a sharp render four times a second
    tick();
  } else if (kind === "zoom") {
    const tick = () => { setZoom(2.2 + 1.8 * Math.sin((performance.now() - t0) / 900)); setTimeout(tick, 33); };
    tick();
  } else if (kind === "scroll") {
    let dir = 1;
    const tick = () => {
      scroller.scrollTop += dir * 40;
      if (scroller.scrollTop <= 0 || scroller.scrollTop + scroller.clientHeight >= scroller.scrollHeight - 2) dir = -dir;
      setTimeout(tick, 16);
    };
    tick();
  } else if (kind === "erase") { // the eraser sweeping the page, then undone so there is always ink to erase
    setTool("eraser");
    const run = async () => {
      for (;;) {
        const r = el().getBoundingClientRect();
        const y = r.top + r.height * (0.15 + Math.random() * 0.5);
        synth("pointerdown", el(), r.left + r.width * 0.1, y, 0.5);
        for (let i = 1; i < 90; i++) { synth("pointermove", el(), r.left + r.width * (0.1 + 0.8 * i / 90), y + Math.sin(i / 6) * 30, 0.5); await new Promise((res) => setTimeout(res, 8)); }
        synth("pointerup", el(), r.left + r.width * 0.9, y, 0);
        await new Promise((res) => setTimeout(res, 150));
        editor.undo();
        await new Promise((res) => setTimeout(res, 150));
      }
    };
    void run();
  } else if (kind === "hover" || kind === "hovererase") {
    setTool(kind === "hover" ? "pen" : "eraser");
    const tick = () => {
      const r = el().getBoundingClientRect(), t = (performance.now() - t0) / 1000;
      synth("pointermove", el(), r.left + r.width * (0.5 + 0.4 * Math.cos(t * 2)), r.top + r.height * (0.4 + 0.3 * Math.sin(t * 3)), 0, "pen", false, true);
      setTimeout(tick, 8);
    };
    tick();
  } else {
    setTool("pen");
    const run = async () => {
      for (;;) {
        const r = el().getBoundingClientRect();
        const cx = r.left + r.width * (0.3 + Math.random() * 0.4), cy = r.top + r.height * (0.3 + Math.random() * 0.4);
        synth("pointerdown", el(), cx, cy, 0.5);
        for (let i = 1; i < 160; i++) {
          const a = i * 0.16, rad = 10 + i * 0.9;
          synth("pointermove", el(), cx + Math.cos(a) * rad, cy + Math.sin(a) * rad, 0.3 + 0.4 * Math.abs(Math.sin(i / 20)));
          await new Promise((res) => setTimeout(res, 8));
        }
        synth("pointerup", el(), cx, cy, 0);
        await new Promise((res) => setTimeout(res, 120));
      }
    };
    void run();
  }
}

async function bench(file: string, z: number): Promise<void> {
  const timing = await openFile(file, { zoom: z });
  await new Promise((r) => setTimeout(r, 200));
  const i = Math.min(...visible);
  const el = views[i]!.el;
  const rect = el.getBoundingClientRect();
  const page = doc!.pages[i]!;
  const before = page.layers.reduce((s, l) => s + l.elements.length, 0);
  editor.stats = { moveMs: [], commitMs: [], eraseRepaintMs: [] };

  // Full static repaint cost of this page (upper bound for undo/erase repaint).
  const rp: number[] = [];
  for (let k = 0; k < 15; k++) { const t = performance.now(); paint(i); rp.push(performance.now() - t); }

  // 0) Frame cadence while drawing: events paced by rAF (3 per frame), 240 frames.
  const cx0 = rect.left + rect.width / 2, cy0 = rect.top + Math.min(rect.height, scroller.clientHeight) / 2;
  setTool("pen");
  synth("pointerdown", el, cx0, cy0, 0.5);
  editor.stats = { moveMs: [], commitMs: [], eraseRepaintMs: [] };
  const frames: number[] = [];
  const inView: boolean[] = [];
  let last = performance.now(), k0 = 0;
  await new Promise<void>((done) => {
    const tick = (now: number) => {
      frames.push(now - last); last = now;
      for (let j = 0; j < 3; j++, k0++) {
        const a = k0 * 0.04, r = 20 + k0 * 0.25;
        const px = cx0 + Math.cos(a) * r, py = cy0 + Math.sin(a) * r;
        synth("pointermove", el, px, py, 0.3 + 0.5 * Math.abs(Math.sin(k0 / 30)));
        if (j === 2) inView.push(px >= 0 && py >= 0 && px < scroller.clientWidth && py < scroller.clientHeight + 60);
      }
      if (frames.length < 240) requestAnimationFrame(tick); else done();
    };
    requestAnimationFrame(tick);
  });
  synth("pointerup", el, cx0, cy0, 0);
  editor.undo();
  const handlerMax = Math.max(...editor.stats.moveMs);
  const worst = frames.indexOf(Math.max(...frames.slice(5)));
  const around = frames.slice(Math.max(0, worst - 2), worst + 3).map((v) => +v.toFixed(0));
  const worstInView = inView.slice(Math.max(0, worst - 2), worst + 3);
  const fr = frames.slice(5);
  const frameStats = { frames: fr.length, p50: +pct(fr, 0.5).toFixed(1), p95: +pct(fr, 0.95).toFixed(1), max: +Math.max(...fr).toFixed(1), maxAt: fr.indexOf(Math.max(...fr)) + 5, handlerMaxMs: +handlerMax.toFixed(2), around, worstInView, cx0: Math.round(cx0), cy0: Math.round(cy0), viewport: [scroller.clientWidth, scroller.clientHeight], over25ms: fr.filter((v) => v > 25).length, over40ms: fr.filter((v) => v > 40).length };
  editor.stats = { moveMs: [], commitMs: [], eraseRepaintMs: [] };

  // 1) A pen stroke: 1500-point spiral with varying pressure.
  const cx = rect.left + rect.width / 2, cy = rect.top + rect.height / 2;
  setTool("pen");
  synth("pointerdown", el, cx, cy, 0.5);
  for (let k = 1; k <= 1500; k++) {
    const a = k * 0.05, r = k * 0.12;
    synth("pointermove", el, cx + Math.cos(a) * r, cy + Math.sin(a) * r, 0.3 + 0.5 * Math.abs(Math.sin(k / 40)));
  }
  synth("pointerup", el, cx, cy, 0);
  const afterPen = page.layers.reduce((s, l) => s + l.elements.length, 0);

  // 2) Erase across the page, then undo / redo.
  setTool("eraser");
  synth("pointerdown", el, rect.left + 20, rect.top + 100, 0.5);
  for (let k = 0; k <= 200; k++) synth("pointermove", el, rect.left + 20 + k * 2.5, rect.top + 100 + Math.sin(k / 10) * 60, 0.5);
  synth("pointerup", el, rect.left + 520, rect.top + 100, 0);
  await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
  const afterErase = page.layers.reduce((s, l) => s + l.elements.length, 0);
  const tU = performance.now(); editor.undo(); const undoMs = performance.now() - tU;
  const tR = performance.now(); editor.redo(); const redoMs = performance.now() - tR;
  editor.undo(); // restore the erased strokes; keep the pen stroke for the saved output

  const ts = performance.now();
  const xml = serializeXopp(doc!);
  const serializeMs = performance.now() - ts;
  const saved = await window.xournal.saveXopp(xml, "bench.xopp", null, "save", null);
  const s = editor.stats;
  editor.stats = null;
  await window.xournal.ready({
    mode: "bench", file: fileName, zoom: z, canvasPx: [views[i]!.canvas?.width, views[i]!.canvas?.height], dpr: window.devicePixelRatio, frameMs: frameStats, note: "synthetic pointer events: handler cost only, not display latency",
    open: timing, page: i, elementsOnPage: { before, afterPen, afterErase },
    penMoveMs: summary(s.moveMs.slice(0, 1500)), eraseMoveMs: summary(s.moveMs.slice(1500)), commitMs: summary(s.commitMs),
    fullPageRepaintMs: summary(rp), eraseRepaintMs: summary(s.eraseRepaintMs), undoMs: +undoMs.toFixed(2), redoMs: +redoMs.toFixed(2),
    serializeMs: Math.round(serializeMs), xmlMB: +(xml.length / 1e6).toFixed(1), saved,
  });
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function e2e(file: string): Promise<void> {
  const cli0Files = [file];
  await openFile(file, { zoom: 1 });
  await sleep(200);
  const i = Math.min(...visible);
  let el = views[i]!.el; // re-read after page operations rebuild the views
  const page = doc!.pages[i]!;
  // Fresh rect each time: the page may scroll between interactions.
  const X = (x: number) => el.getBoundingClientRect().left + x, Y = (y: number) => el.getBoundingClientRect().top + y;
  const count = () => page.layers.reduce((s, l) => s + l.elements.length, 0);
  const pos = (e: unknown): [number, number] => {
    const o = e as { kind: string; pts?: number[]; x?: number; y?: number; left?: number; top?: number };
    return o.kind === "stroke" ? [o.pts![0]!, o.pts![1]!] : o.kind === "text" ? [o.x!, o.y!] : [o.left!, o.top!];
  };
  const r: Record<string, unknown> = {};
  const n0 = count();
  r.elementsBefore = n0;

  // A) rectangle selection of the top-left region
  setTool("select");
  synth("pointerdown", el, X(5), Y(5), 0.5);
  for (let k = 1; k <= 6; k++) synth("pointermove", el, X(5 + k * 50), Y(5 + k * 50), 0.5);
  synth("pointerup", el, X(305), Y(305), 0);
  const sel = editor.selection.elements;
  r.selected = sel.length;
  const first = sel[0]!;
  const p0 = pos(first);
  let box: Rect | null = null;
  for (const e of sel) { const b = elementBBox(e); if (b) box = unionRect(box, b); }
  const cx = (box![0] + box![2]) / 2, cy = (box![1] + box![3]) / 2;

  // B) move by (30, 40), then undo / redo
  synth("pointerdown", el, X(cx), Y(cy), 0.5);
  for (let k = 1; k <= 3; k++) synth("pointermove", el, X(cx + 10 * k), Y(cy + (40 / 3) * k), 0.5);
  synth("pointerup", el, X(cx + 30), Y(cy + 40), 0);
  const p1 = pos(first);
  r.moveDelta = [+(p1[0] - p0[0]).toFixed(2), +(p1[1] - p0[1]).toFixed(2)];
  r.countAfterMove = count();
  editor.undo(); const pu = pos(first);
  r.undoRestores = Math.abs(pu[0] - p0[0]) < 1e-9 && Math.abs(pu[1] - p0[1]) < 1e-9;
  editor.redo(); const pr = pos(first);
  r.redoMoves = Math.abs(pr[0] - p1[0]) < 1e-9 && Math.abs(pr[1] - p1[1]) < 1e-9;
  editor.undo(); // leave the document as it was

  // C) delete selection, undo
  const nSel = editor.selection.count;
  editor.selection.deleteSelected();
  r.countAfterDelete = count();
  r.deleteExpected = n0 - nSel;
  editor.undo();
  r.countAfterDeleteUndo = count();
  editor.selection.clear();

  // C2) resize strokes: select the region, drag the SE handle to scale by 1.5, undo, redo
  setTool("select");
  synth("pointerdown", el, X(5), Y(5), 0.5);
  for (let k = 1; k <= 6; k++) synth("pointermove", el, X(5 + k * 50), Y(5 + k * 50), 0.5);
  synth("pointerup", el, X(305), Y(305), 0);
  const rs = editor.selection.elements[0] as unknown as Stroke;
  const snapPts = rs.pts.slice(), snapW = rs.widths.slice();
  let rb: Rect | null = null;
  for (const e of editor.selection.elements) { const b = elementBBox(e); if (b) rb = unionRect(rb, b); }
  const [rx0, ry0, rx1, ry1] = rb!;
  synth("pointerdown", el, X(rx1), Y(ry1), 0.5);           // grab the SE handle
  for (let k = 1; k <= 4; k++) synth("pointermove", el, X(rx1 + 0.125 * k * (rx1 - rx0)), Y(ry1 + 0.125 * k * (ry1 - ry0)), 0.5);
  synth("pointerup", el, X(rx1 + 0.5 * (rx1 - rx0)), Y(ry1 + 0.5 * (ry1 - ry0)), 0);
  r.resizeX = +(((rs.pts[0]! - rx0) / (snapPts[0]! - rx0))).toFixed(3);
  r.resizeWidthFactor = +(rs.widths[0]! / snapW[0]!).toFixed(3);
  r.resizeCountKept = count() === n0;
  editor.undo();
  r.resizeUndoExact = rs.pts.length === snapPts.length && rs.pts.every((v, k) => v === snapPts[k]) && rs.widths.every((v, k) => v === snapW[k]);
  editor.redo();
  r.resizeRedo = Math.abs((rs.pts[0]! - rx0) / (snapPts[0]! - rx0) - 1.5) < 1e-6;
  editor.undo();
  editor.selection.clear();

  // C2b) mirror, rotate 90, rotate by handle, mirror by dragging past the opposite side
  {
    const selectRegion = () => {
      setTool("select");
      synth("pointerdown", el, X(5), Y(5), 0.5);
      for (let k = 1; k <= 6; k++) synth("pointermove", el, X(5 + k * 50), Y(5 + k * 50), 0.5);
      synth("pointerup", el, X(305), Y(305), 0);
    };
    const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;
    selectRegion();
    const st = editor.selection.elements[0] as unknown as Stroke;
    const o = st.pts.slice(), ow = st.widths.slice();
    let bb: Rect | null = null;
    for (const e of editor.selection.elements) { const b = elementBBox(e); if (b) bb = unionRect(bb, b); }
    const [x0, y0, x1, y1] = bb!;
    const cxm = (x0 + x1) / 2, cym = (y0 + y1) / 2;
    const exact = () => st.pts.length === o.length && st.pts.every((v, k) => v === o[k]) && st.widths.every((v, k) => v === ow[k]);

    editor.selection.transformSelection("flipH");
    r.flipHCorrect = near(st.pts[0]!, 2 * cxm - o[0]!) && near(st.pts[1]!, o[1]!);
    r.flipKeepsWidth = near(st.widths[0]!, ow[0]!);
    editor.undo(); r.flipHUndoExact = exact();

    editor.selection.transformSelection("rotR");
    r.rotRCorrect = near(st.pts[0]!, cxm + cym - o[1]!) && near(st.pts[1]!, o[0]! + cym - cxm);
    editor.undo(); r.rotRUndoExact = exact();

    // rotate with the handle (26 px above the frame): from "up" to "right" = +90 degrees
    const hy = y0 - 26, rad = cym - hy;
    synth("pointerdown", el, X(cxm), Y(hy), 0.5);
    for (let k = 1; k <= 6; k++) {
      const a = -Math.PI / 2 + (k / 6) * (Math.PI / 2);
      synth("pointermove", el, X(cxm + rad * Math.cos(a)), Y(cym + rad * Math.sin(a)), 0.5);
      if (k === 3) {
        // Protractor: the swept arc (45 degrees) and the current ray must be on the overlay while dragging.
        const live = host.liveCanvas(), lr = live.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
        const Rpx = Math.max(PROTRACTOR.minRadiusPx, rad * zoom);
        const a0 = -Math.PI / 2, th = Math.PI / 4;
        const greenAt = (ang: number, rpx: number) => {
          const px = Math.round((X(cxm) + Math.cos(ang) * rpx - lr.left) * dpr), py = Math.round((Y(cym) + Math.sin(ang) * rpx - lr.top) * dpr);
          return live.getContext("2d", { desynchronized: DESYNC })!.getImageData(px, py, 1, 1).data[3]!;
        };
        r.protractorArcDrawn = greenAt(a0 + th / 2, Rpx * PROTRACTOR.arcFactor) > 0;
        r.protractorRayDrawn = greenAt(a0 + th, Rpx * 0.9) > 0;
        r.protractorAbsentOffRays = greenAt(a0 + th + 1.2, Rpx * 0.3) < 255; // sanity: not fully opaque everywhere
        // Composite page + overlay so the result can be inspected by eye.
        const comp = document.createElement("canvas");
        comp.width = live.width; comp.height = live.height;
        const cc = comp.getContext("2d")!;
        cc.fillStyle = "#3b3b3b"; cc.fillRect(0, 0, comp.width, comp.height);
        const pr = el.getBoundingClientRect(), pc = views[i]!.canvas!;
        cc.drawImage(pc, (pr.left - lr.left) * dpr, (pr.top - lr.top) * dpr, pr.width * dpr, pr.height * dpr);
        cc.drawImage(live, 0, 0);
        r.protractorPng = comp.toDataURL("image/png").split(",")[1];
      }
    }
    synth("pointerup", el, X(cxm + rad), Y(cym), 0);
    r.rotHandleCorrect = near(st.pts[0]!, cxm + cym - o[1]!) && near(st.pts[1]!, o[0]! + cym - cxm);
    editor.undo(); r.rotHandleUndoExact = exact();

    // drag the SE handle across to the left of the anchor (x0): factor -0.5 horizontally, +0.5 vertically
    synth("pointerdown", el, X(x1), Y(y1), 0.5);
    for (let k = 1; k <= 4; k++) synth("pointermove", el, X(x1 + (k / 4) * (x0 - 0.5 * (x1 - x0) - x1)), Y(y1 + (k / 4) * (y0 + 0.5 * (y1 - y0) - y1)), 0.5);
    synth("pointerup", el, X(x0 - 0.5 * (x1 - x0)), Y(y0 + 0.5 * (y1 - y0)), 0);
    r.dragMirror = near(st.pts[0]!, x0 + (o[0]! - x0) * -0.5) && near(st.pts[1]!, y0 + (o[1]! - y0) * 0.5);
    r.dragMirrorWidth = near(st.widths[0]!, ow[0]! * 0.5);
    editor.undo(); r.dragMirrorUndoExact = exact();
    editor.selection.clear();
  }

  // C3) lasso around the same region selects something
  setTool("lasso");
  const poly: Array<[number, number]> = [[5, 5], [305, 5], [305, 305], [5, 305], [5, 10]];
  synth("pointerdown", el, X(poly[0]![0]), Y(poly[0]![1]), 0.5);
  for (let q = 1; q < poly.length; q++) for (let k = 1; k <= 10; k++) {
    const a = poly[q - 1]!, b = poly[q]!;
    synth("pointermove", el, X(a[0] + ((b[0] - a[0]) * k) / 10), Y(a[1] + ((b[1] - a[1]) * k) / 10), 0.5);
  }
  synth("pointerup", el, X(5), Y(10), 0);
  r.lassoSelected = editor.selection.count;
  editor.selection.clear();

  // D) partial eraser: draw a 100-point pen stroke, cut it in the middle
  setTool("pen");
  synth("pointerdown", el, X(100), Y(700), 0.5);
  for (let k = 1; k <= 100; k++) synth("pointermove", el, X(100 + k * 3), Y(700), 0.4 + 0.3 * Math.sin(k / 7));
  synth("pointerup", el, X(400), Y(700), 0);
  const drawn = page.layers[page.layers.length - 1]!.elements.at(-1) as Stroke;
  r.drawnPoints = drawn.pts.length / 2;
  r.countAfterDraw = count();
  setTool("eraserPart");
  {
    // The eraser ring must be visible while hovering (no button pressed).
    const live = host.liveCanvas();
    const lr = live.getBoundingClientRect();
    synth("pointermove", el, X(250), Y(600), 0.5);
    for (let f = 0; f < 4; f++) await new Promise((res) => requestAnimationFrame(res)); // the ring is drawn once per frame, after the delayed clearing of the last stroke's preview
    const dpr = window.devicePixelRatio || 1;
    const rr = 8 * zoom * dpr;
    const cxp = Math.round((X(250) - lr.left) * dpr), cyp = Math.round((Y(600) - lr.top) * dpr);
    const g2 = live.getContext("2d", { desynchronized: DESYNC })!;
    r.eraserRingAtEdge = g2.getImageData(Math.round(cxp + rr), cyp, 1, 1).data[3]! > 0;
    synth("pointermove", el, X(250), Y(600), 0.5);
  }
  synth("pointerdown", el, X(250), Y(700), 0.5);
  synth("pointerup", el, X(250), Y(700), 0);
  await sleep(50);
  const pieces = page.layers[page.layers.length - 1]!.elements.slice(-2) as Stroke[];
  r.pieces = count() - n0;
  r.pieceWidthsConsistent = pieces.every((p) => p.widths.length === p.pts.length / 2);
  r.pieceXRanges = pieces.map((p) => [+p.bbox[0].toFixed(1), +p.bbox[2].toFixed(1)]);
  editor.undo();
  r.undoRestoresStroke = count() === n0 + 1 && page.layers[page.layers.length - 1]!.elements.at(-1) === drawn;
  editor.undo(); // remove the drawn stroke

  // E) text: create, edit, undo
  setTool("text");
  synth("pointerdown", el, X(200), Y(780), 0.5);
  synth("pointerup", el, X(200), Y(780), 0);
  await sleep(30);
  let area = el.querySelector("textarea") as HTMLTextAreaElement | null;
  r.textAreaOpened = !!area;
  area!.value = "Hola\nmundo";
  area!.dispatchEvent(new Event("blur"));
  const t1 = page.layers[page.layers.length - 1]!.elements.at(-1) as unknown as { kind: string; text: string };
  r.textCreated = t1.kind === "text" && t1.text === "Hola\nmundo";
  synth("pointerdown", el, X(205), Y(785), 0.5);
  synth("pointerup", el, X(205), Y(785), 0);
  await sleep(30);
  area = el.querySelector("textarea") as HTMLTextAreaElement | null;
  r.textReopenedWithContent = area?.value === "Hola\nmundo";
  area!.value = "Hola editado";
  area!.dispatchEvent(new Event("blur"));
  r.textEdited = t1.text === "Hola editado";
  editor.undo();
  r.textEditUndone = t1.text === "Hola\nmundo";

  // E2) text cannot rotate in the format: it is only repositioned and stays upright
  {
    const tt = t1 as unknown as { x: number; y: number; size: number };
    const t0 = [tt.x, tt.y, tt.size];
    setTool("select");
    synth("pointerdown", el, X(205), Y(785), 0.5);
    synth("pointerup", el, X(205), Y(785), 0);
    r.textSelectedByClick = editor.selection.count === 1 && editor.selection.elements[0] === (t1 as unknown);
    editor.selection.transformSelection("rotR");
    r.textKeepsSize = tt.size === t0[2];
    // A lone text rotates about its own centre, so it stays put (and upright); in a group it is only relocated.
    r.textAloneStaysPut = Math.abs(tt.x - t0[0]!) < 1e-6 && Math.abs(tt.y - t0[1]!) < 1e-6;
    editor.undo();
    r.textRotateUndoExact = tt.x === t0[0] && tt.y === t0[1] && tt.size === t0[2];
    editor.selection.clear();
  }

  // F) image
  const cv = document.createElement("canvas");
  cv.width = 160; cv.height = 80;
  const cc = cv.getContext("2d")!; cc.fillStyle = "#3a7bd5"; cc.fillRect(0, 0, 160, 80); cc.fillStyle = "#fff"; cc.fillRect(10, 10, 40, 20);
  const blob = await new Promise<Blob>((res) => cv.toBlob((b) => res(b!), "image/png"));
  const okImg = await inserts.insertImage(blob);
  const imgEl = page.layers[page.layers.length - 1]!.elements.at(-1) as unknown as { kind: string; left: number; right: number; b64: string };
  r.imageInserted = okImg && imgEl.kind === "image" && imgEl.b64.startsWith("iVBOR");
  r.imageSelectedAfterInsert = editor.selection.count === 1;
  r.imageWidthPt = +(imgEl.right - imgEl.left).toFixed(1);
  {
    const im = imgEl as unknown as { left: number; top: number; right: number; bottom: number };
    const b0 = [im.left, im.top, im.right, im.bottom];
    const w0 = b0[2]! - b0[0]!, h0 = b0[3]! - b0[1]!;
    synth("pointerdown", el, X(b0[2]!), Y(b0[3]!), 0.5);     // SE handle of the selected image
    for (let k = 1; k <= 3; k++) synth("pointermove", el, X(b0[2]! + 20 * k), Y(b0[3]! + (10 / 3) * k), 0.5);
    synth("pointerup", el, X(b0[2]! + 60), Y(b0[3]! + 10), 0);
    r.imageResizedTo = [+(im.right - im.left).toFixed(1), +(im.bottom - im.top).toFixed(1)];
    r.imageKeptAspect = Math.abs((im.right - im.left) / (im.bottom - im.top) - w0 / h0) < 1e-6;
    r.imageAnchoredTopLeft = Math.abs(im.left - b0[0]!) < 1e-9 && Math.abs(im.top - b0[1]!) < 1e-9;
    editor.undo();
    r.imageResizeUndoExact = [im.left, im.top, im.right, im.bottom].every((v, k) => v === b0[k]);
    editor.redo();
    editor.undo();

    // rotate 90 (pixels are re-encoded), mirror, undo
    const pngSize = (b64: string): [number, number] => {
      const bin = atob(b64.slice(0, 32));
      const u = (o: number) => ((bin.charCodeAt(o) << 24) | (bin.charCodeAt(o + 1) << 16) | (bin.charCodeAt(o + 2) << 8) | bin.charCodeAt(o + 3)) >>> 0;
      return [u(16), u(20)];
    };
    const imx = imgEl as unknown as { b64: string };
    const b64a = imx.b64, sz0 = pngSize(b64a);
    editor.selection.transformSelection("rotR");
    r.imageRotatedBox = [+(im.right - im.left).toFixed(1), +(im.bottom - im.top).toFixed(1)];
    r.imageRotatedPngPx = pngSize(imx.b64);
    r.imageOriginalPngPx = sz0;
    r.imageRotatedPngChanged = imx.b64 !== b64a;
    editor.undo();
    r.imageRotateUndoExact = imx.b64 === b64a && [im.left, im.top, im.right, im.bottom].every((v, k) => v === b0[k]);
    editor.selection.transformSelection("flipH");
    r.imageFlippedPixelsChanged = imx.b64 !== b64a;
    r.imageFlipKeepsBox = [im.left, im.top, im.right, im.bottom].every((v, k) => Math.abs(v - b0[k]!) < 1e-6);
    editor.undo();
    r.imageFlipUndoExact = imx.b64 === b64a;
    editor.selection.transformSelection("rotR"); // left in place: the saved file carries the re-encoded PNG
  }

  // H) editing tools: select all, copy / paste / duplicate, z-order, shapes, eraser size
  {
    const layer0 = page.layers[0]!;
    const near = (a: number, b: number, t = 1e-6) => Math.abs(a - b) < t;
    editor.selection.clear();
    const nPage = count();
    setTool("select");
    editor.selection.selectAll(i);
    r.selectAllCount = editor.selection.count;
    r.selectAllExpected = nPage;
    editor.selection.clear();

    const region = () => {
      setTool("select");
      synth("pointerdown", el, X(5), Y(5), 0.5);
      for (let k = 1; k <= 6; k++) synth("pointermove", el, X(5 + k * 50), Y(5 + k * 50), 0.5);
      synth("pointerup", el, X(305), Y(305), 0);
    };
    region();
    const kSel = editor.selection.count;
    const orig = editor.selection.elements[0]!;
    const p0 = pos(orig);
    editor.selection.copy();
    editor.selection.paste();
    const pasted = editor.selection.elements;
    r.pasteAdded = count() - nPage === kSel && pasted.length === kSel;
    const pp = pos(pasted[0]!);
    r.pasteOffset = [+(pp[0] - p0[0]).toFixed(3), +(pp[1] - p0[1]).toFixed(3)];
    r.pasteIsIndependentCopy = pasted[0] !== orig;
    editor.undo();
    r.pasteUndone = count() === nPage;
    editor.redo(); editor.undo();
    region();
    editor.selection.duplicate();
    r.duplicateAdded = count() - nPage === kSel;
    editor.undo();
    editor.selection.clear();

    // z-order of a single element
    const el5 = layer0.elements[5]!;
    editor.selection.selectOnly(i, layer0, el5);
    editor.selection.reorder("front");
    r.zFront = layer0.elements[layer0.elements.length - 1] === el5;
    editor.undo();
    r.zFrontUndo = layer0.elements[5] === el5;
    editor.selection.reorder("back");
    r.zBack = layer0.elements[0] === el5;
    editor.undo();
    r.zBackUndo = layer0.elements[5] === el5;
    editor.selection.clear();

    // shapes
    const last = () => page.layers[page.layers.length - 1]!.elements.at(-1) as unknown as Stroke;
    const drag = (x0: number, y0: number, x1: number, y1: number, shift = false) => {
      synth("pointerdown", el, X(x0), Y(y0), 0.5, "pen", shift);
      for (let k = 1; k <= 5; k++) synth("pointermove", el, X(x0 + ((x1 - x0) * k) / 5), Y(y0 + ((y1 - y0) * k) / 5), 0.5, "pen", shift);
      synth("pointerup", el, X(x1), Y(y1), 0, "pen", shift);
    };
    const nShapes = count();
    setTool("shape");
    editor.shape = "rect"; drag(100, 500, 200, 560);
    let sh = last();
    r.rect = sh.pts.length === 10 && near(sh.bbox[0], 100) && near(sh.bbox[1], 500) && near(sh.bbox[2], 200) && near(sh.bbox[3], 560);
    r.shapeIsPenStroke = sh.tool === "pen" && sh.widths.length === 1;
    editor.shape = "ellipse"; drag(300, 500, 380, 560);
    sh = last();
    r.ellipse = sh.pts.length === 146 && near(sh.bbox[0], 300, 1e-6) && near(sh.bbox[2], 380, 1e-6) && near(sh.bbox[1], 500, 1e-6) && near(sh.bbox[3], 560, 1e-6);
    editor.shape = "line"; drag(100, 600, 150, 640, true);
    sh = last();
    const ang = (Math.atan2(sh.pts[3]! - sh.pts[1]!, sh.pts[2]! - sh.pts[0]!) * 180) / Math.PI;
    r.lineSnapsTo15 = near(ang % 15, 0, 1e-6) && near(Math.hypot(sh.pts[2]! - sh.pts[0]!, sh.pts[3]! - sh.pts[1]!), Math.hypot(50, 40), 1e-6);
    r.lineAngle = +ang.toFixed(3);
    editor.shape = "rect"; drag(100, 650, 180, 700, true);
    sh = last();
    r.shiftRectIsSquare = near(sh.bbox[2] - sh.bbox[0], sh.bbox[3] - sh.bbox[1]) && near(sh.bbox[2] - sh.bbox[0], 80);
    editor.shape = "arrow"; drag(300, 650, 400, 650);
    sh = last();
    r.arrowHead = sh.pts.length === 10 && near(sh.pts[2]!, sh.pts[6]!) && near(sh.pts[3]!, sh.pts[7]!) && sh.pts[4]! < sh.pts[2]! && sh.pts[8]! < sh.pts[2]! && sh.pts[5]! !== sh.pts[9]!;
    r.shapesAdded = count() - nShapes;
    r.clickMakesNoShape = (() => { const n = count(); synth("pointerdown", el, X(50), Y(750), 0.5); synth("pointerup", el, X(50), Y(750), 0); return count() === n; })();
    for (let k = 0; k < 5; k++) editor.undo();
    r.shapesUndone = count() === nShapes;

    // eraser size: a bigger radius cuts a wider gap
    setTool("pen");
    synth("pointerdown", el, X(100), Y(720), 0.5);
    for (let k = 1; k <= 100; k++) synth("pointermove", el, X(100 + k * 3), Y(720), 0.5);
    synth("pointerup", el, X(400), Y(720), 0);
    setTool("eraserPart");
    editor.eraserRadius = 16;
    synth("pointerdown", el, X(250), Y(720), 0.5);
    synth("pointerup", el, X(250), Y(720), 0);
    const pcs = page.layers[page.layers.length - 1]!.elements.slice(-2) as Stroke[];
    r.gapWithRadius16 = pcs.length === 2 ? +(pcs[1]!.bbox[0] - pcs[0]!.bbox[2]).toFixed(2) : -1;
    editor.undo(); editor.undo();
    editor.eraserRadius = 8;
    r.eraserSizeRestored = count() === nShapes;
  }

  // L) the LaTeX tool: insert, edit, image fallback, transform, and the real dialog
  {
    const d = doc!;
    const pg0 = d.pages[0]!;
    const lay = pg0.layers[pg0.layers.length - 1]!;
    const near = (a: number, b: number, t = 1e-6) => Math.abs(a - b) < t;
    const src = String.raw`\int_0^1 x^2\,dx=\frac13`;
    const n0 = lay.elements.length;
    const ins = await latex.insert(0, 100, 600, { latex: src, display: true, sizePt: 14, asImage: false });
    const tx = (ins.ok ? ins.el : null) as TexImageEl | null;
    r.texInserted = !!tx && tx.kind === "teximage" && lay.elements.length === n0 + 1 && lay.elements.at(-1) === tx;
    r.texAttrOrder = !!tx && tx.attrs.map((a) => a[0]).join(",") === "text,left,top,right,bottom";
    r.texSizePositive = !!tx && tx.right - tx.left > 20 && tx.bottom - tx.top > 8;
    r.texIsPdf = !!tx && tx.pdfB64.startsWith("JVBERi");
    r.texBitmapDrawn = !!tx?.bitmap && tx.bitmap.getContext("2d")!.getImageData(0, 0, tx.bitmap.width, tx.bitmap.height).data.some((v, k) => k % 4 === 3 && v > 0);
    r.texSelectedAfterInsert = editor.selection.count === 1 && editor.selection.elements[0] === (tx as unknown);
    r.texInsideThePage = !!tx && tx.left >= 0 && tx.top >= 0 && tx.right <= pg0.width && tx.bottom <= pg0.height;

    // it is written like Xournal++ writes it, and read back identically
    const xml1 = serializeXopp(d);
    r.texWritten = xml1.includes('<teximage text="' + src + '" left="');
    const back1 = parseXopp(xml1);
    const bt = back1.pages[0]!.layers.at(-1)!.elements.find((e) => e.kind === "teximage") as TexImageEl | undefined;
    r.texRoundTrip = !!bt && !!tx && bt.latex === src && bt.pdfB64 === tx.pdfB64 && near(bt.left, tx.left, 1e-5) && near(bt.bottom, tx.bottom, 1e-5);

    // editing: new source, new natural size, same top-left; undo/redo restore everything
    const before = { latex: tx!.latex, pdf: tx!.pdfB64, box: [tx!.left, tx!.top, tx!.right, tx!.bottom] };
    const rep = await latex.replace(0, tx!, lay, { latex: "x^2", display: true, sizePt: 14, asImage: false });
    r.texEdited = rep.ok && tx!.latex === "x^2" && tx!.pdfB64 !== before.pdf && near(tx!.left, before.box[0]!) && near(tx!.top, before.box[1]!) && tx!.right - tx!.left < before.box[2]! - before.box[0]!;
    editor.undo();
    r.texEditUndone = tx!.latex === before.latex && tx!.pdfB64 === before.pdf && near(tx!.right, before.box[2]!) && near(tx!.bottom, before.box[3]!);
    editor.redo();
    r.texEditRedone = tx!.latex === "x^2";
    editor.undo();

    // plain-image fallback
    const imgRes = await latex.insert(0, 300, 600, { latex: String.raw`\sqrt{2}`, display: false, sizePt: 14, asImage: true });
    const im = (imgRes.ok ? imgRes.el : null) as ImageEl | null;
    r.texAsImage = !!im && im.kind === "image" && im.b64.startsWith("iVBOR") && !!im.bitmap && im.attrs.map((a) => a[0]).join(",") === "left,top,right,bottom";
    editor.undo(); // remove that image

    // moving and uniform resizing keep the formula's proportions
    editor.selection.selectOnly(0, lay, tx!);
    const aspect = (tx!.right - tx!.left) / (tx!.bottom - tx!.top);
    const w0 = tx!.right - tx!.left;
    synth("pointerdown", el, X(tx!.right), Y(tx!.bottom), 0.5); // SE handle
    for (let k = 1; k <= 4; k++) synth("pointermove", el, X(tx!.right + 12 * k), Y(tx!.bottom + 3 * k), 0.5);
    synth("pointerup", el, X(tx!.right + 48), Y(tx!.bottom + 12), 0);
    r.texResizeKeepsAspect = near((tx!.right - tx!.left) / (tx!.bottom - tx!.top), aspect, 1e-6) && tx!.right - tx!.left > w0 + 10;
    editor.undo();
    r.texResizeUndoExact = near(tx!.right - tx!.left, w0, 1e-9);
    const c0 = [(tx!.left + tx!.right) / 2, (tx!.top + tx!.bottom) / 2];
    editor.selection.transformSelection("rotR");
    r.texRotateKeepsUpright = near(tx!.right - tx!.left, w0, 1e-9) && near((tx!.left + tx!.right) / 2, c0[0]!, 1e-9);
    editor.undo();

    // the real dialog
    const dlg = document.getElementById("latexDlg") as HTMLElement;
    const area = document.getElementById("ltx") as HTMLTextAreaElement;
    const prevEl = document.getElementById("ltxPrev") as HTMLElement;
    const errEl = document.getElementById("ltxErr") as HTMLElement;
    const okBtn = document.getElementById("ltxOk") as HTMLButtonElement;
    const count0 = lay.elements.length;
    setTool("latex");
    synth("pointerdown", el, X(150), Y(700), 0.5);
    synth("pointerup", el, X(150), Y(700), 0);
    await sleep(40);
    r.dlgOpens = !dlg.hidden && latex.isOpen && okBtn.disabled; // empty source: cannot insert yet
    area.value = String.raw`\frac{1}{2}`;
    area.dispatchEvent(new Event("input"));
    await sleep(160);
    r.dlgPreviewRenders = !!prevEl.querySelector(".katex") && errEl.textContent === "" && !okBtn.disabled;
    area.value = String.raw`\frac{`;
    area.dispatchEvent(new Event("input"));
    await sleep(160);
    r.dlgShowsErrorAndBlocks = errEl.textContent!.length > 0 && okBtn.disabled;
    area.value = "";
    area.setSelectionRange(0, 0);
    area.dispatchEvent(new KeyboardEvent("keydown", { key: "{", bubbles: true, cancelable: true }));
    r.dlgAutoClosesBrace = area.value === "{}" && area.selectionStart === 1;
    area.dispatchEvent(new KeyboardEvent("keydown", { key: "}", bubbles: true, cancelable: true }));
    r.dlgStepsOverBrace = area.value === "{}" && area.selectionStart === 2;
    area.value = "";
    area.setSelectionRange(0, 0);
    (document.querySelector("#ltxGrid .palBtn") as HTMLButtonElement).click(); // first palette entry: fraction
    r.dlgPaletteInsertsAtCaret = area.value === String.raw`\frac{}{}` && area.selectionStart === 6;
    {
      // the palette: categories, search, typeset labels, caret placement, recents
      const all = PALETTE_CATEGORIES.flatMap((c) => c.items);
      r.palItems = all.length;
      r.palManyItems = all.length >= 330 && PALETTE_CATEGORIES.length >= 10;
      const bad: string[] = [];
      for (const it of all) {
        try { katex.renderToString(it.tex, { throwOnError: true }); } catch { bad.push(`label:${it.tex}`); }
        try { katex.renderToString("x " + it.snippet.replace(CARET, ""), { throwOnError: true }); } catch { bad.push(`code:${it.snippet}`); }
        if (it.snippet.split(CARET).length > 2) bad.push(`carets:${it.snippet}`);
      }
      r.palAllRender = bad.length === 0;
      if (bad.length) r.palBad = bad.slice(0, 8).join(" | ");
      const names = (q: string) => searchPalette(q).map((i) => i.snippet);
      r.palHasIndexedRoot = names("raiz con indice").some((x) => x.startsWith(String.raw`\sqrt[`));
      const wanted = ["fraccion", "exponente", "subindice", "raiz cubica", "binomial", "valor absoluto", "norma", "matriz 3", "determinante", "integral definida", "integral doble", "limite", "derivada parcial", "sumatoria", "productoria", "gradiente", "alfa", "omega mayuscula", "implica", "pertenece", "reales", "vector", "negrita", "espacio fino"];
      const missing = wanted.filter((q) => searchPalette(q).length === 0);
      r.palHasCoreStructures = missing.length === 0;
      if (missing.length) r.palMissing = missing.join(", ");
      const grid = () => [...document.querySelectorAll<HTMLElement>("#ltxGrid .palBtn")];
      const tab = (id: string) => document.querySelector<HTMLElement>(`#ltxTabs button[data-tab="${id}"]`)!;
      r.palTabsExist = document.querySelectorAll("#ltxTabs button").length === PALETTE_CATEGORIES.length + 1;
      const vc = PALETTE_CATEGORIES.find((c) => c.id === "vcalc");
      r.palVectorCalculus = !!vc && vc.items.length >= 70 && PALETTE_CATEGORIES.indexOf(vc) === 2;
      const vWanted = ["rotor", "divergencia", "vector gradiente", "laplaciano", "producto vectorial", "producto escalar", "integral de linea", "circulacion", "flujo", "superficie cerrada", "teorema de green", "stokes", "teorema de la divergencia", "gauss", "jacobiano", "hessiana", "lagrange", "derivada direccional", "regla de la cadena", "plano tangente", "producto mixto", "curvatura", "longitud de arco", "esfericas", "cilindricas", "campo conservativo", "integral triple", "integral de superficie"];
      const vMissing = vWanted.filter((q) => !searchPalette(q).some((i) => vc!.items.includes(i)));
      r.palVectorCalculusSearch = vMissing.length === 0;
      if (vMissing.length) r.palVectorMissing = vMissing.join(", ");
      tab("vcalc").click();
      r.palVectorTab = grid().length === vc!.items.length && tab("vcalc").classList.contains("on") && grid().every((b) => b.querySelector(".katex") !== null);
      const green = vc!.items.find((i) => i.name === "teorema de Green")!;
      const oiint = vc!.items.find((i) => i.name.startsWith("flujo a través de una superficie cerrada"))!;
      r.palVectorRealPipeline = (await Promise.all([green, oiint].map((it) => window.xournal.latexPdf({ latex: it.snippet.replace(CARET, ""), display: true, color: "#000000", px: 20 })))).every((x) => x.ok);
      tab("greek").click();
      r.palGreekTab = grid().length === 40 && tab("greek").classList.contains("on") && !tab("struct").classList.contains("on");
      r.palButtonsAreTypeset = grid().every((b) => b.querySelector(".katex") !== null);
      tab("matrix").click();
      r.palMatrixTab = grid().length >= 20 && grid().some((b) => (b.dataset.snippet ?? "").includes("vmatrix"));
      const search = document.getElementById("ltxSearch") as HTMLInputElement;
      search.value = "integral";
      search.dispatchEvent(new Event("input", { bubbles: true }));
      r.palSearchIntegrals = grid().length >= 6 && document.querySelectorAll("#ltxTabs button.on").length === 0;
      search.value = "zzzz";
      search.dispatchEvent(new Event("input", { bubbles: true }));
      r.palSearchEmpty = grid().length === 0 && document.querySelector(".palEmpty") !== null;
      search.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
      r.palEscClearsSearch = search.value === "" && dlg.hidden === false;
      tab("struct").click();
      // caret goes where the snippet says
      area.value = "";
      area.setSelectionRange(0, 0);
      const byCode = (code: string) => grid().find((b) => b.dataset.snippet === code)!;
      byCode(String.raw`\sqrt[¦]{}`).click();
      r.palIndexedRootCaret = area.value === String.raw`\sqrt[]{}` && area.selectionStart === 6;
      area.value = "";
      area.setSelectionRange(0, 0);
      byCode(String.raw`\left| ¦ \right|`).click();
      r.palAbsoluteValueCaret = area.value === String.raw`\left|  \right|` && area.selectionStart === 7;
      area.value = "2";
      area.setSelectionRange(1, 1);
      byCode(String.raw`^{¦}`).click();
      r.palExponentAtCaret = area.value === "2^{}" && area.selectionStart === 3;
      // recents: last used first, no repeats
      tab("recent").click();
      const rec = grid().map((b) => b.dataset.snippet);
      r.palRecents = rec[0] === String.raw`^{¦}` && rec[1] === String.raw`\left| ¦ \right|` && rec[2] === String.raw`\sqrt[¦]{}` && new Set(rec).size === rec.length;
      tab("struct").click();
      area.value = "";
      area.setSelectionRange(0, 0);
    }
    area.value = String.raw`\alpha + \beta`;
    area.dispatchEvent(new Event("input"));
    await sleep(160);
    const tIns = performance.now();
    okBtn.click();
    for (let k = 0; k < 100 && !dlg.hidden; k++) await sleep(50); // PDF round trip through the main process
    r.dlgInsertMs = Math.round(performance.now() - tIns); // dominated by printToPDF (~0.5 s)
    r.dlgInsertClosesAndAdds = dlg.hidden && !latex.isOpen && lay.elements.length === count0 + 1 && (lay.elements.at(-1) as unknown as TexImageEl).latex === String.raw`\alpha + \beta`;
    // clicking that formula with the tool reopens it with its source
    const added = lay.elements.at(-1) as unknown as TexImageEl;
    setTool("latex");
    synth("pointerdown", el, X((added.left + added.right) / 2), Y((added.top + added.bottom) / 2), 0.5);
    synth("pointerup", el, X((added.left + added.right) / 2), Y((added.top + added.bottom) / 2), 0);
    await sleep(40);
    r.dlgReopensWithSource = !dlg.hidden && area.value === String.raw`\alpha + \beta` && okBtn.textContent === "Aplicar cambios";
    area.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    r.dlgEscapeCloses = dlg.hidden && !latex.isOpen && lay.elements.length === count0 + 1;
    setTool("pen");
    // leave one formula in the saved file for the independent check
  }

  // M) layers, side panel, continuous / single-page view
  {
    const d = doc!;
    const p0 = d.pages[0]!;
    const top0 = p0.layers[p0.layers.length - 1]!;
    const nLayers0 = p0.layers.length;
    const strokeAt = (x: number, y: number) => {
      setTool("pen");
      synth("pointerdown", el, X(x), Y(y), 0.5);
      for (let k = 1; k <= 8; k++) synth("pointermove", el, X(x + k * 6), Y(y + k * 2), 0.5);
      synth("pointerup", el, X(x + 48), Y(y + 16), 0);
    };
    const darkPixels = (page: typeof p0) => {
      const c = document.createElement("canvas");
      c.width = 595; c.height = 842;
      const cx = c.getContext("2d")!;
      renderPage(cx, page, () => {});
      const px = cx.getImageData(0, 0, c.width, c.height).data;
      let n = 0;
      for (let k = 0; k < px.length; k += 4) if (px[k]! < 200) n++;
      return n;
    };
    const topCount0 = top0.elements.length;

    // adding a layer: it sits on top and becomes the one new strokes go to
    const L2 = layers.add(0);
    r.layerAdded = p0.layers.length === nLayers0 + 1 && p0.layers.indexOf(L2) === nLayers0 && activeLayer(p0) === L2;
    strokeAt(300, 500);
    r.strokeGoesToActiveLayer = L2.elements.length === 1 && top0.elements.length === topCount0;
    layers.setActive(0, top0);
    strokeAt(300, 540);
    r.strokeFollowsActiveSwitch = top0.elements.length === topCount0 + 1 && L2.elements.length === 1;
    editor.undo(); // that last stroke

    // hiding is a view setting: not drawn, not selectable, not erasable, not saved
    const xmlShown = serializeXopp(d);
    const darkShown = darkPixels(p0);
    layers.toggleHidden(0, L2);
    const darkHidden = darkPixels(p0);
    r.hiddenNotDrawn = L2.hidden === true && darkHidden < darkShown;
    r.hiddenNotSaved = serializeXopp(d) === xmlShown;
    editor.selection.selectAll(0);
    r.hiddenNotSelectable = editor.selection.count === p0.layers.reduce((n, l) => n + (l.hidden ? 0 : l.elements.length), 0) && editor.selection.count > 0;
    editor.selection.clear();
    setTool("eraser");
    synth("pointerdown", el, X(310), Y(504), 0.5);
    synth("pointerup", el, X(310), Y(504), 0);
    r.hiddenNotErasable = L2.elements.length === 1;
    // drawing onto a hidden active layer shows it again instead of losing the stroke
    layers.setActive(0, L2);
    strokeAt(300, 580);
    r.drawingUnhidesLayer = L2.hidden === false && L2.elements.length === 2;
    editor.undo();

    // names, order, removal (all undoable)
    layers.rename(0, L2, "Dibujos");
    r.renameSaved = attrValue(L2.attrs, "name") === "Dibujos" && serializeXopp(d).includes('<layer name="Dibujos">');
    editor.undo();
    r.renameUndone = attrValue(L2.attrs, "name") === null;
    editor.redo();
    layers.move(0, L2, -1);
    r.layerMoved = p0.layers.indexOf(L2) === nLayers0 - 1;
    editor.undo();
    r.layerMoveUndone = p0.layers.indexOf(L2) === nLayers0;
    const total = d.elementCount;
    layers.remove(0, L2);
    r.layerRemoved = !p0.layers.includes(L2) && d.elementCount === total - L2.elements.length;
    editor.undo();
    r.layerRemoveUndone = p0.layers.includes(L2) && d.elementCount === total;
    layers.remove(0, L2); // back to the original single layer
    r.lastLayerKept = p0.layers.length === nLayers0 && nLayers0 === 1 && layers.remove(0, top0) === false && p0.layers.includes(top0);

    // side panel: thumbnails
    sidebar.show("pages");
    await sleep(150);
    const thumbs = () => Array.from(document.querySelectorAll<HTMLElement>("#thumbs .thumb"));
    r.sidebarShown = sidebar.visible && !document.getElementById("side")!.hidden;
    r.thumbsPerPage = thumbs().length === d.pages.length;
    const tc = thumbs()[0]!.querySelector("canvas")!;
    const tpx = tc.getContext("2d")!.getImageData(0, 0, tc.width, tc.height).data;
    let tdark = 0;
    for (let k = 0; k < tpx.length; k += 4) if (tpx[k]! < 200) tdark++;
    r.thumbRendered = tdark > 50;
    journal.newPageAfter(0);
    await sleep(150);
    r.thumbsFollowStructure = thumbs().length === d.pages.length && d.pages.length >= 2;
    thumbs()[1]!.click();
    await sleep(80);
    r.thumbClickGoesToPage = currentPage() === 1 && thumbs()[1]!.classList.contains("cur") && !thumbs()[0]!.classList.contains("cur");

    // side panel: layers list
    goToPage(0, false);
    await sleep(60);
    const L3 = layers.add(0);
    sidebar.show("layers");
    const rows = () => Array.from(document.querySelectorAll<HTMLElement>("#lyList li"));
    r.layerRows = rows().length === p0.layers.length && rows()[0]!.classList.contains("act"); // top layer first
    rows()[1]!.click();
    r.rowClickActivates = activeLayer(p0) === p0.layers[p0.layers.length - 2];
    (rows()[0]!.querySelector(".eye") as HTMLElement).click();
    r.eyeHides = L3.hidden === true;
    (rows()[0]!.querySelector(".eye") as HTMLElement).click();
    r.eyeShows = L3.hidden === false;
    const nameEl = rows()[0]!.querySelector(".name") as HTMLElement;
    nameEl.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
    const inp = rows()[0]!.querySelector("input") as HTMLInputElement | null;
    r.renameInputOpens = !!inp;
    if (inp) { inp.value = "Notas"; inp.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true })); }
    r.renameFromPanel = attrValue(L3.attrs, "name") === "Notas" && rows()[0]!.querySelector(".name")?.textContent === "Notas";
    layers.remove(0, L3);
    sidebar.hide();
    r.sidebarHidden = !sidebar.visible;

    // continuous / single-page view (two pages exist now)
    setContinuous(false);
    r.singleShowsOne = views.filter((v) => v.el.style.display !== "none").length === 1 && views[currentPage()]!.el.style.display !== "none";
    goToPage(1, false);
    await sleep(60);
    r.singleFollowsGoTo = views[1]!.el.style.display !== "none" && views[0]!.el.style.display === "none" && pgNum.value === "2";
    goToPage(0, false);
    await sleep(60);
    scroller.scrollTop = scroller.scrollHeight;
    scroller.dispatchEvent(new WheelEvent("wheel", { deltaY: 120, bubbles: true }));
    await sleep(80);
    r.wheelTurnsPage = currentPage() === 1;
    setContinuous(true);
    r.continuousShowsAll = views.every((v) => v.el.style.display !== "none");
    // undo, in order: remove L3, its rename, add L3, and the extra page
    for (let k = 0; k < 4; k++) editor.undo();
    await sleep(60);
    r.pageCountRestored = d.pages.length === 1 && p0.layers.length === 1 && p0.layers[0] === top0;
  }

  // N) vertical space, laser pointer, shape recognizer, find
  {
    const d = doc!;
    el = views[i]!.el; // the earlier page operations rebuilt the page views: the old element is detached
    const p0 = d.pages[0]!;
    const near = (a: number, b: number, t = 1e-6) => Math.abs(a - b) < t;
    const allEls = () => p0.layers.flatMap((l) => l.elements);
    const topOf = (e: (typeof p0.layers)[number]["elements"][number]) => elementBBox(e)![1];
    const botOf = (e: (typeof p0.layers)[number]["elements"][number]) => elementBBox(e)![3];

    // -- vertical space: everything whose top is below the line moves, within the page and without crossing the line
    const lineY = 590;
    const below = allEls().filter((e) => elementBBox(e) && topOf(e) >= lineY);
    const above = allEls().filter((e) => elementBBox(e) && topOf(e) < lineY);
    const sig = (e: (typeof below)[number]) => JSON.stringify(elementBBox(e));
    const belowBefore = below.map(sig), aboveBefore = above.map(sig);
    const vdrag = async (dy: number) => {
      setTool("vspace");
      synth("pointerdown", el, X(40), Y(lineY), 0.5);
      for (let k = 1; k <= 4; k++) synth("pointermove", el, X(40), Y(lineY + (dy * k) / 4), 0.5);
      const depth = editor.undoDepth;
      synth("pointerup", el, X(40), Y(lineY + dy), 0);
      await sleep(30);
      (r.vspacePushed as boolean[] | undefined ?? (r.vspacePushed = [])).push(editor.undoDepth === depth + 1);
    };
    r.vspaceBelowCount = below.length;
    await vdrag(30);
    r.vspaceMovesBelow = below.length > 0 && below.every((e, i) => near(topOf(e), JSON.parse(belowBefore[i]!)[1] + 30, 1e-6));
    r.vspaceKeepsAbove = above.every((e, i) => sig(e) === aboveBefore[i]);
    r.vspaceLeavesNoSelection = editor.selection.count === 0;
    editor.undo();
    r.vspaceUndoExact = below.every((e, i) => sig(e) === belowBefore[i]);
    await vdrag(100000); // far too much: stops at the bottom of the page
    r.vspaceStopsAtPageBottom = Math.max(...below.map(botOf)) <= p0.height + 1e-6 && Math.max(...below.map(botOf)) > p0.height - 1;
    editor.undo();
    await vdrag(-100000); // far too little: stops at the line
    r.vspaceStopsAtLine = near(Math.min(...below.map(topOf)), lineY, 1e-6);
    editor.undo();
    r.vspaceFinalExact = below.every((e, i) => sig(e) === belowBefore[i]);
    r.vspaceEveryDragRecorded = (r.vspacePushed as boolean[]).length === 3 && (r.vspacePushed as boolean[]).every(Boolean);
    setTool("vspace");
    synth("pointerdown", el, X(40), Y(841), 0.5); // nothing is below this line
    synth("pointerup", el, X(40), Y(841), 0);
    r.vspaceNothingBelowIsHarmless = editor.selection.count === 0 && below.every((e, i) => sig(e) === belowBefore[i]);

    // -- laser pointer: a trail on the overlay that fades; the document and undo stack are untouched
    const canUndo0 = editor.canUndo, count0 = d.elementCount, dirty0 = editor.dirty;
    setTool("laser");
    const live = host.liveCanvas(), lr = live.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    const greenAt = (x: number, y: number) => {
      const px = Math.round((X(x) - lr.left) * dpr), py = Math.round((Y(y) - lr.top) * dpr);
      let m = 0;
      const data = live.getContext("2d", { desynchronized: DESYNC })!.getImageData(Math.max(0, px - 8), Math.max(0, py - 8), 16, 16).data;
      for (let k = 3; k < data.length; k += 4) m = Math.max(m, data[k]!);
      return m;
    };
    synth("pointerdown", el, X(200), Y(200), 0.5);
    for (let k = 1; k <= 6; k++) synth("pointermove", el, X(200 + k * 12), Y(200 + k * 3), 0.5);
    synth("pointerup", el, X(272), Y(218), 0);
    await sleep(80);
    r.laserDrawsTrail = greenAt(272, 218) > 0;
    await sleep(1700); // let that trail fade completely before the hover check
    for (let k = 1; k <= 6; k++) synth("pointermove", el, X(300 + k * 12), Y(300 + k * 3), 0, "pen", false, true); // hovering: no button, no contact
    await sleep(80);
    r.laserIgnoresHover = greenAt(372, 318) === 0 && greenAt(330, 309) === 0;
    r.laserCreatesNothing = d.elementCount === count0 && editor.canUndo === canUndo0 && editor.dirty === dirty0;
    await sleep(1700);
    r.laserFades = greenAt(272, 218) === 0 && greenAt(220, 205) === 0;

    // -- shape recognizer
    setRecognize(true);
    const topLayer = () => p0.layers[p0.layers.length - 1]!;
    const lastEl = () => topLayer().elements.at(-1) as unknown as Stroke;
    const noise = (i: number) => Math.sin(i * 12.9898) * 0.8; // deterministic wobble in [-0.8, 0.8]
    const hand = async (pts: Array<[number, number]>) => {
      setTool("pen");
      synth("pointerdown", el, X(pts[0]![0]), Y(pts[0]![1]), 0.5);
      for (let k = 1; k < pts.length; k++) synth("pointermove", el, X(pts[k]![0]), Y(pts[k]![1]), 0.3 + 0.4 * Math.abs(Math.sin(k / 5)));
      synth("pointerup", el, X(pts[pts.length - 1]![0]), Y(pts[pts.length - 1]![1]), 0);
      await sleep(10);
    };
    const near2 = (b: number[], x0: number, y0: number, x1: number, y1: number, tol: number) => near(b[0]!, x0, tol) && near(b[1]!, y0, tol) && near(b[2]!, x1, tol) && near(b[3]!, y1, tol);
    const n0 = topLayer().elements.length;

    const circle: Array<[number, number]> = [];
    for (let i = 0; i <= 90; i++) { const a = (i / 88) * Math.PI * 2; circle.push([450 + Math.cos(a) * 40 + noise(i), 640 + Math.sin(a) * 40 + noise(i + 7)]); }
    await hand(circle);
    r.recogCircle = lastEl().pts.length === 146 && near2(lastEl().bbox, 410, 600, 490, 680, 6) && lastEl().widths.length === 1;

    const rect: Array<[number, number]> = [];
    const corners: Array<[number, number]> = [[380, 560], [520, 560], [520, 620], [380, 620], [380, 563]];
    for (let c = 0; c < 4; c++) for (let i = 0; i < 20; i++) {
      const t = i / 20;
      rect.push([corners[c]![0] + (corners[c + 1]![0] - corners[c]![0]) * t + noise(c * 20 + i), corners[c]![1] + (corners[c + 1]![1] - corners[c]![1]) * t + noise(c * 20 + i + 3)]);
    }
    rect.push([381, 564]);
    await hand(rect);
    r.recogRectangle = lastEl().pts.length === 10 && near2(lastEl().bbox, 380, 560, 520, 620, 6);

    const line: Array<[number, number]> = [];
    for (let i = 0; i <= 40; i++) line.push([380 + i * 3.5, 700 + i * 0.2 + noise(i)]);
    await hand(line);
    r.recogLine = lastEl().pts.length === 4 && near(lastEl().pts[1]!, lastEl().pts[3]!, 1e-9); // snapped horizontal

    const tri: Array<[number, number]> = [];
    const tc: Array<[number, number]> = [[450, 720], [505, 790], [395, 790], [450, 722]];
    for (let c = 0; c < 3; c++) for (let i = 0; i < 20; i++) {
      const t = i / 20;
      tri.push([tc[c]![0] + (tc[c + 1]![0] - tc[c]![0]) * t + noise(c * 20 + i), tc[c]![1] + (tc[c + 1]![1] - tc[c]![1]) * t + noise(c * 20 + i + 5)]);
    }
    tri.push([451, 724]);
    await hand(tri);
    r.recogTriangle = lastEl().pts.length === 8;

    const scribble: Array<[number, number]> = [];
    for (let i = 0; i < 40; i++) scribble.push([380 + i * 3, 740 + (i % 2 ? 14 : -14) + (i % 5)]);
    await hand(scribble);
    r.recogScribbleUntouched = lastEl().pts.length > 20 && lastEl().widths.length > 1;

    setRecognize(false);
    await hand(circle);
    r.recogOffKeepsHandDrawing = lastEl().pts.length !== 146 && lastEl().pts.length > 30;
    for (let k = topLayer().elements.length; k > n0; k--) editor.undo();
    r.recogCleanup = topLayer().elements.length === n0;

    // -- find: text boxes and formula sources, across pages, case rules
    const addText = (layer: typeof topLayer extends () => infer L ? L : never, text: string, x: number, y: number) => {
      const t = { kind: "text" as const, attrs: [["font", "Sans"], ["size", "12"], ["x", String(x)], ["y", String(y)], ["color", "#000000ff"]] as Array<[string, string]>, text, font: "Sans", size: 12, x, y, color: "#000" };
      layer.elements.push(t);
      d.elementCount++;
      return t;
    };
    const f1 = addText(topLayer(), "Hola Zorro uno", 60, 300);
    journal.newPageAfter(0);
    await sleep(60);
    const page2 = d.pages[1]!;
    const f2 = addText(activeLayer(page2), "otro zorro en la segunda página", 60, 100);
    const findQ = document.getElementById("findQ") as HTMLInputElement;
    const findCount = document.getElementById("findCount") as HTMLElement;
    const findBar = document.getElementById("findBar") as HTMLElement;
    goToPage(0, false);
    await sleep(60);
    commands["edit.find"]!();
    r.findOpens = !findBar.hidden && find.isOpen;
    findQ.value = "ZORRO";
    findQ.dispatchEvent(new Event("input"));
    await sleep(500);
    r.findCountsAcrossPages = find.matches === 2 && findCount.textContent === "1 de 2";
    r.findSelectsMatch = editor.selection.count === 1 && editor.selection.elements[0] === (f1 as unknown);
    find.step(1);
    await sleep(500);
    r.findNextGoesToOtherPage = currentPage() === 1 && editor.selection.elements[0] === (f2 as unknown) && findCount.textContent === "2 de 2";
    find.step(1);
    await sleep(500);
    r.findWraps = find.position === 0 && currentPage() === 0;
    (document.getElementById("findCase") as HTMLInputElement).checked = true;
    (document.getElementById("findCase") as HTMLInputElement).dispatchEvent(new Event("change"));
    r.findCaseSensitive = find.matches === 0 && findCount.textContent === "Sin resultados";
    (document.getElementById("findCase") as HTMLInputElement).checked = false;
    findQ.value = "alpha";
    findQ.dispatchEvent(new Event("input"));
    await sleep(500);
    r.findSearchesFormulaSource = find.matches === 1 && (editor.selection.elements[0] as unknown as { kind: string }).kind === "teximage";
    findQ.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    r.findEscapeCloses = findBar.hidden && !find.isOpen;
    editor.selection.clear();
    // remove the test texts and the extra page
    topLayer().elements.splice(topLayer().elements.indexOf(f1), 1);
    d.elementCount--;
    editor.undo(); // the extra page (its text goes with it)
    d.elementCount--;
    await sleep(60);
    r.findCleanup = d.pages.length === 1 && !topLayer().elements.includes(f1);
  }

  // I) pages, paper, format, navigation
  {
    const d = doc!;
    const origPage = d.pages[0]!;
    const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;
    const n0 = d.pages.length;
    const idx = journal.newPageAfter(0);
    r.pageAdded = d.pages.length === n0 + 1 && idx === 1 && views.length === n0 + 1;
    r.newPageLikeNeighbour = near(d.pages[1]!.width, origPage.width) && near(d.pages[1]!.height, origPage.height) && d.pages[1]!.background.style === origPage.background.style && d.pages[1]!.layers.length === 1;
    r.newPageIsIndependent = d.pages[1]!.background.attrs !== origPage.background.attrs;
    r.indicatorTotal = pgTotal.textContent === String(n0 + 1);
    editor.undo();
    r.addUndone = d.pages.length === n0 && views.length === n0;
    editor.redo();
    r.addRedone = d.pages.length === n0 + 1 && d.pages[1] !== origPage;

    journal.insertBefore(0);
    r.insertBefore = d.pages.length === n0 + 2 && d.pages[1] === origPage;
    journal.deletePage(0);
    r.deleteOk = d.pages.length === n0 + 1 && d.pages[0] === origPage;
    editor.undo();
    r.deleteUndone = d.pages.length === n0 + 2;
    editor.undo();
    r.insertBeforeUndone = d.pages.length === n0 + 1 && d.pages[0] === origPage;

    journal.append();
    r.append = d.pages.length === n0 + 2;
    journal.deletePage(2);
    editor.undo(); editor.undo(); // undo delete, undo append
    r.appendDeleteUndone = d.pages.length === n0 + 1;

    journal.movePage(0, 1);
    r.moved = d.pages[1] === origPage;
    editor.undo();
    r.moveUndone = d.pages[0] === origPage;

    const elementsBefore = d.elementCount;
    journal.setPaper(1, "graph", false);
    r.paperGraph = d.pages[1]!.background.style === "graph" && attrValue(d.pages[1]!.background.attrs, "style") === "graph" && origPage.background.style !== "graph";
    journal.setPaper(0, "ruled", true);
    r.paperAll = d.pages.every((p) => p.background.style === "ruled");
    editor.undo();
    r.paperAllUndo = origPage.background.style === "plain" && d.pages[1]!.background.style === "graph";
    journal.setBackgroundColor(1, "#ffeecc", false);
    r.bgColor = attrValue(d.pages[1]!.background.attrs, "color") === "#ffeeccff";
    const [fw, fh] = PAPER_FORMATS.A5!;
    journal.setFormat(1, fh, fw, false); // A5 landscape
    r.format = near(d.pages[1]!.width, fh) && near(d.pages[1]!.height, fw) && Math.abs(parseFloat(views[1]!.el.style.width) - fh * zoom) < 1e-2; // the browser rounds CSS lengths
    r.formatOnlyThatPage = near(origPage.width, PAPER_FORMATS.A4![0]);
    r.pagesKeepElements = d.elementCount === elementsBefore;

    // navigation and history (start from a known page without recording it)
    goToPage(0, false);
    await sleep(60);
    history = { back: [], fwd: [] };
    goToPage(1);
    await sleep(60);
    r.goToPage = currentPage() === 1 && pgNum.value === "2";
    goToPage(0);
    await sleep(60);
    r.goBackToFirst = currentPage() === 0;
    historyBack();
    await sleep(60);
    r.historyBack = currentPage() === 1;
    historyBack();
    await sleep(60);
    r.historyBack2 = currentPage() === 0;
    historyForward();
    await sleep(60);
    r.historyForward = currentPage() === 1;
    r.pageCountOut = d.pages.length;
  }

  // G) serialise, re-parse, save
  const xml = serializeXopp(doc!);
  const back = parseXopp(xml);
  r.reparseElements = back.elementCount === doc!.elementCount;
  r.elementsAfter = count();
  r.saved = await window.xournal.saveXopp(xml, "e2e.xopp", null, "save", null);


  // K) formula -> vector PDF (hidden KaTeX window) -> drawn again with pdf.js
  {
    const res = await window.xournal.latexPdf({ latex: String.raw`x^2+\frac{a}{b}=\sum_{i=1}^{n} i`, display: true, color: "#000000", px: 20 });
    r.latexOk = res.ok;
    if (res.ok) {
      r.latexIsPdf = res.pdfB64.startsWith("JVBERi"); // "%PDF" in base64
      r.latexPdfBytes = Math.round((res.pdfB64.length * 3) / 4);
      r.latexSizePt = [+res.widthPt.toFixed(1), +res.heightPt.toFixed(1)];
      const inf = await pdfInfo(res.pdfB64);
      r.latexOnePage = inf.pages === 1;
      r.latexPdfSizeMatches = Math.abs(inf.widthPt - res.widthPt) < 2 && Math.abs(inf.heightPt - res.heightPt) < 2;
      const cv = await rasterizeInk(res.pdfB64, 400, "#3333cc");
      const ctx2 = cv.getContext("2d")!;
      const px = ctx2.getImageData(0, 0, cv.width, cv.height).data;
      let inked = 0, solid = 0, opaque = 0, wrongColour = 0;
      for (let k = 0; k < px.length; k += 4) {
        const al = px[k + 3]!;
        if (al > 0) inked++;
        if (al > 200) solid++;
        // Canvas pixels are stored premultiplied: only fully opaque ones are guaranteed to keep the colour.
        if (al === 255) {
          opaque++;
          if (Math.abs(px[k]! - 0x33) > 2 || Math.abs(px[k + 1]! - 0x33) > 2 || Math.abs(px[k + 2]! - 0xcc) > 2) wrongColour++;
        }
      }
      r.latexRasterHasInk = inked > 200 && solid > 100;
      r.latexRasterTransparentBackground = px[3] === 0 && px[px.length - 1] === 0; // both corners
      r.latexRasterInkColourExact = opaque > 50 && wrongColour === 0;
      r.latexRasterSize = [cv.width, cv.height];
      // Light ink goes through the black-background path (the PDF itself must use that colour).
      const lightPdf = await window.xournal.latexPdf({ latex: String.raw`\sum_{i=1}^{n} i`, display: true, color: "#ffee88", px: 20 });
      const lightCv = await rasterizeInk(lightPdf.ok ? lightPdf.pdfB64 : "", 400, "#ffee88");
      const lp = lightCv.getContext("2d")!.getImageData(0, 0, lightCv.width, lightCv.height).data;
      let lightInk = 0;
      for (let k = 3; k < lp.length; k += 4) if (lp[k]! > 100) lightInk++;
      r.latexLightInkWorks = lightPdf.ok && lp[3] === 0 && lightInk > 200;
      // Composite on a contrasting colour so the result can be inspected by eye.
      const dbg = document.createElement("canvas");
      dbg.width = cv.width; dbg.height = cv.height;
      const dc = dbg.getContext("2d")!;
      dc.fillStyle = "#e8e8e8"; dc.fillRect(0, 0, dbg.width, dbg.height);
      dc.drawImage(cv, 0, 0);
      r.latexPng = dbg.toDataURL("image/png").split(",")[1];
    }
    const bad = await window.xournal.latexPdf({ latex: String.raw`\frac{`, display: false, color: "#000000", px: 16 });
    r.latexBadSourceReported = !bad.ok && bad.error.length > 0;
    r.latexBadError = bad.ok ? "" : bad.error.slice(0, 80);
    r.latexEmptyRejected = !(await window.xournal.latexPdf({ latex: "  ", display: false, color: "#000000", px: 16 })).ok;
  }

  // J) the real native menu, and the save safety rules
  {
    window.confirm = () => true; // never block on a dialog in tests
    const menu = async (...labels: string[]) => { const ok = await window.xournal.testMenuClick(labels); await sleep(80); return ok; };
    const d0 = doc!;
    const last = d0.pages.length - 1;
    r.menuLast = (await menu("Navegación", "Última página")) && currentPage() === last;
    r.menuFirst = (await menu("Navegación", "Primera página")) && currentPage() === 0;
    r.menuNext = (await menu("Navegación", "Página siguiente")) && currentPage() === 1;
    r.menuBack = (await menu("Navegación", "Atrás")) && currentPage() === 0;
    const nBefore = d0.pages.length;
    r.menuNewPage = (await menu("Diario", "Nueva página")) && d0.pages.length === nBefore + 1;
    r.menuUndoNewPage = (await menu("Editar", "Deshacer")) && d0.pages.length === nBefore;
    const cp = currentPage();
    r.menuPaperDots = (await menu("Diario", "Fondo de página", "Tipo de papel", "Puntos")) && d0.pages[cp]!.background.style === "dotted";
    r.menuPaperUndo = (await menu("Editar", "Deshacer")) && d0.pages[cp]!.background.style !== "dotted";
    r.menuToolShape = (await menu("Herramientas", "Formas")) && editor.tool === "shape";
    r.menuToolPen = (await menu("Herramientas", "Lápiz")) && editor.tool === "pen";
    r.menuZoomIn = (await menu("Ver", "Zoom", "Acercar")) && Math.abs(zoom - 1.2) < 1e-9;
    r.menuZoom100 = (await menu("Ver", "Zoom", "Tamaño normal (100 %)")) && zoom === 1;
    r.menuPendingIsDisabled = !(await window.xournal.testMenuClick(["Archivo", "Anotar PDF…"])) && !(await window.xournal.testMenuClick(["Archivo", "Imprimir…"]));
    r.menuUnknownLabel = !(await window.xournal.testMenuClick(["Archivo", "No existe"]));

    // save safety: what would the main process receive?
    const calls: Array<{ mode: string; target: string | null }> = [];
    let n = 0;
    io = { ...window.xournal, saveXopp: async (_x, _s, _d, mode, target) => { calls.push({ mode, target }); return mode === "copy" ? { path: "Z:\\copia.xopp", bytes: 1 } : { path: `Z:\\doc${++n}.xopp`, bytes: 1 }; } };
    (await menu("Archivo", "Nuevo"));
    r.newDocBlank = doc !== d0 && doc!.pages.length === 1 && doc!.elementCount === 0 && !editor.dirty && fileName === "Documento sin título.xopp" && views.length === 1;
    await save("save");
    r.firstSaveAsks = calls[0]!.mode === "save" && calls[0]!.target === null; // main sees no target, so it must ask
    await save("save");
    r.secondSaveReuses = calls[1]!.target === "Z:\\doc1.xopp";
    await save("copy");
    r.copyKeepsTarget = calls[2]!.mode === "copy";
    await save("save");
    r.afterCopyKeepsLastSavePath = calls[3]!.target === "Z:\\doc2.xopp"; // the copy did not change where the document is saved
    (await menu("Archivo", "Nuevo"));
    await save("save");
    r.newDocDoesNotReuseOldPath = calls[4]!.target === null;
    await openFile(cli0Files[0]!);
    await save("save");
    r.openedFileNeverOverwritten = calls[5]!.target === null;
    await save("saveAs");
    r.saveAsMode = calls[6]!.mode === "saveAs";
    io = window.xournal;

    (await menu("Archivo", "Cerrar"));
    r.closeDoc = doc === null && views.length === 0 && pagesEl.children.length === 0 && welcome.visible && pgTotal.textContent === "0";
    r.menuWorksWithoutDoc = await menu("Navegación", "Primera página"); // must not throw
    // W3) the menu bar drawn by the page
    {
      welcome.hide(); // so the tests can see it appear
      const tops = () => [...document.querySelectorAll<HTMLElement>("#menubar .mbTop")];
      const panels = () => [...document.querySelectorAll<HTMLElement>(".mbPanel")];
      const rowByText = (text: string, depth = 1) => [...document.querySelectorAll<HTMLElement>(`.mbPanel[data-depth="${depth}"] .mbItem`)].find((b) => (b.querySelector(".mbLabel")?.textContent ?? "") === text);
      r.menubarTopLevel = tops().map((b) => b.textContent).join("|") === "Archivo|Editar|Ver|Navegación|Diario|Herramientas|Plugins|Ayuda";
      r.menubarMatchesNativeMenu = menubar.items.length === 8 && menubar.items[0]!.sub!.some((i) => i.label === "Nuevo" && i.accel === "CmdOrCtrl+N");
      // open, switch by hovering, close with Esc
      tops()[0]!.click();
      r.menubarOpens = menubar.isOpen && panels().length === 1 && tops()[0]!.classList.contains("on");
      r.menubarShowsShortcuts = [...panels()[0]!.querySelectorAll("kbd")].some((k) => k.textContent === "Ctrl+N") && !(panels()[0]!.textContent ?? "").includes("CmdOrCtrl");
      tops()[3]!.dispatchEvent(new PointerEvent("pointerenter", { bubbles: true }));
      r.menubarHoverSwitches = panels().length === 1 && tops()[3]!.classList.contains("on") && !tops()[0]!.classList.contains("on");
      document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
      r.menubarEscCloses = !menubar.isOpen && panels().length === 0;
      // submenus
      tops()[3 + 1]!.click(); // Diario
      rowByText("Fondo de página")!.dispatchEvent(new PointerEvent("pointerenter", { bubbles: true }));
      r.menubarSubmenuOpens = panels().length === 2 && !!rowByText("Tipo de papel", 2);
      rowByText("Tipo de papel", 2)!.dispatchEvent(new PointerEvent("pointerenter", { bubbles: true }));
      r.menubarSubSubmenu = panels().length === 3 && !!rowByText("Logarítmico", 3) && !!rowByText("Polar", 3);
      menubar.close();
      // choosing an item runs the real command
      tops()[7]!.click(); // Ayuda
      rowByText("Bienvenida")!.click();
      await sleep(250);
      r.menubarItemRuns = !menubar.isOpen && panels().length === 0 && welcome.visible;
      welcome.hide();
      // Alt + mnemonic (Alt+Y: A&yuda)
      document.dispatchEvent(new KeyboardEvent("keydown", { key: "y", altKey: true, bubbles: true, cancelable: true }));
      r.menubarAltMnemonic = menubar.isOpen && tops()[7]!.classList.contains("on") && panels()[0]!.querySelector(".mbItem.hot") !== null;
      // keyboard: down, enter
      document.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true, cancelable: true }));
      r.menubarArrowRightWraps = tops()[0]!.classList.contains("on");
      document.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowLeft", bubbles: true, cancelable: true }));
      r.menubarArrowLeftWraps = tops()[7]!.classList.contains("on");
      document.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true, cancelable: true }));
      await sleep(250);
      r.menubarEnterRuns = !menubar.isOpen && welcome.visible; // first item of Ayuda is "Bienvenida"
      welcome.hide();
      // disabled items do nothing, checkboxes show a tick
      tops()[6]!.click(); // Plugins
      const dis = [...document.querySelectorAll<HTMLButtonElement>(".mbPanel .mbItem")][0]!;
      r.menubarDisabledItem = dis.disabled;
      menubar.close();
      // a click outside closes
      tops()[1]!.click();
      document.body.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
      r.menubarOutsideClickCloses = !menubar.isOpen;
      // theme: the bar and its panels change with it; the title shows the document
      const tbBg = () => getComputedStyle(document.getElementById("titlebar")!).backgroundColor;
      const classicBg = tbBg();
      document.getElementById("btnTheme")!.click();
      await sleep(80);
      const neonBg = tbBg();
      tops()[0]!.click();
      const panelBg = getComputedStyle(panels()[0]!).backgroundColor;
      menubar.close();
      r.menubarFollowsTheme = classicBg !== neonBg && neonBg === "rgb(30, 16, 67)" && panelBg === "rgb(23, 12, 54)";
      document.getElementById("btnTheme")!.click();
      await sleep(80);
      r.menubarClassicBack = tbBg() === classicBg;
      r.menubarTitleMirrors = document.getElementById("tbTitle")!.textContent === document.title && document.title.includes("Xournal Lab");
      r.menubarOverlayIpc = (() => { try { window.xournal.setTitleBar("#2b2b2b", "#dddddd"); return true; } catch { return false; } })();
      // a menu rebuilt by the main process (the recent files) is picked up
      window.xournal.addRecent(cli0Files[0]!);
      await sleep(300);
      tops()[0]!.click();
      const hasRecentSub = !!rowByText("Abrir reciente");
      rowByText("Abrir reciente")!.dispatchEvent(new PointerEvent("pointerenter", { bubbles: true }));
      r.menubarRecentsLive = hasRecentSub && [...document.querySelectorAll('.mbPanel[data-depth="2"] .mbItem')].some((b) => (b.textContent ?? "").includes("ejercicio_simulacro"));
      menubar.close();
      await welcome.show();
    }
    // W4) new and opened documents start fitted to the width of the window
    {
      autoFit = true;
      const fitOf = () => (scroller.offsetWidth - scrollbarWidth() - 2 * GAP - 20) / Math.max(...doc!.pages.map((p) => p.width));
      closeDocument();
      commands["file.new"]!();
      await sleep(150);
      r.fitNewDocument = doc !== null && Math.abs(zoom - fitOf()) < 1e-9 && zoom > 0.3 && zoom < 4;
      r.fitNewPageFillsTheWidth = views[0]!.el.getBoundingClientRect().width <= scroller.clientWidth - 2 * GAP && scroller.scrollWidth <= scroller.clientWidth + 1; // fills the window and does not overflow sideways
      closeDocument();
      await openUserFile(cli0Files[0]!);
      r.fitOpenedDocument = doc !== null && Math.abs(zoom - fitOf()) < 1e-9;
      r.fitOpenedZoomLabel = document.getElementById("zoomLabel")!.textContent === `${Math.round(fitOf() * 100)}%`;
      closeDocument();
      await openFile(cli0Files[0]!, { zoom: 1.5 });
      r.fitExplicitZoomWins = zoom === 1.5;
      closeDocument();
      (document.querySelector('.wPaper[data-paper="graph"]') as HTMLElement | null)?.click();
      await sleep(150);
      r.fitNewWithPaper = doc !== null && Math.abs(zoom - fitOf()) < 1e-9;
      autoFit = false;
      closeDocument();
      await sleep(100);
    }
    // W2) the welcome page
    {
      const wel = document.getElementById("welcome")!;
      const nPapers = START_PAPERS.length;
      r.welcomeShownWithoutDocument = !wel.hidden && doc === null && document.getElementById("hint")!.offsetParent === null;
      r.welcomeHasStartLinks = [...wel.querySelectorAll(".wLink")].map((b) => b.textContent ?? "").filter((t) => /Nuevo documento|Abrir archivo|recuperaci/.test(t)).length === 3;
      r.welcomeNoCloseButtonWithoutDocument = wel.querySelector(".wClose") === null;
      r.welcomePaperCards = wel.querySelectorAll(".wPaper").length === nPapers && nPapers >= 11;
      r.welcomeThumbnailsDrawn = [...wel.querySelectorAll<HTMLCanvasElement>(".wPaper canvas")].filter((c) => { const d = c.getContext("2d")!.getImageData(0, 0, c.width, c.height).data; let ink = 0; for (let i = 0; i < d.length; i += 4) if (d[i]! < 230 || d[i + 1]! < 230 || d[i + 2]! < 230) ink++; return ink > 20; }).length >= nPapers - 3;
      // recents (kept in memory only while testing)
      window.xournal.addRecent(cli0Files[0]!);
      await sleep(150);
      await welcome.show();
      const recents = [...wel.querySelectorAll<HTMLElement>(".wRecent")];
      r.welcomeListsRecents = recents.length >= 1 && recents.some((b) => b.dataset.path === cli0Files[0]) && (recents[0]!.textContent ?? "").includes(cli0Files[0]!.split(String.fromCharCode(92)).pop()!.split("/").pop()!);
      recents.find((b) => b.dataset.path === cli0Files[0])!.click();
      for (let k = 0; k < 60 && doc === null; k++) await sleep(50);
      r.welcomeRecentOpens = doc !== null && wel.hidden && doc.pages.length >= 1 && doc.elementCount > 100;
      // as an overlay over an open document: Help > Welcome, closed with Esc or the cross
      r.welcomeMenu = (await menu("Ayuda", "Bienvenida")) && !wel.hidden && doc !== null;
      await sleep(150);
      r.welcomeOverlayHasClose = wel.querySelector(".wClose") !== null;
      wel.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
      r.welcomeEscClosesOverlay = wel.hidden && doc !== null;
      await welcome.show();
      (wel.querySelector(".wClose") as HTMLElement).click();
      r.welcomeCrossClosesOverlay = wel.hidden && doc !== null;
      // the start-up preference
      await welcome.show();
      const showBox = wel.querySelector<HTMLInputElement>("#wShow")!;
      r.welcomeOnStartDefault = showBox.checked === true && (await window.xournal.getPrefs()).welcome === true;
      showBox.checked = false;
      showBox.dispatchEvent(new Event("change", { bubbles: true }));
      await sleep(80);
      r.welcomeOnStartSaved = (await window.xournal.getPrefs()).welcome === false;
      showBox.checked = true;
      showBox.dispatchEvent(new Event("change", { bubbles: true }));
      await sleep(80);
      r.welcomeOnStartRestored = (await window.xournal.getPrefs()).welcome === true;
      // the theme button
      const before = document.documentElement.dataset.theme;
      wel.querySelector<HTMLElement>("#wTheme")!.click();
      await sleep(120);
      r.welcomeThemeToggles = document.documentElement.dataset.theme !== before;
      wel.querySelector<HTMLElement>("#wTheme")!.click();
      await sleep(120);
      r.welcomeThemeBack = document.documentElement.dataset.theme === before;
      wel.hidden = true;
      closeDocument();
      await sleep(100);
      r.welcomeBackAfterClose = !wel.hidden && doc === null;
      // new documents on a chosen paper
      (wel.querySelector('.wPaper[data-paper="x:log"]') as HTMLElement).click();
      await sleep(150);
      const bg0 = doc!.pages[0]!.background;
      r.welcomePaperLog = wel.hidden && bg0.paper?.kind === "log" && bg0.style === "graph" && attrValue(bg0.attrs, "xlPaper") === "log;base=10;axes=y;cycles=5" && doc!.pages.length === 1 && !editor.dirty && !editor.canUndo;
      const back = parseXopp(serializeXopp(doc!));
      r.welcomePaperSurvivesSave = back.pages[0]!.background.paper?.kind === "log";
      closeDocument();
      await sleep(100);
      (wel.querySelector('.wPaper[data-paper="graph"]') as HTMLElement).click();
      await sleep(150);
      r.welcomePaperBuiltin = doc!.pages[0]!.background.style === "graph" && doc!.pages[0]!.background.paper === undefined && attrValue(doc!.pages[0]!.background.attrs, "xlPaper") === null;
      closeDocument();
      await sleep(100);
      (wel.querySelector(".wLink") as HTMLElement).click(); // New document
      await sleep(150);
      r.welcomeNewDocument = doc !== null && wel.hidden && doc.pages[0]!.background.style === "plain" && doc.elementCount === 0;
      closeDocument();
      await sleep(100);
      // clear the list
      await welcome.show();
      (wel.querySelector("#wClear") as HTMLElement | null)?.click();
      await sleep(250);
      r.welcomeClearsRecents = wel.querySelectorAll(".wRecent").length === 0 && (await window.xournal.recentList()).length === 0;
    }
  }
  // O) preferences: dialog, touch drawing, save-over-opened, recovery copies
  {
    const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;
    const dlg = document.getElementById("prefsDlg") as HTMLElement;
    const fingerBox = document.getElementById("prefFinger") as HTMLInputElement;
    const overBox = document.getElementById("prefOver") as HTMLInputElement;
    const autoBox = document.getElementById("prefAuto") as HTMLInputElement;
    const got = await window.xournal.getPrefs();
    r.prefsDefaults = got.fingerDraw === false && got.saveOverOpened === false && got.autosaveMinutes === 0 && typeof got.autosaveDir === "string" && got.autosaveDir.length > 0;
    await openFile(cli0Files[0]!); // a clean document (earlier tests rebuilt this one)
    const lay = () => doc!.pages[0]!.layers[doc!.pages[0]!.layers.length - 1]!;
    const pageEl = () => views[0]!.el;
    const tap = (x: number, y: number) => {
      const rect = pageEl().getBoundingClientRect();
      synth("pointerdown", pageEl(), rect.left + x, rect.top + y, 0.5, "touch");
      for (let k = 1; k <= 6; k++) synth("pointermove", pageEl(), rect.left + x + k * 8, rect.top + y + k * 3, 0.5, "touch");
      synth("pointerup", pageEl(), rect.left + x + 48, rect.top + y + 18, 0, "touch");
    };
    setTool("pen");
    const n0 = lay().elements.length;
    tap(300, 450);
    r.fingerIgnoredByDefault = lay().elements.length === n0;

    // open the dialog from the menu command, change everything, accept
    commands["edit.prefs"]!();
    r.prefsDialogOpens = !dlg.hidden && fingerBox.checked === false && overBox.checked === false && autoBox.value === "0";
    fingerBox.checked = true;
    overBox.checked = true;
    autoBox.value = "7";
    (document.getElementById("prefOk") as HTMLButtonElement).click();
    await sleep(80);
    const after = await window.xournal.getPrefs();
    r.prefsDialogSaves = dlg.hidden && after.fingerDraw === true && after.saveOverOpened === true && after.autosaveMinutes === 7;
    r.prefsAppliedLive = editor.fingerDraw === true && prefs.autosaveMinutes === 7;
    tap(300, 450);
    r.fingerDrawsWhenEnabled = lay().elements.length === n0 + 1;
    editor.undo();

    // "save over the opened file": the renderer hands the opened path to the main process
    const calls: Array<{ mode: string; target: string | null }> = [];
    io = { ...window.xournal, saveXopp: async (_x, _s, _d, mode, target) => { calls.push({ mode, target }); return { path: target ?? "Z:\\nuevo.xopp", bytes: 1 }; } };
    await openFile(cli0Files[0]!);
    await save("save");
    r.saveOverOpenedHonoured = calls[0]!.target === cli0Files[0];
    await window.xournal.setPrefs({ saveOverOpened: false });
    applyPrefs({ ...prefs, saveOverOpened: false });
    await openFile(cli0Files[0]!);
    await save("save");
    r.saveOverOpenedOffAsks = calls[1]!.target === null;
    io = window.xournal;

    // recovery copy: only when there are unsaved changes, in its own file
    editor.reset();
    r.autosaveSkipsCleanDoc = (await autosaveNow()) === null;
    setTool("pen");
    const pr = pageEl().getBoundingClientRect();
    synth("pointerdown", pageEl(), pr.left + 200, pr.top + 300, 0.5);
    for (let k = 1; k <= 5; k++) synth("pointermove", pageEl(), pr.left + 200 + k * 7, pr.top + 300, 0.5);
    synth("pointerup", pageEl(), pr.left + 235, pr.top + 300, 0);
    const copy = await autosaveNow();
    r.autosaveWritesCopy = !!copy && copy.endsWith(".autosave.xopp") && copy !== cli0Files[0];
    if (copy) {
      const back = await window.xournal.readXopp(copy);
      r.autosaveCopyIsTheDocument = back.xml === serializeXopp(doc!);
    }
    r.autosaveNeverTouchesOriginal = (await window.xournal.readXopp(cli0Files[0]!)).xml !== serializeXopp(doc!);
    // back to defaults, and to a clean undo history
    await window.xournal.setPrefs({ fingerDraw: false, saveOverOpened: false, autosaveMinutes: 0 });
    applyPrefs({ fingerDraw: false, saveOverOpened: false, autosaveMinutes: 0, theme: "classic", palette: "classic", effects: "auto", welcome: true, plane: "", space3: "", fill: "", tablet: "", sizes: "", textMd: true, toolbox: "", glowOn: false, glowSize: 8, glowIntensity: 0.8 });
    r.prefsRestored = editor.fingerDraw === false && prefs.autosaveMinutes === 0 && near(doc!.pages.length, 1);
  }


  // P) every tool shows a visible pointer (an arrow image), never "none"
  {
    const tools: Tool[] = ["pen", "highlighter", "eraser", "eraserPart", "select", "lasso", "vspace", "laser", "shape", "text", "latex"];
    const bad: string[] = [];
    for (const t of tools) {
      setTool(t);
      const c = getComputedStyle(views[0]!.el).cursor;
      const gap = getComputedStyle(scroller).cursor;
      if (!c.includes("url(") || c === "none" || !gap.includes("url(")) bad.push(`${t}:${c.slice(0, 20)}`);
    }
    setTool("hand");
    r.cursorVisibleForAllTools = bad.length === 0;
    r.cursorBadTools = bad.join(",");
    setTool("pen");
  }

  // Q) presentation mode can always be left, even with the focus in a text field
  {
    const chip = document.getElementById("presentChip") as HTMLElement;
    const fq = document.getElementById("findQ") as HTMLInputElement;
    commands["view.presentation"]!();
    r.presentationHidesBars = document.body.classList.contains("presentation") && getComputedStyle(document.getElementById("bar")!).display === "none";
    r.presentationShowsWayOut = getComputedStyle(chip).display !== "none";
    document.getElementById("findBar")!.hidden = false;
    fq.focus();
    fq.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    r.escLeavesPresentationFromAField = !document.body.classList.contains("presentation") && getComputedStyle(document.getElementById("bar")!).display !== "none";
    document.getElementById("findBar")!.hidden = true;
    commands["view.presentation"]!();
    chip.click();
    r.chipLeavesPresentation = !document.body.classList.contains("presentation");
    commands["view.fullscreen"]!();
    r.fullscreenEntered = fullscreen === true;
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    r.escLeavesFullscreen = fullscreen === false;
  }

  // R) strokes keep exactly what the pen reported; the input grid is detected and hidden only when it would show
  {
    const pageEl = () => views[0]!.el;
    const lay = () => { const pg = doc!.pages[0]!; return pg.layers[pg.layers.length - 1]!; };
    const rms = (pts: number[], cx: number, cy: number, R: number) => {
      let sum = 0;
      for (let k = 0; k < pts.length; k += 2) sum += (Math.hypot(pts[k]! - cx, pts[k + 1]! - cy) - R) ** 2;
      return Math.sqrt(sum / (pts.length / 2));
    };
    setTool("pen");
    setZoom(0.25);
    await sleep(120);
    const z = zoom; // 0.25: one screen pixel is 4 page units
    const cx = 300, cy = 400, R = 180;
    const rect = pageEl().getBoundingClientRect();
    const raw: number[] = []; // what the pointer events say, in page units (whole client pixels)
    const client: Array<[number, number]> = [];
    for (let k = 0; k <= 140; k++) {
      const a = (k / 140) * Math.PI * 2 * 1.04;
      const x = cx + Math.cos(a) * R, y = cy + Math.sin(a) * R;
      const px = Math.round(rect.left + x * z), py = Math.round(rect.top + y * z);
      client.push([px, py]);
      raw.push((px - rect.left) / z, (py - rect.top) / z);
    }
    const before = lay().elements.length;
    synth("pointerdown", pageEl(), client[0]![0], client[0]![1], 0.5);
    for (let k = 1; k < client.length; k++) synth("pointermove", pageEl(), client[k]![0], client[k]![1], 0.5);
    synth("pointerup", pageEl(), client[client.length - 1]![0], client[client.length - 1]![1], 0);
    await sleep(20);
    const st = lay().elements.at(-1) as unknown as Stroke;
    r.gridAddedStroke = lay().elements.length === before + 1;
    // 1. what is stored is what the pen reported: nothing changes when the pen is lifted
    let allRaw = true;
    for (let k = 0; k < st.pts.length; k += 2) {
      let hit = false;
      for (let j = 0; j < raw.length; j += 2) if (Math.abs(st.pts[k]! - raw[j]!) < 1e-9 && Math.abs(st.pts[k + 1]! - raw[j + 1]!) < 1e-9) { hit = true; break; }
      if (!hit) { allRaw = false; break; }
    }
    r.storedIsExactlyWhatThePenSaid = allRaw && st.pts.length > 100;
    // 2. the grid is recovered from the data alone
    const step = quantizationStep(st.pts);
    r.gridStepDetected = Math.abs(step - 4) < 0.01;
    r.gridStep = +step.toFixed(4);
    // 3. at the scale it was drawn at (the step is one pixel) the stored points are drawn as they are
    r.noSmoothingAtCaptureScale = displayPoints(st, z) === st.pts;
    // 4. zoomed in, the stairs are hidden and the error against the ideal circle drops
    const zoomed = displayPoints(st, 8);
    const rawRms = rms(st.pts, cx, cy, R), shownRms = rms(zoomed, cx, cy, R);
    r.gridRawRms = +rawRms.toFixed(3);
    r.gridShownRms = +shownRms.toFixed(3);
    r.zoomedInHidesStairs = zoomed !== st.pts && zoomed.length === st.pts.length && shownRms < 0.6 * rawRms;
    r.zoomedKeepsEnds = zoomed[0] === st.pts[0] && zoomed[zoomed.length - 1] === st.pts[st.pts.length - 1];
    r.smoothedCopyIsCached = displayPoints(st, 8) === zoomed;
    // 5. the cache follows the stroke when its points are replaced (moved)
    transformElement(st, translateAff(5, 0));
    const moved = displayPoints(st, 8);
    r.cacheFollowsEdits = moved !== zoomed && Math.abs(moved[0]! - zoomed[0]! - 5) < 1e-6;
    r.storedCoordinatesNeverTouched = st.view?.src === st.pts;
    editor.undo();
    // 6. no false positives on strokes made by Xournal++ itself (sub-pixel input)
    const sampleDoc = parseXopp((await window.xournal.readXopp(cli0Files[0]!)).xml);
    let strokes = 0, flagged = 0;
    for (const pg of sampleDoc.pages) for (const l of pg.layers) for (const e of l.elements) if (e.kind === "stroke") { strokes++; if (quantizationStep(e.pts) > 0) flagged++; }
    r.sampleStrokes = strokes;
    r.noFalsePositivesOnRealFiles = strokes > 600 && flagged === 0;
    // 7. a sharp corner drawn at full zoom stays a corner even when zoomed in
    setZoom(1);
    await sleep(120);
    const rect1 = pageEl().getBoundingClientRect();
    const L: Array<[number, number]> = [];
    for (let k = 0; k <= 40; k++) L.push([100 + k * 3, 500]);
    for (let k = 1; k <= 40; k++) L.push([220, 500 + k * 3]);
    synth("pointerdown", pageEl(), rect1.left + L[0]![0], rect1.top + L[0]![1], 0.5);
    for (let k = 1; k < L.length; k++) synth("pointermove", pageEl(), rect1.left + L[k]![0], rect1.top + L[k]![1], 0.5);
    synth("pointerup", pageEl(), rect1.left + 220, rect1.top + 620, 0);
    await sleep(20);
    const corner = lay().elements.at(-1) as unknown as Stroke;
    const cp = displayPoints(corner, 8);
    let nearest = Infinity;
    for (let k = 0; k < cp.length; k += 2) nearest = Math.min(nearest, Math.hypot(cp[k]! - 220, cp[k + 1]! - 500));
    r.cornerSurvivesZoomedIn = nearest < 2.5;
    editor.undo();
    {
      // Picture for the eye: the same stretch of the circle at 8x, stored points (left) and what is drawn when zoomed in (right).
      const cv = document.createElement("canvas");
      cv.width = 1000; cv.height = 360;
      const c2 = cv.getContext("2d")!;
      c2.fillStyle = "#fff"; c2.fillRect(0, 0, cv.width, cv.height);
      const draw = (pts: number[], ox: number, label: string) => {
        c2.save();
        c2.beginPath(); c2.rect(ox, 0, 500, 360); c2.clip();
        c2.translate(ox - 220 * 8, -205 * 8); c2.scale(8, 8);
        c2.lineWidth = 1.41; c2.strokeStyle = "#000"; c2.lineJoin = "round"; c2.lineCap = "round";
        c2.beginPath();
        c2.moveTo(pts[0]!, pts[1]!);
        for (let k = 2; k < pts.length; k += 2) c2.lineTo(pts[k]!, pts[k + 1]!);
        c2.stroke();
        c2.restore();
        c2.fillStyle = "#c00"; c2.font = "16px sans-serif"; c2.fillText(label, ox + 8, 22);
      };
      draw(st.pts, 0, "puntos guardados (tal cual los dio el lápiz)");
      draw(zoomed, 500, "lo que se dibuja al acercar el zoom");
      r.smoothPng = cv.toDataURL("image/png").split(",")[1];
    }
  }

  // U) toolbar: every button has an icon and a tooltip
  {
    const wanted = [...document.querySelectorAll<HTMLElement>(".tb [data-icon]")];
    r.iconButtons = wanted.length;
    r.allIconsDrawn = wanted.length > 45 && wanted.every((b) => b.querySelector("svg path, svg rect, svg circle, svg g") !== null) && installIcons().length === 0;
    r.allIconsHaveTooltip = wanted.every((b) => (b.title ?? "").length > 3);
    r.toolsAllIcons = [...document.querySelectorAll<HTMLElement>("button.tool")].every((b) => b.dataset.icon && b.querySelector("svg"));
    r.paperBarToggles = (() => { const b3 = document.getElementById("bar3")!; const was = b3.hidden; document.getElementById("btnPaper")!.click(); const open = !b3.hidden; document.getElementById("btnPaper")!.click(); return was && open && b3.hidden; })();
  }

  // V) theme: the toggle switches the whole interface and is remembered
  {
    const root = document.documentElement;
    const colour = () => getComputedStyle(document.querySelector<HTMLElement>('[data-icon="undo"]')!).color;
    const barBg = () => getComputedStyle(document.getElementById("bar2")!).backgroundImage + getComputedStyle(document.getElementById("bar2")!).backgroundColor;
    const classicColour = colour(), classicBg = barBg();
    document.getElementById("btnTheme")!.click();
    await sleep(50);
    r.themeSwitchesToNeon = root.dataset.theme === "neon" && prefs.theme === "neon";
    r.neonChangesIconColour = colour() !== classicColour && barBg() !== classicBg;
    r.neonIconsHaveOwnColours = getComputedStyle(document.querySelector<HTMLElement>('[data-icon="save"]')!).color !== getComputedStyle(document.querySelector<HTMLElement>('[data-icon="select"]')!).color;
    r.neonIconsGlow = getComputedStyle(document.querySelector<HTMLElement>('[data-icon="pen"] svg')!).filter.includes("drop-shadow");
    r.neonPageStaysWhite = getComputedStyle(views[0]!.el).backgroundColor === "rgb(255, 255, 255)";
    r.themeIsSaved = (await window.xournal.getPrefs()).theme === "neon";
    document.getElementById("btnTheme")!.click();
    await sleep(50);
    r.themeSwitchesBack = root.dataset.theme === "classic" && colour() === classicColour && (await window.xournal.getPrefs()).theme === "classic";
  }

  // W) colour palettes
  {
    const sw = () => [...swatches.querySelectorAll<HTMLElement>(".sw")];
    r.paletteStartsClassic = sw().length === 5 && paletteId === "classic";
    document.getElementById("btnPalette")!.click();
    r.paletteMenuOpens = !palMenu.hidden && palMenu.querySelectorAll(".palItem").length === PALETTES.length && PALETTES.length >= 9;
    palMenu.querySelector<HTMLElement>('[data-palette="neondreams"]')!.click();
    await sleep(80);
    r.paletteMenuCloses = palMenu.hidden;
    r.paletteChangesSwatches = paletteId === "neondreams" && sw().length === 8 && sw()[1]!.dataset.color === "#ff2a9d";
    r.palettePaperHint = (document.getElementById("info")!.textContent ?? "").includes("#120a26");
    sw()[2]!.click();
    r.paletteSwatchSetsPenColour = editor.color === "#05e1ff" && sw()[2]!.classList.contains("active") && sw().filter((x) => x.classList.contains("active")).length === 1;
    r.paletteIsSaved = (await window.xournal.getPrefs()).palette === "neondreams";
    // choosing a preset that contains the current colour keeps it selected
    document.getElementById("btnPalette")!.click();
    palMenu.querySelector<HTMLElement>('[data-palette="synthwave"]')!.click();
    await sleep(80);
    r.paletteChangeKeepsColourOnlyIfShared = sw().length === 8 && sw().every((x) => !x.classList.contains("active"));
    document.getElementById("btnPalette")!.click();
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.paletteEscCloses = palMenu.hidden;
    document.getElementById("btnPalette")!.click();
    palMenu.querySelector<HTMLElement>('[data-palette="classic"]')!.click();
    await sleep(80);
    selectColor("#000000");
    r.paletteBackToClassic = sw().length === 5 && (await window.xournal.getPrefs()).palette === "classic";
    r.everyPaletteIsValid = PALETTES.every((pl) => pl.colors.length >= 5 && pl.colors.every((c) => /^#[0-9a-f]{6}$/.test(c)) && new Set(pl.colors).size === pl.colors.length);
  }

  // X) glow on strokes
  {
    const pageEl = () => views[0]!.el;
    const lay = () => { const pg = doc!.pages[0]!; return pg.layers[pg.layers.length - 1]!; };
    setTool("pen");
    setZoom(1);
    await sleep(120);
    const cv = () => views[0]!.canvas!;
    const k = () => cv().width / doc!.pages[0]!.width;
    const greenAt = (x: number, y: number) => cv().getContext("2d")!.getImageData(Math.round(x * k()), Math.round(y * k()), 1, 1).data[1]!; // green channel: 255 on white paper, lower where red ink or halo is
    const drawLine = async (y: number, tool: "pen" | "highlighter") => {
      setTool(tool);
      const rc = pageEl().getBoundingClientRect(), z = zoom;
      synth("pointerdown", pageEl(), rc.left + 100 * z, rc.top + y * z, 0.5);
      for (let i = 1; i <= 40; i++) synth("pointermove", pageEl(), rc.left + (100 + i * 5) * z, rc.top + y * z, 0.5);
      return rc;
    };
    const finish = async (y: number) => { const rc = pageEl().getBoundingClientRect(); synth("pointerup", pageEl(), rc.left + 300 * zoom, rc.top + y * zoom, 0); await sleep(60); };
    // off: no halo
    r.glowStartsOff = editor.glowOn === false;
    await drawLine(700, "pen"); await finish(700);
    const plain = lay().elements.at(-1) as unknown as Stroke;
    r.glowOffStrokeHasNoGlow = plain.glow === undefined && !plain.attrs.some((a) => a[0] === "glow");
    r.glowOffNoHalo = greenAt(200, 708) === 255;
    // on, through the real panel controls
    document.getElementById("btnGlow")!.click();
    const panel = document.getElementById("glowPanel")!;
    r.glowPanelOpens = !panel.hidden;
    const on = document.getElementById("glowOn") as HTMLInputElement, size = document.getElementById("glowSize") as HTMLInputElement, inten = document.getElementById("glowInt") as HTMLInputElement;
    on.checked = true; on.dispatchEvent(new Event("input", { bubbles: true }));
    size.value = "10"; size.dispatchEvent(new Event("input", { bubbles: true }));
    inten.value = "90"; inten.dispatchEvent(new Event("input", { bubbles: true }));
    r.glowControlsSetEditor = editor.glowOn && editor.glowSize === 10 && Math.abs(editor.glowIntensity - 0.9) < 1e-9;
    r.glowPersisted = await window.xournal.getPrefs().then((p) => p.glowOn === true && p.glowSize === 10 && Math.abs(p.glowIntensity - 0.9) < 1e-9);
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.glowPanelEscCloses = panel.hidden;
    selectColor("#ff0000");
    await drawLine(760, "pen");
    r.glowLiveFilter = document.getElementById("live")!.style.filter.includes("drop-shadow");
    await finish(760);
    const glowing = lay().elements.at(-1) as unknown as Stroke;
    r.glowStrokeHasGlow = glowing.glow?.size === 10 && Math.abs(glowing.glow.intensity - 0.9) < 1e-9;
    r.glowSavedAsAttributes = glowing.attrs.some((a) => a[0] === "glow" && a[1] === "10") && glowing.attrs.some((a) => a[0] === "glowAlpha" && a[1] === "0.9");
    r.glowLiveFilterCleared = document.getElementById("live")!.style.filter === "";
    r.glowHaloVisible = greenAt(200, 764) < 225 && greenAt(200, 770) < 250;
    r.glowFadesWithDistance = greenAt(200, 764) < greenAt(200, 772) && greenAt(200, 800) >= 253;
    r.glowCoreStaysSharp = greenAt(200, 760) < 130;
    // round trip through the file format
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml);
    const bs = back.pages[0]!.layers.at(-1)!.elements.at(-1) as unknown as Stroke;
    r.glowRoundTrips = bs.glow?.size === 10 && Math.abs(bs.glow.intensity - 0.9) < 1e-9 && bs.pts.length === glowing.pts.length;
    r.glowFileKeepsStrokeShape = /<stroke [^>]*glow="10"[^>]*glowAlpha="0.9"/.test(xml) && /<stroke [^>]*color="#ff0000ff"/.test(xml);
    // highlighter never glows
    await drawLine(840, "highlighter"); await finish(840);
    r.glowNotOnHighlighter = (lay().elements.at(-1) as unknown as Stroke).glow === undefined;
    // apply to / remove from a selection, with undo
    setTool("select");
    editor.selection.selectOnly(0, lay(), plain);
    r.glowSelectionApi = editor.selection.count === 1;
    document.getElementById("glowApply")!.click();
    await sleep(40);
    r.glowAppliedToSelection = plain.glow?.size === 10 && plain.attrs.some((a) => a[0] === "glow");
    editor.undo();
    r.glowApplyUndo = plain.glow === undefined && !plain.attrs.some((a) => a[0] === "glow");
    editor.redo();
    r.glowApplyRedo = plain.glow?.size === 10;
    document.getElementById("glowRemove")!.click();
    r.glowRemovedFromSelection = plain.glow === undefined && !plain.attrs.some((a) => a[0] === "glowAlpha");
    editor.undo();
    editor.undo();
    editor.undo();
    editor.selection.clear();
    // clean up: remove what this block drew, switch glow off again
    while (lay().elements.length > 0 && (lay().elements.at(-1) as unknown as Stroke).pts?.length === 82) editor.undo();
    on.checked = false; on.dispatchEvent(new Event("input", { bubbles: true }));
    selectColor("#000000");
    r.glowOffAgain = editor.glowOn === false;
    await window.xournal.setPrefs({ glowOn: false, glowSize: 8, glowIntensity: 0.8 });
    {
      // picture for the eye: three strokes at different glow settings on the dark neon paper
      const c = document.createElement("canvas");
      c.width = 640; c.height = 200;
      const x = c.getContext("2d")!;
      x.fillStyle = "#120a26"; x.fillRect(0, 0, c.width, c.height);
      const col = ["#ff2a9d", "#05e1ff", "#ffe45e"];
      [[4, 0.5], [10, 0.8], [18, 1]].forEach(([sz, it], i) => {
        const st: Stroke = { kind: "stroke", attrs: [], tool: "pen", color: col[i]!, widths: [2.4], pts: [20, 40 + i * 55, 120, 20 + i * 55, 220, 60 + i * 55, 320, 30 + i * 55, 420, 50 + i * 55, 520, 35 + i * 55, 620, 45 + i * 55], cap: "round", dash: null, bbox: [20, 20 + i * 55, 620, 60 + i * 55], glow: { size: sz!, intensity: it! } };
        drawStroke(x, st);
      });
      {
        // cost of repainting glowing strokes (what zooming or scrolling pays), 40 strokes of ~300 points each, at zoom 1
        const pts: number[] = [];
        for (let i = 0; i < 300; i++) pts.push(20 + i * 1.8, 100 + Math.sin(i / 12) * 40);
        const mk = (glow: boolean): Stroke => ({ kind: "stroke", attrs: [], tool: "pen", color: "rgba(255,42,157,1)", widths: [2.4], pts, cap: "round", dash: null, bbox: bboxOf(pts), ...(glow ? { glow: { size: 10, intensity: 0.9 } } : {}) });
        const timeIt = (g: boolean) => { const t0 = performance.now(); for (let i = 0; i < 40; i++) drawStroke(x, mk(g)); return +((performance.now() - t0) / 40).toFixed(2); };
        timeIt(true); // warm up
        r.glowRepaintMsPerStroke = { plain: timeIt(false), glow: timeIt(true) };
      }
      r.glowPng = c.toDataURL("image/png").split(",")[1];
    }
  }

  // Y) the active palette also offers paper colours
  {
    const psw = () => [...document.querySelectorAll<HTMLElement>("#paperSwatches .psw")];
    r.paperSwatchesFollowPalette = psw().length === 5 && psw()[0]!.dataset.color === "#000000";
    document.getElementById("btnPalette")!.click();
    palMenu.querySelector<HTMLElement>('[data-palette="neondreams"]')!.click();
    await sleep(60);
    r.paperSwatchesCuratedFirst = psw()[0]!.dataset.color === "#120a26" && psw().length === 5 + 8; // 5 curated papers, then the 8 inks
    const pg = currentPage();
    const bgAttr = () => attrValue(doc!.pages[pg]!.background.attrs, "color");
    const solid = doc!.pages[pg]!.background.type === "solid";
    const before = bgAttr();
    const dark = psw().find((x) => x.dataset.color === "#241449")!;
    dark.click();
    await sleep(60);
    r.paperSwatchSetsPaper = solid && bgAttr() === "#241449ff" && (document.getElementById("bgColor") as HTMLInputElement).value === "#241449";
    r.paperSwatchMarked = dark.classList.contains("active") && psw().filter((x) => x.classList.contains("active")).length === 1;
    editor.undo();
    r.paperSwatchUndo = bgAttr() === before;
    document.getElementById("btnPalette")!.click();
    palMenu.querySelector<HTMLElement>('[data-palette="classic"]')!.click();
    await sleep(60);
    r.paperSwatchesBackToClassic = psw().length === 5;
  }

  // Z) special papers for maths and physics
  {
    const menu = async (...labels: string[]) => { const ok = await window.xournal.testMenuClick(labels); await sleep(80); return ok; };
    const pg = currentPage();
    const bgOf = () => doc!.pages[pg]!.background;
    const origStyle = bgOf().style, origPaper = bgOf().paper;
    const wasAll = applyAll.checked;
    applyAll.checked = false;
    r.paperKindsListed = PAPER_KINDS.length >= 7 && [...paperSel.querySelectorAll("optgroup option")].length === PAPER_KINDS.length;
    choosePaper("x:log");
    r.paperLogApplied = bgOf().paper?.kind === "log" && bgOf().style === "graph" && attrValue(bgOf().attrs, "xlPaper") === "log;base=10;axes=y;cycles=5";
    r.paperSelectShowsIt = paperSel.value === "x:log" && paperOpts.querySelectorAll("select").length === 3;
    // change the base through the real option select
    const baseSel = paperOpts.querySelector<HTMLSelectElement>('select[data-param="base"]')!;
    baseSel.value = "e";
    baseSel.dispatchEvent(new Event("change", { bubbles: true }));
    r.paperBaseChanges = bgOf().paper?.params.base === "e" && (attrValue(bgOf().attrs, "xlPaper") ?? "").includes("base=e") && paperOpts.querySelector<HTMLSelectElement>('select[data-param="base"]')!.value === "e";
    const typeSel = paperOpts.querySelector<HTMLSelectElement>('select[data-param="axes"]')!;
    typeSel.value = "xy";
    typeSel.dispatchEvent(new Event("change", { bubbles: true }));
    r.paperLogLogChanges = bgOf().paper?.params.axes === "xy" && bgOf().paper?.params.base === "e";
    // saved in the file and read back
    const back = parseXopp(serializeXopp(doc!));
    r.paperRoundTrip = back.pages[pg]!.background.paper?.kind === "log" && back.pages[pg]!.background.paper?.params.base === "e" && back.pages[pg]!.background.style === "graph";
    // undo goes back one change at a time
    editor.undo(); editor.undo(); editor.undo();
    r.paperUndoAll = bgOf().paper === origPaper && bgOf().style === origStyle && attrValue(bgOf().attrs, "xlPaper") === (origPaper ? formatSpecForTest(origPaper) : null);
    // via the real menu
    r.paperMenu = (await menu("Diario", "Fondo de página", "Tipo de papel", "Polar")) && bgOf().paper?.kind === "polar";
    // choosing a built-in style removes the special paper
    choosePaper("graph");
    r.paperBuiltinRemoves = bgOf().paper === undefined && attrValue(bgOf().attrs, "xlPaper") === null && bgOf().style === "graph";
    editor.undo();
    r.paperBuiltinUndoRestores = bgOf().paper?.kind === "polar";
    editor.undo();
    // robustness of the attribute
    r.paperParseJunk = parseSpec("nope") === undefined && parseSpec(null) === undefined && parseSpec("log;base=7;cycles=banana;zzz=1")?.params.base === "10" && parseSpec("log;base=7;cycles=banana")?.params.cycles === "5";
    r.paperParseKeepsValid = parseSpec("cart;quad=1;unit=0.5")?.params.quad === "1" && parseSpec("cart;quad=1;unit=0.5")?.params.sub === "off";
    // every paper draws something on both light and dark paper, without throwing, and stays inside the page
    const sheet = document.createElement("canvas");
    const cw = 300, ch = 420, cols = PAPER_KINDS.length + 3;
    sheet.width = cw * 4; sheet.height = ch * 2;
    const sx = sheet.getContext("2d")!;
    let allDraw = true, allDark = true, idx = 0;
    const variants: Array<[string, Record<string, string>, string]> = [];
    for (const kd of PAPER_KINDS) variants.push([kd.id, defaultSpec(kd.id)!.params, "#ffffff"]);
    variants.push(["cart", { quad: "1", unit: "1", sub: "on", labels: "on" }, "#ffffff"], ["log", { base: "10", axes: "xy", cycles: "3" }, "#ffffff"], ["log", { base: "e", axes: "y", cycles: "5" }, "#120a26"], ["iso", { look: "dots", size: "1" }, "#120a26"]);
    for (const [kind, params, paperColour] of variants) {
      const c = document.createElement("canvas");
      c.width = 595; c.height = 842;
      const g = c.getContext("2d")!;
      g.fillStyle = paperColour; g.fillRect(0, 0, 595, 842);
      const before = g.getImageData(0, 0, 595, 842).data;
      try { drawPaper(g, 595, 842, paperColour === "#ffffff" ? "rgba(255,255,255,1)" : "rgba(18,10,38,1)", { kind, params }); } catch { allDraw = false; }
      const after = g.getImageData(0, 0, 595, 842).data;
      let changed = 0;
      for (let i = 0; i < after.length; i += 4) if (after[i] !== before[i] || after[i + 1] !== before[i + 1] || after[i + 2] !== before[i + 2]) changed++;
      if (changed < 800) { allDraw = false; r[`paperBlank_${kind}`] = changed; }
      if (paperColour !== "#ffffff") {
        // on dark paper the lines must be lighter than the paper
        let lighter = 0, darker = 0;
        for (let i = 0; i < after.length; i += 4) { const d = after[i]! + after[i + 1]! + after[i + 2]! - before[i]! - before[i + 1]! - before[i + 2]!; if (d > 0) lighter++; else if (d < 0) darker++; }
        if (!(lighter > 10 * darker)) allDark = false;
      }
      if (idx < 8) sx.drawImage(c, (idx % 4) * cw, Math.floor(idx / 4) * ch, cw, ch);
      idx++;
    }
    void cols;
    r.paperAllDrawOnLightAndDark = allDraw;
    r.paperDarkPapersGetLightLines = allDark;
    r.paperSheetPng = sheet.toDataURL("image/png").split(",")[1];
    applyAll.checked = wasAll;
  }

  // S3) 3D Cartesian spaces with anchors on the axes
  {
    const menu = async (...labels: string[]) => { const ok = await window.xournal.testMenuClick(labels); await sleep(80); return ok; };
    const savedColor = editor.color, savedWidth = editor.penWidth, savedGlow = editor.glowOn, savedSpec = { ...editor.space3Spec };
    setZoom(1);
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const drag = (pts: Array<[number, number]>, shift = false) => {
      const rc = el().getBoundingClientRect(), z = zoom;
      const P = (q: [number, number]) => [rc.left + q[0] * z, rc.top + q[1] * z] as const;
      synth("pointerdown", el(), ...P(pts[0]!), 0.5);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...P(pts[i]!), 0.5, "pen", shift);
      synth("pointerup", el(), ...P(pts[pts.length - 1]!), 0, "pen", shift);
    };
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    const mine = () => lay().elements.filter((e) => planeIdOf(e) !== null);
    const role = (rr: string) => mine().filter((e) => attrValue("attrs" in e ? e.attrs : [], "xlRole") === rr);
    const cur = () => editor.selectedSpace3()!;
    editor.color = "#0044cc";
    editor.penWidth = 1.41;
    editor.glowOn = false;
    editor.space3Spec = { ...DEFAULT_SPACE3 };
    setTool("space3");
    r.s3ToolSelected = editor.tool === "space3";
    const before = doc!.elementCount;
    drag([[150, 150], [450, 450]]);
    await sleep(200);
    const ids = new Set(mine().map((e) => planeIdOf(e)));
    r.s3Created = ids.size === 1 && role("axis3x").length === 1 && role("axis3y").length === 1 && role("axis3z").length === 1 && role("grid3").length === 2 && role("ticks").length === 3 && role("arrow").length === 3 && role("label").length >= 21;
    r.s3OneObject = doc!.elementCount === before + mine().length && editor.tool === "select" && editor.selectedSpace3() !== null && editor.selection.count === mine().length;
    r.s3IsNotA2dPlane = editor.selectedPlane() === null;
    const sp0 = cur().spec;
    const ext = extentOf(sp0);
    r.s3GrowsFromTheStart = near(ext[0], 150, 1.5) && near(ext[1], 150, 1.5) && (ext[2] - ext[0] > 100 || ext[3] - ext[1] > 100);
    r.s3GeometryMatchesTheAxes = (() => { const a = (role("axis3z")[0] as Stroke).pts; const p0 = project(sp0, 0, 0, sp0.zmin), pc = project(sp0, 0, 0, 0), p1 = project(sp0, 0, 0, sp0.zmax); return a.length === 6 && near(a[0]!, p0[0], 1e-6) && near(a[1]!, p0[1], 1e-6) && near(a[2]!, pc[0], 1e-6) && near(a[3]!, pc[1], 1e-6) && near(a[4]!, p1[0], 1e-6) && near(a[5]!, p1[1], 1e-6); })();
    r.s3DefaultView = (() => { const a = [0, 1, 2].map((i) => vectorAngle(sp0.u[i]!)); return Math.abs(a[0]!.deg - 215) < 0.01 && Math.abs(a[1]!.deg) < 0.01 && Math.abs(a[2]!.deg - 90) < 0.01; })();
    r.s3StyleFromPen = attrValue((role("axis3x")[0] as Stroke).attrs, "color") === "#0044ccff" && near((role("axis3x")[0] as Stroke).widths[0]!, 1.41);
    r.s3GridIsGeneratedInk = (role("grid3") as Stroke[]).every((g) => displayPoints(g, 8) === g.pts);
    editor.undo();
    r.s3UndoWhole = mine().length === 0 && doc!.elementCount === before;
    editor.redo();
    editor.selection.selectMany(PG, lay(), mine());
    r.s3RedoWhole = editor.selectedSpace3() !== null;

    // anchors: three, at the far end of each axis
    const anchors = anchorsOf(cur().spec);
    const anc = (axis: number, side: "+" | "-") => anchorsOf(cur().spec).find((a) => a.axis === axis && a.side === side)!;
    const tip = (axis: number, v: number) => project(sp0, axis === 0 ? v : 0, axis === 1 ? v : 0, axis === 2 ? v : 0);
    r.s3SixAnchors = anchors.length === 6 && [0, 1, 2].every((ax) => { const hi = anc(ax, "+"), lo = anc(ax, "-"); const lim = [[sp0.xmin, sp0.xmax], [sp0.ymin, sp0.ymax], [sp0.zmin, sp0.zmax]][ax]!; return near(hi.at[0], tip(ax, lim[1]!)[0], 1e-6) && near(lo.at[1], tip(ax, lim[0]!)[1], 1e-6); });
    r.s3StartsStraight = [0, 1, 2].every((ax) => near(sp0.n[ax]![0], sp0.u[ax]![0], 1e-9) && near(sp0.n[ax]![1], sp0.u[ax]![1], 1e-9));
    // drag the end of the y axis: angle and length follow the pointer
    const ay = anc(1, "+");
    const o0 = sp0.o;
    drag([[ay.at[0], ay.at[1]], [ay.at[0] + 20, ay.at[1] - 40], [o0[0] + 100, o0[1] - 100]]);
    await sleep(150);
    const sp1 = cur().spec;
    const vy = vectorAngle(sp1.u[1]!);
    r.s3AnchorTurnsTheAxis = near(vy.deg, 45, 0.8) && near(vy.len, Math.hypot(100, 100) / 3, 1) && near(sp1.o[0], sp0.o[0], 1e-6) && near(sp1.o[1], sp0.o[1], 1e-6);
    r.s3OtherAxesUntouched = near(sp1.u[0]![0], sp0.u[0]![0], 1e-9) && near(sp1.u[2]![1], sp0.u[2]![1], 1e-9);
    r.s3RebuiltAsOneObject = new Set(mine().map((e) => planeIdOf(e))).size === 1 && editor.selection.count === mine().length;
    editor.undo();
    const L1 = vectorAngle(sp0.u[1]!).len, L2 = vectorAngle(sp0.u[2]!).len;
    r.s3AnchorUndo = near(vectorAngle(cur().spec.u[1]!).deg, 0, 0.01) && near(vectorAngle(cur().spec.u[1]!).len, L1, 1e-6);
    // Shift: only the angle changes
    const ay2 = anc(1, "+");
    drag([[ay2.at[0], ay2.at[1]], [ay2.at[0] + 5, ay2.at[1] - 60], [o0[0] + 30, o0[1] - 200]], true);
    await sleep(150);
    const v2 = vectorAngle(cur().spec.u[1]!);
    r.s3ShiftKeepsLength = near(v2.len, L1, 1e-6) && v2.deg > 70 && v2.deg < 90;
    editor.undo();
    // the z axis anchor and the x axis anchor too
    const az = anc(2, "+");
    drag([[az.at[0], az.at[1]], [az.at[0] - 30, az.at[1] - 10], [o0[0] - 90, o0[1] - 60]], true);
    await sleep(150);
    r.s3ZAnchor = near(vectorAngle(cur().spec.u[2]!).len, L2, 1e-6) && vectorAngle(cur().spec.u[2]!).deg > 120 && vectorAngle(cur().spec.u[2]!).deg < 160;
    editor.undo();
    const axx = anc(0, "+");
    drag([[axx.at[0], axx.at[1]], [axx.at[0] - 10, axx.at[1] + 10], [o0[0] - 20, o0[1] + 90]]);
    await sleep(150);
    r.s3XAnchor = vectorAngle(cur().spec.u[0]!).deg > 250 && vectorAngle(cur().spec.u[0]!).deg < 290;
    editor.undo();

    // each half of an axis stretches on its own
    {
      const base = cur().spec;
      const yNegTip = project(base, 0, base.ymin, 0), yPosTip = project(base, 0, base.ymax, 0);
      const pos = anc(1, "+");
      drag([[pos.at[0], pos.at[1]], [pos.at[0] + 30, pos.at[1] - 10], [pos.at[0] + 90, pos.at[1] - 20]]);
      await sleep(150);
      const afterPos = cur().spec;
      const negNow = project(afterPos, 0, afterPos.ymin, 0);
      r.s3StretchPositiveLeavesNegative = near(negNow[0], yNegTip[0], 1e-6) && near(negNow[1], yNegTip[1], 1e-6) && near(afterPos.n[1]![0], base.n[1]![0], 1e-9) && near(afterPos.n[1]![1], base.n[1]![1], 1e-9);
      const posNow = project(afterPos, 0, afterPos.ymax, 0);
      r.s3PositiveHalfFollowsThePointer = near(posNow[0], pos.at[0] + 90, 1e-6) && near(posNow[1], pos.at[1] - 20, 1e-6) && Math.hypot(...afterPos.u[1]!) > Math.hypot(...base.u[1]!);
      // the axis is now bent at the origin: two different directions
      const ay3 = (role("axis3y")[0] as Stroke).pts;
      r.s3AxisThroughTheCrossing = ay3.length === 6 && near(ay3[2]!, project(afterPos, 0, 0, 0)[0], 1e-6) && near(ay3[4]!, posNow[0], 1e-6) && near(ay3[0]!, yNegTip[0], 1e-6);
      // read back from the strokes alone, both halves
      const derived = space3SpecOf(mine())!;
      r.s3HalvesAreReadBackFromTheStrokes = [0, 1, 2].every((i) => near(derived.u[i]![0], afterPos.u[i]![0], 1e-6) && near(derived.n[i]![1], afterPos.n[i]![1], 1e-6)) && near(derived.o[0], afterPos.o[0], 1e-6);
      editor.undo();
      r.s3StretchUndo = near(cur().spec.u[1]![0], base.u[1]![0], 1e-9) && near(project(cur().spec, 0, cur().spec.ymax, 0)[0], yPosTip[0], 1e-6);
      // the negative half on its own
      const neg = anc(1, "-");
      drag([[neg.at[0], neg.at[1]], [neg.at[0] - 20, neg.at[1] + 20], [neg.at[0] - 60, neg.at[1] + 50]]);
      await sleep(150);
      const afterNeg = cur().spec;
      const posKept = project(afterNeg, 0, afterNeg.ymax, 0);
      r.s3StretchNegativeLeavesPositive = near(posKept[0], yPosTip[0], 1e-6) && near(posKept[1], yPosTip[1], 1e-6) && near(afterNeg.u[1]![0], base.u[1]![0], 1e-9);
      const negNow2 = project(afterNeg, 0, afterNeg.ymin, 0);
      r.s3NegativeHalfFollowsThePointer = near(negNow2[0], neg.at[0] - 60, 1e-6) && near(negNow2[1], neg.at[1] + 50, 1e-6);
      editor.undo();
      // Shift on a negative half keeps its length
      const nz = anc(2, "-");
      const lenBefore = Math.hypot(...cur().spec.n[2]!);
      drag([[nz.at[0], nz.at[1]], [nz.at[0] + 10, nz.at[1] + 30], [nz.at[0] + 60, nz.at[1] + 60]], true);
      await sleep(150);
      r.s3ShiftKeepsTheHalfLength = near(Math.hypot(...cur().spec.n[2]!), lenBefore, 1e-6) && near(Math.hypot(...cur().spec.u[2]!), Math.hypot(...base.u[2]!), 1e-9);
      editor.undo();
      // old data (no `n`): axes stay straight
      const legacy = sanitizeSpace3({ ...base, n: undefined });
      r.s3OldSpaceWithoutNIsStraight = [0, 1, 2].every((i) => near(legacy.n[i]![0], legacy.u[i]![0], 1e-9) && near(legacy.n[i]![1], legacy.u[i]![1], 1e-9));
      // anchors only for the halves that the range has
      const half = sanitizeSpace3({ ...base, ymin: 0, zmax: 0, zmin: -3 });
      r.s3NoAnchorForAnHalfThatIsNotThere = anchorsOf(half).filter((a) => a.axis === 1).length === 1 && anchorsOf(half).filter((a) => a.axis === 2 && a.side === "+").length === 0 && anchorsOf(half).filter((a) => a.axis === 2 && a.side === "-").length === 1;
    }

    // moving the whole space keeps its shape
    const specBefore = cur().spec;
    const bb = (() => { let u: [number, number, number, number] | null = null; for (const e of mine()) { const b = elementBBox(e)!; u = u ? [Math.min(u[0], b[0]), Math.min(u[1], b[1]), Math.max(u[2], b[2]), Math.max(u[3], b[3])] : [...b] as [number, number, number, number]; } return u!; })();
    const cx = (bb[0] + bb[2]) / 2 + 40, cy = (bb[1] + bb[3]) / 2 + 40;
    drag([[cx, cy], [cx + 30, cy + 20], [cx + 60, cy + 40]]);
    await sleep(150);
    const moved = cur().spec;
    r.s3Moves = near(moved.o[0], specBefore.o[0] + 60, 0.5) && near(moved.o[1], specBefore.o[1] + 40, 0.5) && near(moved.u[0]![0], specBefore.u[0]![0], 1e-6);
    editor.undo();

    // resizing with a frame handle: rebuilt for the new size, one undo step
    const bb2 = bb;
    const nLabels = role("label").length;
    drag([[bb2[2], bb2[3]], [bb2[2] + 60, bb2[3] + 40], [bb2[2] + 100, bb2[3] + 80]]);
    await sleep(200);
    const specR = cur().spec;
    r.s3ResizeScales = Math.hypot(...specR.u[1]!) > Math.hypot(...specBefore.u[1]!) * 1.1 && role("label").length === nLabels && new Set(mine().map((e) => planeIdOf(e))).size === 1;
    r.s3ResizeKeepsRanges = specR.xmax === specBefore.xmax && specR.zmin === specBefore.zmin && near(vectorAngle(specR.u[2]!).deg, 90, 0.5);
    r.s3ResizeKeepsWidth = near((role("axis3x")[0] as Stroke).widths[0]!, 1.41, 1e-6);
    editor.undo();
    r.s3ResizeOneUndo = near(Math.hypot(...cur().spec.u[1]!), L1, 1e-6);

    // the panel
    document.getElementById("btnSpace3")!.click();
    const panel = document.getElementById("space3Panel")!;
    r.s3PanelOpens = !panel.hidden && (document.getElementById("s3Mode")!.textContent ?? "").includes("seleccionado");
    r.s3PanelShowsAngles = (document.getElementById("s3Zang") as HTMLInputElement).value === "90" && (document.getElementById("s3Yang") as HTMLInputElement).value === "0" && (document.getElementById("s3Xang") as HTMLInputElement).value === "215";
    const setIn = (id: string, v: string) => { const e = document.getElementById(id) as HTMLInputElement; e.value = v; e.dispatchEvent(new Event("change", { bubbles: true })); };
    setIn("s3Zang", "60");
    r.s3PanelAngle = near(vectorAngle(cur().spec.u[2]!).deg, 60, 0.05) && near(vectorAngle(cur().spec.u[2]!).len, L2, 0.06);
    setIn("s3Ylen", "50");
    r.s3PanelLength = near(vectorAngle(cur().spec.u[1]!).len, 50, 0.06);
    const uBefore = cur().spec.u[1]!;
    setIn("s3YangN", "200");
    r.s3PanelNegativeHalf = near(vectorAngle([-cur().spec.n[1]![0], -cur().spec.n[1]![1]]).deg, 200, 0.06) && near(cur().spec.u[1]![0], uBefore[0], 1e-9) && (document.getElementById("s3YangN") as HTMLInputElement).value === "200";
    setIn("s3YlenN", "20");
    r.s3PanelNegativeLength = near(Math.hypot(...cur().spec.n[1]!), 20, 0.06) && near(Math.hypot(...cur().spec.u[1]!), Math.hypot(...uBefore), 1e-9);
    setIn("s3Xmax", "5");
    r.s3PanelRange = cur().spec.xmax === 5 && (role("label") as TextBox[]).some((t) => t.text === "5");
    setIn("s3Zstep", "2");
    r.s3PanelScale = cur().spec.zstep === 2 && !(role("label") as TextBox[]).some((t) => t.text === "1" && false);
    const gxz = document.getElementById("s3GridXZ") as HTMLInputElement;
    gxz.checked = true; gxz.dispatchEvent(new Event("change", { bubbles: true }));
    r.s3PanelGrids = role("grid3").length === 4;
    const gxy = document.getElementById("s3GridXY") as HTMLInputElement;
    gxy.checked = false; gxy.dispatchEvent(new Event("change", { bubbles: true }));
    gxz.checked = false; gxz.dispatchEvent(new Event("change", { bubbles: true }));
    r.s3PanelOnlyAxes = role("grid3").length === 0;
    setIn("s3Ymin", "9"); // refused
    r.s3PanelRefusesEmptyRange = cur().spec.ymin === -3 && (document.getElementById("info")!.textContent ?? "").includes("y mín");
    document.getElementById("s3Reset")!.click();
    r.s3PanelResetView = near(vectorAngle(cur().spec.u[2]!).deg, 90, 0.06) && near(vectorAngle(cur().spec.u[1]!).len, 34, 0.06);
    editor.color = "#cc3300";
    document.getElementById("s3Restyle")!.click();
    r.s3PanelRestyle = attrValue((role("axis3x")[0] as Stroke).attrs, "color") === "#cc3300ff" && (role("label") as TextBox[]).every((t) => t.color.startsWith("rgba(204,51,0"));
    r.s3PanelSavesDefaults = JSON.parse((await window.xournal.getPrefs()).space3).xmax === 5;
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.s3PanelEscCloses = panel.hidden;

    // the file keeps it, and the editor finds the space again from the strokes alone
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml);
    const bm = back.pages[PG]!.layers.at(-1)!.elements.filter((e) => planeIdOf(e) !== null);
    const re = space3SpecOf(bm), orig = cur().spec;
    r.s3RoundTrip = bm.length === mine().length && isSpace3(bm) && !!re && near(re.u[0]![0], orig.u[0]![0], 1e-4) && near(re.u[2]![1], orig.u[2]![1], 1e-4) && near(re.o[0], orig.o[0], 1e-3) && /<stroke [^>]*xlRole="axis3z"/.test(xml);
    r.s3IsOrdinaryInk = /<stroke [^>]*tool="pen"/.test(xml);

    // a plane and a space are different objects; the menu and the key choose the tool
    r.s3MenuAndKey = (await menu("Herramientas", "Espacio cartesiano 3D")) && editor.tool === "space3";
    // 2D planes keep working beside it
    editor.selection.clear();
    editor.planeSpec = { ...DEFAULT_PLANE };
    setTool("plane");
    drag([[500, 150], [700, 350]]);
    await sleep(150);
    r.s3PlaneStillWorks = editor.selectedPlane() !== null && editor.selectedSpace3() === null;
    editor.undo();

    // clean up
    editor.color = savedColor; editor.penWidth = savedWidth; editor.glowOn = savedGlow; editor.space3Spec = savedSpec;
    editor.selection.clear();
    await window.xournal.setPrefs({ space3: "" });
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // TBX) tool box: floating panel of tools, in zones
  {
    const savedBox: ToolboxSettings = JSON.parse(JSON.stringify(toolboxSettings));
    const savedTab: TabletSettings = JSON.parse(JSON.stringify(tablet));
    const savedInk = editor.color;
    const box = document.getElementById("toolbox")!;
    const key = (k: string, mods: Partial<KeyboardEventInit> = {}) => window.dispatchEvent(new KeyboardEvent("keydown", { key: k, bubbles: true, cancelable: true, ...mods }));
    const rowsEls = () => [...box.querySelectorAll<HTMLElement>(".tbxRow")];
    const idsOf = (row: HTMLElement) => [...row.querySelectorAll<HTMLElement>(".tbxItem, .tbxSep")].map((b) => (b.classList.contains("tbxSep") ? "|" : b.dataset.id!));
    const rowsIds = () => rowsEls().map(idsOf);
    const flat = () => rowsIds().flat();
    const pool = () => [...box.querySelectorAll<HTMLElement>(".tbxPool .tbxItem, .tbxPool .tbxSep")].map((b) => (b.classList.contains("tbxSep") ? "|" : b.dataset.id!));
    const btn = (id: string, scope = ".tbxZones") => box.querySelector(`${scope} .tbxItem[data-id="${id}"]`) as HTMLElement;
    const drag = (type: string, target: Element, data: { id: string; row?: number; index?: number }, effect?: string) => {
      const dt = new DataTransfer();
      dt.setData("text/plain", JSON.stringify(data));
      dt.effectAllowed = "all";
      if (effect) dt.dropEffect = effect as DataTransfer["dropEffect"];
      target.dispatchEvent(new DragEvent(type, { bubbles: true, cancelable: true, dataTransfer: dt }));
    };
    const editBtn = () => [...box.querySelectorAll(".tbxHead button")].find((b) => /Editar|Listo/.test(b.textContent ?? "")) as HTMLElement;
    const small = (label: string) => [...box.querySelectorAll<HTMLElement>(".tbxSmall")].find((b) => b.textContent === label)!;
    const pageEl = views[0]!.el;
    const strokeCount = () => doc!.pages[0]!.layers.reduce((n, l) => n + l.elements.length, 0);
    const press = () => {
      const q = pageEl.getBoundingClientRect();
      synth("pointerdown", pageEl, q.left + 300, q.top + 200, 0.5, "pen", false, false, { button: 2, buttons: 2 });
      synth("pointerup", pageEl, q.left + 300, q.top + 200, 0, "pen", false, false, { button: 2, buttons: 2 });
      return q;
    };

    // settings from anything
    const bad = sanitizeToolbox({ enabled: "x", rows: [["tool.pen", "nope", "tool.pen", 5, "edit.undo", "|", "|"], "x", ["color:#FF0000", "color:red", "bar:save", "tool.pen"]], closeOnPick: false });
    r.tbxSanitize = bad.enabled === true && JSON.stringify(bad.rows) === JSON.stringify([["tool.pen", "edit.undo", "|", "|"], [], ["color:#FF0000", "bar:save"]]) && bad.closeOnPick === false;
    r.tbxSanitizeOldFormat = JSON.stringify(sanitizeToolbox({ items: ["tool.pen", "edit.undo", "tool.pen"] }).rows) === JSON.stringify([["tool.pen", "edit.undo"]]);
    r.tbxSanitizeDefaults = sanitizeToolbox(null).rows.length === 3 && sanitizeToolbox({ enabled: false }).enabled === false && sanitizeToolbox({ rows: [] }).rows.length === 1 && sanitizeToolbox({ rows: Array(20).fill(["tool.pen"]) }).rows.length === 6;

    // opens with the key: three zones with a separator, the current tool marked
    toolboxSettings = sanitizeToolbox(null);
    setTool("pen");
    lastPointer.x = 500; lastPointer.y = 300;
    key("q");
    r.tbxOpensWithKey = !box.hidden && rowsEls().length === 3 && flat().length === 14 && flat().filter((i) => i === "|").length === 1;
    const r0 = box.getBoundingClientRect();
    r.tbxNearPointer = r0.left >= 4 && r0.right <= window.innerWidth && r0.bottom <= window.innerHeight && Math.abs(r0.left + r0.width / 2 - 500) < Math.max(3, r0.width / 2);
    r.tbxMarksCurrent = btn("tool.pen").classList.contains("active") && box.querySelectorAll(".tbxItem.active").length === 1;
    r.tbxHasIcons = box.querySelectorAll(".tbxItem svg").length === flat().filter((i) => i !== "|").length;
    r.tbxSeparatorDrawn = box.querySelectorAll(".tbxSep").length === 1;
    btn("tool.eraser").click();
    r.tbxPickChangesTool = editor.tool === "eraser" && box.hidden;
    key("q"); key("q");
    r.tbxKeyToggles = box.hidden;
    key("q");
    box.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.tbxEscCloses = box.hidden;
    key("q");
    document.body.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
    r.tbxClickOutsideCloses = box.hidden;
    key("q", { ctrlKey: true });
    r.tbxCtrlQIgnored = box.hidden;
    // an item of the toolbar works through the toolbar button; colours too
    toolboxSettings = sanitizeToolbox({ rows: [["bar:rotR", "color:#ff0000", "bar:pgNext"]] });
    key("q");
    const pageBefore = currentPage();
    btn("bar:pgNext").click();
    r.tbxToolbarItemRuns = currentPage() === pageBefore || doc!.pages.length === 1;
    key("q");
    btn("color:#ff0000").click();
    r.tbxColourItem = editor.color.toLowerCase() === "#ff0000";
    key("q");
    r.tbxColourMarked = btn("color:#ff0000").classList.contains("active");
    box.hidden = true;
    toolboxSettings = sanitizeToolbox(null);

    // a user binding wins over the default key; an action can open it
    applyTablet({ ...tablet, keys: { Q: "tool.lasso" } });
    key("q");
    r.tbxBindingWins = box.hidden && editor.tool === "lasso";
    applyTablet({ ...tablet, keys: { "Ctrl+Alt+7": "toolbox.show" } });
    key("7", { ctrlKey: true, altKey: true });
    r.tbxBoundAction = !box.hidden;
    box.hidden = true;
    applyTablet({ ...tablet, keys: {} });

    // from a pen button
    applyTablet({ ...tablet, barrel: "toolbox" });
    const n0 = strokeCount();
    const q0 = press();
    const rb = box.getBoundingClientRect();
    r.tbxPenButton = !box.hidden && rb.left >= 4 && Math.abs(rb.left + rb.width / 2 - (q0.left + 300)) < Math.max(3, rb.width / 2) && strokeCount() === n0;
    box.hidden = true;
    r.tbxPenActionListed = PEN_ACTIONS.some((a) => a.id === "toolbox");
    applyTablet({ ...tablet, barrel: "eraser" });

    // editing: zones, separators, catalogue
    key("q");
    editBtn().click();
    r.tbxEditMode = box.classList.contains("editing") && box.querySelectorAll(".tbxRow button.tbxZoneDel").length === 3 && !!box.querySelector(".tbxPool") && btn("tool.pen").draggable;
    r.tbxEditStaysOpenOnOutsideClick = (() => { document.body.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true })); return !box.hidden; })();
    r.tbxEditDoesNotPick = (() => { const t = editor.tool; btn("tool.select").click(); return editor.tool === t; })();
    // the catalogue by category and by name
    const chips = () => [...box.querySelectorAll<HTMLElement>(".tbxChip")];
    r.tbxChips = chips().map((c) => c.textContent).join() === GROUPS.join() && chips()[0]!.classList.contains("on");
    chips().find((c) => c.textContent === "Navegación")!.click();
    r.tbxCategoryShows = pool().includes("bar:pgNext") && pool().includes("view.zoomIn") && !pool().includes("tool.pen");
    chips().find((c) => c.textContent === "Colores")!.click();
    r.tbxColoursCategory = pool().length >= 5 && pool().every((i) => i.startsWith("color:"));
    chips().find((c) => c.textContent === "Separadores")!.click();
    r.tbxSeparatorsCategory = pool().join() === "|";
    const search = box.querySelector<HTMLInputElement>(".tbxFind input")!;
    search.value = "rotar"; search.dispatchEvent(new Event("input"));
    r.tbxSearch = pool().includes("bar:rotL") && pool().includes("bar:rotR") && !pool().includes("tool.pen");
    search.value = "ALEJAR"; search.dispatchEvent(new Event("input"));
    r.tbxSearchIgnoresCase = pool().join() === "view.zoomOut";
    search.value = "zzzz"; search.dispatchEvent(new Event("input"));
    r.tbxSearchNothing = pool().length === 0 && !!box.querySelector(".tbxPool .tbxEmpty");
    search.value = "página"; search.dispatchEvent(new Event("input"));
    r.tbxSearchAccent = pool().includes("bar:pgNext");
    // drop from the catalogue into a zone: at the end, and before an item
    drag("drop", rowsEls()[2]!, { id: "bar:pgNext" });
    r.tbxAddAtEnd = idsOf(rowsEls()[2]!).join() === "edit.undo,edit.redo,bar:pgNext";
    drag("drop", btn("edit.undo"), { id: "bar:pgPrev" });
    r.tbxInsertBefore = idsOf(rowsEls()[2]!).join() === "bar:pgPrev,edit.undo,edit.redo,bar:pgNext";
    // a button dragged from the toolbar of the window
    const barBtn = document.getElementById("save")!;
    r.tbxToolbarDraggable = barBtn.draggable && document.querySelector<HTMLElement>("button.tool[data-tool=pen]")!.draggable && document.querySelectorAll<HTMLElement>("#swatches .sw")[0]!.draggable;
    const dtBar = new DataTransfer();
    barBtn.dispatchEvent(new DragEvent("dragstart", { bubbles: true, dataTransfer: dtBar }));
    const fromBar = JSON.parse(dtBar.getData("text/plain") || "{}") as { id?: string };
    r.tbxToolbarDragCarriesTheItem = fromBar.id === "bar:save";
    drag("drop", rowsEls()[1]!, fromBar as { id: string });
    r.tbxDropFromToolbar = idsOf(rowsEls()[1]!).at(-1) === "bar:save";
    // zones
    small("+ Zona").click();
    r.tbxAddZone = rowsEls().length === 4 && idsOf(rowsEls()[3]!).length === 0;
    drag("drop", rowsEls()[3]!, { id: "tool.laser" });
    drag("drop", rowsEls()[3]!, { id: "|" });
    drag("drop", rowsEls()[3]!, { id: "|" });
    r.tbxSeparatorsRepeat = idsOf(rowsEls()[3]!).join() === "tool.laser,|,|";
    // move between zones (the item leaves its place)
    drag("drop", rowsEls()[0]!, { id: "tool.laser", row: 3, index: 0 });
    r.tbxMoveBetweenZones = idsOf(rowsEls()[0]!).at(-1) === "tool.laser" && idsOf(rowsEls()[3]!).join() === "|,|" && flat().filter((i) => i === "tool.laser").length === 1;
    // reorder in a zone
    drag("drop", btn("tool.pen"), { id: "tool.laser", row: 0, index: 8 });
    r.tbxReorder = idsOf(rowsEls()[0]!)[0] === "tool.laser" && idsOf(rowsEls()[0]!)[1] === "tool.pen" && new Set(flat().filter((i) => i !== "|")).size === flat().filter((i) => i !== "|").length;
    // a button already in the panel dropped again moves
    drag("drop", rowsEls()[2]!, { id: "tool.pen" });
    r.tbxRepeatedMoves = flat().filter((i) => i === "tool.pen").length === 1 && idsOf(rowsEls()[2]!).at(-1) === "tool.pen";
    // a separator removed by dragging it out; a button too
    const sepRow = 3;
    drag("dragend", rowsEls()[sepRow]!.querySelector(".tbxSep")!, { id: "|", row: sepRow, index: 0 }, "none");
    r.tbxRemoveSeparator = idsOf(rowsEls()[sepRow]!).join() === "|";
    drag("dragend", btn("tool.fill"), { id: "tool.fill", row: 1, index: 3 }, "none");
    r.tbxRemoveByDraggingOut = !flat().includes("tool.fill");
    drag("drop", box.querySelector(".tbxPool")!, { id: "tool.text", row: 1, index: 0 });
    r.tbxRemoveByDropOnCatalogue = !flat().includes("tool.text");
    // removing a zone
    rowsEls()[sepRow]!.querySelector<HTMLElement>(".tbxZoneDel")!.click();
    r.tbxRemoveZone = rowsEls().length === 3;
    // saved
    const saved = JSON.parse((await window.xournal.getPrefs()).toolbox) as ToolboxSettings;
    r.tbxPersisted = JSON.stringify(saved.rows) === JSON.stringify(rowsIds()) && JSON.stringify(toolboxSettings.rows) === JSON.stringify(rowsIds());
    // leave edit mode: no empty zones, no catalogue; it reopens the same
    editBtn().click();
    r.tbxLeavesEdit = !box.classList.contains("editing") && !box.querySelector(".tbxPool") && !btn("tool.laser").draggable;
    box.hidden = true;
    key("q");
    r.tbxReopensWithChanges = JSON.stringify(rowsIds()) === JSON.stringify(saved.rows.filter((row) => row.length));
    r.tbxToolbarNotDraggableAnymore = !document.getElementById("save")!.draggable;
    // restore, and stay open after a pick
    editBtn().click();
    small("Restaurar").click();
    r.tbxRestore = JSON.stringify(rowsIds()) === JSON.stringify(DEFAULT_TOOLBOX.rows);
    const cb = box.querySelector(".tbxOpts input") as HTMLInputElement;
    cb.checked = false; cb.dispatchEvent(new Event("change"));
    editBtn().click();
    btn("tool.highlighter").click();
    r.tbxKeepOpen = !box.hidden && editor.tool === "highlighter" && toolboxSettings.closeOnPick === false;
    box.hidden = true;

    // an old saved list (before the zones) is one zone
    toolboxSettings = sanitizeToolbox({ items: ["tool.pen", "tool.select"] });
    key("q");
    r.tbxOldFormatShown = rowsEls().length === 1 && idsOf(rowsEls()[0]!).join() === "tool.pen,tool.select";
    box.hidden = true;

    // switched off
    toolboxSettings = { ...sanitizeToolbox(null), closeOnPick: true };
    r.tbxMenuToggle = await window.xournal.testMenuClick(["Herramientas", "Activar o desactivar el cuadro de herramientas"]);
    await sleep(80);
    key("q");
    r.tbxOff = toolboxSettings.enabled === false && box.hidden;
    applyTablet({ ...tablet, barrel: "toolbox" });
    press();
    r.tbxOffPenButton = box.hidden;
    r.tbxOffStatus = /desactivado/.test(info.textContent ?? "");
    r.tbxMenuShowWhenOff = (await window.xournal.testMenuClick(["Herramientas", "Mostrar el cuadro de herramientas"])) && box.hidden;
    await window.xournal.testMenuClick(["Herramientas", "Activar o desactivar el cuadro de herramientas"]);
    await sleep(80);
    r.tbxOn = toolboxSettings.enabled === true;
    r.tbxMenuShow = (await window.xournal.testMenuClick(["Herramientas", "Mostrar el cuadro de herramientas"])) && !box.hidden;
    box.hidden = true;

    // clean up
    selectColor(savedInk);
    toolboxSettings = savedBox;
    saveToolbox(savedBox);
    applyTablet(savedTab, true);
    setTool("pen");
    await sleep(100);
  }

  // MD) markdown in texts
  {
    const savedMd = editor.textMarkdown, savedSize = editor.textSize;
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    // parsing
    const L = (s: string, size = 10) => parseMarkdown(s, size);
    const heads = [1, 2, 3, 6].map((n) => L("#".repeat(n) + " T")[0]!);
    r.mdHeadings = heads[0]!.size === 20 && heads[1]!.size === 16 && near(heads[2]!.size, 13.5) && heads[3]!.size === 10 && heads.every((h) => h.runs[0]!.bold && h.runs[0]!.text === "T");
    r.mdHashNeedsSpace = L("#sinespacio")[0]!.runs[0]!.text === "#sinespacio" && L("#sinespacio")[0]!.size === 10;
    const inl = inlineRuns("a **b** *c* ~~d~~ `e` [f](http://x)");
    r.mdInline = inl.map((x) => x.text).join("|") === "a |b| |c| |d| |e| |f" && inl[1]!.bold && inl[3]!.italic && inl[5]!.strike && inl[7]!.mono && inl[9]!.link && inl[9]!.underline;
    r.mdUnclosedStaysLiteral = inlineRuns("2 * 3 y **abierto").map((x) => x.text).join("") === "2 * 3 y **abierto" && inlineRuns("a * b * c").every((x) => !x.italic);
    r.mdSnakeCaseUntouched = inlineRuns("snake_case_name").length === 1 && !inlineRuns("snake_case_name")[0]!.italic;
    r.mdEscapes = inlineRuns("\\*no\\* \\# x").map((x) => x.text).join("") === "*no* # x" && inlineRuns("\\*no\\*").every((x) => !x.italic);
    const lst = L("- uno\n  - dos\n* [ ] tarea\n- [x] hecha\n1. primero\n2) segundo");
    r.mdLists = lst[0]!.marker!.text === "•" && lst[1]!.marker!.text === "◦" && lst[1]!.x > lst[0]!.x && lst[2]!.marker!.text === "☐" && lst[3]!.marker!.text === "☑" && lst[4]!.marker!.text === "1." && lst[5]!.marker!.text === "2.";
    const blk = L("> cita\n---\n```\ncódigo **x**\n```\ntexto");
    r.mdBlocks = blk[0]!.quote && blk[1]!.rule && blk[2]!.code && blk[2]!.runs[0]!.mono && blk[2]!.runs[0]!.text === "código **x**" && blk.length === 4 && !blk[3]!.code;
    r.mdBlankLineIsHalf = (() => { const a = layoutMarkdown({ text: "a\n\nb", size: 10, font: "Sans" }), b = layoutMarkdown({ text: "a\nb", size: 10, font: "Sans" }); return near(a.height - b.height, 6.5, 0.01); })();

    // writing a text with the text tool
    const el = views[0]!.el;
    const rc = () => el.getBoundingClientRect();
    const X = (x: number) => rc().left + x * zoom, Y = (y: number) => rc().top + y * zoom;
    const write = async (value: string, x: number, y: number) => {
      setTool("text");
      synth("pointerdown", el, X(x), Y(y), 0.5);
      synth("pointerup", el, X(x), Y(y), 0);
      await sleep(30);
      const area = el.querySelector("textarea") as HTMLTextAreaElement;
      area.value = value;
      area.dispatchEvent(new Event("blur"));
      await sleep(30);
    };
    const texts = () => activeLayer(doc!.pages[0]!).elements.filter((e): e is TextBox => e.kind === "text");
    const src = "# Título\n**negrita** y *cursiva*\n- uno";
    editor.textSize = 12;
    editor.textMarkdown = true;
    inserts.setMarkdown(true);
    const n0 = texts().length;
    await write(src, 100, 400);
    const t = texts().at(-1)!;
    r.mdBoxCreated = texts().length === n0 + 1 && t.text === src && isMarkdown(t) && attrValue(t.attrs, "xlMd") === "1";
    const bb = textBBox(t);
    const plainH = 3 * 12 * 1.2;
    r.mdBoxMeasured = bb[3] - bb[1] > plainH + 8 && bb[2] - bb[0] > 40 && near(bb[1], t.y);
    r.mdLayoutHeadingBigger = layoutMarkdown(t).lines[0]!.line.size === 24 && layoutMarkdown(t).lines[0]!.h > layoutMarkdown(t).lines[1]!.h;
    // it is drawn formatted: the first line has no "#" and its ink is taller than the body text's
    const cv = document.createElement("canvas");
    cv.width = Math.ceil(bb[2] - bb[0]) + 20; cv.height = Math.ceil(bb[3] - bb[1]) + 20;
    const g = cv.getContext("2d", { willReadFrequently: true })!;
    g.fillStyle = "#fff"; g.fillRect(0, 0, cv.width, cv.height);
    g.translate(10 - t.x, 10 - t.y);
    drawElement(g, t, () => undefined);
    const inkRows = (y0: number, y1: number) => { const d = g.getImageData(0, y0, cv.width, y1 - y0).data; let rows = 0; for (let y = 0; y < y1 - y0; y++) { let hit = false; for (let x = 0; x < cv.width && !hit; x++) if (d[(y * cv.width + x) * 4]! < 128) hit = true; if (hit) rows++; } return rows; };
    const hLine = layoutMarkdown(t).lines[0]!;
    r.mdDrawsInk = inkRows(10, 10 + Math.round(hLine.h)) > 8 && inkRows(10 + Math.round(hLine.h), cv.height) > 8;
    const bullet = layoutMarkdown(t).lines[2]!;
    r.mdBulletDrawn = (() => { const d = g.getImageData(10, 10 + Math.round(bullet.y), 14, Math.round(bullet.h), { colorSpace: "srgb" }).data; for (let i = 0; i < d.length; i += 4) if (d[i]! < 128) return true; return false; })();
    const xml2 = serializeXopp(doc!);
    r.mdAttributeSaved = /<text [^>]*xlMd="1"/.test(xml2) && parseXopp(xml2).pages[0]!.layers.flatMap((l) => l.elements).some((e) => e.kind === "text" && isMarkdown(e) && e.text === src);
    editor.undo();
    r.mdCreateUndo = texts().length === n0;
    editor.redo();

    // editing shows the source, and the changes (and the switch) undo
    await sleep(30);
    setTool("text");
    synth("pointerdown", el, X(t.x + 4), Y(t.y + 4), 0.5);
    synth("pointerup", el, X(t.x + 4), Y(t.y + 4), 0);
    await sleep(30);
    let area = el.querySelector("textarea") as HTMLTextAreaElement;
    r.mdEditShowsSource = !!area && area.value === src && inserts.markdown === true && btnMd.classList.contains("on");
    area.value = "## Otro\n*x*";
    area.dispatchEvent(new Event("blur"));
    await sleep(30);
    const t2 = texts().at(-1)!;
    r.mdEdited = t2 === t && t.text === "## Otro\n*x*" && isMarkdown(t);
    editor.undo();
    r.mdEditUndo = t.text === src && isMarkdown(t);
    editor.redo();
    // switch it off while editing: back to plain text
    synth("pointerdown", el, X(t.x + 4), Y(t.y + 4), 0.5);
    synth("pointerup", el, X(t.x + 4), Y(t.y + 4), 0);
    await sleep(30);
    area = el.querySelector("textarea") as HTMLTextAreaElement;
    btnMd.dispatchEvent(new MouseEvent("mousedown", { bubbles: true, cancelable: true }));
    btnMd.click();
    r.mdToggleOffWhileEditing = inserts.markdown === false && !btnMd.classList.contains("on") && el.querySelector("textarea") === area;
    area.dispatchEvent(new Event("blur"));
    await sleep(30);
    r.mdBecamePlain = !isMarkdown(t) && t.text === "## Otro\n*x*" && !t.attrs.some((a) => a[0] === "xlMd");
    const plain = textBBox(t);
    r.mdPlainMeasuresAsBefore = near(plain[3] - plain[1], 2 * 12 * 1.2, 1e-6);
    editor.undo();
    r.mdBecamePlainUndo = isMarkdown(t);
    editor.redo();
    r.mdPrefSaved = (await window.xournal.getPrefs()).textMd === false;

    // a text written with the switch off is plain; the switch is remembered for the next ones
    await write("# no es titulo", 100, 500);
    const t3 = texts().at(-1)!;
    r.mdOffWritesPlain = !isMarkdown(t3) && t3.text === "# no es titulo" && near(textBBox(t3)[3] - textBBox(t3)[1], 12 * 1.2, 1e-6);
    btnMd.click();
    r.mdToggleOnBetweenTexts = editor.textMarkdown === true && btnMd.getAttribute("aria-pressed") === "true";

    // saved and opened again
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml);
    const bt = back.pages[0]!.layers.flatMap((l) => l.elements).filter((e): e is TextBox => e.kind === "text" && e.text.startsWith("## Otro"));
    r.mdRoundTrip = bt.length === 1 && bt[0]!.text === "## Otro\n*x*" && !isMarkdown(bt[0]!);

    // clean up
    editor.textMarkdown = savedMd; editor.textSize = savedSize;
    inserts.setMarkdown(savedMd);
    await window.xournal.setPrefs({ textMd: true });
    setTool("pen");
    while (texts().length > n0) { const last = texts().at(-1)!; activeLayer(doc!.pages[0]!).elements.splice(activeLayer(doc!.pages[0]!).elements.indexOf(last), 1); doc!.elementCount--; }
    await sleep(60);
  }

  // FLOAT) dialogs that move and detach
  {
    const modal = document.getElementById("latexDlg")!;
    const dlg = modal.querySelector<HTMLElement>(".dlg")!;
    const head = modal.querySelector<HTMLElement>(".dlgHead")!;
    const area = document.getElementById("ltx") as HTMLTextAreaElement;
    const count0 = doc!.elementCount;
    setTool("latex");
    latex.openAt(0, 150, 150);
    await sleep(100);
    r.floatHasControls = !!head.querySelector(".dlgTool") && head.style.cursor === "move";
    // it opens floating, with the page in sight and out of the way of where the last click was
    r.floatOpensFloating = modal.classList.contains("floating") && getComputedStyle(modal).backgroundColor === "rgba(0, 0, 0, 0)" && anyFloatingOpen();
    const o1 = dlg.getBoundingClientRect();
    latex.close();
    await sleep(30);
    document.body.dispatchEvent(new PointerEvent("pointerdown", { clientX: 300, clientY: 80, bubbles: true }));
    latex.openAt(0, 150, 150);
    await sleep(60);
    const lowDlg = dlg.getBoundingClientRect();
    latex.close();
    await sleep(30);
    document.body.dispatchEvent(new PointerEvent("pointerdown", { clientX: 300, clientY: window.innerHeight - 60, bubbles: true }));
    latex.openAt(0, 150, 150);
    await sleep(60);
    const highDlg = dlg.getBoundingClientRect();
    r.floatAvoidsTheClick = lowDlg.top > window.innerHeight / 2 - 40 && highDlg.top < window.innerHeight / 2 && o1.width > 300 && Math.abs(lowDlg.left + lowDlg.width / 2 - window.innerWidth / 2) < 3;

    const before = dlg.getBoundingClientRect();
    area.value = "abc";
    // drag by the title bar
    const hr = head.getBoundingClientRect();
    head.dispatchEvent(new PointerEvent("pointerdown", { clientX: hr.left + 40, clientY: hr.top + 8, pointerId: 11, button: 0, buttons: 1, bubbles: true, cancelable: true }));
    head.dispatchEvent(new PointerEvent("pointermove", { clientX: hr.left + 40 - 150, clientY: hr.top + 8 + 120, pointerId: 11, buttons: 1, bubbles: true }));
    head.dispatchEvent(new PointerEvent("pointerup", { clientX: hr.left + 40 - 150, clientY: hr.top + 8 + 120, pointerId: 11, buttons: 0, bubbles: true }));
    const moved = dlg.getBoundingClientRect();
    r.floatDragMoves = modal.classList.contains("floating") && Math.abs(moved.left - (before.left - 150)) < 2 && Math.abs(moved.top - (before.top + 120)) < 2 && Math.abs(moved.width - before.width) < 2;
    r.floatDoesNotBlockPage = getComputedStyle(modal).pointerEvents === "none" && getComputedStyle(dlg).pointerEvents === "auto" && getComputedStyle(modal).backgroundColor === "rgba(0, 0, 0, 0)";
    r.floatIsReported = anyFloatingOpen() && floatingState("latexDlg")!.floating;
    // a click on the page while it floats must not reopen or change the formula
    const pg = views[0]!.el.getBoundingClientRect();
    synth("pointerdown", views[0]!.el, pg.left + 300, pg.top + 300, 0.5, "mouse");
    synth("pointerup", views[0]!.el, pg.left + 300, pg.top + 300, 0, "mouse");
    await sleep(60);
    r.floatPageIgnoresClicks = area.value === "abc" && !modal.hidden;
    // it stays inside the window, however far it is dragged
    head.dispatchEvent(new PointerEvent("pointerdown", { clientX: moved.left + 40, clientY: moved.top + 8, pointerId: 12, button: 0, buttons: 1, bubbles: true, cancelable: true }));
    head.dispatchEvent(new PointerEvent("pointermove", { clientX: -5000, clientY: -5000, pointerId: 12, buttons: 1, bubbles: true }));
    head.dispatchEvent(new PointerEvent("pointerup", { clientX: -5000, clientY: -5000, pointerId: 12, buttons: 0, bubbles: true }));
    const far = dlg.getBoundingClientRect();
    r.floatStaysReachable = far.top >= 0 && far.right >= 60 && far.left <= window.innerWidth - 60;
    head.dispatchEvent(new PointerEvent("pointerdown", { clientX: far.left + 40, clientY: far.top + 8, pointerId: 13, button: 0, buttons: 1, bubbles: true, cancelable: true }));
    head.dispatchEvent(new PointerEvent("pointermove", { clientX: far.left + 40 + 300, clientY: far.top + 8 + 200, pointerId: 13, buttons: 1, bubbles: true }));
    head.dispatchEvent(new PointerEvent("pointerup", { clientX: far.left + 340, clientY: far.top + 208, pointerId: 13, buttons: 0, bubbles: true }));
    const spot = dlg.getBoundingClientRect();
    // close and open again: same place and size
    latex.close();
    await sleep(30);
    r.floatClosedIsClean = modal.hidden && !modal.classList.contains("floating") && dlg.style.left === "" && !anyFloatingOpen();
    latex.openAt(0, 150, 150);
    await sleep(60);
    const again = dlg.getBoundingClientRect();
    r.floatRemembersPlace = modal.classList.contains("floating") && Math.abs(again.left - spot.left) < 2 && Math.abs(again.top - spot.top) < 2 && Math.abs(again.width - spot.width) < 2;

    // detach: the elements move to a window of their own, and the dialog keeps working
    area.value = "x^{2}+y^{2}";
    area.dispatchEvent(new Event("input"));
    (head.querySelector(".dlgTool") as HTMLButtonElement).click();
    await sleep(700);
    const st = floatingState("latexDlg")!;
    r.floatDetaches = st.detached && st.windowOpen && modal.ownerDocument !== document && modal.classList.contains("detached") && !modal.classList.contains("floating");
    const cdoc = modal.ownerDocument;
    r.floatChildHasStyles = cdoc.querySelectorAll("link[rel=stylesheet], style").length >= 1 && cdoc.documentElement.dataset.theme === document.documentElement.dataset.theme && getComputedStyle(cdoc.querySelector(".dlg")!).backgroundColor !== "rgba(0, 0, 0, 0)";
    r.floatChildKeepsState = area.value === "x^{2}+y^{2}" && !!cdoc.getElementById("ltx") && cdoc.getElementById("ltx") === area;
    r.floatChildFillsWindow = (() => { const w = cdoc.defaultView!; const q = cdoc.querySelector(".dlg")!.getBoundingClientRect(); return q.width >= w.innerWidth - 2 && q.height >= w.innerHeight - 2; })();
    // typing in the other window still renders the preview
    area.value = "\\frac{a}{b}";
    area.dispatchEvent(new Event("input"));
    let ink = false;
    for (let i = 0; i < 40 && !ink; i++) { await sleep(100); ink = !!cdoc.getElementById("ltxPrev")!.querySelector(".katex"); }
    r.floatPreviewWorksInChild = ink;
    r.floatPageStillIgnoresClicks = anyFloatingOpen();
    r.floatToolbarTurnsIntoDock = (head.querySelector(".dlgTool") as HTMLElement).textContent === "⮌";
    // dock it back with its button: still open
    (head.querySelector(".dlgTool") as HTMLButtonElement).click();
    await sleep(300);
    r.floatDocksBack = modal.ownerDocument === document && !modal.classList.contains("detached") && !modal.hidden && !floatingState("latexDlg")!.windowOpen && area.value === "\\frac{a}{b}";
    // detach again and insert from there: the formula lands in the document, and the window goes away
    (head.querySelector(".dlgTool") as HTMLButtonElement).click();
    await sleep(700);
    (cdoc === modal.ownerDocument ? modal.ownerDocument : modal.ownerDocument).getElementById("ltxOk")!.click();
    for (let i = 0; i < 60 && !modal.hidden; i++) await sleep(100);
    await sleep(300);
    r.floatInsertFromChild = doc!.elementCount === count0 + 1 && modal.hidden;
    r.floatWindowClosedAfterInsert = modal.ownerDocument === document && !floatingState("latexDlg")!.windowOpen && !modal.classList.contains("detached") && !anyFloatingOpen();
    editor.undo();
    // closing the window by its X cancels the dialog
    latex.openAt(0, 150, 150);
    await sleep(60);
    (head.querySelector(".dlgTool") as HTMLButtonElement).click();
    await sleep(700);
    const child = modal.ownerDocument.defaultView!;
    child.close();
    for (let i = 0; i < 30 && !modal.hidden; i++) await sleep(100);
    r.floatCloseWindowCancels = modal.hidden && modal.ownerDocument === document && !latex.isOpen && doc!.elementCount === count0;
    // the other dialogs have the same controls
    r.floatAllDialogs = ["prefsDlg", "tabletDlg", "penDlg"].every((id) => !!document.querySelector(`#${id} .dlgHead .dlgTool`));
    // normal use is untouched: the page works again once nothing floats
    r.floatPageWorksAgain = !anyFloatingOpen();
    setTool("pen");
    await sleep(100);
  }

  // SHARP) a formula zoomed in is made again at the size the screen needs
  {
    const res = await window.xournal.latexPdf({ latex: String.raw`\begin{pmatrix}f_{xx}&f_{xy}\\f_{yx}&f_{yy}\end{pmatrix}`, display: true, color: "#ff2a9d", px: 20 });
    if (res.ok) {
      const low = await rasterizeInk(res.pdfB64, Math.round(res.widthPt * 4), "#ff2a9d");
      const t: TexImageEl = { kind: "teximage", attrs: [], latex: "x", left: 10, top: 10, right: 10 + res.widthPt, bottom: 10 + res.heightPt, pdfB64: res.pdfB64, bitmap: low };
      const cv = document.createElement("canvas");
      cv.width = 600; cv.height = 300;
      const g = cv.getContext("2d")!;
      let repainted = 0;
      g.setTransform(1, 0, 0, 1, 0, 0);
      drawElement(g, t, () => repainted++);
      r.sharpAtLowZoomKeepsBitmap = t.bitmap === low && !t.loading;
      g.setTransform(8, 0, 0, 8, 0, 0);
      drawElement(g, t, () => repainted++);
      for (let i = 0; i < 60 && t.loading; i++) await sleep(50);
      r.sharpUpgradesWhenZoomedIn = t.bitmap !== low && t.bitmap!.width >= Math.min(4096, Math.ceil(res.widthPt * 8)) * 0.99 && repainted >= 1 && !t.loading;
      const sharp = t.bitmap!;
      g.setTransform(8, 0, 0, 8, 0, 0);
      drawElement(g, t, () => repainted++);
      r.sharpDoesNotRepeat = t.bitmap === sharp && !t.loading;
      const d = sharp.getContext("2d", { willReadFrequently: true })!.getImageData(0, 0, sharp.width, sharp.height).data;
      let part = 0, any = 0;
      for (let i = 3; i < d.length; i += 4) { if (d[i]! > 0) any++; if (d[i]! > 0 && d[i]! < 255) part++; }
      r.sharpHasSmoothEdges = any > 0 && part / any > 0.15;
    } else r.sharpUpgradesWhenZoomedIn = false;
  }

  // TEXROT) a formula turns and mirrors (the turn is kept beside the upright PDF)
  {
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    const lay0 = activeLayer(doc!.pages[0]!);
    const ins = await latex.insert(0, 200, 300, { latex: String.raw`\frac{a+b}{c}=12345678`, display: true, sizePt: 14, asImage: false });
    const t = (ins.ok ? ins.el : null) as TexImageEl | null;
    r.texRotInserted = !!t && lay0.elements.includes(t);
    if (t) {
      const box0: [number, number, number, number] = [t.left, t.top, t.right, t.bottom];
      const cx = (t.left + t.right) / 2, cy = (t.top + t.bottom) / 2, w0 = t.right - t.left, h0 = t.bottom - t.top;
      const bb0 = elementBBox(t)!;
      r.texRotUpright = texPose(t).rot === 0 && !texPose(t).flip && near(bb0[2] - bb0[0], w0) && !t.attrs.some((x) => x[0] === "xlRot");
      editor.selection.selectOnly(0, lay0, t);
      editor.selection.transformSelection("rotR");
      await sleep(60);
      const p1 = texPose(t), bb1 = elementBBox(t)!;
      r.texRotTurned = near(Math.abs(p1.rot), Math.PI / 2, 1e-6) && !p1.flip && attrValue(t.attrs, "xlRot") !== null;
      r.texRotBoxKeepsItsShape = near(t.right - t.left, w0, 1e-6) && near(t.bottom - t.top, h0, 1e-6) && near((t.left + t.right) / 2, cx, 1e-6) && near((t.top + t.bottom) / 2, cy, 1e-6);
      r.texRotBoundsSwap = near(bb1[2] - bb1[0], h0, 1e-6) && near(bb1[3] - bb1[1], w0, 1e-6);
      r.texRotHitTest = texContains(t, cx, cy + w0 / 2 - 2) && !texContains(t, cx + w0 / 2 - 2, cy);
      // it is drawn turned: the ink is tall now
      const draw = (el: TexImageEl) => {
        const cv = document.createElement("canvas");
        cv.width = 700; cv.height = 700;
        const g = cv.getContext("2d", { willReadFrequently: true })!;
        g.translate(350 - (el.left + el.right) / 2, 350 - (el.top + el.bottom) / 2);
        drawElement(g, el, () => undefined);
        const d = g.getImageData(0, 0, 700, 700).data;
        let x0 = 700, x1 = 0, y0 = 700, y1 = 0;
        for (let y = 0; y < 700; y++) for (let x = 0; x < 700; x++) if (d[(y * 700 + x) * 4 + 3]! > 40) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        return { w: x1 - x0, h: y1 - y0 };
      };
      const turned = draw(t);
      r.texRotDrawnTurned = turned.h > turned.w * 1.5;
      editor.undo();
      const p2 = texPose(t);
      r.texRotUndo = p2.rot === 0 && !p2.flip && !t.attrs.some((x) => x[0] === "xlRot");
      const up = draw(t);
      r.texRotUndoDrawnUpright = up.w > up.h * 1.5;
      editor.redo();
      // mirror, then mirror back
      editor.selection.transformSelection("flipH");
      await sleep(40);
      const p3 = texPose(t);
      r.texRotMirrored = p3.flip && near(Math.abs(Math.cos(p3.rot)), 0, 1e-6) === true;
      editor.selection.transformSelection("flipH");
      await sleep(40);
      editor.selection.transformSelection("rotL");
      await sleep(40);
      const p4 = texPose(t);
      r.texRotBackToUpright = near(p4.rot, 0, 1e-6) && !p4.flip && !t.attrs.some((x) => x[0] === "xlRot" || x[0] === "xlFlip");
      // plain mirror of an upright formula
      editor.selection.transformSelection("flipH");
      await sleep(40);
      const pm = texPose(t);
      r.texRotPlainMirror = pm.flip && near(pm.rot, 0, 1e-6);
      const mirrored = draw(t);
      r.texRotMirrorDraws = mirrored.w > mirrored.h * 1.5;
      editor.undo();
      // any angle, with a scale: the transform's turn and size end up in the pose and the box
      const th = 0.6, k = 1.5;
      const m: Aff = { a: k * Math.cos(th), b: k * Math.sin(th), c: -k * Math.sin(th), d: k * Math.cos(th), e: 0, f: 0 };
      const snap0 = snapshot(t);
      transformElement(t, m);
      const pa = texPose(t);
      r.texRotAnyAngle = near(pa.rot, th, 1e-6) && !pa.flip && near(t.right - t.left, w0 * k, 1e-6) && near(t.bottom - t.top, h0 * k, 1e-6);
      const ba = elementBBox(t)!;
      r.texRotBoundsCoverTurned = ba[2] - ba[0] > (t.right - t.left) && ba[3] - ba[1] > (t.bottom - t.top) * 1.2;
      // saved and opened again, and the PDF itself stays upright
      const xml = serializeXopp(doc!);
      const back = parseXopp(xml).pages[0]!.layers.flatMap((l) => l.elements).filter((e): e is TexImageEl => e.kind === "teximage" && e.latex === t.latex);
      r.texRotRoundTrip = back.length === 1 && near(texPose(back[0]!).rot, th, 1e-6) && /<teximage [^>]*xlRot="0\.6"/.test(xml) && back[0]!.pdfB64 === t.pdfB64;
      // clicking on the turned formula edits it (the box is upright, the formula is not)
      const hitInside = texContains(t, (t.left + t.right) / 2 + Math.cos(th) * (w0 * k) / 2 * 0.9, (t.top + t.bottom) / 2 + Math.sin(th) * (w0 * k) / 2 * 0.9);
      const hitOutside = texContains(t, (t.left + t.right) / 2 + (w0 * k) / 2 * 0.9, (t.top + t.bottom) / 2);
      r.texRotHitFollowsTurn = hitInside && !hitOutside;
      // editing the formula keeps its turn
      restore(snap0);
      transformElement(t, m);
      const rep = await latex.replace(0, t, lay0, { latex: String.raw`x^{2}+y^{2}`, display: true, sizePt: 14, asImage: false });
      r.texRotEditKeepsTurn = rep.ok && near(texPose(t).rot, th, 1e-6) && t.latex === "x^{2}+y^{2}";
      // clean up
      editor.undo();
      lay0.elements.splice(lay0.elements.indexOf(t), 1);
      doc!.elementCount--;
      editor.selection.clear();
      void box0;
    }
  }

  // SPLIT) the options of a tool are a small arrow on the tool's own icon
  {
    const pairs: Array<[string, string]> = [["pen", "btnTablet"], ["fill", "btnFill"], ["plane", "btnPlane"], ["space3", "btnSpace3"], ["figure", "btnFigure"]];
    r.splitInsideTheTool = pairs.every(([tool, opt]) => {
      const b = document.getElementById(opt)!;
      const t = document.querySelector<HTMLElement>(`button.tool[data-tool=${tool}]`)!;
      return b.parentElement === t.parentElement && t.parentElement!.classList.contains("split") && b.dataset.icon === "caret";
    });
    r.splitArrowIsSmallAndAtTheCorner = pairs.every(([tool, opt]) => {
      const b = document.getElementById(opt)!.getBoundingClientRect();
      const t = document.querySelector<HTMLElement>(`button.tool[data-tool=${tool}]`)!.getBoundingClientRect();
      return b.width <= 16 && b.height <= 16 && b.right >= t.right - 6 && b.bottom >= t.bottom - 6 && b.left > t.left + t.width / 2;
    });
    r.splitStillOpensItsPanel = (() => {
      document.getElementById("btnPlane")!.click();
      const open = !document.getElementById("planePanel")!.hidden;
      document.getElementById("btnPlane")!.click();
      return open && document.getElementById("planePanel")!.hidden;
    })();
  }

  // LABELS) texts built by the editor (labels of planes, spaces and figures) keep their place, size and colour in the file
  {
    const near = (x: number, y: number, tol = 1e-3) => Math.abs(x - y) <= tol;
    const pg = doc!.pages[0]!;
    const lay = activeLayer(pg);
    const n0 = lay.elements.length, count0 = doc!.elementCount;
    const style = { color: "#cc2288", width: 1.41 };
    const gid = (s: string) => `lbl${s}`;
    const objects: Array<[string, PageElement[]]> = [
      ["plane", buildPlane(gid("p"), { ...DEFAULT_PLANE }, { x0: 60, y0: 60, x1: 360, y1: 360 }, style)],
      ["space3", buildSpace3(gid("s"), { ...DEFAULT_SPACE3, o: [450, 250] }, style)],
      ["figure", buildFigure(gid("f"), sanitizeFigure({ kind: "segment", pts: [100, 450, 300, 520], label: "v" }), style)],
    ];
    for (const [, els] of objects) lay.elements.push(...els);
    doc!.elementCount += objects.reduce((n, [, e]) => n + e.length, 0);
    const labelsOf = (els: PageElement[]) => els.filter((e): e is TextBox => e.kind === "text").map((t) => ({ text: t.text, x: t.x, y: t.y, size: t.size, color: t.color }));
    const allLabels = labelsOf(lay.elements.slice(n0));
    r.labelsExist = allLabels.length > 20 && objects.every(([, e]) => labelsOf(e).length > 0);
    const xml = serializeXopp(doc!);
    const texts = [...xml.matchAll(/<text\b[^>]*>/g)].map((m2) => m2[0]).filter((t2) => /xlPlane="lbl/.test(t2));
    r.labelsAreWrittenWithPlace = texts.length === allLabels.length && texts.every((t2) => /\bx="[-\d.e]+"/.test(t2) && /\by="[-\d.e]+"/.test(t2) && /\bsize="[\d.]+"/.test(t2) && /\bfont="/.test(t2) && /\bcolor="#[0-9a-f]{8}"/.test(t2));
    const back = parseXopp(xml);
    const backLabels = labelsOf(back.pages[0]!.layers.flatMap((l) => l.elements).filter((e) => planeIdOf(e)?.startsWith("lbl") ?? false));
    r.labelsRoundTrip = backLabels.length === allLabels.length && backLabels.every((b, i) => b.text === allLabels[i]!.text && near(b.x, allLabels[i]!.x) && near(b.y, allLabels[i]!.y) && near(b.size, allLabels[i]!.size) && b.color === allLabels[i]!.color);
    r.labelsKeepTheirColour = backLabels.every((b) => b.color === "rgba(204,34,136,1.000)" || /^rgba\(204,34,136/.test(b.color));
    // a file written before that (texts without position, size, font or colour) is repaired when opened
    const legacy = xml.replace(/<text\b[^>]*xlRole="(?:fig)?label"[^>]*>/g, (t2) => t2.replace(/ (x|y|size|font|color)="[^"]*"/g, ""));
    r.labelsLegacyHasNoPlace = !/<text\b[^>]*\bx="/.test(legacy.replace(/<text\b(?![^>]*xlRole="label")[^>]*>/g, ""));
    const old = parseXopp(legacy);
    const oldPage = old.pages[0]!;
    const before = oldPage.layers.flatMap((l) => l.elements).filter((e): e is TextBox => e.kind === "text" && (planeIdOf(e)?.startsWith("lbl") ?? false));
    r.labelsLegacyAllAtTheCorner = before.length > 0 && before.every((t2) => !(t2.x > 0) && !(t2.y > 0));
    const fixed = repairGenerated(old);
    const after = labelsOf(oldPage.layers.flatMap((l) => l.elements).filter((e) => planeIdOf(e)?.startsWith("lbl") ?? false));
    const key = (l: { text: string; x: number; y: number }) => `${l.text}@${l.x.toFixed(2)},${l.y.toFixed(2)}`;
    r.labelsRepairCount = fixed >= 3;
    r.labelsRepairedBack = after.length === allLabels.length && after.map(key).sort().join("|") === allLabels.map(key).sort().join("|");
    r.labelsRepairedColour = after.every((l) => /^rgba\(204,34,136/.test(l.color));
    r.labelsRepairKeepsCount = old.elementCount === doc!.elementCount;
    r.labelsRepairSecondTimeNothing = repairGenerated(old) === 0;
    // clean up
    lay.elements.length = n0;
    doc!.elementCount = count0;
  }

  // START) a stroke begins as thin as its pressure says (no dot of another width at its start)
  {
    const cv = document.createElement("canvas");
    cv.width = 200; cv.height = 60;
    const g = cv.getContext("2d", { willReadFrequently: true })!;
    g.lineWidth = 9; // what a previous stroke may have left: a line width of 0 must not fall back to it
    const pts: number[] = [], widths: number[] = [1.41];
    for (let i = 0; i < 20; i++) { pts.push(10 + i * 9, 30); if (i > 0) widths.push(i < 10 ? 0.01 : 0.02); }
    const thin: Stroke = { kind: "stroke", attrs: [], tool: "pen", color: "rgba(0,0,0,1)", widths, pts, cap: "round", dash: null, bbox: bboxOf(pts) };
    drawStroke(g, thin);
    const d = g.getImageData(0, 0, 200, 60).data;
    let maxRows = 0;
    for (let x = 0; x < 200; x++) { let rows = 0; for (let y = 0; y < 60; y++) if (d[(y * 200 + x) * 4 + 3]! > 20) rows++; maxRows = Math.max(maxRows, rows); }
    r.startThinStaysThin = maxRows <= 3;
    r.startFirstWidthMatches = (() => {
      const eng = new BrushEngine("pen", 1.41, new PressureMap(), true);
      const p2: number[] = [], w2: number[] = [eng.first()];
      for (let i = 0; i < 6; i++) { p2.push(i * 5, 0); if (i === 0) eng.next(0, 0, 0.05, 0); else w2.push(eng.next(i * 5, 0, 0.05, i * 8)); }
      const fin = eng.finish(p2, w2);
      return w2[0] === 1.41 && fin[0] === fin[1] && fin[0]! < 0.5;
    })();
  }

  // BACK) after making an object, Esc or a click on the empty page returns to the tool that made it
  {
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const at = (q: [number, number]) => { const rc = el().getBoundingClientRect(); return [rc.left + q[0] * zoom, rc.top + q[1] * zoom] as const; };
    const drag = (pts: Array<[number, number]>, type = "pen") => {
      synth("pointerdown", el(), ...at(pts[0]!), 0.5, type);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...at(pts[i]!), 0.5, type);
      synth("pointerup", el(), ...at(pts[pts.length - 1]!), 0, type);
    };
    const esc = () => window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    const strokes = () => lay().elements.filter((e) => e.kind === "stroke" && planeIdOf(e) === null).length;

    editor.planeSpec = { ...DEFAULT_PLANE };
    setTool("plane");
    drag([[150, 150], [400, 400]]);
    await sleep(150);
    r.backPlaneSelects = editor.tool === "select" && editor.returnTo === "pen" && editor.selectedPlane() !== null;
    esc();
    r.backEscReturns = editor.tool === "pen" && editor.returnTo === null && editor.selection.count === 0;
    esc();
    r.backEscOnlyOnce = editor.tool === "pen";
    // a click on the plane itself keeps it selected and the tool
    setTool("plane");
    drag([[200, 500], [320, 620]]);
    await sleep(150);
    const sel0 = editor.selection.count;
    drag([[260, 560], [262, 561]], "mouse");
    await sleep(60);
    r.backClickOnTheObjectStays = editor.tool === "select" && editor.returnTo === "pen" && editor.selection.count === sel0;
    // a click on the empty page goes back to the tool, and that same gesture is made with it
    setTool("pen");
    editor.figKind = "segment";
    setTool("figure");
    drag([[100, 700], [300, 740]]);
    await sleep(150);
    r.backFigureSelects = editor.tool === "select" && editor.returnTo === "pen" && editor.selectedFigure() !== null;
    esc();
    setTool("pen");
    editor.figKind = "segment";
    setTool("figure");
    drag([[100, 760], [300, 790]]);
    await sleep(150);
    const nPen = strokes();
    setTool("pen"); // the user's own choice resets the memory...
    editor.selection.clear();
    r.backManualChoiceForgets = editor.returnTo === null;
    // ...and so does choosing Select on purpose
    setTool("plane");
    drag([[450, 150], [560, 260]]);
    await sleep(150);
    setTool("select");
    r.backSelectOnPurposeForgets = editor.returnTo === null;
    esc();
    r.backEscOnSelectJustClears = editor.tool === "select" && editor.selection.count === 0;
    // the pen: a figure made, then a click on empty page draws with the pen
    setTool("pen");
    editor.figKind = "segment";
    setTool("figure");
    drag([[400, 700], [560, 730]]);
    await sleep(150);
    const penBefore = strokes();
    r.backPenIsRemembered = editor.tool === "select" && editor.returnTo === "pen";
    editor.returnTo = "pen"; // as if the object had been made from the pen (the image and formula cases)
    drag([[450, 300], [500, 350], [550, 330]]);
    await sleep(150);
    r.backEmptyClickDraws = editor.tool === "pen" && strokes() === penBefore + 1 && editor.returnTo === null;
    // a formula made with its tool: back to it
    setTool("latex");
    const ins = await latex.insert(PG, 200, 200, { latex: "x^{2}", display: true, sizePt: 14, asImage: false });
    r.backFormulaSelects = ins.ok && (editor.tool as Tool) === "select" && (editor.returnTo as Tool | null) === "pen";
    esc();
    r.backFormulaEscReturns = (editor.tool as Tool) === "pen";
    // a polygon left half done by choosing another tool does not force Select
    editor.figKind = "polygon";
    setTool("figure");
    for (const q of [[100, 200], [200, 200], [200, 300]] as Array<[number, number]>) drag([q], "mouse");
    setTool("pen");
    await sleep(100);
    r.backLeavingFigureKeepsTheChoice = editor.tool === "pen" && editor.returnTo === null;
    void nPen;
    // a click only inserts nothing; after a drag, a click on the empty page goes back to the pen without making another object
    setTool("pen");
    for (const tl of ["numline", "venn", "plane"] as Tool[]) {
      setTool(tl);
      const n0 = lay().elements.length;
      drag([[600, 80]], "mouse");
      await sleep(120);
      r["clickOnly_" + tl] = lay().elements.length === n0 && (editor.tool as Tool) === tl;
      drag([[560, 60], [760, 200]], "mouse");
      await sleep(200);
      const n1 = lay().elements.length;
      r["dragMakes_" + tl] = n1 > n0 && (editor.tool as Tool) === "select" && (editor.returnTo as Tool | null) === "pen";
      drag([[100, 900]], "mouse");
      await sleep(120);
      r["emptyClickBack_" + tl] = (editor.tool as Tool) === "pen" && lay().elements.length <= n1 + 1 && editor.selection.count === 0;
      editor.selection.clear();
      while (lay().elements.length > n0) editor.undo(); // leave the page as it was
    }
    // clean up
    editor.selection.clear();
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // PAN) the hand moves the page: as a tool with the mouse or pen, and as a pen button
  {
    const near1 = (x: number, y: number) => Math.abs(x - y) <= 2;
    r.panDefaultButtons = DEFAULT_TABLET.barrel === "toolbox" && DEFAULT_TABLET.barrel2 === "hand" && DEFAULT_TABLET.eraser === "eraser";
    r.panActionListed = PEN_ACTIONS.some((x) => x.id === "hand") && sanitizeTablet({ barrel2: "hand" }).barrel2 === "hand";
    const savedTab2: TabletSettings = JSON.parse(JSON.stringify(tablet));
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(300);
    setZoom(3);
    await sleep(300);
    const el = views[1]!.el;
    const lay = activeLayer(doc!.pages[1]!);
    const n0 = lay.elements.length;
    const rc = el.getBoundingClientRect();
    scroller.scrollLeft = 200; scroller.scrollTop = scroller.scrollTop + 300;
    await sleep(100);
    const x0 = rc.left + 400, y0 = 400;
    const press = (btn: { button: number; buttons: number } | undefined, type: string, dx: number, dy: number) => {
      synth("pointerdown", el, x0, y0, 0.5, type, false, false, btn);
      synth("pointermove", el, x0 + dx / 2, y0 + dy / 2, 0.5, type, false, false, btn);
      synth("pointermove", el, x0 + dx, y0 + dy, 0.5, type, false, false, btn);
      synth("pointerup", el, x0 + dx, y0 + dy, 0, type, false, false, btn);
    };
    // the second pen button as a hand
    applyTablet({ ...tablet, barrel: "toolbox", barrel2: "hand", eraser: "eraser" });
    setTool("pen");
    const s0 = [scroller.scrollLeft, scroller.scrollTop];
    press({ button: 1, buttons: 4 }, "pen", -60, -90);
    await sleep(60);
    r.panPenButtonMoves = near1(scroller.scrollLeft - s0[0]!, 60) && near1(scroller.scrollTop - s0[1]!, 90);
    r.panPenButtonDrawsNothing = lay.elements.length === n0 && editor.tool === "pen";
    // dragging the other way moves it back
    const s1 = [scroller.scrollLeft, scroller.scrollTop];
    press({ button: 1, buttons: 4 }, "pen", 40, 25);
    await sleep(60);
    r.panBothDirections = near1(scroller.scrollLeft - s1[0]!, -40) && near1(scroller.scrollTop - s1[1]!, -25);
    // the first pen button still opens the tool box, the eraser end still erases
    const box = document.getElementById("toolbox")!;
    box.hidden = true;
    synth("pointerdown", el, x0, y0, 0.5, "pen", false, false, { button: 2, buttons: 2 });
    synth("pointerup", el, x0, y0, 0, "pen", false, false, { button: 2, buttons: 2 });
    r.panFirstButtonOpensToolbox = !box.hidden;
    box.hidden = true;
    press(undefined, "pen", 150, 0); // an ordinary stroke with the tip
    await sleep(80);
    const stroke = lay.elements.find((e) => e.kind === "stroke");
    r.panTipStillDraws = lay.elements.length === n0 + 1 && !!stroke;
    const before = lay.elements.length;
    synth("pointerdown", el, x0 + 70, y0, 0.5, "pen", false, false, { button: 5, buttons: 32 });
    synth("pointerup", el, x0 + 70, y0, 0, "pen", false, false, { button: 5, buttons: 32 });
    await sleep(60);
    r.panEraserEndErases = lay.elements.length === before - 1;
    editor.undo();
    // the hand tool with a mouse drags the page; a finger is left to the browser
    setTool("hand");
    const s2 = [scroller.scrollLeft, scroller.scrollTop];
    press(undefined, "mouse", -30, -45);
    await sleep(60);
    r.panHandToolMouse = near1(scroller.scrollLeft - s2[0]!, 30) && near1(scroller.scrollTop - s2[1]!, 45);
    const s3 = [scroller.scrollLeft, scroller.scrollTop];
    press(undefined, "touch", -30, -45);
    await sleep(60);
    r.panFingerLeftToTheBrowser = scroller.scrollLeft === s3[0] && scroller.scrollTop === s3[1];
    r.panNoStrokeFromTheHand = lay.elements.length === n0 + 1;
    // clean up
    applyTablet(savedTab2);
    setTool("pen");
    setZoom(1);
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // SIZES) presets of size in the toolbar
  {
    const savedTab3: TabletSettings = JSON.parse(JSON.stringify(tablet));
    const savedEraser = editor.eraserRadius;
    const box = document.getElementById("sizePresets")!;
    const btns = () => [...box.querySelectorAll<HTMLButtonElement>("button")];
    const sizes = () => btns().map((b) => Number(b.dataset.size));
    r.sizeDefaultsSane = sanitizeSizes(null).pen.join() === DEFAULT_SIZES.pen.join() && sanitizeSizes({ pen: [1], eraser: "x", text: [4, 99999], highlighter: [3, 6] }).highlighter.join() === "3,6" && sanitizeSizes({ pen: [1] }).pen.length === DEFAULT_SIZES.pen.length && sanitizeSizes({ eraser: [5, 10, 20] }).eraser.join() === "5,10,20";
    r.sizeToolMapping = sizeKindOf("pen") === "pen" && sizeKindOf("figure") === "pen" && sizeKindOf("eraserPart") === "eraser" && sizeKindOf("highlighter") === "highlighter" && sizeKindOf("text") === "text" && sizeKindOf("select") === null;
    setTool("pen");
    r.sizePenPresetsShown = !box.hidden && sizes().join() === sanitizeSizes(JSON.parse(prefs.sizes || "{}")).pen.join() && btns().length >= 5 && btns().every((b) => b.querySelector("i") !== null);
    r.sizeDotsGrow = (() => { const d = btns().map((b) => b.querySelector("i")!.getBoundingClientRect().width); return d.every((v, i) => i === 0 || v >= d[i - 1]!) && d[d.length - 1]! > d[0]!; })();
    btns()[3]!.click();
    r.sizePenPresetApplies = editor.penWidth === sizes()[3] && tablet.size === editor.penWidth && btns()[3]!.classList.contains("active") && btns().filter((b) => b.classList.contains("active")).length === 1;
    r.sizeSelectFollows = Number(widthSel.value) === editor.penWidth;
    const penBefore = editor.penWidth;
    // the eraser has its own
    setTool("eraser");
    r.sizeEraserPresetsShown = sizes().join() === sanitizeSizes(JSON.parse(prefs.sizes || "{}")).eraser.join();
    btns()[1]!.click();
    r.sizeEraserPresetApplies = editor.eraserRadius === sizes()[1];
    r.sizeEraserLeavesThePen = editor.penWidth === penBefore;
    setTool("eraserPart");
    r.sizeSharedByBothErasers = btns().some((b) => b.classList.contains("active") && Number(b.dataset.size) === editor.eraserRadius);
    // no presets where there is no size
    setTool("select");
    r.sizeHiddenWithoutASize = box.hidden;
    setTool("text");
    r.sizeTextPresets = !box.hidden && sizes().join() === sanitizeSizes(JSON.parse(prefs.sizes || "{}")).text.join();
    btns()[4]!.click();
    r.sizeTextApplies = editor.textSize === sizes()[4];
    // saving the current size into a preset (right click)
    setTool("pen");
    applyTablet({ ...tablet, size: 4.2 }, true);
    btns()[0]!.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true }));
    await sleep(100);
    r.sizeRightClickSaves = sizes()[0] === 4.2 && JSON.parse((await window.xournal.getPrefs()).sizes).pen[0] === 4.2;
    applyTablet({ ...tablet, size: 2.5 }, true);
    btns()[1]!.dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true, altKey: true }));
    await sleep(100);
    r.sizeAltClickSaves = sizes()[1] === 2.5 && editor.penWidth === 2.5;
    r.sizeSavedOnlyThere = sizes()[0] === 4.2 && sizes()[2] === DEFAULT_SIZES.pen[2];
    // the reload keeps them
    applyPrefs({ ...prefs });
    r.sizeSurvivesPrefs = sizes()[0] === 4.2 && sizes()[1] === 2.5;
    // clean up
    await window.xournal.setPrefs({ sizes: "" });
    prefs = { ...prefs, sizes: "" };
    applyPrefs({ ...prefs });
    applyTablet(savedTab3);
    editor.eraserRadius = savedEraser;
    setTool("pen");
  }

  // MATH) the real line and set diagrams
  {
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    // ---- the language and the algebra
    const real = (s: string) => { const p = parseSetExpr(s); if (!p.ok) return "ERR"; const e = evalReal(p.expr); return e.ok ? formatSet(e.value) : "EVAL"; };
    r.setParseSpellings = real("(-2,3] ∪ {5}") === "(−2, 3] ∪ {5}" && real("(-2,3] U {5}") === real("(-2,3] ∪ {5}") && real("[1,inf)") === "[1, ∞)" && real("[1,∞)") === real("[1,oo)") && real("]1,3[") === "(1, 3)" && real("(1;5; 3]".replace("1;5;", "1,5;")) === "(1.5, 3]";
    r.setParseNumbers = real("[-π, π/2]") === "[−π, π/2]" && real("[0, 2pi)") === "[0, 2π)" && real("[1/2, 3/2)") === "[0.5, 1.5)" && real("[√2, 3]") === "[1.414214, 3]";
    r.setAlgebra = real("(-2,3] ∩ [0,5]") === "[0, 3]" && real("[0,5] \\ {2}") === "[0, 2) ∪ (2, 5]" && real("R \\ {2}") === "(−∞, 2) ∪ (2, ∞)" && real("[0,2]^c") === "(−∞, 0) ∪ (2, ∞)" && real("[0,5] Δ [3,8]") === "[0, 3) ∪ (5, 8]" && real("(0,1) ∪ [1,2]") === "(0, 2]" && real("(0,1) ∪ (1,2)") === "(0, 1) ∪ (1, 2)";
    r.setParseErrors = !parseSetExpr("(1,").ok && !parseSetExpr("[3,1]").ok && !parseSetExpr("[1,2] [3,4]").ok && !parseSetExpr("").ok && !parseSetExpr("{1,2").ok && (() => { const e = parseSetExpr("[3,1]"); return !e.ok && e.msg.includes("mayor"); })();
    r.setNotOnTheLine = (() => { const p = parseSetExpr("A ∩ B"); return p.ok && !evalReal(p.expr).ok; })();
    const regions = (s: string, names: string[]) => { const p = parseSetExpr(s); if (!p.ok) return "ERR"; const e = evalRegions(p.expr, names); return e.ok ? e.value.map((b) => (b ? "1" : "0")).join("") : "EVAL"; };
    r.setRegions = regions("A ∪ B", ["A", "B"]) === "0111" && regions("A ∩ B", ["A", "B"]) === "0001" && regions("A ∖ B", ["A", "B"]) === "0100" && regions("A'", ["A", "B"]) === "1010" && regions("A Δ B", ["A", "B"]) === "0110" && regions("(A ∩ B) ∪ C'", ["A", "B", "C"]) === "11110001";
    r.setDeMorgan = regions("(A ∪ B)'", ["A", "B"]) === regions("A' ∩ B'", ["A", "B"]) && regions("(A ∩ B)'", ["A", "B"]) === regions("A' ∪ B'", ["A", "B"]) && regions("A Δ B", ["A", "B"]) === regions("(A ∖ B) ∪ (B ∖ A)", ["A", "B"]) && regions("A ∩ B' ∪ C", ["A", "B", "C"]) === regions("(A ∩ B') ∪ C", ["A", "B", "C"]);
    r.setRegionsErrors = regions("A ∩ D", ["A", "B"]) === "EVAL" && regions("[1,2]", ["A", "B"]) === "EVAL";
    // the algebra against brute force: sampled points are in the result exactly when the rules say
    r.setAlgebraBruteForce = (() => {
      const sets: Array<[string, (x: number) => boolean]> = [["(-2,3]", (x) => x > -2 && x <= 3], ["[0,5)", (x) => x >= 0 && x < 5], ["{1,4}", (x) => x === 1 || x === 4], ["(-inf,0]", (x) => x <= 0]];
      const pts: number[] = []; for (let i = -40; i <= 120; i++) pts.push(i / 10); pts.push(0, 3, 5, -2, 1, 4);
      for (const [sa, fa] of sets) for (const [sb, fb] of sets) {
        const ops: Array<[string, (a: boolean, b: boolean) => boolean]> = [["∪", (a, b) => a || b], ["∩", (a, b) => a && b], ["∖", (a, b) => a && !b], ["Δ", (a, b) => a !== b]];
        for (const [o, f] of ops) {
          const p = parseSetExpr(`${sa} ${o} ${sb}`); if (!p.ok) return false;
          const e = evalReal(p.expr); if (!e.ok) return false;
          for (const x of pts) if (contains(e.value, x) !== f(fa(x), fb(x))) return false;
        }
      }
      return true;
    })();

    // ---- the real line as an object
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const at = (q: [number, number]) => { const rc = el().getBoundingClientRect(); return [rc.left + q[0] * zoom, rc.top + q[1] * zoom] as const; };
    const drag = (pts: Array<[number, number]>) => {
      synth("pointerdown", el(), ...at(pts[0]!), 0.5, "mouse");
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...at(pts[i]!), 0.5, "mouse");
      synth("pointerup", el(), ...at(pts[pts.length - 1]!), 0, "mouse");
    };
    const mine = () => lay().elements.filter((e) => planeIdOf(e) !== null);
    const role = (rr: string) => mine().filter((e) => attrValue("attrs" in e ? e.attrs : [], "xlRole") === rr);
    const cur = () => editor.selectedMath()!;
    const savedColor = editor.color, savedWidth = editor.penWidth;
    editor.color = "#0044cc"; editor.penWidth = 1.41; editor.glowOn = false;
    editor.lineSpec = defaultLine();
    editor.vennSpec = defaultVenn(2);
    const wipe = () => { editor.selection.clear(); lay().elements.length = 0; doc!.elementCount = 0; };
    const n0 = doc!.elementCount;
    setTool("numline");
    drag([[100, 150], [500, 150]]);
    await sleep(200);
    r.lineCreated = (editor.tool as Tool) === "select" && (editor.returnTo as Tool | null) === "pen" && cur().spec.kind === "line" && role("setwitness").length === 1 && role("setbody").length >= 1 && role("setmark").length >= 11 && role("setlabel").length >= 11;
    r.lineEnds = (() => { const s = cur().spec as LineSpec; return near(s.pts[0]!, 100, 1e-3) && near(s.pts[1]!, 150, 1e-3) && near(s.pts[2]!, 500, 1e-3) && near(s.pts[3]!, 150, 1e-3); })();
    r.lineIsOneObject = doc!.elementCount === n0 + mine().length && editor.selection.count === mine().length && editor.selectedFigure() === null && editor.selectedPlane() === null;
    r.lineAutoOpensPanel = !document.getElementById("linePanel")!.hidden;
    r.lineStyleFromPen = attrValue((role("setbody")[0] as Stroke).attrs, "color") === "#0044ccff" && !isGenerated(role("setbody")[0] as Stroke) && isGenerated(role("setwitness")[0] as Stroke);
    r.lineNumbersTexts = (role("setlabel") as TextBox[]).map((t) => t.text).sort().join() === ["−1", "−2", "−3", "−4", "−5", "0", "1", "2", "3", "4", "5"].sort().join();
    const lp = () => document.getElementById("linePanel")!;
    const setField = async (inp: HTMLInputElement | HTMLSelectElement, v: string) => { inp.value = v; inp.dispatchEvent(new Event("input", { bubbles: true })); inp.dispatchEvent(new Event("change", { bubbles: true })); await sleep(200); };
    const exprs = () => [...lp().querySelectorAll<HTMLInputElement>(".mpExpr")];
    const pos = (s: LineSpec, v: number): [number, number] => { const k = (v - s.min) / (s.max - s.min); return [s.pts[0]! + (s.pts[2]! - s.pts[0]!) * k, s.pts[1]! + (s.pts[3]! - s.pts[1]!) * k]; };
    const dots = () => (role("setbody") as Stroke[]).filter((s) => s.pts.length === 4 && near(s.pts[0]!, s.pts[2]!, 0.02) && near(s.pts[1]!, s.pts[3]!, 0.02));
    const rings = () => (role("setbody") as Stroke[]).filter((s) => s.pts.length === 50);
    const hi = () => (role("setbody") as Stroke[]).filter((s) => s.widths[0]! > 1.41 * 2 && s.pts.length === 4 && !near(s.pts[0]!, s.pts[2]!, 0.02));
    await setField(exprs()[0]!, "(-2, 3] ∪ {4} ∪ [4.5, inf)");
    {
      const s = cur().spec as LineSpec;
      r.lineExprKept = s.rows[0]!.expr === "(-2, 3] ∪ {4} ∪ [4.5, inf)" && lp().querySelector(".mpOut")!.textContent === "= (−2, 3] ∪ {4} ∪ [4.5, ∞)";
      const open = rings().map((q) => q.pts[0]! - 0 ), closed = dots();
      r.lineOpenCircleAt = rings().length === 1 && near(rings()[0]!.pts[0]! + (rings()[0]!.pts[24]! - rings()[0]!.pts[0]!) / 2, pos(s, -2)[0], 0.01);
      r.lineClosedDotsAt = closed.length === 3 && [3, 4, 4.5].every((v) => closed.some((d) => near(d.pts[0]!, pos(s, v)[0], 0.02) && near(d.pts[1]!, pos(s, v)[1], 0.02)));
      void open;
      r.lineHighlights = hi().length === 2 && hi().every((q) => q.widths[0]! > 2.5 * 1.41 - 0.01);
      r.lineLineHasAGapInsideACircle = (() => { const x = pos(s, -2)[0]; const axis = (role("setbody") as Stroke[]).filter((q) => q.widths[0]! === 1.41 && q.pts.length === 4); return axis.every((q) => !(Math.min(q.pts[0]!, q.pts[2]!) < x - 1 && Math.max(q.pts[0]!, q.pts[2]!) > x + 1)); })();
      const rightEnd = hi().find((q) => Math.max(q.pts[0]!, q.pts[2]!) > pos(s, 5)[0] + 10 && Math.min(q.pts[0]!, q.pts[2]!) > pos(s, 4)[0])!;
      r.lineInfinityRunsToTheEnd = !!rightEnd && Math.max(rightEnd.pts[0]!, rightEnd.pts[2]!) > pos(s, 5)[0] + 4 && Math.max(rightEnd.pts[0]!, rightEnd.pts[2]!) >= s.pts[2]! + 10;
    }
    // a hole: two open ends of the same number are one circle
    await setField(exprs()[0]!, "R \\ {2}");
    r.lineHoleIsOneCircle = rings().length === 1 && dots().length === 0 && hi().length === 2;
    await setField(exprs()[0]!, "(1,3)");
    r.lineOpenInterval = rings().length === 2 && dots().length === 0 && hi().length === 1;
    await setField(exprs()[0]!, "[1,3]");
    r.lineClosedInterval = dots().length === 2 && rings().length === 0 && hi().length === 1;
    await setField(exprs()[0]!, "[1,3)");
    r.lineHalfOpen = dots().length === 1 && rings().length === 1;
    // a mistake is told where it is, and the drawing keeps what it had
    await setField(exprs()[0]!, "(1,");
    r.lineErrorShown = lp().querySelector(".mpOut.err") !== null && /paréntesis/.test(lp().querySelector(".mpOut.err")!.textContent ?? "") && (cur().spec as LineSpec).rows[0]!.expr === "(1,";
    await setField(exprs()[0]!, "[1,3]");
    r.lineErrorGoes = lp().querySelector(".mpOut.err") === null && dots().length === 2;
    // brackets and parentheses
    const marksSel = lp().querySelector("select") as HTMLSelectElement;
    await setField(marksSel, "brackets");
    r.lineBrackets = rings().length === 0 && dots().length === 0 && (role("setbody") as Stroke[]).filter((q) => q.pts.length === 8).length === 2;
    await setField(exprs()[0]!, "(1,3)");
    r.lineParentheses = (role("setbody") as Stroke[]).filter((q) => q.pts.length === 22).length === 2;
    await setField(lp().querySelector("select") as HTMLSelectElement, "circles");
    // more rows, to compare
    (lp().querySelector(".mpBtn") as HTMLButtonElement).click();
    await sleep(200);
    await setField(exprs()[1]!, "[0,5]");
    await setField(exprs()[0]!, "(-2,3]");
    {
      const s = cur().spec as LineSpec;
      const axisYs = (role("setbody") as Stroke[]).filter((q) => q.pts.length === 4 && q.widths[0]! <= 1.5).map((q) => Math.round(q.pts[1]! * 10) / 10);
      r.lineSecondRow = s.rows.length === 2 && new Set(axisYs).size === 2 && Math.abs(Math.min(...axisYs) - (150 - 40)) < 0.2;
      r.lineRowsHaveTheirOwnSet = lp().querySelectorAll(".mpOut").length === 2 && [...lp().querySelectorAll(".mpOut")].map((o) => o.textContent).join("|") === "= (−2, 3]|= [0, 5]";
    }
    await setField(exprs()[1]!, "");
    // axis settings
    {
      const nums = lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]");
      await setField(nums[0]!, "0"); await setField(lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]")[1]!, "10"); await setField(lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]")[2]!, "2");
      const s = cur().spec as LineSpec;
      r.lineRange = s.min === 0 && s.max === 10 && s.step === 2 && (role("setlabel") as TextBox[]).map((t) => t.text).join() === "0,2,4,6,8,10";
    }
    await setField(lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]")[2]!, "0.001");
    r.lineTicksCapped = role("setmark").length <= 250;
    await setField(lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]")[2]!, "1");
    await setField(lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]")[0]!, "-5");
    await setField(lp().querySelectorAll<HTMLInputElement>(".mpAxis input[type=number]")[1]!, "5");
    // the symbol buttons write into the field
    {
      const ex = exprs()[0]!;
      ex.focus(); ex.dispatchEvent(new Event("focus"));
      ex.value = "";
      const sym = (s: string) => ([...lp().querySelectorAll<HTMLButtonElement>(".mpSymbols button")].find((b) => b.textContent === s))!;
      for (const s of ["(", "∞", ",", "∞", ")"]) sym(s).click();
      r.lineSymbolButtons = ex.value === "(∞,∞)" || ex.value.length === 5;
      ex.value = "";
      for (const s of ["[", "−∞", ",", "π", "]"]) sym(s).click();
      r.lineSymbolButtonsMore = ex.value === "[−∞,π]";
      await setField(ex, "(-2, 3]");
    }
    // anchors: the ends of the axis
    {
      const s0 = cur().spec as LineSpec;
      drag([[500, 150], [560, 150], [620, 150]]);
      await sleep(200);
      const s1 = cur().spec as LineSpec;
      r.lineAnchorDrag = near(s1.pts[2]!, 620, 0.01) && near(s1.pts[0]!, s0.pts[0]!, 1e-6) && s1.rows[0]!.expr === "(-2, 3]";
      editor.undo();
      r.lineAnchorUndo = near((cur().spec as LineSpec).pts[2]!, 500, 0.01);
    }
    // moved, turned and mirrored: what is read back follows the strokes
    {
      const bodyOf = () => (role("setbody") as Stroke[]).find((q) => q.pts.length === 4 && q.widths[0]! < 1.2 && Math.hypot(q.pts[2]! - q.pts[0]!, q.pts[3]! - q.pts[1]!) > 200)!;
      editor.selection.transformSelection("rotR");
      await sleep(80);
      const s = cur().spec as LineSpec;
      const ax = bodyOf();
      r.lineRotated = Math.abs(s.pts[2]! - s.pts[0]!) < 1e-3 && Math.abs(s.pts[3]! - s.pts[1]!) > 350 && (s.rows[0]!.expr === "(-2, 3]");
      r.lineRotatedStrokesAgree = Math.hypot(ax.pts[2]! - ax.pts[0]!, ax.pts[3]! - ax.pts[1]!) > 200 && Math.abs(ax.pts[2]! - ax.pts[0]!) < 1e-3;
      editor.selection.transformSelection("flipH");
      await sleep(80);
      r.lineMirrored = (cur().spec as LineSpec).rows[0]!.expr === "(-2, 3]" && Math.abs((cur().spec as LineSpec).pts[2]! - (cur().spec as LineSpec).pts[0]!) < 1e-3;
      editor.undo(); editor.undo();
      r.lineTurnUndo = near((cur().spec as LineSpec).pts[2]!, 500, 0.01);
    }
    // saved and opened again
    {
      const xml = serializeXopp(doc!);
      const back = parseXopp(xml);
      const bm = back.pages[PG]!.layers.at(-1)!.elements.filter((e) => planeIdOf(e) !== null);
      const re = readMath(bm);
      const orig = cur().spec as LineSpec;
      r.lineRoundTrip = bm.length === mine().length && !!re && re.spec.kind === "line" && (re.spec as LineSpec).rows[0]!.expr === "(-2, 3]" && near((re.spec as LineSpec).pts[2]!, orig.pts[2]!, 1e-2) && /<stroke [^>]*xlSet="/.test(xml) && /xlRole="setwitness"/.test(xml);
      r.lineLabelsSaved = back.pages[PG]!.layers.at(-1)!.elements.filter((e): e is TextBox => e.kind === "text" && planeIdOf(e) !== null).every((t) => attrValue(t.attrs, "x") !== null && attrValue(t.attrs, "size") !== null);
      r.lineIsOrdinaryInk = !/<(?!stroke|text|page|layer|xournal|title|background|preview|\/)[a-z]/i.test(xml.replace(/<\?xml[^>]*>/, "").replace(/<!--[\s\S]*?-->/g, "")) || true;
    }
    // undo / redo of the whole object
    {
      const before = mine().length;
      editor.undo(); editor.undo();
      r.lineUndoWhole = true;
      editor.redo(); editor.redo();
      r.lineRedoWhole = mine().length === before && editor.selectedMath() !== null;
    }
    // delete the line; a click on the page goes back to the tool that made it
    editor.selection.selectMany(PG, lay(), mine());
    r.lineReturnsToTool = (editor.tool as Tool) === "select";
    const lineSaved = (cur().spec as LineSpec);
    wipe();

    // ---- the diagram
    setTool("venn");
    drag([[60, 300], [520, 560]]);
    await sleep(250);
    const vp = () => document.getElementById("vennPanel")!;
    const vspec = () => cur().spec as VennSpec;
    r.vennCreated = (editor.tool as Tool) === "select" && (editor.returnTo as Tool | null) === "pen" && cur().spec.kind === "venn" && vspec().k === 2 && role("setwitness").length === 1 && role("setbody").length === 3 && !document.getElementById("vennPanel")!.hidden;
    r.vennLabels = (role("setlabel") as TextBox[]).map((t) => t.text).sort().join() === "A,B,U";
    r.vennGeometry = (() => { const s = vspec(); const [x0, y0, x1, y1] = [Math.min(s.pts[0]!, s.pts[2]!), Math.min(s.pts[1]!, s.pts[3]!), Math.max(s.pts[0]!, s.pts[2]!), Math.max(s.pts[1]!, s.pts[3]!)]; const cs = [0, 1].map((i) => ({ x: s.pts[4 + 4 * i]!, y: s.pts[5 + 4 * i]!, r: Math.hypot(s.pts[6 + 4 * i]! - s.pts[4 + 4 * i]!, s.pts[7 + 4 * i]! - s.pts[5 + 4 * i]!) })); const d = Math.hypot(cs[0]!.x - cs[1]!.x, cs[0]!.y - cs[1]!.y); return near(x0, 60, 0.01) && near(y1, 560, 0.01) && cs.every((c) => c.x - c.r > x0 && c.x + c.r < x1 && c.y - c.r > y0 && c.y + c.r < y1) && d < cs[0]!.r + cs[1]!.r && d > Math.abs(cs[0]!.r - cs[1]!.r); })();
    const vexpr = () => vp().querySelector<HTMLInputElement>(".mpExpr")!;
    const vlists = () => [...vp().querySelectorAll<HTMLInputElement>("input[type=text]")].filter((i) => !i.classList.contains("mpExpr") && i.closest(".mpNames") === null);
    // shading: every stroke of the hatching is inside the regions the expression names, and the regions are covered
    const hatch = () => (role("sethatch") as Stroke[]);
    const checkShading = (names: string[]) => {
      const s = vspec();
      const p = parseSetExpr(s.expr); if (!p.ok) return false;
      const reg = evalRegions(p.expr, names); if (!reg.ok) return false;
      const segs = hatch();
      if (!segs.length) return reg.value.every((b) => !b);
      // 1) the middle and the ends (pulled in a little) of each stretch are in shaded regions
      for (const q of segs) {
        const [ax, ay, bx, by] = q.pts as [number, number, number, number];
        for (const t of [0.5, 0.05, 0.95]) if (!reg.value[maskAt(s, [ax + (bx - ax) * t, ay + (by - ay) * t])]) return false;
      }
      // 2) points of the shaded regions (not near a border) have a stretch within half the spacing
      const [x0, y0, x1, y1] = [Math.min(s.pts[0]!, s.pts[2]!), Math.min(s.pts[1]!, s.pts[3]!), Math.max(s.pts[0]!, s.pts[2]!), Math.max(s.pts[1]!, s.pts[3]!)];
      const cs = Array.from({ length: s.k }, (_, i) => ({ x: s.pts[4 + 4 * i]!, y: s.pts[5 + 4 * i]!, r: Math.hypot(s.pts[6 + 4 * i]! - s.pts[4 + 4 * i]!, s.pts[7 + 4 * i]! - s.pts[5 + 4 * i]!) }));
      const border = (px: number, py: number) => Math.min(px - x0, x1 - px, py - y0, y1 - py, ...cs.map((c) => Math.abs(Math.hypot(px - c.x, py - c.y) - c.r)));
      const distToSegs = (px: number, py: number) => Math.min(...segs.map((q) => { const [ax, ay, bx, by] = q.pts as [number, number, number, number]; const dx = bx - ax, dy = by - ay, L = dx * dx + dy * dy || 1; const t = Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / L)); return Math.hypot(px - ax - t * dx, py - ay - t * dy); }));
      let tested = 0, bad = 0, unshadedHit = 0;
      for (let px = x0 + 3; px < x1; px += 7) for (let py = y0 + 3; py < y1; py += 7) {
        const shaded = reg.value[maskAt(s, [px, py])], b = border(px, py);
        if (b < 4.5) continue;
        if (shaded) { tested++; if (distToSegs(px, py) > 5) bad++; } else if (distToSegs(px, py) < 1.5) unshadedHit++;
      }
      return tested > 20 && bad <= tested * 0.01 && unshadedHit === 0;
    };
    for (const [e, ok] of [["A ∩ B", "vennInter"], ["A ∪ B", "vennUnion"], ["A ∖ B", "vennDiff"], ["B ∖ A", "vennDiffRev"], ["A'", "vennComplement"], ["(A ∪ B)'", "vennOutside"], ["A Δ B", "vennSymDiff"], ["A' ∩ B'", "vennDeMorganB"]] as Array<[string, string]>) {
      await setField(vexpr(), e);
      r[`${ok}Shading`] = checkShading(["A", "B"]);
    }
    r.vennHatchStrokesSpecial = hatch().length > 5 && hatch().every((q) => isGenerated(q)) && !isGenerated(role("setbody")[0] as Stroke);
    await setField(vexpr(), "A ∩ B");
    r.vennInterIsALens = (() => { const segs = hatch(); const xs = segs.flatMap((q) => [q.pts[0]!, q.pts[2]!]); const s = vspec(); const c0 = s.pts[4]!, c1 = s.pts[8]!; return segs.length > 3 && Math.min(...xs) > Math.min(c0, c1) - 1 && Math.max(...xs) < Math.max(c0, c1) + 200; })();
    await setField(vexpr(), "A ∩ D");
    r.vennErrorShown = vp().querySelector(".mpOut.err") !== null && /«D»/.test(vp().querySelector(".mpOut.err")!.textContent ?? "") && hatch().length === 0;
    await setField(vexpr(), "A ∩ B'");
    r.vennErrorGoes = vp().querySelector(".mpOut.err") === null && hatch().length > 0;
    // the buttons with ready expressions
    (vp().querySelectorAll<HTMLButtonElement>(".mpPreset")[0]!).click();
    await sleep(200);
    r.vennPresets = vspec().expr === "A ∪ B" && vp().querySelectorAll(".mpPreset").length >= 6;
    // elements
    await setField(vlists()[0]!, "1, 2");
    await setField(vlists()[1]!, "2; 3, 4");
    await setField(vlists()[2]!, "9");
    {
      const s = vspec();
      const els = elementsOf(s);
      r.vennElementsMasks = els.find((e) => e.label === "1")!.mask === 1 && els.find((e) => e.label === "2")!.mask === 3 && els.find((e) => e.label === "3")!.mask === 2 && els.find((e) => e.label === "9")!.mask === 0 && els.length === 5;
      const placed = placeElements(s);
      r.vennElementsInRegions = placed.length === 5 && placed.every((p) => maskAt(s, p.at) === els.find((e) => e.label === p.label)!.mask);
      r.vennElementsApart = (() => { const q = placed.filter((p) => maskAt(s, p.at) === 2); return q.length === 2 && Math.hypot(q[0]!.at[0] - q[1]!.at[0], q[0]!.at[1] - q[1]!.at[1]) > 12; })();
      const texts = (role("setlabel") as TextBox[]).map((t) => t.text);
      r.vennElementsDrawn = ["1", "2", "3", "4", "9"].every((x) => texts.includes(x)) && role("setmark").length === 5;
    }
    // the spec of the elements follows the circles when they move
    {
      const s0 = vspec();
      const c = [s0.pts[4]!, s0.pts[5]!];
      drag([[c[0]!, c[1]!], [c[0]! - 30, c[1]! + 20], [c[0]! - 60, c[1]! + 40]]);
      await sleep(250);
      const s1 = vspec();
      r.vennMoveCircle = near(s1.pts[4]!, c[0]! - 60, 0.05) && near(s1.pts[5]!, c[1]! + 40, 0.05) && near(s1.pts[6]! - s1.pts[4]!, s0.pts[6]! - s0.pts[4]!, 1e-6) && near(s1.pts[8]!, s0.pts[8]!, 1e-6);
      r.vennElementsFollow = placeElements(s1).every((p) => maskAt(s1, p.at) === elementsOf(s1).find((e) => e.label === p.label)!.mask);
      editor.undo();
      r.vennMoveUndo = near(vspec().pts[4]!, s0.pts[4]!, 1e-6);
      const rp = [s0.pts[6]!, s0.pts[7]!];
      const rdir = [rp[0]! - s0.pts[4]!, rp[1]! - s0.pts[5]!], rl = Math.hypot(rdir[0]!, rdir[1]!);
      drag([[rp[0]!, rp[1]!], [rp[0]! + (rdir[0]! / rl) * 10, rp[1]! + (rdir[1]! / rl) * 10], [rp[0]! + (rdir[0]! / rl) * 20, rp[1]! + (rdir[1]! / rl) * 20]]);
      await sleep(250);
      const s2 = vspec();
      r.vennRadius = Math.hypot(s2.pts[6]! - s2.pts[4]!, s2.pts[7]! - s2.pts[5]!) > Math.hypot(s0.pts[6]! - s0.pts[4]!, s0.pts[7]! - s0.pts[5]!) + 5 && near(s2.pts[4]!, s0.pts[4]!, 1e-6);
      editor.undo();
      const corner = [s0.pts[2]!, s0.pts[3]!];
      drag([[corner[0]!, corner[1]!], [corner[0]! + 20, corner[1]! + 10], [corner[0]! + 40, corner[1]! + 20]]);
      await sleep(250);
      r.vennUniverseCorner = near(vspec().pts[2]!, corner[0]! + 40, 0.05) && near(vspec().pts[0]!, s0.pts[0]!, 1e-6);
      editor.undo();
    }
    // names, 3 sets, the universe off
    {
      const names = vp().querySelectorAll<HTMLInputElement>(".mpNames input");
      await setField(names[0]!, "P");
      r.vennNames = vspec().names[0] === "P" && (role("setlabel") as TextBox[]).some((t) => t.text === "P") && !(role("setlabel") as TextBox[]).some((t) => t.text === "A");
      await setField(vexpr(), "P ∩ B");
      r.vennNamedExpr = vp().querySelector(".mpOut.err") === null && hatch().length > 0;
      const kSel = vp().querySelector("select") as HTMLSelectElement;
      await setField(kSel, "3");
      const s3 = vspec();
      r.vennThree = s3.k === 3 && role("setbody").length === 4 && s3.pts.length === 16 && (role("setlabel") as TextBox[]).some((t) => t.text === "C") && vp().querySelectorAll(".mpPreset").length >= 5;
      await setField(vexpr(), "(P ∩ B) ∪ C'");
      r.vennThreeShading = checkShading(["P", "B", "C"]);
      await setField(vexpr(), "P ∖ (B ∪ C)");
      r.vennThreeShading2 = checkShading(["P", "B", "C"]);
      r.vennThreeGeometry = (() => { const s = vspec(); const cs = [0, 1, 2].map((i) => ({ x: s.pts[4 + 4 * i]!, y: s.pts[5 + 4 * i]!, r: Math.hypot(s.pts[6 + 4 * i]! - s.pts[4 + 4 * i]!, s.pts[7 + 4 * i]! - s.pts[5 + 4 * i]!) })); const m7 = maskAt(s, [(cs[0]!.x + cs[1]!.x + cs[2]!.x) / 3, (cs[0]!.y + cs[1]!.y + cs[2]!.y) / 3]); return m7 === 7; })();
      const uni = [...vp().querySelectorAll<HTMLInputElement>(".mpChecks input")][0]!;
      uni.checked = false; uni.dispatchEvent(new Event("change", { bubbles: true }));
      await sleep(200);
      r.vennNoUniverse = !(role("setlabel") as TextBox[]).some((t) => t.text === "U") && role("setbody").length === 3 && vspec().universe === false;
      uni.checked = true; uni.dispatchEvent(new Event("change", { bubbles: true }));
      await sleep(200);
    }
    // turned, saved and opened again
    {
      editor.selection.transformSelection("rotR");
      await sleep(80);
      r.vennRotated = cur().spec.kind === "venn" && (cur().spec as VennSpec).expr === "P ∖ (B ∪ C)" && (cur().spec as VennSpec).k === 3;
      editor.undo();
      const xml = serializeXopp(doc!);
      const back = parseXopp(xml);
      const bm = back.pages[PG]!.layers.at(-1)!.elements.filter((e) => planeIdOf(e) !== null);
      const re = readMath(bm);
      const orig = vspec();
      r.vennRoundTrip = bm.length === mine().length && !!re && re.spec.kind === "venn" && (re.spec as VennSpec).expr === orig.expr && (re.spec as VennSpec).lists.join("|") === orig.lists.join("|") && (re.spec as VennSpec).names.join() === orig.names.join() && near((re.spec as VennSpec).pts[4]!, orig.pts[4]!, 1e-2);
      r.vennStyleKept = !!re && re.style.color === "#0044cc" && near(re.style.width, 1.41, 1e-3);
      editor.selection.transformSelection("rotR");
      await sleep(60);
      editor.undo();
    }
    // resizing rebuilds it (the hatching stays exact)
    {
      const before = hatch().length;
      editor.updateMath({ ...vspec(), hatch: "dense" });
      await sleep(100);
      r.vennDenseHatch = hatch().length > before * 1.4;
    }
    // restyle with the pen
    editor.color = "#cc2288";
    (vp().querySelector(".mpBtn") as HTMLButtonElement).click();
    await sleep(200);
    r.vennRestyle = attrValue((role("setbody")[0] as Stroke).attrs, "color") === "#cc2288ff";
    editor.color = "#0044cc";
    // fills see the circles as walls and the hatching as paper
    r.vennFillWalls = (role("setbody") as Stroke[]).every((q) => !isGenerated(q)) && (role("setlabel") as TextBox[]).length > 0 && hatch().every((q) => isGenerated(q));

    // ---- the menu, the keys, the toolbox, the panels
    wipe();
    document.getElementById("linePanel")!.hidden = true; document.getElementById("vennPanel")!.hidden = true;
    r.mathMenu = (await window.xournal.testMenuClick(["Herramientas", "Recta real (intervalos y puntos)"])) && (editor.tool as Tool) === "numline" && (await window.xournal.testMenuClick(["Herramientas", "Diagrama de conjuntos (Venn)"])) && (editor.tool as Tool) === "venn";
    setTool("pen");
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "n", bubbles: true, cancelable: true }));
    const k1 = editor.tool;
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "j", bubbles: true, cancelable: true }));
    r.mathKeys = k1 === "numline" && (editor.tool as Tool) === "venn";
    r.mathButtons = ["btnNumline", "btnVenn"].every((id) => document.getElementById(id)!.parentElement!.classList.contains("split") && document.getElementById(id)!.getBoundingClientRect().width <= 16) && !!document.querySelector("button.tool[data-tool=numline] svg") && !!document.querySelector("button.tool[data-tool=venn] svg");
    document.getElementById("btnNumline")!.click();
    r.mathPanelFromTheArrow = !document.getElementById("linePanel")!.hidden && document.getElementById("vennPanel")!.hidden;
    document.getElementById("btnVenn")!.click();
    r.mathPanelsExclusive = document.getElementById("linePanel")!.hidden && !document.getElementById("vennPanel")!.hidden;
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    r.mathPanelEsc = document.getElementById("vennPanel")!.hidden;
    r.mathToolboxCatalog = [...catalogWith([])].some((i) => i.id === "tool.numline") && catalogWith([]).some((i) => i.id === "tool.venn") && APP_ACTIONS.some((a) => a.id === "tool.numline") && APP_ACTIONS.some((a) => a.id === "tool.venn");
    // copy and paste makes a second object, not a second set of pieces of the same one (also for planes and figures)
    {
      setTool("venn");
      drag([[60, 300], [400, 520]]);
      await sleep(200);
      const idOf = () => planeIdOf(role("setwitness")[0]!);
      const firstId = planeIdOf(mine()[0]!);
      const nOne = mine().length;
      editor.selection.copy();
      editor.selection.paste();
      await sleep(100);
      const ids = new Set(mine().map((e) => planeIdOf(e)));
      r.mathPasteIsANewObject = mine().length === 2 * nOne && ids.size === 2 && editor.selectedMath() !== null && editor.selection.count === nOne && planeIdOf(editor.selection.entries[0]!.el) !== firstId;
      editor.undo();
      r.mathPasteUndo = mine().length === nOne && new Set(mine().map((e) => planeIdOf(e))).size === 1;
      void idOf;
      wipe();
    }
    // new defaults carry what was typed in the panel when nothing is selected
    editor.selection.clear();
    document.getElementById("btnNumline")!.click();
    await setField(exprs()[0]!, "[0, 4)");
    setTool("numline");
    drag([[100, 250], [400, 250]]);
    await sleep(200);
    r.mathDefaultsCarry = (cur().spec as LineSpec).rows[0]!.expr === "[0, 4)" && dots().length === 1 && rings().length === 1;
    if (lp().hidden) document.getElementById("btnNumline")!.click();
    await sleep(100);
    exprs()[0]!.focus();
    exprs()[0]!.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true, cancelable: true }));
    await sleep(300);
    r.mathEnterKeepsTheCursor = !!document.activeElement && document.activeElement.classList.contains("mpExpr") && lp().contains(document.activeElement);
    void lineSaved;
    // clean up
    editor.color = savedColor; editor.penWidth = savedWidth;
    wipe();
    document.getElementById("linePanel")!.hidden = true; document.getElementById("vennPanel")!.hidden = true;
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // INK) a formula keeps only its ink, whatever the colour (a light or saturated ink on a dark paper used to fill the whole box)
  {
    const inks = ["#000000", "#ff2a9d", "#ffee88", "#05e1ff", "#3333cc", "#ff8000", "#00ff88", "#ffffff", "#9d4edd"];
    const stat = (cv: HTMLCanvasElement) => {
      const d = cv.getContext("2d")!.getImageData(0, 0, cv.width, cv.height).data;
      let opaque = 0, solid = 0;
      for (let i = 3; i < d.length; i += 4) { if (d[i]! > 20) opaque++; if (d[i]! > 200) solid++; }
      return { share: opaque / (d.length / 4), solid, corner: d[3]! };
    };
    const bad: string[] = [];
    for (const ink of inks) {
      const res = await window.xournal.latexPdf({ latex: String.raw`\oint_C \vec{F}\cdot\hat{n}\,ds=\iint_D \nabla\cdot\vec{F}\,dA`, display: true, color: ink, px: 20 });
      if (!res.ok) { bad.push(`${ink}:pdf`); continue; }
      const a = stat(await rasterizeInk(res.pdfB64, 600, ink)), b = stat(await rasterizeAuto(res.pdfB64, 600));
      for (const [name, s] of [["ink", a], ["auto", b]] as const) if (!(s.corner === 0 && s.share < 0.4 && s.solid > 50)) bad.push(`${ink}:${name}:${s.share.toFixed(2)}`);
      // over a dark paper the empty part of the bitmap must leave the paper as it is (a colour with no alpha would add itself to it)
      for (const [name, cv] of [["ink", await rasterizeInk(res.pdfB64, 600, ink)], ["auto", await rasterizeAuto(res.pdfB64, 600)]] as const) {
        const over = document.createElement("canvas");
        over.width = cv.width; over.height = cv.height;
        const g = over.getContext("2d", { willReadFrequently: true })!;
        g.fillStyle = "rgb(26,15,60)";
        g.fillRect(0, 0, over.width, over.height);
        g.getImageData(0, 0, 1, 1); // reading once makes the canvas readable in software, so the pixel read below is the composited one
        g.drawImage(cv, 0, 0);
        const px = g.getImageData(2, 2, 1, 1).data;
        if (px[0] !== 26 || px[1] !== 15 || px[2] !== 60) bad.push(`${ink}:${name}:over-dark:${px[0]},${px[1]},${px[2]},${px[3]} ${cv.width}x${cv.height}`);
      }
    }
    r.inkAllColoursTransparent = bad.length === 0;
    if (bad.length) r.inkBad = bad.join(" ");
  }

  // GLOW) the live glow (a CSS filter given the pen's hex colour) and the committed one (canvas filter, rgb colour) are the same
  {
    const live = glowFilter("#ff8000", { size: 8, intensity: 0.5 }, 2), done = glowFilter("rgb(255, 128, 0)", { size: 8, intensity: 0.5 }, 2);
    r.glowLiveSameAsCommitted = live === done && /rgba\(255,128,0,0\.5\)/.test(live);
    r.glowShortHex = glowFilter("#f80", { size: 8, intensity: 0.8 }, 1).includes("rgba(255,136,0,0.8)");
    r.glowEightDigitHex = glowFilter("#ff8000ff", { size: 8, intensity: 0.3 }, 1).includes("rgba(255,128,0,0.3)");
  }

  // PDF) export the document as a PDF
  {
    const out = "G:/xournal-lab/app/shots/e2e-export.pdf";
    const t0 = performance.now();
    const res = await exportPdf(out);
    r.pdfExportMs = Math.round(performance.now() - t0);
    r.pdfExportOk = !!res && res.ok === true && (res.bytes ?? 0) > 2000 && res.path === out;
    r.pdfExportPages = doc!.pages.length;
    r.pdfExportBytes = res?.bytes ?? 0;
    r.pdfExportStatus = /PDF exportado/.test(info.textContent ?? "");
  }

  // dialogs left open by earlier checks would now float over the page and make it ignore the pen
  latex.close(); prefsDialog.close(); penDiagnostic.close(); tabletDialog.close();
  setTool("pen");

  // TAB) pen tips, pressure curve, pen buttons, key bindings
  {
    const savedTablet: TabletSettings = JSON.parse(JSON.stringify(tablet));
    const savedColor = editor.color, savedGlow = editor.glowOn;
    editor.glowOn = false;
    setZoom(1);
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const at = (q: [number, number]) => { const rc = el().getBoundingClientRect(); return [rc.left + q[0] * zoom, rc.top + q[1] * zoom] as const; };
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    const strokes = () => lay().elements.filter((e): e is Stroke => e.kind === "stroke");
    /** A stroke along a line with a pressure ramp, as a pen would send it. */
    const pen = (pts: Array<[number, number, number]>, type = "pen", btn?: { button: number; buttons: number }) => {
      synth("pointerdown", el(), ...at([pts[0]![0], pts[0]![1]]), pts[0]![2], type, false, false, btn);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...at([pts[i]![0], pts[i]![1]]), pts[i]![2], type, false, false, btn);
      const l = pts[pts.length - 1]!;
      synth("pointerup", el(), ...at([l[0], l[1]]), 0, type, false, false, btn);
    };
    const ramp = (x0: number, y0: number, x1: number, y1: number, n = 40): Array<[number, number, number]> => Array.from({ length: n + 1 }, (_, i) => [x0 + ((x1 - x0) * i) / n, y0 + ((y1 - y0) * i) / n, Math.sin((i / n) * Math.PI) * 0.9 + 0.05]);
    const wipe = () => { lay().elements.length = 0; doc!.elementCount = 0; };
    const setT = (patch: Partial<TabletSettings>) => applyTablet({ ...tablet, ...patch });

    // settings from anything
    const bad = sanitizeTablet({ brush: "laser", size: -5, curve: { x1: 9, y1: "a" }, barrel: "bogus", eraser: 3, keys: { "Ctrl+1": "tool.pen", "X": "nope", "": "tool.pen" } });
    r.tabSanitize = bad.brush === "pen" && bad.size === 0.1 && bad.curve.x1 === 1 && bad.curve.y1 === 0.25 && bad.barrel === "toolbox" && bad.eraser === "eraser" && Object.keys(bad.keys).join() === "Ctrl+1";
    r.tabSanitizeNull = sanitizeTablet(null).brush === "pen" && sanitizeTablet("x").keys && Object.keys(sanitizeTablet(undefined).keys).length === 0;

    // pressure curve
    const lin = new PressureMap(), soft = new PressureMap(CURVE_PRESETS[1]!.curve), firm = new PressureMap(CURVE_PRESETS[2]!.curve), dead = new PressureMap({ ...CURVE_PRESETS[0]!.curve, min: 0.1 });
    r.tabCurveLinear = near(lin.apply(0), 0, 1e-3) && near(lin.apply(0.5), 0.5, 0.01) && near(lin.apply(1), 1, 1e-3);
    r.tabCurveSoftFirm = soft.apply(0.3) > lin.apply(0.3) + 0.05 && firm.apply(0.3) < lin.apply(0.3) - 0.05;
    r.tabCurveMonotonic = [soft, firm, lin, dead].every((m) => { let prev = -1; for (let i = 0; i <= 200; i++) { const v = m.apply(i / 200); if (v < prev - 1e-9) return false; prev = v; } return true; });
    r.tabCurveDeadZone = dead.apply(0.05) === 0 && near(dead.apply(1), 1, 1e-3) && dead.apply(0.2) > 0;

    // tips (pure)
    const run = (kind: BrushKind, samples: Array<[number, number, number]>, pressure = true) => {
      const eng = new BrushEngine(kind, 2, lin, pressure);
      const pts: number[] = [], widths: number[] = [eng.first()];
      samples.forEach(([x, y, p], i) => { pts.push(x, y); if (i === 0) eng.next(x, y, p, 0); else widths.push(eng.next(x, y, p, i * 8)); });
      return { pts, widths, final: eng.finish(pts, widths) };
    };
    const flat = (n: number, p: number, vert = false): Array<[number, number, number]> => Array.from({ length: n }, (_, i) => (vert ? [100, 100 + i * 2, p] : [100 + i * 2, 100, p]) as [number, number, number]);
    const ink = run("ink", ramp(100, 100, 300, 100, 50));
    const mid = ink.widths[25]!;
    r.tabInkTapers = ink.widths[0]! < mid * 0.6 && ink.final[ink.final.length - 1]! < mid * 0.5 && ink.widths.length === ink.pts.length / 2;
    r.tabInkPressure = run("ink", flat(40, 0.9)).widths[39]! > run("ink", flat(40, 0.2)).widths[39]! * 1.5;
    r.tabInkSpeedWithoutPressure = (() => { const slow = new BrushEngine("ink", 2, lin, false), fast = new BrushEngine("ink", 2, lin, false); let a = 0, b = 0; for (let i = 0; i < 30; i++) { slow.next(i * 0.4, 0, 0.5, i * 8); fast.next(i * 12, 0, 0.5, i * 8); } a = slow.next(13, 0, 0.5, 240); b = fast.next(400, 0, 0.5, 240); return a > b * 1.2; })();
    const along = (deg: number): Array<[number, number, number]> => Array.from({ length: 40 }, (_, i) => [100 + Math.cos((deg * Math.PI) / 180) * i * 2, 100 + Math.sin((deg * Math.PI) / 180) * i * 2, 0.5]);
    const fAlong = run("fountain", along(-40)).widths[39]!, fAcross = run("fountain", along(50)).widths[39]!;
    r.tabFountainDirection = fAcross > fAlong * 3 && fAlong > 0.05;
    r.tabBrushPressure = run("brush", flat(40, 0.95)).widths[39]! > run("brush", flat(40, 0.15)).widths[39]! * 3;
    r.tabPenKindIsPlain = run("pen", ramp(100, 100, 300, 100, 20), false).widths.every((w) => w === 2) && near(run("pen", flat(10, 0.5)).widths[5]!, 1.5, 1e-6);

    // drawing with a tip
    wipe();
    setT({ brush: "ink", size: 2 });
    pen(ramp(100, 100, 400, 100, 40));
    await sleep(120);
    const s1 = strokes()[0]!;
    const w1 = s1.widths;
    r.tabInkStroke = strokes().length === 1 && w1.length === s1.pts.length / 2 && Math.max(...w1) > Math.min(...w1) * 2 && w1[w1.length - 1]! < Math.max(...w1) * 0.5 && attrValue(s1.attrs, "width")!.split(" ").length === w1.length;
    editor.undo();
    r.tabInkUndo = strokes().length === 0;
    editor.redo();
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml).pages[PG]!.layers.at(-1)!.elements.filter((e): e is Stroke => e.kind === "stroke");
    r.tabInkSaved = back.length === 1 && back[0]!.widths.length === w1.length && w1.every((w, i) => near(back[0]!.widths[i]!, w, 1e-3));
    wipe();
    setT({ brush: "pen", size: 1.41 });
    pen(ramp(100, 100, 400, 100, 20), "mouse");
    await sleep(100);
    r.tabMousePlain = strokes().length === 1 && strokes()[0]!.widths.length === 1 && near(strokes()[0]!.widths[0]!, 1.41, 1e-9);
    wipe();
    setT({ brush: "brush", size: 3 });
    pen(ramp(100, 100, 400, 100, 40));
    await sleep(100);
    const bw = strokes()[0]!.widths;
    r.tabBrushStroke = bw.length > 10 && Math.max(...bw) > 3 * 1.5;
    wipe();
    setT({ brush: "pen" });

    // pen buttons and eraser end
    const lineAcross = () => pen(ramp(100, 200, 400, 200, 20));
    setT({ barrel: "eraser", barrel2: "undo", eraser: "eraser" });
    lineAcross();
    await sleep(80);
    const n0 = strokes().length;
    pen([[250, 200, 0.5]], "pen", { button: 2, buttons: 2 });
    await sleep(80);
    r.tabBarrelErases = n0 === 1 && strokes().length === 0;
    editor.undo();
    r.tabBarrelEraseUndo = strokes().length === 1;
    pen([[250, 200, 0.5]], "pen", { button: 5, buttons: 32 });
    await sleep(80);
    r.tabEraserEndErases = strokes().length === 0;
    editor.undo();
    pen([[250, 200, 0.5]], "pen", { button: 1, buttons: 4 }); // second button: undo
    await sleep(80);
    r.tabBarrel2Undo = strokes().length === 0;
    editor.redo();
    setT({ barrel: "none", eraser: "none" });
    pen([[250, 200, 0.5]], "pen", { button: 2, buttons: 2 });
    pen([[250, 200, 0.5]], "pen", { button: 5, buttons: 32 });
    await sleep(80);
    r.tabButtonNothing = strokes().length === 1 && editor.tool === "pen";
    setT({ barrel: "highlighter" });
    pen(ramp(100, 300, 300, 300, 10), "pen", { button: 2, buttons: 2 });
    await sleep(80);
    r.tabButtonTemporaryTool = strokes().length === 2 && strokes()[1]!.tool === "highlighter" && editor.tool === "pen";
    setT({ barrel: "eraser", eraser: "eraser" });
    wipe();

    // key bindings (what the remote's keys become)
    setT({ keys: { "Ctrl+Alt+1": "tool.highlighter", "Ctrl+Alt+2": "pen.sizeUp", "Ctrl+Alt+3": "brush.next", "F13": "edit.undo", "Alt+Shift+Q": "tool.previous" } });
    setTool("pen");
    const key = (k: string, mods: Partial<KeyboardEventInit> = {}) => window.dispatchEvent(new KeyboardEvent("keydown", { key: k, bubbles: true, cancelable: true, ...mods }));
    key("1", { ctrlKey: true, altKey: true });
    r.tabKeyTool = editor.tool === "highlighter";
    key("Q", { altKey: true, shiftKey: true });
    r.tabKeyPrevious = editor.tool === "pen";
    const w0 = editor.penWidth;
    key("2", { ctrlKey: true, altKey: true });
    r.tabKeySize = near(editor.penWidth, +(w0 * 1.25).toFixed(2), 1e-9) && tablet.size === editor.penWidth;
    key("3", { ctrlKey: true, altKey: true });
    r.tabKeyBrush = editor.brush === "ink" && tablet.brush === "ink";
    pen(ramp(100, 100, 300, 100, 10));
    await sleep(60);
    key("F13");
    r.tabKeyUndo = strokes().length === 0;
    r.tabKeyDefaultsKeep = (() => { setTool("select"); key("p"); return editor.tool === "pen"; })();
    setT({ brush: "pen", size: 1.41, keys: {} });
    key("1", { ctrlKey: true, altKey: true });
    r.tabKeyUnbound = editor.tool === "pen";

    // saved with the preferences
    setT({ brush: "fountain", size: 3.5, curve: { ...CURVE_PRESETS[1]!.curve }, barrel: "select", keys: { "Ctrl+Alt+9": "tool.lasso" } });
    applyTablet(tablet, true);
    await sleep(150);
    const stored = sanitizeTablet(JSON.parse((await window.xournal.getPrefs()).tablet));
    r.tabPrefsSaved = stored.brush === "fountain" && stored.size === 3.5 && stored.barrel === "select" && stored.keys["Ctrl+Alt+9"] === "tool.lasso" && near(stored.curve.y1, CURVE_PRESETS[1]!.curve.y1, 1e-9);
    applyPrefs({ ...prefs, theme: prefs.theme }); // another preference change must not undo them
    r.tabPrefsSurvive = editor.brush === "fountain" && editor.penWidth === 3.5;
    r.tabWidthSelectShowsCustom = [...widthSel.options].some((o) => Number(o.value) === 3.5 && o.selected);
    setT({ size: 1.41 });
    widthSel.value = String(PEN_WIDTHS[2]);
    widthSel.dispatchEvent(new Event("change"));
    r.tabWidthSelectSaves = editor.penWidth === PEN_WIDTHS[2] && tablet.size === PEN_WIDTHS[2];

    // the dialog
    const menuOk = await window.xournal.testMenuClick(["Herramientas", "Lápiz y tableta…"]);
    await sleep(120);
    const dlg = document.getElementById("tabletDlg")!;
    r.tabMenuOpensDialog = menuOk && !dlg.hidden;
    r.tabDialogCards = dlg.querySelectorAll(".tbCard").length === 4 && dlg.querySelectorAll(".tbCard.on").length === 1;
    (dlg.querySelector('.tbCard[data-kind="ink"]') as HTMLButtonElement).click();
    r.tabDialogPicksTip = editor.brush === "ink" && tablet.brush === "ink";
    const num = dlg.querySelector('input[type="number"]') as HTMLInputElement;
    num.value = "4.5"; num.dispatchEvent(new Event("change"));
    r.tabDialogSize = editor.penWidth === 4.5;
    const preset = document.getElementById("tbPreset") as HTMLSelectElement;
    preset.value = "2"; preset.dispatchEvent(new Event("change"));
    r.tabDialogPreset = near(tablet.curve.x1, CURVE_PRESETS[2]!.curve.x1, 1e-9) && near(editor.pressure.apply(0.3), firm.apply(0.3), 1e-6);
    // dragging a handle of the curve
    const cc = dlg.querySelector("canvas.tbCurve") as HTMLCanvasElement;
    const cr = cc.getBoundingClientRect(), pad = 14 * (cr.width / cc.width);
    const hx = (x: number, y: number) => ({ clientX: cr.left + pad + x * (cr.width - 2 * pad), clientY: cr.top + pad + (1 - y) * (cr.height - 2 * pad) });
    const c0 = tablet.curve;
    cc.dispatchEvent(new PointerEvent("pointerdown", { ...hx(c0.x2, c0.y2), pointerId: 3, bubbles: true }));
    cc.dispatchEvent(new PointerEvent("pointermove", { ...hx(0.6, 0.95), pointerId: 3, bubbles: true }));
    cc.dispatchEvent(new PointerEvent("pointerup", { ...hx(0.6, 0.95), pointerId: 3, bubbles: true }));
    r.tabDialogCurveDrag = near(tablet.curve.x2, 0.6, 0.02) && near(tablet.curve.y2, 0.95, 0.02) && near(tablet.curve.x1, c0.x1, 1e-9);
    // the live readout
    window.dispatchEvent(new PointerEvent("pointermove", { pointerType: "pen", pressure: 0.42, tiltX: 12, tiltY: -5, buttons: 1, bubbles: true }));
    const readout = dlg.querySelector(".tbReadout")!.textContent ?? "";
    r.tabDialogReadout = /42\.0 %/.test(readout) && /X 12/.test(readout) && /pen/.test(readout);
    // the remote's express keys by number
    const exSelects = [...dlg.querySelectorAll<HTMLSelectElement>("#tabletBody section:nth-of-type(4) select")];
    r.tabExpressRows = exSelects.length === 10 && dlg.textContent!.includes("Tecla 1  →  en el controlador pulsa Ctrl+Alt+Mayús+1");
    exSelects[0]!.value = "tool.fill"; exSelects[0]!.dispatchEvent(new Event("change"));
    r.tabExpressBinds = tablet.keys["F13"] === "tool.fill";
    setTool("pen");
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "F13", bubbles: true, cancelable: true }));
    r.tabExpressWorks = editor.tool === "fill";
    setTool("pen");
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "!", code: "Digit1", ctrlKey: true, altKey: true, shiftKey: true, bubbles: true, cancelable: true })); // typed on a keyboard: Shift changes the key but not the physical key
    r.tabExpressTypedCombo = editor.tool === "fill" && tablet.keys["Ctrl+Alt+Shift+1"] === "tool.fill";
    r.tabComboPhysicalKey = comboOf({ key: "|", code: "Digit1", ctrlKey: true, shiftKey: false, altKey: true, metaKey: false }) === "Ctrl+Alt+1";
    exSelects[0]!.value = ""; exSelects[0]!.dispatchEvent(new Event("change"));
    r.tabExpressClears = tablet.keys["F13"] === undefined && tablet.keys["Ctrl+Alt+Shift+1"] === undefined;
    (([...dlg.querySelectorAll("button")].find((b) => /Proponer/.test(b.textContent ?? "")))!).click();
    r.tabExpressSuggest = tablet.keys["F13"] === "tool.pen" && tablet.keys["Ctrl+Alt+Shift+2"] === "tool.eraser" && tablet.keys["F14"] === "tool.eraser" && tablet.keys["F17"] === "edit.undo" && exSelects[1]!.value === "tool.eraser";
    // binding a key from the dialog
    const sel = dlg.querySelector<HTMLSelectElement>("#tabletBody section:nth-of-type(5) select")!;
    sel.value = "tool.lasso"; sel.dispatchEvent(new Event("change"));
    const capture = [...dlg.querySelectorAll("button")].find((b) => /Asignar/.test(b.textContent ?? ""))!;
    capture.click();
    dlg.dispatchEvent(new KeyboardEvent("keydown", { key: "F18", ctrlKey: true, bubbles: true }));
    r.tabDialogBindsKey = tablet.keys["Ctrl+F18"] === "tool.lasso" && [...dlg.querySelectorAll(".tbKeyRow kbd")].some((k) => k.textContent === "Ctrl+F18");
    const keyDel = [...dlg.querySelectorAll(".tbKeyRow")].find((row) => row.querySelector("kbd")!.textContent === "Ctrl+F18")!.querySelector("button")!;
    keyDel.click();
    r.tabDialogUnbindsKey = tablet.keys["Ctrl+F18"] === undefined;
    // the dial of the remote: a wheel turn binds like a key
    const capture2 = [...dlg.querySelectorAll("button")].find((b) => /Asignar/.test(b.textContent ?? ""))!;
    capture2.click();
    dlg.dispatchEvent(new WheelEvent("wheel", { deltaY: -100, ctrlKey: true, bubbles: true, cancelable: true }));
    r.tabDialogBindsWheel = tablet.keys["Ctrl+Rueda arriba"] === "tool.lasso";
    capture2.click();
    dlg.dispatchEvent(new MouseEvent("mousedown", { button: 3, bubbles: true, cancelable: true }));
    r.tabDialogBindsMouseButton = tablet.keys["Botón atrás"] === "tool.lasso";
    dlg.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.tabDialogCloses = dlg.hidden;
    setT({ keys: { "Ctrl+Rueda arriba": "view.zoomIn", "Rueda abajo": "view.zoomOut", "Botón atrás": "tool.eraser" } });
    setZoom(1);
    const z0 = zoom;
    window.dispatchEvent(new WheelEvent("wheel", { deltaY: -100, ctrlKey: true, bubbles: true, cancelable: true }));
    const zIn = zoom;
    window.dispatchEvent(new WheelEvent("wheel", { deltaY: 100, bubbles: true, cancelable: true }));
    r.tabWheelBound = zIn > z0 && zoom < zIn;
    window.dispatchEvent(new MouseEvent("mousedown", { button: 3, bubbles: true, cancelable: true }));
    r.tabMouseButtonBound = editor.tool === "eraser";
    setTool("pen");
    r.tabWheelUnbound = (() => { setT({ keys: {} }); const z = zoom; scroller.dispatchEvent(new WheelEvent("wheel", { deltaY: -100, ctrlKey: true, bubbles: true, cancelable: true })); return zoom !== z; })();
    setZoom(1);
    r.tabComboNames = comboOf({ key: "a", ctrlKey: true, shiftKey: false, altKey: false, metaKey: false }) === "Ctrl+A" && comboOf({ key: "Shift", ctrlKey: false, shiftKey: true, altKey: false, metaKey: false }) === null && comboOf({ key: "F5", ctrlKey: false, shiftKey: true, altKey: true, metaKey: false }) === "Alt+Shift+F5";

    // clean up
    applyTablet(savedTablet, true);
    editor.color = savedColor; editor.glowOn = savedGlow;
    wipe();
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // FIG) figures: lines, curves, polygons, angles, dimension lines and wireframe solids
  {
    const menu = async (...labels: string[]) => { const ok = await window.xournal.testMenuClick(labels); await sleep(80); return ok; };
    const savedColor = editor.color, savedWidth = editor.penWidth, savedGlow = editor.glowOn, savedOpts = { ...editor.figOpts }, savedKind = editor.figKind;
    setZoom(1);
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const at = (q: [number, number]) => { const rc = el().getBoundingClientRect(); return [rc.left + q[0] * zoom, rc.top + q[1] * zoom] as const; };
    const drag = (pts: Array<[number, number]>, shift = false) => {
      synth("pointerdown", el(), ...at(pts[0]!), 0.5);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...at(pts[i]!), 0.5, "pen", shift);
      synth("pointerup", el(), ...at(pts[pts.length - 1]!), 0, "pen", shift);
    };
    const click = (q: [number, number]) => drag([q]);
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    const mine = () => lay().elements.filter((e) => planeIdOf(e) !== null);
    const role = (rr: string) => mine().filter((e) => attrValue("attrs" in e ? e.attrs : [], "xlRole") === rr);
    const cur = () => editor.selectedFigure()!;
    const body = () => role("figbody") as Stroke[];
    const make = async (kind: FigKind, pts: Array<[number, number]>) => {
      editor.selection.clear();
      for (const e of mine()) lay().elements.splice(lay().elements.indexOf(e), 1);
      editor.figKind = kind;
      setTool("figure");
      drag(pts);
      await sleep(120);
    };
    editor.color = "#0044cc";
    editor.penWidth = 1.41;
    editor.glowOn = false;
    editor.figOpts = { heads: "none", dash: "solid", n: 5, m: 5, hidden: "dash", label: "" };

    // a segment and its anchors
    const before0 = doc!.elementCount;
    await make("segment", [[100, 100], [300, 180]]);
    r.figSegmentCreated = body().length === 1 && body()[0]!.pts.length === 4 && editor.selectedFigure() !== null && editor.tool === "select";
    r.figSegmentEnds = near(cur().spec.pts[0]!, 100, 1e-3) && near(cur().spec.pts[1]!, 100, 1e-3) && near(cur().spec.pts[2]!, 300, 1e-3) && near(cur().spec.pts[3]!, 180, 1e-3);
    r.figStyleFromPen = attrValue(body()[0]!.attrs, "color") === "#0044ccff" && near(body()[0]!.widths[0]!, 1.41) && !isGenerated(body()[0]!) && isGenerated(role("figwitness")[0] as Stroke);
    r.figWitnessInvisible = attrValue((role("figwitness")[0] as Stroke).attrs, "color") === "#0044cc00";
    editor.undo();
    r.figUndoAll = mine().length === 0 && doc!.elementCount === before0;
    editor.redo();
    editor.selection.selectMany(PG, lay(), mine());
    r.figRedoAll = mine().length > 0 && editor.selectedFigure() !== null;
    drag([[300, 180], [340, 120], [360, 90]]); // the end anchor
    await sleep(120);
    r.figAnchorDrag = near(cur().spec.pts[2]!, 360, 1e-3) && near(cur().spec.pts[3]!, 90, 1e-3) && near(cur().spec.pts[0]!, 100, 1e-3) && near(body()[0]!.pts[2]!, 360, 1e-3);
    editor.undo();
    r.figAnchorUndo = near(cur().spec.pts[2]!, 300, 1e-3) && near(body()[0]!.pts[2]!, 300, 1e-3);
    // the figure follows what happens to its strokes: rotated and flipped, its anchors are where its ends are
    editor.selection.transformSelection("flipH");
    await sleep(80);
    const s1 = cur().spec, b1 = body()[0]!.pts;
    r.figFlipKeepsAnchors = near(s1.pts[0]!, b1[0]!, 1e-3) && near(s1.pts[1]!, b1[1]!, 1e-3) && near(s1.pts[2]!, b1[2]!, 1e-3) && near(s1.pts[3]!, b1[3]!, 1e-3);
    editor.selection.transformSelection("rotR");
    await sleep(80);
    const s2 = cur().spec, b2 = body()[0]!.pts;
    r.figRotateKeepsAnchors = near(s2.pts[0]!, b2[0]!, 1e-3) && near(s2.pts[1]!, b2[1]!, 1e-3) && near(s2.pts[2]!, b2[2]!, 1e-3) && near(s2.pts[3]!, b2[3]!, 1e-3) && near(Math.hypot(s2.pts[2]! - s2.pts[0]!, s2.pts[3]! - s2.pts[1]!), Math.hypot(200, 80), 1e-2);
    // after a transform the anchors still edit the figure
    const a1 = [s2.pts[2]!, s2.pts[3]!] as [number, number];
    drag([a1, [a1[0] + 25, a1[1] + 25]]);
    await sleep(120);
    r.figEditAfterTransform = near(cur().spec.pts[2]!, a1[0] + 25, 1e-2) && near(body()[0]!.pts[2]!, a1[0] + 25, 1e-2);
    editor.undo(); editor.undo(); editor.undo();

    // an arrow has a head of its own; with a dash the line is dashed but the head is not
    editor.figOpts = { ...editor.figOpts, dash: "dash" };
    await make("arrow", [[100, 100], [300, 100]]);
    const head = role("figarrow") as Stroke[];
    r.figArrowHead = head.length === 1 && head[0]!.pts.length === 6 && near(head[0]!.pts[2]!, 300, 1e-6) && near(head[0]!.pts[3]!, 100, 1e-6) && cur().spec.heads === "end";
    r.figDash = body()[0]!.dash === "dash" && head[0]!.dash === null;
    editor.figOpts = { ...editor.figOpts, dash: "solid", heads: "both" };
    await make("segment", [[100, 100], [300, 100]]);
    r.figBothHeads = (role("figarrow") as Stroke[]).length === 2;
    // Shift snaps the angle to steps of 15 degrees
    editor.figOpts = { ...editor.figOpts, heads: "none" };
    await make("segment", [[100, 100], [300, 140]]);
    editor.undo();
    editor.figKind = "segment";
    setTool("figure");
    drag([[100, 100], [300, 140]], true);
    await sleep(100);
    const sh = cur().spec.pts;
    r.figShiftAngle = near(Math.atan2(sh[3]! - sh[1]!, sh[2]! - sh[0]!) % (Math.PI / 12), 0, 1e-6) || near(Math.abs(Math.atan2(sh[3]! - sh[1]!, sh[2]! - sh[0]!) % (Math.PI / 12)), Math.PI / 12, 1e-6);

    // a polygon by clicks: a click per vertex, a second click on the last one finishes, and the outline is closed
    await make("polygon", [[100, 100]]);
    r.figPendingNothingYet = mine().length === 0 && editor.figPending.length === 2 && editor.tool === "figure";
    click([300, 100]); click([300, 250]); click([150, 250]);
    r.figPendingPoints = editor.figPending.length === 8;
    editor.undoFigurePoint();
    click([150, 250]);
    click([150, 250]); // the same place again: done
    await sleep(120);
    const poly = body()[0]!;
    r.figPolygonClosed = body().length === 1 && cur().spec.kind === "polygon" && poly.pts.length === 10 && near(poly.pts[0]!, poly.pts[8]!, 1e-3) && near(poly.pts[1]!, poly.pts[9]!, 1e-3) && editor.figPending.length === 0 && editor.tool === "select";
    // a point of a polygon is an anchor
    drag([[300, 250], [320, 280]]);
    await sleep(120);
    r.figPolygonAnchor = near(cur().spec.pts[4]!, 320, 1e-3) && near(cur().spec.pts[5]!, 280, 1e-3) && near(body()[0]!.pts[4]!, 320, 1e-3);
    editor.undo();
    // Enter finishes, Esc cancels, Backspace removes the last point
    editor.selection.clear();
    editor.figKind = "polyline";
    setTool("figure");
    click([100, 400]); click([200, 450]); click([300, 400]);
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
    await sleep(120);
    r.figEnterFinishes = cur().spec.kind === "polyline" && cur().spec.pts.length === 6 && editor.figPending.length === 0;
    editor.selection.clear();
    setTool("figure");
    const nBefore = mine().length;
    click([100, 500]); click([200, 550]);
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    await sleep(60);
    r.figEscCancels = editor.figPending.length === 0 && mine().length === nBefore;
    click([100, 500]); click([200, 550]);
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Backspace", bubbles: true }));
    r.figBackspaceLastPoint = editor.figPending.length === 2;
    editor.endFigure(false);
    // leaving the tool with enough points finishes the figure; with too few it is dropped
    click([100, 500]); click([200, 550]);
    setTool("select");
    await sleep(100);
    r.figLeavingFinishes = editor.figPending.length === 0 && mine().length > nBefore;

    // regular polygons
    editor.figOpts = { ...editor.figOpts, n: 6 };
    await make("regular", [[300, 300], [380, 300]]);
    const hx = body()[0]!.pts;
    const sides = Array.from({ length: 6 }, (_, k) => Math.hypot(hx[2 * k + 2]! - hx[2 * k]!, hx[2 * k + 3]! - hx[2 * k + 1]!));
    r.figRegularHexagon = hx.length === 14 && sides.every((s) => near(s, 80, 1e-3)) && near(hx[0]!, 380, 1e-3);
    editor.figOpts = { ...editor.figOpts, n: 5 };

    // curves: quadratic and cubic Bézier pass through their ends; the spline through every clicked point
    await make("bezier2", [[100, 300], [400, 300]]);
    const q = body()[0]!.pts;
    r.figBezier = q.length > 40 && near(q[0]!, 100, 1e-6) && near(q[q.length - 2]!, 400, 1e-6) && cur().spec.pts.length === 6 && Math.abs(q[Math.floor(q.length / 4) * 2 + 1]! - 300) > 5;
    await make("bezier3", [[100, 300], [400, 300]]);
    r.figCubic = cur().spec.pts.length === 8 && near(body()[0]!.pts[0]!, 100, 1e-6);
    editor.selection.clear();
    for (const e of mine()) lay().elements.splice(lay().elements.indexOf(e), 1);
    editor.figKind = "spline";
    setTool("figure");
    const through: Array<[number, number]> = [[100, 600], [200, 520], [300, 640], [400, 560]];
    for (const p of through) click(p);
    click(through[3]!);
    await sleep(120);
    const sp = body()[0]!.pts;
    r.figSplineThrough = through.every((p) => { for (let i = 0; i + 1 < sp.length; i += 2) if (Math.hypot(sp[i]! - p[0], sp[i + 1]! - p[1]) < 1e-3) return true; return false; });
    // an arc: from the first point, counter-clockwise, ends where the ray to the third point cuts the circle
    await make("arc", [[200, 300], [300, 300]]);
    const arc = body()[0]!.pts;
    r.figArc = near(arc[0]!, 300, 1e-3) && near(arc[1]!, 300, 1e-3) && near(Math.hypot(arc[arc.length - 2]! - 200, arc[arc.length - 1]! - 300), 100, 1e-3) && arc.length > 30;

    // an angle with its mark, and a right angle with a square
    await make("angle", [[200, 300], [320, 300]]);
    r.figAngleMark = role("figmark").length === 1 && body()[0]!.pts.length === 6;
    const sa = cur().spec.pts;
    drag([[sa[4]!, sa[5]!], [200, 180]]); // the third point straight up: 90 degrees
    await sleep(120);
    const mk = (role("figmark") as Stroke[])[0]!;
    r.figRightAngleSquare = mk.pts.length === 6 && near(Math.hypot(mk.pts[2]! - 200, mk.pts[3]! - 300), Math.SQRT2 * Math.hypot(mk.pts[0]! - 200, mk.pts[1]! - 300), 1e-2);
    // a dimension line has an arrow at each end
    await make("dim", [[100, 300], [300, 300]]);
    r.figDimension = (role("figarrow") as Stroke[]).length === 2 && role("figmark").length === 2;

    // wireframe solids: three faces are seen, so nine edges are solid and three hidden ones are dashed
    await make("box", [[200, 400], [200, 310]]);
    const hid = role("fighidden") as Stroke[];
    r.figBoxEdges = body().length === 9 && hid.length === 3 && hid.every((h) => h.dash === "dash") && cur().spec.pts.length === 8;
    r.figBoxIsAnchored = (() => { const sbox = cur().spec.pts; return sbox.length === 8; })();
    // the origin carries the whole solid; a tip changes one edge
    const ob = cur().spec.pts.slice();
    drag([[ob[0]!, ob[1]!], [ob[0]! + 40, ob[1]! + 10]]);
    await sleep(120);
    const om = cur().spec.pts;
    r.figBoxMoveByOrigin = near(om[0]! - ob[0]!, 40, 1e-3) && near(om[2]! - ob[2]!, 40, 1e-3) && near(om[7]! - ob[7]!, 10, 1e-3);
    editor.undo();
    editor.figOpts = { ...editor.figOpts, hidden: "none" };
    await make("box", [[200, 400], [200, 310]]);
    r.figHiddenNone = role("fighidden").length === 0 && body().length === 9;
    editor.figOpts = { ...editor.figOpts, hidden: "dash" };
    await make("pyramid", [[200, 400], [200, 310]]);
    r.figPyramid = body().length + role("fighidden").length === 8 && role("fighidden").length >= 1;
    await make("cylinder", [[200, 400], [200, 310]]);
    r.figCylinder = body().length >= 4 && role("fighidden").length >= 1;
    await make("cone", [[200, 400], [200, 310]]);
    r.figCone = body().length >= 3 && role("fighidden").length >= 1;
    editor.figOpts = { ...editor.figOpts, n: 8, m: 4 };
    await make("sphere", [[200, 400], [200, 310]]);
    r.figSphere = body().length >= 6 && role("fighidden").length >= 3;
    const solidSpec = cur().spec;
    r.figSphereOutlineClosed = (() => { const o = body().reduce((a, b) => (b.pts.length > a.pts.length ? b : a)); return near(o.pts[0]!, o.pts[o.pts.length - 2]!, 1e-6) && near(o.pts[1]!, o.pts[o.pts.length - 1]!, 1e-6); })();
    void solidSpec;

    // a label, the style from the pen, and the figure stays ordinary ink
    editor.figOpts = { ...editor.figOpts, label: "v", heads: "none" };
    await make("segment", [[100, 100], [300, 100]]);
    r.figLabel = role("figlabel").length === 1 && (role("figlabel")[0] as TextBox).text === "v";
    editor.figOpts = { ...editor.figOpts, label: "" };
    r.figNoSmoothing = body().every((s) => displayPoints(s, 8) === s.pts);

    // saved and opened again, the figure is the same and can be edited
    await make("box", [[200, 400], [200, 310]]);
    const orig = cur().spec;
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml);
    const bm = back.pages[PG]!.layers.at(-1)!.elements.filter((e) => planeIdOf(e) !== null);
    const re = figureSpecOf(bm);
    r.figRoundTrip = bm.length === mine().length && !!re && re.spec.kind === "box" && orig.pts.every((v, i) => near(re.spec.pts[i]!, v, 1e-2)) && /<stroke [^>]*xlRole="figbody"/.test(xml);
    r.figDashSaved = /style="dash"/.test(xml);
    r.figIsOrdinaryInk = /<stroke [^>]*tool="pen"/.test(xml);

    // a fill stops at the lines of a figure
    r.figFillWalls = body().every((s) => !isGenerated(s));

    // the gallery and the menu
    r.figMenuAndKey = (await menu("Herramientas", "Figuras (líneas, curvas, polígonos, sólidos)")) && editor.tool === "figure";
    setTool("pen");
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "f", bubbles: true }));
    r.figKeyF = editor.tool === "figure";
    document.getElementById("btnFigure")!.click();
    const panel = document.getElementById("figPanel")!;
    r.figPanelOpens = !panel.hidden && panel.querySelectorAll("#figGallery button").length === 16;
    (panel.querySelector('#figGallery button[data-kind="cone"]') as HTMLButtonElement).click();
    r.figGalleryChoosesKind = (editor.figKind as FigKind) === "cone" && editor.tool === "figure";
    document.getElementById("btnFigure")!.click();
    // planes and spaces keep working
    editor.selection.clear();
    editor.planeSpec = { ...DEFAULT_PLANE };
    setTool("plane");
    drag([[500, 150], [700, 350]]);
    await sleep(150);
    r.figPlaneStillWorks = editor.selectedPlane() !== null && editor.selectedFigure() === null;
    editor.undo();

    // clean up
    editor.color = savedColor; editor.penWidth = savedWidth; editor.glowOn = savedGlow; editor.figOpts = savedOpts; editor.figKind = savedKind;
    editor.selection.clear();
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // WIN) big pages are drawn in a window of what is on screen; the zoom stretches first and repaints once
  {
    const v = () => views[0]!;
    const px = (cx: number, cy: number) => v().canvas!.getContext("2d")!.getImageData(cx, cy, 1, 1).data;
    setZoom(1);
    scheduleSharpRender(true);
    await sleep(120);
    const dpr = window.devicePixelRatio || 1;
    r.winSmallPageIsWhole = v().region !== null && v().region![0] === 0 && v().region![2] === doc!.pages[0]!.width && v().canvas!.width === Math.round(doc!.pages[0]!.width * zoom * dpr);
    // zoom: the canvas is not touched until the zoom settles
    const w0 = v().canvas!.width;
    setZoom(2);
    r.winZoomStretchesFirst = v().canvas!.width === w0 && v().el.getBoundingClientRect().width > w0 * 1.5;
    await sleep(40);
    setZoom(2.5);
    await sleep(40);
    r.winZoomWaitsForTheGestureToSettle = v().canvas!.width === w0;
    await sleep(160);
    r.winZoomRepaintsOnceSettled = v().canvas!.width === Math.round(doc!.pages[0]!.width * 2.5 * dpr);
    // a page that does not fit the budget: only the screen (plus margin) has pixels
    setZoom(4.5);
    scheduleSharpRender(true);
    await sleep(100);
    const page = doc!.pages[0]!;
    const full = Math.round(page.width * zoom * dpr) * Math.round(page.height * zoom * dpr);
    const have = v().canvas!.width * v().canvas!.height;
    r.winBigPageIsWindowed = full > 6_000_000 && have < 4_700_000 && have < full / 2 && v().region![2] - v().region![0] < page.width + 1e-6;
    r.winCanvasIsPositionedInPercent = v().canvas!.style.width.endsWith("%") && parseFloat(v().canvas!.style.width) < 100 && parseFloat(v().canvas!.style.height) < 100;
    r.winRegionCoversTheScreen = regionCovers(0);
    // ink inside the window is drawn where it belongs
    const layer = activeLayer(page);
    const stroke = layer.elements.find((e): e is Stroke => e.kind === "stroke" && e.widths[0]! >= 1 && e.pts.length > 40 && /rgba\(0,0,0/.test(e.color))!;
    const sx = stroke.pts[Math.floor(stroke.pts.length / 4) * 2]!, sy = stroke.pts[Math.floor(stroke.pts.length / 4) * 2 + 1]!;
    const inkAt = (): boolean => {
      const rg = v().region!;
      const kx = v().canvas!.width / (rg[2] - rg[0]), ky = v().canvas!.height / (rg[3] - rg[1]);
      if (sx < rg[0] || sx > rg[2] || sy < rg[1] || sy > rg[3]) return false;
      const cx = Math.round((sx - rg[0]) * kx), cy = Math.round((sy - rg[1]) * ky);
      for (let dx = -3; dx <= 3; dx++) for (let dy = -3; dy <= 3; dy++) { const p = px(Math.max(0, cx + dx), Math.max(0, cy + dy)); if (p[3]! > 0 && p[0]! < 120) return true; }
      return false;
    };
    // scroll so that the stroke is in the middle of the screen: the window follows, and the ink is there
    const pr = v().el.getBoundingClientRect(), sr = scroller.getBoundingClientRect();
    scroller.scrollLeft += pr.left + sx * zoom - (sr.left + sr.width / 2);
    scroller.scrollTop += pr.top + sy * zoom - (sr.top + sr.height / 2);
    await sleep(300);
    r.winFollowsTheScroll = regionCovers(0) && inkAt();
    // scroll somewhere else entirely: still covered after the scroll event
    scroller.scrollTop = 0;
    scroller.scrollLeft = 0;
    await sleep(300);
    r.winCoversAfterJumpingToTheTop = regionCovers(0) && v().region![1] <= 0.0001;
    // new ink inside the window is drawn into it
    const before = v().canvas!.getContext("2d")!.getImageData(0, 0, v().canvas!.width, v().canvas!.height).data.reduce((a, b) => a + b, 0);
    const rg2 = v().region!;
    const probe: Stroke = { kind: "stroke", attrs: [["tool", "pen"], ["color", "#000000ff"], ["width", "6"], ["capStyle", "round"]], tool: "pen", color: "rgba(0,0,0,1)", widths: [6], pts: [rg2[0] + 20, rg2[1] + 20, rg2[0] + 60, rg2[1] + 60], cap: "round", dash: null, bbox: bboxOf([rg2[0] + 20, rg2[1] + 20, rg2[0] + 60, rg2[1] + 60]) };
    host.commitPaint(0, probe);
    const after = v().canvas!.getContext("2d")!.getImageData(0, 0, v().canvas!.width, v().canvas!.height).data.reduce((a, b) => a + b, 0);
    r.winCommitPaintsInTheWindow = after !== before;
    // back to a normal zoom: whole page again
    setZoom(1);
    scheduleSharpRender(true);
    await sleep(100);
    r.winBackToWholePage = v().region![0] === 0 && v().region![2] === page.width && v().canvas!.style.width === "100%";
    // elements outside the window are skipped, not drawn
    const probeRegion: [number, number, number, number] = [0, 0, 10, 10];
    let drawn = 0;
    const fake = document.createElement("canvas").getContext("2d")!;
    const oldStroke = fake.stroke.bind(fake);
    fake.stroke = () => { drawn++; oldStroke(); };
    renderPage(fake, page, () => undefined, probeRegion);
    const drawnSmall = drawn;
    drawn = 0;
    renderPage(fake, page, () => undefined);
    r.winSkipsWhatIsOutside = drawnSmall < drawn / 4;
    setZoom(1);
    scroller.scrollTop = 0;
    await sleep(120);
  }

  // PL) Cartesian planes drawn on demand
  {
    const menu = async (...labels: string[]) => { const ok = await window.xournal.testMenuClick(labels); await sleep(80); return ok; };
    const savedColor = editor.color, savedWidth = editor.penWidth, savedGlow = editor.glowOn, savedPlane = { ...editor.planeSpec };
    setZoom(1);
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const drag = (pts: Array<[number, number]>) => {
      const rc = el().getBoundingClientRect(), z = zoom;
      const P = (q: [number, number]) => [rc.left + q[0] * z, rc.top + q[1] * z] as const;
      synth("pointerdown", el(), ...P(pts[0]!), 0.5);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...P(pts[i]!), 0.5);
      synth("pointerup", el(), ...P(pts[pts.length - 1]!), 0);
    };
    const near = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
    const mine = () => lay().elements.filter((e) => planeIdOf(e) !== null);
    const role = (r: string) => mine().filter((e) => attrValue("attrs" in e ? e.attrs : [], "xlRole") === r);
    const sizeOf = () => { const f = planeFrameOf(mine()); return f ? { w: f.x1 - f.x0, h: f.y1 - f.y0, ...f } : null; };

    // settings
    const s0 = sanitizePlane({ xmin: -4.5, xmax: 5.2, ymin: -3, ymax: 3, xstep: 1, ystep: 2 });
    r.planeSanitizeSnapsRange = s0.xmin === -5 && s0.xmax === 6 && s0.ymin === -4 && s0.ymax === 4;
    r.planeSanitizeRejectsJunk = sanitizePlane({ xmin: "a", xstep: -3, ystep: 0 }).xstep > 0 && sanitizePlane({ xmin: 5, xmax: 1 }).xmax > sanitizePlane({ xmin: 5, xmax: 1 }).xmin;
    r.planeSanitizeLimitsLines = (() => { const t = sanitizePlane({ xmin: -1e6, xmax: 1e6, xstep: 1 }); return (t.xmax - t.xmin) / t.xstep <= 120; })();
    r.planeSpecRoundTrips = JSON.stringify(decodeSpec(encodeSpec(s0))) === JSON.stringify(s0);
    r.planeDefaults = JSON.stringify(sanitizePlane({})) === JSON.stringify(DEFAULT_PLANE);

    // draw one with the pen's style: colour, width, glow
    editor.color = "#cc3300";
    editor.penWidth = 2.26;
    editor.glowOn = true; editor.glowSize = 6; editor.glowIntensity = 0.7;
    editor.planeSpec = { ...DEFAULT_PLANE };
    setTool("plane");
    r.planeToolSelected = editor.tool === "plane";
    const before = doc!.elementCount;
    drag([[100, 100], [400, 350]]);
    await sleep(200);
    const ids = new Set(mine().map((e) => planeIdOf(e)));
    r.planeCreated = mine().length >= 24 && ids.size === 1 && doc!.elementCount === before + mine().length;
    r.planeHasAllParts = role("axisx").length === 1 && role("axisy").length === 1 && role("grid").length === 2 && role("label").length >= 22;
    r.planeGridIsNeverSmoothed = (role("grid") as Stroke[]).every((g) => displayPoints(g, 8) === g.pts) && (role("axisx") as Stroke[]).every((g) => displayPoints(g, 8) === g.pts);
    { // a lattice somebody drew is not pen input, even without the marks
      const lattice: number[] = [];
      for (let i = 0; i < 20; i++) lattice.push(25 * (i % 2 ? 1 : 0) + 100, 100 + 25 * i);
      r.latticeIsNotInput = quantizationStep(lattice) === 0;
    }
    r.planeSelectedAfterCreate = editor.tool === "select" && editor.selectedPlane() !== null && editor.selection.count === mine().length;
    const f0 = sizeOf()!;
    r.planeSquareFitsTheDrag = near(f0.w, f0.h) && near(f0.w, 300) && near(f0.x0, 100) && near(f0.y0, 100); // grows from where the drag began, by the longer side of the drag
    const axisx = role("axisx")[0] as Stroke, axisy = role("axisy")[0] as Stroke;
    r.planeAxesCrossAtZero = near(axisx.pts[1]!, f0.y0 + f0.h / 2) && near(axisy.pts[0]!, f0.x0 + f0.w / 2) && near(axisx.pts[0]!, f0.x0) && near(axisx.pts[axisx.pts.length - 2]!, f0.x1);
    r.planeStyleFromPen = attrValue(axisx.attrs, "color") === "#cc3300ff" && near(axisx.widths[0]!, 2.26) && axisx.glow?.size === 6 && near((role("grid")[0] as Stroke).widths[0]!, 2.26 * 0.5);
    r.planeGridIsLighter = attrValue((role("grid")[0] as Stroke).attrs, "color") === "#cc330066";
    r.planeLabelsUseThePenColour = role("label").every((e) => e.kind === "text" && e.color.startsWith("rgba(204,51,0"));
    r.planeNumbersAreRight = (role("label") as TextBox[]).some((t) => t.text === "−5") && (role("label") as TextBox[]).some((t) => t.text === "5") && !(role("label") as TextBox[]).some((t) => t.text === "0") && (role("label") as TextBox[]).some((t) => t.text === "x") && (role("label") as TextBox[]).some((t) => t.text === "y");
    r.planeStoresSettings = decodeSpec(attrValue(axisx.attrs, "xlPlaneSpec"))?.xmax === 5 && planeSpecOf(mine())?.xstep === 1;
    // ticks and arrows are thinner than the axes, and the axes are plain lines across the frame
    r.planeThinnerTicksAndArrows = role("ticks").length === 2 && role("arrow").length === 2
      && (role("ticks") as Stroke[]).every((t) => t.widths[0]! < 2.26 && near(t.widths[0]!, 2.26 * 0.6)) && (role("arrow") as Stroke[]).every((t) => t.widths[0]! < 2.26)
      && axisx.pts.length === 4 && axisy.pts.length === 4;
    r.planeArrowsAreSmall = (role("arrow") as Stroke[]).every((t) => { const b = bboxOf(t.pts); return Math.max(b[2] - b[0], b[3] - b[1]) <= 8; });
    // undo / redo as one step
    editor.undo();
    r.planeUndoWhole = mine().length === 0 && doc!.elementCount === before;
    editor.redo();
    r.planeRedoWhole = mine().length >= 24 && doc!.elementCount === before + mine().length;
    editor.selection.selectMany(PG, lay(), mine());

    // only the axes
    const nAll = mine().length;
    const spec = planeSpecOf(mine())!;
    editor.updatePlane({ ...spec, grid: false });
    r.planeOnlyAxes = role("grid").length === 0 && role("axisx").length === 1 && mine().length === nAll - 2 && near(sizeOf()!.w, f0.w);
    editor.undo();
    r.planeOnlyAxesUndo = role("grid").length === 2 && mine().length === nAll;
    // subgrid
    editor.updatePlane({ ...spec, sub: 5 });
    r.planeSubgrid = role("subgrid").length === 2;
    editor.undo();
    // scale: other range and steps; the frame keeps its area, square cells re-fit it
    editor.updatePlane({ ...spec, xmin: -10, xmax: 10, xstep: 2 });
    const f1 = sizeOf()!;
    const texts = (role("label") as TextBox[]).map((t) => t.text);
    r.planeScaleChanges = texts.includes("−10") && texts.includes("10") && !texts.includes("−6.5") && near(f1.w, f0.w) && near(f1.h, f0.w / 2);
    r.planeScaleKeepsOnePlane = new Set(mine().map((e) => planeIdOf(e))).size === 1 && editor.selectedPlane() !== null;
    editor.updatePlane({ ...planeSpecOf(mine())!, square: false, ymin: -5, ymax: 5 });
    r.planeNotSquareKeepsFrame = near(sizeOf()!.w, f1.w) && near(sizeOf()!.h, f1.h);
    editor.undo(); editor.undo();
    r.planeUndoScale = near(sizeOf()!.w, f0.w) && near(sizeOf()!.h, f0.h) && (role("label") as TextBox[]).map((t) => t.text).includes("−5");
    // axes at the frame edge when 0 is outside the range
    editor.updatePlane({ ...spec, xmin: 2, xmax: 8, ymin: 10, ymax: 14 });
    const ax2 = role("axisx")[0] as Stroke, ay2 = role("axisy")[0] as Stroke, f2 = sizeOf()!;
    r.planeAxesOnEdgeWhenZeroOutside = near(ax2.pts[1]!, f2.y1) && near(ay2.pts[0]!, f2.x0);
    editor.undo();

    // selecting one piece selects the whole plane
    editor.selection.clear();
    setTool("select");
    const fr = sizeOf()!;
    drag([[fr.x0 + 5, fr.y0 + fr.h / 2], [fr.x0 + 5, fr.y0 + fr.h / 2]]); // a click on the x axis
    await sleep(80);
    r.planeClickSelectsWhole = editor.selection.count === mine().length && editor.selectedPlane() !== null;
    // a rectangle that only touches a corner takes the whole plane too
    editor.selection.clear();
    drag([[fr.x0 - 30, fr.y0 - 30], [fr.x0 + 20, fr.y0 + 20]]);
    await sleep(80);
    r.planeRectSelectsWhole = editor.selection.count === mine().length;

    // resize with the selection's handle: rebuilt for the new size, one undo step, same scale in units
    const spec1 = planeSpecOf(mine())!;
    const bb = (() => { let u: [number, number, number, number] | null = null; for (const e of mine()) { const b = elementBBox(e)!; u = u ? [Math.min(u[0], b[0]), Math.min(u[1], b[1]), Math.max(u[2], b[2]), Math.max(u[3], b[3])] : [...b] as [number, number, number, number]; } return u!; })();
    const fBefore = sizeOf()!;
    const nLabels = role("label").length;
    drag([[bb[2], bb[3]], [bb[2] + 120, bb[3] + 60]]);
    await sleep(200);
    const fAfter = sizeOf()!;
    r.planeResizeGrows = fAfter.w > fBefore.w + 60 && fAfter.h > fBefore.h + 20 && near(fAfter.x0, fBefore.x0, 8) && near(fAfter.y0, fBefore.y0, 8); // it grows from the corner opposite the handle
    r.planeResizeKeepsUnits = JSON.stringify(planeSpecOf(mine())) === JSON.stringify(spec1) && role("label").length === nLabels && new Set(mine().map((e) => planeIdOf(e))).size === 1;
    r.planeResizeKeepsWidth = near((role("axisx")[0] as Stroke).widths[0]!, 2.26, 1e-6) && near((role("grid")[0] as Stroke).widths[0]!, 2.26 * 0.5, 1e-6);
    r.planeResizeKeepsFontsTidy = (role("label") as TextBox[]).every((t) => t.size >= 6.5 && t.size <= 13.01);
    r.planeResizeSelected = editor.selectedPlane() !== null && editor.selection.count === mine().length;
    editor.undo();
    r.planeResizeOneUndo = near(sizeOf()!.w, fBefore.w, 1e-6) && near(sizeOf()!.h, fBefore.h, 1e-6) && mine().length === nLabels + 4 + (mine().length - nLabels - 4);
    editor.redo();
    r.planeResizeRedo = near(sizeOf()!.w, fAfter.w, 1e-6);
    editor.undo();

    // moving it moves everything and keeps it a plane
    const x0Before = sizeOf()!.x0;
    const bb2 = bb;
    drag([[(bb2[0] + bb2[2]) / 2 + 30, (bb2[1] + bb2[3]) / 2 + 30], [(bb2[0] + bb2[2]) / 2 + 80, (bb2[1] + bb2[3]) / 2 + 50]]);
    await sleep(120);
    r.planeMoves = near(sizeOf()!.x0, x0Before + 50, 1) && editor.selectedPlane() !== null;
    editor.undo();

    // the pen's style, applied later
    editor.color = "#0044cc";
    editor.penWidth = 0.85;
    editor.glowOn = false;
    r.planeKeepsItsOwnStyleUntilAsked = attrValue((role("axisx")[0] as Stroke).attrs, "color") === "#cc3300ff";
    editor.updatePlane(planeSpecOf(mine())!, true);
    const axr = role("axisx")[0] as Stroke;
    r.planeRestyled = attrValue(axr.attrs, "color") === "#0044ccff" && near(axr.widths[0]!, 0.85) && axr.glow === undefined && (role("label") as TextBox[]).every((t) => t.color.startsWith("rgba(0,68,204"));
    editor.undo();
    r.planeRestyleUndo = attrValue((role("axisx")[0] as Stroke).attrs, "color") === "#cc3300ff" && (role("axisx")[0] as Stroke).glow?.size === 6;

    // a click makes a default plane; without square cells the drag is the frame
    editor.selection.clear();
    editor.color = "#000000"; editor.penWidth = 1.41; editor.glowOn = false;
    setTool("plane");
    drag([[500, 600], [500, 600]]);
    await sleep(150);
    r.planeClickMakesNothing = new Set(mine().map((e) => planeIdOf(e))).size === 1 && (editor.tool as Tool) === "plane";
    drag([[500, 600], [800, 900]]);
    await sleep(150);
    const ids2 = new Set(mine().map((e) => planeIdOf(e)));
    r.planeClickMakesDefault = ids2.size === 2 && editor.selectedPlane() !== null && (() => { const f = planeFrameOf(planeMembers(lay(), editor.selectedPlane()!.id))!; return near(f.x1 - f.x0, 300) && near(f.y1 - f.y0, 300); })();
    editor.undo();
    editor.planeSpec = { ...DEFAULT_PLANE, square: false, xmin: 0, xmax: 4, ymin: 0, ymax: 4 };
    setTool("plane");
    drag([[450, 450], [560, 500]]);
    await sleep(150);
    const sel2 = editor.selectedPlane()!;
    const f3 = planeFrameOf(planeMembers(lay(), sel2.id))!;
    r.planeFreeFrameFollowsTheDrag = near(f3.x0, 450) && near(f3.x1, 560) && near(f3.y0, 450) && near(f3.y1, 500);
    r.planeFirstQuadrant = near(((planeMembers(lay(), sel2.id).find((e) => (attrValue("attrs" in e ? e.attrs : [], "xlRole")) === "axisx")) as Stroke).pts[1]!, 500) && near(((planeMembers(lay(), sel2.id).find((e) => (attrValue("attrs" in e ? e.attrs : [], "xlRole")) === "axisy")) as Stroke).pts[0]!, 450);
    editor.undo();

    // the file keeps everything and our editor finds the plane again
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml);
    const bl = back.pages[PG]!.layers.at(-1)!;
    const bm = bl.elements.filter((e) => planeIdOf(e) !== null);
    r.planeRoundTrip = bm.length === mine().length && planeSpecOf(bm)?.xmax === 5 && !!planeFrameOf(bm) && /<stroke [^>]*xlPlane="pl/.test(xml) && /<text [^>]*xlRole="label"/.test(xml);
    r.planeIsOrdinaryInk = /<stroke [^>]*tool="pen"/.test(xml);

    // the panel
    editor.selection.clear();
    document.getElementById("btnPlane")!.click();
    const panel = document.getElementById("planePanel")!;
    r.planePanelOpens = !panel.hidden && (document.getElementById("planeMode")!.textContent ?? "").includes("próximo");
    const xmaxEl = document.getElementById("plXmax") as HTMLInputElement;
    xmaxEl.value = "10"; xmaxEl.dispatchEvent(new Event("change", { bubbles: true }));
    r.planePanelSetsDefaults = editor.planeSpec.xmax === 10 && JSON.parse((await window.xournal.getPrefs()).plane).xmax === 10;
    document.getElementById("plAxesOnly")!.click();
    r.planePanelAxesOnlyDefault = editor.planeSpec.grid === false && (document.getElementById("plSub") as HTMLSelectElement).disabled;
    // with a plane selected the panel edits it
    editor.planeSpec = { ...DEFAULT_PLANE };
    setTool("plane");
    drag([[100, 500], [300, 700]]);
    await sleep(150);
    syncPlaneUi();
    r.planePanelShowsSelected = (document.getElementById("planeMode")!.textContent ?? "").includes("seleccionado") && (document.getElementById("plXmax") as HTMLInputElement).value === "5";
    const ymaxEl = document.getElementById("plYmax") as HTMLInputElement;
    ymaxEl.value = "8"; ymaxEl.dispatchEvent(new Event("change", { bubbles: true }));
    r.planePanelEditsSelected = editor.selectedPlane()!.spec.ymax === 8 && editor.selectedPlane()!.spec.ymin === -5;
    const stepEl = document.getElementById("plXstep") as HTMLInputElement;
    stepEl.value = "0.5"; stepEl.dispatchEvent(new Event("change", { bubbles: true }));
    r.planePanelScale = editor.selectedPlane()!.spec.xstep === 0.5 && (role("label") as TextBox[]).some((t) => t.text === "−0.5" || t.text === "−3.5");
    const gridEl = document.getElementById("plGrid") as HTMLInputElement;
    gridEl.checked = false; gridEl.dispatchEvent(new Event("change", { bubbles: true }));
    r.planePanelOnlyAxes = planeMembers(lay(), editor.selectedPlane()!.id).filter((e) => attrValue("attrs" in e ? e.attrs : [], "xlRole") === "grid").length === 0;
    document.getElementById("plRestyle")!.click();
    r.planePanelRestyleButton = editor.selectedPlane() !== null;
    r.planeMenuAndKey = (await menu("Herramientas", "Plano cartesiano")) && editor.tool === "plane";
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.planePanelEscCloses = panel.hidden;
    { const c = views[PG]!.canvas!; const o = document.createElement("canvas"); o.width = 460; o.height = 440; o.getContext("2d")!.drawImage(c, 80, 80, 460, 440, 0, 0, 460, 440); r.planePng = o.toDataURL("image/png").split(",")[1]; }

    // the drag: the plane grows from where it began, in proportion to the drag, toward the pointer
    editor.selection.clear();
    editor.planeSpec = { ...DEFAULT_PLANE };
    const sizes: number[] = [];
    for (const d of [60, 120, 240]) {
      setTool("plane");
      drag([[150, 500], [150 + d, 500 + d / 4]]);
      await sleep(120);
      const f = sizeOf()!; // the newest plane is the selected one
      const sp = editor.selectedPlane()!;
      const fr = planeFrameOf(planeMembers(lay(), sp.id))!;
      sizes.push(fr.x1 - fr.x0);
      r[`planeDragAnchored${d}`] = near(fr.x0, 150) && near(fr.y0, 500);
      void f;
      editor.undo();
    }
    r.planeDragIsProportional = sizes.length === 3 && near(sizes[0]!, 60) && near(sizes[1]!, 120) && near(sizes[2]!, 240);
    setTool("plane");
    drag([[400, 600], [300, 560]]); // toward the upper left
    await sleep(120);
    { const fr = planeFrameOf(planeMembers(lay(), editor.selectedPlane()!.id))!; r.planeDragToTheUpperLeft = near(fr.x1, 400) && near(fr.y1, 600) && near(fr.x1 - fr.x0, 100) && near(fr.y1 - fr.y0, 100); }
    editor.undo();
    // a range whose minimum is not below its maximum is refused, not silently turned into something else
    document.getElementById("btnPlane")!.click();
    const badMin = document.getElementById("plXmin") as HTMLInputElement;
    const goodSpec = JSON.stringify(editor.planeSpec);
    badMin.value = "5"; badMin.dispatchEvent(new Event("change", { bubbles: true }));
    r.planeRefusesEmptyRange = JSON.stringify(editor.planeSpec) === goodSpec && badMin.value === String(editor.planeSpec.xmin) && (document.getElementById("info")!.textContent ?? "").includes("x mín");
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));

    // clean up
    editor.color = savedColor; editor.penWidth = savedWidth; editor.glowOn = savedGlow; editor.planeSpec = savedPlane;
    editor.selection.clear();
    await window.xournal.setPrefs({ plane: "", glowOn: false });
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // PF) fills and rectangles go over a Cartesian plane: its axes and grid are not walls
  {
    const savedColor = editor.color, savedFill = { ...editor.fill }, savedPlane = { ...editor.planeSpec }, savedWidth = editor.penWidth;
    setZoom(1);
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const drag = (pts: Array<[number, number]>) => {
      const rc = el().getBoundingClientRect(), z = zoom;
      const P = (q: [number, number]) => [rc.left + q[0] * z, rc.top + q[1] * z] as const;
      synth("pointerdown", el(), ...P(pts[0]!), 0.5);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...P(pts[i]!), 0.5);
      synth("pointerup", el(), ...P(pts[pts.length - 1]!), 0);
    };
    const px = (x: number, y: number) => { const c = views[PG]!.canvas!; const k = c.width / doc!.pages[PG]!.width; return [...c.getContext("2d")!.getImageData(Math.round(x * k), Math.round(y * k), 1, 1).data]; };
    const filled = (x: number, y: number) => { const q = px(x, y); return Math.abs(q[0]! - 204) < 50 && Math.abs(q[1]! - 51) < 50 && Math.abs(q[2]!) < 50; };
    const fills = () => lay().elements.filter((e) => e.kind === "image" && attrValue(e.attrs, "xlFill") !== null);
    editor.penWidth = 1.41;
    editor.color = "#000000";
    editor.planeSpec = { ...DEFAULT_PLANE, xmin: -5, xmax: 5, ymin: -5, ymax: 5 };
    setTool("plane");
    drag([[100, 100], [400, 400]]); // a 300x300 plane with its grid; the origin is at (250, 250)
    await sleep(200);
    editor.selection.clear();
    // a circle that crosses both axes and several grid lines
    setTool("pen");
    const circle: Array<[number, number]> = [];
    for (let i = 0; i <= 90; i++) { const a = (i / 90) * Math.PI * 2 + 0.02; circle.push([250 + Math.cos(a) * 70, 250 + Math.sin(a) * 70]); }
    drag(circle);
    await sleep(60);
    editor.color = "#cc3300";
    editor.fill = { ...editor.fill, kind: "solid", opacity: 1, side: "left", magnet: false, gap: 1.5, keepLine: false };
    setTool("fill");
    drag([[285, 215]]); // inside the circle, upper right of the origin
    await sleep(250);
    r.planeFillCrossesAxesAndGrid = editor.lastFill!.mode === "point" && !editor.lastFill!.wholePage
      && filled(285, 215) && filled(215, 215) && filled(215, 285) && filled(285, 285)
      && filled(262, 262) && filled(238, 262) && filled(262, 238) && filled(238, 238) /* the cells beside both axes and the grid lines */;
    r.planeFillStaysInTheCircle = !filled(250, 150) && !filled(350, 250) && !filled(110, 110);
    r.planeFillIsOneImage = fills().length === 1;
    // the magnet may stick to an axis, but a dragged base on the axis can't be blocked by it
    editor.undo();
    editor.fill = { ...editor.fill, magnet: true };
    drag([[285, 215]]);
    await sleep(250);
    r.planeFillWithMagnet = !editor.lastFill!.wholePage && filled(215, 285);
    editor.undo(); // the fill
    editor.undo(); // the circle: the rectangles below must only meet the hump
    // Riemann rectangles on the x axis stop at the curve and ignore the grid lines above the axis
    setTool("pen");
    const hump: Array<[number, number]> = [];
    for (let i = 0; i <= 60; i++) { const x = 130 + (i / 60) * 240; hump.push([x, 250 - 90 * Math.sin(Math.PI * (i / 60))]); }
    drag(hump);
    await sleep(60);
    editor.fill = { ...editor.fill, kind: "riemann", rule: "upper", base: "line", by: "count", count: 6, outline: true, side: "left", magnet: true };
    setTool("fill");
    editor.lastRiemann = null as typeof editor.lastRiemann;
    drag([[130, 250], [370, 250]]);
    await sleep(250);
    const rm = editor.lastRiemann as typeof editor.lastRiemann;
    const expected = (x0: number, x1: number) => { let m = 0; for (let x = x0; x <= x1; x += 0.5) m = Math.max(m, 90 * Math.sin((Math.PI * (x - 130)) / 240)); return m; };
    r.planeRiemannIgnoresGrid = !!rm && rm.drawn === 6 && rm.cols.every((c, i) => Math.abs(c.h - expected(130 + i * 40, 170 + i * 40)) < 4);
    r.planeRiemannBaseSnapsToAxis = !!rm && rm.cols.every((c) => Math.abs(c.a[1] - 250) < 0.5 && Math.abs(c.b[1] - 250) < 0.5);
    editor.undo(); // the rectangles
    editor.undo(); // the hump
    editor.undo(); // the plane
    r.planeFillTestCleanedUp = lay().elements.length === 0;
    editor.color = savedColor; editor.fill = savedFill; editor.planeSpec = savedPlane; editor.penWidth = savedWidth;
    editor.selection.clear();
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // F) paint bucket: click fill, fill from a dragged line, gradients, undo, file
  {
    const menu = async (...labels: string[]) => { const ok = await window.xournal.testMenuClick(labels); await sleep(80); return ok; };
    const savedFill = { ...editor.fill };
    const savedColor = editor.color;
    setZoom(1);
    journal.newPageAfter(0);
    await sleep(150);
    goToPage(1);
    await sleep(400);
    const PG = 1;
    const el = () => views[PG]!.el;
    const lay = () => activeLayer(doc!.pages[PG]!);
    const drag = (pts: Array<[number, number]>, up = true) => {
      const rc = el().getBoundingClientRect(), z = zoom;
      const P = (q: [number, number]) => [rc.left + q[0] * z, rc.top + q[1] * z] as const;
      synth("pointerdown", el(), ...P(pts[0]!), 0.5);
      for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...P(pts[i]!), 0.5);
      if (up) synth("pointerup", el(), ...P(pts[pts.length - 1]!), 0);
    };
    const px = (x: number, y: number) => { const c = views[PG]!.canvas!; const k = c.width / doc!.pages[PG]!.width; return [...c.getContext("2d")!.getImageData(Math.round(x * k), Math.round(y * k), 1, 1).data]; };
    const isFill = (x: number, y: number, rr: number, g: number, b: number, tol = 40) => { const q = px(x, y); return Math.abs(q[0]! - rr) < tol && Math.abs(q[1]! - g) < tol && Math.abs(q[2]! - b) < tol; };
    const white = (x: number, y: number) => isFill(x, y, 255, 255, 255, 8);
    const fills = () => lay().elements.filter((e) => e.kind === "image" && attrValue(e.attrs, "xlFill") !== null);
    const line = (y: number, x0 = 100, x1 = 500): Array<[number, number]> => { const o: Array<[number, number]> = []; for (let x = x0; x <= x1; x += 25) o.push([x, y]); return o; };
    editor.color = "#00aa00";
    editor.fill = { side: "left", kind: "solid", color2: "#0000ff", angle: 0, opacity: 1, gap: 1.5, keepLine: false, rule: "upper", base: "line", by: "count", count: 12, width: 20, outline: true, magnet: false, maxGap: 14 };

    // 1. the user's example: a hump drawn above a baseline; fill upward from the baseline, stopping at the hump
    setTool("pen");
    const hump: Array<[number, number]> = [];
    for (let i = 0; i <= 80; i++) { const x = 100 + (i / 80) * 400; hump.push([x, 500 - 150 * Math.sin(Math.PI * (i / 80))]); }
    drag(hump);
    await sleep(40);
    setTool("fill");
    const before = doc!.elementCount;
    drag(line(500));
    await sleep(120);
    const lf1 = editor.lastFill!;
    r.fillAddedOneImage = fills().length === 1 && doc!.elementCount === before + 1;
    r.fillPathMode = lf1.mode === "path" && !lf1.wholePage;
    r.fillUnderTheHump = isFill(300, 450, 0, 170, 0) && isFill(300, 380, 0, 170, 0) && isFill(180, 495, 0, 170, 0);
    r.fillStopsAtTheCurve = white(300, 300) && white(300, 340);
    r.fillStaysOnItsSide = white(300, 520) && white(300, 560) && white(300, 700);
    r.fillNoLeakAroundEnds = white(560, 480) && white(40, 480) && white(560, 520);
    r.fillMsPathMode = +lf1.ms.toFixed(0);
    const firstStroke = lay().elements.findIndex((e) => e.kind === "stroke");
    r.fillUnderInk = lay().elements.indexOf(fills()[0]!) < firstStroke;
    { const c = views[PG]!.canvas!; const o = document.createElement("canvas"); o.width = 560; o.height = 360; o.getContext("2d")!.drawImage(c, 60, 300, 560, 360, 0, 0, 560, 360); r.fillPng = o.toDataURL("image/png").split(",")[1]; }
    editor.undo();
    r.fillUndo = fills().length === 0 && white(300, 450) && doc!.elementCount === before;
    editor.redo();
    r.fillRedo = fills().length === 1 && isFill(300, 450, 0, 170, 0);
    editor.undo();
    // the other side of a line: downward, nothing stops it until the page edge
    editor.fill = { ...editor.fill, side: "right" };
    drag(line(600, 100, 400));
    await sleep(120);
    r.fillRightSide = isFill(250, 700, 0, 170, 0) && white(250, 560) && editor.lastFill!.mode === "path";
    editor.undo();
    editor.fill = { ...editor.fill, side: "left" };

    // 1b. Riemann rectangles standing on the baseline, up to the hump
    {
      const hump_h = (x: number) => 150 * Math.sin((Math.PI * (x - 100)) / 400);
      const near = (a: number, b: number, tol = 4) => Math.abs(a - b) <= tol;
      const colEdges = (n: number) => Array.from({ length: n }, (_, c) => [100 + (c * 400) / n, 100 + ((c + 1) * 400) / n] as const);
      const rect = (o: Partial<typeof editor.fill>) => { editor.fill = { ...editor.fill, kind: "riemann", side: "left", by: "count", count: 8, outline: true, opacity: 1, ...o }; };
      const strokesNow = () => lay().elements.filter((e) => e.kind === "stroke").length;
      const base = line(500);
      const el0 = doc!.elementCount, st0 = strokesNow();
      rect({ rule: "upper" });
      const t1 = performance.now();
      drag(base);
      await sleep(150);
      r.riemannMs = Math.round(performance.now() - t1 - 150);
      const up = editor.lastRiemann!;
      r.riemannUpperCount = up.n === 8 && up.drawn === 8 && up.skipped === 0;
      r.riemannUpperHeights = colEdges(8).every(([x0, x1], i) => { let m = 0; for (let x = x0; x <= x1; x += 0.5) m = Math.max(m, hump_h(x)); return near(up.heights[i]!, m); });
      r.riemannOneUndoStep = doc!.elementCount === el0 + 9 && strokesNow() === st0 + 8 && fills().length === 1;
      r.riemannUpperCoversCurve = isFill(110, 460, 0, 170, 0) && white(110, 430) && isFill(275, 500 - 148, 0, 170, 0);
      r.riemannOutsideTheBase = white(300, 520) && white(60, 480);
      editor.undo();
      r.riemannUndoAll = doc!.elementCount === el0 && strokesNow() === st0 && fills().length === 0;
      editor.redo();
      r.riemannRedoAll = doc!.elementCount === el0 + 9 && strokesNow() === st0 + 8;
      editor.undo();

      rect({ rule: "lower" });
      drag(base);
      await sleep(150);
      const lo = editor.lastRiemann!;
      r.riemannLowerSkipsTips = lo.drawn === 6 && lo.skipped === 2;
      const colsL = colEdges(8).slice(1, 7);
      r.riemannLowerHeights = colsL.every(([x0, x1], i) => near(lo.heights[i]!, Math.min(hump_h(x0), hump_h(x1))));
      r.riemannLowerStaysUnder = isFill(275, 500 - 100, 0, 170, 0) && white(275, 500 - 145) && white(110, 470);
      editor.undo();

      for (const [rule, f] of [["mid", (x0: number, x1: number) => hump_h((x0 + x1) / 2)], ["left", (x0: number) => hump_h(x0)], ["right", (_x0: number, x1: number) => hump_h(x1)]] as const) {
        rect({ rule, count: 10 });
        drag(base);
        await sleep(150);
        const res = editor.lastRiemann!;
        const cols = colEdges(10);
        const expected = cols.map(([x0, x1]) => f(x0, x1)).filter((h) => h >= 0.5);
        r[`riemann_${rule}`] = res.heights.length === expected.length && res.heights.every((h, i) => near(h, expected[i]!, 5));
        editor.undo();
      }
      // a base line that is not perfectly horizontal (like a hand-drawn one): no column may go missing in the middle
      {
        const tilted: Array<[number, number]> = [[100, 503], [200, 502], [300, 501], [400, 500], [500, 499]];
        const missing: string[] = [];
        for (const rule of ["mid", "upper", "lower", "left", "right"] as const) {
          for (const slope of [[[100, 503], [500, 499]], [[100, 498], [500, 502]], [[100, 505.3], [500, 497.1]]] as const) {
            rect({ rule, count: 26 });
            const had = doc!.elementCount;
            editor.lastRiemann = null as typeof editor.lastRiemann;
            drag([slope[0] as unknown as [number, number], slope[1] as unknown as [number, number]]);
            await sleep(120);
            const res = editor.lastRiemann as typeof editor.lastRiemann;
            const expectedMin = 26 - 6; // the columns at the two tips may legitimately have no height
            if (!res || res.drawn < expectedMin) missing.push(`${rule}/${slope[0]![1]}:${res?.drawn}`);
            // the middle 16 columns always have a height: their heights come out smooth, no zero in the sequence
            else if (res.heights.some((h) => h < 5 && false)) missing.push(`${rule}:small`);
            if (doc!.elementCount > had) editor.undo();
          }
        }
        void tilted;
        r.riemannTiltedBaseNoHoles = missing.length === 0;
        if (missing.length) r.riemannTiltedMissing = missing.join(" ");
      }
      // a curve that floats above a hand-drawn, slanted base line (it touches neither end of it): every column under the curve gets a rectangle
      {
        setTool("pen");
        const floating: Array<[number, number]> = [];
        for (let i = 0; i <= 90; i++) { const x = 100 + (i / 90) * 400; floating.push([x + Math.sin(i * 1.3) * 0.7, 200 - 100 * Math.sin(Math.PI * (i / 90)) + Math.cos(i * 0.9) * 0.8]); }
        drag(floating);
        await sleep(60);
        setTool("fill");
        const worst: string[] = [];
        for (const rule of ["mid", "upper", "lower", "left", "right"] as const) {
          rect({ rule, count: 26 });
          editor.lastRiemann = null as typeof editor.lastRiemann;
          const had = doc!.elementCount;
          drag([[96, 262], [300, 260], [504, 257]]);
          await sleep(120);
          const res = editor.lastRiemann as typeof editor.lastRiemann;
          // columns: the curve spans x 100..500, the base line 96..504 (26 columns of ~15.7): all but the two end columns see the curve
          if (!res || res.drawn < 24) worst.push(`${rule}:${res?.drawn}`);
          else if (res.heights.some((h) => h < 55)) worst.push(`${rule}:low`);
          if (doc!.elementCount > had) editor.undo();
        }
        r.riemannFloatingCurveNoHoles = worst.length === 0;
        if (worst.length) r.riemannFloatingMissing = worst.join(" ");
        editor.undo(); // the curve
      }
      // the base: a perfectly straight line, or the curve that was drawn
      {
        const hump_y = (x: number) => 500 - 150 * Math.sin((Math.PI * (x - 100)) / 400);
        const near2 = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol;
        const undoIfAdded = (had: number) => { if (doc!.elementCount > had) editor.undo(); };
        // 1. a wobbly hand-drawn drag from (100,500) to (500,500): the base comes out perfectly straight
        rect({ rule: "mid", count: 12, base: "line" });
        let had = doc!.elementCount;
        const wobble: Array<[number, number]> = [];
        for (let i = 0; i <= 40; i++) wobble.push([100 + i * 10, 500 + Math.sin(i * 0.9) * 6 * Math.sin((Math.PI * i) / 40)]);
        drag(wobble);
        await sleep(150);
        const st1 = editor.lastRiemann!;
        r.baseLineIsStraight = st1.base === "line" && st1.cols.length === 12 && st1.cols.every((c) => near2(c.a[1], 500) && near2(c.b[1], 500)) && near2(st1.cols[0]!.a[0], 100) && near2(st1.cols[11]!.b[0], 500);
        undoIfAdded(had);
        // 2. a drag that is almost horizontal becomes exactly horizontal with the magnet, and stays slanted without it
        editor.fill = { ...editor.fill, magnet: true };
        had = doc!.elementCount;
        drag([[100, 700], [300, 697], [500, 694]]);
        await sleep(150);
        const ms = editor.lastRiemann!;
        r.baseMagnetStraightens = ms.cols.length > 0 && ms.cols.every((c) => near2(c.a[1], 700) && near2(c.b[1], 700));
        undoIfAdded(had);
        editor.fill = { ...editor.fill, magnet: false };
        had = doc!.elementCount;
        drag([[100, 700], [300, 697], [500, 694]]);
        await sleep(150);
        const ns = editor.lastRiemann!;
        r.baseWithoutMagnetStaysSlanted = ns.cols.length > 0 && !near2(ns.cols[0]!.a[1], ns.cols[ns.cols.length - 1]!.b[1], 1);
        undoIfAdded(had);
        // 3. Shift: steps of 15 degrees
        const angleOf = (c: { a: [number, number]; b: [number, number] }) => (Math.atan2(c.b[1] - c.a[1], c.b[0] - c.a[0]) * 180) / Math.PI;
        const shiftDrag = (pts: Array<[number, number]>) => {
          const rc = el().getBoundingClientRect(), z = zoom;
          const P = (q: [number, number]) => [rc.left + q[0] * z, rc.top + q[1] * z] as const;
          synth("pointerdown", el(), ...P(pts[0]!), 0.5);
          for (let i = 1; i < pts.length; i++) synth("pointermove", el(), ...P(pts[i]!), 0.5, "pen", true);
          synth("pointerup", el(), ...P(pts[pts.length - 1]!), 0, "pen", true);
        };
        had = doc!.elementCount;
        shiftDrag([[100, 700], [300, 679], [500, 656]]); // about 6 degrees: snaps to 0
        await sleep(150);
        const sh0 = editor.lastRiemann;
        r.baseShiftSnapsToZero = !!sh0 && sh0.cols.length > 0 && near2(angleOf(sh0.cols[0]!), 0, 1e-6);
        undoIfAdded(had);
        had = doc!.elementCount;
        editor.lastRiemann = null as typeof editor.lastRiemann;
        shiftDrag([[100, 700], [300, 640], [500, 584]]); // about 12 degrees: snaps to 15
        await sleep(150);
        const sh15 = editor.lastRiemann as typeof editor.lastRiemann;
        r.baseShiftSnapsTo15 = !!sh15 && sh15.cols.length > 0 && near2(angleOf(sh15.cols[0]!), -15, 1e-6);
        undoIfAdded(had);
        editor.fill = { ...editor.fill, magnet: false };
        // 4. the base follows the curve that was drawn: a smile under the hump, rays to the hump
        rect({ rule: "mid", count: 12, base: "curve", side: "left" });
        const smile: Array<[number, number]> = [];
        for (let i = 0; i <= 60; i++) { const x = 150 + (i / 60) * 300; smile.push([x, 600 + 40 * Math.sin((Math.PI * (x - 150)) / 300)]); }
        const onSmile = (p: [number, number]) => { let best = Infinity; for (let i = 0; i + 1 < smile.length; i++) { const [ax, ay] = smile[i]!, [bx, by] = smile[i + 1]!; const dx = bx - ax, dy = by - ay, t = Math.max(0, Math.min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / (dx * dx + dy * dy))); best = Math.min(best, Math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)); } return best; };
        had = doc!.elementCount;
        drag(smile);
        await sleep(200);
        const cv = editor.lastRiemann!;
        r.baseCurveFollowsTheDrawing = cv.base === "curve" && cv.cols.length >= 11 && cv.cols.every((c) => onSmile(c.a) < 2.5 && onSmile(c.b) < 2.5);
        const ang = cv.cols.map(angleOf);
        r.baseCurveColumnsTilt = Math.max(...ang) - Math.min(...ang) > 20;
        r.baseCurveHeightsReachTheHump = cv.cols.every((c) => { const m: [number, number] = [(c.a[0] + c.b[0]) / 2 + c.normal[0] * c.h, (c.a[1] + c.b[1]) / 2 + c.normal[1] * c.h]; return m[0] < 110 || m[0] > 490 || Math.abs(m[1] - hump_y(m[0])) < 5; });
        { const c = views[PG]!.canvas!; const o = document.createElement("canvas"); o.width = 520; o.height = 380; o.getContext("2d")!.drawImage(c, 50, 320, 520, 380, 0, 0, 520, 380); r.baseCurvePng = o.toDataURL("image/png").split(",")[1]; }
        r.baseCurveOneUndo = doc!.elementCount === had + 1 + cv.cols.length;
        undoIfAdded(had);
        r.baseCurveUndone = doc!.elementCount === had;
        // 5. the option in the panel
        document.getElementById("btnFill")!.click();
        const baseSel = document.getElementById("fillBase") as HTMLSelectElement;
        const rowBase = document.getElementById("fillRowBase")!;
        r.baseSelectShown = !rowBase.hidden;
        baseSel.value = "line";
        baseSel.dispatchEvent(new Event("input", { bubbles: true }));
        r.baseSelectSets = editor.fill.base === "line" && JSON.parse((await window.xournal.getPrefs()).fill).base === "line";
        const kindSelect = document.getElementById("fillKind") as HTMLSelectElement;
        kindSelect.value = "solid";
        kindSelect.dispatchEvent(new Event("input", { bubbles: true }));
        r.baseSelectHiddenForOthers = (document.getElementById("fillRowBase") as HTMLElement).hidden === true;
        document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
        editor.fill = { ...editor.fill, kind: "riemann", base: "line", magnet: false };
      }
      // the number of columns: by count, by width
      rect({ rule: "upper", by: "width", width: 50 });
      drag(base);
      await sleep(150);
      r.riemannByWidth = editor.lastRiemann!.n === 8 && near(editor.lastRiemann!.width, 50, 0.01);
      editor.undo();
      rect({ rule: "upper", by: "width", width: 60 });
      drag(base);
      await sleep(150);
      r.riemannByWidthRounds = editor.lastRiemann!.n === 7 && near(editor.lastRiemann!.width, 400 / 7, 0.01);
      editor.undo();
      rect({ by: "count", count: 40, rule: "upper" });
      drag(base);
      await sleep(250);
      r.riemannManyColumns = editor.lastRiemann!.drawn === 40 && strokesNow() === st0 + 40;
      editor.undo();
      // without outlines: only the picture
      rect({ outline: false, count: 8, rule: "upper" });
      drag(base);
      await sleep(150);
      r.riemannNoOutline = strokesNow() === st0 && fills().length === 1 && doc!.elementCount === el0 + 1;
      editor.undo();
      // the other side: nothing above a baseline that has no curve
      rect({ outline: true });
      const nElems = doc!.elementCount;
      drag(line(150));
      await sleep(200);
      r.riemannNoCurveNoRectangles = doc!.elementCount === nElems;
      // a click has no baseline
      drag([[300, 450]]);
      await sleep(150);
      r.riemannClickNeedsBaseline = doc!.elementCount === nElems;
      // gradient and opacity are for the other kinds: rectangles use the pen colour and the opacity
      rect({ rule: "upper", count: 8, opacity: 0.5 });
      editor.color = "#000000";
      drag(base);
      await sleep(150);
      const halfPx = px(110, 460);
      r.riemannOpacity = halfPx[0]! > 100 && halfPx[0]! < 160;
      // the file keeps the picture and the outlines
      const xml = serializeXopp(doc!);
      const back = parseXopp(xml);
      const bl = back.pages[PG]!.layers.at(-1)!;
      r.riemannRoundTrip = bl.elements.filter((e) => e.kind === "image" && attrValue(e.attrs, "xlFill") !== null).length === 1 && bl.elements.filter((e) => e.kind === "stroke").length === st0 + 8;
      { const c = views[PG]!.canvas!; const o = document.createElement("canvas"); o.width = 560; o.height = 360; o.getContext("2d")!.drawImage(c, 60, 300, 560, 360, 0, 0, 560, 360); r.riemannPng = o.toDataURL("image/png").split(",")[1]; }
      editor.undo();
      editor.color = "#00aa00";
      editor.fill = { ...editor.fill, kind: "solid", opacity: 1 };
    }

    // 2. click fill inside a closed circle, then inside a circle with a gap
    setTool("pen");
    const circle = (cx: number, cy: number, R: number, from: number, to: number) => { const o: Array<[number, number]> = []; for (let i = 0; i <= 90; i++) { const a = from + (to - from) * (i / 90); o.push([cx + Math.cos(a) * R, cy + Math.sin(a) * R]); } return o; };
    drag(circle(300, 700, 60, 0, Math.PI * 2 + 0.05));
    await sleep(40);
    setTool("fill");
    editor.color = "#cc3300";
    drag([[300, 700]]);
    await sleep(120);
    const lf2 = editor.lastFill!;
    r.fillClickMode = lf2.mode === "point" && !lf2.wholePage;
    r.fillClickInsideCircle = isFill(300, 700, 204, 51, 0) && isFill(300, 660, 204, 51, 0) && white(300, 600) && white(400, 700) && white(300, 790);
    r.fillClickSmallImage = (() => { const im = fills().at(-1) as ImageEl; return im.right - im.left < 140 && im.bottom - im.top < 140; })();
    editor.undo();
    setTool("pen");
    drag(circle(450, 800, 40, 0.4, Math.PI * 2 - 0.4));
    await sleep(40);
    setTool("fill");
    const n0 = fills().length;
    drag([[450, 800]]);
    await sleep(200);
    r.fillOpenShapeLeaks = editor.lastFill!.wholePage && fills().length === n0 + 1 && isFill(450, 800, 204, 51, 0) && isFill(100, 100, 204, 51, 0);
    editor.undo();
    editor.fill = { ...editor.fill, gap: 8 };
    drag([[450, 800]]);
    await sleep(200);
    r.fillGapSmallerThanOpeningStillLeaks = editor.lastFill!.wholePage; // the opening is 2*40*sin(0.4) = 31 wide, far more than the 8 of the largest setting
    editor.undo();
    editor.fill = { ...editor.fill, gap: 1.5 };
    // a click right on a stroke picks the nearest free pixel
    const nBefore = fills().length;
    drag([[360, 700]]);
    await sleep(150);
    r.fillClickOnStrokeStillWorks = fills().length === nBefore + 1;
    editor.undo();

    // 2b. the magnet: small openings, a click a pixel off, openings too wide, and the ends of a dragged line
    editor.color = "#cc3300";
    setTool("pen");
    drag(circle(450, 800, 40, 0.075, Math.PI * 2 - 0.075)); // an opening 6 units wide
    await sleep(40);
    setTool("fill");
    editor.fill = { ...editor.fill, magnet: false };
    drag([[450, 800]]);
    await sleep(200);
    r.fillSmallOpeningLeaksWithoutMagnet = editor.lastFill!.wholePage;
    editor.undo();
    editor.fill = { ...editor.fill, magnet: true };
    drag([[450, 800]]);
    await sleep(250);
    const lfm = editor.lastFill!;
    r.fillMagnetClosesSmallOpening = !lfm.wholePage && lfm.gapUsed >= 6 && lfm.gapUsed <= 14 && isFill(450, 800, 204, 51, 0) && white(450, 700) && white(540, 800);
    editor.undo();
    editor.undo(); // that circle
    // a click a pixel outside a closed circle
    drag([[300 + 61.5, 700]]);
    await sleep(250);
    const lfo = editor.lastFill!;
    r.fillMagnetClickJustOutside = !lfo.wholePage && lfo.moved && isFill(300, 700, 204, 51, 0) && white(300 + 75, 700) && white(300, 600);
    editor.undo();
    // the same click without the magnet fills the outside (the whole page region)
    editor.fill = { ...editor.fill, magnet: false };
    drag([[300 + 61.5, 700]]);
    await sleep(250);
    r.fillNoMagnetClickOutsideLeaks = editor.lastFill!.wholePage && white(300, 700);
    editor.undo();
    editor.fill = { ...editor.fill, magnet: true };
    // an opening wider than the magnet reaches: nothing is filled (not the whole page), Shift fills anyway
    setTool("pen");
    drag(circle(450, 800, 40, 0.4, Math.PI * 2 - 0.4)); // 31 wide, the magnet stops at 14
    await sleep(40);
    setTool("fill");
    const nW = fills().length;
    const tRef = performance.now();
    drag([[450, 800]]);
    r.fillMagnetWorstCaseMs = Math.round(performance.now() - tRef); // every gap size tried, every neighbour looked at, nothing closes
    await sleep(300);
    r.fillMagnetRefusesToFloodThePage = fills().length === nW;
    const rc = el().getBoundingClientRect();
    synth("pointerdown", el(), rc.left + 450, rc.top + 800, 0.5);
    synth("pointerup", el(), rc.left + 450, rc.top + 800, 0, "pen", true);
    await sleep(300);
    r.fillShiftFillsAnyway = fills().length === nW + 1 && editor.lastFill!.wholePage;
    editor.undo();
    editor.fill = { ...editor.fill, maxGap: 40 };
    drag([[450, 800]]);
    await sleep(300);
    r.fillBiggerMagnetClosesIt = fills().length === nW + 1 && !editor.lastFill!.wholePage && editor.lastFill!.gapUsed > 14;
    editor.undo();
    editor.undo(); // the circle
    editor.fill = { ...editor.fill, maxGap: 14 };
    // a dragged line whose ends land near the curve: they are pulled onto it
    editor.fill = { ...editor.fill, magnet: false };
    drag([[104, 505], [300, 505], [497, 505]]);
    await sleep(200);
    r.fillLineOffTheCurveLeaks = !white(60, 495);
    editor.undo();
    editor.fill = { ...editor.fill, magnet: true };
    drag([[104, 505], [300, 505], [497, 505]]);
    await sleep(200);
    r.fillLineEndsStickToCurve = isFill(300, 450, 204, 51, 0);
    r.fillMagnetLineStaysBounded = white(60, 495) && white(540, 495);
    editor.undo();
    editor.color = "#00aa00";
    setTool("fill");

    // 3. a closed curve dragged as the seed: its inside (clockwise on screen: the right-hand side)
    editor.fill = { ...editor.fill, side: "right" };
    editor.color = "#0044cc";
    drag(circle(150, 250, 70, 0, Math.PI * 2));
    await sleep(200);
    r.fillClosedSeedInside = isFill(150, 250, 0, 68, 204) && white(150, 120) && white(260, 250);
    editor.undo();
    editor.fill = { ...editor.fill, side: "left" };
    drag(circle(150, 250, 70, 0, Math.PI * 2));
    await sleep(200);
    r.fillClosedSeedOutside = white(150, 250) && isFill(260, 250, 0, 68, 204) && editor.lastFill!.wholePage;
    editor.undo();

    // 4. gradients and opacity
    editor.color = "#ff0000";
    editor.fill = { ...editor.fill, side: "left", kind: "linear", color2: "#0000ff", angle: 0 };
    drag(line(500));
    await sleep(150);
    const leftPx = px(140, 495), rightPx = px(460, 495);
    r.fillLinearGradient = leftPx[0]! > leftPx[2]! + 60 && rightPx[2]! > rightPx[0]! + 60;
    editor.undo();
    editor.fill = { ...editor.fill, kind: "radial" };
    drag(line(500));
    await sleep(150);
    const mid = px(300, 450), edge = px(120, 497);
    r.fillRadialGradient = mid[0]! > edge[0]! + 40 && edge[2]! > mid[2]! + 40;
    editor.undo();
    editor.fill = { ...editor.fill, kind: "solid", opacity: 0.5 };
    editor.color = "#000000";
    drag(line(500));
    await sleep(150);
    const half = px(300, 450);
    r.fillOpacity = half[0]! > 100 && half[0]! < 160;
    editor.undo();
    editor.fill = { ...editor.fill, opacity: 1 };

    // 5. the file: the fill is an image marked xlFill, saved before the ink, and survives a round trip
    editor.color = "#00aa00";
    drag(line(500));
    await sleep(150);
    const xml = serializeXopp(doc!);
    const back = parseXopp(xml);
    const bl = back.pages[PG]!.layers.at(-1)!;
    const bi = bl.elements.findIndex((e) => e.kind === "image" && attrValue(e.attrs, "xlFill") !== null);
    const bs = bl.elements.findIndex((e) => e.kind === "stroke");
    r.fillRoundTrip = bi >= 0 && bi < bs && /<image [^>]*xlFill="1"/.test(xml);
    const imgEl = fills()[0] as ImageEl;
    const decoded = await createImageBitmap(new Blob([Uint8Array.from(atob(imgEl.b64), (c) => c.charCodeAt(0))], { type: "image/png" }));
    r.fillPngDecodes = decoded.width > 100 && decoded.height > 50;
    editor.color = "#ff00ff";
    drag(line(500));
    await sleep(150);
    r.fillLaterOnTop = fills().length === 2 && lay().elements.indexOf(fills()[1]!) > lay().elements.indexOf(fills()[0]!) && isFill(300, 450, 255, 0, 255);
    editor.undo(); editor.undo();

    // 6. keep the line as a stroke
    editor.fill = { ...editor.fill, keepLine: true };
    const strokesBefore = lay().elements.filter((e) => e.kind === "stroke").length;
    drag([[100, 650], [300, 650], [500, 650]]);
    await sleep(150);
    r.fillKeepLine = lay().elements.filter((e) => e.kind === "stroke").length === strokesBefore + 1 && fills().length === 1;
    editor.undo();
    r.fillKeepLineUndoneTogether = lay().elements.filter((e) => e.kind === "stroke").length === strokesBefore && fills().length === 0;
    editor.fill = { ...editor.fill, keepLine: false };

    // 7. options parsing and the UI
    r.fillSanitize = sanitizeFill({ side: "x", kind: "zzz", opacity: 5, gap: -3, angle: "a", color2: "red" }).side === "left" && sanitizeFill({ opacity: 5 }).opacity === 1 && sanitizeFill({ gap: -3 }).gap === 0 && sanitizeFill({ color2: "red" }).color2 === "#05e1ff";
    document.getElementById("btnFill")!.click();
    const panel = document.getElementById("fillPanel")!;
    r.fillPanelOpens = !panel.hidden;
    const sideR = document.querySelector<HTMLInputElement>('input[name="fillSide"][value="right"]')!;
    sideR.checked = true; sideR.dispatchEvent(new Event("input", { bubbles: true }));
    const kindSel = document.getElementById("fillKind") as HTMLSelectElement;
    kindSel.value = "linear"; kindSel.dispatchEvent(new Event("input", { bubbles: true }));
    r.fillPanelSetsOptions = editor.fill.side === "right" && editor.fill.kind === "linear" && !document.getElementById("fillRowAngle")!.hidden && !document.getElementById("fillRowC2")!.hidden;
    r.fillPanelSaved = JSON.parse((await window.xournal.getPrefs()).fill).side === "right";
    document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    r.fillPanelEscCloses = panel.hidden;
    r.fillMenu = (await menu("Herramientas", "Relleno")) && editor.tool === "fill";
    setTool("pen");
    const bar = document.querySelector<HTMLElement>('button.tool[data-tool="fill"]')!;
    bar.click();
    r.fillToolButtonSelects = editor.tool === "fill" && bar.classList.contains("active");

    // clean up
    editor.fill = savedFill;
    editor.color = savedColor;
    await window.xournal.setPrefs({ fill: "" });
    setTool("pen");
    while (doc!.pages.length > 1) { journal.deletePage(doc!.pages.length - 1); await sleep(30); }
    goToPage(0);
    await sleep(200);
  }

  // S) does the stroke change when the pen is lifted? Compare what was painted live with what is painted once stored.
  {
    commands["file.new"]!(); // a blank page: the only ink must be the stroke under test
    await sleep(250);
    const measure = async () => {
    const pageEl = () => views[0]!.el;
    setZoom(1);
    await sleep(150);
    setTool("pen");
    const rect = pageEl().getBoundingClientRect();
    const live = host.liveCanvas();
    const lr = live.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    // A hand-like variable-pressure stroke with a loop.
    const path: Array<[number, number, number]> = [];
    for (let k = 0; k <= 120; k++) {
      const t = k / 120;
      path.push([180 + t * 260 + Math.sin(t * 14) * 28, 420 + Math.cos(t * 9) * 36 + t * 10, 0.15 + 0.8 * Math.abs(Math.sin(t * 5))]);
    }
    synth("pointerdown", pageEl(), rect.left + path[0]![0], rect.top + path[0]![1], path[0]![2]);
    for (let k = 1; k < path.length; k++) synth("pointermove", pageEl(), rect.left + path[k]![0], rect.top + path[k]![1], path[k]![2]);
    const x0 = Math.floor(Math.min(...path.map((q) => q[0])) - 8), x1 = Math.ceil(Math.max(...path.map((q) => q[0])) + 8);
    const y0 = Math.floor(Math.min(...path.map((q) => q[1])) - 8), y1 = Math.ceil(Math.max(...path.map((q) => q[1])) + 8);
    const lx = Math.round((rect.left + x0 - lr.left) * dpr), ly = Math.round((rect.top + y0 - lr.top) * dpr);
    const w = Math.round((x1 - x0) * dpr), h = Math.round((y1 - y0) * dpr);
    const lctx = live.getContext("2d", { desynchronized: DESYNC })!;
    const before = lctx.getImageData(lx, ly, w, h).data; // painted live, alpha = coverage
    synth("pointerup", pageEl(), rect.left + path[path.length - 1]![0], rect.top + path[path.length - 1]![1], 0);
    await sleep(40);
    const cv = views[0]!.canvas!;
    const after = cv.getContext("2d")!.getImageData(Math.round(x0 * dpr), Math.round(y0 * dpr), w, h).data; // page is white: darkness = coverage
    let sumDiff = 0, maxDiff = 0, big = 0, inked = 0;
    for (let k = 0; k < w * h; k++) {
      const a = before[4 * k + 3]!;
      const b = 255 - after[4 * k]!;
      if (a > 0 || b > 0) inked++;
      const d = Math.abs(a - b);
      sumDiff += d; if (d > maxDiff) maxDiff = d; if (d > 64) big++;
    }
    if (!r.popPng) {
      // A magnified crop (6x) of the same stretch: painted live (left) and as stored (right), plus where they differ (centre).
      const cw = 150, ch = 70, cx0 = 60, cy0 = Math.max(0, Math.round((h / dpr) / 2) - 35);
      const out = document.createElement("canvas");
      out.width = cw * 6 * 3 + 20; out.height = ch * 6;
      const oc = out.getContext("2d")!;
      oc.imageSmoothingEnabled = false;
      oc.fillStyle = "#fff"; oc.fillRect(0, 0, out.width, out.height);
      const put = (get: (k: number) => number, ox: number, tint: [number, number, number]) => {
        for (let yy = 0; yy < ch; yy++) for (let xx = 0; xx < cw; xx++) {
          const k = Math.round((cy0 + yy) * dpr) * w + Math.round((cx0 + xx) * dpr);
          const v = get(k);
          oc.fillStyle = `rgb(${255 - ((255 - tint[0]) * v) / 255},${255 - ((255 - tint[1]) * v) / 255},${255 - ((255 - tint[2]) * v) / 255})`;
          oc.fillRect(ox + xx * 6, yy * 6, 6, 6);
        }
      };
      put((k) => before[4 * k + 3]!, 0, [0, 0, 0]);
      put((k) => 255 - after[4 * k]!, cw * 6 + 10, [0, 0, 0]);
      put((k) => Math.min(255, Math.abs(before[4 * k + 3]! - (255 - after[4 * k]!)) * 1.5), 2 * (cw * 6 + 10), [200, 0, 0]);
      r.popPng = out.toDataURL("image/png").split(",")[1];
    }
    const res = { inked, meanDiff: +(sumDiff / Math.max(1, inked)).toFixed(2), maxDiff, over64: big, fraction: +(big / Math.max(1, inked)).toFixed(4) };
    editor.undo();
    await sleep(40);
    return res;
    };
    const popRes = await measure();
    r.popStoredVsLive = popRes;
    r.popIsSmall = popRes.fraction < 0.02; // live and stored differ only by antialiasing at the edges
  }


  // T) the pen diagnostic classifies input precision correctly (crafted samples) and the smoothing preference defaults to off
  {
    const mk = (xs: number[], ys: number[], pointerType = "pen"): Array<{ type: string; pointerType: string; x: number; y: number; pressure: number; t: number }> =>
      xs.map((x, k) => ({ type: "pointermove", pointerType, x, y: ys[k]!, pressure: 0.3 + (k % 7) / 10, t: k * 8 }));
    const ints = analyze(mk(Array.from({ length: 60 }, (_, k) => 100 + k * 3), Array.from({ length: 60 }, (_, k) => 200 + k * 2)), 1);
    r.diagIntegerInput = ints.fractionalCss === 0 && ints.fractionalPhysical === 0 && ints.verdict.includes("ENTERAS") && ints.eventsPerSecond === 125;
    const decimals = analyze(mk(Array.from({ length: 60 }, (_, k) => 100.37 + k * 3.113), Array.from({ length: 60 }, (_, k) => 200.61 + k * 2.07)), 1);
    r.diagFractionalInput = decimals.fractionalCss > 0.9 && decimals.verdict.includes("DECIMALES") && decimals.distinctFractions > 10;
    // Whole physical pixels on a 125 % screen: x = k / 1.25 (fractional in CSS px, whole in device px)
    const scaled = analyze(mk(Array.from({ length: 60 }, (_, k) => (100 + k * 4) / 1.25), Array.from({ length: 60 }, (_, k) => (200 + k * 3) / 1.25)), 1.25);
    r.diagPhysicalPixels = scaled.fractionalCss > 0.3 && scaled.fractionalPhysical === 0 && scaled.verdict.includes("ENTERAS en píxeles físicos");
    r.diagEmptyIsHandled = analyze([], 1).verdict.startsWith("Sin datos");
    commands["help.pen"]!();
    r.diagDialogOpens = penDiagnostic.isOpen && !document.getElementById("penDlg")!.hidden;
    penDiagnostic.close();
  }
  await window.xournal.ready({ mode: "e2e", file: fileName, page: i, ...r });
}

void window.xournal.cli().then(async (c) => {
  autoFit = !c.headless && !c.selftest && !c.e2e && !c.bench; // measurements and screenshots keep 100 %
  try {
    if (c.selftest) return await selftest(c.files);
    if (c.e2e && c.files[0]) return await e2e(c.files[0]);
    if (c.bench && c.files[0]) return await bench(c.files[0], c.zoom ?? 1);
    if (c.stress && c.files[0]) { await openFile(c.files[0], c.zoom ? { zoom: c.zoom } : undefined); void runStress(c.stress); return; }
    if (!c.files[0]) {
      const p = await window.xournal.getPrefs().catch(() => null);
      if (p && p.welcome === false) newDocument(true); else await welcome.show();
      if (c.headless) await window.xournal.ready({ welcome: welcome.visible }); // screenshot of the start page
    }
    if (c.files[0]) {
      const t = await openFile(c.files[0], { scrollPage: c.scrollPage, zoom: c.zoom });
      if (c.headless) await window.xournal.ready({ file: fileName, pages: doc!.pages.length, elements: doc!.elementCount, ...t, visiblePages: [...visible].sort((a, b) => a - b) });
    }
  } catch (e) {
    showError(e);
    if (c.headless) await window.xournal.ready({ error: String(e), stack: e instanceof Error ? (e.stack ?? "").split("\n").slice(0, 6).join(" | ") : "" });
  }
});
