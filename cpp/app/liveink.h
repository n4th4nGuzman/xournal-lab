// Draws a stroke piece by piece as its points arrive: quadratic curves between midpoints, width following the per-point width. This is the look of the
// prototype the user tried with a real pen. Use it on the painter DocumentView::beginLive() returns (units = page points).
//
// It draws exactly what core/render_stroke.cpp draws for the finished stroke (both go through drawInkPiece, with the same pieces and widths), so that
// nothing changes when the pen lifts. The width given with point i is stored as widths[i] of the stroke: piece i (from the midpoint before point i to the
// midpoint after it) goes from widths[i] to widths[i+1], the first piece (point 0 up to the first midpoint) has widths[1] and the last piece ends in
// the last point with widths[n-1]. The start has no separate dot: the first piece begins with a round cap of its own width, so a first sample with a
// different pressure cannot leave a blob (the dot shown for a pen that has not moved yet is replaced as soon as the second point arrives).
#pragma once
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

class LiveInk {
 public:
  /** `scale` = device pixels per page unit (view->zoom() * devicePixelRatio): chooses how finely a curve is cut and how widths are rounded. */
  LiveInk(QPainter& p, const QColor& color, double scale);
  /** Add a point with the width at it. Returns the page area touched (what to pass to DocumentView::liveDirty). */
  QRectF add(const QPointF& pt, double width);
  /** Close the stroke: the last stretch up to the final point. Returns the area touched. */
  QRectF end();

 private:
  QRectF piece(const QPointF& a, const QPointF& c, const QPointF& b, double wa, double wb);
  QPainter& p_;
  QColor color_;
  double scale_;
  QPen pen_;
  QPointF first_, prev_, mid_;
  double wPrev_ = 0;
  QRectF dot_;   // the dot drawn at the first point, until the second point replaces it
  int n_ = 0;
};
