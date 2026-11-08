import { contextBridge, ipcRenderer, webUtils } from "electron";

contextBridge.exposeInMainWorld("xournal", {
  openDialog: (dir?: string): Promise<string | null> => ipcRenderer.invoke("dialog:open", dir),
  readXopp: (file: string): Promise<{ xml: string; bytes: number; name: string; dir: string }> => ipcRenderer.invoke("file:read", file),
  saveXopp: (
    xml: string, suggested: string, dir: string | null, mode: "save" | "saveAs" | "copy", target: string | null,
  ): Promise<{ path: string; bytes: number } | null> => ipcRenderer.invoke("file:save", xml, suggested, dir, mode, target),
  pdfBegin: (suggested: string, dir: string | null, testPath?: string): Promise<string | null> => ipcRenderer.invoke("pdf:begin", suggested, dir, testPath),
  pdfPage: (index: number, w: number, h: number, pw: number, ph: number, jpg: Uint8Array): Promise<boolean> => ipcRenderer.invoke("pdf:page", index, w, h, pw, ph, jpg),
  pdfFinish: (): Promise<{ ok: true; path: string; bytes: number } | { ok: false; error: string }> => ipcRenderer.invoke("pdf:finish"),
  pdfAbort: (): Promise<void> => ipcRenderer.invoke("pdf:abort"),
  cli: (): Promise<unknown> => ipcRenderer.invoke("app:cli"),
  ready: (stats: unknown): Promise<void> => ipcRenderer.invoke("app:ready", stats),
  setDirty: (d: boolean): void => ipcRenderer.send("app:dirty", d),
  addRecent: (file: string): void => ipcRenderer.send("app:addRecent", file),
  menuGet: (): Promise<unknown[]> => ipcRenderer.invoke("menu:get"),
  menuClick: (path: number[]): Promise<boolean> => ipcRenderer.invoke("menu:click", path),
  onMenuChanged: (cb: () => void): void => { ipcRenderer.on("menu:changed", () => cb()); },
  setTitleBar: (color: string, symbolColor: string): void => ipcRenderer.send("app:titleBar", color, symbolColor),
  recentList: (): Promise<Array<{ path: string; name: string; dir: string }>> => ipcRenderer.invoke("recent:list"),
  recentClear: (): Promise<void> => ipcRenderer.invoke("recent:clear"),
  setFullscreen: (on: boolean): void => ipcRenderer.send("app:setFullscreen", on),
  setMenuBar: (on: boolean): void => ipcRenderer.send("app:setMenuBar", on),
  /** Menu commands sent by the main process. */
  onCommand: (handler: (id: string, arg: unknown) => void): void => {
    ipcRenderer.on("cmd", (_e, id: string, arg: unknown) => handler(id, arg));
  },
  latexPdf: (req: { latex: string; display: boolean; color: string; px: number }): Promise<
    { ok: true; pdfB64: string; widthPt: number; heightPt: number } | { ok: false; error: string }
  > => ipcRenderer.invoke("latex:pdf", req),
  getPrefs: (): Promise<{ fingerDraw: boolean; saveOverOpened: boolean; autosaveMinutes: number; theme: "classic" | "neon"; palette: string; effects: "auto" | "full" | "reduced"; welcome: boolean; plane: string; space3: string; fill: string; glowOn: boolean; glowSize: number; glowIntensity: number; autosaveDir: string }> => ipcRenderer.invoke("prefs:get"),
  setPrefs: (p: { fingerDraw?: boolean; saveOverOpened?: boolean; autosaveMinutes?: number; theme?: "classic" | "neon"; palette?: string; effects?: "auto" | "full" | "reduced"; welcome?: boolean; plane?: string; space3?: string; fill?: string; tablet?: string; sizes?: string; textMd?: boolean; toolbox?: string; glowOn?: boolean; glowSize?: number; glowIntensity?: number }): Promise<unknown> => ipcRenderer.invoke("prefs:set", p),
  autosave: (xml: string, name: string): Promise<string> => ipcRenderer.invoke("autosave:write", xml, name),
  testMenuClick: (labels: string[]): Promise<boolean> => ipcRenderer.invoke("test:menuClick", labels),
  pathForFile: (f: File): string => webUtils.getPathForFile(f),
});
