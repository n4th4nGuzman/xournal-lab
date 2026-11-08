/**
 * The window's own menu bar. Windows draws the native menu bar white whatever the theme, so the window has no native
 * one: the main process describes its menu (the same one the tests click) and this draws it in the page, where it
 * follows the theme. Clicking an item asks the main process to run the original item.
 */
export interface MenuNode {
  label: string;
  accel?: string;
  type: "normal" | "separator" | "checkbox";
  enabled: boolean;
  checked?: boolean;
  tip?: string;
  sub?: MenuNode[];
}

export interface MenuBridge {
  get(): Promise<MenuNode[]>;
  click(path: number[]): void;
  onChanged(cb: () => void): void;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** `CmdOrCtrl+Shift+N` as shown to the user. */
export function formatAccel(a: string | undefined): string {
  return (a ?? "").replace(/CmdOrCtrl|CommandOrControl|Control|Command/g, "Ctrl").replace(/\+Plus$/, "++");
}

/** The label without its `&`, and the mnemonic letter (the one after the `&`). */
export function splitLabel(label: string): { text: string; mnemonic: string | null; at: number } {
  const i = label.indexOf("&");
  if (i < 0 || i === label.length - 1) return { text: label, mnemonic: null, at: -1 };
  return { text: label.slice(0, i) + label.slice(i + 1), mnemonic: label[i + 1]!.toLowerCase(), at: i };
}

function labelEl(label: string, underline: boolean): HTMLSpanElement {
  const { text, at } = splitLabel(label);
  const span = document.createElement("span");
  span.className = "mbLabel";
  if (underline && at >= 0) {
    span.append(text.slice(0, at));
    const u = document.createElement("u");
    u.textContent = text[at]!;
    span.append(u, text.slice(at + 1));
  } else span.textContent = text;
  return span;
}

export class MenuBar {
  private nav = $("menubar");
  private model: MenuNode[] = [];
  /** Index of the open top-level menu, or -1. */
  private open = -1;
  private panels: HTMLElement[] = [];

  constructor(private bridge: MenuBridge) {
    bridge.onChanged(() => void this.reload());
    document.addEventListener("pointerdown", (e) => { if (this.open >= 0 && !(e.target as HTMLElement).closest("#menubar, .mbPanel")) this.close(); }, true);
    window.addEventListener("blur", () => this.close());
    document.addEventListener("keydown", (e) => this.key(e), true);
    void this.reload();
  }

  get isOpen(): boolean { return this.open >= 0; }
  get items(): MenuNode[] { return this.model; }

  async reload(): Promise<void> {
    this.close();
    this.model = await this.bridge.get().catch(() => []);
    this.nav.replaceChildren();
    this.model.forEach((m, i) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "mbTop";
      b.dataset.index = String(i);
      b.append(labelEl(m.label, false));
      b.addEventListener("click", () => { if (this.open === i) this.close(); else this.openMenu(i); });
      b.addEventListener("pointerenter", () => { if (this.open >= 0 && this.open !== i) this.openMenu(i); });
      this.nav.append(b);
    });
  }

  close(): void {
    this.panels.forEach((p) => p.remove());
    this.panels = [];
    this.nav.querySelectorAll(".mbTop.on").forEach((b) => b.classList.remove("on"));
    this.open = -1;
  }

  /** Open top-level menu `i`, with its first item highlighted when `focusFirst`. */
  openMenu(i: number, focusFirst = false): void {
    this.close();
    const top = this.nav.children[i] as HTMLElement | undefined;
    const m = this.model[i];
    if (!top || !m?.sub) return;
    this.open = i;
    top.classList.add("on");
    const r = top.getBoundingClientRect();
    const panel = this.panel(m.sub, [i], r.left, r.bottom);
    if (focusFirst) this.highlight(panel, 0, 1);
  }

