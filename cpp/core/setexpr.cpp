#include "setexpr.h"
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace sets {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

struct Fail { SetError err; };

// Characters are UTF-16 units; -1 stands for "past the end" (JS undefined).
bool isLetter(int c) { return c >= 0 && QChar::isLetter(char32_t(c)); }
bool isDigit(int c) { return c >= '0' && c <= '9'; }

/** `(1,5; 3]` writes decimals with a comma: when ";" separates the ends, a comma between digits is a decimal point (non-overlapping, like a global regexp). */
QString prepare(const QString& src) {
  if (!src.contains(';')) return src;
  QString out;
  int i = 0;
  while (i < src.size()) {
    if (i + 2 < src.size() && isDigit(src[i].unicode()) && src[i + 1] == ',' && isDigit(src[i + 2].unicode())) {
      out += src[i];
      out += '.';
      out += src[i + 2];
      i += 3;
    } else out += src[i++];
  }
  return out;
}

ExprPtr mk(Expr::Kind k, ExprPtr a = nullptr, ExprPtr b = nullptr) {
  auto e = std::make_shared<Expr>();
  e->t = k;
  e->a = std::move(a);
  e->b = std::move(b);
  return e;
}
ExprPtr mkFin(std::vector<FinVal> vals) {
  auto e = std::make_shared<Expr>();
  e->t = Expr::Fin;
  e->vals = std::move(vals);
  return e;
}
ExprPtr mkRef(const QString& name) {
  auto e = std::make_shared<Expr>();
  e->t = Expr::Ref;
  e->name = name;
  return e;
}

struct Parser {
  int i = 0;
  QString s;
  explicit Parser(QString text) : s(std::move(text)) {}

  int at(int k) const { return k >= 0 && k < s.size() ? int(s[k].unicode()) : -1; }
  [[noreturn]] void fail(const QString& msg, int pos, int len = 1) const { throw Fail{{pos, len, msg}}; }
  void ws() { while (i < s.size() && (s[i].isSpace() || s[i].unicode() == 0xFEFF)) i++; }
  int peek() { ws(); return at(i); }
  bool starts(const QString& t) const { return s.mid(i, t.size()) == t; }
  bool eat(const QString& t) { ws(); if (starts(t)) { i += int(t.size()); return true; } return false; }
  bool word(const QString& w) {
    ws();
    const int end = i + int(w.size());
    if (s.mid(i, w.size()).toLower() == w && !isLetter(at(end)) && !isDigit(at(end))) { i = end; return true; }
    return false;
  }

  // ---- numbers: 3, −2.5, 1/3, π/2, 3π/2, √2, ∞
  struct Num { double v = 0; QString src; };

  bool plainNumber(double* out) {
    int j = i;
    while (isDigit(at(j))) j++;
    if (j > i) {
      if (at(j) == '.' && isDigit(at(j + 1))) { j++; while (isDigit(at(j))) j++; }
    } else if (at(j) == '.' && isDigit(at(j + 1))) { j++; while (isDigit(at(j))) j++; }
    else return false;
    *out = s.mid(i, j - i).toDouble();
    i = j;
    return true;
  }

  bool number(Num* res) {
    ws();
    const int start = i;
    double sign = 1;
    const int c0 = at(i);
    if (c0 == '-' || c0 == 0x2212 || c0 == 0x2013) { sign = -1; i++; } else if (c0 == '+') i++;
    ws();
    double v;
    if (eat(u"∞"_s) || word(u"infinity"_s) || word(u"inf"_s) || word(u"oo"_s)) v = INF;
    else if (eat(u"√"_s)) {
      const bool paren = eat(u"("_s);
      double n;
      if (!plainNumber(&n)) { i = start; return false; }
      if (paren && !eat(u")"_s)) { i = start; return false; }
      v = std::sqrt(n);
    } else if (word(u"sqrt"_s)) {
      double n;
      if (!eat(u"("_s)) { i = start; return false; }
      if (!plainNumber(&n) || !eat(u")"_s)) { i = start; return false; }
      v = std::sqrt(n);
    } else {
      double n = 0;
      const bool hasN = plainNumber(&n);
      const bool pi = eat(u"π"_s) || word(u"pi"_s);
      if (!hasN && !pi) { i = start; return false; }
      v = (hasN ? n : 1) * (pi ? M_PI : 1);
    }
    if (v != INF && at(i) == '/' && (isDigit(at(i + 1)) || at(i + 1) == '.')) {
      i++;
      double d;
      if (!plainNumber(&d) || d == 0) { i = start; return false; }
      v /= d;
    }
    res->v = sign * v;
    res->src = s.mid(start, i - start).trimmed();
    return true;
  }

  // ---- intervals and finite sets
  ExprPtr interval() {
    const int save = i;
    ws();
    const int open = at(i);
    if (open != '(' && open != '[' && open != ']') return nullptr;
    i++;
    Num a, b;
    if (!number(&a) || !(eat(u","_s) || eat(u";"_s))) { i = save; return nullptr; }
    const bool hasB = number(&b);
    ws();
    const int close = at(i);
    if (!hasB || (close != ')' && close != ']' && close != '[')) { i = save; return nullptr; }
    i++;
    if (a.v > b.v) fail(u"El extremo izquierdo es mayor que el derecho"_s, save, i - save);
    auto e = std::make_shared<Expr>();
    e->t = Expr::Iv;
    e->lo = {a.v, open == '[', a.src};
    e->hi = {b.v, close == ']', b.src};
    return e;
  }

  ExprPtr finite() {
    const int start = i;
    i++;  // {
    std::vector<FinVal> vals;
    ws();
    if (eat(u"}"_s)) return mkFin(vals);
    for (;;) {
      Num n;
      if (!number(&n)) fail(u"Se esperaba un número dentro de { }"_s, i);
      vals.push_back({n.v, n.src});
      if (eat(u","_s) || eat(u";"_s)) continue;
      if (eat(u"}"_s)) return mkFin(vals);
      fail(u"Falta cerrar la llave }"_s, start, i - start);
    }
  }

  // ---- operands
  ExprPtr atom() {
    const int c = peek();
    if (c < 0) fail(u"La expresión está incompleta"_s, int(s.size()), 0);
    if (c == '(' || c == '[' || c == ']') {
      if (ExprPtr iv = interval()) return iv;
      if (c == '(') {
        const int start = i;
        i++;
        ExprPtr e = unionLevel();
        if (!eat(u")"_s)) fail(u"Falta cerrar el paréntesis )"_s, start, 1);
        return e;
      }
      fail(c == '[' ? u"Intervalo mal escrito: falta un extremo o el cierre"_s : u"Intervalo mal escrito"_s, i);
    }
    if (c == '{') return finite();
    if (c == 0x2205 || c == 0xD8) { i++; return mkFin({}); }                       // ∅ Ø
    if (c == 0xAC || c == '~' || c == '!') { i++; return mk(Expr::Cp, postfix()); }  // ¬ ~ !
    if (c == 0x211D) { i++; return mkRef(u"R"_s); }                                 // ℝ
    if (c == 0x3A9) { i++; return mkRef(u"U"_s); }                                  // Ω
    if (isLetter(c)) {
      // a letter is a set; digits or an underscore after it are a subscript (A1, A_2), and a following letter is the next token
      int j = i + 1;
      int k = j;
      if (at(k) == '_') k++;
      if (isDigit(at(k))) { while (isDigit(at(k))) k++; j = k; }
      const QString name = s.mid(i, j - i);
      i = j;
      return mkRef(name.toUpper());
    }
    // a bare number is the one-point set {x}
    Num n;
    if (number(&n)) return mkFin({{n.v, n.src}});
    fail(u"No entiendo «"_s + QChar(c) + u"»"_s, i);
  }

  ExprPtr postfix() {
    ExprPtr e = atom();
    for (;;) {
      ws();
      if (eat(u"'"_s) || eat(u"’"_s) || eat(u"′"_s) || eat(u"ᶜ"_s) || eat(u"^c"_s) || eat(u"^C"_s) || eat(u"^{c}"_s) || eat(u"^{C}"_s))
        e = mk(Expr::Cp, e);
      else return e;
    }
  }

  /** Intersection: ∩, &, *, n, and. */
  bool interOp() {
    ws();
    if (eat(u"∩"_s) || eat(u"&"_s) || eat(u"*"_s)) return true;
    if (word(u"and"_s)) return true;
    if (at(i) == 'n' && !isLetter(at(i + 1)) && !isDigit(at(i + 1))) { i++; return true; }
    return false;
  }

  ExprPtr inter() {
    ExprPtr e = postfix();
    while (interOp()) e = mk(Expr::In, e, postfix());
    return e;
  }

  /** Union, difference and symmetric difference, at the same level. */
  ExprPtr unionLevel() {
    ExprPtr e = inter();
    for (;;) {
      ws();
      int kind = -1;
      if (eat(u"∪"_s) || eat(u"|"_s) || eat(u"+"_s)) kind = Expr::Un;
      else if (eat(u"∖"_s) || eat(u"\\"_s) || eat(u"−"_s) || eat(u"–"_s)) kind = Expr::Df;
      else if (eat(u"-"_s)) kind = Expr::Df;
      else if (eat(u"Δ"_s) || eat(u"△"_s) || eat(u"▵"_s)) kind = Expr::Sd;
      else if (word(u"or"_s)) kind = Expr::Un;
      else if (word(u"minus"_s)) kind = Expr::Df;
      else if (word(u"xor"_s)) kind = Expr::Sd;
      else if ((at(i) == 'U' || at(i) == 'u') && !isLetter(at(i + 1)) && !isDigit(at(i + 1)) && at(i + 1) != '_') { i++; kind = Expr::Un; }
      if (kind < 0) return e;
      e = mk(Expr::Kind(kind), e, inter());
    }
  }
};

IntervalSet real(const Expr& e) {
  switch (e.t) {
    case Expr::Iv: return normalize({piece(e.lo.v, e.hi.v, e.lo.closed, e.hi.closed)});
    case Expr::Fin: {
      std::vector<Piece> ps;
      for (const FinVal& x : e.vals) ps.push_back(piece(x.v, x.v, true, true));
      return normalize(std::move(ps));
    }
    case Expr::Ref:
      if (e.name == "R" || e.name == "U") return fullSet();
      throw Fail{{0, 0, u"«"_s + e.name + u"» no es un conjunto de números: en la recta usa intervalos, {…}, R o ∅"_s}};
    case Expr::Un: case Expr::In: case Expr::Df: case Expr::Sd: {
      // left operand first, so the first mistake reported is the leftmost one (C++ leaves the order of arguments open)
      const IntervalSet a = real(*e.a);
      const IntervalSet b = real(*e.b);
      if (e.t == Expr::Un) return unionOf(a, b);
      if (e.t == Expr::In) return intersect(a, b);
      if (e.t == Expr::Df) return difference(a, b);
      return symmetric(a, b);
    }
    case Expr::Cp: return complement(real(*e.a));
  }
  return {};
}

bool inRegion(const Expr& e, const QStringList& names, int m) {
  switch (e.t) {
    case Expr::Ref: {
      if (e.name == "U") return true;
      int idx = -1;
      for (int k = 0; k < names.size(); k++)
        if (names[k].toUpper() == e.name) { idx = k; break; }
      if (idx < 0) throw Fail{{0, 0, u"No hay un conjunto «"_s + e.name + u"» en el diagrama (hay "_s + names.join(u", "_s) + u")"_s}};
      return ((m >> idx) & 1) == 1;
    }
    case Expr::Iv: case Expr::Fin:
      throw Fail{{0, 0, u"Un diagrama de conjuntos no lleva intervalos ni números: usa los nombres de los conjuntos"_s}};
    case Expr::Un: return inRegion(*e.a, names, m) || inRegion(*e.b, names, m);
    case Expr::In: return inRegion(*e.a, names, m) && inRegion(*e.b, names, m);
    case Expr::Df: return inRegion(*e.a, names, m) && !inRegion(*e.b, names, m);
    case Expr::Sd: return inRegion(*e.a, names, m) != inRegion(*e.b, names, m);
    case Expr::Cp: return !inRegion(*e.a, names, m);
  }
  return false;
}

void collect(const Expr& e, QStringList& out) {
  switch (e.t) {
    case Expr::Ref: if (!out.contains(e.name)) out << e.name; break;
    case Expr::Cp: collect(*e.a, out); break;
    case Expr::Un: case Expr::In: case Expr::Df: case Expr::Sd: collect(*e.a, out); collect(*e.b, out); break;
    default: break;
  }
}

}  // namespace

