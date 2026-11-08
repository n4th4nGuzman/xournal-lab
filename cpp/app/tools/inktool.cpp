// Pen and highlighter: the stroke pipeline of the prototype (live layer while drawing, one cached-tile patch when the pen lifts), with the width of each
// point from core/brush.h (tip, pressure curve) and the finished stroke stored as an ordinary Xournal++ stroke with its per-point width list.
// The plain pen uses the prototype's width mapping by default (TabletSettings::penLook; PenLook::Electron brings the Electron app's back). The live layer
// and the finished stroke draw the same pieces with the same widths (liveink.h), so lifting the pen changes nothing on screen.
#include "brush.h"
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "geometry.h"
#include "icons.h"
#include "liveink.h"
#include "pencursor.h"
#include "tools.h"
#include <cmath>
#include <optional>

namespace {

class InkTool : public Tool {
 public:
  InkTool(Editor& ed, bool highlighter) : Tool(ed), highlighter_(highlighter) {}

  /** A disc as wide as the stroke at this zoom, in the tool colour (the preferences may replace it: Preferencias > Ver). */
  QCursor cursor() const override {
    const DocumentView* v = view();
    const Style& st = editor().style();
    QColor c(st.color);
    if (highlighter_) c.setAlpha(std::max(90, int(alphaOf(st.highlighterAlpha) * 255)));
    const double d = v ? v->devicePixelRatioF() : 1.0;
    return pencursor::disc(c, pencursor::diameterFor(highlighter_ ? st.highlighterWidth : st.penWidth, v ? v->zoom() : 1.0), d);
  }

  void pointerDown(const PointerEvent& e) override {
    DocumentView* v = view();
    if (!v || e.page < 0) return;
    Editor& ed = editor();
    page_ = e.page;
    layer_ = ed.activeLayer(page_);
    if (!ed.layerVisible(page_, layer_)) {   // drawing on a hidden layer would be invisible: show it, as the Electron app does
      ed.setLayerVisible(page_, layer_, true);
      ed.notify(QStringLiteral("La capa activa estaba oculta: se ha vuelto a mostrar"));
    }
    const Style& st = ed.style();
    color_ = QColor(st.color);
    base_ = highlighter_ ? st.highlighterWidth : st.penWidth;
    pts_.clear();
    widths_.clear();
    engine_.reset();
    useWidthList_ = false;
    penFirstFromPressure_ = false;
    lastPressure_ = e.pressure;
    if (!highlighter_) {
      const TabletSettings& ts = ed.tablet();
      // a plain pen under a mouse keeps one width for the whole stroke; every other tip, or any pen with pressure, computes a width per point
      if (ts.brush != BrushKind::Pen || e.hasPressure) {
        map_ = PressureMap(ts.curve);
        engine_.emplace(ts.brush, st.penWidth, map_, e.hasPressure, ts.penLook);
        useWidthList_ = true;
        penFirstFromPressure_ = ts.brush == BrushKind::Pen && ts.penLook == PenLook::Proto;   // every point, the first included, has its own width
      }
    }
    // a finished highlighter stroke multiplies with what is under it (plain alpha only over a dark paper): the live layer is composed the same way
    // a glowing pen: the halo the finished stroke will have, by the page renderer's own filter, brought up to date as the stroke grows (values as stored in the stroke)
    DocumentView::LiveGlowParams glow{color_, std::round(st.glowSize * 100) / 100, std::round(st.glowIntensity * 100) / 100};
    QPainter& p = v->beginLive(page_, highlighter_ ? alphaOf(st.highlighterAlpha) : 1.0, highlighter_ && !v->renderBase().darkPaper, !highlighter_ && st.glowOn ? &glow : nullptr);
    ink_.emplace(p, color_, v->zoom() * v->devicePixelRatioF());
    drawing_ = true;
    addPoint(e);
  }

  void pointerMove(const PointerEvent& e) override { if (drawing_) addPoint(e); }

  void pointerUp(const PointerEvent& e) override {
    if (!drawing_) return;
    PointerEvent last = e;
    if (penFirstFromPressure_) last.pressure = lastPressure_;   // a lifted pen reports pressure 0: that is not a sample of the stroke, only its end position
    addPoint(last);   // the release position is a sample too
    finish();
  }

  void pointerCancel() override {
    if (!drawing_) return;
    drawing_ = false;
    ink_.reset();
    if (DocumentView* v = view()) v->endLive();
  }

