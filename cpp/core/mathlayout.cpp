// TeX-subset formula engine: parser (errors in Spanish), layout following the TeXbook's appendix G, and rendering to QPainter / QImage / PDF.
// Pipeline: text -> Node tree (Parser) -> boxes made of vector shapes (Layout) -> MathBox. Glyph outlines are taken from the system math font once
// and cached; everything drawn afterwards is a path, so a window, a PNG and a PDF all show the same shapes.
#include "mathlayout.h"
#include "genobj.h"
#include <QBuffer>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {

// ===================================================================================================================================
// Classes, fonts and the symbol tables
// ===================================================================================================================================
enum Cls : uint8_t { cOrd, cOp, cBin, cRel, cOpen, cClose, cPunct, cInner };
enum class Fnt : uint8_t { Def, Rm, Bf, It, BfIt, Sf, Tt, Cal, Frak, Bb };
enum Style { D = 0, Dc = 1, T = 2, Tc = 3, S = 4, Sc = 5, SS = 6, SSc = 7 };

struct ParseError { QString msg; int pos; };

struct SymDef { const char* name; char32_t cp; Cls cls; };

// Command symbols. Greek letters are stored as their plain code point and turned into math italic when drawn (so \mathrm{\alpha} can be upright).
const SymDef kSyms[] = {
  // Greek
  {"alpha", U'α', cOrd}, {"beta", U'β', cOrd}, {"gamma", U'γ', cOrd}, {"delta", U'δ', cOrd}, {"epsilon", 0x3F5, cOrd}, {"varepsilon", U'ε', cOrd},
  {"zeta", U'ζ', cOrd}, {"eta", U'η', cOrd}, {"theta", U'θ', cOrd}, {"vartheta", 0x3D1, cOrd}, {"iota", U'ι', cOrd}, {"kappa", U'κ', cOrd},
  {"varkappa", 0x3F0, cOrd}, {"lambda", U'λ', cOrd}, {"mu", U'μ', cOrd}, {"nu", U'ν', cOrd}, {"xi", U'ξ', cOrd}, {"omicron", U'ο', cOrd},
  {"pi", U'π', cOrd}, {"varpi", 0x3D6, cOrd}, {"rho", U'ρ', cOrd}, {"varrho", 0x3F1, cOrd}, {"sigma", U'σ', cOrd}, {"varsigma", U'ς', cOrd},
  {"tau", U'τ', cOrd}, {"upsilon", U'υ', cOrd}, {"phi", 0x3D5, cOrd}, {"varphi", U'φ', cOrd}, {"chi", U'χ', cOrd}, {"psi", U'ψ', cOrd},
  {"omega", U'ω', cOrd}, {"Gamma", U'Γ', cOrd}, {"Delta", U'Δ', cOrd}, {"Theta", U'Θ', cOrd}, {"Lambda", U'Λ', cOrd}, {"Xi", U'Ξ', cOrd},
  {"Pi", U'Π', cOrd}, {"Sigma", U'Σ', cOrd}, {"Upsilon", U'Υ', cOrd}, {"Phi", U'Φ', cOrd}, {"Psi", U'Ψ', cOrd}, {"Omega", U'Ω', cOrd},
  // binary operators
  {"pm", U'±', cBin}, {"mp", U'∓', cBin}, {"times", U'×', cBin}, {"div", U'÷', cBin}, {"cdot", U'⋅', cBin}, {"ast", U'∗', cBin}, {"star", U'⋆', cBin},
  {"circ", U'∘', cBin}, {"bullet", U'∙', cBin}, {"oplus", U'⊕', cBin}, {"ominus", U'⊖', cBin}, {"otimes", U'⊗', cBin}, {"oslash", U'⊘', cBin},
  {"odot", U'⊙', cBin}, {"setminus", U'∖', cBin}, {"smallsetminus", U'∖', cBin}, {"wedge", U'∧', cBin}, {"land", U'∧', cBin}, {"vee", U'∨', cBin},
  {"lor", U'∨', cBin}, {"cap", U'∩', cBin}, {"cup", U'∪', cBin}, {"sqcup", U'⊔', cBin}, {"sqcap", U'⊓', cBin}, {"dagger", U'†', cBin},
  {"ddagger", U'‡', cBin}, {"amalg", U'⨿', cBin}, {"uplus", U'⊎', cBin}, {"diamond", U'⋄', cBin}, {"bigcirc", U'◯', cBin}, {"wr", U'≀', cBin},
  {"boxplus", U'⊞', cBin}, {"boxtimes", U'⊠', cBin}, {"boxminus", U'⊟', cBin}, {"boxdot", U'⊡', cBin}, {"bigtriangleup", U'△', cBin},
  {"bigtriangledown", U'▽', cBin}, {"triangleleft", U'◁', cBin}, {"triangleright", U'▷', cBin}, {"lhd", U'⊲', cBin}, {"rhd", U'⊳', cBin},
  {"intercal", U'⊺', cBin}, {"curlywedge", U'⋏', cBin}, {"curlyvee", U'⋎', cBin}, {"dotplus", U'∔', cBin}, {"centerdot", U'⋅', cBin},
  // relations
  {"neq", U'≠', cRel}, {"ne", U'≠', cRel}, {"approx", U'≈', cRel}, {"cong", U'≅', cRel}, {"equiv", U'≡', cRel}, {"sim", U'∼', cRel},
  {"simeq", U'≃', cRel}, {"propto", U'∝', cRel}, {"doteq", U'≐', cRel}, {"leq", U'≤', cRel}, {"le", U'≤', cRel}, {"geq", U'≥', cRel},
  {"ge", U'≥', cRel}, {"ll", U'≪', cRel}, {"gg", U'≫', cRel}, {"prec", U'≺', cRel}, {"succ", U'≻', cRel}, {"preceq", U'⪯', cRel},
  {"succeq", U'⪰', cRel}, {"nless", U'≮', cRel}, {"ngtr", U'≯', cRel}, {"in", U'∈', cRel}, {"notin", U'∉', cRel}, {"ni", U'∋', cRel},
  {"owns", U'∋', cRel}, {"subset", U'⊂', cRel}, {"supset", U'⊃', cRel}, {"subseteq", U'⊆', cRel}, {"supseteq", U'⊇', cRel},
  {"subsetneq", U'⊊', cRel}, {"supsetneq", U'⊋', cRel}, {"nsubseteq", U'⊈', cRel}, {"nsupseteq", U'⊉', cRel}, {"mid", U'∣', cRel},
  {"nmid", U'∤', cRel}, {"parallel", U'∥', cRel}, {"nparallel", U'∦', cRel}, {"perp", U'⊥', cRel}, {"vdash", U'⊢', cRel}, {"dashv", U'⊣', cRel},
  {"models", U'⊨', cRel}, {"vDash", U'⊨', cRel}, {"asymp", U'≍', cRel}, {"bowtie", U'⋈', cRel}, {"Join", U'⨝', cRel}, {"nleq", U'≰', cRel},
  {"ngeq", U'≱', cRel}, {"leqslant", U'⩽', cRel}, {"geqslant", U'⩾', cRel}, {"lesssim", U'≲', cRel}, {"gtrsim", U'≳', cRel},
  {"approxeq", U'≊', cRel}, {"ncong", U'≇', cRel}, {"nsim", U'≁', cRel}, {"napprox", U'≉', cRel}, {"nequiv", U'≢', cRel},
  {"sqsubset", U'⊏', cRel}, {"sqsupset", U'⊐', cRel}, {"sqsubseteq", U'⊑', cRel}, {"sqsupseteq", U'⊒', cRel}, {"triangleq", U'≜', cRel},
  {"smile", U'⌣', cRel}, {"frown", U'⌢', cRel}, {"Vdash", U'⊩', cRel}, {"trianglelefteq", U'⊴', cRel}, {"trianglerighteq", U'⊵', cRel},
  {"vartriangleleft", U'⊲', cRel}, {"vartriangleright", U'⊳', cRel}, {"lneq", U'⪇', cRel}, {"gneq", U'⪈', cRel}, {"subsetneqq", U'⫋', cRel},
  {"eqsim", U'≂', cRel}, {"coloneqq", U'≔', cRel}, {"eqqcolon", U'≕', cRel}, {"fallingdotseq", U'≒', cRel}, {"risingdotseq", U'≓', cRel},
  // arrows
  {"to", U'→', cRel}, {"rightarrow", U'→', cRel}, {"leftarrow", U'←', cRel}, {"gets", U'←', cRel}, {"leftrightarrow", U'↔', cRel},
  {"mapsto", U'↦', cRel}, {"longrightarrow", U'⟶', cRel}, {"longleftarrow", U'⟵', cRel}, {"longleftrightarrow", U'⟷', cRel},
  {"longmapsto", U'⟼', cRel}, {"Rightarrow", U'⇒', cRel}, {"Leftarrow", U'⇐', cRel}, {"Leftrightarrow", U'⇔', cRel},
  {"Longrightarrow", U'⟹', cRel}, {"Longleftarrow", U'⟸', cRel}, {"Longleftrightarrow", U'⟺', cRel}, {"iff", U'⟺', cRel},
  {"implies", U'⟹', cRel}, {"impliedby", U'⟸', cRel}, {"uparrow", U'↑', cRel}, {"downarrow", U'↓', cRel}, {"updownarrow", U'↕', cRel},
  {"Uparrow", U'⇑', cRel}, {"Downarrow", U'⇓', cRel}, {"Updownarrow", U'⇕', cRel}, {"nearrow", U'↗', cRel}, {"searrow", U'↘', cRel},
  {"nwarrow", U'↖', cRel}, {"swarrow", U'↙', cRel}, {"rightleftharpoons", U'⇌', cRel}, {"leftrightharpoons", U'⇋', cRel},
  {"rightharpoonup", U'⇀', cRel}, {"leftharpoonup", U'↼', cRel}, {"rightharpoondown", U'⇁', cRel}, {"leftharpoondown", U'↽', cRel},
  {"hookrightarrow", U'↪', cRel}, {"hookleftarrow", U'↩', cRel}, {"twoheadrightarrow", U'↠', cRel}, {"twoheadleftarrow", U'↞', cRel},
  {"rightrightarrows", U'⇉', cRel}, {"leftleftarrows", U'⇇', cRel}, {"circlearrowleft", U'↺', cRel}, {"circlearrowright", U'↻', cRel},
  {"rightsquigarrow", U'⇝', cRel}, {"leadsto", U'⇝', cRel}, {"nrightarrow", U'↛', cRel}, {"nleftarrow", U'↚', cRel}, {"nRightarrow", U'⇏', cRel},
  {"nLeftarrow", U'⇍', cRel}, {"nleftrightarrow", U'↮', cRel}, {"nLeftrightarrow", U'⇎', cRel}, {"leftrightarrows", U'⇆', cRel},
  {"rightleftarrows", U'⇄', cRel}, {"Rrightarrow", U'⇛', cRel}, {"Lleftarrow", U'⇚', cRel}, {"curvearrowright", U'↷', cRel},
  {"curvearrowleft", U'↶', cRel}, {"rightarrowtail", U'↣', cRel}, {"leftarrowtail", U'↢', cRel}, {"upharpoonright", U'↾', cRel},
  {"downharpoonright", U'⇂', cRel}, {"Lsh", U'↰', cRel}, {"Rsh", U'↱', cRel}, {"multimap", U'⊸', cRel}, {"dashrightarrow", U'⇢', cRel},
  // ordinary symbols
  {"infty", U'∞', cOrd}, {"partial", U'∂', cOrd}, {"nabla", U'∇', cOrd}, {"emptyset", U'∅', cOrd}, {"varnothing", U'∅', cOrd},
  {"aleph", U'ℵ', cOrd}, {"beth", U'ℶ', cOrd}, {"gimel", U'ℷ', cOrd}, {"daleth", U'ℸ', cOrd}, {"forall", U'∀', cOrd}, {"exists", U'∃', cOrd},
  {"nexists", U'∄', cOrd}, {"neg", U'¬', cOrd}, {"lnot", U'¬', cOrd}, {"top", U'⊤', cOrd}, {"bot", U'⊥', cOrd}, {"therefore", U'∴', cRel},
  {"because", U'∵', cRel}, {"square", U'□', cOrd}, {"Box", U'□', cOrd}, {"blacksquare", U'■', cOrd}, {"triangle", U'△', cOrd},
  {"vartriangle", U'△', cOrd}, {"blacktriangle", U'▲', cOrd}, {"triangledown", U'▽', cOrd}, {"angle", U'∠', cOrd}, {"measuredangle", U'∡', cOrd},
  {"sphericalangle", U'∢', cOrd}, {"degree", U'°', cOrd}, {"ell", U'ℓ', cOrd}, {"hbar", U'ℏ', cOrd}, {"hslash", U'ℏ', cOrd},
  {"imath", 0x1D6A4, cOrd}, {"jmath", 0x1D6A5, cOrd}, {"Re", U'ℜ', cOrd}, {"Im", U'ℑ', cOrd}, {"wp", U'℘', cOrd}, {"mho", U'℧', cOrd},
  {"complement", U'∁', cOrd}, {"clubsuit", U'♣', cOrd}, {"diamondsuit", U'♢', cOrd}, {"heartsuit", U'♡', cOrd}, {"spadesuit", U'♠', cOrd},
  {"flat", U'♭', cOrd}, {"natural", U'♮', cOrd}, {"sharp", U'♯', cOrd}, {"checkmark", U'✓', cOrd}, {"bigstar", U'★', cOrd}, {"S", U'§', cOrd},
  {"P", U'¶', cOrd}, {"copyright", U'©', cOrd}, {"pounds", U'£', cOrd}, {"yen", U'¥', cOrd}, {"backprime", U'‵', cOrd}, {"prime", 0x2032, cOrd}, {"Diamond", U'◊', cOrd},
  {"lozenge", U'◊', cOrd}, {"blacklozenge", U'⧫', cOrd}, {"ldots", U'…', cInner}, {"dots", U'…', cInner}, {"dotsc", U'…', cInner},
  {"cdots", U'⋯', cInner}, {"dotsb", U'⋯', cInner}, {"dotsm", U'⋯', cInner}, {"vdots", U'⋮', cOrd}, {"ddots", U'⋱', cInner}, {"textellipsis", U'…', cInner},
  {"surd", U'√', cOrd}, {"smallint", U'∫', cOp},
  {"backslash", U'\\', cOrd}, {"vert", U'|', cOrd}, {"Vert", U'‖', cOrd}, {"lbrace", U'{', cOpen}, {"rbrace", U'}', cClose}, {"lbrack", U'[', cOpen},
  {"rbrack", U']', cClose}, {"langle", U'⟨', cOpen}, {"rangle", U'⟩', cClose}, {"lfloor", U'⌊', cOpen}, {"rfloor", U'⌋', cClose},
  {"lceil", U'⌈', cOpen}, {"rceil", U'⌉', cClose}, {"lvert", U'|', cOpen}, {"rvert", U'|', cClose}, {"lVert", U'‖', cOpen}, {"rVert", U'‖', cClose},
  {"colon", U':', cPunct}, {"cdotp", U'⋅', cOrd}, {"ldotp", U'.', cPunct}, {"lq", U'‘', cOrd}, {"rq", U'’', cOrd},
  {"{", U'{', cOpen}, {"}", U'}', cClose}, {"|", U'‖', cOrd}, {"$", U'$', cOrd}, {"%", U'%', cOrd}, {"&", U'&', cOrd}, {"#", U'#', cOrd},
  {"_", U'_', cOrd}, {"\\textbackslash", U'\\', cOrd},
};

