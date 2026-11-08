#include "fill.h"
#include "genobj.h"
#include "geometry.h"
#include <QBuffer>
#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kMaxPixels = 4500000.0;

double clampNum(const QJsonValue& v, double lo, double hi, double d) {
  if (!v.isDouble()) return d;
  const double x = v.toDouble();
  return std::isfinite(x) ? std::min(hi, std::max(lo, x)) : d;
}

/** Rasterise something and return which pixels it covers (alpha at least `min`). */
std::vector<uint8_t> coverage(int W, int H, double scale, const std::function<void(QPainter&)>& draw, int min = 100, int originX = 0, int originY = 0) {
  QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  {
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-originX, -originY);   // whole pixels, before the scale: a pixel gets exactly the coverage it has in the full raster
    p.scale(scale, scale);
    draw(p);
  }
  std::vector<uint8_t> out(size_t(W) * size_t(H));
  for (int y = 0; y < H; y++) {
    const auto* line = reinterpret_cast<const uint32_t*>(img.constScanLine(y));
    uint8_t* o = out.data() + size_t(y) * size_t(W);
    for (int x = 0; x < W; x++) o[x] = int(line[x] >> 24) >= min ? 1 : 0;
  }
  return out;
}

Qt::PenCapStyle capOf(const Stroke& s) {
  const QString c = attrOr(s.attrs, "capStyle", "round");
  return c == "butt" ? Qt::FlatCap : c == "square" ? Qt::SquareCap : Qt::RoundCap;
}

/** The ink as a wall: plain black, at least 1.6 device pixels wide so a hairline still stops the fill. Per-segment widths drawn in runs of equal width. */
void drawWall(QPainter& p, const Stroke& s, double scale) {
  const double minW = 1.6 / scale;
  const size_t n = s.pts.size() >> 1;
  if (n == 0) return;
  const auto wAt = [&](size_t i) { return std::max(i < s.widths.size() ? s.widths[i] : (s.widths.empty() ? 0.0 : s.widths[0]), minW); };
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::black);
  if (n == 1) {
    const double r = std::max(wAt(0), s.widths.size() > 1 ? wAt(1) : wAt(0)) / 2;
    p.drawEllipse(QPointF(s.pts[0], s.pts[1]), r, r);
    return;
  }
  QPen pen(Qt::black);
  pen.setCapStyle(capOf(s));
  pen.setJoinStyle(Qt::RoundJoin);
  p.setBrush(Qt::NoBrush);
  std::vector<QPointF> poly;
  if (s.widths.size() <= 1) {
    pen.setWidthF(wAt(0));
    p.setPen(pen);
    poly.reserve(n);
    for (size_t i = 0; i < n; i++) poly.emplace_back(s.pts[2 * i], s.pts[2 * i + 1]);
    p.drawPolyline(poly.data(), int(n));
    return;
  }
  const double step = std::max(0.05, 0.33 / scale);
  const auto q = [&](double v) { return std::max(0.05, std::round(v / step) * step); };
  double runW = q(wAt(1));
  poly.emplace_back(s.pts[0], s.pts[1]);
  for (size_t i = 1; i < n; i++) {
    const double w = q(wAt(i));
    if (w != runW) {
      pen.setWidthF(std::max(runW, minW));
      p.setPen(pen);
      p.drawPolyline(poly.data(), int(poly.size()));
      poly.assign(1, QPointF(s.pts[2 * i - 2], s.pts[2 * i - 1]));
      runW = w;
    }
    poly.emplace_back(s.pts[2 * i], s.pts[2 * i + 1]);
  }
  pen.setWidthF(std::max(runW, minW));
  p.setPen(pen);
  p.drawPolyline(poly.data(), int(poly.size()));
}

/** The ink wall of the raster window [originX, originX+W) x [originY, originY+H) of the full page raster. */
std::vector<uint8_t> inkMask(const StrokeRefs& strokes, int W, int H, double scale, int originX = 0, int originY = 0) {
  return coverage(W, H, scale, [&](QPainter& p) {
    for (const Stroke* s : strokes) {
      if (s->tool() == "eraser") continue;
      drawWall(p, *s, scale);
    }
  }, 40, originX, originY);
}

/** A raster window of the page: the part of the full raster a fill works on. Pixels within `band` of an edge that is not the page's edge are not trustworthy. */
struct Window { int x0 = 0, y0 = 0, w = 0, h = 0; bool left = true, top = true, right = true, bottom = true; /* edge is the page's */ };

/** Which strokes can put ink in the window grown by `pad` pixels: each stroke's box, grown by its line width, against the window. */
class StrokeWindowIndex {
 public:
  StrokeWindowIndex(const StrokeRefs& strokes, double scale) : strokes_(strokes) {
    box_.reserve(strokes.size());
    for (const Stroke* s : strokes) {
      const Rect b = strokeBBox(*s);
      // half the widest line, plus the minimum line of the wall and a pixel of anti-aliasing (caps are round or square: both are within width/2 * sqrt2)
      const double grow = maxWidth(*s) * 0.71 + 1.6 / scale + 2.0 / scale;
      box_.push_back({b[0] - grow, b[1] - grow, b[2] + grow, b[3] + grow});
    }
  }
  StrokeRefs within(double x0, double y0, double x1, double y1) const {
    StrokeRefs out;
    for (size_t i = 0; i < strokes_.size(); i++) {
      const Rect& b = box_[i];
      if (b[2] < x0 || b[0] > x1 || b[3] < y0 || b[1] > y1) continue;
      out.push_back(strokes_[i]);
    }
    return out;
  }
 private:
  const StrokeRefs& strokes_;
  std::vector<Rect> box_;
};

std::vector<uint8_t> invertedFree(const std::vector<uint8_t>& wall) {
  std::vector<uint8_t> f(wall.size());
  for (size_t i = 0; i < wall.size(); i++) f[i] = wall[i] ? 0 : 1;
  return f;
}

double signedArea(const std::vector<double>& p) {
  double a = 0;
  const size_t n = p.size() >> 1;
  for (size_t i = 0; i < n; i++) { const size_t j = (i + 1) % n; a += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1]; }
  return a / 2;
}

