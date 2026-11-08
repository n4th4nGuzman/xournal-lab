#include "selicons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace selx {
namespace {

const QColor kInk(0xe6, 0xe6, 0xe6);

/** A rounded rectangle given in the 24-unit grid of the Electron icons. */
void rrect(QPainter& p, double x, double y, double w, double h, double r) { p.drawRoundedRect(QRectF(x, y, w, h), r, r); }

void draw(QPainter& p, const QString& n) {
  QPen pen(kInk, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  auto line = [&](double x0, double y0, double x1, double y1) { p.drawLine(QPointF(x0, y0), QPointF(x1, y1)); };
  auto poly = [&](std::initializer_list<QPointF> pts, bool close = false) {
    QPainterPath path;
    bool first = true;
    for (const QPointF& q : pts) { if (first) path.moveTo(q); else path.lineTo(q); first = false; }
    if (close) path.closeSubpath();
    p.drawPath(path);
  };
  if (n == "cut") {
    p.drawEllipse(QPointF(6, 6), 3, 3);
    p.drawEllipse(QPointF(6, 18), 3, 3);
    line(20, 4, 8.1, 15.9); line(14.5, 14.5, 20, 20); line(8.1, 8.1, 12, 12);
  } else if (n == "copy") {
    rrect(p, 9, 9, 11, 11, 2);
    QPainterPath b;
    b.moveTo(5, 15); b.lineTo(5, 6); b.quadTo(5, 4, 7, 4); b.lineTo(16, 4);
    p.drawPath(b);
  } else if (n == "paste") {
    rrect(p, 6, 5, 12, 16, 2);
    poly({{9, 5}, {9, 3.5}, {15, 3.5}, {15, 5}});
    line(9.5, 11, 14.5, 11); line(9.5, 15, 14.5, 15);
  } else if (n == "dup") {
    rrect(p, 3, 3, 12, 12, 2);
    rrect(p, 9, 9, 12, 12, 2);
    line(15, 12, 15, 18); line(12, 15, 18, 15);
  } else if (n == "front" || n == "back") {
    const bool front = n == "front";
    // the one behind is the faint filled square, the one in front is outlined and covers it
    const QRectF behind = front ? QRectF(8, 8, 13, 13) : QRectF(3, 3, 13, 13), ahead = front ? QRectF(3, 3, 13, 13) : QRectF(8, 8, 13, 13);
    p.setBrush(QColor(0xe6, 0xe6, 0xe6, 90));
    p.drawRoundedRect(behind, 2, 2);
    p.setBrush(QColor(0x33, 0x33, 0x33));
    p.drawRoundedRect(ahead, 2, 2);
  } else if (n == "rotR" || n == "rotL") {
    p.save();
    if (n == "rotL") { p.translate(24, 0); p.scale(-1, 1); }
    poly({{21, 4}, {21, 10}, {15, 10}});
    QPainterPath arc;   // M20 10 a8 8 0 1 0 .8 5.5: almost a full circle, counter-clockwise on the screen
    arc.moveTo(20, 10);
    arc.arcTo(QRectF(13 - 8, 13.8 - 8, 16, 16), 28.5, 319);
    p.drawPath(arc);
    p.restore();
  } else if (n == "flipH" || n == "flipV") {
    p.save();
    if (n == "flipV") { p.translate(12, 12); p.rotate(90); p.translate(-12, -12); }
    QPen d = pen;
    d.setStyle(Qt::CustomDashLine);
    d.setDashPattern({2.5 / 1.7, 2.0 / 1.7});
    p.setPen(d);
    line(12, 3, 12, 21);
    p.setPen(pen);
    poly({{9, 7}, {3, 12}, {9, 17}}, true);
    poly({{15, 7}, {21, 12}, {15, 17}}, true);
    p.restore();
  } else if (n == "lasso") {
    QPen d = pen;
    d.setStyle(Qt::CustomDashLine);
    d.setDashPattern({3 / 1.7, 2.4 / 1.7});
    p.setPen(d);
    QPainterPath l;
    l.moveTo(12, 5); l.cubicTo(7, 5, 4, 7, 4, 10); l.cubicTo(4, 13, 7, 15, 12, 15); l.cubicTo(17, 15, 20, 13, 20, 10); l.cubicTo(20, 7, 17, 5, 12, 5);
    p.drawPath(l);
    p.setPen(pen);
    QPainterPath t;
    t.moveTo(10, 15); t.cubicTo(9, 18, 10, 20, 13, 20);
    p.drawPath(t);
  } else if (n == "glow" || n == "glowOn") {
    const QColor c = n == "glowOn" ? QColor(0xff, 0xd2, 0x4a) : kInk;
    QPen gp(c, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(gp);
    QPainterPath w;
    w.moveTo(5, 17); w.cubicTo(8, 9, 11, 9, 12, 13); w.cubicTo(13, 17, 16, 17, 19, 8);
    if (n == "glowOn") { QPen halo(QColor(0xff, 0xd2, 0x4a, 90), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); p.setPen(halo); p.drawPath(w); p.setPen(gp); }
    p.drawPath(w);
    p.setOpacity(0.45);
    line(3, 20, 21, 20);
    p.setOpacity(1);
    line(12, 2, 12, 4); line(4.2, 5.2, 5.6, 6.6); line(19.8, 5.2, 18.4, 6.6);
  } else if (n == "tablet") {
    rrect(p, 3, 5, 14, 13, 1.5);
    poly({{17, 15}, {20.5, 5.5}, {22, 6.5}, {17, 15.5}}, true);
    p.setOpacity(0.5);
    line(7, 9, 13, 9); line(7, 12, 11, 12);
    p.setOpacity(1);
  } else if (n == "vspace") {
    QPen d = pen;
    d.setStyle(Qt::CustomDashLine);
    d.setDashPattern({3 / 1.7, 2.4 / 1.7});
    p.setPen(d);
    line(4, 12, 20, 12);
    p.setPen(pen);
    poly({{12, 9}, {12, 3}}); poly({{9, 6}, {12, 3}, {15, 6}});
    poly({{12, 15}, {12, 21}}); poly({{9, 18}, {12, 21}, {15, 18}});
  }
}

}  // namespace

QIcon selIcon(const QString& name) {
  static const QStringList known = {"cut", "copy", "paste", "dup", "front", "back", "rotL", "rotR", "flipH", "flipV", "lasso", "vspace", "glow", "glowOn", "tablet"};
  if (!known.contains(name)) return QIcon();
  QIcon icon;
  for (int px : {24, 32, 48}) {
    QPixmap pm(px * 2, px * 2);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(2);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 24.0, px / 24.0);
    draw(p, name);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

}  // namespace selx