struct OpSym { const char* name; char32_t cp; };
const OpSym kBigOps[] = {
  {"sum", U'∑'}, {"prod", U'∏'}, {"coprod", U'∐'}, {"int", U'∫'}, {"iint", U'∬'}, {"iiint", U'∭'}, {"oint", U'∮'}, {"oiint", U'∯'}, {"oiiint", U'∰'},
  {"bigcup", U'⋃'}, {"bigcap", U'⋂'}, {"bigsqcup", U'⨆'}, {"bigvee", U'⋁'}, {"bigwedge", U'⋀'}, {"bigoplus", U'⨁'}, {"bigotimes", U'⨂'},
  {"bigodot", U'⨀'}, {"biguplus", U'⨄'},
};

struct NamedOpDef { const char* name; const char* text; bool limits; };
const NamedOpDef kNamedOps[] = {
  {"sin", "sin", false}, {"cos", "cos", false}, {"tan", "tan", false}, {"cot", "cot", false}, {"sec", "sec", false}, {"csc", "csc", false},
  {"arcsin", "arcsin", false}, {"arccos", "arccos", false}, {"arctan", "arctan", false}, {"sinh", "sinh", false}, {"cosh", "cosh", false},
  {"tanh", "tanh", false}, {"coth", "coth", false}, {"log", "log", false}, {"ln", "ln", false}, {"lg", "lg", false}, {"exp", "exp", false},
  {"deg", "deg", false}, {"dim", "dim", false}, {"ker", "ker", false}, {"hom", "hom", false}, {"arg", "arg", false}, {"Pr", "Pr", true},
  {"det", "det", true}, {"gcd", "gcd", true}, {"inf", "inf", true}, {"sup", "sup", true}, {"lim", "lim", true}, {"max", "max", true},
  {"min", "min", true}, {"limsup", "lim sup", true}, {"liminf", "lim inf", true}, {"injlim", "inj lim", true},
  {"projlim", "proj lim", true}, {"varlimsup", "lim sup", true}, {"varliminf", "lim inf", true},
};

// ===================================================================================================================================
// Glyphs: outlines of the math font, measured once at a large size
// ===================================================================================================================================
struct GInfo {
  std::shared_ptr<const QPainterPath> path;   // em = 1, y down, baseline at 0
  double adv = 0, x0 = 0, x1 = 0, top = 0, bot = 0;   // advance, ink extent; top/bot are positive up / down from the baseline
};

const QFont& refFont() {
  static const QFont f = [] {
    QFont q;
    q.setFamilies({"Cambria Math", "STIX Two Math", "Latin Modern Math", "XITS Math", "Noto Sans Math", "DejaVu Math TeX Gyre", "Segoe UI Symbol",
                   "Segoe UI", "DejaVu Serif", "Times New Roman", "serif"});
    q.setPixelSize(100);
    q.setHintingPreference(QFont::PreferNoHinting);
    q.setStyleStrategy(QFont::PreferOutline);
    return q;
  }();
  return f;
}

GInfo makeGlyph(const QString& s) {
  GInfo g;
  QPainterPath raw;
  if (s.size() == 1 && s[0].unicode() == 0x2032) {
    // the prime is drawn: the font's one sits where a superscript would put it, and the shift would be applied twice
    QPainterPath p;
    p.moveTo(0.02, -0.50); p.lineTo(0.17, -0.50); p.lineTo(0.11, -0.28); p.lineTo(0.03, 0.0); p.closeSubpath();
    g.path = std::make_shared<QPainterPath>(p);
    g.adv = 0.2; g.x0 = 0.02; g.x1 = 0.17; g.top = 0.5; g.bot = 0;
    return g;
  }
  raw.addText(0, 0, refFont(), s);
  const QTransform sc = QTransform::fromScale(0.01, 0.01);
  const QPainterPath p = sc.map(raw);
  g.adv = QFontMetricsF(refFont()).horizontalAdvance(s) / 100.0;
  const QRectF r = p.boundingRect();
  if (p.isEmpty() || r.isEmpty()) { g.x0 = g.x1 = 0; g.top = g.bot = 0; }
  else { g.x0 = r.left(); g.x1 = r.right(); g.top = std::max(0.0, -r.top()); g.bot = std::max(0.0, r.bottom()); }
  g.path = std::make_shared<QPainterPath>(p);
  return g;
}

GInfo ginfo(const QString& s) {
  static QMutex mu;
  static QHash<QString, GInfo> cache;
  QMutexLocker lock(&mu);
  auto it = cache.constFind(s);
  if (it != cache.constEnd()) return *it;
  const GInfo g = makeGlyph(s);
  cache.insert(s, g);
  return g;
}

QString ucs(char32_t c) { return QString::fromUcs4(&c, 1); }

// Constants of the font, measured from its own glyphs so the maths axis, x-height and rule thickness match the shapes that are drawn.
struct Kal { double axis = 0.25, xh = 0.45, rule = 0.045; };
const Kal& kal() {
  static const Kal k = [] {
    Kal r;
    const GInfo plus = ginfo("+");
    if (plus.top + plus.bot > 0.1) r.axis = std::clamp((plus.top - plus.bot) / 2, 0.2, 0.3);
    const GInfo x = ginfo("x");
    if (x.top > 0.1) r.xh = std::clamp(x.top, 0.35, 0.55);
    const GInfo minus = ginfo(ucs(0x2212));
    if (minus.top + minus.bot > 0.01) r.rule = std::clamp(minus.top + minus.bot, 0.035, 0.06);
    return r;
  }();
  return k;
}

// Math alphanumerics (U+1D400...) for the font commands; exceptions are the letters Unicode already had elsewhere.
QString glyphFor(char32_t cp, Fnt f) {
  const bool up = cp >= 'A' && cp <= 'Z', lo = cp >= 'a' && cp <= 'z', dg = cp >= '0' && cp <= '9';
  if (up || lo) {
    const int i = int(up ? cp - 'A' : cp - 'a');
    auto pick = [&](char32_t baseUp, char32_t baseLo, const std::vector<std::pair<int, char32_t>>& ex, bool isUp) {
      for (const auto& e : ex)
        if (e.first == i + (isUp ? 0 : 100)) return ucs(e.second);
      return ucs((isUp ? baseUp : baseLo) + i);
    };
    switch (f) {
      case Fnt::Def: case Fnt::It: return lo && i == 7 ? ucs(0x210E) : ucs((up ? 0x1D434 : 0x1D44E) + i);
      case Fnt::Rm: return ucs(cp);
      case Fnt::Bf: return ucs((up ? 0x1D400 : 0x1D41A) + i);
      case Fnt::BfIt: return ucs((up ? 0x1D468 : 0x1D482) + i);
      case Fnt::Sf: return ucs((up ? 0x1D5A0 : 0x1D5BA) + i);
      case Fnt::Tt: return ucs((up ? 0x1D670 : 0x1D68A) + i);
      case Fnt::Cal: return pick(0x1D49C, 0x1D4B6, {{1, 0x212C}, {4, 0x2130}, {5, 0x2131}, {7, 0x210B}, {8, 0x2110}, {11, 0x2112}, {12, 0x2133}, {17, 0x211B},
                                                     {104, 0x212F}, {106, 0x210A}, {114, 0x2134}}, up);
      case Fnt::Frak: return pick(0x1D504, 0x1D51E, {{2, 0x212D}, {7, 0x210C}, {8, 0x2111}, {17, 0x211C}, {25, 0x2128}}, up);
      case Fnt::Bb: return pick(0x1D538, 0x1D552, {{2, 0x2102}, {7, 0x210D}, {13, 0x2115}, {15, 0x2119}, {16, 0x211A}, {17, 0x211D}, {25, 0x2124}}, up);
    }
  }
  if (dg) {
    const int i = int(cp - '0');
    switch (f) {
      case Fnt::Bf: case Fnt::BfIt: return ucs(0x1D7CE + i);
      case Fnt::Sf: return ucs(0x1D7E2 + i);
      case Fnt::Tt: return ucs(0x1D7F6 + i);
      case Fnt::Bb: return ucs(0x1D7D8 + i);
      default: return ucs(cp);
    }
  }
  const bool greekLo = (cp >= 0x3B1 && cp <= 0x3C9) || cp == 0x3D1 || cp == 0x3D5 || cp == 0x3D6 || cp == 0x3F0 || cp == 0x3F1 || cp == 0x3F5;
  if (greekLo) {
    if (f == Fnt::Rm) return ucs(cp);
    char32_t it;
    if (cp >= 0x3B1 && cp <= 0x3C9) it = 0x1D6FC + (cp - 0x3B1);
    else switch (cp) { case 0x3F5: it = 0x1D716; break; case 0x3D1: it = 0x1D717; break; case 0x3F0: it = 0x1D718; break; case 0x3D5: it = 0x1D719; break;
                       case 0x3F1: it = 0x1D71A; break; default: it = 0x1D71B; }
    return ucs(f == Fnt::BfIt ? it + 0x3A : it);
  }
  if (cp >= 0x391 && cp <= 0x3A9 && (f == Fnt::Bf || f == Fnt::BfIt)) return ucs(0x1D6A8 + (cp - 0x391));
  return ucs(cp);
}

// ===================================================================================================================================
// Syntax tree
// ===================================================================================================================================
struct Node;
using NodeP = std::shared_ptr<Node>;

struct MatData {
  QString env;
  std::vector<std::vector<NodeP>> rows;
  QString align;                 // one of l c r per column
  std::vector<int> vbars;        // rules before each column (size columns + 1)
  std::vector<int> hlines;       // \hline before each row (size rows + 1)
  QString ldelim, rdelim;
  int cols() const { return int(align.size()); }
};

struct Node {
  enum K { Atom, Group, Frac, Sqrt, Scripts, BigOp, NamedOp, Accent, Line, Brace, Stack, XArrow, Left, BigDelim, Matrix, Text, Space, StyleSw, Color,
           FontWrap, Phantom, Boxed, Cancel, ClassWrap } k = Group;
  Cls cls = cOrd;
  char32_t cp = 0;
  QString s;
  NodeP a, b, c;
  std::vector<NodeP> list;
  std::vector<QString> strs;
  int iv = 0;
  double dv = 0;
  int pos = 0;
  std::shared_ptr<MatData> mat;
  QColor color;
};

NodeP mk(Node::K k, int pos = 0) { auto n = std::make_shared<Node>(); n->k = k; n->pos = pos; return n; }
NodeP mkGroup(std::vector<NodeP> l) { auto n = mk(Node::Group); n->list = std::move(l); return n; }
NodeP mkAtom(char32_t cp, Cls cls, bool fixed, int pos = 0) { auto n = mk(Node::Atom, pos); n->cp = cp; n->cls = cls; n->iv = fixed ? 1 : 0; return n; }

// ===================================================================================================================================
// Parser
// ===================================================================================================================================
struct Tok {
  enum T { End, Ch, Cmd } t = End;
  QString cmd;
  char32_t ch = 0;
  int pos = 0;
  bool isCh(char32_t c) const { return t == Ch && ch == c; }
  bool isCmd(const char* n) const { return t == Cmd && cmd == QLatin1String(n); }
};

const QHash<QString, const SymDef*>& symTable() {
  static const QHash<QString, const SymDef*> h = [] {
    QHash<QString, const SymDef*> m;
    for (const SymDef& d : kSyms) m.insert(QString::fromLatin1(d.name), &d);
    return m;
  }();
  return h;
}
const SymDef* symByCp(char32_t cp) {
  static const QHash<char32_t, const SymDef*> h = [] {
    QHash<char32_t, const SymDef*> m;
    for (const SymDef& d : kSyms)
      if (!m.contains(d.cp)) m.insert(d.cp, &d);
    return m;
  }();
  auto it = h.constFind(cp);
  return it == h.constEnd() ? nullptr : *it;
}

QString canonDelim(const Tok& t) {
  if (t.t == Tok::Ch) {
    switch (t.ch) {
      case '(': case ')': case '[': case ']': case '|': case '.': case '/': return ucs(t.ch);
      case '<': return ucs(0x27E8);
      case '>': return ucs(0x27E9);
      case 0x27E8: case 0x27E9: case 0x2016: case 0x230A: case 0x230B: case 0x2308: case 0x2309: return ucs(t.ch);
      default: return QString();
    }
  }
  if (t.t == Tok::Cmd) {
    static const QHash<QString, QString> m = {
      {"{", "{"}, {"}", "}"}, {"lbrace", "{"}, {"rbrace", "}"}, {"|", QString(QChar(0x2016))}, {"Vert", QString(QChar(0x2016))},
      {"vert", "|"}, {"lvert", "|"}, {"rvert", "|"}, {"lVert", QString(QChar(0x2016))}, {"rVert", QString(QChar(0x2016))},
      {"langle", QString(QChar(0x27E8))}, {"rangle", QString(QChar(0x27E9))}, {"lfloor", QString(QChar(0x230A))}, {"rfloor", QString(QChar(0x230B))},
      {"lceil", QString(QChar(0x2308))}, {"rceil", QString(QChar(0x2309))}, {"lbrack", "["}, {"rbrack", "]"}, {"backslash", "\\"},
      {"uparrow", QString(QChar(0x2191))}, {"downarrow", QString(QChar(0x2193))}, {"updownarrow", QString(QChar(0x2195))},
      {"Uparrow", QString(QChar(0x21D1))}, {"Downarrow", QString(QChar(0x21D3))}, {"Updownarrow", QString(QChar(0x21D5))},
      {"lgroup", "("}, {"rgroup", ")"}, {"lmoustache", "("}, {"rmoustache", ")"},
    };
    return m.value(t.cmd);
  }
  return QString();
}

