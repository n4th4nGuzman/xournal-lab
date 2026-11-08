// Bundles math_entry.ts (the Electron app's generated-object code) for the renderer, with the esbuild of the Electron app (read only).
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import path from "node:path";
const here = path.dirname(fileURLToPath(import.meta.url));
const { build } = createRequire(path.resolve(here, "../../../app/package.json"))("esbuild");
await build({ entryPoints: [path.join(here, "math_entry.ts")], bundle: true, outfile: path.join(here, "math_bundle.js"), platform: "browser", format: "iife", target: "chrome130", logLevel: "error" });
console.log("bundled");
