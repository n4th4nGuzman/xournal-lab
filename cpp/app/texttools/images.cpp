// Pictures: from a file, from the clipboard or dropped on the page. They are stored as base64 PNG, like the Electron app does (inserts.ts insertImage).
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "texttools.h"
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <algorithm>

namespace texttools {

namespace {
constexpr int kMaxSide = 2000;      // px: larger pictures are shrunk before encoding
constexpr double kPxToPt = 0.75;    // 96 dpi to the 72 dpi of page units
}  // namespace

bool insertImage(Editor& ed, const QImage& src) {
  if (src.isNull() || src.width() < 1 || src.height() < 1 || ed.pageCount() == 0) { ed.notify(QStringLiteral("No se pudo leer esa imagen")); return false; }
  commitOpenText();
  QImage img = src;
  if (std::max(img.width(), img.height()) > kMaxSide) img = img.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  if (!img.save(&buf, "PNG")) { ed.notify(QStringLiteral("No se pudo leer esa imagen")); return false; }

  const int page = std::clamp(ed.currentPage(), 0, ed.pageCount() - 1);
  const Page& pg = ed.doc().pages[size_t(page)];
  const double pw = pg.width(), ph = pg.height();
  const double w = std::min(src.width() * kPxToPt, pw * 0.6);
  const double h = w * (double(img.height()) / img.width());
  // just below the top of what is on screen, so the picture appears where the reader is looking
  double topVisible = 0;
  if (DocumentView* v = ed.view()) topVisible = v->toPage(page, QPointF(0, 0)).y();
  const double y = std::max(20.0, std::min(topVisible + 20, ph - h - 10));
  const double x = (pw - w) / 2;

  Image el;
  el.tag = QStringLiteral("image");
  el.attrs = {{"left", fmt8(x)}, {"top", fmt8(y)}, {"right", fmt8(x + w)}, {"bottom", fmt8(y + h)}};
  el.data = QString::fromLatin1(png.toBase64());

  const int layer = ed.activeLayer(page);
  if (!ed.layerVisible(page, layer)) {
    ed.setLayerVisible(page, layer, true);
    ed.notify(QStringLiteral("La capa activa estaba oculta: se ha vuelto a mostrar"));
  }
  const int index = ed.commitAdd(page, layer, Element(std::move(el)), QStringLiteral("Imagen"));
  ed.afterCreate(QString(ed.tool()));   // a copy: afterCreate switches tools, and `from` would otherwise follow the change
  ed.selection().select(page, {ElementRef{page, layer, index}});
  return true;
}

bool insertImageFile(Editor& ed, const QString& path) {
  QImageReader r(path);
  r.setAutoTransform(true);   // a photo's EXIF turn
  const QImage img = r.read();
  if (img.isNull()) { ed.notify(QStringLiteral("No se pudo leer esa imagen")); return false; }
  return insertImage(ed, img);
}

bool clipboardHasImage() {
  const QMimeData* m = QGuiApplication::clipboard()->mimeData();
  return m && m->hasImage();
}

bool pasteImageFromClipboard(Editor& ed) {
  const QMimeData* m = QGuiApplication::clipboard()->mimeData();
  if (!m || !m->hasImage()) return false;
  const QImage img = qvariant_cast<QImage>(m->imageData());
  return insertImage(ed, img);
}

}  // namespace texttools