class Parser {
 public:
  explicit Parser(const QString& s) : s_(s) {}

  NodeP parseAll() {
    auto rows = parseRows(false);
    const Tok t = peek();
    if (t.t != Tok::End) closerError(t, nullptr);
    if (rows.rows.size() == 1 && rows.rows[0].size() == 1 && rows.hlines.empty()) return mkGroup(rows.rows[0][0]->list);
    return makeMatrix("gathered", std::move(rows), QString(), 0);
  }

 private:
  const QString& s_;
  int p_ = 0;

  struct Rows { std::vector<std::vector<NodeP>> rows; std::vector<int> hlines; };

  [[noreturn]] void fail(const QString& msg, int pos) const { throw ParseError{msg, pos}; }

  Tok next() {
    for (;;) {
      while (p_ < s_.size() && s_[p_].isSpace()) p_++;
      if (p_ < s_.size() && s_[p_] == '%') { while (p_ < s_.size() && s_[p_] != '\n') p_++; continue; }
      break;
    }
    Tok t;
    t.pos = p_;
    if (p_ >= s_.size()) return t;
    const QChar c = s_[p_];
    if (c == '\\') {
      if (p_ + 1 >= s_.size()) fail(QStringLiteral("La barra \\ del final no tiene comando"), p_);
      p_++;
      t.t = Tok::Cmd;
      if (s_[p_].isLetter() && s_[p_].unicode() < 128) {
        int q = p_;
        while (q < s_.size() && s_[q].isLetter() && s_[q].unicode() < 128) q++;
        t.cmd = s_.mid(p_, q - p_);
        p_ = q;
      } else {
        t.cmd = QString(s_[p_]);
        p_++;
      }
      return t;
    }
    t.t = Tok::Ch;
    if (c.isHighSurrogate() && p_ + 1 < s_.size() && s_[p_ + 1].isLowSurrogate()) { t.ch = QChar::surrogateToUcs4(c, s_[p_ + 1]); p_ += 2; }
    else { t.ch = c.unicode(); p_++; }
    return t;
  }
  Tok peek() { const int save = p_; Tok t = next(); p_ = save; return t; }

  static bool isCloser(const Tok& t) {
    if (t.t == Tok::End) return true;
    if (t.t == Tok::Ch) return t.ch == '}' || t.ch == '&';
    return t.cmd == "right" || t.cmd == "middle" || t.cmd == "end" || t.cmd == "\\" || t.cmd == "cr";
  }

  [[noreturn]] void closerError(const Tok& t, const char* inside) {
    if (t.t == Tok::End) fail(QStringLiteral("Fin inesperado de la fórmula"), t.pos);
    if (t.isCh('}')) fail(QStringLiteral("Llave de cierre } sin su llave de apertura"), t.pos);
    if (t.isCh('&')) fail(QStringLiteral("El símbolo & solo se puede usar dentro de matrices y entornos alineados"), t.pos);
    if (t.isCmd("right")) fail(QStringLiteral("\\right sin su \\left"), t.pos);
    if (t.isCmd("middle")) fail(QStringLiteral("\\middle solo se puede usar entre \\left y \\right"), t.pos);
    if (t.isCmd("end")) fail(QStringLiteral("\\end sin su \\begin"), t.pos);
    (void)inside;
    fail(QStringLiteral("El salto de línea \\\\ no se puede usar aquí"), t.pos);
  }

  // ---- lists --------------------------------------------------------------------------------------------------------------------
  std::vector<NodeP> parseList() {
    std::vector<NodeP> out;
    for (;;) {
      const Tok t = peek();
      if (isCloser(t)) break;
      if (t.t == Tok::Cmd) {
        const QString& n = t.cmd;
        auto wrapRest = [&](NodeP w) { w->a = mkGroup(parseList()); out.push_back(w); };
        if (n == "displaystyle" || n == "textstyle" || n == "scriptstyle" || n == "scriptscriptstyle") {
          next();
          auto w = mk(Node::StyleSw, t.pos);
          w->iv = n == "displaystyle" ? D : n == "textstyle" ? T : n == "scriptstyle" ? S : SS;
          wrapRest(w);
          break;
        }
        if (n == "over" || n == "atop" || n == "choose" || n == "brace" || n == "brack") {
          next();
          auto f = mk(Node::Frac, t.pos);
          f->a = mkGroup(std::move(out));
          f->b = mkGroup(parseList());
          f->dv = n == "over" ? 0 : 1;
          out.clear();
          if (n == "choose") f->iv = 4;
          if (n == "brace" || n == "brack") {
            auto lft = mk(Node::Left, t.pos);
            lft->strs = {n == "brace" ? "{" : "[", n == "brace" ? "}" : "]"};
            lft->list = {mkGroup({f})};
            out.push_back(lft);
          } else {
            out.push_back(f);
          }
          break;
        }
        if (n == "color") {
          next();
          auto w = mk(Node::Color, t.pos);
          w->color = parseColor(t.pos);
          wrapRest(w);
          break;
        }
        static const QHash<QString, int> oldFonts = {{"rm", int(Fnt::Rm)}, {"bf", int(Fnt::Bf)}, {"it", int(Fnt::It)}, {"sf", int(Fnt::Sf)},
                                                     {"tt", int(Fnt::Tt)}, {"cal", int(Fnt::Cal)}, {"bfseries", int(Fnt::Bf)}, {"mathnormal", int(Fnt::Def)}};
        if (oldFonts.contains(n) && n != "mathnormal") {
          next();
          auto w = mk(Node::FontWrap, t.pos);
          w->iv = oldFonts.value(n);
          wrapRest(w);
          break;
        }
      }
      out.push_back(parseItem());
    }
    return out;
  }

  NodeP parseItem() {
    Tok t = next();
    NodeP base;
    if (t.isCh('^') || t.isCh('_')) { p_ = t.pos; base = mkGroup({}); }
    else base = nucleus(t);
    NodeP sub, sup;
    for (;;) {
      const Tok q = peek();
      if (q.isCh('^')) {
        next();
        if (sup) fail(QStringLiteral("Superíndice doble: usa llaves, por ejemplo {x^a}^b"), q.pos);
        sup = scriptArg(q, "el superíndice (^)");
      } else if (q.isCh('_')) {
        next();
        if (sub) fail(QStringLiteral("Subíndice doble: usa llaves, por ejemplo {x_a}_b"), q.pos);
        sub = scriptArg(q, "el subíndice (_)");
      } else if (q.isCh('\'') || q.isCh(0x2032)) {
        if (sup) fail(QStringLiteral("Superíndice doble: usa llaves, por ejemplo {x^a}^b"), q.pos);
        std::vector<NodeP> primes;
        for (;;) {
          const Tok r = peek();
          if (!(r.isCh('\'') || r.isCh(0x2032))) break;
          next();
          primes.push_back(mkAtom(0x2032, cOrd, true, r.pos));
        }
        if (peek().isCh('^')) {
          const Tok c = next();
          primes.push_back(scriptArg(c, "el superíndice (^)"));
        }
        sup = mkGroup(std::move(primes));
      } else if (q.isCmd("limits") || q.isCmd("nolimits") || q.isCmd("displaylimits")) {
        next();
        if (base->k == Node::BigOp || base->k == Node::NamedOp) base->iv = q.cmd == "limits" ? 1 : q.cmd == "nolimits" ? 2 : 0;
      } else {
        break;
      }
    }
    if (!sub && !sup) return base;
    auto sc = mk(Node::Scripts, t.pos);
    sc->a = base; sc->b = sub; sc->c = sup;
    return sc;
  }

  NodeP scriptArg(const Tok& at, const char* what) {
    const Tok t = peek();
    if (isCloser(t)) fail(QStringLiteral("Falta el argumento de %1").arg(QString::fromUtf8(what)), at.pos);
    return singleArg(QStringLiteral("^"), at.pos);
  }

  /** One argument: a {group} or a single token. */
  NodeP singleArg(const QString& cmd, int pos) {
    const Tok t = peek();
    if (isCloser(t)) fail(QStringLiteral("Falta el argumento de %1").arg(cmd.startsWith('^') || cmd.startsWith('_') ? cmd : "\\" + cmd), pos);
    next();
    if (t.isCh('^') || t.isCh('_')) fail(QStringLiteral("Falta el argumento de %1").arg("\\" + cmd), pos);
    return nucleus(t);
  }

  NodeP braced(int openPos) {
    auto g = mkGroup(parseList());
    const Tok c = next();
    if (c.t == Tok::End) fail(QStringLiteral("Falta cerrar la llave { de la posición %1: se esperaba }").arg(openPos + 1), c.pos);
    if (!c.isCh('}')) closerError(c, "{");
    return g;
  }

  /** Raw text between braces (for \text, \operatorname, environment names...): nested braces are kept, the outer ones dropped. */
  QString rawBraced(const QString& cmd) {
    while (p_ < s_.size() && s_[p_].isSpace()) p_++;
    if (p_ >= s_.size() || s_[p_] != '{') fail(QStringLiteral("Falta el argumento de \\%1: se esperaba {").arg(cmd), p_);
    const int open = p_++;
    int depth = 1;
    QString out;
    while (p_ < s_.size()) {
      const QChar c = s_[p_];
      if (c == '\\' && p_ + 1 < s_.size()) {
        const QChar d = s_[p_ + 1];
        if (d == '{' || d == '}' || d == '&' || d == '%' || d == '$' || d == '#' || d == '_') { out += d; p_ += 2; continue; }
        if (d == ' ') { out += ' '; p_ += 2; continue; }
        if (d == '\\') { out += ' '; p_ += 2; continue; }
        out += c; p_++; continue;
      }
      if (c == '{') depth++;
      if (c == '}' && --depth == 0) { p_++; return out; }
      out += c;
      p_++;
    }
    fail(QStringLiteral("Falta cerrar la llave { de la posición %1: se esperaba }").arg(open + 1), p_);
  }

  QColor parseColor(int pos) {
    const QString name = rawBraced("color").trimmed();
    QColor c(name.startsWith('#') || name.contains(QRegularExpression("^[0-9a-fA-F]{6}$")) ? (name.startsWith('#') ? name : "#" + name) : name.toLower());
    if (!c.isValid()) fail(QStringLiteral("Color desconocido: %1").arg(name), pos);
    return c;
  }

  // ---- delimiters, environments -------------------------------------------------------------------------------------------------------
  QString delimArg(const QString& cmd, int pos) {
    const Tok t = next();
    const QString d = canonDelim(t);
    if (d.isEmpty()) fail(QStringLiteral("Delimitador no válido después de \\%1").arg(cmd), t.t == Tok::End ? pos : t.pos);
    return d;
  }

  NodeP parseLeft(const Tok& t) {
    auto n = mk(Node::Left, t.pos);
    n->strs.push_back(delimArg("left", t.pos));
    for (;;) {
      n->list.push_back(mkGroup(parseList()));
      const Tok c = next();
      if (c.isCmd("middle")) { n->strs.push_back(delimArg("middle", c.pos)); continue; }
      if (c.isCmd("right")) { n->strs.push_back(delimArg("right", c.pos)); break; }
      if (c.t == Tok::End) fail(QStringLiteral("Falta \\right para cerrar el \\left de la posición %1").arg(t.pos + 1), c.pos);
      closerError(c, "left");
    }
    return n;
  }

  Rows parseRows(bool amp) {
    Rows r;
    std::vector<NodeP> row;
    bool any = false;
    auto hlines = [&]() {
      int cnt = 0;
      for (;;) {
        const Tok t = peek();
        if (t.isCmd("hline") || t.isCmd("hdashline")) { next(); cnt++; } else break;
      }
      return cnt;
    };
    int pendingH = hlines();
    for (;;) {
      auto cell = mkGroup(parseList());
      row.push_back(cell);
      any = true;
      const Tok t = peek();
      if (t.isCh('&')) {
        if (!amp) closerError(t, nullptr);
        next();
        continue;
      }
      if (t.isCmd("\\") || t.isCmd("cr")) {
        next();
        if (peek().isCh('[')) {   // \\[2pt]: the extra space is not used
          const int save = p_;
          next();
          while (p_ < s_.size() && s_[p_] != ']') p_++;
          if (p_ >= s_.size()) { p_ = save; } else p_++;
        }
        r.hlines.resize(r.rows.size() + 1, 0);
        r.hlines[r.rows.size()] = pendingH;
        r.rows.push_back(std::move(row));
        row.clear();
        pendingH = hlines();
        const Tok after = peek();
        if (isCloser(after) && !after.isCh(0x26) && !after.isCmd("\\") && !after.isCmd("cr")) {   // a trailing \\ does not start another row
          r.hlines.resize(r.rows.size() + 1, 0);
          r.hlines[r.rows.size()] = pendingH;
          return r;
        }
        continue;
      }
      break;
    }
    // the last row, when it was not ended by a row break
    bool blank = row.size() == 1 && row[0]->list.empty();
    if (any && (!blank || r.rows.empty())) {
      r.hlines.resize(r.rows.size() + 1, 0);
      r.hlines[r.rows.size()] = pendingH;
      r.rows.push_back(std::move(row));
      r.hlines.resize(r.rows.size() + 1, 0);
      r.hlines[r.rows.size()] = 0;
    }
    bool noH = true;
    for (int h : r.hlines) if (h) noH = false;
    if (noH) r.hlines.clear();
    return r;
  }

