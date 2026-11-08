// The one definition of how a piece of a pen stroke is drawn. The stroke is a chain of quadratic pieces between the midpoints of its points (the
// look of proto/canvas.cpp); the width goes from `wa` at the start of a piece to `wb` at its end. The live layer (app/liveink) and the finished
// stroke (render_stroke.cpp) both draw every piece through drawInkPiece, so nothing changes when the pen lifts.
#pragma once
#include <QPainter>
#include <QPen>
#include <QPointF>

/** The width that is really drawn: rounded to a third of a device pixel (lite: a whole pixel), never below 0.05. `k` = device pixels per page unit. */
double inkWidth(double w, double k, bool lite);

/** Draws the piece a -> b with control point c. `pen` carries the colour and join; its width and cap are set here. A piece whose rounded widths are equal is
 *  one stroked curve of that width, otherwise short round-capped lines of interpolated width follow the curve. */
void drawInkPiece(QPainter& p, QPen& pen, Qt::PenCapStyle cap, const QPointF& a, const QPointF& c, const QPointF& b, double wa, double wb, double k, bool lite);

/** Finished strokes with a varying width draw runs of equal-width pieces as ONE stroked path (fast). The live layer can only draw piece by piece, whose
 *  overlapping round caps make the antialiased edge a hair darker; with batching off the finished stroke is drawn piece by piece as well and the two are
 *  identical to the last rounding (about 4 times slower on dense pages). Default: on. */
void setInkBatching(bool on);
bool inkBatching();
