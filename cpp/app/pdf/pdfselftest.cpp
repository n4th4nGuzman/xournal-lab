// Checks of the PDF area for --selftest: the real code paths (open a PDF, the view's tiles with the async background, zoom, export PDF/PNG, print to a PDF
// file, formulas) driven headless. What it cannot show: a physical printer, another program opening the exports (Xournal++ itself).
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "mainwindow.h"
#include "mathlayout.h"
#include "pdfactions.h"
#include "pdfbg.h"
#include "pdfraster.h"
#include "xopp.h"
#include <QFileInfo>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QThread>
#include <windows.h>
#include <psapi.h>
#undef near   // windef.h defines it away; this file has a function of that name
#ifdef XL_HAVE_PRINT
#include <QPrinter>
#include <QPrinterInfo>
#endif

namespace {

/** Working set now and the peak so far, for the stages of the self test that make big images (which stage holds the memory). */
QString memNow() {
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof(pmc);
  K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
  return QString("        [memory: %1 MB now, %2 MB peak so far]").arg(double(pmc.WorkingSetSize) / 1048576.0, 0, 'f', 0).arg(double(pmc.PeakWorkingSetSize) / 1048576.0, 0, 'f', 0);
}

QByteArray samplePdf() {
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  QPdfWriter w(&buf);
  w.setResolution(72);
  w.setPageMargins(QMarginsF(0, 0, 0, 0));
  w.setPageSize(QPageSize(QSizeF(595, 842), QPageSize::Point));
  QPainter p(&w);
  p.fillRect(QRectF(0, 0, 595, 100), QColor(30, 60, 160));
  p.fillRect(QRectF(60, 160, 200, 200), QColor(255, 0, 0));
  p.setPen(Qt::black);
  p.setFont(QFont("Arial", 20));
  p.drawText(QPointF(300, 250), "Texto del PDF original");
  w.setPageSize(QPageSize(QSizeF(420, 300), QPageSize::Point));
  w.newPage();
  p.fillRect(QRectF(160, 100, 100, 100), QColor(0, 160, 0));
  w.setPageSize(QPageSize(QSizeF(612, 792), QPageSize::Point));
  w.newPage();
  p.fillRect(QRectF(412, 592, 200, 200), QColor(0, 0, 255));
  p.end();
  return bytes;
}

bool near(QRgb a, QRgb b, int tol = 14) { return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol && std::abs(qBlue(a) - qBlue(b)) <= tol; }

}  // namespace

