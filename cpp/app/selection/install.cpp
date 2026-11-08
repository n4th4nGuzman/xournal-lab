#include "modules.h"
#include "mainwindow.h"
#include "documentview.h"
#include "selectionimpl.h"
#include "glowpanel.h"
#include "selicons.h"
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <QKeyEvent>
#include <QMenu>

// The "selection" area (see ARCHITECTURE.md and API.md). Register tools, commands, handlers and panels here.

namespace selx {
void registerSelectionTools(Editor& ed);   // selecttools.cpp

namespace {

/**
 * The keys the selection uses without a modifier (R, F, arrows, Delete) are also shortcuts of other tools or of the window. Accepting the
 * ShortcutOverride event while something is selected makes Qt deliver them to the view as ordinary key presses, like the TS keydown handler.
 */
class KeyGuard : public QObject {
 public:
  KeyGuard(Editor& ed, QObject* parent) : QObject(parent), ed_(ed) {}
  bool eventFilter(QObject*, QEvent* ev) override {
    if (ev->type() != QEvent::ShortcutOverride) return false;
    auto* k = static_cast<QKeyEvent*>(ev);
    if (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    if (ed_.tool() != "select" && ed_.tool() != "lasso") return false;
    RealSelection* s = RealSelection::of(ed_);
    if (!s || s->isEmpty()) return false;
    switch (k->key()) {
      case Qt::Key_R: case Qt::Key_F: case Qt::Key_Delete: case Qt::Key_Backspace:
      case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
        ev->accept();
        return true;
      default: return false;
    }
  }
 private:
  Editor& ed_;
};

}  // namespace
}  // namespace selx

void installSelection(MainWindow& win) {
  using namespace selx;
  Editor& ed = win.editor();
  ed.setSelection(std::make_unique<RealSelection>(ed));
  registerSelectionTools(ed);

  Editor* e = &ed;
  auto with = [e](auto fn) { return [e, fn] { if (RealSelection* s = RealSelection::of(*e)) fn(*s, *e); }; };

  ed.addCommand("edit.cut", "Cortar", QKeySequence::Cut, with([](RealSelection& s, Editor&) { s.cut(); }));
  ed.addCommand("edit.copy", "Copiar", QKeySequence::Copy, with([](RealSelection& s, Editor&) { s.copy(); }));
  ed.addCommand("edit.paste", "Pegar", QKeySequence::Paste, with([](RealSelection& s, Editor&) { s.paste(); }));
  ed.addCommand("edit.delete", "Eliminar", QKeySequence::Delete, with([](RealSelection& s, Editor&) { s.deleteSelected(); }));
  ed.addCommand("edit.selectAll", "Seleccionar todo", QKeySequence::SelectAll, with([](RealSelection& s, Editor& ed2) {
    ed2.setTool("select");
    s.selectAll(ed2.currentPage());
  }));
  ed.addCommand("edit.duplicate", "Duplicar", QKeySequence("Ctrl+D"), with([](RealSelection& s, Editor&) { s.duplicate(); }));
  ed.addCommand("sel.front", "Traer al frente", QKeySequence("Ctrl+Up"), with([](RealSelection& s, Editor&) { s.reorder(true); }));
  ed.addCommand("sel.back", "Enviar al fondo", QKeySequence("Ctrl+Down"), with([](RealSelection& s, Editor&) { s.reorder(false); }));
  // R / F and the arrows are handled as key presses of the tool (see KeyGuard), so these have no shortcut of their own
  ed.addCommand("sel.rotL", "Rotar a la izquierda (Mayús+R)", QKeySequence(), with([](RealSelection& s, Editor&) { s.quick(Quick::RotL); }));
  ed.addCommand("sel.rotR", "Rotar a la derecha (R)", QKeySequence(), with([](RealSelection& s, Editor&) { s.quick(Quick::RotR); }));
  ed.addCommand("sel.flipH", "Espejar horizontalmente (F)", QKeySequence(), with([](RealSelection& s, Editor&) { s.quick(Quick::FlipH); }));
  ed.addCommand("sel.flipV", "Espejar verticalmente (Mayús+F)", QKeySequence(), with([](RealSelection& s, Editor&) { s.quick(Quick::FlipV); }));
  // the glow panel calls these two: they use the glow size and intensity of the drawing style
  ed.addCommand("glow.toggle", "Brillo de los trazos", QKeySequence(), [e] { Style s = e->style(); s.glowOn = !s.glowOn; e->setStyle(s); });
  ed.addCommand("sel.glowApply", "Aplicar brillo a la selección", QKeySequence(), with([](RealSelection& s, Editor& ed2) {
    const Style& st = ed2.style();
    if (s.setGlow(Glow{st.glowSize, st.glowIntensity}) == 0) ed2.notify(QStringLiteral("No hay trazos seleccionados que puedan brillar"));
  }));
  ed.addCommand("sel.glowRemove", "Quitar brillo de la selección", QKeySequence(), with([](RealSelection& s, Editor&) { s.setGlow(std::nullopt); }));

  {   // icons and tips as the Electron toolbar has them; the buttons live in the style row of the window (see below)
    struct Dress { const char* id; const char* icon; const char* tip; };
    static const Dress dress[] = {
      {"edit.cut", "cut", "Cortar (Ctrl+X)"}, {"edit.copy", "copy", "Copiar la selección (Ctrl+C)"}, {"edit.paste", "paste", "Pegar (Ctrl+V)"}, {"edit.duplicate", "dup", "Duplicar (Ctrl+D)"},
      {"sel.front", "front", "Traer al frente (Ctrl+↑)"}, {"sel.back", "back", "Enviar al fondo (Ctrl+↓)"},
      {"sel.rotL", "rotL", "Rotar la selección 90° a la izquierda (Mayús+R)"}, {"sel.rotR", "rotR", "Rotar la selección 90° a la derecha (R)"},
      {"sel.flipH", "flipH", "Espejar horizontalmente (F). También arrastrando un asa al otro lado"}, {"sel.flipV", "flipV", "Espejar verticalmente (Mayús+F)"},
    };
    ed.action("glow.toggle")->setIcon(selIcon("glow"));
    ed.action("glow.toggle")->setToolTip(QString::fromUtf8("Brillo (glow) de los trazos: interruptor, tamaño e intensidad"));
    ed.action("pen.settings")->setIcon(selIcon("tablet"));   // the box and the menu show it; the settings area registers the command itself
    for (const Dress& d : dress) { QAction* a = ed.action(QString::fromLatin1(d.id)); a->setIcon(selIcon(QString::fromLatin1(d.icon))); a->setToolTip(QString::fromUtf8(d.tip)); }
    // The window rebuilds its toolbar rows whenever a tool is registered or the palette changes: the selection buttons are put back after each rebuild.
    auto put = [e, w = &win] {
      QToolBar* bar = w->styleBar();
      if (!bar) return;
      // Electron's second line: [glow] [palette] colours sizes | front back rotL rotR flipH flipV (cut / copy / paste / duplicate are in the first line)
      QWidget* glow = makeGlowButton(*e, bar);
      auto* wa = new QWidgetAction(bar);
      wa->setDefaultWidget(glow);
      bar->insertAction(w->styleLeadMarker(), wa);
      for (const char* id : {"sel.front", "sel.back", "sel.rotL", "sel.rotR", "sel.flipH", "sel.flipV"}) bar->addAction(e->action(QString::fromLatin1(id)));
    };
    put();
    QObject::connect(&win, &MainWindow::toolbarRebuilt, &win, put);
  }
  if (QMenu* edit = win.menu("edit")) {
    edit->addSeparator();
    edit->addAction(ed.action("edit.selectAll"));
    edit->addAction(ed.action("edit.duplicate"));
    QMenu* sub = edit->addMenu("Selección");
    for (const char* id : {"sel.rotL", "sel.rotR", "sel.flipH", "sel.flipV", "sel.front", "sel.back", "sel.glowApply", "sel.glowRemove"}) sub->addAction(ed.action(id));
  }
  if (DocumentView* v = win.view()) v->installEventFilter(new KeyGuard(ed, v));
  // pages were added, removed or moved: a (page, layer, index) reference can no longer be trusted to name the same element, so it is let go
  QObject::connect(&ed, &Editor::structureChanged, &win, [e] { if (RealSelection* s = RealSelection::of(*e)) s->clear(); });
}
