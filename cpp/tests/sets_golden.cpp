// The C++ sets module against the TypeScript one: tests/data/sets_golden.json is produced by tests/data/gen_sets_golden.mjs from the .ts sources
// (parser table with error messages and positions, evaluation, number formatting, built number lines and Venn diagrams, sanitizing, anchors).
#include "mathobj.h"
#include "setexpr.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

using namespace sets;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 60) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } } while (0)

static const char* c(const QString& s) { static QByteArray b[8]; static int i = 0; i = (i + 1) % 8; b[i] = s.toUtf8(); return b[i].constData(); }

static QString jsonOf(const QJsonValue& v) {
  if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
  if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
  return v.toVariant().toString();
}

static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static void compareElements(const std::vector<Element>& got, const QJsonArray& want, const QString& what) {
  CHECK(got.size() == size_t(want.size()), "%s: %zu elements, want %d", c(what), got.size(), int(want.size()));
  if (got.size() != size_t(want.size())) return;
  for (int i = 0; i < want.size(); i++) {
    const QJsonObject w = want[i].toObject();
    const Element& e = got[size_t(i)];
    const QString role = w.value("role").toString();
    CHECK(roleOf(e) == role, "%s #%d role %s want %s", c(what), i, c(roleOf(e).value_or("?")), c(role));
    if (w.value("k").toString() == "s") {
      const Stroke* s = std::get_if<Stroke>(&e);
      CHECK(s, "%s #%d should be a stroke", c(what), i);
      if (!s) continue;
      CHECK(s->color() == w.value("color").toString(), "%s #%d color", c(what), i);
      CHECK(near(s->widths.at(0), w.value("width").toDouble(), 1e-3), "%s #%d width %g want %g", c(what), i, s->widths.at(0), w.value("width").toDouble());
      const QJsonArray pts = w.value("pts").toArray();
      bool ok = s->pts.size() == size_t(pts.size());
      for (size_t k = 0; ok && k < s->pts.size(); k++) ok = near(s->pts[k], pts[int(k)].toDouble(), 2e-5);
      CHECK(ok, "%s #%d (%s) points differ (%zu vs %d)", c(what), i, c(role), s->pts.size(), int(pts.size()));
      if (role == "setwitness") {
        const QString want_ = w.value("xlSet").toString();
        CHECK(attrOr(s->attrs, "xlSet") == want_, "%s witness JSON differs:\n  got  %s\n  want %s", c(what), c(attrOr(s->attrs, "xlSet")), c(want_));
      }
    } else {
      const Text* t = std::get_if<Text>(&e);
      CHECK(t, "%s #%d should be a text", c(what), i);
      if (!t) continue;
      CHECK(t->text == w.value("text").toString(), "%s #%d text %s want %s", c(what), i, c(t->text), c(w.value("text").toString()));
      CHECK(near(attrOr(t->attrs, "size").toDouble(), w.value("size").toDouble(), 1e-6), "%s #%d size", c(what), i);
    }
  }
}

static void compareLine(const LineSpec& g, const QJsonObject& w, const QString& what) {
  CHECK(jsonOf(QJsonDocument::fromJson(lineSpecJson(g).toUtf8()).object()) == jsonOf(w), "%s spec\n  got  %s\n  want %s", c(what), c(lineSpecJson(g)), c(jsonOf(w)));
}
static void compareVenn(const VennSpec& g, const QJsonObject& w, const QString& what) {
  CHECK(jsonOf(QJsonDocument::fromJson(vennSpecJson(g).toUtf8()).object()) == jsonOf(w), "%s spec\n  got  %s\n  want %s", c(what), c(vennSpecJson(g)), c(jsonOf(w)));
}

