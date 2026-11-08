/** The "Lápiz y tableta" dialog: tip and size, pressure curve with live readout, pen buttons and keyboard shortcuts. */
import { windowOf } from "./floating";
import { BRUSHES, BrushEngine, CURVE_PRESETS, PressureMap, demoStroke, type BrushKind, type PressureCurve } from "./brush";
import { APP_ACTIONS, PEN_ACTIONS, comboOf, mouseCombo, penButtonOf, sanitizeTablet, wheelCombo, type PenAction, type TabletSettings } from "./tablet";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const el = <K extends keyof HTMLElementTagNameMap>(tag: K, cls?: string, text?: string): HTMLElementTagNameMap[K] => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};

/** The keys of the remote as the driver should be told to send them: Ctrl+Alt+Shift+number can be typed in its field; F13 and up are the alternative when it lets you pick from a list. */
const EXPRESS: Array<[string, string, string]> = [
  ["Tecla 1", "Ctrl+Alt+Shift+1", "F13"], ["Tecla 2", "Ctrl+Alt+Shift+2", "F14"], ["Tecla 3", "Ctrl+Alt+Shift+3", "F15"], ["Tecla 4", "Ctrl+Alt+Shift+4", "F16"],
  ["Tecla 5", "Ctrl+Alt+Shift+5", "F17"], ["Tecla 6", "Ctrl+Alt+Shift+6", "F18"], ["Tecla 7", "Ctrl+Alt+Shift+7", "F19"], ["Tecla 8", "Ctrl+Alt+Shift+8", "F20"],
  ["Dial ↻ (si el controlador lo deja como tecla)", "Ctrl+Alt+Shift+9", "F21"], ["Dial ↺ (si el controlador lo deja como tecla)", "Ctrl+Alt+Shift+0", "F22"],
];
const SUGGESTED = ["tool.pen", "tool.eraser", "tool.highlighter", "tool.select", "edit.undo", "edit.redo", "brush.next", "color.next", "view.zoomIn", "view.zoomOut"];

interface Done { pts: number[]; widths: number[] }

export interface TabletHost {
  /** Current settings and size; the dialog edits them and calls `change`. */
  settings(): TabletSettings;
  change(s: TabletSettings): void;
}

export class TabletDialog {
  private dlg = $("tabletDlg");
  private body = $("tabletBody");
  private s!: TabletSettings;
  private size = 1.41;
  private cards = new Map<BrushKind, HTMLCanvasElement>();
  private curveCv!: HTMLCanvasElement;
  private testCv!: HTMLCanvasElement;
  private readout!: HTMLElement;
  private bar!: HTMLElement;
  private barMapped!: HTMLElement;
  private levels = new Set<number>();
  private recent: Array<[number, number]> = [];
  private strokes: Done[] = [];
  private live: { engine: BrushEngine; pts: number[]; widths: number[] } | null = null;
  private drag: 1 | 2 | null = null;
  private capturing = false;
  private pickedAction = APP_ACTIONS[0]!.id;
  private keyList!: HTMLElement;
  private expressSel = new Map<string, HTMLSelectElement>();
  private captureBtn!: HTMLButtonElement;
  private sizeRange!: HTMLInputElement;
  private sizeNum!: HTMLInputElement;
  private onWindowMove = (e: PointerEvent) => this.sample(e);

  constructor(private host: TabletHost) {
    $("tabletClose").addEventListener("click", () => this.close());
    this.dlg.addEventListener("pointerdown", (e) => { if (e.target === this.dlg && !this.dlg.classList.contains("floating")) this.close(); });
    this.dlg.addEventListener("xl:moved", () => this.listen());
    this.dlg.addEventListener("keydown", (e) => {
      e.stopPropagation();
      if (this.capturing) { this.capture(e); return; }
      if (e.key === "Escape") this.close();
    });
    // the dial of the remote usually sends the mouse wheel, and a driver can send the extra mouse buttons: they bind like keys
    this.dlg.addEventListener("wheel", (e) => { if (this.capturing) this.bind(e, wheelCombo(e)); }, { passive: false });
    this.dlg.addEventListener("mousedown", (e) => { if (this.capturing) this.bind(e, mouseCombo(e)); });
  }

  private bind(e: Event, combo: string | null): void {
    if (!combo) return;
    e.preventDefault();
    this.s.keys[combo] = this.pickedAction;
    this.stopCapture();
    this.commit();
  }

