// Regenerates sets_golden.json from the TypeScript sources (the reference implementation): node tests/data/gen_sets_golden.mjs
// The C++ test tests/sets_golden.cpp checks that core/{setexpr,numberline,venn} give the same results. Text positions are not compared
// (they depend on font metrics); strokes (points, widths, colours, roles), text strings and the stored xlSet JSON are.
import { createRequire } from "node:module";
import { writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const appDir = path.resolve(here, "../../../app");
const require = createRequire(path.join(appDir, "package.json"));
const esbuild = require("esbuild");
const tmp = mkdtempSync(path.join(tmpdir(), "setsgold-"));
const r = (f) => path.join(appDir, "src/renderer", f).replace(/\\/g, "/");
const entry = path.join(tmp, "entry.ts");
writeFileSync(entry, `
export * from "${r("setexpr.ts")}";
export * from "${r("intervals.ts")}";
export * as NL from "${r("numberline.ts")}";
export * as VN from "${r("venn.ts")}";
`);
const out = path.join(tmp, "bundle.mjs");
await esbuild.build({ entryPoints: [entry], bundle: true, format: "esm", platform: "node", outfile: out, logLevel: "error" });
// textBBox measures with a canvas: a stand-in is enough, text positions are not compared
globalThis.document = { createElement: () => { let size = 10; return { getContext: () => ({ measureText: (t) => ({ width: t.length * 0.5 * size }), set font(v) { size = parseFloat(String(v)); } }) }; } };
const T = await import(pathToFileURL(out).href);

const exprs = [
  "(-2,3] ∪ {5}", "(-2,3] U {5}", "(-2,3] u {5}", "(-2,3] | {5}", "(-2,3] + {5}", "(-2,3] or {5}", "[1,inf)", "[1,∞)", "[1,oo)", "(-inf, oo)", "]1,3[", "[1,3[", "]1,3]",
  "(1,5; 3]", "(1,5; 3,5)", "[-π, π/2]", "[0, 2pi)", "[1/2, 3/2)", "[√2, 3]", "[sqrt(2), 3]", "[√(2), 3]", "{1, 2, 5}", "{1; 2; 2,5}", "∅", "Ø", "{}", "R", "ℝ", "U", "Ω",
  "(-2,3] ∩ [0,5]", "(-2,3] n [0,5]", "(-2,3] & [0,5]", "(-2,3] and [0,5]", "(-2,3] * [0,5]", "[0,5] \\ {2}", "[0,5] - {2}", "[0,5] − {2}", "[0,5] minus {2}", "R \\ {2}",
  "[0,2]^c", "[0,2]'", "[0,2]ᶜ", "[0,2]^{c}", "¬[0,2]", "~[0,2]", "![0,2]", "[0,2]''", "[0,5] Δ [3,8]", "[0,5] xor [3,8]", "[0,5] △ [3,8]", "(0,1) ∪ [1,2]", "(0,1) ∪ (1,2)",
  "A ∩ B", "A", "A'", "A1", "A_2 ∪ B", "AB", "(A ∪ B)'", "A ∩ B' ∪ C", "(A ∩ B) ∪ C'", "A ∖ (B ∪ C)", "A Δ B", "A xor B", "A n B u C", "1/3", "5", "-3", "π", "2π", "3π/2", "inf", "-inf",
  "(1,", "[3,1]", "[1,2] [3,4]", "", "   ", "{1,2", "{1,}", "{a}", "(1,2", "(1 2)", "[1,2", "]1,2", "(", ")", "[", "A ∩", "∩ B", "A ∪ ∪ B", "A @ B", "A B", "1/0", "[1/0, 2]", "(1,2) )", "(A", "((A))", "(A ∪ B", "[1,2] ∪", "x", "5)", "[1;2]", "(1, 2) junk", "1,5; 2", "{1,5; 2,5}",
  "(−∞, π]", "(-2, 3]  ∖ {0}", "[ 1 , 3 ]", "(0,1)ᶜ ∩ [−1,2]", "[1.5, .5]", "[.5, 1.5]", "[1., 2]", "A’", "A′", "A^c^c",
];
const evalCases = exprs.map((s) => {
  const p = T.parseSetExpr(s);
  const o = { src: s };
  if (!p.ok) { o.ok = false; o.pos = p.pos; o.len = p.len; o.msg = p.msg; return o; }
  o.ok = true;
  o.names = T.namesIn(p.expr);
  const e = T.evalReal(p.expr);
  if (e.ok) o.real = T.formatSet(e.value); else o.realErr = e.msg;
  for (const [key, names] of [["reg2", ["A", "B"]], ["reg3", ["A", "B", "C"]]]) {
    const g = T.evalRegions(p.expr, names);
    if (g.ok) o[key] = g.value.map((b) => (b ? "1" : "0")).join(""); else o[key + "Err"] = g.msg;
  }
  return o;
});

const fmt = [0, 1, -1, 0.5, 2.5, -2.5, Math.PI, -Math.PI, Math.PI / 2, 3 * Math.PI / 2, 2 * Math.PI, Math.PI / 3, Math.PI / 4, Math.PI / 6, 5 * Math.PI / 6, 13 * Math.PI, 1 / 3, Math.SQRT2, 1e-7, 123456.789, -0.000001, 100, Infinity, -Infinity]
  .map((v) => ({ v: v === Infinity ? "inf" : v === -Infinity ? "-inf" : v, s: T.formatNumber(v) }));

const attr = (e, k) => (e.attrs.find((a) => a[0] === k) ?? [0, null])[1];
const brief = (els) => els.map((e) => {
  if (e.kind === "stroke") return { k: "s", role: attr(e, "xlRole"), color: attr(e, "color"), width: e.widths[0], pts: e.pts.map((v) => +v.toFixed(5)), attrs: e.attrs.filter((a) => !["tool", "color", "width", "capStyle", "xlPlane", "xlRole", "xlSet"].includes(a[0])).map((a) => a.join("=")), xlSet: attr(e, "xlSet") };
  return { k: "t", role: attr(e, "xlRole"), text: e.text, size: e.size };
});
const style = { color: "#0044cc", width: 1.41 };
const base = T.NL.defaultLine([100, 150], [500, 150]);
const L = (over) => ({ ...base, ...over, rows: over.rows ?? [{ expr: "", label: "" }] });
const lineSpecs = [];
for (const rows of [[""], ["(-2, 3] ∪ {4} ∪ [4.5, inf)"], ["R \\ {2}"], ["(1,3)"], ["[1,3]"], ["[1,3)"], ["(1,"], ["(-2,3]", "[0,5]"], ["[-π, π/2]", "(-inf, 0) ∪ {1}", "∅"], ["(-9,-8) ∪ [7, 9]"], ["{0}"]])
  for (const marks of ["circles", "brackets"]) lineSpecs.push(L({ rows: rows.map((e, i) => ({ expr: e, label: i === 1 ? "B" : "" })), marks }));
lineSpecs.push(L({ min: 0, max: 10, step: 2, every: 2 }), L({ min: 0, max: 10, step: 0.001 }), L({ arrows: false, rows: [{ expr: "[1,inf)", label: "x" }] }), L({ ticks: false, numbers: false, rows: [{ expr: "(-inf,2]", label: "" }] }), L({ pts: [100, 150, 400, 300], min: -3, max: 7, rows: [{ expr: "[0,2) ∪ (2,5]", label: "" }] }));
const lines = lineSpecs.map((raw) => ({ raw, spec: T.NL.sanitizeLine(raw), els: brief(T.NL.buildLine("plTEST", raw, { ...style })) }));
const sanitize = [null, 5, "x", {}, { pts: [1, 2, 3], min: 4, max: 2, step: 0, every: 99.5, rows: [{ expr: 5 }, { expr: "x".repeat(300), label: "y".repeat(50) }, 3, 4, 5], ticks: false, marks: "brackets" }, { pts: [1e9, "a", 3, 4], min: "no", max: 1e12 }]
  .map((raw) => ({ raw, spec: T.NL.sanitizeLine(raw) }));

const v2 = (over) => ({ ...T.VN.defaultVenn(2, [60, 300], [520, 560]), ...over });
const vennSpecs = [];
for (const e of ["", "A ∩ B", "A ∪ B", "A ∖ B", "B ∖ A", "A'", "(A ∪ B)'", "A Δ B", "A' ∩ B'", "A ∩ D"]) vennSpecs.push(v2({ expr: e }));
vennSpecs.push(v2({ expr: "A ∩ B", hatch: "dense" }));
vennSpecs.push(v2({ expr: "A ∪ B", lists: ["1, 2", "2; 3, 4", "9"], universe: false }));
for (const e of ["(A ∩ B) ∪ C'", "A ∖ (B ∪ C)", "A ∩ B ∩ C", "A Δ B Δ C"]) vennSpecs.push({ ...T.VN.defaultVenn(3, [60, 300], [520, 600]), expr: e, lists: ["1", "2", "3", "4"] });
const vennOut = vennSpecs.map((raw) => {
  const spec = T.VN.sanitizeVenn(raw);
  const sh = T.VN.shadedRegions(spec);
  return { raw, spec, els: brief(T.VN.buildVenn("plTEST", raw, { ...style })), shaded: sh.shaded ? sh.shaded.map((b) => (b ? "1" : "0")).join("") : null, error: sh.error, elements: T.VN.elementsOf(spec), placed: T.VN.placeElements(spec).map((p) => ({ label: p.label, at: p.at.map((v) => +v.toFixed(5)) })) };
});
const sanVenn = [null, {}, { k: 3, pts: [1, 2, 3], names: ["Q", "bad name", 5, "X"], lists: [1, "a"] }, { k: 3, names: ["ab_1", "a b", "ñ"], expr: "z".repeat(300), universe: false, hatch: "dense", lists: ["x".repeat(400)] }]
  .map((raw) => ({ raw, spec: T.VN.sanitizeVenn(raw) }));

const lineMove = [[0, [90, 160], false], [1, [620, 150], false], [1, [400, 300], true], [0, [495, 150], false], [1, [300, 190], true]]
  .map(([i, p, shift]) => ({ i, p, shift, spec: T.NL.moveLineAnchor(base, i, p, shift) }));
const v0 = T.VN.defaultVenn(2, [60, 300], [520, 560]);
const vennMove = [[0, [50, 290]], [1, [100, 320]], [1, [530, 570]], [2, [250, 420]], [3, [330, 440]], [3, [250, 425]], [4, [300, 400]], [5, [300, 500]]]
  .map(([i, p]) => ({ i, p, spec: T.VN.moveVennAnchor(v0, i, p, false) }));

writeFileSync(path.join(here, "sets_golden.json"), JSON.stringify({ evalCases, fmt, lines, sanitize, venn: vennOut, sanVenn, lineMove, vennMove, base, v0 }));
console.log("ok", evalCases.length, lines.length, vennOut.length);
