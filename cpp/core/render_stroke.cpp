// Stroke drawing: smooth quadratic curves between midpoints with the width following the segment widths (the look of proto/canvas.cpp StrokeRenderer),
// highlighter, dash styles, caps and the glow halo (render.ts drawStrokeCore / drawGlowing).
#include "render_internal.h"
#include "liveglow.h"
#include "geometry.h"
#include "inkpiece.h"
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <list>
#include <memory>
#include <unordered_map>

double strokeWidthAt(const Stroke& s, size_t segment) {
  if (s.widths.empty()) return 1.41;
  if (s.widths.size() == 1) return s.widths[0];
  return segment + 1 < s.widths.size() ? s.widths[segment + 1] : s.widths[0];
}

QColor colorOf(const QString& hex) {
  if (hex.size() == 7 || hex.size() == 9) {
    bool ok = false;
    const uint rgb = hex.mid(1, 6).toUInt(&ok, 16);
    if (ok && hex[0] == '#') {
      int a = 255;
      if (hex.size() == 9) {
        a = hex.mid(7, 2).toInt(&ok, 16);
        if (!ok) return QColor(0, 0, 0);
      }
      return QColor(int(rgb >> 16 & 255), int(rgb >> 8 & 255), int(rgb & 255), a);
    }
  }
  return QColor(0, 0, 0);
}

namespace {

constexpr double kMaxLayerPixels = 24e6;       // a temporary layer or glow bitmap bigger than this is not made
constexpr double kGlowKeepPixels = 2.5e6;      // bigger glow bitmaps are drawn but not kept
constexpr qint64 kGlowCacheBytes = 96ll << 20;

bool isHighlighter(const Stroke& s) { return s.tool() == "highlighter"; }

Qt::PenCapStyle capOf(const Stroke& s) {
  const QString c = attrOr(s.attrs, "capStyle");
  if (c == "butt") return Qt::FlatCap;
  if (c == "square") return Qt::SquareCap;
  if (c == "round") return Qt::RoundCap;
  return isHighlighter(s) ? Qt::FlatCap : Qt::RoundCap;   // a highlighter without a cap attribute is flat, like Xournal++'s
}

std::vector<qreal> dashOf(const Stroke& s, double width) {
  // The canvas version dashes in page units; Qt wants multiples of the pen width.
  const QString d = attrOr(s.attrs, "style");
  std::vector<double> units;
  if (d == "dash") units = {6, 3};
  else if (d == "dashdot") units = {6, 3, 0.5, 3};
  else if (d == "dot") units = {0.5, 3};
  else if (d.startsWith(QLatin1String("cust:"))) {   // Xournal++'s own pattern: "cust: 4.00 2.50" (page units: on, off, on...)
    for (const QString& tok : d.mid(5).split(' ', Qt::SkipEmptyParts)) {
      bool ok = false;
      const double v = tok.toDouble(&ok);
      if (!ok || !(v >= 0) || !std::isfinite(v)) { units.clear(); break; }
      units.push_back(v);
    }
    double sum = 0;
    for (double u : units) sum += u;
    if (!(sum > 0)) units.clear();   // all zeros or malformed: draw it solid rather than nothing
    else if (units.size() % 2) { const auto once = units; units.insert(units.end(), once.begin(), once.end()); }   // cairo repeats an odd pattern, swapping on and off
  }
  std::vector<qreal> out;
  for (double u : units) out.push_back(u / std::max(width, 1e-3));
  return out;
}

/** True when the stroke is a polyline whose corners must stay sharp (render.ts draws every stroke as plain lineTo's; only the free pen is smoothed here).
 *  Generated geometry says so by its xlPlane / xlRiemann attribute and the shape tool / recognizer by xlShape. A file from the Electron app or Xournal++ has
 *  no such mark, so a few widely spaced points of one width (a rectangle, triangle, arrow head: never a pen stroke, which has dozens of points a few
 *  units apart) count as a polyline too. */
bool isPolylineStroke(const Stroke& s) {
  if (attrFind(s.attrs, "xlPlane") || attrFind(s.attrs, "xlRiemann") || attrFind(s.attrs, "xlShape")) return true;
  const size_t n = s.pts.size() / 2;
  if (n < 2 || n > 12 || s.widths.size() > 1 || s.tool() == QLatin1String("highlighter")) return false;
  for (size_t i = 1; i < n; i++)
    if (std::hypot(s.pts[2 * i] - s.pts[2 * i - 2], s.pts[2 * i + 1] - s.pts[2 * i - 1]) < 5.0) return false;
  return true;
}

struct Piece { QPointF a, c, b; double wa, wb; };

/** Piece i of the smoothed outline. Pieces run from point to the first midpoint, midpoint to midpoint (the control point is the data point between)
 *  and from the last midpoint to the last point; a piece's ends take the widths of the segments they leave and enter. */
struct Outline {
  const Stroke& s;
  const std::vector<double>& pts;   // the points to draw: the stored ones or a smoothed copy
  size_t n;
  bool straight;   // generated geometry (planes, figures, lines, rectangles) is a polyline: its corners must stay corners, not be rounded through midpoints
  Outline(const Stroke& st, const std::vector<double>& p) : s(st), pts(p), n(p.size() / 2), straight(isPolylineStroke(st)) {}
  QPointF pt(size_t i) const { return {pts[2 * i], pts[2 * i + 1]}; }
  QPointF mid(size_t i) const { return (pt(i) + pt(i + 1)) / 2; }
  size_t pieces() const { return straight ? n - 1 : n; }   // free strokes: first + (n-2) inner + last; generated: one straight piece per segment
  Piece piece(size_t i) const {
    const size_t segs = n - 1;
    if (straight) { const double w = strokeWidthAt(s, i); return {pt(i), pt(i), pt(i + 1), w, w}; }
    if (i == 0) { const double w = strokeWidthAt(s, 0); return {pt(0), pt(0), mid(0), w, w}; }
    if (i == segs) { const double w = strokeWidthAt(s, segs - 1); return {mid(segs - 1), pt(segs), pt(segs), w, w}; }
    return {mid(i - 1), pt(i), mid(i), strokeWidthAt(s, i - 1), strokeWidthAt(s, i)};
  }
};

QPen penOf(const QColor& c, double w, Qt::PenCapStyle cap) {
  QPen pen(c);
  pen.setWidthF(w);
  pen.setCapStyle(cap);
  pen.setJoinStyle(Qt::RoundJoin);
  return pen;
}

void appendPiece(QPainterPath& path, const Piece& pc) {
  if (pc.a == pc.c || pc.c == pc.b) path.lineTo(pc.b);
  else path.quadTo(pc.c, pc.b);
}

}  // namespace

