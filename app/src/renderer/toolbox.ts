/**
 * The tool box: a floating panel of tools that opens where the pointer is, from a key or a pen button, to change tool quickly.
 * Its contents are the user's. It is made of zones (rows) of buttons and separators. In edit mode buttons are dragged in from a
 * catalogue by category (or found by name), or straight from the toolbar of the window; around, to reorder and to move between
 * zones; and out of the panel to remove them. Zones are added and removed, and separators put between buttons.
 */

export interface ToolboxSettings {
  enabled: boolean;
  /** The zones, top to bottom; each a list of item ids in order. `|` is a separator. */
  rows: string[][];
  closeOnPick: boolean;
}

export interface ToolboxItem {
  id: string;
  /** Name of the icon (see icons.ts), or empty for a colour swatch. */
  icon: string;
  label: string;
  group: ToolboxGroup;
  /** Selector of the toolbar button this item stands for, so it can be dragged from the toolbar. */
  bar?: string;
}

export const GROUPS = ["Herramientas", "Edición", "Archivo", "Navegación", "Selección", "Colores", "Separadores"] as const;
export type ToolboxGroup = (typeof GROUPS)[number];

export const SEPARATOR = "|";

const tool = (id: string, icon: string, label: string): ToolboxItem => ({ id: `tool.${id}`, icon, label, group: "Herramientas", bar: `button.tool[data-tool=${id}]` });
const bar = (id: string, icon: string, label: string, group: ToolboxGroup): ToolboxItem => ({ id: `bar:${id}`, icon, label, group, bar: `#${id}` });
/** An item that does its work through a command of the application, and also stands for a toolbar button. */
const cmd = (id: string, icon: string, label: string, group: ToolboxGroup, button: string): ToolboxItem => ({ id, icon, label, group, bar: button });

/** Everything that can be put in the tool box, except the colours (they depend on the palette) and the separator. */
export const TOOLBOX_CATALOG: ToolboxItem[] = [
  tool("pen", "pen", "Lápiz"), tool("highlighter", "highlighter", "Resaltador"), tool("eraser", "eraser", "Goma (trazo entero)"), tool("eraserPart", "eraserPart", "Goma (parcial)"),
  tool("select", "select", "Seleccionar"), tool("lasso", "lasso", "Lazo"), tool("vspace", "vspace", "Espacio vertical"), tool("hand", "hand", "Mano"),
  tool("shape", "shape", "Formas"), tool("figure", "figure", "Figuras"), tool("numline", "numline", "Recta real"), tool("venn", "venn", "Diagrama de conjuntos"), tool("text", "text", "Texto"), tool("latex", "latex", "LaTeX"),
  tool("laser", "laser", "Puntero láser"), tool("fill", "fill", "Relleno"), tool("plane", "plane", "Plano cartesiano"), tool("space3", "space3", "Espacio 3D"),
  bar("recog", "recog", "Formas automáticas (activar o quitar)", "Herramientas"),
  bar("image", "image", "Insertar imagen", "Herramientas"),
  cmd("glow.toggle", "glow", "Brillo de los trazos", "Herramientas", "#btnGlow"),
  cmd("pen.settings", "tablet", "Lápiz y tableta", "Herramientas", "#btnTablet"),
  cmd("edit.undo", "undo", "Deshacer", "Edición", "#undo"), cmd("edit.redo", "redo", "Rehacer", "Edición", "#redo"),
  bar("cut", "cut", "Cortar", "Edición"), bar("copy", "copy", "Copiar", "Edición"), bar("paste", "paste", "Pegar", "Edición"), bar("dup", "dup", "Duplicar", "Edición"),
  bar("open", "open", "Abrir", "Archivo"), bar("save", "save", "Guardar", "Archivo"),
  bar("pgFirst", "first", "Primera página", "Navegación"), bar("pgPrev", "prev", "Página anterior", "Navegación"), bar("pgNext", "next", "Página siguiente", "Navegación"), bar("pgLast", "last", "Última página", "Navegación"),
  bar("histBack", "histBack", "Volver en el historial", "Navegación"), bar("histFwd", "histFwd", "Avanzar en el historial", "Navegación"),
  bar("pgNew", "pageAdd", "Nueva página después de la actual", "Navegación"), bar("pgBefore", "pageBefore", "Insertar página antes", "Navegación"), bar("pgAppend", "pageEnd", "Añadir página al final", "Navegación"),
  bar("pgDelete", "pageDel", "Eliminar la página", "Navegación"), bar("pgUp", "up", "Mover la página hacia arriba", "Navegación"), bar("pgDown", "down", "Mover la página hacia abajo", "Navegación"),
  cmd("view.zoomIn", "zoomIn", "Acercar", "Navegación", "#zoomIn"), cmd("view.zoomOut", "zoomOut", "Alejar", "Navegación", "#zoomOut"), bar("fit", "fit", "Ajustar al ancho", "Navegación"),
  bar("front", "front", "Traer al frente", "Selección"), bar("back", "back", "Enviar al fondo", "Selección"),
  bar("rotL", "rotL", "Rotar 90° a la izquierda", "Selección"), bar("rotR", "rotR", "Rotar 90° a la derecha", "Selección"),
  bar("flipH", "flipH", "Espejar horizontalmente", "Selección"), bar("flipV", "flipV", "Espejar verticalmente", "Selección"),
  { id: SEPARATOR, icon: "", label: "Separador (una línea entre botones)", group: "Separadores" },
];
const CATALOG_IDS = new Set(TOOLBOX_CATALOG.map((i) => i.id));
const COLOR_ID = /^color:#[0-9a-f]{6}$/i;

