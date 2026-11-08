/** The option panels of the real line and of set diagrams: expressions written with help of a row of symbols, and the settings of the drawing. */
import type { Editor } from "./editor";
import { formatSet } from "./intervals";
import { MAX_ROWS, defaultLine, lineSets, sanitizeLine, type LineSpec } from "./numberline";
import { defaultVenn, sanitizeVenn, shadedRegions, type VennSpec } from "./venn";

const el = <K extends keyof HTMLElementTagNameMap>(tag: K, cls?: string, text?: string): HTMLElementTagNameMap[K] => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};

const LINE_SYMBOLS = ["(", ")", "[", "]", "{", "}", ",", "∞", "−∞", "π", "∪", "∩", "∖", "Δ", "′", "ℝ", "∅"];
const VENN_SYMBOLS = ["(", ")", "∪", "∩", "∖", "Δ", "′", "U"];

type Kind = "line" | "venn";

export class MathPanels {
  private panels: Record<Kind, HTMLElement> = { line: document.getElementById("linePanel")!, venn: document.getElementById("vennPanel")! };
  private buttons: Record<Kind, HTMLElement> = { line: document.getElementById("btnNumline")!, venn: document.getElementById("btnVenn")! };
  private last: HTMLInputElement | null = null;
  /** After Enter the panel is built again: the cursor goes back to the field that was being written (its place among the text fields). */
  private refocus = -1;
  private refocusUntil = 0;

  constructor(private ed: Editor) {
    (["line", "venn"] as Kind[]).forEach((k) => {
      this.buttons[k].addEventListener("click", (e) => { e.stopPropagation(); if (!this.panels[k].hidden) this.close(); else this.open(k); });
    });
    document.addEventListener("pointerdown", (e) => {
      for (const k of ["line", "venn"] as Kind[]) if (!this.panels[k].hidden && !this.panels[k].contains(e.target as Node) && !this.buttons[k].contains(e.target as Node)) this.close();
    }, true);
    document.addEventListener("keydown", (e) => { if (e.key === "Escape" && (!this.panels.line.hidden || !this.panels.venn.hidden)) { this.close(); e.stopPropagation(); } }, true);
  }

  get isOpen(): boolean { return !this.panels.line.hidden || !this.panels.venn.hidden; }

  open(kind: Kind): void {
    this.close();
    this.render(kind);
    const r = this.buttons[kind].getBoundingClientRect();
    const p = this.panels[kind];
    p.hidden = false;
    p.style.left = `${Math.max(4, Math.min(r.left, window.innerWidth - p.offsetWidth - 8))}px`;
    p.style.top = `${Math.min(r.bottom + 4, Math.max(4, window.innerHeight - p.offsetHeight - 8))}px`;
  }

  close(): void { this.panels.line.hidden = true; this.panels.venn.hidden = true; }

  /** After the selection changed: show what is selected (or the defaults) in the open panel. */
  sync(): void {
    const sel = this.ed.selectedMath();
    for (const k of ["line", "venn"] as Kind[]) if (!this.panels[k].hidden && (!sel || sel.spec.kind === k)) this.render(k);
  }

  // ---------------------------------------------------------------------------------------------------- helpers
  private field(parent: HTMLElement, label: string, input: HTMLElement): void {
    const row = el("label", "mpRow");
    row.append(el("span", undefined, label), input);
    parent.append(row);
  }

  private text(value: string, placeholder = "", width = ""): HTMLInputElement {
    const i = el("input");
    i.type = "text";
    i.value = value;
    i.placeholder = placeholder;
    i.spellcheck = false;
    if (width) i.style.width = width;
    i.addEventListener("focus", () => { this.last = i; });
    return i;
  }

  private number(value: number): HTMLInputElement {
    const i = el("input");
    i.type = "number";
    i.step = "any";
    i.value = String(+value.toFixed(6));
    return i;
  }

  private check(parent: HTMLElement, label: string, on: boolean, change: () => void): HTMLInputElement {
    const l = el("label", "mpCheck");
    const c = el("input");
    c.type = "checkbox";
    c.checked = on;
    c.addEventListener("change", change);
    l.append(c, document.createTextNode(" " + label));
    parent.append(l);
    return c;
  }

  private symbols(parent: HTMLElement, list: string[]): void {
    const bar = el("div", "mpSymbols");
    for (const s of list) {
      const b = el("button", undefined, s);
      b.type = "button";
      b.title = `Insertar ${s}`;
      b.addEventListener("mousedown", (e) => e.preventDefault()); // the field keeps its focus and caret
      b.addEventListener("click", () => {
        const i = this.last;
        if (!i || !i.isConnected) return;
        const a = i.selectionStart ?? i.value.length, z = i.selectionEnd ?? a;
        i.setRangeText(s === "′" ? "'" : s, a, z, "end");
        i.dispatchEvent(new Event("input"));
        i.focus();
      });
      bar.append(b);
    }
    parent.append(bar);
  }

