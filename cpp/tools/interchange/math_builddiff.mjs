// node math_builddiff.mjs A.json B.json NAMES.json : compares two canonical dumps (the Electron parse of a file the C++ builders wrote and one the Electron builders wrote,
// same specs and ids), page by page, and summarises what differs: structure, attributes, points, widths, texts (max numeric delta per kind of difference).
import fs from "node:fs";
const [a, b] = process.argv.slice(2, 4).map((f) => JSON.parse(fs.readFileSync(f, "utf8")));
const names = process.argv[4] ? JSON.parse(fs.readFileSync(process.argv[4], "utf8")) : [];
const tol = 1e-6;
const num = (s) => (typeof s === "string" && s.trim() !== "" && Number.isFinite(Number(s)) ? Number(s) : null);
let total = 0;
a.pages.forEach((pa, pi) => {
  const pb = b.pages[pi];
  const name = names[pi] ?? `page ${pi}`;
  const issues = new Map();
  const note = (k, d = 0) => { const v = issues.get(k) ?? { n: 0, max: 0 }; v.n++; v.max = Math.max(v.max, d); issues.set(k, v); };
  const ea = pa.layers[0].els, eb = pb?.layers[0].els ?? [];
  if (ea.length !== eb.length) note(`element count ${ea.length} vs ${eb.length}`);
  for (let i = 0; i < Math.min(ea.length, eb.length); i++) {
    const x = ea[i], y = eb[i];
    if (x.k !== y.k) { note(`kind at ${i}: ${x.k} vs ${y.k}`); continue; }
    const role = (x.attrs.find(([k]) => k === "xlRole") ?? [0, "?"])[1];
    const ax = Object.fromEntries(x.attrs), ay = Object.fromEntries(y.attrs);
    for (const k of new Set([...Object.keys(ax), ...Object.keys(ay)])) {
      const p = ax[k], q = ay[k];
      if (p === q) continue;
      if (p === undefined || q === undefined) { note(`${x.k} ${role}: attr ${k} only on ${p === undefined ? "Electron" : "C++"}`); continue; }
      const m = num(p), n = num(q);
      if (m !== null && n !== null) { if (Math.abs(m - n) > tol * Math.max(1, Math.abs(m))) note(`${x.k} ${role}: attr ${k} differs`, Math.abs(m - n)); }
      else note(`${x.k} ${role}: attr ${k} "${String(p).slice(0, 40)}" vs "${String(q).slice(0, 40)}"`);
    }
    if (x.k === "stroke") {
      if (x.pts.length !== y.pts.length) note(`stroke ${role}: ${x.pts.length / 2} vs ${y.pts.length / 2} points`);
      else { let d = 0; for (let j = 0; j < x.pts.length; j++) d = Math.max(d, Math.abs(x.pts[j] - y.pts[j])); if (d > tol) note(`stroke ${role}: points differ`, d); }
      if (x.widths.length !== y.widths.length) note(`stroke ${role}: ${x.widths.length} vs ${y.widths.length} widths`);
      else { let d = 0; for (let j = 0; j < x.widths.length; j++) d = Math.max(d, Math.abs(x.widths[j] - y.widths[j])); if (d > tol) note(`stroke ${role}: widths differ`, d); }
    } else if (x.k === "text" && x.text !== y.text) note(`text ${role}: "${x.text}" vs "${y.text}"`);
  }
  total += issues.size;
  if (issues.size) { console.log(`-- ${name}`); for (const [k, v] of issues) console.log(`   ${k}  x${v.n}${v.max ? "  max delta " + v.max.toPrecision(3) : ""}`); }
  else console.log(`-- ${name}: identical`);
});
console.log(`kinds of difference: ${total}`);
