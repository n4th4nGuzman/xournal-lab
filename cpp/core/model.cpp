#include "model.h"
#include <algorithm>
#include <cmath>

const QString* attrFind(const Attrs& a, const QString& name) {
  for (const auto& kv : a)
    if (kv.first == name) return &kv.second;
  return nullptr;
}

QString attrOr(const Attrs& a, const QString& name, const QString& fallback) {
  const QString* v = attrFind(a, name);
  return v ? *v : fallback;
}

void attrSet(Attrs& a, const QString& name, const QString& value) {
  for (auto& kv : a)
    if (kv.first == name) { kv.second = value; return; }
  a.emplace_back(name, value);
}

/** A page size the layout can use: the attribute may be missing, text, NaN or absurd (the file keeps whatever it said; only what the app lays out is bounded). */
static double pageDimension(const Attrs& a, const QString& name, double fallback) {
  bool ok = false;
  const double v = attrOr(a, name).toDouble(&ok);
  if (!ok || !std::isfinite(v) || v <= 0) return fallback;
  return std::clamp(v, 1.0, kMaxPageSize);
}
double Page::width() const { return pageDimension(attrs, "width", 612); }
double Page::height() const { return pageDimension(attrs, "height", 792); }

bool operator==(const Stroke& a, const Stroke& b) { return a.attrs == b.attrs && a.widths == b.widths && a.pts == b.pts; }
bool operator==(const Text& a, const Text& b) { return a.attrs == b.attrs && a.text == b.text; }
bool operator==(const Image& a, const Image& b) { return a.tag == b.tag && a.attrs == b.attrs && a.data == b.data; }
bool operator==(const Layer& a, const Layer& b) { return a.attrs == b.attrs && a.elements == b.elements && a.extra == b.extra; }
bool operator==(const Page& a, const Page& b) { return a.attrs == b.attrs && a.background == b.background && a.layers == b.layers && a.extra == b.extra; }
bool operator==(const Document& a, const Document& b) {
  return a.rootAttrs == b.rootAttrs && a.title == b.title && a.preview == b.preview && a.pages == b.pages && a.extra == b.extra;
}

Counts countOf(const Document& d) {
  Counts c;
  c.pages = d.pages.size();
  for (const Page& p : d.pages) {
    c.layers += p.layers.size();
    for (const Layer& l : p.layers)
      for (const Element& e : l.elements) {
        if (const auto* s = std::get_if<Stroke>(&e)) { c.strokes++; c.points += s->pointCount(); }
        else if (std::holds_alternative<Text>(e)) c.texts++;
        else c.images++;
      }
  }
  return c;
}
