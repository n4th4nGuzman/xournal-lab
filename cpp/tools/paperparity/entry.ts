// Bundled into a hidden Electron renderer: the Electron app's own paper code, so the C++ papers can be compared pixel by pixel with it.
import { parseXopp, setAttr, cssColor } from "../../../app/src/renderer/model";
import { serializeXopp } from "../../../app/src/renderer/serialize";
import { renderPage } from "../../../app/src/renderer/render";
import { blankDocument } from "../../../app/src/renderer/journal";
import { PAPER_KINDS, defaultSpec, formatSpec, parseSpec } from "../../../app/src/renderer/paper";

(window as any).xlp = {
  kinds: () => PAPER_KINDS.map((k) => ({ id: k.id, label: k.label, fallback: k.fallback, params: k.params })),
  /** One A4 page with the paper `raw` ("x:kind;k=v" or a built-in style) on `color`, serialized as the Electron app writes it. */
  make(value: string, color: string): { xml: string; spec: string | null } {
    const doc = blankDocument();
    const bg = doc.pages[0]!.background;
    setAttr(bg.attrs, "color", color + "ff");
    bg.color = cssColor(color + "ff");
    let spec: string | null = null;
    if (value.startsWith("x:")) {
      const s = parseSpec(value.slice(2))!;
      const k = PAPER_KINDS.find((q) => q.id === s.kind)!;
      bg.paper = s; bg.style = k.fallback;
      setAttr(bg.attrs, "style", k.fallback); setAttr(bg.attrs, "xlPaper", formatSpec(s));
      spec = formatSpec(s);
    } else { bg.style = value; setAttr(bg.attrs, "style", value); }
    return { xml: serializeXopp(doc), spec };
  },
  /** Render page 0 of a document (xml) at `scale` to a PNG data URL. */
  render(xml: string, scale: number): string {
    const doc = parseXopp(xml);
    const page = doc.pages[0]!;
    const cv = document.createElement("canvas");
    cv.width = Math.ceil(page.width * scale); cv.height = Math.ceil(page.height * scale);
    const ctx = cv.getContext("2d")!;
    ctx.scale(scale, scale);
    renderPage(ctx, page, () => undefined);
    return cv.toDataURL("image/png");
  },
  defaults: () => PAPER_KINDS.map((k) => formatSpec(defaultSpec(k.id)!)),
};