  NodeP makeMatrix(const QString& env, Rows rows, const QString& spec, int pos) {
    auto n = mk(Node::Matrix, pos);
    auto m = std::make_shared<MatData>();
    m->env = env;
    int ncols = 0;
    for (const auto& r : rows.rows) ncols = std::max(ncols, int(r.size()));
    m->rows = std::move(rows.rows);
    m->hlines = std::move(rows.hlines);
    m->hlines.resize(m->rows.size() + 1, 0);
    m->vbars.assign(size_t(ncols) + 1, 0);
    if (env == "array" || env == "tabular") {
      int col = 0;
      QString al;
      for (int i = 0; i < spec.size(); i++) {
        const QChar c = spec[i];
        if (c == '|') { if (col < int(m->vbars.size())) m->vbars[size_t(col)]++; }
        else if (c == 'l' || c == 'c' || c == 'r') { al += c; col++; }
        else if (c == 'p' || c == 'm' || c == 'b' || c == '@' || c == '!' || c == '>' || c == '<') {
          if (c == 'p' || c == 'm' || c == 'b') { al += 'l'; col++; }
          if (i + 1 < spec.size() && spec[i + 1] == '{') { int d = 0; do { if (spec[i + 1] == '{') d++; if (spec[i + 1] == '}') d--; i++; } while (d > 0 && i + 1 < spec.size()); }
        }
      }
      while (al.size() < ncols) al += 'c';
      if (m->vbars.size() < size_t(al.size()) + 1) m->vbars.resize(size_t(al.size()) + 1, 0);
      m->align = al;
    } else if (env == "aligned" || env == "align" || env == "align*" || env == "split" || env == "alignat" || env == "eqnarray" || env == "flalign") {
      QString al;
      for (int i = 0; i < ncols; i++) al += (i % 2 == 0) ? 'r' : 'l';
      m->align = al;
    } else if (env == "cases" || env == "dcases") {
      m->align = QString(std::max(ncols, 2), 'l');
    } else if (env == "rcases") {
      m->align = QString(std::max(ncols, 2), 'l');
    } else {
      m->align = QString(std::max(ncols, 1), 'c');
    }
    if (m->vbars.size() < size_t(m->align.size()) + 1) m->vbars.resize(size_t(m->align.size()) + 1, 0);
    static const QHash<QString, std::pair<QString, QString>> delims = {
      {"pmatrix", {"(", ")"}}, {"bmatrix", {"[", "]"}}, {"Bmatrix", {"{", "}"}}, {"vmatrix", {"|", "|"}},
      {"Vmatrix", {QString(QChar(0x2016)), QString(QChar(0x2016))}}, {"cases", {"{", "."}}, {"dcases", {"{", "."}}, {"rcases", {".", "}"}},
      {"smallmatrix", {"", ""}}, {"psmallmatrix", {"(", ")"}}, {"bsmallmatrix", {"[", "]"}}, {"vsmallmatrix", {"|", "|"}},
    };
    const auto it = delims.constFind(env);
    if (it != delims.constEnd()) { m->ldelim = it->first; m->rdelim = it->second; }
    n->mat = std::move(m);
    return n;
  }

  NodeP parseEnv(const Tok& t) {
    const QString name = rawBraced("begin").trimmed();
    static const QStringList known = {"matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix", "smallmatrix", "psmallmatrix", "bsmallmatrix",
                                      "vsmallmatrix", "cases", "dcases", "rcases", "aligned", "align", "align*", "alignat", "alignat*", "split",
                                      "gathered", "gather", "gather*", "array", "eqnarray", "eqnarray*", "flalign", "flalign*", "tabular", "equation",
                                      "equation*", "multline", "multline*", "subarray"};
    if (!known.contains(name)) fail(QStringLiteral("Entorno desconocido: %1").arg(name), t.pos);
    QString spec;
    if (name == "array" || name == "tabular" || name == "subarray") spec = rawBraced("begin{" + name + "}");
    if (name.startsWith("alignat")) rawBraced("begin{alignat}");
    Rows rows = parseRows(name != "gathered" && name != "gather" && name != "gather*" && name != "equation" && name != "equation*" && name != "multline" && name != "multline*");
    const Tok e = next();
    if (e.t == Tok::End) fail(QStringLiteral("Falta \\end{%1} para cerrar el \\begin{%1} de la posición %2").arg(name).arg(t.pos + 1), e.pos);
    if (!e.isCmd("end")) closerError(e, nullptr);
    const QString endName = rawBraced("end").trimmed();
    if (endName != name) fail(QStringLiteral("\\end{%1} no coincide con \\begin{%2}").arg(endName, name), e.pos);
    if (name == "equation" || name == "equation*") {
      std::vector<NodeP> all;
      for (auto& r : rows.rows) for (auto& c : r) all.push_back(c);
      return mkGroup(std::move(all));
    }
    QString env = name;
    if (env.endsWith('*')) env.chop(1);
    if (env == "gather" || env == "multline") env = "gathered";
    if (env == "subarray") env = "array";
    return makeMatrix(env, std::move(rows), spec, t.pos);
  }

