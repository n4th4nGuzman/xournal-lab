import type { Editor } from "./editor";
import { elementBBox, type Rect } from "./geometry";
import type { Layer, PageElement } from "./model";

interface Hit { page: number; layer: Layer; el: PageElement }

export interface FindApi {
  /** Bring a box of a page into view and wait until the page is drawn. */
  reveal(page: number, box: Rect): Promise<void>;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const textOf = (el: PageElement): string | null => (el.kind === "text" ? el.text : el.kind === "teximage" ? el.latex : null);

/** Search the text boxes and LaTeX formulas of the document; each match is selected in turn. */
export class Find {
  private bar = $("findBar");
  private input = $<HTMLInputElement>("findQ");
  private count = $("findCount");
  private caseBox = $<HTMLInputElement>("findCase");
  private hits: Hit[] = [];
  private idx = -1;
  private timer = 0;

  constructor(private ed: Editor, private api: FindApi) {
    this.input.addEventListener("input", () => { window.clearTimeout(this.timer); this.timer = window.setTimeout(() => this.run(), 120); });
    this.input.addEventListener("keydown", (e) => {
      e.stopPropagation();
      if (e.key === "Enter") { e.preventDefault(); this.step(e.shiftKey ? -1 : 1); }
      else if (e.key === "Escape") { e.preventDefault(); this.close(); }
    });
    this.caseBox.addEventListener("change", () => this.run());
    $("findPrev").addEventListener("click", () => this.step(-1));
    $("findNext").addEventListener("click", () => this.step(1));
    $("findClose").addEventListener("click", () => this.close());
  }

  get isOpen(): boolean { return !this.bar.hidden; }
  get matches(): number { return this.hits.length; }
  get position(): number { return this.idx; }

  open(): void {
    this.bar.hidden = false;
    this.input.focus();
    this.input.select();
    if (this.input.value) this.run();
  }

  close(): void {
    this.bar.hidden = true;
    this.hits = [];
    this.idx = -1;
    this.count.textContent = "";
  }

  /** Collect every match in page order, then go to the first. */
  run(): void {
    const doc = this.ed.host.doc();
    const q = this.input.value;
    this.hits = [];
    this.idx = -1;
    if (!doc || !q) { this.count.textContent = ""; return; }
    const norm = (s: string) => (this.caseBox.checked ? s : s.toLowerCase());
    const needle = norm(q);
    doc.pages.forEach((p, page) => {
      for (const layer of p.layers) {
        if (layer.hidden) continue;
        for (const el of layer.elements) {
          const t = textOf(el);
          if (t !== null && norm(t).includes(needle)) this.hits.push({ page, layer, el });
        }
      }
    });
    if (!this.hits.length) { this.count.textContent = "Sin resultados"; return; }
    this.idx = 0;
    void this.show();
  }

  step(delta: 1 | -1): void {
    if (!this.hits.length) { this.run(); return; }
    this.idx = (this.idx + delta + this.hits.length) % this.hits.length;
    void this.show();
  }

  private async show(): Promise<void> {
    const h = this.hits[this.idx];
    if (!h) return;
    this.count.textContent = `${this.idx + 1} de ${this.hits.length}`;
    const box = elementBBox(h.el);
    if (!box) return;
    this.ed.setTool("select");
    await this.api.reveal(h.page, box);
    if (this.hits[this.idx] === h) this.ed.selection.selectOnly(h.page, h.layer, h.el);
  }
}
