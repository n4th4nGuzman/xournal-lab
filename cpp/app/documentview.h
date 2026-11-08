// The page area: pages laid out vertically (continuous) or one at a time, zoom, scroll, and the whole drawing pipeline of the prototype generalised to a
// document: tiles of the finished pages in a bounded cache (rendered lazily, re-rendered only where something changed), a live layer for the stroke
// being drawn, and an overlay pass for tool previews. Layout and painting work in DEVICE pixels so tiles land on whole pixels at any display scale.
#pragma once
#include "editor.h"
#include "liveglow.h"
#include "pagerender.h"
#include "tilecache.h"
#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <deque>
#include <functional>
#include <optional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class QTouchEvent;
class DocumentView : public QAbstractScrollArea {
  Q_OBJECT
 public:
  explicit DocumentView(Editor& ed, QWidget* parent = nullptr);
  ~DocumentView() override;

  // ---- zoom and layout -----------------------------------------------------------------------------------------------------------------
  double zoom() const { return zoom_; }                          // CSS pixels per page unit; 1 = one point per pixel (A4 = 595 px wide)
  void setZoom(double z, const QPointF& anchor = QPointF(-1, -1));   // anchor in viewport pixels (default: the centre) stays where it is
  void zoomBy(double factor, const QPointF& anchor = QPointF(-1, -1)) { setZoom(zoom_ * factor, anchor); }
  void zoom100() { setZoom(1.0); }
  void zoomFitWidth();
  void zoomFitPage();
  /** While on, the zoom follows the width of the view (window or sidebar resizes, paired pages, a new document); any other zoom change turns it off. */
  void setAutoFitWidth(bool on) { autoFit_ = on; if (on) zoomFitWidth(); }
  bool autoFitWidth() const { return autoFit_; }
  static constexpr double kMinZoom = 0.25, kMaxZoom = 20.0;
  bool continuous() const { return continuous_; }
  void setContinuous(bool on);
  /** Two facing pages per row (1-2, 3-4...), like Xournal++'s "paired pages"; each page keeps its own size. */
  bool pairedPages() const { return paired_; }
  void setPairedPages(bool on);

  // ---- navigation and coordinates -------------------------------------------------------------------------------------------------------
  void scrollToPage(int page);                                   // top of the page at the top of the view
  void ensurePageVisible(int page);                              // only scrolls when the page is not on screen at all
  void scrollByPixels(double dx, double dy);                     // logical pixels
  /** Page under a viewport point (-1 if none). With `nearest`, the closest shown page even when the point is in a gap or beside it. */
  int pageAt(const QPointF& viewPos, bool nearest = false) const;
  QPointF toPage(int page, const QPointF& viewPos) const;        // viewport pixels -> page units of that page
  QPointF toView(int page, const QPointF& pagePos) const;
  QRectF pageRectOnScreen(int page) const;                       // viewport pixels
  std::vector<int> visiblePages() const;
  /** Builds the PointerEvent a real device event would produce for a point of `page` (used by the bench and self test). */
  PointerEvent makeEvent(int page, const QPointF& pagePos, double pressure = 0.5, PointerEvent::Kind kind = PointerEvent::Kind::Pen) const;

  // ---- live layer (the stroke being drawn) ----------------------------------------------------------------------------------------------
  /** Starts a live stroke on `page`: clears the layer and returns a painter whose units are that page's points. `opacity` composes the whole layer (highlighter). */
  struct LiveGlowParams { QColor colour; double size = 0, intensity = 0; };
  /** A glowing pen passes `glow`: the live layer then shows the glow the finished stroke will have, computed by the page renderer's own filter (core/liveglow.h)
   *  incrementally; the layer is made larger than the viewport by the reach of the glow, so ink just outside the view still lights its edge. The ink drawn on the
   *  painter is displayed through the glow (the glow image contains the ink). */
  QPainter& beginLive(int page, double opacity = 1.0, bool multiply = false, const LiveGlowParams* glow = nullptr);
  /** `multiply`: the layer is composed with Multiply like a finished highlighter stroke (so the colour under it shows through and nothing changes when the pen lifts). */
  /** What the live layer shows right now (for tests): the ink, or the glow with the ink in it. Brings the glow up to date first. */
  const QImage& liveImage();
  /** The part of the page that changed in the live layer: schedules its repaint (only that area is painted, never the page). */
  void liveDirty(const QRectF& pagePart);
  void endLive();
  bool liveActive() const { return liveActive_; }

