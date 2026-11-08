// The tools "plane" (C), "space3" (D) and "figure" (F). Gestures and previews of editor.ts (drawPlanePreview/finishPlane/planeFrame, finishSpace3,
// finishFigure/endFigure/figPending), with the latest behaviour: a click without a drag inserts nothing for planes and 3D spaces (a drag says where and
// how big). Previews are painted by paintOverlay from the same builders that make the object, so what is previewed is what is inserted; the object is
// created through Hub::create (swapGenerated + one undo step + afterCreate + select).
#include "documentview.h"
#include "editor.h"
#include "figures.h"
#include "hub.h"
#include "pagerender.h"
#include "objects.h"
#include "plane.h"
#include "space3.h"
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

namespace xlobjects {
namespace {

constexpr double kMinDragPx = 12;   // below this a press-release is a click

/** Preview machinery shared by the three tools: the elements being previewed and the rectangle they cover, so only that area repaints. */
class ObjectTool : public Tool {
 public:
  using Tool::Tool;
  void deactivate() override { clearPreview(); }

  void paintOverlay(QPainter& p, const OverlayInfo& info) override {
    if (info.page != prevPage_ || prev_.empty()) return;
    RenderOptions o;
    o.clip = info.visible;
    o.scale = info.zoom;
    o.glow = false;       // the preview is plain; the glow shows when the object is made
    o.background = false;
    for (const Element& e : prev_) renderElement(p, e, o);
    paintExtras(p, info);
  }

 protected:
  Hub& hub() const { return Hub::of(editor()); }

  virtual void paintExtras(QPainter&, const OverlayInfo&) {}

  void showPreview(int page, std::vector<Element> els, const QRectF& extra = QRectF()) {
    const int oldPage = prevPage_;
    const QRectF oldRect = prevRect_;
    QRectF r = extra;
    for (const Element& e : els) r = r.united(elementRenderBounds(e));
    prev_ = std::move(els);
    prevPage_ = page;
    prevRect_ = r.adjusted(-6, -6, 6, 6);
    if (DocumentView* v = view()) {
      if (oldPage >= 0) v->invalidateOverlay(oldPage, oldPage == page ? oldRect.united(prevRect_) : oldRect);
      if (oldPage != page) v->invalidateOverlay(page, prevRect_);
    }
  }

  void clearPreview() {
    if (prevPage_ < 0) return;
    const int page = prevPage_;
    const QRectF r = prevRect_;
    prev_.clear();
    prevPage_ = -1;
    prevRect_ = QRectF();
    if (DocumentView* v = view()) v->invalidateOverlay(page, r);
  }

  static PlaneStyle plain(PlaneStyle s) { s.glow.reset(); return s; }

  std::vector<Element> prev_;
  int prevPage_ = -1;
  QRectF prevRect_;
};

/** A press-drag-release from a to b. Plane and 3D space share it. */
class DragTool : public ObjectTool {
 public:
  using ObjectTool::ObjectTool;
  void pointerDown(const PointerEvent& e) override {
    if (e.page < 0) return;
    page_ = e.page;
    a_ = b_ = {e.pos.x(), e.pos.y()};
    active_ = true;
  }
  void pointerMove(const PointerEvent& e) override {
    if (!active_) return;
    b_ = {e.pos.x(), e.pos.y()};
    preview(e.zoom);
  }
  void pointerUp(const PointerEvent& e) override {
    if (!active_) return;
    active_ = false;
    b_ = {e.pos.x(), e.pos.y()};
    clearPreview();
    if (!makes(e.zoom)) {
      // a click only is not an insertion (it would drop objects by accident): it takes a drag to say where and how big
      editor().notify(QStringLiteral("Arrastra para dibujarlo (un clic solo no inserta nada)"));
      return;
    }
    commit(e.zoom);
  }
  void pointerCancel() override { active_ = false; clearPreview(); }
  void deactivate() override { active_ = false; ObjectTool::deactivate(); }

