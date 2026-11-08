#include "figures.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

using genjs::jsRound;
using genjs::num;

static double clampd(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
static int sgn(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

const std::vector<FigureCatalogItem>& figureCatalog() {
  static const std::vector<FigureCatalogItem> items = {
      {FigKind::Segment, "Segmento", "Líneas", false},
      {FigKind::Arrow, "Flecha / vector", "Líneas", false},
      {FigKind::Dim, "Cota", "Líneas", false},
      {FigKind::Bezier2, "Curva cuadrática", "Curvas", false},
      {FigKind::Bezier3, "Curva cúbica", "Curvas", false},
      {FigKind::Spline, "Curva por puntos", "Curvas", true},
      {FigKind::Arc, "Arco", "Curvas", false},
      {FigKind::Polyline, "Poligonal", "Polígonos", true},
      {FigKind::Polygon, "Polígono", "Polígonos", true},
      {FigKind::Regular, "Polígono regular", "Polígonos", false},
      {FigKind::Angle, "Ángulo", "Polígonos", false},
      {FigKind::Box, "Cubo / prisma", "3D", false},
      {FigKind::Pyramid, "Pirámide", "3D", false},
      {FigKind::Cylinder, "Cilindro", "3D", false},
      {FigKind::Cone, "Cono", "3D", false},
      {FigKind::Sphere, "Esfera", "3D", false},
  };
  return items;
}

static const std::pair<FigKind, const char*> KIND_NAMES[] = {
    {FigKind::Segment, "segment"}, {FigKind::Arrow, "arrow"}, {FigKind::Polyline, "polyline"}, {FigKind::Polygon, "polygon"},
    {FigKind::Regular, "regular"}, {FigKind::Bezier2, "bezier2"}, {FigKind::Bezier3, "bezier3"}, {FigKind::Spline, "spline"},
    {FigKind::Arc, "arc"}, {FigKind::Angle, "angle"}, {FigKind::Dim, "dim"}, {FigKind::Box, "box"}, {FigKind::Pyramid, "pyramid"},
    {FigKind::Cylinder, "cylinder"}, {FigKind::Cone, "cone"}, {FigKind::Sphere, "sphere"}};

QString figKindName(FigKind k) {
  for (const auto& p : KIND_NAMES) if (p.first == k) return p.second;
  return "segment";
}
std::optional<FigKind> figKindFromName(const QString& name) {
  for (const auto& p : KIND_NAMES) if (name == QLatin1String(p.second)) return p.first;
  return std::nullopt;
}

bool isSolid(FigKind k) { return k == FigKind::Box || k == FigKind::Pyramid || k == FigKind::Cylinder || k == FigKind::Cone || k == FigKind::Sphere; }
bool clicksKind(FigKind k) { return k == FigKind::Polyline || k == FigKind::Polygon || k == FigKind::Spline; }

std::pair<int, int> pointRange(FigKind k) {
  switch (k) {
    case FigKind::Segment: case FigKind::Arrow: case FigKind::Regular: return {2, 2};
    case FigKind::Dim: case FigKind::Bezier2: case FigKind::Arc: case FigKind::Angle: return {3, 3};
    case FigKind::Bezier3: return {4, 4};
    case FigKind::Polyline: case FigKind::Spline: return {2, 200};
    case FigKind::Polygon: return {3, 200};
    default: return {4, 4};   // solids: origin and the three edge tips
  }
}

FigureSpec sanitizeFigure(const FigureSpec& raw) {
  FigureSpec s;
  s.kind = raw.kind;
  const auto [lo, hi] = pointRange(s.kind);
  for (size_t i = 0; i + 1 < raw.pts.size() && int(s.pts.size() / 2) < hi; i += 2) {
    s.pts.push_back(std::isfinite(raw.pts[i]) ? raw.pts[i] : 0);
    s.pts.push_back(std::isfinite(raw.pts[i + 1]) ? raw.pts[i + 1] : 0);
  }
  while (int(s.pts.size() / 2) < lo) {
    const double k = double(s.pts.size() / 2);
    const double x = s.pts.size() >= 2 ? s.pts[s.pts.size() - 2] : 0;
    const double y = (s.pts.size() >= 2 ? s.pts[s.pts.size() - 1] : 0) + 30 * k;
    s.pts.push_back(x);
    s.pts.push_back(y);
  }
  s.heads = raw.heads == Heads::None && s.kind == FigKind::Arrow ? Heads::End : raw.heads;
  s.dash = raw.dash;
  s.n = int(clampd(raw.n, 3, 12));
  s.m = int(clampd(raw.m, 0, 9));
  s.hidden = raw.hidden;
  s.label = raw.label.left(40);
  return s;
}

FigureSpec figureFromJson(const QJsonObject& o) {
  FigureSpec s;
  s.kind = o.value("kind").isString() ? figKindFromName(o.value("kind").toString()).value_or(FigKind::Segment) : FigKind::Segment;
  for (const QJsonValue& v : o.value("pts").toArray()) s.pts.push_back(v.isDouble() && std::isfinite(v.toDouble()) ? v.toDouble() : 0);
  const QString heads = o.value("heads").toString(), dash = o.value("dash").toString(), hidden = o.value("hidden").toString();
  s.heads = heads == "both" ? Heads::Both : heads == "end" ? Heads::End : s.kind == FigKind::Arrow ? Heads::End : Heads::None;
  s.dash = dash == "dash" ? Dash::Dash : dash == "dot" ? Dash::Dot : Dash::Solid;
  s.n = int(jsRound(clampd(genjs::numOr(o, "n", s.kind == FigKind::Regular ? 5 : s.kind == FigKind::Sphere ? 8 : 4), 3, 12)));
  s.m = int(jsRound(clampd(genjs::numOr(o, "m", 3), 0, 9)));
  s.hidden = hidden == "none" ? Hidden::None : hidden == "solid" ? Hidden::Solid : Hidden::Dash;
  s.label = o.value("label").isString() ? o.value("label").toString() : QString();
  return sanitizeFigure(s);
}

QString encodeFigure(const FigureSpec& s) {
  using namespace genjs;
  const char* heads = s.heads == Heads::Both ? "both" : s.heads == Heads::End ? "end" : "none";
  const char* dash = s.dash == Dash::Dash ? "dash" : s.dash == Dash::Dot ? "dot" : "solid";
  const char* hidden = s.hidden == Hidden::None ? "none" : s.hidden == Hidden::Solid ? "solid" : "dash";
  return "{\"kind\":" + str(figKindName(s.kind)) + ",\"pts\":" + numArray(s.pts) + ",\"heads\":\"" + heads + "\",\"dash\":\"" + dash +
         "\",\"n\":" + QString::number(s.n) + ",\"m\":" + QString::number(s.m) + ",\"hidden\":\"" + hidden + "\",\"label\":" + str(s.label) + "}";
}

/** The usual hand-drawn view of a solid: edge vectors for depth (down-left), width (right) and height (up). */
static std::array<V2, 3> viewVectors(double scale) {
  auto a = [](double deg, double len) { return V2{std::cos(deg * M_PI / 180) * len, -std::sin(deg * M_PI / 180) * len}; };
  return {a(215, 30 * scale), a(0, 48 * scale), a(90, 48 * scale)};
}

V2 defaultSize(FigKind kind) {
  if (isSolid(kind)) {
    const auto b = genjs::bboxOf(solidCorners(defaultSpec(kind, {0, 0}, {1, 1})));
    return {b[2] - b[0], b[3] - b[1]};
  }
  return {120, 80};
}

FigureSpec defaultSpec(FigKind kind, V2 a, V2 b, bool shift) {
  const double dx = b[0] - a[0], dy = b[1] - a[1];
  double len = std::hypot(dx, dy);
  if (len == 0) len = 1;
  V2 end = b;
  if (shift) {
    const double ang = jsRound(std::atan2(dy, dx) / (M_PI / 12)) * (M_PI / 12);
    end = {a[0] + std::cos(ang) * len, a[1] + std::sin(ang) * len};
  }
  const double px = -(end[1] - a[1]) / len, py = (end[0] - a[0]) / len;   // a unit normal of the drag
  std::vector<double> pts;
  switch (kind) {
    case FigKind::Segment: case FigKind::Arrow: case FigKind::Regular: case FigKind::Polyline: case FigKind::Spline:
      pts = {a[0], a[1], end[0], end[1]};
      break;
    case FigKind::Dim:
      pts = {a[0], a[1], end[0], end[1], (a[0] + end[0]) / 2 + px * 22, (a[1] + end[1]) / 2 + py * 22};
      break;
    case FigKind::Bezier2:
      pts = {a[0], a[1], (a[0] + end[0]) / 2 + px * len * 0.4, (a[1] + end[1]) / 2 + py * len * 0.4, end[0], end[1]};
      break;
    case FigKind::Bezier3:
      pts = {a[0], a[1],
             a[0] + (end[0] - a[0]) / 3 + px * len * 0.4, a[1] + (end[1] - a[1]) / 3 + py * len * 0.4,
             a[0] + (2 * (end[0] - a[0])) / 3 + px * len * 0.4, a[1] + (2 * (end[1] - a[1])) / 3 + py * len * 0.4,
             end[0], end[1]};
      break;
    case FigKind::Arc:   // 90 degrees, counter-clockwise on the page
      pts = {a[0], a[1], end[0], end[1], a[0] - (end[1] - a[1]), a[1] + (end[0] - a[0])};
      break;
    case FigKind::Angle: {
      const double c = std::cos(-M_PI / 3), s = std::sin(-M_PI / 3);
      pts = {a[0], a[1], end[0], end[1], a[0] + (end[0] - a[0]) * c - (end[1] - a[1]) * s, a[1] + (end[0] - a[0]) * s + (end[1] - a[1]) * c};
      break;
    }
    case FigKind::Polygon:
      pts = {a[0], a[1], end[0], end[1], a[0] + (end[0] - a[0]) / 2 + px * len * 0.7, a[1] + (end[1] - a[1]) / 2 + py * len * 0.7};
      break;
    default: {   // solids: the view vectors scaled by the size of the drag
      const auto e = viewVectors(std::max(0.2, len / 90));
      pts = {a[0], a[1], a[0] + e[0][0], a[1] + e[0][1], a[0] + e[1][0], a[1] + e[1][1], a[0] + e[2][0], a[1] + e[2][1]};
    }
  }
  FigureSpec raw;
  raw.kind = kind;
  raw.pts = pts;
  raw.n = kind == FigKind::Regular ? 5 : kind == FigKind::Sphere ? 8 : 4;
  return sanitizeFigure(raw);
}

// ---------------------------------------------------------------------------------------------------------- geometry helpers
static V2 P(const FigureSpec& s, size_t i) { return {s.pts[2 * i], s.pts[2 * i + 1]}; }
static std::vector<double> flat(const std::vector<V2>& a) {
  std::vector<double> out;
  out.reserve(a.size() * 2);
  for (const V2& p : a) { out.push_back(p[0]); out.push_back(p[1]); }
  return out;
}

static std::vector<V2> sampleBezier(const std::vector<V2>& p, int steps) {
  std::vector<V2> out;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, u = 1 - t;
    if (p.size() == 3)
      out.push_back({u * u * p[0][0] + 2 * u * t * p[1][0] + t * t * p[2][0], u * u * p[0][1] + 2 * u * t * p[1][1] + t * t * p[2][1]});
    else
      out.push_back({u * u * u * p[0][0] + 3 * u * u * t * p[1][0] + 3 * u * t * t * p[2][0] + t * t * t * p[3][0],
                     u * u * u * p[0][1] + 3 * u * u * t * p[1][1] + 3 * u * t * t * p[2][1] + t * t * t * p[3][1]});
  }
  return out;
}

/** Catmull-Rom through the points (the curve passes through every one). */
static std::vector<V2> sampleSpline(const std::vector<V2>& p) {
  if (p.size() < 3) return p;
  std::vector<V2> out;
  const int last = int(p.size()) - 1;
  for (int i = 0; i < last; i++) {
    const V2 &p0 = p[std::max(0, i - 1)], &p1 = p[i], &p2 = p[i + 1], &p3 = p[std::min(last, i + 2)];
    const int steps = int(clampd(jsRound(std::hypot(p2[0] - p1[0], p2[1] - p1[1]) / 4), 6, 40));
    for (int s = 0; s < steps; s++) {
      const double t = double(s) / steps, t2 = t * t, t3 = t2 * t;
      out.push_back({0.5 * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3),
                     0.5 * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)});
    }
  }
  out.push_back(p.back());
  return out;
}

