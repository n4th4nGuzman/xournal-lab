#include "intervals.h"
#include <algorithm>
#include <QStringList>
#include <cmath>
#include <limits>

namespace sets {

static constexpr double EPS = 1e-9;
static constexpr double INF = std::numeric_limits<double>::infinity();

// a === b first: it is the only way two infinities of the same sign are the same.
static bool same(double a, double b) {
  return a == b || (std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= EPS * std::max({1.0, std::abs(a), std::abs(b)}));
}

IntervalSet emptySet() { return {}; }
IntervalSet fullSet() { return {Piece{-INF, INF, false, false}}; }

Piece piece(double lo, double hi, bool loClosed, bool hiClosed) {
  return Piece{lo, hi, loClosed && std::isfinite(lo), hiClosed && std::isfinite(hi)};
}

static bool isEmptyPiece(const Piece& p) { return p.lo > p.hi + EPS || (same(p.lo, p.hi) && !(p.loClosed && p.hiClosed)); }

IntervalSet normalize(std::vector<Piece> list) {
  std::vector<Piece> ps;
  for (const Piece& p : list)
    if (!isEmptyPiece(p)) ps.push_back(p);
  // Same order as `a.lo - b.lo || closed first`; comparing instead of subtracting keeps infinities well defined.
  std::stable_sort(ps.begin(), ps.end(), [](const Piece& a, const Piece& b) {
    if (a.lo != b.lo) return a.lo < b.lo;
    return a.loClosed && !b.loClosed;
  });
  IntervalSet out;
  for (const Piece& p : ps) {
    if (!out.empty()) {
      Piece& last = out.back();
      if (p.lo < last.hi - EPS || (same(p.lo, last.hi) && (p.loClosed || last.hiClosed))) {
        if (p.hi > last.hi + EPS) { last.hi = p.hi; last.hiClosed = p.hiClosed; }
        else if (same(p.hi, last.hi)) last.hiClosed = last.hiClosed || p.hiClosed;
        continue;
      }
    }
    out.push_back(p);
  }
  return out;
}

IntervalSet unionOf(const IntervalSet& a, const IntervalSet& b) {
  std::vector<Piece> all(a);
  all.insert(all.end(), b.begin(), b.end());
  return normalize(std::move(all));
}

IntervalSet intersect(const IntervalSet& a, const IntervalSet& b) {
  std::vector<Piece> out;
  for (const Piece& p : a) {
    for (const Piece& q : b) {
      const double lo = std::max(p.lo, q.lo), hi = std::min(p.hi, q.hi);
      const bool loClosed = same(p.lo, q.lo) ? (p.loClosed && q.loClosed) : (p.lo > q.lo ? p.loClosed : q.loClosed);
      const bool hiClosed = same(p.hi, q.hi) ? (p.hiClosed && q.hiClosed) : (p.hi < q.hi ? p.hiClosed : q.hiClosed);
      out.push_back(piece(lo, hi, loClosed, hiClosed));
    }
  }
  return normalize(std::move(out));
}

IntervalSet complement(const IntervalSet& a) {
  std::vector<Piece> out;
  double lo = -INF;
  bool loClosed = false;
  for (const Piece& p : a) {
    out.push_back(piece(lo, p.lo, loClosed, !p.loClosed));
    lo = p.hi;
    loClosed = !p.hiClosed;
  }
  out.push_back(piece(lo, INF, loClosed, false));
  return normalize(std::move(out));
}

IntervalSet difference(const IntervalSet& a, const IntervalSet& b) { return intersect(a, complement(b)); }
IntervalSet symmetric(const IntervalSet& a, const IntervalSet& b) { return unionOf(difference(a, b), difference(b, a)); }

bool contains(const IntervalSet& a, double x) {
  for (const Piece& p : a)
    if ((x > p.lo + EPS || (same(x, p.lo) && p.loClosed)) && (x < p.hi - EPS || (same(x, p.hi) && p.hiClosed))) return true;
  return false;
}

bool equals(const IntervalSet& a, const IntervalSet& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (!(same(a[i].lo, b[i].lo) && same(a[i].hi, b[i].hi) && a[i].loClosed == b[i].loClosed && a[i].hiClosed == b[i].hiClosed)) return false;
  return true;
}

// JS String(+v.toFixed(6)): six decimals, trailing zeros gone, and no "-0".
static QString trimmedFixed(double v) {
  QString s = QString::number(v, 'f', 6);
  if (s.contains('.')) {
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
  }
  if (s == "-0") s = "0";
  return s;
}

QString formatNumber(double v) {
  const QString minus = QStringLiteral("−");
  if (v == INF) return QStringLiteral("∞");
  if (v == -INF) return minus + QStringLiteral("∞");
  if (same(v, 0)) return "0";
  const double k = v / M_PI;
  const bool isInt = std::floor(v) == v;
  for (int d : {1, 2, 3, 4, 6}) {
    const double n = k * d;
    if (std::abs(n) > 1e-9 && same(n, std::floor(n + 0.5)) && std::abs(std::floor(n + 0.5)) <= 12 && !isInt) {
      const long num = long(std::floor(n + 0.5));
      const QString top = std::labs(num) == 1 ? QStringLiteral("π") : QString::number(std::labs(num)) + QStringLiteral("π");
      return (num < 0 ? minus : QString()) + top + (d == 1 ? QString() : "/" + QString::number(d));
    }
  }
  QString s = trimmedFixed(v);
  s.replace('-', minus);
  return s;
}

QString formatSet(const IntervalSet& a) {
  if (a.empty()) return QStringLiteral("∅");
  QStringList parts;
  for (const Piece& p : a) {
    if (same(p.lo, p.hi)) parts << "{" + formatNumber(p.lo) + "}";
    else parts << (p.loClosed ? "[" : "(") + formatNumber(p.lo) + ", " + formatNumber(p.hi) + (p.hiClosed ? "]" : ")");
  }
  return parts.join(QStringLiteral(" ∪ "));
}

}  // namespace sets