  /** Apply on Enter and when a field is left: one undo step per edit, not one per key. */
  private onCommit(i: HTMLElement, go: () => void): void {
    i.addEventListener("change", go);
    i.addEventListener("keydown", (e) => {
      if ((e as KeyboardEvent).key === "Enter") {
        e.preventDefault();
        this.refocus = [...(i.closest(".mathPanel") ?? document).querySelectorAll<HTMLInputElement>("input[type=text]")].indexOf(i as HTMLInputElement);
        this.refocusUntil = Date.now() + 600; // the panel may be built more than once while the change settles
        go();
      }
      e.stopPropagation();
    });
  }

  // ------------------------------------------------------------------------------------------------- real line
  private render(kind: Kind): void {
    const p = this.panels[kind];
    p.replaceChildren();
    if (kind === "line") this.renderLine(p); else this.renderVenn(p);
    if (this.refocus >= 0 && Date.now() < this.refocusUntil) {
      const f = p.querySelectorAll<HTMLInputElement>("input[type=text]")[this.refocus];
      if (f) { f.focus(); f.setSelectionRange(f.value.length, f.value.length); this.last = f; }
    }
  }

  private renderLine(p: HTMLElement): void {
    const sel = this.ed.selectedMath();
    const base: LineSpec = sel && sel.spec.kind === "line" ? sel.spec : this.ed.lineSpec;
    p.append(el("b", undefined, "Recta real"));
    p.append(el("p", "mpNote", "Escribe conjuntos de números y se marcan sobre la recta: (−2, 3] ∪ {5} · [1, ∞) · R ∖ {2}. Vale U, n, inf, oo… Intro aplica."));
    const rows = el("div", "mpRows");
    const rowEls: Array<{ expr: HTMLInputElement; label: HTMLInputElement; out: HTMLElement }> = [];
    const lines = base.rows.slice();
    const showResults = () => {
      const spec = read();
      lineSets(spec).forEach((r, i) => {
        const o = rowEls[i]!.out;
        o.className = "mpOut" + (r.error ? " err" : "");
        o.textContent = r.error ?? (r.set ? `= ${formatSet(r.set)}` : "");
      });
    };
    lines.forEach((row, i) => {
      const box = el("div", "mpLineRow");
      const expr = this.text(row.expr, i === 0 ? "(-2, 3] ∪ {5}" : "[0, ∞)");
      expr.className = "mpExpr";
      const label = this.text(row.label, "rótulo", "74px");
      const out = el("div", "mpOut");
      const top = el("div", "mpRowTop");
      top.append(el("span", "mpIdx", `${i + 1}`), expr, label);
      if (i > 0) { const del = el("button", "mpDel", "×"); del.type = "button"; del.title = "Quitar esta fila"; del.addEventListener("click", () => { lines.splice(i, 1); apply({ rows: collect().filter((_, k) => k !== i) }); }); top.append(del); }
      box.append(top, out);
      rows.append(box);
      rowEls.push({ expr, label, out });
      expr.addEventListener("input", showResults);
      this.onCommit(expr, () => apply({}));
      this.onCommit(label, () => apply({}));
    });
    p.append(rows);
    const add = el("button", "mpBtn", "+ Fila");
    add.type = "button";
    add.title = "Otra recta debajo, para comparar (intersecciones, uniones…)";
    add.disabled = lines.length >= MAX_ROWS;
    add.addEventListener("click", () => apply({ rows: [...collect(), { expr: "", label: "" }] }));
    p.append(add);
    this.symbols(p, LINE_SYMBOLS);
    this.last = rowEls[0]!.expr;

    const axis = el("div", "mpAxis");
    const min = this.number(base.min), max = this.number(base.max), step = this.number(base.step), every = this.number(base.every);
    this.field(axis, "Desde", min); this.field(axis, "Hasta", max); this.field(axis, "Paso", step); this.field(axis, "Número cada", every);
    for (const i of [min, max, step, every]) this.onCommit(i, () => apply({}));
    p.append(axis);
    const marks = el("select");
    marks.append(new Option("Círculos (abierto ○ / cerrado ●)", "circles"), new Option("Corchetes y paréntesis", "brackets"));
    marks.value = base.marks;
    marks.addEventListener("change", () => apply({}));
    this.field(p, "Extremos", marks);
    const checks = el("div", "mpChecks");
    const ticks = this.check(checks, "Marcas", base.ticks, () => apply({}));
    const nums = this.check(checks, "Números", base.numbers, () => apply({}));
    const arrows = this.check(checks, "Flechas", base.arrows, () => apply({}));
    p.append(checks);
    const style = el("button", "mpBtn", "Estilo del lápiz");
    style.type = "button";
    style.title = "Dar a la recta seleccionada el color, grosor y brillo del lápiz";
    style.addEventListener("click", () => apply({}, true));
    p.append(style);

    const collect = () => rowEls.map((r) => ({ expr: r.expr.value, label: r.label.value }));
    const read = (): LineSpec => sanitizeLine({ ...base, min: Number(min.value), max: Number(max.value), step: Number(step.value), every: Number(every.value), ticks: ticks.checked, numbers: nums.checked, arrows: arrows.checked, marks: marks.value, rows: collect() });
    const apply = (over: Partial<LineSpec>, restyle = false) => {
      const spec = { ...read(), ...over } as LineSpec;
      this.ed.lineSpec = { ...spec, pts: defaultLine().pts };
      if (this.ed.selectedMath()) this.ed.updateMath(spec, restyle);
      else this.render("line");
    };
    showResults();
  }

