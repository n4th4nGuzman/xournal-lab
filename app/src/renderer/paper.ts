/**
 * Special papers for maths and physics classes: millimetre, Cartesian plane, trigonometric, polar, logarithmic,
 * isometric and Cornell notes. A page's paper is a `PaperSpec`, saved on the `<background>` as the extra attribute
 * `xlPaper="kind;key=value;..."`. The ordinary `style` attribute carries a close built-in look (`fallback`), so a
 * program that does not know `xlPaper` (the official Xournal++) still shows a sensible grid or plain paper.
 *
 * Everything here is pure drawing in page units (points); nothing touches the DOM.
 */

export interface PaperSpec { kind: string; params: Record<string, string> }

export interface PaperParam {
  key: string;
  label: string;
  /** [value, label]; the first option is the default. */
  options: Array<[string, string]>;
}

interface Ink {
  /** `rgb` of the grid lines, picked for the paper's brightness. */
  rgb: string;
  /** Colour of axes and labels: black-ish on light paper, white-ish on dark. */
  strong: string;
  dark: boolean;
}

type Params = Record<string, string>;
type Draw = (ctx: CanvasRenderingContext2D, w: number, h: number, p: Params, ink: Ink) => void;

export interface PaperKind {
  id: string;
  label: string;
  /** Built-in `style` attribute written next to `xlPaper`, for programs that do not know it. */
  fallback: string;
  params: PaperParam[];
  draw: Draw;
}

const MM = 72 / 25.4;
const CM = MM * 10;
const SUP = "⁰¹²³⁴⁵⁶⁷⁸⁹";
const sup = (n: number) => String(Math.abs(n)).split("").map((d) => SUP[Number(d)]).join("") ;

const alpha = (ink: Ink, a: number) => `rgba(${ink.rgb},${a})`;

/** Stroke a batch of segments in one path. */
function lines(ctx: CanvasRenderingContext2D, color: string, width: number, build: (move: (x: number, y: number) => void, line: (x: number, y: number) => void) => void): void {
  ctx.beginPath();
  build((x, y) => ctx.moveTo(x, y), (x, y) => ctx.lineTo(x, y));
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.stroke();
}

function text(ctx: CanvasRenderingContext2D, s: string, x: number, y: number, ink: Ink, align: CanvasTextAlign = "center", size = 7.5): void {
  ctx.font = `${size}px system-ui, sans-serif`;
  ctx.textAlign = align;
  ctx.textBaseline = "middle";
  ctx.fillStyle = ink.strong;
  ctx.fillText(s, x, y);
}

function arrow(ctx: CanvasRenderingContext2D, x: number, y: number, dx: number, dy: number, ink: Ink): void {
  const s = 5;
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x - dx * s * 1.6 + dy * s * 0.6, y - dy * s * 1.6 - dx * s * 0.6);
  ctx.lineTo(x - dx * s * 1.6 - dy * s * 0.6, y - dy * s * 1.6 + dx * s * 0.6);
  ctx.closePath();
  ctx.fillStyle = ink.strong;
  ctx.fill();
}

const minus = (n: number) => (n < 0 ? "−" : "") + Math.abs(+n.toFixed(2));

// ---------------------------------------------------------------- millimetre
const drawMm: Draw = (ctx, w, h, _p, ink) => {
  const nx = Math.floor(w / MM), ny = Math.floor(h / MM);
  const ox = (w - nx * MM) / 2, oy = (h - ny * MM) / 2;
  const tier = (i: number) => (i % 10 === 0 ? 2 : i % 5 === 0 ? 1 : 0);
  const spec: Array<[number, number, number]> = [[0, 0.2, 0.22], [1, 0.35, 0.45], [2, 0.6, 0.8]];
  for (const [t, wd, a] of spec) {
    lines(ctx, alpha(ink, a), wd, (m, l) => {
      for (let i = 0; i <= nx; i++) if (tier(i) === t) { m(ox + i * MM, oy); l(ox + i * MM, oy + ny * MM); }
      for (let j = 0; j <= ny; j++) if (tier(j) === t) { m(ox, oy + j * MM); l(ox + nx * MM, oy + j * MM); }
    });
  }
};