export const isItemId = (id: unknown): id is string => typeof id === "string" && (CATALOG_IDS.has(id) || COLOR_ID.test(id));

/** The catalogue with the colours of the palette in use as the "Colores" group. */
export function catalogWith(colors: string[]): ToolboxItem[] {
  return [...TOOLBOX_CATALOG, ...colors.map((c): ToolboxItem => ({ id: `color:${c.toLowerCase()}`, icon: "", label: `Color ${c.toLowerCase()}`, group: "Colores", bar: `#swatches .sw[data-color="${c}"]` }))];
}

export const DEFAULT_TOOLBOX: ToolboxSettings = {
  enabled: true,
  rows: [
    ["tool.pen", "tool.highlighter", "tool.eraser", "tool.eraserPart", SEPARATOR, "tool.select", "tool.lasso", "tool.hand"],
    ["tool.text", "tool.latex", "tool.shape", "tool.fill"],
    ["edit.undo", "edit.redo"],
  ],
  closeOnPick: true,
};

const MAX_ROWS = 6, MAX_PER_ROW = 20;

/** Whatever was stored, as valid settings: unknown or repeated items are dropped (a separator may repeat). A list of items from before the zones is one zone. */
export function sanitizeToolbox(raw: unknown): ToolboxSettings {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const seen = new Set<string>();
  const clean = (list: unknown): string[] => (Array.isArray(list) ? (list as unknown[]).filter((v): v is string => isItemId(v) && (v === SEPARATOR || !seen.has(v)) && (v === SEPARATOR || !!seen.add(v))).slice(0, MAX_PER_ROW) : []);
  let rows: string[][] | null = null;
  if (Array.isArray(o.rows)) rows = (o.rows as unknown[]).slice(0, MAX_ROWS).map(clean);
  else if (Array.isArray(o.items)) rows = [clean(o.items)];
  if (rows && !rows.length) rows = [[]];
  return {
    enabled: o.enabled !== false,
    rows: rows ?? DEFAULT_TOOLBOX.rows.map((r) => r.slice()),
    closeOnPick: o.closeOnPick !== false,
  };
}

export interface ToolboxHost {
  settings(): ToolboxSettings;
  save(s: ToolboxSettings): void;
  run(id: string): void;
  /** Whether the item stands for what is active now (the current tool, the current colour), for highlighting. */
  isActive(id: string): boolean;
  /** The colours of the palette in use, as `#rrggbb`. */
  colors(): string[];
  icons(root: ParentNode): void;
}

const el = <K extends keyof HTMLElementTagNameMap>(tag: K, cls?: string, text?: string): HTMLElementTagNameMap[K] => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};
const norm = (v: string) => v.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();

interface DropData { id: string; row?: number; index?: number }

export class Toolbox {
  private root = document.getElementById("toolbox")!;
  private editing = false;
  private s!: ToolboxSettings;
  private at = { x: 200, y: 200 };
  private group: ToolboxGroup = "Herramientas";
  private query = "";
  private barWired = new WeakSet<Element>();

