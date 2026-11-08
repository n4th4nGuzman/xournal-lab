// Page rendering (port of app/src/renderer/render.ts): background, texts, images and the dispatch to the stroke renderer.
#include "pagerender.h"
#include "genobj.h"
#include "markdown.h"
#include "texrender.h"
#include "paper.h"
#include "render_internal.h"
#include <QFont>
#include <QFontMetricsF>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>
#include <list>
#include <unordered_map>

namespace {

PdfImageDecoder g_pdfDecoder = nullptr;
BackgroundImageProvider g_bgProvider;   // set once at start-up, before any render thread exists

/** Hints every public entry point sets and restores, so callers do not have to. */
struct HintGuard {
  QPainter& p;
  QPainter::RenderHints saved;
  HintGuard(QPainter& painter, const RenderOptions& o) : p(painter), saved(painter.renderHints()) {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !o.lite);
    p.setRenderHint(QPainter::TextAntialiasing, true);
  }
  ~HintGuard() { p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing, false); p.setRenderHints(saved, true); }
};

double numAttr(const Attrs& a, const char* name, double fallback = 0) {
  const QString* v = attrFind(a, name);
  if (!v) return fallback;
  bool ok = false;
  const double d = v->toDouble(&ok);
  return ok ? d : fallback;
}

// ---------------------------------------------------------------- images

quint64 hashOf(const QString& s) {
  quint64 h = 14695981039346656037ull;
  const auto* b = reinterpret_cast<const unsigned char*>(s.constData());
  const size_t n = size_t(s.size()) * sizeof(QChar);
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h ^ quint64(s.size());
}

struct ImageCache {
  QMutex mutex;
  struct Entry { QImage img; qint64 bytes; std::list<quint64>::iterator lru; };
  std::unordered_map<quint64, Entry> map;
  std::list<quint64> order;
  qint64 bytes = 0;
  static constexpr qint64 kLimit = 64ll << 20;
};
ImageCache& imageCache() { static ImageCache c; return c; }

/** Decoded picture of base64 data (null when it is not an image Qt reads: a formula's PDF, or damage). Decoded once per distinct content.
 *  `pdfWidthPx` > 0 asks a PDF (a formula) for that many pixels across: the sharper version a deep zoom needs, cached beside the default one. */
QImage decodedImage(const QString& data, int pdfWidthPx = 0) {
  if (data.isEmpty()) return QImage();
  const quint64 key = hashOf(data) ^ (quint64(pdfWidthPx) * 0x9E3779B97F4A7C15ull);
  ImageCache& c = imageCache();
  {
    QMutexLocker lock(&c.mutex);
    auto it = c.map.find(key);
    if (it != c.map.end()) {
      c.order.splice(c.order.end(), c.order, it->second.lru);
      return it->second.img;
    }
  }
  const QByteArray raw = QByteArray::fromBase64(data.toLatin1());
  QImage img;
  // A formula saved as a vector PDF: the installed decoder draws it sharp (4 px per point) with its true ink colour on a transparent background. Qt's own
  // PDF image plugin, when present, reads it too, but at 1 px per point and on white, so it is only the fallback.
  if (raw.startsWith("%PDF") && g_pdfDecoder) img = g_pdfDecoder(raw, pdfWidthPx);
  if (img.isNull() && pdfWidthPx > 0) return decodedImage(data);   // the sharper one failed: the default size
  if (img.isNull()) img = QImage::fromData(raw);
  if (!img.isNull() && img.format() != QImage::Format_ARGB32_Premultiplied) img.convertTo(QImage::Format_ARGB32_Premultiplied);   // what the painter blits fastest
  QMutexLocker lock(&c.mutex);
  if (c.map.count(key)) return img;
  const qint64 bytes = std::max<qint64>(img.sizeInBytes(), 64);   // failures are remembered too, so a bad picture is not decoded on every paint
  c.order.push_back(key);
  c.map.emplace(key, ImageCache::Entry{img, bytes, std::prev(c.order.end())});
  c.bytes += bytes;
  while (c.bytes > ImageCache::kLimit && c.map.size() > 1) {
    const quint64 old = c.order.front();
    c.order.pop_front();
    c.bytes -= c.map[old].bytes;
    c.map.erase(old);
  }
  return img;
}

