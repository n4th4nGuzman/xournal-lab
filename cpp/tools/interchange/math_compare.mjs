// node math_compare.mjs DIR : C++ -> Electron. The Electron readers must recover, from the file the C++ app saved, the same kind and control points the C++ readers
// reported (cpp.xopp.probe.json vs cpp.electron.json), and Electron's repair of label-less objects must find what C++ found (cpp.repair.json).
import fs from "node:fs";
const dir = process.argv[2];
const J = (f) => JSON.parse(fs.readFileSync(`${dir}/${f}`, "utf8"));
const cpp = J("cpp.xopp.probe.json"), el = J("cpp.electron.json"), rep = J("cpp.repair.json");
let bad = 0, ok = 0;
const say = (good, msg) => { if (good) ok++; else { bad++; console.log("FAIL " + msg); } };
say(cpp.length === el.length, `object count cpp ${cpp.length} vs electron ${el.length}`);
for (const c of cpp) {
  const e = el.find((x) => x.page === c.page);
  if (!e) { say(false, `${c.id}: Electron found no object on page ${c.page}`); continue; }
  say(e.kind === c.kind, `${c.id}: kind cpp ${c.kind} vs electron ${e.kind}`);
  let worst = 0;
  const same = e.pts.length === c.pts.length;
  if (same) for (let i = 0; i < c.pts.length; i++) worst = Math.max(worst, Math.abs(e.pts[i] - c.pts[i]));
  say(same && worst < 0.01, `${c.id}: control points ${c.pts.length} vs ${e.pts.length} numbers, worst ${worst}`);
  say(e.textsWithoutPlace === 0, `${c.id}: Electron sees every label with a position`);
}
const withText = rep.before.filter((b) => ["plane", "space3", "figure"].includes(b.kind) && b.texts > 0).length;
say(rep.repaired === withText, `Electron repair rebuilt ${rep.repaired} of ${withText} label-less objects`);
say(rep.againZero, "Electron repair is idempotent");
say(rep.after.every((a) => a.textsWithoutPlace === 0), "after repair no label is left without a position");
for (const b of rep.before) {
  const a = rep.after.find((x) => x.page === b.page);
  const c = cpp.find((x) => x.page === b.page);
  let worst = 0;
  if (a && c && a.pts.length === c.pts.length) for (let i = 0; i < a.pts.length; i++) worst = Math.max(worst, Math.abs(a.pts[i] - c.pts[i]));
  else worst = 1e9;
  say(worst < 0.01, `${c?.id}: after Electron's repair it reads the same control points (worst ${worst})`);
}
console.log(`math interchange C++ -> Electron: ${ok} ok, ${bad} failed`);
process.exit(bad ? 1 : 0);
