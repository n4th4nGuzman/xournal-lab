#include "genobj.h"
#include <QFont>
#include <QFontMetricsF>
#include <QRandomGenerator>
#include <QStringList>
#include <cmath>

static constexpr double LINE_HEIGHT = 1.2;  // keep in sync with the renderer's text drawing

QString fmt8(double v) {
  if (!std::isfinite(v)) return "0";
  return QString::number(v, 'g', 8);
}

QString newObjectId() {
  static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  QString s = "pl";
  for (int i = 0; i < 6; i++) s += QChar(digits[QRandomGenerator::global()->bounded(36)]);
  return s;
}

Stroke strokeEl(const std::vector<double>& pts, const QString& color, const QString& alpha, double width, const QString& role, const QString& id,
                Dash dash, const std::optional<Glow>& glow) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", color + alpha}, {"capStyle", "round"}, {"xlPlane", id}, {"xlRole", role}};
  s.widths = {std::round(width * 1000) / 1000};
  s.pts = pts;
  if (dash != Dash::Solid) s.attrs.emplace_back("style", dash == Dash::Dash ? "dash" : "dot");
  if (glow) {
    s.attrs.emplace_back("glow", QString::number(std::round(glow->size * 100) / 100));
    s.attrs.emplace_back("glowAlpha", QString::number(std::round(glow->intensity * 100) / 100));
  }
  return s;
}

std::array<double, 4> textBBox(const Text& t) {
  const QString family = attrOr(t.attrs, "font", "Sans");
  const double size = attrOr(t.attrs, "size", "12").toDouble();
  const double x = attrOr(t.attrs, "x", "0").toDouble(), y = attrOr(t.attrs, "y", "0").toDouble();
  QFont f(family == "Sans" ? QString("Segoe UI") : family);
  f.setPixelSize(std::max(1, int(std::lround(size))));
  const QFontMetricsF fm(f);
  const QStringList lines = t.text.split('\n');
  double w = 0;
  for (const QString& l : lines) w = std::max(w, fm.horizontalAdvance(l) * size / std::max(1, int(std::lround(size))));
  return {x, y, x + std::max(w, size), y + double(lines.size()) * size * LINE_HEIGHT};
}

void stampText(Text& t) {
  // the fields live in the attributes; this keeps a text built in memory complete as a file
  for (const char* k : {"font", "size", "x", "y", "color"})
    if (!attrFind(t.attrs, k)) attrSet(t.attrs, k, k == QString("font") ? "Sans" : k == QString("color") ? "#000000ff" : "0");
}

Text textEl(const QString& text, V2 at, double size, const QString& color, const QString& id, const QString& role) {
  Text t;
  t.attrs = {{"xlPlane", id}, {"xlRole", role}};
  t.text = text;
  attrSet(t.attrs, "font", "Sans");
  attrSet(t.attrs, "size", fmt8(size));
  attrSet(t.attrs, "x", "0");
  attrSet(t.attrs, "y", "0");
  attrSet(t.attrs, "color", color + "ff");
  const auto b = textBBox(t);
  attrSet(t.attrs, "x", fmt8(at[0] - (b[2] - b[0]) / 2));
  attrSet(t.attrs, "y", fmt8(at[1] - (b[3] - b[1]) / 2));
  return t;
}

std::vector<double> arrowHead(V2 tip, V2 from, double len) {
  const double a = std::atan2(tip[1] - from[1], tip[0] - from[0]);
  const double b1 = a + M_PI - 0.42, b2 = a + M_PI + 0.42;
  return {tip[0] + std::cos(b1) * len, tip[1] + std::sin(b1) * len, tip[0], tip[1], tip[0] + std::cos(b2) * len, tip[1] + std::sin(b2) * len};
}

std::optional<Affine> affineFrom(const std::vector<double>& src, const std::vector<double>& dst) {
  if (src.size() < 6 || dst.size() < 6) return std::nullopt;
  const double x0 = src[0], y0 = src[1], x1 = src[2], y1 = src[3], x2 = src[4], y2 = src[5];
  const double det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (std::abs(det) < 1e-9) return std::nullopt;
  auto solve = [&](double v0, double v1, double v2) {
    const double a = ((v1 - v0) * (y2 - y0) - (v2 - v0) * (y1 - y0)) / det;
    const double b = ((v2 - v0) * (x1 - x0) - (v1 - v0) * (x2 - x0)) / det;
    return std::array<double, 3>{a, b, v0 - a * x0 - b * y0};
  };
  const auto sx = solve(dst[0], dst[2], dst[4]);
  const auto sy = solve(dst[1], dst[3], dst[5]);
  return Affine{sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]};
}

std::vector<double> mapPoints(const Affine& t, const std::vector<double>& pts) {
  std::vector<double> out;
  out.reserve(pts.size());
  for (size_t i = 0; i + 1 < pts.size(); i += 2) {
    out.push_back(t[0] * pts[i] + t[2] * pts[i + 1] + t[4]);
    out.push_back(t[1] * pts[i] + t[3] * pts[i + 1] + t[5]);
  }
  return out;
}

const Attrs& attrsOf(const Element& e) {
  if (const auto* s = std::get_if<Stroke>(&e)) return s->attrs;
  if (const auto* t = std::get_if<Text>(&e)) return t->attrs;
  return std::get<Image>(e).attrs;
}
Attrs& attrsOf(Element& e) {
  if (auto* s = std::get_if<Stroke>(&e)) return s->attrs;
  if (auto* t = std::get_if<Text>(&e)) return t->attrs;
  return std::get<Image>(e).attrs;
}

std::optional<QString> planeIdOf(const Element& e) {
  const QString* v = attrFind(attrsOf(e), "xlPlane");
  return v ? std::optional<QString>(*v) : std::nullopt;
}
std::optional<QString> roleOf(const Element& e) {
  const QString* v = attrFind(attrsOf(e), "xlRole");
  return v ? std::optional<QString>(*v) : std::nullopt;
}

const Stroke* findWitness(const std::vector<const Element*>& members, const QString& role, const QString& attr) {
  for (const Element* m : members)
    if (const auto* s = std::get_if<Stroke>(m))
      if (roleOf(*m) == role && attrFind(s->attrs, attr)) return s;
  return nullptr;
}

Stroke makeWitness(const std::vector<double>& w, const QString& role, const QString& attr, const QString& json, const QString& color, const QString& id) {
  Stroke wit = strokeEl(w, color, "00", 0.3, role, id);
  wit.attrs.emplace_back(attr, json);
  return wit;
}