/** Angles in the figure's own sense: counter-clockwise as seen on the page (the page's y axis points down). */
static double angleOf(const V2& c, const V2& p) { return std::atan2(-(p[1] - c[1]), p[0] - c[0]); }
static V2 onCircle(const V2& c, double r, double a) { return {c[0] + std::cos(a) * r, c[1] - std::sin(a) * r}; }

static std::vector<V2> arcPoints(const V2& c, double r, double a0, double sweep) {
  const int steps = int(clampd(jsRound(std::abs(sweep) * 180 / M_PI / 3), 8, 120));
  std::vector<V2> out;
  for (int i = 0; i <= steps; i++) out.push_back(onCircle(c, r, a0 + sweep * i / steps));
  return out;
}

static double ccw(double from, double to) { double d = to - from; while (d <= 0) d += M_PI * 2; return d; }

// ----------------------------------------------------------------------------------------------------------------- 3D solids
V2 solidPoint(const FigureSpec& s, double a, double b, double c) {
  const V2 o = P(s, 0), e1 = P(s, 1), e2 = P(s, 2), e3 = P(s, 3);
  return {o[0] + a * (e1[0] - o[0]) + b * (e2[0] - o[0]) + c * (e3[0] - o[0]), o[1] + a * (e1[1] - o[1]) + b * (e2[1] - o[1]) + c * (e3[1] - o[1])};
}

