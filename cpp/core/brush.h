// Pen tips. Port of app/src/renderer/brush.ts. A tip decides how wide each part of a stroke is (from pressure, speed and direction); the result is
// the stroke's ordinary per-segment width list, so the file format and the renderer do not change. Everything here is pure and, on the pointer
// path (BrushEngine::next, PressureMap::apply), allocation-free.
#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <array>
#include <vector>

enum class BrushKind { Pen, Ink, Fountain, Brush };
/** How the plain pen (BrushKind::Pen) turns pressure into width. Proto = the look of the prototype the user tried with a real pen:
 *  size * (0.3 + 0.7 * p^0.8), never thinner than 30 % of the size. Electron = max(0.05, 1.5 * p) * size, as the Electron app computes it. */
enum class PenLook { Proto, Electron };
/** Width of the plain pen at (mapped) pressure p with the given look. */
double penWidth(PenLook look, double size, double p);
struct BrushInfo { BrushKind kind; QString id, name, hint; };   // id is the saved name: "pen", "ink", "fountain", "brush"
const std::vector<BrushInfo>& brushes();
QString brushId(BrushKind k);
bool brushFromId(const QString& id, BrushKind* out);   // false for anything else
QString brushName(BrushKind k);

/** A pressure curve: a cubic Bezier from (0,0) to (1,1) with two control points, plus a dead zone below `min`. */
struct PressureCurve {
  double x1 = 0.25, y1 = 0.25, x2 = 0.75, y2 = 0.75, min = 0;
  bool operator==(const PressureCurve&) const = default;
};
extern const PressureCurve kLinearCurve;
struct CurvePreset { QString name; PressureCurve curve; };
const std::vector<CurvePreset>& curvePresets();

/** Values outside their range are clamped, missing or non-numeric ones take the defaults; a non-object gives the linear curve. */
PressureCurve sanitizeCurve(const QJsonValue& raw);
QJsonObject curveToJson(const PressureCurve& c);

constexpr int kLutSize = 256;
using CurveLut = std::array<float, kLutSize>;
/** The curve as a table of 256 values (pressure in, pressure out). Never decreases. */
CurveLut curveLut(const PressureCurve& c);

class PressureMap {
public:
  explicit PressureMap(const PressureCurve& c = kLinearCurve) : curve_(c), lut_(curveLut(c)) {}
  const PressureCurve& curve() const { return curve_; }
  /** Raw pressure (0..1) to the pressure the tip sees (0..1). */
  double apply(double p) const;
private:
  PressureCurve curve_;
  CurveLut lut_;
};

/**
 * Computes the widths of one stroke as its points arrive. first() is the width of the first point (a dot is drawn with it) and next() the width of
 * the segment that ends at each following point, exactly as strokes already store them. The map is referenced, not copied: it must outlive the engine.
 */
class BrushEngine {
public:
  BrushEngine(BrushKind kind, double size, const PressureMap& map, bool pressure, PenLook look = PenLook::Electron);
  double first() const { return w_; }
  /** Width of the segment that ends at (x, y), sampled at time t (ms) with raw pressure p. */
  double next(double x, double y, double p, double t);
  /** Close a finished stroke: the last part of the tips that taper runs out to a point. Returns new widths (the input is not changed). */
  std::vector<double> finish(const std::vector<double>& pts, const std::vector<double>& widths) const;
  BrushKind kind() const { return kind_; }
  double size() const { return size_; }

private:
  double width(double p, double v, bool hasTheta, double theta, double len);
  BrushKind kind_;
  double size_;
  const PressureMap* map_;
  bool pressure_;
  PenLook look_;
  bool hasLast_ = false;
  double lastX_ = 0, lastY_ = 0, lastT_ = 0;
  double len_ = 0;
  double w_;
  bool hasAngle_ = false;
  double angle_ = 0;
  double smooth_ = -1;   // width the tip would have without the entry taper, smoothed
};

/** A made-up stroke through a sine wave to show what a tip looks like: x, y, pressure, ms per sample. */
struct DemoSample { double x, y, p, t; };
std::vector<DemoSample> demoStroke(double w, double h);
