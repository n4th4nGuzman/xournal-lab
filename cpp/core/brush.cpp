#include "brush.h"
#include <QJsonObject>
#include <algorithm>
#include <cmath>

const std::vector<BrushInfo>& brushes() {
  static const std::vector<BrushInfo> b = {
    {BrushKind::Pen, "pen", QString::fromUtf8("Bolígrafo"), QString::fromUtf8("Grosor fijo (con lápiz óptico sigue la presión)")},
    {BrushKind::Ink, "ink", "Tinta", QString::fromUtf8("Entra y sale en punta; con presión engorda, y al ir rápido adelgaza")},
    {BrushKind::Fountain, "fountain", "Pluma", QString::fromUtf8("Punta de caligrafía: fino al trazar a lo largo de la punta y grueso en el sentido contrario")},
    {BrushKind::Brush, "brush", "Pincel", QString::fromUtf8("Muy sensible a la presión, con entrada y salida largas")},
  };
  return b;
}
QString brushId(BrushKind k) { return brushes()[size_t(k)].id; }
QString brushName(BrushKind k) { return brushes()[size_t(k)].name; }
bool brushFromId(const QString& id, BrushKind* out) {
  for (const BrushInfo& b : brushes())
    if (b.id == id) { if (out) *out = b.kind; return true; }
  return false;
}

const PressureCurve kLinearCurve{0.25, 0.25, 0.75, 0.75, 0};
const std::vector<CurvePreset>& curvePresets() {
  static const std::vector<CurvePreset> p = {
    {"Lineal", kLinearCurve},
    {QString::fromUtf8("Suave (más fácil engrosar)"), {0.15, 0.4, 0.5, 0.9, 0}},
    {QString::fromUtf8("Firme (hay que apretar más)"), {0.5, 0.1, 0.85, 0.6, 0.02}},
  };
  return p;
}

static double num(const QJsonObject& o, const char* key, double d, double lo, double hi) {
  const QJsonValue v = o.value(QLatin1String(key));
  return v.isDouble() && std::isfinite(v.toDouble()) ? std::min(hi, std::max(lo, v.toDouble())) : d;
}

PressureCurve sanitizeCurve(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  return {num(o, "x1", 0.25, 0, 1), num(o, "y1", 0.25, 0, 1), num(o, "x2", 0.75, 0, 1), num(o, "y2", 0.75, 0, 1), num(o, "min", 0, 0, 0.5)};
}

QJsonObject curveToJson(const PressureCurve& c) {
  QJsonObject o;
  o["x1"] = c.x1; o["y1"] = c.y1; o["x2"] = c.x2; o["y2"] = c.y2; o["min"] = c.min;
  return o;
}

CurveLut curveLut(const PressureCurve& c) {
  CurveLut lut{};
  auto bez = [](double t, double a, double b) { return 3 * (1 - t) * (1 - t) * t * a + 3 * (1 - t) * t * t * b + t * t * t; };
  // sample the curve densely by t, then resample by x (float tables as in the TypeScript, so both apps give the same values)
  constexpr int N = 512;
  std::array<float, N + 1> xs{}, ys{};
  float prev = 0;
  for (int i = 0; i <= N; i++) {
    const double t = double(i) / N;
    xs[i] = float(std::max(double(prev), bez(t, c.x1, c.x2)));
    prev = xs[i];
    ys[i] = float(bez(t, c.y1, c.y2));
  }
  int j = 0;
  double maxY = 0;
  for (int k = 0; k < kLutSize; k++) {
    const double x = double(k) / (kLutSize - 1);
    while (j < N && double(xs[j + 1]) < x) j++;
    const double x0 = xs[j], x1 = xs[std::min(N, j + 1)], y0 = ys[j], y1 = ys[std::min(N, j + 1)];
    const double y = x1 > x0 ? y0 + ((y1 - y0) * (x - x0)) / (x1 - x0) : y1;
    maxY = std::max(maxY, y);
    lut[k] = float(maxY);   // never decreases
  }
  return lut;
}

double PressureMap::apply(double p) const {
  const double min = curve_.min;
  const double x = min >= 1 ? 0 : std::min(1.0, std::max(0.0, (p - min) / (1 - min)));
  const double f = x * (kLutSize - 1);
  const int i = int(std::floor(f));
  const double k = f - i;
  return double(lut_[i]) + ((double(lut_[std::min(kLutSize - 1, i + 1)]) - double(lut_[i])) * k);
}

// ------------------------------------------------------------------------------------------------------------------------------- engine
static double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
static const double kPi = 3.14159265358979323846;
static const double kNibAngle = -kPi / 4.5;   // the pen's nib, 40 degrees above the horizontal as seen on the page (y points down)

