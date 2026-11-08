#include "inputfixselftest.h"
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include "prefsdialog.h"
#include "settings.h"
#include "tabletdialog.h"
#include "tabletwidgets.h"
#include "tabletcfg.h"
#include "toolboxwidget.h"
#include "tool.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTabletEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

namespace {

void pump(int n = 3) { for (int i = 0; i < n; i++) QApplication::processEvents(); }

/** A turn of the wheel over `w`, the way the window system delivers it (to the widget under the pointer). */
void wheel(QWidget* w, int dy) {
  const QPointF p = QPointF(w->rect().center());
  QWheelEvent ev(p, w->mapToGlobal(p), QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(w, &ev);
}

}  // namespace

void runInputFixChecks(SettingsController& c, MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check) {
  DocumentView& v = *win.view();
  const TabletSettings saved = ed.tablet();
  auto* pen = new QPointingDevice("fix-selftest pen", 301, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                  QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3, QString(), QPointingDeviceUniqueId(), &win);
  c.tabletDialog()->hide();
  c.prefsDialog()->hide();
  c.toolbox()->closeBox();
  win.show();
  win.activateWindow();
  win.raise();
  QApplication::setActiveWindow(&win);
  pump(5);
  ed.setTool("pen");
  v.forgetPen();
  v.setZoom(3);
  v.scrollToPage(0);
  v.verticalScrollBar()->setValue(v.verticalScrollBar()->minimum() + 200);
  pump();

  auto tab = [&](QEvent::Type t, QPointF p, double pressure, Qt::MouseButton b, Qt::MouseButtons bs) {
    QTabletEvent ev(t, pen, p, v.viewport()->mapToGlobal(p), pressure, 0, 0, 0, 0, 0, Qt::NoModifier, b, bs);
    QApplication::sendEvent(v.viewport(), &ev);
  };
  auto mouse = [&](QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
    QMouseEvent ev(t, p, v.viewport()->mapToGlobal(p), b, bs, Qt::NoModifier);
    QApplication::sendEvent(v.viewport(), &ev);
    return ev.isAccepted();
  };
  const QPointF p0(200, 220), p1(210, 160), p2(380, 300);
  auto scrollY = [&] { return v.verticalScrollBar()->value(); };
  const Qt::MouseButton B1 = Qt::RightButton, B2 = Qt::MiddleButton;
  const QString name1 = penCombo(int(B1), Mods()), name2 = penCombo(int(B2), Mods());
  auto bind = [&](const QMap<QString, QString>& keys) {
    TabletSettings t = ed.tablet();
    t.keys = keys;
    c.applyTablet(t, false);
  };
  const int maxScroll = v.verticalScrollBar()->maximum() - v.verticalScrollBar()->minimum();
  check(maxScroll > 600, QString("fix: (setup) the page can scroll (range %1)").arg(maxScroll));

  // ================================================================= A: the barrel button as a real mouse click while the pen is near (XP-Pen driver)
  {
    TabletSettings t = ed.tablet();
    t.keys.clear();
    t.barrel2 = PenAction::Hand;
    c.applyTablet(t, false);   // Preferencias > Lápiz: button 2 = hand, no key binding
  }
  tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  check(ed.penRecent(), "pen mouse: a hovering tablet event makes the pen recent");
  const int y0 = scrollY();
  mouse(QEvent::MouseButtonPress, p0, B2, B2);
  check(ed.cursorTool() == ed.toolInstance("hand") && ed.tool() == "pen", "pen mouse: a middle click while the pen hovers is its button 2: the hand is in use (the toolbar tool stays)");
  for (int i = 1; i <= 8; i++) mouse(QEvent::MouseMove, QPointF(200 + i * 5, 220 - i * 12), Qt::NoButton, B2);
  check(scrollY() == y0, QString("pen mouse: hovering with the button held does not scroll the page (%1 -> %2)").arg(y0).arg(scrollY()));
  for (int i = 1; i <= 8; i++) tab(QEvent::TabletMove, QPointF(200 + i * 5, 220 - i * 12), 0, Qt::NoButton, Qt::NoButton);
  check(scrollY() == y0 && ed.cursorTool() == ed.toolInstance("hand"), "pen mouse: tablet hover moves with the button held (as a mouse click) neither scroll nor drop the hand");
  // the tip lands and drags: the page follows the tip only
  tab(QEvent::TabletPress, p0, 0.5, Qt::LeftButton, Qt::LeftButton);
  check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("hand"), "pen mouse: the tip touching with the button held drags with the hand tool");
  tab(QEvent::TabletMove, p1, 0.5, Qt::NoButton, Qt::LeftButton);
  const int yDrag = scrollY();
  check(yDrag == y0 + int(p0.y() - p1.y()), QString("pen mouse: the page moved with the tip (%1 -> %2, expected %3)").arg(y0).arg(yDrag).arg(y0 + int(p0.y() - p1.y())));
  mouse(QEvent::MouseMove, p2, Qt::NoButton, B2);   // the pointer jumping (the driver's mouse echo) while the tip drags
  check(scrollY() == yDrag, "pen mouse: a mouse move echo while the tip drags does not move the page");
  tab(QEvent::TabletRelease, p1, 0, Qt::LeftButton, Qt::NoButton);
  check(!ed.gestureActive() && scrollY() == yDrag, "pen mouse: lifting the tip ends the drag where it was");
  // lifted, re-landing elsewhere with the button still held: a new drag, no jump of the page
  tab(QEvent::TabletMove, p2, 0, Qt::NoButton, Qt::NoButton);
  mouse(QEvent::MouseMove, p2, Qt::NoButton, B2);
  check(scrollY() == yDrag, "pen mouse: lifting and moving elsewhere with the button held does not scroll");
  tab(QEvent::TabletPress, p2, 0.5, Qt::LeftButton, Qt::LeftButton);
  check(scrollY() == yDrag, "pen mouse: landing elsewhere does not move the page");
  tab(QEvent::TabletRelease, p2, 0, Qt::LeftButton, Qt::NoButton);
  const bool accepted = mouse(QEvent::MouseButtonRelease, p2, B2, Qt::NoButton);
  check(accepted && ed.cursorTool() == ed.toolInstance("pen") && scrollY() == yDrag, "pen mouse: releasing the click gives the pen back and scrolls nothing");
  // leaving the range with the click still down
  mouse(QEvent::MouseButtonPress, p0, B2, B2);
  tab(QEvent::TabletLeaveProximity, p0, 0, Qt::NoButton, Qt::NoButton);
  mouse(QEvent::MouseMove, p2, Qt::NoButton, Qt::NoButton);
  check(!ed.penRecent() && ed.cursorTool() == ed.toolInstance("pen"), "pen mouse: leaving the range forgets the click (the pen is no longer near)");
  mouse(QEvent::MouseButtonRelease, p0, B2, Qt::NoButton);