void drawImageEl(QPainter& p, const Image& im, const RenderOptions& o) {
  const double l = numAttr(im.attrs, "left"), t = numAttr(im.attrs, "top"), r = numAttr(im.attrs, "right"), b = numAttr(im.attrs, "bottom");
  const QRectF box(QPointF(l, t), QPointF(r, b));
  const bool tex = im.tag == "teximage";
  QImage img;
  if (tex && !o.lite) {
    // Zoomed in beyond what the default picture (4 px per point) holds: the formula is drawn again from its PDF at the size the screen needs, in steps of
    // two so that a smooth zoom does not rasterise at every level (the Electron app does the same up to 4096 px, render.ts TEX_SHARP_MAX).
    const double base = std::min(2048.0, std::max(16.0, std::abs(r - l) * 4.0)), need = std::abs(r - l) * o.scale;
    if (need > base * 1.25) {
      double target = base * 2;
      while (target < need && target < 4096) target *= 2;
      img = decodedImage(im.data, int(std::min(target, 4096.0)));
    }
  }
  if (img.isNull()) img = decodedImage(im.data);
  if (img.isNull() && tex) img = texBitmap(im);   // the PDF of a formula cannot be rasterised here: it is typeset again from its source
  if (img.isNull()) {
    if (!tex || o.lite) return;
    // The formula's picture is a PDF that nobody rasterised (no decoder installed, or damaged): a placeholder box keeps its place visible.
    p.save();
    QPen pen(QColor(90, 90, 90, 153), 0.7, Qt::CustomDashLine);
    pen.setDashPattern({4.3, 4.3});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
    p.restore();
    return;
  }
  if (!tex) { p.drawImage(box, img); return; }
  // Formulas can be turned and flipped (xlRot / xlFlip, see model.ts texPose).
  const double rot = numAttr(im.attrs, "xlRot");
  const bool flip = attrOr(im.attrs, "xlFlip") == "1";
  if (rot == 0 && !flip) { p.drawImage(box, img); return; }
  p.save();
  p.translate(box.center());
  p.rotate(rot * 180.0 / M_PI);
  if (flip) p.scale(-1, 1);
  p.drawImage(QRectF(-box.width() / 2, -box.height() / 2, box.width(), box.height()), img);
  p.restore();
}

// ---------------------------------------------------------------- text

QFont fontFor(const QString& family) {
  QFont f;
  if (family.isEmpty() || family == "Sans") f.setFamilies({"Segoe UI", "Arial", "Sans Serif"});   // system-ui of the original
  else f.setFamilies({family, "Sans Serif"});
  f.setPixelSize(100);   // laid out big and scaled, so that fractional sizes are exact
  f.setHintingPreference(QFont::PreferNoHinting);
  return f;
}

void drawTextEl(QPainter& p, const Text& t) {
  const double size = numAttr(t.attrs, "size", 12);
  if (!(size > 0)) return;
  const QColor color = colorOf(attrOr(t.attrs, "color", "#000000ff"));
  if (isMarkdown(t)) {
    drawMarkdown(p, t.text, size, attrOr(t.attrs, "font", "Sans"), QPointF(numAttr(t.attrs, "x"), numAttr(t.attrs, "y")), color);
    return;
  }
  const QFont font = fontFor(attrOr(t.attrs, "font", "Sans"));
  const QFontMetricsF fm(font);
  const double k = size / 100.0;
  const double lh = size * 1.2;   // keep in sync with textBBox in genobj.cpp
  p.save();
  p.translate(numAttr(t.attrs, "x"), numAttr(t.attrs, "y"));
  p.scale(k, k);
  p.setFont(font);
  p.setPen(color);
  const QStringList lines = t.text.split('\n');
  for (int i = 0; i < lines.size(); i++) {
    if (lines[i].isEmpty()) continue;
    // "top" baseline of a canvas: the top of the em box is at the line's y.
    p.drawText(QPointF(0, fm.ascent() + i * lh / k), lines[i]);
  }
  p.restore();
}

