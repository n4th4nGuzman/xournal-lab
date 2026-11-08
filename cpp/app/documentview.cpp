#include "documentview.h"
#include "settings/devclass.h"
#include "pagerender.h"
#include <QThread>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 16, kGap = 12;               // logical pixels around and between pages
const QColor kDeskColor(0x3b, 0x3b, 0x3b);           // the grey behind the pages (as the Electron app's classic theme)
constexpr int kJobBudgetMs = 12;                     // one slice of tile rendering between two event-loop turns
constexpr int kSettleMs = 150;                       // after the last scroll/zoom step, sharp tiles are rendered
double nowMs() { static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }(); return double(t.nsecsElapsed()) / 1e6; }
}  // namespace

DocumentView::DocumentView(Editor& ed, QWidget* parent) : QAbstractScrollArea(parent), ed_(ed) {
  ed_.setView(this);
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  viewport()->setMouseTracking(true);
  viewport()->setAttribute(Qt::WA_OpaquePaintEvent);   // every pixel is painted by us: no background clear first
  viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);   // real touch events: a finger pans, two fingers pinch (unhandled ones would arrive as emulated mouse events that this view ignores)
  inertia_.setInterval(16);
  connect(&inertia_, &QTimer::timeout, this, &DocumentView::inertiaStep);
  connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) { if (s != Qt::ApplicationActive) penMouse_ = Qt::NoButton; });   // a button released in another window never arrives
  horizontalScrollBar()->setSingleStep(40);
  verticalScrollBar()->setSingleStep(40);

  jobTimer_.setSingleShot(true);
  settleTimer_.setSingleShot(true);
  flushTimer_.setSingleShot(true);
  patchTimer_.setSingleShot(true);
  setWorkerThreads(std::clamp(QThread::idealThreadCount() - 2, 1, 6));
  connect(&patchTimer_, &QTimer::timeout, this, &DocumentView::processPatches);
  connect(&jobTimer_, &QTimer::timeout, this, &DocumentView::processJobs);
  connect(&settleTimer_, &QTimer::timeout, this, &DocumentView::settle);
  connect(&flushTimer_, &QTimer::timeout, this, &DocumentView::flushDirty);

  connect(&ed_, &Editor::contentChanged, this, [this](int page, const QRectF& r) { invalidate(page, r); });
  connect(&ed_, &Editor::elementAppended, this, &DocumentView::onElementAppended);
  connect(&ed_, &Editor::structureChanged, this, [this](int focus) {
    clearCache();
    relayout();
    if (focus >= 0) { ed_.setCurrentPage(focus); if (!continuous_) relayout(false); scrollToPage(std::min(focus, ed_.pageCount() - 1)); }
    viewport()->update();
  });
  connect(&ed_, &Editor::appearanceChanged, this, [this] { clearCache(); relayout(); viewport()->update(); });
  connect(&ed_, &Editor::documentReplaced, this, [this] {
    clearCache();
    relayout(false);
    zoomFitWidth();
    setScrollDev(0, 0);
    viewport()->update();
  });
  connect(&ed_, &Editor::currentPageChanged, this, [this](int) { if (!continuous_) { relayout(false); setScrollDev(0, 0); viewport()->update(); } });
  connect(&ed_, &Editor::toolChanged, this, [this] { applyCursor(); });
  connect(&ed_, &Editor::hoverToolChanged, this, [this] { applyCursor(); });   // a pen button held over the page borrows its tool: the cursor shows it
  connect(&ed_, &Editor::styleChanged, this, [this] { applyCursor(); });   // the pen cursor shows the stroke width and colour
  connect(&ed_, &Editor::xppChanged, this, [this] { applyCursor(); });
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { applyCursor(); });   // the page under a resting pointer changes
  connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { applyCursor(); });
  connect(&ed_, &Editor::historyChanged, this, [this] { if (!dirtyKeys_.empty()) flushDirty(); });   // undo/redo show at once; a stroke just committed is drawn into the tiles a moment later (patches_)
  connect(&ed_, &Editor::focusRequested, this, [this](int page) { ensurePageVisible(page); });
  connect(&ed_, &Editor::selectionChanged, this, [this] { viewport()->update(); });
  applyCursor();
  relayout(false);
}

DocumentView::~DocumentView() {
  cancelAll();
  pool_.waitForDone();   // workers post to this object: they must be gone before it is
  if (liveP_.isActive()) liveP_.end();
  if (ed_.view() == this) ed_.setView(nullptr);
}

// ---- layout --------------------------------------------------------------------------------------------------------------------------------
QPoint DocumentView::scrollDev() const {
  const double d = dpr();
  const int x = int(std::lround(horizontalScrollBar()->value() * d)), y = int(std::lround(verticalScrollBar()->value() * d));
  return QPoint(std::clamp(x, 0, std::max(0, sceneDev_.width() - int(std::lround(viewport()->width() * d)))),
                std::clamp(y, 0, std::max(0, sceneDev_.height() - int(std::lround(viewport()->height() * d)))));
}

QRect DocumentView::viewportDev() const {
  const double d = dpr();
  return QRect(0, 0, int(std::lround(viewport()->width() * d)), int(std::lround(viewport()->height() * d)));
}

void DocumentView::relayout(bool keepScroll) {
  if (inRelayout_) { relayoutAgain_ = true; return; }   // a scroll bar appearing resizes the viewport from inside updateScrollBars: lay out again with the new size
  inRelayout_ = true;
  for (int pass = 0; pass < 3; pass++) {
    relayoutAgain_ = false;
    layoutOnce(keepScroll);
    if (!relayoutAgain_) break;
    keepScroll = true;
  }
  inRelayout_ = false;
}

void DocumentView::layoutOnce(bool keepScroll) {
  const double d = dpr();
  const int margin = int(std::lround(kMargin * d)), gap = int(std::lround(kGap * d));
  const auto& pages = ed_.doc().pages;
  boxes_.assign(pages.size(), Box{});
  const int cur = std::clamp(ed_.currentPage(), 0, std::max(0, int(pages.size()) - 1));
  const int exL = int(std::lround(extra_.left() * d)), exR = int(std::lround(extra_.right() * d)), exT = int(std::lround(extra_.top() * d)), exB = int(std::lround(extra_.bottom() * d));
  int maxW = 0, y = margin + exT;
  // Rows of one page, or of two facing pages (paired view: 1-2, 3-4, ... like an open book; a single shown page in the non-continuous view is the pair of the current one).
  const int facing = int(std::lround(8 * d));
  std::vector<int> rowWidth(pages.size(), 0);   // width of the row each page is in
  for (size_t first = 0; first < pages.size(); first += paired_ ? 2 : 1) {
    const size_t last = std::min(pages.size(), first + (paired_ ? 2 : 1));
    if (!continuous_ && (int(first) > cur || int(last) <= cur)) continue;
    int x = 0, rowH = 0;
    for (size_t i = first; i < last; i++) {
      Box& b = boxes_[i];
      b.shown = true;
      b.w = std::max(1, int(std::ceil(pages[i].width() * scale_ - 1e-6)));
      b.h = std::max(1, int(std::ceil(pages[i].height() * scale_ - 1e-6)));
      b.y = y;
      b.x = x;   // offset inside the row for now
      x += b.w + facing;
      rowH = std::max(rowH, b.h);
    }
    const int rowW = x - facing;
    for (size_t i = first; i < last; i++) rowWidth[i] = rowW;
    y += rowH + gap;
    maxW = std::max(maxW, rowW);
  }
  const QRect vp = viewportDev();
  const int sceneW = std::max(maxW + 2 * margin + exL + exR, vp.width());
  const int sceneH = std::max(y - gap + margin + exB, 0);
  for (size_t i = 0; i < boxes_.size(); i++) if (boxes_[i].shown) boxes_[i].x += (sceneW - exL - exR - rowWidth[i]) / 2 + exL;
  sceneDev_ = QSize(sceneW, sceneH);
  const QPoint keep = keepScroll ? scrollDev() : QPoint(0, 0);
  updateScrollBars();
  if (keepScroll) setScrollDev(keep.x(), keep.y());
}

void DocumentView::updateScrollBars() {
  if (qEnvironmentVariableIsSet("XL_DEBUG_SCROLL")) fprintf(stderr, "updateScrollBars scene %dx%d vp %dx%d range %d\n", sceneDev_.width(), sceneDev_.height(), viewportDev().width(), viewportDev().height(), horizontalScrollBar()->maximum());
  const double d = dpr();
  const QRect vp = viewportDev();
  horizontalScrollBar()->setRange(0, int(std::ceil(std::max(0, sceneDev_.width() - vp.width()) / d)));
  verticalScrollBar()->setRange(0, int(std::ceil(std::max(0, sceneDev_.height() - vp.height()) / d)));
  horizontalScrollBar()->setPageStep(viewport()->width());
  verticalScrollBar()->setPageStep(viewport()->height());
}

void DocumentView::setScrollDev(int x, int y) {
  const double d = dpr();
  horizontalScrollBar()->setValue(int(std::lround(x / d)));
  verticalScrollBar()->setValue(int(std::lround(y / d)));
}

void DocumentView::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  relayout();
  if (autoFit_ && !fitting_) zoomFitWidth();
  if (liveActive_) return;
  settleTimer_.start(kSettleMs);
}

void DocumentView::setContinuous(bool on) {
  if (on == continuous_) return;
  continuous_ = on;
  relayout(false);
  if (on) scrollToPage(ed_.currentPage());
  else setScrollDev(0, 0);
  clearCache();
  viewport()->update();
}

void DocumentView::setPairedPages(bool on) {
  if (on == paired_) return;
  paired_ = on;
  if (autoFit_) zoomFitWidth();
  relayout(false);
  scrollToPage(ed_.currentPage());
  clearCache();
  viewport()->update();
}

