// Tool "fill" (U): a click floods the area around it; a dragged line or curve fills from it toward one side; kind Riemann stands rectangles on the dragged
// base. The work is core/fill.h runFill; this is the gesture, the green preview (drawFillPreview of editor.ts) and the one undo step.
#include "documentview.h"
#include "editor.h"
#include "mathstate.h"
#include "mathtools.h"
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace mathobj {
namespace {

/** The end of a straight base: Shift steps its angle by 15 degrees; with the magnet, a base within 3 degrees of horizontal or vertical becomes exactly so. */
QPointF straightEnd(QPointF a, QPointF b, bool shift, bool magnet) {
  const double dx = b.x() - a.x(), dy = b.y() - a.y(), len = std::hypot(dx, dy);
  if (len < 1e-6) return b;
  double ang = std::atan2(dy, dx);
  if (shift) { const double step = M_PI / 12; ang = std::round(ang / step) * step; return {a.x() + std::cos(ang) * len, a.y() + std::sin(ang) * len}; }
  if (magnet) {
    for (double target : {0.0, M_PI / 2, M_PI, -M_PI / 2}) {
      double d = std::fmod(std::abs(ang - target), 2 * M_PI);
      if (d > M_PI) d = 2 * M_PI - d;
      if (d < 3 * M_PI / 180) return {a.x() + std::cos(target) * len, a.y() + std::sin(target) * len};
    }
  }
  return b;
}

class FillTool final : public Tool {
 public:
  using Tool::Tool;
  QCursor cursor() const override { return Qt::CrossCursor; }
  void deactivate() override { cancel(); }

  void pointerDown(const PointerEvent& e) override {
    if (e.page < 0) return;
    page_ = e.page;
    pts_ = {e.pos.x(), e.pos.y()};
    zoom_ = e.zoom;
    shift_ = e.shift();
    dragging_ = true;
    box_ = boxNow();
    invalidate(box_);
  }

  void pointerMove(const PointerEvent& e) override {
    if (!dragging_) return;
    zoom_ = e.zoom;
    shift_ = e.shift();
    const QRectF old = box_;
    const size_t n = pts_.size();
    if (std::hypot(e.pos.x() - pts_[n - 2], e.pos.y() - pts_[n - 1]) >= 1.5 / zoom_) { pts_.push_back(e.pos.x()); pts_.push_back(e.pos.y()); }
    box_ = boxNow();
    invalidate(old.united(box_));
  }

  void pointerUp(const PointerEvent& e) override {
    if (!dragging_) return;
    dragging_ = false;
    shift_ = e.shift();
    zoom_ = e.zoom;
    const size_t n = pts_.size();
    if (std::hypot(e.pos.x() - pts_[n - 2], e.pos.y() - pts_[n - 1]) > 0) { pts_.push_back(e.pos.x()); pts_.push_back(e.pos.y()); }
    invalidate(box_);
    run();
  }

  void pointerCancel() override { cancel(); }

  void paintOverlay(QPainter& p, const OverlayInfo& info) override {
    if (!dragging_ || info.page != page_) return;
    const double z = info.zoom;
    const FillOptions& o = state().fill;
    const std::vector<double> q = shown();
    const size_t n = q.size() / 2;
    const QColor green("#39d353");
    p.save();
    p.setPen(QPen(green, 2 / z, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(green);
    if (n < 2) {
      p.drawEllipse(QPointF(q[0], q[1]), 4 / z, 4 / z);
      p.restore();
      return;
    }
    QPolygonF poly;
    for (size_t i = 0; i < n; i++) poly << QPointF(q[2 * i], q[2 * i + 1]);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(poly);
    p.setBrush(green);
    const double sgn = o.side == FillSide::Left ? 1 : -1, gapPx = 22 / z;
    double acc = 0;
    for (size_t i = 1; i < n; i++) {   // ticks on the side that will be filled
      const double dx = q[2 * i] - q[2 * i - 2], dy = q[2 * i + 1] - q[2 * i - 1], len = std::hypot(dx, dy);
      if (len < 1e-9) continue;
      acc += len;
      if (acc < gapPx) continue;
      acc = 0;
      const double nx = dy / len * sgn, ny = -dx / len * sgn, x = q[2 * i], y = q[2 * i + 1];
      p.drawLine(QPointF(x, y), QPointF(x + nx * 9 / z, y + ny * 9 / z));
    }
    size_t k = n - 2;   // arrow head along the last stretch of any real length
    const double ex = q[2 * n - 2], ey = q[2 * n - 1];
    while (k > 0 && std::hypot(ex - q[2 * k], ey - q[2 * k + 1]) < 6 / z) k--;
    const double ang = std::atan2(ey - q[2 * k + 1], ex - q[2 * k]);
    QPolygonF head;
    head << QPointF(ex, ey) << QPointF(ex - std::cos(ang - 0.45) * 11 / z, ey - std::sin(ang - 0.45) * 11 / z) << QPointF(ex - std::cos(ang + 0.45) * 11 / z, ey - std::sin(ang + 0.45) * 11 / z);
    p.drawPolygon(head);
    p.restore();
  }

 private:
  /** What the preview draws: with a Riemann base on a straight line, that straight line (what you see is what you get). */
  std::vector<double> shown() const {
    const FillOptions& o = state().fill;
    if (o.kind == FillKind::Riemann && o.base == RiemannBase::Line && pts_.size() >= 4) {
      const QPointF end = straightEnd({pts_[0], pts_[1]}, {pts_[pts_.size() - 2], pts_[pts_.size() - 1]}, shift_, o.magnet);
      return {pts_[0], pts_[1], end.x(), end.y()};
    }
    return pts_;
  }

  QRectF boxNow() const {
    const std::vector<double> q = shown();
    double x0 = q[0], y0 = q[1], x1 = q[0], y1 = q[1];
    for (size_t i = 0; i + 1 < q.size(); i += 2) { x0 = std::min(x0, q[i]); x1 = std::max(x1, q[i]); y0 = std::min(y0, q[i + 1]); y1 = std::max(y1, q[i + 1]); }
    const double m = 16 / zoom_;
    return QRectF(QPointF(x0 - m, y0 - m), QPointF(x1 + m, y1 + m));
  }

  void invalidate(const QRectF& r) const { if (view()) view()->invalidateOverlay(page_, r); }

  void cancel() {
    if (!dragging_) return;
    dragging_ = false;
    invalidate(box_);
  }

  void run() {
    Editor& ed = editor();
    if (page_ < 0 || page_ >= ed.pageCount()) return;
    const int layer = ed.activeLayer(page_);
    if (!ed.layerVisible(page_, layer)) { ed.notify("La capa activa está oculta: muéstrala para rellenar"); return; }
    const Page& pg = ed.doc().pages[size_t(page_)];
    FillRequest req;
    req.opt = state().fill;
    req.penColor = ed.style().color;
    req.penWidth = ed.style().penWidth;
    req.zoom = zoom_;
    req.devicePixelRatio = view() ? view()->devicePixelRatioF() : 1;
    req.force = shift_;
    const FillResult r = runFill(pg.layers[size_t(layer)], pg.width(), pg.height(), pts_, req);
    for (const QString& n : r.notices) ed.notify(n);
    if (!r.ok) return;

    // Under the ink, above earlier fills; the outlines and the kept line go on top. Positions are those applyFill reports, as the layer changes only through Editor.
    const int page = page_, at = int(std::min(r.insertAt, pg.layers[size_t(layer)].elements.size()));
    ed.insertElement(page, layer, at, r.added[0]);
    std::vector<int> positions{at};
    for (size_t i = 1; i < r.added.size(); i++) positions.push_back(ed.addElement(page, layer, r.added[i]));
    auto added = std::make_shared<std::vector<Element>>(r.added);
    Action a;
    a.label = r.riemann ? "Rectángulos de Riemann" : "Relleno";
    a.page = page;
    a.undo = [&ed, page, layer, positions] { for (size_t k = positions.size(); k-- > 0;) ed.takeElement(page, layer, positions[k]); };
    a.redo = [&ed, page, layer, at, added] {
      ed.insertElement(page, layer, at, (*added)[0]);
      for (size_t i = 1; i < added->size(); i++) ed.addElement(page, layer, (*added)[i]);
    };
    ed.pushAction(std::move(a));
    state().lastFill = r.stats;
    state().lastRiemann = r.riemann;
  }

  int page_ = -1;
  std::vector<double> pts_;
  double zoom_ = 1;
  bool shift_ = false, dragging_ = false;
  QRectF box_;
};

}  // namespace

void registerFillTool(Editor& ed) {
  ToolInfo f;
  f.id = "fill"; f.label = "Relleno";
  f.tooltip = "Relleno: clic para rellenar una zona; arrastra una línea o curva para rellenar a partir de ella, hacia el lado elegido. Se detiene en los trazos de la capa activa";
  f.shortcut = QKeySequence("U");
  f.icon = toolIcon("fill");
  f.order = 30;
  f.factory = [](Editor& e) { return std::make_unique<FillTool>(e); };
  f.options = [](QWidget* parent, Editor& e) { return makeFillPanel(parent, e); };
  ed.registerTool(f);
}

}  // namespace mathobj