 protected:
  virtual bool makes(double zoom) const = 0;
  virtual void preview(double zoom) = 0;
  virtual void commit(double zoom) = 0;

  bool tiny(double zoom) const { return std::abs(b_[0] - a_[0]) * zoom < kMinDragPx && std::abs(b_[1] - a_[1]) * zoom < kMinDragPx; }

  int page_ = -1;
  V2 a_{0, 0}, b_{0, 0};
  bool active_ = false;
};

// ---- plane ----------------------------------------------------------------------------------------------------------------------------------
class PlaneTool final : public DragTool {
 public:
  using DragTool::DragTool;

 protected:
  /** With square cells the plane keeps the proportions of its range and grows from where the drag began, toward the pointer, by as much as the longer
   *  side of the drag asks; otherwise it is the dragged rectangle. */
  Frame frame() const {
    const PlaneSpec spec = sanitizePlane(hub().plane);
    const double dx = b_[0] - a_[0], dy = b_[1] - a_[1];
    if (!spec.square) return {std::min(a_[0], b_[0]), std::min(a_[1], b_[1]), std::max(a_[0], b_[0]), std::max(a_[1], b_[1])};
    const double rx = spec.xmax - spec.xmin, ry = spec.ymax - spec.ymin;
    const double k = std::max(std::abs(dx) / rx, std::abs(dy) / ry);
    const double w = rx * k, h = ry * k, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    return {std::min(a_[0], a_[0] + sx * w), std::min(a_[1], a_[1] + sy * h), std::max(a_[0], a_[0] + sx * w), std::max(a_[1], a_[1] + sy * h)};
  }
  bool makes(double zoom) const override {
    if (tiny(zoom)) return false;
    // without square cells a drag along one axis only would be a plane with no height: not an insertion either
    return sanitizePlane(hub().plane).square || (std::abs(b_[0] - a_[0]) * zoom >= kMinDragPx && std::abs(b_[1] - a_[1]) * zoom >= kMinDragPx);
  }
  void preview(double zoom) override {
    if (!makes(zoom)) { clearPreview(); return; }
    const Frame f = frame();
    frame_ = QRectF(f.x0, f.y0, f.x1 - f.x0, f.y1 - f.y0);
    showPreview(page_, buildPlane(QStringLiteral("preview"), sanitizePlane(hub().plane), f, plain(hub().penStyle())), frame_);
  }
  void commit(double) override {
    Hub& h = hub();
    h.create(page_, QStringLiteral("plane"), buildPlane(newPlaneId(), sanitizePlane(h.plane), frame(), h.penStyle()), QStringLiteral("Plano cartesiano"));
  }
  void paintExtras(QPainter& p, const OverlayInfo& info) override {
    QPen pen(QColor("#39d353"), 1.0 / info.zoom, Qt::CustomDashLine);
    pen.setDashPattern({4, 3});   // in units of the pen width
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame_);
  }

 private:
  QRectF frame_;
};

// ---- 3D space -------------------------------------------------------------------------------------------------------------------------------
class Space3Tool final : public DragTool {
 public:
  using DragTool::DragTool;

 protected:
  Space3Spec spec() const {
    const Space3Spec base = sanitizeSpace3(hub().space3);
    const auto [x0, y0, x1, y1] = extentOf([&] { Space3Spec s = base; s.o = {0, 0}; return s; }());
    const double w0 = x1 - x0, h0 = y1 - y0;
    const double dx = b_[0] - a_[0], dy = b_[1] - a_[1];
    const double k = std::min(8.0, std::max(0.2, std::max(std::abs(dx) / w0, std::abs(dy) / h0)));
    return placedSpec(base, k, {dx < 0 ? a_[0] - w0 * k : a_[0], dy < 0 ? a_[1] - h0 * k : a_[1]});
  }
  bool makes(double zoom) const override { return !tiny(zoom); }
  void preview(double zoom) override {
    if (!makes(zoom)) { clearPreview(); return; }
    showPreview(page_, buildSpace3(QStringLiteral("preview"), spec(), plain(hub().penStyle())));
  }
  void commit(double) override {
    Hub& h = hub();
    h.create(page_, QStringLiteral("space3"), buildSpace3(newPlaneId(), spec(), h.penStyle()), QStringLiteral("Espacio 3D"));
  }
};

// ---- figure ---------------------------------------------------------------------------------------------------------------------------------
class FigureTool final : public ObjectTool {
 public:
  explicit FigureTool(Editor& ed) : ObjectTool(ed) {
    // the gallery, Esc from elsewhere or undo of a point change the pending clicks outside of this tool: keep the preview in step
    QObject::connect(&Hub::of(ed), &Hub::settingsChanged, &ctx_, [this] { refresh(); });
  }

