// A small language for sets, written the way it is on a blackboard. Port of app/src/renderer/setexpr.ts.
// Intervals ((−2, 3], [1, ∞), ]a, b[), finite sets ({1, 2, 5}, ∅), names of sets (A, B, C, U, R) and the operations between them. It is forgiving
// about how things are typed (U, u, | or ∪; n, & or ∩; inf, oo or ∞; A', A^c or Aᶜ). The text the user typed is what is kept; this module only
// turns it into a tree, and the tree into a set of numbers or into the regions of a diagram. Positions are UTF-16 indices into the text.
#pragma once
#include "intervals.h"
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace sets {

struct Bound { double v = 0; bool closed = false; QString src; };
struct FinVal { double v = 0; QString src; };

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;
struct Expr {
  enum Kind { Iv, Fin, Ref, Un, In, Df, Sd, Cp } t = Fin;
  Bound lo, hi;                 // Iv
  std::vector<FinVal> vals;     // Fin
  QString name;                 // Ref (upper case)
  ExprPtr a, b;                 // Un/In/Df/Sd (a, b), Cp (a)
};

struct SetError { int pos = 0, len = 0; QString msg; };
struct Parsed {
  bool ok = false;
  ExprPtr expr;                 // when ok
  SetError err;                 // when not ok
};

/** Parse what was typed. A mistake is reported with its place in the text. */
Parsed parseSetExpr(const QString& src);

/** Every name of set used in an expression, in order of appearance. */
QStringList namesIn(const ExprPtr& e);

template <class T> struct Evaluated { bool ok = false; T value{}; QString msg; };

/** The set of real numbers an expression stands for. `R` and `U` are the whole line; other names have no meaning on the line. */
Evaluated<IntervalSet> evalReal(const ExprPtr& e);
/**
 * For a diagram of k sets: which of the 2^k regions an expression shades. A region is a number whose bit i says "inside set i" (0 is outside
 * all the sets, inside the universe). Intervals have no place in a diagram.
 */
Evaluated<std::vector<bool>> evalRegions(const ExprPtr& e, const QStringList& names);

}  // namespace sets
