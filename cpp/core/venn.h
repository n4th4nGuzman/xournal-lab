// Diagrams of two or three sets (Venn): a universe, circles, the elements written in each set, and the region an expression shades. The shading is a
// diagonal hatching computed exactly: along each line of the hatching, the stretches inside each circle are found, and the stretches that belong to a
// shaded region are the ones in the circles of its mask and outside the others. No fills are needed: the drawing is ordinary pen strokes and texts.
// Port of app/src/renderer/venn.ts.
#pragma once
#include "genobj.h"
#include "numberline.h"
#include <QJsonValue>
#include <QString>
#include <optional>
#include <vector>

namespace sets {

struct VennSpec {
  /** Top left and bottom right corners of the universe, then for each set its centre and a point on its circle (4 + 4k numbers). */
  std::vector<double> pts;
  int k = 2;                      // 2 or 3 sets
  std::vector<QString> names;     // always 3 names
  QString expr;                   // which region is shaded, as an expression of the names (A ∩ B', A ∖ B, (A ∪ B)'…)
  bool universe = true;
  bool dense = false;             // hatch: "light" (false) or "dense"
  std::vector<QString> lists;     // k + 1 texts: the elements of each set ("1, 2, 3"), then the ones outside every set
};

VennSpec defaultVenn(int k = 2, V2 a = {100, 100}, V2 b = {500, 360});
VennSpec sanitizeVenn(const VennSpec& raw);
VennSpec sanitizeVenn(const QJsonValue& raw);
QString vennSpecJson(const VennSpec& spec);

/** Which circles a point is inside, as bits (0 is outside all of them). */
int maskAt(const VennSpec& spec, V2 p);

/** The regions the expression shades: `shaded` is empty when there is no expression or it has a mistake (then `error` says which). */
struct Shading { std::vector<bool> shaded; QString error; };
Shading shadedRegions(const VennSpec& spec);

struct VennElement { QString label; int mask; };
/** The elements with the region they belong to (an element in two lists is in both sets). Order of first appearance. */
std::vector<VennElement> elementsOf(const VennSpec& spec);

/** The strokes of the hatching: each is x0, y0, x1, y1 of one stretch. */
std::vector<std::vector<double>> hatchSegments(const VennSpec& spec, const std::vector<bool>& shaded);

struct PlacedElement { QString label; V2 at; };
/** Where each element goes: the point of its region farthest from the borders, then the next ones farthest from both borders and each other. */
std::vector<PlacedElement> placeElements(const VennSpec& spec);

std::vector<Element> buildVenn(const QString& id, const VennSpec& spec, const GenStyle& style);
/** Three points that stand for the diagram's place: three corners of the universe. */
std::vector<double> witnessOf(const VennSpec& spec);
std::vector<V2> vennAnchors(const VennSpec& spec);
/** The spec with anchor `i` moved: a corner of the universe; the centre of a circle (it carries its circle); or the point that sets its radius. */
VennSpec moveVennAnchor(const VennSpec& spec, int i, V2 p, bool shift);

struct VennRead { VennSpec spec; GenStyle style; };
std::optional<VennRead> readVenn(const std::vector<const Element*>& members);

}  // namespace sets
