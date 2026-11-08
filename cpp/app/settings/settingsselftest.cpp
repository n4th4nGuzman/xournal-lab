#include "settingsselftest.h"
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include "inputfixselftest.h"
#include "penholdselftest.h"
#include "palettes.h"
#include "prefs.h"
#include "prefsdialog.h"
#include "prefsselftest.h"
#include "settings.h"
#include "tabletdialog.h"
#include "tabletwidgets.h"
#include "toolboxwidget.h"
#include "wrapbox.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>
#include <memory>

namespace {
using Check = std::function<void(bool, const QString&)>;

void pump(int n = 3) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
void wait(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) { QApplication::processEvents(QEventLoop::AllEvents, 20); }
}

int elementsOn(Editor& ed, int page) {
  int n = 0;
  for (const Layer& l : ed.doc().pages[size_t(page)].layers) n += int(l.elements.size());
  return n;
}

void sendKey(QWidget* to, int key, Qt::KeyboardModifiers m = Qt::NoModifier, const QString& text = QString(), quint32 vk = 0) {
  QKeyEvent ev(QEvent::KeyPress, key, m, 0, vk, 0, text);
  QApplication::sendEvent(to, &ev);
}

bool sendMouse(QWidget* to, QEvent::Type t, Qt::MouseButton b, Qt::KeyboardModifiers m = Qt::NoModifier, QPoint at = QPoint(5, 5)) {
  QMouseEvent ev(t, QPointF(at), QPointF(to->mapToGlobal(at)), QPointF(to->mapToGlobal(at)), b, t == QEvent::MouseButtonRelease ? Qt::NoButton : b, m);
  QApplication::sendEvent(to, &ev);
  return ev.isAccepted();
}

void sendWheel(QWidget* to, int dy, Qt::KeyboardModifiers m) {
  const QPointF at(20, 20);
  QWheelEvent ev(at, to->mapToGlobal(at), QPoint(), QPoint(0, dy), Qt::NoButton, m, Qt::NoScrollPhase, false);
  QApplication::sendEvent(to, &ev);
}

bool dropOn(WrapBox* box, const ToolboxDrag& d, const QPoint& local) {
  QMimeData* mime = ToolboxWidget::makeMime(d);
  const bool ok = box->dropAt(mime, local);
  delete mime;
  return ok;
}
QPoint endOf(WrapBox* b) { return QPoint(b->width() - 10, b->height() - 6); }

QStringList flat(ToolboxWidget* t) {
  QStringList out;
  for (int i = 0; i < t->zoneCount(); i++) out << t->zoneIds(i);
  return out;
}
QList<QStringList> rowsOf(ToolboxWidget* t) {
  QList<QStringList> r;
  for (int i = 0; i < t->zoneCount(); i++) r << t->zoneIds(i);
  return r;
}

std::unique_ptr<QTemporaryDir> g_dir;

QString prefsFile() { return g_dir->filePath("prefs.json"); }

Prefs readPrefs() {
  Prefs p;
  loadPrefs(prefsFile(), &p);
  return p;
}

/** Size dots of the toolbar as (action, widget) pairs, in order. */
QList<QAction*> sizeActions(MainWindow& win) {
  QList<QAction*> l;
  for (QAction* a : win.styleBar()->actions()) if (a->property("xlSizeKind").isValid()) l << a;
  return l;
}
QWidget* widgetOf(MainWindow& win, QAction* a) { return win.toolbarWidget(a); }
QList<QAction*> colorActions(MainWindow& win) {
  QList<QAction*> l;
  for (QAction* a : win.styleBar()->actions()) if (a->data().toString().size() == 7 && a->data().toString().startsWith('#')) l << a;
  return l;
}

QList<QStringList> asList(const std::vector<QStringList>& v) { return QList<QStringList>(v.begin(), v.end()); }
}  // namespace

