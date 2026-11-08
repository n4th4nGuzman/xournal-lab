// Builds, then launches Electron. ELECTRON_RUN_AS_NODE (set by some IDE
// terminals) makes electron.exe behave as plain Node, so it is removed here.
import { spawn } from "node:child_process";
import { createRequire } from "node:module";

await import("./build.mjs");
const electronPath = createRequire(import.meta.url)("electron");
const env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
const child = spawn(electronPath, [".", ...process.argv.slice(2)], { stdio: "inherit", env });
child.on("exit", (code) => process.exit(code ?? 0));
