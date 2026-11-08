// Spatial index of one page that survives insertions and removals: ElementIndex (core/spatial.h) answers "which boxes are near here" by stable ids, and
// this class keeps the map from those ids to the elements' current (layer, position), which shifts whenever an element before them comes or goes.
// Renumbering is a loop over plain ints (microseconds for 20000 elements); the grid itself is never rebuilt after an edit.
#pragma once
#include "spatial.h"
#include <QPointF>
#include <QRectF>
#include <utility>
#include <vector>

class PageIndex {
 public:
  using Hit = std::pair<int, int>;   // layer, position in the layer

  void invalidate() { valid_ = false; }
  bool valid() const { return valid_; }
  /** Rebuilds when invalid (O(elements), once after a wholesale change). */
  void ensure(const Page& page) { if (!valid_) rebuild(page); }
  void rebuild(const Page& page);

  /** The element is now at (layer, index): later ones in the layer moved up by one. */
  void inserted(const Page& page, int layer, int index);
  /** The element that was at (layer, index) is gone: later ones moved down by one. */
  void removed(int layer, int index);
  /** The element at (layer, index) changed shape. */
  void replaced(const Page& page, int layer, int index);

  /** How far any element's drawing can reach beyond the box the grid knows (line caps, glow, a turned formula, markdown texts: unbounded). Pass it as the
   *  `pad` of query() to get every element a repaint of that area must draw. Only grows between rebuilds. */
  double renderPad() const { return renderPad_; }

  /** Elements whose box (grown by `pad`) meets the rectangle, in z order (layer, then position). Needs a valid index. */
  void query(const QRectF& r, double pad, std::vector<Hit>& out) const;
  void queryPoint(const QPointF& p, double pad, std::vector<Hit>& out) const;

 private:
  void renumber(int layer, int from) const;   // lazy: edits only note where the positions went stale; queries fix them once
  void settle() const;
  void collect(const std::vector<uint64_t>& ids, std::vector<Hit>& out) const;
  void growPad(const Element& e);
  bool valid_ = false;
  double renderPad_ = 0;
  ElementIndex grid_;
  std::vector<std::vector<uint64_t>> idAt_;   // per layer, parallel to its elements
  mutable std::vector<Hit> loc_;              // by id
  mutable std::vector<int> staleFrom_;        // per layer: first position whose loc_ is out of date (INT_MAX = none)
  uint64_t next_ = 0;
  mutable std::vector<uint64_t> scratch_;
};
