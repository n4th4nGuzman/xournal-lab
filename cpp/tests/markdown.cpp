// Markdown parsing, measuring and drawing, and its agreement with genobj.cpp's textBBox for text without markup.
#include "genobj.h"
#include "markdown.h"
#include <QGuiApplication>
#include <QImage>
#include <array>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static QString S(const char* s) { return QString::fromUtf8(s); }

static void inlineTable() {
  struct C { const char* src; std::vector<std::array<const char*, 2>> runs; };   // text, flags: b i s u m l
  const C table[] = {
    {"plain", {{"plain", ""}}},
    {"a **b** c", {{"a ", ""}, {"b", "b"}, {" c", ""}}},
    {"a *b* c", {{"a ", ""}, {"b", "i"}, {" c", ""}}},
    {"x _y_", {{"x ", ""}, {"y", "i"}}},
    {"~~gone~~", {{"gone", "s"}}},
    {"use `code` here", {{"use ", ""}, {"code", "m"}, {" here", ""}}},
    {"[site](http://x.y) end", {{"site", "ul"}, {" end", ""}}},
    {"**open", {{"**open", ""}}},                       // an opening mark with no closing one stays as typed
    {"snake_case_name", {{"snake_case_name", ""}}},     // intra-word underscores are not emphasis
    {"2 * 3 * 4", {{"2 * 3 * 4", ""}}},                 // a star followed by a space does not open emphasis
    {"\\*not\\*", {{"*not*", ""}}},
    {"***bi***", {{"bi", "bi"}}},
    {"__bold__ x", {{"bold", "b"}, {" x", ""}}},        // "__" closes after a word character...
    {"a__b__c", {{"a__b__c", ""}}},                     // ...but does not open inside a word
    {"x __y__", {{"x ", ""}, {"y", "b"}}},
  };
  for (const C& c : table) {
    const auto runs = inlineRuns(S(c.src));
    bool ok = runs.size() == c.runs.size();
    for (size_t i = 0; ok && i < runs.size(); i++) {
      const QString f = S(c.runs[i][1]);
      ok = runs[i].text == S(c.runs[i][0]) && runs[i].bold == f.contains('b') && runs[i].italic == f.contains('i') && runs[i].strike == f.contains('s') &&
           runs[i].underline == f.contains('u') && runs[i].mono == f.contains('m') && runs[i].link == f.contains('l');
    }
    if (!ok) {
      std::printf("runs for '%s':", c.src);
      for (const auto& r : runs) std::printf(" [%s%s%s%s%s%s%s]", qPrintable(r.text), r.bold ? " b" : "", r.italic ? " i" : "", r.strike ? " s" : "", r.underline ? " u" : "", r.mono ? " m" : "", r.link ? " l" : "");
      std::printf("\n");
    }
    CHECK(ok, "inline '%s'", c.src);
  }
}

static void blockTable() {
  const double s = 12;
  const auto lines = parseMarkdown(S("# Title\nbody\n\n## Sub\n- one\n  - nested\n- [x] done\n- [ ] todo\n1. first\n2) second\n> quote\n---\n```\ncode *x*\n```\nend"), s);
  CHECK(lines.size() == 14, "line count %d", int(lines.size()));
  if (lines.size() != 14) return;
  CHECK(std::abs(lines[0].size - 2 * s) < 1e-9 && lines[0].runs.size() == 1 && lines[0].runs[0].bold && lines[0].gap == 0, "h1");
  CHECK(std::abs(lines[3].size - 1.6 * s) < 1e-9 && lines[3].gap > 0, "h2 has space above");
  CHECK(lines[2].runs.empty() && lines[2].gap < 0, "blank line is a negative gap");
  CHECK(lines[4].hasMarker && lines[4].marker.text == QString(QChar(0x2022)) && std::abs(lines[4].x - 1.3 * s) < 1e-9, "bullet");
  CHECK(lines[5].marker.text == QString(QChar(0x25E6)) && std::abs(lines[5].x - 2 * 1.3 * s) < 1e-9, "nested bullet");
  CHECK(lines[6].marker.text == QString(QChar(0x2611)) && lines[7].marker.text == QString(QChar(0x2610)), "tasks");
  CHECK(lines[8].marker.text == "1." && lines[9].marker.text == "2.", "numbered");
  CHECK(lines[10].quote && std::abs(lines[10].x - 0.78 * s) < 1e-9, "quote");
  CHECK(lines[11].rule, "rule");
  CHECK(lines[12].code && lines[12].runs.size() == 1 && lines[12].runs[0].mono && lines[12].runs[0].text == "code *x*" && std::abs(lines[12].size - 0.92 * s) < 1e-9, "code block");
  CHECK(!lines[13].code && lines[13].runs.size() == 1 && lines[13].runs[0].text == "end", "after the fence");
  CHECK(parseMarkdown(S("a\r\nb"), s).size() == 2, "CRLF");
}

