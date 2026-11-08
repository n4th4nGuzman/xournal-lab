#include "pdfbackground.h"
#include "genobj.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

QString documentPdfFile(const Document& doc) {
  for (const Page& p : doc.pages)
    if (attrOr(p.background, "type") == "pdf") {
      const QString f = attrOr(p.background, "filename");
      if (!f.isEmpty()) return f;
    }
  return QString();
}

BackgroundRef backgroundRefOf(const Attrs& bg, const QString& documentPdf) {
  BackgroundRef r;
  const QString type = attrOr(bg, "type");
  if (type == "pdf") {
    r.kind = BackgroundRef::Pdf;
    r.file = attrOr(bg, "filename");
    if (r.file.isEmpty()) r.file = documentPdf;
    bool ok = false;
    const int n = attrOr(bg, "pageno").toInt(&ok);
    r.pageNo = ok && n >= 1 ? n : 1;
    if (r.file.isEmpty()) { r.kind = BackgroundRef::None; r.problem = "la página dice ser de un PDF pero ninguna página nombra el archivo"; }
  } else if (type == "pixmap") {
    r.kind = BackgroundRef::Image;
    r.file = attrOr(bg, "filename");
    if (r.file.isEmpty() || attrOr(bg, "domain") == "clone") { r.kind = BackgroundRef::None; if (r.file.isEmpty()) r.problem = "el fondo de imagen no nombra el archivo"; }
  }
  return r;
}

BackgroundRef backgroundRef(const Document& doc, int page) {
  BackgroundRef r;
  if (page < 0 || page >= int(doc.pages.size())) return r;
  const Attrs* bg = &doc.pages[size_t(page)].background;
  if (attrOr(*bg, "type") == "pixmap" && attrOr(*bg, "domain") == "clone") {
    // `filename` is then the index of the page whose picture this one shares (one hop: a clone of a clone is not what Xournal++ writes)
    bool ok = false;
    const int src = attrOr(*bg, "filename").toInt(&ok);
    if (!ok || src < 0 || src >= int(doc.pages.size()) || src == page) { r.problem = "el fondo copia una página que no existe"; return r; }
    bg = &doc.pages[size_t(src)].background;
    if (attrOr(*bg, "type") != "pixmap" || attrOr(*bg, "domain") == "clone") { r.problem = "el fondo copia una página sin imagen"; return r; }
  }
  return backgroundRefOf(*bg, documentPdfFile(doc));
}

bool normalizePdfReferences(Document& doc, const QString& knownFile) {
  QString file = documentPdfFile(doc);
  if (file.isEmpty()) file = knownFile;
  if (file.isEmpty()) return false;
  bool changed = false, seenFirst = false;
  for (Page& p : doc.pages) {
    if (attrOr(p.background, "type") != "pdf") continue;
    const bool has = !attrOr(p.background, "filename").isEmpty();
    if (!seenFirst) {
      seenFirst = true;
      if (!has || attrFind(p.background, "domain") == nullptr) {
        Attrs a;
        for (const auto& kv : p.background)
          if (kv.first != "domain" && kv.first != "filename") a.push_back(kv);
        const QString domain = attrOr(p.background, "domain", "absolute");
        Attrs out;
        for (const auto& kv : a) {
          out.push_back(kv);
          if (kv.first == "type") { out.push_back({"domain", domain}); out.push_back({"filename", has ? attrOr(p.background, "filename") : file}); }
        }
        p.background = out;
        changed = true;
      }
    } else if (has && attrOr(p.background, "filename") == file) {
      Attrs out;
      for (const auto& kv : p.background)
        if (kv.first != "domain" && kv.first != "filename") out.push_back(kv);
      p.background = out;
      changed = true;
    }
  }
  return changed;
}

Document documentForPdf(const PdfDocument& pdf, const QString& absolutePath) {
  Document d;
  d.rootAttrs = {{"creator", "xournalpp 1.3.8"}, {"fileversion", "4"}};
  d.title = "Xournal++ document - see https://xournalpp.github.io/";
  const QString path = QDir::toNativeSeparators(QFileInfo(absolutePath).absoluteFilePath());
  for (int i = 1; i <= pdf.pageCount(); i++) {
    const QSizeF s = pdf.pageSize(i);
    Page p;
    p.attrs = {{"width", fmt8(s.width())}, {"height", fmt8(s.height())}};
    p.background = {{"type", "pdf"}};
    if (i == 1) { p.background.push_back({"domain", "absolute"}); p.background.push_back({"filename", path}); }
    p.background.push_back({"pageno", QString::number(i)});
    p.layers.push_back(Layer{});
    d.pages.push_back(std::move(p));
  }
  return d;
}

