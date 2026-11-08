// Figures for maths and physics classes: segments, arrows, polygons, curves, arcs, angles, dimension lines and wireframe solids. Port of
// app/src/renderer/figures.ts. A figure is made of ordinary strokes and texts (xlPlane="<id>"), rebuilt from its control points. The control
// points live in the xlFig attribute of an invisible "figwitness" stroke together with three reference points; after the pieces were moved,
// scaled, rotated or flipped, the affine map from the stored witness to where it is now carries the stored control points along.
#pragma once
#include "plane.h"
#include <QJsonObject>
#include <vector>

enum class FigKind { Segment, Arrow, Polyline, Polygon, Regular, Bezier2, Bezier3, Spline, Arc, Angle, Dim, Box, Pyramid, Cylinder, Cone, Sphere };
enum class Heads { None, End, Both };
enum class Hidden { Dash, None, Solid };   // edges behind a solid: dashed, not drawn, or drawn like the rest

struct FigureSpec {
  FigKind kind = FigKind::Segment;
  std::vector<double> pts;   // control points (x, y, ...): the anchors. See defaultSpec for each kind
  Heads heads = Heads::None;
  Dash dash = Dash::Solid;
  int n = 4;                 // sides of a regular polygon, meridians of a sphere (3..12)
  int m = 3;                 // parallels of a sphere (0..9)
  Hidden hidden = Hidden::Dash;
  QString label;             // plain text near the figure, at most 40 characters
};

struct FigureCatalogItem { FigKind kind; const char* name; const char* group; bool clicks; };
/** The gallery, in the order of the Electron app (names and groups in Spanish, as shown). */
const std::vector<FigureCatalogItem>& figureCatalog();

QString figKindName(FigKind k);                       // "segment", "box", ... as stored in xlFig
std::optional<FigKind> figKindFromName(const QString& name);
bool isSolid(FigKind k);
bool clicksKind(FigKind k);                           // polyline, polygon, spline: built by clicking points
std::pair<int, int> pointRange(FigKind k);            // fewest and most control points

/** Make a spec usable: known kind, the right number of control points (finite), options in range. */
FigureSpec sanitizeFigure(const FigureSpec& raw);
FigureSpec figureFromJson(const QJsonObject& o);      // the same on stored JSON, with the defaults that depend on the kind
QString encodeFigure(const FigureSpec& s);

/** Size (width, height) of the default figure of a kind, to fit a dragged box. */
V2 defaultSize(FigKind kind);
/** The figure a drag from `a` to `b` (page units) makes; `shift` snaps the drag to steps of 15 degrees. */
FigureSpec defaultSpec(FigKind kind, V2 a, V2 b, bool shift = false);

/** Page position of the point (a, b, c) of a solid's own space: the unit cube on its origin and three edge tips. */
V2 solidPoint(const FigureSpec& s, double a, double b, double c);
/** The eight corners of the solid's unit cube on the page (for sizing): 16 numbers. */
std::vector<double> solidCorners(const FigureSpec& s);

/** The three points standing for the figure's position: its first control points, moved off a line if they are on one. */
std::vector<double> witnessOf(const FigureSpec& s);
std::vector<Element> buildFigure(const QString& id, const FigureSpec& rawSpec, const PlaneStyle& style);

const Stroke* figureWitness(const Members& members);
bool isFigure(const Members& members);
struct FigureRead { FigureSpec spec; PlaneStyle style; };
/** The figure as it is now: the stored control points carried through whatever happened to the witness since. */
std::optional<FigureRead> figureSpecOf(const Members& members);
/** The spec with control point `i` moved to `p`. Shift: angles in steps of 15 degrees (for a solid, the edge keeps its length). */
FigureSpec moveFigureAnchor(const FigureSpec& spec, int i, V2 p, bool shift);
