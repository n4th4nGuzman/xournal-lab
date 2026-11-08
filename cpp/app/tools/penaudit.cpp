// Audit checks of the pen: fingers and palm, zoom and scroll, single-page view, highlighter and glow while drawing, laser, hand, thumbnails on a
// worker. Every check drives the real Qt event path with synthetic events (a QTabletEvent is not a physical pen, a QTouchEvent is not a screen).
#include "brush.h"
#include "chrome/chrome.h"
#include "chrome/parts.h"
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include "penchecks.h"
#include "penutil.h"
#include "pencursor.h"
#include "tabletcfg.h"
#include "settings/settings.h"
#include "settings/tabletdialog.h"
#include <QApplication>
#include <QComboBox>
#include <QTemporaryDir>
#include <QDir>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTabletEvent>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QTouchEvent>
#include <QKeyEvent>
#include <windows.h>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

using namespace penutil;

namespace {

double nowMs() { static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }(); return double(t.nsecsElapsed()) / 1e6; }

QPointingDevice* penDevice() {
  // never destroyed: a static device would unregister itself after the application is gone
  static QPointingDevice* d = new QPointingDevice("audit pen", 301, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
  return d;
}
QPointingDevice* touchDevice() {
  static QPointingDevice* d = new QPointingDevice("audit touch", 302, QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 5, 0);
  return d;
}

void tablet(DocumentView& v, QEvent::Type type, const QPointF& viewPos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
  QTabletEvent ev(type, penDevice(), viewPos, v.viewport()->mapToGlobal(viewPos), pressure, 0, 0, 0, 0, 0, Qt::NoModifier, button, buttons);
  QApplication::sendEvent(v.viewport(), &ev);
}

void mouse(DocumentView& v, QEvent::Type type, const QPointF& viewPos, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::MouseEventSource src = Qt::MouseEventNotSynthesized) {
  QMouseEvent ev(type, viewPos, viewPos, v.viewport()->mapToGlobal(viewPos), button, buttons, Qt::NoModifier, src);
  QApplication::sendEvent(v.viewport(), &ev);
}

void wheel(DocumentView& v, const QPointF& viewPos, int dy, Qt::KeyboardModifiers m) {
  QWheelEvent ev(viewPos, v.viewport()->mapToGlobal(viewPos), QPoint(), QPoint(0, dy), Qt::NoButton, m, Qt::NoScrollPhase, false);
  QApplication::sendEvent(v.viewport(), &ev);
}

/** One touch event with the given fingers: (id, state, view position). */
struct Finger { int id; QEventPoint::State state; QPointF pos; };
void touch(DocumentView& v, QEvent::Type type, const std::vector<Finger>& fingers, quint64 ts) {
  QList<QEventPoint> pts;
  const QPointF origin(v.viewport()->mapTo(v.window(), QPoint(0, 0)));
  for (const Finger& f : fingers) {
    QEventPoint p(f.id, f.state, f.pos + origin, v.viewport()->mapToGlobal(f.pos));
    pts.append(p);
  }
  QTouchEvent ev(type, touchDevice(), Qt::NoModifier, pts);
  ev.setTimestamp(ts);
  QApplication::sendEvent(v.viewport(), &ev);
}

int elementsOf(Editor& ed, int page) {
  int n = 0;
  for (const Layer& l : ed.doc().pages[size_t(page)].layers) n += int(l.elements.size());
  return n;
}

bool redPixels(const QImage& img, const QRect& r, int minRed = 200) {
  const QImage a = img.convertToFormat(QImage::Format_RGB32);
  for (int y = std::max(0, r.top()); y <= std::min(a.height() - 1, r.bottom()); y++)
    for (int x = std::max(0, r.left()); x <= std::min(a.width() - 1, r.right()); x++) {
      const QRgb c = a.pixel(x, y);
      if (qRed(c) >= minRed && qGreen(c) < 120 && qBlue(c) < 120) return true;
    }
  return false;
}

/** Runs a stroke through the tool and returns the viewport as the eye sees it just before the pen lifts (live layer) and after (tiles). */
struct LiveDone { QImage live, liveAtUp, done; QRect box; };
LiveDone drawStroke(Editor& ed, DocumentView& v, int page, const std::vector<QPointF>& pts, double pressure) {
  v.renderVisibleNow();
  pump();
  ed.pointerDown(v.makeEvent(page, pts[0], pressure));
  for (size_t i = 1; i < pts.size(); i++) ed.pointerMove(v.makeEvent(page, pts[i], pressure));
  pump();
  LiveDone r;
  double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
  for (const QPointF& p : pts) { const QPointF q = v.toView(page, p); x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x()); y0 = std::min(y0, q.y()); y1 = std::max(y1, q.y()); }
  r.box = QRectF(QPointF(x0 - 70, y0 - 70), QPointF(x1 + 70, y1 + 70)).toAlignedRect();
  r.live = v.viewport()->grab().toImage();
  ed.pointerUp(v.makeEvent(page, pts.back(), 0.0));
  r.liveAtUp = v.viewport()->grab().toImage();   // the live layer once the last stretch is in, before the tiles take over
  r.done = v.snapshot();
  return r;
}

}  // namespace