void DocumentView::setZoom(double z, const QPointF& anchorIn) {
  const double d = dpr();
  z = std::clamp(z, kMinZoom, kMaxZoom);
  if (!fitting_) autoFit_ = false;   // any zoom the user (or a command) picks ends the automatic fit
  const double newScale = std::llround(z * d * 10000) / 10000.0;   // snapped so a scale is identified by its key
  if (newScale == scale_ && std::abs(zoom_ - newScale / d) < 1e-12) return;
  const QPointF anchor = anchorIn.x() < 0 ? QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0) : anchorIn;
  const int page = pageAt(anchor, true);
  const QPointF pp = page >= 0 ? toPage(page, anchor) : QPointF();
  scale_ = newScale;
  zoom_ = newScale / d;
  relayout(false);
  if (page >= 0 && boxes_[size_t(page)].shown) {
    const Box& b = boxes_[size_t(page)];
    setScrollDev(int(std::lround(b.x + pp.x() * scale_ - anchor.x() * d)), int(std::lround(b.y + pp.y() * scale_ - anchor.y() * d)));
  }
  pending_.clear(); pendingSet_.clear();
  cancelAll();   // what workers are drawing is at the old scale
  noteInteraction();
  updateCurrentPage();
  viewport()->update();
  applyCursor();   // the pen cursor shows the stroke width at this zoom
  emit zoomChanged(zoom_);
}

void DocumentView::zoomFitWidth() {
  double w = 0;   // the widest page fills the area (a narrower one is centred), as the Electron app does; in the paired view, the widest row of two
  const auto& pgs = ed_.doc().pages;
  const size_t step = paired_ ? 2 : 1;
  for (size_t i = 0; i < pgs.size(); i += step) {
    double row = 0;
    for (size_t j = i; j < std::min(pgs.size(), i + step); j++) row += pgs[j].width() + (j > i ? 8 / std::max(zoom_, 1e-6) : 0);
    w = std::max(w, row);
  }
  const int bar = verticalScrollBar()->isVisible() ? 0 : style()->pixelMetric(QStyle::PM_ScrollBarExtent);   // the bar will appear once the page is taller than the view
  const int avail = viewport()->width() - bar - 2 * kMargin - 2;   // 2 px of slack: rounding the page's pixel width up must not bring a horizontal bar
  if (avail > 20 && w > 1) { fitting_ = true; setZoom(avail / w); fitting_ = false; }
}

void DocumentView::zoomFitPage() {
  const int p = std::clamp(ed_.currentPage(), 0, std::max(0, ed_.pageCount() - 1));
  const Page& pg = ed_.doc().pages[size_t(p)];
  double pw = pg.width(), ph = pg.height();
  if (paired_) {   // the facing pair of the current page fits
    const size_t f = size_t(p) / 2 * 2;
    pw = 0; ph = 0;
    for (size_t j = f; j < std::min(ed_.doc().pages.size(), f + 2); j++) { pw += ed_.doc().pages[j].width(); ph = std::max(ph, ed_.doc().pages[j].height()); }
  }
  const double zw = (viewport()->width() - 2.0 * kMargin) / pw, zh = (viewport()->height() - 2.0 * kMargin) / ph;
  if (zw > 0 && zh > 0) setZoom(std::min(zw, zh));
}

void DocumentView::scrollByPixels(double dx, double dy) {
  horizontalScrollBar()->setValue(horizontalScrollBar()->value() + int(std::lround(dx)));
  verticalScrollBar()->setValue(verticalScrollBar()->value() + int(std::lround(dy)));
}

void DocumentView::scrollToPage(int page) {
  if (page < 0 || page >= ed_.pageCount()) return;
  if (!continuous_ && ed_.currentPage() != page) ed_.setCurrentPage(page);
  if (!boxes_[size_t(page)].shown) { ed_.setCurrentPage(page); relayout(false); }
  const Box& b = boxes_[size_t(page)];
  const double d = dpr();
  setScrollDev(scrollDev().x(), b.y - int(std::lround(kMargin * d)));
  if (continuous_) ed_.setCurrentPage(page);
}

void DocumentView::ensurePageVisible(int page) {
  if (page < 0 || page >= int(boxes_.size())) return;
  const auto vis = visiblePages();
  if (std::find(vis.begin(), vis.end(), page) == vis.end()) scrollToPage(page);
}

int DocumentView::pageAt(const QPointF& viewPos, bool nearest) const {
  const double d = dpr();
  const QPoint sc = scrollDev();
  const double x = viewPos.x() * d + sc.x(), y = viewPos.y() * d + sc.y();
  int best = -1;
  double bestDist = 1e18;
  for (size_t i = 0; i < boxes_.size(); i++) {
    const Box& b = boxes_[i];
    if (!b.shown) continue;
    const double dx = x < b.x ? b.x - x : (x > b.x + b.w ? x - b.x - b.w : 0), dy = y < b.y ? b.y - y : (y > b.y + b.h ? y - b.y - b.h : 0);
    if (dx == 0 && dy == 0) return int(i);
    const double dist = dx * dx + dy * dy;
    if (dist < bestDist) { bestDist = dist; best = int(i); }
  }
  return nearest ? best : -1;
}

QPointF DocumentView::toPage(int page, const QPointF& viewPos) const {
  const double d = dpr();
  const QPoint sc = scrollDev();
  const Box& b = boxes_[size_t(page)];
  return QPointF((viewPos.x() * d + sc.x() - b.x) / scale_, (viewPos.y() * d + sc.y() - b.y) / scale_);
}

QPointF DocumentView::toView(int page, const QPointF& pagePos) const {
  const double d = dpr();
  const QPoint sc = scrollDev();
  const Box& b = boxes_[size_t(page)];
  return QPointF((b.x + pagePos.x() * scale_ - sc.x()) / d, (b.y + pagePos.y() * scale_ - sc.y()) / d);
}

QRectF DocumentView::pageRectOnScreen(int page) const {
  const Box& b = boxes_[size_t(page)];
  const double d = dpr();
  const QPoint sc = scrollDev();
  return QRectF((b.x - sc.x()) / d, (b.y - sc.y()) / d, b.w / d, b.h / d);
}

std::vector<int> DocumentView::visiblePages() const {
  std::vector<int> v;
  const QRect vp = viewportDev().translated(scrollDev());
  for (size_t i = 0; i < boxes_.size(); i++) {
    const Box& b = boxes_[i];
    if (b.shown && QRect(b.x, b.y, b.w, b.h).intersects(vp)) v.push_back(int(i));
  }
  return v;
}

void DocumentView::updateCurrentPage() {
  if (!continuous_ || ed_.gestureActive()) return;
  const int p = pageAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0), true);
  if (p >= 0 && p != ed_.currentPage()) ed_.setCurrentPage(p);
}

void DocumentView::scrollContentsBy(int, int) {
  noteInteraction();
  cancelFar();
  updateCurrentPage();
  viewport()->update();
}

QRect DocumentView::pageRectToDev(int page, const QRectF& r) const {
  const Box& b = boxes_[size_t(page)];
  const QPoint sc = scrollDev();
  return QRectF(b.x - sc.x() + r.x() * scale_, b.y - sc.y() + r.y() * scale_, r.width() * scale_, r.height() * scale_).toAlignedRect();
}

void DocumentView::updateDevRect(const QRect& dev) {
  const double d = dpr();
  viewport()->update(QRect(int(std::floor(dev.x() / d)) - 1, int(std::floor(dev.y() / d)) - 1, int(std::ceil(dev.width() / d)) + 3, int(std::ceil(dev.height() / d)) + 3));
}

// ---- tiles ---------------------------------------------------------------------------------------------------------------------------------
namespace {
constexpr size_t kPartialMax = 300;    // a repaint of part of a tile with more elements than this goes to a worker as a whole tile

/** Draws `region` (tile pixels; null = all of it) of the tile `k`: paper, then `els` (z order) on top. Used by the GUI thread and by workers: it touches
 *  nothing but its arguments. `shell` only supplies size and background of the page. */
void paintTile(QPainter& qp, const TileKey& k, const QSize& px, const Page& shell, const RenderOptions& base, const std::vector<const Element*>& els, bool lite,
               const QRect& region, const std::atomic<bool>* cancel) {
  const double s = k.scaleKey / 10000.0;
  const QRect reg = region.isNull() ? QRect(QPoint(0, 0), px) : region.intersected(QRect(QPoint(0, 0), px));
  qp.setRenderHint(QPainter::Antialiasing, false);
  if (!region.isNull()) qp.setClipRect(reg);   // device pixels, before the page transform
  qp.fillRect(reg, Qt::white);
  qp.setRenderHint(QPainter::Antialiasing, !lite);
  qp.setRenderHint(QPainter::TextAntialiasing, true);
  qp.setRenderHint(QPainter::SmoothPixmapTransform, !lite);
  qp.scale(s, s);
  qp.translate(-k.tx * kTile / s, -k.ty * kTile / s);
  RenderOptions o = base;
  o.clip = QRectF((k.tx * kTile + reg.x()) / s, (k.ty * kTile + reg.y()) / s, reg.width() / s, reg.height() / s);
  o.scale = s;
  o.cullPieces = true;
  o.lite = lite;
  renderBackground(qp, shell, o);
  renderElementSeq(qp, o, els, cancel);
}

/** What a worker needs to draw one tile: copies only, so the document can change while it runs. */
struct TileJob {
  TileKey key;
  QSize px;
  Page shell;                        // size and background of the page, no layers
  RenderOptions base;
  std::vector<Element> els;          // the elements that can touch the tile, z order
  bool lite = false;
  QRect region;                      // null = the whole tile, else only this part (tile pixels) and the result is just that part
  uint64_t epoch = 0, seq = 0;
  std::shared_ptr<std::atomic<bool>> cancel;
};
}  // namespace

void DocumentView::elementsFor(int page, const QRectF& rect, std::vector<const Element*>& out) {
  out.clear();
  const PageIndex& ix = ed_.index(page);
  ix.query(rect, ix.renderPad(), hits_);
  const std::vector<int> hidden = ed_.hiddenLayers(page);
  const Page& pg = ed_.doc().pages[size_t(page)];
  out.reserve(hits_.size());
  for (const PageIndex::Hit& h : hits_) {
    if (!hidden.empty() && std::find(hidden.begin(), hidden.end(), h.first) != hidden.end()) continue;
    out.push_back(&pg.layers[size_t(h.first)].elements[size_t(h.second)]);
  }
}

