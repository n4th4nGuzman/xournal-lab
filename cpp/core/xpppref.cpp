#include "xpppref.h"
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>

namespace {
bool num(const QJsonValue& v, double* out) {
  if (v.isDouble()) { *out = v.toDouble(); return std::isfinite(*out); }
  return false;
}
bool getB(const QJsonObject& o, const char* k, bool d) { const QJsonValue v = o.value(QLatin1String(k)); return v.isBool() ? v.toBool() : d; }
double getD(const QJsonObject& o, const char* k, double lo, double hi, double d) {
  double n;
  return num(o.value(QLatin1String(k)), &n) ? std::min(hi, std::max(lo, n)) : d;
}
int getI(const QJsonObject& o, const char* k, int lo, int hi, int d) { return int(std::lround(getD(o, k, lo, hi, d))); }
// A choice among 0..hi: anything else (a string, 9, 1.5) is not a choice and takes the default instead of the nearest one.
int getE(const QJsonObject& o, const char* k, int hi, int d) {
  double n;
  return num(o.value(QLatin1String(k)), &n) && n == std::floor(n) && n >= 0 && n <= hi ? int(n) : d;
}
QString getS(const QJsonObject& o, const char* k, int limit, const QString& d) {
  const QJsonValue v = o.value(QLatin1String(k));
  return v.isString() && v.toString().size() < limit ? v.toString() : d;
}
QString getColor(const QJsonObject& o, const char* k, const QString& d, bool allowEmpty) {
  const QJsonValue v = o.value(QLatin1String(k));
  if (!v.isString()) return d;
  static const QRegularExpression re(QStringLiteral("^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
  const QString s = v.toString();
  if (s.isEmpty()) return allowEmpty ? QString() : d;
  return re.match(s).hasMatch() ? s.toLower() : d;
}
ButtonCfg getBtn(const QJsonObject& o, const char* k, const ButtonCfg& d) {
  const QJsonValue v = o.value(QLatin1String(k));
  if (!v.isObject()) return d;
  const QJsonObject b = v.toObject();
  ButtonCfg c;
  static const QRegularExpression id(QStringLiteral("^[A-Za-z0-9._:-]{1,40}$"));
  const QString t = b.value("tool").toString();
  c.tool = id.match(t).hasMatch() ? t : QString();
  c.color = getColor(b, "color", QString(), true);
  if (c.color.size() != 7) c.color.clear();
  c.size = getI(b, "size", 0, 5, 0);
  return c;
}
QJsonObject btnJson(const ButtonCfg& c) { return QJsonObject{{"tool", c.tool}, {"color", c.color}, {"size", c.size}}; }
}  // namespace

XppPrefs sanitizeXpp(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  XppPrefs x;
  x.autosaveInterval = getI(o, "autosaveInterval", 1, 120, x.autosaveInterval);
  x.saveName = getS(o, "saveName", 200, x.saveName);
  x.pdfName = getS(o, "pdfName", 200, x.pdfName);
  x.autoloadPdfXoj = getB(o, "autoloadPdfXoj", x.autoloadPdfXoj);
  x.autoloadMostRecent = getB(o, "autoloadMostRecent", x.autoloadMostRecent);
  x.drawOutside = getB(o, "drawOutside", x.drawOutside);
  x.pressureGuess = getB(o, "pressureGuess", x.pressureGuess);
  const QJsonObject dc = o.value("deviceClass").toObject();
  for (auto it = dc.begin(); it != dc.end(); ++it) {
    double n;
    if (it.key().size() < 200 && num(it.value(), &n) && n >= 0 && n <= 5) x.deviceClass.insert(it.key(), int(n));
  }
  x.stabAvg = StabAvg(getE(o, "stabAvg", 2, 0));
  x.stabPre = StabPre(getE(o, "stabPre", 2, 0));
  x.stabBuffer = getI(o, "stabBuffer", 2, 100, x.stabBuffer);
  x.stabSigma = getD(o, "stabSigma", 0.05, 5, x.stabSigma);
  x.stabDeadzone = getD(o, "stabDeadzone", 0.1, 50, x.stabDeadzone);
  x.stabDrag = getD(o, "stabDrag", 0, 1, x.stabDrag);
  x.stabMass = getD(o, "stabMass", 1, 30, x.stabMass);
  x.stabCusp = getB(o, "stabCusp", x.stabCusp);
  x.stabFinalize = getB(o, "stabFinalize", x.stabFinalize);
  x.mouseLeft = getBtn(o, "mouseLeft", x.mouseLeft);
  x.mouseMiddle = getBtn(o, "mouseMiddle", x.mouseMiddle);
  x.mouseRight = getBtn(o, "mouseRight", x.mouseRight);
  x.mouse4 = getBtn(o, "mouse4", x.mouse4);
  x.mouse5 = getBtn(o, "mouse5", x.mouse5);
  x.pressureSens = getB(o, "pressureSens", x.pressureSens);
  x.minPressure = getD(o, "minPressure", 0.01, 1, x.minPressure);
  x.pressureMult = getD(o, "pressureMult", 0.5, 4, x.pressureMult);
  x.ignoreFirst = getB(o, "ignoreFirst", x.ignoreFirst);
  x.ignoreFirstN = getI(o, "ignoreFirstN", 1, 1000, x.ignoreFirstN);
  x.stylus1 = getBtn(o, "stylus1", x.stylus1);
  x.stylus2 = getBtn(o, "stylus2", x.stylus2);
  x.stylusEraser = getBtn(o, "stylusEraser", x.stylusEraser);
  x.eraserVisibility = getE(o, "eraserVisibility", 3, x.eraserVisibility);
  x.zoomGestures = getB(o, "zoomGestures", x.zoomGestures);
  x.zoomStartThreshold = getD(o, "zoomStartThreshold", 0, 100, x.zoomStartThreshold);
  x.handRecognition = getB(o, "handRecognition", x.handRecognition);
  x.handTimeout = getD(o, "handTimeout", 0.5, 10, x.handTimeout);
  x.disableInertia = getB(o, "disableInertia", x.disableInertia);
  x.menubarAtStart = getB(o, "menubarAtStart", x.menubarAtStart);
  x.titlePath = getB(o, "titlePath", x.titlePath);
  x.titlePageNumber = getB(o, "titlePageNumber", x.titlePageNumber);
  x.colorBorder = getColor(o, "colorBorder", QString(), true);
  x.colorBackground = getColor(o, "colorBackground", QString(), true);
  x.colorSelection = getColor(o, "colorSelection", QString(), true);
  x.colorActiveSelection = getColor(o, "colorActiveSelection", QString(), true);
  x.pageShadow = getB(o, "pageShadow", x.pageShadow);
  x.cursorIcon = getE(o, "cursorIcon", 5, x.cursorIcon);
  x.highlightCursor = getB(o, "highlightCursor", x.highlightCursor);
  x.highlightRadius = getD(o, "highlightRadius", 0, 30, x.highlightRadius);
  x.highlightColor = getColor(o, "highlightColor", x.highlightColor, false);
  x.highlightBorderWidth = getD(o, "highlightBorderWidth", 0, 30, x.highlightBorderWidth);
  x.highlightBorderColor = getColor(o, "highlightBorderColor", x.highlightBorderColor, false);
  x.sidebarRight = getB(o, "sidebarRight", x.sidebarRight);
  x.scrollbarLeft = getB(o, "scrollbarLeft", x.scrollbarLeft);
  x.sidebarNumbering = getE(o, "sidebarNumbering", 3, x.sidebarNumbering);
  x.hideHScroll = getB(o, "hideHScroll", x.hideHScroll);
  x.hideVScroll = getB(o, "hideVScroll", x.hideVScroll);
  x.fsMenubar = getB(o, "fsMenubar", x.fsMenubar);
  x.fsToolbar = getB(o, "fsToolbar", x.fsToolbar);
  x.fsSidebar = getB(o, "fsSidebar", x.fsSidebar);
  x.prMenubar = getB(o, "prMenubar", x.prMenubar);
  x.prToolbar = getB(o, "prToolbar", x.prToolbar);
  x.prSidebar = getB(o, "prSidebar", x.prSidebar);
  x.prFullscreen = getB(o, "prFullscreen", x.prFullscreen);
  x.zoomStep = getD(o, "zoomStep", 0.1, 50, x.zoomStep);
  x.zoomStepScroll = getD(o, "zoomStepScroll", 0.1, 50, x.zoomStepScroll);
  x.forceFitOnLoad = getB(o, "forceFitOnLoad", x.forceFitOnLoad);
  x.addHSpace = getB(o, "addHSpace", x.addHSpace);
  x.addVSpace = getB(o, "addVSpace", x.addVSpace);
  x.unlimitedScroll = getB(o, "unlimitedScroll", x.unlimitedScroll);
  x.hSpaceLeft = getI(o, "hSpaceLeft", 0, 2000, x.hSpaceLeft);
  x.hSpaceRight = getI(o, "hSpaceRight", 0, 2000, x.hSpaceRight);
  x.vSpaceAbove = getI(o, "vSpaceAbove", 0, 2000, x.vSpaceAbove);
  x.vSpaceBelow = getI(o, "vSpaceBelow", 0, 2000, x.vSpaceBelow);
  x.lastPageAppend = getE(o, "lastPageAppend", 2, 0);
  x.tapEnabled = getB(o, "tapEnabled", x.tapEnabled);
  x.tapTime = getI(o, "tapTime", 0, 1000, x.tapTime);
  x.tapSuccessive = getI(o, "tapSuccessive", 0, 1000, x.tapSuccessive);
  x.tapLength = getD(o, "tapLength", 0.01, 100, x.tapLength);
  x.tapToolbox = getB(o, "tapToolbox", x.tapToolbox);
  x.defaultTool = getBtn(o, "defaultTool", x.defaultTool);
  x.laserFadeMs = getI(o, "laserFadeMs", 0, 4000, x.laserFadeMs);
  x.tabSpaces = getB(o, "tabSpaces", x.tabSpaces);
  x.tabSpaceCount = getI(o, "tabSpaceCount", 1, 8, x.tabSpaceCount);
  return x;
}

QJsonObject xppToJson(const XppPrefs& x) {
  QJsonObject o;
  o["autosaveInterval"] = x.autosaveInterval;
  o["saveName"] = x.saveName;
  o["pdfName"] = x.pdfName;
  o["autoloadPdfXoj"] = x.autoloadPdfXoj;
  o["autoloadMostRecent"] = x.autoloadMostRecent;
  o["drawOutside"] = x.drawOutside;
  o["pressureGuess"] = x.pressureGuess;
  QJsonObject dc;
  for (auto it = x.deviceClass.begin(); it != x.deviceClass.end(); ++it) dc[it.key()] = it.value();
  o["deviceClass"] = dc;
  o["stabAvg"] = int(x.stabAvg);
  o["stabPre"] = int(x.stabPre);
  o["stabBuffer"] = x.stabBuffer;
  o["stabSigma"] = x.stabSigma;
  o["stabDeadzone"] = x.stabDeadzone;
  o["stabDrag"] = x.stabDrag;
  o["stabMass"] = x.stabMass;
  o["stabCusp"] = x.stabCusp;
  o["stabFinalize"] = x.stabFinalize;
  o["mouseLeft"] = btnJson(x.mouseLeft);
  o["mouseMiddle"] = btnJson(x.mouseMiddle);
  o["mouseRight"] = btnJson(x.mouseRight);
  o["mouse4"] = btnJson(x.mouse4);
  o["mouse5"] = btnJson(x.mouse5);
  o["pressureSens"] = x.pressureSens;
  o["minPressure"] = x.minPressure;
  o["pressureMult"] = x.pressureMult;
  o["ignoreFirst"] = x.ignoreFirst;
  o["ignoreFirstN"] = x.ignoreFirstN;
  o["stylus1"] = btnJson(x.stylus1);
  o["stylus2"] = btnJson(x.stylus2);
  o["stylusEraser"] = btnJson(x.stylusEraser);
  o["eraserVisibility"] = x.eraserVisibility;
  o["zoomGestures"] = x.zoomGestures;
  o["zoomStartThreshold"] = x.zoomStartThreshold;
  o["handRecognition"] = x.handRecognition;
  o["handTimeout"] = x.handTimeout;
  o["disableInertia"] = x.disableInertia;
  o["menubarAtStart"] = x.menubarAtStart;
  o["titlePath"] = x.titlePath;
  o["titlePageNumber"] = x.titlePageNumber;
  o["colorBorder"] = x.colorBorder;
  o["colorBackground"] = x.colorBackground;
  o["colorSelection"] = x.colorSelection;
  o["colorActiveSelection"] = x.colorActiveSelection;
  o["pageShadow"] = x.pageShadow;
  o["cursorIcon"] = x.cursorIcon;
  o["highlightCursor"] = x.highlightCursor;
  o["highlightRadius"] = x.highlightRadius;
  o["highlightColor"] = x.highlightColor;
  o["highlightBorderWidth"] = x.highlightBorderWidth;
  o["highlightBorderColor"] = x.highlightBorderColor;
  o["sidebarRight"] = x.sidebarRight;
  o["scrollbarLeft"] = x.scrollbarLeft;
  o["sidebarNumbering"] = x.sidebarNumbering;
  o["hideHScroll"] = x.hideHScroll;
  o["hideVScroll"] = x.hideVScroll;
  o["fsMenubar"] = x.fsMenubar;
  o["fsToolbar"] = x.fsToolbar;
  o["fsSidebar"] = x.fsSidebar;
  o["prMenubar"] = x.prMenubar;
  o["prToolbar"] = x.prToolbar;
  o["prSidebar"] = x.prSidebar;
  o["prFullscreen"] = x.prFullscreen;
  o["zoomStep"] = x.zoomStep;
  o["zoomStepScroll"] = x.zoomStepScroll;
  o["forceFitOnLoad"] = x.forceFitOnLoad;
  o["addHSpace"] = x.addHSpace;
  o["addVSpace"] = x.addVSpace;
  o["unlimitedScroll"] = x.unlimitedScroll;
  o["hSpaceLeft"] = x.hSpaceLeft;
  o["hSpaceRight"] = x.hSpaceRight;
  o["vSpaceAbove"] = x.vSpaceAbove;
  o["vSpaceBelow"] = x.vSpaceBelow;
  o["lastPageAppend"] = x.lastPageAppend;
  o["tapEnabled"] = x.tapEnabled;
  o["tapTime"] = x.tapTime;
  o["tapSuccessive"] = x.tapSuccessive;
  o["tapLength"] = x.tapLength;
  o["tapToolbox"] = x.tapToolbox;
  o["defaultTool"] = btnJson(x.defaultTool);
  o["laserFadeMs"] = x.laserFadeMs;
  o["tabSpaces"] = x.tabSpaces;
  o["tabSpaceCount"] = x.tabSpaceCount;
  return o;
}

QString expandNameTemplate(const QString& tpl, const QDateTime& now, const QString& name, const QString& file) {
  const QLocale es(QLocale::Spanish);
  QString out;
  for (int i = 0; i < tpl.size(); i++) {
    const QChar ch = tpl[i];
    if (ch != '%' || i + 1 >= tpl.size()) { out += ch; continue; }
    const QChar c = tpl[++i];
    if (c == '{') {
      const int end = tpl.indexOf('}', i);
      if (end < 0) { out += QStringLiteral("%{"); continue; }
      const QString key = tpl.mid(i + 1, end - i - 1);
      if (key == "name") out += name;
      else if (key == "file") out += file;
      else out += "%{" + key + "}";
      i = end;
      continue;
    }
    const QDate d = now.date();
    const QTime t = now.time();
    switch (c.unicode()) {
      case 'a': out += es.dayName(d.dayOfWeek(), QLocale::ShortFormat); break;
      case 'A': out += es.dayName(d.dayOfWeek(), QLocale::LongFormat); break;
      case 'b': out += es.monthName(d.month(), QLocale::ShortFormat); break;
      case 'B': out += es.monthName(d.month(), QLocale::LongFormat); break;
      case 'c': out += es.toString(now, QLocale::LongFormat); break;
      case 'd': out += d.toString("dd"); break;
      case 'F': out += d.toString("yyyy-MM-dd"); break;
      case 'H': out += t.toString("HH"); break;
      case 'I': out += QString("%1").arg(t.hour() % 12 == 0 ? 12 : t.hour() % 12, 2, 10, QChar('0')); break;
      case 'j': out += QString("%1").arg(d.dayOfYear(), 3, 10, QChar('0')); break;
      case 'm': out += d.toString("MM"); break;
      case 'M': out += t.toString("mm"); break;
      case 'p': out += t.hour() < 12 ? "AM" : "PM"; break;
      case 'S': out += t.toString("ss"); break;
      case 'U': out += QString("%1").arg((d.dayOfYear() + 6 - (d.dayOfWeek() % 7)) / 7, 2, 10, QChar('0')); break;
      case 'w': out += QString::number(d.dayOfWeek() % 7); break;
      case 'W': out += QString("%1").arg((d.dayOfYear() + 6 - ((d.dayOfWeek() + 6) % 7)) / 7, 2, 10, QChar('0')); break;
      case 'x': out += d.toString("dd/MM/yy"); break;
      case 'X': out += t.toString("HH:mm:ss"); break;
      case 'y': out += d.toString("yy"); break;
      case 'Y': out += d.toString("yyyy"); break;
      case 'Z': out += now.timeZoneAbbreviation(); break;
      case '%': out += '%'; break;
      default: out += '%'; out += c; break;
    }
  }
  static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
  out.replace(bad, "_");
  return out;
}