  // the real mouse is unchanged: no pen near, the middle button drags the page
  v.forgetPen();
  const int ym = scrollY();
  mouse(QEvent::MouseButtonPress, p0, B2, B2);
  mouse(QEvent::MouseMove, p1, Qt::NoButton, B2);
  mouse(QEvent::MouseButtonRelease, p1, B2, Qt::NoButton);
  check(scrollY() == ym + int(p0.y() - p1.y()), "pen mouse: with no pen near, a middle-button drag of a real mouse still pans the page");

  // a barrel button owned by a key binding, reported twice (tablet event and mouse click): the action runs once, the hold lasts while either is down
  bind({{name2, "tool.hand"}});
  ed.setTool("pen");
  tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  tab(QEvent::TabletPress, p0, 0, B2, B2);
  mouse(QEvent::MouseButtonPress, p0, B2, B2);
  check(ed.tool() == "hand", "pen mouse: bound hand held by the button seen as tablet event and as mouse click");
  const int yb = scrollY();
  for (int i = 1; i <= 5; i++) mouse(QEvent::MouseMove, QPointF(200 + i * 5, 220 - i * 12), Qt::NoButton, B2);
  check(scrollY() == yb, "pen mouse: bound hold: hovering moves do not scroll");
  tab(QEvent::TabletRelease, p0, 0, B2, Qt::NoButton);
  const bool swallowed = mouse(QEvent::MouseButtonRelease, p0, B2, Qt::NoButton);
  check(ed.tool() == "pen" && swallowed && scrollY() == yb, "pen mouse: bound hold: released, the pen is back and the late mouse release is swallowed");
  // only a mouse click (no tablet event for the button) bound to a command: once
  bind({{name1, "view.zoomIn"}});
  v.setZoom(1);
  const double z0 = v.zoom();
  tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  tab(QEvent::TabletPress, p0, 0, B1, B1);
  mouse(QEvent::MouseButtonPress, p0, B1, B1);
  const double z1 = v.zoom();
  tab(QEvent::TabletRelease, p0, 0, B1, Qt::NoButton);
  mouse(QEvent::MouseButtonRelease, p0, B1, Qt::NoButton);
  check(z1 > z0 && std::abs(v.zoom() - z1) < 1e-9, QString("pen mouse: a command bound to a barrel button reported twice runs once (%1 -> %2 -> %3)").arg(z0).arg(z1).arg(v.zoom()));
  mouse(QEvent::MouseButtonPress, p0, B1, B1);   // the click alone, the tablet event never came
  const double z2 = v.zoom();
  mouse(QEvent::MouseButtonRelease, p0, B1, Qt::NoButton);
  check(z2 > z1, "pen mouse: the same button seen only as a mouse click (pen near) runs the binding");
  bind({});
  c.applyTablet(saved, false);
  v.setZoom(3);
  ed.setTool("pen");
  v.forgetPen();

