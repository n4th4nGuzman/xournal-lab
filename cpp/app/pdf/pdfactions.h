// What the PDF area does for the user, as plain functions the commands, the self test and other areas call: annotate a PDF, export to PDF/PNG, print.
// The dialogs (range, background, resolution; save-as; progress) are in the .cpp; the functions here take the answers, so they run headless.
#pragma once
#include "pageexport.h"
#include <QString>
#include <functional>

class MainWindow;
class PdfBackgrounds;
class QWidget;
#ifdef XL_HAVE_PRINT
class QPrinter;
#endif

namespace pdfui {

/** The PDF backgrounds of this window (created by installPdf); null before that. */
PdfBackgrounds* backgrounds(MainWindow& win);

/** Replaces the document by one page per page of the PDF (the caller asked to discard changes). False with `error` when the PDF cannot be read. */
bool openPdfFile(MainWindow& win, const QString& path, QString* error = nullptr);

/** Asks for the PDF `file` (a background reference that is nowhere) and makes the document find it. */
void commandLocatePdf(MainWindow& win, const QString& file);

/** Directories where PDFs that moved are looked for (same as the on-screen backgrounds use). */
QStringList searchDirs(MainWindow& win);

/** Options with the editor's hidden layers and the page choice already filled in. `pages` empty = all. */
ExportOptions exportOptions(MainWindow& win, bool background, double bgDpi, const std::vector<int>& pages);

/** Runs the export. With `withProgress` a cancellable progress dialog is shown and the event loop runs between pages. */
ExportResult exportPdfTo(MainWindow& win, const QString& path, const ExportOptions& o, bool withProgress);
ExportResult exportPngTo(MainWindow& win, const QString& path, double dpi, const ExportOptions& o, bool withProgress);

#ifdef XL_HAVE_PRINT
/** Prints the selected pages (0-based; empty = all) on the printer (any QPrinter, also one that writes a PDF). Pages are scaled to fit and centred. */
ExportResult printTo(MainWindow& win, QPrinter& printer, const ExportOptions& o, bool withProgress);
#endif

/** Screenshot of the export dialog (PDF or PNG flavour) for the self test. */
bool saveExportDialogShot(MainWindow& win, bool png, const QString& file);

// the commands (they ask with dialogs)
void commandOpenPdf(MainWindow& win);
void commandExportPdf(MainWindow& win);
void commandExportAs(MainWindow& win);
void commandPrint(MainWindow& win);

}  // namespace pdfui

/** Checks of the PDF area for `--selftest`: drives the real commands' functions and the view. Returns the number of failed checks; `out` prints a line. */
int pdfSelfTest(MainWindow& win, const std::function<void(const QString&)>& out);