using V3 = std::array<double, 3>;

/** The direction (in the solid's own space) the solid is looked at along: faces whose outward normal has a positive dot with it are in front. */
static std::optional<V3> viewDir(const FigureSpec& s) {
  const V2 o = P(s, 0), e1 = P(s, 1), e2 = P(s, 2), e3 = P(s, 3);
  const V3 r1{e1[0] - o[0], e2[0] - o[0], e3[0] - o[0]}, r2{e1[1] - o[1], e2[1] - o[1], e3[1] - o[1]};
  V3 d{r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]};
  const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (l < 1e-9) return std::nullopt;
  d = {d[0] / l, d[1] / l, d[2] / l};
  if (d[0] < 0) d = {-d[0], -d[1], -d[2]};   // the first axis points toward the viewer, as in the default view
  return d;
}

struct Poly { std::vector<V3> verts; std::vector<std::vector<int>> faces; };

static Poly polyhedron(FigKind kind) {
  if (kind == FigKind::Box) {
    Poly p;
    for (int c = 0; c < 2; c++) for (int b = 0; b < 2; b++) for (int a = 0; a < 2; a++) p.verts.push_back({double(a), double(b), double(c)});
    auto v = [](int a, int b, int c) { return a + 2 * b + 4 * c; };
    p.faces = {{v(0, 0, 0), v(0, 1, 0), v(0, 1, 1), v(0, 0, 1)}, {v(1, 0, 0), v(1, 1, 0), v(1, 1, 1), v(1, 0, 1)},
               {v(0, 0, 0), v(1, 0, 0), v(1, 0, 1), v(0, 0, 1)}, {v(0, 1, 0), v(1, 1, 0), v(1, 1, 1), v(0, 1, 1)},
               {v(0, 0, 0), v(1, 0, 0), v(1, 1, 0), v(0, 1, 0)}, {v(0, 0, 1), v(1, 0, 1), v(1, 1, 1), v(0, 1, 1)}};
    return p;
  }
  return {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5, 0.5, 1}}, {{0, 1, 2, 3}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};
}