void penAuditChecks(MainWindow& win, Editor& ed, const CheckReporter& r) {
  DocumentView& v = *win.view();
  r.out("\n== pen audit: zoom, single page, fingers and palm, laser, hand, highlighter and glow while drawing (synthetic events; no physical pen or touch screen) ==");
  const int depth0 = ed.undoDepth();
  const Style st0 = ed.style();
  const TabletSettings ts0 = ed.tablet();
  const bool finger0 = ed.fingerDraw();
  const int page = ed.insertPage(ed.pageCount(), 0);
  v.scrollToPage(page);
  v.setZoom(1.0);
  settle(v);
  ed.setTool("pen");
  const QPointF centre(v.viewport()->width() / 2.0, v.viewport()->height() / 2.0);

  // ---- zoom ---------------------------------------------------------------------------------------------------------------------------------
  {
    v.setZoom(1.0);
    wheel(v, centre, 120, Qt::ControlModifier);
    r.check(std::abs(v.zoom() - std::pow(1.0015, 120)) < 1e-3, QString("zoom: one Ctrl+wheel notch up = x%1 (Electron: exp(0.0015 * delta))").arg(v.zoom(), 0, 'f', 4));
    for (int i = 0; i < 60; i++) wheel(v, centre, 120, Qt::ControlModifier);
    r.check(std::abs(v.zoom() - DocumentView::kMaxZoom) < 1e-9, "zoom: Ctrl+wheel stops at 2000 %");
    for (int i = 0; i < 120; i++) wheel(v, centre, -120, Qt::ControlModifier);
    r.check(std::abs(v.zoom() - 0.25) < 1e-9, "zoom: Ctrl+wheel stops at 25 %");
    v.setZoom(1.0);
    ed.action("view.zoomIn")->trigger();
    r.check(std::abs(v.zoom() - 1.2) < 1e-6, "zoom: Ctrl++ / the button zoom by 1.2 (as the Electron app)");
    ed.action("view.zoomOut")->trigger();
    r.check(std::abs(v.zoom() - 1.0) < 1e-6, "zoom: Ctrl+- is the exact inverse");
    v.setZoom(2.5);
    ed.action("view.zoom100")->trigger();
    r.check(std::abs(v.zoom() - 1.0) < 1e-9, "zoom: Ctrl+0 goes to 100 %");
    v.zoomFitWidth();
    settle(v);
    const double pw = ed.doc().pages[size_t(page)].width() * v.zoom();
    r.check(pw <= v.viewport()->width() && pw > 0.8 * v.viewport()->width(), QString("zoom: fit to width fills the window (page %1 px of %2)").arg(pw, 0, 'f', 0).arg(v.viewport()->width()));
    v.zoomFitPage();
    settle(v);
    const double ph = ed.doc().pages[size_t(page)].height() * v.zoom();
    r.check(ph <= v.viewport()->height() && ph > 0.8 * v.viewport()->height(), QString("zoom: fit to page shows the whole page (%1 px of %2)").arg(ph, 0, 'f', 0).arg(v.viewport()->height()));
    // Shift + wheel scrolls sideways (a browser does), the plain wheel up/down
    v.setZoom(3.0);
    v.scrollToPage(page);
    settle(v);
    const int h0 = v.horizontalScrollBar()->value(), vv0 = v.verticalScrollBar()->value();
    v.horizontalScrollBar()->setValue(std::min(200, v.horizontalScrollBar()->maximum()));
    const int h1 = v.horizontalScrollBar()->value();
    wheel(v, centre, -120, Qt::ShiftModifier);
    r.check(v.horizontalScrollBar()->value() > h1 && v.verticalScrollBar()->value() == vv0, "scroll: Shift + wheel scrolls sideways");
    wheel(v, centre, 120, Qt::NoModifier);
    r.check(v.verticalScrollBar()->value() < vv0 || vv0 == 0, "scroll: the wheel scrolls up");
    Q_UNUSED(h0);
    v.setZoom(1.0);
    // the point under the pointer stays under it
    v.setZoom(1.0);
    v.scrollToPage(page);
    const int pg = v.pageAt(centre, true);
    const QPointF before = v.toPage(pg, centre);
    wheel(v, centre, 240, Qt::ControlModifier);
    const QPointF after = v.toPage(pg, centre);
    r.check(std::abs(before.x() - after.x()) < 1.0 && std::abs(before.y() - after.y()) < 1.0, "zoom: Ctrl+wheel keeps the page point under the pointer");
    v.setZoom(1.0);
  }

  // ---- single-page view: the wheel turns the page at its end ------------------------------------------------------------------------------------
  {
    ed.action("view.continuous")->setChecked(false);
    settle(v);
    v.zoomFitWidth();
    v.scrollToPage(page - 1 >= 0 ? page - 1 : page);
    settle(v);
    const int cur = ed.currentPage();
    r.check(v.visiblePages() == std::vector<int>{cur}, "single page: only the current page is shown");
    QScrollBar* sb = v.verticalScrollBar();
    sb->setValue(sb->maximum());
    wheel(v, centre, -120, Qt::NoModifier);
    r.check(ed.currentPage() == cur + 1, "single page: wheel down at the end of a page turns to the next one");
    wheel(v, centre, -120, Qt::NoModifier);
    r.check(ed.currentPage() == cur + 1, "single page: a second notch within 350 ms does not turn two pages");
    QThread::msleep(380);
    sb->setValue(sb->minimum());
    wheel(v, centre, 120, Qt::NoModifier);
    r.check(ed.currentPage() == cur && v.verticalScrollBar()->value() == v.verticalScrollBar()->maximum(), "single page: wheel up at the top turns back and shows the bottom of the previous page");
    ed.action("view.continuous")->setChecked(true);
    settle(v);
    v.scrollToPage(page);
    r.check(v.visiblePages().size() >= 1 && ed.pageCount() >= 2, "single page: continuous view shows pages again");
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);
  }

  // ---- fingers and palm -------------------------------------------------------------------------------------------------------------------------
  {
    ed.setFingerDraw(false);
    v.forgetPen();
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);
    v.verticalScrollBar()->setValue(200);
    const int n0 = elementsOf(ed, page);
    const int sv0 = v.verticalScrollBar()->value();
    quint64 t = 1000;
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 400)}}, t);
    for (int i = 1; i <= 10; i++) { t += 16; touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300, 400 - i * 6)}}, t); }
    t += 16;
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(300, 340)}}, t);
    r.check(v.verticalScrollBar()->value() > sv0 && elementsOf(ed, page) == n0, "finger: one finger drags the page (finger drawing off) and draws nothing");
    r.check(!ed.gestureActive(), "finger: no gesture is left open");

    // a flick keeps scrolling for a moment after the finger lifts (inertia)
    v.verticalScrollBar()->setValue(100);
    QElapsedTimer clock;
    clock.start();
    auto realT = [&] { return quint64(1000 + clock.elapsed()); };
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 500)}}, realT());
    for (int i = 1; i <= 6; i++) { QThread::msleep(16); touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300, 500 - i * 25)}}, realT()); }
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(300, 350)}}, realT());
    const int atLift = v.verticalScrollBar()->value();
    const bool started = v.inertiaActive();
    clock.restart();
    while (clock.elapsed() < 400) QApplication::processEvents(QEventLoop::AllEvents, 10);
    r.check(started && v.verticalScrollBar()->value() > atLift + 40, QString("finger: a flick keeps scrolling after the finger lifts (inertia %1, %2 px more)").arg(started).arg(v.verticalScrollBar()->value() - atLift));
    clock.restart();
    while (v.inertiaActive() && clock.elapsed() < 4000) QApplication::processEvents(QEventLoop::AllEvents, 10);
    r.check(!v.inertiaActive(), "finger: the flick decays and stops by itself");
    // a finger that rests before lifting does not flick
    v.verticalScrollBar()->setValue(100);
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 500)}}, realT());
    for (int i = 1; i <= 6; i++) { QThread::msleep(16); touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300, 500 - i * 25)}}, realT()); }
    QThread::msleep(250);
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(300, 350)}}, realT());
    r.check(!v.inertiaActive(), "finger: pausing before lifting does not flick");

    // two fingers: pinch zoom around their centre
    v.setZoom(1.0);
    v.verticalScrollBar()->setValue(200);
    const double z0 = v.zoom();
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(500, 400)}}, realT());
    touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Stationary, QPointF(500, 400)}, {2, QEventPoint::State::Pressed, QPointF(600, 400)}}, realT());
    for (int i = 1; i <= 8; i++) touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(500 - i * 10, 400)}, {2, QEventPoint::State::Updated, QPointF(600 + i * 10, 400)}}, realT());
    r.check(std::abs(v.zoom() / z0 - 2.6) < 0.1, QString("fingers: fingers 100 px apart that end 260 px apart zoom by 2.6 (x%1)").arg(v.zoom() / z0, 0, 'f', 2));
    touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Released, QPointF(420, 400)}, {2, QEventPoint::State::Updated, QPointF(680, 400)}}, realT());
    touch(v, QEvent::TouchEnd, {{2, QEventPoint::State::Released, QPointF(680, 400)}}, realT());
    r.check(elementsOf(ed, page) == n0 && !ed.gestureActive(), "fingers: a pinch draws nothing");
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);

    // finger drawing on: one finger draws a stroke (a constant width: a finger reports no pressure)
    ed.setFingerDraw(true);
    ed.setTool("pen");
    const int n1 = elementsOf(ed, page);
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 300)}}, realT());
    for (int i = 1; i <= 12; i++) touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300 + i * 9, 300 + 20 * std::sin(i * 0.5))}}, realT());
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(408, 300)}}, realT());
    r.check(elementsOf(ed, page) == n1 + 1, "finger: with finger drawing on, one finger draws a stroke");
    // a second finger lands in the middle of it: the half stroke is dropped (pinch), nothing is left
    const int n2 = elementsOf(ed, page);
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 500)}}, realT());
    for (int i = 1; i <= 5; i++) touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300 + i * 9, 500)}}, realT());
    touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Stationary, QPointF(345, 500)}, {2, QEventPoint::State::Pressed, QPointF(500, 500)}}, realT());
    touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Released, QPointF(345, 500)}, {2, QEventPoint::State::Released, QPointF(500, 500)}}, realT());
    touch(v, QEvent::TouchEnd, {{2, QEventPoint::State::Released, QPointF(500, 500)}}, realT());
    r.check(elementsOf(ed, page) == n2 && !ed.gestureActive(), "finger: a second finger during a finger stroke drops the stroke instead of leaving a half one");
    // fingers never use the laser
    ed.setTool("laser");
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 300)}}, realT());
    for (int i = 1; i <= 6; i++) touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300 + i * 20, 300)}}, realT());
    pump();
    const QImage withTouch = v.viewport()->grab().toImage();
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(420, 300)}}, realT());
    r.check(!redPixels(withTouch, QRect(280, 280, 200, 40)), "finger: the laser ignores fingers");
    ed.setTool("pen");
    ed.setFingerDraw(false);

    // palm rejection: a finger that lands while the pen is near does nothing (a resting hand), and works again once the pen is gone
    v.forgetPen();
    v.verticalScrollBar()->setValue(200);
    const int sv1 = v.verticalScrollBar()->value();
    tablet(v, QEvent::TabletMove, QPointF(700, 300), 0, Qt::NoButton, Qt::NoButton);   // the pen hovers
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 400)}}, realT());
    for (int i = 1; i <= 8; i++) touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300, 400 - i * 10)}}, realT());
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(300, 320)}}, realT());
    r.check(v.verticalScrollBar()->value() == sv1, "palm: a finger on the screen while the pen hovers is ignored");
    // ... also the mouse events the system makes from a touch
    mouse(v, QEvent::MouseButtonPress, QPointF(300, 400), Qt::LeftButton, Qt::LeftButton, Qt::MouseEventSynthesizedBySystem);
    mouse(v, QEvent::MouseMove, QPointF(300, 300), Qt::NoButton, Qt::LeftButton, Qt::MouseEventSynthesizedBySystem);
    mouse(v, QEvent::MouseButtonRelease, QPointF(300, 300), Qt::LeftButton, Qt::NoButton, Qt::MouseEventSynthesizedBySystem);
    r.check(v.verticalScrollBar()->value() == sv1 && !ed.gestureActive(), "palm: the emulated mouse events of a palm are ignored too");
    v.forgetPen();
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(300, 400)}}, realT());
    for (int i = 1; i <= 8; i++) touch(v, QEvent::TouchUpdate, {{1, QEventPoint::State::Updated, QPointF(300, 400 - i * 10)}}, realT());
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(300, 320)}}, realT());
    r.check(v.verticalScrollBar()->value() > sv1, "palm: once the pen has gone the finger pans again");
    // the pen itself is never rejected, with a hand resting
    v.scrollToPage(page);
    settle(v);
    touch(v, QEvent::TouchBegin, {{1, QEventPoint::State::Pressed, QPointF(900, 600)}}, realT());
    const int n3 = elementsOf(ed, page);
    tablet(v, QEvent::TabletMove, QPointF(300, 300), 0, Qt::NoButton, Qt::NoButton);
    tablet(v, QEvent::TabletPress, QPointF(300, 300), 0.4, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 20; i++) tablet(v, QEvent::TabletMove, QPointF(300 + i * 5, 300 + 10 * std::sin(i * 0.4)), 0.5, Qt::NoButton, Qt::LeftButton);
    tablet(v, QEvent::TabletRelease, QPointF(400, 300), 0, Qt::LeftButton, Qt::NoButton);
    touch(v, QEvent::TouchEnd, {{1, QEventPoint::State::Released, QPointF(900, 600)}}, realT());
    r.check(elementsOf(ed, page) == n3 + 1, "palm: the pen still draws while a hand rests on the screen");
    v.forgetPen();
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);
  }

  // ---- hand tool ------------------------------------------------------------------------------------------------------------------------------
  {
    ed.setTool("hand");
    v.forgetPen();
    v.setZoom(2.0);
    v.scrollToPage(page);
    settle(v);
    v.verticalScrollBar()->setValue(300);
    v.horizontalScrollBar()->setValue(std::min(100, v.horizontalScrollBar()->maximum()));
    const int n0 = elementsOf(ed, page);
    const int sv0 = v.verticalScrollBar()->value();
    mouse(v, QEvent::MouseButtonPress, QPointF(400, 500), Qt::LeftButton, Qt::LeftButton);
    mouse(v, QEvent::MouseMove, QPointF(400, 440), Qt::NoButton, Qt::LeftButton);
    mouse(v, QEvent::MouseMove, QPointF(400, 380), Qt::NoButton, Qt::LeftButton);
    mouse(v, QEvent::MouseButtonRelease, QPointF(400, 380), Qt::LeftButton, Qt::NoButton);
    r.check(v.verticalScrollBar()->value() == sv0 + 120 && elementsOf(ed, page) == n0, "hand: dragging with the mouse moves the page up by the drag distance and draws nothing");
    const int sv1 = v.verticalScrollBar()->value();
    tablet(v, QEvent::TabletMove, QPointF(400, 300), 0, Qt::NoButton, Qt::NoButton);
    tablet(v, QEvent::TabletPress, QPointF(400, 300), 0.5, Qt::LeftButton, Qt::LeftButton);
    tablet(v, QEvent::TabletMove, QPointF(400, 400), 0.5, Qt::NoButton, Qt::LeftButton);
    tablet(v, QEvent::TabletRelease, QPointF(400, 400), 0, Qt::LeftButton, Qt::NoButton);
    r.check(v.verticalScrollBar()->value() == sv1 - 100 && elementsOf(ed, page) == n0, "hand: dragging with the pen moves the page the other way");
    v.setZoom(1.0);
    ed.setTool("pen");
  }

  // ---- laser ------------------------------------------------------------------------------------------------------------------------------------
  {
    ed.setTool("laser");
    v.forgetPen();
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);
    const int n0 = elementsOf(ed, page), d0 = ed.undoDepth();
    // hovering shows nothing
    tablet(v, QEvent::TabletMove, QPointF(300, 300), 0, Qt::NoButton, Qt::NoButton);
    for (int i = 1; i <= 6; i++) tablet(v, QEvent::TabletMove, QPointF(300 + i * 15, 300), 0, Qt::NoButton, Qt::NoButton);
    pump();
    r.check(!redPixels(v.viewport()->grab().toImage(), QRect(250, 250, 300, 100)), "laser: hovering shows nothing (only while pressed)");
    // pressed: a red trail
    tablet(v, QEvent::TabletPress, QPointF(300, 400), 0.5, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; i++) tablet(v, QEvent::TabletMove, QPointF(300 + i * 15, 400), 0.5, Qt::NoButton, Qt::LeftButton);
    pump();
    r.check(redPixels(v.viewport()->grab().toImage(), QRect(250, 370, 300, 60)), "laser: pressed, it leaves a red trail");
    tablet(v, QEvent::TabletRelease, QPointF(480, 400), 0, Qt::LeftButton, Qt::NoButton);
    r.check(elementsOf(ed, page) == n0 && ed.undoDepth() == d0, "laser: it creates nothing and adds no undo step");
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 800) QApplication::processEvents(QEventLoop::AllEvents, 10);
    const bool midway = redPixels(v.viewport()->grab().toImage(), QRect(250, 370, 300, 60), 150);
    while (clock.elapsed() < 1700) QApplication::processEvents(QEventLoop::AllEvents, 10);
    const bool gone = !redPixels(v.viewport()->grab().toImage(), QRect(250, 370, 300, 60), 100);
    r.check(midway && gone, QString("laser: the trail is still there at 0.8 s and gone after 1.4 s (%1, %2)").arg(midway).arg(gone));
    ed.setTool("pen");
  }

  // ---- highlighter: while drawing it multiplies like the finished stroke ------------------------------------------------------------------------------
  {
    Style s = ed.style();
    s.penWidth = 6;
    s.glowOn = false;
    s.color = "#101010";
    ed.setStyle(s);
    v.setZoom(1.5);
    v.scrollToPage(page);
    settle(v);
    ed.setTool("pen");
    drawStroke(ed, v, page, {{150, 300}, {250, 300}, {350, 300}}, 0.5);   // a black bar under the highlighter
    s = ed.style();
    s.color = "#ffe000";
    s.highlighterWidth = 16;
    ed.setStyle(s);
    ed.setTool("highlighter");
    std::vector<QPointF> pts;
    for (int i = 0; i <= 60; i++) pts.push_back({160 + i * 3.0, 280 + i * 0.7});
    const LiveDone c = drawStroke(ed, v, page, pts, 0.5);
    const Diff d = compare(c.live, c.done, QRect(c.box.left(), c.box.top(), c.box.width() - 60, c.box.height()));   // not the last stretch, which the live layer draws when the pen lifts
    r.out("   highlighter over black ink, live vs finished: " + fmtDiff(d));
    if (qEnvironmentVariableIsSet("XL_PEN_DUMP")) { const QString b = qEnvironmentVariable("XL_PEN_DUMP"); c.live.copy(c.box).save(b + "/hl_live.png"); c.done.copy(c.box).save(b + "/hl_done.png"); }
    r.check(d.ssim >= 0.995 && d.mean < 1.0 && d.maxd <= 130, "highlighter: the live layer looks like the finished stroke (Multiply; the edges differ by the overlap of the live pieces, as for the pen)");
    // over the black bar the multiplied colour must stay dark (alpha blending would lighten it)
    const QPointF onBar = v.toView(page, QPointF(250, 300));
    const QRgb px = c.live.convertToFormat(QImage::Format_RGB32).pixel(int(onBar.x()), int(onBar.y()));
    r.check(qRed(px) < 60 && qGreen(px) < 60, QString("highlighter: over black ink the live pixel stays black (%1,%2,%3)").arg(qRed(px)).arg(qGreen(px)).arg(qBlue(px)));
    ed.undo();
    ed.undo();
    ed.setTool("pen");
  }

  // ---- glow while drawing -------------------------------------------------------------------------------------------------------------------------
  {
    Style s = ed.style();
    s.color = "#ff8000";
    s.penWidth = 3;
    s.glowOn = true;
    s.glowSize = 8;
    s.glowIntensity = 0.8;
    ed.setStyle(s);
    v.setZoom(2.0);
    v.scrollToPage(page);
    settle(v);
    ed.setTool("pen");
    // The live glow is the page renderer's own filter run incrementally: the last live frame must be indistinguishable from the committed stroke, for slow and fast
    // strokes, straight and curved, at several zooms and glow settings. SSIM and the largest per-channel difference are measured over the stroke's box and
    // separately over the halo only (pixels the finished stroke has no ink on).
    struct Shape { const char* name; int n; double step; double amp, freq; };
    const Shape shapes[] = {{"slow", 160, 1.2, 25, 0.06}, {"fast", 26, 11.0, 30, 0.35}, {"curved", 90, 2.6, 60, 0.11}};
    struct Setting { double zoom, size, alpha; const char* colour; double width; };
    const Setting settings[] = {{0.5, 8, 0.8, "#ff8000", 3}, {1.0, 8, 0.8, "#000000", 3}, {3.0, 8, 0.8, "#ff8000", 3}, {1.0, 20, 1.0, "#2060ff", 2}, {3.0, 2, 0.3, "#000000", 1.41}, {2.0, 40, 0.5, "#e000a0", 5}};
    const QString dumpDir = qEnvironmentVariable("XL_PEN_DUMP");
    double worstSsim = 1, worstHaloMean = 0;
    int worstMax = 0, runs = 0;
    QString worstWhere;
    for (const Setting& se : settings)
      for (const Shape& sh : shapes) {
        Style s2 = ed.style();
        s2.color = se.colour;
        s2.penWidth = se.width;
        s2.glowOn = true;
        s2.glowSize = se.size;
        s2.glowIntensity = se.alpha;
        ed.setStyle(s2);
        v.setZoom(se.zoom);
        v.scrollToPage(page);
        settle(v);
        std::vector<QPointF> pts;
        for (int i = 0; i < sh.n; i++) pts.push_back({60 + i * sh.step, 250 + sh.amp * std::sin(i * sh.freq)});
        const LiveDone c = drawStroke(ed, v, page, pts, 0.5);
        const Diff d = compare(c.liveAtUp, c.done, c.box);
        const Diff dBefore = compare(c.live, c.done, c.box);   // one frame earlier: still without the last half segment
        // halo only: the area around the stroke, excluding the ink's core, compared by mean difference
        double hs = 0;
        long hn = 0;
        const QImage a = c.liveAtUp.convertToFormat(QImage::Format_RGB32), b = c.done.convertToFormat(QImage::Format_RGB32);
        const QRect rb = c.box.intersected(a.rect());
        for (int y = rb.top(); y <= rb.bottom(); y++)
          for (int x = rb.left(); x <= rb.right(); x++) {
            const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
            if (qRed(pa) == 255 && qGreen(pa) == 255 && qBlue(pa) == 255 && qRed(pb) == 255 && qGreen(pb) == 255 && qBlue(pb) == 255) continue;   // bare paper
            hs += std::max({std::abs(qRed(pa) - qRed(pb)), std::abs(qGreen(pa) - qGreen(pb)), std::abs(qBlue(pa) - qBlue(pb))});
            hn++;
          }
        const double haloMean = hn ? hs / double(hn) : 0;
        r.out(QString("   glow live vs finished: %1 zoom %2 size %3 alpha %4 %5 | %6 | mean over non-paper %7").arg(sh.name).arg(se.zoom).arg(se.size).arg(se.alpha).arg(se.colour).arg(fmtDiff(d)).arg(haloMean, 0, 'f', 3));
        r.out(QString("        (the frame before the pen lifts, without the last half segment: SSIM %1)").arg(dBefore.ssim, 0, 'f', 5));
        if (d.ssim < worstSsim) { worstSsim = d.ssim; worstWhere = QString("%1 zoom %2").arg(sh.name).arg(se.zoom); }
        worstMax = std::max(worstMax, d.maxd);
        worstHaloMean = std::max(worstHaloMean, haloMean);
        runs++;
        if (!dumpDir.isEmpty() && std::abs(se.size - 8) < 1e-9) {
          c.liveAtUp.copy(c.box).save(QString("%1/after_live_%3_z%2.png").arg(dumpDir).arg(int(se.zoom * 100)).arg(sh.name));
          c.done.copy(c.box).save(QString("%1/after_done_%3_z%2.png").arg(dumpDir).arg(int(se.zoom * 100)).arg(sh.name));
        }
        ed.undo();
        settle(v);
      }
    r.out(QString("   glow live vs finished over %1 strokes: worst SSIM %2 (%3), largest channel difference %4 of 255, worst mean difference over non-paper pixels %5").arg(runs).arg(worstSsim, 0, 'f', 5).arg(worstWhere).arg(worstMax).arg(worstHaloMean, 0, 'f', 3));
    r.check(worstSsim >= 0.995, QString("glow: while drawing it looks like the finished stroke (worst SSIM %1 over %2 strokes at zoom 0.5 to 3, glow sizes 2 to 40)").arg(worstSsim, 0, 'f', 5).arg(runs));
    r.check(worstHaloMean < 2.5, QString("glow: the live halo and the finished one differ by under 2.5/255 on average where there is anything (%1)").arg(worstHaloMean, 0, 'f', 3));
    v.setZoom(2.0);
    v.scrollToPage(page);
    settle(v);
    // what the halo costs per event (pen, plain vs glowing; GUI thread, event + the repaint of its rectangle)
    auto perEvent = [&](bool glow, double size = 8) {
      Style sg = ed.style();
      sg.glowOn = glow; sg.glowSize = size; sg.glowIntensity = 0.8;
      ed.setStyle(sg);
      ed.pointerDown(v.makeEvent(page, {100, 400}, 0.4));
      std::vector<double> t;
      for (int i = 1; i <= 800; i++) {
        PointerEvent ev = v.makeEvent(page, {100 + i * 0.5, 400 + 60 * std::sin(i * 0.03)}, 0.3 + 0.6 * std::abs(std::sin(i * 0.01)));
        const double a = nowMs();
        ed.pointerMove(ev);
        QApplication::processEvents(QEventLoop::AllEvents);
        t.push_back(nowMs() - a);
      }
      ed.pointerUp(v.makeEvent(page, {500, 400}, 0.0));
      settle(v);
      ed.undo();
      std::sort(t.begin(), t.end());
      double mean = 0;
      for (double x : t) mean += x;
      return std::array<double, 3>{mean / double(t.size()), t[t.size() / 2], t[size_t(double(t.size()) * 0.99)]};
    };
    const auto plain = perEvent(false);
    v.resetStats();
    const auto glowing = perEvent(true);
    const auto gst = v.stats();
    r.out(QString("   pen event + repaint at zoom 2: plain mean %1 p50 %2 p99 %3 ms | glowing (size 8) mean %4 p50 %5 p99 %6 ms").arg(plain[0], 0, 'f', 3).arg(plain[1], 0, 'f', 3).arg(plain[2], 0, 'f', 3).arg(glowing[0], 0, 'f', 3).arg(glowing[1], 0, 'f', 3).arg(glowing[2], 0, 'f', 3));
    r.out(QString("   live glow (size 8, zoom 2): %1 updates, one per repaint, mean %2 ms worst %3 ms (done when painting, not in the pointer handler)").arg(gst.glowUpdates).arg(gst.glowUpdates ? gst.glowMs / gst.glowUpdates : 0.0, 0, 'f', 3).arg(gst.worstGlowMs, 0, 'f', 3));
    v.resetStats();
    const auto huge = perEvent(true, 40);
    const auto hst = v.stats();
    r.out(QString("   live glow (size 40 = the maximum, zoom 2): event + repaint mean %1 p99 %2 ms; glow update mean %3 ms worst %4 ms").arg(huge[0], 0, 'f', 3).arg(huge[2], 0, 'f', 3).arg(hst.glowUpdates ? hst.glowMs / hst.glowUpdates : 0.0, 0, 'f', 3).arg(hst.worstGlowMs, 0, 'f', 3));
    r.check(huge[2] < 40, QString("glow: even the largest glow (40) at zoom 2 keeps each repaint bounded (p99 %1 ms)").arg(huge[2], 0, 'f', 2));
    r.check(glowing[2] < 8, QString("glow: drawing with the glow on stays fast (p99 %1 ms per event)").arg(glowing[2], 0, 'f', 2));
    s = ed.style();
    s.glowOn = false;
    ed.setStyle(s);
  }

  // ---- cursor: system arrow off the page, the tool's own shape over it ----------------------------------------------------------------------------
  {
    Style s = ed.style();
    s.color = "#cc2200";
    s.penWidth = 4;
    s.glowOn = false;
    ed.setStyle(s);
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);
    QWidget* vp = v.viewport();
    const QRectF pr = v.pageRectOnScreen(page);
    const QPointF onPage = pr.center();
    const QPointF offPage(std::max(2.0, pr.left() / 2), pr.center().y());   // the desk beside the page
    const bool haveDesk = v.pageAt(offPage, false) < 0;
    r.check(haveDesk, "cursor: the test point beside the page really is off the page");
    auto hover = [&](const QPointF& pt) { mouse(v, QEvent::MouseMove, pt, Qt::NoButton, Qt::NoButton); };
    auto diam = [&] { const QCursor c = vp->cursor(); return c.shape() == Qt::BitmapCursor ? int(std::lround(c.pixmap().width() / c.pixmap().devicePixelRatio())) : -1; };
    ed.setTool("pen");
    hover(offPage);
    r.check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: pen, pointer off the page = the system arrow");
    hover(onPage);
    const int d4 = diam();
    r.check(vp->cursor().shape() == Qt::BitmapCursor && d4 > 0 && std::abs(d4 - (4 + 6)) <= 1, QString("cursor: pen over the page = a disc as wide as the stroke (4 units at zoom 1: bitmap %1 px incl. 6 px of outline room)").arg(d4));
    hover(offPage);
    r.check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: leaving the page goes back to the arrow");
    hover(onPage);
    const qint64 key1 = vp->cursor().pixmap().cacheKey();
    s.color = "#0044ff";
    ed.setStyle(s);
    r.check(vp->cursor().shape() == Qt::BitmapCursor && vp->cursor().pixmap().cacheKey() != key1, "cursor: changing the colour changes the disc at once (no pointer motion)");
    s.penWidth = 20;
    ed.setStyle(s);
    r.check(diam() == 20 + 6, QString("cursor: changing the size changes the disc at once (20 units = %1 px)").arg(diam() - 6));
    v.setZoom(2.0);
    v.scrollToPage(page);
    settle(v);
    hover(v.pageRectOnScreen(page).center());
    r.check(diam() == 40 + 6, QString("cursor: zooming x2 doubles the disc (%1 px)").arg(diam() - 6));
    s.penWidth = 0.1;
    ed.setStyle(s);
    r.check(diam() == pencursor::kMinDiameter + 6, QString("cursor: a hairline pen keeps a visible minimum (%1 px)").arg(diam() - 6));
    s.penWidth = 4;
    ed.setStyle(s);
    v.setZoom(1.0);
    v.scrollToPage(page);
    settle(v);
    const QPointF on1 = v.pageRectOnScreen(page).center(), off1(std::max(2.0, v.pageRectOnScreen(page).left() / 2), on1.y());
    ed.setTool("highlighter");
    hover(on1);
    r.check(vp->cursor().shape() == Qt::BitmapCursor, "cursor: highlighter over the page = its own disc");
    hover(off1);
    r.check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: highlighter off the page = the arrow");
    ed.setTool("eraser");
    hover(off1);
    r.check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: eraser off the page = the arrow (its ring is only drawn on the page)");
    hover(on1);
    r.check(vp->cursor().shape() == Qt::BlankCursor, "cursor: eraser over the page = the ring only");
    ed.setTool("text");
    hover(on1);
    r.check(vp->cursor().shape() == Qt::IBeamCursor, "cursor: text over the page = I-beam");
    hover(off1);
    r.check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: text off the page = the arrow");
    ed.setTool("pen");
    hover(on1);
    r.check(vp->cursor().shape() == Qt::BitmapCursor, "cursor: back to the pen over the page = its disc");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(vp, &leave);
    r.check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: the pointer leaving the view = the arrow");
    hover(on1);
    // a pen stroke keeps the disc even if the pointer strays off the page
    ed.pointerDown(v.makeEvent(page, {100, 100}, 0.5));
    hover(off1);
    r.check(vp->cursor().shape() == Qt::BitmapCursor, "cursor: during a stroke the pen keeps its disc");
    ed.pointerCancel();
    ed.setStyle(st0);
  }

  // ---- thumbnails of a dense page are drawn on a worker --------------------------------------------------------------------------------------------
  {
    chrome::Chrome* c = ::chromeOf(win);
    if (c && c->thumbs()) {
      Page& pg = ed.doc().pages[size_t(page)];
      std::mt19937 rng(5);
      std::uniform_real_distribution<double> ux(20, pg.width() - 20), uy(20, pg.height() - 20), ua(0, 6.28);
      Layer& layer = pg.layers.back();
      const size_t keep = layer.elements.size();
      for (int i = 0; i < 20000; i++) {
        Stroke s;
        s.attrs = {{"tool", "pen"}, {"color", "#303090ff"}, {"capStyle", "round"}};
        double x = ux(rng), y = uy(rng), a = ua(rng);
        s.widths = {1.2};
        for (int k = 0; k < 60; k++) { a += 0.1; x = std::clamp(x + std::cos(a) * 2.5, 5.0, pg.width() - 5); y = std::clamp(y + std::sin(a) * 2.5, 5.0, pg.height() - 5); s.pts.push_back(x); s.pts.push_back(y); }
        layer.elements.emplace_back(std::move(s));
      }
      ed.invalidateIndex(page);
      ed.touch(page);
      settle(v);   // the page's own tiles and index first: only the thumbnail is measured
      chrome::ThumbsPanel* t = c->thumbs();
      std::vector<double> worst, totals;
      for (int rep = 0; rep < 3; rep++) {
        t->invalidate(-1, true);
        t->list()->scrollToItem(t->list()->item(page));
        QElapsedTimer clk;
        clk.start();
        t->request(page);
        double worstGui = 0;
        while (t->cached(page).isNull() && clk.elapsed() < 20000) {
          const double a = nowMs();
          QApplication::processEvents(QEventLoop::AllEvents, 20);
          worstGui = std::max(worstGui, nowMs() - a);
          QThread::msleep(2);
        }
        worst.push_back(worstGui);
        totals.push_back(double(clk.elapsed()));
        r.check(!t->cached(page).isNull(), "thumbnails: a 20000-stroke page gets its thumbnail");
      }
      std::sort(worst.begin(), worst.end());
      r.out(QString("   thumbnail of a 20000-stroke page, 3 runs: ready after %1 / %2 / %3 ms, longest GUI-thread turn meanwhile %4 / %5 / %6 ms").arg(totals[0], 0, 'f', 0).arg(totals[1], 0, 'f', 0).arg(totals[2], 0, 'f', 0).arg(worst[0], 0, 'f', 0).arg(worst[1], 0, 'f', 0).arg(worst[2], 0, 'f', 0));
      r.check(worst[1] < 150, QString("thumbnails: the window is not blocked while it is drawn (median of the longest turns %1 ms; it was ~1000 ms on the GUI thread)").arg(worst[1], 0, 'f', 0));
      layer.elements.resize(keep);
      ed.invalidateIndex(page);
      ed.touch(page);
    }
  }

  // ---- the README's tool keys and the eraser's ring -----------------------------------------------------------------------------------------------------------
  {
    v.setFocus();
    const struct { int key; const char* tool; } keys[] = {{Qt::Key_H, "highlighter"}, {Qt::Key_E, "eraser"}, {Qt::Key_B, "eraserPart"}, {Qt::Key_K, "laser"}, {Qt::Key_M, "hand"}, {Qt::Key_P, "pen"}};
    bool all = true;
    QString bad;
    for (const auto& k : keys) {
      QKeyEvent ev(QEvent::KeyPress, k.key, Qt::NoModifier, 0, 0, 0, QString(QChar(char(k.key))).toLower());
      QApplication::sendEvent(v.viewport(), &ev);
      if (ed.tool() != k.tool) { all = false; bad += QString(" %1->%2").arg(char(k.key)).arg(ed.tool()); }
    }
    r.check(all, "keys: P H E B K M select pen, highlighter, eraser, partial eraser, laser pointer and hand" + bad);
    // the ring: hovering with the eraser draws a ring of the real radius; leaving takes it away
    ed.setTool("eraser");
    v.setZoom(2.0);
    v.scrollToPage(page);
    settle(v);
    Style s2 = ed.style();
    s2.eraserRadius = 16;
    ed.setStyle(s2);
    const QImage plain = v.viewport()->grab().toImage();
    tablet(v, QEvent::TabletMove, QPointF(500, 400), 0, Qt::NoButton, Qt::NoButton);
    pump(3);
    const QImage ring = v.viewport()->grab().toImage();
    // radius 16 page units at zoom 2 = 32 px: pixels that changed on the circle of 32 px, none on the circle of 20 px
    auto changedAt = [&](const QImage& img, double radiusPx) {
      const QImage a = img.convertToFormat(QImage::Format_RGB32), b = plain.convertToFormat(QImage::Format_RGB32);
      int n = 0;
      for (int deg = 0; deg < 360; deg += 5) {
        bool any = false;
        for (double dr = -1.5; dr <= 1.5; dr += 1.0) {
          const int x = int(500 + (radiusPx + dr) * std::cos(deg * 3.14159265 / 180)), y = int(400 + (radiusPx + dr) * std::sin(deg * 3.14159265 / 180));
          if (std::abs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y))) > 30) any = true;
        }
        if (any) n++;
      }
      return n;
    };
    r.check(changedAt(ring, 32) > 60 && changedAt(ring, 20) < 5, QString("eraser: hovering shows a ring of the real radius (%1 of 72 directions changed at 32 px, %2 at 20 px)").arg(changedAt(ring, 32)).arg(changedAt(ring, 20)));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(v.viewport(), &leave);
    pump(3);
    r.check(changedAt(v.viewport()->grab().toImage(), 32) < 5, "eraser: the ring goes when the pointer leaves");
    ed.setStyle(st0);
    ed.setTool("pen");
    v.setZoom(1.0);
  }

  // ---- idle: nothing keeps the CPU busy ---------------------------------------------------------------------------------------------------------------------
  {
    settle(v);
    FILETIME c, e, k0, u0, k1, u1;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k0, &u0);
    QElapsedTimer clk;
    clk.start();
    {
      QEventLoop loop;   // a real wait in the event loop: a spinning processEvents would burn the CPU by itself
      QTimer::singleShot(2000, &loop, &QEventLoop::quit);
      loop.exec();
    }
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k1, &u1);
    auto ms = [](const FILETIME& a, const FILETIME& b) { return double(ULARGE_INTEGER{{a.dwLowDateTime, a.dwHighDateTime}}.QuadPart + ULARGE_INTEGER{{b.dwLowDateTime, b.dwHighDateTime}}.QuadPart) / 10000.0; };
    const double cpu = ms(k1, u1) - ms(k0, u0);
    r.out(QString("   idle for 2 s: %1 ms of CPU (all threads)").arg(cpu, 0, 'f', 0));
    r.check(cpu < 100, QString("idle: the application uses no CPU while nothing happens (%1 ms in 2 s)").arg(cpu, 0, 'f', 0));
  }

  // ---- the tablet dialog has the control for the plain pen's pressure mapping (TabletSettings::penLook) ------------------------------------------------------
  {
    SettingsController* c = SettingsController::of(win);
    QTemporaryDir tmp;
    if (c && tmp.isValid()) {
      c->setStorage(tmp.filePath("prefs.json"));   // never the user's file
      TabletDialog* td = c->tabletDialog();
      ed.action("pen.settings")->trigger();
      pump(4);
      QComboBox* look = td ? td->lookCombo() : nullptr;
      r.check(look && look->count() == 2 && look->currentIndex() == 0 && ed.tablet().penLook == PenLook::Proto, "tablet dialog: the pressure mapping selector exists and starts on the prototype's");
      if (look) {
        look->setCurrentIndex(1);
        emit look->activated(1);
        r.check(ed.tablet().penLook == PenLook::Electron, "tablet dialog: choosing the Electron mapping applies it to the editor");
        // a stroke drawn now has Electron widths
        ed.setTool("pen");
        v.setZoom(1.0);
        v.scrollToPage(page);
        settle(v);
        ed.pointerDown(v.makeEvent(page, {200, 200}, 0.5));
        ed.pointerMove(v.makeEvent(page, {210, 200}, 0.5));
        ed.pointerMove(v.makeEvent(page, {220, 200}, 0.5));
        ed.pointerUp(v.makeEvent(page, {220, 200}, 0.0));
        const auto& els = ed.doc().pages[size_t(page)].layers.back().elements;
        const Stroke* k = els.empty() ? nullptr : std::get_if<Stroke>(&els.back());
        r.check(k && k->widths.size() > 1 && std::abs(k->widths[1] - penWidth(PenLook::Electron, ed.style().penWidth, 0.5)) < 1e-6, "tablet dialog: and the next stroke uses the Electron width mapping");
        ed.undo();
        look->setCurrentIndex(0);
        emit look->activated(0);
        r.check(ed.tablet().penLook == PenLook::Proto, "tablet dialog: and back to the prototype's mapping");
        r.check(appActions().size() >= 30, QString("tablet: the key bindings offer %1 actions (README: more than 30)").arg(appActions().size()));
      }
      if (td) td->close();
      pump(2);
      // the arrow on the pen's own icon opens it (Electron: "Lápiz ▾"); a press on the icon itself only selects the pen
      QToolButton* penBtn = nullptr;
      for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>()) if (b->defaultAction() == ed.action("tool.pen")) penBtn = b;
      r.check(penBtn && penBtn->property("xlCaret").toBool(), "pen button: it has a caret in its corner (split button) like the other tools with options");   // UI parity: the caret is painted in the corner, as Electron
      if (penBtn && td) {
        ed.setTool("highlighter");
        QMouseEvent pressIcon(QEvent::MouseButtonPress, QPointF(8, penBtn->height() / 2.0), penBtn->mapToGlobal(QPoint(8, penBtn->height() / 2)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent releaseIcon(QEvent::MouseButtonRelease, QPointF(8, penBtn->height() / 2.0), penBtn->mapToGlobal(QPoint(8, penBtn->height() / 2)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(penBtn, &pressIcon);
        QApplication::sendEvent(penBtn, &releaseIcon);
        pump(3);
        r.check(ed.tool() == "pen" && !td->isVisible(), "pen button: a click on the icon selects the pen and does not open the dialog");
        // Changed with the Xournal++-style dropdown (PARIDAD/xpp_features.md): the arrow now opens a menu (sizes, line styles, fill) whose last entry is Lápiz y tableta…
        // (a popup menu is modal, so the test fills it like showing does and triggers the entry instead of clicking the caret)
        QMenu* dd = penBtn->findChild<QMenu*>("dropdown");
        r.check(dd != nullptr, "pen button: its arrow opens a dropdown (sizes, line style, fill)");
        if (dd) {
          QMetaObject::invokeMethod(dd, "aboutToShow");
          for (QAction* a : dd->actions()) if (a->text().startsWith(QString::fromUtf8("Lápiz y tableta"))) a->trigger();
        }
        pump(3);
        r.check(td->isVisible(), "pen button: the dropdown's Lápiz y tableta entry opens the dialog");
        td->close();
        pump(2);
      }
    }
  }

  // ---- the toolbar at 1400 px and narrower: colours, sizes and zoom stay reachable (a second row instead of a chevron that hides them) -----------------------
  {
    const QString shots = qEnvironmentVariable("XL_PEN_SHOTS", QDir::currentPath() + "/shots");
    QDir().mkpath(shots);
    const QSize size0 = win.size();
    ed.setTool("pen");
    for (const int w : {2600, 1920, 1400, 1200}) {
      win.resize(w, 950);
      pump(6);
      // every button of both rows must be inside the window (not hidden behind the extension chevron)
      int hidden = 0, total = 0;
      for (QToolBar* tb : {win.toolBar(), win.styleBar()})
        for (QAction* a : tb->actions()) {
          QWidget* wd = tb->widgetForAction(a);
          if (!wd || a->isSeparator() || !a->isVisible()) continue;
          total++;
          const QPoint g = wd->mapTo(&win, QPoint(0, 0));
          if (!wd->isVisible() || g.x() + wd->width() > win.width() || g.y() + wd->height() > win.height() || wd->width() < 20) hidden++;
        }
      r.out(QString("   window %1 px: %2 toolbar buttons, %3 hidden or cut; colours/sizes row at y=%4 (tools row y=%5)").arg(w).arg(total).arg(hidden).arg(win.styleBar()->geometry().y()).arg(win.toolBar()->geometry().y()));
      r.check(hidden == 0, QString("toolbar: every button is reachable at %1 px wide (%2 of %3 hidden)").arg(w).arg(hidden).arg(total));
      win.grab().save(QString("%1/pen_toolbar_%2.png").arg(shots).arg(w));
    }
    win.resize(1400, 950);
    pump(6);
    r.check(win.styleBar()->geometry().y() > win.toolBar()->geometry().y(), "toolbar: at 1400 px the colour/size row goes below the tools row");
    win.resize(2600, 950);
    pump(6);
    r.check(win.styleBar()->geometry().y() == win.toolBar()->geometry().y(), "toolbar: at 2600 px both rows share one line");
    win.resize(size0);
    pump(6);
  }

  // clean up
  ed.setStyle(st0);
  ed.setTablet(ts0);
  ed.setFingerDraw(finger0);
  v.forgetPen();
  while (ed.undoDepth() > depth0) ed.undo();
  v.setZoom(1.0);
  settle(v);
}
