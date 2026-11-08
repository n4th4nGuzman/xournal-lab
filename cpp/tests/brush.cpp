// Pen tips and pressure curves: the "tab*" pure checks of the Electron e2e (app.ts), plus the allocation-free pointer path.
#include "brush.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)

// Count heap allocations to prove the pointer path does not allocate.
static long long g_allocs = 0;
void* operator new(std::size_t n) { g_allocs++; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct Sample { double x, y, p; };
struct Run { std::vector<double> pts, widths, fin; };

static Run run(BrushKind kind, const std::vector<Sample>& samples, const PressureMap& map, bool pressure = true) {
  BrushEngine eng(kind, 2, map, pressure);
  Run r;
  r.widths.push_back(eng.first());
  for (size_t i = 0; i < samples.size(); i++) {
    r.pts.push_back(samples[i].x); r.pts.push_back(samples[i].y);
    if (i == 0) eng.next(samples[i].x, samples[i].y, samples[i].p, 0);
    else r.widths.push_back(eng.next(samples[i].x, samples[i].y, samples[i].p, double(i) * 8));
  }
  r.fin = eng.finish(r.pts, r.widths);
  return r;
}
static std::vector<Sample> ramp(double x0, double y0, double x1, double y1, int n = 40) {
  std::vector<Sample> s;
  for (int i = 0; i <= n; i++) s.push_back({x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, std::sin(double(i) / n * M_PI) * 0.9 + 0.05});
  return s;
}
static std::vector<Sample> flat(int n, double p) {
  std::vector<Sample> s;
  for (int i = 0; i < n; i++) s.push_back({100.0 + i * 2, 100, p});
  return s;
}
static std::vector<Sample> along(double deg) {
  std::vector<Sample> s;
  const double r = deg * M_PI / 180;
  for (int i = 0; i < 40; i++) s.push_back({100 + std::cos(r) * i * 2, 100 + std::sin(r) * i * 2, 0.5});
  return s;
}
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) <= e; }

