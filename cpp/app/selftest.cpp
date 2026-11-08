#include "selftest.h"
#include "optionspanel.h"
#include "chrome/chrome.h"
#include "tools/penchecks.h"
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "mainwindow.h"
#include "mathobjects/mathselftest.h"
#include "mathobjects/chainselftest.h"
#include "objects/objects.h"
#include "pdf/pdfactions.h"
#include "texttools/texttools.h"
#include "selection/parity_selftest.h"
#include "selection/selection_selftest.h"
#include "settings/settingsselftest.h"
#include "xopp.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <random>
#include <windows.h>
#include <psapi.h>

extern "C" BOOL WINAPI K32GetProcessMemoryInfo(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);

namespace {

FILE* g_log = nullptr;
int g_fail = 0;

void out(const QString& s) {
  const QByteArray b = s.toLocal8Bit() + "\n";
  fwrite(b.constData(), 1, size_t(b.size()), stdout);
  fflush(stdout);
  if (g_log) { fwrite(b.constData(), 1, size_t(b.size()), g_log); fflush(g_log); }
}

void check(bool ok, const QString& what) {
  out(QString(ok ? "  PASS  " : "  FAIL  ") + what);
  if (!ok) g_fail++;
}

double nowMs() { static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }(); return double(t.nsecsElapsed()) / 1e6; }

struct Stat {
  double mean = 0, p50 = 0, p99 = 0, max = 0;
};
Stat statOf(std::vector<double> v) {
  Stat s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  double sum = 0;
  for (double x : v) sum += x;
  s.mean = sum / double(v.size());
  s.p50 = v[v.size() / 2];
  s.p99 = v[std::min(v.size() - 1, size_t(double(v.size()) * 0.99))];
  s.max = v.back();
  return s;
}
QString fmt(const Stat& s) { return QString("mean %1  p50 %2  p99 %3  max %4 ms").arg(s.mean, 0, 'f', 3).arg(s.p50, 0, 'f', 3).arg(s.p99, 0, 'f', 3).arg(s.max, 0, 'f', 3); }

