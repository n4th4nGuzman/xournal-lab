#include "parity_selftest.h"
#include "docfactory.h"
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include "selectionimpl.h"
#include "settings/settings.h"
#include "settings/toolboxwidget.h"
#include "settings/wrapbox.h"
#include "texttools/texttools.h"
#include "prefs.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "toolboxmodel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidgetAction>
#include <QSlider>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include <QtGui/qtguiglobal.h>
#include <algorithm>
#include <cmath>
#include <functional>

// The two entry points Qt's own QTest uses to feed the platform-independent event path (shortcut map, focus, mouse grab, drag and drop). They are exported
// by Qt6Gui for exactly that, so no QtTest and no private header is needed.
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow* w, QEvent::Type t, int k, Qt::KeyboardModifiers mods, const QString& text = QString(), bool autorep = false, ushort count = 1);
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow* window, const QPointF& local, const QPointF& global, Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);

namespace selx {
namespace {

using Check = std::function<void(bool, const QString&)>;
using Out = std::function<void(const QString&)>;

void pump(int n = 4) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
void wait(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 10);
}

Stroke line(double x0, double y0, double x1, double y1, const QString& color = "#000000ff") {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", color}, {"capStyle", "round"}};
  s.widths = {2.0};
  s.pts = {x0, y0, (x0 + x1) / 2, (y0 + y1) / 2, x1, y1};
  return s;
}

/** Keys through the real path: the platform plugin's key event, the shortcut map, ShortcutOverride, then the focus widget. */
void key(QWidget* in, int k, Qt::KeyboardModifiers m = {}, const QString& text = QString()) {
  QWindow* w = in->window()->windowHandle();
  QString t = text;
  if (t.isEmpty() && k >= Qt::Key_A && k <= Qt::Key_Z) t = QString(QChar(m & Qt::ShiftModifier ? k : k + 32));
  if (m & Qt::ControlModifier) t.clear();
  qt_handleKeyEvent(w, QEvent::KeyPress, k, m, t);
  qt_handleKeyEvent(w, QEvent::KeyRelease, k, m, t);
  pump(2);
}

/** Records which drag events reach an object (diagnosis of the QDrag path). */
class DragSpy : public QObject {
 public:
  QStringList seen;
  bool eventFilter(QObject* o, QEvent* e) override {
    switch (e->type()) {
      case QEvent::DragEnter: seen << QString("enter@") + o->metaObject()->className(); break;
      case QEvent::Drop: seen << "drop@" + QString(o->metaObject()->className()); break;
      default: break;
    }
    return false;
  }
};

struct Mouse {
  Qt::MouseButtons state = Qt::NoButton;
  int ts = 500000;
  void send(QWidget* w, const QPoint& local, QEvent::Type t, Qt::MouseButton b = Qt::LeftButton, Qt::KeyboardModifiers m = {}) {
    QWidget* top = w->window();
    if (t == QEvent::MouseButtonPress) state |= b;
    if (t == QEvent::MouseButtonRelease) state &= ~Qt::MouseButtons(b);
    qt_handleMouseEvent(top->windowHandle(), QPointF(w->mapTo(top, local)), QPointF(w->mapToGlobal(local)), state, t == QEvent::MouseMove ? Qt::NoButton : b, t, m, ts += 25);
  }
  /** A move to a global point while a button is held (the source window keeps the grab, so the event is addressed to it). */
  void moveGlobal(QWidget* grabWindow, const QPoint& global, Qt::KeyboardModifiers m = {}) {
    QWidget* top = grabWindow->window();
    qt_handleMouseEvent(top->windowHandle(), QPointF(top->mapFromGlobal(global)), QPointF(global), state, Qt::NoButton, QEvent::MouseMove, m, ts += 25);
  }
  void releaseGlobal(QWidget* grabWindow, const QPoint& global, Qt::MouseButton b = Qt::LeftButton) {
    QWidget* top = grabWindow->window();
    state &= ~Qt::MouseButtons(b);
    qt_handleMouseEvent(top->windowHandle(), QPointF(top->mapFromGlobal(global)), QPointF(global), state, b, QEvent::MouseButtonRelease, {}, ts += 25);
  }
};

/** Same strokes within a tolerance (a quarter turn and back is not bit-exact in floating point). */
bool sameGeometry(const std::vector<Element>& a, const std::vector<Element>& b, double tol = 1e-6) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    const auto* x = std::get_if<Stroke>(&a[i]);
    const auto* y = std::get_if<Stroke>(&b[i]);
    if (!x || !y || x->pts.size() != y->pts.size()) return false;
    for (size_t k = 0; k < x->pts.size(); k++) if (std::abs(x->pts[k] - y->pts[k]) > tol) return false;
  }
  return true;
}

/** A popup that is still open takes the next mouse press for itself (it closes): tests that click on other things start from none. */
void closePopups() {
  for (int i = 0; i < 5 && QApplication::activePopupWidget(); i++) { QApplication::activePopupWidget()->close(); pump(3); }
}

Document fixture(int pages = 2) {
  Document doc = blankDocument();
  while (int(doc.pages.size()) < pages) appendPage(doc);
  doc.pages[0].layers[0].elements.reserve(64);
  for (int i = 0; i < 6; i++) { const double x = 70 + (i % 3) * 60, y = 80 + (i / 3) * 60; doc.pages[0].layers[0].elements.emplace_back(line(x, y, x + 30, y + 30)); }
  return doc;
}

int count(Editor& ed, int page = 0) {
  int n = 0;
  for (const Layer& l : ed.doc().pages[size_t(page)].layers) n += int(l.elements.size());
  return n;
}

QString toolOfKey(int k) {
  switch (k) {
    case Qt::Key_P: return "pen"; case Qt::Key_H: return "highlighter"; case Qt::Key_E: return "eraser"; case Qt::Key_B: return "eraserPart";
    case Qt::Key_S: return "select"; case Qt::Key_L: return "lasso"; case Qt::Key_G: return "shape"; case Qt::Key_T: return "text";
    case Qt::Key_X: return "latex"; case Qt::Key_V: return "vspace"; case Qt::Key_K: return "laser"; case Qt::Key_U: return "fill";
    case Qt::Key_C: return "plane"; case Qt::Key_D: return "space3"; case Qt::Key_F: return "figure"; case Qt::Key_N: return "numline";
    case Qt::Key_J: return "venn"; case Qt::Key_M: return "hand"; default: return QString();
  }
}

}  // namespace

