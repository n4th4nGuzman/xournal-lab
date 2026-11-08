// node prefs_check.mjs XL_PREFSIO_EXE [real prefs.json to copy] : prefs.json compatibility in both directions, on temp copies only.
// Electron's sanitizePrefs (main.ts) is extracted from the source text and run on the input and on what the C++ app writes; they must agree on every key.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
const exe = process.argv[2];
const real = process.argv[3];
const main = fs.readFileSync(path.resolve(import.meta.dirname, "../../../app/src/main.ts"), "utf8").replaceAll("\r\n", "\n");
const clamp = /const clampNum = [^\n]*\n/.exec(main)[0];
const san = /function sanitizePrefs\(v: unknown\): Prefs \{[\s\S]*?\n\}\n/.exec(main)[0].replace("(v: unknown): Prefs", "(v)").replace("as Record<string, unknown>", "");
const sanitizePrefs = new Function(clamp.replace(/: (unknown|number)/g, "") + san.replace(/\(v as Record<string, unknown>\)/, "v") + "; return sanitizePrefs;")();
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "xl_prefs_"));
const cases = {
  empty: {}, garbage: { fingerDraw: "yes", autosaveMinutes: "abc", theme: 5, palette: "Bad Palette!", effects: [], glowSize: 99, glowIntensity: -1, welcome: 0, plane: 5 },
  extras: { theme: "neon", future: { a: [1, 2, 3] }, other: "x" },
  big: { plane: "x".repeat(700), tablet: "y".repeat(100), sizes: "{}", glowSize: "12.5" },
  nested: { sizes: JSON.stringify({ pen: [1, 2, 3] }), toolbox: "not json", tablet: "{\"brush\":\"pen\"}", textMd: false, glowOn: true },
  xppValid: { xpp: { saveName: "N-%Y", cursorIcon: 3, mouseRight: { tool: "eraser", color: "#ff0000", size: 2 }, deviceClass: { "pen|5": 3 }, stabAvg: 2, minPressure: 0.3 } },
  xppBad: { xpp: { saveName: 5, cursorIcon: "x", mouseRight: "x", deviceClass: [1], stabAvg: 9, minPressure: "high" } },
  edgeNums: { autosaveMinutes: 1000, glowSize: null, glowIntensity: "0.3", autosaveMinutes2: 1 },
};
if (real) cases.real = JSON.parse(fs.readFileSync(real, "utf8"));
const keys = Object.keys(sanitizePrefs({}));
let bad = 0;
for (const [name, v] of Object.entries(cases)) {
  const inp = path.join(dir, name + ".in.json"), out = path.join(dir, name + ".out.json");
  fs.writeFileSync(inp, JSON.stringify(v));
  execFileSync(exe, [inp, out]);
  const a = sanitizePrefs(v), saved = JSON.parse(fs.readFileSync(out, "utf8")), b = sanitizePrefs(saved);
  const sameKeys = keys.every((k) => JSON.stringify(a[k]) === JSON.stringify(b[k]));
  const cppKnown = keys.every((k) => k in saved);
  const extrasKept = Object.keys(v).filter((k) => !keys.includes(k) && k !== "xpp").every((k) => JSON.stringify(saved[k]) === JSON.stringify(v[k]));
  // the additive "xpp" object: sane values survive, bad ones take their default, Electron ignores it (and drops it when it writes the file itself)
  let xppOk = true;
  if (v.xpp !== undefined) {
    const x = saved.xpp ?? {};
    if (name === "xppValid") xppOk = x.saveName === "N-%Y" && x.cursorIcon === 3 && x.mouseRight?.tool === "eraser" && x.mouseRight?.color === "#ff0000" && x.mouseRight?.size === 2 && x.deviceClass?.["pen|5"] === 3 && x.stabAvg === 2 && x.minPressure === 0.3;
    if (name === "xppBad") xppOk = saved.xpp === undefined || (x.saveName === "%F-Nota-%H-%M" && x.cursorIcon === 0 && x.stabAvg === 0 && x.minPressure === 0.05 && x.mouseRight?.tool === "");
    if (name === "xppValid") xppOk = xppOk && !("xpp" in b);
  }
  const ok = sameKeys && cppKnown && extrasKept && xppOk;
  if (!ok) { bad++; for (const k of keys) if (JSON.stringify(a[k]) !== JSON.stringify(b[k])) console.log(`  ${name}.${k}: electron(in)=${JSON.stringify(a[k])?.slice(0, 80)} electron(cpp-saved)=${JSON.stringify(b[k])?.slice(0, 80)}`); }
  console.log(`${ok ? "OK  " : "FAIL"} ${name}: electron(input) == electron(C++ saved): ${sameKeys}; all keys written: ${cppKnown}; unknown keys kept: ${extrasKept}; xpp object: ${xppOk}`);
}
// Electron -> C++: what the Electron app writes (sanitizePrefs of each case, JSON.stringify) must load in C++ and come back out equal.
for (const [name, v] of Object.entries(cases)) {
  const e = sanitizePrefs(v);
  const inp = path.join(dir, name + ".el.json"), out = path.join(dir, name + ".el.out.json");
  fs.writeFileSync(inp, JSON.stringify(e));
  execFileSync(exe, [inp, out]);
  const back = JSON.parse(fs.readFileSync(out, "utf8"));
  const ok = keys.every((k) => JSON.stringify(e[k]) === JSON.stringify(back[k]));
  if (!ok) { bad++; for (const k of keys) if (JSON.stringify(e[k]) !== JSON.stringify(back[k])) console.log(`  ${name}.${k}: electron=${JSON.stringify(e[k])?.slice(0, 80)} cpp=${JSON.stringify(back[k])?.slice(0, 80)}`); }
  console.log(`${ok ? "OK  " : "FAIL"} ${name}: Electron-written prefs survive a C++ load/save unchanged`);
}
fs.rmSync(dir, { recursive: true, force: true });
process.exit(bad ? 1 : 0);
