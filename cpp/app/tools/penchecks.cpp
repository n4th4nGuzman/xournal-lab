#include "penchecks.h"
#include "penutil.h"
#include "brush.h"
#include "documentview.h"
#include "inkpiece.h"
#include "liveink.h"
#include "editor.h"
#include "mainwindow.h"
#include "pagerender.h"
#include "xopp.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <windows.h>
#include <psapi.h>

extern "C" BOOL WINAPI K32GetProcessMemoryInfo(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);

namespace {

using namespace penutil;

struct Sample { QPointF pos; double pressure; };

std::vector<Sample> wave(QPointF from, int n, double step, double firstPressure) {
  std::vector<Sample> v;
  for (int i = 0; i < n; i++) {
    const double p = i == 0 ? firstPressure : 0.15 + 0.8 * std::abs(std::sin(i * 0.05));
    v.push_back({QPointF(from.x() + i * step, from.y() + 25 * std::sin(i * 0.08)), p});
  }
  return v;
}

const Stroke* lastStroke(Editor& ed, int page) {
  const auto& els = ed.doc().pages[size_t(page)].layers.back().elements;
  return els.empty() ? nullptr : std::get_if<Stroke>(&els.back());
}

QRect strokeViewRect(DocumentView& v, int page, const std::vector<Sample>& s) {
  double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
  for (const Sample& p : s) {
    const QPointF q = v.toView(page, p.pos);
    x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x()); y0 = std::min(y0, q.y()); y1 = std::max(y1, q.y());
  }
  return QRectF(QPointF(x0 - 8, y0 - 8), QPointF(x1 + 8, y1 + 8)).toAlignedRect();
}

/** Full gesture through the tool: live image just before the pen lifts, finished image after. */
struct LiveVsDone { QImage live, done; QRect box; };
LiveVsDone drawAndCapture(Editor& ed, DocumentView& v, int page, const std::vector<Sample>& s, double releasePressure = 0.0) {
  v.renderVisibleNow();
  pump();
  ed.pointerDown(v.makeEvent(page, s[0].pos, s[0].pressure));
  for (size_t i = 1; i < s.size(); i++) {
    PointerEvent e = v.makeEvent(page, s[i].pos, s[i].pressure);
    e.timestampMs = quint64(i * 5);
    ed.pointerMove(e);
  }
  pump();
  LiveVsDone r;
  r.box = strokeViewRect(v, page, s);
  ed.pointerUp(v.makeEvent(page, s.back().pos, releasePressure));
  r.live = v.viewport()->grab().toImage();   // the last frame before the stroke is drawn into the tiles: still the live layer (what the eye sees up to the pop)
  r.done = v.snapshot();
  return r;
}

}  // namespace

