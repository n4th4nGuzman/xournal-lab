// The objects of the real line and of set diagrams as one family: what is selected, built, read back and moved by its anchors.
// Port of app/src/renderer/mathobj.ts.
#pragma once
#include "numberline.h"
#include "venn.h"
#include <optional>
#include <variant>
#include <vector>

namespace sets {

using MathSpec = std::variant<LineSpec, VennSpec>;
struct MathRead { MathSpec spec; GenStyle style; };

/** True when one of the members is a "setwitness" stroke carrying an xlSet attribute. */
bool isMath(const std::vector<const Element*>& members);
std::optional<MathRead> readMath(const std::vector<const Element*>& members);
std::vector<Element> buildMath(const QString& id, const MathSpec& spec, const GenStyle& style);
MathSpec sanitizeMath(const MathSpec& spec);
std::vector<V2> mathAnchors(const MathSpec& spec);
MathSpec moveMathAnchor(const MathSpec& spec, int i, V2 p, bool shift);

}  // namespace sets