double penWidth(PenLook look, double size, double p) {
  if (look == PenLook::Proto) return size * (0.3 + 0.7 * std::pow(std::min(1.0, std::max(0.0, p)), 0.8));
  return std::max(0.05, p * 1.5) * size;
}

BrushEngine::BrushEngine(BrushKind kind, double size, const PressureMap& map, bool pressure, PenLook look)
    : kind_(kind), size_(size), map_(&map), pressure_(pressure), look_(look), w_(0) {
  w_ = kind == BrushKind::Pen ? size : width(0.5, 0, false, 0, 0);
}

double BrushEngine::next(double x, double y, double p, double t) {
  double v = 0, theta = 0, d = 0;
  bool hasTheta = false;
  if (hasLast_) {
    d = std::hypot(x - lastX_, y - lastY_);
    const double dt = std::max(1.0, t - lastT_);
    v = d / dt;
    if (d > 0.05) { theta = std::atan2(y - lastY_, x - lastX_); hasTheta = true; }
  }
  hasLast_ = true; lastX_ = x; lastY_ = y; lastT_ = t;
  len_ += d;
  if (hasTheta) { angle_ = theta; hasAngle_ = true; }   // direction of the latest move
  w_ = width(pressure_ ? map_->apply(p) : 0.5, v, hasTheta || hasAngle_, hasTheta ? theta : angle_, len_);
  return w_;
}

double BrushEngine::width(double p, double v, bool hasTheta, double theta, double len) {
  const double S = size_;
  double target = 0, alpha = 0, taperLen = 0;
  switch (kind_) {
    case BrushKind::Pen:
      return pressure_ ? penWidth(look_, S, p) : S;
    case BrushKind::Ink: {
      const double pf = pressure_ ? 0.3 + 1.25 * p : 1;
      const double sf = pressure_ ? 1 - 0.25 * clamp(v / 3, 0, 1) : 1.15 - 0.5 * clamp(v / 2.5, 0, 1);
      target = S * pf * sf; alpha = 0.35; taperLen = 3.5 * S;
      break;
    }
    case BrushKind::Fountain: {
      const double across = !hasTheta ? 0.6 : std::fabs(std::sin(theta - kNibAngle));   // thin along the nib, thick across it
      target = S * 2.2 * (0.16 + 0.84 * across) * (pressure_ ? 0.7 + 0.6 * p : 1); alpha = 0.4; taperLen = 1.2 * S;
      break;
    }
    case BrushKind::Brush: {
      const double pf = pressure_ ? 0.12 + 2.3 * std::pow(p, 1.1) : 0.45 + 1.4 * (1 - clamp(v / 3, 0, 1));
      target = S * pf; alpha = 0.3; taperLen = 3 * S;
      break;
    }
  }
  smooth_ = smooth_ < 0 ? target : smooth_ + (target - smooth_) * alpha;
  const double entry = len >= taperLen ? 1 : 0.35 + 0.65 * (len / taperLen);   // the stroke starts in a point
  return std::max(0.05, smooth_ * entry);
}

std::vector<double> BrushEngine::finish(const std::vector<double>& pts, const std::vector<double>& widths) const {
  std::vector<double> out = widths;
  if (out.size() > 1) out[0] = out[1];   // the first point is as wide as the first segment: the stroke does not begin with a dot of another size
  if (pressure_) return out;
  double tail = kind_ == BrushKind::Ink ? 5 * size_ : kind_ == BrushKind::Brush ? 10 * size_ : kind_ == BrushKind::Fountain ? 1.5 * size_ : 0;
  const size_t n = pts.size() >> 1;
  if (tail == 0 || n < 3 || widths.size() < n) return out;
  double total = 0;
  for (size_t i = 1; i < n; i++) total += std::hypot(pts[2 * i] - pts[2 * i - 2], pts[2 * i + 1] - pts[2 * i - 1]);
  tail = std::min(tail, 0.25 * total);
  if (tail <= 0) return out;
  double acc = 0;
  for (size_t i = n - 1; i >= 1; i--) {
    if (i < n - 1) acc += std::hypot(pts[2 * (i + 1)] - pts[2 * i], pts[2 * (i + 1) + 1] - pts[2 * i + 1]);
    if (acc >= tail) break;
    const double k = 0.2 + 0.8 * (acc / tail);
    out[i] = std::max(0.05, out[i] * k);
  }
  return out;
}

std::vector<DemoSample> demoStroke(double w, double h) {
  std::vector<DemoSample> out;
  const int N = 60;
  for (int i = 0; i <= N; i++) {
    const double u = double(i) / N;
    out.push_back({8 + u * (w - 16), h / 2 + std::sin(u * kPi * 2) * h * 0.28, std::sin(u * kPi) * 0.9 + 0.05, i * 8.0});
  }
  return out;
}
