// PDF backgrounds and export: a PDF made with QPdfWriter becomes a document with one page per PDF page (attributes as Xournal++ writes them), survives
// a .xopp round trip, is drawn as background, and exports to PDF and PNG with the right page sizes, colours and page counts. Formulas saved as vector PDF
// (teximage) are drawn through the rasteriser hook.
#include "docfactory.h"
#include "genobj.h"
#include "mathlayout.h"
#include "pageexport.h"
#include "pagerender.h"
#include "pdfbackground.h"
#include "pdfraster.h"
#include "xopp.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static int fails = 0;
static void check(bool ok, const QString& what) { std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what)); if (!ok) fails++; }
static bool near(QRgb a, QRgb b, int tol = 8) { return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol && std::abs(qBlue(a) - qBlue(b)) <= tol; }

// 3 pages: A4 with a red square top-left; A5 landscape-ish 420x300 with a green one in the middle; Letter with a blue one bottom-right.
static QByteArray samplePdf() {
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  QPdfWriter w(&buf);
  w.setResolution(72);
  w.setPageMargins(QMarginsF(0, 0, 0, 0));
  w.setPageSize(QPageSize(QSizeF(595, 842), QPageSize::Point));
  QPainter p(&w);
  p.fillRect(QRectF(0, 0, 200, 200), QColor(255, 0, 0));
  w.setPageSize(QPageSize(QSizeF(420, 300), QPageSize::Point));
  w.newPage();
  p.fillRect(QRectF(160, 100, 100, 100), QColor(0, 160, 0));
  w.setPageSize(QPageSize(QSizeF(612, 792), QPageSize::Point));
  w.newPage();
  p.fillRect(QRectF(412, 592, 200, 200), QColor(0, 0, 255));
  p.end();
  return bytes;
}

