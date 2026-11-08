import type { Editor } from "./editor";
import { formatSpec, type PaperSpec } from "./paper";
import { attrValue, cssColor, setAttr, type Background, type Page, type XoppDocument } from "./model";
import { fmt8 } from "./serialize";

/** Paper styles. "lined" has a red margin, "ruled" does not (as in Xournal++). */
export const PAPER_STYLES: Array<[style: string, label: string]> = [
  ["plain", "Liso"],
  ["lined", "Rayado (con margen)"],
  ["ruled", "Reglado"],
  ["graph", "Cuadriculado"],
  ["dotted", "Puntos"],
];

/** Page sizes in points (1 pt = 1/72 in). */
export const PAPER_FORMATS: Record<string, [number, number, string]> = {
  A3: [841.889764, 1190.551181, "A3"],
  A4: [595.275591, 841.889764, "A4"],
  A5: [419.527559, 595.275591, "A5"],
  Letter: [612, 792, "Carta (Letter)"],
  Legal: [612, 1008, "Legal"],
};
export const MM_TO_PT = 72 / 25.4;

function cloneBackground(b: Background): Background {
  return { ...b, attrs: b.attrs.map(([k, v]) => [k, v] as [string, string]) };
}

/** A new empty page with the size and paper of `like` (PDF and image backgrounds fall back to plain white). */
export function blankPageLike(like: Page): Page {
  const solid = like.background.type === "solid";
  const bg: Background = solid
    ? cloneBackground(like.background)
    : { attrs: [["type", "solid"], ["color", "#ffffffff"], ["style", "plain"]], type: "solid", color: cssColor("#ffffffff"), style: "plain" };
  return {
    attrs: [["width", fmt8(like.width)], ["height", fmt8(like.height)]],
    width: like.width,
    height: like.height,
    background: bg,
    layers: [{ attrs: [], elements: [] }],
  };
}

export function blankDocument(): XoppDocument {
  const [w, h] = PAPER_FORMATS.A4!;
  const page: Page = {
    attrs: [["width", fmt8(w)], ["height", fmt8(h)]],
    width: w,
    height: h,
    background: { attrs: [["type", "solid"], ["color", "#ffffffff"], ["style", "plain"]], type: "solid", color: cssColor("#ffffffff"), style: "plain" },
    layers: [{ attrs: [], elements: [] }],
  };
  return {
    rootAttrs: [["creator", "xournalpp 1.3.8"], ["fileversion", "4"]],
    title: "Xournal++ document - see https://xournalpp.github.io/",
    previewB64: null,
    pages: [page],
    elementCount: 0,
    droppedChildren: 0,
  };
}

const countElements = (p: Page) => p.layers.reduce((s, l) => s + l.elements.length, 0);

/** Page-level edits of the document. Every operation is undoable. */
export class Journal {
  constructor(private ed: Editor) {}

  private get doc(): XoppDocument { return this.ed.host.doc()!; }

  /** Run a structural change: pages are re-laid out afterwards and the cursor lands on `focus`. */
  private structural(focus: number, doIt: () => void, undoIt: () => void): void {
    doIt();
    this.ed.pushAction({ page: focus, structural: true, undo: undoIt, redo: doIt });
    this.ed.host.rebuildPages(focus);
  }

  /** Insert an empty page at `index`, shaped like the page `like` (default: the neighbour). Returns its index. */
  insertPage(index: number, likeIndex = Math.min(index, this.doc.pages.length - 1)): number {
    const doc = this.doc;
    const idx = Math.max(0, Math.min(index, doc.pages.length));
    const page = blankPageLike(doc.pages[Math.max(0, Math.min(likeIndex, doc.pages.length - 1))]!);
    this.structural(idx, () => { doc.pages.splice(idx, 0, page); }, () => { doc.pages.splice(doc.pages.indexOf(page), 1); });
    return idx;
  }
  newPageAfter(current: number): number { return this.insertPage(current + 1, current); }
  insertBefore(current: number): number { return this.insertPage(current, current); }
  append(): number { return this.insertPage(this.doc.pages.length, this.doc.pages.length - 1); }