void runSettingsChecks(MainWindow& win, Editor& ed, const Check& check, const std::function<void(const QString&)>& out, const QString& shots) {
  SettingsController* c = SettingsController::of(win);
  check(c != nullptr, "settings: the controller exists (child of the window)");
  if (!c) return;
  g_dir = std::make_unique<QTemporaryDir>();
  c->setStorage(prefsFile());   // never the user's file
  check(!c->storagePath().contains("xournal-electron"), "settings: the test writes to a temporary prefs file, not %APPDATA%");
  const TabletSettings savedTablet = ed.tablet();
  const Style savedStyle = ed.style();
  const int page = 0;
  DocumentView* v = win.view();
  QDir().mkpath(shots);
  ed.setTool("pen");

  QStringList notices;
  // the lambda captures a local: it must not outlive this function, or the next notice from any later check runs it on freed memory
  const QMetaObject::Connection noticeConn = QObject::connect(&ed, &Editor::notice, &win, [&](const QString& m) { notices << m; });

  // ---- commands exist
  for (const char* id : {"edit.prefs", "tool.toolbox", "tool.toolboxEnable", "pen.settings", "palette.menu"})
    check(ed.action(id)->isEnabled() && !ed.action(id)->text().isEmpty(), QString("settings: command %1 registered and enabled").arg(id));
  {
    bool inTools = false;
    for (QAction* a : win.menu("tools")->actions()) if (a == ed.action("tool.toolboxEnable")) inTools = true;
    check(inTools, "settings: Herramientas > Activar o desactivar el cuadro de herramientas is in the menu");
  }

  // ================================================================= TOOL BOX
  {
    QJsonDocument bad = QJsonDocument::fromJson(R"({"enabled":"x","rows":[["tool.pen","nope","tool.pen",5,"edit.undo","|","|"],"x",["color:#FF0000","color:red","bar:save","tool.pen"]],"closeOnPick":false})");
    const ToolboxSettings s = sanitizeToolbox(bad.object());
    check(s.enabled && s.rows.size() == 3 && s.rows[0] == QStringList({"tool.pen", "edit.undo", "|", "|"}) && s.rows[1].isEmpty() && s.rows[2] == QStringList({"color:#FF0000", "bar:save"}) && !s.closeOnPick,
          "toolbox: settings from anything are sanitised (unknown/repeated dropped, separators repeat)");
  }
  c->applyToolbox(defaultToolbox());
  ToolboxWidget* tb = c->toolbox();
  ed.setTool("pen");
  QElapsedTimer tm;
  tm.start();
  const QPoint centre = win.mapToGlobal(QPoint(win.width() / 2, win.height() / 2));
  check(tb->open(centre), "toolbox: opens");
  const double openMs = double(tm.nsecsElapsed()) / 1e6;
  pump();
  check(tb->isOpen() && tb->zoneCount() == 3 && flat(tb).size() == 14 && flat(tb).count("|") == 1, "toolbox: three zones, 14 entries, one separator");
  check(std::abs(tb->frameGeometry().center().x() - centre.x()) <= tb->width() / 2 + 4 && tb->geometry().top() >= 0, "toolbox: opens centred on the pointer, inside the screen");
  {
    int active = 0;
    for (QToolButton* b : tb->bodyWidget()->findChildren<QToolButton*>()) if (b->property("active").toBool()) active++;
    auto* pen = qobject_cast<QToolButton*>(tb->itemWidget("tool.pen"));
    check(pen && pen->property("active").toBool() && active == 1, "toolbox: the current tool is marked (and only it)");
    int icons = 0, buttons = 0;
    for (const QString& id : flat(tb)) if (id != "|") { buttons++; auto* b = qobject_cast<QToolButton*>(tb->itemWidget(id)); if (b && !b->icon().isNull()) icons++; }
    check(buttons == 13 && icons == buttons, "toolbox: every button has an icon");
    check(pen && pen->width() >= 48 && pen->height() >= 48, QString("toolbox: tablet-friendly buttons (%1 x %2 px)").arg(pen ? pen->width() : 0).arg(pen ? pen->height() : 0));
  }
  QPixmap shot = tb->grab();
  shot.save(shots + "/settings_toolbox.png");
  qobject_cast<QToolButton*>(tb->itemWidget("tool.eraser"))->click();
  check(ed.tool() == "eraser" && !tb->isOpen(), "toolbox: choosing a tool changes it and closes the box");
  ed.action("tool.toolbox")->trigger();
  check(tb->isOpen(), "toolbox: the command (key Q) opens it");
  ed.action("tool.toolbox")->trigger();
  check(!tb->isOpen(), "toolbox: the command again closes it (toggle)");
  c->showToolbox(centre);
  sendKey(tb, Qt::Key_Q, Qt::NoModifier, "q");
  check(!tb->isOpen(), "toolbox: key Q inside the box closes it");
  c->showToolbox(centre);
  sendKey(tb, Qt::Key_Escape);
  check(!tb->isOpen(), "toolbox: Esc closes it");
  c->showToolbox(centre);
  sendMouse(win.infoWidget(), QEvent::MouseButtonPress, Qt::LeftButton);
  sendMouse(win.infoWidget(), QEvent::MouseButtonRelease, Qt::LeftButton);
  check(!tb->isOpen(), "toolbox: a click outside closes it");
  c->showToolbox(centre);
  sendKey(tb, Qt::Key_Q, Qt::ControlModifier, "q");
  check(tb->isOpen(), "toolbox: Ctrl+Q does not close it");
  tb->closeBox();

  // toolbar items and colours work through the window's commands
  c->applyToolbox(sanitizeToolbox(QJsonDocument::fromJson(R"({"rows":[["bar:rotR","color:#ff0000","bar:pgNext","tool.laser"]]})").object()));
  int navNext = 0;
  QObject::connect(ed.action("nav.next"), &QAction::triggered, &win, [&] { navNext++; });
  c->showToolbox(centre);
  if (auto* b = qobject_cast<QToolButton*>(tb->itemWidget("bar:pgNext"))) b->click();
  check(navNext == 1 || !ed.action("nav.next")->isEnabled(), "toolbox: a toolbar item (next page) runs the window's command");
  c->showToolbox(centre);
  qobject_cast<QToolButton*>(tb->itemWidget("color:#ff0000"))->click();
  check(ed.style().color == "#ff0000", "toolbox: a colour item sets the pen colour");
  c->showToolbox(centre);
  check(qobject_cast<QToolButton*>(tb->itemWidget("color:#ff0000"))->property("active").toBool(), "toolbox: the current colour is marked");
  {
    auto* nonexistent = qobject_cast<QToolButton*>(tb->itemWidget("bar:rotR"));
    check(nonexistent != nullptr && (ed.actions().count("sel.rotR") == 0 || !nonexistent->isEnabled() == !ed.action("sel.rotR")->isEnabled()), "toolbox: an item whose command does not exist yet is shown disabled");
  }
  tb->closeBox();
  Style cs = ed.style();
  cs.color = savedStyle.color;
  ed.setStyle(cs);
  c->applyToolbox(defaultToolbox());

  // pen button 1 -> toolbox, at the pen; draws nothing
  {
    TabletSettings t = ed.tablet();
    t.barrel = PenAction::Toolbox;
    c->applyTablet(t);
    const int n0 = elementsOn(ed, page);
    PointerEvent pe = v->makeEvent(page, QPointF(200, 200), 0.5);
    pe.pressedBarrel = true;
    pe.globalPos = QPointF(win.mapToGlobal(QPoint(420, 330)));
    ed.pointerDown(pe);
    ed.pointerUp(pe);
    pump();
    check(tb->isOpen() && std::abs(tb->frameGeometry().center().x() - 420) <= tb->width() / 2 + 4 && elementsOn(ed, page) == n0, "toolbox: the pen's first button opens it at the pen and draws nothing");
    tb->closeBox();
  }

  // customise mode
  c->showToolbox(centre);
  tm.restart();
  tb->smallButton("Editar")->click();
  const double editMs = double(tm.nsecsElapsed()) / 1e6;
  pump();
  check(tb->isEditing() && tb->poolBox() != nullptr, "toolbox: Editar shows the catalogue and drop zones");
  check(qobject_cast<QToolButton*>(tb->itemWidget("tool.pen"))->cursor().shape() == Qt::OpenHandCursor, "toolbox: buttons are draggable in customise mode (grab cursor)");
  sendMouse(win.infoWidget(), QEvent::MouseButtonPress, Qt::LeftButton);
  check(tb->isOpen(), "toolbox: a click outside does not close it while customising");
  {
    const QString t0 = ed.tool();
    qobject_cast<QToolButton*>(tb->itemWidget("tool.select"))->click();
    check(ed.tool() == t0, "toolbox: clicking a button while customising does not pick it");
  }
  QPixmap shotEdit = tb->grab();
  shotEdit.save(shots + "/settings_toolbox_edit.png");
  tb->setPoolGroup("Navegación");
  check(tb->poolIds().contains("bar:pgNext") && tb->poolIds().contains("view.zoomIn") && !tb->poolIds().contains("tool.pen"), "toolbox: the catalogue shows a category");
  tb->setPoolGroup("Colores");
  const QStringList colourIds = tb->poolIds();
  check(colourIds.size() >= 5 && std::all_of(colourIds.begin(), colourIds.end(), [](const QString& s) { return s.startsWith("color:"); }), "toolbox: the Colores category has the palette's colours");
  tb->setPoolGroup("Separadores");
  check(tb->poolIds() == QStringList({"|"}), "toolbox: the Separadores category is the separator");
  tb->searchField()->setText("rotar");
  check(tb->poolIds().contains("bar:rotL") && tb->poolIds().contains("bar:rotR") && !tb->poolIds().contains("tool.pen"), "toolbox: search by name");
  tb->searchField()->setText("ALEJAR");
  check(tb->poolIds() == QStringList({"view.zoomOut"}), "toolbox: search ignores case");
  tb->searchField()->setText("zzzz");
  check(tb->poolIds().isEmpty(), "toolbox: search with no match is empty");
  tb->searchField()->setText("pagina");
  check(tb->poolIds().contains("bar:pgNext"), "toolbox: search ignores accents");
  tb->searchField()->setText("");
  tb->setPoolGroup("Herramientas");

  // drops (the real drop path: a QDropEvent sent to the zone)
  dropOn(tb->zoneBox(2), {"bar:pgNext", -1, -1}, endOf(tb->zoneBox(2)));
  check(tb->zoneIds(2) == QStringList({"edit.undo", "edit.redo", "bar:pgNext"}), "toolbox: drop from the catalogue appends to the zone");
  {
    QWidget* undo = tb->itemWidget("edit.undo");
    dropOn(tb->zoneBox(2), {"bar:pgPrev", -1, -1}, undo->geometry().center());
  }
  check(tb->zoneIds(2) == QStringList({"bar:pgPrev", "edit.undo", "edit.redo", "bar:pgNext"}), "toolbox: drop on a button inserts before it");
  // a button dragged from the window's toolbar carries its item
  {
    QAction* save = ed.action("file.save");
    const QString item = c->itemOfAction(save);
    check(item == "bar:save", "toolbox: the toolbar's Guardar button stands for bar:save (" + item + ")");
    check(c->itemOfAction(ed.action("tool.pen")) == "tool.pen" && c->itemOfAction(ed.action("edit.undo")) == "edit.undo", "toolbox: tool and command buttons map to their items");
    const auto cols = colorActions(win);
    check(!cols.isEmpty() && c->itemOfAction(cols[0]) == "color:" + cols[0]->data().toString().toLower(), "toolbox: colour swatches map to colour items");
    dropOn(tb->zoneBox(1), {item, -1, -1}, endOf(tb->zoneBox(1)));
    check(tb->zoneIds(1).last() == "bar:save", "toolbox: a drop from the toolbar adds the button");
  }
  tb->smallButton("+ Zona")->click();
  check(tb->zoneCount() == 4 && tb->zoneIds(3).isEmpty(), "toolbox: + Zona adds an empty zone");
  dropOn(tb->zoneBox(3), {"tool.laser", -1, -1}, endOf(tb->zoneBox(3)));
  dropOn(tb->zoneBox(3), {"|", -1, -1}, endOf(tb->zoneBox(3)));
  dropOn(tb->zoneBox(3), {"|", -1, -1}, endOf(tb->zoneBox(3)));
  check(tb->zoneIds(3) == QStringList({"tool.laser", "|", "|"}), "toolbox: separators may repeat");
  dropOn(tb->zoneBox(0), {"tool.laser", 3, 0}, endOf(tb->zoneBox(0)));
  check(tb->zoneIds(0).last() == "tool.laser" && tb->zoneIds(3) == QStringList({"|", "|"}) && flat(tb).count("tool.laser") == 1, "toolbox: moving between zones takes the button out of its place");
  dropOn(tb->zoneBox(0), {"tool.laser", 0, 8}, tb->itemWidget("tool.pen")->geometry().center());
  check(tb->zoneIds(0)[0] == "tool.laser" && tb->zoneIds(0)[1] == "tool.pen", "toolbox: reorder inside a zone");
  dropOn(tb->zoneBox(2), {"tool.pen", -1, -1}, endOf(tb->zoneBox(2)));
  check(flat(tb).count("tool.pen") == 1 && tb->zoneIds(2).last() == "tool.pen", "toolbox: a button already in the panel dropped again moves");
  tb->removeAt(3, 0);   // what the end of a drag outside the box does
  check(tb->zoneIds(3) == QStringList({"|"}), "toolbox: a separator dragged out is removed");
  tb->removeAt(1, tb->settings().rows[1].indexOf("tool.fill"));
  check(!flat(tb).contains("tool.fill"), "toolbox: a button dragged out is removed");
  dropOn(tb->poolBox(), {"tool.text", 1, int(tb->settings().rows[1].indexOf("tool.text"))}, QPoint(10, 10));
  check(!flat(tb).contains("tool.text"), "toolbox: a button dropped on the catalogue is removed");
  {
    // the delete button of the zone (a "×" in its corner)
    WrapBox* z = tb->zoneBox(3);
    QToolButton* del = nullptr;
    for (QToolButton* b : z->findChildren<QToolButton*>()) if (b->text() == QString::fromUtf8("×")) del = b;
    check(del != nullptr, "toolbox: every zone has a delete button while customising");
    if (del) del->click();
    check(tb->zoneCount() == 3, "toolbox: removing a zone");
  }
  c->saveNow();
  {
    const ToolboxSettings saved = prefsToolbox(readPrefs());
    check(asList(saved.rows) == rowsOf(tb) && c->toolboxSettings().rows.size() == 3, "toolbox: every edit is saved in prefs.json (same JSON as the Electron app)");
  }
  tb->smallButton("Listo")->click();
  check(!tb->isEditing() && tb->poolBox() == nullptr, "toolbox: Listo leaves customise mode");
  tb->closeBox();
  c->showToolbox(centre);
  check(rowsOf(tb) == asList(c->toolboxSettings().rows), "toolbox: reopens with the changes");
  tb->smallButton("Editar")->click();
  tb->smallButton("Restaurar")->click();
  check(rowsOf(tb) == asList(defaultToolbox().rows), "toolbox: Restaurar puts the default layout back");
  {
    auto boxes = tb->bodyWidget()->findChildren<QCheckBox*>();
    check(!boxes.isEmpty(), "toolbox: 'Cerrar al elegir' option present");
    if (!boxes.isEmpty()) boxes.first()->setChecked(false);
    tb->smallButton("Listo")->click();
    qobject_cast<QToolButton*>(tb->itemWidget("tool.highlighter"))->click();
    check(tb->isOpen() && ed.tool() == "highlighter" && !c->toolboxSettings().closeOnPick, "toolbox: with 'Cerrar al elegir' off it stays open after a pick");
    tb->closeBox();
  }
  c->applyToolbox(sanitizeToolbox(QJsonDocument::fromJson(R"({"items":["tool.pen","tool.select"]})").object()));
  c->showToolbox(centre);
  check(tb->zoneCount() == 1 && tb->zoneIds(0) == QStringList({"tool.pen", "tool.select"}), "toolbox: an old saved list (before the zones) is one zone");
  tb->closeBox();
  // switched off
  c->applyToolbox(defaultToolbox());
  ed.action("tool.toolboxEnable")->trigger();
  notices.clear();
  ed.action("tool.toolbox")->trigger();
  check(!c->toolboxSettings().enabled && !tb->isOpen(), "toolbox: switched off, the command does not open it");
  check(!notices.isEmpty() && notices.last().contains("desactivado"), "toolbox: switched off, the status bar says how to switch it on");
  {
    TabletSettings t = ed.tablet();
    t.barrel = PenAction::Toolbox;
    c->applyTablet(t);
    PointerEvent pe = v->makeEvent(page, QPointF(200, 200), 0.5);
    pe.pressedBarrel = true;
    pe.globalPos = QPointF(win.mapToGlobal(QPoint(420, 330)));
    ed.pointerDown(pe);
    ed.pointerUp(pe);
    check(!tb->isOpen(), "toolbox: switched off, the pen's button does not open it");
  }
  ed.action("tool.toolboxEnable")->trigger();
  ed.action("tool.toolbox")->trigger();
  check(c->toolboxSettings().enabled && tb->isOpen(), "toolbox: switched on again, the command opens it");
  tb->closeBox();
  c->applyToolbox(defaultToolbox());

  // ================================================================= PEN TIPS AS TOOL BOX BUTTONS
  {
    const TabletSettings t0 = ed.tablet();
    for (const char* id : {"brush.pen", "brush.ink", "brush.fountain", "brush.brush"})
      check(isToolboxItemId(QString::fromLatin1(id)) && toolboxItemById(QString::fromLatin1(id), {}) && ed.action(QString::fromLatin1(id)) && ed.action(QString::fromLatin1(id))->isEnabled(),
            QString("tips: %1 is a tool box item and a command").arg(id));
    ToolboxSettings withTips = defaultToolbox();
    withTips.rows[0] << "brush.ink" << "brush.brush";
    check(sanitizeToolbox(toolboxToJson(withTips)) == withTips, "tips: a layout with tip buttons survives save and load");
    c->runToolboxItem("brush.fountain");
    check(ed.tablet().brush == BrushKind::Fountain && c->itemActive("brush.fountain") && !c->itemActive("brush.ink"), "tips: a tool box tip button selects the tip and the box shows which one is in use");
    const double w = ed.style().penWidth;
    c->runToolboxItem("brush.brush");
    check(ed.tablet().brush == BrushKind::Brush && ed.style().penWidth == w, "tips: choosing a tip keeps the pen size in use");
    TabletSettings back = ed.tablet();
    back.brush = t0.brush;
    c->applyTablet(back);
  }

  // ================================================================= KEYS, WHEEL, MOUSE BUTTONS
  {
    TabletSettings t = ed.tablet();
    t.barrel = PenAction::Toolbox;
    t.keys = {{"Ctrl+Alt+1", "tool.highlighter"}, {"Ctrl+Alt+2", "pen.sizeUp"}, {"Ctrl+Alt+3", "brush.next"}, {"F13", "edit.undo"}, {"Alt+Shift+Q", "tool.previous"}};
    c->applyTablet(t);
    ed.setTool("pen");
    sendKey(v, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier, "1");
    check(ed.tool() == "highlighter", "keys: a bound key combination changes the tool");
    sendKey(v, Qt::Key_Q, Qt::AltModifier | Qt::ShiftModifier, "Q");
    check(ed.tool() == "pen", "keys: tool.previous goes back");
    const double w0 = ed.style().penWidth;
    sendKey(v, Qt::Key_2, Qt::ControlModifier | Qt::AltModifier, "2");
    check(std::abs(ed.style().penWidth - std::round(w0 * 1.25 * 100) / 100) < 1e-9 && ed.tablet().size == ed.style().penWidth, "keys: pen.sizeUp grows the pen and the tablet's reference size");
    sendKey(v, Qt::Key_3, Qt::ControlModifier | Qt::AltModifier, "3");
    check(ed.tablet().brush == BrushKind::Ink, "keys: brush.next cycles the tip");
    const int n0 = elementsOn(ed, page);
    ed.pointerDown(v->makeEvent(page, QPointF(100, 100), 0.5));
    for (int i = 1; i < 12; i++) ed.pointerMove(v->makeEvent(page, QPointF(100 + i * 10, 100), 0.6));
    ed.pointerUp(v->makeEvent(page, QPointF(210, 100), 0.5));
    const bool drew = elementsOn(ed, page) == n0 + 1;
    sendKey(v, Qt::Key_F13);
    check(drew && elementsOn(ed, page) == n0, "keys: F13 bound to undo undoes the stroke");
    {
      QKeyEvent so(QEvent::ShortcutOverride, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier, 0, 0, 0, "1");
      so.ignore();
      QApplication::sendEvent(v, &so);
      QKeyEvent so2(QEvent::ShortcutOverride, Qt::Key_9, Qt::ControlModifier | Qt::AltModifier, 0, 0, 0, "9");
      so2.ignore();
      QApplication::sendEvent(v, &so2);
      check(so.isAccepted() && !so2.isAccepted(), "keys: a bound combination wins over the window's shortcuts (ShortcutOverride accepted), others do not");
    }
    // typed on a keyboard: Shift changes the key but not the physical key (the virtual key of the digit)
    t.keys = {{"Ctrl+Alt+Shift+1", "tool.eraser"}};
    c->applyTablet(t);
    ed.setTool("pen");
    sendKey(v, Qt::Key_Exclam, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier, "!", '1');
    check(ed.tool() == "eraser", "keys: the physical key names the combination (Ctrl+Alt+Shift+1 although Shift made it '!')");
    // unbound
    t.keys = {};
    c->applyTablet(t);
    ed.setTool("pen");
    sendKey(v, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier, "1");
    check(ed.tool() == "pen", "keys: unbound combination does nothing");
    t.brush = BrushKind::Pen;
    t.size = savedTablet.size;
    c->applyTablet(t);

    // wheel and extra mouse buttons
    t.keys = {{"Ctrl+Rueda arriba", "view.zoomIn"}, {"Rueda abajo", "view.zoomOut"}, {"Botón atrás", "tool.eraser"}};
    c->applyTablet(t);
    v->setZoom(1);
    const double z0 = v->zoom();
    sendWheel(v->viewport(), 120, Qt::ControlModifier);
    const double zIn = v->zoom();
    check(std::abs(zIn / z0 - 1.2) < 1e-6, "wheel: Ctrl+wheel up bound to view.zoomIn runs the action instead of the page's own zoom");
    sendWheel(v->viewport(), -120, Qt::NoModifier);
    check(std::abs(v->zoom() - z0) < 1e-6, "wheel: wheel down bound to view.zoomOut");
    ed.setTool("pen");
    sendMouse(v->viewport(), QEvent::MouseButtonPress, Qt::BackButton);
    const bool pressed = ed.tool() == "eraser";
    const bool r = sendMouse(v->viewport(), QEvent::MouseButtonRelease, Qt::BackButton);
    check(pressed && r && ed.tool() == "pen", "mouse: the back button bound to a tool selects it while held, its release is swallowed and gives the tool before back");
    t.keys = {};
    c->applyTablet(t);
    v->setZoom(1);
    sendWheel(v->viewport(), 120, Qt::ControlModifier);
    check(std::abs(v->zoom() / z0 - std::pow(1.0015, 120)) < 1e-3, QString("wheel: unbound, Ctrl+wheel keeps the page's own zoom (x%1)").arg(v->zoom() / z0, 0, 'f', 4));
    v->setZoom(1);
    ed.setTool("pen");
  }

  // ================================================================= TABLET DIALOG
  c->applyTablet(savedTablet);
  tm.restart();
  ed.action("pen.settings")->trigger();
  const double tabletMs = double(tm.nsecsElapsed()) / 1e6;
  TabletDialog* td = c->tabletDialog();
  pump();
  check(td->isVisible(), "tablet: the menu command opens the dialog");
  {
    int cards = 0, on = 0;
    for (BrushInfo b : brushes()) { BrushCard* k = td->card(b.kind); if (k) { cards++; if (k->isChecked()) on++; } }
    check(cards == 4 && on == 1, "tablet: four tip cards, one selected");
  }
  td->card(BrushKind::Ink)->click();
  check(ed.tablet().brush == BrushKind::Ink, "tablet: picking a tip applies it");
  td->sizeSpin()->setValue(4.5);
  check(std::abs(ed.style().penWidth - 4.5) < 1e-9 && std::abs(ed.tablet().size - 4.5) < 1e-9, "tablet: the size field sets the pen width");
  td->presetCombo()->setCurrentIndex(3);
  emit td->presetCombo()->activated(3);
  check(std::abs(ed.tablet().curve.x1 - curvePresets()[2].curve.x1) < 1e-9 && std::abs(PressureMap(ed.tablet().curve).apply(0.3) - PressureMap(curvePresets()[2].curve).apply(0.3)) < 1e-9, "tablet: a pressure preset applies");
  {
    CurveWidget* cw = td->curveWidget();
    const PressureCurve c0 = ed.tablet().curve;
    const QPoint from = cw->toPixel(c0.x2, c0.y2).toPoint(), to = cw->toPixel(0.6, 0.95).toPoint();
    sendMouse(cw, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent mv(QEvent::MouseMove, QPointF(to), QPointF(cw->mapToGlobal(to)), QPointF(cw->mapToGlobal(to)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cw, &mv);
    sendMouse(cw, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, to);
    const PressureCurve c1 = ed.tablet().curve;
    check(std::abs(c1.x2 - 0.6) < 0.02 && std::abs(c1.y2 - 0.95) < 0.02 && std::abs(c1.x1 - c0.x1) < 1e-9, "tablet: dragging the second handle of the curve changes only that point");
    cw->mapToGlobal(QPoint());
    const QPointF h = cw->toPixel(0.5, 0.5);
    const QPointF back = cw->toPlot(h);
    check(std::abs(back.x() - 0.5) < 1e-9 && std::abs(back.y() - 0.5) < 1e-9, "tablet: curve plot coordinates round-trip");
  }
  // a real tablet event: the pad draws with the chosen tip and the readout follows
  {
    auto* dev = new QPointingDevice("settings-selftest pen", 77, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure | QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt, 1, 3, QString(), QPointingDeviceUniqueId(), &win);
    PadWidget* pad = td->pad();
    pad->clear();
    auto tablet = [&](QEvent::Type t, QPointF p, double pressure, Qt::MouseButton b, Qt::MouseButtons bs) {
      QTabletEvent ev(t, dev, p, pad->mapToGlobal(p.toPoint()), pressure, 12, -5, 0, 0, 0, Qt::NoModifier, b, bs);
      QApplication::sendEvent(pad, &ev);
    };
    tablet(QEvent::TabletPress, {30, 60}, 0.42, Qt::LeftButton, Qt::LeftButton);
    const QString ro = td->readoutText();
    for (int i = 1; i <= 30; i++) tablet(QEvent::TabletMove, {30 + i * 8.0, 60 + 30 * std::sin(i * 0.3)}, 0.2 + 0.7 * std::abs(std::sin(i * 0.12)), Qt::NoButton, Qt::LeftButton);
    tablet(QEvent::TabletRelease, {280, 60}, 0, Qt::LeftButton, Qt::NoButton);
    check(pad->strokeCount() == 1, "tablet: a stroke on the test sheet through real tablet events");
    check(ro.contains("42.0 %") && ro.contains("X 12") && ro.contains("pen"), "tablet: the live readout shows pressure, tilt and device (" + ro.left(40).replace('\n', ' ') + "...)");
    // mouse on the pad
    sendMouse(pad, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, QPoint(300, 40));
    QMouseEvent mv(QEvent::MouseMove, QPointF(340, 70), QPointF(pad->mapToGlobal(QPoint(340, 70))), QPointF(pad->mapToGlobal(QPoint(340, 70))), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pad, &mv);
    sendMouse(pad, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, QPoint(340, 70));
    check(pad->strokeCount() == 2, "tablet: the mouse draws on the test sheet too");
    // a nicer sample for the screenshot
    pad->clear();
    for (int s = 0; s < 3; s++) {
      pad->begin(QPointF(20, 40 + s * 30), 0.1, true, 0);
      for (int i = 1; i <= 70; i++) pad->addPoint(QPointF(20 + i * 8.0, 40 + s * 30 + 14 * std::sin(i * 0.25 + s)), 0.1 + 0.85 * std::abs(std::sin(i * 0.07 + s)), i * 8);
      pad->end();
    }
  }
  td->penCombo(0)->setCurrentIndex(td->penCombo(0)->findData("eraser"));
  emit td->penCombo(0)->activated(td->penCombo(0)->currentIndex());
  check(ed.tablet().barrel == PenAction::Eraser, "tablet: the pen button selector applies (button 1 -> goma)");
  td->penCombo(0)->setCurrentIndex(td->penCombo(0)->findData("toolbox"));
  emit td->penCombo(0)->activated(td->penCombo(0)->currentIndex());
  check(ed.tablet().barrel == PenAction::Toolbox && td->penCombo(1)->currentData().toString() == "hand" && td->penCombo(2)->currentData().toString() == "eraser", "tablet: defaults barrel -> cuadro, barrel2 -> mano, eraser end -> goma");
  check(td->current().barrel == PenAction::Toolbox, "tablet: the dialog and the editor agree");
  // express keys
  {
    int n = 0;
    for (int i = 0; i < 10; i++) if (td->expressCombo(i)) n++;
    check(n == 10 && td->findChild<QLabel*>() != nullptr, "tablet: ten express keys");
    bool label = false;
    for (QLabel* l : td->findChildren<QLabel*>()) if (l->text().contains("Tecla 1  →  en el controlador pulsa Ctrl+Alt+Mayús+1")) label = true;
    check(label, "tablet: express row text names the key as the driver must send it");
    QComboBox* e0 = td->expressCombo(0);
    e0->setCurrentIndex(e0->findData("tool.eraser"));
    emit e0->activated(e0->currentIndex());
    check(ed.tablet().keys.value("F13") == "tool.eraser" && ed.tablet().keys.value("Ctrl+Alt+Shift+1") == "tool.eraser", "tablet: an express key binds both its combination and its F-key");
    ed.setTool("pen");
    td->hide();   // while the dialog is the active window the keys are the dialog's own (as in the Electron app)
    sendKey(v, Qt::Key_F13);
    td->show();
    check(ed.tool() == "eraser", "tablet: the express key works");
    e0->setCurrentIndex(0);
    emit e0->activated(0);
    check(!ed.tablet().keys.contains("F13") && !ed.tablet().keys.contains("Ctrl+Alt+Shift+1"), "tablet: choosing 'nada' clears both");
    td->suggestButton()->click();
    check(ed.tablet().keys.value("F13") == "tool.pen" && ed.tablet().keys.value("Ctrl+Alt+Shift+2") == "tool.eraser" && ed.tablet().keys.value("F14") == "tool.eraser" && ed.tablet().keys.value("F17") == "edit.undo" && td->expressCombo(1)->currentData().toString() == "tool.eraser", "tablet: 'Proponer una asignación' fills the empty keys");
  }
  // binding keys, wheel turns and mouse buttons from the dialog
  {
    QComboBox* ac = td->actionCombo();
    ac->setCurrentIndex(ac->findData("tool.eraser"));
    emit ac->activated(ac->currentIndex());
    td->captureButton()->click();
    check(c->capturing(), "tablet: 'Asignar tecla…' starts the capture");
    sendKey(td, Qt::Key_F18, Qt::ControlModifier);
    check(!c->capturing() && ed.tablet().keys.value("Ctrl+F18") == "tool.eraser", "tablet: the next key press is bound to the chosen action");
    int rows = td->keyRowCount();
    check(rows >= 1, "tablet: the binding is listed");
    QPushButton* del = nullptr;
    pump();
    for (QPushButton* b : td->findChildren<QPushButton*>()) if (b->text() == "Quitar" && b->isVisibleTo(td)) { del = b; break; }
    int before = ed.tablet().keys.size();
    // the row of Ctrl+F18 sorts after the F-keys: press its Quitar button via the dialog's API as the click does
    td->removeKey("Ctrl+F18");
    check(del != nullptr && !ed.tablet().keys.contains("Ctrl+F18") && ed.tablet().keys.size() == before - 1, "tablet: Quitar unbinds it");
    td->captureButton()->click();
    sendWheel(td, 120, Qt::ControlModifier);
    check(ed.tablet().keys.value("Ctrl+Rueda arriba") == "tool.eraser", "tablet: a wheel turn with Ctrl binds like a key (the remote's dial)");
    td->captureButton()->click();
    sendMouse(td, QEvent::MouseButtonPress, Qt::BackButton);
    sendMouse(td, QEvent::MouseButtonRelease, Qt::BackButton);
    check(ed.tablet().keys.value("Botón atrás") == "tool.eraser", "tablet: an extra mouse button binds too");
    td->captureButton()->click();
    sendKey(td, Qt::Key_Escape);
    check(!c->capturing() && td->isVisible(), "tablet: Esc cancels the capture without closing the dialog");
    sendKey(td, Qt::Key_Escape);
    check(!td->isVisible(), "tablet: Esc closes the dialog");
  }
  // saved with the preferences, and another preference change must not undo them
  {
    TabletSettings t = ed.tablet();
    t.brush = BrushKind::Fountain;
    t.size = 3.5;
    t.curve = curvePresets()[1].curve;
    t.barrel = PenAction::Select;
    t.keys = {{"Ctrl+Alt+9", "tool.eraser"}};
    c->applyTablet(t);
    c->saveNow();
    const TabletSettings stored = prefsTablet(readPrefs());
    check(stored.brush == BrushKind::Fountain && stored.size == 3.5 && stored.barrel == PenAction::Select && stored.keys.value("Ctrl+Alt+9") == "tool.eraser" && std::abs(stored.curve.y1 - curvePresets()[1].curve.y1) < 1e-9, "tablet: settings are saved in prefs.json");
    Prefs p = c->prefs();
    p.theme = "neon";
    c->applyPrefs(p);
    check(ed.tablet().brush == BrushKind::Fountain && ed.style().penWidth == 3.5, "tablet: another preference change does not undo them");
    p.theme = "classic";
    c->applyPrefs(p);
    // a size dot of the toolbar changes the tablet's reference size too
    Style s = ed.style();
    s.penWidth = 2.26;
    ed.setStyle(s);
    check(std::abs(ed.tablet().size - 2.26) < 1e-9, "tablet: a width chosen in the toolbar is the tablet's size too");
    c->applyTablet(savedTablet);
  }

  // ================================================================= SIZE PRESETS
  {
    ed.setTool("pen");
    pump();
    auto sz = sizeActions(win);
    const auto& defPen = defaultSizes().of(SizeKind::Pen);
    check(int(sz.size()) == int(defPen.size()) && sz.size() >= 5, "sizes: the pen shows its presets in the toolbar");
    bool grow = true;
    for (size_t i = 1; i < defPen.size(); i++) grow = grow && dotSize(SizeKind::Pen, defPen[i]) >= dotSize(SizeKind::Pen, defPen[i - 1]);
    check(grow && dotSize(SizeKind::Pen, defPen.back()) > dotSize(SizeKind::Pen, defPen.front()), "sizes: dots grow with the size (dotSize)");
    bool icons = true;
    for (QAction* a : sz) icons = icons && !a->icon().isNull();
    check(icons, "sizes: every preset has its dot");
    sz[3]->trigger();
    check(std::abs(ed.style().penWidth - defPen[3]) < 1e-9 && std::abs(ed.tablet().size - ed.style().penWidth) < 1e-9, "sizes: a pen preset applies and is the tablet's size too");
    sz = sizeActions(win);
    int checked = 0;
    for (QAction* a : sz) if (a->isChecked()) checked++;
    check(sz[3]->isChecked() && checked == 1, "sizes: the current one is marked");
    const double penBefore = ed.style().penWidth;
    ed.setTool("eraser");
    sz = sizeActions(win);
    check(int(sz.size()) == int(defaultSizes().of(SizeKind::Eraser).size()), "sizes: the eraser has its own presets");
    sz[1]->trigger();
    check(std::abs(ed.style().eraserRadius - defaultSizes().of(SizeKind::Eraser)[1]) < 1e-9 && ed.style().penWidth == penBefore, "sizes: an eraser preset applies and leaves the pen alone");
    ed.setTool("eraserPart");
    sz = sizeActions(win);
    bool shared = false;
    for (QAction* a : sz) if (a->isChecked()) shared = true;
    check(shared, "sizes: both erasers share the presets");
    if (ed.hasTool("select")) {   // a tool with no size of its own shows the pen's presets, and picking one sets the pen width
      ed.setTool("select"); pump();
      const auto sel = sizeActions(win);
      check(int(sel.size()) == int(defaultSizes().of(SizeKind::Pen).size()), "sizes: a tool with no size shows the pen presets");
      if (!sel.isEmpty()) {
        sel.last()->trigger();
        check(std::abs(ed.style().penWidth - defaultSizes().of(SizeKind::Pen).back()) < 1e-9, "sizes: a pen preset picked under the selection tool sets the pen width");
      }
    }
    ed.setTool("highlighter");
    check(int(sizeActions(win).size()) == int(defaultSizes().of(SizeKind::Highlighter).size()), "sizes: the highlighter has its own presets");

    // right click, Alt+click and long press store the current size there
    ed.setTool("pen");
    Style s = ed.style();
    s.penWidth = 4.2;
    ed.setStyle(s);
    sz = sizeActions(win);
    {
      QWidget* w0 = widgetOf(win, sz[0]);
      sendMouse(w0, QEvent::MouseButtonPress, Qt::RightButton);
      sendMouse(w0, QEvent::MouseButtonRelease, Qt::RightButton);
    }
    check(std::abs(win.sizePresets().of(SizeKind::Pen)[0] - 4.2) < 1e-9, "sizes: right click stores the current size in that button");
    c->saveNow();
    check(std::abs(prefsSizes(readPrefs()).of(SizeKind::Pen)[0] - 4.2) < 1e-9, "sizes: and it is saved in prefs.json");
    s.penWidth = 2.5;
    ed.setStyle(s);
    sz = sizeActions(win);
    {
      QWidget* w1 = widgetOf(win, sz[1]);
      sendMouse(w1, QEvent::MouseButtonPress, Qt::LeftButton, Qt::AltModifier);
      sendMouse(w1, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::AltModifier);
    }
    check(std::abs(win.sizePresets().of(SizeKind::Pen)[1] - 2.5) < 1e-9 && std::abs(ed.style().penWidth - 2.5) < 1e-9, "sizes: Alt+click stores it too (and does not apply the old value)");
    check(std::abs(win.sizePresets().of(SizeKind::Pen)[0] - 4.2) < 1e-9 && std::abs(win.sizePresets().of(SizeKind::Pen)[2] - defPen[2]) < 1e-9, "sizes: only that button changed");
    s.penWidth = 3.1;
    ed.setStyle(s);
    sz = sizeActions(win);
    QElapsedTimer lp;
    lp.start();
    QPointer<QWidget> w2 = widgetOf(win, sz[2]);
    sendMouse(w2, QEvent::MouseButtonPress, Qt::LeftButton);
    wait(150);
    check(std::abs(win.sizePresets().of(SizeKind::Pen)[2] - defPen[2]) < 1e-9, "sizes: a short press does not store");
    wait(750);
    check(std::abs(win.sizePresets().of(SizeKind::Pen)[2] - 3.1) < 1e-9, "sizes: a long press (650 ms) stores the current size");
    const double dur = double(lp.elapsed());
    {   // the pressed button was replaced by the rebuild: as with a real pen, the release lands on whatever button is under the pointer now
      QWidget* target = w2 ? w2.data() : widgetOf(win, sizeActions(win)[2]);
      sendMouse(target, QEvent::MouseButtonRelease, Qt::LeftButton);
    }
    check(std::abs(ed.style().penWidth - 3.1) < 1e-9, "sizes: the release after a long press does not also apply a size");
    Q_UNUSED(dur);
    // outside the limits the whole list would be reset: the size is clamped
    ed.setTool("eraser");
    s = ed.style();
    s.eraserRadius = 500;
    ed.setStyle(s);
    sz = sizeActions(win);
    {
      QWidget* we = widgetOf(win, sz[0]);
      sendMouse(we, QEvent::MouseButtonPress, Qt::RightButton);
      sendMouse(we, QEvent::MouseButtonRelease, Qt::RightButton);
    }
    check(win.sizePresets().of(SizeKind::Eraser)[0] == sizeLimits(SizeKind::Eraser).hi && win.sizePresets().of(SizeKind::Eraser).size() == defaultSizes().of(SizeKind::Eraser).size(), "sizes: a size outside the limits is clamped, the list is kept");
    // survive a preferences change
    c->applyPrefs(c->prefs());
    check(std::abs(win.sizePresets().of(SizeKind::Pen)[0] - 4.2) < 1e-9 && std::abs(win.sizePresets().of(SizeKind::Pen)[1] - 2.5) < 1e-9, "sizes: stored presets survive a preferences change");
    c->saveNow();
    check(sanitizeSizes(QJsonDocument::fromJson(readPrefs().sizes.toUtf8()).object()).of(SizeKind::Pen)[0] == 4.2, "sizes: prefs.json holds them in the Electron JSON form");
    c->applySizes(defaultSizes());
    s = ed.style();
    s.penWidth = savedStyle.penWidth;
    s.eraserRadius = savedStyle.eraserRadius;
    ed.setStyle(s);
    ed.setTool("pen");
  }

  // ================================================================= PALETTES
  {
    check(colorActions(win).size() == 5 && c->paletteId() == "classic", "palette: starts with the classic five colours");
    int items = 0;
    QMenu* sub = nullptr;
    for (QAction* a : win.menu("view")->actions()) if (a->menu() && a->text() == "Paleta de colores") sub = a->menu();
    if (sub) items = int(sub->actions().size());
    check(items == int(palettes().size()) && items >= 9, "palette: Ver > Paleta de colores lists every preset");
    for (QAction* a : sub->actions()) if (a->data().toString() == "neondreams") a->trigger();
    auto cols = colorActions(win);
    check(c->paletteId() == "neondreams" && cols.size() == 8 && cols[1]->data().toString() == "#ff2a9d", "palette: choosing a preset replaces the colour buttons");
    cols[2]->trigger();
    check(ed.style().color == "#05e1ff", "palette: a swatch sets the pen colour");
    cols = colorActions(win);
    int on = 0;
    for (QAction* a : cols) if (a->isChecked()) on++;
    check(cols[2]->isChecked() && on == 1, "palette: the current colour is marked");
    c->saveNow();
    check(readPrefs().palette == "neondreams", "palette: the choice is saved");
    c->applyPalette("synthwave");
    cols = colorActions(win);
    on = 0;
    for (QAction* a : cols) if (a->isChecked()) on++;
    check(cols.size() == 8 && on == 0, "palette: a preset without the current colour leaves none marked");
    c->applyPalette("classic");
    Style s = ed.style();
    s.color = "#000000";
    ed.setStyle(s);
    cols = colorActions(win);
    check(cols.size() == 5 && cols[0]->isChecked(), "palette: back to classic keeps working");
    c->runAction("color.next");
    check(ed.style().color == "#3333cc", "keys: color.next goes to the next colour of the palette");
    c->runAction("color.prev");
    check(ed.style().color == "#000000", "keys: color.prev goes back");
    bool valid = true;
    for (const Palette& p : palettes()) {
      QSet<QString> seen;
      for (const QString& col : p.colors) { valid = valid && QRegularExpression("^#[0-9a-f]{6}$").match(col).hasMatch(); seen.insert(col); }
      valid = valid && p.colors.size() >= 5 && seen.size() == p.colors.size();
    }
    check(valid, "palette: every preset is valid (>= 5 distinct #rrggbb colours)");
    check(!ed.action("palette.menu")->icon().isNull() && ed.action("palette.menu")->toolTip().contains("Paleta"), "palette: the toolbar has a palette button with a tooltip");
  }

  // ================================================================= PREFERENCES
  {
    // seed the file with a key this version does not know and a theme the chrome area "wrote"
    {
      Prefs p = readPrefs();
      p.extra["zzUnknown"] = QJsonObject{{"a", 1}};
      p.theme = "neon";
      savePrefs(prefsFile(), p);
    }
    c->loadNow();
    check(c->prefs().theme == "neon" && ed.action("edit.prefs")->isEnabled(), "prefs: loads the file (theme neon) and applies it");
    ed.action("edit.prefs")->trigger();
    PrefsDialog* pd = c->prefsDialog();
    pump();
    check(pd->isVisible() && pd->theme->currentData().toString() == "neon" && !pd->finger->isChecked() && !pd->over->isChecked() && !pd->autosaveOn->isChecked(), "prefs: the dialog opens with the current values");   // changed with the Xournal++ layout: autosave is a check box plus an interval (the interval keeps its value while it is off)
    int applied = 0;
    QObject::connect(c, &SettingsController::prefsApplied, &win, [&] { applied++; });
    pd->finger->setChecked(true);
    pd->over->setChecked(true);
    pd->autosaveOn->setChecked(true);
    pd->autosave->setValue(7);
    pd->theme->setCurrentIndex(pd->theme->findData("classic"));
    pd->palette->setCurrentIndex(pd->palette->findData("vivid"));
    pd->effects->setCurrentIndex(pd->effects->findData("reduced"));
    pd->welcome->setChecked(false);
    QPixmap shotPrefs = pd->grab();
    shotPrefs.save(shots + "/settings_prefs.png");
    pd->accept();
    check(!pd->isVisible() && ed.fingerDraw() && c->prefs().autosaveMinutes == 7 && c->prefs().saveOverOpened && applied > 0, "prefs: accepting applies live (finger drawing, autosave, save over) and closes the dialog");
    check(colorActions(win).size() == 8 && c->paletteId() == "vivid", "prefs: the palette choice replaces the colour buttons");
    c->saveNow();
    const Prefs on = readPrefs();
    check(on.fingerDraw && on.saveOverOpened && on.autosaveMinutes == 7 && on.palette == "vivid" && on.effects == "reduced" && !on.welcome && on.theme == "classic", "prefs: accepted values are written to prefs.json");
    check(on.extra.contains("zzUnknown") && on.extra["zzUnknown"].toObject()["a"].toInt() == 1, "prefs: keys this version does not know are kept");
    // another writer (the chrome area, the Electron app) changes the theme while we are running: our next save must not revert it
    {
      Prefs p = readPrefs();
      p.theme = "neon";
      savePrefs(prefsFile(), p);
    }
    c->applyPalette("classic");
    c->saveNow();
    check(readPrefs().theme == "neon" && readPrefs().palette == "classic", "prefs: a save writes only what this area changed (a theme written meanwhile stays)");
    // cancel changes nothing
    pd->openDialog();
    pd->finger->setChecked(false);
    pd->reject();
    check(ed.fingerDraw(), "prefs: Cancelar changes nothing");
    // restore
    Prefs p = c->prefs();
    p.fingerDraw = false;
    p.saveOverOpened = false;
    p.autosaveMinutes = 0;
    p.theme = "classic";
    p.effects = "auto";
    p.welcome = true;
    c->applyPrefs(p);
    check(!ed.fingerDraw() && c->prefs().autosaveMinutes == 0, "prefs: restored");
    runPenHoldChecks(*c, win, ed, check);
    runInputFixChecks(*c, win, ed, check);
    runPrefsPageChecks(*c, win, ed, prefsFile(), check, out, shots);
    // not writable without a chosen path in a headless window
    check(!win.persistence(), "prefs: this window is headless (persistence off), so it never writes the user's file");
  }

  // ================================================================= measurements and screenshots
  {
    // cost of the application-wide event filter on the pointer path: mouse moves delivered to the page, with and without the filter
    ed.setTool("pen");
    QWidget* vp = v->viewport();
    auto run = [&](int n) {
      QElapsedTimer t;
      t.start();
      for (int i = 0; i < n; i++) {
        QMouseEvent mv(QEvent::MouseMove, QPointF(50 + i % 300, 60 + i % 200), QPointF(vp->mapToGlobal(QPoint(50 + i % 300, 60 + i % 200))), QPointF(vp->mapToGlobal(QPoint(50 + i % 300, 60 + i % 200))), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &mv);
      }
      return double(t.nsecsElapsed()) / 1e3 / n;
    };
    run(2000);
    double with = 1e9, without = 1e9;
    for (int k = 0; k < 3; k++) with = std::min(with, run(20000));
    qApp->removeEventFilter(c);
    for (int k = 0; k < 3; k++) without = std::min(without, run(20000));
    qApp->installEventFilter(c);
    check(with - without < 5.0, QString("perf: the settings event filter costs %1 us per mouse move (%2 us with, %3 us without)").arg(with - without, 0, 'f', 3).arg(with, 0, 'f', 3).arg(without, 0, 'f', 3));
    out(QString("  settings: toolbox open %1 ms (first time, includes building the window), customise mode %2 ms, tablet dialog first open %3 ms").arg(openMs, 0, 'f', 1).arg(editMs, 0, 'f', 1).arg(tabletMs, 0, 'f', 1));
  }
  // screenshots: LOOK at them (tablet-sized targets)
  win.toolBar()->grab().save(shots + "/settings_toolbar.png");
  td->openDialog();
  pump();
  {
    PadWidget* pad = td->pad();   // a few strokes with changing pressure, so the sheet shows what the tip does
    for (int s = 0; s < 3; s++) {
      pad->begin(QPointF(20, 30 + s * 32), 0.1, true, 0);
      for (int i = 1; i <= 70; i++) pad->addPoint(QPointF(20 + i * 8.0, 30 + s * 32 + 14 * std::sin(i * 0.25 + s)), 0.1 + 0.85 * std::abs(std::sin(i * 0.07 + s)), i * 8);
      pad->end();
    }
    td->sample("pen", 0.62, 8, -3, 1, PenButton::None);
  }
  pump();
  td->grab().save(shots + "/settings_tablet.png");
  if (auto* sa = td->findChild<QScrollArea*>()) { sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum()); pump(); td->grab().save(shots + "/settings_tablet_keys.png"); }
  td->close();
  c->prefsDialog()->openDialog();
  pump();
  c->prefsDialog()->grab().save(shots + "/settings_prefs.png");
  c->prefsDialog()->reject();

  // ---- leave everything as it was
  c->applyTablet(savedTablet);
  c->applyToolbox(defaultToolbox());
  c->applyPalette("classic");
  c->applySizes(defaultSizes());
  ed.setStyle(savedStyle);
  ed.setTool("pen");
  tb->closeBox();
  td->close();
  c->prefsDialog()->hide();
  pump();
  QObject::disconnect(noticeConn);
}
