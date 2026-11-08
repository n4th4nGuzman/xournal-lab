#include "repair.h"
#include "figures.h"
#include "genobj.h"
#include "plane.h"
#include "space3.h"
#include <QSet>

bool lacksPlace(const Element& e) {
  const auto* t = std::get_if<Text>(&e);
  return t && !attrFind(t->attrs, "x") && !attrFind(t->attrs, "y");
}

static int repairLayer(Layer& layer, const std::vector<Rebuilder>& rebuilders) {
  QStringList broken;
  QSet<QString> seen;
  for (const Element& e : layer.elements) {
    const auto id = planeIdOf(e);
    if (id && lacksPlace(e) && !seen.contains(*id)) { seen.insert(*id); broken << *id; }
  }
  int fixed = 0;
  for (const QString& id : broken) {
    std::vector<const Element*> members;
    int at = -1;
    for (size_t i = 0; i < layer.elements.size(); i++) {
      const auto pid = planeIdOf(layer.elements[i]);
      if (pid && *pid == id) {
        if (at < 0) at = int(i);
        members.push_back(&layer.elements[i]);
      }
    }
    std::optional<std::vector<Element>> fresh;
    for (const Rebuilder& r : rebuilders) {
      const Rebuild res = r(id, members);
      if (!res.claimed) continue;
      fresh = res.built;
      break;
    }
    if (!fresh || fresh->empty()) continue;
    std::vector<Element> next;
    next.reserve(layer.elements.size() + fresh->size());
    for (int i = 0; i < int(layer.elements.size()); i++) {
      if (i == at) for (Element& e : *fresh) next.push_back(std::move(e));
      const auto pid = planeIdOf(layer.elements[i]);
      if (!(pid && *pid == id)) next.push_back(std::move(layer.elements[i]));
    }
    layer.elements = std::move(next);
    fixed++;
  }
  return fixed;
}

int repairGenerated(Document& doc, const std::vector<Rebuilder>& rebuilders) {
  int fixed = 0;
  for (Page& page : doc.pages)
    for (Layer& layer : page.layers) fixed += repairLayer(layer, rebuilders);
  return fixed;
}

std::vector<Rebuilder> standardRebuilders() {
  std::vector<Rebuilder> r;
  r.push_back([](const QString& id, const Members& m) {
    Rebuild out;
    const auto spec = planeSpecOf(m);
    const auto frame = planeFrameOf(m);
    const auto style = planeStyleOf(m);
    if (spec && frame && style) { out.claimed = true; out.built = buildPlane(id, *spec, *frame, *style); }
    return out;
  });
  r.push_back([](const QString& id, const Members& m) {
    Rebuild out;
    if (!isSpace3(m)) return out;
    out.claimed = true;
    const auto spec = space3SpecOf(m);
    const auto style = space3StyleOf(m);
    if (spec && style) out.built = buildSpace3(id, *spec, *style);
    return out;
  });
  r.push_back([](const QString& id, const Members& m) {
    Rebuild out;
    if (!isFigure(m)) return out;
    out.claimed = true;
    if (const auto f = figureSpecOf(m)) out.built = buildFigure(id, f->spec, f->style);
    return out;
  });
  return r;
}
