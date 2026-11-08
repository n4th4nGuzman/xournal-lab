// The real Selection of the editor: click / rectangle / lasso selection through the page index, moving, resizing, turning and mirroring (with the
// protractor), generated-object anchors, clipboard, delete, z-order, glow and vertical space. Port of selection.ts plus the selection parts of
// editor.ts/app.ts. The gesture state lives here (like the TS class), the tools in tools.cpp only forward the pointer to it.
//
// Undo model: every operation is one Action made of "replace these positions of this layer by those elements" sets (Editor::applyReplacements), never
// pointers into the document. While a transform is dragged the selected elements are lifted out of the document and drawn from a cached pixmap
// through the current map (committed once on release), so moving thousands of strokes costs one bitmap blit per event.
#pragma once
#include "editor.h"
#include "generated.h"
#include "genobj.h"
#include "geometry.h"
#include <QCursor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace selx {

/** layer -> replacements of that layer (positions as Editor::applyReplacements wants them). */
using ReplSet = std::vector<std::pair<int, std::vector<Editor::Repl>>>;

enum class Handle { None, NW, N, NE, E, SE, S, SW, W, Rot };
enum class Quick { RotL, RotR, FlipH, FlipV };

constexpr double kHandlePx = 8;       // drawn size and grab tolerance, screen pixels
constexpr double kRotOffsetPx = 26;   // rotation handle distance above the frame
constexpr double kMinScale = 0.05;
constexpr double kSnapStep = 3.14159265358979323846 / 12;   // 15 degrees with Shift
constexpr double kProtractorMinRadiusPx = 70, kProtractorArc = 0.55;

class RealSelection : public Selection {
 public:
  explicit RealSelection(Editor& ed);
  ~RealSelection() override;
  static RealSelection* of(Editor& ed) { return dynamic_cast<RealSelection*>(&ed.selection()); }

  // ---- Selection interface -------------------------------------------------------------------------------------------------------------
  bool isEmpty() const override { return items_.empty(); }
  void clear() override;
  void select(int page, const std::vector<ElementRef>& items) override;
  bool contains(const ElementRef& e) const override;
  std::vector<ElementRef> items() const override { return items_; }
  int page() const override { return items_.empty() ? -1 : page_; }
  QRectF bbox() const override;
  bool wouldHit(int page, const QPointF& pos, double zoom) const override;
  void refresh() override;
  void onSplices(int page, int layer, const std::vector<ElementSplice>& splices) override;
  void paintOverlay(QPainter& p, const OverlayInfo& info) override;

  // ---- gestures (the tools forward here) -----------------------------------------------------------------------------------------------
  /** Press with the select or lasso tool. `newSelection` (Shift) skips the frame so a new selection can start inside the old one. */
  void down(const PointerEvent& e, bool lasso, bool newSelection);
  void startVspace(const PointerEvent& e);
  void move(const PointerEvent& e);
  void up(const PointerEvent& e);
  void cancel();
  bool dragging() const { return drag_.mode != Mode::None; }
  /** Cursor to show while hovering (handles, body) at a point of a page. */
  QCursor cursorAt(int page, const QPointF& pos, double zoom) const;
  /** The preview of the running gesture (rubber band, lasso, lifted pixmap, protractor, rebuilt anchor drag). */
  void paintDrag(QPainter& p, const OverlayInfo& info);

  // ---- operations ----------------------------------------------------------------------------------------------------------------------
  void selectAll(int page);
  bool copy();
  void cut();
  bool paste();
  void duplicate();
  void deleteSelected();
  void reorder(bool front);
  void quick(Quick kind);
  /** Move the selection by (dx, dy) page units as one undo step (arrow keys). */
  void nudge(double dx, double dy);
  /** Scroll the view so the selection frame is on screen (programmatic selections: paste, undo, arrow keys). Does nothing when it already is, or during a gesture. */
  void reveal();
  /** Add (or with nullopt remove) a glow on the selected strokes (not highlighters). Returns how many were changed. */
  int setGlow(const std::optional<Glow>& g);

  /** The generated object that is the whole selection, when a handler claims it (kind id, else empty). */
  QString generatedKind() const { return gen_.h ? gen_.h->kind() : QString(); }
  const std::vector<QPointF>& anchors() const { return anchors_; }
  int count() const { return int(items_.size()); }

 private:
  enum class Mode { None, Rect, Lasso, Move, Resize, Rotate, Anchor };