  private panel(items: MenuNode[], path: number[], x: number, y: number): HTMLElement {
    const p = document.createElement("div");
    p.className = "mbPanel";
    p.dataset.depth = String(path.length);
    p.setAttribute("role", "menu");
    items.forEach((it, idx) => {
      if (it.type === "separator") { const s = document.createElement("div"); s.className = "mbSep"; p.append(s); return; }
      const row = document.createElement("button");
      row.type = "button";
      row.className = "mbItem";
      row.disabled = !it.enabled;
      row.dataset.path = [...path, idx].join(".");
      if (it.tip) row.title = it.tip;
      const tick = document.createElement("span");
      tick.className = "mbTick";
      tick.textContent = it.type === "checkbox" && it.checked ? "✓" : "";
      row.append(tick, labelEl(it.label, false));
      if (it.sub) { const a = document.createElement("span"); a.className = "mbArrow"; a.textContent = "▸"; row.append(a); }
      else if (it.accel) { const k = document.createElement("kbd"); k.textContent = formatAccel(it.accel); row.append(k); }
      row.addEventListener("pointerenter", () => {
        this.closeDeeperThan(path.length);
        p.querySelectorAll(".mbItem.hot").forEach((e) => e.classList.remove("hot"));
        if (it.enabled) row.classList.add("hot");
        if (it.sub && it.enabled) { const r = row.getBoundingClientRect(); this.panel(it.sub, [...path, idx], r.right - 2, r.top - 4); }
      });
      row.addEventListener("click", () => {
        if (it.sub) { if (!this.panels.some((q) => Number(q.dataset.depth) > path.length)) { const r = row.getBoundingClientRect(); this.panel(it.sub, [...path, idx], r.right - 2, r.top - 4); } return; }
        this.close();
        this.bridge.click([...path, idx]);
      });
      p.append(row);
    });
    document.body.append(p);
    // keep inside the window
    const w = p.offsetWidth, h = p.offsetHeight;
    p.style.left = `${Math.max(2, Math.min(x, window.innerWidth - w - 2))}px`;
    p.style.top = `${Math.max(2, Math.min(y, window.innerHeight - h - 2))}px`;
    this.panels.push(p);
    return p;
  }

  private closeDeeperThan(depth: number): void {
    this.panels = this.panels.filter((p) => { if (Number(p.dataset.depth) > depth) { p.remove(); return false; } return true; });
  }

  private rows(panel: HTMLElement): HTMLButtonElement[] { return [...panel.querySelectorAll<HTMLButtonElement>(".mbItem:not(:disabled)")]; }

  private highlight(panel: HTMLElement, from: number, dir: 1 | -1): void {
    const rows = this.rows(panel);
    if (!rows.length) return;
    panel.querySelectorAll(".mbItem.hot").forEach((e) => e.classList.remove("hot"));
    const row = rows[((from % rows.length) + rows.length) % rows.length]!;
    row.classList.add("hot");
    row.scrollIntoView({ block: "nearest" });
    void dir;
  }

  private key(e: KeyboardEvent): void {
    // Alt+letter opens the menu with that mnemonic (Alt+Y: Ayuda).
    if (e.altKey && !e.ctrlKey && !e.metaKey && e.key.length === 1) {
      const i = this.model.findIndex((m) => splitLabel(m.label).mnemonic === e.key.toLowerCase());
      if (i >= 0) { e.preventDefault(); e.stopPropagation(); this.openMenu(i, true); return; }
    }
    if (this.open < 0) return;
    const top = this.panels[this.panels.length - 1];
    if (!top) return;
    const rows = this.rows(top);
    const cur = rows.findIndex((r) => r.classList.contains("hot"));
    const stop = () => { e.preventDefault(); e.stopPropagation(); };
    if (e.key === "Escape") { stop(); if (this.panels.length > 1) this.closeDeeperThan(this.panels.length - 1); else this.close(); }
    else if (e.key === "ArrowDown") { stop(); this.highlight(top, cur + 1, 1); }
    else if (e.key === "ArrowUp") { stop(); this.highlight(top, cur < 0 ? -1 : cur - 1, -1); }
    else if (e.key === "ArrowRight") {
      stop();
      const row = rows[cur];
      if (row && row.querySelector(".mbArrow")) { row.click(); const sub = this.panels[this.panels.length - 1]!; if (sub !== top) this.highlight(sub, 0, 1); }
      else this.openMenu((this.open + 1) % this.model.length, true);
    } else if (e.key === "ArrowLeft") {
      stop();
      if (this.panels.length > 1) this.closeDeeperThan(this.panels.length - 1);
      else this.openMenu((this.open - 1 + this.model.length) % this.model.length, true);
    } else if (e.key === "Enter" || e.key === " ") { if (rows[cur]) { stop(); rows[cur]!.click(); } }
    else if (!e.altKey && !e.ctrlKey && e.key.length === 1) { // first letter of an item
      const k = e.key.toLowerCase();
      const next = rows.findIndex((r, i) => i > cur && (r.querySelector(".mbLabel")?.textContent ?? "").toLowerCase().startsWith(k));
      const any = next >= 0 ? next : rows.findIndex((r) => (r.querySelector(".mbLabel")?.textContent ?? "").toLowerCase().startsWith(k));
      if (any >= 0) { stop(); this.highlight(top, any, 1); }
    }
  }
}