  constructor(private host: ToolboxHost) {
    this.root.addEventListener("keydown", (e) => { if (e.key === "Escape") { e.stopPropagation(); this.close(); } });
    document.addEventListener("pointerdown", (e) => { if (this.isOpen && !this.editing && !this.root.contains(e.target as Node)) this.close(); }, true);
    window.addEventListener("blur", () => { if (!this.editing) this.close(); });
  }

  get isOpen(): boolean { return !this.root.hidden; }
  get isEditing(): boolean { return this.editing; }

  /** Show the tool box with its centre on (x, y), in window pixels. Does nothing when it is switched off. */
  open(x: number, y: number): boolean {
    this.s = this.host.settings();
    if (!this.s.enabled) return false;
    this.at = { x, y };
    this.setEditing(false);
    this.render();
    this.root.hidden = false;
    this.place();
    return true;
  }

  close(): void { this.root.hidden = true; this.setEditing(false); }

  toggle(x: number, y: number): void { if (this.isOpen) this.close(); else this.open(x, y); }

  private setEditing(on: boolean): void {
    this.editing = on;
    this.wireBar(on);
  }

  /** In edit mode the toolbar's buttons can be dragged into the panel. */
  private wireBar(on: boolean): void {
    for (const item of catalogWith(this.host.colors())) {
      if (!item.bar) continue;
      for (const b of document.querySelectorAll<HTMLElement>(item.bar)) {
        b.draggable = on;
        if (!this.barWired.has(b)) {
          this.barWired.add(b);
          b.addEventListener("dragstart", (e) => {
            const id = (b as HTMLElement & { _tbxId?: string })._tbxId;
            if (!id || !this.editing) return;
            e.dataTransfer?.setData("text/plain", JSON.stringify({ id } as DropData));
            if (e.dataTransfer) e.dataTransfer.effectAllowed = "copyMove";
          });
        }
        (b as HTMLElement & { _tbxId?: string })._tbxId = item.id;
      }
    }
  }

  private place(): void {
    const r = this.root.getBoundingClientRect();
    const left = Math.min(Math.max(4, this.at.x - r.width / 2), Math.max(4, window.innerWidth - r.width - 4));
    const top = Math.min(Math.max(4, this.at.y - 24), Math.max(4, window.innerHeight - r.height - 4));
    this.root.style.left = `${left}px`;
    this.root.style.top = `${top}px`;
  }

  private commit(): void {
    this.host.save(this.s);
    this.render();
    const r = this.root.getBoundingClientRect(); // growing or shrinking keeps the top left where the user put it, inside the window
    this.root.style.left = `${Math.min(Math.max(4, r.left), Math.max(4, window.innerWidth - r.width - 4))}px`;
    this.root.style.top = `${Math.min(Math.max(4, r.top), Math.max(4, window.innerHeight - r.height - 4))}px`;
  }

  private itemById(id: string): ToolboxItem | undefined {
    return catalogWith(this.host.colors()).find((i) => i.id === id) ?? (COLOR_ID.test(id) ? { id, icon: "", label: `Color ${id.slice(6)}`, group: "Colores" } : undefined);
  }

  private buttonFor(item: ToolboxItem, row: number | null, index: number): HTMLElement {
    if (item.id === SEPARATOR) {
      const s = el("div", "tbxSep");
      s.title = item.label;
      if (row !== null) { s.dataset.index = String(index); this.makeDraggable(s, { id: item.id, row, index }); }
      return s;
    }
    const b = el("button", "tbxItem");
    b.type = "button";
    b.title = item.label;
    b.dataset.id = item.id;
    if (item.icon) b.dataset.icon = item.icon;
    else { b.classList.add("swatch"); const c = item.id.slice(6); b.style.setProperty("--sw", c); }
    if (row !== null) b.dataset.index = String(index);
    if (row !== null && !this.editing && this.host.isActive(item.id)) b.classList.add("active");
    if (this.editing) this.makeDraggable(b, { id: item.id, ...(row !== null ? { row, index } : {}) });
    else if (row !== null) b.addEventListener("click", () => { this.host.run(item.id); if (this.s.closeOnPick) this.close(); else this.render(); });
    return b;
  }

  private makeDraggable(node: HTMLElement, data: DropData): void {
    node.draggable = true;
    node.addEventListener("dragstart", (e) => { e.dataTransfer?.setData("text/plain", JSON.stringify(data)); if (e.dataTransfer) e.dataTransfer.effectAllowed = "copyMove"; node.classList.add("dragging"); });
    node.addEventListener("dragend", (e) => {
      node.classList.remove("dragging");
      if (data.row !== undefined && e.dataTransfer?.dropEffect === "none") this.removeAt(data.row, data.index!); // taken out of the panel and let go elsewhere
    });
  }

