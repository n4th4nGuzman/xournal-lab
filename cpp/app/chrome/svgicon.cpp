#include "svgicon.h"
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

namespace chrome {
namespace {

struct IconDef { const char* name; const char* body; };
const IconDef kIcons[] = {
#include "svgicons_data.inc"
};

const char* findBody(const QString& name) {
  for (const IconDef& d : kIcons) if (name == QLatin1String(d.name)) return d.body;
  return nullptr;
}

// ---- path data -----------------------------------------------------------------------------------------------------------------------------
struct Scanner {
  const QString& s;
  int i = 0;
  explicit Scanner(const QString& str) : s(str) {}
  void skipSep() { while (i < s.size() && (s[i].isSpace() || s[i] == ',')) i++; }
  bool more() { skipSep(); return i < s.size(); }
  bool atCommand() { skipSep(); return i < s.size() && s[i].isLetter(); }
  bool number(double& out) {
    skipSep();
    const int start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
    bool dot = false, digits = false;
    while (i < s.size()) {
      if (s[i].isDigit()) { digits = true; i++; }
      else if (s[i] == '.' && !dot) { dot = true; i++; }
      else break;
    }
    if (!digits) { i = start; return false; }
    out = s.mid(start, i - start).toDouble();
    return true;
  }
  bool flag(bool& out) {   // arc flags are one character: "a8 8 0 10.8 5.5" is large=1 sweep=0 x=.8 y=5.5
    skipSep();
    if (i < s.size() && (s[i] == '0' || s[i] == '1')) { out = s[i] == '1'; i++; return true; }
    return false;
  }
};

void arcTo(QPainterPath& path, QPointF from, double rx, double ry, double rotDeg, bool large, bool sweep, QPointF to) {
  if (from == to) return;
  rx = std::abs(rx); ry = std::abs(ry);
  if (rx == 0 || ry == 0) { path.lineTo(to); return; }
  const double phi = rotDeg * M_PI / 180, cp = std::cos(phi), sp = std::sin(phi);
  const double dx = (from.x() - to.x()) / 2, dy = (from.y() - to.y()) / 2;
  const double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
  const double lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lam > 1) { rx *= std::sqrt(lam); ry *= std::sqrt(lam); }
  const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  double coef = den == 0 ? 0 : std::sqrt(std::max(0.0, num / den));
  if (large == sweep) coef = -coef;
  const double cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
  const double cx = cp * cx1 - sp * cy1 + (from.x() + to.x()) / 2, cy = sp * cx1 + cp * cy1 + (from.y() + to.y()) / 2;
  auto ang = [](double ux, double uy, double vx, double vy) {
    const double a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    return a;
  };
  const double th1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
  double dth = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
  if (!sweep && dth > 0) dth -= 2 * M_PI;
  if (sweep && dth < 0) dth += 2 * M_PI;
  const int n = std::max(1, int(std::ceil(std::abs(dth) / (M_PI / 2) - 1e-9)));
  const double step = dth / n, k = 4.0 / 3.0 * std::tan(step / 4);
  double t = th1;
  for (int j = 0; j < n; j++) {
    const double c1 = std::cos(t), s1 = std::sin(t), c2 = std::cos(t + step), s2 = std::sin(t + step);
    auto pt = [&](double x, double y) { return QPointF(cp * rx * x - sp * ry * y + cx, sp * rx * x + cp * ry * y + cy); };
    // unit circle control points, then the ellipse transform
    const QPointF p1 = pt(c1 - k * s1, s1 + k * c1), p2 = pt(c2 + k * s2, s2 - k * c2), p3 = j == n - 1 ? to : pt(c2, s2);
    path.cubicTo(p1, p2, p3);
    t += step;
  }
}

QPainterPath parsePath(const QString& d) {
  QPainterPath path;
  Scanner sc(d);
  QPointF cur, start, lastCtrl;
  QChar cmd, prev;
  while (sc.more()) {
    if (sc.atCommand()) { prev = cmd; cmd = sc.s[sc.i++]; }
    else if (cmd.isNull()) break;
    else if (cmd == 'M') cmd = 'L';
    else if (cmd == 'm') cmd = 'l';
    const bool rel = cmd.isLower();
    const QChar c = cmd.toUpper();
    auto num = [&](double& v) { return sc.number(v); };
    double a, b, c1, d1, e, f, g;
    if (c == 'Z') { path.closeSubpath(); cur = start; lastCtrl = cur; prev = cmd; continue; }
    if (c == 'M') { if (!num(a) || !num(b)) break; cur = rel ? cur + QPointF(a, b) : QPointF(a, b); start = cur; path.moveTo(cur); lastCtrl = cur; }
    else if (c == 'L') { if (!num(a) || !num(b)) break; cur = rel ? cur + QPointF(a, b) : QPointF(a, b); path.lineTo(cur); lastCtrl = cur; }
    else if (c == 'H') { if (!num(a)) break; cur.setX(rel ? cur.x() + a : a); path.lineTo(cur); lastCtrl = cur; }
    else if (c == 'V') { if (!num(a)) break; cur.setY(rel ? cur.y() + a : a); path.lineTo(cur); lastCtrl = cur; }
    else if (c == 'C') {
      if (!num(a) || !num(b) || !num(c1) || !num(d1) || !num(e) || !num(f)) break;
      const QPointF o = rel ? cur : QPointF();
      const QPointF p1 = o + QPointF(a, b), p2 = o + QPointF(c1, d1), p3 = o + QPointF(e, f);
      path.cubicTo(p1, p2, p3); lastCtrl = p2; cur = p3;
    } else if (c == 'S') {
      if (!num(c1) || !num(d1) || !num(e) || !num(f)) break;
      const QPointF o = rel ? cur : QPointF();
      const bool afterCurve = prev.toUpper() == 'C' || prev.toUpper() == 'S';
      const QPointF p1 = afterCurve ? 2 * cur - lastCtrl : cur, p2 = o + QPointF(c1, d1), p3 = o + QPointF(e, f);
      path.cubicTo(p1, p2, p3); lastCtrl = p2; cur = p3;
    } else if (c == 'Q') {
      if (!num(a) || !num(b) || !num(e) || !num(f)) break;
      const QPointF o = rel ? cur : QPointF();
      const QPointF p1 = o + QPointF(a, b), p2 = o + QPointF(e, f);
      path.quadTo(p1, p2); lastCtrl = p1; cur = p2;
    } else if (c == 'A') {
      bool large = false, sweep = false;
      if (!num(a) || !num(b) || !num(c1) || !sc.flag(large) || !sc.flag(sweep) || !num(e) || !num(f)) break;
      const QPointF to = rel ? cur + QPointF(e, f) : QPointF(e, f);
      arcTo(path, cur, a, b, c1, large, sweep, to);
      cur = to; lastCtrl = cur;
    } else break;
    (void)g;
    prev = cmd;
  }
  return path;
}

// ---- the drawing ---------------------------------------------------------------------------------------------------------------------------
enum class Fill { None, Ink, ButtonBg };
struct Shape {
  QPainterPath path;
  QTransform xf;
  double opacity = 1, fillOpacity = 1, strokeWidth = -1;
  Fill fill = Fill::None;
  QVector<qreal> dash;
};

QTransform parseTransform(const QString& t) {
  QTransform r;
  static const QRegularExpression re(R"((\w+)\s*\(([^)]*)\))");
  auto it = re.globalMatch(t);
  while (it.hasNext()) {
    const auto m = it.next();
    QList<double> v;
    for (const QString& p : m.captured(2).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) v << p.toDouble();
    const QString k = m.captured(1);
    QTransform one;
    if (k == "translate" && !v.isEmpty()) one.translate(v[0], v.size() > 1 ? v[1] : 0);
    else if (k == "scale" && !v.isEmpty()) one.scale(v[0], v.size() > 1 ? v[1] : v[0]);
    else if (k == "rotate" && !v.isEmpty()) { one.translate(v.size() > 2 ? v[1] : 0, v.size() > 2 ? v[2] : 0); one.rotate(v[0]); one.translate(v.size() > 2 ? -v[1] : 0, v.size() > 2 ? -v[2] : 0); }
    r = one * r;
  }
  return r;
}

const std::vector<Shape>& shapesOf(const QString& name) {
  static std::map<QString, std::vector<Shape>> cache;
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  std::vector<Shape>& out = cache[name];
  const char* body = findBody(name);
  if (!body) return out;
  QXmlStreamReader xml(QString("<svg>") + QString::fromUtf8(body) + "</svg>");
  std::vector<QTransform> stack{QTransform()};
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isEndElement()) { if (xml.name() == QLatin1String("g") && stack.size() > 1) stack.pop_back(); continue; }
    if (!xml.isStartElement()) continue;
    const QString tag = xml.name().toString();
    const auto at = xml.attributes();
    auto num = [&](const char* k, double def = 0) { return at.hasAttribute(QLatin1String(k)) ? at.value(QLatin1String(k)).toDouble() : def; };
    if (tag == "g") { stack.push_back(at.hasAttribute("transform") ? parseTransform(at.value("transform").toString()) * stack.back() : stack.back()); continue; }
    Shape sh;
    sh.xf = stack.back();
    if (tag == "path") sh.path = parsePath(at.value("d").toString());
    else if (tag == "circle") sh.path.addEllipse(QPointF(num("cx"), num("cy")), num("r"), num("r"));
    else if (tag == "rect") {
      const double rx = num("rx");
      sh.path.addRoundedRect(QRectF(num("x"), num("y"), num("width"), num("height")), rx, rx);
    } else continue;
    sh.opacity = num("opacity", 1);
    sh.fillOpacity = num("fill-opacity", 1);
    sh.strokeWidth = num("stroke-width", -1);
    const QString fill = at.value("fill").toString();
    if (fill == "currentColor") sh.fill = Fill::Ink;
    else if (fill.startsWith("var(")) sh.fill = Fill::ButtonBg;
    if (at.hasAttribute("stroke-dasharray"))
      for (const QString& p : at.value("stroke-dasharray").toString().split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) sh.dash << p.toDouble();
    out.push_back(std::move(sh));
  }
  return out;
}

