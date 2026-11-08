// Small icons of the chrome area, drawn with QPainter like app/icons.cpp (no font symbols: a font without them shows empty boxes, and the glyphs must scale
// to any display).
#include "chrome.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace chrome {

QIcon glyph(const QString& n, const QColor& ink) {
  QPixmap pm(64, 64);
  pm.setDevicePixelRatio(2);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(1.0, 1.0);   // 32 x 32 logical units
  const QPen pen(ink, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  auto tri = [&](double cx, double cy, double r, int dir, bool fill) {   // dir: 0 left, 1 right
    QPainterPath t;
    const double s = dir == 0 ? -1 : 1;
    t.moveTo(cx + s * r, cy);
    t.lineTo(cx - s * r * 0.7, cy - r);
    t.lineTo(cx - s * r * 0.7, cy + r);
    t.closeSubpath();
    p.setBrush(fill ? QBrush(ink) : Qt::NoBrush);
    p.drawPath(t);
    p.setBrush(Qt::NoBrush);
  };
  auto arrow = [&](double x0, double y0, double x1, double y1) {
    p.drawLine(QPointF(x0, y0), QPointF(x1, y1));
    const double a = std::atan2(y1 - y0, x1 - x0), h = 6;
    p.drawLine(QPointF(x1, y1), QPointF(x1 - h * std::cos(a - 0.6), y1 - h * std::sin(a - 0.6)));
    p.drawLine(QPointF(x1, y1), QPointF(x1 - h * std::cos(a + 0.6), y1 - h * std::sin(a + 0.6)));
  };
  if (n == "first") { p.drawLine(QPointF(8, 8), QPointF(8, 24)); tri(16, 16, 7, 0, true); }
  else if (n == "last") { p.drawLine(QPointF(24, 8), QPointF(24, 24)); tri(16, 16, 7, 1, true); }
  else if (n == "prev") tri(15, 16, 8, 0, true);
  else if (n == "next") tri(17, 16, 8, 1, true);
  else if (n == "back") arrow(26, 16, 6, 16);
  else if (n == "forward") arrow(6, 16, 26, 16);
  else if (n == "up") arrow(16, 26, 16, 6);
  else if (n == "down") arrow(16, 6, 16, 26);
  else if (n == "plus") { p.drawLine(QPointF(16, 7), QPointF(16, 25)); p.drawLine(QPointF(7, 16), QPointF(25, 16)); }
  else if (n == "cross") { p.drawLine(QPointF(8, 8), QPointF(24, 24)); p.drawLine(QPointF(24, 8), QPointF(8, 24)); }
  else if (n == "trash") {
    p.drawLine(QPointF(7, 9), QPointF(25, 9));
    p.drawLine(QPointF(13, 9), QPointF(13, 6));
    p.drawLine(QPointF(19, 9), QPointF(19, 6));
    p.drawLine(QPointF(13, 6), QPointF(19, 6));
    QPainterPath b;
    b.moveTo(9, 9); b.lineTo(10.5, 26); b.lineTo(21.5, 26); b.lineTo(23, 9);
    p.drawPath(b);
    p.drawLine(QPointF(14, 13), QPointF(14, 22));
    p.drawLine(QPointF(18, 13), QPointF(18, 22));
  } else if (n == "pageBefore" || n == "pageEnd") {
    p.drawRoundedRect(QRectF(9, 6, 14, 20), 1.5, 1.5);
    const double y = n == "pageBefore" ? 4 : 28;
    p.drawLine(QPointF(5, y), QPointF(27, y));
  } else if (n == "pages") {
    p.drawRect(QRectF(6, 5, 8, 10)); p.drawRect(QRectF(18, 5, 8, 10)); p.drawRect(QRectF(6, 18, 8, 10)); p.drawRect(QRectF(18, 18, 8, 10));
  } else if (n == "layers") {
    for (int i = 0; i < 3; i++) {
      QPainterPath d;
      const double y = 21 - i * 6;
      d.moveTo(16, y - 7); d.lineTo(27, y - 2); d.lineTo(16, y + 3); d.lineTo(5, y - 2); d.closeSubpath();
      p.drawPath(d);
    }
  } else if (n == "paper") {
    p.drawRect(QRectF(6, 6, 20, 20));
    for (int i = 1; i < 3; i++) { p.drawLine(QPointF(6 + i * 20 / 3.0, 6), QPointF(6 + i * 20 / 3.0, 26)); p.drawLine(QPointF(6, 6 + i * 20 / 3.0), QPointF(26, 6 + i * 20 / 3.0)); }
  } else if (n == "theme") {
    p.drawEllipse(QPointF(16, 16), 10, 10);
    QPainterPath half;
    half.moveTo(16, 6); half.arcTo(QRectF(6, 6, 20, 20), 90, -180); half.closeSubpath();
    p.setBrush(ink);
    p.drawPath(half);
  } else if (n == "find") {
    p.drawEllipse(QPointF(14, 14), 7.5, 7.5);
    p.drawLine(QPointF(19.5, 19.5), QPointF(26, 26));
  } else if (n == "twopage") {   // two facing pages
    p.drawRect(QRectF(4.5, 8, 10, 16)); p.drawRect(QRectF(17.5, 8, 10, 16));
    p.drawLine(QPointF(7, 12), QPointF(12, 12)); p.drawLine(QPointF(20, 12), QPointF(25, 12));
  } else if (n == "present") {   // a screen with a play triangle
    p.drawRoundedRect(QRectF(4, 6, 24, 16), 2, 2);
    p.drawLine(QPointF(12, 27), QPointF(20, 27)); p.drawLine(QPointF(16, 22), QPointF(16, 27));
    tri(16.5, 14, 4.5, 1, true);
  } else if (n == "zoom100") {   // magnifier with "1:1"
    p.drawEllipse(QPointF(14, 14), 8.5, 8.5);
    p.drawLine(QPointF(20.5, 20.5), QPointF(27, 27));
    p.drawLine(QPointF(14, 10), QPointF(14, 18)); p.drawLine(QPointF(11.5, 12), QPointF(14, 10));
  } else if (n == "fullscreen") {   // four corners
    for (int sx : {0, 1}) for (int sy : {0, 1}) {
      const double x = sx ? 25 : 7, y = sy ? 25 : 7, dx = sx ? -6 : 6, dy = sy ? -6 : 6;
      p.drawLine(QPointF(x, y), QPointF(x + dx, y));
      p.drawLine(QPointF(x, y), QPointF(x, y + dy));
    }
  } else if (n == "undo") {
    arrow(24, 20, 9, 14);
  } else {
    p.drawRoundedRect(QRectF(6, 6, 20, 20), 4, 4);
  }
  return QIcon(pm);
}

}  // namespace chrome
