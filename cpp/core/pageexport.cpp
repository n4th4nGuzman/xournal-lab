#include "pageexport.h"
#include "pagerender.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kPdfResolution = 288;                 // device dots per inch of the PDF writer: 4 per point, so raster pieces (glow) stay sharp
constexpr double kMaxBackgroundPixels = 36e6;         // ~144 MB as ARGB32: a PDF/picture background is rasterised at most this fine (~600 dpi on A4)
constexpr double kMaxPixels = 120e6;                // ~480 MB as ARGB32: more than that and the allocation would fail or thrash

std::vector<int> pagesOf(const Document& doc, const ExportOptions& o) {
  std::vector<int> r;
  if (o.pages.empty()) {
    for (int i = 0; i < int(doc.pages.size()); i++) r.push_back(i);
  } else {
    for (int p : o.pages)
      if (p >= 0 && p < int(doc.pages.size())) r.push_back(p);
  }
  return r;
}

void drawOne(QPainter& p, const Document& doc, int index, double scale, int bgWidthPx, const ExportOptions& o, BackgroundResolver& res, QStringList* warn) {
  const Page& page = doc.pages[size_t(index)];
  // A printer asks for its own resolution (1200 dpi for a PDF printer: 140 Mpx, 560 MB for one A4 background): the picture behind the ink is
  // never rasterised finer than ~600 dpi (35 Mpx); the printer scales it, the ink stays vector.
  bgWidthPx = std::min(bgWidthPx, int(std::sqrt(kMaxBackgroundPixels * page.width() / page.height())));
  RenderOptions ro;
  ro.scale = scale;
  ro.background = o.background;
  QImage bg;
  if (o.background) {
    const BackgroundRef ref = backgroundRef(doc, index);
    if (ref.kind != BackgroundRef::None) {
      QString err;
      bg = res.render(ref, bgWidthPx, &err);
      if (bg.isNull() && warn) warn->push_back(QString("página %1: %2").arg(index + 1).arg(err.isEmpty() ? "no se pudo dibujar el fondo" : err));
    } else if (!ref.problem.isEmpty() && warn) {
      warn->push_back(QString("página %1: %2").arg(index + 1).arg(ref.problem));
    }
  }
  p.save();
  p.setClipRect(QRectF(0, 0, page.width(), page.height()));
  // An explicit (even blank) picture also keeps renderBackground from asking the on-screen provider, which would start rasterising for nothing.
  static const QImage blank = [] { QImage i(1, 1, QImage::Format_ARGB32_Premultiplied); i.fill(Qt::transparent); return i; }();
  renderBackground(p, page, ro, bg.isNull() ? &blank : &bg);
  renderLayers(p, page, ro, o.hiddenLayers ? o.hiddenLayers(index) : std::vector<int>{});
  p.restore();
}

}  // namespace

void paintPage(QPainter& p, const Document& doc, int page, const QRectF& target, int bgWidthPx, const ExportOptions& o, BackgroundResolver& resolver, QStringList* warnings) {
  if (page < 0 || page >= int(doc.pages.size())) return;
  const Page& pg = doc.pages[size_t(page)];
  if (pg.width() <= 0 || pg.height() <= 0) return;
  const double s = std::min(target.width() / pg.width(), target.height() / pg.height());
  p.save();
  p.translate(target.center().x() - pg.width() * s / 2, target.center().y() - pg.height() * s / 2);
  p.scale(s, s);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  drawOne(p, doc, page, s, bgWidthPx, o, resolver, warnings);
  p.restore();
}

QImage renderPageImage(const Document& doc, int page, double dpi, const ExportOptions& o, BackgroundResolver& resolver, QStringList* warnings, QString* error) {
  if (page < 0 || page >= int(doc.pages.size()) || dpi <= 0) { if (error) *error = "página o resolución no válidas"; return QImage(); }
  const Page& pg = doc.pages[size_t(page)];
  const double s = dpi / 72.0;
  const int w = std::max(1, int(std::ceil(pg.width() * s))), h = std::max(1, int(std::ceil(pg.height() * s)));
  if (double(w) * h > kMaxPixels) { if (error) *error = QString("la imagen de %1 x %2 píxeles es demasiado grande; baja la resolución").arg(w).arg(h); return QImage(); }
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  if (img.isNull()) { if (error) *error = "no hay memoria para la imagen"; return QImage(); }
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.scale(double(w) / pg.width(), double(h) / pg.height());
  drawOne(p, doc, page, s, w, o, resolver, warnings);
  p.end();
  return img;
}

