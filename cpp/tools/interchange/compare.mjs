// node compare.mjs A.json B.json : deep comparison of two canonical dumps; numbers (also numeric attribute strings) match within 1e-6 relative.
import fs from "node:fs";
const [a, b] = process.argv.slice(2).map((f) => JSON.parse(fs.readFileSync(f, "utf8")));
let diffs = 0;
const num = (s) => (typeof s === "string" && s.trim() !== "" && Number.isFinite(Number(s)) ? Number(s) : null);
function eq(x, y, where) {
  if (diffs > 15) return;
  if (typeof x === "number" && typeof y === "number") { if (Math.abs(x - y) <= 1e-6 * Math.max(1, Math.abs(x), Math.abs(y))) return; }
  else if (typeof x === "string" && typeof y === "string") { if (x === y) return; const p = num(x), q = num(y); if (p !== null && q !== null && Math.abs(p - q) <= 1e-6 * Math.max(1, Math.abs(p), Math.abs(q))) return; }
  else if (Array.isArray(x) && Array.isArray(y)) {
    if (x.length !== y.length) { diffs++; console.log(`DIFF ${where}: length ${x.length} vs ${y.length}`); return; }
    x.forEach((v, i) => eq(v, y[i], `${where}[${i}]`));
    return;
  } else if (x && y && typeof x === "object" && typeof y === "object") {
    for (const k of new Set([...Object.keys(x), ...Object.keys(y)])) { if (k === "skipped" || k === "dropped") continue; eq(x[k], y[k], `${where}.${k}`); }
    return;
  }
  if (x === y) return;
  diffs++;
  console.log(`DIFF ${where}: ${JSON.stringify(x)?.slice(0, 120)} vs ${JSON.stringify(y)?.slice(0, 120)}`);
}
eq(a, b, "doc");
console.log(diffs ? `${diffs} difference(s)` : "IDENTICAL");
process.exit(diffs ? 1 : 0);