/** The region a path radiates into: a union of strips (open path) or the inside / outside (closed path). */
std::vector<uint8_t> sideRegion(int W, int H, double scale, double w, double h, const std::vector<double>& path, FillSide side) {
  const double sgn = side == FillSide::Left ? 1 : -1;
  if (isClosed(path)) {
    const bool inside = (side == FillSide::Left) == (signedArea(path) < 0);
    auto poly = coverage(W, H, scale, [&](QPainter& p) {
      QPainterPath pp;
      pp.setFillRule(Qt::WindingFill);
      pp.moveTo(path[0], path[1]);
      for (size_t i = 2; i + 1 < path.size(); i += 2) pp.lineTo(path[i], path[i + 1]);
      pp.closeSubpath();
      p.fillPath(pp, Qt::black);
    }, 128);
    if (!inside) for (auto& v : poly) v = v ? 0 : 1;
    return poly;
  }
  const double D = std::hypot(w, h) + 10;
  const size_t n = path.size() >> 1;
  std::vector<double> nx, ny, tx, ty;
  for (size_t i = 0; i + 1 < n; i++) {
    const double dx = path[2 * i + 2] - path[2 * i], dy = path[2 * i + 3] - path[2 * i + 1];
    double len = std::hypot(dx, dy);
    if (len == 0) len = 1;
    tx.push_back(dx / len); ty.push_back(dy / len);
    nx.push_back((dy / len) * sgn); ny.push_back((-dx / len) * sgn);   // left of travel in y-down coordinates is (dy, -dx)
  }
  return coverage(W, H, scale, [&](QPainter& p) {
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (size_t i = 0; i + 1 < n; i++) {
      double ax = path[2 * i], ay = path[2 * i + 1], bx = path[2 * i + 2], by = path[2 * i + 3];
      if (i == 0) { ax -= tx[i] * D; ay -= ty[i] * D; }   // the half-plane goes on past the ends
      if (i == n - 2) { bx += tx[i] * D; by += ty[i] * D; }
      const QPointF quad[4] = {{ax, ay}, {bx, by}, {bx + nx[i] * D, by + ny[i] * D}, {ax + nx[i] * D, ay + ny[i] * D}};
      p.drawPolygon(quad, 4);
      if (i > 0) {   // join the strips of two segments at their shared vertex
        const double px = path[2 * i], py = path[2 * i + 1];
        const QPointF tri[3] = {{px, py}, {px + nx[i - 1] * D, py + ny[i - 1] * D}, {px + nx[i] * D, py + ny[i] * D}};
        p.drawPolygon(tri, 3);
      }
    }
  }, 128);
}

std::vector<double> gapSteps(double first, double max) {
  std::vector<double> out{first};
  for (double g : {3.0, 6.0, 10.0, 16.0, 24.0, 32.0, 40.0}) if (g > first && g < max) out.push_back(g);
  if (max > first) out.push_back(max);
  return out;
}

/** Crop a full-raster mask to its bounding box. Returns false when it is empty. */
bool cropMask(const std::vector<uint8_t>& full, int W, int H, double scale, FillMask* out) {
  int x0 = W, y0 = H, x1 = -1, y1 = -1;
  int64_t total = 0;
  for (int y = 0; y < H; y++) {
    const uint8_t* row = full.data() + size_t(y) * size_t(W);
    for (int x = 0; x < W; x++) if (row[x]) { total++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
  }
  if (total == 0) return false;
  const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  out->x0 = x0; out->y0 = y0; out->w = bw; out->h = bh; out->W = W; out->H = H; out->scale = scale;
  out->count = total; out->total = int64_t(W) * H;
  out->mask.assign(size_t(bw) * size_t(bh), 0);
  for (int y = 0; y < bh; y++)
    std::copy_n(full.data() + size_t(y + y0) * size_t(W) + size_t(x0), bw, out->mask.data() + size_t(y) * size_t(bw));
  return true;
}
}  // namespace

// ---------------------------------------------------------------- options
FillOptions sanitizeFill(const QJsonObject& o) {
  FillOptions d, r;
  const QString side = o.value("side").toString(), kind = o.value("kind").toString(), rule = o.value("rule").toString();
  r.side = side == "right" ? FillSide::Right : FillSide::Left;
  r.kind = kind == "linear" ? FillKind::Linear : kind == "radial" ? FillKind::Radial : kind == "riemann" ? FillKind::Riemann : FillKind::Solid;
  const QString c2 = o.value("color2").toString();
  static const QRegularExpression hex6("^#[0-9a-fA-F]{6}$");
  r.color2 = (o.value("color2").isString() && hex6.match(c2).hasMatch()) ? c2.toLower() : d.color2;
  r.angle = clampNum(o.value("angle"), 0, 360, d.angle);
  r.opacity = clampNum(o.value("opacity"), 0.05, 1, 1);
  r.gap = clampNum(o.value("gap"), 0, 16, d.gap);
  r.keepLine = o.value("keepLine").isBool() && o.value("keepLine").toBool();
  r.rule = rule == "lower" ? RiemannRule::Lower : rule == "mid" ? RiemannRule::Mid : rule == "left" ? RiemannRule::Left : rule == "right" ? RiemannRule::Right : RiemannRule::Upper;
  r.base = o.value("base").toString() == "curve" ? RiemannBase::Curve : RiemannBase::Line;
  r.by = o.value("by").toString() == "width" ? RiemannBy::Width : RiemannBy::Count;
  r.count = int(std::floor(clampNum(o.value("count"), 1, 400, d.count) + 0.5));
  r.width = clampNum(o.value("width"), 1, 200, d.width);
  r.outline = !(o.value("outline").isBool() && !o.value("outline").toBool());
  r.magnet = !(o.value("magnet").isBool() && !o.value("magnet").toBool());
  r.maxGap = clampNum(o.value("maxGap"), 2, 40, d.maxGap);
  return r;
}

