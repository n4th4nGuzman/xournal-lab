// Runs in a hidden window owned by the main process. Renders one formula and
// reports its size in CSS pixels so the caller can print it to a tight PDF page.
window.renderLatex = async function (tex, display, color, px) {
  const el = document.getElementById("f");
  el.style.fontSize = px + "px";
  el.style.color = color;
  katex.render(tex, el, { displayMode: display, throwOnError: true, output: "html", trust: false, strict: "ignore" });
  await document.fonts.ready;
  const r = el.getBoundingClientRect();
  return { w: Math.ceil(r.width), h: Math.ceil(r.height) };
};