  private removeAt(row: number, index: number): void {
    const rows = this.s.rows.map((r) => r.slice());
    if (rows[row]?.[index] === undefined) return;
    rows[row]!.splice(index, 1);
    this.s = { ...this.s, rows };
    this.commit();
  }

  /** Put an item in a zone, before `index` (or last): from another place of the panel it moves, from outside it is added (a repeated button moves too). */
  private drop(d: DropData, row: number, index: number | null): void {
    const rows = this.s.rows.map((r) => r.slice());
    let at = index ?? rows[row]!.length;
    const take = (r: number, i: number) => { rows[r]!.splice(i, 1); if (r === row && i < at) at--; };
    if (d.row !== undefined && d.index !== undefined && rows[d.row]?.[d.index] === d.id) take(d.row, d.index);
    else if (d.id !== SEPARATOR) rows.forEach((r, ri) => { const i = r.indexOf(d.id); if (i >= 0) take(ri, i); });
    if (rows[row]!.length >= MAX_PER_ROW) return;
    rows[row]!.splice(Math.max(0, Math.min(at, rows[row]!.length)), 0, d.id);
    this.s = { ...this.s, rows };
    this.commit();
  }

  private parseDrop(e: DragEvent): DropData | null {
    try {
      const d = JSON.parse(e.dataTransfer?.getData("text/plain") ?? "") as { id?: unknown; row?: unknown; index?: unknown };
      if (!isItemId(d.id)) return null;
      return typeof d.row === "number" && typeof d.index === "number" ? { id: d.id, row: d.row, index: d.index } : { id: d.id };
    } catch { return null; }
  }

  private render(): void {
    this.root.replaceChildren();
    this.root.classList.toggle("editing", this.editing);
    const head = el("div", "tbxHead");
    const title = el("b", undefined, this.editing ? "Editar el cuadro" : "Herramientas");
    title.title = "Arrastra para mover el cuadro";
    const edit = el("button", "tbxSmall" + (this.editing ? " on" : ""), this.editing ? "Listo" : "Editar");
    edit.type = "button";
    edit.title = "Arrastra botones desde el catálogo o desde la barra de herramientas, ordénalos y pásalos de una zona a otra; sácalos del cuadro para quitarlos";
    edit.addEventListener("click", () => { this.setEditing(!this.editing); this.render(); this.place(); });
    const close = el("button", "tbxSmall", "✕");
    close.type = "button";
    close.title = "Cerrar (Esc)";
    close.addEventListener("click", () => this.close());
    head.append(title, edit, close);
    this.moveByHead(head);
    this.root.append(head);

    const zones = el("div", "tbxZones");
    let shown = 0;
    this.s.rows.forEach((ids, ri) => {
      const items = ids.map((id) => this.itemById(id)).map((it, i) => ({ it, i })).filter((x): x is { it: ToolboxItem; i: number } => !!x.it);
      if (!items.length && !this.editing) return;
      shown += items.length;
      const rowEl = el("div", "tbxRow");
      rowEl.dataset.row = String(ri);
      for (const { it, i } of items) rowEl.append(this.buttonFor(it, ri, i));
      if (!items.length) rowEl.append(el("span", "tbxEmpty", "Arrastra aquí los botones"));
      if (this.editing) {
        rowEl.addEventListener("dragover", (e) => { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = "move"; });
        rowEl.addEventListener("drop", (e) => {
          e.preventDefault();
          e.stopPropagation();
          const d = this.parseDrop(e);
          if (!d) return;
          const over = (e.target as HTMLElement).closest<HTMLElement>("[data-index]");
          this.drop(d, ri, over && rowEl.contains(over) ? Number(over.dataset.index) : null);
        });
        const del = el("button", "tbxZoneDel", "×");
        del.type = "button";
        del.title = "Quitar esta zona y sus botones";
        del.addEventListener("click", () => { const rows = this.s.rows.filter((_, k) => k !== ri); this.s = { ...this.s, rows: rows.length ? rows : [[]] }; this.commit(); });
        rowEl.append(del);
      }
      zones.append(rowEl);
    });
    if (!shown && !this.editing) zones.append(el("p", "tbxEmpty", "Vacío: pulsa Editar"));
    this.root.append(zones);

    if (!this.editing) { this.host.icons(this.root); return; }

    const add = el("button", "tbxSmall", "+ Zona");
    add.type = "button";
    add.title = "Añadir una zona (una fila) más";
    add.disabled = this.s.rows.length >= MAX_ROWS;
    add.addEventListener("click", () => { this.s = { ...this.s, rows: [...this.s.rows, []] }; this.commit(); });
    this.root.append(add);

    // the catalogue: by category, or found by name
    const find = el("div", "tbxFind");
    const search = el("input");
    search.type = "search";
    search.placeholder = "Buscar un botón…";
    search.value = this.query;
    search.addEventListener("input", () => { this.query = search.value; this.renderPool(pool, chips); });
    const chips = el("div", "tbxChips");
    find.append(search, chips);
    const pool = el("div", "tbxPool");
    pool.addEventListener("dragover", (e) => { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = "move"; });
    pool.addEventListener("drop", (e) => { e.preventDefault(); e.stopPropagation(); const d = this.parseDrop(e); if (d && d.row !== undefined) this.removeAt(d.row, d.index!); });
    this.root.append(el("div", "tbxLabel", "Catálogo"), find, pool);
    this.renderPool(pool, chips);

    const opts = el("div", "tbxOpts");
    const close1 = el("label");
    const cb = el("input"); cb.type = "checkbox"; cb.checked = this.s.closeOnPick;
    cb.addEventListener("change", () => { this.s = { ...this.s, closeOnPick: cb.checked }; this.host.save(this.s); });
    close1.append(cb, document.createTextNode(" Cerrar al elegir"));
    const reset = el("button", "tbxSmall", "Restaurar"); reset.type = "button";
    reset.addEventListener("click", () => { this.s = { ...this.s, rows: DEFAULT_TOOLBOX.rows.map((r) => r.slice()) }; this.commit(); });
    const off = el("button", "tbxSmall", "Desactivar el cuadro"); off.type = "button";
    off.title = "Se vuelve a activar desde Herramientas ▸ Activar o desactivar el cuadro de herramientas";
    off.addEventListener("click", () => { this.s = { ...this.s, enabled: false }; this.host.save(this.s); this.close(); });
    opts.append(close1, reset, off);
    this.root.append(opts);
    this.host.icons(this.root);
  }

