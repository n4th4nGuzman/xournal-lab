#include "venn.h"
#include "setexpr.h"
#include "setsjson.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace sets {

using namespace detail;

static constexpr int MAX_LINES = 400;

VennSpec defaultVenn(int k, V2 a, V2 b) {
  const double w = b[0] - a[0], h = b[1] - a[1];
  const double s = std::min(w * 0.5, h * (k == 2 ? 0.8 : 0.62));
  const double r = s * 0.5;
  const V2 mid{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2};
  std::vector<V2> centres;
  if (k == 2) centres = {{mid[0] - r * 0.62, mid[1]}, {mid[0] + r * 0.62, mid[1]}};
  else centres = {{mid[0] - r * 0.6, mid[1] - r * 0.4}, {mid[0] + r * 0.6, mid[1] - r * 0.4}, {mid[0], mid[1] + r * 0.62}};
  VennSpec spec;
  spec.pts = {a[0], a[1], b[0], b[1]};
  for (const V2& c : centres) {
    double d = std::hypot(c[0] - mid[0], c[1] - mid[1]);
    if (d == 0) d = 1;
    const V2 dir = d < 1e-6 ? V2{0.7071, -0.7071} : V2{(c[0] - mid[0]) / d, (c[1] - mid[1]) / d};
    spec.pts.insert(spec.pts.end(), {c[0], c[1], c[0] + dir[0] * r, c[1] + dir[1] * r});
  }
  spec.k = k == 3 ? 3 : 2;
  spec.names = {"A", "B", "C"};
  spec.lists.assign(size_t(spec.k) + 1, QString());
  return spec;
}

// /^[\p{L}][\p{L}\d_]{0,3}$/u
static bool validName(const QString& n) {
  if (n.isEmpty() || n.size() > 4 || !n[0].isLetter()) return false;
  for (int i = 1; i < n.size(); i++) {
    const QChar c = n[i];
    if (!(c.isLetter() || (c.unicode() >= '0' && c.unicode() <= '9') || c == '_')) return false;
  }
  return true;
}

VennSpec sanitizeVenn(const VennSpec& raw) {
  const int k = raw.k == 3 ? 3 : 2;
  const VennSpec d = defaultVenn(k);
  const size_t need = 4 + 4 * size_t(k);
  VennSpec s;
  s.k = k;
  bool ok = raw.pts.size() >= need;
  for (size_t i = 0; ok && i < need; i++) ok = std::isfinite(raw.pts[i]);
  s.pts = ok ? std::vector<double>(raw.pts.begin(), raw.pts.begin() + long(need)) : d.pts;
  for (size_t i = 0; i < 3; i++) {
    const QString t = i < raw.names.size() ? raw.names[i].trimmed() : QString();
    s.names.push_back(validName(t) ? t : d.names[i]);
  }
  s.expr = raw.expr.left(200);
  s.universe = raw.universe;
  s.dense = raw.dense;
  for (size_t i = 0; i < size_t(k) + 1; i++) s.lists.push_back(i < raw.lists.size() ? raw.lists[i].left(300) : QString());
  return s;
}

VennSpec sanitizeVenn(const QJsonValue& rawv) {
  const QJsonObject o = rawv.isObject() ? rawv.toObject() : QJsonObject();
  VennSpec raw;
  raw.k = o.value("k").isDouble() && o.value("k").toDouble() == 3 ? 3 : 2;
  const QJsonArray pts = o.value("pts").toArray();
  bool numeric = true;
  for (const QJsonValue& v : pts) numeric = numeric && v.isDouble();
  if (numeric)
    for (const QJsonValue& v : pts) raw.pts.push_back(v.toDouble());
  const QJsonArray names = o.value("names").toArray();
  for (const QJsonValue& n : names) raw.names.push_back(n.isString() ? n.toString() : QString());
  raw.expr = o.value("expr").toString();
  raw.universe = o.value("universe") != QJsonValue(false);
  raw.dense = o.value("hatch").toString() == "dense";
  for (const QJsonValue& l : o.value("lists").toArray()) raw.lists.push_back(l.isString() ? l.toString() : QString());
  return sanitizeVenn(raw);
}

