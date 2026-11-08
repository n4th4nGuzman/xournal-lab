#include "paper.h"
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr double MM = 72.0 / 25.4;
constexpr double CM = MM * 10;
constexpr double PI = 3.14159265358979323846;

struct Ink {
  QColor rgb;      // grid lines (alpha is set per use)
  QColor strong;   // axes and labels
  bool dark;
};

QColor alphaOf(const Ink& ink, double a) {
  QColor c = ink.rgb;
  c.setAlphaF(std::clamp(a, 0.0, 1.0));
  return c;
}

using Params = std::map<QString, QString>;
using Move = const std::function<void(double, double)>&;
using Build = const std::function<void(Move, Move)>&;

double num(const Params& p, const char* key) {
  auto it = p.find(key);
  return it == p.end() ? 0.0 : it->second.toDouble();
}
QString str(const Params& p, const char* key) {
  auto it = p.find(key);
  return it == p.end() ? QString() : it->second;
}

constexpr double kMiddleEm = 0.27;   // where Chromium puts the "middle" baseline of the page font, measured against the Electron renderer
double jsRound(double v) { return std::floor(v + 0.5); }   // JS Math.round: halves go up, also for negatives

/** Strokes a batch of segments as one path (one draw call, like the canvas version). */
void lines(QPainter& p, const QColor& color, double width, Build build) {
  QPainterPath path;
  build([&](double x, double y) { path.moveTo(x, y); }, [&](double x, double y) { path.lineTo(x, y); });
  QPen pen(color);
  pen.setWidthF(width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);
}

enum class Align { Left, Center, Right };

/** Label in the page's system font. The font is laid out at 100 px and scaled so that fractional sizes are exact at any zoom. */
void label(QPainter& p, const QString& s, double x, double y, const Ink& ink, Align align = Align::Center, double size = 7.5) {
  static const QFont font = [] {
    QFont f;
    f.setFamilies({"Segoe UI", "Arial", "Sans Serif"});
    f.setPixelSize(100);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
  }();
  static const QFontMetricsF fm(font);
  const double k = size / 100.0;
  double dx = 0;
  if (align != Align::Left) {
    const double w = fm.horizontalAdvance(s);
    dx = align == Align::Center ? -w / 2 : -w;
  }
  p.save();
  p.translate(x, y);
  p.scale(k, k);
  p.setFont(font);
  p.setPen(ink.strong);
  // "middle" baseline of a canvas: the centre of the em box sits on y.
  p.drawText(QPointF(dx, kMiddleEm * 100), s);
  p.restore();
}

void arrow(QPainter& p, double x, double y, double dx, double dy, const Ink& ink) {
  const double s = 5;
  QPolygonF tri;
  tri << QPointF(x, y) << QPointF(x - dx * s * 1.6 + dy * s * 0.6, y - dy * s * 1.6 - dx * s * 0.6)
      << QPointF(x - dx * s * 1.6 - dy * s * 0.6, y - dy * s * 1.6 + dx * s * 0.6);
  p.setPen(Qt::NoPen);
  p.setBrush(ink.strong);
  p.drawPolygon(tri);
}

QString trimmed(double v) {   // +n.toFixed(2) as text
  QString s = QString::number(v, 'f', 2);
  if (s.contains('.')) {
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
  }
  if (s == "-0") s = "0";
  return s;
}
QString minus(double n) { return (n < 0 ? QString(QChar(0x2212)) : QString()) + trimmed(std::fabs(n)); }
QString sup(int n) {
  static const QChar digits[] = {QChar(0x2070), QChar(0x00B9), QChar(0x00B2), QChar(0x00B3), QChar(0x2074),
                                 QChar(0x2075), QChar(0x2076), QChar(0x2077), QChar(0x2078), QChar(0x2079)};
  QString out;
  for (QChar c : QString::number(std::abs(n))) out += digits[c.digitValue()];
  return out;
}