static std::atomic<bool> g_batch{true};
void setInkBatching(bool on) { g_batch.store(on); }
bool inkBatching() { return g_batch.load(); }

double inkWidth(double w, double k, bool lite) {
  const double step = std::max(0.05, (lite ? 1.0 : 0.33) / std::max(k, 1e-6));   // a third of a device pixel: invisible, and runs of pieces batch
  return std::max(0.05, std::round(w / step) * step);
}

void drawInkPiece(QPainter& p, QPen& pen, Qt::PenCapStyle cap, const QPointF& a, const QPointF& c, const QPointF& b, double wa, double wb, double k, bool lite) {
  const double qa = inkWidth(wa, k, lite), qb = inkWidth(wb, k, lite);
  if (qa == qb) {
    pen.setWidthF(qa);
    pen.setCapStyle(cap);
    // A piece whose control point lies within a tenth of a device pixel of the chord is a straight line to the eye: no path object needed (the pen path
    // draws one of these per event, so it should not allocate).
    const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (len > 1e-9 && std::abs((b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x())) / len * k < 0.1) {
      p.setPen(pen);
      p.drawLine(a, b);
      return;
    }
    QPainterPath path;
    path.moveTo(a);
    if (a == c || c == b) path.lineTo(b);
    else path.quadTo(c, b);
    p.setPen(pen);
    p.drawPath(path);
    return;
  }
  const double rough = std::hypot(b.x() - a.x(), b.y() - a.y()) + std::hypot(c.x() - a.x(), c.y() - a.y());
  const int steps = lite ? 1 : std::clamp(int(rough * k / 3.0), 1, 24);
  pen.setCapStyle(Qt::RoundCap);
  QPointF last = a;
  for (int j = 1; j <= steps; j++) {
    const double t = double(j) / steps, u = 1 - t;
    const QPointF pt = u * u * a + 2 * u * t * c + t * t * b;
    pen.setWidthF(std::max(0.05, wa + (wb - wa) * (j - 0.5) / steps));
    p.setPen(pen);
    p.drawLine(last, pt);
    last = pt;
  }
}

