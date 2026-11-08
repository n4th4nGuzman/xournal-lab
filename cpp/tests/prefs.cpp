// Preferences, size presets, palettes: the real prefs.json of the Electron app is read (never written), everything else goes through a temp folder.
#include "palettes.h"
#include "prefs.h"
#include "sizes.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)

static QJsonValue parse(const char* json) {
  const QJsonDocument d = QJsonDocument::fromJson(json);
  return d.isObject() ? QJsonValue(d.object()) : QJsonValue(d.array());
}

static void sizesTests() {
  const SizePresets d = sanitizeSizes(QJsonValue());
  CHECK(d.of(SizeKind::Pen) == defaultSizes().of(SizeKind::Pen));
  // the cases of the Electron e2e check sizeDefaultsSane
  const SizePresets s = sanitizeSizes(parse(R"({"pen":[1],"eraser":"x","text":[4,99999],"highlighter":[3,6]})"));
  CHECK(s.of(SizeKind::Highlighter) == std::vector<double>({3, 6}));
  CHECK(s.of(SizeKind::Pen) == defaultSizes().of(SizeKind::Pen));        // one value is too few
  CHECK(s.of(SizeKind::Eraser) == defaultSizes().of(SizeKind::Eraser));  // not a list
  CHECK(s.of(SizeKind::Text) == defaultSizes().of(SizeKind::Text));      // out of range
  CHECK(sanitizeSizes(parse(R"({"eraser":[5,10,20]})")).of(SizeKind::Eraser) == std::vector<double>({5, 10, 20}));
  CHECK(sanitizeSizes(parse(R"({"pen":[0.123,1.999]})")).of(SizeKind::Pen) == std::vector<double>({0.12, 2.0}));   // toFixed(2)
  CHECK(sanitizeSizes(parse(R"({"pen":[0.5,1,2,3,4,5,6,7,8]})")).of(SizeKind::Pen) == defaultSizes().of(SizeKind::Pen));   // more than 8
  CHECK(sanitizeSizes(parse(R"({"pen":[0.5,"1"]})")).of(SizeKind::Pen) == defaultSizes().of(SizeKind::Pen));
  CHECK(sanitizeSizes(parse("[1,2]")).of(SizeKind::Text) == defaultSizes().of(SizeKind::Text));
  // round trip through JSON
  CHECK(sanitizeSizes(sizesToJson(s)).of(SizeKind::Highlighter) == s.of(SizeKind::Highlighter));

  SizeKind k;
  CHECK(sizeKindOf(SizeTool::Plane, &k) && k == SizeKind::Pen);
  CHECK(sizeKindOf(SizeTool::Venn, &k) && k == SizeKind::Pen);
  CHECK(sizeKindOf(SizeTool::EraserPart, &k) && k == SizeKind::Eraser);
  CHECK(sizeKindOf(SizeTool::Text, &k) && k == SizeKind::Text);
  CHECK(sizeKindOf(SizeTool::Highlighter, &k) && k == SizeKind::Highlighter);
  CHECK(!sizeKindOf(SizeTool::Select, &k) && !sizeKindOf(SizeTool::Hand, &k));
  CHECK(dotSize(SizeKind::Pen, 0.5) == 3 && dotSize(SizeKind::Pen, 6) == 16 && dotSize(SizeKind::Pen, 60) == 18 && dotSize(SizeKind::Eraser, 56) == 16);
  CHECK(dotSize(SizeKind::Text, 36) == 17 && dotSize(SizeKind::Highlighter, 2.83) == 6);
}

static void paletteTests() {
  CHECK(palettes().size() == 9);
  CHECK(paletteById("classic").colors.size() == 5 && paletteById("vivid").colors.size() == 8);
  CHECK(paletteById("nope").id == "classic");
  CHECK(paletteById("neondreams").paper == "#120a26" && paletteById("mono").papers.size() == 5);
  CHECK(paletteById("ocean").name == QString::fromUtf8("Océano"));
}

