// Text/shape/formula fixes: sharp corners of polyline shapes, formulas drawn again from their PDF at deep zoom, \tag, the Electron
// app's formula PDFs (app/shots/e2e-out.xopp teximages when present) and Markdown "__bold__".
#include "markdown.h"
#include "mathlayout.h"
#include "pagerender.h"
#include "pdfraster.h"
#include "xopp.h"
#include <QDir>
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static QImage draw(const Page& page, double scale) {
  QImage img(int(std::ceil(page.width() * scale)), int(std::ceil(page.height() * scale)), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter p(&img);
  p.scale(scale, scale);
  RenderOptions o;
  o.scale = scale;
  o.background = false;
  renderPage(p, page, o);
  return img;
}

static Page pageWith(Element e) {
  Page pg;
  pg.background = {{"type", "solid"}, {"color", "#ffffffff"}};
  pg.attrs = {{"width", "200"}, {"height", "200"}};
  pg.layers.emplace_back();
  pg.layers[0].elements.push_back(std::move(e));
  return pg;
}

static int lastRequested = -1;
static QImage recordingDecoder(const QByteArray& pdf, int widthPx) {
  lastRequested = widthPx;
  return rasterizePdfFormula(pdf, widthPx);
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  // sharp corners: a 5-point rectangle outline must reach its corner pixel (a midpoint quadratic would round it off by about a stroke width)
  {
    Stroke s;
    s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "butt"}};
    s.widths = {8};
    s.pts = {50, 50, 150, 50, 150, 150, 50, 150, 50, 50};
    Page legacy = pageWith(Element(s));
    s.attrs.emplace_back("xlShape", "1");
    Page marked = pageWith(Element(s));
    for (const Page* pg : {&legacy, &marked}) {
      const QImage im = draw(*pg, 4);
      // the outer corner of the outline: a square joint (round join keeps the corner at 4 units from it, so look at 2.5 units out along the diagonal)
      const QColor c = im.pixelColor(int((150 + 2.5) * 4), int((50 - 2.5) * 4));
      CHECK(c.red() < 120, "corner of a polyline shape is rounded off (%s, red=%d)", pg == &legacy ? "no mark, 5 spaced points" : "xlShape", c.red());
    }
    // a pen stroke (many close points) is still smoothed through midpoints: its corner is rounded as before
    Stroke pen;
    pen.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
    pen.widths = {8};
    for (int i = 0; i <= 20; i++) { pen.pts.push_back(50 + i * 5); pen.pts.push_back(50); }
    for (int i = 1; i <= 20; i++) { pen.pts.push_back(150); pen.pts.push_back(50 + i * 5); }
    const QImage im = draw(pageWith(Element(pen)), 4);
    const QColor c = im.pixelColor(int((150 + 3.8) * 4), int((50 - 3.8) * 4));
    CHECK(c.red() > 200, "a free pen stroke lost its smoothed corner (red=%d)", c.red());
  }

  // \tag puts the number at the right of the formula
  {
    const MathBox a = layoutMath("a=b", 14, true), b = layoutMath("a=b \\tag{1}", 14, true), c = layoutMath("a=b \\tag*{x}", 14, true);
    CHECK(a.ok && b.ok && c.ok && b.width > a.width + 10 && c.width > a.width + 5 && c.width < b.width, "\\tag widths %g %g %g", a.width, b.width, c.width);
  }

  // Markdown
  {
    const auto r = inlineRuns("__bold__ x");
    CHECK(r.size() == 2 && r[0].bold && r[0].text == "bold", "__bold__ closes after a word character");
  }

  // formulas made by the Electron app: drawn from their own PDF, sharper when zoomed
  if (pdfRasterAvailable()) {
    const QString xopp = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("G:/xournal-lab/app/shots/e2e-out.xopp");
    const LoadResult lr = QFile::exists(xopp) ? loadXopp(xopp) : LoadResult();
    const Document& doc = lr.doc;
    if (!lr.ok) {
      std::printf("note: %s not available, Electron formula checks skipped\n", qPrintable(xopp));
    } else {
      int n = 0;
      for (const Page& pg : doc.pages)
        for (const Layer& l : pg.layers)
          for (const Element& e : l.elements) {
            const auto* im = std::get_if<Image>(&e);
            if (!im || im->tag != "teximage") continue;
            n++;
            const QByteArray pdf = QByteArray::fromBase64(im->data.toLatin1());
            CHECK(pdf.startsWith("%PDF"), "teximage data is not a PDF");
            const QImage deflt = rasterizePdfFormula(pdf);
            CHECK(!deflt.isNull() && deflt.width() >= 60, "Electron formula PDF did not rasterise");
            const QImage big = rasterizePdfFormula(pdf, 3000);
            CHECK(big.width() == 3000, "deep-zoom raster is %d px wide, wanted 3000", big.width());
            // rendering the page at a deep zoom asks the decoder for more pixels than the default
            setPdfImageDecoder(&recordingDecoder);
            Image copy = *im;
            Page one = pageWith(Element(copy));
            lastRequested = -1;
            const double box = std::abs(attrOr(copy.attrs, "right", "0").toDouble() - attrOr(copy.attrs, "left", "0").toDouble());
            (void)draw(one, 3);
            CHECK(lastRequested == 0 || lastRequested == -1 || lastRequested <= int(box * 4) + 1, "at 3x the default raster is enough (asked %d)", lastRequested);
            lastRequested = -1;
            clearRenderCaches();
            (void)draw(one, 12);
            CHECK(lastRequested > box * 4 * 1.2 && lastRequested <= 4096, "at 12x the formula is drawn again from its PDF (asked %d px for a %.0f pt box)", lastRequested, box);
            setPdfImageDecoder(&rasterizePdfFormula);
          }
      CHECK(n >= 2, "expected the Electron formulas of the e2e file (found %d)", n);
    }
  }
  std::printf(failures ? "\n%d FAILED\n" : "\ntextfixes: all passed\n", failures);
  return failures ? 1 : 0;
}
