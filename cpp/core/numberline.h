// The real line: an axis with marks and numbers, and on it (or on copies of it, one under the other) the sets of numbers that are written as text:
// points, intervals open or closed or half open, rays to infinity, unions, and holes. Everything is drawn with ordinary pen strokes and texts; the
// text of each expression is the definition (kept as JSON in the attribute xlSet of the "setwitness" stroke). Port of app/src/renderer/numberline.ts.
#pragma once
#include "genobj.h"
#include "intervals.h"
#include <QJsonValue>
#include <QString>
#include <optional>
#include <vector>

namespace sets {

inline const QString SET_ATTR = QStringLiteral("xlSet");

struct LineRow { QString expr, label; };
struct LineSpec {
  std::vector<double> pts;      // x0, y0, x1, y1: the two ends of the axis, where `min` and `max` are
  double min = -5, max = 5, step = 1;
  int every = 1;                // a number is written every this many marks
  bool ticks = true, numbers = true;
  bool arrows = true;           // arrows at both ends of the axis
  bool brackets = false;        // marks: false = circles (open) and dots (closed); true = parentheses and brackets
  std::vector<LineRow> rows;
};

inline constexpr int MAX_ROWS = 3;

LineSpec defaultLine(V2 a = {100, 200}, V2 b = {500, 200});
/** Clamp every field to its range (what sanitizeLine does to any object). */
LineSpec sanitizeLine(const LineSpec& raw);
/** From stored JSON: missing or wrongly typed fields take their defaults. */
LineSpec sanitizeLine(const QJsonValue& raw);
/** The JSON the TypeScript writes for the spec (same keys, same order). */
QString lineSpecJson(const LineSpec& spec);

/** The sets of the rows: a mistake in a row leaves it empty and is reported apart. */
struct LineSetResult { std::optional<IntervalSet> set; QString error; };   // set empty and error null: an empty row
std::vector<LineSetResult> lineSets(const LineSpec& spec);

/** The elements of object `id` (strokes, texts, and last the invisible witness). */
std::vector<Element> buildLine(const QString& id, const LineSpec& spec, const GenStyle& style);
/** Three points that stand for the line's place: its two ends and a point off to the side. */
std::vector<double> witnessOf(const LineSpec& spec);
/** The anchors: the two ends of the axis. */
std::vector<V2> lineAnchors(const LineSpec& spec);
/** The spec with an end of the axis moved. Shift: the angle goes in steps of 15°. Unchanged when the axis would be shorter than 20. */
LineSpec moveLineAnchor(const LineSpec& spec, int i, V2 p, bool shift);

struct LineRead { LineSpec spec; GenStyle style; };
/** The line as it is now: the stored points carried through whatever happened to the witness since. nullopt when `members` is not a line. */
std::optional<LineRead> readLine(const std::vector<const Element*>& members);

}  // namespace sets
