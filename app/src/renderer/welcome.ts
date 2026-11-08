import { installIcons } from "./icons";
import { blankDocument } from "./journal";
import { PAPER_KINDS, defaultSpec } from "./paper";
import { renderPage } from "./render";

export interface RecentFile { path: string; name: string; dir: string }

/** What the welcome page can ask the app to do. */
export interface WelcomeHost {
  newDocument(): void;
  /** A new document whose first page has this paper: a built-in style (`graph`) or a special one (`x:log`). */
  newWithPaper(value: string): void;
  open(): void;
  openRecent(file: string): void;
  openAutosave(): void;
  recent(): Promise<RecentFile[]>;
  clearRecent(): Promise<void>;
  theme(): "classic" | "neon";
  toggleTheme(): void;
  showOnStart(): boolean;
  setShowOnStart(on: boolean): void;
  /** Is a document open (then the page is an overlay that can be closed)? */
  hasDocument(): boolean;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** Papers offered as starting points, with the label shown on the card. */
export const START_PAPERS: Array<[value: string, label: string]> = [
  ["plain", "Liso"], ["graph", "Cuadriculado"], ["lined", "Rayado"], ["dotted", "Puntos"],
  ...PAPER_KINDS.map((k): [string, string] => [`x:${k.id}`, k.label]),
];

/** A small picture of a page with this paper, drawn by the same code that draws real pages. */
const thumbCache = new Map<string, HTMLCanvasElement>();
export function paperThumbnail(value: string, width = 66): HTMLCanvasElement {
  const key = `${value}|${width}|${window.devicePixelRatio || 1}`;
  const kept = thumbCache.get(key);
  if (kept) return kept; // the page is shown again and again (theme switch, clearing the list): its pictures are drawn once
  const doc = blankDocument();
  const page = doc.pages[0]!;
  if (value.startsWith("x:")) {
    const spec = defaultSpec(value.slice(2));
    const kind = PAPER_KINDS.find((k) => k.id === value.slice(2));
    if (spec && kind) { page.background.paper = spec; page.background.style = kind.fallback; }
  } else page.background.style = value;
  const cv = document.createElement("canvas");
  const dpr = window.devicePixelRatio || 1;
  const w = Math.round(width * dpr), h = Math.round(width * (page.height / page.width) * dpr);
  cv.width = w;
  cv.height = h;
  cv.style.width = `${width}px`;
  cv.style.height = `${(h / dpr).toFixed(1)}px`;
  const ctx = cv.getContext("2d")!;
  ctx.setTransform(w / page.width, 0, 0, h / page.height, 0, 0);
  renderPage(ctx, page, () => undefined);
  thumbCache.set(key, cv);
  return cv;
}

/** The welcome page: start a document, reopen a recent one, or begin on a special paper. */
export class Welcome {
  private el = $("welcome");

  constructor(private host: WelcomeHost) {
    this.el.addEventListener("keydown", (e) => { if (e.key === "Escape" && this.host.hasDocument()) { this.hide(); e.stopPropagation(); } });
  }

  get visible(): boolean { return !this.el.hidden; }

  hide(): void { this.el.hidden = true; }

  async show(): Promise<void> {
    this.el.hidden = false;
    await this.render();
    this.el.querySelector<HTMLElement>("button")?.focus();
  }

  private link(icon: string, label: string, hint: string, run: () => void): HTMLButtonElement {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "wLink";
    const ic = document.createElement("span");
    ic.dataset.icon = icon;
    ic.className = "wIcon";
    const t = document.createElement("span");
    t.className = "wText";
    t.textContent = label;
    b.append(ic, t);
    if (hint) { const h = document.createElement("kbd"); h.textContent = hint; b.append(h); }
    b.addEventListener("click", run);
    return b;
  }

