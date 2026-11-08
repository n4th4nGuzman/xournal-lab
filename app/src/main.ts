import { PAPER_KINDS } from "./renderer/paper";
import { app, BrowserWindow, dialog, ipcMain, Menu, shell, type IpcMainEvent, type IpcMainInvokeEvent, type MenuItem, type MenuItemConstructorOptions } from "electron";
import { promises as fs } from "node:fs";
import { gunzip, gzip } from "node:zlib";
import { promisify } from "node:util";

const gunzipAsync = promisify(gunzip), gzipAsync = promisify(gzip); // in the thread pool: a big file no longer freezes the window and the menus
import path from "node:path";

interface CliOptions {
  files: string[];
  screenshot?: string;
  scrollPage?: number;
  zoom?: number;
  selftest: boolean;
  bench: boolean;
  e2e: boolean;
  sync: boolean;
  out?: string;
  theme?: "classic" | "neon";
  size?: [number, number];   // --size 1920x1080: window size of a --screenshot run (UI parity captures)
  click?: string;
  js?: string;
  stress?: string;
  profile?: number;
}

function parseArgs(argv: string[]): CliOptions {
  const o: CliOptions = { files: [], selftest: false, bench: false, e2e: false, sync: false };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i]!;
    if (a === "--screenshot") o.screenshot = argv[++i];
    else if (a === "--page") o.scrollPage = Number(argv[++i]);
    else if (a === "--zoom") o.zoom = Number(argv[++i]);
    else if (a === "--out") o.out = path.resolve(argv[++i]!);
    else if (a === "--selftest") o.selftest = true;
    else if (a === "--bench") o.bench = true;
    else if (a === "--size") { const m = /^(\d+)x(\d+)$/.exec(argv[++i] ?? ""); if (m) o.size = [Number(m[1]), Number(m[2])]; }
    else if (a === "--theme") o.theme = argv[++i] === "neon" ? "neon" : "classic";
    else if (a === "--click") o.click = argv[++i];
    else if (a === "--js") o.js = argv[++i];
    else if (a === "--stress") o.stress = argv[++i];
    else if (a === "--profile") o.profile = Number(argv[++i]);
    else if (a === "--sync") o.sync = true;
    else if (a === "--e2e") o.e2e = true;
    else if (a.toLowerCase().endsWith(".xopp")) o.files.push(path.resolve(a));
  }
  return o;
}

const cli = parseArgs(process.argv.slice(1));
const autoQuit = !!cli.screenshot || cli.selftest || cli.bench || cli.e2e;
const MANUAL_URL = "https://xournalpp.github.io/guide/overview/";

// ---- windows ----------------------------------------------------------------
interface WinState { dirty: boolean; first: boolean }
const states = new Map<number, WinState>(); // by webContents id
let mainWin: BrowserWindow | undefined;

function winOf(e: IpcMainEvent | IpcMainInvokeEvent): BrowserWindow {
  return BrowserWindow.fromWebContents(e.sender)!;
}

// The window has no native title or menu bar: the page draws its own, so both follow the theme. The operating system
// still draws the minimise / maximise / close buttons (an overlay), whose colours are set from the page.
const BAR_HEIGHT = 32;
const BAR_COLORS = { classic: { color: "#2b2b2b", symbolColor: "#dddddd" }, neon: { color: "#1e1043", symbolColor: "#05e1ff" } } as const;

