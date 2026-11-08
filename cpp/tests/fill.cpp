// Fill tool and Riemann rectangles: the assertions of the e2e checks of the Electron app (fill*, riemann*, plane*), plus pixel counts and sums
// against the analytic integral.
#include "fill.h"
#include "genobj.h"
#include "geometry.h"
#include "xopp.h"
#include <QGuiApplication>
#include <QImage>
#include <chrono>
#include <cmath>
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static constexpr double PW = 600, PH = 900;

static Stroke penStroke(std::vector<double> pts, double width = 2.82) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "round"}};
  s.widths = {width};
  s.pts = std::move(pts);
  return s;
}
static std::vector<double> circlePts(double cx, double cy, double R, double from, double to) {
  std::vector<double> o;
  for (int i = 0; i <= 90; i++) { const double a = from + (to - from) * (i / 90.0); o.push_back(cx + std::cos(a) * R); o.push_back(cy + std::sin(a) * R); }
  return o;
}
static Layer layerOf(std::vector<Stroke> strokes) {
  Layer l;
  for (auto& s : strokes) l.elements.push_back(std::move(s));
  return l;
}
static FillRequest request(const FillOptions& o, const QString& color = "#cc3300") {
  FillRequest r;
  r.opt = o;
  r.penColor = color;
  return r;
}
static QImage decode(const Image& im) { return QImage::fromData(QByteArray::fromBase64(im.data.toLatin1())); }
/** Colour of the page point (x, y) in the fill image of the result (transparent when outside it). */
static QColor pixelOf(const FillResult& r, double x, double y) {
  const Image& im = std::get<Image>(r.added[0]);
  const double l = attrOr(im.attrs, "left").toDouble(), t = attrOr(im.attrs, "top").toDouble(), rr = attrOr(im.attrs, "right").toDouble(), b = attrOr(im.attrs, "bottom").toDouble();
  if (x < l || y < t || x >= rr || y >= b) return QColor(0, 0, 0, 0);
  const QImage img = decode(im);
  const int px = std::min(img.width() - 1, int((x - l) / (rr - l) * img.width())), py = std::min(img.height() - 1, int((y - t) / (b - t) * img.height()));
  return QColor::fromRgba(img.pixel(px, py));
}
static bool isFill(const FillResult& r, double x, double y, int red, int green, int blue, int tol = 12) {
  const QColor c = pixelOf(r, x, y);
  return c.alpha() > 200 && std::abs(c.red() - red) <= tol && std::abs(c.green() - green) <= tol && std::abs(c.blue() - blue) <= tol;
}
static bool blank(const FillResult& r, double x, double y) { return pixelOf(r, x, y).alpha() < 20; }