// ---------------------------------------------------------------- background

QColor backgroundColor(const Page& page, const RenderOptions& o) {
  const QString raw = attrOr(page.background, "color", "#ffffffff");
  QColor c = colorOf(raw);
  if (raw.isEmpty() || raw[0] != '#') {
    const QColor named(raw);   // Xournal++ also writes names such as "white" for old files
    c = named.isValid() ? named : QColor(255, 255, 255);
  }
  const bool defaultWhite = c.red() == 255 && c.green() == 255 && c.blue() == 255 && c.alpha() == 255;
  if (defaultWhite) {
    const QColor ov(o.paperColorOverride);
    if (!o.paperColorOverride.isEmpty() && ov.isValid()) return ov;
    if (o.darkPaper) return QColor(0x1e, 0x1e, 0x22);
  }
  return c;
}

QRectF boundsOf(const Element& e) { return elementRenderBounds(e); }

void drawElement(QPainter& p, const Element& e, const RenderOptions& o) {
  if (const auto* s = std::get_if<Stroke>(&e)) renderStroke(p, *s, o);
  else if (const auto* t = std::get_if<Text>(&e)) drawTextEl(p, *t);
  else if (const auto* im = std::get_if<Image>(&e)) drawImageEl(p, *im, o);
}

}  // namespace

std::vector<std::array<double, 4>> textSpanBoxes(const Text& t, int pos, int len) {
  std::vector<std::array<double, 4>> out;
  const double size = numAttr(t.attrs, "size", 12);
  if (!(size > 0) || len <= 0 || pos < 0 || pos + len > t.text.size() || isMarkdown(t)) return out;
  const QFontMetricsF fm(fontFor(attrOr(t.attrs, "font", "Sans")));   // the font and the line pitch of drawTextEl
  const double k = size / 100.0, lh = size * 1.2, x = numAttr(t.attrs, "x"), y = numAttr(t.attrs, "y");
  const QStringList lines = t.text.split('\n');
  int start = 0;
  for (int i = 0; i < lines.size(); start += int(lines[i].size()) + 1, i++) {
    const int a = std::max(pos, start) - start, b = std::min(pos + len, start + int(lines[i].size())) - start;
    if (b <= a) continue;
    out.push_back({x + fm.horizontalAdvance(lines[i].left(a)) * k, y + i * lh, x + fm.horizontalAdvance(lines[i].left(b)) * k, y + (i + 1) * lh});
  }
  return out;
}

QRectF elementRenderBounds(const Element& e) {
  if (const auto* s = std::get_if<Stroke>(&e)) return strokeBounds(*s);
  if (const auto* t = std::get_if<Text>(&e)) {
    if (isMarkdown(*t)) {
      const QSizeF sz = markdownSize(t->text, numAttr(t->attrs, "size", 12), attrOr(t->attrs, "font", "Sans"));
      const double x = numAttr(t->attrs, "x"), y = numAttr(t->attrs, "y");
      return QRectF(QPointF(x - 6, y - 6), QPointF(x + sz.width() + 6, y + sz.height() + 6));
    }
    const auto b = textBBox(*t);
    return QRectF(QPointF(b[0] - 6, b[1] - 6), QPointF(b[2] + 6, b[3] + 6));
  }
  const auto& im = std::get<Image>(e);
  const double l = numAttr(im.attrs, "left"), t = numAttr(im.attrs, "top"), r = numAttr(im.attrs, "right"), b = numAttr(im.attrs, "bottom");
  const double pad = 4 + (im.tag == "teximage" ? std::max(std::abs(r - l), std::abs(b - t)) : 0);   // a turned formula sticks out of its box
  return QRectF(QPointF(std::min(l, r) - pad, std::min(t, b) - pad), QPointF(std::max(l, r) + pad, std::max(t, b) + pad));
}