  // ---- atoms and commands -------------------------------------------------------------------------------------------------------------
  NodeP charAtom(const Tok& t) {
    const char32_t c = t.ch;
    switch (c) {
      case '+': return mkAtom('+', cBin, true, t.pos);
      case '-': return mkAtom(0x2212, cBin, true, t.pos);
      case '*': return mkAtom(0x2217, cBin, true, t.pos);
      case '=': case '<': case '>': case ':': return mkAtom(c, cRel, true, t.pos);
      case ',': case ';': return mkAtom(c, cPunct, true, t.pos);
      case '(': case '[': return mkAtom(c, cOpen, true, t.pos);
      case ')': case ']': case '!': case '?': return mkAtom(c, cClose, true, t.pos);
      case '~': { auto s = mk(Node::Space, t.pos); s->dv = 0.333; return s; }
      case '$': { auto g = mkGroup({}); return g; }
      default: break;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return mkAtom(c, cOrd, false, t.pos);
    if (c >= 0x80) {
      if (const SymDef* d = symByCp(c)) return mkAtom(c, d->cls, d->cp >= 0x391 && d->cp <= 0x3F5 ? false : true, t.pos);
      if ((c >= 0x391 && c <= 0x3C9) || c == 0x3D1 || c == 0x3D5 || c == 0x3D6 || c == 0x3F0 || c == 0x3F1 || c == 0x3F5) return mkAtom(c, cOrd, false, t.pos);
      return mkAtom(c, cOrd, true, t.pos);
    }
    return mkAtom(c, cOrd, true, t.pos);
  }

  NodeP nucleus(const Tok& t) {
    if (t.t == Tok::End) fail(QStringLiteral("Fin inesperado de la fórmula"), t.pos);
    if (t.t == Tok::Ch) {
      if (t.ch == '{') return braced(t.pos);
      if (t.ch == '}') fail(QStringLiteral("Llave de cierre } sin su llave de apertura"), t.pos);
      if (t.ch == '&') closerError(t, nullptr);
      return charAtom(t);
    }
    return command(t);
  }

  NodeP command(const Tok& t) {
    const QString& n = t.cmd;
    const int pos = t.pos;
    auto arg = [&]() { return singleArg(n, pos); };
    // --- symbols
    if (const SymDef* d = symTable().value(n, nullptr)) {
      const bool greek = (d->cp >= 0x391 && d->cp <= 0x3C9) || d->cp == 0x3D1 || d->cp == 0x3D5 || d->cp == 0x3D6 || d->cp == 0x3F0 || d->cp == 0x3F1 || d->cp == 0x3F5;
      return mkAtom(d->cp, d->cls, !greek, pos);
    }
    for (const OpSym& o : kBigOps)
      if (n == QLatin1String(o.name)) { auto b = mk(Node::BigOp, pos); b->cp = o.cp; b->cls = cOp; return b; }
    for (const NamedOpDef& o : kNamedOps)
      if (n == QLatin1String(o.name)) { auto b = mk(Node::NamedOp, pos); b->s = QString::fromUtf8(o.text); b->dv = o.limits ? 1 : 0; b->cls = cOp; return b; }
    // --- spaces
    static const QHash<QString, double> spaces = {{",", 3.0 / 18}, {":", 4.0 / 18}, {">", 4.0 / 18}, {";", 5.0 / 18}, {"!", -3.0 / 18}, {" ", 0.333},
        {"quad", 1}, {"qquad", 2}, {"enspace", 0.5}, {"thinspace", 3.0 / 18}, {"medspace", 4.0 / 18}, {"thickspace", 5.0 / 18}, {"negthinspace", -3.0 / 18},
        {"negmedspace", -4.0 / 18}, {"negthickspace", -5.0 / 18}, {"nobreakspace", 0.333}, {"hfill", 0}, {"allowbreak", 0}, {"relax", 0}};
    if (spaces.contains(n)) { auto s = mk(Node::Space, pos); s->dv = spaces.value(n); return s; }
    if (n == "hspace" || n == "hskip" || n == "mkern" || n == "kern" || n == "mskip" || n == "hspace*") {
      QString raw;
      if (n == "hspace" || n == "hspace*") raw = rawBraced(n);
      else {
        while (p_ < s_.size() && s_[p_].isSpace()) p_++;
        const int st = p_;
        while (p_ < s_.size() && (s_[p_].isLetterOrNumber() || s_[p_] == '.' || s_[p_] == '-' || s_[p_] == '+')) p_++;
        raw = s_.mid(st, p_ - st);
      }
      static const QRegularExpression re(QStringLiteral("^\\s*([+-]?\\d*\\.?\\d+)\\s*(em|ex|pt|mu|px|cm|mm|in|bp)\\s*$"));
      const auto m = re.match(raw);
      if (!m.hasMatch()) fail(QStringLiteral("Medida no válida en \\%1: «%2» (ejemplo: 1em, 5pt)").arg(n, raw), pos);
      const double v = m.captured(1).toDouble();
      const QString u = m.captured(2);
      auto s = mk(Node::Space, pos);
      if (u == "em") s->dv = v;
      else if (u == "mu") s->dv = v / 18;
      else if (u == "ex") s->dv = v * 0.45;
      else { s->iv = 1; s->dv = u == "pt" || u == "px" || u == "bp" ? v : u == "cm" ? v * 28.4527 : u == "mm" ? v * 2.84527 : v * 72.27; }
      return s;
    }
    if (n == "\\" || n == "cr" || n == "newline") fail(QStringLiteral("El salto de línea \\\\ no se puede usar aquí"), pos);
    // --- structures
    if (n == "frac" || n == "dfrac" || n == "tfrac" || n == "cfrac" || n == "binom" || n == "dbinom" || n == "tbinom" || n == "genfrac") {
      auto f = mk(Node::Frac, pos);
      f->iv = n == "dfrac" ? 1 : n == "tfrac" ? 2 : n == "cfrac" ? 3 : n == "binom" ? 4 : n == "dbinom" ? 5 : n == "tbinom" ? 6 : 0;
      f->dv = f->iv >= 4 ? 1 : 0;
      f->a = arg();
      f->b = arg();
      return f;
    }
    if (n == "sqrt") {
      auto s = mk(Node::Sqrt, pos);
      if (peek().isCh('[')) {
        const int open = next().pos;
        std::vector<NodeP> idx;
        for (;;) {
          const Tok q = peek();
          if (q.isCh(']')) { next(); break; }
          if (isCloser(q)) fail(QStringLiteral("Falta cerrar el corchete [ de la raíz (posición %1)").arg(open + 1), q.pos);
          idx.push_back(parseItem());
        }
        s->b = mkGroup(std::move(idx));
      }
      s->a = arg();
      return s;
    }
    if (n == "left") return parseLeft(t);
    if (n == "right") closerError(t, nullptr);
    if (n == "middle") closerError(t, nullptr);
    static const QHash<QString, double> bigSizes = {{"big", 1.2}, {"Big", 1.8}, {"bigg", 2.4}, {"Bigg", 3.0}};
    {
      QString base = n;
      Cls cls = cOrd;
      if (n.size() > 3 && (n.endsWith('l') || n.endsWith('r') || n.endsWith('m'))) {
        const QString b2 = n.left(n.size() - 1);
        if (bigSizes.contains(b2)) { base = b2; cls = n.endsWith('l') ? cOpen : n.endsWith('r') ? cClose : cRel; }
      }
      if (bigSizes.contains(base)) {
        auto b = mk(Node::BigDelim, pos);
        b->s = delimArg(n, pos);
        b->dv = bigSizes.value(base);
        b->cls = cls;
        return b;
      }
    }
    if (n == "begin") return parseEnv(t);
    if (n == "end") closerError(t, nullptr);
    if (n == "substack") {
      rawPeekBrace("substack");
      next();
      Rows rows = parseRows(false);
      const Tok c = next();
      if (!c.isCh('}')) fail(QStringLiteral("Falta cerrar la llave { de \\substack"), c.pos);
      auto m = makeMatrix("substack", std::move(rows), QString(), pos);
      return m;
    }
    // --- text and fonts
    if (n == "text" || n == "textrm" || n == "mbox" || n == "hbox" || n == "textnormal" || n == "textbf" || n == "textit" || n == "textsf" || n == "texttt" || n == "textsc" || n == "emph") {
      auto x = mk(Node::Text, pos);
      x->s = rawBraced(n);
      x->iv = int(n == "textbf" ? Fnt::Bf : n == "textit" || n == "emph" ? Fnt::It : n == "textsf" ? Fnt::Sf : n == "texttt" ? Fnt::Tt : Fnt::Rm);
      return x;
    }
    static const QHash<QString, int> fonts = {{"mathrm", int(Fnt::Rm)}, {"mathbf", int(Fnt::Bf)}, {"mathit", int(Fnt::It)}, {"mathsf", int(Fnt::Sf)},
        {"mathtt", int(Fnt::Tt)}, {"mathcal", int(Fnt::Cal)}, {"mathscr", int(Fnt::Cal)}, {"mathfrak", int(Fnt::Frak)}, {"mathbb", int(Fnt::Bb)},
        {"boldsymbol", int(Fnt::BfIt)}, {"bm", int(Fnt::BfIt)}, {"mathbfit", int(Fnt::BfIt)}, {"mathnormal", int(Fnt::Def)}, {"pmb", int(Fnt::BfIt)}};
    if (fonts.contains(n)) { auto w = mk(Node::FontWrap, pos); w->iv = fonts.value(n); w->a = arg(); return w; }
    if (n == "operatorname" || n == "operatorname*") {
      bool star = false;
      if (p_ < s_.size() && s_[p_] == '*') { p_++; star = true; }
      auto o = mk(Node::NamedOp, pos);
      o->s = rawBraced("operatorname");
      o->cls = cOp;
      o->dv = star ? 1 : 0;
      return o;
    }
    if (n == "bmod") { auto o = mk(Node::NamedOp, pos); o->s = "mod"; o->cls = cBin; o->iv = 2; return o; }
    if (n == "mod" || n == "pmod" || n == "pod") {
      std::vector<NodeP> g;
      auto sp = mk(Node::Space, pos); sp->dv = n == "pod" || n == "pmod" ? 1 : 0.5; g.push_back(sp);
      if (n != "mod") g.push_back(mkAtom('(', cOpen, true, pos));
      if (n != "pod") {
        auto o = mk(Node::NamedOp, pos); o->s = "mod"; o->cls = cOp; o->iv = 2; g.push_back(o);
        auto s2 = mk(Node::Space, pos); s2->dv = n == "mod" ? 0.333 : 0.25; g.push_back(s2);
      }
      g.push_back(arg());
      if (n != "mod") g.push_back(mkAtom(')', cClose, true, pos));
      return mkGroup(std::move(g));
    }
    // --- accents and lines
    static const QStringList accents = {"hat", "check", "tilde", "acute", "grave", "dot", "ddot", "dddot", "ddddot", "breve", "bar", "vec", "mathring",
                                        "widehat", "widetilde", "overrightarrow", "overleftarrow", "overleftrightarrow", "underrightarrow",
                                        "underleftarrow", "widebar", "utilde", "overline", "underline", "underbar", "overbrace", "underbrace",
                                        "Hat", "Bar", "Vec", "Tilde", "Dot", "Ddot"};
    if (accents.contains(n)) {
      if (n == "overline" || n == "underline" || n == "underbar") { auto l = mk(Node::Line, pos); l->iv = n == "overline" ? 0 : 1; l->a = arg(); return l; }
      if (n == "overbrace" || n == "underbrace") { auto l = mk(Node::Brace, pos); l->iv = n == "overbrace" ? 0 : 1; l->a = arg(); return l; }
      auto a = mk(Node::Accent, pos);
      a->s = n;
      a->a = arg();
      return a;
    }
    if (n == "overset" || n == "underset" || n == "stackrel" || n == "overarc") {
      auto s = mk(Node::Stack, pos);
      s->iv = n == "underset" ? 1 : 0;
      s->a = arg();
      s->b = arg();
      return s;
    }
    static const QHash<QString, char32_t> xarrows = {{"xrightarrow", 0x2192}, {"xleftarrow", 0x2190}, {"xRightarrow", 0x21D2}, {"xLeftarrow", 0x21D0},
        {"xleftrightarrow", 0x2194}, {"xLeftrightarrow", 0x21D4}, {"xmapsto", 0x21A6}, {"xhookrightarrow", 0x21AA}, {"xrightleftharpoons", 0x21CC},
        {"xtwoheadrightarrow", 0x21A0}};
    if (xarrows.contains(n)) {
      auto x = mk(Node::XArrow, pos);
      x->cp = xarrows.value(n);
      x->cls = cRel;
      if (peek().isCh('[')) {
        const int open = next().pos;
        std::vector<NodeP> l;
        for (;;) {
          const Tok q = peek();
          if (q.isCh(']')) { next(); break; }
          if (isCloser(q)) fail(QStringLiteral("Falta cerrar el corchete [ (posición %1)").arg(open + 1), q.pos);
          l.push_back(parseItem());
        }
        x->b = mkGroup(std::move(l));
      }
      x->a = arg();
      return x;
    }
    if (n == "boxed" || n == "fbox") { auto b = mk(Node::Boxed, pos); b->a = arg(); return b; }
    if (n == "cancel" || n == "bcancel" || n == "xcancel" || n == "sout" || n == "not") {
      auto c = mk(Node::Cancel, pos);
      c->iv = n == "cancel" ? 0 : n == "bcancel" ? 1 : n == "xcancel" ? 2 : n == "sout" ? 4 : 3;
      c->a = arg();
      if (n == "not") c->cls = cRel;
      return c;
    }
    if (n == "color") {   // \color{red} used as an argument: only colours what follows in the group, handled by parseList; here it has no scope
      parseColor(pos);
      return mkGroup({});
    }
    if (n == "textcolor" || n == "colorbox" || n == "fcolorbox") {
      auto w = mk(Node::Color, pos);
      w->color = parseColor(pos);
      w->a = arg();
      return w;
    }
    if (n == "phantom" || n == "hphantom" || n == "vphantom" || n == "smash" || n == "mathstrut") {
      auto p = mk(Node::Phantom, pos);
      p->iv = n == "phantom" ? 0 : n == "hphantom" ? 1 : n == "vphantom" ? 2 : n == "smash" ? 3 : 4;
      if (n == "mathstrut") { p->a = mkGroup({mkAtom('(', cOrd, true, pos)}); p->iv = 2; }
      else p->a = arg();
      return p;
    }
    static const QHash<QString, Cls> classes = {{"mathbin", cBin}, {"mathrel", cRel}, {"mathord", cOrd}, {"mathop", cOp}, {"mathopen", cOpen},
        {"mathclose", cClose}, {"mathpunct", cPunct}, {"mathinner", cInner}};
    if (classes.contains(n)) { auto w = mk(Node::ClassWrap, pos); w->cls = classes.value(n); w->a = arg(); return w; }
    if (n == "displaystyle" || n == "textstyle" || n == "scriptstyle" || n == "scriptscriptstyle") {
      auto w = mk(Node::StyleSw, pos);
      w->iv = n == "displaystyle" ? D : n == "textstyle" ? T : n == "scriptstyle" ? S : SS;
      w->a = mkGroup({});
      return w;
    }
    if (n == "limits" || n == "nolimits" || n == "displaylimits") return mkGroup({});   // with nothing to apply to
    if (n == "tag") {   // KaTeX sets the tag at the right of the formula, in parentheses (	ag*{...} without them)
      bool star = false;
      if (p_ < s_.size() && s_[p_] == '*') { p_++; star = true; }
      auto t = mk(Node::Text, pos);
      const QString raw = rawBraced("tag");
      t->s = star ? raw : "(" + raw + ")";
      t->iv = int(Fnt::Rm);
      auto gap = mk(Node::Space, pos);
      gap->dv = 2;
      return mkGroup({gap, t});
    }
    if (n == "label" || n == "nonumber" || n == "notag" || n == "quad" || n == "protect" || n == "middle") { if (n == "label") rawBraced(n); return mkGroup({}); }
    if (n == "ensuremath") return arg();
    fail(QStringLiteral("Comando desconocido: \\%1").arg(n), pos);
  }

  void rawPeekBrace(const char* cmd) {
    const Tok t = peek();
    if (!t.isCh('{')) fail(QStringLiteral("Falta el argumento de \\%1: se esperaba {").arg(QString::fromLatin1(cmd)), t.pos);
  }
};

// ===================================================================================================================================
// Layout
// ===================================================================================================================================
struct B {
  double w = 0, h = 0, d = 0;
  double ital = 0;              // overhang of the ink to the right of the box (what a superscript must clear)
  double subKern = 0, supKern = 0;   // big integral: where side scripts go relative to the right edge
  Cls cls = cOrd;
  bool simple = false;          // a single character: scripts are placed from the font's own heights
  bool glue = false;            // spaces do not take part in the spacing between atoms
  bool isInt = false;
  std::vector<MathPrim> prims;
};

struct Ctx {
  int style = T;
  double base = 10;             // font size of text style, points
  Fnt font = Fnt::Def;
  double em() const { return base * (style >= SS ? 0.5 : style >= S ? 0.7 : 1.0); }
  bool display() const { return style < T; }
  bool script() const { return style >= S; }
  bool cramped() const { return style & 1; }
  Ctx with(int st) const { Ctx c = *this; c.style = st; return c; }
};
int supStyle(int s) { return s < T ? S : s < S ? S : SS; }                      // (cramped bit cleared)
int subStyle(int s) { return supStyle(s) | 1; }
int numStyle(int s) { return s < T ? T : s < S ? S : SS; }                       // display -> text, text -> script
int denStyle(int s) { return numStyle(s) | 1; }

void put(B& dst, const B& src, double x, double y) {
  dst.prims.reserve(dst.prims.size() + src.prims.size());
  for (const MathPrim& p : src.prims) { MathPrim q = p; q.tx += x; q.ty += y; dst.prims.push_back(std::move(q)); }
}

MathPrim glyphPrim(const GInfo& g, double scale, double x, double y) {
  MathPrim p;
  p.path = g.path; p.scale = scale; p.tx = x; p.ty = y;
  return p;
}
MathPrim rectPrim(double x, double y, double w, double h) {
  QPainterPath pp;
  pp.addRect(x, y, w, h);
  MathPrim p;
  p.path = std::make_shared<QPainterPath>(pp);
  return p;
}
MathPrim pathPrim(const QPainterPath& pp, bool stroke, double lw) {
  MathPrim p;
  p.path = std::make_shared<QPainterPath>(pp);
  p.stroke = stroke; p.lineWidth = lw;
  return p;
}
MathPrim linePrim(double x1, double y1, double x2, double y2, double lw) {
  QPainterPath pp;
  pp.moveTo(x1, y1); pp.lineTo(x2, y2);
  return pathPrim(pp, true, lw);
}

B glyphBox(const QString& glyph, double em, Cls cls) {
  const GInfo g = ginfo(glyph);
  B b;
  b.w = g.adv * em;
  b.h = g.top * em;
  b.d = g.bot * em;
  b.ital = std::max(0.0, (g.x1 - g.adv) * em);
  b.cls = cls;
  b.simple = true;
  if (g.path && !g.path->isEmpty()) b.prims.push_back(glyphPrim(g, em, 0, 0));
  return b;
}

// ---- inter-atom spacing (TeXbook p. 170): thin 3mu, medium 4mu, thick 5mu; negative entries only outside script styles
const int8_t kSpace[8][8] = {
    /*Ord  */ {0, 1, -2, -3, 0, 0, 0, -1},
    /*Op   */ {1, 1, 0, -3, 0, 0, 0, -1},
    /*Bin  */ {-2, -2, 0, 0, -2, 0, 0, -2},
    /*Rel  */ {-3, -3, 0, 0, -3, 0, 0, -3},
    /*Open */ {0, 0, 0, 0, 0, 0, 0, 0},
    /*Close*/ {0, 1, -2, -3, 0, 0, 0, -1},
    /*Punct*/ {-1, -1, 0, -1, -1, -1, -1, -1},
    /*Inner*/ {-1, 1, -2, -3, -1, 0, -1, -1},
};
double spaceBetween(Cls a, Cls b, const Ctx& c) {
  const int v = kSpace[a][b];
  if (v == 0) return 0;
  if (v < 0 && c.script()) return 0;
  switch (std::abs(v)) { case 1: return 3.0 / 18; case 2: return 4.0 / 18; default: return 5.0 / 18; }
}

B layoutNode(const Node& n, const Ctx& c);

B layoutList(const std::vector<NodeP>& items, const Ctx& c) {
  std::vector<B> bs;
  bs.reserve(items.size());
  for (const NodeP& it : items) bs.push_back(layoutNode(*it, c));
  // a binary operator with no left operand, or no right one, is an ordinary symbol
  Cls prev = cOpen;   // start behaves like "after Open": a leading Bin is Ord
  bool first = true;
  for (size_t i = 0; i < bs.size(); i++) {
    if (bs[i].glue) continue;
    if (bs[i].cls == cBin && (first || prev == cBin || prev == cOp || prev == cRel || prev == cOpen || prev == cPunct)) bs[i].cls = cOrd;
    if ((bs[i].cls == cRel || bs[i].cls == cClose || bs[i].cls == cPunct) && !first && prev == cBin) {
      for (size_t j = i; j-- > 0;) if (!bs[j].glue) { bs[j].cls = cOrd; break; }
    }
    prev = bs[i].cls;
    first = false;
  }
  for (size_t j = bs.size(); j-- > 0;) {
    if (bs[j].glue) continue;
    if (bs[j].cls == cBin) bs[j].cls = cOrd;
    break;
  }
  B out;
  double x = 0;
  const double em = c.em();
  std::optional<Cls> last;
  for (B& b : bs) {
    if (!b.glue) {
      if (last) x += spaceBetween(*last, b.cls, c) * em;
      last = b.cls;
    }
    put(out, b, x, 0);
    out.h = std::max(out.h, b.h);
    out.d = std::max(out.d, b.d);
    x += b.w;
    out.ital = b.ital;
  }
  out.w = x;
  if (bs.size() == 1) { out.simple = bs[0].simple; out.subKern = bs[0].subKern; out.supKern = bs[0].supKern; }
  return out;
}

// ---- stretchy delimiters ----------------------------------------------------------------------------------------------------------
/** A delimiter `d` whose total height (above and below the axis) is `H` points; H <= 0 gives the font's own size. */
B delimBox(const QString& d, double H, double em, Cls cls) {
  B b;
  b.cls = cls;
  if (d.isEmpty() || d == ".") return b;
  const Kal& k = kal();
  const double ax = k.axis * em;
  const double thick = std::max(k.rule, 0.045) * em;
  const char16_t ch = d[0].unicode();
  const bool arrow = ch >= 0x2191 && ch <= 0x21D5;
  const bool hasGlyph = QString("()[]{}⌊⌋⌈⌉").contains(d[0]) || arrow;
  if (hasGlyph) {
    const GInfo g = ginfo(d);
    const double inkH = (g.top + g.bot) * em;
    if (H <= 0 || H <= 1.15 * inkH || arrow) {
      const double s = (H > inkH && inkH > 0 && !arrow) ? std::min(H / inkH, 1.15) : 1.0;
      const double shiftUp = ax - (g.top - g.bot) / 2 * em * s;
      b.w = g.adv * em * s;
      b.h = std::max(0.0, g.top * em * s + shiftUp);
      b.d = std::max(0.0, g.bot * em * s - shiftUp);
      b.prims.push_back(glyphPrim(g, em * s, 0, -shiftUp));
      return b;
    }
  }
  const double Hh = std::max(H, em);
  const double yc = -ax, yt = yc - Hh / 2, yb = yc + Hh / 2;
  b.h = ax + Hh / 2;
  b.d = std::max(0.0, Hh / 2 - ax);
  const double Hn = Hh / em;
  auto stroke = [&](const QPainterPath& pp, double lw) { b.prims.push_back(pathPrim(pp, true, lw)); };
  switch (ch) {
    case '(': case ')': {
      const double w = em * (0.27 + 0.03 * std::min(Hn, 4.0));
      const double c = 0.075 * em / 0.75;
      QPainterPath p;
      p.moveTo(w, yt);
      p.cubicTo(0, yt + 0.2 * Hh, 0, yb - 0.2 * Hh, w, yb);
      p.cubicTo(c, yb - 0.2 * Hh, c, yt + 0.2 * Hh, w, yt);
      p.closeSubpath();
      if (ch == ')') p = QTransform(-1, 0, 0, 1, w, 0).map(p);
      b.w = w + 0.02 * em;
      b.prims.push_back(pathPrim(p, false, 0));
      if (ch == '(') for (auto& q : b.prims) q.tx += 0.02 * em;
      break;
    }
    case '[': case ']': {
      const double w = 0.32 * em, arm = 0.2 * em;
      const double xl = ch == '[' ? 0.08 * em : w - 0.08 * em - thick;
      b.w = w;
      b.prims.push_back(rectPrim(xl, yt, thick, Hh));
      const double ax0 = ch == '[' ? xl : xl + thick - arm;
      b.prims.push_back(rectPrim(ax0, yt, arm, thick));
      b.prims.push_back(rectPrim(ax0, yb - thick, arm, thick));
      break;
    }
    case '{': case '}': {
      const double w = 0.42 * em, xs = 0.24 * em, tip = 0.05 * em, xr = 0.38 * em;
      const double r = std::min(0.22 * em, Hh / 8);
      QPainterPath p;
      p.moveTo(xr, yt);
      p.quadTo(xs, yt, xs, yt + r);
      p.lineTo(xs, yc - r);
      p.quadTo(xs, yc, tip, yc);
      p.quadTo(xs, yc, xs, yc + r);
      p.lineTo(xs, yb - r);
      p.quadTo(xs, yb, xr, yb);
      if (ch == '}') p = QTransform(-1, 0, 0, 1, w, 0).map(p);
      b.w = w;
      stroke(p, 0.06 * em);
      break;
    }
    case '|': {
      b.w = 0.27 * em;
      b.prims.push_back(rectPrim(b.w / 2 - thick / 2, yt, thick, Hh));
      break;
    }
    case 0x2016: {
      b.w = 0.42 * em;
      b.prims.push_back(rectPrim(b.w / 2 - 0.08 * em - thick / 2, yt, thick, Hh));
      b.prims.push_back(rectPrim(b.w / 2 + 0.08 * em - thick / 2, yt, thick, Hh));
      break;
    }
    case 0x27E8: case 0x27E9: {
      const double w = 0.36 * em;
      QPainterPath p;
      p.moveTo(0.82 * w, yt); p.lineTo(0.18 * w, yc); p.lineTo(0.82 * w, yb);
      if (ch == 0x27E9) p = QTransform(-1, 0, 0, 1, w, 0).map(p);
      b.w = w;
      stroke(p, thick);
      break;
    }
    case 0x230A: case 0x230B: case 0x2308: case 0x2309: {
      const double w = 0.32 * em, arm = 0.2 * em;
      const bool left = ch == 0x230A || ch == 0x2308, top = ch == 0x2308 || ch == 0x2309;
      const double xl = left ? 0.08 * em : w - 0.08 * em - thick;
      b.w = w;
      b.prims.push_back(rectPrim(xl, yt, thick, Hh));
      const double ax0 = left ? xl : xl + thick - arm;
      b.prims.push_back(rectPrim(ax0, top ? yt : yb - thick, arm, thick));
      break;
    }
    case '/': case '\\': {
      const double w = std::max(0.3 * em, 0.22 * Hh);
      b.w = w;
      b.prims.push_back(ch == '/' ? linePrim(0, yb, w, yt, thick) : linePrim(0, yt, w, yb, thick));
      break;
    }
    default: break;
  }
  return b;
}

// ---- pieces -------------------------------------------------------------------------------------------------------------------
/** Superscript/subscript boxes around `nuc` (TeXbook rule 18). */
B attachScripts(B nuc, const B* sub, const B* sup, const Ctx& c) {
  const double em = c.em();
  const double emS = em * (c.style >= S ? 0.714 : 0.7);   // size of the script font, for the drop constants
  const Kal& k = kal();
  const double xh = k.xh * em, theta = k.rule * em;
  double u = 0, v = 0;
  if (!nuc.simple) { u = nuc.h - 0.386 * emS; v = nuc.d + 0.05 * emS; }
  const double scriptSpace = 0.04 * em;
  const double xSup = nuc.w + nuc.ital + nuc.supKern, xSub = nuc.w + nuc.subKern;
  B out;
  put(out, nuc, 0, 0);
  double wEnd = nuc.w;
  if (sup && !sub) {
    const double p = (c.display() && !c.cramped() ? 0.413 : c.cramped() ? 0.289 : 0.363) * em;
    u = std::max({u, p, sup->d + xh / 4});
    put(out, *sup, xSup, -u);
    out.h = std::max(nuc.h, u + sup->h);
    out.d = nuc.d;
    wEnd = std::max(wEnd, xSup + sup->w);
  } else if (sub && !sup) {
    v = std::max({v, 0.15 * em, sub->h - 0.8 * xh});
    put(out, *sub, xSub, v);
    out.h = nuc.h;
    out.d = std::max(nuc.d, v + sub->d);
    wEnd = std::max(wEnd, xSub + sub->w);
  } else if (sub && sup) {
    const double p = (c.display() && !c.cramped() ? 0.413 : c.cramped() ? 0.289 : 0.363) * em;
    u = std::max({u, p, sup->d + xh / 4});
    v = std::max(v, 0.247 * em);
    double gap = (u - sup->d) - (sub->h - v);
    if (gap < 4 * theta) {
      v += 4 * theta - gap;
      const double psi = 0.8 * xh - (u - sup->d);
      if (psi > 0) { u += psi; v -= psi; }
    }
    put(out, *sup, xSup, -u);
    put(out, *sub, xSub, v);
    out.h = std::max(nuc.h, u + sup->h);
    out.d = std::max(nuc.d, v + sub->d);
    wEnd = std::max({wEnd, xSup + sup->w, xSub + sub->w});
  }
  out.w = wEnd + scriptSpace;
  out.cls = nuc.cls;
  out.isInt = false;
  return out;
}

/** Limits above and below a big operator (TeXbook rule 13a). */
B attachLimits(const B& nuc, const B* sub, const B* sup, const Ctx& c) {
  const double em = c.em();
  double W = nuc.w;
  if (sub) W = std::max(W, sub->w);
  if (sup) W = std::max(W, sup->w);
  B out;
  const double shift = nuc.ital / 2;
  put(out, nuc, (W - nuc.w) / 2, 0);
  out.h = nuc.h; out.d = nuc.d;
  if (sup) {
    const double gap = std::max(0.111 * em, 0.2 * em - sup->d);
    const double y = -(nuc.h + gap + sup->d);
    put(out, *sup, (W - sup->w) / 2 + shift, y);
    out.h = nuc.h + gap + sup->d + sup->h + 0.1 * em;
  }
  if (sub) {
    const double gap = std::max(0.166 * em, 0.6 * em - sub->h);
    const double y = nuc.d + gap + sub->h;
    put(out, *sub, (W - sub->w) / 2 - shift, y);
    out.d = nuc.d + gap + sub->h + sub->d + 0.1 * em;
  }
  out.w = W;
  out.cls = cOp;
  return out;
}

bool limitsWanted(const Node& base, const Ctx& c) {
  switch (base.k) {
    case Node::BigOp: {
      const bool integral = base.cp >= 0x222B && base.cp <= 0x2230;
      if (base.iv == 1) return true;
      if (base.iv == 2) return false;
      return !integral && c.display();
    }
    case Node::NamedOp:
      if (base.iv == 1) return true;
      if (base.iv == 2) return false;
      return base.dv != 0 && c.display();
    case Node::Brace: case Node::XArrow: return true;
    case Node::Stack: return false;
    default: return false;
  }
}

B layoutBigOp(const Node& n, const Ctx& c) {
  const double em = c.em();
  const Kal& k = kal();
  const bool integral = n.cp >= 0x222B && n.cp <= 0x2230;
  const bool tall = c.display();
  const double target = (integral ? (tall ? 2.2 : 1.1) : (tall ? 1.4 : 1.0)) * em;   // total height of the ink
  const GInfo g = ginfo(ucs(n.cp));
  B b;
  b.cls = cOp;
  const double inkH = g.top + g.bot;
  if (inkH <= 0.01) return b;
  const double sc = target / inkH;
  const double shiftUp = k.axis * em - (g.top - g.bot) / 2 * sc;
  const double pad = 0.03 * em;
  b.w = (g.x1 - g.x0) * sc + 2 * pad;
  b.h = std::max(0.0, g.top * sc + shiftUp);
  b.d = std::max(0.0, g.bot * sc - shiftUp);
  b.prims.push_back(glyphPrim(g, sc, -g.x0 * sc + pad, -shiftUp));
  if (integral) {
    b.isInt = true;
    b.ital = 0;
    b.subKern = -0.20 * target;
    b.supKern = 0.04 * target;
  }
  return b;
}

B layoutSqrt(const Node& n, const Ctx& c) {
  const double em = c.em();
  const Kal& k = kal();
  B body = layoutNode(*n.a, c.with(c.style | 1));
  const double theta = k.rule * em;
  const double phi = c.display() ? theta + k.xh * em / 4 : theta + theta / 4;
  const double yTop = -(body.h + phi + theta);        // top edge of the bar
  const double yBot = body.d + 0.07 * em;            // lowest point of the radical sign
  const double H = yBot - yTop;
  const double surdW = 0.62 * em + 0.04 * em;
  B idx;
  double surdX = 0;
  if (n.b) {
    idx = layoutNode(*n.b, c.with(SS));
    surdX = std::max(0.0, idx.w - 0.32 * em);
  }
  B out;
  const double hookY = yTop + 0.58 * H;
  const double x0 = surdX;
  const double thin = theta * 1.05;
  QPainterPath p1;
  p1.moveTo(x0 + 0.01 * em, hookY + 0.05 * em); p1.lineTo(x0 + 0.10 * em, hookY - 0.02 * em);
  out.prims.push_back(pathPrim(p1, true, thin));
  out.prims.push_back(linePrim(x0 + 0.10 * em, hookY - 0.02 * em, x0 + 0.30 * em, yBot, 0.085 * em));
  out.prims.push_back(linePrim(x0 + 0.30 * em, yBot, x0 + 0.62 * em, yTop + theta / 2, thin));
  const double rightEnd = surdX + surdW + body.w + 0.03 * em;
  out.prims.push_back(rectPrim(x0 + 0.60 * em, yTop, rightEnd - (x0 + 0.60 * em), theta));
  put(out, body, surdX + surdW, 0);
  if (n.b) {
    const double yIdx = yBot - 0.6 * H;
    put(out, idx, std::max(0.0, surdX + 0.30 * em - idx.w + 0.02 * em), yIdx);
    out.h = std::max(-yTop, -yIdx + idx.h);
  } else {
    out.h = -yTop;
  }
  out.d = std::max(body.d, yBot);
  out.w = rightEnd;
  out.cls = cOrd;
  return out;
}

B layoutFrac(const Node& n, const Ctx& c0) {
  Ctx c = c0;
  if (n.iv == 1 || n.iv == 3 || n.iv == 5) c.style = D;
  else if (n.iv == 2 || n.iv == 6) c.style = T;
  const double em = c.em();
  const Kal& k = kal();
  const bool rule = n.dv == 0;
  const double theta = k.rule * em;
  B num = layoutNode(*n.a, c.with(numStyle(c.style)));
  B den = layoutNode(*n.b, c.with(denStyle(c.style)));
  const bool disp = c.display();
  double u, v;
  if (disp) { u = (rule ? 0.677 : 0.677) * em; v = 0.686 * em; }
  else { u = (rule ? 0.394 : 0.444) * em; v = 0.345 * em; }
  const double axis = k.axis * em;
  if (rule) {
    const double phi = disp ? 3 * theta : theta;
    const double d1 = (u - num.d) - (axis + theta / 2);
    if (d1 < phi) u += phi - d1;
    const double d2 = (axis - theta / 2) - (den.h - v);
    if (d2 < phi) v += phi - d2;
  } else {
    const double phi = disp ? 7 * theta : 3 * theta;
    const double psi = (u - num.d) - (den.h - v);
    if (psi < phi) { u += (phi - psi) / 2; v += (phi - psi) / 2; }
  }
  const double pad = 0.12 * em;
  const double W = std::max(num.w, den.w);
  B fr;
  put(fr, num, pad + (W - num.w) / 2, -u);
  put(fr, den, pad + (W - den.w) / 2, v);
  if (rule) fr.prims.push_back(rectPrim(pad, -axis - theta / 2, W, theta));
  fr.w = W + 2 * pad;
  fr.h = u + num.h;
  fr.d = v + den.d;
  fr.cls = cOrd;
  if (n.iv < 4) return fr;
  // binomial: the same stack between stretchy parentheses
  const double H = disp ? 2.39 * em : 1.01 * em;
  B l = delimBox("(", H, em, cOpen), r = delimBox(")", H, em, cClose);
  B out;
  put(out, l, 0, 0);
  put(out, fr, l.w, 0);
  put(out, r, l.w + fr.w, 0);
  out.w = l.w + fr.w + r.w;
  out.h = std::max({l.h, fr.h, r.h});
  out.d = std::max({l.d, fr.d, r.d});
  return out;
}

// ---- accents ------------------------------------------------------------------------------------------------------------------
B layoutAccent(const Node& n, const Ctx& c) {
  const double em = c.em();
  const Kal& k = kal();
  B nuc = layoutNode(*n.a, c);
  const QString& a = n.s;
  const bool wide = a == "widehat" || a == "widetilde" || a == "overrightarrow" || a == "overleftarrow" || a == "overleftrightarrow" || a == "widebar" ||
                    a == "underrightarrow" || a == "underleftarrow" || a == "utilde";
  const bool under = a == "underrightarrow" || a == "underleftarrow" || a == "utilde";
  const double lw = 0.045 * em;
  double aw = wide ? std::max(nuc.w, 0.5 * em) : std::min(std::max(nuc.w, 0.3 * em), 0.5 * em);
  if (a == "vec" || a == "Vec") aw = std::max(std::min(nuc.w, 0.5 * em), 0.35 * em);
  if (a == "bar" || a == "Bar") aw = std::max(std::min(nuc.w, 0.5 * em), 0.35 * em);
  double skew = 0;
  if (!wide && nuc.simple && nuc.ital >= 0) skew = 0.03 * em + (nuc.h > 0.6 * em ? 0.05 * em : 0);
  const double ax0 = (nuc.w - aw) / 2 + skew;   // left end of the accent
  const double top = std::max(nuc.h, k.xh * em);
  const double gap = 0.07 * em;
  const double yb = under ? nuc.d + gap : -(top + gap);   // the accent's lower edge (over) / upper edge (under)
  B out;
  put(out, nuc, 0, 0);
  double ah = 0;
  auto add = [&](const QPainterPath& p, bool stroke) { out.prims.push_back(pathPrim(p, stroke, lw)); };
  QPainterPath p;
  if (a == "hat" || a == "widehat" || a == "Hat") {
    ah = wide ? std::min(0.17 * em + aw * 0.06, 0.32 * em) : 0.17 * em;
    p.moveTo(ax0, yb); p.lineTo(ax0 + aw / 2, yb - ah); p.lineTo(ax0 + aw, yb);
    add(p, true);
  } else if (a == "check") {
    ah = 0.17 * em;
    p.moveTo(ax0, yb - ah); p.lineTo(ax0 + aw / 2, yb); p.lineTo(ax0 + aw, yb - ah);
    add(p, true);
  } else if (a == "tilde" || a == "widetilde" || a == "Tilde" || a == "utilde") {
    const double amp = 0.055 * em + (wide ? std::min(aw * 0.02, 0.04 * em) : 0);
    ah = 2 * amp + lw;
    const double yc = under ? yb + ah / 2 : yb - ah / 2;
    const int N = 24;
    for (int i = 0; i <= N; i++) {
      const double t = double(i) / N;
      const double x = ax0 + t * aw;
      const double y = yc - amp * std::sin(2 * M_PI * t) * 1.0 - amp * 0.3 * (t - 0.5) * 2;
      if (i == 0) p.moveTo(x, y); else p.lineTo(x, y);
    }
    add(p, true);
  } else if (a == "acute" || a == "grave") {
    ah = 0.16 * em;
    const double cx = ax0 + aw / 2;
    if (a == "acute") { p.moveTo(cx - 0.05 * em, yb); p.lineTo(cx + 0.07 * em, yb - ah); }
    else { p.moveTo(cx + 0.05 * em, yb); p.lineTo(cx - 0.07 * em, yb - ah); }
    add(p, true);
  } else if (a == "dot" || a == "ddot" || a == "dddot" || a == "ddddot" || a == "Dot" || a == "Ddot") {
    const int cnt = a == "dot" || a == "Dot" ? 1 : a == "ddot" || a == "Ddot" ? 2 : a == "dddot" ? 3 : 4;
    const double r = 0.05 * em, sp = 0.16 * em;
    const double cx = ax0 + aw / 2;
    ah = 2 * r;
    for (int i = 0; i < cnt; i++) {
      QPainterPath q;
      q.addEllipse(QPointF(cx + (i - (cnt - 1) / 2.0) * sp, yb - r), r, r);
      add(q, false);
    }
  } else if (a == "breve") {
    ah = 0.12 * em;
    p.moveTo(ax0 + 0.02 * em, yb - ah); p.cubicTo(ax0 + 0.05 * em, yb + 0.02 * em, ax0 + aw - 0.05 * em, yb + 0.02 * em, ax0 + aw - 0.02 * em, yb - ah);
    add(p, true);
  } else if (a == "mathring") {
    const double r = 0.065 * em;
    ah = 2 * r;
    QPainterPath q;
    q.addEllipse(QPointF(ax0 + aw / 2, yb - r), r, r);
    add(q, true);
  } else if (a == "bar" || a == "Bar" || a == "widebar") {
    ah = lw;
    out.prims.push_back(rectPrim(ax0 + 0.02 * em, yb - lw - 0.02 * em, aw - 0.04 * em, lw));
  } else {   // arrows: vec, overrightarrow, overleftarrow, overleftrightarrow, underrightarrow, underleftarrow
    const bool left = a == "overleftarrow" || a == "underleftarrow";
    const bool both = a == "overleftrightarrow";
    const double hl = 0.09 * em, hw = 0.065 * em;
    const double y = yb - 0.06 * em * 1.0 + (under ? 0.06 * em : 0) * 0;
    ah = 0.14 * em;
    const double x1 = ax0, x2 = ax0 + aw;
    out.prims.push_back(linePrim(x1 + (left || both ? 0.02 * em : 0), y, x2 - (!left || both ? 0.02 * em : 0), y, lw));
    auto head = [&](double tipX, double dir) {
      QPainterPath q;
      q.moveTo(tipX - dir * hl, y - hw); q.lineTo(tipX, y); q.lineTo(tipX - dir * hl, y + hw);
      add(q, true);
    };
    if (!left || both) head(x2, 1);
    if (left || both) head(x1, -1);
  }
  if (under) { out.h = nuc.h; out.d = nuc.d + gap + ah; }
  else { out.h = top + gap + ah; out.d = nuc.d; }
  out.w = nuc.w;
  out.cls = cOrd;
  return out;
}

QPainterPath bracePath(double w, double r, bool over) {
  // curly brace, tip at the middle; over = tips up (above the body), ends reach y = 0
  QPainterPath p;
  const double s = over ? 1 : -1;
  p.moveTo(0, 0);
  p.quadTo(0, -s * r, r, -s * r);
  p.lineTo(w / 2 - r, -s * r);
  p.quadTo(w / 2, -s * r, w / 2, -s * 2 * r);
  p.quadTo(w / 2, -s * r, w / 2 + r, -s * r);
  p.lineTo(w - r, -s * r);
  p.quadTo(w, -s * r, w, 0);
  return p;
}

B layoutBrace(const Node& n, const Ctx& c) {
  const double em = c.em();
  B body = layoutNode(*n.a, c);
  const double r = 0.16 * em;
  const double w = std::max(body.w, 0.5 * em);
  B out;
  put(out, body, (w - body.w) / 2, 0);
  const double gap = 0.12 * em;
  if (n.iv == 0) {
    QPainterPath p = bracePath(w, r, true);
    p.translate(0, -(body.h + gap));
    out.prims.push_back(pathPrim(p, true, 0.075 * em));
    out.h = body.h + gap + 2 * r + 0.09 * em; out.d = body.d;
  } else {
    QPainterPath p = bracePath(w, r, false);
    p.translate(0, body.d + gap);
    out.prims.push_back(pathPrim(p, true, 0.075 * em));
    out.h = body.h; out.d = body.d + gap + 2 * r + 0.09 * em;
  }
  out.w = w;
  out.cls = cOrd;
  return out;
}

B layoutXArrow(const Node& n, const Ctx& c) {
  const double em = c.em();
  const Kal& k = kal();
  const Ctx cs = c.with(c.style < S ? S : SS);
  B over = layoutNode(*n.a, cs);
  B under;
  if (n.b) under = layoutNode(*n.b, c.with(subStyle(c.style) < SS ? S | 1 : SSc));
  const double len = std::max({1.6 * em, over.w + 1.0 * em, (n.b ? under.w + 1.0 * em : 0.0)});
  const double axis = k.axis * em;
  const double lw = 0.045 * em;
  B out;
  const bool two = n.cp == 0x2194 || n.cp == 0x21D4 || n.cp == 0x21CC;
  const bool dbl = n.cp == 0x21D2 || n.cp == 0x21D0 || n.cp == 0x21D4;
  const bool left = n.cp == 0x2190 || n.cp == 0x21D0;
  const double y = -axis;
  const double hl = 0.13 * em, hw = 0.075 * em;
  auto head = [&](double tipX, double dir, double yy) {
    QPainterPath q;
    q.moveTo(tipX - dir * hl, yy - hw); q.lineTo(tipX, yy); q.lineTo(tipX - dir * hl, yy + hw);
    out.prims.push_back(pathPrim(q, true, lw));
  };
  if (n.cp == 0x21CC) {
    out.prims.push_back(linePrim(0, y - 0.05 * em, len, y - 0.05 * em, lw));
    out.prims.push_back(linePrim(0, y + 0.05 * em, len, y + 0.05 * em, lw));
    QPainterPath a, b;
    a.moveTo(len - hl, y - 0.05 * em - hw); a.lineTo(len, y - 0.05 * em);
    b.moveTo(hl, y + 0.05 * em + hw); b.lineTo(0, y + 0.05 * em);
    out.prims.push_back(pathPrim(a, true, lw));
    out.prims.push_back(pathPrim(b, true, lw));
  } else if (dbl) {
    out.prims.push_back(linePrim(left || two ? hl * 0.6 : 0, y - 0.035 * em, len - (!left || two ? hl * 0.6 : 0), y - 0.035 * em, lw));
    out.prims.push_back(linePrim(left || two ? hl * 0.6 : 0, y + 0.035 * em, len - (!left || two ? hl * 0.6 : 0), y + 0.035 * em, lw));
    if (!left || two) head(len, 1, y);
    if (left || two) head(0, -1, y);
  } else {
    out.prims.push_back(linePrim(0, y, len, y, lw));
    if (!left || two) head(len, 1, y);
    if (left || two) head(0, -1, y);
    if (n.cp == 0x21A6) out.prims.push_back(linePrim(0, y - 0.07 * em, 0, y + 0.07 * em, lw));
  }
  put(out, over, (len - over.w) / 2, -(axis + 0.12 * em + over.d + 0.02 * em));
  out.h = axis + 0.12 * em + over.d + 0.02 * em + over.h;
  out.d = 0;
  if (n.b) {
    put(out, under, (len - under.w) / 2, -axis + 0.12 * em + under.h + 0.02 * em + axis * 0 + 0.05 * em);
    out.d = std::max(0.0, -axis + 0.12 * em + under.h + 0.07 * em + under.d);
  }
  out.w = len;
  out.cls = cRel;
  return out;
}

// ---- matrices and aligned environments ---------------------------------------------------------------------------------------------
B layoutMatrix(const Node& n, const Ctx& c0) {
  const MatData& m = *n.mat;
  const bool alignedEnv = m.env == "aligned" || m.env == "align" || m.env == "split" || m.env == "alignat" || m.env == "eqnarray" || m.env == "flalign" ||
                          m.env == "gathered";
  const bool isSmall = m.env.endsWith("smallmatrix");
  Ctx c = c0;
  if (m.env == "substack") c = c0.with(c0.style < S ? S : c0.style | 0);
  else if (alignedEnv) c = c0.with(D);
  else if (isSmall) c = c0.with(S);
  else c = c0.with(T | (c0.style & 1));
  const double em = c.em();
  const Kal& k = kal();
  const int R = int(m.rows.size()), C = m.cols();
  std::vector<std::vector<B>> cells(static_cast<size_t>(R));
  std::vector<double> colW(size_t(std::max(C, 1)), 0.0), rowH(size_t(R), 0.0), rowD(size_t(R), 0.0);
  const double base = (m.env == "substack" ? 0.95 : isSmall ? 0.9 : 1.2) * em;
  const double strutH = 0.7 * base, strutD = 0.3 * base;
  for (int r = 0; r < R; r++) {
    cells[size_t(r)].resize(size_t(C));
    rowH[size_t(r)] = strutH; rowD[size_t(r)] = strutD;
    for (int col = 0; col < int(m.rows[size_t(r)].size()) && col < C; col++) {
      B b;
      if (m.env == "aligned" || m.env == "align" || m.env == "split" || m.env == "alignat" || m.env == "eqnarray" || m.env == "flalign") {
        // an empty {} in front of the cells that start after & keeps a leading = or + from losing its space on the left
        std::vector<NodeP> l;
        if (col % 2 == 1) l.push_back(mkGroup({}));
        l.push_back(m.rows[size_t(r)][size_t(col)]);
        b = layoutList(l, c);
      } else {
        b = layoutNode(*m.rows[size_t(r)][size_t(col)], c);
      }
      colW[size_t(col)] = std::max(colW[size_t(col)], b.w);
      rowH[size_t(r)] = std::max(rowH[size_t(r)], b.h);
      rowD[size_t(r)] = std::max(rowD[size_t(r)], b.d);
      cells[size_t(r)][size_t(col)] = std::move(b);
    }
  }
  // gaps between columns
  const bool arrayLike = m.env == "array" || m.env == "tabular";
  std::vector<double> gap(size_t(C) + 1, 0.0);   // gap[i] = space before column i; gap[C] after the last
  for (int i = 0; i <= C; i++) {
    double g = 0;
    if (arrayLike) g = (i == 0 || i == C) ? 0.5 * em : 1.0 * em;
    else if (alignedEnv && m.env != "gathered") g = (i == 0 || i == C || i % 2 == 1) ? 0 : 1.0 * em * (m.env == "eqnarray" ? 1.0 : 2.0);
    else if (m.env == "cases" || m.env == "dcases" || m.env == "rcases") g = (i == 0 || i == C) ? 0 : 1.0 * em;
    else if (m.env == "gathered" || m.env == "substack") g = 0;
    else g = (i == 0 || i == C) ? 0 : (isSmall ? 0.6 : 1.0) * em;
    gap[size_t(i)] = g;
  }
  if (arrayLike) for (int i = 1; i < C; i++) if (m.vbars[size_t(i)] > 0) gap[size_t(i)] += m.vbars[size_t(i)] * 0.1 * em;
  const double rule = std::max(k.rule, 0.04) * em;
  // row positions
  double y = 0;
  std::vector<double> rowY(size_t(R), 0.0);
  double totalH;
  std::vector<double> hlineY;
  for (int r = 0; r < R; r++) {
    if (m.hlines.size() > size_t(r) && m.hlines[size_t(r)] > 0) { y += 0.1 * em; hlineY.push_back(y); y += rule * m.hlines[size_t(r)] + 0.1 * em; }
    y += rowH[size_t(r)];
    rowY[size_t(r)] = y;
    y += rowD[size_t(r)];
  }
  if (m.hlines.size() > size_t(R) && m.hlines[size_t(R)] > 0) { y += 0.1 * em; hlineY.push_back(y); y += rule * m.hlines[size_t(R)] + 0.1 * em; }
  totalH = y;
  const double axis = k.axis * em;
  const double top = -(totalH / 2 + axis);        // y of the matrix's upper edge (down is positive)
  // x positions
  std::vector<double> colX(size_t(C), 0.0);
  double x = 0;
  for (int i = 0; i < C; i++) { x += gap[size_t(i)]; colX[size_t(i)] = x; x += colW[size_t(i)]; }
  x += gap[size_t(C)];
  const double W = x;
  B body;
  for (int r = 0; r < R; r++)
    for (int col = 0; col < int(m.rows[size_t(r)].size()) && col < C; col++) {
      const B& b = cells[size_t(r)][size_t(col)];
      double cx = colX[size_t(col)];
      const QChar al = m.align[col];
      if (al == 'c') cx += (colW[size_t(col)] - b.w) / 2; else if (al == 'r') cx += colW[size_t(col)] - b.w;
      put(body, b, cx, top + rowY[size_t(r)]);
    }
  for (size_t i = 0; i < hlineY.size(); i++) body.prims.push_back(rectPrim(0, top + hlineY[i], W, rule));
  if (arrayLike) {
    for (int i = 0; i <= C; i++)
      for (int v = 0; v < m.vbars[size_t(i)]; v++) {
        double vx = i == 0 ? 0.0 : i == C ? W - rule : colX[size_t(i)] - gap[size_t(i)] / 2 + (v - (m.vbars[size_t(i)] - 1) / 2.0) * 0.1 * em;
        body.prims.push_back(rectPrim(vx, top, rule, totalH));
      }
  }
  body.w = W;
  body.h = totalH / 2 + axis;
  body.d = std::max(0.0, totalH / 2 - axis);
  if (m.ldelim.isEmpty() && m.rdelim.isEmpty()) { body.cls = cOrd; return body; }
  // delimiters grow with the matrix (TeXbook: at least 90.1 % of twice the larger distance from the axis, or 5pt less than it)
  const double delta = std::max(body.h - axis, body.d + axis);
  const double H = std::max({0.901 * 2 * delta, 2 * delta - 0.5 * em, em});
  B l = delimBox(m.ldelim, H, em, cOpen), r = delimBox(m.rdelim, H, em, cClose);
  const double padL = (m.ldelim.isEmpty() ? 0 : 0.04 * em), padR = (m.rdelim.isEmpty() ? 0 : 0.04 * em);
  B out;
  put(out, l, 0, 0);
  put(out, body, l.w + padL, 0);
  put(out, r, l.w + padL + body.w + padR, 0);
  out.w = l.w + padL + body.w + padR + r.w;
  out.h = std::max({l.h, body.h, r.h});
  out.d = std::max({l.d, body.d, r.d});
  out.cls = cOrd;
  return out;
}

QString mapText(const QString& s, Fnt f) {
  QString out;
  const auto u = s.toUcs4();
  for (char32_t ch : u) out += glyphFor(ch, f);
  return out;
}

B layoutText(const Node& n, const Ctx& c) {
  const double em = c.em();
  const QString mapped = mapText(n.s, Fnt(n.iv));
  return glyphBox(mapped, em, cOrd);
}

B layoutLeft(const Node& n, const Ctx& c) {
  const double em = c.em();
  const Kal& k = kal();
  std::vector<B> segs;
  double h = 0, d = 0;
  for (const NodeP& g : n.list) {
    segs.push_back(layoutNode(*g, c));
    h = std::max(h, segs.back().h);
    d = std::max(d, segs.back().d);
  }
  const double axis = k.axis * em;
  const double delta = std::max(h - axis, d + axis);
  const double H = std::max({0.901 * 2 * delta, 2 * delta - 0.5 * em, em});
  B out;
  double x = 0;
  for (size_t i = 0; i < n.strs.size(); i++) {
    const Cls cl = i == 0 ? cOpen : i + 1 == n.strs.size() ? cClose : cRel;
    B dl = delimBox(n.strs[i], H, em, cl);
    put(out, dl, x, 0);
    x += dl.w;
    out.h = std::max(out.h, dl.h);
    out.d = std::max(out.d, dl.d);
    if (i < segs.size()) {
      put(out, segs[i], x, 0);
      x += segs[i].w;
      out.h = std::max(out.h, segs[i].h);
      out.d = std::max(out.d, segs[i].d);
    }
  }
  out.w = x;
  out.cls = cInner;
  return out;
}

B layoutNode(const Node& n, const Ctx& c) {
  const double em = c.em();
  switch (n.k) {
    case Node::Atom: {
      B b;
      if (n.cp == 0x2032) {
        b = glyphBox(QString(QChar(0x2032)), em, cOrd);
      } else {
        const QString g = n.iv == 1 ? ucs(n.cp) : glyphFor(n.cp, c.font);
        b = glyphBox(g, em, n.cls);
      }
      b.cls = n.cls;
      return b;
    }
    case Node::Group: {
      B b = layoutList(n.list, c);
      b.cls = cOrd;
      return b;
    }
    case Node::Frac: return layoutFrac(n, c);
    case Node::Sqrt: return layoutSqrt(n, c);
    case Node::BigOp: return layoutBigOp(n, c);
    case Node::NamedOp: {
      B b = glyphBox(mapText(n.s, Fnt::Rm), em, n.cls);
      b.simple = false;
      b.cls = n.cls;
      return b;
    }
    case Node::Scripts: {
      const Node& base = *n.a;
      B nuc = layoutNode(base, c);
      B sub, sup;
      if (n.b) sub = layoutNode(*n.b, c.with(subStyle(c.style)));
      if (n.c) sup = layoutNode(*n.c, c.with(supStyle(c.style) | (c.cramped() ? 1 : 0)));
      if (limitsWanted(base, c)) return attachLimits(nuc, n.b ? &sub : nullptr, n.c ? &sup : nullptr, c);
      return attachScripts(std::move(nuc), n.b ? &sub : nullptr, n.c ? &sup : nullptr, c);
    }
    case Node::Accent: return layoutAccent(n, c);
    case Node::Line: {
      B body = layoutNode(*n.a, c.with(n.iv == 0 ? (c.style | 1) : c.style));
      const double theta = kal().rule * em;
      B out;
      put(out, body, 0, 0);
      if (n.iv == 0) {
        out.prims.push_back(rectPrim(0, -(body.h + 3 * theta + theta), body.w, theta));
        out.h = body.h + 4 * theta; out.d = body.d;
      } else {
        out.prims.push_back(rectPrim(0, body.d + 3 * theta, body.w, theta));
        out.h = body.h; out.d = body.d + 4 * theta;
      }
      out.w = body.w;
      out.ital = body.ital;
      return out;
    }
    case Node::Brace: return layoutBrace(n, c);
    case Node::Stack: {
      B base = layoutNode(*n.b, c);
      const Ctx cs = c.with(c.style < S ? S : SS);
      B lab = layoutNode(*n.a, n.iv == 0 ? cs : cs.with(cs.style | 1));
      const double W = std::max(base.w, lab.w);
      B out;
      put(out, base, (W - base.w) / 2, 0);
      if (n.iv == 0) {
        const double gap = 0.15 * em;
        put(out, lab, (W - lab.w) / 2, -(base.h + gap + lab.d));
        out.h = base.h + gap + lab.d + lab.h; out.d = base.d;
      } else {
        const double gap = 0.15 * em;
        put(out, lab, (W - lab.w) / 2, base.d + gap + lab.h);
        out.h = base.h; out.d = base.d + gap + lab.h + lab.d;
      }
      out.w = W;
      out.cls = base.cls == cRel ? cRel : cOrd;
      return out;
    }
    case Node::XArrow: return layoutXArrow(n, c);
    case Node::Left: return layoutLeft(n, c);
    case Node::BigDelim: {
      B b = delimBox(n.s, n.dv * em, em, n.cls);
      return b;
    }
    case Node::Matrix: return layoutMatrix(n, c);
    case Node::Text: return layoutText(n, c);
    case Node::Space: {
      B b;
      b.w = n.iv == 1 ? n.dv : n.dv * em;
      b.glue = true;
      return b;
    }
    case Node::StyleSw: {
      Ctx c2 = c;
      c2.style = n.iv | (c.cramped() ? 1 : 0);
      return layoutNode(*n.a, c2);
    }
    case Node::Color: {
      B b = layoutNode(*n.a, c);
      for (MathPrim& p : b.prims) if (!p.color.isValid()) p.color = n.color;
      return b;
    }
    case Node::FontWrap: {
      Ctx c2 = c;
      c2.font = Fnt(n.iv);
      return layoutNode(*n.a, c2);
    }
    case Node::Phantom: {
      B b = layoutNode(*n.a, c);
      b.prims.clear();
      if (n.iv == 1) { b.h = b.d = 0; }
      if (n.iv == 2) { b.w = 0; }
      if (n.iv == 3) {   // \smash keeps the ink but drops its height and depth
        B s = layoutNode(*n.a, c);
        s.h = s.d = 0;
        return s;
      }
      b.glue = false;
      return b;
    }
    case Node::Boxed: {
      B body = layoutNode(*n.a, c);
      const double sep = 0.3 * em, lw = 0.04 * em;
      B out;
      put(out, body, sep, 0);
      QPainterPath p;
      p.addRect(lw / 2, -(body.h + sep) + lw / 2, body.w + 2 * sep - lw, body.h + body.d + 2 * sep - lw);
      out.prims.push_back(pathPrim(p, true, lw));
      out.w = body.w + 2 * sep;
      out.h = body.h + sep;
      out.d = body.d + sep;
      return out;
    }
    case Node::Cancel: {
      B body = layoutNode(*n.a, c);
      B out = body;
      const double lw = 0.04 * em, ov = 0.05 * em;
      if (n.iv == 4) { out.prims.push_back(linePrim(0, -kal().xh * em / 2, body.w, -kal().xh * em / 2, lw)); return out; }
      if (n.iv == 3) {   // \not: a slash through the middle of the symbol
        out.prims.push_back(linePrim(body.w * 0.25, body.d + ov, body.w * 0.75, -(body.h + ov), lw));
        return out;
      }
      if (n.iv == 0 || n.iv == 2) out.prims.push_back(linePrim(-ov, body.d + ov, body.w + ov, -(body.h + ov), lw));
      if (n.iv == 1 || n.iv == 2) out.prims.push_back(linePrim(-ov, -(body.h + ov), body.w + ov, body.d + ov, lw));
      return out;
    }
    case Node::ClassWrap: {
      B b = layoutNode(*n.a, c);
      b.cls = n.cls;
      return b;
    }
  }
  return B();
}

// ===================================================================================================================================
// Finishing, drawing
// ===================================================================================================================================
QRectF primBounds(const MathPrim& p) {
  if (!p.path || p.path->isEmpty()) return QRectF();
  QRectF r = p.path->boundingRect();
  if (p.stroke) r.adjust(-p.lineWidth / 2, -p.lineWidth / 2, p.lineWidth / 2, p.lineWidth / 2);
  return QRectF(r.x() * p.scale + p.tx, r.y() * p.scale + p.ty, r.width() * p.scale, r.height() * p.scale);
}

}  // namespace