// ---------------------------------------------------------------- millimetre
void drawMm(QPainter& p, double w, double h, const Params&, const Ink& ink) {
  const int nx = int(std::floor(w / MM)), ny = int(std::floor(h / MM));
  const double ox = (w - nx * MM) / 2, oy = (h - ny * MM) / 2;
  auto tier = [](int i) { return i % 10 == 0 ? 2 : i % 5 == 0 ? 1 : 0; };
  const double spec[3][3] = {{0, 0.2, 0.22}, {1, 0.35, 0.45}, {2, 0.6, 0.8}};
  for (auto& sp : spec) {
    const int t = int(sp[0]);
    lines(p, alphaOf(ink, sp[2]), sp[1], [&](Move m, Move l) {
      for (int i = 0; i <= nx; i++) if (tier(i) == t) { m(ox + i * MM, oy); l(ox + i * MM, oy + ny * MM); }
      for (int j = 0; j <= ny; j++) if (tier(j) == t) { m(ox, oy + j * MM); l(ox + nx * MM, oy + j * MM); }
    });
  }
}

// ---------------------------------------------------------------- Cartesian plane
void drawCartesian(QPainter& p, double w, double h, const Params& prm, const Ink& ink) {
  const double u = num(prm, "unit") * CM;
  const bool sub = str(prm, "sub") == "on";
  const bool labels = str(prm, "labels") != "off";
  const bool one = str(prm, "quad") == "1";
  const double x0 = one ? jsRound(44 / u) * u : jsRound(w / 2 / u) * u;
  const double y0 = one ? std::floor((h - 44) / u) * u : jsRound(h / 2 / u) * u;
  const double left = one ? x0 : 0, right = w, top = 0, bottom = one ? y0 : h;
  const int kx0 = int(std::ceil((left - x0) / u - 1e-6)), kx1 = int(std::floor((right - x0) / u + 1e-6));
  const int ky0 = int(std::ceil((top - y0) / u - 1e-6)), ky1 = int(std::floor((bottom - y0) / u + 1e-6));
  if (sub) {
    const double f = u / 5;
    lines(p, alphaOf(ink, 0.16), 0.3, [&](Move m, Move l) {
      for (int i = int(std::ceil((left - x0) / f)); i <= int(std::floor((right - x0) / f)); i++) if (i % 5 != 0) { m(x0 + i * f, top); l(x0 + i * f, bottom); }
      for (int j = int(std::ceil((top - y0) / f)); j <= int(std::floor((bottom - y0) / f)); j++) if (j % 5 != 0) { m(left, y0 + j * f); l(right, y0 + j * f); }
    });
  }
  lines(p, alphaOf(ink, 0.42), 0.5, [&](Move m, Move l) {
    for (int k = kx0; k <= kx1; k++) if (k != 0) { m(x0 + k * u, top); l(x0 + k * u, bottom); }
    for (int k = ky0; k <= ky1; k++) if (k != 0) { m(left, y0 + k * u); l(right, y0 + k * u); }
  });
  lines(p, ink.strong, 1.2, [&](Move m, Move l) { m(left, y0); l(right - 8, y0); m(x0, bottom); l(x0, top + 8); });
  arrow(p, right - 2, y0, 1, 0, ink);
  arrow(p, x0, top + 2, 0, -1, ink);
  label(p, "x", right - 10, y0 + 12, ink, Align::Center, 9);
  label(p, "y", x0 + 11, top + 12, ink, Align::Center, 9);
  if (labels) {
    const int every = u < 0.8 * CM ? 2 : 1;
    for (int k = kx0; k <= kx1; k++) if (k != 0 && k % every == 0 && x0 + k * u < right - 14) label(p, minus(k), x0 + k * u, y0 + 9, ink);
    for (int k = ky0; k <= ky1; k++) if (k != 0 && k % every == 0 && y0 + k * u > top + 14) label(p, minus(-k), x0 - 8, y0 + k * u, ink, Align::Right);
    label(p, "O", x0 - 7, y0 + 9, ink, Align::Right);
  }
}

