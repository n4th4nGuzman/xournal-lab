import katex from "katex";
import type { Editor } from "./editor";
import { pointInRect, restore, snapshot, texContains } from "./geometry";
import { activeLayer, type ImageEl, type Layer, type TexImageEl } from "./model";
import { rasterizeInk } from "./pdfraster";
import { fmt8 } from "./serialize";
import { CARET, PALETTE_CATEGORIES, searchPalette, type PaletteItem } from "./latexpalette";

const PT_TO_PX = 96 / 72;
const BITMAP_PX_PER_PT = 4;
const BITMAP_MAX_PX = 2048;

export interface LatexOptions {
  latex: string;
  display: boolean;
  sizePt: number;
  /** Store a plain PNG image instead of an editable formula (most compatible, but not editable later). */
  asImage: boolean;
}

type Inserted = { ok: true; el: TexImageEl | ImageEl } | { ok: false; error: string };

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** The LaTeX tool: a dialog with live preview, and insertion/editing of formulas. */
export class LatexTool {
  lastDisplay = true;
  lastSize = 14;
  private dlg = $("latexDlg");
  private area = $<HTMLTextAreaElement>("ltx");
  private prev = $("ltxPrev");
  private err = $("ltxErr");
  private sizeSel = $<HTMLSelectElement>("ltxSize");
  private asImage = $<HTMLInputElement>("ltxImage");
  private ok = $<HTMLButtonElement>("ltxOk");
  private ctx: { page: number; x: number; y: number; existing: { el: TexImageEl; layer: Layer } | null } | null = null;
  private timer = 0;
  private busy = false;

  constructor(private ed: Editor) {
    this.buildPalette();
    this.area.addEventListener("input", () => this.schedulePreview());
    this.area.addEventListener("keydown", (e) => this.onKey(e));
    this.dlg.querySelectorAll<HTMLInputElement>("input[name=ltxMode]").forEach((r) => r.addEventListener("change", () => this.schedulePreview(0)));
    this.sizeSel.addEventListener("change", () => this.schedulePreview(0));
    $("ltxCancel").addEventListener("click", () => this.close());
    this.ok.addEventListener("click", () => void this.commit());
    this.dlg.addEventListener("pointerdown", (e) => { if (e.target === this.dlg) this.close(); });
  }

  get isOpen(): boolean { return this.ctx !== null; }

  private get display(): boolean { return (this.dlg.querySelector("input[name=ltxMode]:checked") as HTMLInputElement).value === "display"; }
  private get sizePt(): number { return Number(this.sizeSel.value); }

  // ---- dialog -------------------------------------------------------------------
  /** Open the dialog for a click at (x, y): edits the formula under the point, or starts a new one. */
  openAt(page: number, x: number, y: number): void {
    const doc = this.ed.host.doc();
    if (!doc) return;
    let existing: { el: TexImageEl; layer: Layer } | null = null;
    const p = doc.pages[page]!;
    for (let l = p.layers.length - 1; l >= 0 && !existing; l--) {
      const layer = p.layers[l]!;
      if (layer.hidden) continue;
      for (let i = layer.elements.length - 1; i >= 0; i--) {
        const el = layer.elements[i]!;
        if (el.kind === "teximage" && texContains(el, x, y)) { existing = { el, layer }; break; }
      }
    }
    this.openWith({ page, x, y, existing });
  }

  /** Edit a specific formula (used by double-click on a selected one). */
  edit(page: number, el: TexImageEl, layer: Layer): void {
    this.openWith({ page, x: el.left, y: el.top, existing: { el, layer } });
  }

  private openWith(ctx: NonNullable<LatexTool["ctx"]>): void {
    this.ctx = ctx;
    this.area.value = ctx.existing?.el.latex ?? "";
    this.sizeSel.value = String(this.lastSize);
    (this.dlg.querySelector(`input[name=ltxMode][value=${this.lastDisplay ? "display" : "inline"}]`) as HTMLInputElement).checked = true;
    this.asImage.checked = false;
    this.asImage.disabled = !!ctx.existing;
    this.ok.textContent = ctx.existing ? "Aplicar cambios" : "Insertar";
    this.err.textContent = "";
    this.dlg.hidden = false;
    this.renderPreview();
    setTimeout(() => { this.area.focus(); this.area.setSelectionRange(this.area.value.length, this.area.value.length); }, 0);
  }

  close(): void {
    this.ctx = null;
    this.dlg.hidden = true;
    window.clearTimeout(this.timer);
  }