  get isOpen(): boolean { return !this.dlg.hidden; }

  open(): void {
    this.s = sanitizeTablet(JSON.parse(JSON.stringify(this.host.settings())));
    this.size = this.s.size;
    this.levels.clear();
    this.recent = [];
    this.strokes = [];
    this.build();
    this.dlg.hidden = false;
    this.paintAll();
    this.listen();
  }

  /** The live readout listens in the window the dialog is in now (it can be in one of its own). */
  private listenedIn: Window | null = null;
  private listen(): void {
    this.unlisten();
    if (this.dlg.hidden) return;
    this.listenedIn = windowOf(this.dlg);
    this.listenedIn.addEventListener("pointermove", this.onWindowMove, true);
    this.listenedIn.addEventListener("pointerdown", this.onWindowMove, true);
  }
  private unlisten(): void {
    this.listenedIn?.removeEventListener("pointermove", this.onWindowMove, true);
    this.listenedIn?.removeEventListener("pointerdown", this.onWindowMove, true);
    this.listenedIn = null;
  }

  close(): void {
    this.capturing = false;
    this.dlg.hidden = true;
    this.unlisten();
  }

  private commit(): void {
    this.s.size = this.size;
    this.host.change(this.s);
    this.paintAll();
  }

  // ------------------------------------------------------------------------------------------------------ layout
  private build(): void {
    this.body.replaceChildren();
    this.cards.clear();

    const tip = el("section");
    tip.append(el("h3", undefined, "Punta y tamaño"));
    const row = el("div", "tbCards");
    for (const b of BRUSHES) {
      const card = el("button", "tbCard");
      card.type = "button";
      card.title = b.hint;
      card.dataset.kind = b.kind;
      const cv = el("canvas");
      cv.width = 150; cv.height = 46;
      this.cards.set(b.kind, cv);
      card.append(cv, el("span", undefined, b.name));
      card.addEventListener("click", () => { this.s.brush = b.kind; this.commit(); });
      row.append(card);
    }
    tip.append(row);
    const size = el("div", "tbRow");
    this.sizeRange = el("input"); this.sizeRange.type = "range"; this.sizeRange.min = "0.2"; this.sizeRange.max = "24"; this.sizeRange.step = "0.05";
    this.sizeNum = el("input"); this.sizeNum.type = "number"; this.sizeNum.min = "0.1"; this.sizeNum.max = "60"; this.sizeNum.step = "0.05";
    const setSize = (v: number) => { if (Number.isFinite(v) && v > 0) { this.size = Math.min(60, Math.max(0.1, v)); this.commit(); } };
    this.sizeRange.addEventListener("input", () => setSize(Number(this.sizeRange.value)));
    this.sizeNum.addEventListener("change", () => setSize(Number(this.sizeNum.value)));
    size.append(el("label", undefined, "Grosor"), this.sizeRange, this.sizeNum);
    tip.append(size, el("p", "tbNote", "El grosor es el de referencia: cada punta lo lleva más fino o más grueso según presión, velocidad o dirección. En un lápiz óptico con presión se nota más; con el ratón, la tinta y el pincel responden a la velocidad."));
    this.body.append(tip);

    const pr = el("section");
    pr.append(el("h3", undefined, "Presión"));
    const wrap = el("div", "tbPressure");
    this.curveCv = el("canvas"); this.curveCv.width = 240; this.curveCv.height = 240; this.curveCv.className = "tbCurve";
    this.curveCv.title = "Arrastra los dos puntos para cambiar la curva";
    this.curveCv.addEventListener("pointerdown", (e) => this.curveDown(e));
    this.curveCv.addEventListener("pointermove", (e) => this.curveMove(e));
    this.curveCv.addEventListener("pointerup", () => { this.drag = null; });
    const side = el("div", "tbSide");
    const preset = el("select");
    preset.append(new Option("Curva personalizada", "-1"), ...CURVE_PRESETS.map((p, i) => new Option(p.name, String(i))));
    preset.id = "tbPreset";
    preset.addEventListener("change", () => { const i = Number(preset.value); if (i >= 0) { this.s.curve = { ...CURVE_PRESETS[i]!.curve }; this.commit(); } });
    const dead = el("input"); dead.type = "range"; dead.min = "0"; dead.max = "0.3"; dead.step = "0.005"; dead.id = "tbDead";
    dead.addEventListener("input", () => { this.s.curve = { ...this.s.curve, min: Number(dead.value) }; this.commit(); });
    this.bar = el("div", "tbBar"); this.bar.append(el("i"));
    this.barMapped = el("div", "tbBar mapped"); this.barMapped.append(el("i"));
    this.readout = el("pre", "tbReadout", "Mueve el lápiz sobre la tableta (o toca la pantalla) para ver la presión.");
    side.append(el("label", undefined, "Ajuste"), preset, el("label", undefined, "Zona muerta (presión mínima que cuenta)"), dead, el("label", undefined, "Presión del lápiz"), this.bar, el("label", undefined, "Presión con la curva"), this.barMapped, this.readout);
    wrap.append(this.curveCv, side);
    pr.append(wrap);
    this.testCv = el("canvas", "tbTest"); this.testCv.width = 640; this.testCv.height = 130;
    this.testCv.addEventListener("pointerdown", (e) => this.testDown(e));
    this.testCv.addEventListener("pointermove", (e) => this.testMove(e));
    this.testCv.addEventListener("pointerup", (e) => this.testUp(e));
    this.testCv.addEventListener("pointercancel", (e) => this.testUp(e));
    const clear = el("button", undefined, "Limpiar la hoja de prueba"); clear.type = "button";
    clear.addEventListener("click", () => { this.strokes = []; this.paintTest(); });
    pr.append(el("label", undefined, "Hoja de prueba (dibuja aquí con la punta elegida)"), this.testCv, clear);
    this.body.append(pr);

    const bt = el("section");
    bt.append(el("h3", undefined, "Botones del lápiz"));
    const grid = el("div", "tbGrid");
    const sel = (key: "barrel" | "barrel2" | "eraser", label: string) => {
      const s = el("select"); s.id = "tb_" + key;
      s.append(...PEN_ACTIONS.map((a) => new Option(a.label, a.id)));
      s.addEventListener("change", () => { this.s[key] = s.value as PenAction; this.commit(); });
      grid.append(el("label", undefined, label), s);
    };
    sel("barrel", "Botón lateral 1 (el más cercano a la punta)");
    sel("barrel2", "Botón lateral 2");
    sel("eraser", "Extremo borrador");
    bt.append(grid, el("p", "tbNote", "Una herramienta se usa mientras mantienes pulsado el botón y vuelve a la anterior al soltarlo. Deshacer y rehacer actúan al pulsar. Si el controlador XP-Pen reasigna los botones a otras funciones (clic derecho, teclas, etc.), aquí no llegan como botones: déjalos en «clic derecho» / «borrador» en el controlador, o usa los atajos de teclado de abajo."));
    this.body.append(bt);

    const ex = el("section");
    ex.append(el("h3", undefined, "Teclas exprés del mando (Tecla 1, Tecla 2…)"));
    ex.append(el("p", "tbNote", "El controlador de XP-Pen no deja que una tecla se identifique sola: a cada una hay que ponerle una combinación de teclado. Usa estas, que puedes teclear en su campo (mantén Ctrl, Alt y Mayús y pulsa el número) y casi nunca chocan con nada; aquí eliges qué hace cada una. Si el controlador te deja elegir la tecla de una lista, también valen F13 a F22."));
    const exGrid = el("div", "tbGrid");
    this.expressSel.clear();
    EXPRESS.forEach(([label, key, alt]) => {
      const sel = el("select");
      sel.append(new Option("— nada —", ""));
      let grp = "";
      let og2: HTMLOptGroupElement | null = null;
      for (const a of APP_ACTIONS) { if (a.group !== grp) { grp = a.group; og2 = el("optgroup"); og2.label = grp; sel.append(og2); } og2!.append(new Option(a.label, a.id)); }
      sel.addEventListener("change", () => { for (const k of [key, alt]) { if (sel.value) this.s.keys[k] = sel.value; else delete this.s.keys[k]; } this.commit(); });
      this.expressSel.set(key, sel);
      exGrid.append(el("label", undefined, `${label}  →  en el controlador pulsa ${key.replace("Shift", "Mayús")}`), sel);
    });
    const suggest = el("button", undefined, "Proponer una asignación"); suggest.type = "button";
    suggest.title = "Rellena las teclas vacías con una asignación cómoda para dibujar";
    suggest.addEventListener("click", () => { EXPRESS.forEach(([, key, alt], i) => { if (!this.s.keys[key] && SUGGESTED[i]) { this.s.keys[key] = SUGGESTED[i]!; this.s.keys[alt] = SUGGESTED[i]!; } }); this.commit(); });
    ex.append(exGrid, suggest);
    this.body.append(ex);

    const ks = el("section");
    ks.append(el("h3", undefined, "Atajos de teclado (teclas de la tableta y del mando ACK05)"));
    ks.append(el("p", "tbNote", "El controlador convierte cada tecla del mando en una combinación (por ejemplo Ctrl+Alt+1). Aquí eliges una acción, pulsas «Asignar» y luego pulsas la tecla del mando o giras su rueda (también vale la rueda con Ctrl, Alt o Mayús): queda enlazada. Estos atajos tienen prioridad sobre los de siempre."));
    const add = el("div", "tbRow");
    const act = el("select");
    let group = "";
    let og: HTMLOptGroupElement | null = null;
    for (const a of APP_ACTIONS) { if (a.group !== group) { group = a.group; og = el("optgroup"); og.label = group; act.append(og); } og!.append(new Option(a.label, a.id)); }
    act.addEventListener("change", () => { this.pickedAction = act.value; });
    this.captureBtn = el("button", undefined, "Asignar tecla…"); this.captureBtn.type = "button";
    this.captureBtn.addEventListener("click", () => { this.capturing = true; this.captureBtn.textContent = "Pulsa la tecla o gira la rueda… (Esc cancela)"; this.captureBtn.classList.add("on"); this.captureBtn.focus(); });
    add.append(act, this.captureBtn);
    this.keyList = el("div", "tbKeys");
    ks.append(add, this.keyList);
    this.body.append(ks);
  }

