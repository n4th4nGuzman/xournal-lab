import fs from "node:fs";
const kinds = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const out = [];
const colors = [["w", "#ffffff"], ["d", "#120a26"]];
for (const s of ["plain", "lined", "ruled", "graph", "dotted"]) for (const [c, col] of colors) out.push({ name: `${s}_${c}`, value: s, color: col });
for (const k of kinds) {
  const opts = k.params.map((p) => p.options.map((o) => [p.key, o[0]]));
  // default, plus every option of every parameter changed alone, plus the full cartesian product when small
  const combos = [[]];
  let all = [[]];
  for (const o of opts) all = all.flatMap((a) => o.map((x) => [...a, x]));
  const seen = new Set();
  for (const combo of all) {
    const v = `x:${k.id}` + combo.map(([a, b]) => `;${a}=${b}`).join("");
    if (seen.has(v)) continue; seen.add(v);
    const name = `${k.id}${combo.length ? "_" + combo.map(([a, b]) => `${a}-${b}`).join("_") : ""}`;
    out.push({ name, value: v, color: "#ffffff" });
    if (combo.length === 0 || combo.every(([a, b], i) => b === opts[i][0][1])) out.push({ name: name + "_d", value: v, color: "#120a26" });
  }
}
fs.writeFileSync(process.argv[3], JSON.stringify(out));
console.log(out.length);
