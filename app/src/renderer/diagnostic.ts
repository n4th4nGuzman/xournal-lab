/** One pointer sample as the browser reported it. */
export interface Sample {
  type: string; // pointermove, pointerrawupdate, coalesced
  pointerType: string;
  x: number; // clientX, CSS pixels
  y: number;
  pressure: number;
  t: number; // timeStamp, ms
}

export interface Report {
  samples: number;
  pointerTypes: string;
  /** Share of samples whose position has a fractional part in CSS pixels. */
  fractionalCss: number;
  /** Share whose position, in physical pixels (CSS * devicePixelRatio), is still not a whole number. */
  fractionalPhysical: number;
  /** How many different fractional values (in 1/100 px) the X coordinates take. */
  distinctFractions: number;
  medianIntervalMs: number;
  eventsPerSecond: number;
  distinctPressures: number;
  dpr: number;
  verdict: string;
}

const frac = (v: number) => Math.abs(v - Math.round(v));
const EPS = 1e-6;

/** Summarise the precision of recorded input. Pure, so it can be tested with crafted samples. */
export function analyze(samples: Sample[], dpr: number): Report {
  const n = samples.length;
  const moves = samples.filter((s) => s.type === "pointermove");
  const base = moves.length ? moves : samples;
  const fracCss = base.filter((s) => frac(s.x) > EPS || frac(s.y) > EPS).length;
  const fracPhys = base.filter((s) => frac(s.x * dpr) > 1e-4 || frac(s.y * dpr) > 1e-4).length;
  const fractions = new Set(base.map((s) => Math.round(frac(s.x) * 100)));
  const dts: number[] = [];
  for (let i = 1; i < base.length; i++) { const d = base[i]!.t - base[i - 1]!.t; if (d > 0) dts.push(d); }
  dts.sort((a, b) => a - b);
  const median = dts.length ? dts[Math.floor(dts.length / 2)]! : 0;
  const types = new Map<string, number>();
  for (const s of base) types.set(s.pointerType, (types.get(s.pointerType) ?? 0) + 1);
  const shareCss = base.length ? fracCss / base.length : 0;
  const sharePhys = base.length ? fracPhys / base.length : 0;
  // What matters is whether positions have a fractional part in PHYSICAL pixels: on a scaled screen
  // (e.g. 125 %) CSS pixels are fractional anyway, yet the input can still be whole device pixels.
  let verdict: string;
  if (!base.length) verdict = "Sin datos: dibuja un trazo dentro del recuadro.";
  else if (sharePhys > 0.3) verdict = "El navegador entrega posiciones con DECIMALES incluso en píxeles físicos: la entrada tiene precisión de sub-píxel. Los escalones a zoom bajo no vendrían de aquí.";
  else verdict = `Posiciones ENTERAS en píxeles físicos${dpr !== 1 ? ` (en píxeles CSS son múltiplos de 1/${dpr})` : ""}: el navegador redondea la entrada a píxeles de pantalla. Esa es la fuente de los escalones a zoom bajo.`;
  return {
    samples: n,
    pointerTypes: [...types].map(([k, v]) => `${k}: ${v}`).join(", ") || "-",
    fractionalCss: +shareCss.toFixed(3),
    fractionalPhysical: +sharePhys.toFixed(3),
    distinctFractions: fractions.size,
    medianIntervalMs: +median.toFixed(2),
    eventsPerSecond: median > 0 ? Math.round(1000 / median) : 0,
    distinctPressures: new Set(base.map((s) => s.pressure.toFixed(4))).size,
    dpr,
    verdict,
  };
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** A small dialog where the user draws a stroke and sees how precisely the input arrives. */
export class PenDiagnostic {
  private dlg = $("penDlg");
  private cv = $<HTMLCanvasElement>("penCanvas");
  private out = $("penReport");
  private samples: Sample[] = [];
  private down = false;
  private lastDraw: { x: number; y: number } | null = null;

  constructor() {
    const rec = (e: PointerEvent, type: string) => {
      this.samples.push({ type, pointerType: e.pointerType, x: e.clientX, y: e.clientY, pressure: e.pressure, t: e.timeStamp });
    };
    this.cv.addEventListener("pointerdown", (e) => {
      this.cv.setPointerCapture(e.pointerId);
      this.down = true;
      this.samples = [];
      this.clear();
      rec(e, "pointermove");
      this.lastDraw = null;
    });
    this.cv.addEventListener("pointermove", (e) => {
      if (!this.down) return;
      const co = typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
      rec(e, "pointermove");
      for (const c of co) rec(c, "coalesced");
      this.draw(e);
    });
    this.cv.addEventListener("pointerrawupdate", ((e: PointerEvent) => { if (this.down) rec(e, "pointerrawupdate"); }) as EventListener);
    const end = () => { if (!this.down) return; this.down = false; this.show(); };
    this.cv.addEventListener("pointerup", end);
    this.cv.addEventListener("pointercancel", end);
    $("penClose").addEventListener("click", () => this.close());
    $("penCopy").addEventListener("click", () => { void navigator.clipboard?.writeText(this.out.textContent ?? ""); });
    this.dlg.addEventListener("keydown", (e) => { e.stopPropagation(); if (e.key === "Escape") this.close(); });
  }

  get isOpen(): boolean { return !this.dlg.hidden; }

  open(): void {
    this.dlg.hidden = false;
    this.samples = [];
    this.out.textContent = "Dibuja con el lápiz (o el ratón) un trazo lento y curvo dentro del recuadro.";
    const dpr = window.devicePixelRatio || 1;
    this.cv.width = Math.round(this.cv.clientWidth * dpr);
    this.cv.height = Math.round(this.cv.clientHeight * dpr);
    this.clear();
  }

  close(): void { this.dlg.hidden = true; }

  private clear(): void {
    const c = this.cv.getContext("2d")!;
    c.fillStyle = "#fff";
    c.fillRect(0, 0, this.cv.width, this.cv.height);
  }

  private draw(e: PointerEvent): void {
    const r = this.cv.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const x = (e.clientX - r.left) * dpr, y = (e.clientY - r.top) * dpr;
    const c = this.cv.getContext("2d")!;
    c.strokeStyle = "#000";
    c.lineWidth = 1.5 * dpr;
    c.lineCap = "round";
    c.beginPath();
    c.moveTo(this.lastDraw?.x ?? x, this.lastDraw?.y ?? y);
    c.lineTo(x, y);
    c.stroke();
    this.lastDraw = { x, y };
  }

  private show(): void {
    const rep = analyze(this.samples, window.devicePixelRatio || 1);
    const pct = (v: number) => `${Math.round(v * 100)} %`;
    this.out.textContent = [
      rep.verdict,
      "",
      `Muestras: ${rep.samples}   (${rep.pointerTypes})`,
      `Con decimales en píxeles CSS: ${pct(rep.fractionalCss)}`,
      `Con decimales en píxeles físicos: ${pct(rep.fractionalPhysical)}`,
      `Valores distintos de la parte decimal de X: ${rep.distinctFractions}`,
      `Intervalo mediano entre eventos: ${rep.medianIntervalMs} ms  (~${rep.eventsPerSecond} eventos/s)`,
      `Niveles distintos de presión: ${rep.distinctPressures}`,
      `Escala de pantalla (devicePixelRatio): ${rep.dpr}`,
    ].join("\n");
  }
}