  struct Lift {
    bool done = false;
    Rect rbox{};                 // area the bitmap covers (page units, padded for glow)
    QPixmap bitmap;
    ReplSet inverse;             // re-inserts what was lifted (the elements are in `with`, in selection order)
  };
  struct Drag {
    Mode mode = Mode::None;
    int page = -1;
    double zoom = 1;
    QPointF down;
    double x1 = 0, y1 = 0;       // rect: far corner
    std::vector<double> poly;    // lasso
    Rect polyBox{};
    Handle handle = Handle::None;
    double ax = 0, ay = 0;       // resize: fixed point
    bool uniform = false;
    Rect bbox{};                 // selection box when the gesture started
    Aff m;                       // current map
    double cx = 0, cy = 0, a0 = 0, prev = 0, total = 0, shown = 0, r0px = 0;
    bool snapped = false;
    bool vspace = false;
    double vy = 0, minDy = 0, maxDy = 0, pageW = 0;
    int anchorI = -1;
    std::optional<std::vector<Element>> cur;   // anchor drag: what the handler rebuilt
    QRectF dirty;                // last area asked to repaint
    bool moved = false;
    Lift lift;
  };
  struct Gen { const GeneratedHandler* h = nullptr; QString id; int layer = -1; };

  // geometry of the frame
  Handle handleAt(double x, double y, const Rect& b, double zoom) const;
  int anchorAt(double x, double y, double zoom) const;
  QRectF frameArea(const Rect& b, double zoom) const;
  const Rect& box() const;
  void dirtyBox() { boxValid_ = false; }

  // selection state
  void setItems(int page, std::vector<ElementRef> items, bool expandPlanes);
  void recompute();
  void changed(const QRectF& oldArea);
  void invalidate(int page, const QRectF& r) const;
  void expandPlanes(int page, std::vector<ElementRef>& items) const;
  const Element& elementAt(const ElementRef& r) const;
  Element& elementAt(const ElementRef& r);
  static uint64_t fingerprint(const Element& e);

  // finding things
  void finishRect(const Drag& d, double x, double y);
  void finishLasso(const Drag& d);
  void setFound(int page, std::vector<ElementRef> found);

  // lifting and committing
  void lift(Drag& d, const Rect& bbox);
  void restoreLift(Drag& d);
  void updateTransformPreview(Drag& d, const PointerEvent& e);
  void finishTransform(Drag& d);
  void finishAnchor(Drag& d);
  QRectF previewArea(const Drag& d) const;
  void startMove(const PointerEvent& e, const Rect& b);
  void startResize(const PointerEvent& e, Handle h, const Rect& b);
  void startRotate(const PointerEvent& e, const Rect& b);
  void startAnchor(const PointerEvent& e, int i);

  // history
  void push(const QString& label, ReplSet fwd, ReplSet inv, std::optional<Editor::Swap> sw = std::nullopt, std::function<void(bool undo)> after = nullptr);
  void pushShared(const QString& label, std::shared_ptr<ReplSet> fwd, std::shared_ptr<ReplSet> inv, std::optional<Editor::Swap> sw, std::function<void(bool undo)> after);
  ReplSet sameIndexSet(std::vector<Element> after) const;
  void afterInPlaceEdit(const QRectF& oldFrame);
  void performAndPush(const QString& label, ReplSet fwd, std::function<void(bool undo)> after = nullptr);
  void editSameIndex(const QString& label, std::vector<Element> after);
  std::optional<Editor::Swap> rebuildGenerated(int page);

  void drawProtractor(QPainter& p, const Drag& d, double zoom) const;
  void startGhost(int page, QPixmap pm, const Rect& rbox, const Aff& m, const QRectF& area);
  void stopGhost();

  Editor& ed_;
  int page_ = -1;
  std::vector<ElementRef> items_;       // sorted by (layer, index); one page
  std::vector<uint64_t> fp_;            // parallel to items_: what identified the element when it was selected
  std::set<std::pair<int, QString>> planes_;   // (layer, xlPlane id) of the selected pieces: re-found after undo/redo moved positions
  mutable bool boxValid_ = false;
  mutable Rect box_{};
  Gen gen_;
  std::vector<QPointF> anchors_;
  Drag drag_;
  // After a commit the tiles under the moved elements are re-rendered a moment later; until then the lifted pixmap stays on the overlay at its
  // final place, so the elements do not blink out while the tiles catch up.
  struct Ghost { int page = -1; QPixmap pm; Rect rbox{}; Aff m; QRectF area; };
  Ghost ghost_;
  std::unique_ptr<QTimer> ghostTimer_;
  QElapsedTimer ghostAge_;
  // clipboard bookkeeping: what the last copy wrote, so a paste knows whether it is the same data and how many times it was pasted
  double grab_ = 1;                     // size of the handles' grab area relative to a mouse: 1.5 for a pen, 2.5 for a finger
  bool own_ = false;                    // this class is lifting / putting back its own elements: the splices it causes are not edits of someone else
  struct Nudge { std::shared_ptr<ReplSet> fwd, inv; int depth = 0, mutations = 0; std::vector<ElementRef> items; QElapsedTimer age; };
  std::optional<Nudge> nudge_;          // the arrow-key step on top of the undo stack: further steps within a moment fold into it
    QByteArray lastCopy_;
  int clipPage_ = -1, pasteCount_ = 0;
};

}  // namespace selx