QImage blurred(const QImage& src, int radius) {   // three box passes ~ a gaussian; the icons are a few hundred pixels, so this is cheap (and cached by the engine)
  if (radius < 1) return src;
  QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const int w = img.width(), h = img.height();
  std::vector<float> buf(size_t(w) * h * 4), tmp(buf.size());
  for (int y = 0; y < h; y++) {
    const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < w; x++) {
      float* o = &buf[(size_t(y) * w + x) * 4];
      o[0] = qAlpha(line[x]); o[1] = qRed(line[x]); o[2] = qGreen(line[x]); o[3] = qBlue(line[x]);
    }
  }
  auto pass = [&](bool horizontal) {
    const int n = horizontal ? w : h, m = horizontal ? h : w;
    for (int j = 0; j < m; j++) {
      for (int ch = 0; ch < 4; ch++) {
        auto at = [&](int k) { k = std::clamp(k, -1, n); if (k < 0 || k >= n) return 0.0f; return horizontal ? buf[(size_t(j) * w + k) * 4 + ch] : buf[(size_t(k) * w + j) * 4 + ch]; };
        float sum = 0;
        for (int k = -radius; k <= radius; k++) sum += at(k);
        for (int k = 0; k < n; k++) {
          const size_t idx = horizontal ? (size_t(j) * w + k) * 4 + ch : (size_t(k) * w + j) * 4 + ch;
          tmp[idx] = sum / (2 * radius + 1);
          sum += at(k + radius + 1) - at(k - radius);
        }
      }
    }
    buf.swap(tmp);
  };
  for (int i = 0; i < 3; i++) { pass(true); pass(false); }
  QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < h; y++) {
    QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
    for (int x = 0; x < w; x++) {
      const float* o = &buf[(size_t(y) * w + x) * 4];
      line[x] = qPremultiply(qRgba(int(o[1] + 0.5f), int(o[2] + 0.5f), int(o[3] + 0.5f), int(o[0] + 0.5f)));
    }
  }
  return out;
}

