#include "geometry.h"
#include "genobj.h"
#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
TextMeasureFn g_markdownMeasure = nullptr;
constexpr double kInf = std::numeric_limits<double>::infinity();

double num(const Attrs& a, const char* name) { return attrOr(a, name, "0").toDouble(); }
double baseWidth(const Stroke& s) { return s.widths.empty() ? 0.0 : s.widths[0]; }
}  // namespace

// ---------------------------------------------------------------- boxes
Rect bboxOfPoints(const std::vector<double>& p) {
  double x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
  for (size_t i = 0; i + 1 < p.size(); i += 2) {
    const double x = p[i], y = p[i + 1];
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  return {x0, y0, x1, y1};
}

double maxOf(const std::vector<double>& a) {
  double m = -kInf;
  for (double v : a) if (v > m) m = v;
  return m;
}

double maxWidth(const Stroke& s) { return s.widths.empty() ? 0.0 : maxOf(s.widths); }

void setMarkdownMeasure(TextMeasureFn fn) { g_markdownMeasure = fn; }
bool isMarkdownText(const Text& t) { const QString* v = attrFind(t.attrs, "xlMd"); return v && *v == "1"; }
Rect textRect(const Text& t) {
  if (g_markdownMeasure && isMarkdownText(t)) return g_markdownMeasure(t);
  return textBBox(t);
}

bool isTexImage(const Image& im) { return im.tag == "teximage"; }
Rect imageBox(const Image& im) { return {num(im.attrs, "left"), num(im.attrs, "top"), num(im.attrs, "right"), num(im.attrs, "bottom")}; }
void setImageBox(Image& im, const Rect& r) {
  attrSet(im.attrs, "left", fmt8(r[0]));
  attrSet(im.attrs, "top", fmt8(r[1]));
  attrSet(im.attrs, "right", fmt8(r[2]));
  attrSet(im.attrs, "bottom", fmt8(r[3]));
}

TexPose texPose(const Image& im) {
  TexPose p;
  for (const auto& a : im.attrs) {
    if (a.first == "xlRot") { bool ok = false; const double v = a.second.toDouble(&ok); if (ok && std::isfinite(v)) p.rot = v; }
    else if (a.first == "xlFlip") p.flip = a.second == "1";
  }
  return p;
}

void setTexPose(Image& im, const TexPose& pose) {
  Attrs keep;
  for (auto& a : im.attrs) if (a.first != "xlRot" && a.first != "xlFlip") keep.push_back(std::move(a));
  im.attrs = std::move(keep);
  double rot = std::fmod(pose.rot, 2 * M_PI);
  if (rot > M_PI) rot -= 2 * M_PI;
  if (rot <= -M_PI) rot += 2 * M_PI;
  if (std::abs(rot) > 1e-6) {
    const double r8 = std::round(rot * 1e8) / 1e8;
    im.attrs.emplace_back("xlRot", QString::number(r8, 'g', QLocale::FloatingPointShortest));
  }
  if (pose.flip) im.attrs.emplace_back("xlFlip", "1");
}

/** Bounding box of a formula as it is drawn: its upright box turned around its centre. */
static Rect texBounds(const Image& im) {
  const Rect b = imageBox(im);
  const double rot = texPose(im).rot;
  if (rot == 0) return b;
  const double cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2, hw = (b[2] - b[0]) / 2, hh = (b[3] - b[1]) / 2;
  const double c = std::abs(std::cos(rot)), s = std::abs(std::sin(rot));
  const double w = hw * c + hh * s, h = hw * s + hh * c;
  return {cx - w, cy - h, cx + w, cy + h};
}

bool texContains(const Image& im, double x, double y) {
  const double rot = texPose(im).rot;
  const Rect b = imageBox(im);
  const double cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
  const double dx = x - cx, dy = y - cy, c = std::cos(-rot), s = std::sin(-rot);
  const double lx = dx * c - dy * s, ly = dx * s + dy * c;
  return std::abs(lx) <= (b[2] - b[0]) / 2 && std::abs(ly) <= (b[3] - b[1]) / 2;
}

Rect elementBBox(const Element& e) {
  if (const auto* s = std::get_if<Stroke>(&e)) {
    const Rect b = strokeBBox(*s);
    const double h = maxWidth(*s) / 2;
    return {b[0] - h, b[1] - h, b[2] + h, b[3] + h};
  }
  if (const auto* t = std::get_if<Text>(&e)) return textRect(*t);
  const auto& im = std::get<Image>(e);
  return isTexImage(im) ? texBounds(im) : imageBox(im);
}

Rect unionRect(const std::optional<Rect>& a, const Rect& b) {
  if (!a) return b;
  return {std::min((*a)[0], b[0]), std::min((*a)[1], b[1]), std::max((*a)[2], b[2]), std::max((*a)[3], b[3])};
}
bool rectsIntersect(const Rect& a, const Rect& b) { return a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1]; }
bool pointInRect(double x, double y, const Rect& r) { return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]; }