function createWindow(first: boolean): BrowserWindow {
  const win = new BrowserWindow({
    width: cli.size?.[0] ?? 1180,
    height: cli.size?.[1] ?? 900,
    // Normal runs open maximized (shown once ready, so there is no flash of a small window); automated runs keep a fixed size.
    show: autoQuit && (cli.bench || cli.e2e || !!cli.click || !!cli.js), // frame timing needs a visible window
    title: "Xournal Lab",
    backgroundColor: "#3b3b3b",
    titleBarStyle: "hidden",
    titleBarOverlay: { ...BAR_COLORS[prefs.theme === "neon" ? "neon" : "classic"], height: BAR_HEIGHT },
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      backgroundThrottling: false,
    },
  });
  win.removeMenu(); // the menu is drawn by the page
  // A dialog sent to a window of its own ("Desacoplar") is an empty window the page fills; nothing else may open windows.
  win.webContents.setWindowOpenHandler(({ url }) => (url === "" || url === "about:blank"
    ? { action: "allow", overrideBrowserWindowOptions: { parent: win, frame: true, titleBarStyle: "default", titleBarOverlay: false, autoHideMenuBar: true, backgroundColor: "#2b2b2b", show: true } }
    : { action: "deny" }));
  win.webContents.on("did-create-window", (child) => { child.removeMenu(); });
  if (!autoQuit) { win.maximize(); win.once("ready-to-show", () => win.show()); }
  const id = win.webContents.id;
  states.set(id, { dirty: false, first });
  win.on("close", (e) => {
    if (!states.get(id)?.dirty || autoQuit) return;
    const choice = dialog.showMessageBoxSync(win, {
      type: "warning", buttons: ["Cancelar", "Cerrar sin guardar"], defaultId: 0, cancelId: 0,
      message: "Hay cambios sin guardar", detail: "Si cierras ahora se perderán.",
    });
    if (choice === 0) e.preventDefault();
  });
  win.on("closed", () => {
    states.delete(id);
    if (BrowserWindow.getAllWindows().every((w) => w === latexWin)) dropLatexWindow();
  });
  if (cli.profile) { // a CPU profile of the page while it works (with --stress), printed as the functions that took the most time
    win.webContents.once("did-finish-load", () => {
      setTimeout(async () => {
        const dbg = win.webContents.debugger;
        try {
          dbg.attach("1.3");
          await dbg.sendCommand("Profiler.enable");
          await dbg.sendCommand("Profiler.start");
          await new Promise((r) => setTimeout(r, cli.profile! * 1000));
          const { profile } = (await dbg.sendCommand("Profiler.stop")) as { profile: { nodes: Array<{ id: number; callFrame: { functionName: string; url: string; lineNumber: number } }>; samples: number[]; timeDeltas: number[] } };
          const byId = new Map(profile.nodes.map((n) => [n.id, n]));
          const self = new Map<string, number>();
          let total = 0;
          profile.samples.forEach((id, i) => {
            const n = byId.get(id)!;
            const key = `${n.callFrame.functionName || "(anonymous)"} ${n.callFrame.url.split("/").pop() ?? ""}:${n.callFrame.lineNumber}`;
            const dt = (profile.timeDeltas[i] ?? 0) / 1000;
            self.set(key, (self.get(key) ?? 0) + dt);
            total += dt;
          });
          const top = [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 28).map(([k, v]) => `${((100 * v) / total).toFixed(1).padStart(5)}%  ${v.toFixed(0).padStart(6)} ms  ${k}`);
          console.log("PROFILE " + cli.stress + " total " + total.toFixed(0) + " ms" + String.fromCharCode(10) + top.join(String.fromCharCode(10)));
        } catch (e) { console.log("PROFILE failed " + String(e)); }
        app.quit();
      }, 7000);
    });
  }
  void win.loadFile(path.join(__dirname, "index.html"), cli.sync ? { search: "sync" } : undefined);
  return win;
}

// ---- files ------------------------------------------------------------------
async function readXopp(file: string): Promise<{ xml: string; bytes: number; name: string; dir: string }> {
  if (!file.toLowerCase().endsWith(".xopp")) throw new Error("Solo se pueden abrir archivos .xopp");
  const raw = await fs.readFile(file);
  const gz = raw.length > 2 && raw[0] === 0x1f && raw[1] === 0x8b;
  const xml = (gz ? await gunzipAsync(raw) : raw).toString("utf8");
  return { xml, bytes: raw.length, name: path.basename(file), dir: path.dirname(file) };
}

async function writeAtomic(file: string, xml: string): Promise<number> {
  const data = await gzipAsync(Buffer.from(xml, "utf8"), { level: 6 });
  const tmp = file + ".tmp";
  await fs.writeFile(tmp, data);
  await fs.rename(tmp, file);
  return data.length;
}

// Recent files live next to the app's other per-user data.
const recentFile = () => path.join(app.getPath("userData"), "recent.json");
let recent: string[] = [];
async function loadRecent(): Promise<void> {
  if (autoQuit) { recent = []; return; } // automated runs neither read nor write the user's list
  try {
    const v: unknown = JSON.parse(await fs.readFile(recentFile(), "utf8"));
    recent = Array.isArray(v) ? v.filter((p): p is string => typeof p === "string").slice(0, 10) : [];
  } catch { recent = []; }
}
async function saveRecent(): Promise<void> {
  if (autoQuit) return;
  try { await fs.mkdir(path.dirname(recentFile()), { recursive: true }); await fs.writeFile(recentFile(), JSON.stringify(recent)); } catch { /* best effort */ }
}
async function addRecent(file: string): Promise<void> {
  recent = [file, ...recent.filter((p) => p !== file)].slice(0, 10);
  await saveRecent();
  buildMenu();
}