  // ----------------------------------------------------------------------------------------------------- painting
  private paintAll(): void {
    for (const [kind, cv] of this.cards) {
      cv.parentElement!.classList.toggle("on", kind === this.s.brush);
      this.paintDemo(cv, kind);
    }
    this.sizeRange.value = String(Math.min(24, this.size));
    this.sizeNum.value = String(+this.size.toFixed(2));
    $<HTMLSelectElement>("tb_barrel").value = this.s.barrel;
    $<HTMLSelectElement>("tb_barrel2").value = this.s.barrel2;
    $<HTMLSelectElement>("tb_eraser").value = this.s.eraser;
    const c = this.s.curve;
    const idx = CURVE_PRESETS.findIndex((p) => ["x1", "y1", "x2", "y2", "min"].every((k) => Math.abs((p.curve as unknown as Record<string, number>)[k]! - (c as unknown as Record<string, number>)[k]!) < 1e-6));
    $<HTMLSelectElement>("tbPreset").value = String(idx);
    $<HTMLInputElement>("tbDead").value = String(c.min);
    this.paintCurve();
    this.paintTest();
    for (const [key, sel] of this.expressSel) sel.value = this.s.keys[key] ?? "";
    this.paintKeys();
  }

  private ink(): string { return getComputedStyle(this.dlg).color || "#ddd"; }

