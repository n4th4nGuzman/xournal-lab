#include "plane.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <algorithm>
#include <cmath>

const PlaneSpec DEFAULT_PLANE{};

namespace genjs {
double jsRound(double v) { return std::floor(v + 0.5); }
double round6(double v) { return jsRound(v * 1e6) / 1e6; }

QString num(double v) {
  if (!std::isfinite(v)) return "null";
  if (v == 0) return "0";
  if (v == std::floor(v) && std::abs(v) < 9e15) return QString::number(static_cast<qlonglong>(v));
  return QString::number(v, 'g', QLocale::FloatingPointShortest);
}

QString str(const QString& s) {
  const QByteArray j = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);   // ["..."]
  return QString::fromUtf8(j.mid(1, j.size() - 2));
}

QString numArray(const std::vector<double>& v) {
  QString out = "[";
  for (size_t i = 0; i < v.size(); i++) out += (i ? "," : "") + num(v[i]);
  return out + "]";
}

std::optional<Glow> glowOf(const Stroke& s) {
  const QString* g = attrFind(s.attrs, "glow");
  const double size = g ? g->toDouble() : 0;
  if (!(size > 0)) return std::nullopt;
  const QString* ga = attrFind(s.attrs, "glowAlpha");
  bool ok = true;
  const double a = ga ? ga->toDouble(&ok) : 0.8;
  return Glow{std::min(40.0, size), ok && std::isfinite(a) ? std::min(1.0, std::max(0.05, a)) : 0.8};
}

std::array<double, 4> bboxOf(const std::vector<double>& pts) {
  if (pts.size() < 2) return {0, 0, 0, 0};
  std::array<double, 4> b{pts[0], pts[1], pts[0], pts[1]};
  for (size_t i = 0; i + 1 < pts.size(); i += 2) {
    b[0] = std::min(b[0], pts[i]); b[2] = std::max(b[2], pts[i]);
    b[1] = std::min(b[1], pts[i + 1]); b[3] = std::max(b[3], pts[i + 1]);
  }
  return b;
}

QString hexColorOf(const Stroke& s) { return attrOr(s.attrs, "color", "#000000ff").left(7); }

double numOr(const QJsonObject& o, const QString& key, double def) {
  const QJsonValue v = o.value(key);
  return v.isDouble() && std::isfinite(v.toDouble()) ? v.toDouble() : def;
}
}  // namespace genjs

using genjs::jsRound;
using genjs::round6;

