/**
 * Settings of the pen and the graphics tablet (XP-Pen Deco Pro MW Gen 2, ACK05 remote and the like): the tip, the pressure curve,
 * what the pen's buttons and eraser end do, and which keyboard combination triggers which action. The XP-Pen driver turns the
 * tablet's and the remote's keys into key combinations (or mouse buttons); here those combinations are bound to actions of the app.
 */
import { BRUSHES, LINEAR_CURVE, isBrushKind, sanitizeCurve, type BrushKind, type PressureCurve } from "./brush";

/** What a pen button or the eraser end does while it is held (a tool) or when it is pressed (undo, redo). */
export type PenAction = "none" | "pen" | "highlighter" | "eraser" | "eraserPart" | "select" | "lasso" | "fill" | "hand" | "toolbox" | "undo" | "redo";

export const PEN_ACTIONS: Array<{ id: PenAction; label: string }> = [
  { id: "none", label: "Nada" },
  { id: "pen", label: "Lápiz" },
  { id: "highlighter", label: "Resaltador" },
  { id: "eraser", label: "Goma (trazo entero)" },
  { id: "eraserPart", label: "Goma (parcial)" },
  { id: "select", label: "Seleccionar" },
  { id: "lasso", label: "Lazo" },
  { id: "fill", label: "Relleno" },
  { id: "hand", label: "Mano (mover el lienzo mientras lo mantienes)" },
  { id: "toolbox", label: "Cuadro de herramientas (al pulsar)" },
  { id: "undo", label: "Deshacer (al pulsar)" },
  { id: "redo", label: "Rehacer (al pulsar)" },
];
const PEN_ACTION_IDS = new Set<string>(PEN_ACTIONS.map((a) => a.id));
export const isPenAction = (v: unknown): v is PenAction => typeof v === "string" && PEN_ACTION_IDS.has(v);

export interface AppAction { id: string; label: string; group: "Herramientas" | "Edición" | "Vista y páginas" | "Lápiz" }

/** Actions a key combination can trigger. The app owns what each one does (see `runAction` in app.ts). */
export const APP_ACTIONS: AppAction[] = [
  { id: "tool.pen", label: "Lápiz", group: "Herramientas" },
  { id: "tool.highlighter", label: "Resaltador", group: "Herramientas" },
  { id: "tool.eraser", label: "Goma (trazo entero)", group: "Herramientas" },
  { id: "tool.eraserPart", label: "Goma (parcial)", group: "Herramientas" },
  { id: "tool.select", label: "Seleccionar", group: "Herramientas" },
  { id: "tool.lasso", label: "Lazo", group: "Herramientas" },
  { id: "tool.hand", label: "Mano", group: "Herramientas" },
  { id: "tool.laser", label: "Puntero láser", group: "Herramientas" },
  { id: "tool.fill", label: "Relleno", group: "Herramientas" },
  { id: "tool.shape", label: "Formas", group: "Herramientas" },
  { id: "tool.figure", label: "Figuras", group: "Herramientas" },
  { id: "tool.numline", label: "Recta real", group: "Herramientas" },
  { id: "tool.venn", label: "Diagrama de conjuntos", group: "Herramientas" },
  { id: "tool.plane", label: "Plano cartesiano", group: "Herramientas" },
  { id: "tool.space3", label: "Espacio 3D", group: "Herramientas" },
  { id: "tool.text", label: "Texto", group: "Herramientas" },
  { id: "tool.latex", label: "LaTeX", group: "Herramientas" },
  { id: "tool.vspace", label: "Espacio vertical", group: "Herramientas" },
  { id: "tool.previous", label: "Volver a la herramienta anterior", group: "Herramientas" },
  { id: "toolbox.show", label: "Mostrar u ocultar el cuadro de herramientas", group: "Herramientas" },
  { id: "edit.undo", label: "Deshacer", group: "Edición" },
  { id: "edit.redo", label: "Rehacer", group: "Edición" },
  { id: "edit.delete", label: "Borrar la selección", group: "Edición" },
  { id: "file.save", label: "Guardar", group: "Edición" },
  { id: "view.zoomIn", label: "Acercar", group: "Vista y páginas" },
  { id: "view.zoomOut", label: "Alejar", group: "Vista y páginas" },
  { id: "view.zoom100", label: "Zoom 100 %", group: "Vista y páginas" },
  { id: "view.fitWidth", label: "Ajustar al ancho", group: "Vista y páginas" },
  { id: "view.fullscreen", label: "Pantalla completa", group: "Vista y páginas" },
  { id: "view.presentation", label: "Modo presentación", group: "Vista y páginas" },
  { id: "nav.prev", label: "Página anterior", group: "Vista y páginas" },
  { id: "nav.next", label: "Página siguiente", group: "Vista y páginas" },
  { id: "journal.newPage", label: "Página nueva", group: "Vista y páginas" },
  { id: "pen.settings", label: "Abrir «Lápiz y tableta»", group: "Lápiz" },
  { id: "pen.sizeUp", label: "Grosor más grande", group: "Lápiz" },
  { id: "pen.sizeDown", label: "Grosor más pequeño", group: "Lápiz" },
  { id: "brush.next", label: "Punta siguiente", group: "Lápiz" },
  { id: "brush.prev", label: "Punta anterior", group: "Lápiz" },
  { id: "color.next", label: "Color siguiente", group: "Lápiz" },
  { id: "color.prev", label: "Color anterior", group: "Lápiz" },
  { id: "glow.toggle", label: "Activar o quitar el brillo", group: "Lápiz" },
];
const APP_ACTION_IDS = new Set(APP_ACTIONS.map((a) => a.id));

