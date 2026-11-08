#include "find.h"
#include "genobj.h"
#include "markdown.h"
#include "pagerender.h"
#include <algorithm>
#include <cmath>

std::optional<QString> searchableText(const Element& e) {
  if (const auto* t = std::get_if<Text>(&e)) return t->text;
  if (const auto* im = std::get_if<Image>(&e))
    if (im->tag == "teximage") return attrOr(im->attrs, "text");
  return std::nullopt;
}

std::vector<FindHit> findText(const Document& doc, const QString& query, const FindOptions& opt) {
  std::vector<FindHit> hits;
  if (query.isEmpty()) return hits;
  const Qt::CaseSensitivity cs = opt.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
  for (int p = 0; p < int(doc.pages.size()); p++) {
    const auto& layers = doc.pages[size_t(p)].layers;
    for (int l = 0; l < int(layers.size()); l++) {
      if (opt.skipLayer && opt.skipLayer(p, l)) continue;
      const auto& els = layers[size_t(l)].elements;
      for (int i = 0; i < int(els.size()); i++) {
        const auto text = searchableText(els[size_t(i)]);
        if (!text) continue;
        FindHit h;
        for (qsizetype at = text->indexOf(query, 0, cs); at >= 0; at = text->indexOf(query, at + query.size(), cs)) h.positions.push_back(int(at));
        if (h.positions.empty()) continue;
        h.page = p; h.layer = l; h.element = i; h.length = int(query.size());
        hits.push_back(std::move(h));
      }
    }
  }
  return hits;
}

std::optional<std::array<double, 4>> findHitBox(const Document& doc, const FindHit& h) {
  if (h.page < 0 || h.page >= int(doc.pages.size())) return std::nullopt;
  const auto& layers = doc.pages[size_t(h.page)].layers;
  if (h.layer < 0 || h.layer >= int(layers.size())) return std::nullopt;
  const auto& els = layers[size_t(h.layer)].elements;
  if (h.element < 0 || h.element >= int(els.size())) return std::nullopt;
  const Element& e = els[size_t(h.element)];
  if (const auto* t = std::get_if<Text>(&e)) return textBBox(*t);
  if (const auto* im = std::get_if<Image>(&e)) {
    const double l = attrOr(im->attrs, "left").toDouble(), t = attrOr(im->attrs, "top").toDouble(), r = attrOr(im->attrs, "right").toDouble(),
                 b = attrOr(im->attrs, "bottom").toDouble();
    bool ok = false;
    double rot = attrOr(im->attrs, "xlRot").toDouble(&ok);
    if (!ok || !std::isfinite(rot) || im->tag != "teximage") rot = 0;
    if (rot == 0) return std::array<double, 4>{l, t, r, b};
    const double cx = (l + r) / 2, cy = (t + b) / 2, hw = (r - l) / 2, hh = (b - t) / 2;
    const double c = std::fabs(std::cos(rot)), s = std::fabs(std::sin(rot));
    const double w = hw * c + hh * s, hgt = hw * s + hh * c;
    return std::array<double, 4>{cx - w, cy - hgt, cx + w, cy + hgt};
  }
  return std::nullopt;
}

std::vector<std::array<double, 4>> findSpanBoxes(const Document& doc, const FindHit& h) {
  std::vector<std::array<double, 4>> out;
  if (h.page < 0 || h.page >= int(doc.pages.size())) return out;
  const auto& layers = doc.pages[size_t(h.page)].layers;
  if (h.layer < 0 || h.layer >= int(layers.size())) return out;
  const auto& els = layers[size_t(h.layer)].elements;
  if (h.element < 0 || h.element >= int(els.size())) return out;
  const auto* t = std::get_if<Text>(&els[size_t(h.element)]);
  if (!t) return out;
  if (isMarkdown(*t)) {   // the source offsets include the markup: look for the matched text in what is drawn instead
    const double x = attrOr(t->attrs, "x").toDouble(), y = attrOr(t->attrs, "y").toDouble();
    QStringList needles;
    for (int pos : h.positions) { const QString n = t->text.mid(pos, h.length); if (!needles.contains(n)) needles << n; }
    for (const QString& n : needles) {
      const auto boxes = markdownSpanBoxes(t->text, attrOr(t->attrs, "size", "12").toDouble(), attrOr(t->attrs, "font", "Sans"), n);
      if (boxes.empty()) return {};
      for (const auto& b : boxes) out.push_back({x + b[0], y + b[1], x + b[2], y + b[3]});
    }
    return out;
  }
  for (int pos : h.positions) {
    const auto boxes = textSpanBoxes(*t, pos, h.length);
    if (boxes.empty()) return {};   // one match that cannot be placed: all-or-nothing, so the highlight is never half exact
    out.insert(out.end(), boxes.begin(), boxes.end());
  }
  return out;
}

void FindState::run(const Document& doc, const QString& query, const FindOptions& opt) {
  hits = findText(doc, query, opt);
  index = hits.empty() ? -1 : 0;
}

const FindHit* FindState::step(int delta) {
  if (hits.empty()) return nullptr;
  const int n = int(hits.size());
  index = ((index + delta) % n + n) % n;
  return &hits[size_t(index)];
}