  private paintDemo(cv: HTMLCanvasElement, kind: BrushKind): void {
    const g = cv.getContext("2d")!;
    g.clearRect(0, 0, cv.width, cv.height);
    const engine = new BrushEngine(kind, Math.min(this.size, 7), new PressureMap(this.s.curve), true);
    const pts: number[] = [], widths: number[] = [];
    demoStroke(cv.width, cv.height).forEach(([x, y, p, t], i) => { pts.push(x, y); widths.push(i === 0 ? engine.first() : engine.next(x, y, p, t)); if (i === 0) engine.next(x, y, p, t); });
    this.drawStroke(g, pts, engine.finish(pts, widths), 1);
  }

  private drawStroke(g: CanvasRenderingContext2D, pts: number[], widths: number[], scale: number, color = this.ink()): void {
    g.strokeStyle = color; g.fillStyle = g.strokeStyle; g.lineCap = "round"; g.lineJoin = "round";
    if (pts.length === 2) { g.beginPath(); g.arc(pts[0]!, pts[1]!, (widths[0]! * scale) / 2, 0, Math.PI * 2); g.fill(); return; }
    for (let i = 1; i < pts.length / 2; i++) {
      g.lineWidth = (widths[i] ?? widths[0]!) * scale;
      g.beginPath(); g.moveTo(pts[2 * i - 2]!, pts[2 * i - 1]!); g.lineTo(pts[2 * i]!, pts[2 * i + 1]!); g.stroke();
    }
  }