  // ---- overlay ------------------------------------------------------------------------------------------------------------------------
  /** Ask for a repaint of a page area (null = the visible part of the page) so Tool::paintOverlay runs for it. */
  void invalidateOverlay(int page, const QRectF& pagePart = QRectF());
  /** Puts the cursor the situation asks for on the viewport: the system arrow while the pointer is not over a page (and no gesture runs), otherwise the tool's own
   *  (through the mapper, if one is set). Called when the tool, its style, the zoom, the scroll position or the hover state changes. */
  void applyCursor();
  /** Lets the preferences replace the cursor a tool asks for (the pen cursor options); nothing set = the tool's own. */
  void setCursorMapper(std::function<QCursor(const Tool&, const QCursor&)> m) { cursorMapper_ = std::move(m); applyCursor(); }

  // ---- measurement and tests ----------------------------------------------------------------------------------------------------------
  struct Stats {
    int tilesRendered = 0, tilesLite = 0, tilesPatched = 0, paints = 0;
    int tilesAsync = 0, tilesPartial = 0, tilesDropped = 0;   // finished by a worker thread / only a rectangle of the tile drawn again / worker result thrown away (stale)
    double renderMs = 0;       // time the GUI thread spent in tile rendering
    double asyncMs = 0;        // time the worker threads spent (not on the GUI thread)
    double lastPaintMs = 0, worstPaintMs = 0;
    int glowUpdates = 0;       // times the live glow was brought up to date (once per repaint that follows new ink)
    double glowMs = 0, worstGlowMs = 0;   // time spent in them
  };
  Stats stats() const { return stats_; }
  void resetStats() { stats_ = Stats(); }
  size_t cachedTiles() const { return cache_.count(); }
  size_t cacheBytes() const { return cache_.bytes(); }
  /** Renders every visible tile sharp now (blocking). Returns how many tiles were rendered. */
  int renderVisibleNow();
  void clearCache();
  /** Tiles with at least this many elements are drawn by a worker thread (default 200); smaller ones on the GUI thread right away. 0 = everything async. */
  void setAsyncThreshold(int elements) { asyncMin_ = elements; }
  /** Number of worker threads for tiles (default: cores - 2, between 1 and 6). */
  void setWorkerThreads(int n);
  /** False = the previous tile path: every element of the page tested per tile, all on the GUI thread (for comparing, see --bench). */
  void setIndexedTiles(bool on) { indexed_ = on; clearCache(); }

  // ---- look (the chrome area's themes) ------------------------------------------------------------------------------------------------
  /** What is behind the pages and around them. An invalid gradTop = a flat `desk`; a valid `frame` replaces the drop shadow by an outline with a `glow` halo. */
  struct DeskStyle { QColor desk{0x3b, 0x3b, 0x3b}; QColor gradTop, gradMid, gradBottom; QColor frame, glow; };
  void setDeskStyle(const DeskStyle& s) { desk_ = s; viewport()->update(); }
  const DeskStyle& deskStyle() const { return desk_; }
  /** Defaults of every render call (paper colour of the theme, halos of glowing strokes): the tiles are drawn again. */
  void setRenderBase(const RenderOptions& o) { renderBase_ = o; clearCache(); }
  const RenderOptions& renderBase() const { return renderBase_; }
  bool hasPendingTiles() const { return !pending_.empty() || !inflight_.empty() || !patches_.empty() || dirtyFlushQueued_ || interacting_ || settleTimer_.isActive(); }
  /** Draws the viewport exactly as the screen shows it into an image (tiles that are not ready are rendered first). */
  QImage snapshot();

