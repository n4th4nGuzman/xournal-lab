// Shape tool: drag a line, rectangle, ellipse or arrow; Shift constrains it (15-degree steps, square, circle). The preview is drawn in the overlay and
// the result is an ordinary pen stroke, as in the Electron app (editor.ts "shape"), so Xournal++ opens it as any other stroke.
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "texttools.h"
#include <QMenu>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace texttools {

namespace {

class ShapeTool : public Tool {
 public:
  using Tool::Tool;
  QCursor cursor() const override { return Qt::CrossCursor; }

  void deactivate() override { cancel(); }
  void pointerCancel() override { cancel(); }

  void pointerDown(const PointerEvent& e) override {
    if (e.page < 0) return;
    page_ = e.page;
    a_ = e.pos;
    pts_ = {a_.x(), a_.y(), a_.x(), a_.y()};
    dragging_ = true;
    shown_ = QRectF();
  }

  void pointerMove(const PointerEvent& e) override {
    if (!dragging_) return;
    update(e);
    DocumentView* v = view();
    if (!v) return;
    const QRectF now = box();
    v->invalidateOverlay(page_, shown_.united(now));
    shown_ = now;
  }

  void pointerUp(const PointerEvent& e) override {
    if (!dragging_) return;
    update(e);
    dragging_ = false;
    DocumentView* v = view();
    if (v) v->invalidateOverlay(page_, shown_.united(box()));
    shown_ = QRectF();
    const Rect b = bboxOfPoints(pts_);
    if (std::max(b[2] - b[0], b[3] - b[1]) < 1) { pts_.clear(); return; }   // a click, not a drag
    commit();
  }

  void paintOverlay(QPainter& p, const OverlayInfo& info) override {
    if (!dragging_ || info.page != page_ || pts_.size() < 4) return;
    QPainterPath path;
    path.moveTo(pts_[0], pts_[1]);
    for (size_t i = 2; i + 1 < pts_.size(); i += 2) path.lineTo(pts_[i], pts_[i + 1]);
    QColor c(editor().style().color);
    QPen pen(c, editor().style().penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (editor().style().lineStyle != QLatin1String("plain")) pen.setStyle(editor().style().lineStyle == QLatin1String("dot") ? Qt::DotLine : Qt::DashLine);   // a hint; the saved stroke has the exact pattern
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
  }

 private:
  void update(const PointerEvent& e) {
    pts_ = shapePoints(texttools::state().shape, a_.x(), a_.y(), e.pos.x(), e.pos.y(), e.shift(), editor().style().penWidth);
  }
  QRectF box() const {
    const Rect b = bboxOfPoints(pts_);
    if (!std::isfinite(b[0])) return QRectF();
    const double pad = editor().style().penWidth + 4;
    return QRectF(QPointF(b[0] - pad, b[1] - pad), QPointF(b[2] + pad, b[3] + pad));
  }
  void cancel() {
    if (!dragging_) return;
    dragging_ = false;
    if (DocumentView* v = view()) v->invalidateOverlay(page_, shown_.united(box()));
    pts_.clear();
  }

  void commit() {
    Editor& ed = editor();
    const Style& st = ed.style();
    if (page_ < 0 || page_ >= ed.pageCount()) return;
    const int layer = ed.activeLayer(page_);
    if (!ed.layerVisible(page_, layer)) {
      ed.setLayerVisible(page_, layer, true);
      ed.notify(QStringLiteral("La capa activa estaba oculta: se ha vuelto a mostrar"));
    }
    Stroke s;
    s.attrs = {{"tool", "pen"}, {"color", st.color + "ff"}, {"capStyle", "round"}, {"xlShape", "1"}};   // ordinary pen strokes; xlShape keeps their corners sharp when drawn
    s.widths = {st.penWidth};
    s.pts = pts_;
    applyLineStyle(s.attrs, st, true);
    if (st.glowOn) {
      s.attrs.emplace_back("glow", QString::number(std::round(st.glowSize * 100) / 100));
      s.attrs.emplace_back("glowAlpha", QString::number(std::round(st.glowIntensity * 100) / 100));
    }
    pts_.clear();
    ed.commitAdd(page_, layer, Element(std::move(s)), QStringLiteral("Forma"));
  }

  int page_ = 0;
  QPointF a_;
  std::vector<double> pts_;
  bool dragging_ = false;
  QRectF shown_;
};

// the four kinds, as a small panel behind the caret of the toolbar button (the select of the Electron toolbar)
QWidget* shapePanel(QWidget* parent, Editor&) {
  auto* w = new QWidget(parent);
  auto* lay = new QVBoxLayout(w);
  lay->setContentsMargins(6, 6, 6, 6);
  lay->setSpacing(4);
  struct Row { ShapeKind kind; const char* label; const char* icon; };
  const Row rows[] = {{ShapeKind::Rect, "Rectángulo", "shape-rect"}, {ShapeKind::Ellipse, "Elipse", "shape-ellipse"}, {ShapeKind::Line, "Línea", "shape-line"}, {ShapeKind::Arrow, "Flecha", "shape-arrow"},
                       {ShapeKind::DoubleArrow, "Flecha doble", "shape-darrow"}, {ShapeKind::Axes, "Sistema de coordenadas (ejes)", "shape-axes"}};
  for (const Row& r : rows) {
    auto* b = new QToolButton(w);
    b->setText(QString::fromUtf8(r.label));
    b->setIcon(icon(r.icon));
    b->setIconSize(QSize(28, 28));
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setCheckable(true);
    b->setAutoRaise(false);
    b->setMinimumHeight(44);
    b->setMinimumWidth(170);
    b->setChecked(state().shape == r.kind);
    b->setToolTip(QStringLiteral("Mayús = cuadrado, círculo o ángulos de 15°"));
    QObject::connect(b, &QToolButton::clicked, w, [kind = r.kind, w, b] {
      state().shape = kind;
      for (auto* o : w->findChildren<QToolButton*>()) o->setChecked(false);
      b->setChecked(true);
      if (QMenu* m = qobject_cast<QMenu*>(w->parentWidget())) m->close();
    });
    lay->addWidget(b);
  }
  return w;
}

}  // namespace

void registerShapeTool(Editor& ed) {
  ToolInfo i;
  i.id = "shape"; i.label = "Formas"; i.tooltip = "Formas: arrastra. Mayús = cuadrado, círculo o ángulos de 15°";
  i.shortcut = QKeySequence("G");
  i.icon = icon("shape");
  i.inserter = true;
  i.order = 20;
  i.factory = [](Editor& e) { return std::make_unique<ShapeTool>(e); };
  i.options = shapePanel;
  ed.registerTool(i);
}

}  // namespace texttools