void penFeelChecks(MainWindow& win, Editor& ed, const CheckReporter& r) {
  DocumentView& v = *win.view();
  r.out("\n== pen feel: live layer vs finished stroke, widths, start of a stroke, file round trip (synthetic events, not a physical pen) ==");
  const int depth0 = ed.undoDepth();
  const int page = ed.insertPage(ed.pageCount(), 0);
  v.scrollToPage(page);
  settle(v);
  ed.setTool("pen");
  Style st0 = ed.style();
  Style st = st0;
  st.penWidth = 4.0;   // thick enough for differences to show
  st.glowOn = false;
  ed.setStyle(st);
  const TabletSettings ts0 = ed.tablet();
  {
    TabletSettings t = ts0;
    t.brush = BrushKind::Pen;
    t.penLook = PenLook::Proto;
    t.curve = kLinearCurve;
    ed.setTablet(t);
  }
  r.check(ed.tablet().penLook == PenLook::Proto, "default pen look is the prototype's (TabletSettings::penLook = Proto)");

  const double zoom0 = v.zoom();
  for (const bool batching : {true, false}) {
    setInkBatching(batching);   // true = how pages are drawn by default, false = piece by piece like the live layer
    for (double z : {1.0, 2.2}) {
      v.setZoom(z);
      v.scrollToPage(page);
      settle(v);
      for (int variant = 0; variant < 2; variant++) {
        const bool slow = variant == 1;   // slow = many close points (a pen at 200 Hz), fast = few far points
        const auto s = wave(QPointF(60 + variant * 10, 80 + variant * 60), slow ? 220 : 70, slow ? 1.0 : 3.2, 0.9);
        const LiveVsDone c = drawAndCapture(ed, v, page, s);
        const Diff d = compare(c.live, c.done, c.box);
        r.out(QString("   %1 zoom %2, %3 stroke: live vs finished: %4").arg(batching ? "batched runs," : "piece by piece,").arg(z).arg(slow ? "slow" : "fast").arg(fmtDiff(d)));
        if (qEnvironmentVariableIsSet("XL_PEN_DUMP")) {
          const QString base = QString("%1/pen_%2_z%3_%4").arg(qEnvironmentVariable("XL_PEN_DUMP")).arg(batching ? "b" : "p").arg(z).arg(slow ? "slow" : "fast");
          QImage la = c.live.copy(c.box).convertToFormat(QImage::Format_RGB32), lb = c.done.copy(c.box).convertToFormat(QImage::Format_RGB32), df(la.size(), QImage::Format_RGB32);
          for (int y = 0; y < la.height(); y++) for (int x = 0; x < la.width(); x++) { const int m = std::min(255, 4 * std::abs(qGray(la.pixel(x, y)) - qGray(lb.pixel(x, y)))); df.setPixel(x, y, qRgb(255 - m, 255 - m, 255 - m)); }
          la.save(base + "_live.png"); lb.save(base + "_done.png"); df.save(base + "_diff.png");
        }
        const bool ok = batching ? d.ssim >= 0.995 && d.mean < 1.0 && d.maxd <= 90 : d.ssim >= 0.9999 && d.maxd <= 3;
        r.check(ok, QString("live layer and finished stroke look the same (%1, zoom %2, %3)").arg(batching ? "batched runs" : "piece by piece").arg(z).arg(slow ? "slow" : "fast"));
      }
    }
  }
  setInkBatching(true);

  for (BrushKind k : {BrushKind::Brush, BrushKind::Ink, BrushKind::Fountain}) {
    TabletSettings t = ed.tablet();
    t.brush = k;
    ed.setTablet(t);
    v.setZoom(2.2);
    v.scrollToPage(page);
    settle(v);
    const auto s = wave(QPointF(70, 230 + 50 * (int(k) - 1)), 28, 1.1, 0.9);
    const LiveVsDone c = drawAndCapture(ed, v, page, s);
    const Diff d = compare(c.live, c.done, c.box);
    int inkLive = 0, inkDone = 0;
    const QRect inside = c.box.intersected(c.live.rect()).intersected(c.done.rect());
    const int paperLive = inside.isEmpty() ? 0 : qGray(c.live.pixel(inside.topLeft())), paperDone = inside.isEmpty() ? 0 : qGray(c.done.pixel(inside.topLeft()));
    for (int y = inside.top(); y <= inside.bottom(); y++)
      for (int x = inside.left(); x <= inside.right(); x++) {
        if (std::abs(qGray(c.live.pixel(x, y)) - paperLive) > 60) inkLive++;
        if (std::abs(qGray(c.done.pixel(x, y)) - paperDone) > 60) inkDone++;
      }
    r.out(QString("   tip %1, short stroke: live vs finished: %2; dark pixels live %3, finished %4").arg(brushId(k)).arg(fmtDiff(d)).arg(inkLive).arg(inkDone));
    if (const Stroke* st = lastStroke(ed, page)) {
      QStringList ws;
      for (size_t i = 0; i < st->widths.size(); i += std::max<size_t>(1, st->widths.size() / 8)) ws << QString::number(st->widths[i], 'f', 2);
      r.out(QString("      stored widths (%1 points, every few): %2").arg(st->widths.size()).arg(ws.join(' ')));
    }
    r.check(inkLive > 50 && inkDone > 50 && inkDone >= inkLive * 0.9 && inkDone <= inkLive * 1.1 && d.ssim >= 0.995,
            QString("a short stroke keeps its weight when the pen lifts (tip %1: %2 dark pixels while drawing, %3 after)").arg(brushId(k)).arg(inkLive).arg(inkDone));
  }
  {
    TabletSettings t = ed.tablet();
    t.brush = BrushKind::Pen;
    ed.setTablet(t);
  }

  // the stored widths are the prototype's mapping of the pressure the pen reported, and the first width is not a different-sized dot
  {
    v.setZoom(1.5);
    settle(v);
    const auto s = wave(QPointF(80, 300), 40, 4.0, 0.95);
    drawAndCapture(ed, v, page, s);
    const Stroke* k = lastStroke(ed, page);
    bool ok = k && k->widths.size() == k->pts.size() / 2 && k->widths.size() == s.size();
    double worst = 0;
    if (ok) {
      for (size_t i = 1; i + 1 < s.size(); i++) worst = std::max(worst, std::abs(k->widths[i] - penWidth(PenLook::Proto, st.penWidth, s[i].pressure)));
      ok = worst < 1e-5 && k->widths[0] == k->widths[1];
    }
    r.out(QString("   widths[i] = size * (0.3 + 0.7 * p^0.8): worst difference %1, first width == second: %2").arg(worst).arg(k && k->widths.size() > 1 && k->widths[0] == k->widths[1]));
    r.check(ok, "saved widths follow the prototype mapping, one per point, first = second (no start dot of another size)");
    r.check(std::abs(penWidth(PenLook::Proto, 2, 0) - 0.6) < 1e-12 && std::abs(penWidth(PenLook::Proto, 2, 1) - 2.0) < 1e-12 && std::abs(penWidth(PenLook::Electron, 2, 0.5) - 1.5) < 1e-12 && std::abs(penWidth(PenLook::Electron, 2, 1) - 3.0) < 1e-12,
            "penWidth: Proto 0.3..1.0 x size, Electron 1.5 x p x size");

    // start of a stroke: a first sample at full pressure followed by light ones. The finished stroke must start as thick as its first piece, not as the first sample
    if (k && s.size() > 2) {
      const double w1 = k->widths[1], w0pressure = penWidth(PenLook::Proto, st.penWidth, 0.95);
      Stroke copy = *k;
      QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      {
        QPainter p(&img);
        const double sc = 8;
        p.scale(sc, sc);
        p.translate(-copy.pts[0] + 5, -copy.pts[1] + 12);
        RenderOptions o;
        o.scale = sc;
        o.background = false;
        renderElement(p, Element(copy), o);
      }
      int top = 1000, bottom = -1;   // ink extent in the column of the first point (x = 5 * 8)
      for (int y = 0; y < img.height(); y++) if (qAlpha(img.pixel(40, y)) > 100) { top = std::min(top, y); bottom = std::max(bottom, y); }
      const double thick = (bottom - top + 1) / 8.0;
      r.out(QString("   start of the stroke: ink %1 pt thick at the first point; first piece width %2, a dot of the first sample would be %3").arg(thick, 0, 'f', 2).arg(w1, 0, 'f', 2).arg(w0pressure, 0, 'f', 2));
      r.check(thick <= w1 * 1.2 + 0.3, "no odd dot at the start of the committed stroke");
    }
  }

  // a stroke saved and opened again draws the same pixels
  {
    const Stroke* k = lastStroke(ed, page);
    QTemporaryDir dir;
    QString err;
    const QString f = dir.filePath("pen.xopp");
    bool same = false;
    Diff d;
    if (k && saveXopp(f, ed.doc(), &err)) {
      LoadResult lr = loadXopp(f);
      if (lr.ok && size_t(page) < lr.doc.pages.size()) {
        const auto& els = lr.doc.pages[size_t(page)].layers.back().elements;
        const Stroke* k2 = els.empty() ? nullptr : std::get_if<Stroke>(&els.back());
        if (k2) {
          auto render = [&](const Stroke& s) {
            QImage img(1000, 400, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            p.scale(2, 2);
            p.translate(-40, -260);
            RenderOptions o;
            o.scale = 2;
            o.background = false;
            renderElement(p, Element(s), o);
            return img;
          };
          d = compare(render(*k), render(*k2), QRect(0, 0, 1000, 400));
          same = d.maxd <= 2;
        }
      }
    }
    r.out("   saved and re-opened stroke vs the one in memory: " + fmtDiff(d));
    r.check(same, "a saved and re-opened pen stroke draws the same pixels");
  }

  // the Electron mapping stays available behind the setting
  {
    TabletSettings t = ed.tablet();
    t.penLook = PenLook::Electron;
    ed.setTablet(t);
    const auto s = wave(QPointF(80, 380), 30, 4.0, 0.5);
    drawAndCapture(ed, v, page, s);
    const Stroke* k = lastStroke(ed, page);
    double worst = 1e9;
    if (k && k->widths.size() == s.size()) {
      worst = 0;
      for (size_t i = 1; i + 1 < s.size(); i++) worst = std::max(worst, std::abs(k->widths[i] - penWidth(PenLook::Electron, st.penWidth, s[i].pressure)));
    }
    r.check(worst < 1e-5, "setting penLook = Electron brings back max(0.05, 1.5 p) * size");
    TabletSettings back = ed.tablet();
    back.penLook = PenLook::Proto;
    ed.setTablet(back);
    r.check(sanitizeTablet(tabletToJson(t)).penLook == PenLook::Electron && sanitizeTablet(tabletToJson(back)).penLook == PenLook::Proto && !tabletToJson(back).contains("penLook"),
            "penLook round trips through the prefs JSON and is not written when it is the default");
  }

  // a dot (pen put down and lifted without moving) is a round dot of the pressure's width
  {
    const int before = int(ed.doc().pages[size_t(page)].layers.back().elements.size());
    ed.pointerDown(v.makeEvent(page, {300, 420}, 0.8));
    ed.pointerUp(v.makeEvent(page, {300, 420}, 0.0));
    settle(v);
    const Stroke* k = lastStroke(ed, page);
    r.check(int(ed.doc().pages[size_t(page)].layers.back().elements.size()) == before + 1 && k && k->pointCount() == 2 && std::abs(k->widths.back() - penWidth(PenLook::Proto, st.penWidth, 0.8)) < 1e-5,
            "a tap makes a dot with the width of its pressure");
  }

  // clean up: the extra page goes away again
  ed.setStyle(st0);
  ed.setTablet(ts0);
  while (ed.undoDepth() > depth0) ed.undo();
  v.setZoom(zoom0);
  settle(v);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------

namespace {
Stroke randomStroke(std::mt19937& rng, double W, double H, bool pressure) {
  std::uniform_real_distribution<double> ux(20, W - 20), uy(20, H - 20), ua(0, 6.2831), ud(-0.25, 0.25), up(0.1, 1.0);
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", QString("#%1%2%3ff").arg(int(30 + rng() % 150), 2, 16, QChar('0')).arg(int(30 + rng() % 150), 2, 16, QChar('0')).arg(int(30 + rng() % 150), 2, 16, QChar('0'))}, {"capStyle", "round"}};
  double x = ux(rng), y = uy(rng), a = ua(rng);
  s.widths.push_back(1.4);
  for (int k = 0; k < 60; k++) {
    a += ud(rng);
    x = std::clamp(x + std::cos(a) * 2.5, 5.0, W - 5);
    y = std::clamp(y + std::sin(a) * 2.5, 5.0, H - 5);
    s.pts.push_back(x);
    s.pts.push_back(y);
    if (pressure) s.widths.push_back(penWidth(PenLook::Proto, 1.6, up(rng)));
  }
  if (pressure) s.widths[0] = s.widths[1];
  else s.widths = {1.4};
  return s;
}
}  // namespace

void denseTileChecks(MainWindow& win, Editor& ed, const CheckReporter& r) {
  DocumentView& v = *win.view();
  const int depth0 = ed.undoDepth();
  r.out("\n== dense page tiles: index path vs full scan, partial repaint, worker threads, stale results ==");
  const int page = ed.insertPage(ed.pageCount(), 0);
  v.scrollToPage(page);
  settle(v);
  v.zoomFitWidth();
  v.scrollToPage(page);
  settle(v);
  Page& pg = ed.doc().pages[size_t(page)];
  std::mt19937 rng(11);
  Layer& layer = pg.layers.back();
  for (int i = 0; i < 4000; i++) layer.elements.emplace_back(randomStroke(rng, pg.width(), pg.height(), i % 2 == 0));
  for (int i = 0; i < 3; i++) {   // texts: fonts are used on worker threads too
    Text t;
    t.attrs = {{"font", "Sans"}, {"size", "18"}, {"x", QString::number(60 + i * 40)}, {"y", QString::number(120 + i * 150)}, {"color", "#102030ff"}};
    t.text = QString("Texto de prueba %1\nsegunda linea x^2").arg(i);
    layer.elements.emplace_back(std::move(t));
  }
  ed.invalidateIndex(page);
  ed.touch(page);
  const QRect all(0, 0, v.viewport()->width(), v.viewport()->height());

  v.setIndexedTiles(false);
  v.renderVisibleNow();
  const QImage legacy = v.viewport()->grab().toImage();
  v.setIndexedTiles(true);
  v.renderVisibleNow();
  const QImage indexed = v.viewport()->grab().toImage();
  Diff d = compare(legacy, indexed, all);
  r.out("   index + piece culling vs full scan (cold tiles): " + fmtDiff(d));
  r.check(d.maxd <= 2 && d.over16 == 0, "tiles drawn from the spatial index equal the full scan");

  v.setAsyncThreshold(0);   // everything through the workers
  v.clearCache();
  v.resetStats();
  settle(v);
  const QImage async = v.viewport()->grab().toImage();
  d = compare(indexed, async, all);
  const DocumentView::Stats stats = v.stats();
  r.out(QString("   worker-thread tiles vs GUI-thread tiles: %1 (tiles by workers %2, dropped %3)").arg(fmtDiff(d)).arg(stats.tilesAsync).arg(stats.tilesDropped));
  r.check(stats.tilesAsync > 0 && d.maxd <= 2, "tiles drawn on worker threads (text included) equal the GUI-thread ones");

  // a change while workers are drawing: the stale result is thrown away and the final picture has the new stroke
  // (XL_STALE_LOOP=N repeats it N times with different timings: a stress loop for the stale-result handling)
  const int staleRuns = std::max(1, qEnvironmentVariableIntValue("XL_STALE_LOOP"));
  for (int run = 0; run < staleRuns; run++) {
    v.setAsyncThreshold(0);
    v.setIndexedTiles(true);
    v.clearCache();
    v.resetStats();
    v.viewport()->update();
    QApplication::processEvents(QEventLoop::AllEvents, 5 + (run * 7) % 60);   // jobs start (a different number of them done each run)
    Stroke fresh = randomStroke(rng, pg.width(), pg.height(), true);
    fresh.attrs = {{"tool", "pen"}, {"color", "#ff0000ff"}, {"capStyle", "round"}};
    for (size_t i = 0; i < fresh.pts.size(); i += 2) { fresh.pts[i] = 100 + double(i) * 3; fresh.pts[i + 1] = 200 + 40 * std::sin(double(i) * 0.1); }
    ed.commitAdd(page, int(pg.layers.size()) - 1, Element(fresh));
    settle(v);
    const QImage afterChange = v.viewport()->grab().toImage();
    v.setAsyncThreshold(200);
    v.setIndexedTiles(false);
    v.renderVisibleNow();
    const QImage truth = v.viewport()->grab().toImage();
    v.setIndexedTiles(true);
    d = compare(truth, afterChange, all);
    if (staleRuns == 1 || d.maxd > 2) r.out(QString("   change while workers draw: %1 (dropped %2)").arg(fmtDiff(d)).arg(v.stats().tilesDropped));
    if (d.maxd > 2 && qEnvironmentVariableIsSet("XL_PEN_DUMP")) { afterChange.save(qEnvironmentVariable("XL_PEN_DUMP") + QString("/stale_after_%1.png").arg(run)); truth.save(qEnvironmentVariable("XL_PEN_DUMP") + QString("/stale_truth_%1.png").arg(run)); }
    r.check(d.maxd <= 2, QString("a stroke added while workers draw ends up in every tile (stale results are not shown)") + (staleRuns > 1 ? QString(" [run %1]").arg(run) : QString()));
    if (run + 1 < staleRuns) ed.undo();
  }
  v.setAsyncThreshold(200);

  // undo repaints only the stroke's rectangle (partial tile repaint) and gives the same pixels as a full render
  QRectF changed;
  const QMetaObject::Connection cc = QObject::connect(&ed, &Editor::contentChanged, [&](int, const QRectF& rc) { changed = changed.united(rc); });
  v.renderVisibleNow();
  v.resetStats();
  ed.undo();
  QObject::disconnect(cc);
  v.renderVisibleNow();
  const QImage afterUndo = v.viewport()->grab().toImage();
  const DocumentView::Stats us = v.stats();
  v.setIndexedTiles(false);
  v.renderVisibleNow();
  const QImage truth2 = v.viewport()->grab().toImage();
  v.setIndexedTiles(true);
  d = compare(truth2, afterUndo, all);
  const double areaFrac = changed.width() * changed.height() / (pg.width() * pg.height());
  r.out(QString("   undo of one stroke: invalidated %1 x %2 pt (%3 % of the page), tiles repainted partially %4, whole %5; vs full render: %6")
            .arg(changed.width(), 0, 'f', 0).arg(changed.height(), 0, 'f', 0).arg(areaFrac * 100, 0, 'f', 1).arg(us.tilesPartial).arg(us.tilesRendered).arg(fmtDiff(d)));
  r.check(areaFrac < 0.12 && changed.contains(QRectF(100, 160, 100, 80)) && d.maxd <= 2, "undo invalidates the stroke's rectangle only and the repaint equals a full render");
  r.check(us.tilesPartial > 0, "undo repaints a part of the tile, not the whole tile");

  // clean up
  while (ed.undoDepth() > depth0) ed.undo();
  v.setAsyncThreshold(200);
  settle(v);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------

namespace {
double nowMs() { static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }(); return double(t.nsecsElapsed()) / 1e6; }
double workingSetMB() {
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof(pmc);
  return K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) ? double(pmc.WorkingSetSize) / 1048576.0 : -1;
}
double ftMs(const FILETIME& a, const FILETIME& b) { return double((ULARGE_INTEGER{{a.dwLowDateTime, a.dwHighDateTime}}.QuadPart + ULARGE_INTEGER{{b.dwLowDateTime, b.dwHighDateTime}}.QuadPart)) / 10000.0; }
/** CPU time of this thread (the GUI thread) in ms: steady when other processes make the wall clock jump around. */
double threadCpuMs() { FILETIME c, e, k, u; return GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u) ? ftMs(k, u) : -1; }
/** CPU time of all threads of the process in ms. */
double procCpuMs() { FILETIME c, e, k, u; return GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? ftMs(k, u) : -1; }
QString stat(std::vector<double> v) {
  if (v.empty()) return "-";
  std::sort(v.begin(), v.end());
  double sum = 0;
  for (double x : v) sum += x;
  return QString("mean %1 p50 %2 max %3").arg(sum / double(v.size()), 0, 'f', 1).arg(v[v.size() / 2], 0, 'f', 1).arg(v.back(), 0, 'f', 1);
}
}  // namespace

