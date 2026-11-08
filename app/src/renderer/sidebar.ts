import type { Editor } from "./editor";
import type { Layers } from "./layers";
import { layerName, type Layer } from "./model";
import { renderPage } from "./render";

export interface SidebarApi {
  goTo(i: number): void;
  current(): number;
  newPage(): void;
  deletePage(): void;
  movePage(delta: -1 | 1): void;
}

type Tab = "pages" | "layers";
const THUMB_CSS_W = 120;
const REFRESH_DELAY_MS = 250;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const button = (text: string, title: string, onClick: () => void): HTMLButtonElement => {
  const b = document.createElement("button");
  b.type = "button";
  b.textContent = text;
  b.title = title;
  b.addEventListener("click", onClick);
  return b;
};

/** Left panel: page thumbnails and the layer manager. */
export class Sidebar {
  private side = $("side");
  private thumbsEl = $("thumbs");
  private lyList = $("lyList");
  private paneTools = { pages: $("panePagesTools"), layers: $("paneLayersTools") };
  private tab: Tab = "pages";
  private canvases: HTMLCanvasElement[] = [];
  private shown = new Set<number>();
  private io: IntersectionObserver;
  private timer = 0;

  constructor(private ed: Editor, private layers: Layers, private api: SidebarApi) {
    this.io = new IntersectionObserver((entries) => {
      for (const e of entries) {
        const i = Number((e.target as HTMLElement).dataset.i);
        if (e.isIntersecting) { this.shown.add(i); this.draw(i); } else this.shown.delete(i);
      }
    }, { root: this.thumbsEl, rootMargin: "200px 0px" });
    $("tabPages").addEventListener("click", () => this.show("pages"));
    $("tabLayers").addEventListener("click", () => this.show("layers"));
    $("sideClose").addEventListener("click", () => this.hide());
    this.paneTools.pages.replaceChildren(
      button("＋", "Nueva página después de la actual", () => api.newPage()),
      button("🗑", "Eliminar la página actual", () => api.deletePage()),
      button("▲", "Subir la página", () => api.movePage(-1)),
      button("▼", "Bajar la página", () => api.movePage(1)),
    );
    this.paneTools.layers.replaceChildren(
      button("＋", "Nueva capa sobre la activa", () => { if (this.ed.host.doc()) { this.layers.add(api.current()); } }),
      button("🗑", "Eliminar la capa activa con todo su contenido", () => { const l = this.activeLayer(); if (l) this.layers.remove(api.current(), l); }),
      button("▲", "Subir la capa activa (se dibuja encima)", () => { const l = this.activeLayer(); if (l) this.layers.move(api.current(), l, 1); }),
      button("▼", "Bajar la capa activa", () => { const l = this.activeLayer(); if (l) this.layers.move(api.current(), l, -1); }),
    );
  }

  get visible(): boolean { return !this.side.hidden; }

  private activeLayer(): Layer | null {
    const doc = this.ed.host.doc();
    if (!doc) return null;
    const p = doc.pages[this.api.current()]!;
    return p.active && p.layers.includes(p.active) ? p.active : p.layers[p.layers.length - 1] ?? null;
  }

  /** Show a tab; pressing the same tab's command again hides the panel. */
  toggle(tab: Tab): void {
    if (this.visible && this.tab === tab) this.hide();
    else this.show(tab);
  }
  show(tab: Tab): void {
    this.tab = tab;
    this.side.hidden = false;
    $("tabPages").classList.toggle("on", tab === "pages");
    $("tabLayers").classList.toggle("on", tab === "layers");
    $("panePages").hidden = tab !== "pages";
    $("paneLayers").hidden = tab !== "layers";
    if (tab === "pages") { this.rebuild(); this.setCurrent(this.api.current()); } else this.renderLayers();
  }
  hide(): void { this.side.hidden = true; }