ExportResult exportPdf(const Document& doc, const QString& path, const ExportOptions& o, BackgroundResolver& resolver, const ExportProgress& progress) {
  ExportResult r;
  const std::vector<int> pages = pagesOf(doc, o);
  if (pages.empty()) { r.error = "no hay páginas que exportar"; return r; }
  QSaveFile file(path);   // written aside and renamed on commit: a failure or a cancel never leaves a half PDF over an older one
  if (!file.open(QIODevice::WriteOnly)) { r.error = "no se pudo crear " + path + ": " + file.errorString(); return r; }
  const int total = int(pages.size());
  if (progress && !progress(0, total)) { file.cancelWriting(); r.cancelled = true; return r; }
  {
    QPdfWriter w(&file);
    w.setResolution(kPdfResolution);
    w.setCreator("Xournal Lab");
    w.setTitle(doc.title);
    w.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    for (int i = 0; i < total; i++) {
      const Page& pg = doc.pages[size_t(pages[size_t(i)])];
      w.setPageSize(QPageSize(QSizeF(pg.width(), pg.height()), QPageSize::Point));
      w.setPageMargins(QMarginsF(0, 0, 0, 0));
      if (i == 0) {
        if (!p.begin(&w)) { file.cancelWriting(); r.error = "no se pudo iniciar el PDF"; return r; }
      } else {
        w.newPage();
      }
      p.resetTransform();
      p.scale(kPdfResolution / 72.0, kPdfResolution / 72.0);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      p.setRenderHint(QPainter::TextAntialiasing, true);
      drawOne(p, doc, pages[size_t(i)], kPdfResolution / 72.0, int(std::ceil(pg.width() * o.bgDpi / 72.0)), o, resolver, &r.warnings);
      if (progress && !progress(i + 1, total)) { p.end(); file.cancelWriting(); r.cancelled = true; return r; }
    }
    p.end();
  }
  if (!file.commit()) { r.error = "no se pudo guardar " + path + ": " + file.errorString(); return r; }
  r.ok = true;
  r.pages = total;
  r.bytes = QFileInfo(path).size();
  r.files << path;
  return r;
}

ExportResult exportPng(const Document& doc, const QString& path, double dpi, const ExportOptions& o, BackgroundResolver& resolver, const ExportProgress& progress) {
  ExportResult r;
  const std::vector<int> pages = pagesOf(doc, o);
  if (pages.empty()) { r.error = "no hay páginas que exportar"; return r; }
  const int total = int(pages.size());
  const QFileInfo fi(path);
  QStringList written;
  if (progress && !progress(0, total)) { r.cancelled = true; return r; }
  for (int i = 0; i < total; i++) {
    QString err;
    QImage img = renderPageImage(doc, pages[size_t(i)], dpi, o, resolver, &r.warnings, &err);
    if (img.isNull()) { r.error = QString("página %1: %2").arg(pages[size_t(i)] + 1).arg(err); for (const QString& f : written) QFile::remove(f); return r; }
    const QString target = total == 1 ? path : fi.dir().filePath(QString("%1-%2.png").arg(fi.completeBaseName()).arg(pages[size_t(i)] + 1, 3, 10, QChar('0')));
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly) || !img.save(&f, "PNG") || !f.commit()) {
      r.error = "no se pudo escribir " + target;
      for (const QString& w : written) QFile::remove(w);
      return r;
    }
    written << target;
    if (progress && !progress(i + 1, total)) {
      for (const QString& w : written) QFile::remove(w);
      r.cancelled = true;
      return r;
    }
  }
  r.ok = true;
  r.pages = total;
  r.files = written;
  for (const QString& f : written) r.bytes += QFileInfo(f).size();
  return r;
}

std::vector<int> parsePageRange(const QString& text, int pageCount, QString* error) {
  std::vector<int> out;
  const auto fail = [&](const QString& why) { if (error) *error = why; return std::vector<int>(); };
  const QStringList parts = text.split(',', Qt::SkipEmptyParts);
  if (parts.isEmpty()) return fail("escribe las páginas, por ejemplo 1,3-5");
  for (const QString& raw : parts) {
    const QString part = raw.trimmed();
    const QStringList ends = part.split('-');
    bool ok1 = false, ok2 = false;
    const int a = ends[0].trimmed().toInt(&ok1);
    const int b = ends.size() == 2 ? ends[1].trimmed().toInt(&ok2) : a;
    if (ends.size() > 2 || !ok1 || (ends.size() == 2 && !ok2)) return fail("«" + part + "» no es una página ni un intervalo");
    if (a < 1 || b < 1 || a > pageCount || b > pageCount) return fail(QString("el documento tiene %1 páginas").arg(pageCount));
    if (b < a) return fail("el intervalo «" + part + "» está al revés");
    for (int i = a; i <= b; i++)
      if (std::find(out.begin(), out.end(), i - 1) == out.end()) out.push_back(i - 1);
  }
  return out;
}