/** Faces' outward normals (the solid is convex, so outward is away from its centroid). */
static std::vector<V3> normals(const Poly& p) {
  V3 cen{0, 0, 0};
  for (const V3& v : p.verts) for (int k = 0; k < 3; k++) cen[k] += v[k] / double(p.verts.size());
  std::vector<V3> out;
  for (const auto& f : p.faces) {
    const V3 &a = p.verts[f[0]], &b = p.verts[f[1]], &c = p.verts[f[2]];
    const V3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    V3 n{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
    V3 fc{0, 0, 0};
    for (int i : f) for (int k = 0; k < 3; k++) fc[k] += p.verts[i][k] / double(f.size());
    if (n[0] * (fc[0] - cen[0]) + n[1] * (fc[1] - cen[1]) + n[2] * (fc[2] - cen[2]) < 0) n = {-n[0], -n[1], -n[2]};
    double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l == 0) l = 1;
    out.push_back({n[0] / l, n[1] / l, n[2] / l});
  }
  return out;
}

std::vector<double> solidCorners(const FigureSpec& s) {
  std::vector<double> out;
  for (int a : {0, 1}) for (int b : {0, 1}) for (int c : {0, 1}) {
    const V2 p = solidPoint(s, a, b, c);
    out.push_back(p[0]);
    out.push_back(p[1]);
  }
  return out;
}

struct Lines { std::vector<std::vector<double>> solid, hidden; };

/** Cut a path of 3D points into runs in front of and behind the silhouette (by the sign of `depth`); runs meet at the crossing. */
static void splitByDepth(const std::vector<V3>& path, const std::function<double(const V3&)>& depth, const std::function<V2(const V3&)>& toPage,
                         std::vector<std::vector<double>>& front, std::vector<std::vector<double>>& back) {
  std::vector<double> run;
  bool inFront = depth(path[0]) >= 0;
  auto flush = [&] {
    if (run.size() >= 4) (inFront ? front : back).push_back(run);
    run.clear();
  };
  for (size_t i = 0; i < path.size(); i++) {
    const V3& p = path[i];
    const double dp = depth(p);
    const bool f = dp >= 0;
    if (i > 0 && f != inFront) {
      const V3& q = path[i - 1];
      const double dq = depth(q), t = dq / (dq - dp);
      const V2 x = toPage({q[0] + (p[0] - q[0]) * t, q[1] + (p[1] - q[1]) * t, q[2] + (p[2] - q[2]) * t});
      run.push_back(x[0]); run.push_back(x[1]);
      flush();
      inFront = f;
      run = {x[0], x[1]};
    }
    const V2 pg = toPage(p);
    run.push_back(pg[0]); run.push_back(pg[1]);
  }
  flush();
}