FillOptions sanitizeFill(const QString& json) {
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
  return sanitizeFill(doc.isObject() ? doc.object() : QJsonObject());
}

QJsonObject fillToJson(const FillOptions& o) {
  QJsonObject j;
  j["side"] = o.side == FillSide::Right ? "right" : "left";
  j["kind"] = o.kind == FillKind::Linear ? "linear" : o.kind == FillKind::Radial ? "radial" : o.kind == FillKind::Riemann ? "riemann" : "solid";
  j["color2"] = o.color2;
  j["angle"] = o.angle;
  j["opacity"] = o.opacity;
  j["gap"] = o.gap;
  j["keepLine"] = o.keepLine;
  j["base"] = o.base == RiemannBase::Curve ? "curve" : "line";
  j["rule"] = o.rule == RiemannRule::Lower ? "lower" : o.rule == RiemannRule::Mid ? "mid" : o.rule == RiemannRule::Left ? "left" : o.rule == RiemannRule::Right ? "right" : "upper";
  j["by"] = o.by == RiemannBy::Width ? "width" : "count";
  j["count"] = o.count;
  j["width"] = o.width;
  j["outline"] = o.outline;
  j["magnet"] = o.magnet;
  j["maxGap"] = o.maxGap;
  return j;
}

// ---------------------------------------------------------------- ink rules
bool isGenerated(const Stroke& s) {
  if (attrFind(s.attrs, "xlRiemann")) return true;
  if (!attrFind(s.attrs, "xlPlane")) return false;
  const QString* role = attrFind(s.attrs, "xlRole");
  if (!role) return true;
  // the lines of a figure or a set diagram are ordinary ink for a fill
  return !(*role == "figbody" || *role == "figarrow" || *role == "figmark" || *role == "setbody" || *role == "setarrow" || *role == "setmark");
}

StrokeRefs layerStrokes(const Layer& l) {
  StrokeRefs r;
  for (const Element& e : l.elements) if (const auto* s = std::get_if<Stroke>(&e)) r.push_back(s);
  return r;
}
StrokeRefs wallStrokes(const Layer& l) {
  StrokeRefs r;
  for (const Element& e : l.elements) if (const auto* s = std::get_if<Stroke>(&e)) if (!isGenerated(*s)) r.push_back(s);
  return r;
}

// ---------------------------------------------------------------- raster pieces
double fillScale(double w, double h, double want) { return std::max(0.5, std::min(want, std::sqrt(kMaxPixels / (w * h)))); }

std::vector<uint8_t> dilate(const std::vector<uint8_t>& src, int W, int H, int r) {
  if (r <= 0) return src;
  std::vector<uint8_t> tmp(src.size(), 0), out(src.size(), 0);
  for (int y = 0; y < H; y++) {
    const size_t row = size_t(y) * size_t(W);
    long last = -1000000000L;
    for (int x = 0; x < W; x++) { if (src[row + x]) last = x; if (x - last <= r) tmp[row + x] = 1; }
    last = 1000000000L;
    for (int x = W - 1; x >= 0; x--) { if (src[row + x]) last = x; if (last - x <= r) tmp[row + x] = 1; }
  }
  for (int x = 0; x < W; x++) {
    long last = -1000000000L;
    for (int y = 0; y < H; y++) { if (tmp[size_t(y) * W + x]) last = y; if (y - last <= r) out[size_t(y) * W + x] = 1; }
    last = 1000000000L;
    for (int y = H - 1; y >= 0; y--) { if (tmp[size_t(y) * W + x]) last = y; if (last - y <= r) out[size_t(y) * W + x] = 1; }
  }
  return out;
}

std::vector<double> simplifyPath(const std::vector<double>& p, double eps) {
  const size_t n = p.size() >> 1;
  if (n < 3) return p;
  std::vector<char> keep(n, 0);
  keep[0] = keep[n - 1] = 1;
  std::vector<std::pair<size_t, size_t>> stack{{0, n - 1}};
  while (!stack.empty()) {
    const auto [a, b] = stack.back();
    stack.pop_back();
    const double ax = p[2 * a], ay = p[2 * a + 1], dx = p[2 * b] - ax, dy = p[2 * b + 1] - ay;
    const double len = std::hypot(dx, dy);
    double worst = -1;
    size_t wi = 0;
    for (size_t i = a + 1; i < b; i++) {
      const double px = p[2 * i] - ax, py = p[2 * i + 1] - ay;
      const double d = len < 1e-9 ? std::hypot(px, py) : std::abs(px * dy - py * dx) / len;
      if (d > worst) { worst = d; wi = i; }
    }
    if (worst > eps) { keep[wi] = 1; stack.push_back({a, wi}); stack.push_back({wi, b}); }
  }
  std::vector<double> out;
  for (size_t i = 0; i < n; i++) if (keep[i]) { out.push_back(p[2 * i]); out.push_back(p[2 * i + 1]); }
  return out;
}

double pathLength(const std::vector<double>& p) {
  double s = 0;
  for (size_t i = 2; i + 1 < p.size(); i += 2) s += std::hypot(p[i] - p[i - 2], p[i + 1] - p[i - 1]);
  return s;
}

std::vector<double> middleOf(const std::vector<double>& p, double trim) {
  const double total = pathLength(p);
  if (total < 30) return p;
  const double lo = total * trim, hi = total * (1 - trim);
  std::vector<double> out;
  double acc = 0;
  for (size_t i = 0; i + 3 < p.size(); i += 2) {
    const double ax = p[i], ay = p[i + 1], bx = p[i + 2], by = p[i + 3], len = std::hypot(bx - ax, by - ay);
    const double l = len != 0 ? len : 1;
    const double t0 = std::max(0.0, (lo - acc) / l), t1 = std::min(1.0, (hi - acc) / l);
    if (t1 > t0) {
      if (out.empty()) { out.push_back(ax + (bx - ax) * t0); out.push_back(ay + (by - ay) * t0); }
      out.push_back(ax + (bx - ax) * t1);
      out.push_back(ay + (by - ay) * t1);
    }
    acc += len;
  }
  return out.size() >= 4 ? out : p;
}