QPixmap DocumentView::renderTile(const TileKey& k, bool lite) {
  const double t0 = nowMs();
  const double s = k.scaleKey / 10000.0;
  const Box& b = boxes_[size_t(k.page)];
  const int w = std::min(kTile, b.w - k.tx * kTile), h = std::min(kTile, b.h - k.ty * kTile);
  QPixmap pm(std::max(1, w), std::max(1, h));
  pm.fill(Qt::white);
  const Page& page = ed_.doc().pages[size_t(k.page)];
  {
    QPainter qp(&pm);
    if (indexed_) {
      std::vector<const Element*> els;
      elementsFor(k.page, tilePageRect(k), els);
      paintTile(qp, k, pm.size(), page, renderBase_, els, lite, QRect(), nullptr);
    } else {   // the earlier path: every element of the page is tested for every tile
      qp.setRenderHint(QPainter::Antialiasing, !lite);
      qp.setRenderHint(QPainter::TextAntialiasing, true);
      qp.setRenderHint(QPainter::SmoothPixmapTransform, !lite);
      qp.scale(s, s);
      qp.translate(-k.tx * kTile / s, -k.ty * kTile / s);
      RenderOptions o = renderBase_;
      o.clip = QRectF(k.tx * kTile / s, k.ty * kTile / s, w / s, h / s);
      o.scale = s;
      o.lite = lite;
      renderBackground(qp, page, o);
      renderLayers(qp, page, o, ed_.hiddenLayers(k.page));
    }
  }
  stats_.tilesRendered++;
  if (lite) stats_.tilesLite++;
  stats_.renderMs += nowMs() - t0;
  return pm;
}

void DocumentView::refreshTile(const TileKey& k, const QRect& regionIn, bool lite) {
  Tile* t = cache_.find(k);
  const Box& b = boxes_[size_t(k.page)];
  const int w = std::min(kTile, b.w - k.tx * kTile), h = std::min(kTile, b.h - k.ty * kTile);
  const QRect region = regionIn.isNull() ? QRect() : regionIn.intersected(QRect(0, 0, w, h));
  if (!t || region.isNull() || lite || !indexed_) { cache_.put(k, renderTile(k, lite), lite); return; }
  const double t0 = nowMs();
  const double s = k.scaleKey / 10000.0;
  std::vector<const Element*> els;
  elementsFor(k.page, QRectF((k.tx * kTile + region.x()) / s, (k.ty * kTile + region.y()) / s, region.width() / s, region.height() / s), els);
  {
    QPainter qp(&t->pm);
    paintTile(qp, k, t->pm.size(), ed_.doc().pages[size_t(k.page)], renderBase_, els, false, region, nullptr);
  }
  t->dirty = false;
  t->dirtyDev = QRect();
  t->lite = false;
  stats_.tilesPartial++;
  stats_.renderMs += nowMs() - t0;
}

bool DocumentView::startAsync(const TileKey& k, const std::vector<const Element*>& els, bool lite, const QRect& region) {
  if (inflight_.size() >= size_t(maxInflight_)) return false;
  const Box& b = boxes_[size_t(k.page)];
  auto job = std::make_shared<TileJob>();
  job->key = k;
  job->px = QSize(std::max(1, std::min(kTile, b.w - k.tx * kTile)), std::max(1, std::min(kTile, b.h - k.ty * kTile)));
  const Page& page = ed_.doc().pages[size_t(k.page)];
  job->shell.attrs = page.attrs;
  job->shell.background = page.background;
  job->base = renderBase_;
  job->els.reserve(els.size());
  for (const Element* e : els) job->els.push_back(*e);
  job->lite = lite;
  job->region = region;
  job->epoch = epoch_;
  job->seq = seq_;
  job->cancel = std::make_shared<std::atomic<bool>>(false);
  inflight_[k] = job->cancel;
  pool_.start([this, job] {
    if (job->cancel->load()) return;
    thread_local bool lowered = false;
    if (!lowered) { QThread::currentThread()->setPriority(QThread::LowPriority); lowered = true; }   // the pen's thread comes first
    const double t0 = nowMs();
    QImage img(job->region.isNull() ? job->px : job->region.size(), QImage::Format_RGB32);
    img.fill(Qt::white);
    std::vector<const Element*> ptrs;
    ptrs.reserve(job->els.size());
    for (const Element& e : job->els) ptrs.push_back(&e);
    {
      QPainter qp(&img);
      if (!job->region.isNull()) qp.translate(-job->region.topLeft());   // the image holds just the region
      paintTile(qp, job->key, job->px, job->shell, job->base, ptrs, job->lite, job->region, job->cancel.get());
    }
    if (job->cancel->load()) return;   // the result would be thrown away
    TileDone d;
    d.key = job->key; d.img = std::move(img); d.lite = job->lite; d.region = job->region; d.epoch = job->epoch; d.seq = job->seq; d.ms = nowMs() - t0; d.token = job->cancel;
    {
      std::lock_guard<std::mutex> lock(doneMx_);
      done_.push_back(std::move(d));
    }
    QMetaObject::invokeMethod(this, [this] { collectDone(); }, Qt::QueuedConnection);
  });
  return true;
}

void DocumentView::collectDone() {
  flushPatches();   // a result already holds every committed stroke (older ones are stale and dropped): a patch still waiting would draw its stroke on it a second time
  std::vector<TileDone> batch;
  {
    std::lock_guard<std::mutex> lock(doneMx_);
    batch.swap(done_);
  }
  for (TileDone& d : batch) {
    const auto it = inflight_.find(d.key);
    if (it == inflight_.end() || it->second != d.token) { stats_.tilesDropped++; continue; }   // cancelled meanwhile (or a newer job owns the key)
    inflight_.erase(it);
    const TileKey& k = d.key;
    if (d.epoch != epoch_ || k.scaleKey != scaleKey() || k.page >= int(boxes_.size()) || !boxes_[size_t(k.page)].shown) { stats_.tilesDropped++; continue; }
    if (staleSince(k, d.seq)) {   // something changed there while it was being drawn: the tile is dirty again (or missing), draw it again
      stats_.tilesDropped++;
      if (Tile* t = cache_.find(k); t && t->dirty) { dirtyKeys_.insert(k); dirtyFlushQueued_ = true; if (!flushTimer_.isActive()) flushTimer_.start(8); }
      else request(k);
      continue;
    }
    const Box& b = boxes_[size_t(k.page)];
    if (!d.region.isNull()) {   // a part of a tile that is already there
      Tile* t = cache_.find(k);
      if (!t) { request(k); continue; }
      {
        QPainter qp(&t->pm);
        qp.setCompositionMode(QPainter::CompositionMode_Source);
        qp.drawImage(d.region.topLeft(), d.img);
      }
      t->dirty = false;
      t->dirtyDev = QRect();
      t->lite = false;
      stats_.tilesPartial++;
      stats_.tilesAsync++;
      stats_.asyncMs += d.ms;
      const QPoint sc = scrollDev();
      updateDevRect(QRect(b.x - sc.x() + k.tx * kTile + d.region.x(), b.y - sc.y() + k.ty * kTile + d.region.y(), d.region.width(), d.region.height()));
      continue;
    }
    cache_.put(k, QPixmap::fromImage(std::move(d.img)), d.lite);
    if (d.lite && !interacting_) request(k);   // drawn rough while the view was moving, which has stopped meanwhile (settle() skipped it: a worker was on it): draw it sharp
    stats_.tilesRendered++;
    stats_.tilesAsync++;
    if (d.lite) stats_.tilesLite++;
    stats_.asyncMs += d.ms;
    const QPoint sc = scrollDev();
    const int w = std::min(kTile, b.w - k.tx * kTile), h = std::min(kTile, b.h - k.ty * kTile);
    updateDevRect(QRect(b.x - sc.x() + k.tx * kTile, b.y - sc.y() + k.ty * kTile, w, h));
  }
  if (!pending_.empty()) jobTimer_.start(0);
  else if (!interacting_ && inflight_.empty()) queuePrefetch();
}

void DocumentView::setWorkerThreads(int n) {
  n = std::clamp(n, 1, 16);
  pool_.setMaxThreadCount(n);
  maxInflight_ = n;
}

void DocumentView::clearCache() {
  cache_.clear();
  pending_.clear();
  pendingSet_.clear();
  dirtyKeys_.clear();
  cancelAll();
  patches_.clear();
  liveHold_ = false;
  holdRect_ = QRect();
  viewport()->update();
}

void DocumentView::cancelAll() {
  ++epoch_;
  for (auto& kv : inflight_) kv.second->store(true);
  inflight_.clear();
  std::lock_guard<std::mutex> lock(doneMx_);
  done_.clear();
}

void DocumentView::cancelFar() {
  if (inflight_.empty()) return;
  const QRect keep = viewportDev().translated(scrollDev()).adjusted(-2 * kTile, -2 * kTile, 2 * kTile, 2 * kTile);
  for (auto it = inflight_.begin(); it != inflight_.end();) {
    const TileKey& k = it->first;
    bool near = false;
    if (k.page < int(boxes_.size()) && boxes_[size_t(k.page)].shown) {
      const Box& b = boxes_[size_t(k.page)];
      near = keep.intersects(QRect(b.x + k.tx * kTile, b.y + k.ty * kTile, kTile, kTile));
    }
    if (near) { ++it; continue; }
    it->second->store(true);
    it = inflight_.erase(it);
  }
  if (!pending_.empty() && !jobTimer_.isActive()) jobTimer_.start(0);   // jobs that waited for a free worker: a cancelled one posts no result to restart the queue
}

void DocumentView::noteChange(int page, const QRectF& rect) {
  changeLog_.push_back({++seq_, page, rect});
  if (changeLog_.size() > 512) changeLog_.pop_front();
}

bool DocumentView::staleSince(const TileKey& k, uint64_t seq) const {
  if (seq_ == seq) return false;
  if (changeLog_.empty() || changeLog_.front().seq > seq + 1) return true;   // the log no longer reaches back that far: assume the worst
  const QRectF r = tilePageRect(k);
  for (const Change& c : changeLog_)
    if (c.seq > seq && c.page == k.page && (c.rect.isNull() || c.rect.intersects(r))) return true;
  return false;
}

