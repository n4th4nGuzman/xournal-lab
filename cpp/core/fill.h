// Paint-bucket fill and Riemann rectangles. Port of app/src/renderer/fill.ts plus the assembly that editor.ts runFill() does (the elements written).
//  - a click: flood fill from that point, stopped by the strokes of the active layer;
//  - a dragged line or curve: the fill radiates from it toward its left or right side (a half-plane next to the line, or the inside/outside of a
//    closed curve) and stops at any stroke of the layer;
//  - kind Riemann: rectangles stand on the dragged base and rise to the curve.
// The result is a raster mask in page space, stored as an ordinary <image> (attr xlFill=1) under the strokes of the layer, plus, for Riemann
// rectangles, one pen stroke per outline (attr xlRiemann=1), so the file stays valid Xournal++.
//
// Which strokes are walls: ink that is "generated" (axes and grid of a plane, outlines of earlier rectangles) is ignored, except the lines of
// figures and set diagrams (roles figbody/figarrow/figmark/setbody/setarrow/setmark), which are ordinary ink. See isGenerated().
// Rendering of the ink mask uses a private rasteriser (this file), not renderLayers: the mask needs plain black coverage with a minimum line width,
// never colours, glow or pictures.
#pragma once
#include "model.h"
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

enum class FillSide { Left, Right };
enum class FillKind { Solid, Linear, Radial, Riemann };
enum class RiemannRule { Upper, Lower, Mid, Left, Right };
enum class RiemannBase { Line, Curve };
enum class RiemannBy { Count, Width };

struct FillOptions {
  FillSide side = FillSide::Left;       // which side of the dragged line is filled (its travel direction defines left and right)
  FillKind kind = FillKind::Solid;
  QString color2 = "#05e1ff";           // second colour of a gradient; the first is the pen colour
  double angle = 90;                    // linear gradient direction in degrees: 0 left to right, 90 top to bottom
  double opacity = 1;
  double gap = 2;                       // openings up to this width (page units) do not let the fill through
  bool keepLine = false;                // keep the dragged line as a pen stroke
  RiemannBase base = RiemannBase::Line; // rectangles stand on a straight base (start to end of the drag) or follow the drawn curve
  RiemannRule rule = RiemannRule::Upper;
  RiemannBy by = RiemannBy::Count;
  int count = 12;
  double width = 20;
  bool outline = true;                  // draw each rectangle's outline as a pen stroke
  bool magnet = true;                   // close bigger gaps, stick to the nearest closed shape, pull a dragged line onto the curve
  double maxGap = 14;                   // widest gap (page units) the magnet may close
};

/** Parse stored options (preferences are JSON text); anything missing or invalid falls back to its default, numbers are clamped to their range. */
FillOptions sanitizeFill(const QJsonObject& raw);
FillOptions sanitizeFill(const QString& json);
QJsonObject fillToJson(const FillOptions& o);   // the keys of the TS options (side, kind, color2, angle, ...)

// ---------------------------------------------------------------- ink rules
using StrokeRefs = std::vector<const Stroke*>;
/** Ink the editor drew as a backdrop: fills and rectangles go over it instead of stopping at it. */
bool isGenerated(const Stroke& s);
/** Pointers to every stroke of the layer / to those that stop a fill (not generated). */
StrokeRefs layerStrokes(const Layer& l);
StrokeRefs wallStrokes(const Layer& l);

// ---------------------------------------------------------------- raster pieces
using Pt = std::array<double, 2>;

struct FillMask {
  int x0 = 0, y0 = 0, w = 0, h = 0;   // position and size in pixels of the fill raster (`scale` pixels per page unit)
  int W = 0, H = 0;                   // size of the whole raster the mask was cut from
  double scale = 1;
  std::vector<uint8_t> mask;          // w*h, 1 = filled
  int64_t count = 0;                  // filled pixels
  int64_t total = 0;                  // raster size: "filled the whole page" (an open shape) versus a bounded area
};

struct FillSeed {
  bool isPath = false;
  double x = 0, y = 0;                // click
  std::vector<double> pts;            // dragged path
  FillSide side = FillSide::Left;
};

struct FillMagnet { double maxGap = 14; int snapPx = 3; };
struct FillInfo { double gapUsed = 0; bool moved = false; };
struct FillOutcome {
  bool ok = false;
  QString reason;                     // when !ok (Spanish, ready for the status line)
  FillMask mask;
  FillInfo info;
};

/** Pixels per page unit for a fill of w x h page units, wanting `want` but within the pixel budget (4.5 M pixels). */
double fillScale(double w, double h, double want);
/** Grow a mask by r pixels in every direction (square neighbourhood, two linear passes). */
std::vector<uint8_t> dilate(const std::vector<uint8_t>& src, int W, int H, int r);
/** Ramer-Douglas-Peucker: removes near-duplicate points so a wobbly drag becomes a clean polyline. */
std::vector<double> simplifyPath(const std::vector<double>& p, double eps);
/** The part of a polyline between trim and 1-trim of its length (the whole path when it is short). */
std::vector<double> middleOf(const std::vector<double>& p, double trim);
double pathLength(const std::vector<double>& p);
/** Does a dragged path come back to where it started? */
bool isClosed(const std::vector<double>& p);