QString vennSpecJson(const VennSpec& s) {
  QString names = "[", lists = "[";
  for (size_t i = 0; i < s.names.size(); i++) names += QString(i ? "," : "") + jsStr(s.names[i]);
  for (size_t i = 0; i < s.lists.size(); i++) lists += QString(i ? "," : "") + jsStr(s.lists[i]);
  return "{\"kind\":\"venn\",\"pts\":" + jsNums(s.pts) + ",\"k\":" + QString::number(s.k) + ",\"names\":" + names + "],\"expr\":" + jsStr(s.expr) +
         ",\"universe\":" + jsBool(s.universe) + ",\"hatch\":" + jsStr(s.dense ? "dense" : "light") + ",\"lists\":" + lists + "]}";
}

namespace {

struct Circle { V2 c; double r; };

std::vector<Circle> circlesOf(const VennSpec& spec) {
  std::vector<Circle> out;
  for (int i = 0; i < spec.k; i++) {
    const double cx = spec.pts[4 + 4 * i], cy = spec.pts[5 + 4 * i], rx = spec.pts[6 + 4 * i], ry = spec.pts[7 + 4 * i];
    out.push_back({{cx, cy}, std::max(8.0, std::hypot(rx - cx, ry - cy))});
  }
  return out;
}

struct Rect { double x0, y0, x1, y1; };
Rect rectOf(const VennSpec& spec) {
  const double ax = spec.pts[0], ay = spec.pts[1], bx = spec.pts[2], by = spec.pts[3];
  return {std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
}

std::vector<QString> splitList(const QString& s) {
  std::vector<QString> out;
  QString cur;
  auto flush = [&] { const QString t = cur.trimmed(); if (!t.isEmpty()) out.push_back(t); cur.clear(); };
  for (const QChar c : s) {
    if (c == ',' || c == ';' || c == '\n') flush(); else cur += c;
  }
  flush();
  return out;
}

/** The interval of t where the line P(t) = base + t·v is inside the circle. */
IntervalSet chord(V2 base, V2 v, const Circle& c) {
  const double bx = base[0] - c.c[0], by = base[1] - c.c[1];
  const double b = bx * v[0] + by * v[1], cc = bx * bx + by * by - c.r * c.r, disc = b * b - cc;
  if (!(disc > 0)) return {};
  return normalize({piece(-b - std::sqrt(disc), -b + std::sqrt(disc), true, true)});
}

std::vector<double> polygon(V2 c, double r) {
  std::vector<double> out;
  const int n = int(std::max(36.0, std::min(120.0, std::floor(r / 2 + 0.5))));
  for (int i = 0; i <= n; i++) {
    out.push_back(c[0] + std::cos(double(i) / n * M_PI * 2) * r);
    out.push_back(c[1] + std::sin(double(i) / n * M_PI * 2) * r);
  }
  return out;
}

}  // namespace

int maskAt(const VennSpec& spec, V2 p) {
  int m = 0;
  const auto cs = circlesOf(spec);
  for (size_t i = 0; i < cs.size(); i++)
    if (std::hypot(p[0] - cs[i].c[0], p[1] - cs[i].c[1]) < cs[i].r) m |= 1 << i;
  return m;
}

Shading shadedRegions(const VennSpec& spec) {
  Shading out;
  if (spec.expr.trimmed().isEmpty()) return out;
  const Parsed p = parseSetExpr(spec.expr);
  if (!p.ok) { out.error = p.err.msg; return out; }
  QStringList names;
  for (int i = 0; i < spec.k; i++) names << spec.names[size_t(i)];
  const auto e = evalRegions(p.expr, names);
  if (e.ok) out.shaded = e.value; else out.error = e.msg;
  return out;
}

std::vector<VennElement> elementsOf(const VennSpec& spec) {
  std::vector<VennElement> out;
  auto find = [&](const QString& l) -> VennElement* {
    for (VennElement& e : out) if (e.label == l) return &e;
    return nullptr;
  };
  for (int i = 0; i < spec.k; i++)
    for (const QString& e : splitList(spec.lists[size_t(i)])) {
      if (VennElement* x = find(e)) x->mask |= 1 << i; else out.push_back({e, 1 << i});
    }
  for (const QString& e : splitList(spec.lists[size_t(spec.k)]))
    if (!find(e)) out.push_back({e, 0});
  return out;
}

std::vector<std::vector<double>> hatchSegments(const VennSpec& spec, const std::vector<bool>& shaded) {
  const Rect r = rectOf(spec);
  const double x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
  const auto circles = circlesOf(spec);
  const double gap = spec.dense ? 5 : 9;
  double d = gap;
  if ((x1 - x0 + (y1 - y0)) / d > MAX_LINES) d = (x1 - x0 + (y1 - y0)) / MAX_LINES;
  const V2 v{M_SQRT1_2, -M_SQRT1_2};   // lines go up to the right, x + y = c
  std::vector<std::vector<double>> out;
  std::vector<int> masks;
  for (size_t m = 0; m < shaded.size(); m++)
    if (shaded[m]) masks.push_back(int(m));
  if (masks.empty()) return out;
  const double cMin = x0 + y0;
  for (double c = cMin + d / 2; c < x1 + y1; c += d) {
    const V2 base{c / 2, c / 2};
    // the stretch of the line inside the universe
    double tLo = -INFINITY, tHi = INFINITY;
    const double bounds[2][2] = {{x0, x1}, {y0, y1}};
    for (int axis = 0; axis < 2; axis++) {
      const double bv = base[size_t(axis)], vv = v[size_t(axis)];
      const double a = (bounds[axis][0] - bv) / vv, b = (bounds[axis][1] - bv) / vv;
      tLo = std::max(tLo, std::min(a, b));
      tHi = std::min(tHi, std::max(a, b));
    }
    if (!(tHi > tLo)) continue;
    const IntervalSet universe = normalize({piece(tLo, tHi, true, true)});
    std::vector<IntervalSet> inside;
    for (const Circle& ci : circles) inside.push_back(chord(base, v, ci));
    IntervalSet total;
    for (int m : masks) {
      IntervalSet part = universe;
      for (size_t i = 0; i < circles.size(); i++) part = intersect(part, ((m >> i) & 1) == 1 ? inside[i] : complement(inside[i]));
      total = unionOf(total, part);
    }
    for (const Piece& p : total)
      if (p.hi - p.lo > 0.5) out.push_back({base[0] + v[0] * p.lo, base[1] + v[1] * p.lo, base[0] + v[0] * p.hi, base[1] + v[1] * p.hi});
  }
  return out;
}

std::vector<PlacedElement> placeElements(const VennSpec& spec) {
  const auto els = elementsOf(spec);
  const Rect r = rectOf(spec);
  const double x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
  const auto circles = circlesOf(spec);
  const double step = std::max(3.0, std::min(x1 - x0, y1 - y0) / 60);
  auto dist = [&](V2 p) {
    double d = std::min({p[0] - x0, x1 - p[0], p[1] - y0, y1 - p[1]});
    for (const Circle& ci : circles) d = std::min(d, std::abs(std::hypot(p[0] - ci.c[0], p[1] - ci.c[1]) - ci.r));
    return d;
  };
  struct Sample { V2 p; double d; };
  std::map<int, std::vector<Sample>> samples;
  for (double x = x0 + step / 2; x < x1; x += step)
    for (double y = y0 + step / 2; y < y1; y += step) {
      const V2 p{x, y};
      samples[maskAt(spec, p)].push_back({p, dist(p)});
    }
  std::map<int, std::vector<V2>> placed;
  std::vector<PlacedElement> out;
  for (const VennElement& e : els) {
    const auto it = samples.find(e.mask);
    std::vector<V2>& mine = placed[e.mask];
    std::optional<V2> best;
    double bestScore = -1;
    if (it != samples.end())
      for (const Sample& s : it->second) {
        double near = INFINITY;
        for (const V2& q : mine) near = std::min(near, std::hypot(q[0] - s.p[0], q[1] - s.p[1]));
        const double score = std::min(s.d, near * 0.55);
        if (score > bestScore) { bestScore = score; best = s.p; }
      }
    if (!best) best = V2{(x0 + x1) / 2, (y0 + y1) / 2};   // a region that is not on the page: the middle
    mine.push_back(*best);
    out.push_back({e.label, *best});
  }
  return out;
}

std::vector<double> witnessOf(const VennSpec& spec) {
  const Rect r = rectOf(spec);
  return {r.x0, r.y0, r.x1, r.y0, r.x0, r.y1};
}

std::vector<Element> buildVenn(const QString& id, const VennSpec& rawSpec, const GenStyle& style) {
  const VennSpec spec = sanitizeVenn(rawSpec);
  const double w = std::max(0.3, style.width);
  const QString& color = style.color;
  std::vector<Element> out;
  const Rect r = rectOf(spec);
  const double x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
  const auto circles = circlesOf(spec);
  auto line = [&](const std::vector<double>& pts, const char* role, double width, const char* alpha = "ff") {
    out.push_back(strokeEl(pts, color, alpha, width, role, id, Dash::Solid, style.glow));
  };

  // the shading goes first, so the outlines are over it
  const Shading sh = shadedRegions(spec);
  if (!sh.shaded.empty())
    for (const auto& seg : hatchSegments(spec, sh.shaded)) line(seg, "sethatch", std::max(0.5, w * 0.5), "b0");

  if (spec.universe) {
    line({x0, y0, x1, y0, x1, y1, x0, y1, x0, y0}, "setbody", w);
    out.push_back(textEl("U", {x0 + 12, y0 + 12}, 13, color, id, "setlabel"));
  }
  const V2 mid{(x0 + x1) / 2, (y0 + y1) / 2};
  for (size_t i = 0; i < circles.size(); i++) {
    const Circle& ci = circles[i];
    line(polygon(ci.c, ci.r), "setbody", w);
    const double dx = ci.c[0] - mid[0], dy = ci.c[1] - mid[1], d = std::hypot(dx, dy);
    const V2 dir = d < 1e-6 ? V2{0, -1} : V2{dx / d, dy / d};
    out.push_back(textEl(spec.names[i], {ci.c[0] + dir[0] * (ci.r + 12), ci.c[1] + dir[1] * (ci.r + 12)}, 14, color, id, "setlabel"));
  }
  for (const PlacedElement& e : placeElements(spec)) {
    line({e.at[0], e.at[1], e.at[0] + 0.01, e.at[1]}, "setmark", 3.2);
    out.push_back(textEl(e.label, {e.at[0] + 5 + double(e.label.size()) * 3, e.at[1]}, 11, color, id, "setlabel"));
  }

  const std::vector<double> w3 = witnessOf(spec);
  const QString json = "{\"kind\":\"venn\",\"spec\":" + vennSpecJson(spec) + ",\"w\":" + jsNums(w3) + ",\"width\":" + jsNum(std::round(w * 1000) / 1000) + "}";
  out.push_back(makeWitness(w3, "setwitness", SET_ATTR, json, color, id));
  return out;
}

std::vector<V2> vennAnchors(const VennSpec& spec) {
  std::vector<V2> out;
  for (size_t i = 0; i + 1 < 4 + 4 * size_t(spec.k); i += 2) out.push_back({spec.pts[i], spec.pts[i + 1]});
  return out;
}

VennSpec moveVennAnchor(const VennSpec& spec, int i, V2 p, bool) {
  VennSpec out = spec;
  auto& pts = out.pts;
  if (i < 2) {
    pts[size_t(2 * i)] = p[0];
    pts[size_t(2 * i + 1)] = p[1];
    if (std::abs(pts[2] - pts[0]) < 80 || std::abs(pts[3] - pts[1]) < 60) return spec;
  } else if ((i - 2) % 2 == 0) {
    const double dx = p[0] - pts[size_t(2 * i)], dy = p[1] - pts[size_t(2 * i + 1)];
    pts[size_t(2 * i)] = p[0];
    pts[size_t(2 * i + 1)] = p[1];
    pts[size_t(2 * i + 2)] += dx;
    pts[size_t(2 * i + 3)] += dy;
  } else {
    const V2 c{pts[size_t(2 * (i - 1))], pts[size_t(2 * (i - 1) + 1)]};
    if (std::hypot(p[0] - c[0], p[1] - c[1]) < 12) return spec;
    pts[size_t(2 * i)] = p[0];
    pts[size_t(2 * i + 1)] = p[1];
  }
  return out;
}

std::optional<VennRead> readVenn(const std::vector<const Element*>& members) {
  const Stroke* wit = findWitness(members, "setwitness", SET_ATTR);
  if (!wit) return std::nullopt;
  const auto stored = readStored(*wit, "venn");
  if (!stored) return std::nullopt;
  VennSpec spec = sanitizeVenn(stored->obj.value("spec"));
  const auto T = affineFrom(stored->w, wit->pts);
  if (!T) return std::nullopt;
  spec.pts = mapPoints(*T, spec.pts);
  return VennRead{spec, styleOfBody(members, stored->width)};
}

}  // namespace sets