static void layoutAndDraw() {
  // text without markup measures exactly as the editor measures plain text
  for (const char* txt : {"Hola mundo", "una linea\nsegunda linea mas larga\ntercera", "x"}) {
    for (double size : {12.0, 17.5, 9.0}) {
      Text t;
      t.text = S(txt);
      t.attrs = {{"font", "Sans"}, {"size", QString::number(size)}, {"x", "0"}, {"y", "0"}, {"color", "#000000ff"}};
      const auto bb = textBBox(t);
      const QSizeF sz = markdownSize(S(txt), size, "Sans");
      CHECK(std::abs(sz.width() - (bb[2] - bb[0])) < 1e-6 && std::abs(sz.height() - (bb[3] - bb[1])) < 1e-6, "'%s' %g: markdown %gx%g vs textBBox %gx%g", txt, size, sz.width(), sz.height(), bb[2] - bb[0], bb[3] - bb[1]);
    }
  }
  const double s = 12;
  const MdLayout lay = layoutMarkdown(S("# Big\ntext\n\nmore"), s, "Sans");
  CHECK(lay.lines.size() == 4, "lines");
  CHECK(lay.lines[1].y > lay.lines[0].y + lay.lines[0].h - 1e-9, "lines stack");
  CHECK(std::abs(lay.lines[3].y - (lay.lines[2].y + lay.lines[2].h)) < 1e-9 && std::abs(lay.lines[2].h - (s * 1.2 - s * 0.55)) < 1e-9, "blank line is half a line");
  CHECK(lay.lines[0].width > lay.lines[1].width, "heading wider than body text");
  CHECK(layoutMarkdown(S("**bold**"), s, "Sans").width > layoutMarkdown(S("bold"), s, "Sans").width * 0.9, "bold width");
  CHECK(markdownSize(S(""), s, "Sans").width() >= s && markdownSize(S(""), s, "Sans").height() >= s * 1.2, "empty box has a minimum size");
  // the same call twice is served by the cache and gives the same answer
  CHECK(markdownSize(S("# Big\ntext"), s, "Sans") == markdownSize(S("# Big\ntext"), s, "Sans"), "cache");

  // drawing: ink lands inside the measured box (plus the code padding) and a rule, a quote bar and strike-through leave marks
  const QString src = S("# Titulo\nplain **bold** *it* ~~old~~ `mono`\n> quoted\n---\n- item\n```\ncode\n```");
  const QSizeF sz = markdownSize(src, s, "Sans");
  QImage img(int(std::ceil(sz.width() + 60)), int(std::ceil(sz.height() + 40)), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  {
    QPainter p(&img);
    drawMarkdown(p, src, s, "Sans", QPointF(20, 20), Qt::black);
  }
  int minX = img.width(), minY = img.height(), maxX = -1, maxY = -1;
  for (int y = 0; y < img.height(); y++)
    for (int x = 0; x < img.width(); x++)
      if (qGray(img.pixel(x, y)) < 200) { minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y); }
  CHECK(maxX >= 0, "something was drawn");
  CHECK(minX >= 20 - s * 0.2 - 1 && minY >= 20 - 2 && maxX <= 20 + sz.width() + s * 0.2 + 2 && maxY <= 20 + sz.height() + 3, "ink %d..%d x %d..%d outside the box %gx%g", minX, maxX, minY, maxY, sz.width(), sz.height());
  CHECK(maxY > 20 + sz.height() * 0.6, "the last lines are drawn near the bottom");
  CHECK(isMarkdown(Attrs{{"xlMd", "1"}}) && !isMarkdown(Attrs{{"xlMd", "0"}}) && !isMarkdown(Attrs{}), "isMarkdown");
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  inlineTable();
  blockTable();
  layoutAndDraw();
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("markdown: all checks passed\n");
  return 0;
}