struct FloodResult { std::vector<uint8_t> mask; int64_t count = 0; bool leaked = false; };
/** 4-connected scanline flood fill from several seeds (pixel indices) over the free pixels. With abortOnBorder it stops at the raster edge and reports leaked. */
FloodResult floodFill(const std::vector<uint8_t>& free, int W, int H, const std::vector<int>& seeds, bool abortOnBorder = false);

/** Closest point of any stroke within radius of (x, y). Eraser strokes are skipped; generated ink too, except the axes (they attract, grid lines do not). */
std::optional<Pt> nearestOnStrokes(const StrokeRefs& strokes, double x, double y, double radius);
/** Magnet for a dragged line: each end near a stroke is pulled onto it. */
std::vector<double> snapPathEnds(const std::vector<double>& path, const StrokeRefs& strokes, double radius);

/** Which pixels a fill covers. `strokes` are the strokes that stop the fill (normally wallStrokes of the active layer). */
/**
 * `windowed` (the default) lets a click work on a window of the raster around it, growing until the answer is certain; the result is identical to the
 * whole-raster one (`windowed = false`, kept as the reference for the tests), only cheaper on a page with a lot of ink.
 */
FillOutcome computeFill(const StrokeRefs& strokes, double w, double h, double scale, const FillSeed& seed, double gap, const std::optional<FillMagnet>& magnet, bool windowed = true);

// ---------------------------------------------------------------- Riemann rectangles
/** One rectangle: it stands on the segment a-b of the base and reaches h along normal. */
struct RiemannCol { Pt a, b, normal; double h = 0; };
struct RiemannBaseSeg { Pt a, b, normal; };
struct RiemannResult {
  std::vector<RiemannCol> cols;
  int skipped = 0;                    // columns left out: no curve there, or the curve touches the base
  int n = 0;
  double width = 0;                   // base length per column
};
/** Number of columns for a base of length len: asked for, or from a width (equal columns). Clamped to 1..400. */
int riemannColumns(double len, RiemannBy by, int count, double width);
/** Cut a base into n columns of equal length along it (a curve is followed: each column stands on the chord of its stretch). */
std::vector<RiemannBaseSeg> riemannBases(const std::vector<double>& path, int n, FillSide side);
/** Heights of rectangles on `bases`: a ray from each point of a column's base until it meets a stroke; gaps up to `gap` do not let a ray through. */
RiemannResult measureRiemann(const StrokeRefs& strokes, double w, double h, double scale, const std::vector<RiemannBaseSeg>& bases, RiemannRule rule, double gap);
/** The four corners of a column, from the base start counter-clockwise on the page. */
std::array<Pt, 4> colCorners(const RiemannCol& c);
/** Pixels covered by the rectangles (one shape, no seams), or nullopt when none. */
std::optional<FillMask> rectsMask(int W, int H, double scale, const std::vector<RiemannCol>& cols);

/** Paint a mask with the colour (#rrggbb) or a gradient: an image the size of the mask's bounding box. Opacity applied last, once. */
QImage fillImage(const FillMask& m, const QString& color1, const FillOptions& o);

// ---------------------------------------------------------------- the whole gesture
struct FillRequest {
  FillOptions opt;
  QString penColor = "#000000";       // #rrggbb
  double penWidth = 1.41;
  double zoom = 1;                    // screen zoom of the page (thresholds are in screen pixels)
  double devicePixelRatio = 1;
  bool force = false;                 // Shift: no magnet for this fill
};
struct FillStats { double ms = 0; int64_t pixels = 0; bool path = false; bool wholePage = false; double gapUsed = 0; bool moved = false; };
struct RiemannInfo { int n = 0; double width = 0; int drawn = 0; int skipped = 0; std::vector<RiemannCol> cols; std::vector<double> heights() const { std::vector<double> h; for (auto& c : cols) h.push_back(c.h); return h; } };

struct FillResult {
  bool ok = false;                    // false: nothing to add (notices say why)
  std::vector<QString> notices;       // status-line messages, in order (Spanish, like the TS)
  std::vector<Element> added;         // added[0] = the xlFill image (goes at insertAt), the rest (outlines, kept line) go to the end of the layer
  size_t insertAt = 0;                // index in the layer: right after the last xlFill image already there (under the ink, above earlier fills)
  FillStats stats;
  std::optional<RiemannInfo> riemann;
};
/**
 * What the Fill tool does on a click or drag. `drag` is the pointer path in page units (one point for a click; a drag is a path whose screen
 * length is at least 8 px). Pure: the caller inserts `added` (applyFill) and records the undo step.
 */
FillResult runFill(const Layer& layer, double pageW, double pageH, const std::vector<double>& drag, const FillRequest& req);
/** Insert the result into the layer as the TS does; returns the indices of the inserted elements in layer order (for undo). */
std::vector<size_t> applyFill(Layer& layer, const FillResult& r);
