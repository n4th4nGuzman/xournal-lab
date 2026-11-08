// Toolbar glyphs of the plane, 3D space and figure tools: the strokes of the Electron icons (24 x 24 grid), drawn with QPainter like app/icons.cpp does
// for the built-in tools (no image files: they scale to any display).
#include "objects.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace xlobjects {
namespace {

QPainterPath line(std::initializer_list<QPointF> pts) {
  QPainterPath p;
  bool first = true;
  for (const QPointF& q : pts) { if (first) p.moveTo(q); else p.lineTo(q); first = false; }
  return p;
}

void glyph(QPainter& p, const QString& name) {
  const QColor ink(0xe6, 0xe6, 0xe6);
  p.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  auto stroke = [&](const QPainterPath& path, double opacity = 1.0) { p.setOpacity(opacity); p.drawPath(path); p.setOpacity(1.0); };
  if (name == "plane") {
    stroke(line({{5, 21}, {5, 4}}));
    stroke(line({{3, 21}, {21, 21}}));
    stroke(line({{6.6, 5.4}, {5, 3}, {3.4, 5.4}}));
    stroke(line({{19.6, 19.4}, {22, 21}, {19.6, 22.6}}));
    for (const auto& t : {line({{10, 21}, {10, 18}}), line({{15, 21}, {15, 18}}), line({{5, 15}, {8, 15}}), line({{5, 10}, {8, 10}}) }) stroke(t, 0.6);
    QPainterPath curve;
    curve.moveTo(7, 17);
    curve.cubicTo(10, 17, 11, 10, 14, 10);
    curve.cubicTo(17, 10, 16, 13, 20, 13);
    stroke(curve, 0.85);
  } else if (name == "space3") {
    stroke(line({{12, 14}, {12, 3}}));
    stroke(line({{12, 14}, {4, 20}}));
    stroke(line({{12, 14}, {21, 14}}));
    stroke(line({{10.4, 5.4}, {12, 3}, {13.6, 5.4}}));
    stroke(line({{18.6, 12.4}, {21, 14}, {18.6, 15.6}}));
    stroke(line({{6.7, 19.7}, {4, 20}, {4.9, 17.4}}));
    stroke(line({{8, 17}, {12, 11}, {16, 14}}), 0.5);
    stroke(line({{12, 8}, {17, 11}}), 0.5);
  } else {   // figure
    QPainterPath poly = line({{4, 18}, {9, 6}, {17, 10}, {20, 18}});
    poly.closeSubpath();
    stroke(poly);
    for (const QPointF& c : {QPointF(4, 18), QPointF(9, 6), QPointF(17, 10), QPointF(20, 18)}) p.drawEllipse(c, 1.3, 1.3);
  }
}

}  // namespace

QIcon objectIcon(const QString& name) {
  QIcon icon;
  for (int px : {24, 32, 48}) {
    QPixmap pm(px * 2, px * 2);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(2);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 24.0, px / 24.0);
    glyph(p, name);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

}  // namespace xlobjects
