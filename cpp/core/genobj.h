// What the objects built out of ordinary strokes and texts (planes, figures, number lines, set diagrams) have in common. Port of app/src/renderer/genobj.ts.
// An object keeps its definition in an invisible "witness" stroke together with three reference points; the affine map from where those points
// were stored to where they are now is applied to every point of the stored definition (so moving, turning or scaling the pieces keeps it editable).
#pragma once
#include "model.h"
#include <array>
#include <optional>
#include <vector>

using V2 = std::array<double, 2>;
using Affine = std::array<double, 6>;   // a, b, c, d, e, f: x' = a*x + c*y + e, y' = b*x + d*y + f

enum class Dash { Solid, Dash, Dot };

struct Glow { double size = 0, intensity = 0; };
struct GenStyle { QString color = "#000000"; double width = 1.41; std::optional<Glow> glow; };  // color is #rrggbb

/** A piece of line: an ordinary pen stroke that belongs to object `id` with a role. `alpha` is two hex digits ("ff", or "00" for invisible). */
Stroke strokeEl(const std::vector<double>& pts, const QString& color, const QString& alpha, double width, const QString& role, const QString& id,
                Dash dash = Dash::Solid, const std::optional<Glow>& glow = std::nullopt);

/** A text centred on `at`, as a piece of object `id`. */
Text textEl(const QString& text, V2 at, double size, const QString& color, const QString& id, const QString& role);

/** The three points of an arrow head with its tip at `tip`, coming from the direction of `from`. */
std::vector<double> arrowHead(V2 tip, V2 from, double len);

/** The map taking three points to three others, or nullopt when the first three are on a line. src and dst hold 6 numbers each. */
std::optional<Affine> affineFrom(const std::vector<double>& src, const std::vector<double>& dst);
std::vector<double> mapPoints(const Affine& t, const std::vector<double>& pts);

/** attr "xlPlane" (the object id) and "xlRole" of an element, or null when it is not a piece of a generated object. */
std::optional<QString> planeIdOf(const Element& e);
std::optional<QString> roleOf(const Element& e);
const Attrs& attrsOf(const Element& e);
Attrs& attrsOf(Element& e);

/** The invisible stroke of an object (`role`) that holds its definition in attribute `attr`, if the members have one. */
const Stroke* findWitness(const std::vector<const Element*>& members, const QString& role, const QString& attr);
/** Make the witness stroke: invisible, at three reference points `w` (6 numbers), with the definition (any JSON text) in attribute `attr`. */
Stroke makeWitness(const std::vector<double>& w, const QString& role, const QString& attr, const QString& json, const QString& color, const QString& id);

/** Text size as the editor lays it out: width of the widest line (at least the font size) by lines * size * 1.2. Returns x0, y0, x1, y1. */
std::array<double, 4> textBBox(const Text& t);
void stampText(Text& t);               // writes font, size, x, y, color attrs from the fields the editor set
QString fmt8(double v);                // at most 8 significant digits, no trailing zeros: how Xournal++ writes numbers
QString newObjectId();                 // "pl" + 6 random base-36 characters
