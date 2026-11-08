/**
 * Markdown in a text box. The box keeps the markdown source as its text (so it can always be edited, and other programs see
 * the plain text); this module turns the source into lines of styled runs, which the page draws and measures.
 * Supported: headings (# to ######), **bold**, *italic*, ~~strike~~, `code`, fenced code blocks, bullet / numbered / task lists
 * (nested by indentation), > quotes, --- rules and [links](url). Lines are not wrapped: a paragraph is as wide as it was typed.
 */

export interface MdRun { text: string; bold: boolean; italic: boolean; strike: boolean; underline: boolean; mono: boolean; link: boolean }
export interface MdLine {
  runs: MdRun[];
  /** Font size of the line, in page units. */
  size: number;
  /** Horizontal start of the text and, for list items, of the marker before it. */
  x: number;
  marker?: { text: string; x: number };
  quote: boolean;
  code: boolean;
  rule: boolean;
  /** Space above the line, before it. */
  gap: number;
}

const HEADING = [2, 1.6, 1.35, 1.15, 1.05, 1];

const run = (text: string, over: Partial<MdRun> = {}): MdRun => ({ text, bold: false, italic: false, strike: false, underline: false, mono: false, link: false, ...over });

/** Inline markup of one line into runs. An opening mark with no closing one stays as typed. */
export function inlineRuns(src: string, base: Partial<MdRun> = {}): MdRun[] {
  const out: MdRun[] = [];
  let buf = "";
  const st = { bold: !!base.bold, italic: false, strike: false };
  const flush = () => { if (buf) { out.push(run(buf, { ...base, ...st })); buf = ""; } };
  const closes = (mark: string, from: number) => src.indexOf(mark, from) >= 0;
  const wordChar = (c: string | undefined) => !!c && /[\p{L}\p{N}]/u.test(c);
  let i = 0;
  while (i < src.length) {
    const c = src[i]!;
    if (c === "\\" && i + 1 < src.length && /[\\`*_~[\]()#>+\-.!]/.test(src[i + 1]!)) { buf += src[i + 1]; i += 2; continue; }
    if (c === "`") {
      const end = src.indexOf("`", i + 1);
      if (end > i) { flush(); out.push(run(src.slice(i + 1, end), { ...base, mono: true })); i = end + 1; continue; }
    }
    if (c === "[") {
      const m = /^\[([^\]]+)\]\(([^)\s]*)\)/.exec(src.slice(i));
      if (m) { flush(); out.push(...inlineRuns(m[1]!, { ...base, ...st, underline: true, link: true })); i += m[0].length; continue; }
    }
    const two = src.slice(i, i + 2);
    // "__" may not OPEN inside a word (snake__case) but must be able to CLOSE after one ("__bold__").
    if (two === "__" && !st.bold && wordChar(src[i - 1])) { buf += two; i += 2; continue; } // snake__case: neither bold nor two italics
    if (two === "**" || two === "__") {
      const open = !st.bold;
      if (!open || closes(two, i + 2)) { flush(); st.bold = open; i += 2; continue; }
    }
    if (two === "~~" && (st.strike || closes("~~", i + 2))) { flush(); st.strike = !st.strike; i += 2; continue; }
    if ((c === "*" || c === "_") && two !== "**" && two !== "__") {
      const intraWord = c === "_" && wordChar(src[i - 1]) && wordChar(src[i + 1]);
      if (!intraWord && (st.italic || (src[i + 1] !== " " && closes(c, i + 1)))) { flush(); st.italic = !st.italic; i += 1; continue; }
    }
    buf += c;
    i++;
  }
  flush();
  return out;
}