namespace {
/** The live ink as it was before this change (kept here only to compare costs): dot at the start, widths averaged at the midpoints. */
class OldLiveInk {
 public:
  OldLiveInk(QPainter& p, const QColor& color, double scale) : p_(p), color_(color), scale_(scale) {}
  QRectF add(const QPointF& pt, double width) {
    if (n_ == 0) {
      prev_ = mid_ = pt; wPrev_ = wMid_ = width; n_ = 1;
      p_.setPen(Qt::NoPen); p_.setBrush(color_);
      const double r = width / 2;
      p_.drawEllipse(pt, r, r);
      return QRectF(pt.x() - r - 2, pt.y() - r - 2, 2 * r + 4, 2 * r + 4);
    }
    const QPointF m((prev_.x() + pt.x()) / 2, (prev_.y() + pt.y()) / 2);
    const double wm = (wPrev_ + width) / 2;
    const QRectF r = seg(mid_, prev_, m, wMid_, wm);
    prev_ = pt; mid_ = m; wPrev_ = width; wMid_ = wm; n_++;
    return r;
  }
 private:
  QRectF seg(const QPointF& a, const QPointF& c, const QPointF& b, double wa, double wb) {
    const double lenPx = (std::hypot(b.x() - a.x(), b.y() - a.y()) + std::hypot(c.x() - a.x(), c.y() - a.y())) * scale_;
    const int steps = std::clamp(int(lenPx / 3.0), 1, 24);
    QPen pen(color_);
    pen.setCapStyle(Qt::RoundCap);
    QPointF last = a;
    for (int k = 1; k <= steps; k++) {
      const double t = double(k) / steps, u = 1 - t;
      const QPointF q(u * u * a.x() + 2 * u * t * c.x() + t * t * b.x(), u * u * a.y() + 2 * u * t * c.y() + t * t * b.y());
      pen.setWidthF(wa + (wb - wa) * (double(k) - 0.5) / steps);
      p_.setPen(pen);
      p_.drawLine(last, q);
      last = q;
    }
    const double w = std::max(wa, wb) + 2 / std::max(scale_, 1e-6);
    return QRectF(QPointF(std::min({a.x(), b.x(), c.x()}) - w, std::min({a.y(), b.y(), c.y()}) - w), QPointF(std::max({a.x(), b.x(), c.x()}) + w, std::max({a.y(), b.y(), c.y()}) + w));
  }
  QPainter& p_; QColor color_; double scale_; QPointF prev_, mid_; double wPrev_ = 0, wMid_ = 0; int n_ = 0;
};
}  // namespace

