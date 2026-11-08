import { build } from "esbuild";
import { cpSync, mkdirSync } from "node:fs";

mkdirSync("dist", { recursive: true });
const common = { bundle: true, sourcemap: true, logLevel: "info" };
await build({ ...common, entryPoints: ["src/main.ts"], outfile: "dist/main.js", platform: "node", format: "cjs", external: ["electron"] });
await build({ ...common, entryPoints: ["src/preload.ts"], outfile: "dist/preload.js", platform: "node", format: "cjs", external: ["electron"] });
await build({ ...common, entryPoints: ["src/renderer/app.ts"], outfile: "dist/renderer.js", platform: "browser", format: "iife", target: "chrome130" });
cpSync("src/renderer/index.html", "dist/index.html");
cpSync("src/renderer/styles.css", "dist/styles.css");
// KaTeX is used by a hidden window in the main process (PDF output) and by the renderer (live preview).
cpSync("node_modules/katex/dist/katex.min.css", "dist/katex/katex.min.css");
cpSync("node_modules/katex/dist/katex.min.js", "dist/katex/katex.min.js");
cpSync("node_modules/katex/dist/fonts", "dist/katex/fonts", { recursive: true });
cpSync("src/latexpage.html", "dist/latexpage.html");
cpSync("src/latexpage.js", "dist/latexpage.js");
// pdf.js is loaded on demand as a module, with its worker.
// The "legacy" build: the modern one needs Map.getOrInsertComputed, which this Electron lacks.
cpSync("node_modules/pdfjs-dist/legacy/build/pdf.min.mjs", "dist/pdf.mjs");
cpSync("node_modules/pdfjs-dist/legacy/build/pdf.worker.min.mjs", "dist/pdf.worker.mjs");