// ===================================================================================================================================
// Public API
// ===================================================================================================================================
MathBox layoutMath(const QString& tex, double sizePt, bool display) {
  MathBox r;
  r.sizePt = sizePt;
  r.display = display;
  if (tex.trimmed().isEmpty()) { r.ok = true; r.empty = true; return r; }
  try {
    const NodeP root = Parser(tex).parseAll();
    Ctx c;
    c.style = display ? D : T;
    c.base = sizePt;
    B b = layoutNode(*root, c);
    // the box takes in every bit of ink (italic overhang, tall integrals) plus a margin, so nothing is clipped
    const double pad = 0.05 * sizePt;
    double x0 = 0, x1 = b.w, y0 = -b.h, y1 = b.d;
    for (const MathPrim& p : b.prims) {
      const QRectF q = primBounds(p);
      if (q.isNull()) continue;
      x0 = std::min(x0, q.left()); x1 = std::max(x1, q.right());
      y0 = std::min(y0, q.top()); y1 = std::max(y1, q.bottom());
    }
    x0 -= pad; x1 += pad; y0 -= pad; y1 += pad;
    for (MathPrim& p : b.prims) p.tx -= x0;
    r.width = x1 - x0;
    r.ascent = -y0;
    r.descent = y1;
    r.prims = std::move(b.prims);
    r.ok = true;
  } catch (const ParseError& e) {
    r.ok = false;
    r.error = e.pos >= 0 ? QStringLiteral("%1 (posición %2)").arg(e.msg).arg(e.pos + 1) : e.msg;
    r.errorPos = e.pos;
  }
  return r;
}

