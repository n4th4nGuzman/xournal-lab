/**
 * Dialogs that move and detach. Every dialog (a `.modal` holding a `.dlg`) can be dragged by its title bar, which also stops
 * it from darkening and blocking the page so the document stays visible and scrollable behind it, and can be sent to a window
 * of its own ("Desacoplar") that goes to another monitor. The dialog's code keeps running where it was: only its elements
 * move to the other window's document, so nothing in the dialogs has to know.
 */

interface Entry {
  modal: HTMLElement;
  dlg: HTMLElement;
  close: () => void;
  child: Window | null;
  /** Last place and size in this window, kept for the next time the dialog opens. */
  rect: { left: number; top: number; width: number; height: number } | null;
  dock: HTMLElement | null;
  /** The user has put it somewhere: that place is used next time; otherwise it opens out of the way. */
  moved: boolean;
  watcher: number;
  /** Brings the elements back to this window (set while detached). */
  back: (() => void) | null;
}

const entries: Entry[] = [];

/** True while a dialog is floating over the page or detached: the page must not react to clicks meant for reading. */
export function anyFloatingOpen(): boolean {
  return entries.some((e) => !e.modal.hidden && (e.modal.classList.contains("floating") || e.modal.classList.contains("detached")));
}

/** The window a dialog's elements are in now (its own when detached). */
export function windowOf(el: HTMLElement): Window { return el.ownerDocument.defaultView ?? window; }

const MIN_VISIBLE = 80;

/** Where the pointer last went down in the window, to open dialogs away from it. */
const lastClick = { x: 0, y: 0 };
window.addEventListener("pointerdown", (ev) => { lastClick.x = ev.clientX; lastClick.y = ev.clientY; }, true);

function clampToWindow(e: Entry, left: number, top: number): [number, number] {
  const w = e.dlg.offsetWidth, h = e.dlg.offsetHeight;
  return [Math.min(Math.max(left, MIN_VISIBLE - w), window.innerWidth - MIN_VISIBLE), Math.min(Math.max(top, 0), window.innerHeight - Math.min(h, 40))];
}

function place(e: Entry, left: number, top: number): void {
  const [x, y] = clampToWindow(e, left, top);
  Object.assign(e.dlg.style, { left: `${x}px`, top: `${y}px` });
}

function floatIn(e: Entry): void {
  if (e.modal.classList.contains("floating")) return;
  const r = e.dlg.getBoundingClientRect();
  e.modal.classList.add("floating");
  Object.assign(e.dlg.style, { width: `${r.width}px`, height: `${r.height}px`, maxHeight: "none" });
  place(e, r.left, r.top);
}

function remember(e: Entry): void {
  const r = e.dlg.getBoundingClientRect();
  if (e.modal.classList.contains("floating")) e.rect = { left: r.left, top: r.top, width: r.width, height: r.height };
}

/** Opening: back where the user left it, or else floating over the half of the window the last click was not in, so the page stays in sight. */
function showAgain(e: Entry): void {
  if (e.modal.classList.contains("detached")) return;
  if (e.moved && e.rect) {
    e.modal.classList.add("floating");
    Object.assign(e.dlg.style, { width: `${e.rect.width}px`, height: `${e.rect.height}px`, maxHeight: "none" });
    place(e, e.rect.left, e.rect.top);
    return;
  }
  const r = e.dlg.getBoundingClientRect();
  floatIn(e);
  const below = lastClick.y < window.innerHeight / 2; // the click was in the upper half: the dialog goes down
  place(e, (window.innerWidth - r.width) / 2, below ? window.innerHeight - r.height - 24 : 56);
  remember(e);
}

function copyLook(from: Document, to: Document): void {
  to.documentElement.dataset.theme = from.documentElement.dataset.theme ?? "classic";
  to.documentElement.dataset.fx = from.documentElement.dataset.fx ?? "full";
}

