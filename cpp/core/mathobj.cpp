#include "mathobj.h"

namespace sets {

bool isMath(const std::vector<const Element*>& members) {
  for (const Element* m : members)
    if (std::holds_alternative<Stroke>(*m) && roleOf(*m) == QString("setwitness") && attrFind(attrsOf(*m), SET_ATTR)) return true;
  return false;
}

std::optional<MathRead> readMath(const std::vector<const Element*>& members) {
  if (auto l = readLine(members)) return MathRead{std::move(l->spec), l->style};
  if (auto v = readVenn(members)) return MathRead{std::move(v->spec), v->style};
  return std::nullopt;
}

std::vector<Element> buildMath(const QString& id, const MathSpec& spec, const GenStyle& style) {
  if (const auto* l = std::get_if<LineSpec>(&spec)) return buildLine(id, *l, style);
  return buildVenn(id, std::get<VennSpec>(spec), style);
}

MathSpec sanitizeMath(const MathSpec& spec) {
  if (const auto* l = std::get_if<LineSpec>(&spec)) return sanitizeLine(*l);
  return sanitizeVenn(std::get<VennSpec>(spec));
}

std::vector<V2> mathAnchors(const MathSpec& spec) {
  if (const auto* l = std::get_if<LineSpec>(&spec)) return lineAnchors(*l);
  return vennAnchors(std::get<VennSpec>(spec));
}

MathSpec moveMathAnchor(const MathSpec& spec, int i, V2 p, bool shift) {
  if (const auto* l = std::get_if<LineSpec>(&spec)) return moveLineAnchor(*l, i, p, shift);
  return moveVennAnchor(std::get<VennSpec>(spec), i, p, shift);
}

}  // namespace sets
