#include "pdfraster.h"
#include <QBuffer>
#include <QPainter>
#include <algorithm>
#include <cmath>

quint64 pdfContentHash(const QByteArray& pdf) {
  quint64 h = 14695981039346656037ull;
  const auto* b = reinterpret_cast<const unsigned char*>(pdf.constData());
  for (qsizetype i = 0; i < pdf.size(); i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h ^ quint64(pdf.size());
}

#ifdef XL_HAVE_PDF
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>

bool pdfRasterAvailable() { return true; }

struct PdfDocument::Impl {
  QByteArray bytes;          // the document reads from it for as long as it is open
  QBuffer buffer;
  QPdfDocument doc;
};

PdfDocument::PdfDocument() : d(new Impl) {}
PdfDocument::~PdfDocument() = default;

bool PdfDocument::open(const QByteArray& pdf, QString* error) {
  d->doc.close();
  d->buffer.close();
  d->bytes = pdf;
  d->buffer.setData(d->bytes);
  d->buffer.open(QIODevice::ReadOnly);
  d->doc.load(&d->buffer);
  const QPdfDocument::Error e = d->doc.error();
  if (e != QPdfDocument::Error::None) {
    if (error) {
      switch (e) {
        case QPdfDocument::Error::InvalidFileFormat: *error = "el archivo no es un PDF válido"; break;
        case QPdfDocument::Error::IncorrectPassword: *error = "el PDF está protegido con contraseña"; break;
        case QPdfDocument::Error::UnsupportedSecurityScheme: *error = "el PDF usa un cifrado no admitido"; break;
        default: *error = "no se pudo leer el PDF"; break;
      }
    }
    d->doc.close();
    return false;
  }
  if (d->doc.pageCount() < 1) {
    if (error) *error = "el PDF no tiene páginas";
    return false;
  }
  return true;
}

bool PdfDocument::isOpen() const { return d->doc.status() == QPdfDocument::Status::Ready; }
int PdfDocument::pageCount() const { return isOpen() ? d->doc.pageCount() : 0; }

QSizeF PdfDocument::pageSize(int pageNo) const {
  if (!isOpen() || pageNo < 1 || pageNo > d->doc.pageCount()) return QSizeF();
  return d->doc.pagePointSize(pageNo - 1);
}

QImage PdfDocument::renderClip(int pageNo, int widthPx, const QRect& clipPx, const QColor& background) const {
  const QSizeF pt = pageSize(pageNo);
  if (pt.isEmpty() || widthPx < 1) return QImage();
  const int w = widthPx, h = std::max(1, int(std::ceil(double(w) * pt.height() / pt.width())));
  const QRect whole(0, 0, w, h);
  const QRect clip = clipPx.isNull() ? whole : clipPx.intersected(whole);
  if (clip.isEmpty()) return QImage();
  QPdfDocumentRenderOptions opt;
  if (clip != whole) { opt.setScaledSize(QSize(w, h)); opt.setScaledClipRect(clip); }   // the image is the clip; the page is scaled as if whole
  QImage img = d->doc.render(pageNo - 1, clip.size(), opt);
  if (img.isNull()) return QImage();
  if (img.format() != QImage::Format_ARGB32_Premultiplied) img.convertTo(QImage::Format_ARGB32_Premultiplied);
  if (background.isValid()) {
    QImage out(img.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(background);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    return out;
  }
  return img;
}

#else   // ---- built without Qt6Pdf: nothing can be drawn, and everything says so -------------------------------------------------------------------

bool pdfRasterAvailable() { return false; }
struct PdfDocument::Impl {};
PdfDocument::PdfDocument() : d(new Impl) {}
PdfDocument::~PdfDocument() = default;
bool PdfDocument::open(const QByteArray&, QString* error) { if (error) *error = "esta compilación no incluye Qt6Pdf"; return false; }
bool PdfDocument::isOpen() const { return false; }
int PdfDocument::pageCount() const { return 0; }
QSizeF PdfDocument::pageSize(int) const { return QSizeF(); }
QImage PdfDocument::renderClip(int, int, const QRect&, const QColor&) const { return QImage(); }
#endif

QImage PdfDocument::render(int pageNo, int widthPx, const QColor& background) const { return renderClip(pageNo, widthPx, QRect(), background); }

namespace {
/** The document the calling thread used last: a formula is drawn at several sizes and a page's tiles in a row, and parsing each time would dominate. */
struct ThreadDoc {
  QByteArray bytes;
  std::unique_ptr<PdfDocument> doc;
  QString error;
};
const PdfDocument* docFor(const QByteArray& pdf, QString* error) {
  thread_local ThreadDoc c;
  if (!c.doc || c.bytes.size() != pdf.size() || c.bytes != pdf) {
    c.doc = std::make_unique<PdfDocument>();
    c.bytes = pdf;
    c.error.clear();
    if (!c.doc->open(pdf, &c.error)) c.doc.reset();
  }
  if (!c.doc && error) *error = c.error;
  return c.doc.get();
}
}  // namespace

PdfInfo pdfInfo(const QByteArray& pdf) {
  PdfInfo r;
  const PdfDocument* d = docFor(pdf, &r.error);
  if (!d) return r;
  const QSizeF s = d->pageSize(1);
  r.ok = true;
  r.pages = d->pageCount();
  r.widthPt = s.width();
  r.heightPt = s.height();
  return r;
}

QImage rasterizePdfPage(const QByteArray& pdf, int pageNo, int widthPx, const QColor& background) {
  const PdfDocument* d = docFor(pdf, nullptr);
  return d ? d->render(pageNo, widthPx, background) : QImage();
}

QImage rasterizePdfInk(const QByteArray& pdf, int widthPx) {
  const PdfDocument* d = docFor(pdf, nullptr);
  if (!d) return QImage();
  // Difference matting: over white P = a*C + (1-a)*255, over black P = a*C, so a = 1 - (white - black)/255 and C = black / a, exactly,
  // without guessing the ink colour (also right for white ink, which the TS had to special-case).
  QImage w = d->render(1, widthPx, QColor(255, 255, 255));
  QImage k = d->render(1, widthPx, QColor(0, 0, 0));
  if (w.isNull() || k.isNull() || w.size() != k.size()) return QImage();
  w.convertTo(QImage::Format_ARGB32);
  k.convertTo(QImage::Format_ARGB32);
  QImage out(w.size(), QImage::Format_ARGB32);
  bool any = false;
  for (int y = 0; y < w.height(); y++) {
    const QRgb* pw = reinterpret_cast<const QRgb*>(w.constScanLine(y));
    const QRgb* pk = reinterpret_cast<const QRgb*>(k.constScanLine(y));
    QRgb* po = reinterpret_cast<QRgb*>(out.scanLine(y));
    for (int x = 0; x < w.width(); x++) {
      const int diff = (qRed(pw[x]) - qRed(pk[x]) + qGreen(pw[x]) - qGreen(pk[x]) + qBlue(pw[x]) - qBlue(pk[x])) / 3;
      const double a = std::clamp(1.0 - diff / 255.0, 0.0, 1.0);
      if (a < 1.0 / 255) { po[x] = 0; continue; }
      any = true;
      const auto ch = [&](int v) { return std::clamp(int(std::lround(v / a)), 0, 255); };
      po[x] = qRgba(ch(qRed(pk[x])), ch(qGreen(pk[x])), ch(qBlue(pk[x])), int(std::lround(a * 255)));
    }
  }
  if (!any) return QImage();
  out.convertTo(QImage::Format_ARGB32_Premultiplied);
  return out;
}

QImage rasterizePdfFormula(const QByteArray& pdf, int widthPx) {
  const PdfDocument* d = docFor(pdf, nullptr);
  if (!d) return QImage();
  const QSizeF s = d->pageSize(1);
  if (s.isEmpty()) return QImage();
  int w = std::clamp(int(std::lround(s.width() * 4)), 16, 2048);
  if (widthPx > w) {
    w = std::min(widthPx, 4096);
    const double aspect = s.height() / s.width();
    if (double(w) * w * aspect > 16e6) w = std::max(16, int(std::sqrt(16e6 / aspect)));   // a tall formula: bounded in pixels too
  }
  return rasterizePdfInk(pdf, w);
}
