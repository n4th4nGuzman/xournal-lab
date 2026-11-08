// Eraser: whole stroke ("eraser") or only the part under the circle ("eraserPart"). Candidates come from the page's spatial index, so an event costs
// what is under the pointer, not what is on the page. All the cuts of one event are applied in a single pass over the layer (Editor::applyReplacements);
// the page repaints coalesced; the whole gesture is one undo step.
#include "documentview.h"
#include "editor.h"
#include "geometry.h"
#include "icons.h"
#include "tools.h"
#include <algorithm>

namespace {

class EraserTool : public Tool {
 public:
  EraserTool(Editor& ed, bool partial) : Tool(ed), partial_(partial) {}

  QCursor cursor() const override { return Qt::BlankCursor; }   // the ring drawn by paintOverlay is the cursor
  void deactivate() override { moveRing(-1, {}); }
  void pointerLeft() override { moveRing(-1, {}); }

  void pointerDown(const PointerEvent& e) override {
    kind_ = e.kind;
    if (e.page < 0) return;
    page_ = e.page;
    steps_.clear();
    active_ = true;
    erase(e);
    moveRing(e.page, e.pos);
  }
  void pointerMove(const PointerEvent& e) override { if (active_) { erase(e); moveRing(e.page, e.pos); } }
  void hover(const PointerEvent& e) override { kind_ = e.kind; moveRing(e.page, e.pos); }
  void pointerUp(const PointerEvent& e) override {
    if (!active_) return;
    erase(e);
    active_ = false;
    if (steps_.empty()) return;
    Editor& ed = editor();
    const int page = page_;
    auto steps = std::make_shared<std::vector<Step>>(std::move(steps_));
    steps_.clear();
    Action a;
    a.label = QStringLiteral("Borrar");
    a.page = page;
    a.undo = [&ed, page, steps] { for (size_t k = steps->size(); k-- > 0;) ed.applyReplacements(page, (*steps)[k].layer, (*steps)[k].inverse); };
    a.redo = [&ed, page, steps] { for (const Step& s : *steps) ed.applyReplacements(page, s.layer, s.forward); };
    ed.pushAction(std::move(a));
  }
  void pointerCancel() override { active_ = false; steps_.clear(); }

  void paintOverlay(QPainter& p, const OverlayInfo& info) override {
    if (info.page != ringPage_) return;
    if (kind_ == PointerEvent::Kind::Eraser) {   // Preferencias > Lápiz: when the pen's eraser end shows its ring (never / always / only above the page / only touching)
      const int vis = editor().xpp().eraserVisibility;
      if (vis == 0 || (vis == 2 && active_) || (vis == 3 && !active_)) return;
    }
    const double r = editor().style().eraserRadius, z = info.zoom;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 160), 2.6 / z));
    p.drawEllipse(ringPos_, r, r);
    p.setPen(QPen(QColor(255, 255, 255, 230), 1.2 / z));
    p.drawEllipse(ringPos_, r, r);
  }

 private:
  struct Step { int layer; std::vector<Editor::Repl> forward, inverse; };

  void moveRing(int page, const QPointF& pos) {
    DocumentView* v = view();
    const double r = editor().style().eraserRadius;
    if (v && ringPage_ >= 0) v->invalidateOverlay(ringPage_, QRectF(ringPos_.x() - r - 2, ringPos_.y() - r - 2, 2 * r + 4, 2 * r + 4));
    ringPage_ = page;
    ringPos_ = pos;
    if (v && page >= 0) v->invalidateOverlay(page, QRectF(pos.x() - r - 2, pos.y() - r - 2, 2 * r + 4, 2 * r + 4));
  }

  void erase(const PointerEvent& e) {
    Editor& ed = editor();
    const double r = ed.style().eraserRadius, x = e.pos.x(), y = e.pos.y();
    std::vector<PageIndex::Hit> hits;
    ed.index(page_).queryPoint(e.pos, r + 0.5, hits);   // z order: layer, then position
    if (hits.empty()) return;
    const Page& pg = ed.doc().pages[size_t(page_)];
    int batchLayer = -1;   // one batch per layer touched; hits are ascending, so each batch is sorted by position
    std::vector<Editor::Repl> batch;
    auto flush = [&] {
      if (batch.empty()) return;
      Step s;
      s.layer = batchLayer;
      s.forward = batch;   // a copy: applyReplacements consumes its argument
      s.inverse = ed.applyReplacements(page_, batchLayer, std::move(batch));
      steps_.push_back(std::move(s));
      batch.clear();
    };
    for (const auto& [layer, idx] : hits) {
      if (layer != batchLayer) { flush(); batchLayer = layer; }
      if (!ed.layerVisible(page_, layer)) continue;
      const Stroke* s = std::get_if<Stroke>(&pg.layers[size_t(layer)].elements[size_t(idx)]);
      if (!s) continue;
      Editor::Repl rp;
      rp.index = idx;
      if (partial_) {
        auto cut = splitStroke(*s, x, y, r);
        if (!cut) continue;
        for (Stroke& piece : *cut) rp.with.emplace_back(std::move(piece));
      } else if (!strokeHit(*s, x, y, r + (s->widths.empty() ? 1.0 : s->widths[0]) / 2)) continue;
      batch.push_back(std::move(rp));
    }
    flush();
  }

  bool partial_, active_ = false;
  int page_ = 0, ringPage_ = -1;
  PointerEvent::Kind kind_ = PointerEvent::Kind::Mouse;
  QPointF ringPos_;
  std::vector<Step> steps_;
};

}  // namespace

void registerEraserTools(Editor& ed) {
  ToolInfo a;
  a.id = "eraser"; a.label = "Goma (trazo completo)"; a.tooltip = "Goma: borra el trazo entero"; a.shortcut = QKeySequence("E"); a.icon = Icons::make("eraser"); a.order = 2;
  a.factory = [](Editor& e) { return std::make_unique<EraserTool>(e, false); };
  ed.registerTool(a);
  ToolInfo b;
  b.id = "eraserPart"; b.label = "Borrar solo la zona que tocas"; b.tooltip = "Goma parcial: corta solo la parte tocada"; b.shortcut = QKeySequence("B"); b.icon = Icons::make("eraserPart"); b.order = 3;
  b.factory = [](Editor& e) { return std::make_unique<EraserTool>(e, true); };
  ed.registerTool(b);
}