  // ---- thumbnails ---------------------------------------------------------------
  /** Rebuild the thumbnail list (pages were added, removed, moved or resized). */
  rebuild(): void {
    if (!this.visible) return; // nothing to build while the panel is closed: opening it rebuilds
    this.io.disconnect();
    this.shown.clear();
    this.canvases = [];
    this.thumbsEl.replaceChildren();
    const doc = this.ed.host.doc();
    if (!doc) { this.renderLayers(); return; }
    const dpr = window.devicePixelRatio || 1;
    doc.pages.forEach((p, i) => {
      const box = document.createElement("div");
      box.className = "thumb";
      box.dataset.i = String(i);
      const cv = document.createElement("canvas");
      cv.width = Math.round(THUMB_CSS_W * dpr);
      cv.height = Math.max(1, Math.round(THUMB_CSS_W * (p.height / p.width) * dpr));
      cv.style.width = `${THUMB_CSS_W}px`;
      cv.style.height = `${THUMB_CSS_W * (p.height / p.width)}px`;
      const label = document.createElement("div");
      label.textContent = String(i + 1);
      box.append(cv, label);
      box.addEventListener("click", () => this.api.goTo(i));
      this.thumbsEl.appendChild(box);
      this.canvases.push(cv);
      this.io.observe(box);
    });
    this.renderLayers();
  }

  private draw(i: number): void {
    const doc = this.ed.host.doc();
    const cv = this.canvases[i];
    const page = doc?.pages[i];
    if (!cv || !page) return;
    const ctx = cv.getContext("2d")!;
    ctx.setTransform(cv.width / page.width, 0, 0, cv.height / page.height, 0, 0);
    renderPage(ctx, page, () => { if (this.shown.has(i)) this.draw(i); }, undefined, true);
  }

  setCurrent(i: number): void {
    if (!this.visible) return;
    this.thumbsEl.querySelectorAll(".thumb").forEach((t) => t.classList.toggle("cur", Number((t as HTMLElement).dataset.i) === i));
    if (this.tab === "pages") this.thumbsEl.querySelector(`.thumb[data-i="${i}"]`)?.scrollIntoView({ block: "nearest" });
    else this.renderLayers();
  }

  /** The document changed: refresh thumbnails (debounced) and the layer list. */
  refresh(page?: number): void {
    if (!this.visible) return;
    if (this.tab === "layers") this.renderLayers();
    if (page === undefined) this.dirtyAll = true; else this.dirty.add(page);
    window.clearTimeout(this.timer);
    this.timer = window.setTimeout(() => {
      const all = this.dirtyAll;
      const which = [...this.dirty];
      this.dirtyAll = false;
      this.dirty.clear();
      // only the thumbnails of the pages that changed (all of them when it is not known which)
      this.shown.forEach((i) => { if (all || which.includes(i)) this.draw(i); });
    }, REFRESH_DELAY_MS);
  }
  private dirtyAll = false;
  private dirty = new Set<number>();

  // ---- layers ---------------------------------------------------------------------
  renderLayers(): void {
    this.lyList.replaceChildren();
    const doc = this.ed.host.doc();
    if (!doc || !this.visible) return;
    const pageIdx = this.api.current();
    const page = doc.pages[pageIdx]!;
    const active = this.activeLayer();
    // Top layer first, as in a drawing program.
    for (let k = page.layers.length - 1; k >= 0; k--) {
      const layer = page.layers[k]!;
      const li = document.createElement("li");
      li.className = (layer === active ? "act " : "") + (layer.hidden ? "off" : "");
      const eye = button(layer.hidden ? "○" : "●", layer.hidden ? "Mostrar la capa" : "Ocultar la capa (solo en pantalla)", () => this.layers.toggleHidden(pageIdx, layer));
      eye.className = "eye";
      const name = document.createElement("span");
      name.className = "name";
      name.textContent = layerName(page, layer);
      name.title = "Doble clic para renombrar";
      const count = document.createElement("span");
      count.className = "cnt";
      count.textContent = String(layer.elements.length);
      name.addEventListener("dblclick", () => this.rename(pageIdx, layer, name));
      li.addEventListener("click", (e) => { if (e.target === eye || (e.target as HTMLElement).tagName === "INPUT") return; this.layers.setActive(pageIdx, layer); });
      li.append(eye, name, count);
      this.lyList.appendChild(li);
    }
  }

  private rename(pageIdx: number, layer: Layer, nameEl: HTMLElement): void {
    const input = document.createElement("input");
    input.value = nameEl.textContent ?? "";
    let done = false;
    const finish = (apply: boolean) => {
      if (done) return;
      done = true;
      if (apply) this.layers.rename(pageIdx, layer, input.value);
      this.renderLayers();
    };
    input.addEventListener("keydown", (e) => { e.stopPropagation(); if (e.key === "Enter") finish(true); else if (e.key === "Escape") finish(false); });
    input.addEventListener("blur", () => finish(true));
    nameEl.replaceWith(input);
    input.focus();
    input.select();
  }
}
