#include "penholdselftest.h"
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include "prefsdialog.h"
#include "settings.h"
#include "tabletdialog.h"
#include "tabletcfg.h"
#include "tool.h"
#include "winpen.h"
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QSpinBox>
#include <QTabletEvent>
#include <QWidget>

namespace {

size_t elementCount(Editor& ed) {
  size_t n = 0;
  for (const Page& p : ed.doc().pages) for (const Layer& l : p.layers) n += l.elements.size();
  return n;
}

}  // namespace

void runPenHoldChecks(SettingsController& c, MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check) {
  DocumentView& v = *win.view();
  const TabletSettings saved = ed.tablet();
  auto* pen = new QPointingDevice("hold-selftest pen", 201, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                  QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3, QString(), QPointingDeviceUniqueId(), &win);
  auto* eraser = new QPointingDevice("hold-selftest eraser", 202, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Eraser,
                                     QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3, QString(), QPointingDeviceUniqueId(), &win);
  c.tabletDialog()->hide();   // while one of the two dialogs is the active window the bound buttons belong to it (as in the Electron app)
  c.prefsDialog()->hide();
  win.show();   // an earlier check may have left the window hidden
  win.activateWindow();
  win.raise();
  QApplication::setActiveWindow(&win);   // a hidden dialog can stay the active window on the offscreen platform
  v.scrollToPage(0);
  for (int i = 0; i < 5; i++) QApplication::processEvents();
  check(QApplication::activeWindow() == &win, QString("hold: (setup) the main window is the active one (active: %1 %2)").arg(QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "none", QApplication::activeWindow() ? QApplication::activeWindow()->objectName() : QString()) + QString(" prefsVisible=%1 tabletVisible=%2 winVisible=%3").arg(c.prefsDialog()->isVisible()).arg(c.tabletDialog()->isVisible()).arg(win.isVisible()));
  auto at = [&](double x, double y) { return v.toView(0, QPointF(x, y)); };
  auto tab = [&](const QPointingDevice* dev, QEvent::Type t, QPointF p, double pressure, Qt::MouseButton b, Qt::MouseButtons bs) {
    QTabletEvent ev(t, dev, p, v.viewport()->mapToGlobal(p), pressure, 0, 0, 0, 0, 0, Qt::NoModifier, b, bs);
    QApplication::sendEvent(v.viewport(), &ev);
  };
  auto mouse = [&](QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
    QMouseEvent ev(t, p, v.viewport()->mapToGlobal(p), b, bs, Qt::NoModifier);
    QApplication::sendEvent(v.viewport(), &ev);
    return ev.isAccepted();
  };
  const Qt::MouseButton B1 = Qt::RightButton, B2 = Qt::MiddleButton;   // how Qt reports the two barrel buttons of a pen
  const QString name1 = penCombo(int(B1), Mods()), name2 = penCombo(int(B2), Mods());
  auto bind = [&](const QMap<QString, QString>& keys) {
    TabletSettings t = ed.tablet();
    t.keys = keys;
    c.applyTablet(t, false);
  };
  auto instance = [&](const char* id) { return ed.toolInstance(id); };
  const QPointF p0 = at(200, 200), p1 = at(260, 230);

  // ---- a bound barrel button holds its tool only while pressed
  bind({{name1, "tool.hand"}});
  ed.setTool("pen");
  tab(pen, QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  check(ed.tool() == "hand", "hold: pressing the barrel button bound to the hand selects it (with the pen only hovering)");
  tab(pen, QEvent::TabletMove, p1, 0, Qt::NoButton, B1);
  check(ed.tool() == "hand" && ed.cursorTool() == instance("hand"), "hold: it stays selected while the button is held and the pen moves");
  tab(pen, QEvent::TabletRelease, p1, 0, B1, Qt::NoButton);
  check(ed.tool() == "pen" && ed.cursorTool() == instance("pen"), "hold: releasing the button gives the tool before (the pen) back");

  // the pen touches down while the button is held: the drag is the hand's; the button is released first, the tool goes back after the drag
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  tab(pen, QEvent::TabletPress, p0, 0.5, Qt::LeftButton, Qt::LeftButton | B1);
  check(ed.gestureActive() && ed.gestureTool() == instance("hand") && ed.tool() == "hand", "hold: the tip touching with the button held drags with the hand tool");
  tab(pen, QEvent::TabletMove, p1, 0.5, Qt::NoButton, Qt::LeftButton | B1);
  tab(pen, QEvent::TabletRelease, p1, 0, B1, Qt::LeftButton);
  check(ed.gestureActive() && ed.tool() == "hand", "hold: the button released mid-drag does not pull the tool away from the drag");
  tab(pen, QEvent::TabletRelease, p1, 0, Qt::LeftButton, Qt::NoButton);
  check(!ed.gestureActive() && ed.tool() == "pen", "hold: when the drag ends the tool before comes back");

  // a stroke in progress is not corrupted by the button
  const size_t n0 = elementCount(ed);
  tab(pen, QEvent::TabletPress, p0, 0.4, Qt::LeftButton, Qt::LeftButton);
  for (int i = 1; i <= 6; i++) tab(pen, QEvent::TabletMove, at(200 + i * 10, 200 + i * 3), 0.5, Qt::NoButton, Qt::LeftButton);
  tab(pen, QEvent::TabletPress, at(260, 218), 0.5, B1, Qt::LeftButton | B1);
  check(ed.tool() == "pen" && ed.gestureActive() && ed.gestureTool() == instance("pen"), "hold: the button pressed mid-stroke does not switch the tool under the stroke");
  for (int i = 7; i <= 10; i++) tab(pen, QEvent::TabletMove, at(200 + i * 10, 200 + i * 3), 0.5, Qt::NoButton, Qt::LeftButton | B1);
  tab(pen, QEvent::TabletRelease, at(300, 230), 0, Qt::LeftButton, B1);
  check(elementCount(ed) == n0 + 1 && ed.tool() == "pen", "hold: the stroke is committed whole (one new element) and the tool is still the pen");
  tab(pen, QEvent::TabletRelease, at(300, 230), 0, B1, Qt::NoButton);
  check(ed.tool() == "pen" && !ed.gestureActive(), "hold: the late release of that button changes nothing");
  ed.undo();

  // another tool picked while the button is held stays picked
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  ed.setTool("eraser");
  tab(pen, QEvent::TabletRelease, p0, 0, B1, Qt::NoButton);
  check(ed.tool() == "eraser", "hold: a tool picked meanwhile is not overridden by the release");
  ed.setTool("pen");

  // a non-tool action fires once on press and the release is swallowed
  bind({{name2, "view.zoomIn"}});
  v.setZoom(1);
  const double z0 = v.zoom();
  tab(pen, QEvent::TabletPress, p0, 0, B2, B2);
  const double z1 = v.zoom();
  tab(pen, QEvent::TabletRelease, p0, 0, B2, Qt::NoButton);
  check(z1 > z0 && std::abs(v.zoom() - z1) < 1e-9 && ed.tool() == "pen", "hold: a bound command (zoom in) runs once on press, the release does nothing");
  v.setZoom(1);

  // a bound button is not also read as the Preferencias mapping when hovering (barrel 2 = hand by default)
  tab(pen, QEvent::TabletMove, p1, 0, Qt::NoButton, B2);
  check(ed.cursorTool() == instance("pen"), "hold: a barrel button owned by a key binding does not also borrow its Preferencias tool");
  tab(pen, QEvent::TabletMove, p1, 0, Qt::NoButton, Qt::NoButton);

  // capture: the button is captured, selects nothing and its release is swallowed
  bind({});
  QString got;
  c.beginCapture([&](const QString& s) { got = s; });
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  tab(pen, QEvent::TabletRelease, p0, 0, B1, Qt::NoButton);
  check(got == name1 && !c.capturing() && ed.tool() == "pen", "hold: capturing a barrel button still works and holds nothing");

  // stale state: pen leaves the proximity / window deactivates / app loses focus, with the button still "down"
  bind({{name1, "tool.hand"}});
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  check(ed.tool() == "hand", "stale: (setup) the hold is on");
  tab(pen, QEvent::TabletLeaveProximity, p0, 0, Qt::NoButton, Qt::NoButton);
  check(ed.tool() == "pen", "stale: the pen leaving the tablet's range gives the tool back");
  tab(pen, QEvent::TabletRelease, p0, 0, B1, Qt::NoButton);   // a late release must not do anything odd
  check(ed.tool() == "pen", "stale: the release that arrives after that is harmless");
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  { QEvent e(QEvent::WindowDeactivate); QApplication::sendEvent(&win, &e); }
  check(ed.tool() == "pen", "stale: the window losing activation gives the tool back");
  { QEvent e(QEvent::WindowActivate); QApplication::sendEvent(&win, &e); }
  tab(pen, QEvent::TabletRelease, p0, 0, B1, Qt::NoButton);
  tab(pen, QEvent::TabletPress, p0, 0, B1, B1);
  emit qApp->applicationStateChanged(Qt::ApplicationInactive);
  check(ed.tool() == "pen", "stale: the application losing focus gives the tool back");
  emit qApp->applicationStateChanged(Qt::ApplicationActive);
  tab(pen, QEvent::TabletRelease, p0, 0, B1, Qt::NoButton);

  // the same for the buttons a driver sends as mouse clicks (middle / back / forward)
  bind({{penCombo(int(Qt::MiddleButton), Mods()).replace(name2, QString()), "tool.hand"}});   // (no key: placeholder removed below)
  bind({{QString::fromUtf8("Botón central"), "tool.hand"}});
  ed.setTool("pen");
  mouse(QEvent::MouseButtonPress, p0, Qt::MiddleButton, Qt::MiddleButton);
  check(ed.tool() == "hand", "mouse: a middle click bound to the hand selects it while held");
  const bool swallowed = mouse(QEvent::MouseButtonRelease, p0, Qt::MiddleButton, Qt::NoButton);
  check(ed.tool() == "pen" && swallowed, "mouse: releasing it gives the pen back (the same hold as the pen's own button)");
  mouse(QEvent::MouseButtonPress, p0, Qt::MiddleButton, Qt::MiddleButton);
  { QEvent e(QEvent::WindowDeactivate); QApplication::sendEvent(&win, &e); }
  check(ed.tool() == "pen", "stale: a mouse-button hold is given back when the window deactivates");
  { QEvent e(QEvent::WindowActivate); QApplication::sendEvent(&win, &e); }
  mouse(QEvent::MouseButtonRelease, p0, Qt::MiddleButton, Qt::NoButton);
  bind({});

  // ---- Preferencias per-button mapping: the tool is in use while the button is held, hovering included
  TabletSettings t = ed.tablet();
  t.barrel2 = PenAction::Hand;
  t.eraser = PenAction::Eraser;
  c.applyTablet(t, false);
  ed.setTool("pen");
  tab(pen, QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  tab(pen, QEvent::TabletPress, p0, 0, B2, B2);
  check(ed.tool() == "pen" && ed.cursorTool() == instance("hand"), "mapping: button 2 (hand) held while hovering: the hand is the tool in use, the toolbar tool is unchanged");
  tab(pen, QEvent::TabletMove, p1, 0, Qt::NoButton, B2);
  check(ed.cursorTool() == instance("hand"), "mapping: still the hand after a move");
  tab(pen, QEvent::TabletRelease, p1, 0, B2, Qt::NoButton);
  check(ed.cursorTool() == instance("pen"), "mapping: released while hovering: back to the pen at once");
  tab(pen, QEvent::TabletPress, p0, 0, B2, B2);
  tab(pen, QEvent::TabletLeaveProximity, p0, 0, Qt::NoButton, Qt::NoButton);
  check(ed.cursorTool() == instance("pen"), "mapping: the pen leaving the range gives it back too");
  tab(pen, QEvent::TabletRelease, p0, 0, B2, Qt::NoButton);
  tab(pen, QEvent::TabletPress, p0, 0, B2, B2);
  tab(pen, QEvent::TabletPress, p0, 0.5, Qt::LeftButton, Qt::LeftButton | B2);
  check(ed.gestureActive() && ed.gestureTool() == instance("hand") && ed.tool() == "pen", "mapping: touching down with the button held drags with the hand");
  tab(pen, QEvent::TabletRelease, p1, 0, Qt::LeftButton, B2);
  tab(pen, QEvent::TabletRelease, p1, 0, B2, Qt::NoButton);
  check(!ed.gestureActive() && ed.cursorTool() == instance("pen") && ed.tool() == "pen", "mapping: after the drag the pen is back");
  tab(eraser, QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  check(ed.cursorTool() == instance("eraser") && ed.tool() == "pen", "mapping: the eraser end near the page already uses the eraser");
  tab(pen, QEvent::TabletMove, p0, 0, Qt::NoButton, Qt::NoButton);
  check(ed.cursorTool() == instance("pen"), "mapping: back to the pen end, back to the pen");
  c.applyTablet(saved, false);
  ed.setTool("pen");

  // ---- winpen: text inputs have no input-method context, typing still works
  {
    const winpen::Stats before = winpen::stats();
    QWidget host;
    auto* le = new QLineEdit(&host);
    auto* sp = new QSpinBox(&host);
    auto* pt = new QPlainTextEdit(&host);
    auto* cb = new QComboBox(&host);
    cb->setEditable(true);
    host.show();
    QApplication::processEvents();
    check(!le->testAttribute(Qt::WA_InputMethodEnabled) && !sp->findChild<QLineEdit*>()->testAttribute(Qt::WA_InputMethodEnabled) && !pt->viewport()->testAttribute(Qt::WA_InputMethodEnabled) &&
              !pt->testAttribute(Qt::WA_InputMethodEnabled) && !cb->lineEdit()->testAttribute(Qt::WA_InputMethodEnabled),
          "winpen: line edit, spin box, plain text editor and editable combo have no input-method context");
    check(winpen::stats().textInputs >= before.textInputs + 4, "winpen: the filter treated them as they were shown");
    le->setReadOnly(true);
    le->setReadOnly(false);
    const bool back = le->testAttribute(Qt::WA_InputMethodEnabled);
    QFocusEvent in(QEvent::FocusIn, Qt::MouseFocusReason);
    QApplication::sendEvent(le, &in);
    check(back && !le->testAttribute(Qt::WA_InputMethodEnabled), "winpen: a field whose context Qt switched back on loses it again when it takes the focus");
    le->setFocus();
    for (const QString& ch : {QString::fromUtf8("ñ"), QString::fromUtf8("á"), QString::fromUtf8("ü")}) {
      QKeyEvent kp(QEvent::KeyPress, 0, Qt::NoModifier, ch), kr(QEvent::KeyRelease, 0, Qt::NoModifier, ch);
      QApplication::sendEvent(le, &kp);
      QApplication::sendEvent(le, &kr);
    }
    check(le->text() == QString::fromUtf8("ñáü"), "winpen: characters that come as key text (n tilde, accented vowels, ue) are typed with no input-method context");
    check(winpen::tabletGestureFlags() & 0x1 && winpen::tabletGestureFlags() & 0x8 && winpen::tabletGestureFlags() & 0x10 && winpen::tabletGestureFlags() & 0x10000,
          "winpen: the tablet-service flags turn off press-and-hold, pen-tap and barrel feedback and flicks");
  }
  c.applyTablet(saved, false);
}