/** The lines of a markdown source, as they are laid out (without measuring: widths come when drawing). */
export function parseMarkdown(src: string, size: number): MdLine[] {
  const lines: MdLine[] = [];
  const raw = src.replace(/\r\n?/g, "\n").split("\n");
  let fence = false;
  const TAB = size * 1.3;
  for (let n = 0; n < raw.length; n++) {
    const line = raw[n]!;
    if (/^\s*```/.test(line)) { fence = !fence; continue; }
    if (fence) { lines.push({ runs: [run(line, { mono: true })], size: size * 0.92, x: 0, quote: false, code: true, rule: false, gap: 0 }); continue; }
    if (line.trim() === "") { lines.push({ runs: [], size, x: 0, quote: false, code: false, rule: false, gap: -size * 0.55 }); continue; } // a blank line is half a line
    if (/^\s*([-*_])(\s*\1){2,}\s*$/.test(line)) { lines.push({ runs: [], size, x: 0, quote: false, code: false, rule: true, gap: 0 }); continue; }
    let m = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line);
    if (m) {
      const k = HEADING[m[1]!.length - 1]!;
      lines.push({ runs: inlineRuns(m[2]!, { bold: true }), size: size * k, x: 0, quote: false, code: false, rule: false, gap: lines.length ? size * 0.25 * k : 0 });
      continue;
    }
    m = /^(\s*)>\s?(.*)$/.exec(line);
    if (m) { lines.push({ runs: inlineRuns(m[2]!, { italic: false }), size, x: TAB * 0.6, quote: true, code: false, rule: false, gap: 0 }); continue; }
    m = /^(\s*)([-*+])\s+(?:\[([ xX])\]\s+)?(.*)$/.exec(line);
    if (m) {
      const level = Math.floor(m[1]!.replace(/\t/g, "  ").length / 2);
      const mark = m[3] !== undefined ? (m[3] === " " ? "☐" : "☑") : ["•", "◦", "▪"][level % 3]!;
      lines.push({ runs: inlineRuns(m[4]!), size, x: TAB * (level + 1), marker: { text: mark, x: TAB * level + TAB * 0.2 }, quote: false, code: false, rule: false, gap: 0 });
      continue;
    }
    m = /^(\s*)(\d+)[.)]\s+(.*)$/.exec(line);
    if (m) {
      const level = Math.floor(m[1]!.replace(/\t/g, "  ").length / 2);
      lines.push({ runs: inlineRuns(m[3]!), size, x: TAB * (level + 1), marker: { text: `${m[2]}.`, x: TAB * level + TAB * 0.1 }, quote: false, code: false, rule: false, gap: 0 });
      continue;
    }
    lines.push({ runs: inlineRuns(line.trimEnd()), size, x: 0, quote: false, code: false, rule: false, gap: 0 });
  }
  return lines;
}

export const isMarkdown = (t: { attrs: Array<[string, string]> }): boolean => t.attrs.some((a) => a[0] === "xlMd" && a[1] === "1");

const FAMILY = (font: string) => (font === "Sans" ? "system-ui, sans-serif" : `"${font}", sans-serif`);
const MONO = "Consolas, 'Cascadia Mono', ui-monospace, monospace";
export const runFont = (r: MdRun, size: number, font: string) => `${r.italic ? "italic " : ""}${r.bold ? "bold " : ""}${size}px ${r.mono ? MONO : FAMILY(font)}`;

export interface MdLayoutLine { line: MdLine; y: number; h: number; widths: number[]; width: number }
export interface MdLayout { lines: MdLayoutLine[]; width: number; height: number }

const LINE = 1.2;
let measureCtx: CanvasRenderingContext2D | null = null;
const cache = new WeakMap<object, { key: string; layout: MdLayout }>();

/** Lines placed one under the other, with measured widths. Cached per text box until its text, size or font change. */
export function layoutMarkdown(owner: { text: string; size: number; font: string }): MdLayout {
  const key = `${owner.size}|${owner.font}|${owner.text}`;
  const hit = cache.get(owner);
  if (hit && hit.key === key) return hit.layout;
  measureCtx ??= document.createElement("canvas").getContext("2d")!;
  const ctx = measureCtx;
  const out: MdLayoutLine[] = [];
  let y = 0, width = 0;
  for (const line of parseMarkdown(owner.text, owner.size)) {
    const h = line.size * LINE;
    y += Math.max(0, line.gap);
    const widths = line.runs.map((r) => { ctx.font = runFont(r, line.size, owner.font); return ctx.measureText(r.text).width; });
    const w = widths.reduce((a, b) => a + b, 0);
    const lineH = line.gap < 0 ? h + line.gap : h; // a blank line: half a line
    out.push({ line, y, h: lineH, widths, width: w });
    width = Math.max(width, line.x + w + (line.code ? owner.size * 0.4 : 0));
    y += lineH;
  }
  const layout: MdLayout = { lines: out, width: Math.max(width, owner.size), height: Math.max(y, owner.size * LINE) };
  cache.set(owner, { key, layout });
  return layout;
}

/** Draw a markdown text box with its top left corner at (x, y). */
export function drawMarkdown(ctx: CanvasRenderingContext2D, owner: { text: string; size: number; font: string; x: number; y: number; color: string }): void {
  const lay = layoutMarkdown(owner);
  ctx.textBaseline = "alphabetic";
  for (const l of lay.lines) {
    const { line } = l;
    const top = owner.y + l.y;
    const base = top + l.h * 0.5 + line.size * 0.32; // the baseline: the middle of the line plus about a third of the type size
    if (line.code) {
      ctx.fillStyle = "rgba(128,128,128,0.18)";
      ctx.fillRect(owner.x - owner.size * 0.15, top, lay.width + owner.size * 0.3, l.h);
    }
    if (line.rule) {
      ctx.strokeStyle = owner.color;
      ctx.globalAlpha = 0.6;
      ctx.lineWidth = Math.max(0.6, owner.size * 0.06);
      ctx.beginPath();
      ctx.moveTo(owner.x, top + l.h / 2);
      ctx.lineTo(owner.x + Math.max(lay.width, owner.size * 6), top + l.h / 2);
      ctx.stroke();
      ctx.globalAlpha = 1;
      continue;
    }
    if (line.quote) {
      ctx.fillStyle = owner.color;
      ctx.globalAlpha = 0.45;
      ctx.fillRect(owner.x + line.x - owner.size * 0.7, top, owner.size * 0.18, l.h);
      ctx.globalAlpha = 1;
    }
    if (line.marker) {
      ctx.font = `${line.size}px ${FAMILY(owner.font)}`;
      ctx.fillStyle = owner.color;
      ctx.fillText(line.marker.text, owner.x + line.marker.x, base);
    }
    let x = owner.x + line.x;
    line.runs.forEach((r, i) => {
      const w = l.widths[i]!;
      ctx.font = runFont(r, line.size, owner.font);
      ctx.fillStyle = r.link ? "#2f6fd0" : owner.color;
      if (line.quote) ctx.globalAlpha = 0.8;
      ctx.fillText(r.text, x, base);
      ctx.globalAlpha = 1;
      if (r.strike || r.underline) {
        ctx.strokeStyle = ctx.fillStyle;
        ctx.lineWidth = Math.max(0.5, line.size * 0.06);
        ctx.beginPath();
        const yy = r.strike ? base - line.size * 0.3 : base + line.size * 0.12;
        ctx.moveTo(x, yy);
        ctx.lineTo(x + w, yy);
        ctx.stroke();
      }
      x += w;
    });
  }
}

/** Size of a markdown text box: [width, height] in page units. */
export function markdownSize(owner: { text: string; size: number; font: string }): [number, number] {
  const l = layoutMarkdown(owner);
  return [l.width, l.height];
}