void DocumentView::tilesInRect(int page, const QRect& r, std::vector<TileKey>& out) const {
  const Box& b = boxes_[size_t(page)];
  const QRect rr = r.intersected(QRect(0, 0, b.w, b.h));
  if (rr.isEmpty()) return;
  const int sk = scaleKey();
  for (int ty = rr.top() / kTile; ty <= (rr.bottom()) / kTile; ty++)
    for (int tx = rr.left() / kTile; tx <= (rr.right()) / kTile; tx++) out.push_back({page, sk, tx, ty});
}

void DocumentView::request(const TileKey& k, bool prefetch) {
  if (k.scaleKey != scaleKey()) return;
  if (inflight_.count(k)) return;   // a worker is already on it
  if (!pendingSet_.insert(k).second) {
    if (!prefetch) for (Job& j : pending_) if (j.key == k) { j.prefetch = false; break; }
    return;
  }
  pending_.push_back({k, prefetch});
  if (!jobTimer_.isActive()) jobTimer_.start(0);
}

void DocumentView::processJobs() {
  if (ed_.gestureActive()) { jobTimer_.start(30); return; }   // never steal time from the pen: the stroke in progress comes first
  flushPatches();   // a stroke just committed lands in the tiles before any tile is drawn from the document, which already holds it (drawn on top again it would show doubled edges)
  QElapsedTimer slice;
  slice.start();
  const double d = dpr();
  const QPoint sc = scrollDev();
  const QPoint centre = QPoint(sc.x() + int(viewport()->width() * d / 2), sc.y() + int(viewport()->height() * d / 2));
  int done = 0;
  bool busy = false;   // every worker is occupied: the rest waits for collectDone
  std::vector<const Element*> els;
  while (!pending_.empty()) {
    size_t best = 0;
    double bestScore = 1e30;
    for (size_t i = 0; i < pending_.size(); i++) {
      const TileKey& k = pending_[i].key;
      if (k.page >= int(boxes_.size())) { bestScore = -1; best = i; break; }
      const Box& b = boxes_[size_t(k.page)];
      const double cx = b.x + k.tx * kTile + kTile / 2.0, cy = b.y + k.ty * kTile + kTile / 2.0;
      const double score = std::hypot(cx - centre.x(), cy - centre.y()) + (pending_[i].prefetch ? 1e7 : 0);
      if (score < bestScore) { bestScore = score; best = i; }
    }
    const Job job = pending_[best];
    const bool valid = job.key.page < int(boxes_.size()) && boxes_[size_t(job.key.page)].shown && job.key.scaleKey == scaleKey();
    const Box* bx = valid ? &boxes_[size_t(job.key.page)] : nullptr;
    const Tile* old = valid ? cache_.peek(job.key) : nullptr;
    const bool skip = !valid || job.key.tx * kTile >= bx->w || job.key.ty * kTile >= bx->h || (old && !old->lite && !old->dirty) || inflight_.count(job.key);
    if (!skip && indexed_) {
      elementsFor(job.key.page, tilePageRect(job.key), els);
      if (int(els.size()) >= asyncMin_) {
        if (!startAsync(job.key, els, interacting_)) { busy = true; break; }
        pending_.erase(pending_.begin() + long(best));
        pendingSet_.erase(job.key);
        if (slice.elapsed() >= kJobBudgetMs) break;
        continue;
      }
    }
    pending_.erase(pending_.begin() + long(best));
    pendingSet_.erase(job.key);
    if (skip) continue;
    refreshTile(job.key, QRect(), interacting_);
    const int w = std::min(kTile, bx->w - job.key.tx * kTile), h = std::min(kTile, bx->h - job.key.ty * kTile);
    updateDevRect(QRect(bx->x - sc.x() + job.key.tx * kTile, bx->y - sc.y() + job.key.ty * kTile, w, h));
    done++;
    if (slice.elapsed() >= kJobBudgetMs) break;
  }
  if (busy) return;   // collectDone restarts the queue when a worker is free
  if (!pending_.empty()) jobTimer_.start(0);
  else if (!interacting_ && done && inflight_.empty()) queuePrefetch();
}

void DocumentView::noteInteraction() {
  interacting_ = true;
  settleTimer_.start(kSettleMs);
}

void DocumentView::settle() {
  if (ed_.gestureActive()) { settleTimer_.start(kSettleMs); return; }
  interacting_ = false;
  // everything on screen that was drawn rough is drawn again sharp
  std::vector<TileKey> keys;
  for (int p : visiblePages()) {
    const QRect vis = viewportDev().translated(scrollDev()).translated(-boxes_[size_t(p)].x, -boxes_[size_t(p)].y);
    tilesInRect(p, vis, keys);
  }
  for (const TileKey& k : keys) {
    const Tile* t = cache_.peek(k);
    if (!t || t->lite) request(k);
  }
  if (keys.empty() || pending_.empty()) queuePrefetch();
}

void DocumentView::queuePrefetch() {
  // one ring of tiles around the screen, so the next scroll step finds them ready
  int queued = 0;
  for (size_t p = 0; p < boxes_.size() && queued < 48; p++) {
    const Box& b = boxes_[p];
    if (!b.shown) continue;
    const QRect ring = viewportDev().translated(scrollDev()).adjusted(-kTile, -kTile, kTile, kTile).translated(-b.x, -b.y);
    if (!ring.intersects(QRect(0, 0, b.w, b.h))) continue;
    std::vector<TileKey> keys;
    tilesInRect(int(p), ring, keys);
    for (const TileKey& k : keys)
      if (!cache_.peek(k) && queued < 48) { request(k, true); queued++; }
  }
}

void DocumentView::drawStale(QPainter& p, const TileKey& k, const QPoint& origin, const QRect& target) {
  // look for the same page at the scale closest to the current one, and stretch what it has over the hole
  int bestScale = 0;
  double bestDiff = 1e9;
  cache_.forEach([&](const TileKey& o, Tile&) {
    if (o.page != k.page || o.scaleKey == k.scaleKey) return;
    const double diff = std::abs(std::log(double(o.scaleKey) / double(k.scaleKey)));
    if (diff < bestDiff) { bestDiff = diff; bestScale = o.scaleKey; }
  });
  if (!bestScale) return;
  const double s2 = bestScale / 10000.0, s = k.scaleKey / 10000.0;
  const QRectF pageRect(k.tx * kTile / s, k.ty * kTile / s, kTile / s, kTile / s);
  p.save();
  p.setClipRect(target);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  for (int ty = int(std::floor(pageRect.top() * s2 / kTile)); ty <= int(std::floor((pageRect.bottom() * s2 - 1e-6) / kTile)); ty++)
    for (int tx = int(std::floor(pageRect.left() * s2 / kTile)); tx <= int(std::floor((pageRect.right() * s2 - 1e-6) / kTile)); tx++) {
      Tile* t = cache_.find({k.page, bestScale, tx, ty});
      if (!t) continue;
      const QRectF dst(origin.x() + tx * kTile / s2 * s, origin.y() + ty * kTile / s2 * s, t->pm.width() / s2 * s, t->pm.height() / s2 * s);
      p.drawPixmap(dst, t->pm, QRectF(QPointF(0, 0), QSizeF(t->pm.size())));
    }
  p.restore();
}

void DocumentView::drawTileOrStale(QPainter& p, const TileKey& k, const QPoint& origin) {
  const Box& b = boxes_[size_t(k.page)];
  const QRect target(origin.x() + k.tx * kTile, origin.y() + k.ty * kTile, std::min(kTile, b.w - k.tx * kTile), std::min(kTile, b.h - k.ty * kTile));
  if (Tile* t = cache_.find(k)) {
    p.drawPixmap(target.topLeft(), t->pm);
    if (t->lite && !interacting_) request(k);
    return;
  }
  p.fillRect(target, Qt::white);
  drawStale(p, k, origin, target);
  request(k);
}

int DocumentView::renderVisibleNow() {
  flushDirty();
  int n = 0;
  for (int pg : visiblePages()) {
    const Box& b = boxes_[size_t(pg)];
    std::vector<TileKey> keys;
    tilesInRect(pg, viewportDev().translated(scrollDev()).translated(-b.x, -b.y), keys);
    for (const TileKey& k : keys) {
      const Tile* t = cache_.peek(k);
      if (t && !t->lite && !t->dirty) continue;
      if (const auto it = inflight_.find(k); it != inflight_.end()) { it->second->store(true); inflight_.erase(it); }   // drawn here and now instead
      cache_.put(k, renderTile(k, false), false);
      n++;
    }
  }
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](const Job& j) { return !j.prefetch; }), pending_.end());
  pendingSet_.clear();
  for (const Job& j : pending_) pendingSet_.insert(j.key);
  viewport()->update();
  return n;
}

void DocumentView::invalidate(int page, const QRectF& rect) {
  if (page < 0 || page >= int(boxes_.size())) return;
  flushPatches();   // earlier strokes land in the tiles first, so that what follows sees them
  noteChange(page, rect);
  const int sk = scaleKey();
  auto hit = cache_.keysWhere([&](const TileKey& k, const Tile&) { return k.page == page && (rect.isNull() || tilePageRect(k).intersects(rect)); });
  for (const TileKey& k : hit) {
    if (k.scaleKey != sk) { cache_.erase(k); continue; }
    if (Tile* t = cache_.find(k)) {
      // the changed part in tile pixels, a pixel wider on every side for the antialiasing; a null rect (everything) = the whole tile
      QRect part;
      if (!rect.isNull()) {
        const double s = k.scaleKey / 10000.0;
        part = QRectF(rect.x() * s - k.tx * kTile, rect.y() * s - k.ty * kTile, rect.width() * s, rect.height() * s).toAlignedRect().adjusted(-1, -1, 1, 1).intersected(QRect(0, 0, t->pm.width(), t->pm.height()));
      }
      if (!t->dirty) { t->dirty = true; t->dirtyDev = part; }
      else if (!t->dirtyDev.isNull()) t->dirtyDev = part.isNull() ? QRect() : t->dirtyDev.united(part);
      dirtyKeys_.insert(k);
    }
  }
  if (!dirtyKeys_.empty()) { dirtyFlushQueued_ = true; if (!flushTimer_.isActive()) flushTimer_.start(8); }
  if (boxes_[size_t(page)].shown) updateDevRect(pageRectToDev(page, rect.isNull() ? QRectF(0, 0, ed_.doc().pages[size_t(page)].width(), ed_.doc().pages[size_t(page)].height()) : rect));
}