// ---------------------------------------------------------------- Cartesian plane
const drawCartesian: Draw = (ctx, w, h, p, ink) => {
  const u = Number(p.unit) * CM;
  const sub = p.sub === "on";
  const labels = p.labels !== "off";
  const one = p.quad === "1";
  const x0 = one ? Math.round(44 / u) * u : Math.round(w / 2 / u) * u;
  const y0 = one ? Math.floor((h - 44) / u) * u : Math.round(h / 2 / u) * u;
  const left = one ? x0 : 0, right = w, top = 0, bottom = one ? y0 : h;
  const kx0 = Math.ceil((left - x0) / u - 1e-6), kx1 = Math.floor((right - x0) / u + 1e-6);
  const ky0 = Math.ceil((top - y0) / u - 1e-6), ky1 = Math.floor((bottom - y0) / u + 1e-6);
  if (sub) {
    const f = u / 5;
    lines(ctx, alpha(ink, 0.16), 0.3, (m, l) => {
      for (let i = Math.ceil((left - x0) / f); i <= Math.floor((right - x0) / f); i++) if (i % 5 !== 0) { m(x0 + i * f, top); l(x0 + i * f, bottom); }
      for (let j = Math.ceil((top - y0) / f); j <= Math.floor((bottom - y0) / f); j++) if (j % 5 !== 0) { m(left, y0 + j * f); l(right, y0 + j * f); }
    });
  }
  lines(ctx, alpha(ink, 0.42), 0.5, (m, l) => {
    for (let k = kx0; k <= kx1; k++) if (k !== 0) { m(x0 + k * u, top); l(x0 + k * u, bottom); }
    for (let k = ky0; k <= ky1; k++) if (k !== 0) { m(left, y0 + k * u); l(right, y0 + k * u); }
  });
  ctx.lineCap = "butt";
  lines(ctx, ink.strong, 1.2, (m, l) => { m(left, y0); l(right - 8, y0); m(x0, bottom); l(x0, top + 8); });
  arrow(ctx, right - 2, y0, 1, 0, ink);
  arrow(ctx, x0, top + 2, 0, -1, ink);
  text(ctx, "x", right - 10, y0 + 12, ink, "center", 9);
  text(ctx, "y", x0 + 11, top + 12, ink, "center", 9);
  if (labels) {
    const every = u < 0.8 * CM ? 2 : 1;
    for (let k = kx0; k <= kx1; k++) if (k !== 0 && k % every === 0 && x0 + k * u < right - 14) text(ctx, minus(k), x0 + k * u, y0 + 9, ink);
    for (let k = ky0; k <= ky1; k++) if (k !== 0 && k % every === 0 && y0 + k * u > top + 14) text(ctx, minus(-k), x0 - 8, y0 + k * u, ink, "right");
    text(ctx, "O", x0 - 7, y0 + 9, ink, "right");
  }
};

// ---------------------------------------------------------------- trigonometric
const drawTrig: Draw = (ctx, w, h, p, ink) => {
  const m = 26;
  const sx = (w - 2 * m) / (4 * Math.PI); // points per radian: the page shows -2π..2π
  const sy = 2 * sx; // points per unit of y
  const x0 = w / 2, y0 = Math.round(h / 2 / (sy / 4)) * (sy / 4);
  const div = Number(p.grid); // minor vertical lines every π/div
  const step = Math.PI / div;
  const ymax = Math.floor((y0 - 20) / sy * 4) / 4, ymin = Math.floor((h - y0 - 20) / sy * 4) / 4;
  lines(ctx, alpha(ink, 0.16), 0.3, (mv, l) => {
    for (let i = -2 * div; i <= 2 * div; i++) if (i !== 0) { mv(x0 + i * step * sx, y0 - ymax * sy); l(x0 + i * step * sx, y0 + ymin * sy); }
    for (let j = -ymin * 4; j <= ymax * 4; j++) if (j !== 0 && j % 4 !== 0) { mv(m, y0 - (j / 4) * sy); l(w - m, y0 - (j / 4) * sy); }
  });
  lines(ctx, alpha(ink, 0.5), 0.5, (mv, l) => {
    for (let i = -4; i <= 4; i++) if (i !== 0) { mv(x0 + i * (Math.PI / 2) * sx, y0 - ymax * sy); l(x0 + i * (Math.PI / 2) * sx, y0 + ymin * sy); }
    for (let j = -Math.floor(ymin); j <= Math.floor(ymax); j++) if (j !== 0) { mv(m, y0 - j * sy); l(w - m, y0 - j * sy); }
  });
  lines(ctx, ink.strong, 1.2, (mv, l) => { mv(m - 6, y0); l(w - m, y0); mv(x0, y0 + ymin * sy); l(x0, y0 - ymax * sy - 4); });
  arrow(ctx, w - m + 6, y0, 1, 0, ink);
  arrow(ctx, x0, y0 - ymax * sy - 8, 0, -1, ink);
  const lab = ["", "π/2", "π", "3π/2", "2π"];
  for (let i = -4; i <= 4; i++) if (i !== 0) text(ctx, (i < 0 ? "−" : "") + lab[Math.abs(i)], x0 + i * (Math.PI / 2) * sx, y0 + 9, ink);
  for (let j = -Math.floor(ymin); j <= Math.floor(ymax); j++) if (j !== 0) text(ctx, minus(j), x0 - 6, y0 - j * sy, ink, "right");
  text(ctx, "x", w - m + 4, y0 + 12, ink, "center", 9);
  text(ctx, "y", x0 + 10, y0 - ymax * sy - 10, ink, "center", 9);
};