/** Move the dialog's elements to a window of their own. */
function detach(e: Entry): void {
  if (e.child && !e.child.closed) { e.child.focus(); return; }
  const r = e.dlg.getBoundingClientRect();
  const w = window.open("", `xl-${e.modal.id}`, `popup,width=${Math.round(Math.max(420, r.width + 24))},height=${Math.round(Math.max(300, r.height + 48))},left=${Math.round(window.screenX + r.left + 40)},top=${Math.round(window.screenY + r.top + 60)}`);
  if (!w) return;
  e.child = w;
  e.dock = e.modal.parentElement;
  remember(e);
  const d = w.document;
  d.title = e.dlg.getAttribute("aria-label") ?? "Xournal Lab";
  const base = d.createElement("base");
  base.href = location.href;
  d.head.append(base);
  for (const node of document.querySelectorAll("link[rel=stylesheet], style")) d.head.append(node.cloneNode(true));
  copyLook(document, d);
  d.body.style.margin = "0";
  e.modal.classList.remove("floating");
  e.modal.classList.add("detached");
  for (const p of ["left", "top", "width", "height", "maxHeight"] as const) e.dlg.style[p] = "";
  d.body.append(d.adoptNode(e.modal));
  e.modal.dispatchEvent(new CustomEvent("xl:moved", { bubbles: false }));
  const sync = new MutationObserver(() => copyLook(document, d));
  sync.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme", "data-fx"] });
  const back = () => {
    sync.disconnect();
    window.clearInterval(e.watcher);
    e.child = null;
    e.back = null;
    e.modal.classList.remove("detached");
    (e.dock ?? document.body).append(document.adoptNode(e.modal));
    e.modal.dispatchEvent(new CustomEvent("xl:moved", { bubbles: false }));
    if (!w.closed) w.close();
    if (!e.modal.hidden) showAgain(e);
  };
  e.back = back;
  // the window closed by its own X: the dialog is cancelled
  e.watcher = window.setInterval(() => { if (w.closed) { back(); e.close(); } }, 300);
  w.addEventListener("pagehide", () => { if (e.child === w) { back(); e.close(); } });
  setTimeout(() => w.focus(), 0);
}

function attach(e: Entry): void {
  e.back?.();
}

/** Give a dialog its title-bar controls, dragging and detaching. `close` cancels it (also used when its window is closed). */
export function makeFloating(modal: HTMLElement, close: () => void): void {
  const dlg = modal.querySelector<HTMLElement>(".dlg");
  const head = modal.querySelector<HTMLElement>(".dlgHead");
  if (!dlg || !head) return;
  const e: Entry = { modal, dlg, close, child: null, rect: null, dock: null, moved: false, watcher: 0, back: null };
  entries.push(e);

  const tools = document.createElement("span");
  tools.className = "dlgTools";
  const pop = document.createElement("button");
  pop.type = "button";
  pop.className = "dlgTool";
  pop.textContent = "⧉";
  pop.title = "Desacoplar: sacar a una ventana aparte (para llevarla a otro monitor)";
  pop.addEventListener("click", () => { if (e.child) attach(e); else detach(e); });
  tools.append(pop);
  head.append(tools);
  const refreshTool = () => {
    const out = !!e.child;
    pop.textContent = out ? "⮌" : "⧉";
    pop.title = out ? "Acoplar: devolverla a la ventana principal" : "Desacoplar: sacar a una ventana aparte (para llevarla a otro monitor)";
  };
  modal.addEventListener("xl:moved", refreshTool);

  // drag by the title bar
  head.addEventListener("pointerdown", (ev) => {
    if (e.child || ev.button !== 0 || (ev.target as HTMLElement).closest("button, input, select, textarea")) return;
    ev.preventDefault();
    floatIn(e);
    const r = dlg.getBoundingClientRect();
    const dx = ev.clientX - r.left, dy = ev.clientY - r.top;
    try { head.setPointerCapture(ev.pointerId); } catch { /* synthetic events have no active pointer */ }
    const move = (m: PointerEvent) => place(e, m.clientX - dx, m.clientY - dy);
    const up = () => { e.moved = true; head.removeEventListener("pointermove", move); head.removeEventListener("pointerup", up); head.removeEventListener("pointercancel", up); remember(e); };
    head.addEventListener("pointermove", move);
    head.addEventListener("pointerup", up);
    head.addEventListener("pointercancel", up);
  });
  head.style.cursor = "move";
  dlg.addEventListener("pointerup", () => { // resized by its corner: that size is kept too
    if (!modal.classList.contains("floating")) return;
    const was = e.rect;
    remember(e);
    if (was && e.rect && (Math.abs(was.width - e.rect.width) > 1 || Math.abs(was.height - e.rect.height) > 1)) e.moved = true;
  });

  // opening again: back where it was left; closing while detached: the window goes away
  new MutationObserver(() => {
    if (modal.hidden) {
      if (e.child) attach(e);
      modal.classList.remove("floating");
      for (const p of ["left", "top", "width", "height", "maxHeight"] as const) dlg.style[p] = "";
    } else showAgain(e);
  }).observe(modal, { attributes: true, attributeFilter: ["hidden"] });
  window.addEventListener("resize", () => { if (modal.classList.contains("floating") && !modal.hidden) place(e, dlg.offsetLeft, dlg.offsetTop); });
}

/** Test and diagnostics: where a dialog is now. */
export function floatingState(id: string): { floating: boolean; detached: boolean; windowOpen: boolean } | null {
  const e = entries.find((x) => x.modal.id === id);
  return e ? { floating: e.modal.classList.contains("floating"), detached: e.modal.classList.contains("detached"), windowOpen: !!e.child && !e.child.closed } : null;
}