void DocumentView::flushDirty() {
  flushPatches();
  flushTimer_.stop();
  dirtyFlushQueued_ = false;
  if (dirtyKeys_.empty()) return;
  const QPoint sc = scrollDev();
  const QRect vp = viewportDev().translated(sc);
  const auto keys = std::move(dirtyKeys_);
  dirtyKeys_.clear();
  std::vector<const Element*> els;
  for (const TileKey& k : keys) {
    const Tile* t = cache_.peek(k);
    if (!t || !t->dirty) continue;
    if (k.page >= int(boxes_.size()) || !boxes_[size_t(k.page)].shown || k.scaleKey != scaleKey()) { cache_.erase(k); continue; }
    const Box& b = boxes_[size_t(k.page)];
    const int w = std::min(kTile, b.w - k.tx * kTile), h = std::min(kTile, b.h - k.ty * kTile);
    const QRect r(b.x + k.tx * kTile, b.y + k.ty * kTile, w, h);
    if (!r.intersects(vp)) { cache_.erase(k); continue; }   // off screen: rendered when it comes into view
    // Only the changed rectangle is drawn again (with only the elements the index finds there) unless that is most of the tile, the tile is rough, or a
    // lot of elements would have to be drawn: then the whole tile goes to a worker and the old picture stays up until it is ready.
    QRect region = t->lite || !indexed_ ? QRect() : t->dirtyDev;
    if (!region.isNull() && region.width() * region.height() * 2 > w * h) region = QRect();
    if (indexed_) {
      const double s = k.scaleKey / 10000.0;
      const QRectF pr = region.isNull() ? tilePageRect(k) : QRectF((k.tx * kTile + region.x()) / s, (k.ty * kTile + region.y()) / s, region.width() / s, region.height() / s);
      elementsFor(k.page, pr, els);
      const bool big = region.isNull() ? int(els.size()) >= asyncMin_ : els.size() > kPartialMax;
      if (big) {
        if (!region.isNull() && startAsync(k, els, false, region)) continue;   // the old picture stays up until the part is ready
        request(k);
        continue;
      }
    }
    refreshTile(k, region, false);
    updateDevRect(r.translated(-sc));
  }
}

void DocumentView::onElementAppended(int page, int layer, int index, const QRectF& rect) {
  if (page < 0 || page >= int(boxes_.size())) return;
  const Page& pg = ed_.doc().pages[size_t(page)];
  const bool topmost = layer == int(pg.layers.size()) - 1 && ed_.layerVisible(page, layer);
  if (!topmost) { invalidate(page, rect); return; }
  // The new element lies on top of everything: draw just it into the finished tiles it touches (no re-render of the page). While the live layer
  // still shows it, the drawing is left for the next turn of the event loop so that lifting the pen costs nothing visible.
  flushPatches();
  noteChange(page, rect);
  patches_.push_back({page, rect, pg.layers[size_t(layer)].elements[size_t(index)]});
  if (liveActive_) patchTimer_.start(0);
  else flushPatches();
}

void DocumentView::applyPatch(const Patch& pt) {
  const int page = pt.page;
  if (page < 0 || page >= int(boxes_.size())) return;
  const int sk = scaleKey();
  const auto hit = cache_.keysWhere([&](const TileKey& k, const Tile&) { return k.page == page && tilePageRect(k).intersects(pt.rect); });
  for (const TileKey& k : hit) {
    Tile* t = cache_.find(k);
    if (!t) continue;
    if (k.scaleKey != sk || t->dirty || t->lite) { cache_.erase(k); dirtyKeys_.erase(k); continue; }
    const double s = k.scaleKey / 10000.0;
    QPainter qp(&t->pm);
    qp.setRenderHint(QPainter::Antialiasing, true);
    qp.scale(s, s);
    qp.translate(-k.tx * kTile / s, -k.ty * kTile / s);
    RenderOptions o = renderBase_;
    o.clip = pt.rect.intersected(tilePageRect(k));
    o.scale = s;
    renderElement(qp, pt.el, o);
    stats_.tilesPatched++;
  }
  if (boxes_[size_t(page)].shown) updateDevRect(pageRectToDev(page, pt.rect));
}

void DocumentView::releaseHold() {
  if (!liveHold_) return;
  liveHold_ = false;
  if (!holdRect_.isNull()) updateDevRect(holdRect_);
  holdRect_ = QRect();
}

void DocumentView::flushPatches() {
  patchTimer_.stop();
  while (!patches_.empty()) {
    const Patch pt = std::move(patches_.front());
    patches_.pop_front();
    applyPatch(pt);
  }
  releaseHold();
}

void DocumentView::processPatches() {
  QElapsedTimer sl;
  sl.start();
  while (!patches_.empty()) {
    const Patch pt = std::move(patches_.front());
    patches_.pop_front();
    applyPatch(pt);
    if (sl.elapsed() >= 4) break;
  }
  if (patches_.empty()) releaseHold();
  else patchTimer_.start(0);
}

QImage DocumentView::snapshot() {
  renderVisibleNow();
  return viewport()->grab().toImage();
}

// ---- painting -------------------------------------------------------------------------------------------------------------------------------
void DocumentView::paintEvent(QPaintEvent* ev) {
  const double t0 = nowMs();
  stats_.paints++;
  const double d = dpr();
  QPainter p(viewport());
  const QRect evLogical = ev->rect();
  if (desk_.gradTop.isValid()) {   // the neon sky: an ellipse of light at the top centre, as the CSS radial gradient
    QRadialGradient g(QPointF(width() * 0.5, -height() * 0.1), std::max(width(), height()) * 0.9);
    g.setColorAt(0, desk_.gradTop); g.setColorAt(0.55, desk_.gradMid); g.setColorAt(1, desk_.gradBottom);
    p.fillRect(evLogical, QBrush(g));
  } else p.fillRect(evLogical, desk_.desk);
  const QRect evDev(int(std::floor(evLogical.x() * d)), int(std::floor(evLogical.y() * d)), int(std::ceil(evLogical.width() * d)) + 1, int(std::ceil(evLogical.height() * d)) + 1);
  const QPoint sc = scrollDev();
  p.save();
  p.scale(1 / d, 1 / d);   // from here on: device pixels of the viewport
  std::vector<TileKey> keys;
  const double shadow = std::max(2.0, 3 * d);
  for (size_t i = 0; i < boxes_.size(); i++) {
    const Box& b = boxes_[i];
    if (!b.shown) continue;
    const QRect pageDev(b.x - sc.x(), b.y - sc.y(), b.w, b.h);
    if (!pageDev.adjusted(-12, -12, int(shadow) + 12, int(shadow) + 12).intersects(evDev)) continue;
    if (desk_.frame.isValid()) {
      if (desk_.glow.isValid())
        for (int g = 7; g >= 1; g--) {   // halo: rings that fade out
          QColor c = desk_.glow; c.setAlphaF(0.07 * (8 - g) / 7.0 + 0.01);
          p.setPen(QPen(c, 2)); p.setBrush(Qt::NoBrush);
          p.drawRect(pageDev.adjusted(-int(g * d), -int(g * d), int(g * d) - 1, int(g * d) - 1));
        }
      p.fillRect(pageDev.adjusted(-int(d), -int(d), int(d), int(d)), desk_.frame);
    } else if (pageShadow_) p.fillRect(pageDev.translated(int(shadow), int(shadow)), QColor(0, 0, 0, 70));
    keys.clear();
    tilesInRect(int(i), (evDev & pageDev).translated(-pageDev.topLeft()), keys);
    for (const TileKey& k : keys) drawTileOrStale(p, k, pageDev.topLeft());
  }
  if (liveActive_ || liveHold_) {
    flushLiveGlow();
    const QImage& layer = liveGlow_ ? liveGlow_->image() : live_;   // a glowing pen: the glow image holds the ink too
    const QRect srcL = evDev.translated(liveMargin_, liveMargin_) & layer.rect();   // layer coordinates
    const QRect src = srcL.translated(-liveMargin_, -liveMargin_);                   // viewport device pixels
    if (!srcL.isEmpty()) {
      p.setOpacity(liveOpacity_);
      if (liveMultiply_) p.setCompositionMode(QPainter::CompositionMode_Multiply);
      p.drawImage(src.topLeft(), layer, srcL);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.setOpacity(1.0);
    }
  }
  // overlays, once per visible page, in that page's own units
  p.setRenderHint(QPainter::Antialiasing, true);
  const bool wantOverlay = !ed_.selection().isEmpty() || ed_.currentTool();
  if (wantOverlay)
    for (size_t i = 0; i < boxes_.size(); i++) {
      const Box& b = boxes_[i];
      if (!b.shown) continue;
      const QRect pageDev(b.x - sc.x(), b.y - sc.y(), b.w, b.h);
      const QRect part = evDev & pageDev;
      if (part.isEmpty()) continue;
      p.save();
      p.translate(pageDev.topLeft());
      p.scale(scale_, scale_);
      OverlayInfo info;
      info.page = int(i);
      info.zoom = zoom_;
      info.visible = QRectF(QPointF((part.left() - pageDev.left()) / scale_, (part.top() - pageDev.top()) / scale_), QPointF((part.right() + 1 - pageDev.left()) / scale_, (part.bottom() + 1 - pageDev.top()) / scale_));
      p.setClipRect(info.visible);
      if (!ed_.selection().isEmpty()) ed_.selection().paintOverlay(p, info);
      if (Tool* t = ed_.gestureTool() ? ed_.gestureTool() : ed_.currentTool()) t->paintOverlay(p, info);
      p.restore();
    }
  p.restore();
  stats_.lastPaintMs = nowMs() - t0;
  stats_.worstPaintMs = std::max(stats_.worstPaintMs, stats_.lastPaintMs);
}