ipcMain.handle("recent:list", async () => {
  const out: Array<{ path: string; name: string; dir: string }> = [];
  for (const p of recent) { try { await fs.access(p); out.push({ path: p, name: path.basename(p), dir: path.dirname(p) }); } catch { /* moved or deleted: not offered */ } }
  return out;
});
ipcMain.handle("recent:clear", async () => { recent = []; await saveRecent(); buildMenu(); });

ipcMain.handle("dialog:open", async (e, dir?: string) => {
  const r = await dialog.showOpenDialog(winOf(e), {
    defaultPath: typeof dir === "string" ? dir : undefined,
    properties: ["openFile"],
    filters: [{ name: "Xournal++", extensions: ["xopp"] }],
  });
  return r.canceled ? null : r.filePaths[0] ?? null;
});
ipcMain.handle("file:read", (_e, file: string) => readXopp(file));

type SaveMode = "save" | "saveAs" | "copy";
// `target` is the path the renderer last saved this document to (null for a new or
// freshly opened document): a plain Save with no target always asks, so an opened
// original is never overwritten silently. Headless runs write to --out.
ipcMain.handle("file:save", async (e, xml: string, suggested: string, dir: string | null, mode: SaveMode, target: string | null) => {
  let file: string | null = cli.out ?? (mode === "save" ? target : null);
  if (!file) {
    const r = await dialog.showSaveDialog(winOf(e), {
      title: mode === "copy" ? "Guardar copia" : "Guardar como",
      defaultPath: dir ? path.join(dir, suggested) : suggested,
      filters: [{ name: "Xournal++", extensions: ["xopp"] }],
    });
    if (r.canceled || !r.filePath) return null;
    file = r.filePath;
  }
  const bytes = await writeAtomic(file, xml);
  if (mode !== "copy" && !cli.out) void addRecent(file);
  return { path: file, bytes };
});

// ---- export to PDF: the renderer sends each page as a JPEG; the file is written here, one image per page at the page's size ----
interface PdfPage { w: number; h: number; pw: number; ph: number; jpg: Buffer }
interface PdfJob { target: string; pages: PdfPage[] }
const pdfJobs = new Map<number, PdfJob>();

/** A PDF with one full-page JPEG per page. `w`/`h` are the page size in points, `pw`/`ph` the image's pixels. */
export function buildPdf(pages: PdfPage[]): Buffer {
  const parts: Buffer[] = [];
  const offsets: number[] = [];
  let len = 0;
  const push = (b: Buffer | string) => { const buf = typeof b === "string" ? Buffer.from(b, "latin1") : b; parts.push(buf); len += buf.length; };
  const obj = (n: number, body: Buffer | string, stream?: Buffer) => {
    offsets[n] = len;
    push(`${n} 0 obj\n`);
    push(body);
    if (stream) { push("\nstream\n"); push(stream); push("\nendstream"); }
    push("\nendobj\n");
  };
  push("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  const kids = pages.map((_, i) => `${3 + i * 3} 0 R`).join(" ");
  obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
  obj(2, `<< /Type /Pages /Kids [${kids}] /Count ${pages.length} >>`);
  pages.forEach((p, i) => {
    const page = 3 + i * 3, content = page + 1, image = page + 2;
    const f = (v: number) => (+v.toFixed(3)).toString();
    obj(page, `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${f(p.w)} ${f(p.h)}] /Resources << /XObject << /Im0 ${image} 0 R >> >> /Contents ${content} 0 R >>`);
    const draw = `q ${f(p.w)} 0 0 ${f(p.h)} 0 0 cm /Im0 Do Q`;
    obj(content, `<< /Length ${draw.length} >>`, Buffer.from(draw, "latin1"));
    obj(image, `<< /Type /XObject /Subtype /Image /Width ${p.pw} /Height ${p.ph} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length ${p.jpg.length} >>`, p.jpg);
  });
  const xref = len, count = 3 + pages.length * 3;
  push(`xref\n0 ${count}\n0000000000 65535 f \n`);
  for (let n = 1; n < count; n++) push(`${String(offsets[n]).padStart(10, "0")} 00000 n \n`);
  push(`trailer\n<< /Size ${count} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`);
  return Buffer.concat(parts);
}

ipcMain.handle("pdf:begin", async (e, suggested: string, dir: string | null, testPath?: string) => {
  pdfJobs.delete(e.sender.id);
  let target: string | null = cli.e2e && typeof testPath === "string" ? testPath : null; // tests have no one to answer the dialog
  if (!target) {
    const r = await dialog.showSaveDialog(winOf(e), {
      title: "Exportar como PDF",
      defaultPath: dir ? path.join(dir, suggested) : suggested,
      filters: [{ name: "PDF", extensions: ["pdf"] }],
    });
    if (r.canceled || !r.filePath) return null;
    target = /\.pdf$/i.test(r.filePath) ? r.filePath : r.filePath + ".pdf";
  }
  pdfJobs.set(e.sender.id, { target, pages: [] });
  return target;
});
ipcMain.handle("pdf:page", (e, index: number, w: number, h: number, pw: number, ph: number, jpg: Uint8Array) => {
  const job = pdfJobs.get(e.sender.id);
  const ok = (v: number, max: number) => Number.isFinite(v) && v > 0 && v < max;
  if (!job || index !== job.pages.length || !ok(w, 20000) || !ok(h, 20000) || !ok(pw, 16384) || !ok(ph, 16384) || !(jpg instanceof Uint8Array) || jpg.length < 4 || jpg[0] !== 0xff || jpg[1] !== 0xd8) return false;
  job.pages.push({ w, h, pw: Math.round(pw), ph: Math.round(ph), jpg: Buffer.from(jpg) });
  return true;
});
ipcMain.handle("pdf:abort", (e) => { pdfJobs.delete(e.sender.id); });
ipcMain.handle("pdf:finish", async (e): Promise<{ ok: true; path: string; bytes: number } | { ok: false; error: string }> => {
  const job = pdfJobs.get(e.sender.id);
  pdfJobs.delete(e.sender.id);
  if (!job || !job.pages.length) return { ok: false, error: "No hay páginas que exportar" };
  try {
    const pdf = buildPdf(job.pages);
    await fs.writeFile(job.target, pdf);
    return { ok: true, path: job.target, bytes: pdf.length };
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : String(err) };
  }
});

