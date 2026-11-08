// Tools "numline" (N) and "venn" (J): drag to draw, the object is made on release and its panel opens. A click without a drag inserts nothing.
// Port of mathOfGesture / drawMathPreview / finishMath of editor.ts, and of the toolbar entries of index.html.
#include "documentview.h"
#include "editor.h"
#include "mathstate.h"
#include "mathtools.h"
#include "pagerender.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace mathobj {
namespace {

class MathTool final : public Tool {
 public:
  MathTool(Editor& ed, bool line) : Tool(ed), line_(line) {}

  QCursor cursor() const override { return Qt::CrossCursor; }
  void deactivate() override { cancel(); }

  void pointerDown(const PointerEvent& e) override {
    if (e.page < 0) return;
    page_ = e.page;
    a_ = b_ = e.pos;
    zoom_ = e.zoom;
    shift_ = e.shift();
    dragging_ = true;
    rebuildPreview();
  }

  void pointerMove(const PointerEvent& e) override {
    if (!dragging_) return;
    const QRectF old = dirty_;
    b_ = e.pos;
    zoom_ = e.zoom;
    shift_ = e.shift();
    rebuildPreview();
    if (view()) view()->invalidateOverlay(page_, old.united(dirty_));
  }

  void pointerUp(const PointerEvent& e) override {
    if (!dragging_) return;
    b_ = e.pos;
    zoom_ = e.zoom;
    shift_ = e.shift();
    dragging_ = false;
    preview_.clear();
    if (view()) view()->invalidateOverlay(page_, dirty_);
    // a click only is not an insertion (it would drop objects by accident): it takes a drag to say where and how big
    if (tiny()) { editor().notify("Arrastra para dibujarlo (un clic solo no inserta nada)"); return; }
    createMath(editor(), page_, specOfGesture(), line_ ? "numline" : "venn");
  }

  void pointerCancel() override { cancel(); }

  void paintOverlay(QPainter& p, const OverlayInfo& info) override {
    if (!dragging_ || info.page != page_) return;
    RenderOptions o;
    o.scale = info.zoom;
    o.glow = false;
    o.background = false;
    for (const Element& el : preview_) renderElement(p, el, o);
  }

 private:
  bool tiny() const { return std::abs(b_.x() - a_.x()) * zoom_ < 12 && std::abs(b_.y() - a_.y()) * zoom_ < 12; }

  /** The line or diagram the drag describes: a click makes one of the default size around the point. */
  sets::MathSpec specOfGesture() const {
    const double ax = a_.x(), ay = a_.y(), bx = b_.x(), by = b_.y();
    if (line_) {
      V2 a{ax, ay}, b{bx, by};
      if (tiny() || std::hypot(bx - ax, by - ay) < 40) { a = {ax - 200, ay}; b = {ax + 200, ay}; }
      else if (shift_) {
        const double len = std::hypot(bx - ax, by - ay), step = M_PI / 12, ang = std::round(std::atan2(by - ay, bx - ax) / step) * step;
        b = {ax + std::cos(ang) * len, ay + std::sin(ang) * len};
      }
      sets::LineSpec s = state().line;
      s.pts = {a[0], a[1], b[0], b[1]};
      return s;
    }
    V2 a{std::min(ax, bx), std::min(ay, by)}, b{std::max(ax, bx), std::max(ay, by)};
    if (tiny() || b[0] - a[0] < 120 || b[1] - a[1] < 90) { a = {ax - 200, ay - 130}; b = {ax + 200, ay + 130}; }
    sets::VennSpec s = sets::defaultVenn(state().venn.k, a, b);
    s.names = state().venn.names;
    s.expr = state().venn.expr;
    s.universe = state().venn.universe;
    s.dense = state().venn.dense;
    s.lists = state().venn.lists;
    return s;
  }