// ---------------------------------------------------------------- polar
const drawPolar: Draw = (ctx, w, h, p, ink) => {
  const cx = w / 2, cy = h / 2;
  const ring = Number(p.ring) * CM;
  const rmax = Math.floor((Math.min(w, h) / 2 - 26) / ring) * ring;
  const step = Number(p.step);
  const rings = Math.round(rmax / ring);
  lines(ctx, alpha(ink, 0.35), 0.45, (mv, l) => {
    for (let k = 1; k <= rings; k++) if (k % 5 !== 0) { mv(cx + k * ring, cy); for (let a = 1; a <= 120; a++) l(cx + Math.cos((a / 120) * 2 * Math.PI) * k * ring, cy - Math.sin((a / 120) * 2 * Math.PI) * k * ring); }
  });
  lines(ctx, alpha(ink, 0.7), 0.7, (mv, l) => {
    for (let k = 5; k <= rings; k += 5) { mv(cx + k * ring, cy); for (let a = 1; a <= 160; a++) l(cx + Math.cos((a / 160) * 2 * Math.PI) * k * ring, cy - Math.sin((a / 160) * 2 * Math.PI) * k * ring); }
    mv(cx + rmax, cy); for (let a = 1; a <= 160; a++) l(cx + Math.cos((a / 160) * 2 * Math.PI) * rmax, cy - Math.sin((a / 160) * 2 * Math.PI) * rmax);
  });
  for (const [major, wd, a] of [[false, 0.4, 0.3], [true, 0.7, 0.6]] as const) {
    lines(ctx, alpha(ink, a), wd, (mv, l) => {
      for (let d = 0; d < 360; d += step) if ((d % 30 === 0) === major) { const r = (d * Math.PI) / 180; mv(cx, cy); l(cx + Math.cos(r) * rmax, cy - Math.sin(r) * rmax); }
    });
  }
  for (let d = 0; d < 360; d += 30) { const r = (d * Math.PI) / 180; text(ctx, `${d}°`, cx + Math.cos(r) * (rmax + 14), cy - Math.sin(r) * (rmax + 14), ink); }
  for (let k = 1; k <= rings; k++) if (k % (ring < 0.8 * CM ? 2 : 1) === 0) text(ctx, +(k * Number(p.ring)).toFixed(2) + "", cx + k * ring + 1, cy + 7, ink, "left", 6.5);
};

// ---------------------------------------------------------------- isometric
const drawIso: Draw = (ctx, w, h, p, ink) => {
  const s = Number(p.size) * CM; // edge of the triangles
  const dx = (s * Math.sqrt(3)) / 2;
  const t = Math.tan(Math.PI / 6);
  const n = Math.floor(w / dx);
  const ox = (w - n * dx) / 2;
  ctx.save();
  ctx.translate(ox, 0);
  ctx.beginPath();
  ctx.rect(-ox, 0, w, h);
  ctx.clip();
  if (p.look === "dots") {
    ctx.fillStyle = alpha(ink, 0.6);
    for (let i = 0; i <= n; i++) for (let y = i % 2 ? s / 2 : 0; y <= h; y += s) ctx.fillRect(i * dx - 0.6, y - 0.6, 1.2, 1.2);
  } else {
    lines(ctx, alpha(ink, 0.4), 0.45, (mv, l) => {
      for (let i = 0; i <= n; i++) { mv(i * dx, 0); l(i * dx, h); }
      const span = n * dx;
      for (let j = Math.floor(-span * t / s) - 1; j <= Math.ceil(h / s) + 1; j++) { mv(0, j * s); l(span, j * s + span * t); mv(0, j * s); l(span, j * s - span * t); }
    });
  }
  ctx.restore();
};

