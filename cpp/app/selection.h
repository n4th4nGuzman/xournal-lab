// The selection the Editor owns. The real one (rectangle and lasso tools, handles, move/scale, clipboard) is in selection/: it subclasses Selection and is
// installed with Editor::setSelection(). Until then NullSelection answers "nothing selected" and the rest of the app can already call these.
#pragma once
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <vector>
#include "tool.h"

/** One element of the document: the page, the layer in it and the position in that layer. Positions shift when elements before it are inserted or erased. */
struct ElementRef {
  int page = 0, layer = 0, index = 0;
  bool operator==(const ElementRef&) const = default;
};

/** One run of a layer that an edit replaced: `removed` elements at `at` (position before the edit) became `inserted` ones. removed == inserted is an in-place edit. */
struct ElementSplice {
  int at = 0, removed = 0, inserted = 0;
};

class Selection {
 public:
  virtual ~Selection() = default;
  /** The Editor's element primitives report every edit of a layer here (ascending by `at`, positions before the edit), so references follow the elements instead of guessing. */
  virtual void onSplices(int page, int layer, const std::vector<ElementSplice>& splices) { (void)page; (void)layer; (void)splices; }
  virtual bool isEmpty() const = 0;
  virtual void clear() = 0;
  /** Replace the selection by these elements of one page (what afterCreate uses for the object that was just made). */
  virtual void select(int page, const std::vector<ElementRef>& items) = 0;
  virtual bool contains(const ElementRef& e) const = 0;
  virtual std::vector<ElementRef> items() const = 0;
  virtual int page() const = 0;                 // -1 when empty
  virtual QRectF bbox() const = 0;              // page units, null when empty
  /** Would a click here start working on the selection (grab a handle, drag the selected items)? A click on the empty page is what returns to the tool before. */
  virtual bool wouldHit(int page, const QPointF& pos, double zoom) const = 0;
  /** Called after undo/redo and structural edits: drop references that no longer exist, recompute the box. */
  virtual void refresh() = 0;
  /** Frame, handles... drawn in the same overlay pass as the tool's. */
  virtual void paintOverlay(QPainter&, const OverlayInfo&) = 0;
};

class NullSelection final : public Selection {
 public:
  bool isEmpty() const override { return true; }
  void clear() override {}
  void select(int, const std::vector<ElementRef>&) override {}
  bool contains(const ElementRef&) const override { return false; }
  std::vector<ElementRef> items() const override { return {}; }
  int page() const override { return -1; }
  QRectF bbox() const override { return {}; }
  bool wouldHit(int, const QPointF&, double) const override { return false; }
  void refresh() override {}
  void paintOverlay(QPainter&, const OverlayInfo&) override {}
};
