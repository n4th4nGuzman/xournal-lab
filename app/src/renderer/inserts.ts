import type { Editor } from "./editor";
import { pointInRect, textBBox } from "./geometry";
import { activeLayer, cssColor, type ImageEl, type Layer, type TextBox } from "./model";
import { fmt8 } from "./serialize";
import { isMarkdown } from "./markdown";

const MAX_IMAGE_SIDE = 2000; // px: larger images are downscaled before PNG encoding
const PX_TO_PT = 0.75; // 96 dpi -> 72 dpi page units

interface Open {
  page: number;
  area: HTMLTextAreaElement;
  layer: Layer;
  existing: { el: TextBox; index: number } | null;
  x: number;
  y: number;
  done: boolean;
  /** The text is markdown: it is drawn formatted and its source is what is edited. */
  md: boolean;
}

export class Inserts {
  private open: Open | null = null;

  constructor(private ed: Editor) {}

  private lastLayer(page: number): Layer {
    return activeLayer(this.ed.host.doc()!.pages[page]!);
  }

  // ---- text -------------------------------------------------------------------
  /** Start editing a new text box at (x, y), or the existing one under that point. */
  openText(page: number, x: number, y: number): void {
    this.commitText();
    const doc = this.ed.host.doc();
    if (!doc) return;
    const p = doc.pages[page]!;
    let found: { el: TextBox; layer: Layer; index: number } | null = null;
    for (let l = p.layers.length - 1; l >= 0 && !found; l--) {
      const layer = p.layers[l]!;
      if (layer.hidden) continue;
      for (let i = layer.elements.length - 1; i >= 0; i--) {
        const el = layer.elements[i]!;
        if (el.kind === "text" && pointInRect(x, y, textBBox(el))) { found = { el, layer, index: i }; break; }
      }
    }
    const zoom = this.ed.host.zoom();
    const size = found?.el.size ?? this.ed.textSize;
    const color = found?.el.color ?? cssColor(this.ed.color + "ff");
    const area = document.createElement("textarea");
    area.className = "textedit";
    area.rows = 1;
    area.spellcheck = false;
    area.value = found?.el.text ?? "";
    const tx = found?.el.x ?? x, ty = found?.el.y ?? y;
    Object.assign(area.style, {
      left: `${tx * zoom}px`, top: `${ty * zoom}px`, font: `${size * zoom}px system-ui, sans-serif`,
      lineHeight: "1.2", color,
    });
    const fit = () => {
      area.style.height = "0";
      area.style.height = `${area.scrollHeight}px`;
      area.style.width = "0";
      area.style.width = `${Math.max(area.scrollWidth + 4, 60)}px`;
    };
    area.addEventListener("input", fit);
    area.addEventListener("blur", () => this.commitText());
    area.addEventListener("keydown", (e) => {
      if (e.key === "Escape") { e.preventDefault(); this.commitText(true); }
      else if (e.key === "Enter" && e.ctrlKey) { e.preventDefault(); this.commitText(); }
      e.stopPropagation(); // keep tool shortcuts and Delete from firing while typing
    });
    const layer = found?.layer ?? this.lastLayer(page);
    this.open = { page, area, layer, existing: found ? { el: found.el, index: found.index } : null, x: tx, y: ty, done: false, md: found ? isMarkdown(found.el) : this.ed.textMarkdown };
    if (found) {
      // Hide the original while editing so it is not drawn twice.
      layer.elements.splice(found.index, 1);
      doc.elementCount--;
      this.ed.host.repaint(page);
    }
    this.ed.host.pageEl(page).appendChild(area);
    this.ed.onTextMd?.(this.open.md);
    fit();
    setTimeout(() => area.focus({ preventScroll: true }), 0);
  }

  get editingText(): boolean { return this.open !== null; }

  /** Whether the text being written (or, with none open, the next one) is markdown. */
  get markdown(): boolean { return this.open ? this.open.md : this.ed.textMarkdown; }

  /** Switch markdown on or off for the open text and for the next ones. */
  setMarkdown(on: boolean): void {
    this.ed.textMarkdown = on;
    if (this.open) this.open.md = on;
    this.ed.onTextMd?.(on);
  }

