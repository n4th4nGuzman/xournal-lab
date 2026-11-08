// The interval algebra against point sampling, and the set-expression parser (spellings, numbers, regions, Spanish errors with their position).
// The assertions of the Electron app's e2e ("MATH" block of app.ts: setParse*, setAlgebra*, setRegions, setDeMorgan, setAlgebraBruteForce) are included.
#include "intervals.h"
#include "setexpr.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>

using namespace sets;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 50) { std::printf("FAIL line %d: %s  ", __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } } while (0)

static const char* c(const QString& s) { static QByteArray b[8]; static int i = 0; i = (i + 1) % 8; b[i] = s.toUtf8(); return b[i].constData(); }
static constexpr double INF = INFINITY;

static QString real(const QString& s) {
  const Parsed p = parseSetExpr(s);
  if (!p.ok) return "ERR";
  const auto e = evalReal(p.expr);
  return e.ok ? formatSet(e.value) : QString("EVAL");
}
static QString regions(const QString& s, const QStringList& names) {
  const Parsed p = parseSetExpr(s);
  if (!p.ok) return "ERR";
  const auto e = evalRegions(p.expr, names);
  if (!e.ok) return "EVAL";
  QString out;
  for (bool b : e.value) out += b ? '1' : '0';
  return out;
}

// A set is "normal": sorted, no empty piece, no two pieces that touch or overlap.
static bool isNormal(const IntervalSet& s) {
  for (size_t i = 0; i < s.size(); i++) {
    const Piece& p = s[i];
    if (p.lo > p.hi) return false;
    if (p.lo == p.hi && !(p.loClosed && p.hiClosed)) return false;
    if (!std::isfinite(p.lo) && p.loClosed) return false;
    if (!std::isfinite(p.hi) && p.hiClosed) return false;
    if (i + 1 < s.size()) {
      const Piece& q = s[i + 1];
      if (q.lo < p.hi) return false;
      if (q.lo == p.hi && (q.loClosed || p.hiClosed)) return false;   // would have merged
    }
  }
  return true;
}

struct Atom { double lo, hi; bool lc, hc; };
static bool inAtom(const Atom& a, double x) { return (x > a.lo || (x == a.lo && a.lc)) && (x < a.hi || (x == a.hi && a.hc)); }
using Model = std::vector<Atom>;
static bool inModel(const Model& m, double x) { for (const Atom& a : m) if (inAtom(a, x)) return true; return false; }
static IntervalSet setOf(const Model& m) {
  std::vector<Piece> ps;
  for (const Atom& a : m) ps.push_back(piece(a.lo, a.hi, a.lc, a.hc));
  return normalize(ps);
}