// ---- live layer and overlay -----------------------------------------------------------------------------------------------------------------
QPainter& DocumentView::beginLive(int page, double opacity, bool multiply, const LiveGlowParams* glow) {
  flushPatches();   // the previous stroke is drawn into the tiles before this one reuses the layer
  if (liveP_.isActive()) liveP_.end();
  const double d = dpr();
  liveMargin_ = glow ? std::min(600, glowReach(glowSpec(glow->size, glow->intensity), scale_)) : 0;
  const QSize want(std::max(1, int(std::lround(viewport()->width() * d))) + 2 * liveMargin_, std::max(1, int(std::lround(viewport()->height() * d))) + 2 * liveMargin_);
  if (live_.size() != want) live_ = QImage(want, QImage::Format_ARGB32_Premultiplied);
  live_.fill(Qt::transparent);
  liveP_.begin(&live_);
  liveP_.setRenderHint(QPainter::Antialiasing, true);
  const Box& b = boxes_[size_t(page)];
  const QPoint sc = scrollDev();
  liveP_.translate(b.x - sc.x() + liveMargin_, b.y - sc.y() + liveMargin_);   // fixed for the whole stroke: scrolling and zooming are locked while a gesture runs
  liveP_.scale(scale_, scale_);
  liveActive_ = true;
  livePage_ = page;
  liveOpacity_ = opacity;
  liveMultiply_ = multiply;
  liveGlow_.reset();
  if (glow) liveGlow_.emplace(live_.size(), glow->colour, glowSpec(glow->size, glow->intensity), scale_);
  liveGlowDirty_ = QRect();
  liveDirtyDev_ = QRect();
  return liveP_;
}

void DocumentView::flushLiveGlow() {
  if (!liveGlow_ || liveGlowDirty_.isNull()) return;
  const QRect d = liveGlowDirty_;
  liveGlowDirty_ = QRect();
  const double t0 = nowMs();
  const QRect changed = liveGlow_->update(live_, d);
  const double ms = nowMs() - t0;
  stats_.glowUpdates++;
  stats_.glowMs += ms;
  stats_.worstGlowMs = std::max(stats_.worstGlowMs, ms);
  if (!changed.isNull()) updateDevRect(changed.translated(-liveMargin_, -liveMargin_));   // the glow reaches beyond what the pieces reported
}

const QImage& DocumentView::liveImage() {
  flushLiveGlow();
  return liveGlow_ ? liveGlow_->image() : live_;
}

void DocumentView::liveDirty(const QRectF& pagePart) {
  if (!liveActive_) return;
  const QRect r = pageRectToDev(livePage_, pagePart);
  liveDirtyDev_ = liveDirtyDev_.isNull() ? r : liveDirtyDev_.united(r);
  if (liveGlow_) {   // the glow is brought up to date once per repaint, however many pieces arrived since
    const QRect rl = r.translated(liveMargin_, liveMargin_);
    liveGlowDirty_ = liveGlowDirty_.isNull() ? rl : liveGlowDirty_.united(rl);
    updateDevRect(r.adjusted(-liveGlow_->reach(), -liveGlow_->reach(), liveGlow_->reach(), liveGlow_->reach()));
    return;
  }
  updateDevRect(r);
}

void DocumentView::endLive() {
  if (!liveActive_) return;
  if (liveP_.isActive()) liveP_.end();
  liveActive_ = false;
  if (!liveDirtyDev_.isNull() && liveGlow_) { const int g = liveGlow_->reach(); liveDirtyDev_.adjust(-g, -g, g, g); }   // the glow reaches beyond the pieces
  if (!patches_.empty()) { liveHold_ = true; holdRect_ = liveDirtyDev_; }   // keep showing the live layer until the finished stroke is in the tiles
  if (!liveDirtyDev_.isNull()) updateDevRect(liveDirtyDev_);
  liveDirtyDev_ = QRect();
}

void DocumentView::invalidateOverlay(int page, const QRectF& pagePart) {
  if (page < 0 || page >= int(boxes_.size()) || !boxes_[size_t(page)].shown) return;
  if (pagePart.isNull()) { viewport()->update(pageRectOnScreen(page).toAlignedRect()); return; }
  updateDevRect(pageRectToDev(page, pagePart.adjusted(-2 / zoom_, -2 / zoom_, 2 / zoom_, 2 / zoom_)));
}

/** The pointer moved over the viewport without a gesture (or left it): the cursor is the system arrow off the pages, the tool's own on them. */
void DocumentView::noteHover(const QPointF& viewPos, bool inside) {
  hoverIn_ = inside;
  hoverPos_ = viewPos;
  const bool onPage = inside && pageAt(viewPos, false) >= 0;
  if (onPage != cursorOnPage_) applyCursor();
}

void DocumentView::applyCursor() {
  Tool* t = ed_.cursorTool();
  if (!t) return;
  cursorOnPage_ = hoverIn_ && pageAt(hoverPos_, false) >= 0;
  QCursor want = Qt::ArrowCursor;
  if (cursorOnPage_ || ed_.gestureActive() || tabletDown_ || mouseDown_ || panning_) {
    want = t->cursor();
    if (cursorMapper_) want = cursorMapper_(*t, want);
  }
  const QCursor cur = viewport()->cursor();
  const bool same = cur.shape() == want.shape() && (want.shape() != Qt::BitmapCursor || (cur.pixmap().cacheKey() == want.pixmap().cacheKey() && cur.hotSpot() == want.hotSpot()));
  if (!same) viewport()->setCursor(want);
}

// ---- input ----------------------------------------------------------------------------------------------------------------------------------
// The kind a mouse event becomes when the device was assigned a class (Preferencias > Sistema de entrada). A mouse assigned to Pen / Eraser / Touchscreen has no pressure.
static PointerEvent::Kind classKind(DeviceClass dc) {
  switch (dc) {
    case DeviceClass::Pen: return PointerEvent::Kind::Pen;
    case DeviceClass::Eraser: return PointerEvent::Kind::Eraser;
    case DeviceClass::Touch: return PointerEvent::Kind::Touch;
    default: return PointerEvent::Kind::Mouse;
  }
}

PointerEvent DocumentView::buildEvent(const QPointF& viewPos, const QPointF& globalPos, int page, PointerEvent::Kind kind, Qt::MouseButtons buttons,
                                      Qt::KeyboardModifiers mods, double pressure, bool hasPressure, quint64 ts) const {
  PointerEvent e;
  e.page = page;
  e.viewPos = viewPos;
  e.globalPos = globalPos;
  e.pos = page >= 0 && page < int(boxes_.size()) && boxes_[size_t(page)].shown ? toPage(page, viewPos) : viewPos;
  e.pressure = pressure;
  e.hasPressure = hasPressure;
  e.kind = kind;
  e.buttons = buttons;
  e.mods = mods;
  e.timestampMs = ts;
  e.zoom = zoom_;
  if (kind == PointerEvent::Kind::Pen) {
    const Qt::MouseButtons b = buttons | penMouse_ | penTablet_;   // what the tablet reports plus the barrel buttons it reported earlier and the driver's mouse clicks
    e.pressedBarrel = b.testFlag(Qt::RightButton) || b.testFlag(Qt::BackButton);
    e.pressedBarrel2 = b.testFlag(Qt::MiddleButton) || b.testFlag(Qt::ForwardButton);
  }
  return e;
}

PointerEvent DocumentView::makeEvent(int page, const QPointF& pagePos, double pressure, PointerEvent::Kind kind) const {
  const QPointF v = toView(page, pagePos);
  return buildEvent(v, viewport()->mapToGlobal(v), page, kind, Qt::LeftButton, Qt::NoModifier, pressure, kind == PointerEvent::Kind::Pen, quint64(nowMs()));
}

/** A widget laid over the page (the welcome page) is a child of this view, not of its viewport: a pen on it must reach that widget, not draw below it. */
static bool overChild(const QAbstractScrollArea& area, const QPointF& pos) {
  QWidget* w = area.childAt(pos.toPoint());
  if (!w || w == area.viewport() || area.viewport()->isAncestorOf(w)) return false;
  return !qobject_cast<QScrollBar*>(w) && !qobject_cast<QScrollBar*>(w->parentWidget());
}

void DocumentView::tabletEvent(QTabletEvent* e) {
  if (!tabletDown_ && overChild(*this, e->position())) { e->ignore(); return; }   // Qt then sends that widget the mouse click
  e->accept();
  const bool eraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
  const DeviceClass dc = deviceClassOf(ed_.xpp(), e->pointingDevice());   // Preferencias > Sistema de entrada: what this device is treated as
  if (dc == DeviceClass::Disabled) return;
  const auto kind = dc == DeviceClass::Mouse ? PointerEvent::Kind::Mouse : dc == DeviceClass::Touch ? PointerEvent::Kind::Touch : dc == DeviceClass::Eraser ? PointerEvent::Kind::Eraser
                    : dc == DeviceClass::Pen ? PointerEvent::Kind::Pen : eraser ? PointerEvent::Kind::Eraser : PointerEvent::Kind::Pen;
  const bool penLike = kind == PointerEvent::Kind::Pen || kind == PointerEvent::Kind::Eraser;
  const QPointF pos = e->position();
  auto make = [&](int page) {
    PointerEvent ev = buildEvent(pos, e->globalPosition(), page, kind, e->buttons(), e->modifiers(), penLike ? e->pressure() : 0.5, penLike, e->timestamp());
    ev.tiltX = e->xTilt();
    ev.tiltY = e->yTilt();
    ev.rotation = e->rotation();
    return ev;
  };
  // "Tip in contact" is defined once: the Left button, or (XP-Pen with Windows Ink) pressure with a barrel button held, which Qt reports as a right/middle drag: no press
  // event comes when contact begins with the button already down, only moves with pressure > 0 and buttons == Right/Middle. Raw pressure: the curve never decides contact.
  const bool touching = penLike && e->pressure() > 0;
  constexpr Qt::MouseButtons kBarrel =Qt::RightButton | Qt::MiddleButton | Qt::BackButton | Qt::ForwardButton;
  // Which barrel buttons are down comes from the buttons() of the events while the pen hovers (the driver reports a barrel release as a 'Left release', so the event's own
  // button() cannot be trusted); once the tip touches, buttons() loses them, so what the hover said stays until the pressure is gone.
  if (!touching) penTablet_ = e->buttons() & kBarrel;
  else penTablet_ |= e->buttons() & kBarrel;
  const Qt::MouseButtons barrelBits = (e->buttons() | penTablet_) & kBarrel;
  auto startGesture = [&](bool byBarrel) {
    gesturePage_ = pageAt(pos, true);
    if (gesturePage_ < 0) return;
    tabletDown_ = true;
    barrelContact_ = byBarrel;
    ed_.pointerDown(make(gesturePage_));
    applyCursor();
  };
  auto endGesture = [&] {
    tabletDown_ = false;
    barrelContact_ = false;
    ed_.pointerUp(make(gesturePage_));
    applyCursor();
  };
  switch (e->type()) {
    case QEvent::TabletPress:
      if (e->button() == Qt::LeftButton && !tabletDown_) {
        startGesture(false);
      } else if (!tabletDown_ && touching && (barrelBits || penMouse_ || penTablet_)) {
        startGesture(true);
      } else if (!tabletDown_ && e->button() != Qt::LeftButton) {
        noteHover(pos, true);
        ed_.hover(make(pageAt(pos, false)));   // a barrel button pressed with the pen only hovering: its tool is in use from now on, with no move needed
      }
      break;
    case QEvent::TabletMove:
      if (tabletDown_ && !ed_.xpp().drawOutside && !viewport()->rect().contains(pos.toPoint())) {   // drawing outside the window switched off: the stroke ends at the border
        endGesture();
        break;
      }
      // contact began with a barrel button held. The driver then reports the tip as 'Left only' (the barrel bit is gone from the tablet's buttons, the real mouse click it
      // sends in parallel is `penMouse_`), with no press event: any pressure while a button is down, from the tablet or the mouse, is the tip. It ends when the pressure does.
      if (!tabletDown_ && touching && (e->buttons() || penMouse_ || penTablet_)) { startGesture(true); if (tabletDown_) break; }
      if (tabletDown_ && barrelContact_ && !touching) { endGesture(); noteHover(pos, true); ed_.hover(make(pageAt(pos, false))); break; }   // lifted: the drag ends where it was
      if (tabletDown_) ed_.pointerMove(make(gesturePage_));
      else { noteHover(pos, true); ed_.hover(make(pageAt(pos, false))); }
      break;
    case QEvent::TabletRelease:
      if (tabletDown_ && (barrelContact_ ? !touching : e->button() == Qt::LeftButton)) {   // a contact the pressure started ends with the pressure (the driver's stray 'Left release' mid-contact is not the end)
        endGesture();
      } else if (!tabletDown_ && e->button() != Qt::LeftButton) {
        noteHover(pos, true);
        ed_.hover(make(pageAt(pos, false)));   // a barrel button released while hovering: the tool goes back
      }
      break;
    default: break;
  }
}