  // ================================================================= XP-Pen / Windows Ink: contact with a barrel button held arrives as moves with pressure > 0 and buttons == Right / Middle
  for (int variant = 0; variant < 4; variant++) {
    const bool middle = variant % 2 == 1, bound = variant >= 2;
    const Qt::MouseButton B = middle ? Qt::MiddleButton : Qt::RightButton;
    const QString lbl = QString(middle ? "button 2" : "button 1") + (bound ? " bound to the hand" : " mapped to the hand");
    TabletSettings t = ed.tablet();
    t.keys.clear();
    if (bound) t.keys[middle ? name2 : name1] = "tool.hand";
    else (middle ? t.barrel2 : t.barrel) = PenAction::Hand;
    c.applyTablet(t, false);
    ed.setTool("pen");
    v.forgetPen();
    v.verticalScrollBar()->setValue(v.verticalScrollBar()->minimum() + 400);
    pump();
    const int base = scrollY();
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletPress, p0, 0, B, B);   // the button, pen hovering
    check(ed.cursorTool() == ed.toolInstance("hand"), "contact: " + lbl + ": the hand is in use while hovering");
    tab(QEvent::TabletMove, p2, 0, Qt::NoButton, B);
    check(scrollY() == base && !ed.gestureActive(), "contact: " + lbl + ": hovering with the button held scrolls nothing");
    tab(QEvent::TabletMove, p0, 0.37, Qt::NoButton, B);   // tip lands: pressure, buttons == barrel only, no Left, no press event
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("hand"), "contact: " + lbl + ": pressure with the button held starts the hand drag");
    tab(QEvent::TabletMove, p1, 0.4, Qt::NoButton, B);
    check(scrollY() == base + int(p0.y() - p1.y()), QString("contact: %1: the page follows the tip (%2 -> %3)").arg(lbl).arg(base).arg(scrollY()));
    const int dragged = scrollY();
    tab(QEvent::TabletMove, p1, 0.02, Qt::NoButton, B);   // a very light touch is still contact
    check(ed.gestureActive(), "contact: " + lbl + ": a light touch (raw pressure > 0) stays in contact");
    tab(QEvent::TabletMove, p1, 0, Qt::NoButton, B);   // lifted, button still held
    check(!ed.gestureActive() && scrollY() == dragged, "contact: " + lbl + ": lifting (pressure 0) ends the drag where it was");
    tab(QEvent::TabletMove, p2, 0, Qt::NoButton, B);
    tab(QEvent::TabletMove, p2, 0.5, Qt::NoButton, B);   // re-land elsewhere
    check(scrollY() == dragged && ed.gestureActive(), "contact: " + lbl + ": re-landing elsewhere starts a new drag with no jump");
    tab(QEvent::TabletMove, QPointF(p2.x(), p2.y() - 20), 0.5, Qt::NoButton, B);
    check(scrollY() == dragged + 20, "contact: " + lbl + ": and the new drag moves the page from the landing point");
    tab(QEvent::TabletMove, QPointF(p2.x(), p2.y() - 20), 0, Qt::NoButton, B);
    tab(QEvent::TabletRelease, p2, 0, B, Qt::NoButton);
    check(!ed.gestureActive() && ed.cursorTool() == ed.toolInstance("pen") && ed.tool() == "pen", "contact: " + lbl + ": releasing the button gives the pen back");
    // the button pressed in the middle of a tip stroke does not end it
    const size_t n0 = [&] { size_t n = 0; for (const Page& pg : ed.doc().pages) for (const Layer& l : pg.layers) n += l.elements.size(); return n; }();
    tab(QEvent::TabletPress, p0, 0.4, Qt::LeftButton, Qt::LeftButton);
    tab(QEvent::TabletMove, p0 + QPointF(10, 5), 0.5, Qt::NoButton, Qt::LeftButton);
    tab(QEvent::TabletMove, p0 + QPointF(20, 8), 0.5, Qt::NoButton, Qt::LeftButton | B);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("pen"), "contact: " + lbl + ": a barrel press during a tip stroke does not end or change it");
    tab(QEvent::TabletRelease, p0 + QPointF(30, 10), 0, Qt::LeftButton, B);
    tab(QEvent::TabletRelease, p0 + QPointF(30, 10), 0, B, Qt::NoButton);
    size_t n1 = 0;
    for (const Page& pg : ed.doc().pages) for (const Layer& l : pg.layers) n1 += l.elements.size();
    check(!ed.gestureActive() && n1 == n0 + 1, "contact: " + lbl + ": the stroke is committed whole");
    ed.undo();
    // leaving the range while dragging
    tab(QEvent::TabletPress, p0, 0, B, B);
    tab(QEvent::TabletMove, p0, 0.5, Qt::NoButton, B);
    tab(QEvent::TabletLeaveProximity, p0, 0, Qt::NoButton, Qt::NoButton);
    check(!ed.gestureActive(), "contact: " + lbl + ": leaving the range ends the drag");
    tab(QEvent::TabletRelease, p0, 0, B, Qt::NoButton);
  }
  {   // tip only draws as before
    TabletSettings t = ed.tablet();
    t.keys.clear();
    c.applyTablet(saved, false);
    ed.setTool("pen");
    const size_t n0 = [&] { size_t n = 0; for (const Page& pg : ed.doc().pages) for (const Layer& l : pg.layers) n += l.elements.size(); return n; }();
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletPress, p0, 0.4, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 5; i++) tab(QEvent::TabletMove, p0 + QPointF(i * 8, i * 3), 0.5, Qt::NoButton, Qt::LeftButton);
    tab(QEvent::TabletRelease, p0 + QPointF(48, 18), 0, Qt::LeftButton, Qt::NoButton);
    size_t n1 = 0;
    for (const Page& pg : ed.doc().pages) for (const Layer& l : pg.layers) n1 += l.elements.size();
    check(n1 == n0 + 1 && !ed.gestureActive(), "contact: tip only (Left) still draws one stroke");
    ed.undo();
  }
  {   // replay of what the user's XP-Pen really sent (xl_penlog): a barrel release arrives as a 'Left release'; a barrel + tip contact as buttons == Left only
    TabletSettings t = ed.tablet();
    t.keys.clear();
    t.barrel = PenAction::None;
    t.barrel2 = PenAction::Hand;
    c.applyTablet(t, false);
    ed.setTool("pen");
    v.forgetPen();
    const auto strokes = [&] { size_t n = 0; for (const Page& pg : ed.doc().pages) for (const Layer& l : pg.layers) n += l.elements.size(); return n; };
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletPress, p0, 0, Qt::RightButton, Qt::RightButton);   // barrel 1 in the air
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::RightButton);
    tab(QEvent::TabletRelease, p0, 0, Qt::LeftButton, Qt::LeftButton);   // ... its release, as the driver reports it
    tab(QEvent::TabletLeaveProximity, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletEnterProximity, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletRelease, p0, 0, Qt::LeftButton, Qt::NoButton);
    const size_t s0 = strokes();
    tab(QEvent::TabletMove, p0, 0.02, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletPress, p0, 0.03, Qt::LeftButton, Qt::LeftButton);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("pen"), "replay: a plain stroke after a barrel button was released the driver's way is a pen stroke");
    for (int i = 1; i <= 4; i++) tab(QEvent::TabletMove, p0 + QPointF(i * 8, i * 3), 0.3, Qt::NoButton, Qt::LeftButton);
    tab(QEvent::TabletRelease, p0 + QPointF(40, 15), 0, Qt::LeftButton, Qt::NoButton);
    check(!ed.gestureActive() && strokes() == s0 + 1, "replay: and it is committed as one stroke");
    ed.undo();
    // barrel 2 (hand) held, tip lands: 'Release Left' with pressure, then moves with buttons == Left only
    v.verticalScrollBar()->setValue(v.verticalScrollBar()->minimum() + 400);
    pump();
    const int base = scrollY();
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletPress, p0, 0, Qt::MiddleButton, Qt::MiddleButton);
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::MiddleButton);
    tab(QEvent::TabletRelease, p0, 0.02, Qt::LeftButton, Qt::LeftButton);
    tab(QEvent::TabletMove, p0, 0.04, Qt::NoButton, Qt::LeftButton);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("hand"), "replay: the tip landing with button 2 held (buttons == Left only) drags with the hand");
    tab(QEvent::TabletMove, p1, 0.25, Qt::NoButton, Qt::LeftButton);
    check(scrollY() == base + int(p0.y() - p1.y()), "replay: and the page follows the tip");
    tab(QEvent::TabletMove, p1, 0, Qt::NoButton, Qt::MiddleButton);
    check(!ed.gestureActive(), "replay: lifting the tip ends the drag");
    tab(QEvent::TabletRelease, p1, 0, Qt::MiddleButton, Qt::NoButton);
    tab(QEvent::TabletMove, p1, 0.04, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletPress, p1, 0.04, Qt::LeftButton, Qt::LeftButton);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("pen"), "replay: after the button is released the next touch is the pen again");
    tab(QEvent::TabletRelease, p1, 0, Qt::LeftButton, Qt::NoButton);
    ed.undo();
  }
  c.applyTablet(saved, false);
  ed.setTool("pen");
  v.forgetPen();

  // ================================================================= the raw input trace of Lápiz y tableta
  {
    TabletDialog* td = c.tabletDialog();
    td->openDialog();
    pump();
    QWidget* sheet = td->pad();   // the page ignores pointers while a floating dialog is open: the trace is read on the dialog's own test sheet
    auto tab = [&](QEvent::Type t, QPointF p, double pressure, Qt::MouseButton b, Qt::MouseButtons bs) {
      QTabletEvent ev(t, pen, p, sheet->mapToGlobal(p), pressure, 0, 0, 0, 0, 0, Qt::NoModifier, b, bs);
      QApplication::sendEvent(sheet, &ev);
    };
    auto mouse = [&](QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
      QMouseEvent ev(t, p, sheet->mapToGlobal(p), b, bs, Qt::NoModifier);
      QApplication::sendEvent(sheet, &ev);
      return ev.isAccepted();
    };
    check(td->traceText().isEmpty(), "trace: empty when the dialog opens");
    tab(QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
    tab(QEvent::TabletMove, p1, 0, Qt::NoButton, Qt::NoButton);
    check(td->traceText().isEmpty() || td->traceText().count('\n') == 0, "trace: hovering moves with no button change are not logged (at most the first)");
    const int before = td->traceText().isEmpty() ? 0 : td->traceText().count('\n') + 1;
    tab(QEvent::TabletPress, p1, 0, B2, B2);
    mouse(QEvent::MouseButtonPress, p1, B2, B2);
    mouse(QEvent::MouseButtonRelease, p1, B2, Qt::NoButton);
    tab(QEvent::TabletRelease, p1, 0, B2, Qt::NoButton);
    const QString txt = td->traceText();
    check(txt.contains("Tablet") && txt.contains(QString::fromUtf8("Ratón")) && txt.contains("pulsa") && txt.contains("suelta") && txt.contains("central") && txt.contains(QString::fromUtf8("sintético=no")) && txt.contains("fix-selftest pen"),
          QString("trace: tablet and mouse press / release with the button in words, synthesized or not and the device (visible=%1 text=[%2])").arg(td->isVisible()).arg(txt));
    check(txt.count('\n') + 1 - before == 4, QString("trace: four events logged (%1)").arg(txt.count('\n') + 1 - before));
    check(TabletDialog::buttonWords(int(Qt::LeftButton | Qt::MiddleButton)) == "izq+central" && TabletDialog::buttonWords(0) == QString::fromUtf8("ninguno"), "trace: buttons mask in words");
    for (int i = 0; i < 30; i++) { tab(QEvent::TabletPress, p1, 0, B1, B1); tab(QEvent::TabletRelease, p1, 0, B1, Qt::NoButton); }
    check(td->traceText().count('\n') + 1 <= 12, "trace: keeps only the last 12 lines");
    td->hide();
    const QString kept = td->traceText();
    tab(QEvent::TabletPress, p1, 0, B1, B1);
    tab(QEvent::TabletRelease, p1, 0, B1, Qt::NoButton);
    check(td->traceText() == kept, "trace: nothing is logged while the dialog is hidden");
  }

  // ================================================================= B: the tool box and the pen
  {
    c.applyToolbox(defaultToolbox());
    ToolboxWidget* tb = c.toolbox();
    const QPoint centre = win.mapToGlobal(QPoint(win.width() / 2, win.height() / 2));
    ed.setTool("pen");
    // a pen reaches the application as a tablet event on the QWindow first; a widget that ignores it gets the mouse click Qt makes of it (source SynthesizedByQt)
    auto penTap = [&](QWidget* target, bool synthesizeMouse = true) {
      QWindow* win_ = target->window()->windowHandle();
      const QPointF local = QPointF(target->mapTo(target->window(), target->rect().center()));
      const QPointF global = target->mapToGlobal(QPointF(target->rect().center()));
      QTabletEvent press(QEvent::TabletPress, pen, local, global, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
      QApplication::sendEvent(win_, &press);
      if (synthesizeMouse) { QMouseEvent m(QEvent::MouseButtonPress, local, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt); QApplication::sendEvent(win_, &m); }
      QTabletEvent release(QEvent::TabletRelease, pen, local, global, 0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
      QApplication::sendEvent(win_, &release);
      if (synthesizeMouse) { QMouseEvent m(QEvent::MouseButtonRelease, local, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt); QApplication::sendEvent(win_, &m); }
      pump();
    };
    c.showToolbox(centre);
    pump();
    check(tb->isOpen() && tb->windowHandle() != nullptr, "pen toolbox: (setup) the box is open and has its own window");
    QPushButton* edit = tb->smallButton("Editar");
    penTap(edit);
    check(tb->isOpen() && tb->isEditing(), "pen toolbox: a pen tap on Editar enters the customise mode and the box stays open");
    penTap(tb->smallButton("Listo"));
    check(tb->isOpen() && !tb->isEditing(), "pen toolbox: a pen tap on Listo leaves it, box still open");
    penTap(tb->smallButton("Editar"));
    check(tb->isEditing(), "pen toolbox: it can be re-entered");
    penTap(tb->smallButton("Listo"));
    // a tool button: acts like the mouse click (the tool changes, the box closes because it is set to close on pick)
    penTap(tb->itemWidget("tool.eraser"));
    check(ed.tool() == "eraser" && !tb->isOpen(), "pen toolbox: a pen tap on a tool button chooses the tool and closes the box, as the mouse does");
    ed.setTool("pen");
    // a tap outside closes it
    c.showToolbox(centre);
    pump();
    penTap(win.infoWidget(), true);
    check(!tb->isOpen(), "pen toolbox: a pen tap outside the box closes it");
    // the tablet event alone (the QWindow case, no widget involved) inside the box must not close it
    c.showToolbox(centre);
    pump();
    {
      QWindow* w = tb->windowHandle();
      const QPointF local(tb->width() / 2.0, 6), global = QPointF(tb->mapToGlobal(QPoint(tb->width() / 2, 6)));
      QTabletEvent press(QEvent::TabletPress, pen, local, global, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
      QApplication::sendEvent(w, &press);
      check(tb->isOpen(), "pen toolbox: a tablet event on the box's window (title area) does not close it");
      QTabletEvent release(QEvent::TabletRelease, pen, local, global, 0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
      QApplication::sendEvent(w, &release);
    }
    tb->closeBox();

    // a combo of a floating dialog and its drop-down list with the pen
    TabletDialog* td = c.tabletDialog();
    td->openDialog();
    pump(5);
    QComboBox* cb = td->penCombo(0);
    if (auto* sa = td->findChild<QScrollArea*>()) sa->ensureWidgetVisible(cb);
    pump();
    QApplication::setActiveWindow(td);
    td->ensurePolished();
    const PenAction before = ed.tablet().barrel;
    penTap(cb);
    const bool opened = QApplication::activePopupWidget() != nullptr || cb->view()->isVisible();
    check(opened, "pen dialog: a pen tap on a combo box opens its list");
    if (opened) {
      QElapsedTimer wait;   // a drop-down list ignores a release that comes within the double-click interval of opening it
      wait.start();
      while (wait.elapsed() < QApplication::doubleClickInterval() + 150) QApplication::processEvents(QEventLoop::AllEvents, 20);
      const int target = cb->findData("eraser");
      QWidget* vp = cb->view()->viewport();
      const QRect r = cb->view()->visualRect(cb->model()->index(target, 0));
      QWindow* pw = vp->window()->windowHandle();
      const QPointF local = QPointF(vp->mapTo(vp->window(), r.center())), global = QPointF(vp->mapToGlobal(r.center()));
      QTabletEvent press(QEvent::TabletPress, pen, local, global, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
      if (pw) QApplication::sendEvent(pw, &press);
      QMouseEvent mp(QEvent::MouseButtonPress, local, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt);
      if (pw) QApplication::sendEvent(pw, &mp);
      QTabletEvent release(QEvent::TabletRelease, pen, local, global, 0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
      if (pw) QApplication::sendEvent(pw, &release);
      QMouseEvent mr(QEvent::MouseButtonRelease, local, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt);
      if (pw) QApplication::sendEvent(pw, &mr);
      pump();
      check(ed.tablet().barrel == PenAction::Eraser && before != PenAction::Eraser, QString("pen dialog: a pen tap on an item of the list picks it (barrel=%1, combo=%2, target=%3, popup=%4, rect=%5,%6 %7x%8)")
            .arg(int(ed.tablet().barrel)).arg(cb->currentIndex()).arg(target).arg(QApplication::activePopupWidget() != nullptr).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
    }
    if (QApplication::activePopupWidget()) QApplication::activePopupWidget()->close();
    td->hide();
    c.applyTablet(saved, false);
  }

  // ================================================================= C: the wheel over form controls
  {
    QScrollArea area;
    area.setFixedSize(360, 220);
    auto* content = new QWidget;
    auto* lay = new QVBoxLayout(content);
    auto* combo = new QComboBox(content);
    for (int i = 0; i < 6; i++) combo->addItem(QString("item %1").arg(i));
    auto* spin = new QSpinBox(content);
    spin->setRange(0, 100);
    spin->setValue(50);
    auto* slider = new QSlider(Qt::Horizontal, content);
    slider->setRange(0, 100);
    slider->setValue(50);
    auto* editableCombo = new QComboBox(content);
    editableCombo->setEditable(true);
    for (int i = 0; i < 4; i++) editableCombo->addItem(QString("e %1").arg(i));
    lay->addWidget(combo);
    lay->addWidget(spin);
    lay->addWidget(slider);
    lay->addWidget(editableCombo);
    lay->addSpacing(900);   // tall: the area scrolls
    area.setWidget(content);
    area.setWidgetResizable(true);
    area.show();
    area.activateWindow();
    QApplication::setActiveWindow(&area);
    pump(5);
    combo->clearFocus();
    spin->clearFocus();
    slider->clearFocus();
    area.setFocus();
    check(combo->focusPolicy() == Qt::StrongFocus && spin->focusPolicy() == Qt::StrongFocus && slider->focusPolicy() == Qt::StrongFocus, "wheel: value widgets take the focus by click or Tab, never by the wheel");
    QScrollBar* sb = area.verticalScrollBar();
    check(sb->maximum() > 300, QString("wheel: (setup) the form scrolls (range %1)").arg(sb->maximum()));
    sb->setValue(0);
    wheel(area.viewport(), -120);
    check(sb->value() > 0, QString("wheel: (setup) a wheel turn on the form itself scrolls it (%1)").arg(sb->value()));
    sb->setValue(0);
    wheel(combo, -120);
    check(combo->currentIndex() == 0, "wheel: an unfocused combo box does not change");
    const int s1 = sb->value();
    check(s1 > 0, QString("wheel: ...and the wheel scrolled the form instead (%1)").arg(s1));
    wheel(spin, -120);
    check(spin->value() == 50 && sb->value() > s1, "wheel: an unfocused spin box does not change and the form scrolls");
    const int s2 = sb->value();
    wheel(slider, -120);
    check(slider->value() == 50 && sb->value() > s2, "wheel: an unfocused slider does not change and the form scrolls");
    const int s3 = sb->value();
    wheel(spin->findChild<QLineEdit*>(), -120);
    check(spin->value() == 50 && sb->value() > s3, "wheel: the line edit of an unfocused spin box does not change it either");
    wheel(editableCombo, -120);
    check(editableCombo->currentIndex() == 0, "wheel: an unfocused editable combo box does not change");
    combo->setFocus(Qt::MouseFocusReason);
    pump();
    check(combo->hasFocus(), "wheel: (setup) the combo box has the focus");
    wheel(combo, -120);
    check(combo->currentIndex() == 1, "wheel: a focused combo box changes with the wheel");
    spin->setFocus(Qt::MouseFocusReason);
    pump();
    wheel(spin, 120);
    check(spin->value() == 51, "wheel: a focused spin box changes with the wheel");
    slider->setFocus(Qt::MouseFocusReason);
    pump();
    wheel(slider, 120);
    check(slider->value() > 50, "wheel: a focused slider changes with the wheel");
    // the page scrolls as ever, with or without a control having the focus elsewhere
    const int vy = v.verticalScrollBar()->value();
    wheel(v.viewport(), -120);
    check(v.verticalScrollBar()->value() > vy, "wheel: the page (document view) still scrolls");
    // the ACK05 dial: a binding on the wheel runs first, even over an unfocused control
    combo->clearFocus();
    TabletSettings t = ed.tablet();
    t.keys = {{QString::fromUtf8("Rueda arriba"), "view.zoomIn"}};
    c.applyTablet(t, false);
    v.setZoom(1);
    const double z = v.zoom();
    QWidget* old = QApplication::activeWindow();
    QApplication::setActiveWindow(&win);
    wheel(win.infoWidget(), 120);
    check(v.zoom() > z, "wheel: a bound wheel (the ACK05 dial) still runs its action");
    if (old) QApplication::setActiveWindow(old);
    c.applyTablet(saved, false);
    v.setZoom(1);
    // the real dialog: the size box and the combos of Lápiz y tableta
    TabletDialog* td = c.tabletDialog();
    td->openDialog();
    pump(5);
    const double size0 = td->sizeSpin()->value();
    td->sizeSpin()->clearFocus();
    wheel(td->sizeSpin(), 120);
    check(std::abs(td->sizeSpin()->value() - size0) < 1e-9, "wheel: the size box of Lápiz y tableta ignores the wheel without the focus");
    const int pi = td->presetCombo()->currentIndex();
    wheel(td->presetCombo(), -120);
    check(td->presetCombo()->currentIndex() == pi, "wheel: the pressure preset combo of Lápiz y tableta ignores it too");
    td->hide();
  }
  c.applyTablet(saved, false);
  ed.setTool("pen");
  v.setZoom(1);
  v.forgetPen();
}
