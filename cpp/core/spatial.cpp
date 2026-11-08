#include "spatial.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr int64_t kMaxCells = 400;   // an element covering more cells than this is kept in the always-checked list
}

ElementIndex::ElementIndex(double cell) : cell_(cell > 1 ? cell : 64.0) {}

void ElementIndex::clear() {
  entries_.clear();
  free_.clear();
  slotOf_.clear();
  cells_.clear();
  wide_.clear();
}

ElementIndex::Span ElementIndex::cellsOf(const Rect& r) const {
  auto cl = [](double v) { return int64_t(std::max(-1e9, std::min(1e9, std::floor(v)))); };
  return {cl(r[0] / cell_), cl(r[1] / cell_), cl(r[2] / cell_), cl(r[3] / cell_)};
}

void ElementIndex::rebuild(const Page& page) {
  clear();
  size_t total = 0;
  for (const Layer& l : page.layers) total += l.elements.size();
  entries_.reserve(total);
  slotOf_.reserve(total);
  for (size_t li = 0; li < page.layers.size(); li++) {
    const auto& els = page.layers[li].elements;
    for (size_t i = 0; i < els.size(); i++) insert(elementId(uint32_t(li), uint32_t(i)), elementBBox(els[i]));
  }
}

void ElementIndex::insert(uint64_t id, const Rect& box) {
  if (slotOf_.count(id)) remove(id);
  uint32_t slot;
  if (!free_.empty()) { slot = free_.back(); free_.pop_back(); }
  else { slot = uint32_t(entries_.size()); entries_.emplace_back(); }
  Entry& e = entries_[slot];
  e.id = id; e.box = box; e.live = true; e.stamp = 0;
  slotOf_[id] = slot;
  const bool finite = std::isfinite(box[0]) && std::isfinite(box[1]) && std::isfinite(box[2]) && std::isfinite(box[3]);
  Span c{};
  if (finite) c = cellsOf(box);
  e.wide = !finite || (c[2] - c[0] + 1) * (c[3] - c[1] + 1) > kMaxCells;
  if (e.wide) { wide_.push_back(slot); return; }
  for (int64_t y = c[1]; y <= c[3]; y++)
    for (int64_t x = c[0]; x <= c[2]; x++) cells_[key(x, y)].push_back(slot);
}

void ElementIndex::unlink(uint32_t slot) {
  Entry& e = entries_[slot];
  if (e.wide) {
    wide_.erase(std::remove(wide_.begin(), wide_.end(), slot), wide_.end());
    return;
  }
  const Span c = cellsOf(e.box);
  for (int64_t y = c[1]; y <= c[3]; y++)
    for (int64_t x = c[0]; x <= c[2]; x++) {
      auto it = cells_.find(key(x, y));
      if (it == cells_.end()) continue;
      auto& v = it->second;
      auto p = std::find(v.begin(), v.end(), slot);
      if (p != v.end()) { *p = v.back(); v.pop_back(); }
      if (v.empty()) cells_.erase(it);
    }
}

bool ElementIndex::remove(uint64_t id) {
  auto it = slotOf_.find(id);
  if (it == slotOf_.end()) return false;
  const uint32_t slot = it->second;
  unlink(slot);
  entries_[slot].live = false;
  free_.push_back(slot);
  slotOf_.erase(it);
  return true;
}

void ElementIndex::query(const Rect& r, std::vector<uint64_t>& out) const {
  out.clear();
  if (!(r[0] <= r[2] && r[1] <= r[3])) return;
  if (++stampCounter_ == 0) {   // wrapped: stamps from long ago could collide
    for (const Entry& e : entries_) e.stamp = 0;
    stampCounter_ = 1;
  }
  const uint32_t st = stampCounter_;
  auto visit = [&](uint32_t slot) {
    const Entry& e = entries_[slot];
    if (e.stamp == st) return;
    e.stamp = st;
    if (rectsIntersect(e.box, r)) out.push_back(e.id);
  };
  const Span c = cellsOf(r);
  const double span = double(c[2] - c[0] + 1) * double(c[3] - c[1] + 1);
  if (span > double(cells_.size())) {
    // The area covers more cells than exist: walking the occupied ones is cheaper.
    for (const auto& kv : cells_) for (uint32_t slot : kv.second) visit(slot);
  } else {
    for (int64_t y = c[1]; y <= c[3]; y++)
      for (int64_t x = c[0]; x <= c[2]; x++) {
        auto it = cells_.find(key(x, y));
        if (it == cells_.end()) continue;
        for (uint32_t slot : it->second) visit(slot);
      }
  }
  for (uint32_t slot : wide_) visit(slot);
  std::sort(out.begin(), out.end());
}

void ElementIndex::queryPoint(double x, double y, double pad, std::vector<uint64_t>& out) const {
  query({x - pad, y - pad, x + pad, y + pad}, out);
}