static QString regionsText(const std::vector<bool>& v) {
  QString s;
  for (bool b : v) s += b ? '1' : '0';
  return s;
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QFile f("tests/data/sets_golden.json");
  if (!f.open(QIODevice::ReadOnly)) { std::printf("cannot open tests/data/sets_golden.json (run from the cpp directory)\n"); return 1; }
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

  // ---- parser table: errors with message and position, evaluation on the line and on 2 and 3 sets
  for (const QJsonValue& cv : root.value("evalCases").toArray()) {
    const QJsonObject o = cv.toObject();
    const QString src = o.value("src").toString();
    const Parsed p = parseSetExpr(src);
    CHECK(p.ok == o.value("ok").toBool(), "'%s' parse ok=%d want %d (%s)", c(src), p.ok, int(o.value("ok").toBool()), c(p.err.msg));
    if (p.ok != o.value("ok").toBool()) continue;
    if (!p.ok) {
      CHECK(p.err.pos == o.value("pos").toInt() && p.err.len == o.value("len").toInt(), "'%s' error place %d,%d want %d,%d", c(src), p.err.pos, p.err.len, o.value("pos").toInt(), o.value("len").toInt());
      CHECK(p.err.msg == o.value("msg").toString(), "'%s' error text '%s' want '%s'", c(src), c(p.err.msg), c(o.value("msg").toString()));
      continue;
    }
    QStringList names;
    for (const QJsonValue& n : o.value("names").toArray()) names << n.toString();
    CHECK(namesIn(p.expr) == names, "'%s' names", c(src));
    const auto e = evalReal(p.expr);
    if (o.contains("real")) CHECK(e.ok && formatSet(e.value) == o.value("real").toString(), "'%s' real '%s' want '%s'", c(src), c(e.ok ? formatSet(e.value) : e.msg), c(o.value("real").toString()));
    else CHECK(!e.ok && e.msg == o.value("realErr").toString(), "'%s' real error '%s' want '%s'", c(src), c(e.msg), c(o.value("realErr").toString()));
    for (const auto& [key, nm] : {std::pair<QString, QStringList>{"reg2", {"A", "B"}}, {"reg3", {"A", "B", "C"}}}) {
      const auto g = evalRegions(p.expr, nm);
      if (o.contains(key)) CHECK(g.ok && regionsText(g.value) == o.value(key).toString(), "'%s' %s", c(src), c(key));
      else CHECK(!g.ok && g.msg == o.value(key + "Err").toString(), "'%s' %s error '%s' want '%s'", c(src), c(key), c(g.msg), c(o.value(key + "Err").toString()));
    }
  }

  // ---- number formatting
  for (const QJsonValue& cv : root.value("fmt").toArray()) {
    const QJsonObject o = cv.toObject();
    double v = o.value("v").isString() ? (o.value("v").toString() == "inf" ? INFINITY : -INFINITY) : o.value("v").toDouble();
    if (!o.value("v").isString()) {
      // the golden stores the double through JSON text: recompute the exact special values
      const double want = o.value("v").toDouble();
      for (double cand : {M_PI, -M_PI, M_PI / 2, 3 * M_PI / 2, 2 * M_PI, M_PI / 3, M_PI / 4, M_PI / 6, 5 * M_PI / 6, 13 * M_PI, 1.0 / 3, M_SQRT2})
        if (near(cand, want, 1e-15)) v = cand;
    }
    CHECK(formatNumber(v) == o.value("s").toString(), "formatNumber(%.17g) = %s want %s", v, c(formatNumber(v)), c(o.value("s").toString()));
  }

  // ---- sanitizing arbitrary JSON
  for (const QJsonValue& cv : root.value("sanitize").toArray()) {
    const QJsonObject o = cv.toObject();
    compareLine(sanitizeLine(o.value("raw")), o.value("spec").toObject(), "sanitizeLine " + jsonOf(o.value("raw")).left(80));
  }
  for (const QJsonValue& cv : root.value("sanVenn").toArray()) {
    const QJsonObject o = cv.toObject();
    compareVenn(sanitizeVenn(o.value("raw")), o.value("spec").toObject(), "sanitizeVenn " + jsonOf(o.value("raw")).left(80));
  }

  // ---- number lines
  GenStyle style;
  style.color = "#0044cc";
  style.width = 1.41;
  int n = 0;
  for (const QJsonValue& cv : root.value("lines").toArray()) {
    const QJsonObject o = cv.toObject();
    const QString what = QString("line %1").arg(n++);
    const LineSpec spec = sanitizeLine(o.value("raw"));
    compareLine(spec, o.value("spec").toObject(), what);
    compareElements(buildLine("plTEST", spec, style), o.value("els").toArray(), what);
  }
  for (const QJsonValue& cv : root.value("lineMove").toArray()) {
    const QJsonObject o = cv.toObject();
    const LineSpec base = sanitizeLine(root.value("base"));
    const QJsonArray p = o.value("p").toArray();
    compareLine(moveLineAnchor(base, o.value("i").toInt(), {p[0].toDouble(), p[1].toDouble()}, o.value("shift").toBool()), o.value("spec").toObject(), "moveLineAnchor");
  }

  // ---- Venn diagrams
  n = 0;
  for (const QJsonValue& cv : root.value("venn").toArray()) {
    const QJsonObject o = cv.toObject();
    const QString what = QString("venn %1").arg(n++);
    const VennSpec spec = sanitizeVenn(o.value("raw"));
    compareVenn(spec, o.value("spec").toObject(), what);
    const Shading sh = shadedRegions(spec);
    CHECK((sh.shaded.empty() ? QString("null") : regionsText(sh.shaded)) == (o.value("shaded").isNull() ? QString("null") : o.value("shaded").toString()), "%s shaded", c(what));
    CHECK(sh.error == (o.value("error").isNull() ? QString() : o.value("error").toString()), "%s error '%s'", c(what), c(sh.error));
    const auto els = elementsOf(spec);
    const QJsonArray we = o.value("elements").toArray();
    CHECK(els.size() == size_t(we.size()), "%s element count", c(what));
    for (size_t i = 0; i < els.size() && int(i) < we.size(); i++) CHECK(els[i].label == we[int(i)].toObject().value("label").toString() && els[i].mask == we[int(i)].toObject().value("mask").toInt(), "%s element %zu", c(what), i);
    const auto placed = placeElements(spec);
    const QJsonArray wp = o.value("placed").toArray();
    CHECK(placed.size() == size_t(wp.size()), "%s placed count", c(what));
    for (size_t i = 0; i < placed.size() && int(i) < wp.size(); i++) {
      const QJsonArray at = wp[int(i)].toObject().value("at").toArray();
      CHECK(near(placed[i].at[0], at[0].toDouble(), 1e-4) && near(placed[i].at[1], at[1].toDouble(), 1e-4), "%s placed %zu (%g,%g) want (%g,%g)", c(what), i, placed[i].at[0], placed[i].at[1], at[0].toDouble(), at[1].toDouble());
    }
    compareElements(buildVenn("plTEST", spec, style), o.value("els").toArray(), what);
  }
  for (const QJsonValue& cv : root.value("vennMove").toArray()) {
    const QJsonObject o = cv.toObject();
    const VennSpec v0 = sanitizeVenn(root.value("v0"));
    const QJsonArray p = o.value("p").toArray();
    compareVenn(moveVennAnchor(v0, o.value("i").toInt(), {p[0].toDouble(), p[1].toDouble()}, false), o.value("spec").toObject(), "moveVennAnchor");
  }

  std::printf("sets_golden: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