Parsed parseSetExpr(const QString& src) {
  const QString text = prepare(src);
  Parser p(text);
  Parsed out;
  try {
    p.ws();
    if (p.i >= text.size()) { out.err = {0, 0, u"Escribe una expresión"_s}; return out; }
    ExprPtr e = p.unionLevel();
    p.ws();
    if (p.i < text.size())
      throw Fail{{p.i, int(text.size()) - p.i, u"Sobra «"_s + text.mid(p.i, 8).trimmed() + u"»: falta un operador (∪ ∩ ∖ Δ) entre las partes"_s}};
    out.ok = true;
    out.expr = std::move(e);
  } catch (const Fail& f) {
    out.err = f.err;
  }
  return out;
}

QStringList namesIn(const ExprPtr& e) {
  QStringList out;
  if (e) collect(*e, out);
  return out;
}

Evaluated<IntervalSet> evalReal(const ExprPtr& e) {
  Evaluated<IntervalSet> r;
  try { r.value = real(*e); r.ok = true; } catch (const Fail& f) { r.msg = f.err.msg; }
  return r;
}

Evaluated<std::vector<bool>> evalRegions(const ExprPtr& e, const QStringList& names) {
  Evaluated<std::vector<bool>> r;
  try {
    const int k = int(names.size());
    std::vector<bool> out;
    for (int m = 0; m < (1 << k); m++) out.push_back(inRegion(*e, names, m));
    r.value = std::move(out);
    r.ok = true;
  } catch (const Fail& f) { r.msg = f.err.msg; }
  return r;
}

}  // namespace sets
