// Small helpers shared by numberline.cpp and venn.cpp: JSON text written the way JSON.stringify writes it (keys in the order the TypeScript
// builds its objects, which QJsonObject cannot keep), and the reading of what is stored in the witness stroke. Internal to the "sets" module.
#pragma once
#include "genobj.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QString>
#include <cmath>
#include <optional>
#include <vector>

namespace sets::detail {

inline QString jsNum(double v) {
  if (!std::isfinite(v)) return "null";   // JSON.stringify(NaN | Infinity)
  if (v == 0) return "0";                 // and -0
  QString s = QString::number(v, 'g', QLocale::FloatingPointShortest);
  const int e = int(s.indexOf('e'));
  if (e >= 0) {                            // JS writes 1e-7 and 1e+21, not 1e-07
    QString mant = s.left(e), exp = s.mid(e + 1);
    const QString sign = exp.startsWith('-') ? "-" : "+";
    exp = exp.mid(1);
    while (exp.size() > 1 && exp.startsWith('0')) exp.remove(0, 1);
    s = mant + "e" + sign + exp;
  }
  return s;
}

inline QString jsStr(const QString& s) {
  QString out = "\"";
  for (const QChar c : s) {
    const ushort u = c.unicode();
    if (u == '"') out += "\\\"";
    else if (u == '\\') out += "\\\\";
    else if (u == '\n') out += "\\n";
    else if (u == '\r') out += "\\r";
    else if (u == '\t') out += "\\t";
    else if (u == '\b') out += "\\b";
    else if (u == '\f') out += "\\f";
    else if (u < 0x20) out += QString("\\u%1").arg(u, 4, 16, QChar('0'));
    else out += c;
  }
  return out + "\"";
}

inline QString jsBool(bool b) { return b ? "true" : "false"; }

inline QString jsNums(const std::vector<double>& v) {
  QString out = "[";
  for (size_t i = 0; i < v.size(); i++) out += (i ? "," : "") + jsNum(v[i]);
  return out + "]";
}

inline double numberOr(const QJsonValue& v, double d, double lo = -1e9, double hi = 1e9) {
  if (!v.isDouble()) return d;
  const double x = v.toDouble();
  return std::isfinite(x) ? std::min(hi, std::max(lo, x)) : d;
}

/** The glow described by a stroke's attributes (attrs "glow" and "glowAlpha"), like readGlow of model.ts. */
inline std::optional<Glow> readGlowAttrs(const Attrs& attrs) {
  const QString* g = attrFind(attrs, "glow");
  bool ok = false;
  const double size = g ? g->toDouble(&ok) : 0;
  if (!ok || !(size > 0)) return std::nullopt;
  double a = 0.8;
  if (const QString* ga = attrFind(attrs, "glowAlpha")) {
    bool aok = false;
    const double x = ga->toDouble(&aok);
    a = aok ? std::min(1.0, std::max(0.05, x)) : 0.8;
  }
  return Glow{std::min(40.0, size), a};
}

/** What readLine / readVenn share: the witness JSON object, its "w" points, the style of the object. nullopt when anything is missing. */
struct Stored {
  QJsonObject obj;
  std::vector<double> w;
  double width = 1.41;
};

inline std::optional<Stored> readStored(const Stroke& wit, const QString& kind) {
  const QString* text = attrFind(wit.attrs, "xlSet");
  if (!text) return std::nullopt;
  const QJsonDocument doc = QJsonDocument::fromJson(text->toUtf8());
  if (!doc.isObject()) return std::nullopt;
  Stored s;
  s.obj = doc.object();
  if (s.obj.value("kind").toString() != kind) return std::nullopt;
  const QJsonArray w = s.obj.value("w").toArray();
  if (w.size() < 6) return std::nullopt;
  for (const QJsonValue& v : w) s.w.push_back(v.toDouble(0));
  const QJsonValue wd = s.obj.value("width");
  s.width = wd.isDouble() && std::isfinite(wd.toDouble()) && wd.toDouble() > 0 ? wd.toDouble() : 1.41;
  return s;
}

/** The stroke colour "#rrggbb" and glow of the first piece with role "setbody": the style the object was drawn with. */
inline GenStyle styleOfBody(const std::vector<const Element*>& members, double width) {
  GenStyle st;
  st.width = width;
  for (const Element* m : members) {
    const auto* s = std::get_if<Stroke>(m);
    if (!s || roleOf(*m) != QString("setbody")) continue;
    st.color = attrOr(s->attrs, "color", "#000000ff").left(7);
    st.glow = readGlowAttrs(s->attrs);
    return st;
  }
  st.color = "#000000";
  return st;
}

}  // namespace sets::detail
