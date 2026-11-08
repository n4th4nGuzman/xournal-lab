#include "mathstate.h"
#include <cmath>

namespace mathobj {

State& state() {
  static State s;
  return s;
}

GenStyle styleFromPen(const Editor& ed) {
  const Style& s = ed.style();
  GenStyle g;
  g.color = s.color;
  g.width = s.penWidth;
  if (s.glowOn) g.glow = Glow{s.glowSize, s.glowIntensity};
  return g;
}

QString kindOf(const sets::MathSpec& spec) { return std::holds_alternative<sets::LineSpec>(spec) ? "line" : "venn"; }

QString specJson(const sets::MathSpec& spec) {
  if (const auto* l = std::get_if<sets::LineSpec>(&spec)) return sets::lineSpecJson(*l);
  return sets::vennSpecJson(std::get<sets::VennSpec>(spec));
}

namespace {

const Element& elementAt(const Editor& ed, const ElementRef& r) { return ed.doc().pages[size_t(r.page)].layers[size_t(r.layer)].elements[size_t(r.index)]; }

/** Every piece of object `id` in the layer, as pointers into it (valid until the layer changes). */
ElementPtrs piecesOf(const Editor& ed, int page, int layer, const QString& id) {
  ElementPtrs out;
  for (const Element& e : ed.doc().pages[size_t(page)].layers[size_t(layer)].elements)
    if (planeIdOf(e) == std::optional<QString>(id)) out.push_back(&e);
  return out;
}

}  // namespace

std::optional<Selected> selectedMath(Editor& ed) {
  const std::vector<ElementRef> items = ed.selection().items();
  if (items.empty()) return std::nullopt;
  const ElementRef& first = items.front();
  if (first.page < 0 || first.page >= ed.pageCount()) return std::nullopt;
  const Page& pg = ed.doc().pages[size_t(first.page)];
  if (first.layer < 0 || size_t(first.layer) >= pg.layers.size() || first.index < 0 || size_t(first.index) >= pg.layers[size_t(first.layer)].elements.size()) return std::nullopt;
  const auto id = planeIdOf(elementAt(ed, first));
  if (!id) return std::nullopt;
  for (const ElementRef& r : items) {
    if (r.page != first.page || r.layer != first.layer || r.index < 0 || size_t(r.index) >= pg.layers[size_t(r.layer)].elements.size()) return std::nullopt;
    if (planeIdOf(elementAt(ed, r)) != id) return std::nullopt;
  }
  const ElementPtrs members = piecesOf(ed, first.page, first.layer, *id);
  if (!sets::isMath(members)) return std::nullopt;
  auto read = sets::readMath(members);
  if (!read) return std::nullopt;
  return Selected{first.page, first.layer, *id, std::move(read->spec), read->style};
}

bool createMath(Editor& ed, int page, const sets::MathSpec& spec, const QString& toolId) {
  if (page < 0 || page >= ed.pageCount()) return false;
  const int layer = ed.activeLayer(page);
  if (!ed.layerVisible(page, layer)) { ed.notify("La capa activa está oculta: muéstrala para dibujar"); return false; }
  const QString id = newObjectId();
  const Editor::Swap sw = ed.swapGenerated(page, layer, "none", sets::buildMath(id, spec, styleFromPen(ed)));
  Action a;
  a.label = std::holds_alternative<sets::LineSpec>(spec) ? "Recta real" : "Diagrama de conjuntos";
  a.page = page;
  a.undo = sw.undo;
  a.redo = sw.redo;
  ed.pushAction(std::move(a));
  ed.afterCreate(toolId);
  ed.selection().select(page, ed.membersOf(page, layer, id));
  ed.notifySelectionChanged();
  if (state().openPanel) state().openPanel(toolId);
  return true;
}

bool updateMath(Editor& ed, const sets::MathSpec& spec, bool restyle) {
  const auto m = selectedMath(ed);
  if (!m) return false;
  const Editor::Swap sw = ed.swapGenerated(m->page, m->layer, m->id, sets::buildMath(m->id, sets::sanitizeMath(spec), restyle ? styleFromPen(ed) : m->style));
  Action a;
  a.label = std::holds_alternative<sets::LineSpec>(spec) ? "Editar recta" : "Editar diagrama";
  a.page = m->page;
  a.undo = sw.undo;
  a.redo = sw.redo;
  ed.pushAction(std::move(a));
  ed.selection().select(m->page, ed.membersOf(m->page, m->layer, m->id));
  ed.notifySelectionChanged();
  return true;
}

std::optional<int> mathAnchorAt(Editor& ed, QPointF p, double zoom) {
  const auto m = selectedMath(ed);
  if (!m) return std::nullopt;
  std::optional<int> best;
  double bd = 10 / zoom;
  const auto anchors = sets::mathAnchors(m->spec);
  for (size_t i = 0; i < anchors.size(); i++) {
    const double d = std::hypot(anchors[i][0] - p.x(), anchors[i][1] - p.y());
    if (d <= bd) { bd = d; best = int(i); }
  }
  return best;
}

}  // namespace mathobj
