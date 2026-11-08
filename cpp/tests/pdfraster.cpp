// Rasterising a PDF: pages of known colours made with QPdfWriter must come back with those colours at those places, at any width, clipped or not;
// an ink-only page (a formula) must come back transparent with its true colour; bad bytes fail with a message.
#include "pdfraster.h"
#include <QBuffer>
#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <cstdio>
#include <cstdlib>

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; }
static bool near(QRgb a, QRgb b, int tol = 6) { return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol && std::abs(qBlue(a) - qBlue(b)) <= tol; }

// Two pages: A4 (595x842 pt) with a red square top-left and a blue one bottom-right; then a landscape 400x300 page with a green rectangle in the middle.
static QByteArray samplePdf() {
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  QPdfWriter w(&buf);
  w.setResolution(72);                      // 1 unit = 1 point
  w.setPageSize(QPageSize(QSizeF(595, 842), QPageSize::Point));
  w.setPageMargins(QMarginsF(0, 0, 0, 0));
  QPainter p(&w);
  p.fillRect(QRectF(0, 0, 200, 200), QColor(255, 0, 0));
  p.fillRect(QRectF(395, 642, 200, 200), QColor(0, 0, 255));
  w.setPageSize(QPageSize(QSizeF(400, 300), QPageSize::Point));
  w.newPage();
  p.fillRect(QRectF(100, 100, 200, 100), QColor(0, 160, 0));
  p.end();
  return bytes;
}

static QByteArray inkPdf(const QColor& c) {   // like a formula: ink only, no paper
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  QPdfWriter w(&buf);
  w.setResolution(72);
  w.setPageSize(QPageSize(QSizeF(100, 40), QPageSize::Point));
  w.setPageMargins(QMarginsF(0, 0, 0, 0));
  QPainter p(&w);
  p.fillRect(QRectF(10, 10, 40, 20), c);
  p.end();
  return bytes;
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (!pdfRasterAvailable()) { std::printf("built without Qt6Pdf: nothing to test\n"); return 0; }
  const QByteArray pdf = samplePdf();
  const PdfInfo info = pdfInfo(pdf);
  check(info.ok && info.pages == 2, "page count is 2");
  check(std::abs(info.widthPt - 595) < 1 && std::abs(info.heightPt - 842) < 1, "first page size 595x842 pt");

  QImage a = rasterizePdfPage(pdf, 1, 595, QColor(Qt::white));
  check(!a.isNull() && a.width() == 595 && a.height() == 842, "page 1 at 595 px wide is 595x842");
  check(near(a.pixel(100, 100), qRgb(255, 0, 0)), "red square at (100,100)");
  check(near(a.pixel(500, 740), qRgb(0, 0, 255)), "blue square at (500,740)");
  check(near(a.pixel(300, 400), qRgb(255, 255, 255)), "white elsewhere");
  QImage b = rasterizePdfPage(pdf, 1, 1190, QColor(Qt::white));
  check(b.width() == 1190 && near(b.pixel(200, 200), qRgb(255, 0, 0)) && near(b.pixel(1000, 1480), qRgb(0, 0, 255)), "same page at twice the width: colours at twice the coordinates");
  QImage c2 = rasterizePdfPage(pdf, 2, 400, QColor(Qt::white));
  check(c2.width() == 400 && c2.height() == 300 && near(c2.pixel(200, 150), qRgb(0, 160, 0)) && near(c2.pixel(50, 50), qRgb(255, 255, 255)), "page 2 is landscape with the green rectangle in the middle");
  QImage cream = rasterizePdfPage(pdf, 1, 300, QColor("#fff7d6"));
  std::printf("  (paper over cream: %08x)\n", cream.pixel(150, 200));
  QImage tr = rasterizePdfPage(pdf, 1, 300, QColor());
  std::printf("  (transparent request: alpha at paper = %d)\n", qAlpha(tr.pixel(150, 200)));
  check(rasterizePdfPage(pdf, 3, 300, QColor(Qt::white)).isNull() && rasterizePdfPage(pdf, 0, 300, QColor(Qt::white)).isNull(), "pages out of range give a null image");

  PdfDocument doc;
  check(doc.open(pdf), "PdfDocument opens");
  QImage clip = doc.renderClip(1, 1190, QRect(1000, 1400, 190, 84), QColor(Qt::white));
  std::printf("  (clip %dx%d px=%08x)\n", clip.width(), clip.height(), unsigned(clip.pixel(50, 40)));
  check(clip.size() == QSize(190, 84) && near(clip.pixel(50, 40), qRgb(0, 0, 255)), "clipped render is only that part and has the same colours");

  QString err;
  PdfDocument bad;
  check(!bad.open(QByteArray("not a pdf at all"), &err) && !err.isEmpty(), "garbage is refused with a message");
  check(!pdfInfo(QByteArray()).ok, "empty bytes are refused");

  const QImage ink = rasterizePdfInk(inkPdf(QColor(200, 30, 60)), 400);
  check(!ink.isNull() && ink.width() == 400, "ink page rasterised");
  if (!ink.isNull()) {
    const QRgb inside = ink.pixelColor(100, 60).rgba();   // 400 px / 100 pt = 4 px per pt: the box is (40..200, 40..120)
    check(qAlpha(inside) == 255 && near(inside, qRgb(200, 30, 60), 3), "ink keeps its true colour at full alpha");
    check(qAlpha(ink.pixel(300, 100)) == 0, "no ink is fully transparent");
  }
  const QImage light = rasterizePdfInk(inkPdf(QColor(250, 250, 120)), 400);
  check(!light.isNull() && near(light.pixelColor(100, 60).rgba(), qRgb(250, 250, 120), 3) && qAlpha(light.pixel(100, 60)) == 255, "light ink (yellow) is recovered too");
  const QImage white = rasterizePdfInk(inkPdf(QColor(255, 255, 255)), 200);
  check(!white.isNull() && qAlpha(white.pixel(50, 30)) == 255 && near(white.pixelColor(50, 30).rgba(), qRgb(255, 255, 255), 3), "white ink is recovered too");

  std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
