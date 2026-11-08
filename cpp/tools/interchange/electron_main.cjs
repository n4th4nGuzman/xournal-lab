// Run: electron electron_main.cjs OUTDIR FILE... ; for each FILE writes OUTDIR/<name>.electron.json (canonical dump of the Electron parse)
// and OUTDIR/<name>.electron.xopp (Electron's own re-serialization), reading only; never touches the inputs.
const { app, BrowserWindow } = require("electron");
const fs = require("fs"), path = require("path"), zlib = require("zlib");
app.setPath("userData", require("os").tmpdir() + "/xl_interchange_electron");
app.disableHardwareAcceleration();
app.whenReady().then(async () => {
  const out = process.argv[2], files = process.argv.slice(3);
  fs.mkdirSync(out, { recursive: true });
  const win = new BrowserWindow({ show: false, webPreferences: { contextIsolation: false, nodeIntegration: false } });
  await win.loadURL("data:text/html,<html></html>");
  await win.webContents.executeJavaScript(fs.readFileSync(path.join(__dirname, "bundle.js"), "utf8"));
  let bad = 0;
  for (const f of files) {
    const raw = fs.readFileSync(f);
    const xml = (raw[0] === 0x1f && raw[1] === 0x8b ? zlib.gunzipSync(raw) : raw).toString("utf8");
    const name = path.basename(f).replace(/\.xopp$/, "");
    try {
      const d = await win.webContents.executeJavaScript(`window.xl.dump(${JSON.stringify(xml)})`);
      fs.writeFileSync(path.join(out, name + ".electron.json"), JSON.stringify(d));
      const re = await win.webContents.executeJavaScript(`window.xl.reserialize(${JSON.stringify(xml)})`);
      fs.writeFileSync(path.join(out, name + ".electron.xopp"), zlib.gzipSync(Buffer.from(re, "utf8")));
      console.log("OK " + name + " dropped=" + d.dropped);
    } catch (e) { console.log("FAIL " + name + ": " + e.message); bad++; }
  }
  app.exit(bad ? 1 : 0);
});
