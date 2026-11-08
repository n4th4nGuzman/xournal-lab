#include "markdown.h"
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

const double kHeading[] = {2, 1.6, 1.35, 1.15, 1.05, 1};
const double kLine = 1.2;   // the same line height as genobj.cpp's textBBox

MdRun mkRun(const QString& text, const MdRun& over = MdRun()) {
  MdRun r = over;
  r.text = text;
  return r;
}

bool wordChar(const QString& s, int i) { return i >= 0 && i < s.size() && s[i].isLetterOrNumber(); }

MdLine plainLine(std::vector<MdRun> runs, double size, double x = 0) {
  MdLine l;
  l.runs = std::move(runs);
  l.size = size;
  l.x = x;
  return l;
}

QString familyOf(const QString& font) { return font == QLatin1String("Sans") || font.isEmpty() ? QStringLiteral("Segoe UI") : font; }

QFont runFont(const MdRun& r, double lineSize, const QString& font, int* px) {
  QFont f(r.mono ? QStringLiteral("Consolas") : familyOf(font));
  if (r.mono) f.setStyleHint(QFont::Monospace);
  const int p = std::max(1, int(std::lround(lineSize)));
  f.setPixelSize(p);
  f.setBold(r.bold);
  f.setItalic(r.italic);
  if (px) *px = p;
  return f;
}

double measure(const MdRun& r, double lineSize, const QString& font) {
  int px = 1;
  const QFont f = runFont(r, lineSize, font, &px);
  return QFontMetricsF(f).horizontalAdvance(r.text) * lineSize / px;
}

}  // namespace

std::vector<MdRun> inlineRuns(const QString& src, const MdRun& base) {
  std::vector<MdRun> out;
  QString buf;
  bool bold = base.bold, italic = false, strike = false;
  auto flush = [&]() {
    if (buf.isEmpty()) return;
    MdRun r = base;
    r.bold = bold; r.italic = italic; r.strike = strike;
    out.push_back(mkRun(buf, r));
    buf.clear();
  };
  auto closes = [&](const QString& mark, int from) { return src.indexOf(mark, from) >= 0; };
  static const QRegularExpression linkRe(QStringLiteral("^\\[([^\\]]+)\\]\\(([^)\\s]*)\\)"));
  static const QString escapable = QStringLiteral("\\`*_~[]()#>+-.!");
  int i = 0;
  while (i < src.size()) {
    const QChar c = src[i];
    if (c == '\\' && i + 1 < src.size() && escapable.contains(src[i + 1])) { buf += src[i + 1]; i += 2; continue; }
    if (c == '`') {
      const int end = src.indexOf('`', i + 1);
      if (end > i) {
        flush();
        MdRun r = base;
        r.mono = true;
        out.push_back(mkRun(src.mid(i + 1, end - i - 1), r));
        i = end + 1;
        continue;
      }
    }
    if (c == '[') {
      const auto m = linkRe.match(src.mid(i));
      if (m.hasMatch()) {
        flush();
        MdRun b2 = base;
        b2.bold = bold; b2.italic = italic; b2.strike = strike; b2.underline = true; b2.link = true;
        for (MdRun& r : inlineRuns(m.captured(1), b2)) out.push_back(std::move(r));
        i += int(m.capturedLength(0));
        continue;
      }
    }
    const QString two = src.mid(i, 2);
    // "__" may not OPEN inside a word (snake__case) but must be able to CLOSE after one ("__bold__").
    if (two == "__" && !bold && wordChar(src, i - 1)) { buf += two; i += 2; continue; }   // snake__case: neither bold nor two italics
    if (two == "**" || two == "__") {
      const bool open = !bold;
      if (!open || closes(two, i + 2)) { flush(); bold = open; i += 2; continue; }
    }
    if (two == "~~" && (strike || closes(QStringLiteral("~~"), i + 2))) { flush(); strike = !strike; i += 2; continue; }
    if ((c == '*' || c == '_') && two != "**" && two != "__") {
      const bool intraWord = c == '_' && wordChar(src, i - 1) && wordChar(src, i + 1);
      const bool nextSpace = i + 1 < src.size() && src[i + 1] == ' ';
      if (!intraWord && (italic || (!nextSpace && closes(QString(c), i + 1)))) { flush(); italic = !italic; i += 1; continue; }
    }
    buf += c;
    i++;
  }
  flush();
  return out;
}