static Lines solidLines(const FigureSpec& s) {
  Lines L;
  auto& solid = L.solid;
  auto& hidden = L.hidden;
  const auto d = viewDir(s);
  auto pp = [&](const V3& p) { return solidPoint(s, p[0], p[1], p[2]); };
  if (s.kind == FigKind::Box || s.kind == FigKind::Pyramid) {
    const Poly poly = polyhedron(s.kind);
    const auto nrm = normals(poly);
    std::vector<bool> vis;
    for (const V3& n : nrm) vis.push_back(d ? n[0] * (*d)[0] + n[1] * (*d)[1] + n[2] * (*d)[2] > 1e-9 : true);
    struct Edge { int a, b; std::vector<int> faces; };
    std::vector<Edge> edges;
    std::map<std::pair<int, int>, size_t> index;   // edges keep the order they were first met in
    for (size_t fi = 0; fi < poly.faces.size(); fi++) {
      const auto& f = poly.faces[fi];
      for (size_t k = 0; k < f.size(); k++) {
        const int v = f[k], w = f[(k + 1) % f.size()];
        const auto key = std::make_pair(std::min(v, w), std::max(v, w));
        auto it = index.find(key);
        if (it == index.end()) { index[key] = edges.size(); edges.push_back({key.first, key.second, {}}); it = index.find(key); }
        edges[it->second].faces.push_back(int(fi));
      }
    }
    for (const Edge& e : edges) {
      const auto line = flat({pp(poly.verts[e.a]), pp(poly.verts[e.b])});
      const bool seen = std::any_of(e.faces.begin(), e.faces.end(), [&](int fi) { return vis[fi]; });
      (seen ? solid : hidden).push_back(line);
    }
    return L;
  }
  const V3 dd = d.value_or(V3{1, 0, 0});
  // Round solids live in the unit box, like the cube: a circle of radius 1/2 centred on (1/2, 1/2); the sphere's centre is (1/2, 1/2, 1/2).
  // Their own coordinates are centred (x, y in the unit circle), and `depth` is the signed distance toward the viewer from the centre.
  auto side = [&](const V3& p) { return p[0] * dd[0] + p[1] * dd[1]; };
  auto circle = [](double h, int steps = 72) {
    std::vector<V3> out;
    for (int i = 0; i <= steps; i++) { const double t = M_PI * 2 * i / steps; out.push_back({std::cos(t), std::sin(t), h}); }
    return out;
  };
  if (s.kind == FigKind::Cylinder || s.kind == FigKind::Cone) {
    auto pc = [&](const V3& p) { return solidPoint(s, 0.5 + 0.5 * p[0], 0.5 + 0.5 * p[1], p[2]); };
    const bool cone = s.kind == FigKind::Cone;
    const bool topFaces = dd[2] > 1e-9;   // the top cap faces the viewer (otherwise the base does)
    auto addCircle = [&](double h, bool capFacesViewer) {
      std::vector<V3> path = circle(h);
      if (capFacesViewer) {
        std::vector<V2> pts;
        for (const V3& p : path) pts.push_back(pc(p));
        solid.push_back(flat(pts));
        return;
      }
      splitByDepth(path, side, pc, solid, hidden);
    };
    addCircle(0, !topFaces);
    if (!cone) addCircle(1, topFaces);
    if (!cone) {
      const double th = std::atan2(-dd[0], dd[1]);   // where the side is edge-on: depth zero
      for (double t : {th, th + M_PI}) solid.push_back(flat({pc({std::cos(t), std::sin(t), 0}), pc({std::cos(t), std::sin(t), 1})}));
    } else {
      // the two lines from the apex tangent to the base ellipse (when the apex is outside it, as seen on the page)
      const V2 apex = pc({0, 0, 1}), c0 = pc({0, 0, 0}), ex = pc({1, 0, 0}), ey = pc({0, 1, 0});
      const double m00 = ex[0] - c0[0], m01 = ey[0] - c0[0], m10 = ex[1] - c0[1], m11 = ey[1] - c0[1];
      const double det = m00 * m11 - m01 * m10;
      if (std::abs(det) > 1e-9) {
        const double rx = apex[0] - c0[0], ry = apex[1] - c0[1];
        const double ax = (m11 * rx - m01 * ry) / det, ay = (-m10 * rx + m00 * ry) / det;   // the apex in the base circle's own plane
        const double dist = std::hypot(ax, ay);
        if (dist > 1.0001) {
          const double al = std::atan2(ay, ax), dl = std::acos(1 / dist);
          for (double t : {al - dl, al + dl}) solid.push_back(flat({pc({std::cos(t), std::sin(t), 0}), apex}));
        }
      }
    }
    return L;
  }
  // sphere: meridians (great circles through the poles), parallels and the outline
  auto ps = [&](const V3& p) { return solidPoint(s, 0.5 + 0.5 * p[0], 0.5 + 0.5 * p[1], 0.5 + 0.5 * p[2]); };
  auto depth3 = [&](const V3& p) { return p[0] * dd[0] + p[1] * dd[1] + p[2] * dd[2]; };
  auto addPath = [&](const std::vector<V3>& path) { splitByDepth(path, depth3, ps, solid, hidden); };
  addPath(circle(0));   // the equator
  for (int j = 1; j <= s.m; j++) {
    const double phi = -M_PI / 2 + M_PI * j / (s.m + 1);
    if (std::abs(phi) < 1e-6) continue;
    const double r = std::cos(phi), z = std::sin(phi);
    std::vector<V3> path = circle(0, 72);
    for (V3& p : path) p = {p[0] * r, p[1] * r, z};
    addPath(path);
  }
  for (int k = 0; k < s.n; k++) {
    const double th = M_PI * k / s.n;
    std::vector<V3> path;
    for (int i = 0; i <= 72; i++) { const double t = M_PI * 2 * i / 72; path.push_back({std::cos(t) * std::cos(th), std::cos(t) * std::sin(th), std::sin(t)}); }
    addPath(path);
  }
  // the outline: the shadow of the ball, an ellipse (the 2x3 map applied to the unit ball)
  const V2 c = ps({0, 0, 0}), x1 = ps({1, 0, 0}), y1 = ps({0, 1, 0}), z1 = ps({0, 0, 1});
  const double a[2][3] = {{x1[0] - c[0], y1[0] - c[0], z1[0] - c[0]}, {x1[1] - c[1], y1[1] - c[1], z1[1] - c[1]}};
  const double sxx = a[0][0] * a[0][0] + a[0][1] * a[0][1] + a[0][2] * a[0][2];
  const double syy = a[1][0] * a[1][0] + a[1][1] * a[1][1] + a[1][2] * a[1][2];
  const double sxy = a[0][0] * a[1][0] + a[0][1] * a[1][1] + a[0][2] * a[1][2];
  const double tr = sxx + syy, det = sxx * syy - sxy * sxy, disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
  const double l1 = tr / 2 + disc, l2 = std::max(0.0, tr / 2 - disc);
  const double ang = 0.5 * std::atan2(2 * sxy, sxx - syy);
  std::vector<double> outline;
  for (int i = 0; i <= 96; i++) {
    const double t = M_PI * 2 * i / 96;
    const double u = std::cos(t) * std::sqrt(l1), v = std::sin(t) * std::sqrt(l2);
    outline.push_back(c[0] + u * std::cos(ang) - v * std::sin(ang));
    outline.push_back(c[1] + u * std::sin(ang) + v * std::cos(ang));
  }
  solid.push_back(outline);
  return L;
}

