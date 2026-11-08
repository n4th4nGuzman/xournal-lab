#include "pageindex.h"
#include "geometry.h"
#include "pagerender.h"
#include <algorithm>
#include <climits>

void PageIndex::growPad(const Element& e) {
  double m = 0;
  if (const auto* s = std::get_if<Stroke>(&e)) {
    m = 2;   // strokeBounds: half the widest width (already in the box) plus 2, plus the reach of a glow
    if (const QString* g = attrFind(s->attrs, "glow")) m += std::min(40.0, std::max(0.0, g->toDouble())) * 1.6;
  } else {
    const QRectF r = elementRenderBounds(e);
    const Rect b = elementBBox(e);
    m = std::max({0.0, b[0] - r.left(), b[1] - r.top(), r.right() - b[2], r.bottom() - b[3]});
  }
  if (m > renderPad_) renderPad_ = m;
}

void PageIndex::rebuild(const Page& page) {
  grid_.clear();
  renderPad_ = 0;
  idAt_.assign(page.layers.size(), {});
  staleFrom_.assign(page.layers.size(), INT_MAX);
  loc_.clear();
  next_ = 0;
  for (size_t l = 0; l < page.layers.size(); l++) {
    const auto& els = page.layers[l].elements;
    idAt_[l].reserve(els.size());
    for (size_t i = 0; i < els.size(); i++) {
      const uint64_t id = next_++;
      idAt_[l].push_back(id);
      loc_.push_back({int(l), int(i)});
      grid_.insert(id, elementBBox(els[i]));
      growPad(els[i]);
    }
  }
  valid_ = true;
}

void PageIndex::renumber(int layer, int from) const {
  int& s = staleFrom_[size_t(layer)];
  s = std::min(s, from);
}

void PageIndex::settle() const {
  for (size_t l = 0; l < staleFrom_.size(); l++) {
    if (staleFrom_[l] == INT_MAX) continue;
    const auto& ids = idAt_[l];
    for (size_t j = size_t(staleFrom_[l]); j < ids.size(); j++) loc_[ids[j]] = {int(l), int(j)};
    staleFrom_[l] = INT_MAX;
  }
}

void PageIndex::inserted(const Page& page, int layer, int index) {
  if (!valid_) return;
  if (size_t(layer) >= idAt_.size()) { valid_ = false; return; }
  const uint64_t id = next_++;
  auto& ids = idAt_[size_t(layer)];
  ids.insert(ids.begin() + index, id);
  loc_.push_back({layer, index});
  renumber(layer, index + 1);
  grid_.insert(id, elementBBox(page.layers[size_t(layer)].elements[size_t(index)]));
  growPad(page.layers[size_t(layer)].elements[size_t(index)]);
}

void PageIndex::removed(int layer, int index) {
  if (!valid_) return;
  if (size_t(layer) >= idAt_.size() || size_t(index) >= idAt_[size_t(layer)].size()) { valid_ = false; return; }
  auto& ids = idAt_[size_t(layer)];
  grid_.remove(ids[size_t(index)]);
  ids.erase(ids.begin() + index);
  renumber(layer, index);
}

void PageIndex::replaced(const Page& page, int layer, int index) {
  if (!valid_) return;
  if (size_t(layer) >= idAt_.size() || size_t(index) >= idAt_[size_t(layer)].size()) { valid_ = false; return; }
  grid_.update(idAt_[size_t(layer)][size_t(index)], elementBBox(page.layers[size_t(layer)].elements[size_t(index)]));
  growPad(page.layers[size_t(layer)].elements[size_t(index)]);
}

void PageIndex::collect(const std::vector<uint64_t>& ids, std::vector<Hit>& out) const {
  settle();
  out.clear();
  out.reserve(ids.size());
  for (uint64_t id : ids) out.push_back(loc_[id]);
  std::sort(out.begin(), out.end());
}

void PageIndex::query(const QRectF& r, double pad, std::vector<Hit>& out) const {
  grid_.query({r.left() - pad, r.top() - pad, r.right() + pad, r.bottom() + pad}, scratch_);
  collect(scratch_, out);
}

void PageIndex::queryPoint(const QPointF& p, double pad, std::vector<Hit>& out) const {
  grid_.queryPoint(p.x(), p.y(), pad, scratch_);
  collect(scratch_, out);
}
