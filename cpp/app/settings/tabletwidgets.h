// The three custom widgets of the tablet dialog: a card that shows what a pen tip looks like, the pressure curve editor and the test sheet.
#pragma once
#include "brush.h"
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

/** Draw a stroke given as x,y pairs and one width per point (the width of the segment that ends there), like the Electron dialog does. */
void drawStrokeWidths(QPainter& p, const std::vector<double>& pts, const std::vector<double>& widths, double scale, const QColor& color);

/** A button with a drawn sample stroke of one tip and its name. */
class BrushCard : public QAbstractButton {
  Q_OBJECT
 public:
  BrushCard(const BrushInfo& info, QWidget* parent);
  void setLook(double size, const PressureCurve& curve) { size_ = size; curve_ = curve; update(); }
  BrushKind kind() const { return info_.kind; }
  QSize sizeHint() const override { return QSize(160, 92); }

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  BrushInfo info_;
  double size_ = 1.41;
  PressureCurve curve_ = kLinearCurve;
};

/** The curve as a plot with two draggable handles (big enough for a finger), the curve's dead zone and the last pressures seen. */
class CurveWidget : public QWidget {
  Q_OBJECT
 public:
  explicit CurveWidget(QWidget* parent);
  void setCurve(const PressureCurve& c) { curve_ = c; update(); }
  const PressureCurve& curve() const { return curve_; }
  void setRecent(const std::vector<std::pair<double, double>>& r) { recent_ = r; update(); }
  QSize sizeHint() const override { return QSize(300, 300); }
  /** Plot coordinates (0..1, y up) of a point of the widget and back; used by the handles and the self test. */
  QPointF toPlot(const QPointF& px) const;
  QPointF toPixel(double x, double y) const;
  std::function<void(const PressureCurve&)> changed;

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent*) override { drag_ = 0; }

 private:
  void moveHandle(const QPointF& px);
  PressureCurve curve_ = kLinearCurve;
  std::vector<std::pair<double, double>> recent_;
  int drag_ = 0;
};

/** A sheet to try the tip on: pen (with pressure) or mouse, drawn with the chosen tip and curve. */
class PadWidget : public QWidget {
  Q_OBJECT
 public:
  explicit PadWidget(QWidget* parent);
  void setConfig(BrushKind k, double size, const PressureCurve& c) { kind_ = k; size_ = size; curve_ = c; }
  void clear() { strokes_.clear(); live_.reset(); update(); }
  int strokeCount() const { return int(strokes_.size()); }
  QSize sizeHint() const override { return QSize(640, 140); }
  /** The same path the events take, for tests: a stroke given as points with pressures. */
  void begin(const QPointF& p, double pressure, bool hasPressure, qint64 t);
  void addPoint(const QPointF& p, double pressure, qint64 t);
  void end();

 protected:
  void paintEvent(QPaintEvent*) override;
  void tabletEvent(QTabletEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;

 private:
  struct Done { std::vector<double> pts, widths; };
  struct Live { std::unique_ptr<PressureMap> map; std::unique_ptr<BrushEngine> engine; Done d; };   // the engine points at the map: both live on the heap
  BrushKind kind_ = BrushKind::Pen;
  double size_ = 1.41;
  PressureCurve curve_ = kLinearCurve;
  std::vector<Done> strokes_;
  std::unique_ptr<Live> live_;
  bool tabletDown_ = false;
  QElapsedTimer clock_;
};
