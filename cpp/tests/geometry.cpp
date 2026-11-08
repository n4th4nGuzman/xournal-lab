// Geometry: hit testing against brute force, split-stroke invariants, transform round trips, shape recognition, smoothing, and the spatial index.
#include "geometry.h"
#include "genobj.h"
#include "spatial.h"
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static std::mt19937_64 rng(12345);
static double rnd(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }
static int rndi(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

static Stroke randomStroke(double w, double h, bool variable) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
  double x = rnd(0, w), y = rnd(0, h);
  const int n = rndi(2, 40);
  const double a0 = rnd(0, 6.28);
  for (int i = 0; i < n; i++) {
    s.pts.push_back(x); s.pts.push_back(y);
    const double a = a0 + rnd(-0.6, 0.6) * i * 0.1;
    x += std::cos(a) * rnd(1, 6); y += std::sin(a) * rnd(1, 6);
  }
  s.widths = {rnd(0.5, 4)};
  if (variable) for (int i = 1; i < n; i++) s.widths.push_back(rnd(0.5, 4));
  return s;
}

// independent brute-force point-to-polyline distance (projection by parametric clamp, written differently from segDist2)
static double bruteDist(const Stroke& s, double x, double y) {
  const size_t n = s.pts.size() / 2;
  if (n == 1) return std::hypot(x - s.pts[0], y - s.pts[1]);
  double best = 1e300;
  for (size_t i = 0; i + 1 < n; i++) {
    const double ax = s.pts[2 * i], ay = s.pts[2 * i + 1], bx = s.pts[2 * i + 2], by = s.pts[2 * i + 3];
    const double vx = bx - ax, vy = by - ay, L = vx * vx + vy * vy;
    double bestSeg;
    if (L == 0) bestSeg = std::hypot(x - ax, y - ay);
    else {
      const double t = ((x - ax) * vx + (y - ay) * vy) / L;
      if (t <= 0) bestSeg = std::hypot(x - ax, y - ay);
      else if (t >= 1) bestSeg = std::hypot(x - bx, y - by);
      else bestSeg = std::abs((x - ax) * vy - (y - ay) * vx) / std::sqrt(L);
    }
    best = std::min(best, bestSeg);
  }
  return best;
}

static void testHitTesting() {
  int agree = 0;
  for (int it = 0; it < 400; it++) {
    const Stroke s = randomStroke(100, 100, false);
    for (int k = 0; k < 40; k++) {
      const double x = rnd(-10, 110), y = rnd(-10, 110), pad = rnd(0.5, 8);
      const double d = bruteDist(s, x, y);
      if (std::abs(d - pad) < 1e-9) continue;   // on the boundary: either answer is right
      CHECK(strokeHit(s, x, y, pad) == (d <= pad), "d=%g pad=%g", d, pad);
      agree++;
    }
  }
  std::printf("hit testing: %d point/stroke pairs agree with brute force\n", agree);

  // hitElement pads with half the base width; inRect needs a vertex inside; inPolygon the same
  Stroke s = randomStroke(100, 100, true);
  Element e = s;
  for (int k = 0; k < 200; k++) {
    const double x = rnd(0, 100), y = rnd(0, 100);
    CHECK(hitElement(e, x, y, 2) == (bruteDist(s, x, y) <= 2 + s.widths[0] / 2 || std::abs(bruteDist(s, x, y) - (2 + s.widths[0] / 2)) < 1e-9), "hitElement");
  }
  Stroke line;
  line.widths = {2}; line.pts = {0, 0, 100, 100};
  CHECK(!inRect(Element(line), {40, 0, 60, 10}), "a segment crossing the box between vertices does not select by vertex");
  CHECK(inRect(Element(line), {-5, -5, 5, 5}), "vertex inside");
  const std::vector<double> tri = {-10, -10, 20, -10, -10, 20};
  CHECK(inPolygon(Element(line), tri), "vertex (0,0) in the lasso");
  CHECK(!pointInPolygon(30, 30, tri) && pointInPolygon(0, 0, tri) && pointInPolygon(3, 3, tri) && !pointInPolygon(-20, 0, tri), "pointInPolygon");
  // text / image: box overlap and centre
  Image im; im.tag = "image"; setImageBox(im, {10, 10, 50, 30});
  CHECK(inRect(Element(im), {40, 20, 80, 80}) && !inRect(Element(im), {60, 0, 80, 80}), "image box overlap");
  CHECK(inPolygon(Element(im), {0, 0, 100, 0, 100, 100, 0, 100}) && !inPolygon(Element(im), {0, 0, 20, 0, 20, 20, 0, 20}), "image centre in lasso");
  CHECK(hitElement(Element(im), 52, 20, 3) && !hitElement(Element(im), 56, 20, 3), "image hit padding");
  // a rotated formula: its box turned, texContains follows
  Image tex; tex.tag = "teximage"; setImageBox(tex, {0, 0, 100, 20}); setTexPose(tex, {M_PI / 2, false});
  const Rect tb = elementBBox(Element(tex));
  CHECK(std::abs(tb[0] - 40) < 1e-5 && std::abs(tb[2] - 60) < 1e-5 && std::abs(tb[1] + 40) < 1e-5 && std::abs(tb[3] - 60) < 1e-5, "turned formula box %g %g %g %g", tb[0], tb[1], tb[2], tb[3]);
  CHECK(texContains(tex, 50, -30) && !texContains(tex, 90, 10), "texContains in turned coordinates");
}