int main() {
  // ---- brute force: every operation agrees with the point-wise rule at 0.25 steps and far away, on random unions of intervals and points
  std::mt19937 rng(12345);
  auto randEnd = [&](bool lowSide) {
    const int k = int(rng() % 9);   // -inf, 0..6 (integers), inf
    if (k == 0) return lowSide ? -INF : 0.0;
    if (k == 8) return lowSide ? 6.0 : INF;
    return double(k - 1);
  };
  auto randModel = [&]() {
    Model m;
    const int n = int(rng() % 4);
    for (int i = 0; i < n; i++) {
      Atom a;
      if (rng() % 4 == 0) { const double v = double(rng() % 7); a = {v, v, true, true}; }
      else {
        a = {randEnd(true), randEnd(false), bool(rng() % 2), bool(rng() % 2)};
        if (a.lo > a.hi) std::swap(a.lo, a.hi);
        if (!std::isfinite(a.lo)) a.lc = false;
        if (!std::isfinite(a.hi)) a.hc = false;
      }
      m.push_back(a);
    }
    return m;
  };
  std::vector<double> xs{-100, 100, -1e6, 1e6};
  for (int i = -8; i <= 32; i++) xs.push_back(i / 4.0);
  int pairs = 0;
  for (int iter = 0; iter < 6000; iter++) {
    const Model ma = randModel(), mb = randModel();
    const IntervalSet a = setOf(ma), b = setOf(mb);
    CHECK(isNormal(a) && isNormal(b), "setOf not normal");
    struct Op { const char* name; IntervalSet r; std::function<bool(bool, bool)> f; };
    const Op ops[] = {{"union", unionOf(a, b), [](bool p, bool q) { return p || q; }},
                      {"intersect", intersect(a, b), [](bool p, bool q) { return p && q; }},
                      {"difference", difference(a, b), [](bool p, bool q) { return p && !q; }},
                      {"symmetric", symmetric(a, b), [](bool p, bool q) { return p != q; }}};
    for (const Op& op : ops) {
      CHECK(isNormal(op.r), "%s result not normal: %s", op.name, c(formatSet(op.r)));
      for (double x : xs) {
        const bool want = op.f(inModel(ma, x), inModel(mb, x));
        if (contains(op.r, x) != want) { CHECK(false, "%s at x=%g: %s %s %s", op.name, x, c(formatSet(a)), op.name, c(formatSet(b))); break; }
      }
    }
    const IntervalSet comp = complement(a);
    CHECK(isNormal(comp), "complement not normal");
    for (double x : xs) CHECK(contains(comp, x) == !inModel(ma, x), "complement at %g of %s", x, c(formatSet(a)));
    CHECK(equals(complement(comp), a), "complement twice: %s -> %s", c(formatSet(a)), c(formatSet(complement(comp))));
    // De Morgan and a few laws
    CHECK(equals(complement(unionOf(a, b)), intersect(complement(a), complement(b))), "De Morgan 1");
    CHECK(equals(complement(intersect(a, b)), unionOf(complement(a), complement(b))), "De Morgan 2");
    CHECK(equals(unionOf(a, b), unionOf(b, a)) && equals(intersect(a, b), intersect(b, a)), "commutative");
    CHECK(equals(symmetric(a, b), difference(unionOf(a, b), intersect(a, b))), "symmetric = union minus intersection");
    // formatted text parses back to the same set
    const QString txt = formatSet(a);
    const Parsed p = parseSetExpr(txt);
    CHECK(p.ok, "'%s' does not parse: %s", c(txt), c(p.err.msg));
    if (p.ok) { const auto e = evalReal(p.expr); CHECK(e.ok && equals(e.value, a), "format/parse round trip of %s gives %s", c(txt), c(e.ok ? formatSet(e.value) : e.msg)); }
    pairs++;
  }
  CHECK(equals(complement({}), fullSet()) && complement(fullSet()).empty(), "complement of empty / full");

  // normalize: touching and overlapping pieces merge only when the shared end is in one of them
  CHECK(formatSet(normalize({piece(0, 1, false, false), piece(1, 2, true, false)})) == "(0, 2)", "touching with one closed end merges");
  CHECK(formatSet(normalize({piece(0, 1, false, false), piece(1, 2, false, false)})) == "(0, 1) ∪ (1, 2)", "touching open ends stay apart");
  CHECK(formatSet(normalize({piece(3, 3, true, true), piece(0, 1, true, true), piece(0.5, 4, false, false)})) == "[0, 4)", "overlap absorbs a point");
  CHECK(normalize({piece(2, 1, true, true), piece(1, 1, true, false)}).empty(), "empty pieces vanish");
  CHECK(formatSet(normalize({piece(-INF, 0, true, true)})) == "(−∞, 0]", "an infinite end is forced open");

  // ---- the e2e checks of the Electron app
  CHECK(real("(-2,3] ∪ {5}") == "(−2, 3] ∪ {5}" && real("(-2,3] U {5}") == real("(-2,3] ∪ {5}") && real("[1,inf)") == "[1, ∞)" && real("[1,∞)") == real("[1,oo)") && real("]1,3[") == "(1, 3)" && real("(1,5; 3]") == "(1.5, 3]", "setParseSpellings");
  CHECK(real("[-π, π/2]") == "[−π, π/2]" && real("[0, 2pi)") == "[0, 2π)" && real("[1/2, 3/2)") == "[0.5, 1.5)" && real("[√2, 3]") == "[1.414214, 3]", "setParseNumbers: %s", c(real("[√2, 3]")));
  CHECK(real("(-2,3] ∩ [0,5]") == "[0, 3]" && real("[0,5] \\ {2}") == "[0, 2) ∪ (2, 5]" && real("R \\ {2}") == "(−∞, 2) ∪ (2, ∞)" && real("[0,2]^c") == "(−∞, 0) ∪ (2, ∞)" &&
        real("[0,5] Δ [3,8]") == "[0, 3) ∪ (5, 8]" && real("(0,1) ∪ [1,2]") == "(0, 2]" && real("(0,1) ∪ (1,2)") == "(0, 1) ∪ (1, 2)", "setAlgebra");
  CHECK(!parseSetExpr("(1,").ok && !parseSetExpr("[3,1]").ok && !parseSetExpr("[1,2] [3,4]").ok && !parseSetExpr("").ok && !parseSetExpr("{1,2").ok && parseSetExpr("[3,1]").err.msg.contains("mayor"), "setParseErrors");
  { const Parsed p = parseSetExpr("A ∩ B"); CHECK(p.ok && !evalReal(p.expr).ok, "setNotOnTheLine"); }
  const QStringList AB{"A", "B"}, ABC{"A", "B", "C"};
  CHECK(regions("A ∪ B", AB) == "0111" && regions("A ∩ B", AB) == "0001" && regions("A ∖ B", AB) == "0100" && regions("A'", AB) == "1010" && regions("A Δ B", AB) == "0110" && regions("(A ∩ B) ∪ C'", ABC) == "11110001", "setRegions");
  CHECK(regions("(A ∪ B)'", AB) == regions("A' ∩ B'", AB) && regions("(A ∩ B)'", AB) == regions("A' ∪ B'", AB) && regions("A Δ B", AB) == regions("(A ∖ B) ∪ (B ∖ A)", AB) &&
        regions("A ∩ B' ∪ C", ABC) == regions("(A ∩ B') ∪ C", ABC), "setDeMorgan");
  CHECK(regions("A ∩ D", AB) == "EVAL" && regions("[1,2]", AB) == "EVAL", "setRegionsErrors");
  {  // brute force through the parser, as the e2e does
    struct S { const char* src; std::function<bool(double)> f; };
    const std::vector<S> ss{{"(-2,3]", [](double x) { return x > -2 && x <= 3; }}, {"[0,5)", [](double x) { return x >= 0 && x < 5; }},
                            {"{1,4}", [](double x) { return x == 1 || x == 4; }}, {"(-inf,0]", [](double x) { return x <= 0; }}};
    std::vector<double> pts;
    for (int i = -40; i <= 120; i++) pts.push_back(i / 10.0);
    for (double v : {0.0, 3.0, 5.0, -2.0, 1.0, 4.0}) pts.push_back(v);
    for (const S& sa : ss) for (const S& sb : ss)
      for (const auto& [o, f] : std::vector<std::pair<QString, std::function<bool(bool, bool)>>>{{"∪", [](bool a, bool b) { return a || b; }}, {"∩", [](bool a, bool b) { return a && b; }}, {"∖", [](bool a, bool b) { return a && !b; }}, {"Δ", [](bool a, bool b) { return a != b; }}}) {
        const Parsed p = parseSetExpr(QString("%1 %2 %3").arg(sa.src, o, sb.src));
        bool ok = p.ok;
        if (ok) { const auto e = evalReal(p.expr); ok = e.ok; for (double x : pts) if (ok && contains(e.value, x) != f(sa.f(x), sb.f(x))) ok = false; }
        CHECK(ok, "parser brute force %s %s %s", sa.src, c(o), sb.src);
      }
  }

  // ---- operator spellings, all equivalent
  const char* unions[] = {"A ∪ B", "A U B", "A u B", "A | B", "A + B", "A or B", "A OR B"};
  const char* inters[] = {"A ∩ B", "A n B", "A & B", "A * B", "A and B"};
  const char* diffs[] = {"A ∖ B", "A \\ B", "A - B", "A − B", "A – B", "A minus B"};
  const char* syms[] = {"A Δ B", "A △ B", "A xor B"};
  const char* comps[] = {"A'", "A’", "A′", "Aᶜ", "A^c", "A^C", "A^{c}", "A^{C}", "¬A", "~A", "!A"};
  for (const char* s : unions) CHECK(regions(s, AB) == "0111", "union spelling %s", s);
  for (const char* s : inters) CHECK(regions(s, AB) == "0001", "intersection spelling %s", s);
  for (const char* s : diffs) CHECK(regions(s, AB) == "0100", "difference spelling %s", s);
  for (const char* s : syms) CHECK(regions(s, AB) == "0110", "symmetric spelling %s", s);
  for (const char* s : comps) CHECK(regions(s, AB) == "1010", "complement spelling %s", QString::fromUtf8(s).toUtf8().constData());
  CHECK(regions("A''", AB) == "0101", "double complement");
  CHECK(regions("a ∩ b", AB) == "0001" && regions("A n B u C", ABC) == regions("(A ∩ B) ∪ C", ABC), "lower case names and precedence (∩ binds tighter than ∪)");
  CHECK(regions("A ∪ B ∖ A", AB) == "0010", "∪ and ∖ at the same level, left to right: (A ∪ B) ∖ A = only B: %s", c(regions("A ∪ B ∖ A", AB)));
  CHECK(regions("A ∩ U", AB) == "0101" && regions("Ω", AB) == "1111", "U and Ω are the universe");
  CHECK(regions("A2 ∪ B", QStringList{"A2", "B"}) == "0111", "subscripted names");

  // ---- numbers and brackets
  CHECK(real("[1,3[") == "[1, 3)" && real("]1,3]") == "(1, 3]", "reversed brackets");
  CHECK(real("(1,5; 3,5)") == "(1.5, 3.5)" && real("{1; 2,5}") == "{1} ∪ {2.5}", "decimal commas with ';': %s", c(real("{1; 2,5}")));
  CHECK(real("[ 1 , 3 ]") == "[1, 3]" && real("[.5, 1.5]") == "[0.5, 1.5]" && real("(−∞, π]") == "(−∞, π]", "spaces, .5, unicode minus");
  CHECK(real("[3π/2, 2π]") == "[3π/2, 2π]" && real("[sqrt(2), 3]") == "[1.414214, 3]" && real("[√(2), 3]") == "[1.414214, 3]" && real("{1/3}") == "{0.333333}", "π fractions, roots, fractions");
  CHECK(real("5") == "{5}" && real("-3") == "{−3}" && real("1/3") == "{0.333333}" && real("2π") == "{2π}" && real("π") == "EVAL", "bare numbers (a lone π is a letter, so a name, exactly as in the TS) are points: %s %s %s %s", c(real("5")), c(real("-3")), c(real("1/3")), c(real("2π")));
  CHECK(real("∅") == "∅" && real("Ø") == "∅" && real("{}") == "∅" && real("R") == "(−∞, ∞)" && real("ℝ") == "(−∞, ∞)", "empty set and R");
  CHECK(real("(0,1)ᶜ ∩ [−1,2]") == "[−1, 0] ∪ [1, 2]", "complement then intersection: %s", c(real("(0,1)ᶜ ∩ [−1,2]")));
  CHECK(formatNumber(M_PI / 2) == "π/2" && formatNumber(-M_PI) == "−π" && formatNumber(3 * M_PI / 2) == "3π/2" && formatNumber(0) == "0" && formatNumber(-INF) == "−∞" && formatNumber(2.5) == "2.5" && formatNumber(100) == "100", "formatNumber");

  // ---- errors: message and place (UTF-16 index and length) as the TypeScript reports them
  struct Err { const char* src; int pos, len; const char* msg; };
  const Err errs[] = {
    {"", 0, 0, "Escribe una expresión"},
    {"   ", 0, 0, "Escribe una expresión"},
    {"[3,1]", 0, 5, "El extremo izquierdo es mayor que el derecho"},
    {"(1,", 0, 1, "Falta cerrar el paréntesis )"},
    {"(A", 0, 1, "Falta cerrar el paréntesis )"},
    {"{1,2", 0, 4, "Falta cerrar la llave }"},
    {"{1,}", 3, 1, "Se esperaba un número dentro de { }"},
    {"{a}", 1, 1, "Se esperaba un número dentro de { }"},
    {"A ∩", 3, 0, "La expresión está incompleta"},
    {"[1,2] ∪", 7, 0, "La expresión está incompleta"},
    {"[1,2] [3,4]", 6, 5, "Sobra «[3,4]»: falta un operador (∪ ∩ ∖ Δ) entre las partes"},
    {"A @ B", 2, 3, "Sobra «@ B»: falta un operador (∪ ∩ ∖ Δ) entre las partes"},
    {"(1,2) )", 6, 1, "Sobra «)»: falta un operador (∪ ∩ ∖ Δ) entre las partes"},
    {"∩ B", 0, 1, "No entiendo «∩»"},
    {"A ∪ ∪ B", 4, 1, "No entiendo «∪»"},
    {"[1,2", 0, 1, "Intervalo mal escrito: falta un extremo o el cierre"},
    {"]1,2", 0, 1, "Intervalo mal escrito"},
  };
  for (const Err& e : errs) {
    if (!e.msg[0]) continue;
    const Parsed p = parseSetExpr(QString::fromUtf8(e.src));
    CHECK(!p.ok, "'%s' should not parse", e.src);
    if (p.ok) continue;
    CHECK(p.err.pos == e.pos && p.err.len == e.len, "'%s' place %d,%d want %d,%d", e.src, p.err.pos, p.err.len, e.pos, e.len);
    CHECK(p.err.msg == QString::fromUtf8(e.msg), "'%s' message '%s'", e.src, c(p.err.msg));
  }
  {  // mistakes found when evaluating, not when parsing
    const Parsed p = parseSetExpr("x ∩ [1,2]");
    CHECK(p.ok && evalReal(p.expr).msg == QString::fromUtf8("«X» no es un conjunto de números: en la recta usa intervalos, {…}, R o ∅"), "name on the line");
    const Parsed q = parseSetExpr("A ∩ D");
    CHECK(q.ok && evalRegions(q.expr, AB).msg == QString::fromUtf8("No hay un conjunto «D» en el diagrama (hay A, B)"), "unknown name in the diagram");
    const Parsed r = parseSetExpr("[1,2]");
    CHECK(r.ok && evalRegions(r.expr, AB).msg == QString::fromUtf8("Un diagrama de conjuntos no lleva intervalos ni números: usa los nombres de los conjuntos"), "interval in the diagram");
    CHECK(namesIn(parseSetExpr("(B ∩ A) ∪ B' ∪ C").expr) == (QStringList{"B", "A", "C"}), "namesIn keeps order of appearance");
  }

  std::printf("sets: %d checks (%d random pairs), %d failures\n", checks, pairs, failures);
  return failures ? 1 : 0;
}