static void testPrimitives() {
  // sanitizeFill: defaults, clamps, enums, colours
  FillOptions d = sanitizeFill(QString("garbage"));
  CHECK(d.kind == FillKind::Solid && d.side == FillSide::Left && d.gap == 2 && d.count == 12 && d.maxGap == 14 && d.outline && d.magnet && d.color2 == "#05e1ff", "defaults");
  FillOptions o = sanitizeFill(QString(R"({"side":"right","kind":"riemann","color2":"#AABBCC","angle":900,"opacity":0,"gap":99,"keepLine":true,"rule":"mid","base":"curve","by":"width","count":9999,"width":0,"outline":false,"magnet":false,"maxGap":1})"));
  CHECK(o.side == FillSide::Right && o.kind == FillKind::Riemann && o.color2 == "#aabbcc" && o.angle == 360 && o.opacity == 0.05 && o.gap == 16 && o.keepLine, "clamped 1");
  CHECK(o.rule == RiemannRule::Mid && o.base == RiemannBase::Curve && o.by == RiemannBy::Width && o.count == 400 && o.width == 1 && !o.outline && !o.magnet && o.maxGap == 2, "clamped 2");
  CHECK(sanitizeFill(QString(R"({"color2":"red","kind":"nope","rule":"x","count":"7"})")).color2 == "#05e1ff", "bad colour falls back");
  const FillOptions rt = sanitizeFill(fillToJson(o));
  CHECK(rt.kind == o.kind && rt.rule == o.rule && rt.count == o.count && rt.color2 == o.color2 && rt.opacity == o.opacity, "json round trip");

  // dilate against a brute-force square neighbourhood
  const int W = 40, H = 30;
  std::vector<uint8_t> src(W * H, 0);
  src[5 * W + 7] = 1; src[20 * W + 30] = 1; src[0] = 1;
  for (int r : {0, 1, 3}) {
    const auto got = dilate(src, W, H, r);
    bool ok = true;
    for (int y = 0; y < H && ok; y++) for (int x = 0; x < W; x++) {
      bool want = false;
      for (int yy = std::max(0, y - r); yy <= std::min(H - 1, y + r); yy++) for (int xx = std::max(0, x - r); xx <= std::min(W - 1, x + r); xx++) want = want || src[yy * W + xx];
      if (bool(got[y * W + x]) != want) { ok = false; break; }
    }
    CHECK(ok, "dilate r=%d equals the square neighbourhood", r);
  }
  // flood fill: a walled box with a hole in its wall leaks to the border
  std::vector<uint8_t> free(W * H, 1);
  for (int x = 10; x <= 25; x++) { free[10 * W + x] = 0; free[22 * W + x] = 0; }
  for (int y = 10; y <= 22; y++) { free[y * W + 10] = 0; free[y * W + 25] = 0; }
  auto f = floodFill(free, W, H, {15 * W + 15}, true);
  CHECK(!f.leaked && f.count == 14 * 11, "closed box holds 14x11 = %lld", (long long)f.count);
  free[16 * W + 25] = 1; free[16 * W + 26] = 1;   // not through to the outside yet (a notch)
  free[10 * W + 15] = 1; free[9 * W + 15] = 1;    // hole in the top wall, leads outside
  f = floodFill(free, W, H, {15 * W + 15}, true);
  CHECK(f.leaked, "fill reports the leak through the hole");
  f = floodFill(free, W, H, {15 * W + 15}, false);
  CHECK(!f.leaked && f.count > 14 * 11 + 100, "unbounded fill spreads outside (%lld)", (long long)f.count);

  // paths
  CHECK(pathLength({0, 0, 3, 4, 3, 10}) == 11, "pathLength");
  CHECK(isClosed(circlePts(50, 50, 30, 0, 6.28)) && !isClosed(circlePts(50, 50, 30, 0, 3)) && !isClosed({0, 0, 1, 1}), "isClosed");
  std::vector<double> line;
  for (int i = 0; i <= 100; i++) { line.push_back(i * 2.0); line.push_back(0.3 * std::sin(i)); }
  const auto simp = simplifyPath(line, 1.0);
  CHECK(simp.size() == 4 && simp[0] == 0 && simp[2] == 200, "simplifyPath keeps the ends of a straight wobble");
  const auto mid = middleOf({0, 0, 100, 0}, 0.15);
  CHECK(mid.size() == 4 && near(mid[0], 15, 1e-9) && near(mid[2], 85, 1e-9), "middleOf trims both ends");
  CHECK(middleOf({0, 0, 10, 0}, 0.15) == std::vector<double>({0, 0, 10, 0}), "short paths unchanged");
  CHECK(riemannColumns(100, RiemannBy::Width, 0, 20) == 5 && riemannColumns(100, RiemannBy::Count, 7, 0) == 7 && riemannColumns(1e6, RiemannBy::Width, 0, 1) == 400 && riemannColumns(1, RiemannBy::Width, 0, 200) == 1, "riemannColumns");
  const auto bases = riemannBases({0, 0, 100, 0}, 4, FillSide::Left);
  CHECK(bases.size() == 4 && near(bases[1].a[0], 25, 1e-9) && near(bases[1].b[0], 50, 1e-9) && near(bases[0].normal[1], -1, 1e-9), "left of a rightward line is up the page (y down)");
  CHECK(near(riemannBases({0, 0, 100, 0}, 4, FillSide::Right)[0].normal[1], 1, 1e-9), "right side");
}

static void testGeneratedRules() {
  Stroke ink = penStroke({0, 0, 1, 1});
  CHECK(!isGenerated(ink), "plain ink is a wall");
  Stroke rm = ink; rm.attrs.emplace_back("xlRiemann", "1");
  CHECK(isGenerated(rm), "earlier rectangle outlines are not walls");
  for (const char* role : {"grid", "axisx", "axisy", "sethatch", "witness", "tick"}) {
    Stroke s = ink; s.attrs.emplace_back("xlPlane", "pl1"); s.attrs.emplace_back("xlRole", role);
    CHECK(isGenerated(s), "plane role %s is a backdrop", role);
  }
  for (const char* role : {"figbody", "figarrow", "figmark", "setbody", "setarrow", "setmark"}) {
    Stroke s = ink; s.attrs.emplace_back("xlPlane", "pl1"); s.attrs.emplace_back("xlRole", role);
    CHECK(!isGenerated(s), "role %s is ordinary ink", role);
  }
  // axes attract the base magnet, grid lines do not
  Stroke axis = penStroke({0, 100, 200, 100}); axis.attrs.emplace_back("xlPlane", "p"); axis.attrs.emplace_back("xlRole", "axisx");
  Stroke grid = penStroke({0, 120, 200, 120}); grid.attrs.emplace_back("xlPlane", "p"); grid.attrs.emplace_back("xlRole", "grid");
  CHECK(nearestOnStrokes({&axis, &grid}, 50, 103, 10).has_value() && !nearestOnStrokes({&grid}, 50, 123, 10).has_value(), "axes attract, grid lines do not");
  const auto snapped = snapPathEnds({10, 104, 100, 104, 190, 98}, {&axis}, 10);
  CHECK(near(snapped[1], 100, 1e-9) && near(snapped[5], 100, 1e-9) && snapped[3] == 104, "line ends stick to the axis, the middle stays");
}

static void testClickFill() {
  FillOptions o;
  o.magnet = false; o.gap = 1.5;
  // 2. a click inside a closed circle (computeFill: pixel count)
  const double R = 60;
  Layer circle = layerOf({penStroke(circlePts(300, 700, R, 0, M_PI * 2 + 0.05))});
  {
    const double scale = 2;
    FillSeed seed; seed.x = 300; seed.y = 700;
    const auto t0 = Clock::now();
    const FillOutcome out = computeFill(wallStrokes(circle), PW, PH, scale, seed, 1.5, std::nullopt);
    const double ms = msSince(t0);
    CHECK(out.ok, "%s", out.reason.toUtf8().constData());
    const double area = double(out.mask.count) / (scale * scale);
    // the inside of the stroke's centre line, plus the part tucked under half the line width
    CHECK(area > M_PI * (R - 2) * (R - 2) && area < M_PI * (R + 2) * (R + 2), "closed circle area %.0f vs pi R^2 %.0f", area, M_PI * R * R);
    CHECK(out.mask.count < out.mask.total / 10, "bounded");
    CHECK(out.mask.w / scale < 140 && out.mask.h / scale < 140, "small image");
    std::printf("fill click in a closed circle: %.0f page units^2 (pi R^2 = %.0f), %.1f ms\n", area, M_PI * R * R, ms);
  }
  FillRequest rq = request(o);
  FillResult r = runFill(circle, PW, PH, {300, 700}, rq);
  CHECK(r.ok && !r.stats.wholePage && !r.stats.path, "click mode, bounded");
  CHECK(isFill(r, 300, 700, 204, 51, 0) && isFill(r, 300, 660, 204, 51, 0) && blank(r, 300, 600) && blank(r, 400, 700) && blank(r, 300, 790), "inside circle only");
  CHECK(r.added.size() == 1 && attrOr(std::get<Image>(r.added[0]).attrs, "xlFill") == "1" && std::get<Image>(r.added[0]).tag == "image", "one image with xlFill");
  // click on the stroke itself still works (nearest free pixel)
  FillResult onStroke = runFill(circle, PW, PH, {360, 700}, rq);
  CHECK(onStroke.ok, "a click right on a stroke picks the nearest free pixel");

  // open shape leaks: whole page; a gap of 8 is much narrower than the opening
  Layer open = layerOf({penStroke(circlePts(450, 800, 40, 0.4, M_PI * 2 - 0.4))});
  r = runFill(open, PW, PH, {450, 800}, rq);
  CHECK(r.ok && r.stats.wholePage && isFill(r, 450, 800, 204, 51, 0) && isFill(r, 100, 100, 204, 51, 0), "open shape leaks");
  bool warned = false;
  for (auto& n : r.notices) warned = warned || n.contains("abertura");
  CHECK(warned, "and says so");
  FillOptions g8 = o; g8.gap = 8;
  CHECK(runFill(open, PW, PH, {450, 800}, request(g8)).stats.wholePage, "gap 8 < opening 31 still leaks");

  // 2b. the magnet
  Layer small = layerOf({penStroke(circlePts(450, 800, 40, 0.075, M_PI * 2 - 0.075), 1.41)});   // an opening 6 wide centre to centre, 4.6 free
  CHECK(runFill(small, PW, PH, {450, 800}, request(o)).stats.wholePage, "small opening leaks without the magnet");
  FillOptions mg = o; mg.magnet = true;
  r = runFill(small, PW, PH, {450, 800}, request(mg));
  CHECK(r.ok && !r.stats.wholePage && r.stats.gapUsed >= 6 && r.stats.gapUsed <= 14 && isFill(r, 450, 800, 204, 51, 0) && blank(r, 450, 700) && blank(r, 540, 800),
        "magnet closes a small opening (gapUsed %g)", r.stats.gapUsed);
  bool said = false;
  for (auto& n : r.notices) said = said || n.contains("Imán");
  CHECK(said, "magnet announces the gap it closed");
  // a click a pixel outside a closed circle
  r = runFill(circle, PW, PH, {300 + 61.5, 700}, request(mg));
  CHECK(r.ok && !r.stats.wholePage && r.stats.moved && isFill(r, 300, 700, 204, 51, 0) && blank(r, 300 + 75, 700) && blank(r, 300, 600), "click just outside moves into the shape");
  r = runFill(circle, PW, PH, {300 + 61.5, 700}, rq);
  CHECK(r.ok && r.stats.wholePage && blank(r, 300, 700), "without the magnet it fills the outside");
  // an opening wider than the magnet reaches: nothing is filled; Shift fills anyway; a bigger magnet closes it
  const auto t0 = Clock::now();
  r = runFill(open, PW, PH, {450, 800}, request(mg));
  const double worst = msSince(t0);
  CHECK(!r.ok && !r.notices.empty() && r.notices[0].contains("no se cierra"), "refuses to flood the page");
  FillRequest shift = request(mg);
  shift.force = true;
  r = runFill(open, PW, PH, {450, 800}, shift);
  CHECK(r.ok && r.stats.wholePage, "Shift fills anyway");
  FillOptions big = mg; big.maxGap = 40;
  r = runFill(open, PW, PH, {450, 800}, request(big));
  CHECK(r.ok && !r.stats.wholePage && r.stats.gapUsed > 14, "a bigger magnet closes it (%g)", r.stats.gapUsed);
  std::printf("fill worst case (nothing closes, every gap and neighbour tried): %.0f ms\n", worst);
  // an empty layer is filled entirely, as always
  Layer empty;
  r = runFill(empty, PW, PH, {100, 100}, request(mg));
  CHECK(r.ok && r.stats.wholePage, "a layer without strokes fills whole");
}

static void testDragFill() {
  FillOptions o;
  o.magnet = false; o.gap = 1.5;
  // hump over a baseline: only the area between line and curve
  std::vector<double> hump;
  for (int i = 0; i <= 80; i++) { const double x = 100 + 400.0 * i / 80; hump.push_back(x); hump.push_back(500 - 150 * std::sin(M_PI * (x - 100) / 400)); }
  Layer layer = layerOf({penStroke(hump)});
  FillResult r = runFill(layer, PW, PH, {100, 500, 300, 500, 500, 500}, request(o));   // left of travel = up the page
  CHECK(r.ok && r.stats.path && !r.stats.wholePage, "line drag");
  CHECK(isFill(r, 300, 450, 204, 51, 0) && blank(r, 300, 330) && blank(r, 60, 495) && blank(r, 300, 520), "only between the line and the curve");
  const double area = double(r.stats.pixels) / 4;   // 2 px per unit
  const double expect = 150 * 400 * 2 / M_PI;
  CHECK(near(area, expect, expect * 0.05), "area %.0f vs analytic %.0f", area, expect);
  // the dragged line's ends are off the curve: leaks without the magnet, bounded with it
  FillResult off = runFill(layer, PW, PH, {104, 505, 300, 505, 497, 505}, request(o));
  CHECK(off.ok && !blank(off, 60, 495), "line off the curve leaks");
  FillOptions mg = o; mg.magnet = true;
  FillResult st = runFill(layer, PW, PH, {104, 505, 300, 505, 497, 505}, request(mg));
  CHECK(st.ok && isFill(st, 300, 450, 204, 51, 0) && blank(st, 60, 495) && blank(st, 540, 495), "ends stick to the curve: bounded");
  // 3. a closed curve as the seed: inside on the right of a clockwise drag, outside on the left
  Layer none;
  FillOptions right = o; right.side = FillSide::Right;
  r = runFill(none, PW, PH, circlePts(150, 250, 70, 0, M_PI * 2), request(right, "#0044cc"));
  CHECK(r.ok && isFill(r, 150, 250, 0, 68, 204) && blank(r, 150, 120) && blank(r, 260, 250), "closed seed: inside");
  r = runFill(none, PW, PH, circlePts(150, 250, 70, 0, M_PI * 2), request(o, "#0044cc"));
  CHECK(r.ok && blank(r, 150, 250) && isFill(r, 260, 250, 0, 68, 204) && r.stats.wholePage, "closed seed: outside");
  // 4. gradients and opacity
  FillOptions lin = o; lin.kind = FillKind::Linear; lin.color2 = "#0000ff"; lin.angle = 0;
  const std::vector<double> baseDrag = {100, 500, 300, 500, 500, 500};
  r = runFill(layer, PW, PH, baseDrag, request(lin, "#ff0000"));
  CHECK(r.ok && pixelOf(r, 140, 495).red() > pixelOf(r, 140, 495).blue() + 60 && pixelOf(r, 460, 495).blue() > pixelOf(r, 460, 495).red() + 60, "linear gradient red to blue");
  FillOptions rad = o; rad.kind = FillKind::Radial; rad.color2 = "#0000ff";
  r = runFill(layer, PW, PH, baseDrag, request(rad, "#ff0000"));
  CHECK(r.ok && pixelOf(r, 300, 450).red() > pixelOf(r, 120, 497).red() + 40 && pixelOf(r, 120, 497).blue() > pixelOf(r, 300, 450).blue() + 40, "radial gradient");
  FillOptions half = o; half.opacity = 0.5;
  r = runFill(layer, PW, PH, baseDrag, request(half, "#000000"));
  const QColor c = pixelOf(r, 300, 450);
  CHECK(r.ok && c.alpha() > 100 && c.alpha() < 160, "opacity 0.5 -> alpha %d", c.alpha());
  // keep the dragged line
  FillOptions keep = o; keep.keepLine = true;
  r = runFill(layer, PW, PH, baseDrag, request(keep));
  CHECK(r.ok && r.added.size() == 2 && std::holds_alternative<Stroke>(r.added[1]) && attrOr(std::get<Stroke>(r.added[1]).attrs, "xlRiemann", "no") == "no", "kept line is an ordinary pen stroke");
}

static double humpH(double x) { return 150 * std::sin(M_PI * (x - 100) / 400); }

static void testRiemann() {
  std::vector<double> hump;
  for (int i = 0; i <= 160; i++) { const double x = 100 + 400.0 * i / 160; hump.push_back(x); hump.push_back(500 - humpH(x)); }
  Layer layer = layerOf({penStroke(hump)});
  const std::vector<double> base = {100, 500, 500, 500};
  FillOptions o;
  o.kind = FillKind::Riemann; o.side = FillSide::Left; o.by = RiemannBy::Count; o.count = 8; o.outline = true; o.magnet = true; o.gap = 2;
  auto edges = [](int n, int c) { return std::pair<double, double>{100 + 400.0 * c / n, 100 + 400.0 * (c + 1) / n}; };

  o.rule = RiemannRule::Upper;
  auto t0 = Clock::now();
  FillResult up = runFill(layer, PW, PH, base, request(o, "#00aa00"));
  const double ms = msSince(t0);
  CHECK(up.ok && up.riemann && up.riemann->n == 8 && up.riemann->drawn == 8 && up.riemann->skipped == 0, "upper: 8 of 8");
  bool heights = up.riemann.has_value();
  for (int c = 0; heights && c < 8; c++) {
    double m = 0;
    for (double x = edges(8, c).first; x <= edges(8, c).second; x += 0.5) m = std::max(m, humpH(x));
    heights = heights && near(up.riemann->cols[c].h, m, 4);
    if (!near(up.riemann->cols[c].h, m, 4)) std::printf("  upper col %d: %g vs %g\n", c, up.riemann->cols[c].h, m);
  }
  CHECK(heights, "upper heights within 4 units of the analytic maximum");
  CHECK(up.added.size() == 9 && std::holds_alternative<Image>(up.added[0]), "one image + 8 outlines");
  int outlines = 0;
  for (size_t i = 1; i < up.added.size(); i++) {
    const auto* s = std::get_if<Stroke>(&up.added[i]);
    if (s && attrFind(s->attrs, "xlRiemann") && s->pts.size() == 10 && isGenerated(*s)) outlines++;
  }
  CHECK(outlines == 8, "outlines are generated rectangle strokes with xlRiemann");
  CHECK(isFill(up, 110, 460, 0, 170, 0) && blank(up, 110, 430) && isFill(up, 275, 500 - 148, 0, 170, 0) && blank(up, 300, 520) && blank(up, 60, 480), "upper covers the curve, stays on the base");
  std::printf("riemann upper, 8 columns on a 150-high hump: %.0f ms\n", ms);

  // a second run ignores the first one's outlines (they are generated, not walls)
  Layer withOutlines = layer;
  applyFill(withOutlines, up);
  FillResult again = runFill(withOutlines, PW, PH, base, request(o, "#00aa00"));
  CHECK(again.ok && again.riemann->drawn == 8 && near(again.riemann->cols[3].h, up.riemann->cols[3].h, 1e-6), "earlier outlines do not stop the rays");
  // applyFill puts the image under the ink and the outlines on top; after an earlier fill, after it
  CHECK(std::holds_alternative<Image>(withOutlines.elements[0]) && std::holds_alternative<Stroke>(withOutlines.elements[1]) && withOutlines.elements.size() == 10, "layout after applyFill");
  CHECK(runFill(withOutlines, PW, PH, base, request(o)).insertAt == 1, "a later fill goes above earlier fills");

  o.rule = RiemannRule::Lower;
  FillResult lo = runFill(layer, PW, PH, base, request(o, "#00aa00"));
  CHECK(lo.ok && lo.riemann->drawn == 6 && lo.riemann->skipped == 2, "lower skips the tips (%d drawn)", lo.riemann ? lo.riemann->drawn : -1);
  bool lh = true;
  for (int c = 1; c < 7 && lo.riemann && c - 1 < int(lo.riemann->cols.size()); c++) {
    const auto [x0, x1] = edges(8, c);
    lh = lh && near(lo.riemann->cols[c - 1].h, std::min(humpH(x0), humpH(x1)), 4);
  }
  CHECK(lh, "lower heights");
  CHECK(isFill(lo, 275, 500 - 100, 0, 170, 0) && blank(lo, 275, 500 - 145) && blank(lo, 110, 470), "lower stays under the curve");

  struct Rule { RiemannRule r; double (*f)(double, double); const char* name; };
  const Rule rules[] = {{RiemannRule::Mid, [](double a, double b) { return humpH((a + b) / 2); }, "mid"},
                        {RiemannRule::Left, [](double a, double) { return humpH(a); }, "left"},
                        {RiemannRule::Right, [](double, double b) { return humpH(b); }, "right"}};
  for (const Rule& rule : rules) {
    o.rule = rule.r; o.count = 10;
    FillResult res = runFill(layer, PW, PH, base, request(o));
    std::vector<double> want;
    for (int c = 0; c < 10; c++) { const double h = rule.f(edges(10, c).first, edges(10, c).second); if (h >= 0.5) want.push_back(h); }
    bool ok = res.ok && res.riemann->cols.size() == want.size();
    for (size_t i = 0; ok && i < want.size(); i++) ok = near(res.riemann->cols[i].h, want[i], 5);
    CHECK(ok, "rule %s heights", rule.name);
  }

  // sums against the analytic integral: lower <= exact <= upper, both converge; mid is close
  const double exact = 150 * 400 * 2 / M_PI;
  double prevGap = 1e18;
  for (int n : {10, 40, 100}) {
    o.count = n;
    double sum[3];
    int k = 0;
    for (RiemannRule rl : {RiemannRule::Lower, RiemannRule::Mid, RiemannRule::Upper}) {
      o.rule = rl; o.outline = false;
      FillResult res = runFill(layer, PW, PH, base, request(o));
      double s = 0;
      if (res.riemann) for (auto& c : res.riemann->cols) s += c.h * std::hypot(c.b[0] - c.a[0], c.b[1] - c.a[1]);
      sum[k++] = s;
    }
    CHECK(sum[0] <= exact * 1.01 && sum[2] >= exact * 0.99 && sum[0] <= sum[1] + 1 && sum[1] <= sum[2] + 1, "n=%d lower %.0f <= exact %.0f <= upper %.0f", n, sum[0], exact, sum[2]);
    CHECK(near(sum[1], exact, exact * 0.03), "n=%d midpoint sum %.0f within 3%% of the integral %.0f (the ray stops at the stroke's edge: ~half its width short)", n, sum[1], exact);
    CHECK(sum[2] - sum[0] < prevGap, "upper-lower gap shrinks with n");
    prevGap = sum[2] - sum[0];
    std::printf("riemann n=%3d: lower %.0f  mid %.0f  upper %.0f  (integral %.0f)\n", n, sum[0], sum[1], sum[2], exact);
  }
  o.outline = true;

  // by width: equal columns, the width is reported as the real one
  o.rule = RiemannRule::Mid; o.by = RiemannBy::Width; o.width = 50;
  FillResult bw = runFill(layer, PW, PH, base, request(o));
  CHECK(bw.ok && bw.riemann->n == 8 && near(bw.riemann->width, 50, 1e-9), "by width: 400/50 = 8 columns of 50");
  o.by = RiemannBy::Count; o.count = 8;

  // a base that is not perfectly horizontal (hand drawn): no column goes missing in the middle
  int missing = 0;
  for (RiemannRule rl : {RiemannRule::Mid, RiemannRule::Upper, RiemannRule::Lower, RiemannRule::Left, RiemannRule::Right})
    for (auto sl : std::vector<std::array<double, 4>>{{100, 503, 500, 499}, {100, 498, 500, 502}, {100, 505.3, 500, 497.1}}) {
      o.rule = rl; o.count = 26;
      FillResult res = runFill(layer, PW, PH, {sl[0], sl[1], sl[2], sl[3]}, request(o));
      if (!res.riemann || res.riemann->drawn < 20) missing++;
    }
  CHECK(missing == 0, "tilted bases: %d runs lost columns", missing);

  // no curve above the base: nothing; a click is not a base
  Layer free = layerOf({penStroke({100, 500, 500, 500})});
  o.rule = RiemannRule::Upper;
  FillResult nocurve = runFill(free, PW, PH, {100, 150, 500, 150}, request(o));
  CHECK(!nocurve.ok && !nocurve.notices.empty(), "no curve, no rectangles");
  FillResult click = runFill(layer, PW, PH, {300, 450}, request(o));
  CHECK(!click.ok && click.notices.size() == 1 && click.notices[0].contains("base"), "a click needs a baseline");
  o.rule = RiemannRule::Mid; o.count = 8;
  FillOptions half = o; half.opacity = 0.5; half.rule = RiemannRule::Upper;
  FillResult op = runFill(layer, PW, PH, base, request(half, "#000000"));
  CHECK(op.ok && pixelOf(op, 110, 460).alpha() > 100 && pixelOf(op, 110, 460).alpha() < 160, "opacity applies to the rectangles");

  // a floating curve above a slanted hand drawn base touching neither end
  std::vector<double> floating;
  for (int i = 0; i <= 90; i++) { const double x = 100 + 400.0 * i / 90; floating.push_back(x + std::sin(i * 1.3) * 0.7); floating.push_back(200 - 100 * std::sin(M_PI * i / 90) + std::cos(i * 0.9) * 0.8); }
  Layer fl = layerOf({penStroke(floating)});
  std::string worst;
  for (RiemannRule rl : {RiemannRule::Mid, RiemannRule::Upper, RiemannRule::Lower, RiemannRule::Left, RiemannRule::Right}) {
    o.rule = rl; o.count = 26;
    FillResult res = runFill(fl, PW, PH, {96, 262, 300, 260, 504, 257}, request(o));
    if (!res.riemann || res.riemann->drawn < 24) worst += " drawn";
    else for (auto& c : res.riemann->cols) if (c.h < 55) { worst += " low"; break; }
  }
  CHECK(worst.empty(), "floating curve over a slanted base:%s", worst.c_str());

  // the base: wobbly drag becomes a perfectly straight line (line) or follows the drag (curve)
  o.rule = RiemannRule::Mid; o.count = 12; o.base = RiemannBase::Line;
  std::vector<double> wobble;
  for (int i = 0; i <= 40; i++) { wobble.push_back(100 + i * 10); wobble.push_back(500 + std::sin(i * 0.9) * 6 * std::sin(M_PI * i / 40)); }
  FillResult wl = runFill(layer, PW, PH, wobble, request(o));
  bool straight = wl.riemann.has_value();
  if (straight) for (auto& c : wl.riemann->cols) straight = straight && near(c.a[1], 500, 1e-6) && near(c.b[1], 500, 1e-6);
  CHECK(straight, "straight base from a wobbly drag");
  o.base = RiemannBase::Curve;
  FillResult wc = runFill(layer, PW, PH, wobble, request(o));
  bool follows = wc.riemann.has_value();
  if (follows) { double dev = 0; for (auto& c : wc.riemann->cols) dev = std::max(dev, std::abs(c.a[1] - 500)); follows = dev > 0.5; }
  CHECK(follows, "curve base follows the drag");

  // the file keeps the picture and the outlines
  o.base = RiemannBase::Line; o.rule = RiemannRule::Upper; o.count = 8;
  FillResult fin = runFill(layer, PW, PH, base, request(o, "#00aa00"));
  Document doc;
  doc.pages.resize(1);
  doc.pages[0].layers.push_back(layer);
  applyFill(doc.pages[0].layers[0], fin);
  const QByteArray xml = serializeXml(doc);
  const LoadResult back = parseXml(xml);
  CHECK(back.ok && back.doc == doc, "document with fill survives save and load: %s", back.error.toUtf8().constData());
  const auto cnt = countOf(back.doc);
  CHECK(cnt.images == 1 && cnt.strokes == 9, "1 image, 1 curve + 8 outlines (%zu images, %zu strokes)", cnt.images, cnt.strokes);
}

static void testSpeedWithManyStrokes() {
  // a page with a lot of ink: the fill rasterises it once
  Layer l;
  for (int i = 0; i < 5000; i++) {
    const double x = (i % 100) * 6.0, y = (i / 100) * 17.0;
    l.elements.push_back(penStroke({x, y, x + 4, y + 3, x + 2, y + 9}, 1.0));
  }
  l.elements.push_back(penStroke(circlePts(300, 700, 60, 0, M_PI * 2 + 0.05)));
  FillOptions o;
  o.magnet = false;
  const auto t0 = Clock::now();
  const FillResult r = runFill(l, PW, PH, {300, 700}, request(o));
  std::printf("fill with 5000 strokes on the page: %.0f ms\n", msSince(t0));
  CHECK(r.ok && !r.stats.wholePage, "fills the circle among 5000 strokes");
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  testPrimitives();
  testGeneratedRules();
  testClickFill();
  testDragFill();
  testRiemann();
  testSpeedWithManyStrokes();
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