// ---------------------------------------------------------------- trigonometric
void drawTrig(QPainter& p, double w, double h, const Params& prm, const Ink& ink) {
  const double m = 26;
  const double sx = (w - 2 * m) / (4 * PI);   // points per radian: the page shows -2pi..2pi
  const double sy = 2 * sx;                   // points per unit of y
  const double x0 = w / 2, y0 = jsRound(h / 2 / (sy / 4)) * (sy / 4);
  const int div = int(num(prm, "grid"));
  const double step = PI / div;
  const double ymax = std::floor((y0 - 20) / sy * 4) / 4, ymin = std::floor((h - y0 - 20) / sy * 4) / 4;
  lines(p, alphaOf(ink, 0.16), 0.3, [&](Move mv, Move l) {
    for (int i = -2 * div; i <= 2 * div; i++) if (i != 0) { mv(x0 + i * step * sx, y0 - ymax * sy); l(x0 + i * step * sx, y0 + ymin * sy); }
    for (int j = int(-ymin * 4); j <= int(ymax * 4); j++) if (j != 0 && j % 4 != 0) { mv(m, y0 - (j / 4.0) * sy); l(w - m, y0 - (j / 4.0) * sy); }
  });
  lines(p, alphaOf(ink, 0.5), 0.5, [&](Move mv, Move l) {
    for (int i = -4; i <= 4; i++) if (i != 0) { mv(x0 + i * (PI / 2) * sx, y0 - ymax * sy); l(x0 + i * (PI / 2) * sx, y0 + ymin * sy); }
    for (int j = -int(std::floor(ymin)); j <= int(std::floor(ymax)); j++) if (j != 0) { mv(m, y0 - j * sy); l(w - m, y0 - j * sy); }
  });
  lines(p, ink.strong, 1.2, [&](Move mv, Move l) { mv(m - 6, y0); l(w - m, y0); mv(x0, y0 + ymin * sy); l(x0, y0 - ymax * sy - 4); });
  arrow(p, w - m + 6, y0, 1, 0, ink);
  arrow(p, x0, y0 - ymax * sy - 8, 0, -1, ink);
  const QString lab[] = {"", QString::fromUtf8("\xCF\x80/2"), QString::fromUtf8("\xCF\x80"), QString::fromUtf8("3\xCF\x80/2"), QString::fromUtf8("2\xCF\x80")};
  for (int i = -4; i <= 4; i++) if (i != 0) label(p, (i < 0 ? QString(QChar(0x2212)) : QString()) + lab[std::abs(i)], x0 + i * (PI / 2) * sx, y0 + 9, ink);
  for (int j = -int(std::floor(ymin)); j <= int(std::floor(ymax)); j++) if (j != 0) label(p, minus(j), x0 - 6, y0 - j * sy, ink, Align::Right);
  label(p, "x", w - m + 4, y0 + 12, ink, Align::Center, 9);
  label(p, "y", x0 + 10, y0 - ymax * sy - 10, ink, Align::Center, 9);
}

// ---------------------------------------------------------------- polar
void drawPolar(QPainter& p, double w, double h, const Params& prm, const Ink& ink) {
  const double cx = w / 2, cy = h / 2;
  const double ring = num(prm, "ring") * CM;
  const double rmax = std::floor((std::min(w, h) / 2 - 26) / ring) * ring;
  const int step = std::max(1, int(num(prm, "step")));
  const int rings = int(jsRound(rmax / ring));
  auto circle = [&](Move mv, Move l, double r, int n) {
    mv(cx + r, cy);
    for (int a = 1; a <= n; a++) l(cx + std::cos(double(a) / n * 2 * PI) * r, cy - std::sin(double(a) / n * 2 * PI) * r);
  };
  lines(p, alphaOf(ink, 0.35), 0.45, [&](Move mv, Move l) { for (int k = 1; k <= rings; k++) if (k % 5 != 0) circle(mv, l, k * ring, 120); });
  lines(p, alphaOf(ink, 0.7), 0.7, [&](Move mv, Move l) {
    for (int k = 5; k <= rings; k += 5) circle(mv, l, k * ring, 160);
    circle(mv, l, rmax, 160);
  });
  for (int major = 0; major < 2; major++) {
    lines(p, alphaOf(ink, major ? 0.6 : 0.3), major ? 0.7 : 0.4, [&](Move mv, Move l) {
      for (int d = 0; d < 360; d += step) if ((d % 30 == 0) == (major == 1)) { const double r = d * PI / 180; mv(cx, cy); l(cx + std::cos(r) * rmax, cy - std::sin(r) * rmax); }
    });
  }
  for (int d = 0; d < 360; d += 30) {
    const double r = d * PI / 180;
    label(p, QString::number(d) + QChar(0x00B0), cx + std::cos(r) * (rmax + 14), cy - std::sin(r) * (rmax + 14), ink);
  }
  for (int k = 1; k <= rings; k++)
    if (k % (ring < 0.8 * CM ? 2 : 1) == 0) label(p, trimmed(k * num(prm, "ring")), cx + k * ring + 1, cy + 7, ink, Align::Left, 6.5);
}