bool isClosed(const std::vector<double>& p) {
  const size_t n = p.size() >> 1;
  if (n < 4) return false;
  const double len = pathLength(p);
  return std::hypot(p[0] - p[2 * n - 2], p[1] - p[2 * n - 1]) <= std::max(8.0, 0.06 * len);
}

FloodResult floodFill(const std::vector<uint8_t>& free, int W, int H, const std::vector<int>& seeds, bool abortOnBorder) {
  FloodResult res;
  res.mask.assign(size_t(W) * size_t(H), 0);
  auto& mask = res.mask;
  std::vector<int> stack = seeds;
  while (!stack.empty()) {
    const int i = stack.back();
    stack.pop_back();
    if (!free[i] || mask[i]) continue;
    const int y = i / W;
    const size_t row = size_t(y) * size_t(W);
    int xl = int(i - row), xr = xl;
    while (xl > 0 && free[row + xl - 1] && !mask[row + xl - 1]) xl--;
    while (xr < W - 1 && free[row + xr + 1] && !mask[row + xr + 1]) xr++;
    for (int x = xl; x <= xr; x++) mask[row + x] = 1;
    res.count += xr - xl + 1;
    if (abortOnBorder && (xl == 0 || xr == W - 1 || y == 0 || y == H - 1)) { res.leaked = true; return res; }
    for (int ny : {y - 1, y + 1}) {
      if (ny < 0 || ny >= H) continue;
      const size_t nr = size_t(ny) * size_t(W);
      bool inRun = false;
      for (int x = xl; x <= xr; x++) {
        const bool ok = free[nr + x] && !mask[nr + x];
        if (ok && !inRun) stack.push_back(int(nr + x));
        inRun = ok;
      }
    }
  }
  return res;
}

std::optional<Pt> nearestOnStrokes(const StrokeRefs& strokes, double x, double y, double radius) {
  std::optional<Pt> best;
  double bd = radius * radius;
  for (const Stroke* sp : strokes) {
    const Stroke& s = *sp;
    if (s.tool() == "eraser") continue;
    if (isGenerated(s)) {   // axes attract, grid lines do not
      const QString role = attrOr(s.attrs, "xlRole");
      if (!attrFind(s.attrs, "xlRole") || (role != "axisx" && role != "axisy")) continue;
    }
    const auto& p = s.pts;
    for (size_t i = 0; i + 3 < p.size(); i += 2) {
      const double ax = p[i], ay = p[i + 1], dx = p[i + 2] - ax, dy = p[i + 3] - ay;
      const double l2 = dx * dx + dy * dy;
      const double t = l2 ? std::max(0.0, std::min(1.0, ((x - ax) * dx + (y - ay) * dy) / l2)) : 0;
      const double qx = ax + t * dx, qy = ay + t * dy, d = (x - qx) * (x - qx) + (y - qy) * (y - qy);
      if (d < bd) { bd = d; best = Pt{qx, qy}; }
    }
    if (p.size() == 2) { const double d = (x - p[0]) * (x - p[0]) + (y - p[1]) * (y - p[1]); if (d < bd) { bd = d; best = Pt{p[0], p[1]}; } }
  }
  return best;
}

std::vector<double> snapPathEnds(const std::vector<double>& path, const StrokeRefs& strokes, double radius) {
  std::vector<double> out = path;
  const size_t n = out.size();
  if (n < 2) return out;
  if (auto a = nearestOnStrokes(strokes, out[0], out[1], radius)) { out[0] = (*a)[0]; out[1] = (*a)[1]; }
  if (auto b = nearestOnStrokes(strokes, out[n - 2], out[n - 1], radius)) { out[n - 2] = (*b)[0]; out[n - 1] = (*b)[1]; }
  return out;
}