void setPdfImageDecoder(PdfImageDecoder decoder) {
  g_pdfDecoder = decoder;
  clearRenderCaches();   // pictures remembered as undecodable may be decodable now
}

void setBackgroundImageProvider(BackgroundImageProvider provider) { g_bgProvider = std::move(provider); }

void clearRenderCaches() {
  clearGlowCache();
  ImageCache& c = imageCache();
  QMutexLocker lock(&c.mutex);
  c.map.clear();
  c.order.clear();
  c.bytes = 0;
}

void renderBackground(QPainter& p, const Page& page, const RenderOptions& o, const QImage* bgImage) {
  if (!o.background) return;
  HintGuard hints(p, o);
  const double w = page.width(), h = page.height();
  const QRectF pageRect(0, 0, w, h);
  const QRectF area = o.clip.isEmpty() ? pageRect : pageRect.intersected(o.clip);
  if (area.isEmpty()) return;
  const QColor color = backgroundColor(page, o);
  p.fillRect(area, color);
  const QString type = attrOr(page.background, "type", "solid");
  if (type != "solid") {
    // pdf / image pages: the caller rasterised the page; without it the installed provider is asked; failing that the page stays the plain colour.
    QImage fetched;
    if (!bgImage && g_bgProvider) {
      fetched = g_bgProvider(page.background, w, h, o.scale);
      if (!fetched.isNull()) bgImage = &fetched;
    }
    if (bgImage && !bgImage->isNull()) {
      p.save();
      p.setClipRect(area, Qt::IntersectClip);
      p.drawImage(pageRect, *bgImage);
      p.restore();
    }
    return;
  }
  p.save();
  p.setClipRect(area, Qt::IntersectClip);
  p.setOpacity(p.opacity() * paperOpacityOf(page.background));
  if (const auto spec = paperOfBackground(page.background)) drawPaper(p, w, h, color, *spec);
  else drawBuiltinStyle(p, w, h, attrOr(page.background, "style", "plain"), paperSpacingOf(page.background));
  p.restore();
}

void renderElement(QPainter& p, const Element& e, const RenderOptions& o) {
  HintGuard hints(p, o);
  drawElement(p, e, o);
}

void renderLayers(QPainter& p, const Page& page, const RenderOptions& o, const std::vector<int>& hiddenLayers) {
  HintGuard hints(p, o);
  const bool cull = !o.clip.isEmpty();
  for (size_t li = 0; li < page.layers.size(); li++) {
    if (std::find(hiddenLayers.begin(), hiddenLayers.end(), int(li)) != hiddenLayers.end()) continue;
    for (const Element& e : page.layers[li].elements) {
      if (cull && !boundsOf(e).intersects(o.clip)) continue;
      drawElement(p, e, o);
    }
  }
}

void renderElementSeq(QPainter& p, const RenderOptions& o, const std::vector<const Element*>& els, const std::atomic<bool>* cancel) {
  HintGuard hints(p, o);
  const bool cull = !o.clip.isEmpty();
  for (const Element* e : els) {
    if (cancel && cancel->load(std::memory_order_relaxed)) return;
    if (cull && !boundsOf(*e).intersects(o.clip)) continue;
    drawElement(p, *e, o);
  }
}

void renderPage(QPainter& p, const Page& page, const RenderOptions& o, const QImage* bgImage) {
  renderBackground(p, page, o, bgImage);
  renderLayers(p, page, o);
}