class SvgEngine : public QIconEngine {
 public:
  SvgEngine(QString name, SvgLook look) : name_(std::move(name)), look_(look) {}
  QIconEngine* clone() const override { return new SvgEngine(name_, look_); }
  QString key() const override { return QStringLiteral("xlsvg"); }

  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    const QSize sz(std::max(1, size.width()), std::max(1, size.height()));
    const quint64 k = (quint64(sz.width()) << 32) | (quint64(sz.height()) << 8) | (quint64(mode) << 2) | quint64(state);
    if (const auto it = cache_.constFind(k); it != cache_.constEnd()) return it.value();
    const bool disabled = mode == QIcon::Disabled;
    const bool on = state == QIcon::On;
    const QColor ink = on ? look_.activeInk : look_.ink;
    const QColor bg = on ? look_.activeBg : look_.buttonBg;
    const double scale = std::min(sz.width(), sz.height()) / 24.0;
    QImage glyph(sz, QImage::Format_ARGB32_Premultiplied);
    glyph.fill(Qt::transparent);
    {
      QPainter p(&glyph);
      p.setRenderHint(QPainter::Antialiasing, true);
      paintSvgIcon(p, name_, QRectF(QPointF(0, 0), QSizeF(sz)), ink, bg, look_.strokeWidth);
    }
    QImage out(sz, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    if (look_.halo && !disabled) {
      QImage mask = glyph;   // the glyph's alpha in the ink colour, blurred: drawn under the glyph itself
      const int radius = std::max(1, int(std::lround(1.5 * scale * 24.0 / 20.0)));
      const QImage halo = blurred(mask, radius);
      p.setOpacity(1.0);
      p.drawImage(0, 0, halo);
      p.drawImage(0, 0, halo);   // a thin line's blur is faint: twice gives the strength of the CSS drop-shadow
    }
    p.setOpacity(disabled ? look_.disabledOpacity : 1.0);
    p.drawImage(0, 0, glyph);
    p.end();
    const QPixmap pm = QPixmap::fromImage(out);
    cache_.insert(k, pm);
    return pm;
  }
  void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    QPixmap pm = pixmap(QSize(int(std::lround(rect.width() * dpr)), int(std::lround(rect.height() * dpr))), mode, state);
    pm.setDevicePixelRatio(dpr);
    painter->drawPixmap(rect.topLeft(), pm);
  }
  QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }

 private:
  QString name_;
  SvgLook look_;
  QHash<quint64, QPixmap> cache_;
};

