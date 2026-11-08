#include "chrome/svgicon.h"
#include "tabletwidgets.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <algorithm>
#include <cmath>

void drawStrokeWidths(QPainter& p, const std::vector<double>& pts, const std::vector<double>& widths, double scale, const QColor& color) {
  if (pts.size() < 2 || widths.empty()) return;
  p.setRenderHint(QPainter::Antialiasing);
  if (pts.size() == 2) {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(pts[0], pts[1]), widths[0] * scale / 2, widths[0] * scale / 2);
    return;
  }
  QPen pen(color);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  for (size_t i = 1; i < pts.size() / 2; i++) {
    pen.setWidthF(std::max(0.05, (i < widths.size() ? widths[i] : widths[0]) * scale));
    p.setPen(pen);
    p.drawLine(QPointF(pts[2 * i - 2], pts[2 * i - 1]), QPointF(pts[2 * i], pts[2 * i + 1]));
  }
}

// ---- BrushCard -----------------------------------------------------------------------------------------------------------------------------
BrushCard::BrushCard(const BrushInfo& info, QWidget* parent) : QAbstractButton(parent), info_(info) {
  setCheckable(true);
  setToolTip(info.hint);
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setMinimumSize(128, 92);   // four of them must fit the narrowest dialog
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BrushCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
  p.setPen(QPen(isChecked() ? (chrome::svgThemeNeon() ? QColor("#ff2a9d") : QColor("#6aa0ff")) : QColor("#555555"), isChecked() ? 2.5 : 1));
  p.setBrush(isChecked() ? QColor(47, 111, 208, 70) : (underMouse() ? QColor("#3a3a3a") : QColor("#333333")));
  p.drawRoundedRect(r, 9, 9);
  const QColor ink = QColor("#eeeeee");
  const double w = r.width() - 20, h = r.height() - 34;
  PressureMap map(curve_);
  BrushEngine engine(info_.kind, std::min(size_, 7.0), map, true);
  std::vector<double> pts, widths;
  const auto demo = demoStroke(w, h);
  for (size_t i = 0; i < demo.size(); i++) {
    const DemoSample& s = demo[i];
    pts.push_back(s.x);
    pts.push_back(s.y);
    widths.push_back(i == 0 ? engine.first() : engine.next(s.x, s.y, s.p, s.t));
    if (i == 0) engine.next(s.x, s.y, s.p, s.t);
  }
  p.save();
  p.translate(r.left() + 10, r.top() + 6);
  drawStrokeWidths(p, pts, engine.finish(pts, widths), 1.0, ink);
  p.restore();
  p.setPen(isChecked() ? QColor("#ffffff") : ink);
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() + 0.5);
  f.setBold(isChecked());
  p.setFont(f);
  p.drawText(QRectF(r.left(), r.bottom() - 26, r.width(), 24), Qt::AlignCenter, info_.name);
}

// ---- CurveWidget ---------------------------------------------------------------------------------------------------------------------------
namespace { constexpr double kPad = 20; }

CurveWidget::CurveWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(300, 300);
  setToolTip(QString::fromUtf8("Arrastra los dos puntos para cambiar la curva"));
  setCursor(Qt::CrossCursor);
}

QPointF CurveWidget::toPixel(double x, double y) const { return {kPad + x * (width() - 2 * kPad), height() - kPad - y * (height() - 2 * kPad)}; }
QPointF CurveWidget::toPlot(const QPointF& px) const {
  const double x = (px.x() - kPad) / (width() - 2 * kPad), y = 1 - (px.y() - kPad) / (height() - 2 * kPad);
  return {std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)};
}

void CurveWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), QColor("#262626"));
  const QColor ink("#dddddd");
  QPen grid(ink);
  p.setOpacity(0.2);
  p.setPen(grid);
  for (int i = 0; i <= 4; i++) {
    p.drawLine(toPixel(i / 4.0, 0), toPixel(i / 4.0, 1));
    p.drawLine(toPixel(0, i / 4.0), toPixel(1, i / 4.0));
  }
  p.setOpacity(0.4);
  p.setPen(QPen(ink, 1, Qt::DashLine));
  p.drawLine(toPixel(0, 0), toPixel(1, 1));
  p.setOpacity(1);
  // the dead zone below `min`: shaded
  if (curve_.min > 0) p.fillRect(QRectF(toPixel(0, 1), toPixel(curve_.min, 0)).normalized(), QColor(255, 255, 255, 18));
  const PressureMap map(curve_);
  QPainterPath path;
  for (int i = 0; i <= 100; i++) {
    const QPointF q = toPixel(i / 100.0, map.apply(i / 100.0));
    if (i == 0) path.moveTo(q); else path.lineTo(q);
  }
  p.setPen(QPen(QColor("#2f9bff"), 3));
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);
  const QPointF h1 = toPixel(curve_.x1, curve_.y1), h2 = toPixel(curve_.x2, curve_.y2);
  p.setOpacity(0.55);
  p.setPen(QPen(ink, 1));
  p.drawLine(toPixel(0, 0), h1);
  p.drawLine(toPixel(1, 1), h2);
  p.setOpacity(1);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#ff5a8a"));
  for (const auto& r : recent_) { p.setOpacity(0.55); p.drawEllipse(toPixel(r.first, r.second), 2.6, 2.6); }
  p.setOpacity(1);
  p.setPen(QPen(chrome::svgThemeNeon() ? QColor("#ff2a9d") : QColor("#2f6fd0"), 2.5));
  p.setBrush(Qt::white);
  for (const QPointF& h : {h1, h2}) p.drawEllipse(h, 11, 11);   // 22 px handles: a pen tip or a finger can take them
  p.setPen(ink);
  QFont f = font();
  f.setPointSizeF(8.5);
  p.setFont(f);
  p.drawText(QPointF(width() - 118, height() - 4), QString::fromUtf8("presión del lápiz →"));
  p.save();
  p.translate(12, height() - 70);
  p.rotate(-90);
  p.drawText(QPointF(0, 0), QString::fromUtf8("grosor →"));
  p.restore();
}

