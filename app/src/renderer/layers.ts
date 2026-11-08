import type { Editor } from "./editor";
import { activeLayer, attrValue, setAttr, type Layer, type Page, type XoppDocument } from "./model";

/** Layer operations on a page. Structure and names are undoable; showing/hiding and the active layer are view state. */
export class Layers {
  constructor(private ed: Editor) {}

  private get doc(): XoppDocument { return this.ed.host.doc()!; }
  private page(i: number): Page { return this.doc.pages[i]!; }

  /** A new empty layer right above the active one; it becomes the active layer. */
  add(pageIdx: number): Layer {
    const p = this.page(pageIdx);
    const at = p.layers.indexOf(activeLayer(p)) + 1;
    const prev = p.active;
    const layer: Layer = { attrs: [], elements: [] };
    const doIt = () => { p.layers.splice(Math.min(at, p.layers.length), 0, layer); p.active = layer; };
    const undoIt = () => { const i = p.layers.indexOf(layer); if (i >= 0) p.layers.splice(i, 1); p.active = prev; };
    doIt();
    this.ed.pushAction({ page: pageIdx, undo: undoIt, redo: doIt });
    this.ed.host.repaint(pageIdx);
    return layer;
  }

  /** Delete a layer with its content. A page keeps at least one layer. */
  remove(pageIdx: number, layer: Layer): boolean {
    const p = this.page(pageIdx);
    if (p.layers.length <= 1) { this.ed.onNotice?.("Una página necesita al menos una capa"); return false; }
    const index = p.layers.indexOf(layer);
    if (index < 0) return false;
    const n = layer.elements.length;
    const prev = p.active;
    const doIt = () => {
      const i = p.layers.indexOf(layer);
      if (i >= 0) p.layers.splice(i, 1);
      this.doc.elementCount -= n;
      if (p.active === layer) p.active = undefined;
    };
    const undoIt = () => { p.layers.splice(Math.min(index, p.layers.length), 0, layer); this.doc.elementCount += n; p.active = prev; };
    this.ed.selection.clear();
    doIt();
    this.ed.pushAction({ page: pageIdx, undo: undoIt, redo: doIt });
    this.ed.host.repaint(pageIdx);
    return true;
  }

  /** Rename a layer. An empty name removes the saved name (the layer shows as "Capa N" again). */
  rename(pageIdx: number, layer: Layer, name: string): void {
    const clean = name.trim();
    const before = attrValue(layer.attrs, "name");
    if ((before ?? "") === clean) return;
    const doIt = () => { if (clean) setAttr(layer.attrs, "name", clean); else layer.attrs = layer.attrs.filter((a) => a[0] !== "name"); };
    const undoIt = () => { if (before !== null) setAttr(layer.attrs, "name", before); else layer.attrs = layer.attrs.filter((a) => a[0] !== "name"); };
    doIt();
    this.ed.pushAction({ page: pageIdx, undo: undoIt, redo: doIt });
  }

  /** Move a layer up (+1, drawn later) or down (-1) in the stacking order. */
  move(pageIdx: number, layer: Layer, delta: -1 | 1): boolean {
    const p = this.page(pageIdx);
    const i = p.layers.indexOf(layer);
    const j = i + delta;
    if (i < 0 || j < 0 || j >= p.layers.length) return false;
    const swap = () => { const a = p.layers.indexOf(layer); const b = a + delta; [p.layers[a], p.layers[b]] = [p.layers[b]!, p.layers[a]!]; };
    const unswap = () => { const a = p.layers.indexOf(layer); const b = a - delta; [p.layers[a], p.layers[b]] = [p.layers[b]!, p.layers[a]!]; };
    swap();
    this.ed.pushAction({ page: pageIdx, undo: unswap, redo: swap });
    this.ed.host.repaint(pageIdx);
    return true;
  }

  setActive(pageIdx: number, layer: Layer): void {
    this.page(pageIdx).active = layer;
    this.ed.host.changed();
  }

  /** Show or hide a layer (not saved in the file). Hidden content cannot be selected or erased. */
  toggleHidden(pageIdx: number, layer: Layer): void {
    layer.hidden = !layer.hidden;
    this.ed.selection.clear();
    this.ed.host.repaint(pageIdx);
    this.ed.host.changed();
  }
}