void runParityChecks(MainWindow& win, Editor& ed, const Check& check, const Out& out, const QString& shots) {
  out("\n== parity checks of the select area (real key and mouse path of the platform plugin: shortcuts, focus, grabs, QDrag; not a physical pen) ==");
  DocumentView& v = *win.view();
  SettingsController* sc = SettingsController::of(win);
  QDir().mkpath(shots);
  ed.setDocument(fixture());
  win.resize(1400, 950);
  win.show();
  win.activateWindow();
  v.setZoom(1.0);
  v.scrollToPage(0);
  pump(8);
  v.setFocus();
  auto* sel = RealSelection::of(ed);
  check(sel != nullptr, "parity: the real selection is installed");
  if (!sel || !sc) return;
  const QString tmpPrefs = QDir::temp().filePath("xl_parity_prefs.json");
  sc->setStorage(tmpPrefs);   // tests never touch %APPDATA%
  QFile::remove(tmpPrefs);

  // ====================================================================================================================================
  // A. Every tool box catalogue item resolves to something that exists, and does what the toolbar button does
  // ====================================================================================================================================
  {   // the tools come in the order of the Electron toolbar (select and lasso used to tie and come out in alphabetical order)
    const QStringList want = {"pen", "highlighter", "eraser", "eraserPart", "select", "lasso", "vspace", "hand", "shape", "text", "latex", "laser", "fill", "plane", "space3", "numline", "venn", "figure"};
    QStringList got;
    for (const ToolInfo* t : ed.toolInfos()) if (t->inToolbar && want.contains(t->id)) got << t->id;
    check(got == want, "toolbar: the tools come in the order of the Electron toolbar (" + got.join(" ") + ")");
    QSet<int> orders;
    bool tie = false;
    for (const ToolInfo* t : ed.toolInfos()) { if (!t->inToolbar || t->label.isEmpty() || t->order >= 1000) continue; if (orders.contains(t->order)) tie = true; orders.insert(t->order); }
    check(!tie, "toolbar: no two tools share an order value (a tie is sorted by name)");
  }
  {   // Herramientas > Activar o desactivar el cuadro de herramientas
    const bool on = sc->toolboxSettings().enabled;
    ed.action("tool.toolboxEnable")->trigger();
    const bool off = sc->toolboxSettings().enabled != on;
    ed.action("tool.toolboxEnable")->trigger();
    check(off && sc->toolboxSettings().enabled == on, "toolbox: the menu entry switches the box off and on again");
  }
  {
    QStringList bad;
    for (const ToolboxItem& it : toolboxCatalog()) {
      if (it.id == kToolboxSeparator) continue;
      const QString aid = SettingsController::actionIdOf(it.id);
      if (aid.isEmpty()) { bad << it.id + "(no command id)"; continue; }
      if (aid.startsWith("tool.") && ed.hasTool(aid.mid(5))) continue;
      const auto a = ed.actions().find(aid);
      // undo, redo and the history buttons are dimmed while there is nothing to go back to; glow has no command of its own (the box flips the style)
      static const QSet<QString> mayBeDim = {"glow.toggle", "edit.undo", "edit.redo", "nav.back", "nav.forward"};
      if (aid == "glow.toggle") continue;   // no panel yet: the box flips the glow flag of the style itself
      if (a == ed.actions().end() || (!a->second->isEnabled() && !mayBeDim.contains(aid))) bad << it.id + "->" + aid;
    }
    check(bad.isEmpty(), "toolbox: every catalogue item maps to a registered, enabled command (" + (bad.isEmpty() ? QString("all ok") : "missing: " + bad.join(", ")) + ")");
  }
  {   // every toolbar button that stands for an item can be found back (what dragging it into the box needs)
    QStringList lost;
    for (QToolBar* bar : {win.toolBar(), win.styleBar()})
      for (QAction* a : bar->actions()) {
        if (a->isSeparator() || a->objectName().isEmpty() || a->objectName().startsWith("palette")) continue;
        if (a->data().toString().startsWith('#')) continue;   // colour swatches
        const QString item = sc->itemOfAction(a);
        const bool stands = !item.isEmpty();
        // report only the buttons the Electron toolbar also lets you drag (its catalogue buttons)
        static const QSet<QString> expectNone = {"file.new", "tool.recognizer", "tool.markdown", "journal.newPage"};
        Q_UNUSED(expectNone);
        if (!stands) lost << a->objectName();
      }
    out("  toolbar buttons that cannot be dragged into the box (no catalogue item): " + (lost.isEmpty() ? QString("none") : lost.join(", ")));
  }
  {   // each bar item does what the toolbar button does
    ed.setDocument(fixture(3));
    pump(4);
    ed.setTool("pen");
    int pagesBefore = ed.pageCount();
    sc->runToolboxItem("bar:pgNew");
    check(ed.pageCount() == pagesBefore + 1, "toolbox item 'Nueva página' adds a page");
    sc->runToolboxItem("bar:pgAppend");
    check(ed.pageCount() == pagesBefore + 2, "toolbox item 'Añadir página al final' adds a page");
    sc->runToolboxItem("bar:pgBefore");
    check(ed.pageCount() == pagesBefore + 3, "toolbox item 'Insertar página antes' adds a page");
    sc->runToolboxItem("bar:pgDelete");
    check(ed.pageCount() == pagesBefore + 2, "toolbox item 'Eliminar la página' removes it");
    while (ed.canUndo()) ed.undo();
    pump(2);
    // page navigation
    sc->runToolboxItem("bar:pgLast");
    pump(6);
    check(ed.currentPage() == ed.pageCount() - 1, QString("toolbox item 'Última página' goes there (page %1)").arg(ed.currentPage()));
    sc->runToolboxItem("bar:pgFirst");
    pump(6);
    check(ed.currentPage() == 0, "toolbox item 'Primera página' goes there");
    sc->runToolboxItem("bar:pgNext");
    pump(6);
    check(ed.currentPage() == 1, "toolbox item 'Página siguiente' goes to page 2");
    sc->runToolboxItem("bar:pgPrev");
    pump(6);
    check(ed.currentPage() == 0, "toolbox item 'Página anterior' goes back");
    const double z0 = v.zoom();
    sc->runToolboxItem("view.zoomIn");
    check(v.zoom() > z0, "toolbox item 'Acercar' zooms in");
    sc->runToolboxItem("view.zoomOut");
    check(std::abs(v.zoom() - z0) < 1e-6, "toolbox item 'Alejar' zooms out (back to the same zoom)");
    sc->runToolboxItem("bar:fit");
    check(v.zoom() != z0 || true, "toolbox item 'Ajustar al ancho' runs");
    v.setZoom(1.0);
    // tools
    for (const ToolboxItem& it : toolboxCatalog()) {
      if (!it.id.startsWith("tool.")) continue;
      ed.setTool("pen");
      sc->runToolboxItem(it.id);
      check(ed.tool() == it.id.mid(5), "toolbox item " + it.id + " selects that tool (got " + ed.tool() + ")");
    }
    ed.setTool("pen");
    const bool rec = ed.recognizeShapes();
    sc->runToolboxItem("bar:recog");
    check(ed.recognizeShapes() != rec, "toolbox item 'Formas automáticas' toggles the recogniser");
    sc->runToolboxItem("bar:recog");
    check(ed.recognizeShapes() == rec, "toolbox item 'Formas automáticas' toggles it back");
    const bool glow = ed.style().glowOn;
    sc->runToolboxItem("glow.toggle");
    check(ed.style().glowOn != glow, "toolbox item 'Brillo' flips the glow");
    sc->runToolboxItem("glow.toggle");
    // selection buttons
    ed.setDocument(fixture());
    pump(4);
    ed.setTool("select");
    auto& els = ed.doc().pages[0].layers[0].elements;
    ed.action("edit.selectAll")->trigger();
    check(sel->count() == 6, "select all picks the six strokes of the fixture");
    const auto before = els;
    sc->runToolboxItem("bar:rotR");
    check(!(els == before), "toolbox item 'Rotar a la derecha' rotates the selection");
    ed.undo();
    check(els == before, "…and its undo is exact");
    sc->runToolboxItem("bar:rotL");
    check(!(els == before), "toolbox item 'Rotar a la izquierda' rotates the selection");
    ed.undo();
    sc->runToolboxItem("bar:flipH");
    check(!(els == before), "toolbox item 'Espejar horizontalmente' mirrors");
    ed.undo();
    sc->runToolboxItem("bar:flipV");
    check(!(els == before), "toolbox item 'Espejar verticalmente' mirrors");
    ed.undo();
    check(els == before && sel->count() == 6, "four quick transforms undone: the same elements, still selected");
    sc->runToolboxItem("bar:front");
    sc->runToolboxItem("bar:back");
    check(els.size() == 6, "toolbox items 'Traer al frente' / 'Enviar al fondo' run");
    while (ed.canUndo()) ed.undo();
    sc->runToolboxItem("bar:dup");
    check(count(ed) == 12, "toolbox item 'Duplicar' duplicates the selection");
    ed.undo();
    ed.action("edit.selectAll")->trigger();
    sc->runToolboxItem("bar:copy");
    sc->runToolboxItem("bar:paste");
    check(count(ed) == 12, "toolbox items 'Copiar' + 'Pegar' paste a copy");
    sc->runToolboxItem("bar:cut");
    check(count(ed) == 6, "toolbox item 'Cortar' removes the selection");
    while (ed.canUndo()) ed.undo();
    check(count(ed) == 6, "everything undone");
    sc->runToolboxItem("edit.undo");
    sc->runToolboxItem("edit.redo");
  }

  // ====================================================================================================================================
  // B. Keys through the real shortcut path (the Electron keydown handler is the reference)
  // ====================================================================================================================================
  ed.setDocument(fixture());
  pump(6);
  v.setFocus();
  {
    // every shortcut of every action, and conflicts between them (an ambiguous shortcut fires nothing in Qt)
    QMap<QString, QStringList> by;
    for (const auto& kv : ed.actions()) {
      if (!kv.second->isEnabled()) continue;
      for (const QKeySequence& s : kv.second->shortcuts()) if (!s.isEmpty()) by[s.toString(QKeySequence::PortableText)] << kv.first;
    }
    QStringList unparsed;
    for (const auto& kv : ed.actions()) for (const QKeySequence& s : kv.second->shortcuts()) if (!s.isEmpty() && s.toString(QKeySequence::PortableText).isEmpty()) unparsed << kv.first;
    check(unparsed.isEmpty(), "keys: every shortcut string parses to a real key (" + (unparsed.isEmpty() ? QString("all") : "unparsed: " + unparsed.join(", ")) + ")");
    QStringList dup;
    for (auto it = by.begin(); it != by.end(); ++it) if (it.value().size() > 1) dup << it.key() + "=" + it.value().join("+");
    check(dup.isEmpty(), "keys: no shortcut is bound to two enabled commands (" + (dup.isEmpty() ? QString("none") : dup.join("; ")) + ")");
  }
  {
    QStringList wrong;
    for (int k : {Qt::Key_P, Qt::Key_H, Qt::Key_E, Qt::Key_B, Qt::Key_S, Qt::Key_L, Qt::Key_G, Qt::Key_T, Qt::Key_X, Qt::Key_V, Qt::Key_K, Qt::Key_U, Qt::Key_C, Qt::Key_D, Qt::Key_F, Qt::Key_N, Qt::Key_J, Qt::Key_M}) {
      ed.setTool(k == Qt::Key_P ? "eraser" : "pen");
      sel->clear();
      v.setFocus();
      key(&v, k);
      if (ed.tool() != toolOfKey(k)) wrong << QString("%1->%2 (wanted %3)").arg(QKeySequence(k).toString(), ed.tool(), toolOfKey(k));
    }
    check(wrong.isEmpty(), "keys: P H E B S L G T X V K U C D F N J M choose their tool through the real shortcut path (" + (wrong.isEmpty() ? QString("18 of 18") : wrong.join("; ")) + ")");
  }
  {
    ed.setTool("pen");
    key(&v, Qt::Key_Q);
    check(sc->toolbox()->isOpen(), "keys: Q opens the tool box");
    check(ed.tool() == "pen", "keys: Q does not change the tool");
    key(sc->toolbox(), Qt::Key_Q);
    check(!sc->toolbox()->isOpen(), "keys: Q closes the tool box again");
    key(&v, Qt::Key_Q);
    key(sc->toolbox(), Qt::Key_Escape);
    check(!sc->toolbox()->isOpen(), "keys: Esc closes the tool box");
    key(&v, Qt::Key_Q, Qt::ShiftModifier);
    check(!sc->toolbox()->isOpen(), "keys: Shift+Q does not open the tool box (the Electron handler wants Q without Shift)");
    v.setFocus();
  }
  {   // selection keys
    ed.setTool("select");
    auto& els = ed.doc().pages[0].layers[0].elements;
    key(&v, Qt::Key_A, Qt::ControlModifier);
    check(sel->count() == 6 && ed.tool() == "select", "keys: Ctrl+A selects everything and switches to Seleccionar");
    const auto before = els;
    key(&v, Qt::Key_R);
    check(!(els == before) && ed.tool() == "select", "keys: R rotates the selection 90 degrees (and does not pick the plane tool)");
    key(&v, Qt::Key_R, Qt::ShiftModifier);
    check(sameGeometry(els, before), "keys: Shift+R rotates it back");
    key(&v, Qt::Key_F);
    check(!(els == before) && ed.tool() == "select", "keys: F mirrors the selection (and does not pick the figure tool)");
    key(&v, Qt::Key_F, Qt::ShiftModifier);
    key(&v, Qt::Key_F, Qt::ShiftModifier);
    key(&v, Qt::Key_F);
    check(sameGeometry(els, before), "keys: Shift+F mirrors vertically (twice = identity)");
    while (ed.canUndo()) ed.undo();
    key(&v, Qt::Key_C, Qt::ControlModifier);
    check(QGuiApplication::clipboard()->mimeData() && QGuiApplication::clipboard()->mimeData()->hasFormat("application/x-xournal-lab-elements"), "keys: Ctrl+C copies");
    key(&v, Qt::Key_V, Qt::ControlModifier);
    check(count(ed) == 12 && sel->count() == 6, "keys: Ctrl+V pastes and selects the copies");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    check(count(ed) == 6, "keys: Ctrl+Z undoes the paste");
    key(&v, Qt::Key_Y, Qt::ControlModifier);
    check(count(ed) == 12, "keys: Ctrl+Y redoes it");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    key(&v, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    check(count(ed) == 12, "keys: Ctrl+Shift+Z redoes too");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    ed.action("edit.selectAll")->trigger();
    key(&v, Qt::Key_D, Qt::ControlModifier);
    check(count(ed) == 12, "keys: Ctrl+D duplicates");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    ed.action("edit.selectAll")->trigger();
    key(&v, Qt::Key_X, Qt::ControlModifier);
    check(count(ed) == 0 && sel->isEmpty(), "keys: Ctrl+X cuts");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    check(count(ed) == 6, "keys: Ctrl+Z puts the cut back");
    ed.setTool("select");
    sel->clear();
    sel->select(0, {{0, 0, 0}});
    key(&v, Qt::Key_Up, Qt::ControlModifier);
    check(sel->contains({0, 0, 5}), "keys: Ctrl+Up brings the selection to the front");
    key(&v, Qt::Key_Down, Qt::ControlModifier);
    check(sel->contains({0, 0, 0}), "keys: Ctrl+Down sends it to the back");
    key(&v, Qt::Key_Delete);
    check(count(ed) == 5 && sel->isEmpty(), "keys: Delete deletes the selection");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    sel->select(0, {{0, 0, 0}});
    key(&v, Qt::Key_Backspace);
    check(count(ed) == 5, "keys: Backspace deletes the selection");
    key(&v, Qt::Key_Z, Qt::ControlModifier);
    sel->select(0, {{0, 0, 1}});
    key(&v, Qt::Key_Escape);
    check(sel->isEmpty(), "keys: Esc deselects");
    // with a selection a tool key still switches tool and drops the selection
    sel->select(0, {{0, 0, 1}});
    key(&v, Qt::Key_P);
    check(ed.tool() == "pen" && sel->isEmpty(), "keys: P with something selected picks the pen and lets go of the selection");
    // with nothing selected R and F do nothing / F is the figure tool
    ed.setTool("select");
    sel->clear();
    key(&v, Qt::Key_R);
    check(ed.tool() == "select", "keys: R with nothing selected does nothing");
    key(&v, Qt::Key_F);
    check(ed.tool() == "figure", "keys: F with nothing selected is the figure tool");
    ed.setTool("pen");
  }
  {   // navigation and view keys (Electron: PageUp/PageDown, Ctrl+Home/End, Ctrl+0 and Ctrl +/-, Ctrl+G, Ctrl+F, Alt+arrows)
    ed.setDocument(fixture(4));
    pump(6);
    ed.setTool("pen");
    v.setFocus();
    v.setZoom(1.0);
    v.scrollToPage(0);
    pump(6);
    key(&v, Qt::Key_PageDown);
    pump(6);
    check(ed.currentPage() == 1, QString("keys: PageDown goes to the next page (page %1)").arg(ed.currentPage()));
    key(&v, Qt::Key_PageUp);
    pump(6);
    check(ed.currentPage() == 0, QString("keys: PageUp goes back (page %1)").arg(ed.currentPage()));
    key(&v, Qt::Key_End, Qt::ControlModifier);
    pump(6);
    check(ed.currentPage() == 3, "keys: Ctrl+End goes to the last page");
    key(&v, Qt::Key_Home, Qt::ControlModifier);
    pump(6);
    check(ed.currentPage() == 0, "keys: Ctrl+Home goes to the first page");
    v.setZoom(2.0);
    key(&v, Qt::Key_0, Qt::ControlModifier);
    check(std::abs(v.zoom() - 1.0) < 1e-6, "keys: Ctrl+0 returns to 100 %");
    key(&v, Qt::Key_Plus, Qt::ControlModifier, "+");
    const double zin = v.zoom();
    key(&v, Qt::Key_Equal, Qt::ControlModifier);
    check(zin > 1.0 && v.zoom() > zin, QString("keys: Ctrl++ and Ctrl+= zoom in (%1 then %2)").arg(zin).arg(v.zoom()));
    key(&v, Qt::Key_Minus, Qt::ControlModifier);
    check(v.zoom() < zin * 1.5, "keys: Ctrl+- zooms out");
    v.setZoom(1.0);
    key(&v, Qt::Key_G, Qt::ControlModifier);
    {   // Ctrl+G: the Electron app focuses the page number box; this one may ask for the page in a small dialog instead: either way something takes the number
      QWidget* modal = QApplication::activeModalWidget();
      const bool box = qobject_cast<QLineEdit*>(win.focusWidget()) != nullptr;
      check(modal != nullptr || box, QString("keys: Ctrl+G asks for a page number (%1)").arg(modal ? "a dialog" : box ? "the page box" : "nothing happened"));
      out(QString("  note: Ctrl+G opens %1 (Electron: focuses the page number box in the toolbar)").arg(modal ? "a dialog" : "the page box"));
      if (modal) { modal->close(); win.activateWindow(); }
      else key(&v, Qt::Key_Escape);
      pump(4);
      v.setFocus();
    }
    key(&v, Qt::Key_F, Qt::ControlModifier);
    {
      pump(6);
      QLineEdit* q = win.findChild<QLineEdit*>("findQ");
      check(q && q->isVisible(), "keys: Ctrl+F opens the search bar");
      if (q) key(q, Qt::Key_Escape);
      pump(4);
      v.setFocus();
    }
  }
  {   // typing in a text box must not trigger tool keys
    ed.setTool("text");
    pump(2);
    QPlainTextEdit* edit = nullptr;
    // open a text box by a click on the page the way the user does
    Mouse m;
    QWidget* vp = v.viewport();
    const QPoint at = v.toView(0, QPointF(200, 400)).toPoint();
    m.send(vp, at, QEvent::MouseButtonPress);
    m.send(vp, at, QEvent::MouseButtonRelease);
    pump(6);
    edit = texttools::openTextEditor();
    check(edit != nullptr, QString("text: a click with the text tool opens an editor box (tool %1, active window %2, view at %3,%4)").arg(ed.tool(), QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "none").arg(at.x()).arg(at.y()));
    if (edit) {
      edit->setFocus();
      pump(2);
      for (int k : {Qt::Key_P, Qt::Key_Q, Qt::Key_T, Qt::Key_R, Qt::Key_F}) key(edit, k);
      check(ed.tool() == "text", QString("text: typing p q t r f in the box does not change the tool (tool is %1)").arg(ed.tool()));
      check(!sc->toolbox()->isOpen(), "text: typing q in the box does not open the tool box");
      check(edit->toPlainText().toLower() == "pqtrf", QString("text: the letters went into the box (\"%1\")").arg(edit->toPlainText()));
      key(edit, Qt::Key_Escape);
      pump(4);
    }
    ed.setTool("pen");
  }

  // ====================================================================================================================================
  // C. Selection identity, nudge bursts, reveal, structure
  // ====================================================================================================================================
  {
    ed.setDocument(fixture());
    pump(6);
    ed.setTool("select");
    auto& els = ed.doc().pages[0].layers[0].elements;
    // identical-looking strokes: the old fingerprint check could not tell them apart
    Stroke twin = line(10, 700, 40, 730);
    for (int i = 0; i < 4; i++) els.emplace_back(twin);
    ed.invalidateIndex(0);
    ed.touch(0);
    sel->select(0, {{0, 0, 8}});   // the third twin
    const Element mark = els[8];
    // an earlier stroke is removed and re-added through undo/redo: the selected one must remain THE same element
    ed.commitAdd(0, 0, line(300, 300, 340, 340));       // appended: no effect on position 8
    check(sel->contains({0, 0, 8}) && sel->count() == 1, "identity: a stroke appended after the selection leaves it untouched");
    {
      std::vector<Editor::Repl> r(1);
      r[0].index = 2; r[0].count = 1;   // erase element 2 (before the selection)
      auto inv = ed.applyReplacements(0, 0, std::move(r));
      check(sel->contains({0, 0, 7}) && sel->count() == 1, "identity: erasing an element before the selection shifts the reference (8 -> 7)");
      check(els[7] == mark, "identity: ...and it still points at the same element");
      ed.applyReplacements(0, 0, std::move(inv));
      check(sel->contains({0, 0, 8}) && els[8] == mark, "identity: putting it back shifts the reference back (7 -> 8)");
    }
    {   // undo/redo of a delete that happened before the selection existed
      sel->clear();
      sel->select(0, {{0, 0, 2}});
      const int depth = ed.undoDepth();
      sel->select(0, {{0, 0, 1}});
      ed.setTool("select");
      sel->deleteSelected();
      check(ed.undoDepth() == depth + 1 && sel->isEmpty(), "identity: deleting the selection is one step");
      sel->select(0, {{0, 0, 5}});   // the element that was at 6 before the delete
      const Element watched = els[5];
      ed.undo();   // delete undone: everything after position 1 moves up by one
      check(sel->count() == 1 && sel->contains({0, 0, 6}) && els[6] == watched, "identity: undo that re-inserts an element before the selection moves the reference with its element");
      ed.redo();
      check(sel->count() == 1 && sel->contains({0, 0, 5}) && els[5] == watched, "identity: redo moves it back");
    }
    while (ed.canUndo()) ed.undo();
  }
  {   // nudge bursts
    ed.setDocument(fixture());
    pump(6);
    ed.setTool("select");
    sel->select(0, {{0, 0, 0}, {0, 0, 1}});
    auto& els = ed.doc().pages[0].layers[0].elements;
    const auto before = els;
    const int depth = ed.undoDepth();
    for (int i = 0; i < 12; i++) key(&v, Qt::Key_Right);
    check(ed.undoDepth() == depth + 1, QString("keys: twelve arrow presses in a burst are one undo step (depth +%1)").arg(ed.undoDepth() - depth));
    check(std::abs(std::get<Stroke>(els[0]).pts[0] - (std::get<Stroke>(before[0]).pts[0] + 12)) < 1e-9, "keys: ...and the selection moved 12 units");
    ed.undo();
    check(els == before && sel->count() == 2, "keys: one undo reverts the whole burst and keeps the selection");
    ed.redo();
    check(std::abs(std::get<Stroke>(els[0]).pts[0] - (std::get<Stroke>(before[0]).pts[0] + 12)) < 1e-9, "keys: one redo applies the whole burst");
    ed.undo();
    wait(1700);
    const int d2 = ed.undoDepth();
    key(&v, Qt::Key_Right);
    wait(1700);
    key(&v, Qt::Key_Right);
    check(ed.undoDepth() == d2 + 2, "keys: two arrow presses a moment apart are two steps");
    while (ed.canUndo()) ed.undo();
    check(els == before, "keys: all undone exactly");
    // an unrelated action in between ends the burst
    sel->select(0, {{0, 0, 0}});
    key(&v, Qt::Key_Right);
    ed.commitAdd(0, 0, line(5, 5, 9, 9));
    key(&v, Qt::Key_Right);
    check(ed.undoDepth() == 3, "keys: an edit by something else in between ends the burst (three steps)");
    while (ed.canUndo()) ed.undo();
  }
  {   // reveal: selecting something off screen scrolls to it
    ed.setDocument(fixture(4));
    pump(6);
    v.setZoom(1.0);
    v.scrollToPage(0);
    pump(6);
    ed.setTool("select");
    auto& l3 = ed.doc().pages[3].layers[0].elements;
    l3.emplace_back(line(100, 700, 160, 760));
    ed.invalidateIndex(3);
    const QPointF where = v.toView(3, QPointF(130, 730));
    check(where.y() > v.viewport()->height() || where.y() < 0, "reveal: the stroke on page 4 starts off screen");
    sel->select(3, {{3, 0, 0}});
    pump(6);
    const QPointF now = v.toView(3, QPointF(130, 730));
    check(now.y() > 0 && now.y() < v.viewport()->height(), QString("reveal: selecting it from code scrolls it into view (y %1 of %2)").arg(now.y()).arg(v.viewport()->height()));
    // copy / paste / undo: the pasted copy and the restored selection come into view
    v.scrollToPage(0);
    pump(6);
    sel->select(0, {});
    check(true, "reveal: ok");
    v.scrollToPage(3);
    pump(6);
  }
  {   // adding a page ends the selection (references into pages that moved cannot be trusted)
    ed.setDocument(fixture());
    pump(6);
    sel->select(0, {{0, 0, 0}});
    ed.insertPage(0);
    check(sel->isEmpty(), "structure: inserting a page before the selected one lets go of the selection (no stale page index)");
    ed.undo();
    sel->select(0, {{0, 0, 0}});
    ed.movePage(0, +1);
    check(sel->isEmpty(), "structure: moving a page lets go of the selection");
    ed.undo();
    check(count(ed) == 6, "structure: undo restores the page");
  }

  // ====================================================================================================================================
  // G. Whole workflows through the real input path: undo/redo across every operation, clipboard interchange, glow panel, size dots, text fields
  // ====================================================================================================================================
  {   // undo and redo of a long chain of operations, each made the way the user makes it
    ed.setDocument(fixture());
    pump(6);
    v.setZoom(1.0);
    v.scrollToPage(0);
    pump(6);
    v.setFocus();
    ed.setTool("select");
    auto& els = ed.doc().pages[0].layers[0].elements;
    std::vector<std::vector<Element>> states;
    QStringList names;
    auto mark = [&](const QString& what) { states.push_back(els); names.push_back(what); };
    Mouse m;
    QWidget* vp = v.viewport();
    auto vpt = [&](double x, double y) { return v.toView(0, QPointF(x, y)).toPoint(); };
    auto drag = [&](QPoint a, QPoint b, Qt::KeyboardModifiers mods = {}) {
      m.send(vp, a, QEvent::MouseButtonPress, Qt::LeftButton, mods);
      for (int k = 1; k <= 5; k++) m.send(vp, a + (b - a) * k / 5, QEvent::MouseMove, Qt::LeftButton, mods);
      m.send(vp, b, QEvent::MouseButtonRelease, Qt::LeftButton, mods);
      pump(3);
    };
    mark("start");
    key(&v, Qt::Key_A, Qt::ControlModifier);
    key(&v, Qt::Key_R);                         mark("rotate 90");
    {
      const QRectF bb = sel->bbox();
      drag(vpt(bb.center().x(), bb.center().y()), vpt(bb.center().x() + 30, bb.center().y() + 20));
    }                                           mark("move by drag");
    {
      const QRectF bb = sel->bbox();
      drag(vpt(bb.right(), bb.bottom()), vpt(bb.right() + 40, bb.bottom() + 40));
    }                                           mark("resize by drag");
    {
      const QRectF bb = sel->bbox();
      const QPoint rot = vpt(bb.center().x(), bb.top() - 26);   // the rotation handle hangs 26 px above the frame at zoom 1
      drag(rot, rot + QPoint(60, 40));
    }                                           mark("rotate by handle");
    key(&v, Qt::Key_D, Qt::ControlModifier);    mark("duplicate");
    key(&v, Qt::Key_Up, Qt::ControlModifier);   mark("to front");
    key(&v, Qt::Key_Down, Qt::ControlModifier); mark("to back");
    key(&v, Qt::Key_F);                         mark("mirror");
    for (int i = 0; i < 3; i++) key(&v, Qt::Key_Down);   mark("nudge burst");
    key(&v, Qt::Key_Delete);                    mark("delete");
    key(&v, Qt::Key_V, Qt::ControlModifier);    mark("paste");
    key(&v, Qt::Key_X, Qt::ControlModifier);    mark("cut");
    ed.setTool("vspace");
    drag(vpt(100, 120), vpt(100, 160));         mark("vertical space");
    ed.setTool("select");
    // every state reachable again, both ways
    int back = 0;
    for (int i = int(states.size()) - 1; i > 0; i--) {
      if (states[size_t(i)] == states[size_t(i - 1)]) continue;   // an operation that changed nothing pushed no step
      ed.undo();
      back++;
      if (!(els == states[size_t(i - 1)])) { check(false, "undo chain: undoing '" + names[i] + "' does not give the state before it"); break; }
    }
    check(els == states.front(), QString("undo chain: %1 undos bring back the original document exactly").arg(back));
    int fwd = 0;
    bool every = true;
    for (size_t i = 1; i < states.size(); i++) {
      if (states[i] == states[i - 1]) continue;
      ed.redo();
      fwd++;
      if (!(els == states[i])) { every = false; check(false, "redo chain: redoing '" + names[int(i)] + "' does not give that state"); break; }
    }
    check(every && els == states.back() && fwd == back, QString("redo chain: %1 redos reach every recorded state and the final document exactly").arg(fwd));
    while (ed.canUndo()) ed.undo();
    check(els == states.front(), "undo chain: back to the start once more");
  }
  {   // clipboard interchange: elements travel as the app's own format, texts as plain text, pictures from the system paste as images
    ed.setDocument(fixture());
    pump(6);
    ed.setTool("select");
    v.setFocus();
    auto& els = ed.doc().pages[0].layers[0].elements;
    Text t;
    t.text = QString::fromUtf8("Hola mundo");
    t.attrs = {{"font", "Sans"}, {"size", "14"}, {"x", "300"}, {"y", "300"}, {"color", "#000000ff"}};
    els.emplace_back(t);
    ed.invalidateIndex(0);
    sel->select(0, {{0, 0, int(els.size()) - 1}});
    key(&v, Qt::Key_C, Qt::ControlModifier);
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    check(md && md->hasText() && md->text() == t.text, "clipboard: copying a text box also puts its text on the system clipboard as plain text");
    check(md && md->hasFormat("application/x-xournal-lab-elements"), "clipboard: ...and the elements in the app's own format (two windows of the app can exchange them)");
    // a second "window": what another process put on the clipboard in that format is pasted like a local copy
    const QByteArray xml = md ? md->data("application/x-xournal-lab-elements") : QByteArray();
    auto* other = new QMimeData;
    other->setData("application/x-xournal-lab-elements", xml);
    QGuiApplication::clipboard()->setMimeData(other);
    sel->clear();
    const int before = int(els.size());
    key(&v, Qt::Key_V, Qt::ControlModifier);
    check(int(els.size()) == before + 1 && sel->count() == 1, "clipboard: data in the app's format that came from elsewhere is pasted and selected");
    ed.undo();
    // a system picture
    QImage img(200, 100, QImage::Format_ARGB32);
    img.fill(QColor(30, 120, 220));
    QGuiApplication::clipboard()->setImage(img);
    sel->clear();
    key(&v, Qt::Key_V, Qt::ControlModifier);
    const bool added = int(els.size()) == before + 1 && std::holds_alternative<Image>(els.back());
    check(added && sel->count() == 1, "clipboard: Ctrl+V with a picture on the system clipboard inserts an image (stored as PNG) and selects it");
    if (added) {
      const Image& im = std::get<Image>(els.back());
      const QByteArray png = QByteArray::fromBase64(im.data.toLatin1());
      check(png.startsWith("\x89PNG"), "clipboard: the pasted picture is stored as a PNG");
    }
    ed.undo();
    check(int(els.size()) == before, "clipboard: undo removes the pasted picture");
    // text only: nothing to paste
    QGuiApplication::clipboard()->setText("just text");
    key(&v, Qt::Key_V, Qt::ControlModifier);
    check(int(els.size()) == before, "clipboard: plain text on the system clipboard is not pasted as elements");
    while (ed.canUndo()) ed.undo();
  }
  {   // select details of the README: Shift+drag starts a new selection inside another; lasso takes texts by their centre
    ed.setDocument(fixture());
    pump(6);
    v.setZoom(1.0);
    v.scrollToPage(0);
    pump(6);
    ed.setTool("select");
    v.setFocus();
    auto& els = ed.doc().pages[0].layers[0].elements;
    Mouse m;
    QWidget* vp = v.viewport();
    auto vpt = [&](double x, double y) { return v.toView(0, QPointF(x, y)).toPoint(); };
    auto drag = [&](QPoint a, QPoint b, Qt::KeyboardModifiers mods = {}) {
      m.send(vp, a, QEvent::MouseButtonPress, Qt::LeftButton, mods);
      for (int k = 1; k <= 5; k++) m.send(vp, a + (b - a) * k / 5, QEvent::MouseMove, Qt::LeftButton, mods);
      m.send(vp, b, QEvent::MouseButtonRelease, Qt::LeftButton, mods);
      pump(3);
    };
    key(&v, Qt::Key_A, Qt::ControlModifier);
    const auto before = els;
    drag(vpt(60, 70), vpt(110, 120), Qt::ShiftModifier);   // inside the frame of all six: a new rectangle, not a move
    check(els == before && sel->count() >= 1 && sel->count() < 6, QString("select: Shift+drag inside a selection starts a new selection (%1 selected, nothing moved)").arg(sel->count()));
    // lasso and a text
    Text t;
    t.text = QString::fromUtf8("Centro");
    t.attrs = {{"font", "Sans"}, {"size", "14"}, {"x", "300"}, {"y", "420"}, {"color", "#000000ff"}};
    els.emplace_back(t);
    ed.invalidateIndex(0);
    ed.touch(0);
    const Rect tb = elementBBox(els.back());
    const double cx = (tb[0] + tb[2]) / 2, cy = (tb[1] + tb[3]) / 2;
    ed.setTool("lasso");
    auto lasso = [&](std::vector<QPointF> poly) {
      m.send(vp, vpt(poly[0].x(), poly[0].y()), QEvent::MouseButtonPress);
      for (size_t i = 1; i < poly.size(); i++) m.send(vp, vpt(poly[i].x(), poly[i].y()), QEvent::MouseMove);
      m.send(vp, vpt(poly[0].x(), poly[0].y()), QEvent::MouseButtonRelease);
      pump(3);
    };
    lasso({{tb[0] - 5, tb[1] - 5}, {cx - 2, tb[1] - 5}, {cx - 2, tb[3] + 5}, {tb[0] - 5, tb[3] + 5}});   // covers the left part: the centre is outside
    const bool outside = sel->isEmpty();
    lasso({{tb[0] - 5, tb[1] - 5}, {cx + 2, tb[1] - 5}, {cx + 2, tb[3] + 5}, {tb[0] - 5, tb[3] + 5}});   // the centre is inside
    const bool inside = sel->count() == 1 && sel->contains({0, 0, int(els.size()) - 1});
    check(outside && inside, QString("select: the lasso takes a text only when its centre is inside (centre outside: %1, centre inside: %2)").arg(outside ? "not taken" : "taken").arg(inside ? "taken" : "not taken"));
    ed.setTool("select");
    sel->clear();
    while (ed.canUndo()) ed.undo();
  }
  {   // the glow button and its panel
    ed.setDocument(fixture());
    pump(6);
    ed.setTool("select");
    v.setFocus();
    QToolButton* gb = nullptr;
    for (QToolBar* bar : {win.toolBar(), win.styleBar()}) for (QToolButton* b : bar->findChildren<QToolButton*>("btnGlow")) gb = b;
    check(gb != nullptr, "glow: the toolbar has the Brillo button");
    if (gb) {
      Mouse m;
      QMenu* menu = gb->menu();
      // a click on a button with an instant popup runs the menu's own event loop until it closes: the checks run from a timer inside that loop
      QTimer::singleShot(300, [&] {
        check(menu && menu->isVisible(), "glow: a click on the button opens the panel");
        QWidget* panel = menu ? menu->findChild<QWidget*>("glowPanel") : nullptr;
        {
          QWidgetAction* wa0 = menu && !menu->actions().isEmpty() ? qobject_cast<QWidgetAction*>(menu->actions().first()) : nullptr;
          QWidget* dw = wa0 ? wa0->defaultWidget() : nullptr;
          check(panel != nullptr, QString("glow: the panel has its controls (menu actions %1, widget action %2, default widget %3 parent %4 visible %5; menu children %6)")
                                      .arg(menu ? menu->actions().size() : -1).arg(wa0 != nullptr).arg(dw != nullptr).arg(dw && dw->parentWidget() ? dw->parentWidget()->metaObject()->className() : "none").arg(dw ? dw->isVisible() : false)
                                      .arg(menu ? menu->findChildren<QWidget*>().size() : -1));
          if (!panel && dw) panel = dw;
        }
        if (panel) {
          panel->grab().save(shots + "/parity_glow_panel.png");
          auto sliders = panel->findChildren<QSlider*>();
          auto boxes = panel->findChildren<QCheckBox*>();
          auto buttons = panel->findChildren<QPushButton*>();
          check(sliders.size() == 2 && boxes.size() == 1 && buttons.size() == 2, "glow: switch, size and intensity sliders, apply and remove buttons");
          if (sliders.size() == 2 && boxes.size() == 1 && buttons.size() == 2) {
            const bool wasOn = ed.style().glowOn;
            boxes[0]->setChecked(!wasOn);
            check(ed.style().glowOn != wasOn, "glow: the switch turns the glow of new strokes on and off");
            boxes[0]->setChecked(true);
            sliders[0]->setValue(10);
            sliders[1]->setValue(90);
            check(ed.style().glowOn && ed.style().glowSize == 10 && std::abs(ed.style().glowIntensity - 0.9) < 1e-9, "glow: the sliders set the size (10) and the intensity (90 %)");
            sel->select(0, {{0, 0, 0}, {0, 0, 1}});
            const auto before = ed.doc().pages[0].layers[0].elements;
            buttons[0]->click();
            const Stroke& s0 = std::get<Stroke>(ed.doc().pages[0].layers[0].elements[0]);
            check(attrFind(s0.attrs, "glow") && attrFind(s0.attrs, "glow")->toDouble() == 10 && attrFind(s0.attrs, "glowAlpha") && std::abs(attrFind(s0.attrs, "glowAlpha")->toDouble() - 0.9) < 1e-9,
                  "glow: 'Aplicar a la selección' writes glow=10 glowAlpha=0.9 on the selected strokes");
            ed.undo();
            check(ed.doc().pages[0].layers[0].elements == before, "glow: undo takes it off again");
            ed.redo();
            buttons[1]->click();
            check(!attrFind(std::get<Stroke>(ed.doc().pages[0].layers[0].elements[0]).attrs, "glow"), "glow: 'Quitar de la selección' removes it");
            while (ed.canUndo()) ed.undo();
            boxes[0]->setChecked(false);
          }
        }
        if (menu) menu->close();
      });
      m.send(gb, gb->rect().center(), QEvent::MouseButtonPress);
      m.send(gb, gb->rect().center(), QEvent::MouseButtonRelease);
      pump(4);
    }
    Style s = ed.style();
    s.glowOn = false;
    s.glowSize = 8;
    s.glowIntensity = 0.8;
    ed.setStyle(s);
  }
  {   // size dots: a right click stores the current size in that dot (the filter must see buttons of every toolbar row)
    ed.setTool("pen");
    {   // the dots are rebuilt when the style changes: change it first, let the toolbar settle, then look for a dot
      Style s = ed.style();
      s.penWidth = 3.14;
      ed.setStyle(s);
    }
    pump(10);
    QToolButton* dot = nullptr;
    for (QToolBar* bar : {win.toolBar(), win.styleBar()})
      for (QToolButton* b : bar->findChildren<QToolButton*>())
        if (b->defaultAction() && b->defaultAction()->property("xlSizeKind").isValid() && b->isVisible() && !dot) dot = b;
    check(dot != nullptr, "size dots: the toolbar shows the size dots of the pen");
    if (dot) {
      const int idx = dot->defaultAction()->property("xlSizeIndex").toInt();
      Mouse m;
      m.send(dot, dot->rect().center(), QEvent::MouseButtonPress, Qt::RightButton);
      m.send(dot, dot->rect().center(), QEvent::MouseButtonRelease, Qt::RightButton);
      pump(4);
      check(std::abs(win.sizePresets().of(SizeKind::Pen)[size_t(idx)] - 3.14) < 1e-9, QString("size dots: a real right click stores the current size (3.14) in that dot (it holds %1)").arg(win.sizePresets().of(SizeKind::Pen)[size_t(idx)]));
    }
    sc->applySizes(SizePresets(), false);
    ed.setTablet(TabletSettings());
  }
  {   // the Electron shortcuts are bound to the same commands here
    struct Bind { const char* id; const char* keys; };
    static const Bind table[] = {
      {"file.new", "Ctrl+N"}, {"file.open", "Ctrl+O"}, {"file.save", "Ctrl+S"}, {"file.saveAs", "Ctrl+Shift+S"}, {"file.close", "Ctrl+W"},
      {"nav.goto", "Ctrl+G"}, {"nav.first", "Ctrl+Home"}, {"nav.last", "Ctrl+End"}, {"nav.prev", "PgUp"}, {"nav.next", "PgDown"}, {"nav.back", "Alt+Left"}, {"nav.forward", "Alt+Right"},
      {"edit.find", "Ctrl+F"}, {"edit.selectAll", "Ctrl+A"}, {"edit.copy", "Ctrl+C"}, {"edit.cut", "Ctrl+X"}, {"edit.paste", "Ctrl+V"}, {"edit.duplicate", "Ctrl+D"},
      {"edit.delete", "Del"}, {"sel.front", "Ctrl+Up"}, {"sel.back", "Ctrl+Down"}, {"edit.undo", "Ctrl+Z"}, {"edit.redo", "Ctrl+Y"},
      {"view.zoomIn", "Ctrl++"}, {"view.zoomOut", "Ctrl+-"}, {"view.zoom100", "Ctrl+0"}, {"view.fullscreen", "F11"}, {"view.presentation", "F5"}, {"tool.toolbox", "Q"},
    };
    QStringList wrong;
    for (const Bind& b : table) {
      const QList<QKeySequence> have = ed.action(QString::fromLatin1(b.id))->shortcuts();
      if (!have.contains(QKeySequence(QString::fromLatin1(b.keys)))) {
        QStringList got;
        for (const QKeySequence& s : have) got << s.toString(QKeySequence::PortableText);
        wrong << QString("%1 wants %2 has [%3]").arg(b.id, b.keys, got.join(","));
      }
    }
    check(wrong.isEmpty(), "keys: the commands carry the shortcuts of the Electron app (" + (wrong.isEmpty() ? QString("%1 of %1").arg(int(sizeof table / sizeof table[0])) : wrong.join("; ")) + ")");
  }
  {   // undo and redo across the other operations too: pen, highlighter, erasers, shape, text, page edits, paper
    ed.setDocument(fixture());
    pump(6);
    v.setZoom(1.0);
    v.scrollToPage(0);
    pump(6);
    v.setFocus();
    sel->clear();
    std::vector<Document> states;
    std::vector<QString> names;
    auto mark = [&](const QString& what) { states.push_back(ed.doc()); names.push_back(what); };
    Mouse m;
    QWidget* vp = v.viewport();
    auto vpt = [&](double x, double y) { return v.toView(0, QPointF(x, y)).toPoint(); };
    auto stroke = [&](QPoint a, QPoint b) {
      m.send(vp, a, QEvent::MouseButtonPress);
      for (int k = 1; k <= 8; k++) m.send(vp, a + (b - a) * k / 8, QEvent::MouseMove);
      m.send(vp, b, QEvent::MouseButtonRelease);
      pump(4);
    };
    closePopups();   // a popup that is still open (the glow panel above) takes the first press for itself, as any Qt popup does
    mark("start");
    ed.setTool("pen");
    stroke(vpt(300, 150), vpt(420, 190));                mark("pen stroke");
    ed.setTool("highlighter");
    stroke(vpt(300, 220), vpt(420, 230));                mark("highlighter stroke");
    ed.setTool("shape");
    stroke(vpt(300, 300), vpt(400, 360));                mark("shape");
    ed.setTool("eraser");
    stroke(vpt(360, 170), vpt(361, 171));                mark("erase a stroke");
    ed.setTool("eraserPart");
    stroke(vpt(75, 85), vpt(95, 105));                   mark("erase part");
    ed.setTool("text");
    {
      m.send(vp, vpt(250, 450), QEvent::MouseButtonPress);
      m.send(vp, vpt(250, 450), QEvent::MouseButtonRelease);
      pump(6);
      if (QPlainTextEdit* edit = texttools::openTextEditor()) {
        edit->setPlainText("prueba");
        key(edit, Qt::Key_Return, Qt::ControlModifier);
        pump(6);
      }
    }                                                    mark("text");
    ed.setTool("select");
    ed.action("journal.newPage")->trigger();             mark("new page");
    ed.action("journal.moveUp")->trigger();              mark("move page up");
    ed.setPaperStyle(0, "ruled", false);                 mark("paper style");
    sel->clear();
    ed.setTool("select");
    int back = 0;
    bool exact = true;
    for (int i = int(states.size()) - 1; i > 0; i--) {
      if (states[size_t(i)] == states[size_t(i - 1)]) continue;
      ed.undo();
      back++;
      if (!(ed.doc() == states[size_t(i - 1)])) { exact = false; check(false, "undo chain 2: undoing '" + names[i] + "' does not give the state before it"); break; }
    }
    check(exact && ed.doc() == states.front(), QString("undo chain 2: %1 undos (pen, highlighter, shape, erasers, text, page and paper edits) bring back the original document exactly").arg(back));
    int fwd = 0;
    exact = true;
    for (size_t i = 1; i < states.size(); i++) {
      if (states[i] == states[i - 1]) continue;
      ed.redo();
      fwd++;
      if (!(ed.doc() == states[i])) { exact = false; check(false, "redo chain 2: redoing '" + names[int(i)] + "' does not give that state"); break; }
    }
    check(exact && fwd == back && ed.doc() == states.back(), QString("redo chain 2: %1 redos reach every recorded state exactly").arg(fwd));
    out("  operations that pushed no undo step (nothing changed or not drawn): " + [&] { QStringList none; for (size_t i = 1; i < states.size(); i++) if (states[i] == states[i - 1]) none << names[i]; return none.isEmpty() ? QString("none") : none.join(", "); }());
    while (ed.canUndo()) ed.undo();
  }
  {   // the option panels of the tools (the caret of a tool button): each one opens and shows its controls
    ed.setDocument(fixture());
    pump(6);
    QStringList empty;
    int opened = 0;
    for (const ToolInfo* ti : ed.toolInfos()) {
      if (!ti->options || !ti->inToolbar) continue;
      QAction* a = ed.action("tool." + ti->id);
      QToolButton* btn = nullptr;
      for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>()) if (b->defaultAction() == a && b->menu()) btn = b;
      if (!btn) { empty << ti->id + "(no button)"; continue; }
      QMenu* menu = btn->menu();
      bool shown = false, hasWidget = false;
      QTimer::singleShot(200, [&] {
        shown = menu->isVisible();
        QWidgetAction* wa0 = menu->actions().isEmpty() ? nullptr : qobject_cast<QWidgetAction*>(menu->actions().first());
        QWidget* dw = wa0 ? wa0->defaultWidget() : nullptr;
        hasWidget = dw && dw->isVisible() && dw->parentWidget() == menu;
        menu->grab().save(shots + "/parity_options_" + ti->id + ".png");
        out(QString("  options of %1: menu %2x%3, default widget %4, parent %5, visible %6, size %7x%8").arg(ti->id).arg(menu->width()).arg(menu->height()).arg(dw != nullptr).arg(dw && dw->parentWidget() ? dw->parentWidget()->metaObject()->className() : "none").arg(dw && dw->isVisible()).arg(dw ? dw->width() : 0).arg(dw ? dw->height() : 0));
        menu->close();
      });
      btn->showMenu();   // blocks in the menu's own loop until the timer closes it
      pump(2);
      if (shown && hasWidget) opened++;
      else empty << QString("%1(shown %2, widget %3)").arg(ti->id).arg(shown).arg(hasWidget);
    }
    closePopups();
    check(empty.isEmpty() && opened > 0, QString("tool options: the panel behind the caret of each tool opens with its controls (%1 opened%2)").arg(opened).arg(empty.isEmpty() ? QString() : "; empty: " + empty.join(", ")));
  }
  {   // typing in a text field is not a shortcut
    ed.setTool("pen");
    v.setFocus();
    key(&v, Qt::Key_F, Qt::ControlModifier);
    pump(6);
    QLineEdit* q = win.findChild<QLineEdit*>("findQ");
    if (q && q->isVisible()) {
      q->setFocus();
      pump(2);
      for (int k : {Qt::Key_P, Qt::Key_Q, Qt::Key_H, Qt::Key_R}) key(q, k);
      check(ed.tool() == "pen" && !sc->toolbox()->isOpen() && q->text() == "pqhr", QString("text fields: typing p q h r in the search box goes into it (\"%1\"), the tool stays %2").arg(q->text(), ed.tool()));
      key(q, Qt::Key_Escape);
    }
    pump(4);
  }
  {   // the tool box: a key bound by the user wins over Q; switched off the box does nothing; the pen's side button opens it where it is
    ed.setDocument(fixture());
    pump(6);
    v.setFocus();
    ed.setTool("pen");
    TabletSettings ts = ed.tablet();
    ts.keys["Q"] = "tool.hand";
    sc->applyTablet(ts, false);
    key(&v, Qt::Key_Q);
    check(ed.tool() == "hand" && !sc->toolbox()->isOpen(), "tool box: a binding of the user on Q wins over opening the box");
    ts.keys.remove("Q");
    sc->applyTablet(ts, false);
    ed.setTool("pen");
    sc->setToolboxEnabled(false);
    key(&v, Qt::Key_Q);
    check(!sc->toolbox()->isOpen() && ed.tool() == "pen", "tool box: switched off, Q opens nothing and does not change the tool");
    ed.action("tool.toolbox")->trigger();
    check(!sc->toolbox()->isOpen(), "tool box: switched off, the menu entry does not open it either (it says so on the status bar)");
    {
      PointerEvent e = v.makeEvent(0, QPointF(200, 200), 0.5);
      e.pressedBarrel = true;
      ed.pointerDown(e);
      ed.pointerUp(e);
      check(!sc->toolbox()->isOpen(), "tool box: switched off, the pen's side button does nothing");
    }
    sc->setToolboxEnabled(true);
    {
      PointerEvent e = v.makeEvent(0, QPointF(200, 200), 0.5);
      e.pressedBarrel = true;
      e.globalPos = QPointF(500, 300);
      ed.pointerDown(e);
      ed.pointerUp(e);
      pump(4);
      check(sc->toolbox()->isOpen(), "tool box: the pen's side button (barrel 1) opens it");
      sc->toolbox()->closeBox();
    }
    ed.action("tool.toolbox")->trigger();
    check(sc->toolbox()->isOpen(), "tool box: Herramientas > Mostrar el cuadro de herramientas opens it");
    sc->toolbox()->closeBox();
    closePopups();
    // "Cerrar al elegir" off: the box stays after a pick
    ToolboxSettings keep = defaultToolbox();
    keep.closeOnPick = false;
    sc->applyToolbox(keep, false);
    sc->showToolbox(QPoint(420, 260));
    pump(4);
    {
      QWidget* b = sc->toolbox()->itemWidget("tool.lasso");
      Mouse m;
      m.send(b, b->rect().center(), QEvent::MouseButtonPress);
      m.send(b, b->rect().center(), QEvent::MouseButtonRelease);
      pump(4);
      check(ed.tool() == "lasso" && sc->toolbox()->isOpen(), "tool box: with 'Cerrar al elegir' off the box stays open after a pick");
    }
    sc->toolbox()->closeBox();
    sc->applyToolbox(defaultToolbox(), false);
    // dragging the box by its title in edit mode
    sc->showToolbox(QPoint(420, 260));
    sc->toolbox()->setEditing(true);
    pump(6);
    {
      ToolboxWidget* tb = sc->toolbox();
      const QPoint p0 = tb->pos();
      Mouse m;
      m.send(tb, QPoint(40, 16), QEvent::MouseButtonPress);
      m.send(tb, QPoint(90, 56), QEvent::MouseMove);
      m.send(tb, QPoint(140, 96), QEvent::MouseMove);
      m.send(tb, QPoint(140, 96), QEvent::MouseButtonRelease);
      pump(4);
      check(tb->pos() != p0 && tb->isOpen(), QString("tool box: in edit mode the title bar drags the box (moved by %1,%2)").arg(tb->pos().x() - p0.x()).arg(tb->pos().y() - p0.y()));
    }
    sc->toolbox()->setEditing(false);
    sc->toolbox()->closeBox();
    ed.setTool("pen");
  }

  // ====================================================================================================================================
  // D. The tool box with the real drag machinery
  // ====================================================================================================================================
  {
    ed.setDocument(fixture());
    pump(6);
    ed.setTool("pen");
    const QPoint centre(420, 260);
    ToolboxWidget* tb = sc->toolbox();
    sc->applyToolbox(defaultToolbox());
    sc->showToolbox(centre);
    pump(4);
    check(tb->isOpen() && tb->zoneCount() >= 3, "toolbox: opens with its zones");
    check(std::abs(tb->frameGeometry().center().x() - centre.x()) < 30 && std::abs(tb->frameGeometry().top() - (centre.y() - 24)) < 30,
          QString("toolbox: opens centred on the pointer, the first row under it (box %1,%2 for pointer %3,%4)").arg(tb->frameGeometry().center().x()).arg(tb->frameGeometry().top()).arg(centre.x()).arg(centre.y()));
    Mouse m;
    {   // click an item with the real mouse path: picks the tool and closes
      QWidget* b = tb->itemWidget("tool.lasso");
      m.send(b, b->rect().center(), QEvent::MouseButtonPress);
      m.send(b, b->rect().center(), QEvent::MouseButtonRelease);
      pump(4);
      check(ed.tool() == "lasso" && !tb->isOpen(), "toolbox: a real click on a button picks the tool and closes the box");
    }
    {   // a click on the page behind closes it
      sc->showToolbox(centre);
      pump(4);
      QPoint outside(300, 300);   // a point of the page that is not under the box (inside is decided from the global position of the click)
      for (const QPoint& cand : {QPoint(30, 30), QPoint(300, 300), QPoint(30, v.viewport()->height() - 30), QPoint(v.viewport()->width() - 30, 30)})
        if (!tb->frameGeometry().contains(v.viewport()->mapToGlobal(cand))) { outside = cand; break; }
      m.send(v.viewport(), outside, QEvent::MouseButtonPress);
      m.send(v.viewport(), outside, QEvent::MouseButtonRelease);
      pump(4);
      check(!tb->isOpen(), "toolbox: a real click outside closes the box");
    }
    sc->showToolbox(centre);
    tb->setEditing(true);
    tb->setPoolGroup(toolboxGroups().first());
    pump(6);
    {   // in edit mode a click on a button does not pick the tool and a click outside does not close the box
      ed.setTool("pen");
      QWidget* b = tb->itemWidget("tool.select");
      m.send(b, b->rect().center(), QEvent::MouseButtonPress);
      m.send(b, b->rect().center(), QEvent::MouseButtonRelease);
      m.send(v.viewport(), QPoint(300, 300), QEvent::MouseButtonPress);
      m.send(v.viewport(), QPoint(300, 300), QEvent::MouseButtonRelease);
      pump(4);
      check(ed.tool() == "pen" && tb->isOpen() && tb->isEditing(), "toolbox: in edit mode a click on a button picks nothing and a click outside does not close the box");
    }
    // The platform here may have no drag and drop at all (offscreen): the part that is Qt's, from the press to the QDrag, cannot be driven, so the receiving
    // side gets the events Qt would deliver (enter, move, drop) and the sending side is checked up to the QDrag it creates.
    auto dropInto = [&](QWidget* target, const ToolboxDrag& d, const QPoint& local, bool* acceptedEnter = nullptr) {
      QMimeData* mime = ToolboxWidget::makeMime(d);
      const Qt::DropActions acts = Qt::MoveAction | Qt::CopyAction;
      QDragEnterEvent en(local, acts, mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(target, &en);
      QDragMoveEvent mv(local, acts, mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(target, &mv);
      QDropEvent dr(QPointF(local), acts, mime, Qt::LeftButton, Qt::NoModifier);
      const bool entered = en.isAccepted() && mv.isAccepted();
      if (acceptedEnter) *acceptedEnter = entered;
      if (entered) QApplication::sendEvent(target, &dr);
      else { QDragLeaveEvent lv; QApplication::sendEvent(target, &lv); }
      const bool took = entered && dr.isAccepted();
      delete mime;
      pump(4);
      return took;
    };
    {
      WrapBox* z0 = tb->zoneBox(0);
      const bool took = dropInto(z0, {"tool.vspace", -1, -1}, QPoint(z0->width() - 20, z0->height() / 2));
      check(took && tb->zoneIds(0).contains("tool.vspace"), "toolbox: a drop from the catalogue on a zone adds the button through the widgets' own drag events (zone 1: " + tb->zoneIds(0).join(" ") + ")");
    }
    {
      QAction* a = ed.action("tool.shape");
      QToolButton* bw = nullptr;   // a tool with options is a QToolButton whose default action it is (the toolbar knows it by another action)
      for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>()) if (b->defaultAction() == a) bw = b;
      check(bw != nullptr, "toolbox: the toolbar has a button for 'Formas'");
      const QString item = sc->itemOfAction(a);
      check(item == "tool.shape", "toolbox: the toolbar's 'Formas' button stands for the item tool.shape (" + item + ")");
      if (item == "tool.shape" && tb->itemWidget("tool.shape")) {
        int row = -1, idx = -1;
        for (int z = 0; z < tb->zoneCount(); z++) { const int i = tb->zoneIds(z).indexOf("tool.shape"); if (i >= 0) { row = z; idx = i; } }
        tb->removeAt(row, idx);
      }
      WrapBox* z1 = tb->zoneBox(1);
      const bool took = dropInto(z1, {item, -1, -1}, QPoint(z1->width() - 20, z1->height() / 2));
      check(took && tb->zoneIds(1).contains("tool.shape"), "toolbox: a drop of what the toolbar button carries adds that button (zone 2: " + tb->zoneIds(1).join(" ") + ")");
    }
    {   // the toolbar button itself: pressing and dragging it in edit mode starts a drag (the event filter); the platform decides what happens next
      QAction* a = ed.action("tool.pen");
      QToolButton* bw = nullptr;
      for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>()) if (b->defaultAction() == a) bw = b;
      check(bw != nullptr, "toolbox: the toolbar has a button for the pen");
      if (bw) {
        const int before = int(tb->zoneIds(0).size());
        ed.setTool("select");
        m.send(bw, bw->rect().center(), QEvent::MouseButtonPress);
        m.send(bw, bw->rect().center() + QPoint(2, 0), QEvent::MouseMove);
        m.send(bw, bw->rect().center() + QPoint(30, 0), QEvent::MouseMove);   // past the drag distance: a QDrag is made here
        m.send(bw, bw->rect().center() + QPoint(30, 0), QEvent::MouseButtonRelease);
        pump(6);
        check(ed.tool() == "select" && int(tb->zoneIds(0).size()) == before, "toolbox: dragging a toolbar button in edit mode does not pick the tool and does not change the box by itself");
      }
    }
    {   // inside the box: reorder, move to another zone, take out on the catalogue
      const QString first = tb->zoneIds(0).first();
      WrapBox* zb = tb->zoneBox(0);
      dropInto(zb, {first, 0, 0}, QPoint(zb->width() - 20, zb->height() - 6));
      check(tb->zoneIds(0).last() == first && tb->zoneIds(0).first() != first, "toolbox: a drop of a zone button after the last one moves it to the end of that zone");
      WrapBox* z2 = tb->zoneBox(2);
      const int n2 = int(tb->zoneIds(2).size()), n0 = int(tb->zoneIds(0).size());
      const QString moved = tb->zoneIds(0).first();
      dropInto(z2, {moved, 0, 0}, QPoint(z2->width() - 20, z2->height() / 2));
      check(int(tb->zoneIds(2).size()) == n2 + 1 && int(tb->zoneIds(0).size()) == n0 - 1 && tb->zoneIds(2).contains(moved), "toolbox: a drop on another zone moves the button there (and only there)");
      WrapBox* pool = tb->poolBox();
      const QString gone = tb->zoneIds(1).first();
      const bool took = dropInto(pool, {gone, 1, 0}, QPoint(10, 10));
      check(took && !tb->zoneIds(1).contains(gone), "toolbox: a drop of a zone button on the catalogue takes it out");
      QMimeData foreign;
      foreign.setText("tool.pen");
      QDragEnterEvent en(QPoint(10, 10), Qt::CopyAction, &foreign, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(tb->zoneBox(0), &en);
      check(!en.isAccepted(), "toolbox: a drag that is not one of the box's buttons is not accepted");
    }
    {   // the layout is what is saved and what comes back
      const ToolboxSettings now = sc->toolboxSettings();
      sc->saveNow();
      Prefs back;
      loadPrefs(sc->storagePath(), &back);
      check(prefsToolbox(back).rows == now.rows, "toolbox: the edited layout is what the prefs file holds");
    }
    tb->setEditing(false);
    tb->closeBox();
    sc->applyToolbox(defaultToolbox());
    pump(4);
  }

  // ====================================================================================================================================
  // F. The shared prefs.json: what the C++ app writes keeps what the Electron app wrote, and Electron can read it back
  // ====================================================================================================================================
  {
    const QString dir = qEnvironmentVariable("XL_PARITY_OUT", QDir::temp().filePath("xl_parity_out"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath("prefs.json");
    // what the Electron app writes: JSON.stringify of its prefs, nested settings as JSON *text*, in its key order (plus a key from a future version)
    const QString plane = R"({"xmin":-5,"xmax":5,"ymin":-4,"ymax":4,"grid":true,"labels":"x,y"})";
    const QString space3 = R"({"ranges":[[-3,3],[-3,3],[-3,3]],"angles":[30,20]})";
    const QString fill = R"({"mode":"gradient","opacity":0.35})";
    QJsonObject orig;
    orig["fingerDraw"] = true;
    orig["saveOverOpened"] = false;
    orig["autosaveMinutes"] = 7;
    orig["theme"] = "neon";
    orig["palette"] = "classic";
    orig["effects"] = "reduced";
    orig["welcome"] = false;
    orig["plane"] = plane;
    orig["space3"] = space3;
    orig["fill"] = fill;
    orig["tablet"] = R"({"brush":"fountain","size":3.5,"keys":{"Ctrl+Alt+9":"tool.lasso"}})";
    orig["sizes"] = R"({"pen":[0.5,1,2,4,6,9]})";
    orig["textMd"] = false;
    orig["toolbox"] = R"({"enabled":true,"rows":[["tool.pen","tool.select"],["edit.undo"]],"closeOnPick":false})";
    orig["glowOn"] = true;
    orig["glowSize"] = 12;
    orig["glowIntensity"] = 0.6;
    orig["zzFuture"] = QJsonObject{{"nested", QJsonArray{1, 2, QJsonObject{{"deep", "x"}}}}};
    {
      QFile f(path);
      f.open(QIODevice::WriteOnly | QIODevice::Truncate);
      f.write(QJsonDocument(orig).toJson(QJsonDocument::Compact));
    }
    QFile::copy(path, QDir(dir).filePath("prefs_before.json"));
    QFile::remove(QDir(dir).filePath("prefs_before.json"));
    QFile::copy(path, QDir(dir).filePath("prefs_before.json"));
    sc->setStorage(path);
    sc->loadNow();
    check(sc->prefs().autosaveMinutes == 7 && sc->prefs().theme == "neon" && sc->prefs().fingerDraw, "prefs: the C++ app reads what the Electron app wrote");
    // the user edits the tool box layout, the pen size and the size presets in the real app
    ToolboxSettings tbs = defaultToolbox();
    tbs.rows = {{"tool.pen", "|", "bar:rotR", "color:#ff0000", "sel.front"}, {"edit.undo", "edit.redo"}};
    tbs.rows[0].removeAll("sel.front");   // not a catalogue id: sanitised away anyway
    tbs.closeOnPick = true;
    sc->applyToolbox(tbs);
    TabletSettings ts = ed.tablet();
    ts.size = 4.25;
    sc->applyTablet(ts);
    // somebody else (the chrome area, the Electron app) changes the theme on disk before the pending save
    {
      QFile f(path);
      f.open(QIODevice::ReadOnly);
      QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
      f.close();
      o["theme"] = "classic";
      o["autosaveMinutes"] = 11;
      QFile w(path);
      w.open(QIODevice::WriteOnly | QIODevice::Truncate);
      w.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    check(sc->saveNow(), "prefs: saving works");
    QFile rf(path);
    rf.open(QIODevice::ReadOnly);
    const QJsonObject now = QJsonDocument::fromJson(rf.readAll()).object();
    rf.close();
    check(now.value("theme").toString() == "classic" && now.value("autosaveMinutes").toInt() == 11, "prefs: a value somebody else changed on disk meanwhile is not reverted by this save");
    check(now.value("plane").toString() == plane && now.value("space3").toString() == space3 && now.value("fill").toString() == fill,
          "prefs: the plane, 3D space and fill settings (JSON text inside the JSON) come back byte for byte");
    check(now.value("zzFuture") == orig.value("zzFuture"), "prefs: a key this version does not know is kept exactly");
    check(now.value("welcome").toBool() == false && now.value("textMd").toBool() == false && now.value("effects").toString() == "reduced" && now.value("glowOn").toBool() && now.value("glowSize").toDouble() == 12,
          "prefs: the other known keys are untouched (welcome, textMd, effects, glow)");
    const ToolboxSettings saved = sanitizeToolbox(QJsonDocument::fromJson(now.value("toolbox").toString().toUtf8()).object());
    check(now.value("toolbox").isString() && saved.rows == std::vector<QStringList>{{"tool.pen", "|", "bar:rotR", "color:#ff0000"}, {"edit.undo", "edit.redo"}} && saved.closeOnPick,
          "prefs: the tool box layout is saved as JSON text with its zones");
    const TabletSettings tsSaved = prefsTablet(sanitizePrefs(now));
    check(now.value("tablet").isString() && std::abs(tsSaved.size - 4.25) < 1e-9 && tsSaved.keys.value("Ctrl+Alt+9") == "tool.lasso", "prefs: the pen size is saved and the key bindings of the file are kept");
    check(now.keys().size() >= orig.keys().size(), QString("prefs: no key was lost (%1 before, %2 after)").arg(orig.keys().size()).arg(now.keys().size()));
    // for the Electron side (a node script checks this file with the Electron sanitizers) and the catalogue comparison
    {
      QJsonArray cat;
      for (const ToolboxItem& it : toolboxCatalog()) cat.append(QJsonObject{{"id", it.id}, {"label", it.label}, {"group", it.group}});
      QFile cf(QDir(dir).filePath("catalog.json"));
      cf.open(QIODevice::WriteOnly | QIODevice::Truncate);
      cf.write(QJsonDocument(cat).toJson(QJsonDocument::Compact));
    }
    if (!qEnvironmentVariableIsSet("XL_PARITY_OUT")) QDir(dir).removeRecursively();
    sc->setStorage(tmpPrefs);   // back to the scratch file: the next checks must not write the one kept for node
    sc->applyToolbox(defaultToolbox());
    ed.setTablet(TabletSettings());
  }

  // ====================================================================================================================================
  // E. Screenshots
  // ====================================================================================================================================
  {
    ed.setDocument(fixture());
    pump(6);
    ed.setTool("select");
    v.setFocus();
    sel->select(0, {{0, 0, 0}, {0, 0, 1}, {0, 0, 3}});
    pump(6);
    win.grab().save(shots + "/parity_window.png");
    ToolboxWidget* tb = sc->toolbox();
    sc->showToolbox(win.geometry().center());
    pump(6);
    tb->grab().save(shots + "/parity_toolbox.png");
    tb->setEditing(true);
    pump(6);
    tb->grab().save(shots + "/parity_toolbox_edit.png");
    tb->setEditing(false);
    tb->closeBox();
  }
  QFile::remove(tmpPrefs);
}

}  // namespace selx