// --------------------------------------------------------------------------------------------------------------- building
std::vector<double> witnessOf(const FigureSpec& s) {
  const V2 a = P(s, 0), b = P(s, 1);
  V2 c = s.pts.size() >= 6 ? P(s, 2) : V2{a[0], a[1]};
  const double area = std::abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
  double span = std::hypot(b[0] - a[0], b[1] - a[1]);
  if (span == 0) span = 1;
  if (area < span * 3) c = {a[0] - (b[1] - a[1]) / span * 4, a[1] + (b[0] - a[0]) / span * 4};   // just off the line through the first two (so the selection box does not grow)
  return {a[0], a[1], b[0], b[1], c[0], c[1]};
}

std::vector<Element> buildFigure(const QString& id, const FigureSpec& rawSpec, const PlaneStyle& style) {
  const FigureSpec s = sanitizeFigure(rawSpec);
  const double w = std::max(0.3, style.width);
  const QString& color = style.color;
  std::vector<Element> out;
  auto body = [&](const std::vector<double>& pts, const QString& role = "figbody") { out.push_back(strokeEl(pts, color, "ff", w, role, id, s.dash, style.glow)); };
  auto line = [&](const std::vector<double>& pts, double width, const QString& role) { out.push_back(strokeEl(pts, color, "ff", width, role, id, Dash::Solid, style.glow)); };
  const double arrowW = std::max(0.3, w * 0.9);
  const double headLen = clampd(w * 3 + 5, 6, 14);
  auto addHeads = [&](const std::vector<V2>& path) {
    if (s.heads == Heads::None || path.size() < 2) return;
    const V2& last = path.back();
    const V2& prev = path[path.size() >= 4 ? path.size() - 4 : 0];
    line(arrowHead(last, prev, headLen), arrowW, "figarrow");   // always with `End` or `Both`
    if (s.heads == Heads::Both) line(arrowHead(path[0], path[std::min<size_t>(path.size() - 1, 3)], headLen), arrowW, "figarrow");
  };
  auto labelAt = [&](V2 at) { if (!s.label.isEmpty()) out.push_back(textEl(s.label, at, 12, color, id, "figlabel")); };
  std::vector<V2> pts;
  for (size_t i = 0; i < s.pts.size() / 2; i++) pts.push_back(P(s, i));

  switch (s.kind) {
    case FigKind::Segment: case FigKind::Arrow:
      body(flat(pts));
      addHeads(pts);
      labelAt({(pts[0][0] + pts[1][0]) / 2 + 10, (pts[0][1] + pts[1][1]) / 2 - 10});
      break;
    case FigKind::Polyline: case FigKind::Polygon: {
      std::vector<V2> path = pts;
      if (s.kind == FigKind::Polygon) path.push_back(pts[0]);
      body(flat(path));
      if (s.kind == FigKind::Polyline) addHeads(path);
      double cx = 0, cy = 0;
      for (const V2& p : pts) { cx += p[0]; cy += p[1]; }
      labelAt({cx / double(pts.size()), cy / double(pts.size())});
      break;
    }
    case FigKind::Regular: {
      const V2 c = pts[0], v0 = pts[1];
      std::vector<V2> verts;
      for (int k = 0; k < s.n; k++) {
        const double t = 2 * M_PI * k / s.n;
        verts.push_back({c[0] + (v0[0] - c[0]) * std::cos(t) - (v0[1] - c[1]) * std::sin(t), c[1] + (v0[0] - c[0]) * std::sin(t) + (v0[1] - c[1]) * std::cos(t)});
      }
      verts.push_back(verts[0]);
      body(flat(verts));
      labelAt(c);
      break;
    }
    case FigKind::Bezier2: case FigKind::Bezier3: {
      const double chord = std::hypot(pts.back()[0] - pts[0][0], pts.back()[1] - pts[0][1]);
      const auto path = sampleBezier(pts, int(clampd(jsRound(chord / 4), 24, 160)));
      body(flat(path));
      addHeads(path);
      const V2& mid = path[path.size() / 2];
      labelAt({mid[0] + 10, mid[1] - 10});
      break;
    }
    case FigKind::Spline: {
      const auto path = sampleSpline(pts);
      body(flat(path));
      addHeads(path);
      const V2& mid = pts[pts.size() / 2];
      labelAt({mid[0] + 10, mid[1] - 10});
      break;
    }
    case FigKind::Arc: {
      const V2 c = pts[0];
      const double r = std::hypot(pts[1][0] - c[0], pts[1][1] - c[1]);
      const double a0 = angleOf(c, pts[1]), sweep = ccw(a0, angleOf(c, pts[2]));
      const auto path = arcPoints(c, r, a0, sweep);
      body(flat(path));
      addHeads(path);
      labelAt(onCircle(c, r + 12, a0 + sweep / 2));
      break;
    }
    case FigKind::Angle: {
      const V2 v = pts[0], a = pts[1], b = pts[2];
      body(flat({a, v, b}));
      const double la = std::hypot(a[0] - v[0], a[1] - v[1]), lb = std::hypot(b[0] - v[0], b[1] - v[1]);
      const double r = std::max(8.0, std::min(la, lb) * 0.28);
      const double a0 = angleOf(v, a), a1 = angleOf(v, b);
      double sweep = ccw(a0, a1), from = a0;
      if (sweep > M_PI) { from = a1; sweep = M_PI * 2 - sweep; }
      if (std::abs(sweep - M_PI / 2) < 0.03) {   // a right angle gets a square
        const V2 u{std::cos(from), -std::sin(from)}, t{std::cos(from + sweep), -std::sin(from + sweep)};
        const double k = r * 0.8;
        line({v[0] + u[0] * k, v[1] + u[1] * k, v[0] + (u[0] + t[0]) * k, v[1] + (u[1] + t[1]) * k, v[0] + t[0] * k, v[1] + t[1] * k}, std::max(0.3, w * 0.7), "figmark");
      } else {
        line(flat(arcPoints(v, r, from, sweep)), std::max(0.3, w * 0.7), "figmark");
      }
      labelAt(onCircle(v, r * 1.9 + 4, from + sweep / 2));
      break;
    }
    case FigKind::Dim: {
      const V2 a = pts[0], b = pts[1], c = pts[2];
      const double dx = b[0] - a[0], dy = b[1] - a[1];
      double len = std::hypot(dx, dy);
      if (len == 0) len = 1;
      const double nx = -dy / len, ny = dx / len;
      const double off = (c[0] - a[0]) * nx + (c[1] - a[1]) * ny;   // how far the dimension line stands from the measured segment
      const V2 a2{a[0] + nx * off, a[1] + ny * off}, b2{b[0] + nx * off, b[1] + ny * off};
      auto ext = [&](const V2& p, const V2& q) {
        line({p[0] + (q[0] - p[0]) * 0.1, p[1] + (q[1] - p[1]) * 0.1, q[0] + nx * sgn(off) * 4, q[1] + ny * sgn(off) * 4}, std::max(0.3, w * 0.6), "figmark");
      };
      ext(a, a2);
      ext(b, b2);
      body(flat({a2, b2}));
      line(arrowHead(b2, a2, headLen), arrowW, "figarrow");
      line(arrowHead(a2, b2, headLen), arrowW, "figarrow");
      const int sg = off == 0 ? 1 : sgn(off);
      labelAt({(a2[0] + b2[0]) / 2 + nx * sg * 10, (a2[1] + b2[1]) / 2 + ny * sg * 10});
      break;
    }
    default: {
      const auto [solid, hidden] = solidLines(s);
      for (const auto& p : solid) body(p);
      if (s.hidden != Hidden::None)
        for (const auto& p : hidden)
          out.push_back(strokeEl(p, color, s.hidden == Hidden::Dash ? "99" : "ff", std::max(0.3, w * 0.8), "fighidden", id, s.hidden == Hidden::Dash ? Dash::Dash : s.dash, style.glow));
      labelAt({P(s, 0)[0] - 14, P(s, 0)[1] + 14});
    }
  }
  // the invisible witness: where the figure was built, and what it is
  const auto wp = witnessOf(s);
  const QString json = "{\"spec\":" + encodeFigure(s) + ",\"w\":" + genjs::numArray(wp) + ",\"width\":" + num(std::round(w * 1000) / 1000) + "}";
  out.push_back(makeWitness(wp, "figwitness", "xlFig", json, color, id));
  return out;
}