  private onKey(e: KeyboardEvent): void {
    e.stopPropagation(); // typing: keep tool shortcuts and Delete from firing
    if (e.key === "Escape") { e.preventDefault(); this.close(); return; }
    if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); void this.commit(); return; }
    // Typing "{" adds the closing brace; typing "}" in front of one just steps over it.
    const a = this.area;
    if (e.key === "{" && !e.ctrlKey && !e.altKey) {
      e.preventDefault();
      this.replaceSelection("{}", 1);
    } else if (e.key === "}" && a.selectionStart === a.selectionEnd && a.value[a.selectionStart] === "}") {
      e.preventDefault();
      a.setSelectionRange(a.selectionStart + 1, a.selectionStart + 1);
    }
  }

  private replaceSelection(text: string, caretBack: number): void {
    const a = this.area;
    a.setRangeText(text, a.selectionStart, a.selectionEnd, "end");
    const pos = a.selectionStart - caretBack;
    a.setSelectionRange(pos, pos);
    this.schedulePreview();
  }

  private insertSnippet(snippet: string): void {
    const caret = snippet.indexOf(CARET);
    const clean = snippet.replace(CARET, "");
    this.replaceSelection(clean, caret < 0 ? 0 : clean.length - caret);
    this.area.focus();
  }

  /** The most recently used palette items (kept for next time, when the browser lets us). */
  private recent: PaletteItem[] = (() => {
    try { const v = JSON.parse(localStorage.getItem("xl.latexRecent") ?? "[]"); return Array.isArray(v) ? (v as PaletteItem[]).filter((i) => typeof i?.snippet === "string" && typeof i?.tex === "string").slice(0, 16) : []; } catch { return []; }
  })();
  private remember(item: PaletteItem): void {
    this.recent = [item, ...this.recent.filter((r) => r.snippet !== item.snippet)].slice(0, 16);
    try { localStorage.setItem("xl.latexRecent", JSON.stringify(this.recent)); } catch { /* not saved: fine */ }
  }
  private tab = "struct";

  /** A palette button showing the item as typeset maths. */
  private paletteButton(item: PaletteItem): HTMLButtonElement {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "palBtn";
    try { b.innerHTML = katex.renderToString(item.tex, { throwOnError: true, displayMode: false, output: "html" }); } catch { b.textContent = item.tex; }
    b.title = `${item.name}
${item.snippet.replace(CARET, "")}`;
    b.dataset.snippet = item.snippet;
    b.addEventListener("click", () => { this.insertSnippet(item.snippet); this.remember(item); });
    return b;
  }

  private renderGrid(): void {
    const grid = $("ltxGrid");
    const q = $<HTMLInputElement>("ltxSearch").value.trim();
    let items: PaletteItem[];
    if (q) items = searchPalette(q);
    else if (this.tab === "recent") items = this.recent;
    else items = PALETTE_CATEGORIES.find((c) => c.id === this.tab)?.items ?? [];
    grid.replaceChildren(...items.map((i) => this.paletteButton(i)));
    if (!items.length) {
      const p = document.createElement("span");
      p.className = "palEmpty";
      p.textContent = q ? "Nada con ese nombre: prueba otra palabra (raíz, integral, alfa, matriz…)" : "Aún no has usado ningún símbolo";
      grid.appendChild(p);
    }
    $("ltxTabs").querySelectorAll<HTMLElement>("button").forEach((t) => t.classList.toggle("on", !q && t.dataset.tab === this.tab));
  }

  private buildPalette(): void {
    const pal = $("ltxPal");
    pal.replaceChildren();
    const head = document.createElement("div");
    head.className = "palHead";
    const tabs = document.createElement("div");
    tabs.id = "ltxTabs";
    tabs.className = "palTabs";
    for (const [id, title] of [["recent", "Recientes"], ...PALETTE_CATEGORIES.map((c) => [c.id, c.title] as const)] as Array<[string, string]>) {
      const b = document.createElement("button");
      b.type = "button";
      b.dataset.tab = id;
      b.textContent = title;
      b.addEventListener("click", () => { this.tab = id; $<HTMLInputElement>("ltxSearch").value = ""; this.renderGrid(); });
      tabs.appendChild(b);
    }
    const search = document.createElement("input");
    search.id = "ltxSearch";
    search.type = "search";
    search.placeholder = "Buscar: raíz, integral, alfa…";
    search.spellcheck = false;
    search.addEventListener("input", () => this.renderGrid());
    search.addEventListener("keydown", (e) => { e.stopPropagation(); if (e.key === "Escape" && search.value) { search.value = ""; this.renderGrid(); e.preventDefault(); } });
    head.append(tabs, search);
    const grid = document.createElement("div");
    grid.id = "ltxGrid";
    grid.className = "palGrid";
    pal.append(head, grid);
    this.renderGrid();
  }

  // ---- live preview --------------------------------------------------------------
  private schedulePreview(delay = 60): void {
    window.clearTimeout(this.timer);
    this.timer = window.setTimeout(() => this.renderPreview(), delay);
  }

  private renderPreview(): void {
    const src = this.area.value;
    this.prev.style.fontSize = `${this.sizePt * PT_TO_PX}px`;
    this.prev.style.color = this.ed.color;
    if (!src.trim()) { this.prev.textContent = ""; this.err.textContent = ""; this.ok.disabled = true; return; }
    try {
      katex.render(src, this.prev, { displayMode: this.display, throwOnError: true, trust: false, strict: "ignore", output: "html" });
      this.err.textContent = "";
      this.ok.disabled = false;
    } catch (e) {
      this.err.textContent = (e instanceof Error ? e.message : String(e)).replace(/^KaTeX parse error:\s*/, "");
      this.ok.disabled = true;
    }
  }

  // ---- inserting ---------------------------------------------------------------
  private async commit(): Promise<void> {
    const c = this.ctx;
    if (!c || this.busy || this.ok.disabled) return;
    this.busy = true;
    this.ok.disabled = true;
    const label = this.ok.textContent;
    this.ok.textContent = "Insertando…";
    const o: LatexOptions = { latex: this.area.value, display: this.display, sizePt: this.sizePt, asImage: this.asImage.checked };
    const res = c.existing ? await this.replace(c.page, c.existing.el, c.existing.layer, o) : await this.insert(c.page, c.x, c.y, o);
    this.busy = false;
    this.ok.textContent = label;
    if (!res.ok) { this.err.textContent = res.error; this.ok.disabled = false; return; }
    this.lastDisplay = o.display;
    this.lastSize = o.sizePt;
    this.close();
  }

  private async renderFormula(o: LatexOptions) {
    const color = this.ed.color;
    const res = await window.xournal.latexPdf({ latex: o.latex, display: o.display, color, px: o.sizePt * PT_TO_PX });
    if (!res.ok) return res;
    const bitmap = await rasterizeInk(res.pdfB64, Math.min(BITMAP_MAX_PX, Math.max(64, Math.round(res.widthPt * BITMAP_PX_PER_PT))), color);
    return { ...res, bitmap };
  }

  /** Insert a new formula with its top-left corner near (x, y), kept inside the page. */
  async insert(page: number, x: number, y: number, o: LatexOptions): Promise<Inserted> {
    const doc = this.ed.host.doc();
    if (!doc) return { ok: false, error: "No hay documento abierto" };
    const r = await this.renderFormula(o);
    if (!r.ok) return r;
    const pg = doc.pages[page]!;
    const w = r.widthPt, h = r.heightPt;
    const left = Math.max(2, Math.min(x, pg.width - w - 2));
    const top = Math.max(2, Math.min(y, pg.height - h - 2));
    const attrs: Array<[string, string]> = [["left", fmt8(left)], ["top", fmt8(top)], ["right", fmt8(left + w)], ["bottom", fmt8(top + h)]];
    const layer = activeLayer(pg);
    let el: TexImageEl | ImageEl;
    if (o.asImage) {
      const png = r.bitmap.toDataURL("image/png").split(",")[1] ?? "";
      el = { kind: "image", attrs, left, top, right: left + w, bottom: top + h, b64: png, bitmap: r.bitmap };
    } else {
      el = { kind: "teximage", attrs: [["text", o.latex], ...attrs], latex: o.latex, left, top, right: left + w, bottom: top + h, pdfB64: r.pdfB64, bitmap: r.bitmap };
    }
    layer.elements.push(el);
    doc.elementCount++;
    this.ed.pushAction({
      page,
      undo: () => { const i = layer.elements.lastIndexOf(el); if (i >= 0) { layer.elements.splice(i, 1); doc.elementCount--; } },
      redo: () => { layer.elements.push(el); doc.elementCount++; },
    });
    this.ed.host.repaint(page);
    this.ed.afterCreate(this.ed.tool);
    this.ed.selection.selectOnly(page, layer, el);
    return { ok: true, el };
  }

  /** Re-render an existing formula from new source; its top-left corner stays put. */
  async replace(page: number, el: TexImageEl, layer: Layer, o: LatexOptions): Promise<Inserted> {
    const r = await this.renderFormula(o);
    if (!r.ok) return r;
    const before = snapshot(el);
    el.latex = o.latex;
    el.pdfB64 = r.pdfB64;
    el.bitmap = r.bitmap;
    el.failed = false;
    el.right = el.left + r.widthPt;
    el.bottom = el.top + r.heightPt;
    const after = snapshot(el);
    this.ed.pushAction({ page, undo: () => restore(before), redo: () => restore(after) });
    this.ed.host.repaint(page);
    this.ed.selection.selectOnly(page, layer, el);
    return { ok: true, el };
  }
}