  /** Finish the open text box. `cancel` keeps an existing text unchanged. */
  commitText(cancel = false): void {
    const o = this.open;
    if (!o || o.done) return;
    o.done = true;
    this.open = null;
    const doc = this.ed.host.doc();
    const text = o.area.value.replace(/\s+$/, "");
    o.area.remove();
    if (!doc) return;
    const { layer, existing } = o;
    if (existing) {
      const before = existing.el.text;
      const mdBefore = isMarkdown(existing.el);
      const mdAfter = cancel ? mdBefore : o.md;
      const restore = cancel || text === before ? before : text;
      if (!cancel && text === "") {
        // Emptied: the text box is deleted.
        this.ed.pushAction({
          page: o.page,
          undo: () => { layer.elements.splice(existing.index, 0, existing.el); doc.elementCount++; },
          redo: () => { const i = layer.elements.indexOf(existing.el); if (i >= 0) { layer.elements.splice(i, 1); doc.elementCount--; } },
        });
      } else {
        const el = existing.el;
        const setMd = (on: boolean) => { el.attrs = el.attrs.filter((a) => a[0] !== "xlMd"); if (on) el.attrs.push(["xlMd", "1"]); };
        el.text = restore;
        setMd(mdAfter);
        layer.elements.splice(existing.index, 0, el);
        doc.elementCount++;
        if (restore !== before || mdAfter !== mdBefore) {
          const after = restore;
          this.ed.pushAction({
            page: o.page,
            undo: () => { el.text = before; setMd(mdBefore); },
            redo: () => { el.text = after; setMd(mdAfter); },
          });
        }
      }
      this.ed.host.repaint(o.page);
      this.ed.host.changed();
      return;
    }
    if (cancel || text === "") return;
    const size = this.ed.textSize;
    const colorAttr = this.ed.color + "ff";
    const t: TextBox = {
      kind: "text",
      attrs: [["font", "Sans"], ["size", fmt8(size)], ["x", fmt8(o.x)], ["y", fmt8(o.y)], ["color", colorAttr], ...(o.md ? [["xlMd", "1"] as [string, string]] : [])],
      text, font: "Sans", size, x: o.x, y: o.y, color: cssColor(colorAttr),
    };
    layer.elements.push(t);
    doc.elementCount++;
    this.ed.host.repaint(o.page);
    this.ed.pushAction({
      page: o.page,
      undo: () => { const i = layer.elements.lastIndexOf(t); if (i >= 0) { layer.elements.splice(i, 1); doc.elementCount--; } },
      redo: () => { layer.elements.push(t); doc.elementCount++; },
    });
  }

  // ---- images -----------------------------------------------------------------
  /** Insert an image (any browser-decodable format) as a PNG on the current page. */
  async insertImage(file: Blob): Promise<boolean> {
    const doc = this.ed.host.doc();
    if (!doc) return false;
    this.commitText();
    let src: ImageBitmap;
    try { src = await createImageBitmap(file); } catch { return false; }
    const srcW = src.width, srcH = src.height; // read before close(): a closed bitmap reports 0
    const scale = Math.min(1, MAX_IMAGE_SIDE / Math.max(srcW, srcH));
    const cv = document.createElement("canvas");
    cv.width = Math.max(1, Math.round(srcW * scale));
    cv.height = Math.max(1, Math.round(srcH * scale));
    cv.getContext("2d")!.drawImage(src, 0, 0, cv.width, cv.height);
    src.close();
    const b64 = cv.toDataURL("image/png").split(",")[1] ?? "";
    const bitmap = await createImageBitmap(cv);

    const pageIdx = this.ed.host.currentPage();
    const page = doc.pages[pageIdx]!;
    const zoom = this.ed.host.zoom();
    const natW = srcW * PX_TO_PT;
    const w = Math.min(natW, page.width * 0.6);
    const h = w * (cv.height / cv.width);
    const pageEl = this.ed.host.pageEl(pageIdx);
    const topVisible = (this.ed.scroller.scrollTop - pageEl.offsetTop) / zoom;
    const y = Math.max(20, Math.min(topVisible + 20, page.height - h - 10));
    const x = (page.width - w) / 2;
    const img: ImageEl = {
      kind: "image",
      attrs: [["left", fmt8(x)], ["top", fmt8(y)], ["right", fmt8(x + w)], ["bottom", fmt8(y + h)]],
      left: x, top: y, right: x + w, bottom: y + h, b64, bitmap,
    };
    const layer = this.lastLayer(pageIdx);
    layer.elements.push(img);
    doc.elementCount++;
    this.ed.host.repaint(pageIdx);
    this.ed.pushAction({
      page: pageIdx,
      undo: () => { const i = layer.elements.lastIndexOf(img); if (i >= 0) { layer.elements.splice(i, 1); doc.elementCount--; } },
      redo: () => { layer.elements.push(img); doc.elementCount++; },
    });
    this.ed.afterCreate(this.ed.tool);
    this.ed.selection.selectOnly(pageIdx, layer, img);
    return true;
  }
}