static constexpr int MAX_LINES = 120;
static constexpr int SUBS[] = {0, 2, 4, 5, 10};
static double clampd(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
static double finiteOr(double v, double d) { return std::isfinite(v) ? v : d; }
static int subIndex(int sub) { for (int i = 0; i < 5; i++) if (SUBS[i] == sub) return i; return -1; }

PlaneSpec sanitizePlane(const PlaneSpec& raw) {
  const PlaneSpec& d = DEFAULT_PLANE;
  const double xstep = clampd(finiteOr(raw.xstep, d.xstep), 1e-6, 1e6);
  const double ystep = clampd(finiteOr(raw.ystep, d.ystep), 1e-6, 1e6);
  double xmin = finiteOr(raw.xmin, d.xmin), xmax = finiteOr(raw.xmax, d.xmax), ymin = finiteOr(raw.ymin, d.ymin), ymax = finiteOr(raw.ymax, d.ymax);
  xmin = std::floor(xmin / xstep + 1e-9) * xstep; xmax = std::ceil(xmax / xstep - 1e-9) * xstep;
  ymin = std::floor(ymin / ystep + 1e-9) * ystep; ymax = std::ceil(ymax / ystep - 1e-9) * ystep;
  if (xmax <= xmin) xmax = xmin + xstep;
  if (ymax <= ymin) ymax = ymin + ystep;
  PlaneSpec s;
  s.xmin = round6(xmin); s.xmax = round6(xmax); s.ymin = round6(ymin); s.ymax = round6(ymax);
  s.xstep = round6(xstep); s.ystep = round6(ystep);
  s.grid = raw.grid; s.sub = subIndex(raw.sub) >= 0 ? raw.sub : d.sub;
  s.ticks = raw.ticks; s.numbers = raw.numbers; s.arrows = raw.arrows;
  s.xlabel = raw.xlabel.left(12); s.ylabel = raw.ylabel.left(12);
  s.square = raw.square;
  // too many lines is unreadable and slow: widen the step
  while ((s.xmax - s.xmin) / s.xstep > MAX_LINES) {
    s.xstep = round6(s.xstep * 2);
    s.xmin = std::floor(s.xmin / s.xstep) * s.xstep; s.xmax = std::ceil(s.xmax / s.xstep) * s.xstep;
  }
  while ((s.ymax - s.ymin) / s.ystep > MAX_LINES) {
    s.ystep = round6(s.ystep * 2);
    s.ymin = std::floor(s.ymin / s.ystep) * s.ystep; s.ymax = std::ceil(s.ymax / s.ystep) * s.ystep;
  }
  while (s.sub && ((s.xmax - s.xmin) / s.xstep + (s.ymax - s.ymin) / s.ystep) * s.sub > 600) s.sub = SUBS[std::max(0, subIndex(s.sub) - 1)];
  return s;
}

static bool flag(const QJsonObject& o, const QString& key) {
  const QJsonValue v = o.value(key);
  if (v.isBool()) return v.toBool();
  if (v.isDouble()) return v.toDouble() != 0;
  return true;   // absent: on
}

PlaneSpec planeFromJson(const QJsonObject& o) {
  const PlaneSpec& d = DEFAULT_PLANE;
  PlaneSpec s;
  s.xmin = genjs::numOr(o, "xmin", d.xmin); s.xmax = genjs::numOr(o, "xmax", d.xmax);
  s.ymin = genjs::numOr(o, "ymin", d.ymin); s.ymax = genjs::numOr(o, "ymax", d.ymax);
  s.xstep = genjs::numOr(o, "xstep", d.xstep); s.ystep = genjs::numOr(o, "ystep", d.ystep);
  const double sub = genjs::numOr(o, "sub", d.sub);
  s.sub = sub == std::floor(sub) && std::abs(sub) < 1000 ? int(sub) : d.sub;
  s.grid = flag(o, "grid"); s.ticks = flag(o, "ticks"); s.numbers = flag(o, "numbers"); s.arrows = flag(o, "arrows"); s.square = flag(o, "square");
  s.xlabel = o.value("xlabel").isString() ? o.value("xlabel").toString() : d.xlabel;
  s.ylabel = o.value("ylabel").isString() ? o.value("ylabel").toString() : d.ylabel;
  return sanitizePlane(s);
}

QString encodeSpec(const PlaneSpec& s) {
  using namespace genjs;
  return "{\"xmin\":" + num(s.xmin) + ",\"xmax\":" + num(s.xmax) + ",\"ymin\":" + num(s.ymin) + ",\"ymax\":" + num(s.ymax) +
         ",\"xstep\":" + num(s.xstep) + ",\"ystep\":" + num(s.ystep) + ",\"grid\":" + (s.grid ? "1" : "0") + ",\"sub\":" + QString::number(s.sub) +
         ",\"ticks\":" + (s.ticks ? "1" : "0") + ",\"numbers\":" + (s.numbers ? "1" : "0") + ",\"arrows\":" + (s.arrows ? "1" : "0") +
         ",\"xlabel\":" + str(s.xlabel) + ",\"ylabel\":" + str(s.ylabel) + ",\"square\":" + (s.square ? "1" : "0") + "}";
}

std::optional<PlaneSpec> decodeSpec(const QString& text) {
  if (text.isEmpty()) return std::nullopt;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
  return planeFromJson(doc.object());
}

Members planeMembers(const Layer& layer, const QString& id) {
  Members out;
  for (const Element& e : layer.elements)
    if (planeIdOf(e) == id) out.push_back(&e);
  return out;
}

std::vector<size_t> planeMemberIndices(const Layer& layer, const QString& id) {
  std::vector<size_t> out;
  for (size_t i = 0; i < layer.elements.size(); i++)
    if (planeIdOf(layer.elements[i]) == id) out.push_back(i);
  return out;
}

static const Stroke* strokeWithRole(const Members& members, const QString& role) {
  for (const Element* m : members)
    if (const auto* s = std::get_if<Stroke>(m))
      if (roleOf(*m) == role) return s;
  return nullptr;
}

std::optional<PlaneSpec> planeSpecOf(const Members& members) {
  for (const Element* m : members) {
    const QString* t = attrFind(attrsOf(*m), "xlPlaneSpec");
    if (!t) continue;
    if (auto s = decodeSpec(*t)) return s;
  }
  return std::nullopt;
}

std::optional<Frame> planeFrameOf(const Members& members) {
  const Stroke* ax = strokeWithRole(members, "axisx");
  const Stroke* ay = strokeWithRole(members, "axisy");
  if (!ax || !ay) return std::nullopt;
  const auto bx = genjs::bboxOf(ax->pts), by = genjs::bboxOf(ay->pts);
  return Frame{bx[0], by[1], bx[2], by[3]};
}

std::optional<PlaneStyle> planeStyleOf(const Members& members) {
  const Stroke* ax = strokeWithRole(members, "axisx");
  if (!ax) return std::nullopt;
  PlaneStyle st;
  st.color = genjs::hexColorOf(*ax);
  const QString* wa = attrFind(ax->attrs, "xlPlaneW");
  bool ok = false;
  const double w = wa ? wa->toDouble(&ok) : 0;
  st.width = ok && std::isfinite(w) && w > 0 ? w : (!ax->widths.empty() ? ax->widths[0] : 1.41);
  st.glow = genjs::glowOf(*ax);
  return st;
}

Frame squareFrame(const PlaneSpec& spec, const Frame& f) {
  const double rx = spec.xmax - spec.xmin, ry = spec.ymax - spec.ymin;
  const double k = std::min((f.x1 - f.x0) / rx, (f.y1 - f.y0) / ry);
  const double w = rx * k, h = ry * k;
  const double cx = (f.x0 + f.x1) / 2, cy = (f.y0 + f.y1) / 2;
  return {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
}

static std::vector<double> multiples(double min, double max, double step) {
  std::vector<double> out;
  const long long n0 = static_cast<long long>(jsRound(min / step)), n1 = static_cast<long long>(jsRound(max / step));
  for (long long n = n0; n <= n1; n++) out.push_back(round6(double(n) * step));
  return out;
}

static QString fmtNum(double v) {
  QString s = QString::number(round6(v), 'f', 6);
  if (s.contains('.')) { while (s.endsWith('0')) s.chop(1); if (s.endsWith('.')) s.chop(1); }
  if (s == "-0") return "0";
  return s.replace('-', QChar(0x2212));
}

enum class HAlign { Center, Right, Left };

static Text labelEl(const QString& text, double x, double y, double size, const QString& color, const QString& id, HAlign align, bool vcenter) {
  Text t = textEl(text, {0, 0}, size, color, id, "label");
  const auto b = textBBox(t);
  const double w = b[2] - b[0], h = b[3] - b[1];
  attrSet(t.attrs, "x", fmt8(align == HAlign::Center ? x - w / 2 : align == HAlign::Right ? x - w : x));
  attrSet(t.attrs, "y", fmt8(vcenter ? y - h / 2 : y));
  return t;
}

std::vector<Element> buildPlane(const QString& id, const PlaneSpec& rawSpec, const Frame& frame, const PlaneStyle& style) {
  using genjs::num;
  const PlaneSpec spec = sanitizePlane(rawSpec);
  const double x0 = frame.x0, y0 = frame.y0, x1 = frame.x1, y1 = frame.y1;
  const double rx = spec.xmax - spec.xmin, ry = spec.ymax - spec.ymin;
  auto px = [&](double x) { return x0 + ((x - spec.xmin) / rx) * (x1 - x0); };
  auto py = [&](double y) { return y1 - ((y - spec.ymin) / ry) * (y1 - y0); };
  const double w = std::max(0.3, style.width);
  std::vector<Element> out;
  const QString& color = style.color;
  const auto xs = multiples(spec.xmin, spec.xmax, spec.xstep), ys = multiples(spec.ymin, spec.ymax, spec.ystep);

  // grid: one serpentine stroke per direction (its connectors run along the frame edge, which is a grid line too)
  auto serpentine = [&](const std::vector<double>& vals, bool vertical) {
    std::vector<double> pts;
    for (size_t i = 0; i < vals.size(); i++) {
      const double v = vals[i];
      const bool odd = i % 2;
      if (vertical) pts.insert(pts.end(), {px(v), odd ? y0 : y1, px(v), odd ? y1 : y0});
      else pts.insert(pts.end(), {odd ? x1 : x0, py(v), odd ? x0 : x1, py(v)});
    }
    return pts;
  };
  if (spec.grid) {
    if (spec.sub > 1) {
      auto lighter = [&](double lo, double hi, double step) {
        std::vector<double> v;
        for (double m : multiples(lo, hi, step / spec.sub))
          if (std::abs(m / step - jsRound(m / step)) > 1e-6) v.push_back(m);
        return v;
      };
      const auto mx = lighter(spec.xmin, spec.xmax, spec.xstep), my = lighter(spec.ymin, spec.ymax, spec.ystep);
      if (mx.size() > 1) out.push_back(strokeEl(serpentine(mx, true), color, "33", std::max(0.25, w * 0.3), "subgrid", id));
      if (my.size() > 1) out.push_back(strokeEl(serpentine(my, false), color, "33", std::max(0.25, w * 0.3), "subgrid", id));
    }
    if (xs.size() > 1) out.push_back(strokeEl(serpentine(xs, true), color, "66", std::max(0.3, w * 0.5), "grid", id));
    if (ys.size() > 1) out.push_back(strokeEl(serpentine(ys, false), color, "66", std::max(0.3, w * 0.5), "grid", id));
  }

  // axes cross at 0 when it is inside the range, otherwise they sit on the frame edge
  const double ya = spec.ymin <= 0 && spec.ymax >= 0 ? py(0) : py(spec.ymin);
  const double xa = spec.xmin <= 0 && spec.xmax >= 0 ? px(0) : px(spec.xmin);
  const double tl = clampd(w * 0.9 + 1.2, 2, 3.6);       // half of a tick
  const double head = clampd(w * 1.4 + 3.5, 4.5, 7.5);   // length of an arrow head
  const double tickW = std::max(0.3, w * 0.6), arrowW = std::max(0.3, w * 0.8);

  // the axes are plain lines across the whole frame (resizing reads the frame from them); ticks and arrows are thinner strokes of their own
  Stroke ax = strokeEl({x0, ya, x1, ya}, color, "ff", w, "axisx", id, Dash::Solid, style.glow);
  ax.attrs.emplace_back("xlPlaneSpec", encodeSpec(spec));
  ax.attrs.emplace_back("xlPlaneW", num(std::round(w * 1000) / 1000));
  out.push_back(std::move(ax));
  out.push_back(strokeEl({xa, y1, xa, y0}, color, "ff", w, "axisy", id, Dash::Solid, style.glow));
  if (spec.ticks) {
    std::vector<double> tx, ty;
    for (double v : xs) {
      const double X = px(v);
      if (std::abs(X - xa) < 0.01 || X <= x0 + 0.01 || X >= x1 - 0.01) continue;
      tx.insert(tx.end(), {X, ya, X, ya - tl, X, ya + tl, X, ya});   // between ticks the pen travels along the axis, under its thicker line
    }
    for (auto it = ys.rbegin(); it != ys.rend(); ++it) {
      const double Y = py(*it);
      if (std::abs(Y - ya) < 0.01 || Y >= y1 - 0.01 || Y <= y0 + 0.01) continue;
      ty.insert(ty.end(), {xa, Y, xa - tl, Y, xa + tl, Y, xa, Y});
    }
    if (!tx.empty()) out.push_back(strokeEl(tx, color, "ff", tickW, "ticks", id, Dash::Solid, style.glow));
    if (!ty.empty()) out.push_back(strokeEl(ty, color, "ff", tickW, "ticks", id, Dash::Solid, style.glow));
  }
  if (spec.arrows) {
    out.push_back(strokeEl({x1 - head, ya - head * 0.38, x1, ya, x1 - head, ya + head * 0.38}, color, "ff", arrowW, "arrow", id, Dash::Solid, style.glow));
    out.push_back(strokeEl({xa - head * 0.38, y0 + head, xa, y0, xa + head * 0.38, y0 + head}, color, "ff", arrowW, "arrow", id, Dash::Solid, style.glow));
  }

  // numbers and axis names
  const double cellX = std::abs((x1 - x0) * spec.xstep / rx), cellY = std::abs((y1 - y0) * spec.ystep / ry);
  const double size = clampd(std::min(cellX, cellY) * 0.36, 6.5, 11);
  if (spec.numbers) {
    size_t widestLen = 0;
    for (double v : xs) widestLen = std::max(widestLen, size_t(fmtNum(v).size()));
    const double widest = double(widestLen) * size * 0.62;
    const long long everyX = std::max(1.0, std::ceil((widest * 1.4) / cellX)), everyY = std::max(1.0, std::ceil((size * 1.7) / cellY));
    for (size_t i = 0; i < xs.size(); i++) {
      if (std::abs(px(xs[i]) - xa) < 0.01 || (long long)i % everyX) continue;
      out.push_back(labelEl(fmtNum(xs[i]), px(xs[i]), ya + tl + 1.5, size, color, id, HAlign::Center, false));
    }
    for (size_t i = 0; i < ys.size(); i++) {
      if (std::abs(py(ys[i]) - ya) < 0.01 || (long long)i % everyY) continue;
      out.push_back(labelEl(fmtNum(ys[i]), xa - tl - 2, py(ys[i]), size, color, id, HAlign::Right, true));
    }
  }
  const double nameSize = clampd(size * 1.15, 8, 13);
  if (!spec.xlabel.isEmpty()) out.push_back(labelEl(spec.xlabel, x1 + 3, ya, nameSize, color, id, HAlign::Left, true));
  if (!spec.ylabel.isEmpty()) out.push_back(labelEl(spec.ylabel, xa, y0 - nameSize * 1.25, nameSize, color, id, HAlign::Center, false));
  return out;
}