// ---------------------------------------------------------------- hit testing
double segDist2(double x, double y, double ax, double ay, double bx, double by) {
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;
  double t = len2 ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const double cx = ax + t * dx - x, cy = ay + t * dy - y;
  return cx * cx + cy * cy;
}

bool strokeHit(const Stroke& s, double x, double y, double pad) {
  const auto& p = s.pts;
  const size_t n = p.size() >> 1;
  if (n == 1) return std::hypot(x - p[0], y - p[1]) <= pad;
  const double pad2 = pad * pad;
  for (size_t i = 1; i < n; i++)
    if (segDist2(x, y, p[2 * i - 2], p[2 * i - 1], p[2 * i], p[2 * i + 1]) <= pad2) return true;
  return false;
}

std::optional<std::vector<Stroke>> splitStroke(const Stroke& s, double x, double y, double r) {
  const auto& p = s.pts;
  const size_t n = p.size() >> 1;
  const double pad = r + baseWidth(s) / 2;
  if (n == 1) {
    if (std::hypot(x - p[0], y - p[1]) <= pad) return std::vector<Stroke>{};
    return std::nullopt;
  }
  if (n == 0) return std::nullopt;
  const double pad2 = pad * pad;
  std::vector<char> hit(n - 1);
  bool any = false;
  for (size_t i = 0; i + 1 < n; i++) {
    hit[i] = segDist2(x, y, p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3]) <= pad2;
    any = any || hit[i];
  }
  if (!any) return std::nullopt;
  const bool variable = s.widths.size() > 1;
  std::vector<Stroke> pieces;
  size_t i = 0;
  while (i + 1 < n) {
    if (hit[i]) { i++; continue; }
    size_t j = i;
    while (j + 1 < n && !hit[j]) j++;   // kept segments i..j-1, points i..j
    Stroke piece;
    piece.attrs = s.attrs;
    piece.pts.assign(p.begin() + 2 * i, p.begin() + 2 * j + 2);
    piece.widths.push_back(baseWidth(s));
    if (variable)
      for (size_t k = i + 1; k <= j; k++) piece.widths.push_back(k < s.widths.size() ? s.widths[k] : baseWidth(s));
    pieces.push_back(std::move(piece));
    i = j;
  }
  return pieces;
}

bool inRect(const Element& e, const Rect& r) {
  const Rect b = elementBBox(e);
  if (!rectsIntersect(b, r)) return false;
  const auto* s = std::get_if<Stroke>(&e);
  if (!s) return true;
  const auto& p = s->pts;
  for (size_t i = 0; i + 1 < p.size(); i += 2) if (pointInRect(p[i], p[i + 1], r)) return true;
  return false;
}

bool hitElement(const Element& e, double x, double y, double pad) {
  if (const auto* s = std::get_if<Stroke>(&e)) return strokeHit(*s, x, y, pad + baseWidth(*s) / 2);
  const Rect b = elementBBox(e);
  return pointInRect(x, y, {b[0] - pad, b[1] - pad, b[2] + pad, b[3] + pad});
}

