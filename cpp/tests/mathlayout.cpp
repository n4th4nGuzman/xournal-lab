// Formula engine: every palette snippet and label lays out, tricky formulas have sane sizes, errors are in Spanish, and the teximage element
// has the attributes the Electron app reads. Needs a system math font (Cambria Math on Windows); runs on the offscreen platform.
#include "latexpalette.h"
#include "mathlayout.h"
#include <QByteArray>
#include <QGuiApplication>
#include <QImage>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static MathBox L(const char* tex, bool display = true, double size = 20) { return layoutMath(QString::fromUtf8(tex), size, display); }

// Palette buttons whose whole point is to draw nothing: a space, a line break, an invisible box.
static bool invisible(const QString& tex) { return tex == "\\ " || tex == "\\\\" || tex.startsWith("\\phantom"); }

static void paletteLaysOut() {
  int items = 0;
  for (const PaletteCategory& cat : paletteCategories()) {
    for (const PaletteItem& it : cat.items) {
      items++;
      const QString snippet = snippetInsert(it.snippet).text;
      for (const QString& tex : {it.tex, snippet}) {
        for (bool display : {true, false}) {
          const MathBox b = layoutMath(tex, 14, display);
          CHECK(b.ok, "[%s] %s -> %s", qPrintable(cat.id), qPrintable(tex), qPrintable(b.error));
          if (b.ok && tex == it.tex && !invisible(tex)) {
            CHECK(!b.empty && b.width > 1 && b.height() > 1 && !b.prims.empty(), "empty result for %s", qPrintable(tex));
            CHECK(b.height() < 14 * 12 && b.width < 14 * 90, "absurd size %gx%g for %s", b.width, b.height(), qPrintable(tex));
          }
        }
      }
    }
  }
  CHECK(items >= 400, "palette has only %d items", items);
  // the label of every button renders to visible pixels
  int blank = 0;
  for (const PaletteCategory& cat : paletteCategories())
    for (const PaletteItem& it : cat.items) {
      if (invisible(it.tex)) continue;
      const QImage img = renderMathImage(layoutMath(it.tex, 12, false), Qt::black, 3);
      bool ink = false;
      for (int y = 0; y < img.height() && !ink; y++)
        for (int x = 0; x < img.width(); x++) if (qAlpha(img.pixel(x, y)) > 40) { ink = true; break; }
      if (!ink) { blank++; std::printf("blank label: %s\n", qPrintable(it.tex)); }
    }
  CHECK(blank == 0, "%d labels draw nothing", blank);
  // search and caret
  CHECK(!searchPalette("raiz con indice").empty(), "search without accents");
  CHECK(!searchPalette("determinante").empty(), "search determinante");
  const SnippetInsert si = snippetInsert(QString::fromUtf8("\\frac{\xC2\xA6}{}"));
  CHECK(si.text == "\\frac{}{}" && si.caretBack == 3, "caret %d", si.caretBack);
}

