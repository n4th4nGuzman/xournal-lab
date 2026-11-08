// Export of the document to PDF and PNG, and the page drawing the printer shares. The Electron app writes one JPEG per page into a PDF; here the PDF is
// vector (QPdfWriter): strokes, glow, texts and formulas stay sharp and text stays selectable. PDF and picture backgrounds are the exception: they are
// raster images of the original page at `bgDpi` (the PDF writer cannot embed a page of another PDF).
// No widgets: progress and cancelling come through a callback, so the application drives a QProgressDialog and tests drive nothing.
#pragma once
#include "model.h"
#include "pdfbackground.h"
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <functional>
#include <vector>

struct ExportOptions {
  bool background = true;          // paper, special papers and PDF/picture backgrounds; false = ink only (transparent in a PNG)
  double bgDpi = 150;              // resolution of a PDF/picture background inside the exported PDF (PNG: the PNG's own dpi)
  std::vector<int> pages;          // 0-based, in the order to write; empty = every page
  std::function<std::vector<int>(int page)> hiddenLayers;   // layers not to draw (view state of the editor); null = none
};

/** Called after each page (done of total) and before the first (0 of total). Return false to cancel; the partly written file is removed. */
using ExportProgress = std::function<bool(int done, int total)>;

struct ExportResult {
  bool ok = false;
  bool cancelled = false;
  QString error;
  int pages = 0;
  qint64 bytes = 0;
  QStringList files;               // what was written
  QStringList warnings;            // e.g. a PDF that could not be found: those pages were written with plain paper
};

/**
 * Writes a PDF with one page per selected page, each the size of the page (points). `resolver` finds the PDF/picture backgrounds
 * (its search dirs should include where the document was opened from).
 */
ExportResult exportPdf(const Document& doc, const QString& path, const ExportOptions& o, BackgroundResolver& resolver, const ExportProgress& progress = {});
/**
 * One PNG per selected page at `dpi` (72 = one pixel per point). A single page is written to `path`; several to `<name>-001.png`, `<name>-002.png`...
 * (numbered by their page number in the document).
 */
ExportResult exportPng(const Document& doc, const QString& path, double dpi, const ExportOptions& o, BackgroundResolver& resolver, const ExportProgress& progress = {});

/** One page as an image, `dpi` dots per inch (a page of 595 pt at 72 dpi is 595 px). Null when the size would not fit in memory (`error`). */
QImage renderPageImage(const Document& doc, int page, double dpi, const ExportOptions& o, BackgroundResolver& resolver, QStringList* warnings = nullptr, QString* error = nullptr);

/**
 * Draws page `page` into `target` (device units of `p`) scaled to fit and centred, for a printer or any painter; `bgWidthPx` is the width of the raster
 * background to ask for (a printer's resolution).
 */
void paintPage(QPainter& p, const Document& doc, int page, const QRectF& target, int bgWidthPx, const ExportOptions& o, BackgroundResolver& resolver, QStringList* warnings = nullptr);

/** "1,3-5" -> {0,2,3,4} (pages are 1-based in the text; empty result and `error` when the text is not a valid list for `pageCount` pages). */
std::vector<int> parsePageRange(const QString& text, int pageCount, QString* error = nullptr);
