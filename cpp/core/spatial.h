// Uniform-grid spatial index of the elements of one page, so that hit testing, rectangle selection, the eraser and repaint clipping touch only the
// few elements near the pointer instead of all of them (the TS did an O(n) scan of every layer on each event).
//
// Use: one ElementIndex per page. Build it once when the page is loaded or changed wholesale (rebuild(page)), then keep it current with
// insert/update/remove as elements are added, edited or erased. On a pointer event, query() or queryPoint() returns candidate ids whose bounding
// box overlaps the area; run the exact test (hitElement, inRect, strokeHit ...) on those candidates only. Candidates are a superset: the index
// knows boxes, not shapes. Results are sorted by id, which for rebuild(page) ids is z-order (layer, then position in the layer).
#pragma once
#include "geometry.h"
#include "model.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

/** Id of an element as rebuild(page) assigns it: layer index in the high half, position in the layer in the low half. */
constexpr uint64_t elementId(uint32_t layer, uint32_t index) { return (uint64_t(layer) << 32) | index; }
constexpr uint32_t idLayer(uint64_t id) { return uint32_t(id >> 32); }
constexpr uint32_t idIndex(uint64_t id) { return uint32_t(id & 0xffffffffu); }

class ElementIndex {
 public:
  explicit ElementIndex(double cell = 64.0);

  void clear();
  /** Index every element of every layer of the page (ids = elementId(layer, index)). Replaces the previous content. */
  void rebuild(const Page& page);
  /** Add an element by id and box (replaces the box when the id is already there). Non-finite boxes are kept in the "everywhere" list. */
  void insert(uint64_t id, const Rect& box);
  void update(uint64_t id, const Rect& box) { insert(id, box); }
  /** Returns false when the id was not indexed. */
  bool remove(uint64_t id);
  bool contains(uint64_t id) const { return slotOf_.count(id) != 0; }
  size_t size() const { return slotOf_.size(); }

  /** Ids whose box intersects `r`, ascending, appended to `out` (cleared first). */
  void query(const Rect& r, std::vector<uint64_t>& out) const;
  /** Ids whose box, grown by pad, contains (x, y): the candidates of a hit test with that padding. */
  void queryPoint(double x, double y, double pad, std::vector<uint64_t>& out) const;

 private:
  struct Entry { uint64_t id = 0; Rect box{}; bool live = false; bool wide = false; mutable uint32_t stamp = 0; };
  using Span = std::array<int64_t, 4>;   // cell range x0, y0, x1, y1
  Span cellsOf(const Rect& r) const;
  static uint64_t key(int64_t cx, int64_t cy) { return (uint64_t(uint32_t(int32_t(cx))) << 32) | uint32_t(int32_t(cy)); }
  void unlink(uint32_t slot);

  double cell_;
  std::vector<Entry> entries_;
  std::vector<uint32_t> free_;
  std::unordered_map<uint64_t, uint32_t> slotOf_;
  std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
  std::vector<uint32_t> wide_;           // elements covering too many cells (big pictures, huge strokes): checked on every query
  mutable uint32_t stampCounter_ = 0;
};