bool pointInPolygon(double x, double y, const std::vector<double>& poly) {
  bool inside = false;
  const size_t n = poly.size() >> 1;
  for (size_t i = 0, j = n ? n - 1 : 0; i < n; j = i++) {
    const double xi = poly[2 * i], yi = poly[2 * i + 1], xj = poly[2 * j], yj = poly[2 * j + 1];
    if ((yi > y) != (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

bool inPolygon(const Element& e, const std::vector<double>& poly) {
  if (const auto* s = std::get_if<Stroke>(&e)) {
    const auto& p = s->pts;
    for (size_t i = 0; i + 1 < p.size(); i += 2) if (pointInPolygon(p[i], p[i + 1], poly)) return true;
    return false;
  }
  const Rect b = elementBBox(e);
  return pointInPolygon((b[0] + b[2]) / 2, (b[1] + b[3]) / 2, poly);
}

// ---------------------------------------------------------------- affine maps
Aff scaleAff(double sx, double sy, double ax, double ay) { return {sx, 0, 0, sy, ax * (1 - sx), ay * (1 - sy)}; }
Aff translateAff(double dx, double dy) { return {1, 0, 0, 1, dx, dy}; }
Aff rotateAff(double theta, double cx, double cy) {
  const double co = std::cos(theta), si = std::sin(theta);
  return {co, si, -si, co, cx - co * cx + si * cy, cy - si * cx - co * cy};
}
Aff flipAff(bool horizontal, double cx, double cy) {
  return horizontal ? Aff{-1, 0, 0, 1, 2 * cx, 0} : Aff{1, 0, 0, -1, 0, 2 * cy};
}
std::array<double, 2> applyAff(const Aff& m, double x, double y) { return {m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f}; }

Aff multiplyAff(const Aff& s, const Aff& f) {
  return {s.a * f.a + s.c * f.b, s.b * f.a + s.d * f.b, s.a * f.c + s.c * f.d, s.b * f.c + s.d * f.d,
          s.a * f.e + s.c * f.f + s.e, s.b * f.e + s.d * f.f + s.f};
}
std::optional<Aff> invertAff(const Aff& m) {
  const double det = m.a * m.d - m.b * m.c;
  if (std::abs(det) < 1e-12) return std::nullopt;
  const double a = m.d / det, b = -m.b / det, c = -m.c / det, d = m.a / det;
  return Aff{a, b, c, d, -(a * m.e + c * m.f), -(b * m.e + d * m.f)};
}

Rect transformRect(const Rect& r, const Aff& m) {
  const std::array<double, 2> c[4] = {applyAff(m, r[0], r[1]), applyAff(m, r[2], r[1]), applyAff(m, r[2], r[3]), applyAff(m, r[0], r[3])};
  Rect o = {c[0][0], c[0][1], c[0][0], c[0][1]};
  for (int i = 1; i < 4; i++) {
    o[0] = std::min(o[0], c[i][0]); o[1] = std::min(o[1], c[i][1]);
    o[2] = std::max(o[2], c[i][0]); o[3] = std::max(o[3], c[i][1]);
  }
  return o;
}

Snap snapshot(const Element& e) { return Snap{e}; }
void restore(Element& e, const Snap& s) { e = s.el; }

/** Re-encode the image under `m` into a new PNG (the format has no rotation attribute). Without readable pixels only the box moves. */
static void bakeImage(Image& im, const Aff& m, const Rect& nb) {
  const QImage src = QImage::fromData(QByteArray::fromBase64(im.data.toLatin1()));
  if (src.isNull()) return;
  const Rect ob = imageBox(im);
  const double w0 = ob[2] - ob[0], h0 = ob[3] - ob[1];
  double ppu = src.width() / std::max(w0, 1e-6);   // source pixels per page unit
  const double nw = nb[2] - nb[0], nh = nb[3] - nb[1];
  ppu = std::min(ppu, 2000.0 / std::max({nw, nh, 1e-6}));   // keep a baked picture from growing without bound
  QImage out(std::max(1, int(std::lround(nw * ppu))), std::max(1, int(std::lround(nh * ppu))), QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  {
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(QTransform(ppu, 0, 0, ppu, -nb[0] * ppu, -nb[1] * ppu));
    p.setTransform(QTransform(m.a, m.b, m.c, m.d, m.e, m.f), true);
    p.drawImage(QRectF(ob[0], ob[1], w0, h0), src);
  }
  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  out.save(&buf, "PNG");
  im.data = QString::fromLatin1(png.toBase64());
}

void transformElement(Element& e, const Aff& m) {
  const double k = std::sqrt(std::abs(m.a * m.d - m.b * m.c));
  if (auto* s = std::get_if<Stroke>(&e)) {
    auto& p = s->pts;
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
      const double x = m.a * p[i] + m.c * p[i + 1] + m.e, y = m.b * p[i] + m.d * p[i + 1] + m.f;
      p[i] = x; p[i + 1] = y;
    }
    for (double& w : s->widths) w *= k;
  } else if (auto* t = std::get_if<Text>(&e)) {
    const Rect b = textRect(*t);
    const auto c = applyAff(m, (b[0] + b[2]) / 2, (b[1] + b[3]) / 2);
    const double size = num(t->attrs, "size") * k;
    attrSet(t->attrs, "size", fmt8(size));
    attrSet(t->attrs, "x", fmt8(c[0] - ((b[2] - b[0]) * k) / 2));
    attrSet(t->attrs, "y", fmt8(c[1] - ((b[3] - b[1]) * k) / 2));
  } else {
    auto& im = std::get<Image>(e);
    const Rect box = imageBox(im);
    const bool plainScale = m.b == 0 && m.c == 0 && m.a > 0 && m.d > 0;
    if (isTexImage(im)) {
      // A formula is an upright picture in a box; its turn and mirror live beside it (TexPose). The box keeps its shape, scaled,
      // follows the centre, and the pose absorbs the rotation and the mirror of the transform.
      const TexPose pose = texPose(im);
      if (plainScale && pose.rot == 0 && !pose.flip) {
        setImageBox(im, transformRect(box, m));
      } else {
        const auto c = applyAff(m, (box[0] + box[2]) / 2, (box[1] + box[3]) / 2);
        const double det = m.a * m.d - m.b * m.c;
        const double kk = std::sqrt(std::abs(det)) != 0 ? std::sqrt(std::abs(det)) : 1;
        const double w = (box[2] - box[0]) * kk, h = (box[3] - box[1]) * kk;
        setImageBox(im, {c[0] - w / 2, c[1] - h / 2, c[0] + w / 2, c[1] + h / 2});
        // linear part of (transform after the formula's own pose) = turn(phi) * mirror * k
        const double c0 = std::cos(pose.rot), s0 = std::sin(pose.rot), fx = pose.flip ? -1 : 1;
        const double na = (m.a * c0 + m.c * s0) * fx, nb = (m.b * c0 + m.d * s0) * fx, nc = -m.a * s0 + m.c * c0, nd = -m.b * s0 + m.d * c0;
        const bool mirrored = na * nd - nb * nc < 0;
        setTexPose(im, {std::atan2(-nc, nd), mirrored});
      }
    } else {
      const Rect nb = transformRect(box, m);
      if (!plainScale) bakeImage(im, m, nb);
      setImageBox(im, nb);
    }
  }
}

// ---------------------------------------------------------------- shapes
std::vector<double> shapePoints(ShapeKind kind, double x0, double y0, double x1, double y1, bool constrain, double width) {
  constexpr double SNAP15 = M_PI / 12;
  double dx = x1 - x0, dy = y1 - y0;
  if (kind == ShapeKind::Axes) {
    // origin at the bottom-left of the dragged box, y axis up, x axis right; heads sized like an arrow's
    const double lx = std::min(x0, x1), rx = std::max(x0, x1), ty = std::min(y0, y1), by = std::max(y0, y1);
    const double head = std::min(std::max(8.0, width * 5), std::max(1.0, std::min(rx - lx, by - ty) * 0.3));
    return {lx - head * 0.45, ty + head, lx, ty, lx + head * 0.45, ty + head, lx, ty, lx, by, rx, by, rx - head, by - head * 0.45, rx, by, rx - head, by + head * 0.45};
  }
  if (kind == ShapeKind::Line || kind == ShapeKind::Arrow || kind == ShapeKind::DoubleArrow) {
    if (constrain) {
      const double len = std::hypot(dx, dy);
      const double a = std::round(std::atan2(dy, dx) / SNAP15) * SNAP15;
      dx = std::cos(a) * len; dy = std::sin(a) * len;
    }
    const double ex = x0 + dx, ey = y0 + dy;
    if (kind == ShapeKind::Line) return {x0, y0, ex, ey};
    const double len = std::hypot(dx, dy);
    const double head = std::min(std::max(10.0, width * 6), len * 0.4);
    const double ang = std::atan2(dy, dx);
    const double b1 = ang + M_PI - 0.45, b2 = ang + M_PI + 0.45;
    if (kind == ShapeKind::DoubleArrow) {
      const double c1 = ang - 0.45, c2 = ang + 0.45;   // the head at the start points back along the line
      return {x0 + std::cos(c1) * head, y0 + std::sin(c1) * head, x0, y0, x0 + std::cos(c2) * head, y0 + std::sin(c2) * head, x0, y0, ex, ey,
              ex + std::cos(b1) * head, ey + std::sin(b1) * head, ex, ey, ex + std::cos(b2) * head, ey + std::sin(b2) * head};
    }
    return {x0, y0, ex, ey, ex + std::cos(b1) * head, ey + std::sin(b1) * head, ex, ey, ex + std::cos(b2) * head, ey + std::sin(b2) * head};
  }
  if (constrain) {
    const double m = std::max(std::abs(dx), std::abs(dy));
    const double sx = (dx == 0 || std::isnan(dx)) ? 1 : (dx > 0 ? 1 : -1), sy = (dy == 0 || std::isnan(dy)) ? 1 : (dy > 0 ? 1 : -1);
    dx = sx * m; dy = sy * m;
  }
  const double ex = x0 + dx, ey = y0 + dy;
  if (kind == ShapeKind::Rect) return {x0, y0, ex, y0, ex, ey, x0, ey, x0, y0};
  const double cx = x0 + dx / 2, cy = y0 + dy / 2, rx = std::abs(dx) / 2, ry = std::abs(dy) / 2;
  constexpr int N = 72;
  std::vector<double> pts;
  pts.reserve(2 * (N + 1));
  for (int i = 0; i <= N; i++) {
    const double t = (double(i) / N) * M_PI * 2;
    pts.push_back(cx + std::cos(t) * rx);
    pts.push_back(cy + std::sin(t) * ry);
  }
  return pts;
}

/** Douglas-Peucker simplification of an open polyline. */
static std::vector<double> simplifyPoly(const std::vector<double>& p, double eps) {
  const size_t n = p.size() >> 1;
  if (n < 3) return p;
  std::vector<char> keep(n, 0);
  keep[0] = 1; keep[n - 1] = 1;
  std::vector<std::pair<size_t, size_t>> stack{{0, n - 1}};
  while (!stack.empty()) {
    const auto [a, b] = stack.back();
    stack.pop_back();
    double maxD = 0;
    long idx = -1;
    for (size_t i = a + 1; i < b; i++) {
      const double d = std::sqrt(segDist2(p[2 * i], p[2 * i + 1], p[2 * a], p[2 * a + 1], p[2 * b], p[2 * b + 1]));
      if (d > maxD) { maxD = d; idx = long(i); }
    }
    if (maxD > eps && idx >= 0) { keep[idx] = 1; stack.push_back({a, size_t(idx)}); stack.push_back({size_t(idx), b}); }
  }
  std::vector<double> out;
  for (size_t i = 0; i < n; i++) if (keep[i]) { out.push_back(p[2 * i]); out.push_back(p[2 * i + 1]); }
  return out;
}

static double cornerCos(double ax, double ay, double bx, double by, double cx, double cy) {
  const double ux = ax - bx, uy = ay - by, vx = cx - bx, vy = cy - by;
  const double den = std::hypot(ux, uy) * std::hypot(vx, vy);
  return (ux * vx + uy * vy) / (den != 0 ? den : 1);
}

std::optional<std::vector<double>> recognizeShape(const std::vector<double>& pts) {
  const size_t n = pts.size() >> 1;
  if (n < 8) return std::nullopt;
  double len = 0;
  for (size_t i = 1; i < n; i++) len += std::hypot(pts[2 * i] - pts[2 * i - 2], pts[2 * i + 1] - pts[2 * i - 1]);
  const Rect bb = bboxOfPoints(pts);
  const double x0 = bb[0], y0 = bb[1], x1 = bb[2], y1 = bb[3];
  const double w = x1 - x0, h = y1 - y0, diag = std::hypot(w, h);
  if (diag < 18 || len < 25) return std::nullopt;
  const double sx = pts[0], sy = pts[1], ex = pts[2 * n - 2], ey = pts[2 * n - 1];
  const double gap = std::hypot(ex - sx, ey - sy);

  if (gap >= 0.22 * len) {
    // Open stroke: a line when it hugs its chord.
    if (gap < 0.88 * len) return std::nullopt;
    double worst = 0;
    for (size_t i = 0; i < n; i++) worst = std::max(worst, std::sqrt(segDist2(pts[2 * i], pts[2 * i + 1], sx, sy, ex, ey)));
    if (worst > 0.045 * gap) return std::nullopt;
    // Nearly horizontal or vertical lines snap to the axis.
    double tx = ex, ty = ey;
    const double ang = std::atan2(ey - sy, ex - sx) * 180 / M_PI;
    const double r90 = std::abs(std::fmod(ang, 90.0));
    const double off = std::min(r90, 90 - r90);
    if (off < 4) { if (std::abs(ex - sx) > std::abs(ey - sy)) ty = sy; else tx = sx; }
    return std::vector<double>{sx, sy, tx, ty};
  }

  // Closed stroke: ellipse when every point is about the same normalised distance from the centre.
  const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = std::max(w / 2, 1.0), ry = std::max(h / 2, 1.0);
  double sum = 0, sum2 = 0;
  for (size_t i = 0; i < n; i++) {
    const double rho = std::hypot((pts[2 * i] - cx) / rx, (pts[2 * i + 1] - cy) / ry);
    sum += rho; sum2 += rho * rho;
  }
  const double mean = sum / double(n), sd = std::sqrt(std::max(0.0, sum2 / double(n) - mean * mean));
  if (mean > 0.9 && mean < 1.1 && sd < 0.09) return shapePoints(ShapeKind::Ellipse, x0, y0, x1, y1, false, 0);

  // Otherwise a polygon: rectangle (4 right angles) or triangle (3 corners).
  const std::vector<double> simple = simplifyPoly(pts, 0.08 * diag);
  size_t m = simple.size() >> 1;
  if (m >= 2 && std::hypot(simple[2 * m - 2] - simple[0], simple[2 * m - 1] - simple[1]) < 0.25 * diag) m -= 1;   // closing point
  if (m == 0) return std::nullopt;
  auto v = [&](size_t i) { return std::array<double, 2>{simple[2 * (i % m)], simple[2 * (i % m) + 1]}; };
  if (m == 4) {
    for (size_t i = 0; i < 4; i++) {
      const auto a = v(i + 3), b = v(i), c = v(i + 1);
      if (!(std::abs(cornerCos(a[0], a[1], b[0], b[1], c[0], c[1])) < 0.4)) return std::nullopt;
    }
    double e[4];
    for (size_t i = 0; i < 4; i++) e[i] = std::hypot(v(i + 1)[0] - v(i)[0], v(i + 1)[1] - v(i)[1]);
    const bool opposite = e[0] / e[2] > 0.6 && e[0] / e[2] < 1.67 && e[1] / e[3] > 0.6 && e[1] / e[3] < 1.67;
    if (!opposite) return std::nullopt;
    const double a = std::atan2(v(1)[1] - v(0)[1], v(1)[0] - v(0)[0]) * 180 / M_PI;
    const double a90 = std::abs(std::fmod(a, 90.0));
    if (std::min(a90, 90 - a90) < 12) return std::vector<double>{x0, y0, x1, y0, x1, y1, x0, y1, x0, y0};
    return std::vector<double>{v(0)[0], v(0)[1], v(1)[0], v(1)[1], v(2)[0], v(2)[1], v(3)[0], v(3)[1], v(0)[0], v(0)[1]};
  }
  if (m == 3) return std::vector<double>{v(0)[0], v(0)[1], v(1)[0], v(1)[1], v(2)[0], v(2)[1], v(0)[0], v(0)[1]};
  return std::nullopt;
}

// ---------------------------------------------------------------- freehand smoothing
/**
 * Local quadratic regression along the stroke: around every point, fit a parabola to x(s) and y(s) (Gaussian weights by arc length) and take its
 * value at that point. Unlike a plain average a parabola follows an arc without pulling it inward, so a wide window does not shrink curves.
 */
static void localQuadratic(const std::vector<double>& p, const std::vector<double>& s, double sigma, std::vector<double>& out) {
  const size_t n = p.size() >> 1;
  const double R = 3 * sigma;
  out.resize(2 * n);
  size_t lo = 0, hi = 0;
  for (size_t i = 0; i < n; i++) {
    const double si = s[i];
    while (s[lo] < si - R) lo++;
    while (hi < n - 1 && s[hi + 1] <= si + R) hi++;
    double S0 = 0, S1 = 0, S2 = 0, S3 = 0, S4 = 0, X0 = 0, X1 = 0, X2 = 0, Y0 = 0, Y1 = 0, Y2 = 0;
    for (size_t j = lo; j <= hi; j++) {
      const double u = (s[j] - si) / sigma;
      const double ds = ((j < n - 1 ? s[j + 1] : s[j]) - (j > 0 ? s[j - 1] : s[j])) / 2;   // uneven sampling does not bias the fit
      const double w = std::exp(-u * u * 0.5) * ds;
      const double u2 = u * u;
      const double x = p[2 * j], y = p[2 * j + 1];
      S0 += w; S1 += w * u; S2 += w * u2; S3 += w * u2 * u; S4 += w * u2 * u2;
      X0 += w * x; X1 += w * u * x; X2 += w * u2 * x;
      Y0 += w * y; Y1 += w * u * y; Y2 += w * u2 * y;
    }
    // Value at u = 0 of the weighted least-squares parabola (Cramer's rule on the 3x3 normal equations).
    const double c0 = S2 * S4 - S3 * S3;
    const double det = S0 * c0 - S1 * (S1 * S4 - S2 * S3) + S2 * (S1 * S3 - S2 * S2);
    if (S0 > 0 && std::abs(det) > 1e-9 * S0 * S0 * S0) {
      out[2 * i] = (X0 * c0 - S1 * (X1 * S4 - S3 * X2) + S2 * (X1 * S3 - S2 * X2)) / det;
      out[2 * i + 1] = (Y0 * c0 - S1 * (Y1 * S4 - S3 * Y2) + S2 * (Y1 * S3 - S2 * Y2)) / det;
    } else {
      out[2 * i] = S0 > 0 ? X0 / S0 : p[2 * i];
      out[2 * i + 1] = S0 > 0 ? Y0 / S0 : p[2 * i + 1];
    }
  }
}

std::vector<double> smoothFreehand(const std::vector<double>& p, double step) {
  const size_t n = p.size() >> 1;
  if (n < 4 || step <= 0) return p;
  std::vector<double> s(n, 0.0);
  for (size_t i = 1; i < n; i++) s[i] = s[i - 1] + std::hypot(p[2 * i] - p[2 * i - 2], p[2 * i + 1] - p[2 * i - 1]);
  const double sigma = 8 * step;
  if (s[n - 1] < 2 * sigma) return p;
  const double half = step / 2;
  std::vector<double> cur = p, next;
  for (int iter = 0; iter < 3; iter++) {
    localQuadratic(cur, s, sigma, next);
    cur.swap(next);
    for (size_t k = 0; k < cur.size(); k++) cur[k] = std::min(p[k] + half, std::max(p[k] - half, cur[k]));   // back inside the cell
    cur[0] = p[0]; cur[1] = p[1];
    cur[2 * n - 2] = p[2 * n - 2]; cur[2 * n - 1] = p[2 * n - 1];
  }
  return cur;
}

/** Greatest common step of two lengths, tolerating rounding noise below `tol`. */
static double fgcd(double a, double b, double tol) {
  double x = std::max(a, b), y = std::min(a, b);
  while (y > tol) {
    double r = std::fmod(x, y);
    if (r > y - tol) r = 0;   // a whole multiple, up to noise
    x = y;
    y = r;
  }
  return x;
}

double quantizationStep(const std::vector<double>& p) {
  const size_t n = p.size() >> 1;
  if (n < 13) return 0;
  double g = 0;
  size_t count = 0;
  for (size_t i = 1; i < n; i++) {
    for (int a = 0; a < 2; a++) {
      const double d = std::abs(p[2 * i + a] - p[2 * i - 2 + a]);
      if (d <= 1e-3) continue;
      if (count++ == 0) { g = d; continue; }
      g = fgcd(g, d, 2e-3);   // saved coordinates keep 8 significant digits: far below this
      if (g < 0.05) return 0;
    }
  }
  if (count < 12) return 0;
  return g > 12 ? 0 : g;   // coarser than a pixel at 8% zoom cannot be pen input: it is a lattice somebody drew
}

const std::vector<double>& displayPoints(const Stroke& s, double scale, StrokeView& v) {
  constexpr double STAIR_PX = 1.5;   // a grid step shows as a stair when it spans this many screen pixels
  if (v.src != s.pts.data() || v.count != s.pts.size()) {
    // Strokes the editor generated (planes, rectangle outlines) have exact coordinates that merely look like a grid: never smooth them.
    const bool exact = attrFind(s.attrs, "xlPlane") || attrFind(s.attrs, "xlRiemann");
    v.src = s.pts.data();
    v.count = s.pts.size();
    v.step = exact ? 0 : quantizationStep(s.pts);
    v.haveSmooth = false;
    v.smooth.clear();
  }
  if (v.step * scale < STAIR_PX) return s.pts;
  if (!v.haveSmooth) { v.smooth = smoothFreehand(s.pts, v.step); v.haveSmooth = true; }
  return v.smooth;
}