// ------------------------------------------------------------------------------------------------------------------------------ resolver
QString BackgroundResolver::locate(const QString& file) const {
  if (file.isEmpty()) return QString();
  if (QFileInfo::exists(file)) return file;
  const QString name = QFileInfo(file.contains('\\') ? QString(file).replace('\\', '/') : file).fileName();   // a Windows path read on another system and the reverse
  if (name.isEmpty()) return QString();
  for (const QString& d : dirs_) {
    const QString c = QDir(d).filePath(name);
    if (!d.isEmpty() && QFileInfo(c).isFile()) return c;
  }
  if (QFileInfo(file).isRelative())
    for (const QString& d : dirs_) {
      const QString c = QDir(d).filePath(file);
      if (!d.isEmpty() && QFileInfo(c).isFile()) return c;
    }
  return QString();
}

BackgroundResolver::Entry* BackgroundResolver::entry(const QString& file, bool isPdf, QString* error) {
  const QString path = locate(file);
  if (path.isEmpty()) {
    if (error) *error = "no se encuentra el archivo " + QFileInfo(file).fileName();
    entries_.remove(file);
    return nullptr;
  }
  const QFileInfo fi(path);
  std::shared_ptr<Entry>& e = entries_[file];
  if (e && e->path == path && e->size == fi.size() && e->mtime == fi.lastModified().toMSecsSinceEpoch()) {
    if (error && !e->error.isEmpty()) *error = e->error;
    return (isPdf ? bool(e->pdf) : !e->image.isNull()) ? e.get() : nullptr;
  }
  e = std::make_shared<Entry>();
  e->path = path;
  e->size = fi.size();
  e->mtime = fi.lastModified().toMSecsSinceEpoch();
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    e->error = "no se pudo leer " + fi.fileName();
  } else if (isPdf) {
    auto doc = std::make_unique<PdfDocument>();
    if (doc->open(f.readAll(), &e->error)) e->pdf = std::move(doc);
  } else {
    e->image = QImage(path);
    if (e->image.isNull()) e->error = "no se pudo leer la imagen " + fi.fileName();
    else if (e->image.format() != QImage::Format_ARGB32_Premultiplied) e->image.convertTo(QImage::Format_ARGB32_Premultiplied);
  }
  if (error && !e->error.isEmpty()) *error = e->error;
  return (isPdf ? bool(e->pdf) : !e->image.isNull()) ? e.get() : nullptr;
}

PdfDocument* BackgroundResolver::pdf(const QString& file, QString* error) {
  Entry* e = entry(file, true, error);
  return e ? e->pdf.get() : nullptr;
}

QSizeF BackgroundResolver::sizeOf(const BackgroundRef& ref, QString* error) {
  if (ref.kind == BackgroundRef::Pdf) {
    PdfDocument* d = pdf(ref.file, error);
    return d ? d->pageSize(ref.pageNo) : QSizeF();
  }
  if (ref.kind == BackgroundRef::Image) {
    Entry* e = entry(ref.file, false, error);
    return e ? QSizeF(e->image.size()) : QSizeF();
  }
  return QSizeF();
}

QImage BackgroundResolver::render(const BackgroundRef& ref, int widthPx, QString* error) {
  if (widthPx < 1) return QImage();
  if (ref.kind == BackgroundRef::Pdf) {
    PdfDocument* d = pdf(ref.file, error);
    if (!d) return QImage();
    QImage img = d->render(ref.pageNo, widthPx, QColor());
    if (img.isNull() && error) *error = QString("el PDF no tiene la pÃ¡gina %1").arg(ref.pageNo);
    return img;
  }
  if (ref.kind == BackgroundRef::Image) {
    Entry* e = entry(ref.file, false, error);
    if (!e) return QImage();
    // never bigger than the picture itself: scaling up gains nothing and costs memory
    return e->image.width() <= widthPx ? e->image : e->image.scaledToWidth(widthPx, Qt::SmoothTransformation);
  }
  return QImage();
}