// ---------------------------------------------------------------- isometric
void drawIso(QPainter& p, double w, double h, const Params& prm, const Ink& ink) {
  const double s = num(prm, "size") * CM;   // edge of the triangles
  const double dx = s * std::sqrt(3.0) / 2;
  const double t = std::tan(PI / 6);
  const int n = int(std::floor(w / dx));
  const double ox = (w - n * dx) / 2;
  p.save();
  p.translate(ox, 0);
  p.setClipRect(QRectF(-ox, 0, w, h), Qt::IntersectClip);
  if (str(prm, "look") == "dots") {
    p.setPen(Qt::NoPen);
    p.setBrush(alphaOf(ink, 0.6));
    for (int i = 0; i <= n; i++)
      for (double y = i % 2 ? s / 2 : 0; y <= h; y += s) p.drawRect(QRectF(i * dx - 0.6, y - 0.6, 1.2, 1.2));
  } else {
    lines(p, alphaOf(ink, 0.4), 0.45, [&](Move mv, Move l) {
      for (int i = 0; i <= n; i++) { mv(i * dx, 0); l(i * dx, h); }
      const double span = n * dx;
      for (int j = int(std::floor(-span * t / s)) - 1; j <= int(std::ceil(h / s)) + 1; j++) {
        mv(0, j * s); l(span, j * s + span * t);
        mv(0, j * s); l(span, j * s - span * t);
      }
    });
  }
  p.restore();
}

// ---------------------------------------------------------------- Cornell notes
void drawCornell(QPainter& p, double w, double h, const Params&, const Ink& ink) {
  const double top = 74, cue = 150, sumY = h - 150;
  lines(p, alphaOf(ink, 0.3), 0.45, [&](Move m, Move l) {
    for (double y = top + 26; y < sumY; y += 26) { m(cue, y); l(w - 24, y); }
    for (double y = sumY + 26; y < h - 30; y += 26) { m(24, y); l(w - 24, y); }
  });
  lines(p, alphaOf(ink, 0.8), 1, [&](Move m, Move l) { m(24, top); l(w - 24, top); m(cue, top); l(cue, sumY); m(24, sumY); l(w - 24, sumY); });
  label(p, "Tema:", 26, 30, ink, Align::Left, 9);
  label(p, "Fecha:", w - 130, 30, ink, Align::Left, 9);
  lines(p, alphaOf(ink, 0.5), 0.5, [&](Move m, Move l) { m(60, 38); l(w - 140, 38); m(w - 96, 38); l(w - 26, 38); });
  label(p, "Preguntas / ideas clave", 28, top + 10, ink, Align::Left, 7.5);
  label(p, "Apuntes", cue + 6, top + 10, ink, Align::Left, 7.5);
  label(p, "Resumen", 28, sumY + 10, ink, Align::Left, 7.5);
}