static Stroke lineStroke(const QString& color, double x0, double y0, double x1, double y1, double width) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", color}};
  s.widths = {width};
  s.pts = {x0, y0, x1, y1};
  return s;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  QGuiApplication app(argc, argv);
  if (!pdfRasterAvailable()) { std::printf("built without Qt6Pdf: nothing to test\n"); return 0; }
  QTemporaryDir dir;
  const QString pdfPath = dir.filePath("fuente.pdf");
  {
    QFile f(pdfPath);
    (void)f.open(QIODevice::WriteOnly);
    f.write(samplePdf());
  }

  // ---- a document from a PDF
  PdfDocument src;
  QString err;
  check(src.open(samplePdf(), &err), "sample PDF opens");
  Document doc = documentForPdf(src, pdfPath);
  check(doc.pages.size() == 3, "one page per PDF page");
  check(std::abs(doc.pages[1].width() - 420) < 0.1 && std::abs(doc.pages[1].height() - 300) < 0.1 && std::abs(doc.pages[2].width() - 612) < 0.1, "pages keep the PDF page sizes");
  check(attrOr(doc.pages[0].background, "type") == "pdf" && attrOr(doc.pages[0].background, "pageno") == "1" && !attrOr(doc.pages[0].background, "filename").isEmpty() && attrOr(doc.pages[0].background, "domain") == "absolute",
        "first page names the file (domain=absolute, filename, pageno)");
  check(attrOr(doc.pages[1].background, "filename").isEmpty() && attrOr(doc.pages[1].background, "pageno") == "2", "later pages carry only pageno");

  // ink on top of the PDF: a thick black line across page 1 and a thin red one on page 2
  doc.pages[0].layers[0].elements.push_back(lineStroke("#000000ff", 50, 400, 500, 400, 8));
  doc.pages[1].layers[0].elements.push_back(lineStroke("#ff0000ff", 20, 20, 120, 20, 6));

  // ---- file round trip
  const QString xopp = dir.filePath("anotado.xopp");
  check(saveXopp(xopp, doc, &err), "save .xopp");
  QFile xf(xopp);
  (void)xf.open(QIODevice::ReadOnly);
  QByteArray xml;
  check(gunzip(xf.readAll(), &xml, &err), "gunzip");
  check(xml.contains("<background type=\"pdf\" domain=\"absolute\" filename=") && xml.contains("pageno=\"2\"/>") && xml.contains("pageno=\"3\"/>"), "XML has the background elements as Xournal++ writes them");
  LoadResult lr = loadXopp(xopp);
  check(lr.ok && lr.doc == doc, "reload gives the same document " + lr.error); if (!lr.ok) return 1;
  std::printf("  (reloaded %zu pages; doc %zu; equal attrs0 %d bg0 %d layers0 %d)\n", lr.doc.pages.size(), doc.pages.size(), int(lr.doc.pages[0].attrs==doc.pages[0].attrs), int(lr.doc.pages[0].background==doc.pages[0].background), int(lr.doc.pages[0].layers==doc.pages[0].layers));
  const BackgroundRef r2 = backgroundRef(lr.doc, 1);
  check(r2.kind == BackgroundRef::Pdf && r2.pageNo == 2 && r2.file == documentPdfFile(lr.doc), "page 2 resolves to page 2 of the document's PDF");

  // ---- background rendering
  BackgroundResolver res;
  res.setSearchDirs({dir.path()});
  QImage bg = res.render(backgroundRef(lr.doc, 0), 595, &err);
  check(!bg.isNull() && bg.width() == 595 && near(bg.pixel(100, 100), qRgb(255, 0, 0)), "resolver draws page 1 with its red square");
  const QSizeF sz = res.sizeOf(backgroundRef(lr.doc, 2));
  check(std::abs(sz.width() - 612) < 0.1, "resolver reports the size of page 3");

  // ---- PNG export, with and without background
  ExportOptions o;
  const QString png = dir.filePath("p.png");
  o.pages = {0};
  ExportResult pr = exportPng(lr.doc, png, 72, o, res);
  QImage img(png);
  check(pr.ok && pr.pages == 1 && img.width() == 595 && img.height() == 842, "PNG of page 1 at 72 dpi is 595x842");
  check(near(img.pixel(100, 100), qRgb(255, 0, 0)) && near(img.pixel(300, 400), qRgb(0, 0, 0)) && near(img.pixel(300, 600), qRgb(255, 255, 255)), "PNG: PDF red square, black stroke over it, white paper");
  o.background = false;
  pr = exportPng(lr.doc, png, 72, o, res);
  img = QImage(png);
  check(pr.ok && qAlpha(img.pixel(100, 100)) == 0 && near(img.pixel(300, 400), qRgb(0, 0, 0)) && qAlpha(img.pixel(300, 400)) == 255, "PNG without background: transparent paper, ink kept");
  o.background = true;
  o.pages = {};
  const QString all = dir.filePath("todas.png");
  pr = exportPng(lr.doc, all, 100, o, res);
  check(pr.ok && pr.files.size() == 3 && QFile::exists(dir.filePath("todas-001.png")) && QFile::exists(dir.filePath("todas-003.png")), "all pages: three numbered PNGs");
  const QImage p2(dir.filePath("todas-002.png"));
  check(p2.width() == int(std::ceil(420 * 100 / 72.0)) && near(p2.pixel(int(210 * 100 / 72.0), int(150 * 100 / 72.0)), qRgb(0, 160, 0)), "page 2 PNG: landscape size and its green square");
  p2.save(dir.filePath("pagina2.png"));

  // ---- PDF export
  const QString out = dir.filePath("salida.pdf");
  int calls = 0;
  ExportResult er = exportPdf(lr.doc, out, o, res, [&](int, int) { calls++; return true; });
  check(er.ok && er.pages == 3 && er.bytes > 1000 && calls == 4, QString("PDF written: %1 pages, %2 bytes, progress called %3 times").arg(er.pages).arg(er.bytes).arg(calls));
  PdfDocument back;
  check(back.open([&] { QFile f(out); (void)f.open(QIODevice::ReadOnly); return f.readAll(); }(), &err) && back.pageCount() == 3, "exported PDF re-opens with 3 pages (" + err + ")");
  check(std::abs(back.pageSize(1).width() - 595) < 1 && std::abs(back.pageSize(1).height() - 842) < 1 &&
        std::abs(back.pageSize(2).width() - 420) < 1 && std::abs(back.pageSize(2).height() - 300) < 1 &&
        std::abs(back.pageSize(3).width() - 612) < 1 && std::abs(back.pageSize(3).height() - 792) < 1,
        QString("page sizes preserved: %1x%2, %3x%4, %5x%6").arg(back.pageSize(1).width()).arg(back.pageSize(1).height()).arg(back.pageSize(2).width()).arg(back.pageSize(2).height()).arg(back.pageSize(3).width()).arg(back.pageSize(3).height()));
  QImage e1 = back.render(1, 595, QColor(Qt::white));
  check(near(e1.pixel(100, 100), qRgb(255, 0, 0), 12) && near(e1.pixel(300, 400), qRgb(0, 0, 0), 12), "exported page 1: PDF background and ink");
  e1.save(dir.filePath("exportada1.png"));
  QImage e3 = back.render(3, 612, QColor(Qt::white));
  check(near(e3.pixel(500, 700), qRgb(0, 0, 255), 12), "exported page 3: blue square from the background");

  // without background: vector ink only
  ExportOptions nb = o;
  nb.background = false;
  er = exportPdf(lr.doc, dir.filePath("sinfondo.pdf"), nb, res);
  PdfDocument back2;
  back2.open([&] { QFile f(dir.filePath("sinfondo.pdf")); (void)f.open(QIODevice::ReadOnly); return f.readAll(); }());
  QImage n1 = back2.render(1, 595, QColor(Qt::white));
  check(er.ok && near(n1.pixel(100, 100), qRgb(255, 255, 255)) && near(n1.pixel(300, 400), qRgb(0, 0, 0), 12), "PDF without background: no red square, ink still there");

  // a page range, and cancelling
  ExportOptions part = o;
  part.pages = parsePageRange("3,1", 3, &err);
  er = exportPdf(lr.doc, dir.filePath("rango.pdf"), part, res);
  PdfDocument back3;
  back3.open([&] { QFile f(dir.filePath("rango.pdf")); (void)f.open(QIODevice::ReadOnly); return f.readAll(); }());
  check(er.ok && back3.pageCount() == 2 && std::abs(back3.pageSize(1).width() - 612) < 1, "page range 3,1 writes two pages in that order");
  er = exportPdf(lr.doc, dir.filePath("cancelado.pdf"), o, res, [](int done, int) { return done < 1; });
  check(!er.ok && er.cancelled && !QFile::exists(dir.filePath("cancelado.pdf")), "cancelling leaves no file");
  QString perr;
  check(parsePageRange("0", 3, &perr).empty() && !perr.isEmpty() && parsePageRange("2-9", 3).empty() && parsePageRange("a", 3).empty() && parsePageRange("3-1", 3).empty() && parsePageRange("1,3-3", 3).size() == 2, "page range parsing rejects bad input");

  // ---- the PDF is gone: pages stay plain, with a warning, and a copy next to the .xopp is found
  const QString moved = dir.filePath("movido");
  QDir().mkpath(moved);
  QFile::rename(pdfPath, moved + "/fuente.pdf");
  BackgroundResolver lost;
  ExportOptions one;
  one.pages = {0};
  er = exportPng(lr.doc, dir.filePath("perdido.png"), 72, one, lost);
  check(er.ok && !er.warnings.isEmpty() && near(QImage(dir.filePath("perdido.png")).pixel(100, 100), qRgb(255, 255, 255)), "missing PDF: plain paper and a warning (" + er.warnings.join("; ") + ")");
  lost.setSearchDirs({moved});
  er = exportPng(lr.doc, dir.filePath("hallado.png"), 72, one, lost);
  check(er.ok && er.warnings.isEmpty() && near(QImage(dir.filePath("hallado.png")).pixel(100, 100), qRgb(255, 0, 0)), "the PDF is found by name in a search directory");

  // ---- the first PDF page is deleted: the reference moves to the new first one
  Document cut = lr.doc;
  cut.pages.erase(cut.pages.begin());
  check(documentPdfFile(cut).isEmpty(), "after deleting page 1 nobody names the file");
  check(normalizePdfReferences(cut, documentPdfFile(lr.doc)) && !documentPdfFile(cut).isEmpty() && attrOr(cut.pages[0].background, "pageno") == "2" && attrOr(cut.pages[0].background, "domain") == "absolute", "normalising writes the name on the new first PDF page");
  check(!normalizePdfReferences(cut, documentPdfFile(lr.doc)), "normalising twice changes nothing");
  Document moved2 = lr.doc;
  std::swap(moved2.pages[0], moved2.pages[1]);   // page 2 now first
  check(normalizePdfReferences(moved2) && !attrOr(moved2.pages[0].background, "filename").isEmpty() && attrOr(moved2.pages[1].background, "filename").isEmpty(), "after moving, only the first PDF page names the file");

  // ---- formulas saved as vector PDF are drawn through the hook
  const TexImageResult tex = makeTexImage("x^2+\\frac{1}{2}", 20, true, QColor(200, 30, 60), 50, 50);
  check(tex.ok && tex.el.tag == "teximage", "makeTexImage produced a formula");
  Page pg;
  pg.attrs = {{"width", "300"}, {"height", "200"}};
  pg.background = {{"type", "solid"}, {"color", "#ffffffff"}, {"style", "plain"}};
  pg.layers.push_back(Layer{});
  pg.layers[0].elements.push_back(tex.el);
  const double l = attrOr(tex.el.attrs, "left").toDouble(), t = attrOr(tex.el.attrs, "top").toDouble(), rr = attrOr(tex.el.attrs, "right").toDouble(), bb = attrOr(tex.el.attrs, "bottom").toDouble();
  const auto drawIt = [&] {
    QImage im(300, 200, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::white);
    QPainter p(&im);
    RenderOptions ro;
    renderPage(p, pg, ro);
    p.end();
    return im;
  };
  const auto inkPixels = [&](const QImage& im) {
    int n = 0;
    for (int y = int(t); y < int(bb); y++)
      for (int x = int(l); x < int(rr); x++) if (im.pixel(x, y) != qRgb(255, 255, 255) && near(im.pixel(x, y), qRgb(200, 30, 60), 40)) n++;
    return n;
  };
  clearRenderCaches();
  const int before = inkPixels(drawIt());
  setPdfImageDecoder(&rasterizePdfFormula);
  const QImage withHook = drawIt();
  const int after = inkPixels(withHook);
  check(after > 100 && after > before, QString("formula PDF: %1 ink pixels without the decoder (Qt image plugin or placeholder), %2 with it, colour kept").arg(before).arg(after));
  withHook.save(dir.filePath("formula.png"));

  // keep the evidence next to the build for the report
  const QString keep = QDir::temp().filePath("xl_pageexport_out");
  QDir().mkpath(keep);
  for (const char* n : {"pagina2.png", "exportada1.png", "formula.png"}) { QFile::remove(keep + "/" + n); QFile::copy(dir.filePath(n), keep + "/" + n); }
  std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
