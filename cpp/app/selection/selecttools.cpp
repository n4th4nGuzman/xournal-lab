// The tools of the selection area: select (rectangle, click), lasso and vertical space. They only forward the pointer to the RealSelection, which
// owns the gesture state (so the frame, the lifted pixmap and the protractor are drawn from one place).
#include "documentview.h"
#include "editor.h"
#include "icons.h"
#include "selicons.h"
#include "selectionimpl.h"
#include <QKeyEvent>

namespace selx {

namespace {

class SelectTool : public Tool {
 public:
  SelectTool(Editor& ed, bool lasso) : Tool(ed), lasso_(lasso) {}
  QCursor cursor() const override { return cursor_; }
  void activate() override { cursor_ = Qt::ArrowCursor; }
  void deactivate() override { if (RealSelection* s = sel()) s->cancel(); }

  void pointerDown(const PointerEvent& e) override {
    if (RealSelection* s = sel()) s->down(e, lasso_, e.shift());
  }
  void pointerMove(const PointerEvent& e) override { if (RealSelection* s = sel()) s->move(e); }
  void pointerUp(const PointerEvent& e) override {
    if (RealSelection* s = sel()) { s->up(e); setCursor(s->cursorAt(e.page, e.pos, e.zoom)); }
  }
  void pointerCancel() override { if (RealSelection* s = sel()) s->cancel(); }
  void hover(const PointerEvent& e) override {
    if (RealSelection* s = sel()) setCursor(s->cursorAt(e.page, e.pos, e.zoom));
  }
  void paintOverlay(QPainter& p, const OverlayInfo& info) override { if (RealSelection* s = sel()) s->paintDrag(p, info); }

  bool keyPress(QKeyEvent* k) override {
    RealSelection* s = sel();
    if (!s || s->isEmpty() || s->dragging()) return false;
    if (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    const bool shift = k->modifiers() & Qt::ShiftModifier;
    const double step = shift ? 10 : 1;   // page units; Shift = a larger step
    switch (k->key()) {
      case Qt::Key_Delete: case Qt::Key_Backspace: s->deleteSelected(); return true;
      case Qt::Key_R: s->quick(shift ? Quick::RotL : Quick::RotR); return true;
      case Qt::Key_F: s->quick(shift ? Quick::FlipV : Quick::FlipH); return true;
      case Qt::Key_Left: s->nudge(-step, 0); return true;
      case Qt::Key_Right: s->nudge(step, 0); return true;
      case Qt::Key_Up: s->nudge(0, -step); return true;
      case Qt::Key_Down: s->nudge(0, step); return true;
      default: return false;
    }
  }

 private:
  RealSelection* sel() const { return RealSelection::of(editor()); }
  void setCursor(const QCursor& c) {
    if (c.shape() == cursor_.shape()) return;
    cursor_ = c;
    if (view()) view()->applyCursor();
  }
  bool lasso_;
  QCursor cursor_ = Qt::ArrowCursor;
};

class VspaceTool : public Tool {
 public:
  using Tool::Tool;
  QCursor cursor() const override { return Qt::SplitVCursor; }
  void deactivate() override { if (RealSelection* s = sel()) s->cancel(); }
  void pointerDown(const PointerEvent& e) override { if (RealSelection* s = sel()) s->startVspace(e); }
  void pointerMove(const PointerEvent& e) override { if (RealSelection* s = sel()) s->move(e); }
  void pointerUp(const PointerEvent& e) override { if (RealSelection* s = sel()) s->up(e); }
  void pointerCancel() override { if (RealSelection* s = sel()) s->cancel(); }
  void paintOverlay(QPainter& p, const OverlayInfo& info) override { if (RealSelection* s = sel()) s->paintDrag(p, info); }
 private:
  RealSelection* sel() const { return RealSelection::of(editor()); }
};

}  // namespace

void registerSelectionTools(Editor& ed) {
  {
    ToolInfo i;
    i.id = "select"; i.label = "Seleccionar objeto (rectángulo)";
    i.tooltip = "Seleccionar: clic o rectángulo; arrastra para mover y usa las asas para redimensionar o rotar";
    i.shortcut = QKeySequence("S"); i.icon = Icons::make("select"); i.order = 10;
    i.factory = [](Editor& e) { return std::make_unique<SelectTool>(e, false); };
    ed.registerTool(i);
  }
  {
    ToolInfo i;
    i.id = "lasso"; i.label = "Seleccionar región (lazo)";
    i.tooltip = "Lazo: dibuja una región libre; selecciona los trazos con algún punto dentro y los textos e imágenes cuyo centro queda dentro";
    i.shortcut = QKeySequence("L"); i.icon = selIcon("lasso"); i.order = 11;
    i.factory = [](Editor& e) { return std::make_unique<SelectTool>(e, true); };
    ed.registerTool(i);
  }
  {
    ToolInfo i;
    i.id = "vspace"; i.label = "Espacio vertical";
    i.tooltip = "Espacio vertical: haz clic en una línea y arrastra; todo lo que está debajo se mueve";
    i.shortcut = QKeySequence("V"); i.icon = selIcon("vspace"); i.order = 12;
    i.factory = [](Editor& e) { return std::make_unique<VspaceTool>(e); };
    ed.registerTool(i);
  }
}

}  // namespace selx