// ---------------------------------------------------------------- computeFill
FillOutcome computeFill(const StrokeRefs& strokes, double w, double h, double scale, const FillSeed& seed, double gap, const std::optional<FillMagnet>& magnet, bool windowed) {
  const int W = int(std::ceil(w * scale)), H = int(std::ceil(h * scale));
  FillOutcome fail;
  if (W <= 0 || H <= 0) { fail.reason = "La hoja no tiene tamaño"; return fail; }
  const auto radius = [&](double g) { return int(std::lround((g / 2) * scale)); };   // a gap of width g is closed by growing both walls by g/2
  const auto nearestFree = [](const std::vector<uint8_t>& free, int W, int H, int sx, int sy, int reach) -> int {
    int best = -1;
    long bd = std::numeric_limits<long>::max();
    for (int dy = -reach; dy <= reach; dy++)
      for (int dx = -reach; dx <= reach; dx++) {
        const int x = sx + dx, y = sy + dy;
        if (x < 0 || y < 0 || x >= W || y >= H || !free[size_t(y) * W + x]) continue;
        const long d = long(dx) * dx + long(dy) * dy;
        if (d < bd) { bd = d; best = y * W + x; }
      }
    return best;
  };

  if (seed.isPath) {
    const std::vector<uint8_t> raw = inkMask(strokes, W, H, scale);
    const auto freeFor = [&](double g) { return invertedFree(dilate(raw, W, H, radius(g))); };
    const auto finish = [&](std::vector<uint8_t>& mask, double g, FillInfo info) -> FillOutcome {
      // Tuck the fill under the strokes that stopped it, so no white seam shows between ink and colour. Only the margin within reach of the
      // filled area is taken, so colour never crosses to the far side of a thin stroke.
      const int r = radius(g);
      const auto wall = dilate(raw, W, H, r);
      const auto grown = dilate(mask, W, H, r + 1);
      for (size_t i = 0; i < mask.size(); i++) if (!mask[i] && grown[i] && wall[i]) mask[i] = 1;
      FillOutcome o;
      if (!cropMask(mask, W, H, scale, &o.mask)) { o.reason = "No hay nada que rellenar ahí"; return o; }
      o.ok = true;
      o.info = info;
      return o;
    };
    auto free = freeFor(gap);
    const auto side = sideRegion(W, H, scale, w, h, seed.pts, seed.side);
    for (size_t i = 0; i < free.size(); i++) if (!side[i]) free[i] = 0;
    std::vector<int> seeds;
    // Seed from the middle of the line only: near its ends the line meets the curve, and a seed there could land on the wrong side of it.
    const auto core = middleOf(seed.pts, 0.15);
    for (double width : {5.0, 12.0}) {   // a band of pixels along the line, on the chosen side
      const auto band = coverage(W, H, scale, [&](QPainter& p) {
        QPen pen(Qt::black);
        pen.setWidthF(width / scale);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        std::vector<QPointF> poly;
        for (size_t i = 0; i + 1 < core.size(); i += 2) poly.emplace_back(core[i], core[i + 1]);
        if (poly.size() >= 2) p.drawPolyline(poly.data(), int(poly.size()));
      }, 100);
      for (size_t i = 0; i < band.size(); i++) if (band[i] && free[i]) seeds.push_back(int(i));
      if (!seeds.empty()) break;
    }
    if (seeds.empty()) { fail.reason = "No hay espacio libre junto a la línea: ya hay trazos encima"; return fail; }
    auto f = floodFill(free, W, H, seeds);
    if (!f.count) { fail.reason = "No hay nada que rellenar ahí"; return fail; }
    return finish(f.mask, gap, {gap, false});
  }

  // Click mode. The wall, the free space and the flood only matter near the click, so they are computed on a window of the page raster around it, grown
  // until the fill is certain to be the same as on the whole raster (a flood that comes near an edge of the window that is not the page's own edge says
  // nothing yet: the window grows). The last window is the whole raster, which is the unwindowed algorithm. Wall pixels are exact in any window
  // (strokes are drawn at the same device position); only dilated pixels within a few radii of a window edge are not, and those are not allowed to matter.
  const int sxFull = std::min(W - 1, std::max(0, int(std::lround(seed.x * scale)))), syFull = std::min(H - 1, std::max(0, int(std::lround(seed.y * scale))));
  const QString onStroke = "El clic cayó sobre un trazo: haz clic en un hueco libre";
  double widest = gap;   // the biggest gap any attempt may close
  if (magnet) for (double g : gapSteps(gap, magnet->maxGap)) widest = std::max(widest, g);
  const int rMax = radius(widest);
  // pixels from a window edge inside which a result cannot be trusted: free = dilate(raw, r), finish = dilate(raw, r) + dilate(mask, r + 1), plus the probes
  const int band = 2 * rMax + 4 + (magnet ? std::max(magnet->snapPx, rMax + 2) : 0) + int(std::ceil(6 * scale));

  struct Attempt { bool more = false; FillOutcome out; };   // more: the window was too small to decide
  const StrokeWindowIndex index(strokes, scale);
  const auto attempt = [&](const Window& win) -> Attempt {
    Attempt a;
    const int lw = win.w, lh = win.h;
    const bool whole = win.w == W && win.h == H;
    StrokeRefs near;
    const StrokeRefs* use = &strokes;
    if (!whole) {
      near = index.within(win.x0 / scale, win.y0 / scale, (win.x0 + lw) / scale, (win.y0 + lh) / scale);
      use = &near;
    }
    const std::vector<uint8_t> raw = inkMask(*use, lw, lh, scale, win.x0, win.y0);
    const auto freeFor = [&](double g) { return invertedFree(dilate(raw, lw, lh, radius(g))); };
    // does the filled area come within `band` of an edge that is not the page's? then the window cannot decide
    const auto untrusted = [&](const std::vector<uint8_t>& mask) -> bool {
      if (whole) return false;
      const int bx = win.left ? 0 : band, bx1 = win.right ? 0 : band, by = win.top ? 0 : band, by1 = win.bottom ? 0 : band;
      if (!bx && !bx1 && !by && !by1) return false;
      for (int y = 0; y < lh; y++) {
        const bool edgeRow = y < by || y >= lh - by1;
        const uint8_t* row = mask.data() + size_t(y) * size_t(lw);
        if (edgeRow) { for (int x = 0; x < lw; x++) if (row[x]) return true; continue; }
        for (int x = 0; x < bx; x++) if (row[x]) return true;
        for (int x = lw - bx1; x < lw; x++) if (row[x]) return true;
      }
      return false;
    };
    const int sx = sxFull - win.x0, sy = syFull - win.y0;
    const auto finish = [&](std::vector<uint8_t>& mask, double g, FillInfo info) -> FillOutcome {
      const int r = radius(g);
      const auto wall = dilate(raw, lw, lh, r);
      const auto grown = dilate(mask, lw, lh, r + 1);
      for (size_t i = 0; i < mask.size(); i++) if (!mask[i] && grown[i] && wall[i]) mask[i] = 1;
      FillOutcome o;
      if (!cropMask(mask, lw, lh, scale, &o.mask)) { o.reason = "No hay nada que rellenar ahí"; return o; }
      o.mask.x0 += win.x0; o.mask.y0 += win.y0; o.mask.W = W; o.mask.H = H; o.mask.total = int64_t(W) * H;   // back to the full raster
      o.ok = true;
      o.info = info;
      return o;
    };
    if (!magnet || strokes.empty()) {
      const auto free = freeFor(gap);
      const int at = nearestFree(free, lw, lh, sx, sy, int(std::ceil(std::max(radius(gap) + 2.0, 6 * scale))));
      if (at < 0) { a.out.reason = onStroke; return a; }
      auto f = floodFill(free, lw, lh, {at});
      if (untrusted(f.mask)) { a.more = true; return a; }
      a.out = finish(f.mask, gap, {gap, false});
      return a;
    }
    bool sawFree = false;
    for (double g : gapSteps(gap, magnet->maxGap)) {
      const auto free = freeFor(g);
      const int reach = int(std::ceil(std::max(radius(g) + 2.0, 6 * scale)));
      const int primary = nearestFree(free, lw, lh, sx, sy, reach);
      if (primary >= 0) {
        sawFree = true;
        auto f = floodFill(free, lw, lh, {primary}, true);
        if (untrusted(f.mask)) { a.more = true; return a; }
        if (!f.leaked) { a.out = finish(f.mask, g, {g, false}); return a; }
      }
      // The spot under the click leaks. If it sits within a few pixels of a stroke, the closed region on the other side of that stroke
      // (the click was a pixel off) is the one meant.
      std::optional<FloodResult> best;
      const int ring = std::max(magnet->snapPx, radius(g) + 2);
      for (int k = 0; k < 16; k++) {
        const double ang = (k / 16.0) * M_PI * 2;
        const int x = int(std::lround(sx + std::cos(ang) * ring)), y = int(std::lround(sy + std::sin(ang) * ring));
        if (x < 0 || y < 0 || x >= lw || y >= lh) {
          // outside the window: a window that is not the whole raster cannot tell what is there (a probe off the page itself is skipped, as in the whole raster)
          const int fx = x + win.x0, fy = y + win.y0;
          if (!whole && fx >= 0 && fy >= 0 && fx < W && fy < H) { a.more = true; return a; }
          continue;
        }
        const int at = nearestFree(free, lw, lh, x, y, 2);
        if (at < 0 || at == primary) continue;
        sawFree = true;
        auto f = floodFill(free, lw, lh, {at}, true);
        if (untrusted(f.mask)) { a.more = true; return a; }
        if (!f.leaked && (!best || f.count < best->count)) best = std::move(f);
      }
      if (best) { a.out = finish(best->mask, g, {g, true}); return a; }
    }
    if (!sawFree) { a.out.reason = onStroke; return a; }
    a.out.reason = QString("La forma no se cierra ni cerrando huecos de hasta %1: ciérrala, sube el máximo del imán o mantén Mayús para rellenar de todos modos")
                       .arg(QString::number(magnet->maxGap, 'g', 12));
    return a;
  };

  // windows around the click: about 7 cm, then 3 times wider, then the whole raster. Small pages and few strokes go straight to the whole raster.
  const bool worthWindowing = windowed && strokes.size() >= 200;
  std::vector<int> halves;
  if (worthWindowing) for (double half = std::max(200.0 * scale / 2.0, 3.0 * band); half * 2 < std::min(W, H) * 0.7; half *= 3) halves.push_back(int(std::ceil(half)));
  for (int half : halves) {
    Window win;
    win.x0 = std::max(0, sxFull - half); win.y0 = std::max(0, syFull - half);
    const int x1 = std::min(W, sxFull + half + 1), y1 = std::min(H, syFull + half + 1);
    win.w = x1 - win.x0; win.h = y1 - win.y0;
    win.left = win.x0 == 0; win.top = win.y0 == 0; win.right = x1 == W; win.bottom = y1 == H;
    Attempt a = attempt(win);
    if (!a.more) return std::move(a.out);
  }
  Window whole;
  whole.w = W; whole.h = H;
  return attempt(whole).out;
}