ipcMain.on("app:addRecent", (_e, file: unknown) => {
  if (typeof file === "string" && file.toLowerCase().endsWith(".xopp")) void addRecent(file);
});
ipcMain.on("app:dirty", (e, d: boolean) => { const s = states.get(e.sender.id); if (s) s.dirty = d; });
ipcMain.on("app:setFullscreen", (e, on: boolean) => winOf(e).setFullScreen(!!on));
ipcMain.on("app:setMenuBar", (e, on: boolean) => { const w = winOf(e); w.setMenuBarVisibility(!!on); w.autoHideMenuBar = !on; });
ipcMain.handle("app:cli", (e) => {
  const first = states.get(e.sender.id)?.first ?? false;
  return {
    files: first ? cli.files : [], scrollPage: first ? cli.scrollPage ?? null : null, zoom: first ? cli.zoom ?? null : null,
    stress: first ? cli.stress ?? null : null, selftest: first && cli.selftest, bench: first && cli.bench, e2e: first && cli.e2e, headless: autoQuit,
  };
});

ipcMain.handle("app:ready", async (_e, stats: unknown) => {
  console.log("READY " + JSON.stringify(stats));
  if (cli.screenshot && mainWin) {
    await new Promise((r) => setTimeout(r, 400));
    if (cli.js) { const v: unknown = await mainWin.webContents.executeJavaScript(cli.js); if (v !== undefined) console.log("JS " + JSON.stringify(v)); await new Promise((r) => setTimeout(r, 700)); } // screenshots of dialogs that need a gesture
    if (cli.click) { await mainWin.webContents.executeJavaScript(`document.querySelector(${JSON.stringify(cli.click)})?.click();`); mainWin.webContents.invalidate(); await new Promise((r) => setTimeout(r, 700)); } // screenshots of menus and dialogs
    const img = await mainWin.webContents.capturePage();
    await fs.writeFile(cli.screenshot, img.toPNG());
    console.log("SCREENSHOT " + cli.screenshot);
  }
  if (autoQuit) app.quit();
});

// ---- LaTeX ------------------------------------------------------------------
interface LatexRequest { latex: string; display: boolean; color: string; px: number }
type LatexResult = { ok: true; pdfB64: string; widthPt: number; heightPt: number } | { ok: false; error: string };

// One hidden window stays loaded (KaTeX and its fonts ready) so each formula costs a render,
// not a window start-up. Measured: KaTeX takes 2-4 ms; printToPDF always takes ~0.5 s (every option
// tried, including preferCSSPageSize, costs the same), so that is the floor for an insertion. Requests run one at a time; the window goes away when idle.
const LATEX_IDLE_MS = 20_000; // the hidden window is a whole extra Chromium process: it should not outlive a burst of formulas by long
let latexWin: BrowserWindow | null = null;
let latexIdle: NodeJS.Timeout | undefined;
let latexQueue: Promise<unknown> = Promise.resolve();

function dropLatexWindow(): void {
  clearTimeout(latexIdle);
  if (latexWin && !latexWin.isDestroyed()) latexWin.destroy();
  latexWin = null;
}