 private:
  static double alphaOf(const QString& hex) { bool ok = false; const int a = hex.toInt(&ok, 16); return ok ? a / 255.0 : 0.5; }

  void addPoint(const PointerEvent& e) {
    DocumentView* v = view();
    const double x = e.pos.x(), y = e.pos.y();
    const size_t n = pts_.size() / 2;
    if (n && std::abs(pts_[2 * n - 2] - x) < 1e-3 && std::abs(pts_[2 * n - 1] - y) < 1e-3) return;   // a repeated position adds nothing but weight
    double w = base_;
    if (useWidthList_) w = n == 0 && !penFirstFromPressure_ ? engine_->first() : engine_->next(x, y, e.pressure, double(e.timestampMs));
    lastPressure_ = e.pressure;
    pts_.push_back(x);
    pts_.push_back(y);
    if (useWidthList_) widths_.push_back(w);
    v->liveDirty(ink_->add(QPointF(x, y), w));
  }

  void finish() {
    drawing_ = false;
    DocumentView* v = view();
    const QRectF tail = ink_->end();
    if (v && !tail.isNull()) v->liveDirty(tail);
    Editor& ed = editor();
    Stroke s;
    s.attrs = {{"tool", highlighter_ ? "highlighter" : "pen"}, {"color", ed.style().color + (highlighter_ ? ed.style().highlighterAlpha : QStringLiteral("ff"))}, {"capStyle", "round"}};
    if (pts_.size() == 2) { pts_.push_back(pts_[0]); pts_.push_back(pts_[1]); if (useWidthList_) widths_.push_back(widths_.back()); }   // a dot is a two-point stroke
    s.pts = pts_;
    if (useWidthList_ && engine_ && widths_.size() == pts_.size() / 2 && widths_.size() > 1) s.widths = engine_->finish(pts_, widths_);   // the tip runs out
    else if (useWidthList_) s.widths = widths_;
    else s.widths = {base_};
    applyLineStyle(s.attrs, ed.style(), !highlighter_);
    if (!highlighter_ && ed.style().glowOn) {
      s.attrs.emplace_back("glow", QString::number(std::round(ed.style().glowSize * 100) / 100));
      s.attrs.emplace_back("glowAlpha", QString::number(std::round(ed.style().glowIntensity * 100) / 100));
    }
    if (!highlighter_ && ed.recognizeShapes()) {   // a clearly hand-drawn line, ellipse, rectangle or triangle becomes the clean shape (one width, as the Electron app does)
      if (auto shape = recognizeShape(s.pts)) { s.pts = *shape; s.widths = {s.widths.empty() ? base_ : s.widths.front()}; s.attrs.emplace_back("xlShape", "1"); ed.notify(QStringLiteral("Forma reconocida")); }
    }
    ink_.reset();
    ed.commitAdd(page_, layer_, Element(std::move(s)), highlighter_ ? QStringLiteral("Resaltado") : QStringLiteral("Trazo"));   // patches the tiles it touches
    if (v) v->endLive();
  }

  bool highlighter_;
  bool drawing_ = false, useWidthList_ = false, penFirstFromPressure_ = false;
  double lastPressure_ = 0;
  int page_ = 0, layer_ = 0;
  QColor color_;
  double base_ = 1;
  std::vector<double> pts_, widths_;
  PressureMap map_;
  std::optional<BrushEngine> engine_;
  std::optional<LiveInk> ink_;
};

}  // namespace

void registerInkTools(Editor& ed) {
  ToolInfo pen;
  pen.id = "pen"; pen.label = "Lápiz"; pen.tooltip = "Lápiz"; pen.shortcut = QKeySequence("P"); pen.icon = Icons::make("pen"); pen.order = 0;
  pen.optionsCommand = "pen.settings";   // the arrow on the pen opens Lápiz y tableta
  pen.factory = [](Editor& e) { return std::make_unique<InkTool>(e, false); };
  ed.registerTool(pen);
  ToolInfo hl;
  hl.id = "highlighter"; hl.label = "Resaltador"; hl.tooltip = "Resaltador"; hl.shortcut = QKeySequence("H"); hl.icon = Icons::make("highlighter"); hl.order = 1;
  hl.factory = [](Editor& e) { return std::make_unique<InkTool>(e, true); };
  ed.registerTool(hl);
}