// ---------------------------------------------------------------- Riemann rectangles
int riemannColumns(double len, RiemannBy by, int count, double width) {
  const double n = by == RiemannBy::Count ? count : std::floor(len / std::max(0.5, width) + 0.5);
  return int(std::max(1.0, std::min(400.0, std::floor(n + 0.5))));
}

std::vector<RiemannBaseSeg> riemannBases(const std::vector<double>& path, int n, FillSide side) {
  std::vector<double> cum{0};
  for (size_t i = 2; i + 1 < path.size(); i += 2) cum.push_back(cum.back() + std::hypot(path[i] - path[i - 2], path[i + 1] - path[i - 1]));
  const double total = cum.back();
  const auto at = [&](double s) -> Pt {
    size_t i = 1;
    while (i < cum.size() - 1 && cum[i] < s) i++;
    const double seg = cum[i] - cum[i - 1], t = seg > 0 ? (s - cum[i - 1]) / seg : 0;
    return {path[2 * i - 2] + (path[2 * i] - path[2 * i - 2]) * t, path[2 * i - 1] + (path[2 * i + 1] - path[2 * i - 1]) * t};
  };
  std::vector<RiemannBaseSeg> out;
  if (cum.size() < 2) return out;
  for (int c = 0; c < n; c++) {
    const Pt a = at((double(c) / n) * total), b = at((double(c + 1) / n) * total);
    double len = std::hypot(b[0] - a[0], b[1] - a[1]);
    if (len == 0) len = 1;
    const Pt left{(b[1] - a[1]) / len, -(b[0] - a[0]) / len};
    out.push_back({a, b, side == FillSide::Left ? left : Pt{-left[0], -left[1]}});
  }
  return out;
}

RiemannResult measureRiemann(const StrokeRefs& strokes, double w, double h, double scale, const std::vector<RiemannBaseSeg>& bases, RiemannRule rule, double gap) {
  const int W = int(std::ceil(w * scale)), H = int(std::ceil(h * scale));
  const int r = std::max(0, int(std::lround((gap / 2) * scale)));
  StrokeRefs barriers;
  for (const Stroke* s : strokes) if (!isGenerated(*s)) barriers.push_back(s);   // every stroke except the outlines of earlier rectangles
  const auto wall = dilate(inkMask(barriers, W, H, scale), W, H, r);
  const double step = 1 / scale;
  const double reachMax = (W + H) / scale;
  RiemannResult res;
  res.n = int(bases.size());
  double totalLen = 0;
  for (const auto& base : bases) {
    const double len = std::hypot(base.b[0] - base.a[0], base.b[1] - base.a[1]);
    totalLen += len;
    if (len < 1e-6) { res.skipped++; continue; }
    const double tx = (base.b[0] - base.a[0]) / len, ty = (base.b[1] - base.a[1]) / len;
    // Height of the curve above the base at distance u along the column, or infinity when the ray leaves the page without meeting one.
    const auto heightAt = [&](double u) -> double {
      const double x0 = base.a[0] + tx * u, y0 = base.a[1] + ty * u;
      for (double v = 0; v < reachMax; v += step) {
        const int ix = int(std::floor((x0 + base.normal[0] * v) * scale)), iy = int(std::floor((y0 + base.normal[1] * v) * scale));
        if (ix < 0 || iy < 0 || ix >= W || iy >= H) return kInf;
        if (wall[size_t(iy) * W + ix]) return v == 0 ? 0 : v + r * step;   // the margin grown around strokes is not part of the height
      }
      return kInf;
    };
    double height;
    if (rule == RiemannRule::Left) height = heightAt(step);
    else if (rule == RiemannRule::Right) height = heightAt(len - step);
    else if (rule == RiemannRule::Mid) height = heightAt(len / 2);
    else {
      const int samples = std::max(5, int(std::ceil(len * scale)));
      height = rule == RiemannRule::Upper ? 0 : kInf;
      for (int j = 0; j < samples; j++) {
        const double v = heightAt(((j + 0.5) / samples) * len);
        height = rule == RiemannRule::Upper ? std::max(height, v) : std::min(height, v);
      }
    }
    if (!std::isfinite(height) || height < 0.5) res.skipped++;
    else res.cols.push_back({base.a, base.b, base.normal, height});
  }
  res.width = totalLen / std::max<size_t>(1, bases.size());
  return res;
}