// ---------------------------------------------------------------- Cornell notes
const drawCornell: Draw = (ctx, w, h, _p, ink) => {
  const top = 74, cue = 150, sumY = h - 150;
  lines(ctx, alpha(ink, 0.3), 0.45, (m, l) => { for (let y = top + 26; y < sumY; y += 26) { m(cue, y); l(w - 24, y); } for (let y = sumY + 26; y < h - 30; y += 26) { m(24, y); l(w - 24, y); } });
  lines(ctx, alpha(ink, 0.8), 1, (m, l) => { m(24, top); l(w - 24, top); m(cue, top); l(cue, sumY); m(24, sumY); l(w - 24, sumY); });
  text(ctx, "Tema:", 26, 30, ink, "left", 9);
  text(ctx, "Fecha:", w - 130, 30, ink, "left", 9);
  lines(ctx, alpha(ink, 0.5), 0.5, (m, l) => { m(60, 38); l(w - 140, 38); m(w - 96, 38); l(w - 26, 38); });
  text(ctx, "Preguntas / ideas clave", 28, top + 10, ink, "left", 7.5);
  text(ctx, "Apuntes", cue + 6, top + 10, ink, "left", 7.5);
  text(ctx, "Resumen", 28, sumY + 10, ink, "left", 7.5);
};

// ---------------------------------------------------------------- logarithmic
function drawLog(ctx: CanvasRenderingContext2D, w: number, h: number, p: Params, ink: Ink): void {
  const natural = p.base === "e";
  const both = p.axes === "xy";
  const cycles = Number(p.cycles);
  const L = 48, R = w - 26, T = 34, B = h - 42;
  const W = R - L, H = B - T;
  const xcycles = Math.max(1, Math.round((cycles * W) / H));
  /** Positions (0..1 inside a cycle) of the minor lines, with their strength. */
  const inner: Array<[number, number]> = [];
  if (natural) for (let j = 1; j < 10; j++) inner.push([j / 10, j === 5 ? 1 : 0]);
  else for (let v = 2; v <= 9; v++) inner.push([Math.log10(v), v === 5 ? 1 : 0]);
  const label = (c: number) => (natural ? (c === 0 ? "1" : c === 1 ? "e" : `e${sup(c)}`) : c <= 3 ? String(10 ** c) : `10${sup(c)}`);

  for (const [strength, wd, a] of [[0, 0.3, 0.3], [1, 0.5, 0.55]] as const) {
    lines(ctx, alpha(ink, a), wd, (m, l) => {
      for (let c = 0; c < cycles; c++) for (const [f, s] of inner) if (s === strength) { const y = B - ((c + f) / cycles) * H; m(L, y); l(R, y); }
      if (both) for (let c = 0; c < xcycles; c++) for (const [f, s] of inner) if (s === strength) { const x = L + ((c + f) / xcycles) * W; m(x, T); l(x, B); }
      else for (let i = 1; i < 50; i++) if ((i % 5 === 0) === (strength === 1)) { const x = L + (i / 50) * W; m(x, T); l(x, B); }
    });
  }
  lines(ctx, alpha(ink, 0.9), 0.9, (m, l) => {
    for (let c = 0; c <= cycles; c++) { const y = B - (c / cycles) * H; m(L, y); l(R, y); }
    if (both) for (let c = 0; c <= xcycles; c++) { const x = L + (c / xcycles) * W; m(x, T); l(x, B); }
    else for (let i = 0; i <= 10; i++) { const x = L + (i / 10) * W; m(x, T); l(x, B); }
  });
  ctx.lineWidth = 1.2;
  ctx.strokeStyle = ink.strong;
  ctx.strokeRect(L, T, W, H);
  for (let c = 0; c <= cycles; c++) text(ctx, label(c), L - 5, B - (c / cycles) * H, ink, "right", 8);
  if (both) for (let c = 0; c <= xcycles; c++) text(ctx, label(c), L + (c / xcycles) * W, B + 10, ink, "center", 8);
  else for (let i = 0; i <= 10; i++) text(ctx, String(i), L + (i / 10) * W, B + 10, ink, "center", 8);
  text(ctx, natural ? "escala logarítmica natural (ciclo = factor e)" : "escala logarítmica decimal (ciclo = década)", L, T - 12, ink, "left", 7.5);
}

