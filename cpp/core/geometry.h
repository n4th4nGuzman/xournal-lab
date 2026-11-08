// Bounding boxes, hit testing, selection tests, transforms, shape recognition and freehand smoothing of page elements.
// Port of app/src/renderer/geometry.ts. Everything here is on the pointer path: no allocation unless a result needs one.
// O(elements) scans are the caller's job to avoid: use ElementIndex (spatial.h) to get the few candidates near the pointer first.
#pragma once
#include "model.h"
#include <array>
#include <optional>
#include <vector>

using Rect = std::array<double, 4>;   // x0, y0, x1, y1

// ---------------------------------------------------------------- boxes
/** Smallest box around the points (x0 = +inf ... when there are none, like the TS bboxOf). */
Rect bboxOfPoints(const std::vector<double>& pts);
/** Box of the stroke's points, without the line width. */
inline Rect strokeBBox(const Stroke& s) { return bboxOfPoints(s.pts); }
/** Largest of the stroke's widths (the base width when there is no list). */
double maxWidth(const Stroke& s);
double maxOf(const std::vector<double>& a);

/**
 * Text boxes are measured by textBBox (genobj.h) with the plain font metrics. Markdown texts (attr xlMd=1) have their own layout, owned by the
 * markdown module: it installs its measure here and elementBBox/transformElement use it. Without one the plain measure is used.
 */
using TextMeasureFn = Rect (*)(const Text&);
void setMarkdownMeasure(TextMeasureFn fn);
bool isMarkdownText(const Text& t);
Rect textRect(const Text& t);

/** Rotation (radians) and mirror of a formula image (attrs xlRot / xlFlip); the box in left/top/right/bottom is the upright one. */
struct TexPose { double rot = 0; bool flip = false; };
TexPose texPose(const Image& im);
void setTexPose(Image& im, const TexPose& pose);
Rect imageBox(const Image& im);
void setImageBox(Image& im, const Rect& r);
bool isTexImage(const Image& im);
bool texContains(const Image& im, double x, double y);

/** Bounding box in page units; strokes grow by half their widest width. */
Rect elementBBox(const Element& e);
Rect unionRect(const std::optional<Rect>& a, const Rect& b);
bool rectsIntersect(const Rect& a, const Rect& b);
bool pointInRect(double x, double y, const Rect& r);

// ---------------------------------------------------------------- hit testing and selection
/** True when (x, y) is within `pad` of any segment (or the dot) of the stroke. */
bool strokeHit(const Stroke& s, double x, double y, double pad);
/** Cut the parts of a stroke within `r` of (x, y). nullopt when nothing is hit, otherwise the surviving pieces (possibly none). */
std::optional<std::vector<Stroke>> splitStroke(const Stroke& s, double x, double y, double r);
/** Rectangle selection: strokes by vertex, text and images by box overlap. */
bool inRect(const Element& e, const Rect& r);
/** Point selection: nearest-stroke hit (pad + half the base width), or the text/image box grown by pad. */
bool hitElement(const Element& e, double x, double y, double pad);
/** Even-odd point-in-polygon; `poly` is x0,y0,x1,y1,... */
bool pointInPolygon(double x, double y, const std::vector<double>& poly);
/** Lasso selection: strokes by vertex, text and images by box centre. */
bool inPolygon(const Element& e, const std::vector<double>& poly);
double segDist2(double x, double y, double ax, double ay, double bx, double by);

// ---------------------------------------------------------------- affine maps
/** x' = a*x + c*y + e, y' = b*x + d*y + f (page units). */
struct Aff { double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };
Aff scaleAff(double sx, double sy, double ax, double ay);
Aff translateAff(double dx, double dy);
Aff rotateAff(double theta, double cx, double cy);
Aff flipAff(bool horizontal, double cx, double cy);
std::array<double, 2> applyAff(const Aff& m, double x, double y);
Aff multiplyAff(const Aff& second, const Aff& first);   // the map "first, then second"
std::optional<Aff> invertAff(const Aff& m);              // nullopt when singular
Rect transformRect(const Rect& r, const Aff& m);

/** The state an element can be restored to (undo/redo of transforms). Strings are shared, not copied, so this is cheap for images. */
struct Snap { Element el; };
Snap snapshot(const Element& e);
void restore(Element& e, const Snap& s);
/**
 * Apply the map in place. Widths and font sizes follow sqrt(|det|); text stays upright (its box centre is mapped); a formula keeps its box and
 * absorbs turn and mirror in its pose; an ordinary picture under a non-axis-aligned map is re-encoded (baked) into a new PNG.
 */
void transformElement(Element& e, const Aff& m);
inline Element cloneElement(const Element& e) { return e; }

// ---------------------------------------------------------------- shapes
enum class ShapeKind { Line, Rect, Ellipse, Arrow, DoubleArrow, Axes };   // DoubleArrow: a head at each end; Axes: Xournal++'s "coordinate system" (an L with arrow heads) in the dragged box
/** Polyline for a shape dragged from (x0, y0) to (x1, y1). `constrain`: 15-degree steps for lines and arrows, squares and circles otherwise. */
std::vector<double> shapePoints(ShapeKind kind, double x0, double y0, double x1, double y1, bool constrain, double width);
/** Clean polyline of a hand-drawn line, ellipse, rectangle or triangle, or nullopt when the stroke is not clearly one of them. */
std::optional<std::vector<double>> recognizeShape(const std::vector<double>& pts);

// ---------------------------------------------------------------- freehand smoothing
/** Remove the rounding noise of pixel-quantised input; every point stays within half a step of where it was, ends stay put. */
std::vector<double> smoothFreehand(const std::vector<double>& p, double step);
/** Grid spacing (page units) the points sit on, or 0 when they do not sit on one. */
double quantizationStep(const std::vector<double>& p);

/** Per-stroke cache of what displayPoints decides; keep one per stroke (it notices when the points changed). */
struct StrokeView {
  const double* src = nullptr;      // identity of the point buffer the cache was made for
  size_t count = 0;
  double step = 0;
  bool haveSmooth = false;
  std::vector<double> smooth;
};
/** The points to draw `s` with at `scale` device pixels per page unit: the stored ones, or a smoothed copy when an input grid would show as stairs. */
const std::vector<double>& displayPoints(const Stroke& s, double scale, StrokeView& view);