MathBox layoutMathToHeight(const QString& tex, bool display, double heightPt) {
  MathBox m = layoutMath(tex, 10, display);
  if (!m.ok || m.empty || m.height() <= 0 || heightPt <= 0) return m;
  return layoutMath(tex, 10 * heightPt / m.height(), display);
}

void drawMath(QPainter& p, const MathBox& box, QPointF origin, const QColor& color) {
  if (!box.ok) return;
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  const QTransform base = p.worldTransform();
  for (const MathPrim& pr : box.prims) {
    if (!pr.path) continue;
    const QColor col = pr.color.isValid() ? pr.color : color;
    p.setWorldTransform(QTransform(pr.scale, 0, 0, pr.scale, origin.x() + pr.tx, origin.y() + pr.ty) * base, false);
    if (pr.stroke) {
      QPen pen(col, pr.lineWidth / std::max(pr.scale, 1e-9), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
      p.strokePath(*pr.path, pen);
    } else {
      p.fillPath(*pr.path, col);
    }
  }
  p.restore();
}

QImage renderMathImage(const MathBox& box, const QColor& color, double pxPerPt, MathImageInfo* info) {
  if (!box.ok || box.empty) return QImage();
  double s = pxPerPt;
  if (box.width * s > 2048) s = 2048 / box.width;
  const int w = std::max(1, int(std::ceil(box.width * s))), h = std::max(1, int(std::ceil(box.height() * s)));
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.scale(double(w) / box.width, double(h) / box.height());
  drawMath(p, box, QPointF(0, box.ascent), color);
  p.end();
  if (info) {
    info->widthPt = box.width; info->heightPt = box.height(); info->baselinePt = box.ascent;
    info->widthPx = w; info->heightPx = h;
  }
  return img;
}

QByteArray renderMathPdf(const MathBox& box, const QColor& color) {
  QByteArray out;
  if (!box.ok || box.empty) return out;
  QBuffer buf(&out);
  buf.open(QIODevice::WriteOnly);
  {
    QPdfWriter w(&buf);
    w.setResolution(72);
    w.setPageSize(QPageSize(QSizeF(box.width, box.height()), QPageSize::Point));
    w.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    w.setCreator(QStringLiteral("Xournal Lab"));
    QPainter p(&w);
    drawMath(p, box, QPointF(0, box.ascent), color);
    p.end();
  }
  buf.close();
  return out;
}

TexImageResult makeTexImage(const QString& tex, double sizePt, bool display, const QColor& color, double left, double top) {
  TexImageResult r;
  // KaTeX sets .katex at 1.21em, so the Electron app's formulas come out 21% larger than their nominal size: the same look, and the same box in Xournal++.
  r.box = layoutMath(tex, sizePt * 1.21, display);
  if (!r.box.ok) { r.error = r.box.error; return r; }
  if (r.box.empty) { r.error = QStringLiteral("La fórmula está vacía"); return r; }
  const QByteArray pdf = renderMathPdf(r.box, color);
  if (pdf.isEmpty()) { r.error = QStringLiteral("No se pudo generar el PDF de la fórmula"); return r; }
  r.bitmap = renderMathImage(r.box, color, 4.0);
  r.el.tag = QStringLiteral("teximage");
  r.el.attrs = {{"text", tex}, {"left", fmt8(left)}, {"top", fmt8(top)}, {"right", fmt8(left + r.box.width)}, {"bottom", fmt8(top + r.box.height())}};
  r.el.data = QString::fromLatin1(pdf.toBase64());
  r.ok = true;
  return r;
}
