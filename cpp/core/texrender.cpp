#include "texrender.h"
#include "mathlayout.h"
#include <QByteArray>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QString>
#include <algorithm>
#include <cmath>
#include <list>
#include <unordered_map>

namespace {

struct Cache {
  struct Entry { QImage img; qint64 bytes; std::list<size_t>::iterator lru; };
  std::unordered_map<size_t, Entry> map;
  std::list<size_t> order;
  qint64 bytes = 0;
  static constexpr qint64 kLimit = 48ll << 20;
};
Cache& cache() { static Cache c; return c; }

double num(const Attrs& a, const char* name) {
  const QString* v = attrFind(a, name);
  return v ? v->toDouble() : 0.0;
}

}  // namespace

QColor texInkColor(const QString& pdfBase64) {
  const QByteArray pdf = QByteArray::fromBase64(pdfBase64.toLatin1());
  if (!pdf.startsWith("%PDF")) return QColor(0, 0, 0);
  static const QRegularExpression fill(QStringLiteral("(-?[0-9]*\\.?[0-9]+) (-?[0-9]*\\.?[0-9]+) (-?[0-9]*\\.?[0-9]+) (?:scn|rg)\\b"));
  int at = 0;
  while ((at = int(pdf.indexOf("stream", at))) >= 0) {
    at += 6;
    if (at >= pdf.size()) break;
    if (pdf[at] == '\r') at++;
    if (at < pdf.size() && pdf[at] == '\n') at++;
    const int end = int(pdf.indexOf("endstream", at));
    if (end < 0) break;
    const QByteArray raw = pdf.mid(at, end - at);
    at = end + 9;
    if (raw.size() < 8 || raw.startsWith("<?xpacket")) continue;
    // qUncompress wants the expected size big-endian in front of the zlib data; it only uses it as the first guess
    QByteArray framed(4, '\0');
    framed[1] = char(0x40);   // 4 MB
    framed += raw;
    const QByteArray content = qUncompress(framed);
    if (content.isEmpty()) continue;
    const auto m = fill.match(QString::fromLatin1(content.left(4096)));
    if (m.hasMatch()) {
      auto c = [&](int i) { return std::clamp(int(std::lround(m.captured(i).toDouble() * 255)), 0, 255); };
      return QColor(c(1), c(2), c(3));
    }
  }
  return QColor(0, 0, 0);
}

QImage texBitmap(const Image& im) {
  if (im.tag != "teximage" || !QGuiApplication::instance()) return QImage();
  const QString* src = attrFind(im.attrs, "text");
  if (!src || src->trimmed().isEmpty()) return QImage();
  Cache& c = cache();
  const size_t key = qHash(im.data, qHash(*src)) ^ (size_t(std::llround(std::abs(num(im.attrs, "bottom") - num(im.attrs, "top")) * 100)) << 1);
  auto it = c.map.find(key);
  if (it != c.map.end()) {
    c.order.splice(c.order.end(), c.order, it->second.lru);
    return it->second.img;
  }
  const double w = std::abs(num(im.attrs, "right") - num(im.attrs, "left")), h = std::abs(num(im.attrs, "bottom") - num(im.attrs, "top"));
  QImage img;
  if (w > 0 && h > 0) {
    // the file does not say whether it was typeset centred or in line: take the layout whose proportions match the box
    MathBox best;
    double bestErr = 1e30;
    for (bool display : {true, false}) {
      const MathBox b = layoutMathToHeight(*src, display, h);
      if (!b.ok || b.empty || b.height() <= 0) continue;
      const double err = std::abs(std::log(b.width / b.height() / (w / h)));
      if (err < bestErr) { bestErr = err; best = b; }
    }
    if (best.ok) img = renderMathImage(best, texInkColor(im.data), 4.0 * std::min(1.0, 2048.0 / std::max(1.0, best.width * 4.0)));
  }
  const qint64 bytes = std::max<qint64>(img.sizeInBytes(), 64);   // failures are remembered too
  c.order.push_back(key);
  c.map.emplace(key, Cache::Entry{img, bytes, std::prev(c.order.end())});
  c.bytes += bytes;
  while (c.bytes > Cache::kLimit && c.map.size() > 1) {
    const size_t old = c.order.front();
    c.order.pop_front();
    c.bytes -= c.map[old].bytes;
    c.map.erase(old);
  }
  return img;
}

void clearTexCache() {
  Cache& c = cache();
  c.map.clear();
  c.order.clear();
  c.bytes = 0;
}