void CurveWidget::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const QPointF q = toPlot(e->position());
  const double d1 = std::hypot(curve_.x1 - q.x(), curve_.y1 - q.y()), d2 = std::hypot(curve_.x2 - q.x(), curve_.y2 - q.y());
  drag_ = d1 <= d2 ? 1 : 2;
  moveHandle(e->position());
}
void CurveWidget::mouseMoveEvent(QMouseEvent* e) { if (drag_ && (e->buttons() & Qt::LeftButton)) moveHandle(e->position()); }

void CurveWidget::moveHandle(const QPointF& px) {
  const QPointF q = toPlot(px);
  if (drag_ == 1) { curve_.x1 = q.x(); curve_.y1 = q.y(); }
  else if (drag_ == 2) { curve_.x2 = q.x(); curve_.y2 = q.y(); }
  update();
  if (changed) changed(curve_);
}

// ---- PadWidget -----------------------------------------------------------------------------------------------------------------------------
PadWidget::PadWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(320, 140);
  setCursor(Qt::CrossCursor);
  setAttribute(Qt::WA_AcceptTouchEvents, false);
  clock_.start();
}

void PadWidget::begin(const QPointF& p, double pressure, bool hasPressure, qint64 t) {
  auto l = std::make_unique<Live>();
  l->map = std::make_unique<PressureMap>(curve_);
  l->engine = std::make_unique<BrushEngine>(kind_, size_, *l->map, hasPressure);
  l->engine->next(p.x(), p.y(), pressure, double(t));
  l->d.pts = {p.x(), p.y()};
  l->d.widths = {l->engine->first()};
  live_ = std::move(l);
  update();
}

void PadWidget::addPoint(const QPointF& p, double pressure, qint64 t) {
  if (!live_) return;
  Done& d = live_->d;
  if (std::hypot(p.x() - d.pts[d.pts.size() - 2], p.y() - d.pts.back()) < 0.2) return;
  d.pts.push_back(p.x());
  d.pts.push_back(p.y());
  d.widths.push_back(live_->engine->next(p.x(), p.y(), pressure, double(t)));
  update();
}

void PadWidget::end() {
  if (!live_) return;
  Done d = std::move(live_->d);
  if (d.widths.size() == d.pts.size() / 2) d.widths = live_->engine->finish(d.pts, d.widths);
  live_.reset();
  strokes_.push_back(std::move(d));
  update();
}

void PadWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor("#f7f7f4"));
  p.setPen(QColor("#c8c8c0"));
  p.drawRect(rect().adjusted(0, 0, -1, -1));
  for (const Done& d : strokes_) drawStrokeWidths(p, d.pts, d.widths, 1.0, QColor("#111111"));
  if (live_) drawStrokeWidths(p, live_->d.pts, live_->d.widths, 1.0, QColor("#111111"));
  if (strokes_.empty() && !live_) {
    p.setPen(QColor("#9a9a92"));
    p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("Dibuja aquí con la punta elegida"));
  }
}

void PadWidget::tabletEvent(QTabletEvent* e) {
  e->accept();
  const QPointF pos = e->position();
  const double pr = e->pressure();
  switch (e->type()) {
    case QEvent::TabletPress: tabletDown_ = true; begin(pos, pr, true, qint64(e->timestamp())); break;
    case QEvent::TabletMove: if (tabletDown_) addPoint(pos, pr, qint64(e->timestamp())); break;
    case QEvent::TabletRelease: if (tabletDown_) { addPoint(pos, pr, qint64(e->timestamp())); end(); } tabletDown_ = false; break;
    default: break;
  }
}

void PadWidget::mousePressEvent(QMouseEvent* e) {
  if (tabletDown_ || e->button() != Qt::LeftButton) { e->accept(); return; }
  begin(e->position(), 0.5, false, clock_.elapsed());
}
void PadWidget::mouseMoveEvent(QMouseEvent* e) {
  if (tabletDown_) { e->accept(); return; }
  if (live_) addPoint(e->position(), 0.5, clock_.elapsed());
}
void PadWidget::mouseReleaseEvent(QMouseEvent* e) {
  if (tabletDown_) { e->accept(); return; }
  if (live_) { addPoint(e->position(), 0.5, clock_.elapsed()); end(); }
}