// ---------------------------------------------------------------- logarithmic
void drawLog(QPainter& p, double w, double h, const Params& prm, const Ink& ink) {
  const bool natural = str(prm, "base") == "e";
  const bool both = str(prm, "axes") == "xy";
  const int cycles = std::max(1, int(num(prm, "cycles")));
  const double L = 48, R = w - 26, T = 34, B = h - 42;
  const double W = R - L, H = B - T;
  const int xcycles = std::max(1, int(jsRound(cycles * W / H)));
  std::vector<std::pair<double, int>> inner;   // position 0..1 inside a cycle, strength
  if (natural) for (int j = 1; j < 10; j++) inner.push_back({j / 10.0, j == 5 ? 1 : 0});
  else for (int v = 2; v <= 9; v++) inner.push_back({std::log10(double(v)), v == 5 ? 1 : 0});
  auto lab = [&](int c) -> QString {
    if (natural) return c == 0 ? "1" : c == 1 ? "e" : "e" + sup(c);
    return c <= 3 ? QString::number(int(std::pow(10, c))) : "10" + sup(c);
  };
  const double spec[2][3] = {{0, 0.3, 0.3}, {1, 0.5, 0.55}};
  for (auto& sp : spec) {
    const int strength = int(sp[0]);
    lines(p, alphaOf(ink, sp[2]), sp[1], [&](Move m, Move l) {
      for (int c = 0; c < cycles; c++) for (auto& [f, s] : inner) if (s == strength) { const double y = B - (c + f) / cycles * H; m(L, y); l(R, y); }
      if (both) { for (int c = 0; c < xcycles; c++) for (auto& [f, s] : inner) if (s == strength) { const double x = L + (c + f) / xcycles * W; m(x, T); l(x, B); } }
      else for (int i = 1; i < 50; i++) if ((i % 5 == 0) == (strength == 1)) { const double x = L + double(i) / 50 * W; m(x, T); l(x, B); }
    });
  }
  lines(p, alphaOf(ink, 0.9), 0.9, [&](Move m, Move l) {
    for (int c = 0; c <= cycles; c++) { const double y = B - double(c) / cycles * H; m(L, y); l(R, y); }
    if (both) { for (int c = 0; c <= xcycles; c++) { const double x = L + double(c) / xcycles * W; m(x, T); l(x, B); } }
    else for (int i = 0; i <= 10; i++) { const double x = L + i / 10.0 * W; m(x, T); l(x, B); }
  });
  QPen pen(ink.strong);
  pen.setWidthF(1.2);
  pen.setJoinStyle(Qt::MiterJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(L, T, W, H));
  for (int c = 0; c <= cycles; c++) label(p, lab(c), L - 5, B - double(c) / cycles * H, ink, Align::Right, 8);
  if (both) { for (int c = 0; c <= xcycles; c++) label(p, lab(c), L + double(c) / xcycles * W, B + 10, ink, Align::Center, 8); }
  else for (int i = 0; i <= 10; i++) label(p, QString::number(i), L + i / 10.0 * W, B + 10, ink, Align::Center, 8);
  label(p, natural ? QString::fromUtf8("escala logar\xC3\xADtmica natural (ciclo = factor e)") : QString::fromUtf8("escala logar\xC3\xADtmica decimal (ciclo = d\xC3\xA9""cada)"),
        L, T - 12, ink, Align::Left, 7.5);
}

using DrawFn = void (*)(QPainter&, double, double, const Params&, const Ink&);
struct Entry { PaperKind kind; DrawFn draw; };

const std::vector<Entry>& catalogue() {
  static const std::vector<Entry> c = [] {
    auto u = [](const char* s) { return QString::fromUtf8(s); };
    std::vector<Entry> v;
    v.push_back({{"mm", u("Milimetrado"), "graph", {}}, drawMm});
    v.push_back({{"cart", u("Plano cartesiano"), "graph",
                  {{"quad", u("Ejes"), {{"4", u("4 cuadrantes")}, {"1", u("1.er cuadrante")}}},
                   {"unit", u("Unidad"), {{"1", u("1 cm")}, {"0.5", u("0,5 cm")}, {"2", u("2 cm")}}},
                   {"sub", u("Subdivisi\xC3\xB3n"), {{"off", u("no")}, {"on", u("5 por unidad")}}},
                   {"labels", u("N\xC3\xBAmeros"), {{"on", u("s\xC3\xAD")}, {"off", u("no")}}}}},
                 drawCartesian});
    v.push_back({{"trig", u("Trigonom\xC3\xA9trico (eje en \xCF\x80)"), "graph",
                  {{"grid", u("Rejilla"), {{"6", u("cada \xCF\x80/6 (30\xC2\xB0)")}, {"4", u("cada \xCF\x80/4 (45\xC2\xB0)")}}}}},
                 drawTrig});
    v.push_back({{"polar", u("Polar"), "plain",
                  {{"step", u("\xC3\x81ngulo"), {{"15", u("cada 15\xC2\xB0")}, {"30", u("cada 30\xC2\xB0")}, {"10", u("cada 10\xC2\xB0")}}},
                   {"ring", u("Anillos"), {{"1", u("cada 1 cm")}, {"0.5", u("cada 0,5 cm")}}}}},
                 drawPolar});
    v.push_back({{"log", u("Logar\xC3\xADtmico"), "graph",
                  {{"base", u("Base"), {{"10", u("10 (decimal)")}, {"e", u("e (natural)")}}},
                   {"axes", u("Tipo"), {{"y", u("Semilog (y log)")}, {"xy", u("Log-log")}}},
                   {"cycles", u("Ciclos"), {{"5", "5"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"6", "6"}, {"7", "7"}}}}},
                 drawLog});
    v.push_back({{"iso", u("Isom\xC3\xA9trico"), "dotted",
                  {{"look", u("Aspecto"), {{"lines", u("l\xC3\xADneas")}, {"dots", u("puntos")}}},
                   {"size", u("Lado"), {{"1", u("1 cm")}, {"0.5", u("0,5 cm")}, {"2", u("2 cm")}}}}},
                 drawIso});
    v.push_back({{"cornell", u("Cornell (apuntes)"), "ruled", {}}, drawCornell});
    return v;
  }();
  return c;
}