int main() {
  const PressureMap lin, soft(curvePresets()[1].curve), firm(curvePresets()[2].curve);
  PressureCurve dz = kLinearCurve; dz.min = 0.1;
  const PressureMap dead(dz);

  // settings from anything
  const PressureCurve bad = sanitizeCurve(QJsonDocument::fromJson(R"({"x1":9,"y1":"a","min":-3})").object());
  CHECK(bad.x1 == 1 && bad.y1 == 0.25 && bad.x2 == 0.75 && bad.min == 0);
  CHECK(sanitizeCurve(QJsonValue()) == kLinearCurve);
  CHECK(sanitizeCurve(QJsonDocument::fromJson(R"({"min":0.9})").object()).min == 0.5);
  CHECK(sanitizeCurve(curveToJson(curvePresets()[2].curve)) == curvePresets()[2].curve);

  // pressure curve
  CHECK(near(lin.apply(0), 0, 1e-3) && near(lin.apply(0.5), 0.5, 0.01) && near(lin.apply(1), 1, 1e-3));
  CHECK(soft.apply(0.3) > lin.apply(0.3) + 0.05 && firm.apply(0.3) < lin.apply(0.3) - 0.05);
  for (const PressureMap* m : {&soft, &firm, &lin, &dead}) {
    double prev = -1;
    bool mono = true;
    for (int i = 0; i <= 200; i++) { const double v = m->apply(i / 200.0); if (v < prev - 1e-9) mono = false; prev = v; }
    CHECK(mono);
  }
  CHECK(dead.apply(0.05) == 0 && near(dead.apply(1), 1, 1e-3) && dead.apply(0.2) > 0);
  const CurveLut lut = curveLut(kLinearCurve);
  CHECK(lut[0] == 0 && near(lut[255], 1, 1e-6));
  CHECK(lin.apply(-1) == 0 && near(lin.apply(5), 1, 1e-6));   // out-of-range pressure is clamped

  // tips
  const Run ink = run(BrushKind::Ink, ramp(100, 100, 300, 100, 50), lin);
  const double mid = ink.widths[25];
  CHECK(ink.widths[0] < mid * 0.6 && ink.fin.back() < mid * 0.5 && ink.widths.size() == ink.pts.size() / 2);
  CHECK(run(BrushKind::Ink, flat(40, 0.9), lin).widths[39] > run(BrushKind::Ink, flat(40, 0.2), lin).widths[39] * 1.5);
  {   // without pressure, speed thins the ink
    BrushEngine slow(BrushKind::Ink, 2, lin, false), fast(BrushKind::Ink, 2, lin, false);
    for (int i = 0; i < 30; i++) { slow.next(i * 0.4, 0, 0.5, i * 8); fast.next(i * 12, 0, 0.5, i * 8); }
    CHECK(slow.next(13, 0, 0.5, 240) > fast.next(400, 0, 0.5, 240) * 1.2);
  }
  const double fAlong = run(BrushKind::Fountain, along(-40), lin).widths[39], fAcross = run(BrushKind::Fountain, along(50), lin).widths[39];
  CHECK(fAcross > fAlong * 3 && fAlong > 0.05);
  CHECK(run(BrushKind::Brush, flat(40, 0.95), lin).widths[39] > run(BrushKind::Brush, flat(40, 0.15), lin).widths[39] * 3);
  {
    const Run pen = run(BrushKind::Pen, ramp(100, 100, 300, 100, 20), lin, false);
    CHECK(std::all_of(pen.widths.begin(), pen.widths.end(), [](double w) { return w == 2; }));
    CHECK(near(run(BrushKind::Pen, flat(10, 0.5), lin).widths[5], 1.5));
  }
  {   // the start of a stroke does not begin with a dot of another size (startFirstWidthMatches)
    BrushEngine eng(BrushKind::Pen, 1.41, lin, true);
    std::vector<double> p2, w2{eng.first()};
    for (int i = 0; i < 6; i++) { p2.push_back(i * 5); p2.push_back(0); if (i == 0) eng.next(0, 0, 0.05, 0); else w2.push_back(eng.next(i * 5, 0, 0.05, i * 8)); }
    const std::vector<double> fin = eng.finish(p2, w2);
    CHECK(w2[0] == 1.41 && fin[0] == fin[1] && fin[0] < 0.5);
  }
  CHECK(brushName(BrushKind::Pen) == QString::fromUtf8("Bolígrafo") && brushes().size() == 4);
  BrushKind k;
  CHECK(brushFromId("fountain", &k) && k == BrushKind::Fountain && !brushFromId("laser", &k) && brushId(BrushKind::Brush) == "brush");
  CHECK(demoStroke(200, 60).size() == 61);

  // finish() leaves its input alone and gives every width a floor
  {
    const std::vector<double> w = ink.widths;
    BrushEngine e(BrushKind::Ink, 2, lin, true);
    const auto out = e.finish(ink.pts, ink.widths);
    CHECK(ink.widths == w && out.size() == w.size());
    CHECK(*std::min_element(out.begin(), out.end()) >= 0.05);
  }

  for (BrushKind kind : {BrushKind::Ink, BrushKind::Fountain, BrushKind::Brush}) {
    const std::vector<Sample> letter = ramp(100, 100, 112, 108, 24);
    BrushEngine withP(kind, 1.41, lin, true);
    std::vector<double> pts, w;
    for (size_t i = 0; i < letter.size(); i++) {
      pts.push_back(letter[i].x); pts.push_back(letter[i].y);
      const double v = withP.next(letter[i].x, letter[i].y, 0.7, double(i) * 8);
      w.push_back(i == 0 ? withP.first() : v);
    }
    const auto fin = withP.finish(pts, w);
    bool same = true;
    for (size_t i = 1; i < w.size(); i++) same = same && fin[i] == w[i];
    CHECK(same);

    BrushEngine noP(kind, 1.41, lin, false);
    std::vector<double> w2;
    for (size_t i = 0; i < letter.size(); i++) {
      const double v = noP.next(letter[i].x, letter[i].y, 0.5, double(i) * 8);
      w2.push_back(i == 0 ? noP.first() : v);
    }
    const auto fin2 = noP.finish(pts, w2);
    size_t keep = 0;
    for (size_t i = 1; i < w2.size(); i++) if (std::hypot(pts[2 * i] - pts[0], pts[2 * i + 1] - pts[1]) < 0.7 * 14) keep = i;
    bool head = true;
    for (size_t i = 1; i <= keep && i < w2.size(); i++) head = head && fin2[i] == w2[i];
    CHECK(head);
    CHECK(fin2.back() < w2.back());
  }

  // pointer path: no allocation per point, and speed
  {
    BrushEngine eng(BrushKind::Fountain, 2, lin, true);
    const long long before = g_allocs;
    double sum = 0;
    const int N = 2000000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++) sum += eng.next(i * 0.7, std::sin(i * 0.01) * 50, 0.5 + 0.4 * std::sin(i * 0.003), i * 4.0);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    CHECK(g_allocs == before);
    CHECK(sum > 0);
    std::printf("BrushEngine::next: %.1f ns/point (fountain), allocations on the path: %lld\n", us * 1000 / N, g_allocs - before);
  }

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("brush: all passed\n");
  return 0;
}