int pdfSelfTest(MainWindow& win, const std::function<void(const QString&)>& out) {
  int fail = 0;
  const auto check = [&](bool ok, const QString& what) { out(QString(ok ? "  PASS  " : "  FAIL  ") + what); if (!ok) fail++; };
  out("\n== PDF area (backgrounds, formulas, export, print) ==");
  Editor& ed = win.editor();
  DocumentView& v = *win.view();
  PdfBackgrounds* bgs = pdfui::backgrounds(win);
  check(bgs != nullptr, "PDF backgrounds installed on the window");
  if (!bgs) return fail;
  check(pdfRasterAvailable(), "built with the Qt6Pdf rasteriser");
  for (const char* id : {"file.openPdf", "file.exportPdf", "file.exportAs", "file.print"}) check(ed.action(id) && ed.action(id)->isEnabled(), QString("command %1 is implemented and enabled").arg(id));
  const Document original = ed.doc();
  const QString shots = QDir::temp().filePath("xl_pdf_shots");
  QDir().mkpath(shots);

  QTemporaryDir dir;
  const QString pdfPath = dir.filePath("apuntes.pdf");
  { QFile f(pdfPath); if (f.open(QIODevice::WriteOnly)) f.write(samplePdf()); }

  const auto settle = [&] {
    QElapsedTimer t;
    t.start();
    int quiet = 0;
    while (t.elapsed() < 20000 && quiet < 4) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(8);
      quiet = (bgs->idle() && !v.hasPendingTiles()) ? quiet + 1 : 0;
    }
    v.renderVisibleNow();
    QApplication::processEvents();
  };
  const auto pixelOfPage = [&](const QImage& snap, int page, double px, double py) {
    const QRectF r = v.pageRectOnScreen(page);
    const double d = double(snap.width()) / std::max(1, v.viewport()->width());
    return snap.pixel(int((r.left() + px * v.zoom()) * d), int((r.top() + py * v.zoom()) * d));
  };

  // ---- annotate a PDF
  QString err;
  check(pdfui::openPdfFile(win, pdfPath, &err), "Anotar PDF: the document is replaced by the PDF's pages " + err);
  check(win.windowTitle().startsWith(QFileInfo(pdfPath).fileName()) && !win.windowTitle().startsWith("Sin título"), "Anotar PDF: the window title names the PDF (" + win.windowTitle() + ")");
  check(ed.pageCount() == 3 && std::abs(ed.doc().pages[1].width() - 420) < 0.1, "one page per PDF page, with the PDF's sizes");
  check(attrOr(ed.doc().pages[0].background, "type") == "pdf" && attrOr(ed.doc().pages[2].background, "pageno") == "3", "pages are PDF-background pages (type=pdf, pageno)");
  win.resize(1000, 900);
  win.show();
  v.zoomFitWidth();
  v.scrollToPage(0);
  QElapsedTimer t;
  t.start();
  while (bgs->stats().rendered < 1 && t.elapsed() < 20000) { QApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); }
  const double firstMs = double(t.elapsed());
  settle();
  QImage snap = v.snapshot();
  check(near(pixelOfPage(snap, 0, 100, 250), qRgb(255, 0, 0)), "the PDF page is drawn as the tile background (red square at its place)");
  check(near(pixelOfPage(snap, 0, 300, 50), qRgb(30, 60, 160)), "and its blue band");
  out(QString("        first picture of the PDF page ready after %1 ms; async worker rendered %2 image(s), last %3 px wide").arg(firstMs, 0, 'f', 0).arg(bgs->stats().rendered).arg(bgs->stats().lastWidth));
  snap.save(shots + "/pdf-fondo.png");

  // ink over the PDF, then the picture must still be underneath
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
  s.widths = {6};
  s.pts = {40, 300, 540, 320};
  ed.commitAdd(0, 0, Element(s));
  settle();
  snap = v.snapshot();
  check(near(pixelOfPage(snap, 0, 290, 310), qRgb(0, 0, 0), 40) && near(pixelOfPage(snap, 0, 100, 250), qRgb(255, 0, 0)), "ink drawn over the PDF background; the background stays");

  // a formula saved as vector PDF is drawn (the Electron app stores formulas this way)
  const TexImageResult tex = makeTexImage("\\int_0^1 x^2\\,dx=\\frac{1}{3}", 24, true, QColor(200, 30, 60), 60, 380);
  check(tex.ok, "formula laid out");
  ed.commitAdd(0, 0, Element(tex.el), "Fórmula");
  settle();
  snap = v.snapshot();
  int ink = 0;
  for (int y = 380; y < 430; y += 1)
    for (int x = 60; x < 260; x += 1)
      if (near(pixelOfPage(snap, 0, x, y), qRgb(200, 30, 60), 45)) ink++;
  check(ink > 80, QString("formula saved as vector PDF is drawn on the canvas in its own colour (%1 matching pixels)").arg(ink));
  snap.save(shots + "/pdf-tinta-formula.png");

  // zoom: a sharper picture is rasterised for the new scale (asynchronously) and the view never waits for it
  const int widthBefore = bgs->stats().lastWidth;
  v.setZoom(2.4);
  v.scrollToPage(0);
  QElapsedTimer tz;
  tz.start();
  QApplication::processEvents();
  const double zoomEventMs = double(tz.elapsed());
  settle();
  snap = v.snapshot();
  check(bgs->stats().lastWidth > widthBefore, QString("zoom 240%%: a larger raster of the page was made (%1 px -> %2 px)").arg(widthBefore).arg(bgs->stats().lastWidth));
  check(near(pixelOfPage(snap, 0, 100, 250), qRgb(255, 0, 0)), "after the zoom the red square is still at its place");
  out(QString("        zoom step handled in %1 ms on the GUI thread (raster is on the worker)").arg(zoomEventMs, 0, 'f', 1));
  snap.save(shots + "/pdf-zoom.png");
  out(memNow());

  // provider cost on the render path: a cached image is a hash lookup, a missing one only queues
  {
    const Attrs bg = ed.doc().pages[1].background;
    v.zoomFitWidth();
    settle();
    const double sc = v.zoom() * v.devicePixelRatioF();
    (void)bgs->provide(bg, 420, 300, sc);   // page 2 is not on screen: ask for it once and wait for the worker
    settle();
    QElapsedTimer pt;
    pt.start();
    QImage got;
    for (int i = 0; i < 2000; i++) got = bgs->provide(bg, 420, 300, sc);
    const double perCall = double(pt.nsecsElapsed()) / 2000.0 / 1000.0;
    check(!got.isNull() && perCall < 100, QString("cached background lookup costs %1 us per call").arg(perCall, 0, 'f', 2));
    pt.restart();
    (void)bgs->provide(ed.doc().pages[2].background, 612, 792, 3.7);   // a size nobody asked for: queued, not drawn
    check(pt.nsecsElapsed() / 1000.0 < 2000, QString("an uncached background request returns in %1 us (queued)").arg(double(pt.nsecsElapsed()) / 1000.0, 0, 'f', 1));
    settle();
  }

  // ---- export, same document
  const ExportOptions all = pdfui::exportOptions(win, true, 150, {});
  const QString outPdf = dir.filePath("exportado.pdf");
  ExportResult r = pdfui::exportPdfTo(win, outPdf, all, false);
  check(r.ok && r.pages == 3 && r.warnings.isEmpty(), QString("Export PDF: %1 pages, %2 KB").arg(r.pages).arg(r.bytes / 1024));
  out(memNow());
  PdfDocument back;
  { QFile f(outPdf); if (f.open(QIODevice::ReadOnly)) back.open(f.readAll()); }
  check(back.pageCount() == 3 && std::abs(back.pageSize(2).width() - 420) < 1 && std::abs(back.pageSize(3).height() - 792) < 1, "exported PDF reopens: 3 pages with the three original sizes");
  const QImage e1 = back.render(1, 595, Qt::white);
  check(near(e1.pixel(100, 250), qRgb(255, 0, 0)) && near(e1.pixel(290, 310), qRgb(0, 0, 0), 40), "exported page 1 holds the PDF background and the annotation");
  e1.save(shots + "/pdf-exportado-p1.png");
  r = pdfui::exportPdfTo(win, dir.filePath("sin-fondo.pdf"), pdfui::exportOptions(win, false, 150, {0}), false);
  PdfDocument nb;
  { QFile f(dir.filePath("sin-fondo.pdf")); if (f.open(QIODevice::ReadOnly)) nb.open(f.readAll()); }
  check(r.ok && nb.pageCount() == 1 && near(nb.render(1, 595, Qt::white).pixel(100, 250), qRgb(255, 255, 255)), "export without background: one page, no red square");
  r = pdfui::exportPngTo(win, dir.filePath("img.png"), 100, all, false);
  check(r.ok && r.files.size() == 3 && QImage(dir.filePath("img-001.png")).width() == int(std::ceil(595 * 100 / 72.0)), "export PNG of all pages: three numbered files at 100 dpi");
  QImage(dir.filePath("img-001.png")).save(shots + "/pdf-export-png-p1.png");
  r = pdfui::exportPngTo(win, dir.filePath("una.png"), 72, pdfui::exportOptions(win, true, 150, {1}), false);
  check(r.ok && r.files.size() == 1 && QImage(dir.filePath("una.png")).width() == 420, "export PNG of one page: a single file named as asked");

