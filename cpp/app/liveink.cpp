#include "liveink.h"
#include "inkpiece.h"
#include <algorithm>
#include <cmath>

LiveInk::LiveInk(QPainter& p, const QColor& color, double scale) : p_(p), color_(color), scale_(std::max(scale, 1e-6)), pen_(color) {
  pen_.setJoinStyle(Qt::RoundJoin);
}

QRectF LiveInk::piece(const QPointF& a, const QPointF& c, const QPointF& b, double wa, double wb) {
  drawInkPiece(p_, pen_, Qt::RoundCap, a, c, b, wa, wb, scale_, false);
  const double w = std::max(wa, wb) + 2 / scale_;
  return QRectF(QPointF(std::min({a.x(), b.x(), c.x()}) - w, std::min({a.y(), b.y(), c.y()}) - w),
                QPointF(std::max({a.x(), b.x(), c.x()}) + w, std::max({a.y(), b.y(), c.y()}) + w));
}

QRectF LiveInk::add(const QPointF& pt, double width) {
  if (n_ == 0) {
    first_ = prev_ = pt;
    wPrev_ = width;
    n_ = 1;
    p_.setPen(Qt::NoPen);
    p_.setBrush(color_);
    const double r = width / 2;
    p_.drawEllipse(pt, r, r);
    const double m = r + 2 / scale_;
    dot_ = QRectF(pt.x() - m, pt.y() - m, 2 * m, 2 * m);
    return dot_;
  }
  QRectF touched;
  if (n_ == 1) {
    // The first piece runs from the first point to the first midpoint with the width of the first segment. The placeholder dot goes first.
    p_.save();
    p_.setCompositionMode(QPainter::CompositionMode_Source);
    p_.fillRect(dot_, Qt::transparent);
    p_.restore();
    const QPointF m((first_.x() + pt.x()) / 2, (first_.y() + pt.y()) / 2);
    touched = dot_.united(piece(first_, first_, m, width, width));
    mid_ = m;
  } else {
    const QPointF m((prev_.x() + pt.x()) / 2, (prev_.y() + pt.y()) / 2);
    touched = piece(mid_, prev_, m, wPrev_, width);
    mid_ = m;
  }
  prev_ = pt;
  wPrev_ = width;
  n_++;
  return touched;
}

QRectF LiveInk::end() {
  if (n_ < 2) return QRectF();   // a dot stays as it is
  return piece(mid_, prev_, prev_, wPrev_, wPrev_);
}