static void sizes() {
  const double s = 20;
  const MathBox x = L("x"), x2 = L("x^{2}"), fr = L("\\frac{a}{b}"), fr2 = L("\\frac{1}{\\frac{1}{x}}"), a = L("a");
  CHECK(x.ok && x2.ok && fr.ok, "basic");
  CHECK(x2.width > x.width + 0.3 * s * 0.5, "superscript adds width %g vs %g", x2.width, x.width);
  CHECK(x2.ascent > x.ascent + 0.2 * s, "superscript raises the box %g vs %g", x2.ascent, x.ascent);
  // the script is about 70% of the base: x^{x} adds ~0.7 of x's own advance
  const double add = L("x^{x}").width - x.width, own = x.width - 2 * 0.05 * s;
  CHECK(add > 0.55 * own && add < 0.95 * own, "script scale: added %g for a base of %g", add, own);
  CHECK(fr.height() > 1.8 * a.height(), "fraction taller than a letter %g vs %g", fr.height(), a.height());
  CHECK(fr2.height() > fr.height() + 0.5 * s, "nested fraction grows %g vs %g", fr2.height(), fr.height());
  // a fraction is centred on the maths axis: its box is about symmetric around it
  const MathBox f11 = L("\\frac{1}{1}");
  const double off = (f11.ascent - f11.descent) / s;
  CHECK(off > 0.3 && off < 0.9, "fraction centred on the axis (ascent-descent)/size = %g", off);
  // big operators: limits above/below in display, at the side inline
  const MathBox sumD = L("\\sum_{i=1}^{n}", true), sumT = L("\\sum_{i=1}^{n}", false);
  CHECK(sumD.height() > sumT.height() + 0.3 * s, "display sum has limits stacked: %g vs %g", sumD.height(), sumT.height());
  CHECK(sumD.width < sumT.width + s, "display limits do not widen: %g vs %g", sumD.width, sumT.width);
  CHECK(L("\\int_{0}^{1} x\\,dx").height() > 1.8 * s, "integral is tall");
  CHECK(L("\\lim_{x\\to 0}", true).height() > L("\\lim_{x\\to 0}", false).height() + 0.2 * s, "lim limits below in display");
  // stretchy delimiters follow their content
  const MathBox p1 = L("\\left(x\\right)"), p2 = L("\\left(\\frac{a}{b}\\right)"), p3 = L("\\left(\\frac{\\frac{a}{b}}{c}\\right)");
  CHECK(p2.height() > p1.height() + 0.3 * s, "delimiters grow %g vs %g", p2.height(), p1.height());
  CHECK(p3.height() > p2.height() + 0.3 * s, "delimiters grow again %g vs %g", p3.height(), p2.height());
  CHECK(p2.height() >= fr.height() - 1, "delimited fraction not shorter than the fraction");
  // matrices
  const MathBox m2 = L("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}"), m3 = L("\\begin{pmatrix}a&b&c\\\\d&e&f\\\\g&h&i\\end{pmatrix}");
  CHECK(m2.ok && m3.ok, "matrices %s %s", qPrintable(m2.error), qPrintable(m3.error));
  CHECK(m3.height() > m2.height() + 0.8 * s && m3.width > m2.width + 0.8 * s, "bigger matrix is bigger");
  const MathBox cs = L("\\begin{cases}x&\\text{si }x\\ge0\\\\-x&\\text{si }x<0\\end{cases}");
  CHECK(cs.ok && cs.height() > 2 * s, "cases %s", qPrintable(cs.error));
  const MathBox al = L("\\begin{aligned}a&=b\\\\&=c\\end{aligned}");
  CHECK(al.ok && al.height() > 2 * s && al.width < 8 * s, "aligned");
  // roots grow with their contents
  CHECK(L("\\sqrt{\\frac{a}{b}}").height() > L("\\sqrt{x}").height() + 0.5 * s, "root grows");
  CHECK(L("\\sqrt[3]{x}").width >= L("\\sqrt{x}").width && L("\\sqrt[3]{x}").prims.size() > L("\\sqrt{x}").prims.size(), "root index is drawn");
  // accents sit above the letter
  CHECK(L("\\vec{x}").ascent > x.ascent + 0.1 * s, "accent raises the box");
  CHECK(L("\\underline{x}").descent > x.descent + 0.1 * s, "underline lowers the box");
  // spacing: relations and binary operators get space, a leading minus does not
  CHECK(L("a=b").width > L("ab").width + 0.5 * s, "relation spacing");
  CHECK(L("a+b").width > L("ab").width + 0.4 * s, "binary spacing");
  CHECK(std::abs((L("-a").width - L("a").width) - (L("a-a").width - 2 * L("a").width + 2 * 0.05 * s)) > 0.01, "unary vs binary minus differ");
  // text and spaces
  CHECK(L("a\\quad b").width > L("ab").width + 0.9 * s, "quad");
  CHECK(L("a\\qquad b").width > L("a\\quad b").width + 0.9 * s, "qquad");
  CHECK(L("a\\,b").width > L("ab").width + 0.1 * s, "thin space");
  CHECK(L("a\\!b").width < L("ab").width, "negative space");
  CHECK(L("\\text{hola mundo}").width > 4 * s, "text width");
  // font commands and symbols resolve
  for (const char* t : {"\\mathbb{R}", "\\mathcal{L}", "\\mathfrak{g}", "\\mathbf{x}", "\\mathrm{d}x", "\\boldsymbol{\\alpha}", "\\mathsf{A}", "\\mathtt{c}",
                        "\\operatorname{sgn}(x)", "\\alpha\\beta\\Gamma", "\\infty\\partial\\nabla", "\\leq\\geq\\neq\\approx", "x'", "f''(x)", "f'^{2}",
                        "\\overbrace{a}^{n}", "\\underbrace{a}_{n}", "\\xrightarrow{f}", "\\xleftarrow[b]{a}", "\\boxed{x}", "\\cancel{x}", "{\\color{red} x}",
                        "\\textcolor{blue}{y}", "\\substack{a\\\\b}", "\\binom{n}{k}", "\\cfrac{1}{2}", "x\\over y", "{n \\choose k}", "\\hspace{1em}x",
                        "\\displaystyle\\sum_{i}", "\\sum\\limits_{i}^{n}", "\\int\\limits_a^b","\\left.\\frac{a}{b}\\right|_{0}^{1}",
                        "\\bigl( x \\bigr)", "\\Big| x \\Big|", "\\left\\langle x \\middle| y \\right\\rangle", "a \\\\ b", "\\begin{array}{c|c}a&b\\\\\\hline c&d\\end{array}",
                        "\\alpha+\xCE\xB2", "\\text{a b}^2", "{}^{14}_{6}C", "x_{1}^{2}y"}) {
    const MathBox b = L(t);
    CHECK(b.ok && !b.prims.empty(), "%s -> %s", t, qPrintable(b.error));
  }
  CHECK(L("").ok && L("").empty && L("   ").empty, "blank source");
}

