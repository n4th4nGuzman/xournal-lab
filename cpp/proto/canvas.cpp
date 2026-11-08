#include "canvas.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <random>

float StrokeRenderer::widthOf(float pr) const { return width_ * (0.3f + 0.7f * std::pow(std::clamp(pr, 0.f, 1.f), 0.8f)); }

QRectF StrokeRenderer::seg(const QPointF& a, const QPointF& c, const QPointF& b, float wa, float wb) {
  const double len = std::hypot(b.x() - a.x(), b.y() - a.y()) + std::hypot(c.x() - a.x(), c.y() - a.y());
  const int steps = std::clamp(int(len / 3.0), 1, 24);
  QPen pen(color_);
  pen.setCapStyle(Qt::RoundCap);
  QPointF last = a;
  for (int k = 1; k <= steps; k++) {
    const double t = double(k) / steps, u = 1 - t;
    const QPointF q(u * u * a.x() + 2 * u * t * c.x() + t * t * b.x(), u * u * a.y() + 2 * u * t * c.y() + t * t * b.y());
    pen.setWidthF(wa + (wb - wa) * (double(k) - 0.5) / steps);
    qp_.setPen(pen);
    qp_.drawLine(last, q);
    last = q;
  }
  const double w = std::max(wa, wb) + 2;
  return QRectF(QPointF(std::min({a.x(), b.x(), c.x()}) - w, std::min({a.y(), b.y(), c.y()}) - w),
                QPointF(std::max({a.x(), b.x(), c.x()}) + w, std::max({a.y(), b.y(), c.y()}) + w));
}

QRectF StrokeRenderer::add(const Pt& p) {
  if (n_ == 0) {
    prev_ = p; mid_ = p; wmid_ = widthOf(p.p); n_ = 1;
    qp_.setPen(Qt::NoPen);
    qp_.setBrush(color_);
    const double r = wmid_ / 2;
    qp_.drawEllipse(QPointF(p.x, p.y), r, r);
    return QRectF(p.x - r - 2, p.y - r - 2, 2 * r + 4, 2 * r + 4);
  }
  const Pt m{(prev_.x + p.x) / 2, (prev_.y + p.y) / 2, (prev_.p + p.p) / 2};
  const float wm = widthOf(m.p);
  const QRectF r = seg({mid_.x, mid_.y}, {prev_.x, prev_.y}, {m.x, m.y}, wmid_, wm);
  prev_ = p; mid_ = m; wmid_ = wm; n_++;
  return r;
}

QRectF StrokeRenderer::end() {
  if (n_ < 2) return QRectF();
  const float we = widthOf(prev_.p);
  return seg({mid_.x, mid_.y}, {prev_.x, prev_.y}, {prev_.x, prev_.y}, wmid_, we);
}

Canvas::Canvas(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  inputClock_.start(); fpsClock_.start(); evClock_.start();
  auto* t = new QTimer(this);
  connect(t, &QTimer::timeout, this, [this] {
    const double s = fpsClock_.restart() / 1000.0;
    fps_ = frames_ / std::max(s, 1e-3); frames_ = 0;
    evPerSec_ = int(eventsSince_ / std::max(evClock_.restart() / 1000.0, 1e-3)); eventsSince_ = 0;
    update(QRect(0, 0, 520, 24));
  });
  t->start(500);
}

QPixmap Canvas::newLayer(bool opaque) const {
  const qreal dpr = devicePixelRatioF();
  QPixmap pm(QSize(std::max(1, int(width() * dpr)), std::max(1, int(height() * dpr))));
  pm.setDevicePixelRatio(dpr);
  pm.fill(opaque ? QColor(255, 255, 255) : QColor(0, 0, 0, 0));
  return pm;
}

void Canvas::ensureLayers() {
  const qreal dpr = devicePixelRatioF();
  const QSize want(std::max(1, int(width() * dpr)), std::max(1, int(height() * dpr)));
  if (page_.size() == want && live_.size() == want) return;
  page_ = newLayer(true);
  live_ = newLayer(false);
  rebuildPage();
}

void Canvas::rebuildPage() {
  page_.fill(QColor(255, 255, 255));
  QPainter qp(&page_);
  qp.setRenderHint(QPainter::Antialiasing);
  for (const Stroke& s : strokes_) {
    StrokeRenderer r(qp, s.color, s.width);
    for (const Pt& p : s.pts) r.add(p);
    r.end();
  }
  update();
}

void Canvas::resizeEvent(QResizeEvent*) { ensureLayers(); }

void Canvas::liveBegin(const Pt& p) {
  ensureLayers();
  cur_ = Stroke{{}, color, penWidth};
  cur_.pts.push_back(p);
  live_.fill(QColor(0, 0, 0, 0));
  livePainter_.begin(&live_);
  livePainter_.setRenderHint(QPainter::Antialiasing);
  liveRender_ = new StrokeRenderer(livePainter_, color, penWidth);
  drawing_ = true;
  update(liveRender_->add(p).toAlignedRect());
}

void Canvas::liveAdd(const Pt& p) {
  if (!drawing_) return;
  cur_.pts.push_back(p);
  update(liveRender_->add(p).toAlignedRect());
}