#ifdef XL_HAVE_PRINT
  out(QString("        printers known to Qt here: %1 (default \"%2\")").arg(QPrinterInfo::availablePrinterNames().size()).arg(QPrinterInfo::defaultPrinterName()));
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(dir.filePath("impreso.pdf"));
  r = pdfui::printTo(win, printer, all, false);
  out(memNow());
  PdfDocument pr;
  { QFile f(dir.filePath("impreso.pdf")); if (f.open(QIODevice::ReadOnly)) pr.open(f.readAll()); }
  check(r.ok && pr.pageCount() == 3, QString("print path (QPrinter writing a PDF, no physical printer): %1 pages").arg(pr.pageCount()));
  check(r.ok && pr.pageCount() == 3 && qAlpha(pr.render(1, 300, Qt::white).pixel(150, 200)) == 255, "printed pages have content");
  pr.render(1, 600, Qt::white).save(shots + "/pdf-impreso-p1.png");
  out(memNow());
#else
  out("  (built without Qt PrintSupport: print not checked)");
#endif

  // ---- save, reopen: the file names the PDF; a missing PDF degrades to plain paper with a message
  const QString xopp = dir.filePath("anotado.xopp");
  QString serr;
  check(saveXopp(xopp, ed.doc(), &serr), "the annotated document saves as .xopp");
  check(win.openFile(xopp), "and opens again");
  settle();
  snap = v.snapshot();
  v.zoomFitWidth();
  v.scrollToPage(0);
  settle();
  snap = v.snapshot();
  check(near(pixelOfPage(snap, 0, 100, 250), qRgb(255, 0, 0)), "the reopened .xopp shows its PDF page again (found through the recorded path)");

  // deleting the first page must not lose the link to the PDF
  ed.deletePage(0);
  check(!documentPdfFile(ed.doc()).isEmpty() && attrOr(ed.doc().pages[0].background, "filename") == documentPdfFile(ed.doc()), "after deleting the first page the next PDF page carries the file name");
  ed.undo();

  QFile::remove(pdfPath);
  QString problem;
  QObject::connect(bgs, &PdfBackgrounds::problem, &win, [&problem](const QString& m) { problem = m; });
  ed.setDocument(loadXopp(xopp).doc, QString());
  v.zoomFitWidth();
  v.scrollToPage(0);
  settle();
  snap = v.snapshot();
  check(near(pixelOfPage(snap, 0, 100, 250), qRgb(255, 255, 255)) && !problem.isEmpty(), "PDF missing: plain paper and a message (" + problem + ")");
  snap.save(shots + "/pdf-faltante.png");

  check(pdfui::saveExportDialogShot(win, false, shots + "/dialogo-exportar-pdf.png") && pdfui::saveExportDialogShot(win, true, shots + "/dialogo-exportar-png.png"), "export dialogs build and render");
  ed.setDocument(original, QString());
  out("        screenshots in " + shots);
  return fail;
}
