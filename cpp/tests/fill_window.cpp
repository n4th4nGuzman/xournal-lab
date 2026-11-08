// The Fill tool on a page with a lot of ink works on a window of the raster around the click (computeFill, windowed = true). The result must be
// exactly the whole-raster one (windowed = false): same success or reason, same mask box, same pixels, same magnet info. Checked on a dense page
// (8000 short strokes) with closed shapes of every size (smaller and bigger than the first window), shapes against the page edges and corners,
// shapes with a gap the magnet closes, clicks beside a stroke, and open shapes that leak; at two raster scales, with and without the magnet.
// Also times the click the dense page made slow (8000 short strokes: ~275 ms before the window).
#include "fill.h"
#include <QGuiApplication>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static constexpr double PW = 595, PH = 842;

static Stroke penStroke(std::vector<double> pts, double width = 1.41) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "round"}};
  s.widths = {width};
  s.pts = std::move(pts);
  return s;
}
static std::vector<double> circlePts(double cx, double cy, double R, double from, double to) {
  std::vector<double> o;
  for (int i = 0; i <= 120; i++) { const double a = from + (to - from) * (i / 120.0); o.push_back(cx + std::cos(a) * R); o.push_back(cy + std::sin(a) * R); }
  return o;
}
static std::vector<double> rectPts(double x0, double y0, double x1, double y1, double gapAtTopLeft = 0) {
  return {x0 + gapAtTopLeft, y0, x1, y0, x1, y1, x0, y1, x0, y0 + gapAtTopLeft};
}

static bool same(const FillOutcome& a, const FillOutcome& b) {
  if (a.ok != b.ok || a.reason != b.reason) return false;
  if (!a.ok) return true;
  const FillMask &m = a.mask, &n = b.mask;
  return m.x0 == n.x0 && m.y0 == n.y0 && m.w == n.w && m.h == n.h && m.W == n.W && m.H == n.H && m.count == n.count && m.total == n.total && m.mask == n.mask &&
         a.info.gapUsed == b.info.gapUsed && a.info.moved == b.info.moved;
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> ux(0, PW), uy(0, PH), ua(0, 6.2832);

  // dense page: 8000 short strokes of "handwriting" size, plus the shapes
  std::vector<Stroke> ink;
  for (int i = 0; i < 8000; i++) {
    const double x = ux(rng), y = uy(rng), a = ua(rng), l = 3 + 9 * std::abs(std::sin(i * 1.7));
    ink.push_back(penStroke({x, y, x + std::cos(a) * l * 0.5, y + std::sin(a) * l * 0.5 + 1, x + std::cos(a) * l, y + std::sin(a) * l}));
  }
  struct Shape { const char* name; double cx, cy; };
  std::vector<Shape> shapes;
  auto add = [&](const char* name, std::vector<double> pts, double cx, double cy) { ink.push_back(penStroke(std::move(pts), 2.2)); shapes.push_back({name, cx, cy}); };
  add("small circle", circlePts(150, 200, 25, 0, 6.2832 + 0.05), 150, 200);
  add("medium circle", circlePts(420, 300, 70, 0, 6.2832 + 0.05), 420, 300);
  add("big circle (wider than the first window)", circlePts(300, 560, 190, 0, 6.2832 + 0.05), 300, 560);
  add("box on the top-left corner", rectPts(0, 0, 90, 70), 40, 30);
  add("box on the bottom-right corner", rectPts(PW - 120, PH - 90, PW, PH), PW - 50, PH - 40);
  add("tall box on the left edge", rectPts(0, 600, 60, 820), 20, 700);
  add("box with a 6 pt gap (magnet closes it)", rectPts(250, 40, 350, 120, 6), 300, 80);
  add("box with a 30 pt gap (nothing closes it)", rectPts(450, 40, 570, 140, 30), 520, 100);
  std::vector<const Stroke*> refs;
  ink.reserve(ink.size());
  for (auto& s : ink) refs.push_back(&s);

  int compared = 0, ok = 0, windowedSpeedups = 0;
  double msWin = 0, msWhole = 0;
  for (double scale : {2.0, 4.0}) {
    for (int useMagnet = 0; useMagnet < 2; useMagnet++) {
      for (const Shape& sh : shapes) {
        // the centre of the shape, a click 1..3 pixels beside its wall, and clicks scattered around it
        std::vector<std::pair<double, double>> clicks{{sh.cx, sh.cy}, {sh.cx + 2, sh.cy - 1}};
        for (int k = 0; k < 1; k++) clicks.push_back({sh.cx + (ux(rng) - PW / 2) * 0.12, sh.cy + (uy(rng) - PH / 2) * 0.12});
        for (auto [x, y] : clicks) {
          if (x < 0 || y < 0 || x > PW || y > PH) continue;
          FillSeed seed;
          seed.x = x; seed.y = y;
          std::optional<FillMagnet> mg;
          if (useMagnet) mg = FillMagnet{14, 3};
          const auto t0 = Clock::now();
          const FillOutcome a = computeFill(refs, PW, PH, scale, seed, 2, mg, true);
          msWin += msSince(t0);
          const auto t1 = Clock::now();
          const FillOutcome b = computeFill(refs, PW, PH, scale, seed, 2, mg, false);
          msWhole += msSince(t1);
          compared++;
          const bool eq = same(a, b);
          if (eq) ok++;
          CHECK(eq, "%s at (%.1f,%.1f) scale %g magnet %d: windowed %s/%lld px vs whole %s/%lld px", sh.name, x, y, scale, useMagnet, a.ok ? "ok" : "fail", (long long)a.mask.count, b.ok ? "ok" : "fail", (long long)b.mask.count);
        }
      }
    }
  }
  std::printf("windowed == whole raster in %d of %d fills (windowed total %.0f ms, whole total %.0f ms)\n", ok, compared, msWin, msWhole);
  CHECK(compared >= 90, "compared %d", compared);

  // the slow case itself: one click inside a circle on the page with 8000 strokes, at the scale of a zoomed window (zoom 2 x 1.5 device pixels)
  for (double scale : {2.0, 3.0, 4.0}) {
    FillSeed seed;
    seed.x = 420; seed.y = 300;
    const FillMagnet mg{14, 3};
    double best = 1e9, bestWhole = 1e9;
    for (int rep = 0; rep < 3; rep++) {
      auto t0 = Clock::now();
      const FillOutcome a = computeFill(refs, PW, PH, scale, seed, 2, mg, true);
      best = std::min(best, msSince(t0));
      t0 = Clock::now();
      const FillOutcome b = computeFill(refs, PW, PH, scale, seed, 2, mg, false);
      bestWhole = std::min(bestWhole, msSince(t0));
      CHECK(a.ok && b.ok && !a.mask.mask.empty(), "fill inside the circle");
    }
    std::printf("click in a circle among 8000 strokes, scale %g: windowed %.1f ms, whole raster %.1f ms (%.1fx)\n", scale, best, bestWhole, bestWhole / best);
    if (best < bestWhole) windowedSpeedups++;
  }
  CHECK(windowedSpeedups == 3, "the window is faster at every scale");

  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