static void errors() {
  struct E { const char* tex; const char* has; };
  const E table[] = {
    {"\\frac{a}{", "llave"},
    {"{a", "llave"},
    {"a}", "Llave de cierre"},
    {"\\foo", "Comando desconocido: \\foo"},
    {"x^2^3", "Superíndice doble"},
    {"x_1_2", "Subíndice doble"},
    {"\\left( x", "Falta \\right"},
    {"x \\right)", "\\right sin su \\left"},
    {"\\left x \\right)", "Delimitador no válido"},
    {"a & b", "solo se puede usar dentro de matrices"},
    {"\\begin{foo} a \\end{foo}", "Entorno desconocido: foo"},
    {"\\begin{pmatrix} a", "Falta \\end{pmatrix}"},
    {"\\begin{pmatrix} a \\end{bmatrix}", "no coincide"},
    {"\\frac{a}", "Falta el argumento de \\frac"},
    {"x^", "Falta el argumento"},
    {"\\sqrt[3", "Falta cerrar el corchete"},
    {"\\hspace{abc}", "Medida no válida"},
    {"{\\color{nocolor} x}", "Color desconocido"},
    {"\\end{x}", "\\end sin su \\begin"},
    {"a \\middle| b", "\\middle"},
  };
  for (const E& e : table) {
    const MathBox b = L(e.tex);
    CHECK(!b.ok, "%s should fail", e.tex);
    CHECK(b.error.contains(QString::fromUtf8(e.has)), "%s -> '%s' (wanted '%s')", e.tex, qPrintable(b.error), e.has);
    CHECK(b.prims.empty(), "no drawing after an error");
    CHECK(b.error.contains("posición") || b.errorPos < 0, "position in '%s'", qPrintable(b.error));
  }
}

static void output() {
  const MathBox b = L("\\frac{a}{b}+\\sqrt{2}", true, 14);
  MathImageInfo info;
  const QImage img = renderMathImage(b, QColor(10, 20, 200), 4, &info);
  CHECK(!img.isNull() && img.hasAlphaChannel(), "image");
  CHECK(qAlpha(img.pixel(0, 0)) == 0, "transparent corner");
  CHECK(std::abs(info.widthPt - b.width) < 1e-9 && std::abs(info.baselinePt - b.ascent) < 1e-9, "image info");
  CHECK(std::abs(img.width() - b.width * 4) <= 1.5, "pixels per point %d vs %g", img.width(), b.width * 4);
  bool blue = false;
  for (int y = 0; y < img.height() && !blue; y++)
    for (int x = 0; x < img.width(); x++) { const QRgb c = img.pixel(x, y); if (qAlpha(c) > 250 && qBlue(c) > 150 && qRed(c) < 60) { blue = true; break; } }
  CHECK(blue, "ink has the colour asked for");
  const QByteArray pdf = renderMathPdf(b, Qt::black);
  CHECK(pdf.startsWith("%PDF-") && pdf.size() > 300, "pdf %d bytes", int(pdf.size()));
  CHECK(pdf.contains("/MediaBox"), "pdf page box");
  // the element the Electron app re-opens: attributes text, left, top, right, bottom; base64 PDF as content
  const TexImageResult r = makeTexImage(QStringLiteral("\\frac{a}{b}"), 14, true, Qt::black, 100, 200);
  CHECK(r.ok, "%s", qPrintable(r.error));
  CHECK(r.el.tag == "teximage", "tag");
  CHECK(r.el.attrs.size() == 5 && r.el.attrs[0].first == "text" && r.el.attrs[0].second == "\\frac{a}{b}" && r.el.attrs[1].first == "left" &&
        r.el.attrs[2].first == "top" && r.el.attrs[3].first == "right" && r.el.attrs[4].first == "bottom", "attribute order");
  CHECK(std::abs(r.el.attrs[1].second.toDouble() - 100) < 1e-9 && std::abs(r.el.attrs[2].second.toDouble() - 200) < 1e-9, "left/top");
  CHECK(std::abs((r.el.attrs[3].second.toDouble() - 100) - r.box.width) < 1e-4 && std::abs((r.el.attrs[4].second.toDouble() - 200) - r.box.height()) < 1e-4, "right/bottom");
  CHECK(QByteArray::fromBase64(r.el.data.toLatin1()).startsWith("%PDF-"), "data is a base64 PDF");
  CHECK(!r.bitmap.isNull(), "bitmap");
  CHECK(!makeTexImage(QStringLiteral("\\frac{"), 14, true, Qt::black, 0, 0).ok, "error propagates");
  CHECK(!makeTexImage(QStringLiteral(" "), 14, true, Qt::black, 0, 0).ok, "empty formula is refused");
  const MathBox fit = layoutMathToHeight(QStringLiteral("\\frac{a}{b}"), true, 50);
  CHECK(std::abs(fit.height() - 50) < 1.0, "fit height %g", fit.height());
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  paletteLaysOut();
  sizes();
  errors();
  output();
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("mathlayout: all checks passed\n");
  return 0;
}
