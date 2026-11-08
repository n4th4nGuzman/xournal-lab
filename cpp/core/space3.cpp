#include "space3.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

using genjs::jsRound;
using genjs::num;
using genjs::round6;

static constexpr int MAX_LINES = 40;
static double clampd(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
static double finiteOr(double v, double d) { return std::isfinite(v) ? v : d; }
static const double NOT_A_NUMBER = std::nan("");

V2 axisVector(double deg, double len) {
  const double a = deg * M_PI / 180;
  return {std::cos(a) * len, -std::sin(a) * len};
}

Angle vectorAngle(const V2& v) {
  const double deg = std::atan2(-v[1], v[0]) * 180 / M_PI;
  return {std::fmod(deg + 360, 360), std::hypot(v[0], v[1])};
}

std::array<V2, 3> defaultVectors(double scale) { return {axisVector(215, 24 * scale), axisVector(0, 34 * scale), axisVector(90, 34 * scale)}; }

Space3Spec::Space3Spec() : u(defaultVectors()), n(defaultVectors()) {}
const Space3Spec DEFAULT_SPACE3{};

static V2 vecOr(const V2& v, const V2& d) { return std::isfinite(v[0]) && std::isfinite(v[1]) ? v : d; }

Space3Spec sanitizeSpace3(const Space3Spec& raw) {
  const Space3Spec& d = DEFAULT_SPACE3;
  struct Ax { double lo, hi, st; };
  auto axis = [](double min, double max, double step, double dmin, double dmax, double dstep) {
    const double st = clampd(finiteOr(step, dstep), 1e-6, 1e6);
    double lo = std::floor(finiteOr(min, dmin) / st + 1e-9) * st, hi = std::ceil(finiteOr(max, dmax) / st - 1e-9) * st;
    if (hi <= lo) hi = lo + st;
    double s = st;
    while ((hi - lo) / s > MAX_LINES) { s *= 2; lo = std::floor(lo / s) * s; hi = std::ceil(hi / s) * s; }
    return Ax{round6(lo), round6(hi), round6(s)};
  };
  const Ax x = axis(raw.xmin, raw.xmax, raw.xstep, d.xmin, d.xmax, d.xstep);
  const Ax y = axis(raw.ymin, raw.ymax, raw.ystep, d.ymin, d.ymax, d.ystep);
  const Ax z = axis(raw.zmin, raw.zmax, raw.zstep, d.zmin, d.zmax, d.zstep);
  Space3Spec s;
  s.xmin = x.lo; s.xmax = x.hi; s.xstep = x.st; s.ymin = y.lo; s.ymax = y.hi; s.ystep = y.st; s.zmin = z.lo; s.zmax = z.hi; s.zstep = z.st;
  s.gridXY = raw.gridXY; s.gridXZ = raw.gridXZ; s.gridYZ = raw.gridYZ;
  s.ticks = raw.ticks; s.numbers = raw.numbers; s.arrows = raw.arrows;
  for (int i = 0; i < 3; i++) {
    s.names[i] = raw.names[i].left(12);
    s.u[i] = vecOr(raw.u[i], d.u[i]);
  }
  s.o = vecOr(raw.o, d.o);
  for (int i = 0; i < 3; i++) s.n[i] = vecOr(raw.n[i], s.u[i]);   // a space saved before the halves were independent has straight axes
  return s;
}

static V2 vecFromJson(const QJsonValue& v) {
  if (v.isArray()) {
    const QJsonArray a = v.toArray();
    if (a.size() == 2 && a[0].isDouble() && a[1].isDouble()) return {a[0].toDouble(), a[1].toDouble()};
  }
  return {NOT_A_NUMBER, NOT_A_NUMBER};
}

static bool flagOr(const QJsonValue& v, bool whenAbsent) {
  if (v.isBool()) return v.toBool();
  if (v.isDouble()) return v.toDouble() != 0;
  return whenAbsent;
}

Space3Spec space3FromJson(const QJsonObject& o) {
  const Space3Spec& d = DEFAULT_SPACE3;
  Space3Spec s;
  s.xmin = genjs::numOr(o, "xmin", d.xmin); s.xmax = genjs::numOr(o, "xmax", d.xmax); s.xstep = genjs::numOr(o, "xstep", d.xstep);
  s.ymin = genjs::numOr(o, "ymin", d.ymin); s.ymax = genjs::numOr(o, "ymax", d.ymax); s.ystep = genjs::numOr(o, "ystep", d.ystep);
  s.zmin = genjs::numOr(o, "zmin", d.zmin); s.zmax = genjs::numOr(o, "zmax", d.zmax); s.zstep = genjs::numOr(o, "zstep", d.zstep);
  s.gridXY = flagOr(o.value("gridXY"), true);
  s.gridXZ = o.value("gridXZ").isBool() || o.value("gridXZ").isDouble() ? flagOr(o.value("gridXZ"), false) : false;
  s.gridYZ = o.value("gridYZ").isBool() || o.value("gridYZ").isDouble() ? flagOr(o.value("gridYZ"), false) : false;
  s.ticks = flagOr(o.value("ticks"), true); s.numbers = flagOr(o.value("numbers"), true); s.arrows = flagOr(o.value("arrows"), true);
  const QJsonArray names = o.value("names").toArray();
  for (int i = 0; i < 3; i++) s.names[i] = i < names.size() && names[i].isString() ? names[i].toString() : d.names[i];
  s.o = vecFromJson(o.value("o"));
  const QJsonArray uu = o.value("u").toArray(), nn = o.value("n").toArray();
  for (int i = 0; i < 3; i++) { s.u[i] = vecFromJson(uu.at(i)); s.n[i] = vecFromJson(nn.at(i)); }
  return sanitizeSpace3(s);
}

static QString vecJson(const V2& v) { return "[" + num(v[0]) + "," + num(v[1]) + "]"; }

QString encodeSpace3(const Space3Spec& s) {
  auto b = [](bool v) { return QString(v ? "true" : "false"); };
  QString u, n;
  for (int i = 0; i < 3; i++) { u += (i ? "," : "") + vecJson(s.u[i]); n += (i ? "," : "") + vecJson(s.n[i]); }
  // key order is the one sanitizeSpace3 produces in the Electron app
  return "{\"xmin\":" + num(s.xmin) + ",\"xmax\":" + num(s.xmax) + ",\"xstep\":" + num(s.xstep) +
         ",\"ymin\":" + num(s.ymin) + ",\"ymax\":" + num(s.ymax) + ",\"ystep\":" + num(s.ystep) +
         ",\"zmin\":" + num(s.zmin) + ",\"zmax\":" + num(s.zmax) + ",\"zstep\":" + num(s.zstep) +
         ",\"gridXY\":" + b(s.gridXY) + ",\"gridXZ\":" + b(s.gridXZ) + ",\"gridYZ\":" + b(s.gridYZ) +
         ",\"ticks\":" + b(s.ticks) + ",\"numbers\":" + b(s.numbers) + ",\"arrows\":" + b(s.arrows) +
         ",\"names\":[" + genjs::str(s.names[0]) + "," + genjs::str(s.names[1]) + "," + genjs::str(s.names[2]) + "]" +
         ",\"o\":" + vecJson(s.o) + ",\"u\":[" + u + "],\"n\":[" + n + "]}";
}

std::optional<Space3Spec> decodeSpace3(const QString& text) {
  if (text.isEmpty()) return std::nullopt;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
  return space3FromJson(doc.object());
}

V2 project(const Space3Spec& s, double x, double y, double z) {
  const double c[3] = {x, y, z};
  double px = s.o[0], py = s.o[1];
  for (int i = 0; i < 3; i++) {
    const V2& w = c[i] >= 0 ? s.u[i] : s.n[i];   // each half of an axis has its own vector
    px += c[i] * w[0];
    py += c[i] * w[1];
  }
  return {px, py};
}

struct Range { double lo, hi, step; };
static Range rangeOf(const Space3Spec& s, int i) {
  return i == 0 ? Range{s.xmin, s.xmax, s.xstep} : i == 1 ? Range{s.ymin, s.ymax, s.ystep} : Range{s.zmin, s.zmax, s.zstep};
}
/** Where the other axes cross an axis: at 0 when the range has it, else at the range's nearer end. */
static double crossing(const Space3Spec& s, int i) { const Range r = rangeOf(s, i); return clampd(0, r.lo, r.hi); }
static V2 axisPoint(const Space3Spec& s, int i, double v) {
  double c[3] = {crossing(s, 0), crossing(s, 1), crossing(s, 2)};
  c[i] = v;
  return project(s, c[0], c[1], c[2]);
}

std::vector<Space3Anchor> anchorsOf(const Space3Spec& s) {
  std::vector<Space3Anchor> out;
  for (int i = 0; i < 3; i++) {
    const Range r = rangeOf(s, i);
    const double c = crossing(s, i);
    if (r.hi > c) out.push_back({i, AnchorSide::Plus, axisPoint(s, i, r.hi), r.hi});
    if (r.lo < c) out.push_back({i, AnchorSide::Minus, axisPoint(s, i, r.lo), r.lo});
  }
  return out;
}

Space3Spec withAnchor(const Space3Spec& s, int i, AnchorSide side, V2 p, bool keepLength) {
  const auto anchors = anchorsOf(s);
  const auto a = std::find_if(anchors.begin(), anchors.end(), [&](const Space3Anchor& x) { return x.axis == i && x.side == side; });
  if (a == anchors.end()) return s;
  // where the crossing is, without this axis's own share: the end of the half is that point plus value * (the half's vector)
  const double c[3] = {crossing(s, 0), crossing(s, 1), crossing(s, 2)};
  double bx = s.o[0], by = s.o[1];
  for (int j = 0; j < 3; j++) {
    if (j == i) continue;
    const V2& w = c[j] >= 0 ? s.u[j] : s.n[j];
    bx += c[j] * w[0];
    by += c[j] * w[1];
  }
  const V2& own = side == AnchorSide::Plus ? s.u[i] : s.n[i];
  double ux = (p[0] - bx) / a->value, uy = (p[1] - by) / a->value;
  if (keepLength) {
    const double old = std::hypot(own[0], own[1]);
    double now = std::hypot(ux, uy);
    if (now == 0) now = 1;
    ux = ux / now * old;
    uy = uy / now * old;
  }
  if (std::hypot(ux, uy) < 2) return s;   // not shorter than two points per unit: it would vanish
  Space3Spec r = s;
  (side == AnchorSide::Plus ? r.u[i] : r.n[i]) = {ux, uy};
  return r;
}

static QString fmtNum(double v) {
  QString t = QString::number(round6(v), 'f', 6);
  if (t.contains('.')) { while (t.endsWith('0')) t.chop(1); if (t.endsWith('.')) t.chop(1); }
  if (t == "-0") return "0";
  return t.replace('-', QChar(0x2212));
}

static std::vector<double> multiples(double min, double max, double step) {
  std::vector<double> out;
  const long long n1 = static_cast<long long>(jsRound(max / step));
  for (long long n = static_cast<long long>(jsRound(min / step)); n <= n1; n++) out.push_back(round6(double(n) * step));
  return out;
}

std::vector<Element> buildSpace3(const QString& id, const Space3Spec& rawSpec, const PlaneStyle& style) {
  const Space3Spec s = sanitizeSpace3(rawSpec);
  const double w = std::max(0.3, style.width);
  const QString& color = style.color;
  std::vector<Element> out;
  const double cross[3] = {crossing(s, 0), crossing(s, 1), crossing(s, 2)};

  // grids on the coordinate planes: one zig-zag stroke per direction, its connectors run along the plane's edge (also a grid line)
  struct Plane { bool on; int a, b; };
  for (const Plane& pl : {Plane{s.gridXY, 0, 1}, Plane{s.gridXZ, 0, 2}, Plane{s.gridYZ, 1, 2}}) {
    if (!pl.on) continue;
    const int a = pl.a, b = pl.b;
    const int third = 3 - a - b;
    const Range ra = rangeOf(s, a), rb = rangeOf(s, b);
    auto pt = [&](double va, double vb) {
      double c[3] = {cross[0], cross[1], cross[2]};
      c[a] = va; c[b] = vb; c[third] = cross[third];
      return project(s, c[0], c[1], c[2]);
    };
    auto along = [&](const std::vector<double>& vals, bool fixedIsA) {
      std::vector<double> pts;
      for (size_t i = 0; i < vals.size(); i++) {
        const double v = vals[i];
        const bool odd = i % 2;
        const double from = fixedIsA ? (odd ? rb.hi : rb.lo) : (odd ? ra.hi : ra.lo);
        const double to = fixedIsA ? (odd ? rb.lo : rb.hi) : (odd ? ra.lo : ra.hi);
        const V2 p0 = fixedIsA ? pt(v, from) : pt(from, v), p1 = fixedIsA ? pt(v, to) : pt(to, v);
        pts.insert(pts.end(), {p0[0], p0[1], p1[0], p1[1]});
      }
      return pts;
    };
    const auto as = multiples(ra.lo, ra.hi, ra.step), bs = multiples(rb.lo, rb.hi, rb.step);
    if (as.size() > 1) out.push_back(strokeEl(along(as, true), color, "55", std::max(0.3, w * 0.45), "grid3", id));
    if (bs.size() > 1) out.push_back(strokeEl(along(bs, false), color, "55", std::max(0.3, w * 0.45), "grid3", id));
  }

  const double tl = clampd(w * 0.9 + 1.2, 2, 3.6), head = clampd(w * 1.4 + 3.5, 4.5, 7.5);
  const double tickW = std::max(0.3, w * 0.6), arrowW = std::max(0.3, w * 0.8);
  static const char* roles[3] = {"axis3x", "axis3y", "axis3z"};
  std::vector<Element> labels, ticks, arrows;
  auto normalOf = [](const V2& d) {   // a unit normal of the axis on the page, pointing down or to the left
    double l = std::hypot(d[0], d[1]);
    if (l == 0) l = 1;
    V2 n{-d[1] / l, d[0] / l};
    if (n[1] < -0.3 || (std::abs(n[1]) <= 0.3 && n[0] > 0)) n = {-n[0], -n[1]};
    return n;
  };
  for (int i = 0; i < 3; i++) {
    const Range r = rangeOf(s, i);
    const V2 p0 = axisPoint(s, i, r.lo), p1 = axisPoint(s, i, r.hi), pc = axisPoint(s, i, cross[i]);
    Stroke axis = strokeEl({p0[0], p0[1], pc[0], pc[1], p1[0], p1[1]}, color, "ff", w, roles[i], id, Dash::Solid, style.glow);   // through the crossing: the halves can have different directions
    if (i == 0) {
      axis.attrs.emplace_back("xlSpace3", encodeSpace3(s));
      axis.attrs.emplace_back("xlPlaneW", num(std::round(w * 1000) / 1000));
    }
    out.push_back(std::move(axis));
    const V2 dir = r.hi > cross[i] ? V2{p1[0] - pc[0], p1[1] - pc[1]} : V2{p1[0] - p0[0], p1[1] - p0[1]};   // the way the arrow points: along the positive half
    double dl = std::hypot(dir[0], dir[1]);
    if (dl == 0) dl = 1;
    const V2 nrm = normalOf(dir);
    const auto all = multiples(r.lo, r.hi, r.step);
    std::vector<double> vals;
    for (double v : all) if (v != cross[i] && v != r.lo && v != r.hi) vals.push_back(v);
    if (s.ticks && !vals.empty()) {
      std::vector<double> pts;
      for (double v : vals) {
        const V2 p = axisPoint(s, i, v);
        pts.insert(pts.end(), {p[0], p[1], p[0] - nrm[0] * tl, p[1] - nrm[1] * tl, p[0] + nrm[0] * tl, p[1] + nrm[1] * tl, p[0], p[1]});
      }
      ticks.push_back(strokeEl(pts, color, "ff", tickW, "ticks", id, Dash::Solid, style.glow));
    }
    if (s.arrows) {
      const double ux = dir[0] / dl, uy = dir[1] / dl;
      arrows.push_back(strokeEl({p1[0] - ux * head - uy * head * 0.38, p1[1] - uy * head + ux * head * 0.38, p1[0], p1[1],
                                 p1[0] - ux * head + uy * head * 0.38, p1[1] - uy * head - ux * head * 0.38},
                                color, "ff", arrowW, "arrow", id, Dash::Solid, style.glow));
    }
    const double stepPx = std::hypot(s.u[i][0], s.u[i][1]) * r.step;
    const double size = clampd(stepPx * 0.34, 6.5, 11);
    if (s.numbers) {
      const long long every = std::max(1.0, std::ceil((size * 2.2) / std::max(stepPx, 1.0)));
      for (size_t k = 0; k < all.size(); k++) {
        if (all[k] == cross[i] || (long long)k % every) continue;
        const V2 p = axisPoint(s, i, all[k]);
        labels.push_back(textEl(fmtNum(all[k]), {p[0] + nrm[0] * (tl + size * 0.7), p[1] + nrm[1] * (tl + size * 0.7)}, size, color, id, "label"));
      }
    }
    if (!s.names[i].isEmpty())
      labels.push_back(textEl(s.names[i], {p1[0] + dir[0] / dl * (head + 7), p1[1] + dir[1] / dl * (head + 7)}, clampd(size * 1.2, 8, 13), color, id, "label"));
  }
  for (auto* v : {&ticks, &arrows, &labels}) for (Element& e : *v) out.push_back(std::move(e));
  return out;
}

/** The members of the space that are axes, in x, y, z order. */
static std::array<const Stroke*, 3> axesOf(const Members& members) {
  std::array<const Stroke*, 3> out{nullptr, nullptr, nullptr};
  static const char* roles[3] = {"axis3x", "axis3y", "axis3z"};
  for (int i = 0; i < 3; i++)
    for (const Element* m : members)
      if (const auto* st = std::get_if<Stroke>(m))
        if (roleOf(*m) == QString(roles[i])) { out[i] = st; break; }
  return out;
}

bool isSpace3(const Members& members) {
  const auto a = axesOf(members);
  return a[0] && a[1] && a[2];
}

std::optional<Space3Spec> space3SpecOf(const Members& members) {
  const auto axes = axesOf(members);
  if (!axes[0] || !axes[1] || !axes[2]) return std::nullopt;
  const QString* text = attrFind(axes[0]->attrs, "xlSpace3");
  if (!text) return std::nullopt;
  const auto stored = decodeSpace3(*text);
  if (!stored) return std::nullopt;
  std::array<V2, 3> u, n;
  V2 cross{0, 0};
  for (int i = 0; i < 3; i++) {
    const auto& p = axes[i]->pts;   // lowest end, crossing, highest end
    if (p.size() < 6) return std::nullopt;
    const Range r = rangeOf(*stored, i);
    const double c = crossing(*stored, i);
    cross = {p[2], p[3]};
    u[i] = r.hi > c ? V2{(p[4] - p[2]) / (r.hi - c), (p[5] - p[3]) / (r.hi - c)} : stored->u[i];
    n[i] = r.lo < c ? V2{(p[0] - p[2]) / (r.lo - c), (p[1] - p[3]) / (r.lo - c)} : stored->n[i];
  }
  Space3Spec geo = *stored;
  geo.u = u; geo.n = n;
  // the origin: the crossing point minus the share of each axis at its crossing value
  double ox = cross[0], oy = cross[1];
  for (int i = 0; i < 3; i++) {
    const double c = crossing(*stored, i);
    const V2& w = c >= 0 ? geo.u[i] : geo.n[i];
    ox -= c * w[0];
    oy -= c * w[1];
  }
  geo.o = {ox, oy};
  return geo;
}

std::optional<PlaneStyle> space3StyleOf(const Members& members) {
  const Stroke* ax = axesOf(members)[0];
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

std::array<double, 4> extentOf(const Space3Spec& s) {
  std::vector<double> pts;
  for (double x : {s.xmin, s.xmax}) for (double y : {s.ymin, s.ymax}) for (double z : {s.zmin, s.zmax}) {
    const V2 p = project(s, x, y, z);
    pts.insert(pts.end(), {p[0], p[1]});
  }
  return genjs::bboxOf(pts);
}

Space3Spec placedSpec(const Space3Spec& base, double scale, V2 corner) {
  Space3Spec probe = base;
  probe.o = {0, 0};
  for (int i = 0; i < 3; i++) {
    probe.u[i] = {base.u[i][0] * scale, base.u[i][1] * scale};
    probe.n[i] = {base.n[i][0] * scale, base.n[i][1] * scale};
  }
  const auto b = extentOf(probe);
  probe.o = {corner[0] - b[0], corner[1] - b[1]};
  return probe;
}
