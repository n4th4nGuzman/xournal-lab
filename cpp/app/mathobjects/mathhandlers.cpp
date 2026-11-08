// The GeneratedHandlers of the real line ("line") and of set diagrams ("venn"): what the selection asks to draw anchors, drag them and rebuild the
// pieces after the whole object was moved, turned or scaled. All interpretation of the stored definition is core/mathobj.h's; this only adapts it.
#include "mathhandlers.h"
#include "mathstate.h"

namespace mathobj {
namespace {

class MathHandler final : public GeneratedHandler {
 public:
  explicit MathHandler(QString kind) : kind_(std::move(kind)) {}
  QString kind() const override { return kind_; }

  bool claims(const ElementPtrs& members) const override {
    if (!sets::isMath(members)) return false;
    const auto r = sets::readMath(members);
    return r && kindOf(r->spec) == kind_;
  }

  std::vector<QPointF> anchors(const ElementPtrs& members) const override {
    std::vector<QPointF> out;
    const auto r = sets::readMath(members);
    if (!r) return out;
    for (const V2& p : sets::mathAnchors(r->spec)) out.emplace_back(p[0], p[1]);
    return out;
  }

  std::optional<std::vector<Element>> dragAnchor(const ElementPtrs& members, const QString& id, int i, QPointF p, bool shift) const override {
    const auto r = sets::readMath(members);
    if (!r) return std::nullopt;
    const sets::MathSpec moved = sets::sanitizeMath(sets::moveMathAnchor(r->spec, i, {p.x(), p.y()}, shift));
    if (specJson(moved) == specJson(r->spec)) return std::nullopt;
    return sets::buildMath(id, moved, r->style);
  }

  std::optional<std::vector<Element>> rebuildAfterTransform(const ElementPtrs& members, const QString& id) const override {
    const auto r = sets::readMath(members);
    if (!r) return std::nullopt;
    return sets::buildMath(id, r->spec, r->style);
  }

 private:
  QString kind_;
};

}  // namespace

void registerMathHandlers() {
  GeneratedRegistry::instance().add(std::make_unique<MathHandler>("line"));
  GeneratedRegistry::instance().add(std::make_unique<MathHandler>("venn"));
}

}  // namespace mathobj
