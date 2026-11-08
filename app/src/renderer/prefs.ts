export interface Prefs {
  /** Touch input draws (otherwise fingers only scroll: palm rejection). */
  fingerDraw: boolean;
  /** "Save" on a freshly opened file overwrites it without asking. */
  saveOverOpened: boolean;
  /** Write a recovery copy every N minutes while there are unsaved changes (0 = off). */
  autosaveMinutes: number;
  /** Colour scheme: the plain dark one or the synthwave "neon" one. */
  theme: "classic" | "neon";
  /** Id of the pen colour preset (see palettes.ts). */
  palette: string;
  /** Show the welcome page when the app starts (otherwise it opens a blank document). */
  welcome: boolean;
  /** Glows and blurred shadows: automatic (reduced when the system asks for less motion or transparency), always on, or reduced. */
  effects: "auto" | "full" | "reduced";
  /** Settings of the next Cartesian plane, as JSON text (see plane.ts). */
  plane: string;
  /** Settings of the next 3D space, as JSON text (see space3.ts). */
  space3: string;
  /** Options of the paint-bucket tool as JSON text (see fill.ts). */
  fill: string;
  /** Pen tip, size, pressure curve, pen buttons and key bindings as JSON text (see tablet.ts). */
  tablet: string;
  /** Size presets of the toolbar as JSON text (see sizes.ts). */
  sizes: string;
  /** New texts are markdown. */
  textMd: boolean;
  /** Tool box: whether it is on, which tools it holds (see toolbox.ts). */
  toolbox: string;
  /** Glow for new pen and shape strokes. */
  glowOn: boolean;
  glowSize: number;
  glowIntensity: number;
}

export const DEFAULT_PREFS: Prefs = { fingerDraw: false, saveOverOpened: false, autosaveMinutes: 0, theme: "classic", palette: "classic", effects: "auto", welcome: true, plane: "", space3: "", fill: "", tablet: "", sizes: "", textMd: true, toolbox: "", glowOn: false, glowSize: 8, glowIntensity: 0.8 };

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** The preferences dialog. It reads the current values on open and reports the chosen ones on accept. */
export class PrefsDialog {
  private dlg = $("prefsDlg");
  private finger = $<HTMLInputElement>("prefFinger");
  private over = $<HTMLInputElement>("prefOver");
  private auto = $<HTMLInputElement>("prefAuto");
  private theme = $<HTMLSelectElement>("prefTheme");
  private fx = $<HTMLSelectElement>("prefFx");
  private current: Prefs = { ...DEFAULT_PREFS };

  constructor(private onAccept: (p: Prefs) => void) {
    $("prefOk").addEventListener("click", () => this.accept());
    $("prefCancel").addEventListener("click", () => this.close());
    this.dlg.addEventListener("pointerdown", (e) => { if (e.target === this.dlg) this.close(); });
    this.dlg.addEventListener("keydown", (e) => {
      e.stopPropagation();
      if (e.key === "Escape") this.close();
      else if (e.key === "Enter" && (e.target as HTMLElement).tagName !== "BUTTON") this.accept();
    });
  }

  get isOpen(): boolean { return !this.dlg.hidden; }

  open(current: Prefs): void {
    this.current = current;
    this.finger.checked = current.fingerDraw;
    this.over.checked = current.saveOverOpened;
    this.auto.value = String(current.autosaveMinutes);
    this.theme.value = current.theme;
    this.fx.value = current.effects;
    this.dlg.hidden = false;
    this.finger.focus();
  }

  close(): void { this.dlg.hidden = true; }

  private accept(): void {
    const minutes = Math.min(120, Math.max(0, Math.round(Number(this.auto.value) || 0)));
    this.close();
    this.onAccept({ ...this.current, fingerDraw: this.finger.checked, saveOverOpened: this.over.checked, autosaveMinutes: minutes, theme: this.theme.value === "neon" ? "neon" : "classic", effects: this.fx.value === "full" || this.fx.value === "reduced" ? this.fx.value : "auto" });
  }
}