void livePathBench(const CheckReporter& r) {
  r.out("\n== live ink cost per point (CPU time of the GUI thread, QPixmap target, 30000 points, step 1.5 pt, pressure widths) ==");
  for (const double scale : {1.0, 2.2}) {
    QPixmap pm(1400, 900);
    pm.fill(Qt::white);
    auto run = [&](auto&& make, const char* name) {
      double best = 1e18;
      for (int rep = 0; rep < 3; rep++) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(scale, scale);
        std::optional<decltype(make(p))> ink;
        ink.emplace(make(p));
        const double c0 = threadCpuMs();
        for (int i = 0; i < 30000; i++) {
          const double x = 20 + std::fmod(i * 1.5, 500.0), y = 40 + (i / 330) * 4.0 + 20 * std::sin(i * 0.05);
          ink->add(QPointF(x, y), 1.4 * (0.3 + 0.7 * std::abs(std::sin(i * 0.01))));
          if (i % 330 == 329) ink.emplace(make(p));   // a new stroke every ~500 pt
        }
        best = std::min(best, (threadCpuMs() - c0) / 30000.0 * 1000.0);
      }
      r.out(QString("   scale %1: %2: %3 us per point").arg(scale).arg(name).arg(best, 0, 'f', 2));
    };
    run([&](QPainter& p) { return OldLiveInk(p, QColor(10, 20, 30), scale); }, "before (OldLiveInk)");
    run([&](QPainter& p) { return LiveInk(p, QColor(10, 20, 30), scale); }, "after  (LiveInk)   ");
  }
}

