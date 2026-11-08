// Hostile but loadable documents through everything that runs on load and on every repaint: repair, geometry, the spatial index and the renderer.
// Strokes of 0/1 points, mismatching width lists, NaN-looking attributes, coordinates of 1e300, absurd pages, broken images, texts and papers.
// Nothing may crash, hang (each step is timed) or allocate without bound.
#include "geometry.h"
#include "pagerender.h"
#include "repair.h"
#include "spatial.h"
#include "xopp.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static QString kHead = "<?xml version=\"1.0\"?><xournal creator=\"t\" fileversion=\"4\"><title>t</title>";

/** One page with the given layer content; `pageAttrs` lets a case ruin the page size. */
static Document doc(const QString& layerBody, const QString& pageAttrs = "width=\"200\" height=\"200\"", const QString& bg = "type=\"solid\" color=\"#ffffffff\" style=\"plain\"") {
  const LoadResult r = parseXml((kHead + "<page " + pageAttrs + "><background " + bg + "/><layer>" + layerBody + "</layer></page></xournal>").toUtf8());
  if (!r.ok) { std::printf("FAIL setup: %s\n", qPrintable(r.error)); failures++; return Document(); }
  return r.doc;
}

static double stepMs = 0;
static QElapsedTimer timer;

/** Everything the app does with a page, on a small canvas. */
static void exercise(const char* name, Document d) {
  timer.start();
  repairGenerated(d);
  for (Page& p : d.pages) {
    ElementIndex idx;
    idx.rebuild(p);
    std::vector<uint64_t> ids;
    idx.query(Rect{-1e6, -1e6, 1e6, 1e6}, ids);
    idx.queryPoint(5, 5, 4, ids);
    for (Layer& l : p.layers)
      for (Element& e : l.elements) {
        const Rect b = elementBBox(e);
        (void)b;
        hitElement(e, 5, 5, 3);
        inRect(e, Rect{0, 0, 50, 50});
        elementRenderBounds(e);
        snapshot(e);
        transformElement(e, translateAff(3, 4));
        transformElement(e, scaleAff(2, 2, 0, 0));
        if (const auto* s = std::get_if<Stroke>(&e)) { strokeHit(*s, 5, 5, 2); splitStroke(*s, 5, 5, 3); strokeWidthAt(*s, 0); strokeWidthAt(*s, 100000); }
      }
    QImage img(160, 160, QImage::Format_ARGB32_Premultiplied);
    for (double scale : {0.25, 1.0, 3.0}) {
      for (bool lite : {false, true}) {
        img.fill(Qt::white);
        QPainter pt(&img);
        pt.scale(scale, scale);
        RenderOptions o;
        o.scale = scale;
        o.lite = lite;
        o.cullPieces = true;
        o.clip = QRectF(0, 0, 160 / scale, 160 / scale);
        renderPage(pt, p, o);
        std::vector<const Element*> all;
        for (const Layer& l : p.layers) for (const Element& e : l.elements) all.push_back(&e);
        renderElementSeq(pt, o, all);
      }
    }
  }
  serializeXml(d);
  const double ms = double(timer.elapsed());
  stepMs = std::max(stepMs, ms);
  CHECK(ms < 8000, "%s took %.0f ms", name, ms);
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  const QString S = "<stroke tool=\"pen\" color=\"#000000ff\" width=\"%1\">%2</stroke>";
  auto stroke = [&](const QString& w, const QString& pts) { return S.arg(w, pts); };
  struct Case { const char* name; QString body; QString pageAttrs; QString bg; };
  const QString okPage = "width=\"200\" height=\"200\"", okBg = "type=\"solid\" color=\"#ffffffff\" style=\"plain\"";
  const std::vector<Case> cases = {
      {"zero-point stroke", stroke("2", ""), okPage, okBg},
      {"one-point stroke", stroke("2", "50 50"), okPage, okBg},
      {"one-point stroke, huge width", stroke("1e9", "50 50"), okPage, okBg},
      {"two points identical", stroke("2", "5 5 5 5"), okPage, okBg},
      {"no widths", stroke("", "1 1 50 50 90 10"), okPage, okBg},
      {"more widths than segments", stroke("1 2 3 4 5 6 7 8 9", "1 1 50 50"), okPage, okBg},
      {"fewer widths than segments", stroke("1 2", "1 1 50 50 60 20 70 90 80 10 90 90"), okPage, okBg},
      {"negative and zero widths", stroke("-3 0 -1", "1 1 50 50 60 20 70 90"), okPage, okBg},
      {"huge coordinates", stroke("2", "-1e300 -1e300 1e300 1e300 5 5"), okPage, okBg},
      {"coordinates near double max", stroke("2", "1.7e308 1.7e308 -1.7e308 5 5 5"), okPage, okBg},
      {"1e15 coordinates", stroke("5", "-1e15 5 1e15 5"), okPage, okBg},
      {"tiny coordinates", stroke("2", "1e-300 1e-300 2e-300 -1e-300"), okPage, okBg},
      {"width huge list", stroke("1e300 1e300", "1 1 50 50"), okPage, okBg},
      {"highlighter, dash, glow garbage",
       "<stroke tool=\"highlighter\" color=\"#zz\" width=\"3\" style=\"dash\" glow=\"1e9\" glowAlpha=\"nan\" capStyle=\"???\">1 1 50 50 90 10</stroke>"
       "<stroke tool=\"eraser\" color=\"red\" width=\"3\" fill=\"999\">1 1 50 50</stroke>"
       "<stroke tool=\"\" color=\"\" width=\"3\" glow=\"-5\">1 1 50 50</stroke>"
       "<stroke tool=\"nonsense\" color=\"#12345\" width=\"3\" glow=\"40\" glowAlpha=\"2\" style=\"dashdot\">1 1 50 50 70 10</stroke>", okPage, okBg},
      {"texts", "<text font=\"\" size=\"0\" x=\"10\" y=\"10\" color=\"#000000ff\">cero</text>"
                "<text font=\"Sans\" size=\"1e9\" x=\"10\" y=\"10\" color=\"#000000ff\">enorme</text>"
                "<text font=\"Sans\" size=\"-4\" x=\"nan\" y=\"inf\" color=\"zzz\">raro</text>"
                "<text>sin atributos</text>"
                "<text font=\"Sans\" size=\"12\" x=\"1e300\" y=\"-1e300\" color=\"#000000ff\">lejos</text>"
                "<text font=\"Sans\" size=\"12\" x=\"5\" y=\"5\" xlMd=\"1\" color=\"#000000ff\"># " + QString("tit ").repeated(300) + "\n- a\n  - b\n    - c\n> q\n```\ncode\n```\n$$x^{^}$$ **a *b** c*\n</text>"
                "<text font=\"Sans\" size=\"12\" x=\"5\" y=\"5\" xlMd=\"1\" color=\"#000000ff\">" + QString("x\n").repeated(5000) + "</text>", okPage, okBg},
      {"images", "<image left=\"10\" top=\"10\" right=\"50\" bottom=\"50\">!!!not base64!!!</image>"
                 "<image left=\"50\" top=\"50\" right=\"10\" bottom=\"10\">iVBORw0KGgo=</image>"
                 "<image left=\"nan\" top=\"x\" right=\"\" bottom=\"1e400\"></image>"
                 "<image left=\"-1e300\" top=\"-1e300\" right=\"1e300\" bottom=\"1e300\">iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==</image>"
                 "<teximage text=\"\\frac{a}{b}\" left=\"10\" top=\"10\" right=\"50\" bottom=\"50\">%PDF-garbage</teximage>"
                 "<teximage left=\"10\" top=\"10\" right=\"50\" bottom=\"50\"></teximage>"
                 "<teximage text=\"x\" left=\"10\" top=\"10\" right=\"10\" bottom=\"10\" xlRot=\"nan\">AAAA</teximage>", okPage, okBg},
      {"generated objects with broken definitions",
       "<stroke tool=\"pen\" color=\"#000000ff\" width=\"1\" xlPlane=\"pl1\" xlRole=\"axis\" xlPlaneSpec=\"{not json\" xlPlaneW=\"abc\">1 1 50 50</stroke>"
       "<text font=\"Sans\" size=\"12\" color=\"#000000ff\" xlPlane=\"pl1\" xlRole=\"label\">sin posicion</text>"
       "<stroke tool=\"pen\" color=\"#00000000\" width=\"1\" xlPlane=\"fg1\" xlRole=\"figwitness\" xlFig=\"{&quot;kind&quot;:&quot;box&quot;,&quot;pts&quot;:[1,2]}\">1 1 2 2 3 3</stroke>"
       "<text font=\"Sans\" size=\"12\" color=\"#000000ff\" xlPlane=\"fg1\" xlRole=\"label\">otra</text>"
       "<stroke tool=\"pen\" color=\"#00000000\" width=\"1\" xlPlane=\"sp1\" xlRole=\"axis\" xlSpace3=\"[]\">1 1 2 2</stroke>"
       "<text font=\"Sans\" size=\"12\" color=\"#000000ff\" xlPlane=\"sp1\" xlRole=\"label\">sp</text>"
       "<stroke tool=\"pen\" color=\"#00000000\" width=\"1\" xlPlane=\"st1\" xlRole=\"setwitness\" xlSet=\"{&quot;k&quot;:99}\">1 1 2 2 3 3</stroke>"
       "<stroke tool=\"pen\" color=\"#00000000\" width=\"1\" xlRole=\"axis\" xlPlaneSpec=\"{}\" xlPlane=\"\">1 1 2 2</stroke>", okPage, okBg},
      {"absurd page size 0", stroke("2", "1 1 50 50"), "width=\"0\" height=\"0\"", okBg},
      {"absurd page size negative", stroke("2", "1 1 50 50"), "width=\"-5\" height=\"-5\"", okBg},
      {"absurd page size 1e9", stroke("2", "1 1 50 50"), "width=\"1e9\" height=\"1e9\"", okBg},
      {"absurd page size text", stroke("2", "1 1 50 50"), "width=\"abc\" height=\"\"", okBg},
      {"page size missing", stroke("2", "1 1 50 50"), "", okBg},
      {"page size nan", stroke("2", "1 1 50 50"), "width=\"nan\" height=\"inf\"", okBg},
      {"page size tiny", stroke("2", "1 1 50 50"), "width=\"1e-9\" height=\"1e-9\"", okBg},
      {"background garbage", stroke("2", "1 1 50 50"), okPage, "type=\"zzz\" color=\"#nothex\" style=\"???\" xlPaper=\"cart;quad=zzz;unit=-1e9;sub=nan\""},
      {"background pdf without file", stroke("2", "1 1 50 50"), okPage, "type=\"pdf\" domain=\"absolute\" filename=\"Z:/no/such.pdf\" pageno=\"99999999999\""},
      {"background pixmap garbage", stroke("2", "1 1 50 50"), okPage, "type=\"pixmap\" domain=\"attach\" filename=\"\"" },
      {"special papers with extreme params", stroke("2", "1 1 50 50"), okPage, "type=\"solid\" color=\"#ffffffff\" style=\"graph\" xlPaper=\"mm;unit=1e-9\""},
      {"special papers polar extreme", stroke("2", "1 1 50 50"), okPage, "type=\"solid\" color=\"#ffffffff\" style=\"plain\" xlPaper=\"polar;rings=1e9;spokes=1e9\""},
      {"unknown elements only", "<timestamp ts=\"1\" fn=\"x\"/><audio fn=\"a\"/>", okPage, okBg},
  };
  for (const Case& c : cases) exercise(c.name, doc(c.body, c.pageAttrs, c.bg));
  std::printf("robust_render: %zu hostile documents exercised, slowest %.0f ms\n", cases.size(), stepMs);
  if (failures) std::printf("%d check(s) failed\n", failures);
  else std::printf("robust_render: all checks passed\n");
  return failures ? 1 : 0;
}