// -------------------------------------------------------------------------------------------------- reading it back
const Stroke* figureWitness(const Members& members) { return findWitness(members, "figwitness", "xlFig"); }
bool isFigure(const Members& members) { return figureWitness(members) != nullptr; }

std::optional<FigureRead> figureSpecOf(const Members& members) {
  const Stroke* wit = figureWitness(members);
  if (!wit) return std::nullopt;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(attrOr(wit->attrs, "xlFig").toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
  const QJsonObject stored = doc.object();
  FigureSpec spec = figureFromJson(stored.value("spec").toObject());
  std::vector<double> w;
  for (const QJsonValue& v : stored.value("w").toArray()) w.push_back(v.isDouble() ? v.toDouble() : 0);
  const auto T = affineFrom(w, wit->pts);
  if (!T) return std::nullopt;
  spec.pts = mapPoints(*T, spec.pts);
  const Stroke* body = nullptr;
  for (const Element* m : members)
    if (const auto* st = std::get_if<Stroke>(m))
      if (roleOf(*m) == QString("figbody")) { body = st; break; }
  FigureRead r;
  r.spec = std::move(spec);
  r.style.color = body ? genjs::hexColorOf(*body) : QString("#000000");
  const double width = genjs::numOr(stored, "width", 0);
  r.style.width = width > 0 ? width : 1.41;
  if (body) r.style.glow = genjs::glowOf(*body);
  return r;
}

FigureSpec moveFigureAnchor(const FigureSpec& spec, int i, V2 p, bool shift) {
  FigureSpec out = spec;
  auto& pts = out.pts;
  if (i < 0 || size_t(2 * i + 1) >= pts.size()) return spec;
  auto at = [&](int k) { return V2{pts[2 * k], pts[2 * k + 1]}; };
  auto set = [&](int k, V2 q) { pts[2 * k] = q[0]; pts[2 * k + 1] = q[1]; };
  auto move = [&](double dx, double dy) { for (size_t k = 0; k + 1 < pts.size(); k += 2) { pts[k] += dx; pts[k + 1] += dy; } };
  auto snap = [&](const V2& from, const V2& q) {
    if (!shift) return q;
    const double dx = q[0] - from[0], dy = q[1] - from[1], len = std::hypot(dx, dy);
    const double a = jsRound(std::atan2(dy, dx) / (M_PI / 12)) * (M_PI / 12);
    return V2{from[0] + std::cos(a) * len, from[1] + std::sin(a) * len};
  };
  const V2 o = at(0);
  if (isSolid(spec.kind)) {
    if (i == 0) { move(p[0] - o[0], p[1] - o[1]); return out; }   // the origin carries the whole solid
    const V2 tip = at(i);
    V2 v{p[0] - o[0], p[1] - o[1]};
    if (shift) {
      const double old = std::hypot(tip[0] - o[0], tip[1] - o[1]);
      double now = std::hypot(v[0], v[1]);
      if (now == 0) now = 1;
      v = {v[0] / now * old, v[1] / now * old};
    }
    if (std::hypot(v[0], v[1]) < 3) return spec;
    set(i, {o[0] + v[0], o[1] + v[1]});
    return out;
  }
  if (spec.kind == FigKind::Regular || spec.kind == FigKind::Arc) {
    if (i == 0) { move(p[0] - o[0], p[1] - o[1]); return out; }
    set(i, snap(o, p));   // for an arc's end (i == 2) this only sets the angle, as the build uses just the direction
    return out;
  }
  if (spec.kind == FigKind::Segment || spec.kind == FigKind::Arrow) { set(i, snap(at(1 - i), p)); return out; }
  if (spec.kind == FigKind::Angle) { set(i, i == 0 ? p : snap(o, p)); return out; }
  set(i, p);
  return out;
}
