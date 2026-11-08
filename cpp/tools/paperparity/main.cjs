// electron main.cjs OUTDIR SCALE : for every paper value below writes OUTDIR/<name>.electron.png and OUTDIR/<name>.electron.xopp (Electron's own writer), on white and on a dark paper.
const { app, BrowserWindow } = require("electron");
const fs = require("fs"), path = require("path"), zlib = require("zlib");
app.setPath("userData", require("os").tmpdir() + "/xl_paperparity_electron");
app.disableHardwareAcceleration();
app.whenReady().then(async () => {
  const [out, scaleArg, listFile] = process.argv.slice(2);
  const scale = Number(scaleArg || 1);
  fs.mkdirSync(out, { recursive: true });
  const win = new BrowserWindow({ show: false, webPreferences: { contextIsolation: false, nodeIntegration: false } });
  await win.loadURL("data:text/html,<html></html>");
  await win.webContents.executeJavaScript(fs.readFileSync(path.join(__dirname, "bundle.js"), "utf8"));
  const cases = JSON.parse(fs.readFileSync(listFile, "utf8")); // [{name, value, color}]
  for (const c of cases) {
    const m = await win.webContents.executeJavaScript(`window.xlp.make(${JSON.stringify(c.value)}, ${JSON.stringify(c.color)})`);
    fs.writeFileSync(path.join(out, c.name + ".electron.xopp"), zlib.gzipSync(Buffer.from(m.xml, "utf8")));
    const png = await win.webContents.executeJavaScript(`window.xlp.render(${JSON.stringify(m.xml)}, ${scale})`);
    fs.writeFileSync(path.join(out, c.name + ".electron.png"), Buffer.from(png.split(",")[1], "base64"));
  }
  fs.writeFileSync(path.join(out, "kinds.json"), JSON.stringify(await win.webContents.executeJavaScript("window.xlp.kinds()")));
  console.log("OK " + cases.length);
  app.exit(0);
});