export interface TabletSettings {
  brush: BrushKind;
  /** Reference width of the pen. */
  size: number;
  curve: PressureCurve;
  /** First barrel button (the one nearest the tip), second barrel button, and the eraser end of the pen. */
  barrel: PenAction;
  barrel2: PenAction;
  eraser: PenAction;
  /** Key combination (see `comboOf`) to action id. */
  keys: Record<string, string>;
}

export const DEFAULT_TABLET: TabletSettings = { brush: "pen", size: 1.41, curve: LINEAR_CURVE, barrel: "toolbox", barrel2: "hand", eraser: "eraser", keys: {} };

/** Whatever was stored, as valid settings: unknown values fall back to the defaults, bad bindings are dropped. */
export function sanitizeTablet(raw: unknown): TabletSettings {
  const o = typeof raw === "object" && raw !== null ? (raw as Record<string, unknown>) : {};
  const keys: Record<string, string> = {};
  if (typeof o.keys === "object" && o.keys !== null) {
    for (const [combo, id] of Object.entries(o.keys as Record<string, unknown>)) {
      if (typeof id === "string" && APP_ACTION_IDS.has(id) && combo.length > 0 && combo.length < 40 && Object.keys(keys).length < 64) keys[combo] = id;
    }
  }
  return {
    brush: isBrushKind(o.brush) ? o.brush : "pen",
    size: typeof o.size === "number" && Number.isFinite(o.size) ? Math.min(60, Math.max(0.1, o.size)) : DEFAULT_TABLET.size,
    curve: sanitizeCurve(o.curve),
    barrel: isPenAction(o.barrel) ? o.barrel : DEFAULT_TABLET.barrel,
    barrel2: isPenAction(o.barrel2) ? o.barrel2 : DEFAULT_TABLET.barrel2,
    eraser: isPenAction(o.eraser) ? o.eraser : DEFAULT_TABLET.eraser,
    keys,
  };
}

const MODIFIER_KEYS = new Set(["Control", "Shift", "Alt", "Meta", "AltGraph", "CapsLock", "Dead", "Unidentified"]);

/** The combination a key press stands for, like "Ctrl+Shift+F5", or null for a modifier on its own. */
export function comboOf(e: { key: string; code?: string; ctrlKey: boolean; shiftKey: boolean; altKey: boolean; metaKey: boolean }): string | null {
  if (MODIFIER_KEYS.has(e.key)) return null;
  // digits and letters by the physical key, so Shift or AltGr (Ctrl+Alt on some layouts) do not change the name
  const phys = /^(?:Digit(\d)|Key([A-Z]))$/.exec(e.code ?? "");
  const k = phys ? (phys[1] ?? phys[2]!) : e.key === " " ? "Espacio" : e.key.length === 1 ? e.key.toUpperCase() : e.key;
  return [e.ctrlKey ? "Ctrl" : "", e.altKey ? "Alt" : "", e.shiftKey ? "Shift" : "", e.metaKey ? "Meta" : "", k].filter(Boolean).join("+");
}

export const brushName = (k: BrushKind) => BRUSHES.find((b) => b.kind === k)?.name ?? k;

/** The pen action for a pointer event of a pen, or null when it is the plain tip. Reads the button state the browser reports. */
export function penButtonOf(e: { pointerType: string; button: number; buttons: number }): "eraser" | "barrel" | "barrel2" | null {
  if (e.pointerType !== "pen") return null;
  if (e.button === 5 || (e.buttons & 32) !== 0) return "eraser";
  if (e.button === 2 || (e.buttons & 2) !== 0) return "barrel";
  if (e.button === 1 || (e.buttons & 4) !== 0) return "barrel2";
  return null;
}

const mods = (e: { ctrlKey: boolean; shiftKey: boolean; altKey: boolean; metaKey: boolean }) => [e.ctrlKey ? "Ctrl" : "", e.altKey ? "Alt" : "", e.shiftKey ? "Shift" : "", e.metaKey ? "Meta" : ""].filter(Boolean);

/** The combination a wheel turn stands for (the dial of a tablet remote often sends the mouse wheel), like "Ctrl+Rueda arriba". */
export function wheelCombo(e: { deltaY: number; deltaX?: number; ctrlKey: boolean; shiftKey: boolean; altKey: boolean; metaKey: boolean }): string | null {
  const d = e.deltaY !== 0 ? e.deltaY : e.deltaX ?? 0;
  if (d === 0) return null;
  return [...mods(e), d < 0 ? "Rueda arriba" : "Rueda abajo"].join("+");
}

/** The combination for the extra mouse buttons a driver can send (middle, back, forward); the main and right buttons are never bound. */
export function mouseCombo(e: { button: number; ctrlKey: boolean; shiftKey: boolean; altKey: boolean; metaKey: boolean }): string | null {
  const name = e.button === 1 ? "Botón central" : e.button === 3 ? "Botón atrás" : e.button === 4 ? "Botón adelante" : null;
  return name ? [...mods(e), name].join("+") : null;
}