std::vector<MdLine> parseMarkdown(const QString& srcIn, double size) {
  std::vector<MdLine> lines;
  QString src = srcIn;
  src.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace('\r', '\n');
  const QStringList raw = src.split('\n');
  bool fence = false;
  const double TAB = size * 1.3;
  static const QRegularExpression fenceRe(QStringLiteral("^\\s*```"));
  static const QRegularExpression ruleRe(QStringLiteral("^\\s*([-*_])(\\s*\\1){2,}\\s*$"));
  static const QRegularExpression headRe(QStringLiteral("^(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
  static const QRegularExpression quoteRe(QStringLiteral("^(\\s*)>\\s?(.*)$"));
  static const QRegularExpression bulletRe(QStringLiteral("^(\\s*)([-*+])\\s+(?:\\[([ xX])\\]\\s+)?(.*)$"));
  static const QRegularExpression numRe(QStringLiteral("^(\\s*)(\\d+)[.)]\\s+(.*)$"));
  auto levelOf = [](QString ws) { ws.replace('\t', QStringLiteral("  ")); return int(ws.size() / 2); };
  for (const QString& line : raw) {
    if (fenceRe.match(line).hasMatch()) { fence = !fence; continue; }
    if (fence) {
      MdRun r; r.mono = true;
      MdLine l = plainLine({mkRun(line, r)}, size * 0.92);
      l.code = true;
      lines.push_back(std::move(l));
      continue;
    }
    if (line.trimmed().isEmpty()) {   // a blank line is half a line
      MdLine l = plainLine({}, size);
      l.gap = -size * 0.55;
      lines.push_back(std::move(l));
      continue;
    }
    if (ruleRe.match(line).hasMatch()) { MdLine l = plainLine({}, size); l.rule = true; lines.push_back(std::move(l)); continue; }
    auto m = headRe.match(line);
    if (m.hasMatch()) {
      const double k = kHeading[m.captured(1).size() - 1];
      MdRun b; b.bold = true;
      MdLine l = plainLine(inlineRuns(m.captured(2), b), size * k);
      l.gap = lines.empty() ? 0 : size * 0.25 * k;
      lines.push_back(std::move(l));
      continue;
    }
    m = quoteRe.match(line);
    if (m.hasMatch()) { MdLine l = plainLine(inlineRuns(m.captured(2)), size, TAB * 0.6); l.quote = true; lines.push_back(std::move(l)); continue; }
    m = bulletRe.match(line);
    if (m.hasMatch()) {
      const int level = levelOf(m.captured(1));
      static const QString marks[] = {QString(QChar(0x2022)), QString(QChar(0x25E6)), QString(QChar(0x25AA))};
      const QString task = m.captured(3);
      const QString mark = !task.isNull() && !task.isEmpty() ? (task == " " ? QString(QChar(0x2610)) : QString(QChar(0x2611))) : marks[level % 3];
      MdLine l = plainLine(inlineRuns(m.captured(4)), size, TAB * (level + 1));
      l.hasMarker = true;
      l.marker = {mark, TAB * level + TAB * 0.2};
      lines.push_back(std::move(l));
      continue;
    }
    m = numRe.match(line);
    if (m.hasMatch()) {
      const int level = levelOf(m.captured(1));
      MdLine l = plainLine(inlineRuns(m.captured(3)), size, TAB * (level + 1));
      l.hasMarker = true;
      l.marker = {m.captured(2) + ".", TAB * level + TAB * 0.1};
      lines.push_back(std::move(l));
      continue;
    }
    QString t = line;
    while (!t.isEmpty() && t.back().isSpace()) t.chop(1);
    lines.push_back(plainLine(inlineRuns(t), size));
  }
  return lines;
}

bool isMarkdown(const Attrs& attrs) {
  const QString* v = attrFind(attrs, QStringLiteral("xlMd"));
  return v && *v == QLatin1String("1");
}

MdLayout layoutMarkdown(const QString& text, double size, const QString& font) {
  static QMutex mu;
  static QHash<QString, MdLayout> cache;
  const QString key = QString::number(size, 'g', 10) + '|' + font + '|' + text;
  {
    QMutexLocker lock(&mu);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
  }
  MdLayout lay;
  double y = 0, width = 0;
  for (const MdLine& line : parseMarkdown(text, size)) {
    const double h = line.size * kLine;
    y += std::max(0.0, line.gap);
    MdLayoutLine ll;
    double w = 0;
    for (const MdRun& r : line.runs) { const double rw = measure(r, line.size, font); ll.widths.push_back(rw); w += rw; }
    const double lineH = line.gap < 0 ? h + line.gap : h;
    ll.line = line;
    ll.y = y; ll.h = lineH; ll.width = w;
    width = std::max(width, line.x + w + (line.code ? size * 0.4 : 0));
    y += lineH;
    lay.lines.push_back(std::move(ll));
  }
  lay.width = std::max(width, size);
  lay.height = std::max(y, size * kLine);
  QMutexLocker lock(&mu);
  if (cache.size() > 64) cache.clear();   // bounded: boxes being edited produce a new key per keystroke
  cache.insert(key, lay);
  return lay;
}

std::vector<std::array<double, 4>> markdownSpanBoxes(const QString& text, double size, const QString& font, const QString& needle) {
  std::vector<std::array<double, 4>> out;
  if (needle.isEmpty()) return out;
  const MdLayout lay = layoutMarkdown(text, size, font);
  for (const MdLayoutLine& l : lay.lines) {
    QString rendered;
    for (const MdRun& r : l.line.runs) rendered += r.text;
    // x of character `at` of the rendered line: the runs before it, plus the advance inside its own run
    auto xAt = [&](int at) {
      double x = l.line.x;
      int start = 0;
      for (size_t i = 0; i < l.line.runs.size(); i++) {
        const MdRun& r = l.line.runs[i];
        const int end = start + int(r.text.size());
        if (at <= end || i + 1 == l.line.runs.size()) {
          int px = 1;
          const QFont f = runFont(r, l.line.size, font, &px);
          return x + QFontMetricsF(f).horizontalAdvance(r.text.left(std::max(0, at - start))) * l.line.size / px;
        }
        x += l.widths[i];
        start = end;
      }
      return x;
    };
    for (qsizetype at = rendered.indexOf(needle, 0, Qt::CaseSensitive); at >= 0; at = rendered.indexOf(needle, at + needle.size(), Qt::CaseSensitive))
      out.push_back({xAt(int(at)), l.y, xAt(int(at + needle.size())), l.y + l.h});
  }
  return out;
}

QSizeF markdownSize(const QString& text, double size, const QString& font) {
  const MdLayout l = layoutMarkdown(text, size, font);
  return QSizeF(l.width, l.height);
}

void drawMarkdown(QPainter& p, const QString& text, double size, const QString& font, QPointF topLeft, const QColor& color) {
  const MdLayout lay = layoutMarkdown(text, size, font);
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  const double ox = topLeft.x(), oy = topLeft.y();
  auto drawText = [&](const MdRun& r, double lineSize, const QString& s, double x, double base) {
    int px = 1;
    const QFont f = runFont(r, lineSize, font, &px);
    const double k = lineSize / px;   // the font is a whole number of pixels, as when measuring; the transform brings it to the real size
    p.save();
    p.translate(x, base);
    p.scale(k, k);
    p.setFont(f);
    p.drawText(QPointF(0, 0), s);
    p.restore();
  };
  for (const MdLayoutLine& l : lay.lines) {
    const MdLine& line = l.line;
    const double top = oy + l.y;
    const double base = top + l.h * 0.5 + line.size * 0.32;   // the middle of the line plus about a third of the type size
    if (line.code) p.fillRect(QRectF(ox - size * 0.15, top, lay.width + size * 0.3, l.h), QColor(128, 128, 128, int(0.18 * 255)));
    if (line.rule) {
      QPen pen(color, std::max(0.6, size * 0.06));
      QColor c = color; c.setAlphaF(c.alphaF() * 0.6);
      pen.setColor(c);
      p.setPen(pen);
      p.drawLine(QPointF(ox, top + l.h / 2), QPointF(ox + std::max(lay.width, size * 6), top + l.h / 2));
      continue;
    }
    if (line.quote) {
      QColor c = color; c.setAlphaF(c.alphaF() * 0.45);
      p.fillRect(QRectF(ox + line.x - size * 0.7, top, size * 0.18, l.h), c);
    }
    if (line.hasMarker) {
      p.setPen(color);
      drawText(MdRun(), line.size, line.marker.text, ox + line.marker.x, base);
    }
    double x = ox + line.x;
    for (size_t i = 0; i < line.runs.size(); i++) {
      const MdRun& r = line.runs[i];
      const double w = l.widths[i];
      QColor c = r.link ? QColor(0x2f, 0x6f, 0xd0) : color;
      if (line.quote) c.setAlphaF(c.alphaF() * 0.8);
      p.setPen(c);
      drawText(r, line.size, r.text, x, base);
      if (r.strike || r.underline) {
        QPen pen(c, std::max(0.5, line.size * 0.06));
        p.setPen(pen);
        const double yy = r.strike ? base - line.size * 0.3 : base + line.size * 0.12;
        p.drawLine(QPointF(x, yy), QPointF(x + w, yy));
      }
      x += w;
    }
  }
  p.restore();
}