namespace {

bool allPointsEqual(const std::vector<double>& pts) {
  for (size_t i = 2; i + 1 < pts.size(); i += 2)
    if (pts[i] != pts[0] || pts[i + 1] != pts[1]) return false;
  return true;
}

void dot(QPainter& p, const Stroke& s, const std::vector<double>& pts, const QColor& color) {
  const double r = std::max(strokeWidthAt(s, 0), s.widths.size() > 1 ? s.widths[1] : 0.0) / 2;
  p.setPen(Qt::NoPen);
  p.setBrush(color);
  p.drawEllipse(QPointF(pts[0], pts[1]), r, r);
}

/** True when the piece (padded by half its width and a pixel) lies entirely outside `clip`: nothing of it can reach the visible area. */
bool pieceOutside(const Piece& pc, const QRectF& clip, double k) {
  const double pad = std::max(pc.wa, pc.wb) / 2 + 2 / k;
  const double x0 = std::min({pc.a.x(), pc.b.x(), pc.c.x()}) - pad, x1 = std::max({pc.a.x(), pc.b.x(), pc.c.x()}) + pad;
  const double y0 = std::min({pc.a.y(), pc.b.y(), pc.c.y()}) - pad, y1 = std::max({pc.a.y(), pc.b.y(), pc.c.y()}) + pad;
  return x1 < clip.left() || x0 > clip.right() || y1 < clip.top() || y0 > clip.bottom();
}

/** Draws the stroke's ink with `color` (alpha included). `k` = pixels per page unit, only used to choose how fine widths and curve steps are.
 *  `clip` (page units, may be null): pieces that cannot touch it are skipped, which makes a long stroke cost what is visible of it. Only done where it
 *  cannot change a pixel inside the clip: round caps make a cut look like the join it replaces, dashes would lose their phase. */
void strokeCore(QPainter& p, const Stroke& s, const std::vector<double>& pts, const QColor& color, double k, bool lite, const QRectF* clip = nullptr) {
  const size_t n = pts.size() / 2;
  if (n == 0) return;
  if (n == 1 || allPointsEqual(pts)) { dot(p, s, pts, color); return; }
  const Qt::PenCapStyle cap = capOf(s);
  const Outline ol(s, pts);
  const size_t np = ol.pieces();
  p.setBrush(Qt::NoBrush);

  auto q = [k, lite](double v) { return inkWidth(v, k, lite); };

  const bool dashed = !attrOr(s.attrs, "style").isEmpty() && !dashOf(s, 1).empty();
  // A stroke that ends where it starts is joined there by the stroker (round join); cut into pieces it would get two overlapping caps instead.
  const bool closed = pts[0] == pts[2 * n - 2] && pts[1] == pts[2 * n - 1];
  const bool cull = clip && !clip->isEmpty() && cap == Qt::RoundCap && !dashed && !closed;
  bool uniform = s.widths.size() <= 1 || dashed;
  if (!uniform) {
    uniform = true;
    const double w0 = q(strokeWidthAt(s, 0));
    for (size_t i = 1; i + 1 < n; i++) if (q(strokeWidthAt(s, i)) != w0) { uniform = false; break; }
  }
  if (uniform) {
    const double w = q(s.widths.size() > 1 ? strokeWidthAt(s, 0) : (s.widths.empty() ? 1.41 : s.widths[0]));
    QPainterPath path;
    bool open = false;   // the path has a current subpath that continues into the next piece
    if (!cull) { path.moveTo(pts[0], pts[1]); open = true; }
    for (size_t i = 0; i < np; i++) {
      const Piece pc = ol.piece(i);
      if (cull && pieceOutside(pc, *clip, k)) { open = false; continue; }
      if (!open) { path.moveTo(pc.a); open = true; }
      appendPiece(path, pc);
    }
    if (path.isEmpty()) return;
    QPen pen = penOf(color, w, cap);
    if (dashed) {
      const std::vector<qreal> d = dashOf(s, w);
      pen.setDashPattern(QList<qreal>(d.begin(), d.end()));
    }
    p.setPen(pen);
    p.drawPath(path);
    return;
  }

  // Varying width: runs of pieces of equal rounded width are one path each; a piece whose width changes is drawn as short lines of interpolated width
  // (the proto look), with round caps so the joins disappear. The live layer draws the same pieces one by one through drawInkPiece.
  QPainterPath run;
  double runW = -1;
  bool have = false;
  auto flush = [&] {
    if (!have) return;
    p.setPen(penOf(color, runW, cap));
    p.drawPath(run);
    run = QPainterPath();
    have = false;
  };
  QPen ramp = penOf(color, 1, Qt::RoundCap);
  const bool batch = inkBatching();
  for (size_t i = 0; i < np; i++) {
    const Piece pc = ol.piece(i);
    if (cull && pieceOutside(pc, *clip, k)) { flush(); continue; }
    const double qa = q(pc.wa), qb = q(pc.wb);
    if (qa == qb && batch) {
      if (!have || runW != qa) { flush(); runW = qa; run.moveTo(pc.a); have = true; }
      appendPiece(run, pc);
      continue;
    }
    flush();
    drawInkPiece(p, ramp, cap, pc.a, pc.c, pc.b, pc.wa, pc.wb, k, lite);
  }
  flush();
}

bool uniformWidthFor(const Stroke& s, double k, bool lite) {
  if (s.widths.size() <= 1) return true;
  auto q = [k, lite](double v) { return inkWidth(v, k, lite); };
  const double w0 = q(strokeWidthAt(s, 0));
  for (size_t i = 1; i + 1 < s.pointCount(); i++) if (q(strokeWidthAt(s, i)) != w0) return false;
  return true;
}

double widestOf(const Stroke& s) {
  double m = s.widths.empty() ? 1.41 : 0;
  for (double w : s.widths) m = std::max(m, w);
  return m;
}

QRectF pointsBox(const Stroke& s) {
  if (s.pts.size() < 2) return QRectF();
  double x0 = s.pts[0], y0 = s.pts[1], x1 = x0, y1 = y0;
  for (size_t i = 2; i + 1 < s.pts.size(); i += 2) {
    x0 = std::min(x0, s.pts[i]); x1 = std::max(x1, s.pts[i]);
    y0 = std::min(y0, s.pts[i + 1]); y1 = std::max(y1, s.pts[i + 1]);
  }
  return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

}  // namespace

GlowSpec glowSpec(double size, double intensity) {
  return GlowSpec{std::min(40.0, size), std::clamp(intensity, 0.05, 1.0)};
}

namespace {

using Glow = GlowSpec;

std::optional<Glow> glowOf(const Stroke& s) {
  const QString* gs = attrFind(s.attrs, "glow");
  if (!gs) return std::nullopt;
  bool ok = false;
  const double size = gs->toDouble(&ok);
  if (!ok || !(size > 0)) return std::nullopt;
  double a = 0.8;
  if (const QString* ga = attrFind(s.attrs, "glowAlpha")) {
    const double v = ga->toDouble(&ok);
    if (ok) a = v;
  }
  return glowSpec(size, a);
}

/** Draws a device-pixel image whose corner is at pixel (ox, oy) relative to the page origin. Aligned to the pixel grid so it is not resampled
 *  when the transform is a plain scale; going through the painter's page coordinates keeps it right on any device pixel ratio. */
void drawPixels(QPainter& p, const QImage& img, int ox, int oy, double k) {
  p.save();
  p.setRenderHint(QPainter::SmoothPixmapTransform, false);
  p.drawImage(QRectF(ox / k, oy / k, img.width() / k, img.height() / k), img);
  p.restore();
}

/** Ink of the stroke in a bitmap covering `box` (page units) at `k` pixels per unit. Returns the pixel origin. */
QImage inkLayer(const Stroke& s, const std::vector<double>& pts, const QRectF& box, double k, const QColor& color, bool lite, int* ox, int* oy) {
  *ox = int(std::floor(box.left() * k));
  *oy = int(std::floor(box.top() * k));
  const int w = int(std::ceil(box.right() * k)) - *ox, h = int(std::ceil(box.bottom() * k)) - *oy;
  if (w <= 0 || h <= 0 || double(w) * h > kMaxLayerPixels) return QImage();
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter ip(&img);
  ip.setRenderHint(QPainter::Antialiasing);
  ip.setTransform(QTransform(k, 0, 0, k, -*ox, -*oy));
  strokeCore(ip, s, pts, color, k, lite);
  return img;
}

// ---------------------------------------------------------------- glow

/** One box-blur pass (horizontal, then vertical, zero outside the buffer) that computes only the window `o` of the result: every pixel inside it gets the value a
 *  whole-buffer pass would give; pixels outside are left alone. */
void boxBlurPass(std::vector<uint8_t>& a, std::vector<uint8_t>& tmp, int w, int h, int r, const QRect& o) {
  if (r <= 0 || o.isEmpty()) return;
  const int div = 2 * r + 1;
  const int y0 = std::max(0, o.top() - r), y1 = std::min(h - 1, o.bottom() + r);
  for (int y = y0; y <= y1; y++) {   // horizontal, for the columns of the window (the vertical pass reads each column on its own)
    const uint8_t* src = &a[size_t(y) * w];
    uint8_t* dst = &tmp[size_t(y) * w];
    unsigned acc = 0;
    for (int i = std::max(0, o.left() - r); i <= std::min(w - 1, o.left() + r); i++) acc += src[i];
    for (int x = o.left(); x <= o.right(); x++) {
      dst[x] = uint8_t(acc / div);
      if (x + r + 1 < w) acc += src[x + r + 1];
      if (x - r >= 0) acc -= src[x - r];
    }
  }
  for (int x = o.left(); x <= o.right(); x++) {   // vertical
    unsigned acc = 0;
    for (int i = std::max(0, o.top() - r); i <= std::min(h - 1, o.top() + r); i++) acc += tmp[size_t(i) * w + x];
    for (int y = o.top(); y <= o.bottom(); y++) {
      a[size_t(y) * w + x] = uint8_t(acc / div);
      if (y + r + 1 < h) acc += tmp[size_t(y + r + 1) * w + x];
      if (y - r >= 0) acc -= tmp[size_t(y - r) * w + x];
    }
  }
}

/** The three box radii whose chained blur approximates a Gaussian of `sigma` (the standard sizing). */
std::array<int, 3> boxRadii(double sigma) {
  const int passes = 3;
  const double wi = std::sqrt(12 * sigma * sigma / passes + 1);
  int wl = int(std::floor(wi));
  if (wl % 2 == 0) wl--;
  const int wu = wl + 2;
  const int m = int(std::round((12 * sigma * sigma - passes * wl * wl - 4 * passes * wl - 3 * passes) / (-4.0 * wl - 4)));
  std::array<int, 3> r{};
  for (int i = 0; i < passes; i++) r[size_t(i)] = ((i < m ? wl : wu) - 1) / 2;
  return r;
}

int sumOf(const std::array<int, 3>& r) { return r[0] + r[1] + r[2]; }

QRect grown(const QRect& r, int by, int w, int h) { return r.adjusted(-by, -by, by, by).intersected(QRect(0, 0, w, h)); }

/** Gaussian blur as three box blurs; the result is a new buffer, right inside `need` (the rest is not meant to be read). */
std::vector<uint8_t> blurred(const std::vector<uint8_t>& src, int w, int h, double sigma, const QRect& need) {
  std::vector<uint8_t> a = src, tmp(src.size());
  const std::array<int, 3> rad = boxRadii(sigma);
  // the last pass is needed over `need`, each earlier one over what the next reads
  const QRect o3 = need, o2 = grown(need, rad[2], w, h), o1 = grown(o2, rad[1], w, h);
  boxBlurPass(a, tmp, w, h, rad[0], o1);
  boxBlurPass(a, tmp, w, h, rad[1], o2);
  boxBlurPass(a, tmp, w, h, rad[2], o3);
  return a;
}

constexpr double kGlowFactors[3] = {1.0, 0.45, 0.16};   // the three chained shadows of render.ts: wide and faint, middle, tight

double glowBaseSigma(const Glow& g, double k) { return std::max(0.3, g.size * k * 0.5); }

/** Pixels farther than this from a change in the ink are not changed by the glow filter (the sum of every box radius of the three blurs). */
int glowReachOf(const Glow& g, double k) {
  const double base = glowBaseSigma(g, k);
  int r = 0;
  for (double f : kGlowFactors) r += sumOf(boxRadii(base * f));
  return r + 1;
}

/** The render.ts filter on the alpha channel of the ink: three chained soft shadows in the stroke's colour, each laid behind what is already there.
 *  Right inside `need` (default: all of A); computing a window needs A right up to glowReachOf() around it, and the work shrinks stage by stage. */
void glowCascade(std::vector<uint8_t>& A, int w, int h, const Glow& g, double k, QRect need = QRect()) {
  if (need.isNull()) need = QRect(0, 0, w, h);
  const double base = glowBaseSigma(g, k);
  const unsigned I = unsigned(std::lround(g.intensity * 255));
  const int r2 = sumOf(boxRadii(base * kGlowFactors[1])), r3 = sumOf(boxRadii(base * kGlowFactors[2]));
  // stage s is computed over the window the later stages read: the window of the result grown by the radii of the stages after it
  const QRect win[3] = {grown(need, r2 + r3, w, h), grown(need, r3, w, h), need};
  for (int s = 0; s < 3; s++) {
    const std::vector<uint8_t> B = blurred(A, w, h, base * kGlowFactors[s], win[s]);
    for (int y = win[s].top(); y <= win[s].bottom(); y++)
      for (int x = win[s].left(); x <= win[s].right(); x++) {
        const size_t i = size_t(y) * w + x;
        const unsigned shadow = B[i] * I / 255;   // the shadow sits behind what is already there
        A[i] = uint8_t(A[i] + (255 - A[i]) * shadow / 255);
      }
  }
}

struct GlowEntry { QImage img; int ox, oy; qint64 bytes; std::list<quint64>::iterator lru; };
struct GlowCache {
  QMutex mutex;
  std::unordered_map<quint64, GlowEntry> map;
  std::list<quint64> order;   // least recently used first
  qint64 bytes = 0;
};
GlowCache& glowCache() { static GlowCache c; return c; }

quint64 fnv(quint64 h, const void* data, size_t n) {
  const auto* b = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

quint64 glowKey(const Stroke& s, const Glow& g, double k) {
  quint64 h = 14695981039346656037ull;
  h = fnv(h, s.pts.data(), s.pts.size() * sizeof(double));
  h = fnv(h, s.widths.data(), s.widths.size() * sizeof(double));
  const QString attrs = s.color() + "|" + attrOr(s.attrs, "capStyle") + "|" + attrOr(s.attrs, "style");
  h = fnv(h, attrs.constData(), size_t(attrs.size()) * sizeof(QChar));
  const double extra[3] = {g.size, g.intensity, std::round(k * 1000)};
  return fnv(h, extra, sizeof extra);
}

/** The halo of a stroke as a bitmap: the render.ts filter is three chained soft shadows (wide and faint, middle, tight), all in the stroke's colour,
 *  so they combine into one alpha channel. */
QImage makeGlow(const Stroke& s, const std::vector<double>& pts, const Glow& g, double k, int* ox, int* oy) {
  const QColor color = colorOf(s.color());
  const int reach = int(std::ceil(g.size * k * 1.6 + widestOf(s) * k + 2));
  const QRectF b = pointsBox(s);
  const QRectF box(b.left() - reach / k, b.top() - reach / k, b.width() + 2 * reach / k, b.height() + 2 * reach / k);
  QImage ink = inkLayer(s, pts, box, k, color, false, ox, oy);
  if (ink.isNull()) return ink;
  const int w = ink.width(), h = ink.height();
  std::vector<uint8_t> A(size_t(w) * h);
  for (int y = 0; y < h; y++) {
    const QRgb* row = reinterpret_cast<const QRgb*>(ink.constScanLine(y));
    for (int x = 0; x < w; x++) A[size_t(y) * w + x] = uint8_t(qAlpha(row[x]));
  }
  glowCascade(A, w, h, g, k);
  QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
  const unsigned r = unsigned(color.red()), gg = unsigned(color.green()), bl = unsigned(color.blue());
  for (int y = 0; y < h; y++) {
    QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
    for (int x = 0; x < w; x++) {
      const unsigned a = A[size_t(y) * w + x];
      row[x] = qRgba(int(r * a / 255), int(gg * a / 255), int(bl * a / 255), int(a));
    }
  }
  return out;
}

/** True when the glow was drawn (false: too big, the plain stroke is drawn instead). */
bool drawGlowing(QPainter& p, const Stroke& s, const std::vector<double>& pts, const Glow& g, double k) {
  const quint64 key = glowKey(s, g, k);
  GlowCache& c = glowCache();
  {
    QMutexLocker lock(&c.mutex);
    auto it = c.map.find(key);
    if (it != c.map.end()) {
      c.order.splice(c.order.end(), c.order, it->second.lru);
      drawPixels(p, it->second.img, it->second.ox, it->second.oy, k);
      return true;
    }
  }
  int ox = 0, oy = 0;
  QImage img = makeGlow(s, pts, g, k, &ox, &oy);   // outside the lock: other tiles keep drawing meanwhile
  if (img.isNull()) return false;
  drawPixels(p, img, ox, oy, k);
  if (double(img.width()) * img.height() > kGlowKeepPixels) return true;
  QMutexLocker lock(&c.mutex);
  if (c.map.count(key)) return true;   // another thread made it first
  const qint64 bytes = img.sizeInBytes();
  c.order.push_back(key);
  c.map.emplace(key, GlowEntry{std::move(img), ox, oy, bytes, std::prev(c.order.end())});
  c.bytes += bytes;
  while (c.bytes > kGlowCacheBytes && c.map.size() > 1) {
    const quint64 old = c.order.front();
    c.order.pop_front();
    c.bytes -= c.map[old].bytes;
    c.map.erase(old);
  }
  return true;
}

// ---------------------------------------------------------------- display points

struct SmoothCache {
  QMutex mutex;
  struct Entry { std::shared_ptr<const std::vector<double>> pts; qint64 bytes; std::list<quint64>::iterator lru; };
  std::unordered_map<quint64, Entry> map;
  std::list<quint64> order;
  qint64 bytes = 0;
};
SmoothCache& smoothCache() { static SmoothCache c; return c; }

/** Smoothed copy of the points when the input grid would show as stairs at this zoom (geometry.h displayPoints), else null: draw the stored ones.
 *  Smoothing a long stroke is not free, so copies are kept (16 MB) by point content. */
std::shared_ptr<const std::vector<double>> displayPointsOf(const Stroke& s, double k) {
  // Same decision as displayPoints in geometry.h (grid step visible as stairs: >= 1.5 px), made here so that the smoothed copy is looked up by
  // content instead of being rebuilt on every draw.
  constexpr double kStairPx = 1.5;
  const bool exact = attrFind(s.attrs, "xlPlane") || attrFind(s.attrs, "xlRiemann");   // generated geometry is exact, never smoothed
  const double step = exact ? 0 : quantizationStep(s.pts);
  if (step * k < kStairPx) return nullptr;
  SmoothCache& c = smoothCache();
  const quint64 key = fnv(14695981039346656037ull, s.pts.data(), s.pts.size() * sizeof(double));
  {
    QMutexLocker lock(&c.mutex);
    auto it = c.map.find(key);
    if (it != c.map.end()) {
      c.order.splice(c.order.end(), c.order, it->second.lru);
      return it->second.pts;
    }
  }
  auto out = std::make_shared<const std::vector<double>>(smoothFreehand(s.pts, step));
  QMutexLocker lock(&c.mutex);
  const qint64 bytes = qint64(out->size() * sizeof(double));
  if (!c.map.count(key)) {
    c.order.push_back(key);
    c.map.emplace(key, SmoothCache::Entry{out, bytes, std::prev(c.order.end())});
    c.bytes += bytes;
    while (c.bytes > (16ll << 20) && c.map.size() > 1) {
      const quint64 old = c.order.front();
      c.order.pop_front();
      c.bytes -= c.map[old].bytes;
      c.map.erase(old);
    }
  }
  return out;
}

}  // namespace

void clearGlowCache() {
  GlowCache& c = glowCache();
  QMutexLocker lock(&c.mutex);
  c.map.clear();
  c.order.clear();
  c.bytes = 0;
  SmoothCache& sc = smoothCache();
  QMutexLocker lock2(&sc.mutex);
  sc.map.clear();
  sc.order.clear();
  sc.bytes = 0;
}

QRectF strokeBounds(const Stroke& s) {
  if (s.pts.size() < 2) return QRectF();
  const QRectF b = pointsBox(s);
  double pad = widestOf(s) / 2 + 2;
  if (const auto g = glowOf(s)) pad += g->size * 1.6;
  return b.adjusted(-pad, -pad, pad, pad);
}

void renderStroke(QPainter& p, const Stroke& s, const RenderOptions& o) {
  if (s.pointCount() == 0) return;
  const QString tool = s.tool();
  if (tool == "eraser") return;   // erasing strokes are not replayed (same known limitation as the original)
  const bool hl = tool == "highlighter";
  const QColor color = colorOf(s.color());
  const double k = std::max(1e-6, o.scale);
  const std::shared_ptr<const std::vector<double>> smooth = displayPointsOf(s, k);
  const std::vector<double>& pts = smooth ? *smooth : s.pts;
  // Xournal++ `fill="N"`: the polygon the stroke outlines is filled with the stroke's colour at alpha N (0..255), under the line.
  if (const QString* fs = attrFind(s.attrs, "fill"); fs && s.pointCount() >= 3) {
    bool ok = false;
    const int fa = fs->toInt(&ok);
    if (ok && fa > 0) {
      QPainterPath poly;
      poly.moveTo(pts[0], pts[1]);
      for (size_t i = 2; i + 1 < pts.size(); i += 2) poly.lineTo(pts[i], pts[i + 1]);
      poly.closeSubpath();
      QColor f = color;
      f.setAlpha(std::min(255, fa));
      p.save();
      if (hl && !o.darkPaper) p.setCompositionMode(QPainter::CompositionMode_Multiply);
      p.fillPath(poly, f);
      p.restore();
    }
  }
  if (!o.lite && o.glow && !hl) {
    if (const auto g = glowOf(s))
      if (drawGlowing(p, s, pts, *g, k)) return;
  }
  p.save();
  // Over a dark paper multiplying would make the highlighter vanish; there it is plain alpha.
  if (hl && !o.darkPaper) p.setCompositionMode(QPainter::CompositionMode_Multiply);
  if (color.alpha() < 255 && !o.lite && !uniformWidthFor(s, k, false)) {
    // Translucent ink with varying width: drawn opaque in a layer and blended once, so overlapping pieces do not darken each other.
    int ox, oy;
    QColor opaque = color;
    opaque.setAlpha(255);
    const double pad = widestOf(s) / 2 + 2;
    const QRectF box = pointsBox(s).adjusted(-pad, -pad, pad, pad);
    const QImage layer = inkLayer(s, pts, box, k, opaque, false, &ox, &oy);
    if (!layer.isNull()) {
      p.setOpacity(p.opacity() * color.alphaF());
      drawPixels(p, layer, ox, oy, k);
      p.restore();
      return;
    }
  }
  strokeCore(p, s, pts, color, k, o.lite, o.cullPieces ? &o.clip : nullptr);
  p.restore();
}

// ---------------------------------------------------------------- live glow

int glowReach(const GlowSpec& spec, double k) { return glowReachOf(spec, std::max(1e-6, k)); }

LiveGlow::LiveGlow(QSize size, const QColor& colour, const GlowSpec& spec, double k) : colour_(colour), spec_(spec), k_(std::max(1e-6, k)) {
  reach_ = glowReachOf(spec_, k_);
  out_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
  out_.fill(Qt::transparent);
}

QRect LiveGlow::update(const QImage& ink, const QRect& dirty) {
  const QRect all = out_.rect();
  const QRect target = dirty.adjusted(-reach_, -reach_, reach_, reach_).intersected(all);   // pixels whose glow can have changed
  if (target.isEmpty() || ink.size() != out_.size()) return QRect();
  const QRect src = target.adjusted(-reach_, -reach_, reach_, reach_).intersected(all);     // what they depend on
  const int w = src.width(), h = src.height();
  std::vector<uint8_t> A(size_t(w) * h);
  for (int y = 0; y < h; y++) {
    const QRgb* row = reinterpret_cast<const QRgb*>(ink.constScanLine(src.y() + y)) + src.x();
    for (int x = 0; x < w; x++) A[size_t(y) * w + x] = uint8_t(qAlpha(row[x]));
  }
  glowCascade(A, w, h, spec_, k_, target.translated(-src.topLeft()));
  const unsigned r = unsigned(colour_.red()), g = unsigned(colour_.green()), b = unsigned(colour_.blue());
  for (int y = target.top(); y <= target.bottom(); y++) {
    QRgb* row = reinterpret_cast<QRgb*>(out_.scanLine(y));
    const uint8_t* a = &A[size_t(y - src.y()) * w - src.x()];
    for (int x = target.left(); x <= target.right(); x++) {
      const unsigned al = a[x];
      row[x] = qRgba(int(r * al / 255), int(g * al / 255), int(b * al / 255), int(al));
    }
  }
  return target;
}