static void testSplitStroke() {
  int checked = 0, cut = 0;
  for (int it = 0; it < 600; it++) {
    const Stroke s = randomStroke(100, 100, it % 2 == 0);
    const double x = rnd(0, 100), y = rnd(0, 100), r = rnd(1, 15);
    const double pad = r + s.widths[0] / 2;
    const auto res = splitStroke(s, x, y, r);
    const size_t n = s.pts.size() / 2;
    std::vector<bool> hit(n > 0 ? n - 1 : 0);
    bool any = false;
    for (size_t i = 0; i + 1 < n; i++) { hit[i] = std::sqrt(segDist2(x, y, s.pts[2 * i], s.pts[2 * i + 1], s.pts[2 * i + 2], s.pts[2 * i + 3])) <= pad; any = any || hit[i]; }
    if (n == 1) any = std::hypot(x - s.pts[0], y - s.pts[1]) <= pad;
    CHECK(res.has_value() == any, "nullopt iff nothing hit");
    if (!res) continue;
    cut++;
    // pieces: consecutive runs of un-hit segments, in order, widths following their segments
    size_t seg = 0, keptSegs = 0;
    for (const Stroke& p : *res) {
      CHECK(p.pts.size() >= 4, "piece has at least one segment");
      const size_t pn = p.pts.size() / 2;
      while (seg < n - 1 && hit[seg]) seg++;
      CHECK(seg < n - 1, "piece without source");
      if (seg >= n - 1) break;
      for (size_t k = 0; k < pn; k++) CHECK(p.pts[2 * k] == s.pts[2 * (seg + k)] && p.pts[2 * k + 1] == s.pts[2 * (seg + k) + 1], "points copied verbatim");
      for (size_t k = 0; k + 1 < pn; k++) CHECK(!hit[seg + k], "kept segments are outside the eraser");
      if (s.widths.size() > 1) {
        CHECK(p.widths.size() == pn, "one width per segment plus base");
        for (size_t k = 1; k < pn; k++) CHECK(p.widths[k] == s.widths[seg + k], "width follows its segment");
      } else CHECK(p.widths.size() == 1, "constant width stays one value");
      CHECK(p.attrs == s.attrs, "attrs kept");
      seg += pn - 1;
      keptSegs += pn - 1;
    }
    size_t expectKept = 0;
    for (size_t i = 0; i + 1 < n; i++) if (!hit[i]) expectKept++;
    CHECK(keptSegs == expectKept, "every un-hit segment survives exactly once (%zu vs %zu)", keptSegs, expectKept);
    // nothing left within the eraser
    for (const Stroke& p : *res) for (size_t i = 0; i + 1 < p.pts.size() / 2; i++)
      CHECK(std::sqrt(segDist2(x, y, p.pts[2 * i], p.pts[2 * i + 1], p.pts[2 * i + 2], p.pts[2 * i + 3])) > pad - 1e-9, "left inside the eraser");
    checked++;
  }
  std::printf("splitStroke: %d strokes cut, invariants hold\n", cut);
  Stroke dot; dot.widths = {2}; dot.pts = {5, 5};
  CHECK(splitStroke(dot, 5, 5, 1) && splitStroke(dot, 5, 5, 1)->empty() && !splitStroke(dot, 50, 5, 1), "a dot is erased whole or kept");
  (void)checked;
}

