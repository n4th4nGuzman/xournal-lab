#include "icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace {
const QColor kInk(0xe6, 0xe6, 0xe6);

void drawGlyph(QPainter& p, const QString& n, const QString& letter) {
  // 32 x 32 logical units
  QPen pen(kInk, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  if (n == "pen") {
    p.save();
    p.translate(16, 16);
    p.rotate(45);
    p.drawRoundedRect(QRectF(-3.5, -13, 7, 18), 1.5, 1.5);
    QPainterPath tip;
    tip.moveTo(-3.5, 5); tip.lineTo(0, 13); tip.lineTo(3.5, 5); tip.closeSubpath();
    p.setBrush(kInk);
    p.drawPath(tip);
    p.restore();
  } else if (n == "highlighter") {
    p.save();
    p.translate(16, 15);
    p.rotate(45);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 226, 60));
    p.drawRoundedRect(QRectF(-5, -12, 10, 17), 2, 2);
    p.setBrush(QColor(255, 226, 60, 150));
    p.drawRect(QRectF(-5, 5, 10, 6));
    p.restore();
    p.setPen(QPen(QColor(255, 226, 60), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(6, 27), QPointF(22, 27));
  } else if (n == "eraser" || n == "eraserPart") {
    p.save();
    p.translate(16, 16);
    p.rotate(-35);
    p.setBrush(QColor(240, 140, 160));
    p.drawRoundedRect(QRectF(-12, -6, 24, 12), 3, 3);
    p.setPen(QPen(kInk, 1.5));
    p.drawLine(QPointF(-3, -6), QPointF(-3, 6));
    p.restore();
    if (n == "eraserPart") { p.setPen(QPen(kInk, 1.6, Qt::DotLine)); p.drawLine(QPointF(4, 27), QPointF(28, 27)); }
  } else if (n == "select") {
    p.setPen(QPen(kInk, 1.8, Qt::DashLine));
    p.drawRect(QRectF(5, 5, 16, 16));
    p.setPen(pen);
    QPainterPath a;
    a.moveTo(16, 16); a.lineTo(16, 28); a.lineTo(19.5, 24.5); a.lineTo(22.5, 30); a.lineTo(25, 28.8); a.lineTo(22, 23.5); a.lineTo(27, 23.5); a.closeSubpath();
    p.setBrush(kInk);
    p.drawPath(a);
  } else if (n == "hand") {
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 4; i++) p.drawLine(QPointF(9.5 + i * 4.2, 15 - (i == 0 || i == 3 ? 3 : 7) - (i == 3 ? -1 : 0)), QPointF(9.5 + i * 4.2, 18));
    QPainterPath body;
    body.moveTo(8, 18); body.lineTo(8, 22); body.quadTo(9, 28, 16, 28); body.quadTo(23, 28, 24.5, 22); body.lineTo(25.5, 15); body.moveTo(24.5, 22);
    p.drawPath(body);
    p.drawLine(QPointF(7, 17), QPointF(4.5, 12));
  } else if (n == "laser") {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 60, 60));
    p.drawEllipse(QPointF(16, 16), 4.5, 4.5);
    p.setPen(QPen(QColor(255, 90, 90), 2, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 8; i++) {
      const double a = i * M_PI / 4;
      p.drawLine(QPointF(16 + 8 * std::cos(a), 16 + 8 * std::sin(a)), QPointF(16 + 12.5 * std::cos(a), 16 + 12.5 * std::sin(a)));
    }
  } else if (n == "undo" || n == "redo") {
    p.save();
    if (n == "redo") { p.translate(32, 0); p.scale(-1, 1); }
    QPainterPath a;
    a.moveTo(10, 12); a.cubicTo(22, 8, 28, 16, 25, 24);
    p.drawPath(a);
    QPainterPath h;
    h.moveTo(5, 12); h.lineTo(12, 5.5); h.lineTo(13.5, 15); h.closeSubpath();
    p.setBrush(kInk);
    p.drawPath(h);
    p.restore();
  } else if (n == "zoomin" || n == "zoomout" || n == "fit") {
    p.drawEllipse(QPointF(14, 14), 8, 8);
    p.drawLine(QPointF(20, 20), QPointF(27, 27));
    if (n != "fit") p.drawLine(QPointF(10, 14), QPointF(18, 14));
    if (n == "zoomin") p.drawLine(QPointF(14, 10), QPointF(14, 18));
    if (n == "fit") p.drawRect(QRectF(10.5, 10.5, 7, 7));
  } else if (n == "page_add") {
    p.drawRoundedRect(QRectF(7, 4, 18, 24), 2, 2);
    p.drawLine(QPointF(16, 11), QPointF(16, 21));
    p.drawLine(QPointF(11, 16), QPointF(21, 16));
  } else {
    p.drawRoundedRect(QRectF(5, 5, 22, 22), 5, 5);
    QFont f = p.font();
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(5, 5, 22, 22), Qt::AlignCenter, letter.isEmpty() ? n.left(1).toUpper() : letter);
  }
}
}  // namespace

QIcon Icons::make(const QString& name, const QString& letter) {
  QIcon icon;
  for (int px : {24, 32, 48}) {
    QPixmap pm(px * 2, px * 2);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(2);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 32.0, px / 32.0);
    drawGlyph(p, name, letter);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}