  private curveXY(c: PressureCurve): Array<[number, number]> { return [[c.x1, c.y1], [c.x2, c.y2]]; }

  private paintCurve(): void {
    const cv = this.curveCv, g = cv.getContext("2d")!, W = cv.width, H = cv.height, pad = 14;
    const X = (x: number) => pad + x * (W - 2 * pad), Y = (y: number) => H - pad - y * (H - 2 * pad);
    g.clearRect(0, 0, W, H);
    const ink = this.ink();
    g.strokeStyle = ink; g.globalAlpha = 0.2; g.lineWidth = 1;
    for (let i = 0; i <= 4; i++) { g.beginPath(); g.moveTo(X(i / 4), Y(0)); g.lineTo(X(i / 4), Y(1)); g.moveTo(X(0), Y(i / 4)); g.lineTo(X(1), Y(i / 4)); g.stroke(); }
    g.globalAlpha = 0.35; g.setLineDash([4, 4]); g.beginPath(); g.moveTo(X(0), Y(0)); g.lineTo(X(1), Y(1)); g.stroke(); g.setLineDash([]);
    g.globalAlpha = 1;
    const map = new PressureMap(this.s.curve);
    g.strokeStyle = "#2f9bff"; g.lineWidth = 2.5; g.beginPath();
    for (let i = 0; i <= 100; i++) { const x = i / 100, y = map.apply(x); if (i === 0) g.moveTo(X(x), Y(y)); else g.lineTo(X(x), Y(y)); }
    g.stroke();
    const [h1, h2] = this.curveXY(this.s.curve);
    g.strokeStyle = ink; g.globalAlpha = 0.5; g.lineWidth = 1;
    g.beginPath(); g.moveTo(X(0), Y(0)); g.lineTo(X(h1![0]), Y(h1![1])); g.moveTo(X(1), Y(1)); g.lineTo(X(h2![0]), Y(h2![1])); g.stroke(); g.globalAlpha = 1;
    for (const h of [h1!, h2!]) { g.fillStyle = "#fff"; g.strokeStyle = "#2f6fd0"; g.lineWidth = 2; g.beginPath(); g.arc(X(h[0]), Y(h[1]), 6, 0, Math.PI * 2); g.fill(); g.stroke(); }
    g.fillStyle = "#ff5a8a";
    for (const [px, py] of this.recent) { g.globalAlpha = 0.5; g.beginPath(); g.arc(X(px), Y(py), 2.2, 0, Math.PI * 2); g.fill(); }
    g.globalAlpha = 1;
    g.fillStyle = ink; g.font = "11px system-ui, sans-serif";
    g.fillText("presión del lápiz →", W - 110, H - 2);
    g.save(); g.translate(11, 150); g.rotate(-Math.PI / 2); g.fillText("grosor →", 0, 0); g.restore();
  }

  private paintTest(): void {
    const g = this.testCv.getContext("2d")!;
    g.clearRect(0, 0, this.testCv.width, this.testCv.height);
    const scale = 1;
    for (const s of this.strokes) this.drawStroke(g, s.pts, s.widths, scale, "#111");
    if (this.live) this.drawStroke(g, this.live.pts, this.live.widths, scale, "#111");
  }

  private paintKeys(): void {
    this.keyList.replaceChildren();
    const entries = Object.entries(this.s.keys);
    if (!entries.length) this.keyList.append(el("p", "tbNote", "Aún no hay atajos propios."));
    for (const [combo, id] of entries) {
      const r = el("div", "tbKeyRow");
      r.append(el("kbd", undefined, combo), el("span", undefined, APP_ACTIONS.find((a) => a.id === id)?.label ?? id));
      const del = el("button", undefined, "Quitar"); del.type = "button";
      del.addEventListener("click", () => { delete this.s.keys[combo]; this.commit(); });
      r.append(del);
      this.keyList.append(r);
    }
  }

  // ------------------------------------------------------------------------------------------------------ input
  private capture(e: KeyboardEvent): void {
    e.preventDefault();
    if (e.key === "Escape") { this.stopCapture(); return; }
    const combo = comboOf(e);
    if (!combo) return;
    this.s.keys[combo] = this.pickedAction;
    this.stopCapture();
    this.commit();
  }

  private stopCapture(): void {
    this.capturing = false;
    this.captureBtn.textContent = "Asignar tecla…";
    this.captureBtn.classList.remove("on");
  }