static bool close(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static void testTransforms() {
  // affine helpers
  const Aff r = rotateAff(0.7, 30, 40);
  const auto c = applyAff(r, 30, 40);
  CHECK(close(c[0], 30, 1e-9) && close(c[1], 40, 1e-9), "rotation fixes its centre");
  const auto sc = applyAff(scaleAff(2, 3, 10, 10), 10, 10);
  CHECK(close(sc[0], 10, 1e-9) && close(sc[1], 10, 1e-9), "scale fixes its anchor");
  CHECK(close(applyAff(flipAff(true, 5, 0), 3, 7)[0], 7, 1e-9) && close(applyAff(flipAff(false, 0, 5), 3, 7)[1], 3, 1e-9), "flips");
  const Aff m = multiplyAff(translateAff(5, -3), multiplyAff(rotateAff(0.4, 1, 2), scaleAff(1.5, 2, 0, 0)));
  const auto inv = invertAff(m);
  CHECK(inv.has_value(), "invertible");
  const auto fwd = applyAff(m, 12.5, -7);
  const auto p = applyAff(*inv, fwd[0], fwd[1]);
  CHECK(close(p[0], 12.5, 1e-9) && close(p[1], -7, 1e-9), "inverse");
  CHECK(!invertAff(scaleAff(0, 1, 0, 0)), "singular has no inverse");
  const Rect tr = transformRect({0, 0, 10, 10}, rotateAff(M_PI / 4, 5, 5));
  CHECK(close(tr[2] - tr[0], 10 * std::sqrt(2.0), 1e-9), "rotated square bounds");

  // strokes, text, images: apply then inverse
  const std::vector<Aff> maps = {translateAff(13, -7), scaleAff(1.7, 1.7, 20, 30), rotateAff(0.9, 50, 50), flipAff(true, 40, 0), multiplyAff(rotateAff(-0.3, 0, 0), scaleAff(0.6, 0.6, 5, 5))};
  for (const Aff& mm : maps) {
    const auto iv = invertAff(mm);
    Stroke s = randomStroke(100, 100, true);
    Element e = s;
    const Snap snap = snapshot(e);
    const double k = std::sqrt(std::abs(mm.a * mm.d - mm.b * mm.c));
    transformElement(e, mm);
    const auto& t = std::get<Stroke>(e);
    CHECK(close(t.widths[0], s.widths[0] * k, 1e-9), "widths follow sqrt|det|");
    const auto q = applyAff(mm, s.pts[0], s.pts[1]);
    CHECK(close(t.pts[0], q[0], 1e-9) && close(t.pts[1], q[1], 1e-9), "points mapped");
    transformElement(e, *iv);
    const auto& u = std::get<Stroke>(e);
    for (size_t i = 0; i < s.pts.size(); i++) CHECK(close(u.pts[i], s.pts[i], 1e-8), "stroke round trip");
    for (size_t i = 0; i < s.widths.size(); i++) CHECK(close(u.widths[i], s.widths[i], 1e-9), "width round trip");
    restore(e, snap);
    CHECK(std::get<Stroke>(e) == s, "restore gives the original");
  }

  // text: rigid maps and scales round trip (text stays upright; position follows the centre)
  Text tx;
  attrSet(tx.attrs, "font", "Sans"); attrSet(tx.attrs, "size", "20"); attrSet(tx.attrs, "x", "100"); attrSet(tx.attrs, "y", "80"); attrSet(tx.attrs, "color", "#000000ff");
  tx.text = "Hola mundo";
  for (const Aff& mm : {translateAff(33, 9), rotateAff(1.1, 10, 10), flipAff(false, 0, 50)}) {
    Element e = tx;
    const Rect b0 = elementBBox(e);
    transformElement(e, mm);
    const Rect b1 = elementBBox(e);
    const auto cc = applyAff(mm, (b0[0] + b0[2]) / 2, (b0[1] + b0[3]) / 2);
    CHECK(close((b1[0] + b1[2]) / 2, cc[0], 1e-6) && close((b1[1] + b1[3]) / 2, cc[1], 1e-6), "text box centre follows the map");
    transformElement(e, *invertAff(mm));
    const Rect b2 = elementBBox(e);
    for (int i = 0; i < 4; i++) CHECK(close(b2[i], b0[i], 1e-6), "text round trip %d", i);
  }
  {
    Element e = tx;
    transformElement(e, scaleAff(2, 2, 0, 0));
    CHECK(close(std::get<Text>(e).attrs.empty() ? 0 : attrOr(std::get<Text>(e).attrs, "size").toDouble(), 40, 1e-9), "text size scales with sqrt|det|");
  }

  // plain images: box only; turned picture is baked; formula keeps its box and absorbs the pose
  QImage pic(20, 10, QImage::Format_ARGB32);
  pic.fill(QColor(255, 0, 0));
  QByteArray png;
  { QBuffer b(&png); b.open(QIODevice::WriteOnly); pic.save(&b, "PNG"); }
  Image im; im.tag = "image"; im.data = QString::fromLatin1(png.toBase64()); setImageBox(im, {10, 10, 50, 30});
  {
    Element e = im;
    transformElement(e, scaleAff(2, 3, 0, 0));
    CHECK(imageBox(std::get<Image>(e)) == (Rect{20, 30, 100, 90}) && std::get<Image>(e).data == im.data, "plain scale keeps the pixels");
    transformElement(e, rotateAff(M_PI / 2, 60, 60));
    const Image& r2 = std::get<Image>(e);
    const QImage baked = QImage::fromData(QByteArray::fromBase64(r2.data.toLatin1()));
    const Rect bb = imageBox(r2);
    CHECK(!baked.isNull() && r2.data != im.data && close(bb[2] - bb[0], 60, 1e-6) && close(bb[3] - bb[1], 80, 1e-6), "turned picture is baked and its box is the bounds");
    CHECK(baked.width() > 0 && qRed(baked.pixel(baked.width() / 2, baked.height() / 2)) > 200, "baked centre is still red");
  }
  Image tex; tex.tag = "teximage"; setImageBox(tex, {0, 0, 100, 20});
  {
    Element e = tex;
    const Aff rot = rotateAff(M_PI / 2, 50, 10);
    transformElement(e, rot);
    const Image& t = std::get<Image>(e);
    const Rect b = imageBox(t);
    CHECK(close(b[2] - b[0], 100, 1e-9) && close(b[3] - b[1], 20, 1e-9) && close((b[0] + b[2]) / 2, 50, 1e-9) && close((b[1] + b[3]) / 2, 10, 1e-9), "formula keeps its box");
    CHECK(close(texPose(t).rot, M_PI / 2, 1e-6) && !texPose(t).flip, "pose absorbs the turn");
    transformElement(e, *invertAff(rot));
    CHECK(close(texPose(std::get<Image>(e)).rot, 0, 1e-6) && !texPose(std::get<Image>(e)).flip, "back upright");
    const Rect b3 = imageBox(std::get<Image>(e));
    for (int i = 0; i < 4; i++) CHECK(close(b3[i], (Rect{0, 0, 100, 20})[i], 1e-6), "formula box round trip");
    Element f = tex;
    transformElement(f, flipAff(true, 50, 0));
    CHECK(texPose(std::get<Image>(f)).flip, "a mirror sets the flip");
  }
}

static double dist2Poly(const std::vector<double>& poly, double x, double y) {
  double best = 1e300;
  for (size_t i = 0; i + 3 < poly.size(); i += 2) best = std::min(best, segDist2(x, y, poly[i], poly[i + 1], poly[i + 2], poly[i + 3]));
  return std::sqrt(best);
}

static std::vector<double> noisy(const std::vector<double>& poly, int perSeg, double sigma) {
  std::normal_distribution<double> nd(0, sigma);
  std::vector<double> out;
  for (size_t i = 0; i + 3 < poly.size(); i += 2)
    for (int k = 0; k < perSeg; k++) {
      const double t = double(k) / perSeg;
      out.push_back(poly[i] + (poly[i + 2] - poly[i]) * t + nd(rng));
      out.push_back(poly[i + 1] + (poly[i + 3] - poly[i + 1]) * t + nd(rng));
    }
  out.push_back(poly[poly.size() - 2] + nd(rng)); out.push_back(poly.back() + nd(rng));
  return out;
}

static void testShapes() {
  int lines = 0, circles = 0, rects = 0, tris = 0;
  for (int it = 0; it < 30; it++) {
    const double ox = rnd(50, 300), oy = rnd(50, 300);
    // line
    const double ang = rnd(0, 6.28), len = rnd(100, 300);
    auto l = recognizeShape(noisy({ox, oy, ox + std::cos(ang) * len, oy + std::sin(ang) * len}, 40, 0.3));
    CHECK(l && l->size() == 4, "noisy line ang=%g len=%g", ang, len);
    if (l) lines++;
    // ellipse (closed, slightly overshooting)
    std::vector<double> circ;
    const double rx = rnd(40, 120), ry = rnd(40, 120);
    std::normal_distribution<double> nd(0, 0.02 * std::min(rx, ry));
    for (int i = 0; i <= 90; i++) { const double a = i / 90.0 * 6.28 + 0.1; circ.push_back(ox + std::cos(a) * (rx + nd(rng))); circ.push_back(oy + std::sin(a) * (ry + nd(rng))); }
    auto c = recognizeShape(circ);
    CHECK(c && c->size() == 2 * 73, "noisy ellipse -> 72-gon (%zu)", c ? c->size() : 0);
    if (c) circles++;
    // axis-aligned rectangle with a small gap at the closing corner
    const double w = rnd(60, 200), h = rnd(40, 160);
    auto r = recognizeShape(noisy({ox, oy, ox + w, oy, ox + w, oy + h, ox, oy + h, ox + 2, oy + 3}, 20, 0.8));
    CHECK(r && r->size() == 10, "noisy rectangle");
    if (r) {
      rects++;
      CHECK(close((*r)[0], (*r)[8], 1e-9) && close((*r)[1], (*r)[9], 1e-9), "rectangle closes");
      const bool axis = ((*r)[1] == (*r)[3] && (*r)[2] == (*r)[4]) || ((*r)[0] == (*r)[2]);
      CHECK(axis, "aligned rectangle snaps to its bounds");
    }
    // triangle
    auto t = recognizeShape(noisy({ox, oy, ox + 150, oy + 20, ox + 60, oy + 140, ox + 1, oy + 1}, 25, 0.8));
    CHECK(t && t->size() == 8, "noisy triangle (%zu)", t ? t->size() : 0);
    if (t) tris++;
  }
  std::printf("recognizeShape: %d lines, %d ellipses, %d rectangles, %d triangles recognised of 30 each\n", lines, circles, rects, tris);
  // not a shape
  std::vector<double> squiggle;
  for (int i = 0; i < 80; i++) { squiggle.push_back(i * 4.0); squiggle.push_back(50 + 40 * std::sin(i * 0.3)); }
  CHECK(!recognizeShape(squiggle), "a wave is kept as drawn");
  CHECK(!recognizeShape({0, 0, 1, 1, 2, 2}), "too few points");
  // near-horizontal line snaps to the axis
  auto h = recognizeShape(noisy({0, 100, 200, 103}, 30, 0.2));
  CHECK(h && (*h)[1] == (*h)[3], "nearly horizontal snaps");
  // shapePoints
  const auto sq = shapePoints(ShapeKind::Rect, 10, 10, 50, 30, true, 2);
  CHECK(sq.size() == 10 && sq[2] == 50 && sq[5] == 50, "constrained rectangle is a square");
  const auto el = shapePoints(ShapeKind::Ellipse, 0, 0, 100, 60, false, 2);
  CHECK(el.size() == 146 && close(el[0], 100, 1e-9) && close(el[1], 30, 1e-9), "ellipse 72 segments");
  const auto ln = shapePoints(ShapeKind::Line, 0, 0, 100, 27, true, 2);
  CHECK(close(std::atan2(ln[3], ln[2]), M_PI / 12 * 2, 1e-9) || close(std::atan2(ln[3], ln[2]), M_PI / 12, 1e-9), "line snaps to 15 degrees");
  const auto ar = shapePoints(ShapeKind::Arrow, 0, 0, 100, 0, false, 2);
  CHECK(ar.size() == 10 && close(ar[2], 100, 1e-9) && close(std::hypot(ar[4] - 100, ar[5]), 12, 1e-9), "arrow head 6*width long");
}

static void testSmoothing() {
  const double step = 0.5;   // zoom 2
  std::vector<double> truth, q;
  for (int i = 0; i <= 300; i++) {
    const double a = i / 300.0 * 3.0;
    const double x = 50 + 60 * std::cos(a), y = 80 + 60 * std::sin(a);
    truth.push_back(x); truth.push_back(y);
    q.push_back(std::round(x / step) * step); q.push_back(std::round(y / step) * step);
  }
  const double g = quantizationStep(q);
  CHECK(close(g, step, 1e-3), "quantization step recovered: %g", g);
  CHECK(quantizationStep(truth) == 0, "sub-pixel input has no step");
  const auto sm = smoothFreehand(q, step);
  CHECK(sm.size() == q.size(), "same point count");
  CHECK(sm[0] == q[0] && sm[1] == q[1] && sm[sm.size() - 2] == q[q.size() - 2], "ends stay put");
  double maxMove = 0, errRaw = 0, errSm = 0;
  for (size_t i = 0; i < q.size(); i++) maxMove = std::max(maxMove, std::abs(sm[i] - q[i]));
  CHECK(maxMove <= step / 2 + 1e-9, "no point leaves its cell (%g)", maxMove);
  for (size_t i = 0; i + 1 < q.size(); i += 2) { errRaw += dist2Poly(truth, q[i], q[i + 1]); errSm += dist2Poly(truth, sm[i], sm[i + 1]); }
  CHECK(errSm < errRaw, "smoothed is closer to the true curve (%g < %g)", errSm, errRaw);
  std::printf("smoothFreehand: mean error to true arc %.4f -> %.4f (step %.2f)\n", errRaw / 301, errSm / 301, step);
  Stroke s; s.pts = q; s.widths = {1};
  StrokeView v;
  const auto& a = displayPoints(s, 2.0, v);      // step * scale = 1: the stored points
  CHECK(&a == &s.pts, "at the drawn scale the stored points are used");
  const auto& b = displayPoints(s, 8.0, v);      // stairs would show
  CHECK(&b != &s.pts && b == sm, "zoomed in the smoothed copy is drawn");
  Stroke gen = s; gen.attrs = {{"xlPlane", "pl1"}};
  StrokeView v2;
  CHECK(&displayPoints(gen, 8.0, v2) == &gen.pts, "generated strokes are never smoothed");
}

static void testIndex() {
  const double W = 600, H = 800;
  std::vector<Element> els;
  for (int i = 0; i < 3000; i++) els.push_back(randomStroke(W, H, i % 3 == 0));
  Image big; big.tag = "image"; setImageBox(big, {0, 0, 600, 800}); els.push_back(big);   // covers the whole page: the "wide" list
  Image pic; pic.tag = "image"; setImageBox(pic, {100, 100, 160, 140}); els.push_back(pic);
  Page page;
  page.layers.resize(2);
  for (size_t i = 0; i < els.size(); i++) page.layers[i % 2].elements.push_back(els[i]);
  ElementIndex idx(64);
  idx.rebuild(page);
  CHECK(idx.size() == els.size(), "all indexed");

  auto bruteQuery = [&](const Rect& r) {
    std::vector<uint64_t> out;
    for (size_t li = 0; li < page.layers.size(); li++)
      for (size_t i = 0; i < page.layers[li].elements.size(); i++)
        if (rectsIntersect(elementBBox(page.layers[li].elements[i]), r)) out.push_back(elementId(uint32_t(li), uint32_t(i)));
    return out;
  };
  std::vector<uint64_t> got;
  for (int k = 0; k < 300; k++) {
    const double x = rnd(-50, W), y = rnd(-50, H), w = rnd(0, 200), h = rnd(0, 200);
    const Rect r = {x, y, x + w, y + h};
    idx.query(r, got);
    CHECK(got == bruteQuery(r), "query == brute force");
  }
  idx.query({-1e12, -1e12, 1e12, 1e12}, got);
  CHECK(got.size() == els.size(), "huge query returns everything once");
  // point hits: candidates + exact test == full scan
  for (int k = 0; k < 500; k++) {
    const double x = rnd(0, W), y = rnd(0, H), pad = rnd(1, 6);
    idx.queryPoint(x, y, pad + 2.5, got);
    std::vector<uint64_t> viaIndex, brute;
    for (uint64_t id : got) if (hitElement(page.layers[idLayer(id)].elements[idIndex(id)], x, y, pad)) viaIndex.push_back(id);
    for (size_t li = 0; li < page.layers.size(); li++)
      for (size_t i = 0; i < page.layers[li].elements.size(); i++)
        if (hitElement(page.layers[li].elements[i], x, y, pad)) brute.push_back(elementId(uint32_t(li), uint32_t(i)));
    CHECK(viaIndex == brute, "hit candidates miss nothing (pad+max half width covers)");
  }
  // remove / update / reinsert
  std::vector<uint64_t> ids;
  idx.query({0, 0, W, H}, ids);
  for (size_t i = 0; i < ids.size(); i += 3) { CHECK(idx.remove(ids[i]), "remove"); }
  CHECK(!idx.remove(ids[0]) && !idx.contains(ids[0]), "removed is gone");
  idx.update(ids[1], {1000, 1000, 1010, 1010});
  idx.query({995, 995, 1005, 1005}, got);
  CHECK(got.size() == 1 && got[0] == ids[1], "updated element found at its new place");
  idx.query(elementBBox(page.layers[idLayer(ids[1])].elements[idIndex(ids[1])]), got);
  CHECK(std::find(got.begin(), got.end(), ids[1]) == got.end(), "and no longer at the old one");
  idx.insert(ids[0], {5, 5, 6, 6});
  idx.query({4, 4, 7, 7}, got);
  CHECK(std::find(got.begin(), got.end(), ids[0]) != got.end(), "reinserted");
  size_t live = 0;
  for (size_t i = 0; i < ids.size(); i++) if (i % 3 != 0 || i == 0) live++;
  CHECK(idx.size() == live, "size after removals %zu vs %zu", idx.size(), live);

  // 20k strokes: build, pointer queries, versus a scan of every element
  Page big20;
  big20.layers.resize(1);
  for (int i = 0; i < 20000; i++) big20.layers[0].elements.push_back(randomStroke(1200, 1600, i % 4 == 0));
  auto t0 = Clock::now();
  ElementIndex ix(64);
  ix.rebuild(big20);
  const double tBuild = msSince(t0);
  const int N = 2000;
  std::vector<std::pair<double, double>> probes;
  for (int i = 0; i < N; i++) probes.push_back({rnd(0, 1200), rnd(0, 1600)});
  t0 = Clock::now();
  size_t hitsIdx = 0;
  for (auto [x, y] : probes) {
    ix.queryPoint(x, y, 5 + 2.5, got);
    for (uint64_t id : got) hitsIdx += hitElement(big20.layers[0].elements[idIndex(id)], x, y, 5);
  }
  const double tIdx = msSince(t0);
  t0 = Clock::now();
  size_t hitsScan = 0;
  for (auto [x, y] : probes) for (const Element& e : big20.layers[0].elements) hitsScan += hitElement(e, x, y, 5);
  const double tScan = msSince(t0);
  CHECK(hitsIdx == hitsScan, "20k hits agree %zu vs %zu", hitsIdx, hitsScan);
  t0 = Clock::now();
  for (int i = 0; i < 200; i++) { const auto id = elementId(0, uint32_t(i * 7)); const Rect b = elementBBox(big20.layers[0].elements[idIndex(id)]); ix.remove(id); ix.insert(id, b); }
  const double tUpd = msSince(t0);
  std::printf("TIMING 20k strokes: build %.1f ms | %d point hit tests via index %.2f ms (%.1f us each) vs full scan %.1f ms (%.0f us each, %.0fx) | 200 remove+insert %.2f ms\n",
              tBuild, N, tIdx, tIdx * 1000 / N, tScan, tScan * 1000 / N, tScan / std::max(tIdx, 1e-9), tUpd);
  CHECK(tIdx * 20 < tScan, "the index is much faster than a scan");
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);   // text metrics and images need the GUI module's font database
  testHitTesting();
  testSplitStroke();
  testTransforms();
  testShapes();
  testSmoothing();
  testIndex();
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