static void sanitizeTests() {
  const Prefs d = sanitizePrefs(QJsonValue());
  CHECK(d == Prefs());
  CHECK(d.palette == "classic" && d.theme == "classic" && d.effects == "auto" && d.welcome && d.textMd && d.glowSize == 8 && d.glowIntensity == 0.8);
  const Prefs p = sanitizePrefs(parse(R"({"fingerDraw":true,"autosaveMinutes":500,"theme":"neon","palette":"Bad Id","effects":"full","welcome":false,
      "glowSize":99,"glowIntensity":"0.5","glowOn":1,"sizes":5,"plane":"x","mystery":{"a":[1,2]}})"));
  CHECK(p.fingerDraw && p.autosaveMinutes == 120 && p.theme == "neon" && p.palette == "classic" && p.effects == "full" && !p.welcome);
  CHECK(p.glowSize == 40 && p.glowIntensity == 0.5 && !p.glowOn && p.sizes.isEmpty() && p.plane == "x");
  CHECK(sanitizePrefs(parse(R"({"autosaveMinutes":2.5})")).autosaveMinutes == 3);
  CHECK(sanitizePrefs(parse(R"({"autosaveMinutes":-4})")).autosaveMinutes == 0);
  CHECK(sanitizePrefs(parse(R"({"autosaveMinutes":"7"})")).autosaveMinutes == 7);
  CHECK(sanitizePrefs(parse(R"({"autosaveMinutes":null,"glowSize":null})")).glowSize == 8);
  CHECK(sanitizePrefs(parse(R"({"plane":")" + QByteArray(700, 'a') + R"("})")).plane.isEmpty());   // over the length limit
  CHECK(p.extra.contains("mystery") && p.extra.value("mystery").isObject());
  CHECK(!p.extra.contains("theme"));
}

static void roundTripTests() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const QString path = dir.filePath("sub/prefs.json");
  Prefs p;
  QString err;
  CHECK(!loadPrefs(path, &p, &err) && p == Prefs() && err.isEmpty());   // first run: defaults, no error
  p.theme = "neon"; p.palette = "sunset"; p.autosaveMinutes = 7; p.glowOn = true; p.extra.insert("future", QJsonArray{1, "two"});
  CHECK(savePrefs(path, p, &err));
  Prefs q;
  CHECK(loadPrefs(path, &q, &err) && q == p);
  CHECK(q.extra.value("future").toArray().size() == 2);
  // a broken file is reported and gives defaults
  QFile f(dir.filePath("bad.json"));
  CHECK(f.open(QIODevice::WriteOnly)); f.write("{not json"); f.close();
  CHECK(!loadPrefs(dir.filePath("bad.json"), &q, &err) && !err.isEmpty() && q == Prefs());
  // nested JSON text
  Prefs n;
  SizePresets s = defaultSizes(); s.of(SizeKind::Pen) = {1, 2, 3};
  setPrefsSizes(n, s);
  CHECK(prefsSizes(n).of(SizeKind::Pen) == std::vector<double>({1, 2, 3}));
  TabletSettings t; t.brush = BrushKind::Ink; t.keys.insert("F13", "tool.pen");
  setPrefsTablet(n, t);
  CHECK(prefsTablet(n) == t);
  ToolboxSettings tb = defaultToolbox(); tb.closeOnPick = false;
  setPrefsToolbox(n, tb);
  CHECK(prefsToolbox(n) == tb);
  CHECK(prefsToolbox(Prefs()) == defaultToolbox() && prefsSizes(Prefs()).of(SizeKind::Eraser) == defaultSizes().of(SizeKind::Eraser));
  CHECK(prefsTablet(Prefs()) == TabletSettings());
  Prefs bad; bad.sizes = "{oops"; bad.tablet = "[1]"; bad.toolbox = "nope";
  CHECK(prefsSizes(bad).of(SizeKind::Pen) == defaultSizes().of(SizeKind::Pen) && prefsTablet(bad) == TabletSettings());
  CHECK(prefsToolbox(bad) == defaultToolbox());
}

// The file the user already has. Read only.
static void realFileTests() {
  const QString path = defaultPrefsPath();
  if (!QFile::exists(path)) { std::printf("note: %s not found, real-file checks skipped\n", qPrintable(path)); return; }
  Prefs p;
  QString err;
  CHECK(loadPrefs(path, &p, &err));
  CHECK(err.isEmpty());
  CHECK(!p.palette.isEmpty() && p.autosaveMinutes >= 0 && p.autosaveMinutes <= 120);
  CHECK(p.glowSize >= 1 && p.glowSize <= 40 && p.glowIntensity >= 0.05 && p.glowIntensity <= 1);
  // the nested texts the Electron app wrote must all parse into valid settings
  const TabletSettings t = prefsTablet(p);
  CHECK(t.size >= 0.1 && t.size <= 60);
  if (!p.tablet.isEmpty()) {
    const QJsonObject raw = QJsonDocument::fromJson(p.tablet.toUtf8()).object();
    CHECK(brushId(t.brush) == raw.value("brush").toString());
    CHECK(t.keys.size() == raw.value("keys").toObject().size());   // every binding the user made survived sanitising
    CHECK(penActionId(t.barrel) == raw.value("barrel").toString() && penActionId(t.barrel2) == raw.value("barrel2").toString());
    CHECK(t.curve == sanitizeCurve(raw.value("curve")));
  }
  const ToolboxSettings tb = prefsToolbox(p);
  CHECK(!tb.rows.empty());
  if (!p.toolbox.isEmpty()) {
    const QJsonObject raw = QJsonDocument::fromJson(p.toolbox.toUtf8()).object();
    const QJsonArray items = raw.value("items").toArray();
    if (!items.isEmpty()) {   // the layout from before the zones becomes one zone with every item
      CHECK(tb.rows.size() == 1 && tb.rows[0].size() == items.size());
      CHECK(tb.rows[0][0] == items[0].toString());
    }
  }
  const SizePresets s = prefsSizes(p);   // "" in the file: the defaults
  CHECK(!s.of(SizeKind::Pen).empty());
  std::printf("real prefs.json: palette=%s theme=%s tablet keys=%d toolbox items=%lld\n", qPrintable(p.palette), qPrintable(p.theme), int(t.keys.size()),
              tb.rows.empty() ? 0LL : (long long)tb.rows[0].size());
  // writing what we read must not lose a key of the file: compare the key sets (temp copy only)
  QTemporaryDir dir;
  const QString out = dir.filePath("prefs.json");
  CHECK(savePrefs(out, p, &err));
  QFile a(path), b(out);
  CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
  const QJsonObject oa = QJsonDocument::fromJson(a.readAll()).object(), ob = QJsonDocument::fromJson(b.readAll()).object();
  for (auto it = oa.begin(); it != oa.end(); ++it) CHECK(ob.contains(it.key()));
  CHECK(oa == ob);   // an untouched file is rewritten with identical content
}

