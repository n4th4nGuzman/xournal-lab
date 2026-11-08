// PDF rasteriser: port of app/src/renderer/pdfraster.ts on top of Qt6Pdf (pdfium) instead of pdf.js. Qt Gui alone cannot draw a PDF, so everything that
// shows one (PDF page backgrounds, formulas saved as vector PDF) goes through here. Built without Qt6Pdf (no XL_HAVE_PDF) every function reports "not
// available" and returns null images: the callers keep working with a blank page / placeholder.
// Thread rules: a PdfDocument belongs to the thread that uses it (pdfium is serialised inside Qt, but the QObjects are not shareable); the free
// functions below keep a small per-thread cache, so they can be called from any thread.
#pragma once
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QSizeF>
#include <QString>
#include <memory>

/** True when the program was built with a PDF rasteriser. */
bool pdfRasterAvailable();

struct PdfInfo {
  bool ok = false;
  QString error;              // why the bytes are not a readable PDF
  int pages = 0;
  double widthPt = 0, heightPt = 0;   // first page, in points (as pdfInfo of the TS)
};

/** One opened PDF (kept open between pages, so a 300-page book is parsed once). Copies nothing: the bytes are shared. */
class PdfDocument {
 public:
  PdfDocument();
  ~PdfDocument();
  PdfDocument(const PdfDocument&) = delete;
  PdfDocument& operator=(const PdfDocument&) = delete;

  bool open(const QByteArray& pdf, QString* error = nullptr);
  bool isOpen() const;
  int pageCount() const;
  /** Size in points of page `pageNo` (1-based, like the `pageno` attribute); empty when out of range. */
  QSizeF pageSize(int pageNo) const;
  /**
   * The page drawn `widthPx` wide (height follows the aspect ratio). `background` valid = composited over that colour, an opaque image;
   * invalid = transparent where the page paints nothing. A null image on failure.
   */
  QImage render(int pageNo, int widthPx, const QColor& background = QColor(Qt::white)) const;
  /** Same, but only the part `clipPx` of the full `widthPx`-wide image (for zooming into a big page without the whole bitmap). */
  QImage renderClip(int pageNo, int widthPx, const QRect& clipPx, const QColor& background = QColor(Qt::white)) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

PdfInfo pdfInfo(const QByteArray& pdf);
/** The brief's entry point: page `pageNo` (1-based) of `pdf`, `widthPx` wide, over `background` (invalid = transparent). Null on failure. */
QImage rasterizePdfPage(const QByteArray& pdf, int pageNo, int widthPx, const QColor& background);
/**
 * First page of a one-colour PDF (a formula) on a transparent background with its true ink colour (rasterizeAuto of the TS): drawn over white and
 * over black, the alpha is what differs and the colour is recovered from the fully covered pixels. Null when nothing is drawn.
 */
QImage rasterizePdfInk(const QByteArray& pdf, int widthPx);

/** A formula's one-page PDF as the canvas draws it: transparent, true ink colour. `widthPx` <= 0 = 4 px per point (at most 2048 wide, as the Electron app);
 *  a bigger width is what a deep zoom asks for (capped at 4096 wide and 16 Mpx, like the Electron app's TEX_SHARP_MAX). */
QImage rasterizePdfFormula(const QByteArray& pdf, int widthPx = 0);

/** Content hash used as cache key for PDF bytes (64 bit FNV-1a; not a security hash). */
quint64 pdfContentHash(const QByteArray& pdf);
