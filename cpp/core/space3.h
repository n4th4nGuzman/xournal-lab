// Cartesian space in 3D drawn as a projection onto the page. Port of app/src/renderer/space3.ts.
// An origin plus, for each axis, the page vector of one unit toward its positive half (u) and toward its negative half (n). Made of ordinary strokes
// and texts (xlPlane="<id>"); the axis strokes carry the settings (xlSpace3 JSON on the x axis, xlPlaneW) and the geometry is read back from them.
#pragma once
#include "plane.h"
#include <array>

struct Space3Spec {
  double xmin = -3, xmax = 3, ymin = -3, ymax = 3, zmin = -3, zmax = 3;
  double xstep = 1, ystep = 1, zstep = 1;
  bool gridXY = true, gridXZ = false, gridYZ = false;   // grids on the coordinate planes z = 0, y = 0 and x = 0
  bool ticks = true, numbers = true, arrows = true;
  std::array<QString, 3> names{"x", "y", "z"};
  V2 o{0, 0};                       // where the origin is on the page
  std::array<V2, 3> u;              // page vector of one unit toward the positive end of x, y, z
  std::array<V2, 3> n;              // ...and toward the negative end (the same while an axis is straight)
  Space3Spec();
};
extern const Space3Spec DEFAULT_SPACE3;

enum class AnchorSide { Plus, Minus };
struct Space3Anchor { int axis; AnchorSide side; V2 at; double value; };
struct Angle { double deg, len; };

V2 axisVector(double deg, double len);                 // `deg` degrees counter-clockwise from the right, `len` page units per unit
Angle vectorAngle(const V2& v);                        // deg in [0, 360)
std::array<V2, 3> defaultVectors(double scale = 1);    // z up, y to the right, x down-left and foreshortened

/** Make a spec usable (ranges in whole steps, at most 40 lines per axis). Non-finite numbers take the default. */
Space3Spec sanitizeSpace3(const Space3Spec& raw);
Space3Spec space3FromJson(const QJsonObject& o);       // the same on stored JSON; a missing n copies u (old files had straight axes)
QString encodeSpace3(const Space3Spec& s);
std::optional<Space3Spec> decodeSpace3(const QString& text);

V2 project(const Space3Spec& s, double x, double y, double z);
std::vector<Space3Anchor> anchorsOf(const Space3Spec& s);   // the end of each half of each axis (a half the range does not have gets none)
/** The spec with the end of one half of axis `i` moved to page point `p`: that half's angle and length follow; with keepLength only the angle does. */
Space3Spec withAnchor(const Space3Spec& s, int i, AnchorSide side, V2 p, bool keepLength);

std::vector<Element> buildSpace3(const QString& id, const Space3Spec& rawSpec, const PlaneStyle& style);

bool isSpace3(const Members& members);
std::optional<Space3Spec> space3SpecOf(const Members& members);   // settings from the x axis, geometry from where the axes are now
std::optional<PlaneStyle> space3StyleOf(const Members& members);
inline Members space3Members(const Layer& layer, const QString& id) { return planeMembers(layer, id); }

/** A spec whose drawing fits `scale` times the default size, with its bounding box's top-left corner at `corner`. */
Space3Spec placedSpec(const Space3Spec& base, double scale, V2 corner);
/** Page bounding box (x0, y0, x1, y1) of the axes and grids of a spec. */
std::array<double, 4> extentOf(const Space3Spec& s);
