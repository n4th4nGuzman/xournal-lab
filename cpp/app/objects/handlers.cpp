// GeneratedHandlers of planes, 3D spaces and figures: what the selection asks to know whether a set of pieces is one of these objects, which handles to
// show, how to turn a dragged handle into a rebuilt object and how to rebuild after the pieces were moved, turned or scaled as ordinary elements.
// The definition always comes back from the pieces (witness strokes and axes), never from a copy kept here.
#include "handlers.h"
#include "figures.h"
#include "plane.h"
#include "space3.h"
#include <cmath>

namespace xlobjects {
namespace {

// The frame of a plane is read from the boxes of its two axes, so it only means something while the axes are still horizontal and vertical (moved,
// scaled or turned by 180 degrees or flipped is fine). A plane turned by 90 degrees or any other angle would read as a collapsed or invented frame and
// be rebuilt into garbage by a resize or by the panel; it stays what it is, ordinary ink (the README: "al rotar un plano sigue siendo tinta").
bool axesAligned(const ElementPtrs& m) {
  const auto level = [](const Stroke& s, int fixedCoord) {
    if (s.pts.size() < 4) return false;
    double lo = 1e18, hi = -1e18, span = 0;
    for (size_t i = 0; i + 1 < s.pts.size(); i += 2) {
      lo = std::min(lo, s.pts[i + (1 - fixedCoord)]);
      hi = std::max(hi, s.pts[i + (1 - fixedCoord)]);
      span = std::max(span, std::abs(s.pts[i + fixedCoord] - s.pts[fixedCoord]));
    }
    return hi - lo < 0.02 && span >= 0.5;   // no drift across the stroke, and it really goes along the axis
  };
  bool x = false, y = false;
  for (const Element* e : m) {
    const auto* s = std::get_if<Stroke>(e);
    if (!s) continue;
    const auto role = roleOf(*e);
    if (role == std::optional<QString>("axisx")) x = level(*s, 0);
    else if (role == std::optional<QString>("axisy")) y = level(*s, 1);
  }
  return x && y;
}

class PlaneHandler final : public GeneratedHandler {
 public:
  QString kind() const override { return QStringLiteral("plane"); }
  bool claims(const ElementPtrs& m) const override { return planeSpecOf(m) && planeFrameOf(m) && planeStyleOf(m) && axesAligned(m); }
  // a plane is resized by the selection frame: no handles of its own
  std::vector<QPointF> anchors(const ElementPtrs&) const override { return {}; }
  std::optional<std::vector<Element>> dragAnchor(const ElementPtrs&, const QString&, int, QPointF, bool) const override { return std::nullopt; }
  std::optional<std::vector<Element>> rebuildAfterTransform(const ElementPtrs& m, const QString& id) const override {
    const auto spec = planeSpecOf(m);
    const auto frame = planeFrameOf(m);
    const auto style = planeStyleOf(m);
    if (!spec || !frame || !style) return std::nullopt;
    return buildPlane(id, *spec, *frame, *style);   // same settings in the new frame: ticks, labels and arrows keep their size
  }
};

class Space3Handler final : public GeneratedHandler {
 public:
  QString kind() const override { return QStringLiteral("space3"); }
  bool claims(const ElementPtrs& m) const override { return isSpace3(m) && space3SpecOf(m) && space3StyleOf(m); }
  std::vector<QPointF> anchors(const ElementPtrs& m) const override {
    std::vector<QPointF> out;
    if (const auto spec = space3SpecOf(m))
      for (const Space3Anchor& a : anchorsOf(*spec)) out.emplace_back(a.at[0], a.at[1]);
    return out;
  }
  std::optional<std::vector<Element>> dragAnchor(const ElementPtrs& m, const QString& id, int i, QPointF p, bool shift) const override {
    const auto spec = space3SpecOf(m);
    const auto style = space3StyleOf(m);
    if (!spec || !style) return std::nullopt;
    const auto anchors = anchorsOf(*spec);
    if (i < 0 || i >= int(anchors.size())) return std::nullopt;
    const Space3Anchor& a = anchors[size_t(i)];
    // only that half of the axis moves; Shift keeps its length and only turns it
    const Space3Spec next = sanitizeSpace3(withAnchor(*spec, a.axis, a.side, V2{p.x(), p.y()}, shift));
    if (encodeSpace3(next) == encodeSpace3(sanitizeSpace3(*spec))) return std::nullopt;
    return buildSpace3(id, next, *style);
  }
  std::optional<std::vector<Element>> rebuildAfterTransform(const ElementPtrs& m, const QString& id) const override {
    const auto spec = space3SpecOf(m);
    const auto style = space3StyleOf(m);
    if (!spec || !style) return std::nullopt;
    return buildSpace3(id, *spec, *style);
  }
  double anchorHitPx() const override { return 10; }
};

class FigureHandler final : public GeneratedHandler {
 public:
  QString kind() const override { return QStringLiteral("figure"); }
  bool claims(const ElementPtrs& m) const override { return isFigure(m) && figureSpecOf(m); }
  std::vector<QPointF> anchors(const ElementPtrs& m) const override {
    std::vector<QPointF> out;
    if (const auto read = figureSpecOf(m))
      for (size_t i = 0; i + 1 < read->spec.pts.size(); i += 2) out.emplace_back(read->spec.pts[i], read->spec.pts[i + 1]);
    return out;
  }
  std::optional<std::vector<Element>> dragAnchor(const ElementPtrs& m, const QString& id, int i, QPointF p, bool shift) const override {
    const auto read = figureSpecOf(m);
    if (!read || i < 0 || size_t(i) * 2 + 1 >= read->spec.pts.size()) return std::nullopt;
    const FigureSpec next = sanitizeFigure(moveFigureAnchor(read->spec, i, V2{p.x(), p.y()}, shift));
    if (next.pts == read->spec.pts) return std::nullopt;   // refused (too short) or not moved
    return buildFigure(id, next, read->style);
  }
  std::optional<std::vector<Element>> rebuildAfterTransform(const ElementPtrs& m, const QString& id) const override {
    const auto read = figureSpecOf(m);
    if (!read) return std::nullopt;
    return buildFigure(id, read->spec, read->style);
  }
  double anchorHitPx() const override { return 10; }
};

}  // namespace

void registerObjectHandlers() {
  auto& reg = GeneratedRegistry::instance();
  reg.add(std::make_unique<PlaneHandler>());
  reg.add(std::make_unique<Space3Handler>());
  reg.add(std::make_unique<FigureHandler>());
}

}  // namespace xlobjects