double peakMemoryMB() {
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof(pmc);
  if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return -1;
  return double(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
}
double currentMemoryMB();
void memMark(const char* after) { out(QString("   [memory after %1: %2 MB now, %3 MB peak so far]").arg(after).arg(currentMemoryMB(), 0, 'f', 0).arg(peakMemoryMB(), 0, 'f', 0)); }
double currentMemoryMB() {
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof(pmc);
  if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return -1;
  return double(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

void pump(int rounds = 3) { for (int i = 0; i < rounds; i++) QApplication::processEvents(); }

/** Process events until the tile queue is empty (or the timeout), as the real event loop would while idle. */
double settleView(DocumentView& v, int timeoutMs = 20000) {
  const double t0 = nowMs();
  while (nowMs() - t0 < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!v.hasPendingTiles()) break;
    QThread::msleep(1);
  }
  return nowMs() - t0;
}

size_t topLayer(Editor& ed, int page) { return ed.doc().pages[size_t(page)].layers.size() - 1; }
size_t elementsOn(Editor& ed, int page) { size_t n = 0; for (const Layer& l : ed.doc().pages[size_t(page)].layers) n += l.elements.size(); return n; }

void fillPage(Editor& ed, int page, int n, unsigned seed = 7) {
  std::mt19937 rng(seed);
  Page& pg = ed.doc().pages[size_t(page)];
  Layer& layer = pg.layers.back();
  const double W = pg.width(), H = pg.height();
  std::uniform_real_distribution<double> ux(20, W - 20), uy(20, H - 20), ua(0, 6.2831), ud(-0.25, 0.25);
  layer.elements.reserve(layer.elements.size() + size_t(n));
  for (int i = 0; i < n; i++) {
    Stroke s;
    s.attrs = {{"tool", "pen"}, {"color", QString("#%1%2%3ff").arg(int(30 + rng() % 150), 2, 16, QChar('0')).arg(int(30 + rng() % 150), 2, 16, QChar('0')).arg(int(30 + rng() % 150), 2, 16, QChar('0'))}, {"capStyle", "round"}};
    s.widths = {1.4};
    double x = ux(rng), y = uy(rng), a = ua(rng);
    for (int k = 0; k < 60; k++) {
      a += ud(rng);
      x = std::clamp(x + std::cos(a) * 2.5, 5.0, W - 5);
      y = std::clamp(y + std::sin(a) * 2.5, 5.0, H - 5);
      s.pts.push_back(x);
      s.pts.push_back(y);
    }
    layer.elements.emplace_back(std::move(s));
  }
  ed.invalidateIndex(page);
  ed.touch(page);
}

void truncateLayer(Editor& ed, int page, size_t keep) {
  Layer& layer = ed.doc().pages[size_t(page)].layers.back();
  if (layer.elements.size() > keep) layer.elements.resize(keep);
  ed.invalidateIndex(page);
  ed.touch(page);
}

// ---- synthetic devices -------------------------------------------------------------------------------------------------------------------
struct Devices {
  QPointingDevice pen{"selftest pen", 101, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
  QPointingDevice eraser{"selftest eraser", 102, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Eraser, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
};

void sendTablet(DocumentView& v, const QPointingDevice* dev, QEvent::Type type, const QPointF& viewPos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
  QTabletEvent ev(type, dev, viewPos, v.viewport()->mapToGlobal(viewPos), pressure, 0, 0, 0, 0, 0, Qt::NoModifier, button, buttons);
  QApplication::sendEvent(v.viewport(), &ev);
}

void sendMouse(DocumentView& v, QEvent::Type type, const QPointF& viewPos, Qt::MouseButton button, Qt::MouseButtons buttons) {
  QMouseEvent ev(type, viewPos, v.viewport()->mapToGlobal(viewPos), button, buttons, Qt::NoModifier);
  QApplication::sendEvent(v.viewport(), &ev);
}

/** A pen stroke of `n` events as a real driver delivers them: hover, press, moves with varying pressure, release. */
void penStroke(DocumentView& v, const QPointingDevice* dev, int page, const QPointF& from, const QPointF& to, int n, Qt::MouseButtons extra = Qt::NoButton) {
  auto at = [&](double t) { return v.toView(page, from + (to - from) * t); };
  sendTablet(v, dev, QEvent::TabletMove, at(0), 0, Qt::NoButton, extra);
  sendTablet(v, dev, QEvent::TabletPress, at(0), 0.3, Qt::LeftButton, Qt::LeftButton | extra);
  for (int i = 1; i < n; i++) {
    const double t = double(i) / n;
    sendTablet(v, dev, QEvent::TabletMove, at(t) + QPointF(0, 6 * std::sin(t * 9)), 0.2 + 0.7 * std::abs(std::sin(t * 4)), Qt::NoButton, Qt::LeftButton | extra);
  }
  sendTablet(v, dev, QEvent::TabletRelease, at(1), 0, Qt::LeftButton, extra);
  if (extra != Qt::NoButton) sendTablet(v, dev, QEvent::TabletMove, at(1), 0, Qt::NoButton, Qt::NoButton);   // the barrel button is let go too (the view follows it while hovering)
}

// ---- measurements -------------------------------------------------------------------------------------------------------------------------
void pointerTable(MainWindow& win, Editor& ed, int page) {
  DocumentView& v = *win.view();
  out("\n== pointer event cost: pen stroke of 1500 events, N extra strokes (60 points each) on the page ==");
  out("   handle = what the tool does per event (live layer + dirty rect); +paint = also the repaint of the dirty area");
  const size_t base = ed.doc().pages[size_t(page)].layers.back().elements.size();
  ed.setTool("pen");
  for (int total : {0, 8000, 20000}) {
    truncateLayer(ed, page, base);
    double t0 = nowMs();
    if (total) fillPage(ed, page, total);
    const double fillMs = nowMs() - t0;
    t0 = nowMs();
    v.clearCache();
    const int tiles = v.renderVisibleNow();
    const double coldMs = nowMs() - t0;
    pump();
    std::vector<double> handle, both;
    const int pg = page;
    v.resetStats();
    ed.pointerDown(v.makeEvent(pg, {150, 300}, 0.4));
    pump();
    for (int i = 1; i <= 1500; i++) {
      const double x = 150 + i * 0.25, y = 300 + 60 * std::sin(i * 0.03);
      PointerEvent ev = v.makeEvent(pg, {x, y}, 0.3 + 0.6 * std::abs(std::sin(i * 0.01)));
      ev.timestampMs = quint64(i * 5);
      const double a = nowMs();
      ed.pointerMove(ev);
      const double b = nowMs();
      QApplication::processEvents(QEventLoop::AllEvents);   // paints the dirty rectangle the way the event loop would
      const double c = nowMs();
      handle.push_back(b - a);
      both.push_back(c - a);
    }
    t0 = nowMs();
    ed.pointerUp(v.makeEvent(pg, {150 + 1500 * 0.25, 300}, 0.0));
    const double upMs = nowMs() - t0;
    pump();
    t0 = nowMs();
    ed.undo();
    const double undoMs = nowMs() - t0;
    out(QString("N=%1  fill %2 ms | cold render of %3 visible tiles %4 ms").arg(total, 6).arg(fillMs, 0, 'f', 1).arg(tiles).arg(coldMs, 0, 'f', 1));
    out(QString("   paint events delivered during the 1500 events: %1 (worst single paint %2 ms)").arg(v.stats().paints).arg(v.stats().worstPaintMs, 0, 'f', 3));
    out("   handle:        " + fmt(statOf(handle)));
    out("   handle+paint:  " + fmt(statOf(both)));
    out(QString("   pointer up (commit + patch tiles) %1 ms | undo (take element + flush dirty tiles) %2 ms").arg(upMs, 0, 'f', 2).arg(undoMs, 0, 'f', 2));
  }
  truncateLayer(ed, page, base);
}

void eraserTable(MainWindow& win, Editor& ed, int page) {
  DocumentView& v = *win.view();
  out("\n== eraser (partial) over 20000 strokes: 300 events ==");
  const size_t base = ed.doc().pages[size_t(page)].layers.back().elements.size();
  fillPage(ed, page, 20000);
  v.renderVisibleNow();
  ed.setTool("eraserPart");
  Style st = ed.style();
  st.eraserRadius = 8;
  ed.setStyle(st);
  // the first event builds the spatial index (once per page); the others only query it
  double t0 = nowMs();
  ed.index(page);
  out(QString("   spatial index build: %1 ms").arg(nowMs() - t0, 0, 'f', 1));
  std::vector<double> handle;
  ed.pointerDown(v.makeEvent(page, {60, 100}, 0.5, PointerEvent::Kind::Mouse));
  for (int i = 1; i <= 300; i++) {
    PointerEvent ev = v.makeEvent(page, {60 + i * 1.5, 100 + 400 * std::abs(std::sin(i * 0.02))}, 0.5, PointerEvent::Kind::Mouse);
    const double a = nowMs();
    ed.pointerMove(ev);
    handle.push_back(nowMs() - a);
  }
  ed.pointerUp(v.makeEvent(page, {510, 100}, 0.5, PointerEvent::Kind::Mouse));
  const size_t after = ed.doc().pages[size_t(page)].layers.back().elements.size();
  out("   handle (query + cut + replace): " + fmt(statOf(handle)));
  t0 = nowMs();
  pump();
  QThread::msleep(15);
  pump();
  out(QString("   tiles re-rendered after the cut (coalesced, once per ~8 ms): %1 ms | elements %2 -> %3").arg(nowMs() - t0, 0, 'f', 1).arg(base + 20000).arg(after));
  ed.undo();
  check(ed.doc().pages[size_t(page)].layers.back().elements.size() == base + 20000, "eraser: undo restores all 20000 elements");
  truncateLayer(ed, page, base);
  ed.setTool("pen");
}

void scrollZoom(MainWindow& win, Editor&) {
  DocumentView& v = *win.view();
  out("\n== scroll and zoom on the loaded document ==");
  v.clearCache();
  v.resetStats();
  double t0 = nowMs();
  const int n = v.renderVisibleNow();
  out(QString("cold: %1 visible tiles rendered sharp in %2 ms (%3 ms/tile)").arg(n).arg(nowMs() - t0, 0, 'f', 1).arg((nowMs() - t0) / std::max(1, n), 0, 'f', 1));
  t0 = nowMs();
  v.viewport()->repaint();
  out(QString("warm: full repaint from cache %1 ms").arg(nowMs() - t0, 0, 'f', 2));
  t0 = nowMs();
  v.invalidateOverlay(0);
  v.clearCache();
  const double settle = settleView(v);
  out(QString("cold through the event loop (job queue, 12 ms slices, prefetch ring): %1 ms; %2 tiles cached, %3 MB").arg(settle, 0, 'f', 1).arg(v.cachedTiles()).arg(double(v.cacheBytes()) / 1048576.0, 0, 'f', 1));

  std::vector<double> steps;
  for (int i = 0; i < 80; i++) {
    const double a = nowMs();
    v.scrollByPixels(0, 60);
    v.viewport()->repaint();   // the paint that follows a scroll step, counted
    QApplication::processEvents(QEventLoop::AllEvents);   // and any tile job that became due
    steps.push_back(nowMs() - a);
  }
  out("scroll step (60 px, lite tiles while moving): " + fmt(statOf(steps)));
  const double s1 = settleView(v);
  out(QString("   settle after scroll: %1 ms, tiles lite rendered so far %2").arg(s1, 0, 'f', 1).arg(v.stats().tilesLite));

  steps.clear();
  for (int i = 0; i < 20; i++) {
    const double a = nowMs();
    v.zoomBy(1.08);
    v.viewport()->repaint();
    QApplication::processEvents(QEventLoop::AllEvents);
    steps.push_back(nowMs() - a);
  }
  out(QString("zoom step (x1.08, stale tiles scaled + lite): ") + fmt(statOf(steps)) + QString("   -> zoom %1").arg(v.zoom(), 0, 'f', 2));
  const double s2 = settleView(v);
  out(QString("   settle after zoom (sharp tiles at the new scale): %1 ms").arg(s2, 0, 'f', 1));
  v.zoomFitWidth();
  settleView(v);
  v.scrollToPage(0);
  settleView(v);
}

// ---- behaviour checks ----------------------------------------------------------------------------------------------------------------------
void behaviour(MainWindow& win, Editor& ed, int page, const QString& shots) {
  DocumentView& v = *win.view();
  out("\n== behaviour checks (synthetic events through the real Qt event path; not a physical pen) ==");
  Devices dev;
  v.zoomFitWidth();
  v.scrollToPage(page);
  settleView(v);
  const size_t n0 = elementsOn(ed, page);
  const int depth0 = ed.undoDepth();

  ed.setTool("pen");
  penStroke(v, &dev.pen, page, {120, 200}, {400, 260}, 40);
  settleView(v);
  check(elementsOn(ed, page) == n0 + 1, "tablet pen press/move/release adds one stroke");
  const Stroke* last = nullptr;
  if (elementsOn(ed, page) == n0 + 1) last = std::get_if<Stroke>(&ed.doc().pages[size_t(page)].layers.back().elements.back());
  check(last && last->widths.size() > 1 && last->widths.size() == last->pts.size() / 2, "pen stroke stores a width per point (pressure)");
  check(last && last->tool() == "pen", "stroke is a pen stroke");
  check(ed.undoDepth() == depth0 + 1 && ed.dirty(), "one undo step, document dirty");

  sendMouse(v, QEvent::MouseButtonPress, v.toView(page, {120, 300}), Qt::LeftButton, Qt::LeftButton);
  for (int i = 1; i <= 20; i++) sendMouse(v, QEvent::MouseMove, v.toView(page, {120 + i * 12.0, 300 + 20 * std::sin(i * 0.5)}), Qt::NoButton, Qt::LeftButton);
  sendMouse(v, QEvent::MouseButtonRelease, v.toView(page, {360, 300}), Qt::LeftButton, Qt::NoButton);
  check(elementsOn(ed, page) == n0 + 2, "mouse fallback draws a stroke");
  const Stroke* ms = std::get_if<Stroke>(&ed.doc().pages[size_t(page)].layers.back().elements.back());
  check(ms && ms->widths.size() == 1, "a mouse stroke has one constant width");

  ed.undo();
  ed.undo();
  check(elementsOn(ed, page) == n0, "undo twice removes both strokes");
  ed.redo();
  check(elementsOn(ed, page) == n0 + 1, "redo puts one back");
  ed.redo();

  // eraser end of the pen over the first stroke
  penStroke(v, &dev.eraser, page, {100, 200}, {420, 262}, 40);
  check(elementsOn(ed, page) < n0 + 2, "eraser end of the pen erases (mapped to the eraser tool)");
  ed.undo();
  check(elementsOn(ed, page) == n0 + 2, "undo of the erase restores the stroke");
  ed.undo();
  ed.undo();
  check(elementsOn(ed, page) == n0, "back to the original page content");

  // barrel button 2 = hand: dragging scrolls instead of drawing
  v.setZoom(2.0);
  settleView(v);
  v.scrollToPage(page);
  const int sv0 = v.verticalScrollBar()->value();
  penStroke(v, &dev.pen, page, {100, 200}, {100, 100}, 10, Qt::MiddleButton);
  check(elementsOn(ed, page) == n0 && v.verticalScrollBar()->value() != sv0, "pen barrel button 2 drags the page (hand) and draws nothing");
  bool toolbox = false;
  QObject::connect(&ed, &Editor::toolboxRequested, &ed, [&](const QPoint&) { toolbox = true; });
  penStroke(v, &dev.pen, page, {100, 200}, {140, 200}, 5, Qt::RightButton);
  check(toolbox && elementsOn(ed, page) == n0, "pen barrel button 1 requests the tool box and draws nothing");
  v.zoomFitWidth();
  settleView(v);

  // returnTo logic
  ToolInfo f1, f2;
  f1.id = "fake1"; f1.label = "F1"; f1.inserter = true; f1.factory = [](Editor& e) { struct T : Tool { using Tool::Tool; }; return std::make_unique<T>(e); };
  f2 = f1; f2.id = "fake2"; f2.label = "F2";
  ed.registerTool(f1);
  ed.registerTool(f2);
  ed.setTool("highlighter");
  ed.setTool("fake1");
  ed.afterCreate("fake1");
  check(ed.tool() == "select" && ed.returnTo() == "highlighter", "after an inserter: Select, remembering the tool used before it");
  ed.escape();
  check(ed.tool() == "highlighter", "Esc returns to the remembered tool");
  ed.setTool("fake1");
  ed.setTool("fake2");
  ed.afterCreate("fake2");
  check(ed.returnTo() == "pen", "when the tool before was another inserter, return to the pen");
  ed.setTool("select");
  check(ed.returnTo().isEmpty(), "choosing Select on purpose clears the return");
  ed.setTool("pen");

  // a click on the empty page while Select was chosen only to adjust what was just made returns to the tool before, and that tool takes the gesture
  ed.setTool("highlighter");
  ed.setTool("fake1");
  ed.afterCreate("fake1");
  {
    const size_t before = elementsOn(ed, page);
    ed.pointerDown(v.makeEvent(page, {300, 500}, 0.5, PointerEvent::Kind::Mouse));
    ed.pointerMove(v.makeEvent(page, {340, 520}, 0.5, PointerEvent::Kind::Mouse));
    ed.pointerUp(v.makeEvent(page, {380, 540}, 0.5, PointerEvent::Kind::Mouse));
    out(QString("   tool now %1, elements %2 -> %3").arg(ed.tool()).arg(before).arg(elementsOn(ed, page)));
    check(ed.tool() == "highlighter" && elementsOn(ed, page) == before + 1, "click on the empty page returns to the tool before and that tool draws");
    ed.undo();
  }
  ed.setTool("pen");

  // generated objects: swapGenerated as one undo step
  {
    const int layer = int(topLayer(ed, page));
    std::vector<Element> a{strokeEl({0, 0, 10, 10}, "#ff0000", "ff", 1, "axis", "plT1"), strokeEl({0, 10, 10, 0}, "#ff0000", "ff", 1, "grid", "plT1"), strokeEl({5, 5, 6, 6}, "#ff0000", "ff", 1, "x", "plT1")};
    for (const Element& e : a) ed.addElement(page, layer, e);   // primitives push no history
    const size_t n1 = elementsOn(ed, page);
    std::vector<Element> b{strokeEl({1, 1, 2, 2}, "#00ff00", "ff", 2, "axis", "plT1"), strokeEl({3, 3, 4, 4}, "#00ff00", "ff", 2, "grid", "plT1")};
    auto sw = ed.swapGenerated(page, layer, "plT1", b);
    Action act;
    act.label = "Cambiar plano"; act.page = page; act.undo = sw.undo; act.redo = sw.redo;
    ed.pushAction(act);
    check(elementsOn(ed, page) == n1 - 1, "swapGenerated replaces 3 pieces by 2 (net -1)");
    ed.undo();
    check(elementsOn(ed, page) == n1, "undo of the swap restores the 3 pieces");
    ed.redo();
    check(elementsOn(ed, page) == n1 - 1, "redo swaps again");
    ed.undo();
    truncateLayer(ed, page, n0);
  }

  // zoom keeps the point under the pointer where it is
  {
    const QPointF anchor(500, 400);
    const int p = v.pageAt(anchor, true);
    const QPointF before = v.toPage(p, anchor);
    v.zoomBy(1.7, anchor);
    const QPointF after = v.toPage(p, anchor);
    check(std::abs(before.x() - after.x()) < 1.0 && std::abs(before.y() - after.y()) < 1.0, "zoom around the pointer keeps the page point under it");
    v.zoomFitWidth();
    settleView(v);
  }
  // single-page mode shows one page; continuous shows several
  if (ed.pageCount() > 1) {
    v.setContinuous(false);
    ed.setCurrentPage(1);
    check(v.visiblePages() == std::vector<int>{1}, "single-page mode shows only the current page");
    v.setContinuous(true);
    v.zoomFitWidth();
    v.scrollToPage(page);
    settleView(v);
  }

  // pages and files
  const int pages0 = ed.pageCount();
  ed.insertPage(1, 0);
  check(ed.pageCount() == pages0 + 1, "insert page");
  ed.undo();
  check(ed.pageCount() == pages0, "undo insert page");
  ed.redo();
  ed.movePage(1, -1);
  ed.undo();
  ed.undo();
  check(ed.pageCount() == pages0, "move + insert undone");

  QTemporaryDir dir;
  const QString f = dir.filePath("selftest.xopp");
  Document before = ed.doc();
  QString err;
  check(saveXopp(f, ed.doc(), &err), "save .xopp " + err);
  LoadResult lr = loadXopp(f);
  check(lr.ok && lr.doc == before, "save and reload give the same document");

  // the screenshots the report mentions
  v.zoomFitWidth();
  v.scrollToPage(page);
  settleView(v);
  v.renderVisibleNow();
  pump();
  QDir().mkpath(shots);
  check(win.grab().save(shots + "/page.png"), "screenshot of the window saved: " + shots + "/page.png");
  v.setZoom(2.2);
  settleView(v);
  v.renderVisibleNow();
  pump();
  win.grab().save(shots + "/zoomed.png");
  // a stroke in progress (live layer visible), captured mid-gesture
  v.zoomFitWidth();
  settleView(v);
  ed.pointerDown(v.makeEvent(page, {100, 120}, 0.5));
  for (int i = 1; i < 120; i++) ed.pointerMove(v.makeEvent(page, {100 + i * 3.0, 120 + 40 * std::sin(i * 0.12)}, 0.15 + 0.8 * std::abs(std::sin(i * 0.05))));
  pump();
  win.grab().save(shots + "/live.png");
  ed.pointerCancel();
  pump();
  check(elementsOn(ed, page) == n0, "a cancelled gesture leaves nothing behind");
}

}  // namespace

int runSelfTest(MainWindow& win, Editor& ed, const SelfTestOptions& o) {
  if (!o.logPath.isEmpty()) g_log = fopen(o.logPath.toLocal8Bit().constData(), "w");
  else g_log = fopen("xl_app_selftest.log", "w");
  win.resize(1400, 950);
  QString sample = o.files.isEmpty() ? QStringLiteral("G:/xournal-lab/samples/Calculo_multivariable_corte_2.xopp") : o.files.first();
  double t0 = nowMs();
  const bool ok = win.openFile(sample);
  out(QString("open %1: %2 in %3 ms").arg(sample, ok ? "ok" : "FAILED").arg(nowMs() - t0, 0, 'f', 1));
  if (!ok) return 2;
  const Counts c = countOf(ed.doc());
  out(QString("document: %1 pages, %2 layers, %3 strokes, %4 texts, %5 images, %6 points").arg(c.pages).arg(c.layers).arg(c.strokes).arg(c.texts).arg(c.images).arg(c.points));
  t0 = nowMs();
  win.show();
  pump();
  const double first = settleView(*win.view());
  out(QString("window shown, first view complete (visible tiles + prefetch) after %1 ms (show+layout %2 ms)").arg(first, 0, 'f', 1).arg(nowMs() - t0 - first, 0, 'f', 1));
  const int page = 0;
  if (qEnvironmentVariableIsSet("XL_SETTINGS_ONLY")) {   // a quick loop for the settings area: only its checks (set the variable to anything)
    runSettingsChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "panelfit") {   // every tool options popup fits the screen
    runPanelFitChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); });
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "toolbarfit") {
    runToolbarFitChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "selection") {   // only the selection area's checks
    selx::runSelectionChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    out(g_fail ? QString("\n%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("\nall checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "chain") {   // select -> generated handlers for every object kind
    g_fail += mathobj::chainSelfTest(win, ed, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir, [](const QString& line) { out(line); });
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "parity") {   // the select area's parity audit (real key and mouse path)
    selx::runParityChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    out(g_fail ? QString("\n%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("\nall checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "pdf") {   // just the PDF area's checks
    g_fail += pdfSelfTest(win, [](const QString& line) { out(line); });
    out(g_fail ? QString("\n%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("\nall checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "chrome") {   // the chrome area (themes, welcome, panels, file lifecycle, menus) only
    chrome::runChromeSelfTest(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                                        o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir});
    out(g_fail ? QString("\n\n%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("\n\nall checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "texttools") {   // iterate on one area without running the rest
    texttools::runTextToolsSelfTest(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    out(g_fail ? QString("\n%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("\nall checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "math") {   // just the real line / set diagrams / fill / Riemann checks
    g_fail += mathobj::mathObjectsSelfTest(win, ed, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir, [](const QString& line) { out(line); });
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "pointer") {   // the pen path numbers (handle + paint per event, pointer up, undo) at 0 / 8000 / 20000 strokes
    pointerTable(win, ed, page);
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "penaudit") {   // the audit of the pen only
    penAuditChecks(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }});
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "penstale") {   // just the dense-tile checks (with XL_STALE_LOOP=N a stress loop of the stale-result handling)
    denseTileChecks(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }});
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (qEnvironmentVariable("XL_SELFTEST_ONLY") == "pen") {   // the pen feel and dense-page tile checks and the before/after table only
    const CheckReporter rep{[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }};
    penFeelChecks(win, ed, rep);
    denseTileChecks(win, ed, rep);
    denseTileBench(win, ed, rep);
    out(g_fail ? QString("%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("all checks passed"));
    if (g_log) fclose(g_log);
    return g_fail ? 1 : 0;
  }
  if (!o.benchOnly) scrollZoom(win, ed);
  pointerTable(win, ed, page);
  denseTileBench(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }});
  if (!o.benchOnly) {
    eraserTable(win, ed, page);
    memMark("eraserTablewin ed page");
    behaviour(win, ed, page, o.shotsDir);
    memMark("behaviourwin ed page o.shotsDir");
    penFeelChecks(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }});
    memMark("penFeelCheckswin ed bool ok const QStrin");
    denseTileChecks(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }});
    memMark("denseTileCheckswin ed bool ok const QStr");
    penAuditChecks(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }});
    xlobjects::runObjectsSelfTest(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                                            o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir});
    memMark("xlobjects::runObjectsSelfTestwin ed bool");
    texttools::runTextToolsSelfTest(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                                    o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    memMark("texttools::runTextToolsSelfTestwin ed bo");
    chrome::runChromeSelfTest(win, ed, {[](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                                        o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir});
    memMark("chrome::runChromeSelfTestwin ed bool ok ");
    runSettingsChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                      o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    memMark("runSettingsCheckswin ed bool ok const QS");
    g_fail += mathobj::mathObjectsSelfTest(win, ed, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir, [](const QString& line) { out(line); });
    memMark("g_fail  mathobj::mathObjectsSelfTestwin ");
    g_fail += pdfSelfTest(win, [](const QString& line) { out(line); });
    // last: it replaces the open document with its own fixture
    memMark("g_fail  pdfSelfTestwin const QString lin");
    selx::runSelectionChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                             o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    memMark("selx::runSelectionCheckswin ed bool ok c");
    selx::runParityChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); },
                          o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
    // also last: it replaces the document again (its fixtures are one object per page)
    memMark("selx::runParityCheckswin ed bool ok cons");
    g_fail += mathobj::chainSelfTest(win, ed, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir, [](const QString& line) { out(line); });
    memMark("g_fail  mathobj::chainSelfTestwin ed o.s");
    // popups and toolbar widths: they resize the window, so after everything that measures the page
    runPanelFitChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); });
    runToolbarFitChecks(win, ed, [](bool ok, const QString& what) { check(ok, what); }, [](const QString& line) { out(line); }, o.shotsDir.isEmpty() ? QDir::currentPath() + "/shots" : o.shotsDir);
  }
  out(QString("\nmemory: peak working set %1 MB, now %2 MB; tile cache %3 tiles / %4 MB").arg(peakMemoryMB(), 0, 'f', 0).arg(currentMemoryMB(), 0, 'f', 0).arg(win.view()->cachedTiles()).arg(double(win.view()->cacheBytes()) / 1048576.0, 0, 'f', 1));
  out(g_fail ? QString("\n%1 CHECK(S) FAILED").arg(g_fail) : QStringLiteral("\nall checks passed"));
  if (g_log) fclose(g_log);
  return g_fail ? 1 : 0;
}