std::array<Pt, 4> colCorners(const RiemannCol& c) {
  return {c.a, c.b, Pt{c.b[0] + c.normal[0] * c.h, c.b[1] + c.normal[1] * c.h}, Pt{c.a[0] + c.normal[0] * c.h, c.a[1] + c.normal[1] * c.h}};
}

std::optional<FillMask> rectsMask(int W, int H, double scale, const std::vector<RiemannCol>& cols) {
  if (cols.empty()) return std::nullopt;
  const auto full = coverage(W, H, scale, [&](QPainter& p) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const auto& c : cols) {
      const auto q = colCorners(c);
      path.moveTo(q[0][0], q[0][1]);
      for (int i = 1; i < 4; i++) path.lineTo(q[i][0], q[i][1]);
      path.closeSubpath();
    }
    p.fillPath(path, Qt::black);
  }, 128);
  FillMask m;
  if (!cropMask(full, W, H, scale, &m)) return std::nullopt;
  return m;
}

QImage fillImage(const FillMask& m, const QString& color1, const FillOptions& o) {
  const int w = m.w, h = m.h;
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  const QColor c1(color1);
  {
    QPainter p(&img);
    if (o.kind == FillKind::Linear) {
      const double a = o.angle * M_PI / 180, c = std::cos(a), s = std::sin(a);
      const double L = (std::abs(w * c) + std::abs(h * s)) / 2;
      QLinearGradient g(w / 2.0 - c * L, h / 2.0 - s * L, w / 2.0 + c * L, h / 2.0 + s * L);
      g.setColorAt(0, c1);
      g.setColorAt(1, QColor(o.color2));
      p.fillRect(QRect(0, 0, w, h), g);
    } else if (o.kind == FillKind::Radial) {
      QRadialGradient g(w / 2.0, h / 2.0, std::max(1e-6, std::hypot(double(w), double(h)) / 2));
      g.setColorAt(0, c1);
      g.setColorAt(1, QColor(o.color2));
      p.fillRect(QRect(0, 0, w, h), g);
    } else {
      p.fillRect(QRect(0, 0, w, h), c1);
    }
  }
  // Keep only the masked pixels, and apply the opacity last, once, so a gradient keeps its own transparency.
  const double op = o.opacity;
  for (int y = 0; y < h; y++) {
    auto* line = reinterpret_cast<uint32_t*>(img.scanLine(y));
    const uint8_t* mrow = m.mask.data() + size_t(y) * size_t(w);
    for (int x = 0; x < w; x++) {
      if (!mrow[x]) { line[x] = 0; continue; }
      if (op < 1) {
        const uint32_t px = line[x];
        auto sc = [&](uint32_t v) { return uint32_t(std::lround(double(v) * op)); };
        line[x] = (sc(px >> 24) << 24) | (sc((px >> 16) & 255) << 16) | (sc((px >> 8) & 255) << 8) | sc(px & 255);
      }
    }
  }
  return img;
}

// ---------------------------------------------------------------- the whole gesture
namespace {
/** End of a straight base: angle steps of 15 degrees with Shift, snapped to the axes when within 3 degrees and the magnet is on. */
Pt straightEnd(Pt a, Pt b, bool shift, bool magnet) {
  const double dx = b[0] - a[0], dy = b[1] - a[1], len = std::hypot(dx, dy);
  if (len < 1e-6) return b;
  double ang = std::atan2(dy, dx);
  if (shift) {
    const double step = M_PI / 12;
    ang = std::round(ang / step) * step;
    return {a[0] + std::cos(ang) * len, a[1] + std::sin(ang) * len};
  }
  if (magnet) {
    for (double target : {0.0, M_PI / 2, M_PI, -M_PI / 2}) {
      double d = std::fmod(std::abs(ang - target), 2 * M_PI);
      if (d > M_PI) d = 2 * M_PI - d;
      if (d < 3 * M_PI / 180) return {a[0] + std::cos(target) * len, a[1] + std::sin(target) * len};
    }
  }
  return b;
}

QString shown(double v, int decimals = 1) {
  const double f = std::pow(10.0, decimals);
  return QString::number(std::round(v * f) / f, 'g', 12);
}

Stroke penStroke(const std::vector<double>& pts, const QString& color, double width, bool riemann) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", color + "ff"}, {"capStyle", "round"}};
  if (riemann) s.attrs.emplace_back("xlRiemann", "1");
  s.widths = {width};
  s.pts = pts;
  return s;
}
}  // namespace

