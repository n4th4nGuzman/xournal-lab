// Rendered page tiles in a bounded LRU cache. A tile is a square piece (kTile device pixels) of one page at one scale; the DocumentView fills the
// cache lazily with renderPage and draws from it. Scales are compared by their key (scale * 10000, rounded), never as raw doubles.
#pragma once
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

constexpr int kTile = 512;   // device pixels

struct TileKey {
  int page = 0, scaleKey = 0, tx = 0, ty = 0;
  bool operator==(const TileKey&) const = default;
};
struct TileKeyHash {
  size_t operator()(const TileKey& k) const {
    uint64_t h = uint64_t(uint32_t(k.page)) * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t(uint32_t(k.scaleKey)) << 1) + 0x7F4A7C15 + (h << 6) + (h >> 2);
    h ^= (uint64_t(uint32_t(k.tx)) << 20) ^ (uint64_t(uint32_t(k.ty)) << 40) ^ (h >> 7);
    return size_t(h);
  }
};

struct Tile {
  QPixmap pm;                  // device pixels (devicePixelRatio 1)
  bool lite = false;           // rendered rough while scrolling/zooming: re-render sharp when things settle
  bool dirty = false;          // content changed since it was rendered
  QRect dirtyDev;              // while dirty: the changed part in tile pixels (null = all of it); only that part is drawn again
  uint64_t stamp = 0;          // last use, for the LRU
};

/** The page rectangle (page units) a tile covers at its scale. */
inline QRectF tilePageRect(const TileKey& k) {
  const double s = k.scaleKey / 10000.0;
  return QRectF(k.tx * kTile / s, k.ty * kTile / s, kTile / s, kTile / s);
}

class TileCache {
 public:
  explicit TileCache(size_t budgetBytes = size_t(320) << 20) : budget_(budgetBytes) {}
  void setBudget(size_t bytes) { budget_ = bytes; }
  size_t bytes() const { return bytes_; }
  size_t count() const { return map_.size(); }

  Tile* find(const TileKey& k) {
    auto it = map_.find(k);
    if (it == map_.end()) return nullptr;
    it->second.stamp = ++clock_;
    return &it->second;
  }
  const Tile* peek(const TileKey& k) const { auto it = map_.find(k); return it == map_.end() ? nullptr : &it->second; }
  Tile& put(const TileKey& k, QPixmap pm, bool lite) {
    Tile& t = map_[k];
    bytes_ -= bytesOf(t);
    t.pm = std::move(pm);
    t.lite = lite;
    t.dirty = false;
    t.dirtyDev = QRect();
    t.stamp = ++clock_;
    bytes_ += bytesOf(t);
    evict(k);
    return t;
  }
  void erase(const TileKey& k) {
    auto it = map_.find(k);
    if (it == map_.end()) return;
    bytes_ -= bytesOf(it->second);
    map_.erase(it);
  }
  void clear() { map_.clear(); bytes_ = 0; }
  template <class F> void forEach(F&& f) { for (auto& kv : map_) f(kv.first, kv.second); }
  /** Keys matching a predicate (collected first so the caller may erase while iterating the result). */
  template <class P> std::vector<TileKey> keysWhere(P&& pred) const {
    std::vector<TileKey> v;
    for (const auto& kv : map_) if (pred(kv.first, kv.second)) v.push_back(kv.first);
    return v;
  }
  /** Tiles used within the last `n` lookups are never evicted (they are on screen). */
  void setProtectWindow(uint64_t n) { protect_ = n; }

 private:
  static size_t bytesOf(const Tile& t) { return size_t(t.pm.width()) * size_t(t.pm.height()) * 4; }
  void evict(const TileKey& keep) {
    if (bytes_ <= budget_) return;
    std::vector<std::pair<uint64_t, TileKey>> order;
    order.reserve(map_.size());
    for (const auto& kv : map_) order.push_back({kv.second.stamp, kv.first});
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [stamp, key] : order) {
      if (bytes_ <= budget_ * 9 / 10) break;
      if (key == keep || clock_ - stamp < protect_) continue;
      erase(key);
    }
  }
  std::unordered_map<TileKey, Tile, TileKeyHash> map_;
  size_t bytes_ = 0, budget_;
  uint64_t clock_ = 0, protect_ = 64;
};
