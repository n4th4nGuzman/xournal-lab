// xl_render_page file.xopp pageIndex out.png [scale]: renders one page (background and ink) to a PNG, to look at what the renderer does.
#include "pagerender.h"
#include "pdfraster.h"
#include "xopp.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <cstdio>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  if (argc < 4) { std::fprintf(stderr, "usage: xl_render_page file.xopp pageIndex out.png [scale]\n"); return 2; }
  if (pdfRasterAvailable()) setPdfImageDecoder(&rasterizePdfFormula);   // formulas are drawn from their PDF, as in the application
  const LoadResult r = loadXopp(QString::fromLocal8Bit(argv[1]));
  if (!r.ok) { std::fprintf(stderr, "load failed: %s\n", qPrintable(r.error)); return 1; }
  const int idx = QString(argv[2]).toInt();
  if (idx < 0 || size_t(idx) >= r.doc.pages.size()) { std::fprintf(stderr, "page %d of %zu\n", idx, r.doc.pages.size()); return 1; }
  const double scale = argc > 4 ? QString(argv[4]).toDouble() : 1.0;
  const Page& page = r.doc.pages[size_t(idx)];
  QImage img(int(std::ceil(page.width() * scale)), int(std::ceil(page.height() * scale)), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  RenderOptions o;
  o.scale = scale;
  QElapsedTimer t;
  t.start();
  {
    QPainter p(&img);
    p.scale(scale, scale);
    renderPage(p, page, o);
  }
  std::printf("%dx%d rendered in %lld ms\n", img.width(), img.height(), t.elapsed());
  return img.save(QString::fromLocal8Bit(argv[3])) ? 0 : 1;
}