FillResult runFill(const Layer& layer, double pageW, double pageH, const std::vector<double>& dragPts, const FillRequest& req) {
  QElapsedTimer timer;
  timer.start();
  FillResult res;
  const FillOptions& opt = req.opt;
  const double z = req.zoom;
  if (dragPts.size() < 2) return res;
  const bool drag = pathLength(dragPts) * z >= 8;
  const bool riemann = opt.kind == FillKind::Riemann;
  if (riemann && !drag) { res.notices.push_back("Rectángulos de Riemann: arrastra una línea base (por ejemplo el eje x) para marcar dónde se apoyan"); return res; }
  const StrokeRefs all = layerStrokes(layer), strokes = wallStrokes(layer);   // the walls: not the axes and grid of a plane, nor earlier outlines
  const bool magnet = opt.magnet && !req.force;
  std::vector<double> path = drag ? simplifyPath(dragPts, 1.2 / std::max(z, 0.5)) : std::vector<double>{};
  if (drag && magnet) path = snapPathEnds(path, all, 10 / z);   // the ends stick to a curve within 10 screen pixels
  if (riemann && opt.base == RiemannBase::Line) {
    // a perfectly straight base from where the drag began to where it ended: angle steps with Shift, horizontal or vertical when almost so
    const Pt start{dragPts[0], dragPts[1]};
    const Pt end = straightEnd(start, {dragPts[dragPts.size() - 2], dragPts[dragPts.size() - 1]}, req.force, opt.magnet);
    path = {start[0], start[1], end[0], end[1]};
    if (magnet) path = snapPathEnds(path, all, 10 / z);
  }
  FillSeed seed;
  if (drag) { seed.isPath = true; seed.pts = path; seed.side = opt.side; }
  else { seed.x = dragPts[0]; seed.y = dragPts[1]; }
  const double scale = fillScale(pageW, pageH, std::min(4.0, std::max(2.0, z * req.devicePixelRatio)));
  const int W = int(std::ceil(pageW * scale)), H = int(std::ceil(pageH * scale));
  FillOutcome out;
  if (!riemann) {
    std::optional<FillMagnet> mg;
    if (magnet) mg = FillMagnet{opt.maxGap, std::max(3, int(std::lround((8 / z) * scale)))};
    out = computeFill(strokes, pageW, pageH, scale, seed, opt.gap, mg);
    if (!out.ok) { res.notices.push_back(out.reason); return res; }
    if (out.info.moved || out.info.gapUsed > opt.gap)
      res.notices.push_back(out.info.moved ? QString("Imán: se rellenó la zona cerrada junto al clic") : QString("Imán: se cerró un hueco de hasta %1").arg(shown(out.info.gapUsed, 4)));
  } else {
    out.info = {opt.gap, false};
  }
  std::optional<FillMask> m;
  if (!riemann) m = out.mask;
  if (riemann) {
    const int n = riemannColumns(pathLength(path), opt.by, opt.count, opt.width);
    RiemannResult rr = measureRiemann(strokes, pageW, pageH, scale, riemannBases(path, n, opt.side), opt.rule, opt.gap);
    auto rm = rectsMask(W, H, scale, rr.cols);
    if (!rm) { res.notices.push_back("No hay curva sobre la línea que detenga los rectángulos: dibuja la curva o cambia de lado"); return res; }
    m = std::move(rm);
    RiemannInfo ri;
    ri.n = rr.n; ri.width = rr.width; ri.drawn = int(rr.cols.size()); ri.skipped = rr.skipped; ri.cols = rr.cols;
    res.notices.push_back(QString("%1 rectángulos de ancho %2%3").arg(rr.cols.size()).arg(shown(rr.width))
                              .arg(rr.skipped ? QString(" (%1 sin altura: la curva toca la línea o no hay curva)").arg(rr.skipped) : QString()));
    res.riemann = std::move(ri);
  }
  if (!m) return res;
  FillOptions paint = opt;
  if (riemann) paint.kind = FillKind::Solid;
  const QImage cv = fillImage(*m, req.penColor, paint);
  const double left = m->x0 / scale, top = m->y0 / scale, right = (m->x0 + m->w) / scale, bottom = (m->y0 + m->h) / scale;
  QByteArray png;
  {
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    cv.save(&buf, "PNG");
  }
  Image img;
  img.tag = "image";
  const auto r4 = [](double v) { return QString::number(std::round(v * 1e4) / 1e4, 'g', 12); };
  img.attrs = {{"left", r4(left)}, {"top", r4(top)}, {"right", r4(right)}, {"bottom", r4(bottom)}, {"xlFill", "1"}};
  img.data = QString::fromLatin1(png.toBase64());
  res.added.push_back(std::move(img));
  // Under the ink, above earlier fills (a later fill over the same area wins).
  for (size_t i = 0; i < layer.elements.size(); i++)
    if (const auto* e = std::get_if<Image>(&layer.elements[i])) if (attrFind(e->attrs, "xlFill")) res.insertAt = i + 1;
  if (res.riemann && opt.outline) {
    for (const auto& c : res.riemann->cols) {
      const auto q = colCorners(c);
      res.added.push_back(penStroke({q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], q[3][0], q[3][1], q[0][0], q[0][1]}, req.penColor, req.penWidth, true));
    }
  }
  if (drag && opt.keepLine && path.size() >= 4 && (isClosed(path) || pathLength(path) > 2))
    res.added.push_back(penStroke(path, req.penColor, req.penWidth, false));
  res.ok = true;
  res.stats.ms = double(timer.nsecsElapsed()) / 1e6;
  res.stats.pixels = m->count;
  res.stats.path = drag;
  res.stats.wholePage = m->count > 0.6 * double(m->total);
  res.stats.gapUsed = out.info.gapUsed ? out.info.gapUsed : opt.gap;
  res.stats.moved = out.info.moved;
  if (res.stats.wholePage && !drag)
    res.notices.push_back("La forma tiene una abertura: el relleno salió y cubrió toda la zona. Deshaz y sube «Cerrar huecos»");
  return res;
}

std::vector<size_t> applyFill(Layer& layer, const FillResult& r) {
  std::vector<size_t> idx;
  if (!r.ok || r.added.empty()) return idx;
  const size_t at = std::min(r.insertAt, layer.elements.size());
  layer.elements.insert(layer.elements.begin() + long(at), r.added[0]);
  idx.push_back(at);
  for (size_t i = 1; i < r.added.size(); i++) { layer.elements.push_back(r.added[i]); idx.push_back(layer.elements.size() - 1); }
  return idx;
}