  private async render(): Promise<void> {
    const recent = await this.host.recent().catch(() => [] as RecentFile[]);
    const close = () => this.hide();
    const root = document.createElement("div");
    root.className = "wPage";

    const head = document.createElement("div");
    head.className = "wHead";
    const h1 = document.createElement("h1");
    h1.textContent = "Xournal Lab";
    const sub = document.createElement("p");
    sub.textContent = "Notas a mano, matemáticas y física";
    head.append(h1, sub);
    if (this.host.hasDocument()) {
      const x = document.createElement("button");
      x.type = "button";
      x.className = "wClose";
      x.textContent = "✕";
      x.title = "Cerrar la bienvenida (Esc)";
      x.addEventListener("click", close);
      head.append(x);
    }

    const left = document.createElement("div");
    left.className = "wCol";
    const start = document.createElement("h2");
    start.textContent = "Empezar";
    left.append(
      start,
      this.link("pageAdd", "Nuevo documento", "Ctrl+N", () => { this.hide(); this.host.newDocument(); }),
      this.link("open", "Abrir archivo…", "Ctrl+O", () => { this.host.open(); }),
      this.link("undo", "Abrir copia de recuperación…", "", () => { this.host.openAutosave(); }),
    );
    const rh = document.createElement("h2");
    rh.textContent = "Recientes";
    left.append(rh);
    if (!recent.length) {
      const none = document.createElement("p");
      none.className = "wDim";
      none.textContent = "Aún no has abierto ningún archivo.";
      left.append(none);
    }
    for (const f of recent.slice(0, 8)) {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "wRecent";
      b.title = f.path;
      b.dataset.path = f.path;
      const n = document.createElement("span");
      n.className = "wName";
      n.textContent = f.name;
      const d = document.createElement("span");
      d.className = "wDir";
      d.textContent = f.dir;
      b.append(n, d);
      b.addEventListener("click", () => { this.host.openRecent(f.path); });
      left.append(b);
    }
    if (recent.length) {
      const clear = document.createElement("button");
      clear.type = "button";
      clear.className = "wSmall";
      clear.id = "wClear";
      clear.textContent = "Borrar la lista";
      clear.addEventListener("click", () => { void this.host.clearRecent().then(() => this.render()); });
      left.append(clear);
    }

    const right = document.createElement("div");
    right.className = "wCol";
    const ph = document.createElement("h2");
    ph.textContent = "Empezar con un papel";
    const grid = document.createElement("div");
    grid.className = "wPapers";
    for (const [value, label] of START_PAPERS) {
      const card = document.createElement("button");
      card.type = "button";
      card.className = "wPaper";
      card.dataset.paper = value;
      card.title = `Nuevo documento con papel ${label.toLowerCase()}`;
      const cap = document.createElement("span");
      cap.textContent = label;
      card.append(paperThumbnail(value), cap);
      card.addEventListener("click", () => { this.hide(); this.host.newWithPaper(value); });
      grid.append(card);
    }
    const th = document.createElement("h2");
    th.textContent = "Apariencia";
    const theme = document.createElement("button");
    theme.type = "button";
    theme.id = "wTheme";
    theme.className = "wLink";
    const ti = document.createElement("span");
    ti.dataset.icon = "theme";
    ti.className = "wIcon";
    const tt = document.createElement("span");
    tt.className = "wText";
    tt.textContent = `Tema: ${this.host.theme() === "neon" ? "Neon (synthwave)" : "Clásico"} — cambiar`;
    theme.append(ti, tt);
    theme.addEventListener("click", () => { this.host.toggleTheme(); void this.render(); });
    const tips = document.createElement("ul");
    tips.className = "wTips";
    for (const t of ["P lápiz · H resaltador · E goma · U relleno · X fórmula LaTeX", "Arrastra con el relleno una línea para pintar a partir de ella o hacer sumas de Riemann", "Los papeles de matemáticas y física están en Diario → Fondo de página → Tipo de papel"]) {
      const li = document.createElement("li");
      li.textContent = t;
      tips.append(li);
    }
    right.append(ph, grid, th, theme, tips);

    const foot = document.createElement("label");
    foot.className = "wFoot";
    const box = document.createElement("input");
    box.type = "checkbox";
    box.id = "wShow";
    box.checked = this.host.showOnStart();
    box.addEventListener("change", () => this.host.setShowOnStart(box.checked));
    foot.append(box, " Mostrar la bienvenida al iniciar");

    const cols = document.createElement("div");
    cols.className = "wCols";
    cols.append(left, right);
    root.append(head, cols, foot);
    this.el.replaceChildren(root);
    installIcons(this.el);
  }
}
