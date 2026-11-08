// Page backgrounds that live in another file: a page of a PDF (`<background type="pdf" [domain="absolute" filename="X.pdf"] pageno="N"/>`) or a picture
// (`type="pixmap"`), exactly as Xournal++ writes them. The .xopp does not contain the PDF: it names it (Xournal++ writes domain+filename on the first PDF page
// only; the later pages carry just `pageno`), so the file is looked up on disk when the document is shown, and a document whose PDF moved still opens.
// Pure logic and decoding (Qt Core + Gui + the rasteriser); no widgets and no threads: a BackgroundResolver belongs to the thread that uses it.
#pragma once
#include "model.h"
#include "pdfraster.h"
#include <QHash>
#include <QStringList>
#include <memory>

struct BackgroundRef {
  enum Kind { None, Pdf, Image };
  Kind kind = None;
  QString file;        // as written in the document (absolute path, or a name found next to the .xopp)
  int pageNo = 1;      // Pdf: 1-based page of that file
  QString problem;     // set when the reference cannot be followed (the page then shows plain paper)
  bool operator==(const BackgroundRef&) const = default;
};

/** What page `page` of the document wants as background. A PDF page without its own `filename` uses the document's PDF (the first PDF page that names one). */
BackgroundRef backgroundRef(const Document& doc, int page);
/** The same from the page's <background> attributes alone, for code that has no Document (a tile worker): `documentPdf` is what pages without a filename use. A `clone` picture is not followed. */
BackgroundRef backgroundRefOf(const Attrs& background, const QString& documentPdf);
/** The file the document's PDF pages belong to ("" when no page names one). */
QString documentPdfFile(const Document& doc);

/**
 * Keeps the invariant Xournal++ writes and reads: the FIRST PDF page (in page order) names the file (domain + filename), the later ones carry only `pageno`.
 * Deleting or moving that page would otherwise lose the reference. `knownFile` (what the application remembers) is used when no page names it any more.
 * Returns true when something was changed.
 */
bool normalizePdfReferences(Document& doc, const QString& knownFile = QString());

/** A new document with one page per PDF page, each the size of the PDF page (points), background as Xournal++ writes it, one empty layer. */
Document documentForPdf(const PdfDocument& pdf, const QString& absolutePath);

class BackgroundResolver {
 public:
  /** Directories where a file that is not at its recorded path is searched by name (the folder of the .xopp, the one it was opened from...). */
  void setSearchDirs(const QStringList& dirs) { dirs_ = dirs; }
  const QStringList& searchDirs() const { return dirs_; }
  /** The path of the file on this machine, or "" when it is nowhere. */
  QString locate(const QString& file) const;
  /** The PDF of a reference opened (kept open until the file changes); null when it is missing or unreadable (`error` says why). */
  PdfDocument* pdf(const QString& file, QString* error = nullptr);
  /** Page size in points of what the reference shows (empty when unknown). */
  QSizeF sizeOf(const BackgroundRef& ref, QString* error = nullptr);
  /** The background drawn `widthPx` wide, transparent where the page paints nothing. Null with `error` when it cannot be. */
  QImage render(const BackgroundRef& ref, int widthPx, QString* error = nullptr);

 private:
  struct Entry {
    QString path;
    qint64 size = -1, mtime = -1;
    std::unique_ptr<PdfDocument> pdf;
    QString error;
    QImage image;
  };
  Entry* entry(const QString& file, bool isPdf, QString* error);
  QStringList dirs_;
  QHash<QString, std::shared_ptr<Entry>> entries_;
};