const Entry* entryOf(const QString& id) {
  for (const Entry& e : catalogue()) if (e.kind.id == id) return &e;
  return nullptr;
}

Ink inkFor(const QColor& bg) {
  const double lum = (0.2126 * bg.red() + 0.7152 * bg.green() + 0.0722 * bg.blue()) / 255;
  if (lum < 0.5) return {QColor(170, 190, 255), QColor::fromRgbF(235 / 255.0, 235 / 255.0, 1.0, 0.92), true};
  return {QColor(60, 100, 160), QColor::fromRgbF(25 / 255.0, 25 / 255.0, 40 / 255.0, 0.9), false};
}

}  // namespace

const std::vector<PaperKind>& paperKinds() {
  static const std::vector<PaperKind> v = [] {
    std::vector<PaperKind> out;
    for (const Entry& e : catalogue()) out.push_back(e.kind);
    return out;
  }();
  return v;
}

const PaperKind* paperKind(const QString& id) {
  const Entry* e = entryOf(id);
  return e ? &e->kind : nullptr;
}

std::optional<PaperSpec> defaultSpec(const QString& kind) {
  const PaperKind* k = paperKind(kind);
  if (!k) return std::nullopt;
  PaperSpec s;
  s.kind = kind;
  for (const PaperParam& p : k->params) s.params[p.key] = p.options.front().first;
  return s;
}

std::optional<PaperSpec> parseSpec(const QString& raw) {
  if (raw.isEmpty()) return std::nullopt;
  const QStringList parts = raw.split(';');
  auto spec = defaultSpec(parts.front().trimmed());
  if (!spec) return std::nullopt;
  const PaperKind* k = paperKind(spec->kind);
  for (int i = 1; i < parts.size(); i++) {
    const int eq = parts[i].indexOf('=');
    if (eq < 0) continue;
    const QString key = parts[i].left(eq).trimmed(), value = parts[i].mid(eq + 1).trimmed();
    for (const PaperParam& p : k->params) {
      if (p.key != key) continue;
      for (const auto& o : p.options) if (o.first == value) spec->params[key] = value;
    }
  }
  return spec;
}

QString formatSpec(const PaperSpec& s) {
  QStringList out{s.kind};
  if (const PaperKind* k = paperKind(s.kind))
    for (const PaperParam& p : k->params) {
      auto it = s.params.find(p.key);
      out << p.key + "=" + (it != s.params.end() ? it->second : p.options.front().first);
    }
  return out.join(';');
}

PaperSpec withParam(const PaperSpec& s, const QString& key, const QString& value) {
  PaperSpec r = s;
  r.params[key] = value;
  return r;
}

std::optional<PaperSpec> paperOfBackground(const Attrs& background) {
  const QString* raw = attrFind(background, "xlPaper");
  return raw ? parseSpec(*raw) : std::nullopt;
}

void drawPaper(QPainter& p, double w, double h, const QColor& bg, const PaperSpec& spec) {
  const Entry* e = entryOf(spec.kind);
  if (!e) return;
  p.save();
  e->draw(p, w, h, spec.params, inkFor(bg));
  p.restore();
}