struct ThemeState { bool neon = false, halo = false; };
ThemeState& themeState() { static ThemeState t; return t; }

/** An icon painted at paint time (no cache: a circle is cheaper than a lookup, and the theme can change under it). */
class PaintedEngine : public QIconEngine {
 public:
  using Fn = std::function<void(QPainter&, const QRectF&, QIcon::Mode, QIcon::State)>;
  PaintedEngine(Fn fn, double grow) : fn_(std::move(fn)), grow_(grow) {}
  QIconEngine* clone() const override { return new PaintedEngine(fn_, grow_); }
  void paint(QPainter* p, const QRect& r, QIcon::Mode m, QIcon::State s) override { fn_(*p, QRectF(r), m, s); }
  QPixmap pixmap(const QSize& size, QIcon::Mode m, QIcon::State s) override {
    const QSize sz(std::max(1, int(std::lround(size.width() * grow_))), std::max(1, int(std::lround(size.height() * grow_))));
    QPixmap pm(sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    fn_(p, QRectF(QPointF(0, 0), QSizeF(sz)), m, s);
    return pm;
  }
  QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
 private:
  Fn fn_;
  double grow_;
};

}  // namespace

void setSvgTheme(bool neon, bool halo) { themeState().neon = neon; themeState().halo = halo; }
bool svgThemeNeon() { return themeState().neon; }

