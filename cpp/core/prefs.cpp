#include "prefs.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

// JS Number(v) for the values that can come out of JSON: numbers, numeric strings (a blank one is 0 but the caller rejects "" before), booleans, null.
static bool jsNumber(const QJsonValue& v, double* out) {
  if (v.isDouble()) { *out = v.toDouble(); return std::isfinite(*out); }
  if (v.isBool()) { *out = v.toBool() ? 1 : 0; return true; }
  if (v.isString()) {
    bool ok = false;
    const double d = v.toString().trimmed().toDouble(&ok);
    if (ok && std::isfinite(d)) { *out = d; return true; }
  }
  return false;
}

static double clampNum(const QJsonValue& v, double lo, double hi, double d) {
  double n;
  if (v.isNull() || (v.isString() && v.toString().isEmpty()) || !jsNumber(v, &n)) return d;
  return std::min(hi, std::max(lo, n));
}

static QString textUnder(const QJsonObject& o, const char* key, int limit) {
  const QJsonValue v = o.value(QLatin1String(key));
  return v.isString() && v.toString().size() < limit ? v.toString() : QString();
}

static const char* const kKnown[] = {"fingerDraw", "saveOverOpened", "autosaveMinutes", "theme", "palette", "effects", "welcome", "plane", "space3", "fill", "tablet", "sizes",
                                     "textMd", "toolbox", "glowOn", "glowSize", "glowIntensity", "xpp"};

Prefs sanitizePrefs(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  Prefs p;
  p.fingerDraw = o.value("fingerDraw").isBool() && o.value("fingerDraw").toBool();
  p.saveOverOpened = o.value("saveOverOpened").isBool() && o.value("saveOverOpened").toBool();
  double m;
  p.autosaveMinutes = jsNumber(o.value("autosaveMinutes"), &m) && !o.value("autosaveMinutes").isNull() ? int(std::min(120.0, std::max(0.0, std::floor(m + 0.5)))) : 0;
  p.theme = o.value("theme").toString() == "neon" ? "neon" : "classic";
  static const QRegularExpression id(QStringLiteral("^[a-z0-9-]{1,32}$"));
  p.palette = o.value("palette").isString() && id.match(o.value("palette").toString()).hasMatch() ? o.value("palette").toString() : QStringLiteral("classic");
  const QString fx = o.value("effects").toString();
  p.effects = fx == "full" || fx == "reduced" ? fx : QStringLiteral("auto");
  p.welcome = !(o.value("welcome").isBool() && !o.value("welcome").toBool());
  p.plane = textUnder(o, "plane", 600);
  p.space3 = textUnder(o, "space3", 1500);
  p.fill = textUnder(o, "fill", 500);
  p.tablet = textUnder(o, "tablet", 6000);
  p.sizes = textUnder(o, "sizes", 1500);
  p.textMd = !(o.value("textMd").isBool() && !o.value("textMd").toBool());
  p.toolbox = textUnder(o, "toolbox", 2000);
  p.glowOn = o.value("glowOn").isBool() && o.value("glowOn").toBool();
  p.glowSize = clampNum(o.value("glowSize"), 1, 40, 8);
  p.glowIntensity = clampNum(o.value("glowIntensity"), 0.05, 1, 0.8);
  p.xpp = sanitizeXpp(o.value("xpp"));
  for (auto it = o.begin(); it != o.end(); ++it) {
    bool known = false;
    for (const char* k : kKnown) known = known || it.key() == QLatin1String(k);
    if (!known) p.extra.insert(it.key(), it.value());
  }
  return p;
}

QJsonObject prefsToJson(const Prefs& p) {
  QJsonObject o = p.extra;
  o["fingerDraw"] = p.fingerDraw;
  o["saveOverOpened"] = p.saveOverOpened;
  o["autosaveMinutes"] = p.autosaveMinutes;
  o["theme"] = p.theme;
  o["palette"] = p.palette;
  o["effects"] = p.effects;
  o["welcome"] = p.welcome;
  o["plane"] = p.plane;
  o["space3"] = p.space3;
  o["fill"] = p.fill;
  o["tablet"] = p.tablet;
  o["sizes"] = p.sizes;
  o["textMd"] = p.textMd;
  o["toolbox"] = p.toolbox;
  o["glowOn"] = p.glowOn;
  o["glowSize"] = p.glowSize;
  o["glowIntensity"] = p.glowIntensity;
  if (!(p.xpp == XppPrefs())) o["xpp"] = xppToJson(p.xpp);   // additive: a file that never changed one of these settings has no such key (and a file from before has it untouched)
  return o;
}

bool loadPrefs(const QString& path, Prefs* out, QString* error) {
  *out = Prefs();
  QFile f(path);
  if (!f.exists()) return false;
  if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
  QJsonParseError pe;
  const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError || !d.isObject()) { if (error) *error = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not a JSON object"); return false; }
  *out = sanitizePrefs(d.object());
  return true;
}

bool savePrefs(const QString& path, const Prefs& p, QString* error) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
  f.write(QJsonDocument(prefsToJson(p)).toJson(QJsonDocument::Compact));
  if (!f.commit()) { if (error) *error = f.errorString(); return false; }
  return true;
}

QString defaultPrefsPath() {
  // Electron's userData is %APPDATA%/xournal-electron (roaming); Qt's standard locations on Windows point at the local profile instead.
  const QString roaming = qEnvironmentVariable("APPDATA");
  const QString base = roaming.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) : roaming;
  return base + "/xournal-electron/prefs.json";
}

static QJsonValue parseText(const QString& text) {
  if (text.isEmpty()) return QJsonValue();
  QJsonParseError pe;
  const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8(), &pe);
  if (pe.error != QJsonParseError::NoError) return QJsonValue();
  return d.isObject() ? QJsonValue(d.object()) : QJsonValue(d.array());
}
static QString dump(const QJsonObject& o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }

SizePresets prefsSizes(const Prefs& p) { return sanitizeSizes(parseText(p.sizes)); }
void setPrefsSizes(Prefs& p, const SizePresets& s) { p.sizes = dump(sizesToJson(s)); }
TabletSettings prefsTablet(const Prefs& p) { return sanitizeTablet(parseText(p.tablet)); }
void setPrefsTablet(Prefs& p, const TabletSettings& t) { p.tablet = dump(tabletToJson(t)); }
ToolboxSettings prefsToolbox(const Prefs& p) { return p.toolbox.isEmpty() ? defaultToolbox() : sanitizeToolbox(parseText(p.toolbox)); }
void setPrefsToolbox(Prefs& p, const ToolboxSettings& t) { p.toolbox = dump(toolboxToJson(t)); }