void DocumentView::startPanning(const QPointF& pos) { inertia_.stop(); panning_ = true; panLast_ = pos; viewport()->setCursor(Qt::ClosedHandCursor); }

// A driver (XP-Pen with Windows Ink) can report a barrel button as a real mouse click besides the tablet events. While the pen is near, such a click is the pen's button:
// it is merged into the buttons of the pen's events (so the Preferencias mapping applies), never a mouse gesture and never the page-dragging pan of the mouse's middle button.
bool DocumentView::penBarrelMouse(const QMouseEvent* e) const {
  const Qt::MouseButton b = e->button();
  return e->source() == Qt::MouseEventNotSynthesized && (b == Qt::MiddleButton || b == Qt::RightButton || b == Qt::BackButton || b == Qt::ForwardButton) && ed_.penRecent();
}

/** The pen's tool follows its barrel buttons also while it only hovers: tell the editor again with the buttons as they are now. */
void DocumentView::refreshPenHover() {
  if (tabletDown_ || ed_.gestureActive()) return;
  const QPointF p = hoverIn_ ? hoverPos_ : QPointF(viewport()->mapFromGlobal(QCursor::pos()));
  noteHover(p, true);
  ed_.hover(buildEvent(p, viewport()->mapToGlobal(p), pageAt(p, false), PointerEvent::Kind::Pen, Qt::NoButton, Qt::NoModifier, 0, true, quint64(0)));
}

void DocumentView::mousePressEvent(QMouseEvent* e) {
  if (penBarrelMouse(e)) { penMouse_ |= e->button(); e->accept(); refreshPenHover(); return; }
  if (tabletDown_ || e->source() == Qt::MouseEventSynthesizedByQt) { e->accept(); return; }
  const bool touch = e->source() == Qt::MouseEventSynthesizedBySystem;
  if (touch && penNear()) { ignoreMouseTouch_ = true; e->accept(); return; }   // the resting palm, not a finger
  const DeviceClass dc = touch ? DeviceClass::Auto : deviceClassOf(ed_.xpp(), e->pointingDevice());   // Preferencias > Sistema de entrada
  if (dc == DeviceClass::Disabled) { e->accept(); return; }
  if (touch && !ed_.fingerDraw()) { startPanning(e->position()); e->accept(); return; }
  // Preferencias > Ratón: what each button selects while it is held ("hand" is the page-dragging pan, the middle button's default)
  const Qt::MouseButton btn = e->button();
  if (!touch && btn != Qt::LeftButton) {
    const XppPrefs& x = ed_.xpp();
    const ButtonCfg& c = btn == Qt::MiddleButton ? x.mouseMiddle : btn == Qt::RightButton ? x.mouseRight : btn == Qt::BackButton ? x.mouse4 : btn == Qt::ForwardButton ? x.mouse5 : ButtonCfg();
    if (c.tool == "hand") { startPanning(e->position()); e->accept(); return; }
    if (c.tool.isEmpty() || (c.tool != "toolbox" && !ed_.hasTool(c.tool))) return;
  }
  if (!touch && btn != Qt::LeftButton && btn != Qt::MiddleButton && btn != Qt::RightButton && btn != Qt::BackButton && btn != Qt::ForwardButton) return;
  gesturePage_ = pageAt(e->position(), true);
  if (gesturePage_ < 0) return;
  const bool boxOnly = !touch && btn != Qt::LeftButton && (btn == Qt::MiddleButton ? ed_.xpp().mouseMiddle : btn == Qt::RightButton ? ed_.xpp().mouseRight : btn == Qt::BackButton ? ed_.xpp().mouse4 : ed_.xpp().mouse5).tool == "toolbox";
  mouseDown_ = !boxOnly;   // a button that only asks for the tool box has no gesture to follow
  mouseButton_ = btn;
  setFocus();
  ed_.pointerDown(buildEvent(e->position(), e->globalPosition(), gesturePage_, touch ? PointerEvent::Kind::Touch : classKind(dc), e->buttons(), e->modifiers(), 0.5, false, e->timestamp()));
  applyCursor();
}

void DocumentView::mouseMoveEvent(QMouseEvent* e) {
  if (penMouse_ != Qt::NoButton && e->source() == Qt::MouseEventNotSynthesized) {
    if (!(e->buttons() & penMouse_)) penMouse_ = Qt::NoButton;   // the mouse itself says they are up: a release that never arrived
    else { e->accept(); return; }                                // the pen's own echo while a barrel button is down: the tablet events lead
  }
  if (tabletDown_ || ignoreMouseTouch_ || e->source() == Qt::MouseEventSynthesizedByQt) { e->accept(); return; }
  const bool touch = e->source() == Qt::MouseEventSynthesizedBySystem;
  if (panning_) {
    const QPointF delta = panLast_ - e->position();
    panLast_ = e->position();
    scrollByPixels(delta.x(), delta.y());
    return;
  }
  const DeviceClass dc = touch ? DeviceClass::Auto : deviceClassOf(ed_.xpp(), e->pointingDevice());
  if (dc == DeviceClass::Disabled) { e->accept(); return; }
  const auto kind = touch ? PointerEvent::Kind::Touch : classKind(dc);
  if (mouseDown_ && !ed_.xpp().drawOutside && !viewport()->rect().contains(e->position().toPoint())) {   // drawing outside the window switched off
    mouseDown_ = false;
    ed_.pointerUp(buildEvent(e->position(), e->globalPosition(), gesturePage_, kind, e->buttons(), e->modifiers(), 0.5, false, e->timestamp()));
    applyCursor();
    return;
  }
  if (mouseDown_) ed_.pointerMove(buildEvent(e->position(), e->globalPosition(), gesturePage_, kind, e->buttons(), e->modifiers(), 0.5, false, e->timestamp()));
  else { noteHover(e->position(), true); ed_.hover(buildEvent(e->position(), e->globalPosition(), pageAt(e->position(), false), kind, e->buttons(), e->modifiers(), 0.5, false, e->timestamp())); }
}

void DocumentView::mouseReleaseEvent(QMouseEvent* e) {
  if (ignoreMouseTouch_) { ignoreMouseTouch_ = false; e->accept(); return; }
  if (penMouse_.testFlag(e->button())) { penMouse_ &= ~Qt::MouseButtons(e->button()); e->accept(); refreshPenHover(); return; }
  if (tabletDown_ || e->source() == Qt::MouseEventSynthesizedByQt) { e->accept(); return; }
  if (panning_) { panning_ = false; applyCursor(); return; }
  if (mouseDown_ && e->button() == mouseButton_) {
    mouseDown_ = false;
    const bool touch = e->source() == Qt::MouseEventSynthesizedBySystem;
    const DeviceClass dc = touch ? DeviceClass::Auto : deviceClassOf(ed_.xpp(), e->pointingDevice());
    ed_.pointerUp(buildEvent(e->position(), e->globalPosition(), gesturePage_, touch ? PointerEvent::Kind::Touch : classKind(dc), e->buttons(), e->modifiers(), 0.5, false, e->timestamp()));
    applyCursor();
  }
}

