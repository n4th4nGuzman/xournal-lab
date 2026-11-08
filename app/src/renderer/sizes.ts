/** Size presets shown in the toolbar for the pen, the highlighter, the eraser and text, and the user's changes to them. */
import type { Tool } from "./editor";

export type SizeKind = "pen" | "highlighter" | "eraser" | "text";
export type SizePresets = Record<SizeKind, number[]>;

export const DEFAULT_SIZES: SizePresets = {
  pen: [0.5, 0.85, 1.41, 2.26, 3.5, 6],
  highlighter: [2.83, 5.66, 11.32, 18],
  eraser: [4, 8, 16, 32, 56],
  text: [10, 12, 14, 18, 24, 36],
};

export const SIZE_LIMITS: Record<SizeKind, [number, number]> = { pen: [0.1, 60], highlighter: [0.5, 100], eraser: [1, 200], text: [4, 200] };

/** Which presets a tool uses: the pen's serve shapes, planes and figures too; null for tools with no size. */
export function sizeKindOf(tool: Tool): SizeKind | null {
  switch (tool) {
    case "pen": case "shape": case "figure": case "plane": case "space3": case "numline": case "venn": return "pen";
    case "highlighter": return "highlighter";
    case "eraser": case "eraserPart": return "eraser";
    case "text": return "text";
    default: return null;
  }
}

/** Whatever was stored, as valid presets: a list that is not a list of sensible numbers goes back to its default. */
export function sanitizeSizes(raw: unknown): SizePresets {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const out = {} as SizePresets;
  for (const k of Object.keys(DEFAULT_SIZES) as SizeKind[]) {
    const v = o[k];
    const [lo, hi] = SIZE_LIMITS[k];
    const ok = Array.isArray(v) && v.length >= 2 && v.length <= 8 && v.every((n) => typeof n === "number" && Number.isFinite(n) && n >= lo && n <= hi);
    out[k] = ok ? (v as number[]).map((n) => +n.toFixed(2)) : DEFAULT_SIZES[k].slice();
  }
  return out;
}

/** Diameter in pixels of the dot that stands for a size on its button. */
export function dotSize(kind: SizeKind, v: number): number {
  const d = kind === "pen" ? 2 + v * 2.4 : kind === "highlighter" ? 3 + v * 0.9 : kind === "eraser" ? 4 + v * 0.22 : 4 + v * 0.35;
  return Math.round(Math.min(18, Math.max(3, d)));
}
