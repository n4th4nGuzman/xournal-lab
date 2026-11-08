#include "pencursor.h"
#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace pencursor {

int diameterFor(double width, double zoom) {
  return std::clamp(int(std::lround(width * zoom)), kMinDiameter, kMaxDiameter);
}

QCursor disc(const QColor& fill, int diameter, double dpr) {
  struct Key { QRgb rgba = 0; int d = 0; double dpr = 0; };
  static Key last;
  static QCursor cached;
  const Key key{fill.rgba(), diameter, dpr};
  if (cached.shape() == Qt::BitmapCursor && last.rgba == key.rgba && last.d == key.d && last.dpr == key.dpr) return cached;
  const int side = diameter + 6;   // room for the outline and the outer ring
  const double r = diameter / 2.0;
  QPixmap pm(int(std::ceil(side * dpr)), int(std::ceil(side * dpr)));
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  const bool lightFill = fill.lightnessF() > 0.55 && fill.alpha() > 120;
  const QColor ring = lightFill ? QColor(0, 0, 0) : QColor(255, 255, 255);
  QColor outer = lightFill ? QColor(255, 255, 255) : QColor(0, 0, 0);
  outer.setAlpha(150);
  const QPointF c(side / 2.0, side / 2.0);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(outer, 1.0));
  p.drawEllipse(c, r + 1.3, r + 1.3);
  p.setBrush(fill);
  p.setPen(QPen(ring, 1.0));
  p.drawEllipse(c, r, r);
  p.end();
  cached = QCursor(pm, side / 2, side / 2);
  last = key;
  return cached;
}

}  // namespace pencursor
