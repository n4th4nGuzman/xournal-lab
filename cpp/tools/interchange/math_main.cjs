// electron math_main.cjs MODE OUTJSON FILE   MODE = probe | repair (FILE is read) | make (FILE is written); never touches an input.
const { app, BrowserWindow } = require("electron");
const fs = require("fs"), path = require("path"), zlib = require("zlib");
app.setPath("userData", require("os").tmpdir() + "/xl_interchange_electron_math");
app.disableHardwareAcceleration();
app.whenReady().then(async () => {
  const [mode, outJson, file] = process.argv.slice(2);
  const win = new BrowserWindow({ show: false, webPreferences: { contextIsolation: false, nodeIntegration: false } });
  await win.loadURL("data:text/html,<html></html>");
  await win.webContents.executeJavaScript(fs.readFileSync(path.join(__dirname, "math_bundle.js"), "utf8"));
  const read = (f) => { const raw = fs.readFileSync(f); return (raw[0] === 0x1f && raw[1] === 0x8b ? zlib.gunzipSync(raw) : raw).toString("utf8"); };
  let res;
  try {
    if (mode === "probe") res = await win.webContents.executeJavaScript(`window.xlm.probe(${JSON.stringify(read(file))})`);
    else if (mode === "repair") res = await win.webContents.executeJavaScript(`window.xlm.repairProbe(${JSON.stringify(read(file))})`);
    else if (mode === "make") {
      const r = await win.webContents.executeJavaScript("window.xlm.make()");
      fs.writeFileSync(file, zlib.gzipSync(Buffer.from(r.xml, "utf8")));
      res = r.probe;
    }
    fs.writeFileSync(outJson, JSON.stringify(res));
    console.log("OK " + mode);
    app.exit(0);
  } catch (e) { console.log("FAIL " + mode + ": " + e.message); app.exit(1); }
});