async function warmLatexWindow(): Promise<BrowserWindow> {
  if (latexWin && !latexWin.isDestroyed()) return latexWin;
  const w = new BrowserWindow({ show: false, width: 1400, height: 800, webPreferences: { sandbox: true, contextIsolation: true, backgroundThrottling: false } });
  latexWin = w;
  await w.loadFile(path.join(__dirname, "latexpage.html"));
  return w;
}

async function renderLatexPdf(req: LatexRequest): Promise<LatexResult> {
  const latex = String(req.latex ?? "");
  const color = /^#[0-9a-fA-F]{6}$/.test(req.color) ? req.color : "#000000";
  const px = Math.min(200, Math.max(6, Number(req.px) || 16));
  if (!latex.trim()) return { ok: false, error: "La fórmula está vacía" };
  try {
    const w = await warmLatexWindow();
    const call = `window.renderLatex(${JSON.stringify(latex)}, ${req.display ? "true" : "false"}, ${JSON.stringify(color)}, ${px})`;
    let timer: NodeJS.Timeout | undefined;
    const size = (await Promise.race([
      w.webContents.executeJavaScript(call),
      new Promise((_, rej) => { timer = setTimeout(() => rej(new Error("tiempo de espera agotado")), 15000); }),
    ]).finally(() => clearTimeout(timer))) as { w: number; h: number };
    const pdf = await w.webContents.printToPDF({
      pageSize: { width: (size.w + 1) / 96, height: (size.h + 1) / 96 },
      margins: { top: 0, bottom: 0, left: 0, right: 0 },
      printBackground: false,
      pageRanges: "1",
    });
    return { ok: true, pdfB64: pdf.toString("base64"), widthPt: (size.w + 1) * 0.75, heightPt: (size.h + 1) * 0.75 };
  } catch (e) {
    const msg = (e instanceof Error ? e.message : String(e)).replace(/^Error invoking remote method[^:]*: /, "").replace(/^Error: /, "");
    if (msg.includes("tiempo de espera")) dropLatexWindow(); // a stuck page is not reused
    return { ok: false, error: msg };
  } finally {
    clearTimeout(latexIdle);
    latexIdle = setTimeout(dropLatexWindow, LATEX_IDLE_MS);
  }
}

function latexToPdf(req: LatexRequest): Promise<LatexResult> {
  const run = () => renderLatexPdf(req);
  const p = latexQueue.then(run, run);
  latexQueue = p.catch(() => undefined);
  return p;
}
ipcMain.handle("latex:pdf", (_e, req: LatexRequest) => latexToPdf(req));

// ---- preferences and recovery copies ------------------------------------------
interface Prefs { fingerDraw: boolean; saveOverOpened: boolean; autosaveMinutes: number; theme: "classic" | "neon"; palette: string; effects: "auto" | "full" | "reduced"; welcome: boolean; plane: string; space3: string; fill: string; tablet: string; sizes: string; textMd: boolean; toolbox: string; glowOn: boolean; glowSize: number; glowIntensity: number }
let prefs: Prefs = { fingerDraw: false, saveOverOpened: false, autosaveMinutes: 0, theme: "classic", palette: "classic", effects: "auto", welcome: true, plane: "", space3: "", fill: "", tablet: "", sizes: "", textMd: true, toolbox: "", glowOn: false, glowSize: 8, glowIntensity: 0.8 };
const prefsFile = () => path.join(app.getPath("userData"), "prefs.json");
// Test runs neither read nor write the user's preferences, and keep recovery copies in the temp folder.
const autosaveDir = () => (autoQuit ? path.join(app.getPath("temp"), "xl-test-autosave") : path.join(app.getPath("userData"), "autosave"));