  /** Leaving the tool finishes the figure being clicked (with too few points it is dropped, with a notice). */
  void deactivate() override {
    active_ = false;
    hub().endFigure(true, false);
    ObjectTool::deactivate();
  }

  void pointerDown(const PointerEvent& e) override {
    if (e.page < 0) return;
    Hub& h = hub();
    if (!h.figPending.empty() && h.figPage != e.page) h.endFigure(false);   // clicks on another page start over
    page_ = e.page;
    a_ = b_ = {e.pos.x(), e.pos.y()};
    shift_ = e.shift();
    active_ = true;
    preview(e.zoom);
  }
  void pointerMove(const PointerEvent& e) override {
    if (!active_) return;
    b_ = {e.pos.x(), e.pos.y()};
    shift_ = e.shift();
    preview(e.zoom);
  }
  void hover(const PointerEvent& e) override {
    Hub& h = hub();
    if (h.figPending.empty() || e.page != h.figPage) return;
    b_ = {e.pos.x(), e.pos.y()};
    page_ = e.page;
    zoom_ = e.zoom;
    preview(e.zoom);   // the next segment follows the pen while it is above the page
  }
  void pointerUp(const PointerEvent& e) override {
    if (!active_) return;
    active_ = false;
    b_ = {e.pos.x(), e.pos.y()};
    shift_ = e.shift();
    Hub& h = hub();
    if (clicksKind(h.figKind)) {   // a click adds a point; a second click on the same place (double click) finishes
      const size_t n = h.figPending.size();
      if (n && std::hypot(h.figPending[n - 2] - b_[0], h.figPending[n - 1] - b_[1]) * e.zoom < 8) { h.endFigure(true); return; }
      h.figPending.push_back(b_[0]);
      h.figPending.push_back(b_[1]);
      h.figPage = page_;
      zoom_ = e.zoom;
      h.changed();   // the preview follows (refresh)
      return;
    }
    clearPreview();
    h.create(page_, QStringLiteral("figure"), buildFigure(newPlaneId(), h.figureOfDrag(a_, b_, shift_, e.zoom), h.penStyle()), QStringLiteral("Figura"));
  }
  void pointerCancel() override { active_ = false; refresh(); }

  bool keyPress(QKeyEvent* k) override {
    Hub& h = hub();
    if (h.figPending.empty()) return false;   // a figure being clicked point by point
    switch (k->key()) {
      case Qt::Key_Return: case Qt::Key_Enter: h.endFigure(true); return true;
      case Qt::Key_Escape: h.endFigure(false); return true;
      case Qt::Key_Backspace: case Qt::Key_Delete: h.undoFigurePoint(); return true;
      default: return false;
    }
  }

 protected:
  void paintExtras(QPainter& p, const OverlayInfo& info) override {
    const Hub& h = hub();
    if (info.page != h.figPage || h.figPending.empty()) return;
    const double r = 5.5 / info.zoom;
    p.setPen(QPen(QColor("#2f6fd0"), 1.6 / info.zoom));
    p.setBrush(Qt::white);
    for (size_t i = 0; i + 1 < h.figPending.size(); i += 2) p.drawEllipse(QPointF(h.figPending[i], h.figPending[i + 1]), r, r);
  }