void paintSwatch(QPainter& p, const QRectF& r, const QColor& c, bool on, bool enabled) {
  const bool neon = themeState().neon;
  const double u = 1.0;   // the circle is 30 px whatever the button size: the rows shrink by tier through the button, not the colour
  const QPointF ctr = r.center();
  const QRectF circle(ctr.x() - 15 * u, ctr.y() - 15 * u, 30 * u, 30 * u);
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  if (!enabled) p.setOpacity(0.35);
  if (neon && themeState().halo) {   // box-shadow: 0 0 6px currentColor (10px white glow when checked)
    for (int i = 4; i >= 1; i--) { QColor g = on ? QColor(Qt::white) : c; g.setAlphaF((on ? 0.10 : 0.07) * (5 - i)); p.setPen(Qt::NoPen); p.setBrush(g); p.drawEllipse(circle.adjusted(-i * 1.1, -i * 1.1, i * 1.1, i * 1.1)); }
  }
  p.setBrush(c);
  p.setPen(QPen(on ? QColor(Qt::white) : neon ? QColor(0x20, 0x10, 0x4a) : QColor(0x66, 0x66, 0x66), 2));
  p.drawEllipse(circle.adjusted(1, 1, -1, -1));
  if (on && !neon) { p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(0x2f, 0x6f, 0xd0), 1)); p.drawEllipse(circle.adjusted(-0.5, -0.5, 0.5, 0.5)); }
  p.restore();
}

void paintColourBox(QPainter& p, const QRectF& r, const QColor& c) {
  const QRectF box(r.center().x() - 13, r.center().y() - 12, 26, 24);
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(themeState().neon ? QColor(0x3d, 0x2a, 0x7a) : QColor(0x55, 0x55, 0x55), 1));
  p.setBrush(c);
  p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
  p.restore();
}

QIcon swatchIcon(const QColor& c) {
  return QIcon(new PaintedEngine([c](QPainter& p, const QRectF& r, QIcon::Mode m, QIcon::State s) {
    const double k = std::min(r.width(), r.height()) / 30.0;
    p.save();
    p.translate(r.center());
    p.scale(k, k);
    paintSwatch(p, QRectF(0, 0, 0, 0), c, s == QIcon::On, m != QIcon::Disabled);
    p.restore();
  }, 1.0));
}

QIcon colourBoxIcon(const QColor& c) {
  return QIcon(new PaintedEngine([c](QPainter& p, const QRectF& r, QIcon::Mode, QIcon::State) {
    const double k = std::min(r.width(), r.height()) / 26.0;
    p.save();
    p.translate(r.center());
    p.scale(k, k);
    paintColourBox(p, QRectF(0, 0, 0, 0), c);
    p.restore();
  }, 1.0));
}

QIcon sizeDotIcon(int px) {
  return QIcon(new PaintedEngine([px](QPainter& p, const QRectF& r, QIcon::Mode m, QIcon::State s) {
    const bool neon = themeState().neon;
    const QColor ink = s == QIcon::On ? QColor(Qt::white) : neon ? QColor(0x05, 0xe1, 0xff) : QColor(0xd8, 0xd8, 0xd8);
    const double d = px * std::min(r.width(), r.height()) / 20.0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m == QIcon::Disabled) p.setOpacity(0.35);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawEllipse(r.center(), d / 2, d / 2);
    p.restore();
  }, 1.0));
}

QString neonizeCss(QString css) {
  if (!themeState().neon) return css;
  for (const char* blue : {"#2f6fd0", "#4a8af0", "#6aa0ff"}) css.replace(QLatin1String(blue), QLatin1String("#ff2a9d"));
  return css;
}

bool hasSvgIcon(const QString& name) { return findBody(name) != nullptr; }
QString svgIconBody(const QString& name) { const char* b = findBody(name); return b ? QString::fromUtf8(b) : QString(); }

void paintSvgIcon(QPainter& p, const QString& name, const QRectF& target, const QColor& ink, const QColor& buttonBg, double strokeWidth) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.translate(target.topLeft());
  p.scale(target.width() / 24.0, target.height() / 24.0);
  for (const Shape& sh : shapesOf(name)) {
    p.save();
    p.setTransform(sh.xf, true);
    p.setOpacity(p.opacity() * sh.opacity);
    const double sw = sh.strokeWidth > 0 ? sh.strokeWidth : strokeWidth;
    QPen pen(ink, sw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (!sh.dash.isEmpty()) {
      QVector<qreal> d;
      for (qreal v : sh.dash) d << v / sw;   // Qt measures dashes in pen widths, SVG in user units
      if (d.size() % 2) d += d;
      pen.setDashPattern(d);
    }
    if (sh.fill == Fill::Ink) { QColor f = ink; f.setAlphaF(f.alphaF() * sh.fillOpacity); p.setBrush(f); }
    else if (sh.fill == Fill::ButtonBg) p.setBrush(buttonBg);
    else p.setBrush(Qt::NoBrush);
    p.setPen(pen);
    p.drawPath(sh.path);
    p.restore();
  }
  p.restore();
}