  // ------------------------------------------------------------------------------------------------- diagrams
  private renderVenn(p: HTMLElement): void {
    const sel = this.ed.selectedMath();
    const base: VennSpec = sel && sel.spec.kind === "venn" ? sel.spec : this.ed.vennSpec;
    p.append(el("b", undefined, "Diagrama de conjuntos"));
    p.append(el("p", "mpNote", "Escribe qué región sombrear con los nombres de los conjuntos: A ∩ B′ · (A ∪ B)′ · A ∖ B · A Δ B. ′ es el complemento (también A^c o ¬A)."));
    const k = el("select");
    k.append(new Option("2 conjuntos", "2"), new Option("3 conjuntos", "3"));
    k.value = String(base.k);
    this.field(p, "Conjuntos", k);
    const names = el("div", "mpNames");
    const nameIn = [0, 1, 2].slice(0, base.k).map((i) => { const n = this.text(base.names[i]!, "", "46px"); names.append(n); this.onCommit(n, () => apply({})); return n; });
    this.field(p, "Nombres", names);
    const expr = this.text(base.expr, "A ∩ B′");
    expr.className = "mpExpr";
    this.field(p, "Sombrear", expr);
    const out = el("div", "mpOut");
    p.append(out);
    const quick = el("div", "mpSymbols");
    const nm = base.names;
    const presets = base.k === 2 ? [`${nm[0]} ∪ ${nm[1]}`, `${nm[0]} ∩ ${nm[1]}`, `${nm[0]} ∖ ${nm[1]}`, `${nm[1]} ∖ ${nm[0]}`, `${nm[0]}′`, `${nm[0]} Δ ${nm[1]}`, `(${nm[0]} ∪ ${nm[1]})′`, `(${nm[0]} ∩ ${nm[1]})′`]
      : [`${nm[0]} ∪ ${nm[1]} ∪ ${nm[2]}`, `${nm[0]} ∩ ${nm[1]} ∩ ${nm[2]}`, `${nm[0]} ∩ ${nm[1]}`, `(${nm[0]} ∩ ${nm[1]}) ∪ ${nm[2]}′`, `${nm[0]} ∖ (${nm[1]} ∪ ${nm[2]})`, `(${nm[0]} ∪ ${nm[1]} ∪ ${nm[2]})′`];
    for (const s of presets) {
      const b = el("button", "mpPreset", s);
      b.type = "button";
      b.addEventListener("click", () => { expr.value = s; apply({}); });
      quick.append(b);
    }
    p.append(quick);
    this.symbols(p, VENN_SYMBOLS);
    this.last = expr;
    const lists: HTMLInputElement[] = [];
    for (let i = 0; i <= base.k; i++) {
      const t = this.text(base.lists[i] ?? "", i < base.k ? "1, 2, 3" : "5, 8");
      lists.push(t);
      this.field(p, i < base.k ? `Elementos de ${base.names[i]}` : "Fuera de todos", t);
      this.onCommit(t, () => apply({}));
    }
    const checks = el("div", "mpChecks");
    const uni = this.check(checks, "Universo U", base.universe, () => apply({}));
    const dense = this.check(checks, "Trama densa", base.hatch === "dense", () => apply({}));
    p.append(checks);
    const style = el("button", "mpBtn", "Estilo del lápiz");
    style.type = "button";
    style.addEventListener("click", () => apply({}, true));
    p.append(style);

    const read = (): VennSpec => sanitizeVenn({ ...base, k: Number(k.value), names: [...nameIn.map((n) => n.value), ...base.names.slice(nameIn.length)], expr: expr.value, universe: uni.checked, hatch: dense.checked ? "dense" : "light", lists: lists.map((l) => l.value) });
    const show = () => { const s = shadedRegions(read()); out.className = "mpOut" + (s.error ? " err" : ""); out.textContent = s.error ?? ""; };
    expr.addEventListener("input", show);
    this.onCommit(expr, () => apply({}));
    k.addEventListener("change", () => {
      const was = read();
      const kk = Number(k.value) === 3 ? 3 : 2;
      const [x0, y0, x1, y1] = was.pts as [number, number, number, number];
      const shaped = defaultVenn(kk, [Math.min(x0, x1), Math.min(y0, y1)], [Math.max(x0, x1), Math.max(y0, y1)]);
      apply({ k: kk, pts: shaped.pts, lists: [...was.lists.slice(0, Math.min(was.k, kk)), ...Array(kk + 1).fill("")].slice(0, kk + 1) });
    });
    const apply = (over: Partial<VennSpec>, restyle = false) => {
      const spec = sanitizeVenn({ ...read(), ...over });
      this.ed.vennSpec = { ...spec, pts: defaultVenn(spec.k).pts };
      if (this.ed.selectedMath()) this.ed.updateMath(spec, restyle);
      else this.render("venn");
    };
    show();
  }
}