void denseTileBench(MainWindow& win, Editor& ed, const CheckReporter& r) {
  livePathBench(r);
  DocumentView& v = *win.view();
  r.out("\n== dense page tiles, before/after (same process, same load; ms) ==");
  r.out("   before = every element tested per tile, no piece culling, GUI thread; after = index + piece culling + partial repaint + worker threads");
  const int page = 0;
  const size_t base = ed.doc().pages[size_t(page)].layers.back().elements.size();
  v.zoomFitWidth();
  v.scrollToPage(page);
  settle(v);
  for (int total : {0, 8000, 20000}) {
    {
      Page& pg = ed.doc().pages[size_t(page)];
      Layer& layer = pg.layers.back();
      layer.elements.resize(base);
      std::mt19937 rng(7);
      for (int i = 0; i < total; i++) layer.elements.emplace_back(randomStroke(rng, pg.width(), pg.height(), i % 3 == 0));   // a third with pressure widths
      ed.invalidateIndex(page);
      ed.touch(page);
    }
    r.out(QString("N=%1 extra strokes").arg(total));
    for (const bool after : {false, true}) {
      v.setIndexedTiles(after);
      v.setAsyncThreshold(after ? 200 : 1 << 30);
      v.setZoom(1.0);
      v.scrollToPage(page);
      settle(v);
      v.resetStats();
      // cold, blocking (the sync path: what the self test always measured)
      v.clearCache();
      double t0 = nowMs();
      double c0 = threadCpuMs();
      const int tiles = v.renderVisibleNow();
      const double coldBlock = nowMs() - t0, coldBlockCpu = threadCpuMs() - c0;
      // cold through the event loop (what the user sees when opening or jumping): GUI-thread time and wall time
      v.clearCache();
      v.resetStats();
      t0 = nowMs();
      c0 = procCpuMs();
      const double g0 = threadCpuMs();
      settle(v);
      const double coldWall = nowMs() - t0, coldCpuAll = procCpuMs() - c0, coldCpuGui = threadCpuMs() - g0;
      const DocumentView::Stats cs = v.stats();
      // undo of one long stroke
      Stroke s;
      s.attrs = {{"tool", "pen"}, {"color", "#ff0000ff"}, {"capStyle", "round"}};
      s.widths = {1.4};
      for (int i = 0; i < 1500; i++) { s.pts.push_back(150 + i * 0.25); s.pts.push_back(300 + 60 * std::sin(i * 0.03)); }
      ed.commitAdd(page, int(ed.doc().pages[size_t(page)].layers.size()) - 1, Element(s));
      settle(v);
      v.renderVisibleNow();
      std::vector<double> undoMs, undoWall, undoCpu;
      for (int rep = 0; rep < 3; rep++) {
        t0 = nowMs();
        const double u0 = threadCpuMs();
        ed.undo();
        const double a = nowMs() - t0;
        undoCpu.push_back(threadCpuMs() - u0);
        settle(v);
        undoMs.push_back(a);
        undoWall.push_back(nowMs() - t0);
        ed.redo();
        settle(v);
        v.renderVisibleNow();
      }
      ed.undo();
      settle(v);
      // zoom and scroll steps
      std::vector<double> zoom, scroll;
      v.setZoom(1.0);
      settle(v);
      for (int i = 0; i < 12; i++) {
        const double a = nowMs();
        v.zoomBy(1.08);
        v.viewport()->repaint();
        QApplication::processEvents(QEventLoop::AllEvents);
        zoom.push_back(nowMs() - a);
      }
      if (qEnvironmentVariableIsSet("XL_PEN_DEBUG")) { QString l = "      zoom steps:"; for (double z : zoom) l += QString(" %1").arg(z, 0, 'f', 0); r.out(l); const auto st = v.stats(); r.out(QString("      stats: rendered %1 lite %2 async %3 dropped %4 partial %5 renderMs %6 asyncMs %7").arg(st.tilesRendered).arg(st.tilesLite).arg(st.tilesAsync).arg(st.tilesDropped).arg(st.tilesPartial).arg(st.renderMs).arg(st.asyncMs)); }
      t0 = nowMs();
      settle(v);
      const double zoomSettle = nowMs() - t0;
      for (int i = 0; i < 40; i++) {
        const double a = nowMs();
        v.scrollByPixels(0, 60);
        v.viewport()->repaint();
        QApplication::processEvents(QEventLoop::AllEvents);
        scroll.push_back(nowMs() - a);
      }
      t0 = nowMs();
      settle(v);
      const double scrollSettle = nowMs() - t0;
      r.out(QString("   %1: cold %2 tiles blocking: wall %3, GUI CPU %4 | cold via event loop: wall %5, GUI-thread CPU %6, all threads CPU %7, tiles by workers %8")
                .arg(after ? "after " : "before", 6).arg(tiles).arg(coldBlock, 0, 'f', 1).arg(coldBlockCpu, 0, 'f', 1).arg(coldWall, 0, 'f', 1).arg(coldCpuGui, 0, 'f', 1).arg(coldCpuAll, 0, 'f', 1).arg(cs.tilesAsync));
      r.out(QString("            undo call on the GUI thread: wall %1, CPU %2 | undo until pixels right (wall) %3 | zoom step %4 (settle %5) | scroll step %6 (settle %7) | memory %8 MB")
                .arg(stat(undoMs), stat(undoCpu), stat(undoWall), stat(zoom)).arg(zoomSettle, 0, 'f', 0).arg(stat(scroll)).arg(scrollSettle, 0, 'f', 0).arg(workingSetMB(), 0, 'f', 0));
    }
  }
  v.setIndexedTiles(true);
  v.setAsyncThreshold(200);
  Layer& layer = ed.doc().pages[size_t(page)].layers.back();
  layer.elements.resize(base);
  ed.invalidateIndex(page);
  ed.touch(page);
  v.setZoom(1.0);
  v.zoomFitWidth();
  settle(v);
}