double builtinSpacing(const QString& style) {
  if (style == "lined" || style == "ruled") return 24;
  if (style == "graph" || style == "dotted" || style == "isograph" || style == "isodotted") return 14.17;
  return 0;
}

double paperSpacingOf(const Attrs& background) {
  const QString* raw = attrFind(background, "xlSpacing");
  bool ok = false;
  const double v = raw ? raw->toDouble(&ok) : 0;
  return ok && std::isfinite(v) && v >= kMinSpacing && v <= kMaxSpacing ? v : 0;
}

double paperOpacityOf(const Attrs& background) {
  const QString* raw = attrFind(background, "xlOpacity");
  bool ok = false;
  const double v = raw ? raw->toDouble(&ok) : 1;
  return ok && std::isfinite(v) && v >= kMinPaperOpacity && v <= 1 ? v : 1;
}

static void setOrDrop(Attrs& a, const char* key, bool keep, const QString& value) {
  a.erase(std::remove_if(a.begin(), a.end(), [key](const auto& kv) { return kv.first == key; }), a.end());
  if (keep) attrSet(a, key, value);
}
void setPaperSpacing(Attrs& background, double points) { setOrDrop(background, "xlSpacing", points >= kMinSpacing, QString::number(std::min(points, kMaxSpacing), 'f', 2)); }
void setPaperOpacity(Attrs& background, double opacity) { setOrDrop(background, "xlOpacity", opacity < 0.999, QString::number(std::max(opacity, kMinPaperOpacity), 'f', 2)); }

void drawBuiltinStyle(QPainter& p, double w, double h, const QString& style, double spacing) {
  // Geometry of the original renderer: approximate spacing, unknown styles stay plain.
  const QColor line(150, 150, 150);
  const double GRID = spacing > 0 && builtinSpacing(style) == 14.17 ? spacing : 14.17;
  const double RULE = spacing > 0 && builtinSpacing(style) == 24 ? spacing : 24;
  p.save();
  if (style == "lined" || style == "ruled") {
    lines(p, line, 0.5, [&](Move m, Move l) { for (double y = 80; y < h; y += RULE) { m(0, y); l(w, y); } });
    if (style == "lined") lines(p, QColor(220, 80, 80), 0.5, [&](Move m, Move l) { m(72, 0); l(72, h); });   // only "lined" has the red margin
  } else if (style == "graph") {
    lines(p, line, 0.5, [&](Move m, Move l) {
      for (double x = 0; x <= w; x += GRID) { m(x, 0); l(x, h); }
      for (double y = 0; y <= h; y += GRID) { m(0, y); l(w, y); }
    });
  } else if (style == "dotted") {
    p.setPen(Qt::NoPen);
    p.setBrush(line);
    for (double y = GRID; y < h; y += GRID)
      for (double x = GRID; x < w; x += GRID) p.drawRect(QRectF(x - 0.5, y - 0.5, 1, 1));
  } else if (style == "isograph" || style == "isodotted") {
    // Xournal++'s isometric papers (a triangular lattice): vertical lines every GRID*cos30, the other two families at +-30 degrees, parallel ones GRID apart
    // vertically, so every crossing sits on a vertical line at half-step offsets. The original's exact spacing is not documented: approximate, like the others.
    const double dx = GRID * 0.8660254;
    if (style == "isograph") {
      lines(p, line, 0.5, [&](Move m, Move l) {
        for (double x = 0; x <= w; x += dx) { m(x, 0); l(x, h); }
        const double slope = 0.5773503;   // tan 30 degrees
        for (double c = -slope * w; c <= h; c += GRID) { m(0, c); l(w, c + slope * w); }
        for (double c = 0; c <= h + slope * w; c += GRID) { m(0, c); l(w, c - slope * w); }
      });
    } else {
      p.setPen(Qt::NoPen);
      p.setBrush(line);
      int k = 0;
      for (double x = dx; x < w; x += dx, k++)
        for (double y = GRID * ((k % 2) ? 1.5 : 1.0); y < h; y += GRID) p.drawRect(QRectF(x - 0.5, y - 0.5, 1, 1));
    }
  }
  p.restore();
}