void Canvas::liveEnd() {
  if (!drawing_) return;
  liveRender_->end();
  delete liveRender_;
  liveRender_ = nullptr;
  livePainter_.end();
  drawing_ = false;
  // the finished stroke goes to the cached page, drawn once
  {
    QPainter qp(&page_);
    qp.setRenderHint(QPainter::Antialiasing);
    StrokeRenderer r(qp, cur_.color, cur_.width);
    for (const Pt& p : cur_.pts) r.add(p);
    r.end();
  }
  live_.fill(QColor(0, 0, 0, 0));
  strokes_.push_back(std::move(cur_));
  update();
}

void Canvas::undo() {
  if (drawing_ || strokes_.empty()) return;
  strokes_.pop_back();
  rebuildPage();
}

void Canvas::clearAll() {
  strokes_.clear();
  rebuildPage();
}

void Canvas::addRandomStrokes(int n) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> ux(20, float(std::max(40, width() - 20))), uy(20, float(std::max(40, height() - 20))), ua(0, 6.2831f), ud(-0.25f, 0.25f);
  strokes_.reserve(strokes_.size() + size_t(n));
  ensureLayers();
  QPainter qp(&page_);
  qp.setRenderHint(QPainter::Antialiasing);
  for (int i = 0; i < n; i++) {
    Stroke s{{}, QColor(int(30 + rng() % 150), int(30 + rng() % 150), int(30 + rng() % 150)), 1.6f};
    float x = ux(rng), y = uy(rng), a = ua(rng);
    for (int k = 0; k < 60; k++) {
      a += ud(rng);
      x += std::cos(a) * 2.5f;
      y += std::sin(a) * 2.5f;
      s.pts.push_back({x, y, 0.3f + 0.6f * std::abs(std::sin(float(k) * 0.12f))});
    }
    StrokeRenderer r(qp, s.color, s.width);
    for (const Pt& p : s.pts) r.add(p);
    r.end();
    strokes_.push_back(std::move(s));
  }
  update();
}

void Canvas::paintEvent(QPaintEvent* e) {
  ensureLayers();
  QPainter qp(this);
  const QRect r = e->rect();
  const qreal d = page_.devicePixelRatio();
  const QRect src(int(r.x() * d), int(r.y() * d), int(r.width() * d), int(r.height() * d));
  qp.drawPixmap(r.topLeft(), page_, src);
  if (drawing_) qp.drawPixmap(r.topLeft(), live_, src);
  if (r.top() < 24) {
    qp.fillRect(0, 0, 520, 22, QColor(0, 0, 0, 170));
    qp.setPen(Qt::white);
    qp.drawText(8, 15, QString("trazos %1   cuadros/s %2   eventos/s %3   app: ultimo %4 ms, peor %5 ms")
      .arg(strokes_.size()).arg(fps_, 0, 'f', 0).arg(evPerSec_).arg(lastLatencyMs_, 0, 'f', 2).arg(worstLatencyMs_, 0, 'f', 2));
  }
  frames_++;
  if (lastInputNs_) {
    lastLatencyMs_ = double(inputClock_.nsecsElapsed() - lastInputNs_) / 1e6;
    worstLatencyMs_ = std::max(worstLatencyMs_ * 0.995, lastLatencyMs_);
    lastInputNs_ = 0;
  }
}

static Pt ptOf(const QPointF& p, float pr) { return {float(p.x()), float(p.y()), pr}; }

void Canvas::tabletEvent(QTabletEvent* e) {
  e->accept();
  lastInputNs_ = inputClock_.nsecsElapsed();
  eventsSince_++;
  const bool pen = e->pointerType() == QPointingDevice::PointerType::Pen;
  const bool eraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
  if (eraser) return;  // the eraser comes in the next stage
  switch (e->type()) {
    case QEvent::TabletPress: if (pen) { tabletDown_ = true; liveBegin(ptOf(e->position(), float(e->pressure()))); } break;
    case QEvent::TabletMove: if (tabletDown_) liveAdd(ptOf(e->position(), float(e->pressure()))); break;
    case QEvent::TabletRelease: if (tabletDown_) { tabletDown_ = false; liveEnd(); } break;
    default: break;
  }
}

void Canvas::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && !tabletDown_ && e->source() == Qt::MouseEventNotSynthesized) {
    lastInputNs_ = inputClock_.nsecsElapsed(); eventsSince_++;
    liveBegin(ptOf(e->position(), 0.6f));
  }
}

void Canvas::mouseMoveEvent(QMouseEvent* e) {
  if (drawing_ && !tabletDown_) { lastInputNs_ = inputClock_.nsecsElapsed(); eventsSince_++; liveAdd(ptOf(e->position(), 0.6f)); }
}

void Canvas::mouseReleaseEvent(QMouseEvent*) {
  if (drawing_ && !tabletDown_) liveEnd();
}

void Canvas::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Z && (e->modifiers() & Qt::ControlModifier)) undo();
  else if (e->key() == Qt::Key_S) addRandomStrokes(2000);
  else if (e->key() == Qt::Key_C) clearAll();
  else if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) penWidth *= 1.25f;
  else if (e->key() == Qt::Key_Minus) penWidth /= 1.25f;
  else QWidget::keyPressEvent(e);
}
