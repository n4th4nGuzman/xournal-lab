/**
 * Toolbar icons: 24x24 line drawings drawn with `currentColor`, so they follow the button's state colours.
 * Each value is the inside of an <svg>; a button asks for one with `data-icon="name"`.
 */
const ICONS: Record<string, string> = {
  open: '<path d="M3 7a2 2 0 012-2h4l2 2h8a2 2 0 012 2v8a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>',
  save: '<path d="M5 3h12l4 4v12a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2z"/><path d="M7 3v6h8V3M7 21v-7h10v7"/>',
  undo: '<path d="M9 14L4 9l5-5"/><path d="M4 9h10a6 6 0 010 12h-3"/>',
  redo: '<path d="M15 14l5-5-5-5"/><path d="M20 9H10a6 6 0 000 12h3"/>',
  cut: '<circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="M20 4L8.1 15.9M14.5 14.5L20 20M8.1 8.1L12 12"/>',
  copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 012-2h9"/>',
  paste: '<rect x="6" y="5" width="12" height="16" rx="2"/><path d="M9 5V3.5h6V5M9.5 11h5M9.5 15h5"/>',
  dup: '<rect x="3" y="3" width="12" height="12" rx="2"/><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M15 12v6M12 15h6"/>',
  thumbs: '<rect x="3" y="4" width="7" height="9" rx="1"/><rect x="3" y="15" width="7" height="5" rx="1"/><path d="M14 5h7M14 10h7M14 15h7M14 20h7"/>',
  layers: '<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 12.5l9 5 9-5M3 16.5l9 5 9-5"/>',
  first: '<path d="M6 5v14M19 6l-9 6 9 6z"/>',
  prev: '<path d="M16 6l-8 6 8 6"/>',
  next: '<path d="M8 6l8 6-8 6"/>',
  last: '<path d="M18 5v14M5 6l9 6-9 6z"/>',
  histBack: '<path d="M9 14L4 9l5-5"/><path d="M4 9h11a5 5 0 015 5v2"/>',
  histFwd: '<path d="M15 14l5-5-5-5"/><path d="M20 9H9a5 5 0 00-5 5v2"/>',
  pageAdd: '<path d="M6 3h8l4 4v14H6z"/><path d="M12 11v6M9 14h6"/>',
  pageBefore: '<path d="M6 8h12v13H6z"/><path d="M12 2v4M10 4h4"/>',
  pageEnd: '<path d="M6 3h12v13H6z"/><path d="M12 18v4M10 20h4"/>',
  pageDel: '<path d="M6 3h8l4 4v14H6z"/><path d="M9.5 11l5 5M14.5 11l-5 5"/>',
  up: '<path d="M12 19V5M6 11l6-6 6 6"/>',
  down: '<path d="M12 5v14M6 13l6 6 6-6"/>',
  zoomOut: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4M8 11h6"/>',
  zoomIn: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4M8 11h6M11 8v6"/>',
  fit: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
  paper: '<path d="M6 3h12v18H6z"/><path d="M6 9h12M6 15h12M12 3v18" opacity=".55"/>',
  theme: '<path d="M12 3a9 9 0 100 18c1.5 0 2-1 1.5-2.2-.6-1.4.2-2.8 1.8-2.8H18a3 3 0 003-3c0-5-4-10-9-10z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7" r="1"/><circle cx="15" cy="7.5" r="1"/>',
  palette: '<circle cx="8" cy="8" r="3.2"/><circle cx="16" cy="8" r="3.2"/><circle cx="12" cy="15.5" r="3.2"/>',
  glow: '<path d="M5 17c3-8 6-8 7-4s4 4 7-5"/><path d="M3 20h18" opacity=".4"/><path d="M12 2v2M4.2 5.2l1.4 1.4M19.8 5.2l-1.4 1.4"/>',
  fill: '<path d="M6 12l6-7 7 7-6 7z"/><path d="M6 12h13M4 21h6"/><path d="M20.5 14.5c1 1.6 1.5 2.5 1.5 3.3a1.5 1.5 0 01-3 0c0-.8.5-1.7 1.5-3.3z"/>',
  fillOpts: '<path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/>',
  plane: '<path d="M5 21V4M3 21h18"/><path d="M5 3l-1.6 2.4M5 3l1.6 2.4M22 21l-2.4-1.6M22 21l-2.4 1.6"/><path d="M10 21v-3M15 21v-3M5 15h3M5 10h3" opacity=".6"/><path d="M7 17c3 0 4-7 7-7s2 3 6 3" opacity=".85"/>',
  planeOpts: '<path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/><path d="M20 12l1.5 1.5L20 15" opacity=".7"/>',
  figure: '<path d="M4 18l5-12 8 4 3 8z"/><circle cx="4" cy="18" r="1.3"/><circle cx="9" cy="6" r="1.3"/><circle cx="17" cy="10" r="1.3"/><circle cx="20" cy="18" r="1.3"/>',
  tablet: '<rect x="3" y="5" width="14" height="13" rx="1.5"/><path d="M17 15l3.5-9.5 1.5 1-5 9z"/><path d="M7 9h6M7 12h4" opacity=".5"/>',
  markdown: '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6 15V9l2.5 3L11 9v6M15.5 9v6m-2.2-2.3 2.2 2.3 2.2-2.3"/>',
  caret: '<path d="M6 9l6 6 6-6"/>',
  numline: '<path d="M3 16h18"/><path d="M5 13v6M12 13v6M19 13v6"/><circle cx="8" cy="7" r="1.8"/><path d="M10 7h5.5"/><circle cx="17.3" cy="7" r="1.8"/>',
  venn: '<circle cx="9" cy="12" r="5.5"/><circle cx="15" cy="12" r="5.5"/>',
  space3: '<path d="M12 14V3M12 14l-8 6M12 14h9"/><path d="M12 3l-1.6 2.4M12 3l1.6 2.4M21 14l-2.4-1.6M21 14l-2.4 1.6M4 20l2.7-.3M4 20l.9-2.6"/><path d="M8 17l4-6 4 3M12 8l5 3" opacity=".5"/>',
  orient: '<rect x="3" y="7" width="12" height="14" rx="1"/><path d="M17 4a4 4 0 014 4M21 8l-2-2M21 8l-2 2"/>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  pen: '<path d="M4 20l1-4L16 5l3 3L8 19z"/><path d="M14 7l3 3M4 20l4-1"/>',
  highlighter: '<path d="M14 4l6 6-8 8H8l-4-4v-4z" /><path d="M9 14l5-5"/><path d="M3 21h8" stroke-width="3" opacity=".55"/>',
  eraser: '<path d="M5 14l8-8 6 6-8 8H7z"/><path d="M9.5 9.5l6 6M10 20h10"/>',
  eraserPart: '<path d="M5 12l7-7 6 6-5 5H8z"/><path d="M3 20h5M11 20h2M16 20h5"/>',
  select: '<rect x="4" y="4" width="16" height="16" stroke-dasharray="3 2.4"/><path d="M12 9v6M9 12h6"/>',
  lasso: '<path d="M12 5c-5 0-8 2-8 5s3 5 8 5 8-2 8-5-3-5-8-5z" stroke-dasharray="3 2.4"/><path d="M10 15c-1 3 0 5 3 5"/>',
  vspace: '<path d="M4 12h16" stroke-dasharray="3 2.4"/><path d="M12 9V3M9 6l3-3 3 3M12 15v6M9 18l3 3 3-3"/>',
  hand: '<path d="M8 13V6.5a1.5 1.5 0 013 0V11M11 10.5V4.5a1.5 1.5 0 013 0V11M14 11V6.5a1.5 1.5 0 013 0V14c0 4-2 7-6 7-3 0-4.5-2-6.5-5l-1.2-2a1.5 1.5 0 012.5-1.6L8 14"/>',
  shape: '<rect x="3" y="11" width="9" height="9"/><circle cx="15.5" cy="8.5" r="5"/>',
  recog: '<path d="M3 19l5.5-9 5.5 9z"/><path d="M17 3l1 2.5L20.5 6.5 18 7.5 17 10l-1-2.5-2.5-1L16 5.5z"/>',
  text: '<path d="M5 7V4.5h14V7M12 4.5V20M9 20h6"/>',
  latex: '<path d="M18 5H6l7 7-7 7h12"/>',
  image: '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="9" cy="10" r="1.6"/><path d="M21 16l-5-5-9 9"/>',
  laser: '<circle cx="12" cy="12" r="2.4" fill="currentColor"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/>',
  front: '<rect x="8" y="8" width="13" height="13" rx="2" fill="currentColor" fill-opacity=".35"/><rect x="3" y="3" width="13" height="13" rx="2" fill="var(--btn-bg, #444)"/>',
  back: '<rect x="3" y="3" width="13" height="13" rx="2" fill="currentColor" fill-opacity=".35"/><rect x="8" y="8" width="13" height="13" rx="2" fill="var(--btn-bg, #444)"/>',
  rotR: '<path d="M21 4v6h-6"/><path d="M20 10a8 8 0 10.8 5.5"/>',
  rotL: '<g transform="translate(24 0) scale(-1 1)"><path d="M21 4v6h-6"/><path d="M20 10a8 8 0 10.8 5.5"/></g>',
  flipH: '<path d="M12 3v18" stroke-dasharray="2.5 2"/><path d="M9 7L3 12l6 5zM15 7l6 5-6 5z"/>',
  flipV: '<g transform="rotate(90 12 12)"><path d="M12 3v18" stroke-dasharray="2.5 2"/><path d="M9 7L3 12l6 5zM15 7l6 5-6 5z"/></g>',
};

const svg = (inner: string) =>
  `<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${inner}</svg>`;

/** Fill every `[data-icon]` button with its drawing; its tooltip doubles as the accessible name. */
export function installIcons(root: ParentNode = document): string[] {
  const missing: string[] = [];
  root.querySelectorAll<HTMLElement>("[data-icon]").forEach((el) => {
    const inner = ICONS[el.dataset.icon!];
    if (!inner) { missing.push(el.dataset.icon!); return; }
    el.innerHTML = svg(inner);
    if (!el.getAttribute("aria-label")) el.setAttribute("aria-label", (el.title || el.dataset.icon!).split(/[.:(]/)[0]!.trim());
  });
  return missing;
}
