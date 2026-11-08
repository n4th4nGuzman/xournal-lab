// xl_export_pdf IN.xopp OUT.pdf [--png OUT.png --page N --dpi D]  : exports a document the way File > Export as PDF does (no window), and optionally draws
// a page of any PDF (the export just written, or one the Electron app made: --from FILE.pdf) to a PNG with pdfium, to compare exports by eye.
#include "pageexport.h"
#include "pagerender.h"
#include "pdfraster.h"
#include "xopp.h"
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QStringList pos;
  QString png, from;
  int page = 1;
  double dpi = 100;
  for (int i = 1; i < argc; i++) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == "--png" && i + 1 < argc) png = QString::fromLocal8Bit(argv[++i]);
    else if (a == "--from" && i + 1 < argc) from = QString::fromLocal8Bit(argv[++i]);
    else if (a == "--page" && i + 1 < argc) page = QString::fromLocal8Bit(argv[++i]).toInt();
    else if (a == "--dpi" && i + 1 < argc) dpi = QString::fromLocal8Bit(argv[++i]).toDouble();
    else pos << a;
  }
  QString pdfPath;
  if (!from.isEmpty()) pdfPath = from;
  else {
    if (pos.size() < 2) { std::fprintf(stderr, "usage: xl_export_pdf IN.xopp OUT.pdf [--png OUT.png --page N --dpi D] | --from FILE.pdf --png OUT.png\n"); return 2; }
    if (pdfRasterAvailable()) setPdfImageDecoder(&rasterizePdfFormula);
    const LoadResult r = loadXopp(pos[0]);
    if (!r.ok) { std::fprintf(stderr, "load failed: %s\n", qPrintable(r.error)); return 1; }
    BackgroundResolver res;
    const ExportResult e = exportPdf(r.doc, pos[1], ExportOptions(), res);
    if (!e.ok) { std::fprintf(stderr, "export failed: %s\n", qPrintable(e.error)); return 1; }
    std::printf("%d pages, %lld bytes -> %s\n", e.pages, e.bytes, qPrintable(pos[1]));
    pdfPath = pos[1];
  }
  if (!png.isEmpty()) {
    QFile f(pdfPath);
    if (!f.open(QIODevice::ReadOnly)) return 1;
    const QByteArray bytes = f.readAll();
    const PdfInfo info = pdfInfo(bytes);
    if (!info.ok) { std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(pdfPath), qPrintable(info.error)); return 1; }
    const QImage img = rasterizePdfPage(bytes, page, int(info.widthPt * dpi / 72.0), Qt::white);
    std::printf("%d pages, first %gx%g pt -> %s\n", info.pages, info.widthPt, info.heightPt, qPrintable(png));
    return img.save(png) ? 0 : 1;
  }
  return 0;
}