const clampNum = (v: unknown, lo: number, hi: number, d: number) => (Number.isFinite(Number(v)) && v !== null && v !== "" ? Math.min(hi, Math.max(lo, Number(v))) : d);
function sanitizePrefs(v: unknown): Prefs {
  const o = typeof v === "object" && v !== null ? (v as Record<string, unknown>) : {};
  const m = Math.round(Number(o.autosaveMinutes));
  return { fingerDraw: o.fingerDraw === true, saveOverOpened: o.saveOverOpened === true, autosaveMinutes: Number.isFinite(m) ? Math.min(120, Math.max(0, m)) : 0, theme: o.theme === "neon" ? "neon" : "classic", palette: typeof o.palette === "string" && /^[a-z0-9-]{1,32}$/.test(o.palette) ? o.palette : "classic", effects: o.effects === "full" || o.effects === "reduced" ? o.effects : "auto", welcome: o.welcome !== false, plane: typeof o.plane === "string" && o.plane.length < 600 ? o.plane : "", space3: typeof o.space3 === "string" && o.space3.length < 1500 ? o.space3 : "", fill: typeof o.fill === "string" && o.fill.length < 500 ? o.fill : "", tablet: typeof o.tablet === "string" && o.tablet.length < 6000 ? o.tablet : "", sizes: typeof o.sizes === "string" && o.sizes.length < 1500 ? o.sizes : "", textMd: o.textMd !== false, toolbox: typeof o.toolbox === "string" && o.toolbox.length < 2000 ? o.toolbox : "", glowOn: o.glowOn === true, glowSize: clampNum(o.glowSize, 1, 40, 8), glowIntensity: clampNum(o.glowIntensity, 0.05, 1, 0.8) };
}
async function loadPrefs(): Promise<void> {
  if (cli.theme) prefs = { ...prefs, theme: cli.theme }; // test/screenshot runs can ask for a theme
  if (autoQuit) return;
  try { prefs = sanitizePrefs(JSON.parse(await fs.readFile(prefsFile(), "utf8"))); } catch { /* first run: defaults */ }
}
ipcMain.handle("prefs:get", () => ({ ...prefs, autosaveDir: autosaveDir() }));
ipcMain.handle("prefs:set", async (_e, v: unknown) => {
  prefs = sanitizePrefs({ ...prefs, ...(typeof v === "object" && v !== null ? v : {}) });
  if (!autoQuit) { try { await fs.mkdir(path.dirname(prefsFile()), { recursive: true }); await fs.writeFile(prefsFile(), JSON.stringify(prefs)); } catch { /* best effort */ } }
  return prefs;
});
ipcMain.handle("autosave:write", async (_e, xml: string, name: string) => {
  const safe = path.basename(String(name)).replace(/\.xopp$/i, "").replace(/[^\p{L}\p{N} ._()-]/gu, "_") || "documento";
  const dir = autosaveDir();
  await fs.mkdir(dir, { recursive: true });
  const file = path.join(dir, `${safe}.autosave.xopp`);
  await writeAtomic(file, xml);
  return file;
});

// ---- menu -------------------------------------------------------------------
function findItem(items: MenuItem[], labels: string[]): MenuItem | undefined {
  const [head, ...rest] = labels;
  const it = items.find((i) => i.label.replace(/&/g, "") === head);
  if (!it) return undefined;
  return rest.length ? findItem(it.submenu?.items ?? [], rest) : it;
}
// Test hook: click an item of the real application menu by its label path. Only honoured with --e2e.
ipcMain.handle("test:menuClick", (_e, labels: string[]) => {
  if (!cli.e2e) return false;
  const it = findItem(Menu.getApplicationMenu()?.items ?? [], labels);
  if (!it || !it.enabled) return false;
  it.click();
  return true;
});

interface MenuNode { label: string; accel?: string; type: "normal" | "separator" | "checkbox"; enabled: boolean; checked?: boolean; tip?: string; sub?: MenuNode[] }
function menuModel(items: MenuItem[]): MenuNode[] {
  return items.filter((it) => it.visible).map((it): MenuNode => it.type === "separator"
    ? { label: "", type: "separator", enabled: true }
    : { label: it.label, accel: it.accelerator ? String(it.accelerator) : undefined, type: it.type === "checkbox" ? "checkbox" : "normal", enabled: it.enabled, checked: it.checked, tip: it.toolTip || undefined, sub: it.submenu ? menuModel(it.submenu.items) : undefined });
}
ipcMain.handle("menu:get", () => menuModel(Menu.getApplicationMenu()?.items ?? []));
ipcMain.handle("menu:click", (_e, path: unknown) => {
  if (!Array.isArray(path) || !path.every((n) => Number.isInteger(n) && n >= 0)) return false;
  let items = (Menu.getApplicationMenu()?.items ?? []).filter((it) => it.visible);
  let hit: MenuItem | undefined;
  for (const i of path as number[]) { hit = items[i]; items = (hit?.submenu?.items ?? []).filter((it) => it.visible); }
  if (!hit || !hit.enabled || hit.type === "separator" || hit.submenu) return false;
  hit.click();
  return true;
});
ipcMain.on("app:titleBar", (e, color: unknown, symbol: unknown) => {
  const ok = (v: unknown): v is string => typeof v === "string" && /^#[0-9a-fA-F]{6}$/.test(v);
  if (!ok(color) || !ok(symbol)) return;
  try { winOf(e).setTitleBarOverlay({ color, symbolColor: symbol, height: BAR_HEIGHT }); } catch { /* not supported on this platform */ }
});