void DocumentView::wheelEvent(QWheelEvent* e) {
  if (ed_.gestureActive()) { e->accept(); return; }   // the live layer is tied to the scroll position: no scrolling or zooming mid-stroke
  inertia_.stop();
  if (!continuous_ && !(e->modifiers() & Qt::ControlModifier)) {
    // single-page view: scrolling past the end of a page turns to the next one (and past the top to the previous one, showing its bottom), at most every 350 ms
    const int dy = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->pixelDelta().y();
    QScrollBar* sb = verticalScrollBar();
    const int cur = ed_.currentPage(), last = ed_.pageCount() - 1;
    const double now = nowMs();
    if (dy != 0 && now - lastFlipMs_ >= 350) {
      if (dy < 0 && sb->value() >= sb->maximum() && cur < last && (!paired_ || cur / 2 * 2 + 2 <= last)) {
        lastFlipMs_ = now;
        if (paired_) { scrollToPage(cur / 2 * 2 + 2); e->accept(); return; }   // the next pair, not the other page of this one
        ed_.action("nav.next")->trigger();
        if (ed_.currentPage() == cur) scrollToPage(cur + 1);
        e->accept();
        return;
      }
      if (dy > 0 && sb->value() <= sb->minimum() && cur > 0 && (!paired_ || cur / 2 * 2 > 0)) {
        lastFlipMs_ = now;
        if (paired_) { scrollToPage(cur / 2 * 2 - 2); verticalScrollBar()->setValue(verticalScrollBar()->maximum()); e->accept(); return; }
        ed_.action("nav.prev")->trigger();
        if (ed_.currentPage() == cur) scrollToPage(cur - 1);
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        e->accept();
        return;
      }
    }
  }
  if (e->modifiers() & Qt::ControlModifier) {
    const double dy = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->pixelDelta().y();
    if (dy != 0) zoomBy(std::pow(1.0 + ed_.xpp().zoomStepScroll / 100.0, dy / 120.0), e->position());   // Preferencias > Escala: % per wheel notch
    e->accept();
    return;
  }
  if ((e->modifiers() & Qt::ShiftModifier) && e->angleDelta().x() == 0 && e->angleDelta().y() != 0) {   // Shift + wheel scrolls sideways, as in a browser
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y());
    e->accept();
    return;
  }
  QAbstractScrollArea::wheelEvent(e);
}

bool DocumentView::viewportEvent(QEvent* e) {
  switch (e->type()) {
    case QEvent::TabletPress: case QEvent::TabletMove: case QEvent::TabletRelease:
      tabletEvent(static_cast<QTabletEvent*>(e));   // QAbstractScrollArea does not forward tablet events of its viewport
      return true;
    case QEvent::TabletEnterProximity: case QEvent::TabletLeaveProximity:
      if (e->type() == QEvent::TabletLeaveProximity) {
        penMouse_ = penTablet_ = Qt::NoButton;   // a button released out there never reports it
        if (tabletDown_ && barrelContact_) { tabletDown_ = false; barrelContact_ = false; ed_.pointerCancel(); applyCursor(); }
      }
      if (e->type() == QEvent::TabletLeaveProximity && !tabletDown_) { noteHover(hoverPos_, false); ed_.pointerLeft(); }   // out of range: nothing is held or hovered any more
      e->accept();
      return true;
    case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd: case QEvent::TouchCancel:
      return touchEvent(static_cast<QTouchEvent*>(e));
    case QEvent::Leave: noteHover(hoverPos_, false); ed_.pointerLeft(); break;
    default: break;
  }
  return QAbstractScrollArea::viewportEvent(e);
}

// ---- fingers --------------------------------------------------------------------------------------------------------------------------------
bool DocumentView::penNear() const { return ed_.xpp().handRecognition && ed_.penAgeMs() < ed_.xpp().handTimeout * 1000.0; }   // Preferencias > Pantalla táctil

void DocumentView::panSample(const QPointF& viewPos, double tMs) {
  panSamples_.push_back({tMs, viewPos});
  while (panSamples_.size() > 2 && tMs - panSamples_.front().first > 120) panSamples_.pop_front();
}

void DocumentView::endPan(bool flick) {
  if (flick && !ed_.xpp().disableInertia && panSamples_.size() >= 2) {
    const auto& a = panSamples_.front();
    const auto& b = panSamples_.back();
    const double dt = (b.first - a.first) / 1000.0, since = (nowMs() - b.first) / 1000.0;
    if (qEnvironmentVariableIsSet("XL_TOUCH_DEBUG")) fprintf(stderr, "endPan samples=%zu dt=%.3f since=%.3f dy=%.1f\n", panSamples_.size(), dt, since, a.second.y() - b.second.y());
    if (dt > 0.01 && since < 0.1) {   // a finger that stopped before it lifted does not flick
      const QPointF v = (a.second - b.second) / dt;   // the page moves with the finger: scrolling goes the other way
      if (std::hypot(v.x(), v.y()) > 250) { inertiaV_ = v; inertiaRemX_ = inertiaRemY_ = 0; inertia_.start(); }
    }
  }
  panSamples_.clear();
}

void DocumentView::inertiaStep() {
  if (ed_.gestureActive()) { inertia_.stop(); return; }   // a stroke has begun: the live layer is tied to the scroll position
  const double dt = 0.016;
  const int x0 = horizontalScrollBar()->value(), y0 = verticalScrollBar()->value();
  inertiaRemX_ += inertiaV_.x() * dt;
  inertiaRemY_ += inertiaV_.y() * dt;
  const int mx = int(inertiaRemX_), my = int(inertiaRemY_);
  inertiaRemX_ -= mx;
  inertiaRemY_ -= my;
  if (mx || my) scrollByPixels(mx, my);
  if (horizontalScrollBar()->value() == x0 && mx) inertiaV_.setX(0);   // the end of the page: that axis stops
  if (verticalScrollBar()->value() == y0 && my) inertiaV_.setY(0);
  inertiaV_ *= 0.94;
  if (std::hypot(inertiaV_.x(), inertiaV_.y()) < 30) inertia_.stop();
}

bool DocumentView::touchEvent(QTouchEvent* e) {
  e->accept();
  const QPointF origin = QPointF(viewport()->mapTo(window(), QPoint(0, 0)));
  std::vector<QPointF> live;   // fingers that are down (not the ones lifted by this event)
  for (const QEventPoint& p : e->points()) if (p.state() != QEventPoint::State::Released) live.push_back(p.scenePosition() - origin);
  const double t = nowMs();   // the arrival time: the device's own clock is not comparable with ours, and a flick is about what the finger did just now
  const quint64 ts = e->timestamp() ? quint64(e->timestamp()) : quint64(t);
  auto drawEvent = [&](const QPointF& v) { return buildEvent(v, viewport()->mapToGlobal(v.toPoint()), gesturePage_, PointerEvent::Kind::Touch, Qt::LeftButton, e->modifiers(), 0.5, false, ts); };
  const auto type = e->type();
  if (type == QEvent::TouchBegin) {
    inertia_.stop();
    touch_ = Touch::None;
    if (live.empty()) return true;
    if (penNear() || ed_.gestureActive() || deviceClassOf(ed_.xpp(), e->pointingDevice()) == DeviceClass::Disabled) { touch_ = Touch::Ignored; return true; }   // (a touch screen set to 'desactivado' in Preferencias is ignored too)   // a palm next to the pen, or a gesture of the pen is on
    const QPointF v = live.front();
    if (ed_.fingerDraw()) {
      gesturePage_ = pageAt(v, true);
      if (gesturePage_ < 0) { touch_ = Touch::Ignored; return true; }
      touch_ = Touch::Draw;
      panLast_ = v;
      setFocus();
      ed_.pointerDown(drawEvent(v));
      if (Tool* tl = ed_.gestureTool()) viewport()->setCursor(tl->cursor());
    } else {
      touch_ = Touch::Pan;
      panLast_ = v;
      panSamples_.clear();
      panSample(v, t);
    }
    return true;
  }
  if (touch_ == Touch::Ignored || touch_ == Touch::None) { if (type == QEvent::TouchEnd || type == QEvent::TouchCancel) touch_ = Touch::None; return true; }
  if (type == QEvent::TouchCancel || type == QEvent::TouchEnd) {
    if (touch_ == Touch::Draw) { if (type == QEvent::TouchCancel) ed_.pointerCancel(); else ed_.pointerUp(drawEvent(e->points().isEmpty() ? panLast_ : e->points().first().scenePosition() - origin)); }
    else if (touch_ == Touch::Pan) endPan(type == QEvent::TouchEnd);
    touch_ = Touch::None;
    applyCursor();
    return true;
  }
  // TouchUpdate
  if (live.size() >= 2 && touch_ != Touch::Pinch && !ed_.xpp().zoomGestures) return true;   // zoom gestures switched off: two fingers do nothing
  if (live.size() >= 2 && touch_ != Touch::Pinch) {   // a second finger: zoom, and a stroke the first one began is dropped
    if (touch_ == Touch::Draw) ed_.pointerCancel();
    if (touch_ == Touch::Pan) endPan(false);
    touch_ = Touch::Pinch;
    pinchCentre_ = (live[0] + live[1]) / 2;
    pinchDist_ = std::max(1.0, std::hypot(live[0].x() - live[1].x(), live[0].y() - live[1].y()));
    pinchStarted_ = false;
    return true;
  }
  if (touch_ == Touch::Pinch) {
    if (live.size() >= 2) {
      const QPointF c = (live[0] + live[1]) / 2;
      const double d = std::max(1.0, std::hypot(live[0].x() - live[1].x(), live[0].y() - live[1].y()));
      if (!pinchStarted_) {   // Preferencias > Pantalla táctil: zooming starts only after the fingers moved this % further apart (or closer) than at first
        if (std::abs(d / pinchDist_ - 1.0) * 100.0 < ed_.xpp().zoomStartThreshold) return true;
        pinchStarted_ = true;
      }
      scrollByPixels(pinchCentre_.x() - c.x(), pinchCentre_.y() - c.y());   // the content follows the fingers' centre...
      zoomBy(d / pinchDist_, c);                                           // ...and grows with their distance
      pinchCentre_ = c;
      pinchDist_ = d;
    } else if (live.size() == 1) {   // one finger stays: it pans from where it is
      touch_ = Touch::Pan;
      panLast_ = live.front();
      panSamples_.clear();
      panSample(panLast_, t);
    }
    return true;
  }
  if (live.empty()) return true;
  const QPointF v = live.front();
  if (touch_ == Touch::Draw) { ed_.pointerMove(drawEvent(v)); panLast_ = v; return true; }
  const QPointF delta = panLast_ - v;
  panLast_ = v;
  scrollByPixels(delta.x(), delta.y());
  panSample(v, t);
  return true;
}

void DocumentView::keyPressEvent(QKeyEvent* e) {
  if (ed_.keyPress(e)) { e->accept(); return; }
  QAbstractScrollArea::keyPressEvent(e);
}

void DocumentView::keyReleaseEvent(QKeyEvent* e) {
  if (ed_.keyRelease(e)) { e->accept(); return; }
  QAbstractScrollArea::keyReleaseEvent(e);
}