// ---------------------------------------------------------------- catalogue
export const PAPER_KINDS: PaperKind[] = [
  { id: "mm", label: "Milimetrado", fallback: "graph", params: [], draw: drawMm },
  {
    id: "cart", label: "Plano cartesiano", fallback: "graph",
    params: [
      { key: "quad", label: "Ejes", options: [["4", "4 cuadrantes"], ["1", "1.er cuadrante"]] },
      { key: "unit", label: "Unidad", options: [["1", "1 cm"], ["0.5", "0,5 cm"], ["2", "2 cm"]] },
      { key: "sub", label: "Subdivisión", options: [["off", "no"], ["on", "5 por unidad"]] },
      { key: "labels", label: "Números", options: [["on", "sí"], ["off", "no"]] },
    ],
    draw: drawCartesian,
  },
  { id: "trig", label: "Trigonométrico (eje en π)", fallback: "graph", params: [{ key: "grid", label: "Rejilla", options: [["6", "cada π/6 (30°)"], ["4", "cada π/4 (45°)"]] }], draw: drawTrig },
  {
    id: "polar", label: "Polar", fallback: "plain",
    params: [
      { key: "step", label: "Ángulo", options: [["15", "cada 15°"], ["30", "cada 30°"], ["10", "cada 10°"]] },
      { key: "ring", label: "Anillos", options: [["1", "cada 1 cm"], ["0.5", "cada 0,5 cm"]] },
    ],
    draw: drawPolar,
  },
  {
    id: "log", label: "Logarítmico", fallback: "graph",
    params: [
      { key: "base", label: "Base", options: [["10", "10 (decimal)"], ["e", "e (natural)"]] },
      { key: "axes", label: "Tipo", options: [["y", "Semilog (y log)"], ["xy", "Log-log"]] },
      { key: "cycles", label: "Ciclos", options: ["5", "1", "2", "3", "4", "6", "7"].map((c) => [c, c] as [string, string]) },
    ],
    draw: drawLog,
  },
  {
    id: "iso", label: "Isométrico", fallback: "dotted",
    params: [
      { key: "look", label: "Aspecto", options: [["lines", "líneas"], ["dots", "puntos"]] },
      { key: "size", label: "Lado", options: [["1", "1 cm"], ["0.5", "0,5 cm"], ["2", "2 cm"]] },
    ],
    draw: drawIso,
  },
  { id: "cornell", label: "Cornell (apuntes)", fallback: "ruled", params: [], draw: drawCornell },
];

export const paperKind = (id: string): PaperKind | undefined => PAPER_KINDS.find((k) => k.id === id);

export function defaultSpec(kind: string): PaperSpec | undefined {
  const k = paperKind(kind);
  return k ? { kind, params: Object.fromEntries(k.params.map((p) => [p.key, p.options[0]![0]])) } : undefined;
}

/** Parse `kind;key=value;...`. Unknown kinds give undefined; unknown or invalid parameters fall back to defaults. */
export function parseSpec(raw: string | null): PaperSpec | undefined {
  if (!raw) return undefined;
  const [kind, ...rest] = raw.split(";");
  const spec = defaultSpec((kind ?? "").trim());
  if (!spec) return undefined;
  const k = paperKind(spec.kind)!;
  for (const kv of rest) {
    const [key, value] = kv.split("=");
    const param = k.params.find((p) => p.key === key?.trim());
    if (param && param.options.some((o) => o[0] === value?.trim())) spec.params[param.key] = value!.trim();
  }
  return spec;
}

export function formatSpec(s: PaperSpec): string {
  const k = paperKind(s.kind);
  return [s.kind, ...(k?.params ?? []).map((p) => `${p.key}=${s.params[p.key] ?? p.options[0]![0]}`)].join(";");
}

export function withParam(s: PaperSpec, key: string, value: string): PaperSpec {
  return { kind: s.kind, params: { ...s.params, [key]: value } };
}

function inkFor(bg: string): Ink {
  const m = /rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/.exec(bg);
  const [r, g, b] = m ? [Number(m[1]), Number(m[2]), Number(m[3])] : [255, 255, 255];
  const lum = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
  return lum < 0.5 ? { rgb: "170,190,255", strong: "rgba(235,235,255,0.92)", dark: true } : { rgb: "60,100,160", strong: "rgba(25,25,40,0.9)", dark: false };
}

/** Draw the lines of a special paper over an already painted background colour. */
export function drawPaper(ctx: CanvasRenderingContext2D, w: number, h: number, bgCss: string, spec: PaperSpec): void {
  const k = paperKind(spec.kind);
  if (!k) return;
  ctx.save();
  ctx.setLineDash([]);
  ctx.lineCap = "butt";
  ctx.lineJoin = "round";
  k.draw(ctx, w, h, spec.params, inkFor(bgCss));
  ctx.restore();
}