  void rebuildPreview() {
    GenStyle st = styleFromPen(editor());
    st.glow.reset();
    preview_ = sets::buildMath("preview", sets::sanitizeMath(specOfGesture()), st);
    QRectF r;
    for (const Element& el : preview_) r = r.united(elementRenderBounds(el));
    dirty_ = r.adjusted(-4, -4, 4, 4);
  }

  void cancel() {
    if (!dragging_) return;
    dragging_ = false;
    preview_.clear();
    if (view()) view()->invalidateOverlay(page_, dirty_);
  }

  bool line_;
  int page_ = 0;
  QPointF a_, b_;
  double zoom_ = 1;
  bool shift_ = false, dragging_ = false;
  std::vector<Element> preview_;
  QRectF dirty_;
};

}  // namespace

QIcon toolIcon(const QString& n) {
  QIcon icon;
  for (int px : {24, 32, 48}) {
    QPixmap pm(px * 2, px * 2);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(2);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 24.0, px / 24.0);
    const QColor ink(0xe6, 0xe6, 0xe6);
    p.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (n == "numline") {
      p.drawLine(QPointF(3, 16), QPointF(21, 16));
      for (double x : {5.0, 12.0, 19.0}) p.drawLine(QPointF(x, 13), QPointF(x, 19));
      p.drawEllipse(QPointF(8, 7), 1.8, 1.8);
      p.drawLine(QPointF(10, 7), QPointF(15.5, 7));
      p.setBrush(ink);
      p.drawEllipse(QPointF(17.3, 7), 1.8, 1.8);
    } else if (n == "venn") {
      p.drawEllipse(QPointF(9, 12), 5.5, 5.5);
      p.drawEllipse(QPointF(15, 12), 5.5, 5.5);
    } else {   // fill: a tilted bucket and a drop
      QPainterPath b;
      b.moveTo(6, 12); b.lineTo(12, 5); b.lineTo(19, 12); b.lineTo(13, 19); b.closeSubpath();
      p.drawPath(b);
      p.drawLine(QPointF(6, 12), QPointF(19, 12));
      p.drawLine(QPointF(4, 21), QPointF(10, 21));
      QPainterPath d;
      d.moveTo(20.5, 14.5); d.cubicTo(21.5, 16.1, 22, 17, 22, 17.8); d.cubicTo(22, 19.6, 19, 19.6, 19, 17.8); d.cubicTo(19, 17, 19.5, 16.1, 20.5, 14.5);
      p.drawPath(d);
    }
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

void registerMathTools(Editor& ed) {
  ToolInfo n;
  n.id = "numline"; n.label = "Recta real (intervalos y puntos)";
  n.tooltip = "Recta real: arrastra para dibujar una recta numérica; escribe conjuntos como (−2, 3] ∪ {5} y se marcan con círculos abiertos o cerrados. Con ella seleccionada, los círculos de sus extremos se arrastran";
  n.shortcut = QKeySequence("N");
  n.icon = toolIcon("numline");
  n.inserter = true;
  n.order = 45;
  n.factory = [](Editor& e) { return std::make_unique<MathTool>(e, true); };
  n.options = [](QWidget* parent, Editor& e) { return makeMathPanel(parent, e, "line"); };
  ed.registerTool(n);

  ToolInfo v;
  v.id = "venn"; v.label = "Diagrama de conjuntos (Venn)";
  v.tooltip = "Diagrama de conjuntos: arrastra un recuadro (el universo) con 2 o 3 círculos; escribe qué región sombrear (A ∩ B′, A ∖ B…) y los elementos de cada conjunto";
  v.shortcut = QKeySequence("J");
  v.icon = toolIcon("venn");
  v.inserter = true;
  v.order = 46;
  v.factory = [](Editor& e) { return std::make_unique<MathTool>(e, false); };
  v.options = [](QWidget* parent, Editor& e) { return makeMathPanel(parent, e, "venn"); };
  ed.registerTool(v);
}

}  // namespace mathobj