  /** Live readout of any pen, mouse or finger sample that reaches the window while the dialog is open. */
  private sample(e: PointerEvent): void {
    if (this.dlg.hidden) return;
    const raw = e.pressure, map = new PressureMap(this.s.curve), mapped = map.apply(raw);
    (this.bar.firstElementChild as HTMLElement).style.width = `${Math.round(raw * 100)}%`;
    (this.barMapped.firstElementChild as HTMLElement).style.width = `${Math.round(mapped * 100)}%`;
    if (raw > 0) { this.levels.add(Math.round(raw * 100000)); this.recent.push([raw, mapped]); if (this.recent.length > 60) this.recent.shift(); this.paintCurve(); }
    const b = penButtonOf(e);
    this.readout.textContent = [
      `Dispositivo: ${e.pointerType || "-"}`,
      `Presión: ${(raw * 100).toFixed(1)} %  →  ${(mapped * 100).toFixed(1)} % con la curva`,
      `Inclinación: X ${e.tiltX ?? 0}°  Y ${e.tiltY ?? 0}°`,
      `Botones: ${e.buttons}${b === "eraser" ? "  (extremo borrador)" : b === "barrel" ? "  (botón lateral 1)" : b === "barrel2" ? "  (botón lateral 2)" : ""}`,
      `Niveles de presión distintos vistos: ${this.levels.size}`,
    ].join("\n");
  }

  private pos(e: PointerEvent): [number, number] {
    const r = this.testCv.getBoundingClientRect();
    return [((e.clientX - r.left) / r.width) * this.testCv.width, ((e.clientY - r.top) / r.height) * this.testCv.height];
  }

  private testDown(e: PointerEvent): void {
    if (e.button !== 0 && !penButtonOf(e)) return;
    try { this.testCv.setPointerCapture(e.pointerId); } catch { /* synthetic events have no active pointer */ }
    const [x, y] = this.pos(e);
    const engine = new BrushEngine(this.s.brush, this.size, new PressureMap(this.s.curve), e.pointerType === "pen");
    engine.next(x, y, e.pressure, e.timeStamp);
    this.live = { engine, pts: [x, y], widths: [engine.first()] };
    this.paintTest();
  }

  private testMove(e: PointerEvent): void {
    const l = this.live;
    if (!l) return;
    const co = typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
    for (const ev of co.length ? co : [e]) {
      const [x, y] = this.pos(ev);
      if (Math.hypot(x - l.pts[l.pts.length - 2]!, y - l.pts[l.pts.length - 1]!) < 0.2) continue;
      l.pts.push(x, y);
      l.widths.push(l.engine.next(x, y, ev.pressure, ev.timeStamp));
    }
    this.paintTest();
  }

  private testUp(e: PointerEvent): void {
    const l = this.live;
    this.live = null;
    if (!l) return;
    try { this.testCv.releasePointerCapture(e.pointerId); } catch { /* not captured */ }
    this.strokes.push({ pts: l.pts, widths: l.widths.length === l.pts.length / 2 ? l.engine.finish(l.pts, l.widths) : l.widths });
    this.paintTest();
  }

  private curveEvent(e: PointerEvent): [number, number] {
    const r = this.curveCv.getBoundingClientRect(), pad = 14 * (r.width / this.curveCv.width);
    const x = (e.clientX - r.left - pad) / (r.width - 2 * pad), y = 1 - (e.clientY - r.top - pad) / (r.height - 2 * pad);
    return [Math.min(1, Math.max(0, x)), Math.min(1, Math.max(0, y))];
  }

  private curveDown(e: PointerEvent): void {
    const [x, y] = this.curveEvent(e);
    const [h1, h2] = this.curveXY(this.s.curve);
    const d1 = Math.hypot(h1![0] - x, h1![1] - y), d2 = Math.hypot(h2![0] - x, h2![1] - y);
    this.drag = d1 <= d2 ? 1 : 2;
    try { this.curveCv.setPointerCapture(e.pointerId); } catch { /* synthetic events have no active pointer */ }
    this.curveMove(e);
  }

  private curveMove(e: PointerEvent): void {
    if (!this.drag) return;
    const [x, y] = this.curveEvent(e);
    this.s.curve = this.drag === 1 ? { ...this.s.curve, x1: x, y1: y } : { ...this.s.curve, x2: x, y2: y };
    this.commit();
  }
}