  // ---- fingers ------------------------------------------------------------------------------------------------------------------------
  // One finger pans (or draws when Editor::fingerDraw is on), two fingers pinch-zoom and pan, a flick keeps scrolling for a moment, and a finger that touches while
  // a pen is near (the resting palm) is ignored. The pen counts as near for this long after its last event (hovering sends events all the time).
  static constexpr int kPenNearMs = 700;
  bool penNear() const;
  /** Preferencias > Ver and > Área de dibujo: the shadow behind the pages, and extra space (device-independent pixels) around the pages that can be scrolled to. */
  void setPageShadow(bool on) { if (on != pageShadow_) { pageShadow_ = on; viewport()->update(); } }
  void setExtraSpace(const QMargins& m) { if (m != extra_) { extra_ = m; relayout(true); viewport()->update(); } }
  const QMargins& extraSpace() const { return extra_; }
  void forgetPen() { ed_.forgetPen(); penMouse_ = penTablet_ = Qt::NoButton; }   // tests: the pen has gone
  bool inertiaActive() const { return inertia_.isActive(); }

 signals:
  void zoomChanged(double zoom);

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void tabletEvent(QTabletEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void keyReleaseEvent(QKeyEvent*) override;
  bool viewportEvent(QEvent*) override;
  void scrollContentsBy(int dx, int dy) override;

 private:
  struct Box { int x = 0, y = 0, w = 0, h = 0; bool shown = false; };   // scene device pixels
  struct Job { TileKey key; bool prefetch = false; };

  double dpr() const { return viewport()->devicePixelRatioF(); }
  QPoint scrollDev() const;
  QRect viewportDev() const;
  void relayout(bool keepScroll = true);
  void layoutOnce(bool keepScroll);
  void updateScrollBars();
  void setScrollDev(int x, int y);
  int scaleKey() const { return int(std::llround(scale_ * 10000)); }

  // tiles
  struct TileDone { TileKey key; QImage img; bool lite = false; QRect region; uint64_t epoch = 0, seq = 0; double ms = 0; std::shared_ptr<std::atomic<bool>> token; };
  struct Patch { int page; QRectF rect; Element el; };
  QPixmap renderTile(const TileKey& k, bool lite);
  /** Draws `region` (tile pixels, null = all) of an existing tile again, or makes the tile, from the elements the spatial index gives for it. */
  void refreshTile(const TileKey& k, const QRect& region, bool lite);
  void elementsFor(int page, const QRectF& rect, std::vector<const Element*>& out);
  bool startAsync(const TileKey& k, const std::vector<const Element*>& els, bool lite, const QRect& region = QRect());
  void collectDone();
  void cancelAll();                       // content or geometry changed: nothing in flight is of any use
  void cancelFar();                       // scrolled away: drop what is in flight for tiles nobody will look at
  bool staleSince(const TileKey& k, uint64_t seq) const;
  void noteChange(int page, const QRectF& rect);
  void flushPatches();
  void applyPatch(const Patch& pt);
  void releaseHold();
  void processPatches();
  void request(const TileKey& k, bool prefetch = false);
  void processJobs();
  void tilesInRect(int page, const QRect& devRectInPage, std::vector<TileKey>& out) const;
  void drawTileOrStale(QPainter& p, const TileKey& k, const QPoint& originDev);
  void drawStale(QPainter& p, const TileKey& k, const QPoint& originDev, const QRect& target);
  void noteInteraction();
  void settle();
  void queuePrefetch();
  void invalidate(int page, const QRectF& rect);
  void flushDirty();
  void onElementAppended(int page, int layer, int index, const QRectF& rect);
  void updateDevRect(const QRect& dev);
  QRect pageRectToDev(int page, const QRectF& r) const;   // viewport device pixels
  void updateCurrentPage();
  void noteHover(const QPointF& viewPos, bool inside);
  void flushLiveGlow();

  // input
  PointerEvent buildEvent(const QPointF& viewPos, const QPointF& globalPos, int page, PointerEvent::Kind kind, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods, double pressure, bool hasPressure, quint64 ts) const;
  void startPanning(const QPointF& pos);
  bool penBarrelMouse(const QMouseEvent* e) const;
  void refreshPenHover();
  bool touchEvent(QTouchEvent*);
  void panSample(const QPointF& viewPos, double tMs);
  void endPan(bool flick);
  void inertiaStep();

  Editor& ed_;
  DeskStyle desk_;
  RenderOptions renderBase_;
  double zoom_ = 1.0, scale_ = 1.0;
  bool continuous_ = true;
  bool paired_ = false;
  bool autoFit_ = false, fitting_ = false;   // see setAutoFitWidth; fitting_ marks the zoom change that fitting itself makes
  std::vector<Box> boxes_;
  QSize sceneDev_;
  bool inRelayout_ = false, relayoutAgain_ = false;

  TileCache cache_;
  std::vector<Job> pending_;
  std::unordered_set<TileKey, TileKeyHash> pendingSet_;
  QTimer jobTimer_, settleTimer_, flushTimer_, patchTimer_;
  bool interacting_ = false, dirtyFlushQueued_ = false;
  std::unordered_set<TileKey, TileKeyHash> dirtyKeys_;
  Stats stats_;

  // tiles on worker threads: a job owns a copy of what it draws (never a pointer into the document), the result is a QImage handed back to the GUI thread.
  // A result is used only when no change touched its tile since the job was made (changeLog_) and nothing cleared the cache (epoch_).
  QThreadPool pool_;
  std::unordered_map<TileKey, std::shared_ptr<std::atomic<bool>>, TileKeyHash> inflight_;   // key -> its cancel flag
  std::mutex doneMx_;
  std::vector<TileDone> done_;
  struct Change { uint64_t seq; int page; QRectF rect; };
  std::deque<Change> changeLog_;
  uint64_t seq_ = 0, epoch_ = 0;
  int asyncMin_ = 200, maxInflight_ = 4;
  bool indexed_ = true;
  std::vector<PageIndex::Hit> hits_;
  std::deque<Patch> patches_;       // finished strokes waiting to be drawn into the tiles (the live layer stays visible meanwhile)
  bool liveHold_ = false;
  QRect holdRect_;

  // live layer
  QImage live_;                     // the ink of the stroke being drawn (premultiplied ARGB, one device pixel per layer pixel)
  QPainter liveP_;
  bool liveActive_ = false;
  int livePage_ = -1;
  double liveOpacity_ = 1.0;
  bool liveMultiply_ = false;
  std::optional<LiveGlow> liveGlow_;
  int liveMargin_ = 0;              // the live layer extends this many pixels beyond the viewport on every side (room for the glow)
  QRect liveGlowDirty_;             // ink changed here since the glow was last brought up to date
  QRect liveDirtyDev_;

  // cursor
  std::function<QCursor(const Tool&, const QCursor&)> cursorMapper_;
  bool hoverIn_ = false;            // the pointer is over the viewport (last hover event)
  QPointF hoverPos_;
  bool cursorOnPage_ = false;

  // input state
  bool tabletDown_ = false, mouseDown_ = false, panning_ = false;
  bool barrelContact_ = false;              // the gesture in progress began by contact with a barrel button held (no Left in buttons): it ends when the pressure does
  Qt::MouseButton mouseButton_ = Qt::NoButton;   // the mouse button that started the gesture in progress
  bool pageShadow_ = true;
  QMargins extra_;
  int gesturePage_ = -1;
  QPointF panLast_;
  bool pageFollowsScroll_ = true;
  Qt::MouseButtons penTablet_;              // barrel buttons whose press the tablet reported and whose release has not come: with the tip down the driver drops them from buttons() (Xournal++: 'combine button events with tip events')
  Qt::MouseButtons penMouse_;               // mouse buttons a driver sent while the pen was near: they are the pen's barrel buttons (see Editor::notePen)
  bool ignoreMouseTouch_ = false;           // a system-synthesized mouse press from a palm: its move and release are ignored too
  double lastFlipMs_ = -1e9;                // single-page view: when the wheel last turned a page
  // fingers
  enum class Touch { None, Pan, Draw, Pinch, Ignored } touch_ = Touch::None;
  QPointF pinchCentre_;
  double pinchDist_ = 0;
  bool pinchStarted_ = true;
  std::deque<std::pair<double, QPointF>> panSamples_;   // (ms, viewport position) of the last ~120 ms of a pan: the flick speed
  QTimer inertia_;
  QPointF inertiaV_;                         // pixels per second
  double inertiaT_ = 0;
  double inertiaRemX_ = 0, inertiaRemY_ = 0;
};