  private renderPool(pool: HTMLElement, chips: HTMLElement): void {
    const all = catalogWith(this.host.colors());
    const q = norm(this.query.trim());
    chips.replaceChildren();
    for (const g of GROUPS) {
      const c = el("button", "tbxChip" + (!q && g === this.group ? " on" : ""), g);
      c.type = "button";
      c.addEventListener("click", () => { this.group = g; this.query = ""; this.render(); this.place(); });
      chips.append(c);
    }
    pool.replaceChildren();
    const list = q ? all.filter((i) => norm(i.label + " " + i.group).includes(q)) : all.filter((i) => i.group === this.group);
    const used = new Set(this.s.rows.flat());
    for (const item of list) {
      const b = this.buttonFor(item, null, 0);
      if (item.id !== SEPARATOR && used.has(item.id)) b.classList.add("used");
      pool.append(b);
    }
    if (!list.length) pool.append(el("p", "tbxEmpty", q ? "Nada se llama así" : "Sin botones"));
    this.host.icons(pool);
  }

  /** The panel is moved by its title bar. */
  private moveByHead(head: HTMLElement): void {
    head.addEventListener("pointerdown", (ev) => {
      if (ev.button !== 0 || (ev.target as HTMLElement).closest("button")) return;
      ev.preventDefault();
      const r = this.root.getBoundingClientRect();
      const dx = ev.clientX - r.left, dy = ev.clientY - r.top;
      try { head.setPointerCapture(ev.pointerId); } catch { /* synthetic events have no active pointer */ }
      const move = (m: PointerEvent) => {
        this.root.style.left = `${Math.min(Math.max(0, m.clientX - dx), window.innerWidth - 40)}px`;
        this.root.style.top = `${Math.min(Math.max(0, m.clientY - dy), window.innerHeight - 30)}px`;
      };
      const up = () => { head.removeEventListener("pointermove", move); head.removeEventListener("pointerup", up); head.removeEventListener("pointercancel", up); };
      head.addEventListener("pointermove", move);
      head.addEventListener("pointerup", up);
      head.addEventListener("pointercancel", up);
    });
    head.style.cursor = "move";
  }
}
