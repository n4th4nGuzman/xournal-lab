// Sets of real numbers as lists of intervals, with the usual operations. Port of app/src/renderer/intervals.ts.
// An interval has two ends, each open or closed; a single point is the closed interval [a, a]; infinite ends are open. A set is always kept
// normal: sorted, with no empty or touching pieces. Equality of ends is tolerant (EPS, relative) exactly like the TypeScript.
#pragma once
#include <QString>
#include <vector>

namespace sets {

struct Piece { double lo = 0, hi = 0; bool loClosed = false, hiClosed = false; };
using IntervalSet = std::vector<Piece>;

IntervalSet emptySet();
IntervalSet fullSet();                       // (-inf, inf)

/** A piece whose infinite ends are forced open. */
Piece piece(double lo, double hi, bool loClosed, bool hiClosed);

/** Sorted, merged, no empty pieces; touching pieces merge when the shared end is in at least one of them. */
IntervalSet normalize(std::vector<Piece> list);
IntervalSet unionOf(const IntervalSet& a, const IntervalSet& b);
IntervalSet intersect(const IntervalSet& a, const IntervalSet& b);
IntervalSet complement(const IntervalSet& a);
IntervalSet difference(const IntervalSet& a, const IntervalSet& b);
IntervalSet symmetric(const IntervalSet& a, const IntervalSet& b);
bool contains(const IntervalSet& a, double x);
bool equals(const IntervalSet& a, const IntervalSet& b);

/** Numbers as the teacher writes them: "π/2", "1/3", "2.5", "−∞" (U+2212 for the minus sign). */
QString formatNumber(double v);
/** A set as text, in the usual notation: (−2, 3] ∪ {5} ∪ [7, ∞). "∅" when empty. */
QString formatSet(const IntervalSet& a);

}  // namespace sets