// The additive "xpp" object: the settings of the original Xournal++ preferences, tolerant and round-tripping.
static void xppTests() {
  // nothing there: the defaults, and nothing is written for them
  Prefs p = sanitizePrefs(parse("{}"));
  CHECK(p.xpp == XppPrefs());
  CHECK(!prefsToJson(p).contains("xpp"));
  CHECK(p.xpp.mouseMiddle.tool == "hand" && p.xpp.saveName == "%F-Nota-%H-%M" && p.xpp.pdfName == "%{name}_anotado" && p.xpp.minPressure == 0.05 && p.xpp.stabBuffer == 20);
  // garbage takes the defaults, ranges are the original dialog's, unknown keys of the file are kept
  p = sanitizePrefs(parse(R"({"zzFuture":[1,2],"xpp":{"saveName":5,"cursorIcon":"x","stabAvg":9,"minPressure":"high","pressureMult":99,"stabBuffer":1,"mouseRight":"x","mouse4":{"tool":"eraser","color":"red","size":9},"deviceClass":[1],"highlightColor":"nope"}})"));
  CHECK(p.xpp.saveName == "%F-Nota-%H-%M" && p.xpp.cursorIcon == 0 && p.xpp.stabAvg == StabAvg::None && p.xpp.minPressure == 0.05);
  CHECK(p.xpp.pressureMult == 4 && p.xpp.stabBuffer == 2);   // out of range numbers clamp, like the original's spin buttons
  CHECK(p.xpp.mouseRight == ButtonCfg() && p.xpp.mouse4.tool == "eraser" && p.xpp.mouse4.color.isEmpty() && p.xpp.mouse4.size == 5);
  CHECK(p.xpp.deviceClass.isEmpty() && p.xpp.highlightColor == "#80ffff00");
  CHECK(p.extra.contains("zzFuture"));
  // a changed value is written under "xpp" and comes back equal; the Electron keys are untouched
  p = sanitizePrefs(parse(R"({"theme":"neon","xpp":{"saveName":"N-%Y","cursorIcon":3,"mouseRight":{"tool":"highlighter","color":"#ff0000","size":2},"deviceClass":{"Pen|3":4},"stabAvg":2,"tapEnabled":true,"colorBorder":"#00ff00"}})"));
  const QJsonObject o = prefsToJson(p);
  CHECK(o.value("theme").toString() == "neon" && o.value("xpp").isObject());
  const Prefs q = sanitizePrefs(o);
  CHECK(q.xpp == p.xpp && q.xpp.mouseRight.color == "#ff0000" && q.xpp.deviceClass.value("Pen|3") == 4 && q.xpp.colorBorder == "#00ff00");
  CHECK(sanitizeXpp(xppToJson(XppPrefs())) == XppPrefs());
  // file names
  const QDateTime t(QDate(2001, 8, 23), QTime(14, 55, 2));
  CHECK(expandNameTemplate("%F-Nota-%H-%M", t) == "2001-08-23-Nota-14-55");
  CHECK(expandNameTemplate("%{name}_anotado", t, "Tema") == "Tema_anotado");
  CHECK(expandNameTemplate("a:b/c", t) == "a_b_c" && expandNameTemplate("100%%", t) == "100%" && expandNameTemplate("%{x}", t) == "%{x}");
}

int main() {
  xppTests();
  sizesTests();
  paletteTests();
  sanitizeTests();
  roundTripTests();
  realFileTests();
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("prefs: all passed\n");
  return 0;
}