  /** Delete a page. The last remaining page cannot be deleted. */
  deletePage(index: number): boolean {
    const doc = this.doc;
    if (doc.pages.length <= 1) { this.ed.onNotice?.("No se puede eliminar la única página del documento"); return false; }
    const page = doc.pages[index]!;
    const n = countElements(page);
    this.structural(
      Math.min(index, doc.pages.length - 2),
      () => { doc.pages.splice(doc.pages.indexOf(page), 1); doc.elementCount -= n; },
      () => { doc.pages.splice(index, 0, page); doc.elementCount += n; },
    );
    return true;
  }

  movePage(index: number, delta: -1 | 1): number {
    const doc = this.doc;
    const to = index + delta;
    if (to < 0 || to >= doc.pages.length) return index;
    const swap = () => { const [p] = doc.pages.splice(index, 1); doc.pages.splice(to, 0, p!); };
    const unswap = () => { const [p] = doc.pages.splice(to, 1); doc.pages.splice(index, 0, p!); };
    this.structural(to, swap, unswap);
    return to;
  }

  private targets(page: number, all: boolean): Page[] { return all ? this.doc.pages.slice() : [this.doc.pages[page]!]; }

  /** Change the paper of one page or all pages: a built-in `style`, or a special paper (`spec`) with its look-alike `style`. */
  setPaperLook(page: number, style: string, spec: PaperSpec | undefined, all: boolean): void {
    const pages = this.targets(page, all).filter((p) => p.background.type === "solid");
    const before = pages.map((p) => ({ p, style: p.background.style, attr: attrValue(p.background.attrs, "style"), spec: p.background.paper, specAttr: attrValue(p.background.attrs, "xlPaper") }));
    const doIt = () => {
      for (const b of before) {
        setAttr(b.p.background.attrs, "style", style);
        b.p.background.style = style;
        if (spec) { setAttr(b.p.background.attrs, "xlPaper", formatSpec(spec)); b.p.background.paper = spec; }
        else { b.p.background.attrs = b.p.background.attrs.filter((a) => a[0] !== "xlPaper"); delete b.p.background.paper; }
      }
    };
    const undoIt = () => {
      for (const b of before) {
        if (b.attr === null) b.p.background.attrs = b.p.background.attrs.filter((a) => a[0] !== "style");
        else setAttr(b.p.background.attrs, "style", b.attr);
        b.p.background.style = b.style;
        b.p.background.attrs = b.p.background.attrs.filter((a) => a[0] !== "xlPaper");
        if (b.specAttr !== null) setAttr(b.p.background.attrs, "xlPaper", b.specAttr);
        if (b.spec) b.p.background.paper = b.spec; else delete b.p.background.paper;
      }
    };
    doIt();
    this.ed.pushAction({ page, repaintAll: true, undo: undoIt, redo: doIt });
    this.ed.host.repaintVisible();
  }

  /** Change the built-in paper style of one page or all pages (this also removes a special paper). */
  setPaper(page: number, style: string, all: boolean): void { this.setPaperLook(page, style, undefined, all); }

  /** Change the paper colour (`#rrggbb`, opaque) of one page or all pages. */
  setBackgroundColor(page: number, rgb: string, all: boolean): void {
    const value = rgb + "ff";
    const pages = this.targets(page, all).filter((p) => p.background.type === "solid");
    const before = pages.map((p) => ({ p, attr: attrValue(p.background.attrs, "color"), css: p.background.color }));
    const doIt = () => { for (const b of before) { setAttr(b.p.background.attrs, "color", value); b.p.background.color = cssColor(value); } };
    const undoIt = () => {
      for (const b of before) {
        if (b.attr === null) b.p.background.attrs = b.p.background.attrs.filter((a) => a[0] !== "color");
        else setAttr(b.p.background.attrs, "color", b.attr);
        b.p.background.color = b.css;
      }
    };
    doIt();
    this.ed.pushAction({ page, repaintAll: true, undo: undoIt, redo: doIt });
    this.ed.host.repaintVisible();
  }

  /** Resize one page or all pages (points). Content keeps its coordinates, anchored at the top-left. */
  setFormat(page: number, width: number, height: number, all: boolean): void {
    const pages = this.targets(page, all);
    const before = pages.map((p) => ({ p, w: p.width, h: p.height }));
    const doIt = () => { for (const b of before) { b.p.width = width; b.p.height = height; } };
    const undoIt = () => { for (const b of before) { b.p.width = b.w; b.p.height = b.h; } };
    this.structural(page, doIt, undoIt);
  }
}