/** Send a command to the focused window's renderer. */
function cmd(id: string, arg?: unknown): void {
  const w = BrowserWindow.getFocusedWindow() ?? mainWin;
  w?.webContents.send("cmd", id, arg);
}

// Accelerators are shown but not registered: the renderer handles the keys itself,
// so a shortcut never fires twice.
function mi(label: string, id: string, accelerator?: string, extra: Partial<MenuItemConstructorOptions> = {}): MenuItemConstructorOptions {
  return { label, accelerator, registerAccelerator: false, click: (item) => cmd(id, item.type === "checkbox" ? item.checked : undefined), ...extra };
}
const todo = (label: string): MenuItemConstructorOptions => ({ label, enabled: false });
const sep: MenuItemConstructorOptions = { type: "separator" };

function buildMenu(): void {
  const recentItems: MenuItemConstructorOptions[] = recent.length
    ? [
        ...recent.map((p): MenuItemConstructorOptions => ({ label: path.basename(p), toolTip: p, click: () => cmd("file.openRecent", p) })),
        sep,
        { label: "Borrar la lista", click: () => { recent = []; void saveRecent(); buildMenu(); } },
      ]
    : [{ label: "(vacío)", enabled: false }];
  const paper = (id: string, label: string) => mi(label, `journal.paper:${id}`);
  const fmt = (id: string, label: string) => mi(label, `journal.format:${id}`);

  const template: MenuItemConstructorOptions[] = [
    { label: "&Archivo", submenu: [
      mi("Nuevo", "file.new", "CmdOrCtrl+N"),
      { label: "Nueva ventana", accelerator: "CmdOrCtrl+Shift+N", click: () => createWindow(false) },
      mi("Abrir…", "file.open", "CmdOrCtrl+O"),
      { label: "Abrir reciente", submenu: recentItems },
      mi("Abrir copia de recuperación…", "file.openAutosave"),
      todo("Anotar PDF…"),
      sep,
      mi("Guardar", "file.save", "CmdOrCtrl+S"),
      mi("Guardar como…", "file.saveAs", "CmdOrCtrl+Shift+S"),
      mi("Guardar copia…", "file.saveCopy"),
      sep,
      mi("Exportar como PDF…", "file.exportPdf"),
      todo("Exportar como…"),
      todo("Imprimir…"),
      sep,
      mi("Cerrar", "file.close", "CmdOrCtrl+W"),
      { label: "Salir", accelerator: "CmdOrCtrl+Q", click: () => app.quit() },
    ] },
    { label: "&Editar", submenu: [
      mi("Deshacer", "edit.undo", "CmdOrCtrl+Z"),
      mi("Rehacer", "edit.redo", "CmdOrCtrl+Y"),
      sep,
      mi("Cortar", "edit.cut", "CmdOrCtrl+X"),
      mi("Copiar", "edit.copy", "CmdOrCtrl+C"),
      mi("Pegar", "edit.paste", "CmdOrCtrl+V"),
      mi("Eliminar", "edit.delete", "Delete"),
      sep,
      mi("Buscar…", "edit.find", "CmdOrCtrl+F"),
      mi("Preferencias…", "edit.prefs", "CmdOrCtrl+,"),
    ] },
    { label: "&Ver", submenu: [
      mi("Barra de herramientas", "view.toolbars", undefined, { type: "checkbox", checked: true }),
      { label: "Barras laterales", submenu: [mi("Vista previa de páginas", "view.sidebarPages"), mi("Capas", "view.sidebarLayers")] },
      mi("Pantalla completa", "view.fullscreen", "F11"),
      mi("Modo de presentación", "view.presentation", "F5"),
      { label: "Zoom", submenu: [
        mi("Acercar", "view.zoomIn", "CmdOrCtrl+Plus"),
        mi("Alejar", "view.zoomOut", "CmdOrCtrl+-"),
        mi("Tamaño normal (100 %)", "view.zoom100", "CmdOrCtrl+0"),
        mi("Ajustar al ancho", "view.fitWidth"),
        mi("Ajustar a la página", "view.fitPage"),
      ] },
      mi("Vista continua", "view.continuous", undefined, { type: "checkbox", checked: true }),
    ] },
    { label: "&Navegación", submenu: [
      mi("Primera página", "nav.first", "CmdOrCtrl+Home"),
      mi("Página anterior", "nav.prev", "PageUp"),
      mi("Página siguiente", "nav.next", "PageDown"),
      mi("Última página", "nav.last", "CmdOrCtrl+End"),
      mi("Ir a la página…", "nav.goto", "CmdOrCtrl+G"),
      sep,
      mi("Atrás", "nav.back", "Alt+Left"),
      mi("Adelante", "nav.forward", "Alt+Right"),
    ] },
    { label: "&Diario", submenu: [
      mi("Nueva página", "journal.newPage"),
      mi("Insertar página antes", "journal.insertBefore"),
      mi("Insertar página al final", "journal.append"),
      mi("Eliminar página actual", "journal.delete"),
      mi("Mover página hacia arriba", "journal.moveUp"),
      mi("Mover página hacia abajo", "journal.moveDown"),
      sep,
      { label: "Fondo de página", submenu: [
        mi("Color de fondo…", "journal.bgColor"),
        { label: "Tipo de papel", submenu: [paper("plain", "Liso"), paper("lined", "Rayado (con margen)"), paper("ruled", "Reglado"), paper("graph", "Cuadriculado"), paper("dotted", "Puntos"), sep, ...PAPER_KINDS.map((k) => paper(`x:${k.id}`, k.label))] },
        mi("Aplicar a todas las páginas", "journal.applyAll", undefined, { type: "checkbox", checked: false }),
      ] },
      { label: "Formato de papel", submenu: [fmt("A3", "A3"), fmt("A4", "A4"), fmt("A5", "A5"), fmt("Letter", "Carta (Letter)"), fmt("Legal", "Legal"), sep, mi("Girar hoja", "journal.rotate")] },
    ] },
    { label: "&Herramientas", submenu: [
      mi("Lápiz", "tool.pen", "P"),
      mi("Resaltador", "tool.highlighter", "H"),
      mi("Goma (trazo completo)", "tool.eraser", "E"),
      mi("Borrar solo la zona que tocas", "tool.eraserPart", "B"),
      mi("Texto", "tool.text", "T"),
      mi("LaTeX…", "tool.latex", "X"),
      mi("Imagen…", "tool.image"),
      mi("Formas", "tool.shape", "G"),
      sep,
      mi("Seleccionar objeto (rectángulo)", "tool.select", "S"),
      mi("Seleccionar región (lazo)", "tool.lasso", "L"),
      mi("Mano", "tool.hand", "M"),
      sep,
      mi("Espacio vertical", "tool.vspace", "V"),
      mi("Puntero láser", "tool.laser", "K"),
      mi("Relleno", "tool.fill", "U"),
      mi("Plano cartesiano", "tool.plane", "C"),
      mi("Espacio cartesiano 3D", "tool.space3", "D"),
      mi("Figuras (líneas, curvas, polígonos, sólidos)", "tool.figure", "F"),
      mi("Recta real (intervalos y puntos)", "tool.numline", "N"),
      mi("Diagrama de conjuntos (Venn)", "tool.venn", "J"),
      sep,
      mi("Mostrar el cuadro de herramientas", "tool.toolbox", "Q"),
      mi("Activar o desactivar el cuadro de herramientas", "tool.toolboxEnable"),
      mi("Lápiz y tableta…", "pen.settings"),
      mi("Formas automáticas", "tool.recognizer", undefined, { type: "checkbox", checked: false }),
    ] },
    { label: "&Plugins", submenu: [todo("Administrar plugins (no disponible)")] },
    { label: "A&yuda", submenu: [
      mi("Bienvenida", "help.welcome"),
      mi("Diagnóstico del lápiz…", "help.pen"),
      { label: "Manual de Xournal++ (abre el navegador)", click: () => void shell.openExternal(MANUAL_URL) },
      { label: "Acerca de", click: () => {
        void dialog.showMessageBox(BrowserWindow.getFocusedWindow() ?? mainWin!, {
          type: "info", title: "Acerca de Xournal Lab", message: "Xournal Lab 0.1.0",
          detail: `Visor y editor experimental de documentos .xopp (formato de Xournal++ 1.3.8).\n\nElectron ${process.versions.electron} · Chromium ${process.versions.chrome} · Node ${process.versions.node}\n\nProyecto independiente. No es Xournal++ ni está afiliado a su proyecto. Xournal++ es software libre (GPL-2.0).`,
        });
      } },
    ] },
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
  for (const w of BrowserWindow.getAllWindows()) { w.removeMenu(); w.webContents.send("menu:changed"); } // setting the menu puts it back on every window
}

void app.whenReady().then(async () => {
  if (cli.stress) setTimeout(() => { console.log("GPUSTATUS " + JSON.stringify(app.getGPUFeatureStatus())); void app.getGPUInfo("basic").then((i) => console.log("GPUINFO " + JSON.stringify(i).slice(0, 700))); }, 6000);
  await loadRecent();
  await loadPrefs();
  buildMenu();
  mainWin = createWindow(true);
});

app.on("window-all-closed", () => app.quit());