QIcon svgIcon(const QString& name, const SvgLook& look) { return QIcon(new SvgEngine(name, look)); }

QString svgNameForAction(const QString& id) {
  static const std::map<QString, QString> m = {
      {"file.open", "open"}, {"file.save", "save"}, {"edit.undo", "undo"}, {"edit.redo", "redo"}, {"edit.cut", "cut"}, {"edit.copy", "copy"}, {"edit.paste", "paste"},
      {"edit.duplicate", "dup"}, {"view.sidebarPages", "thumbs"}, {"view.sidebarLayers", "layers"}, {"nav.first", "first"}, {"nav.prev", "prev"}, {"nav.next", "next"},
      {"nav.last", "last"}, {"nav.back", "histBack"}, {"nav.forward", "histFwd"}, {"journal.newPage", "pageAdd"}, {"journal.insertBefore", "pageBefore"},
      {"journal.append", "pageEnd"}, {"journal.delete", "pageDel"}, {"journal.moveUp", "up"}, {"journal.moveDown", "down"}, {"view.zoomOut", "zoomOut"},
      {"view.zoomIn", "zoomIn"}, {"view.fitWidth", "fit"}, {"chrome.theme", "theme"}, {"chrome.paper", "paper"}, {"palette.menu", "palette"}, {"glow.toggle", "glow"},
      {"tool.pen", "pen"}, {"tool.highlighter", "highlighter"}, {"tool.eraser", "eraser"}, {"tool.eraserPart", "eraserPart"}, {"tool.select", "select"},
      {"tool.lasso", "lasso"}, {"tool.vspace", "vspace"}, {"tool.hand", "hand"}, {"tool.shape", "shape"}, {"tool.recognizer", "recog"}, {"tool.text", "text"},
      {"tool.markdown", "markdown"}, {"tool.latex", "latex"}, {"tool.image", "image"}, {"tool.laser", "laser"}, {"tool.fill", "fill"}, {"tool.plane", "plane"},
      {"tool.space3", "space3"}, {"tool.numline", "numline"}, {"tool.venn", "venn"}, {"tool.figure", "figure"}, {"sel.front", "front"}, {"sel.back", "back"},
      {"sel.rotL", "rotL"}, {"sel.rotR", "rotR"}, {"sel.flipH", "flipH"}, {"sel.flipV", "flipV"}, {"pen.settings", "tablet"}, {"edit.find", "find"},
  };
  const auto it = m.find(id);
  return it == m.end() ? QString() : it->second;
}

QColor neonInkFor(const QString& n) {
  const QColor pink(0xff, 0x2a, 0x9d), cyan(0x05, 0xe1, 0xff), purple(0xb9, 0x67, 0xff), yellow(0xff, 0xe4, 0x5e), green(0x2d, 0xff, 0xb2), orange(0xff, 0x8a, 0x3d),
      red(0xff, 0x4d, 0x6d), blue(0x4d, 0x7c, 0xff);
  auto in = [&](std::initializer_list<const char*> l) { for (const char* s : l) if (n == QLatin1String(s)) return true; return false; };
  if (in({"open", "save", "highlighter", "theme", "glow"})) return yellow;
  if (in({"undo", "redo", "histBack", "histFwd"})) return orange;
  if (in({"cut", "copy", "paste", "dup", "pageAdd", "pageBefore", "pageEnd", "check", "fill"})) return green;
  if (in({"thumbs", "layers", "paper", "shape", "recog", "text", "latex", "image", "fillOpts", "planeOpts"})) return purple;
  if (in({"pageDel", "laser"})) return red;
  if (in({"zoomIn", "zoomOut", "fit", "front", "back", "rotL", "rotR", "flipH", "flipV", "orient", "space3"})) return blue;
  if (in({"pen", "eraser", "eraserPart", "palette"})) return pink;
  return cyan;   // first/prev/next/last/up/down, select/lasso/vspace/hand, plane and the neon default for a toolbar button
}

}  // namespace chrome
