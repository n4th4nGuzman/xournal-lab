// Cartesian planes drawn on demand: axes (ticks, numbers, arrows), an optional grid and subgrid, on a frame. Port of app/src/renderer/plane.ts.
// A plane is made of ordinary strokes and texts; every piece carries xlPlane="<id>" and xlRole, the axis stroke also carries the settings
// (xlPlaneSpec JSON) and the width chosen for the plane (xlPlaneW). The JSON is written with the same key order and 0/1 booleans as the Electron app.
#pragma once
#include "genobj.h"
#include "model.h"
#include <QJsonObject>
#include <QString>
#include <optional>
#include <vector>

struct PlaneSpec {
  double xmin = -5, xmax = 5, ymin = -5, ymax = 5;
  double xstep = 1, ystep = 1;     // distance between numbered ticks and major grid lines, in axis units
  bool grid = true;
  int sub = 0;                     // subdivisions of each cell drawn lighter (0, 2, 4, 5 or 10); only with the grid
  bool ticks = true, numbers = true, arrows = true;
  QString xlabel = "x", ylabel = "y";
  bool square = true;              // keep the same points per unit on both axes when the frame is created or the range changes
};
extern const PlaneSpec DEFAULT_PLANE;

struct Frame { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
using PlaneStyle = GenStyle;       // color (#rrggbb), width, optional glow: what the pen was when the object was made
using Members = std::vector<const Element*>;

/** Make a spec usable: ranges that are whole numbers of steps, few enough lines. */
PlaneSpec sanitizePlane(const PlaneSpec& raw);
/** The spec out of stored JSON: missing or non-finite fields take the default, then sanitizePlane. */
PlaneSpec planeFromJson(const QJsonObject& o);
QString encodeSpec(const PlaneSpec& s);
std::optional<PlaneSpec> decodeSpec(const QString& text);   // nullopt for empty or malformed text

/** Every piece of object `id` in the layer (pointers into layer.elements, in order) or their indices. */
Members planeMembers(const Layer& layer, const QString& id);
std::vector<size_t> planeMemberIndices(const Layer& layer, const QString& id);

std::optional<PlaneSpec> planeSpecOf(const Members& members);
/** The frame the plane occupies now: the ends of its two axes. */
std::optional<Frame> planeFrameOf(const Members& members);
/** The style of the plane's axes: colour, width (xlPlaneW wins over the stroke's) and glow. */
std::optional<PlaneStyle> planeStyleOf(const Members& members);
Frame squareFrame(const PlaneSpec& spec, const Frame& f);

std::vector<Element> buildPlane(const QString& id, const PlaneSpec& rawSpec, const Frame& frame, const PlaneStyle& style);
inline QString newPlaneId() { return newObjectId(); }

// ---- helpers shared with space3 and figures (they mirror what JavaScript does, so stored text matches the Electron app) ----
namespace genjs {
double round6(double v);
double jsRound(double v);                 // Math.round: halves go up
QString num(double v);                    // String(number): integers plain, otherwise shortest round-trip digits
QString str(const QString& s);            // JSON string literal
QString numArray(const std::vector<double>& v);
std::optional<Glow> glowOf(const Stroke& s);              // from the glow / glowAlpha attributes
std::array<double, 4> bboxOf(const std::vector<double>& pts);
QString hexColorOf(const Stroke& s);      // "#rrggbb" of the colour attribute (default black)
double numOr(const QJsonObject& o, const QString& key, double def);   // finite JSON number, else def
}
