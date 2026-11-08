// Bundles entry.ts (the Electron app's parser/serializer) for the renderer. Uses the esbuild of the Electron app's node_modules (read only).
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import path from "node:path";
const here = path.dirname(fileURLToPath(import.meta.url));
const { build } = createRequire(path.resolve(here, "../../../app/package.json"))("esbuild");
await build({ entryPoints: [path.join(here, "entry.ts")], bundle: true, outfile: path.join(here, "bundle.js"), platform: "browser", format: "iife", target: "chrome130", logLevel: "error" });
console.log("bundled");