 private:
  /** Rebuild the preview from the current state (pending clicks plus the last pointer position, or the drag in progress). */
  void refresh() {
    Hub& h = hub();
    if (h.figPending.empty() && !active_) { clearPreview(); return; }
    preview(zoom_);
  }

  void preview(double zoom) {
    Hub& h = hub();
    zoom_ = zoom;
    std::optional<FigureSpec> spec;
    QRectF extra;
    if (clicksKind(h.figKind)) {
      if (!h.figPending.empty()) {
        spec = h.figureOfClicks(b_);
        const auto [lo, hi] = [&] {
          double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
          for (size_t i = 0; i + 1 < h.figPending.size(); i += 2) { x0 = std::min(x0, h.figPending[i]); x1 = std::max(x1, h.figPending[i]); y0 = std::min(y0, h.figPending[i + 1]); y1 = std::max(y1, h.figPending[i + 1]); }
          return std::pair<QPointF, QPointF>{{x0, y0}, {x1, y1}};
        }();
        extra = QRectF(lo, hi).adjusted(-8, -8, 8, 8);
      }
    } else if (active_) {
      spec = h.figureOfDrag(a_, b_, shift_, zoom);
    }
    if (!spec) { clearPreview(); return; }
    showPreview(h.figPending.empty() ? page_ : h.figPage, buildFigure(QStringLiteral("preview"), *spec, plain(h.penStyle())), extra);
  }

  QObject ctx_;
  int page_ = -1;
  V2 a_{0, 0}, b_{0, 0};
  bool active_ = false, shift_ = false;
  double zoom_ = 1;
};

}  // namespace

void registerObjectTools(Editor& ed) {
  {
    ToolInfo i;
    i.id = "plane";
    i.label = "Plano cartesiano";
    i.tooltip = "Plano cartesiano: arrastra un recuadro para dibujar ejes (y cuadrícula) con tu lápiz. Se puede redimensionar con las asas y cambiar de escala en sus opciones";
    i.shortcut = QKeySequence("C");
    i.icon = objectIcon("plane");
    i.inserter = true;
    i.order = 40;
    i.factory = [](Editor& e) { return std::make_unique<PlaneTool>(e); };
    i.options = [](QWidget* parent, Editor& e) -> QWidget* { return makePlanePanel(parent, e); };
    ed.registerTool(std::move(i));
  }
  {
    ToolInfo i;
    i.id = "space3";
    i.label = "Espacio cartesiano 3D";
    i.tooltip = "Espacio cartesiano 3D: arrastra para dibujar los tres ejes. Con el espacio seleccionado, los círculos del final de cada eje se arrastran para cambiar su ángulo (Mayús: conserva el largo)";
    i.shortcut = QKeySequence("D");
    i.icon = objectIcon("space3");
    i.inserter = true;
    i.order = 41;
    i.factory = [](Editor& e) { return std::make_unique<Space3Tool>(e); };
    i.options = [](QWidget* parent, Editor& e) -> QWidget* { return makeSpace3Panel(parent, e); };
    ed.registerTool(std::move(i));
  }
  {
    ToolInfo i;
    i.id = "figure";
    i.label = "Figuras (líneas, curvas, polígonos, sólidos)";
    i.tooltip = "Figuras: segmentos, flechas, curvas, polígonos, ángulos, cotas y sólidos de alambre (cubo, pirámide, cilindro, cono, esfera). Arrastra para dibujar; en polígonos y curvas por puntos, un clic por vértice (doble clic o Intro termina, Retroceso quita el último, Esc cancela). Con la figura seleccionada, los círculos se arrastran para editarla";
    i.shortcut = QKeySequence("F");
    i.icon = objectIcon("figure");
    i.inserter = true;
    i.order = 47;
    i.factory = [](Editor& e) { return std::make_unique<FigureTool>(e); };
    i.options = [](QWidget* parent, Editor& e) -> QWidget* { return makeFigurePanel(parent, e); };
    ed.registerTool(std::move(i));
  }
}

}  // namespace xlobjects
