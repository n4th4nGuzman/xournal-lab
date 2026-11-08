// Markdown in a text box. The box keeps the markdown source as its text (so it can always be edited, and other programs see the plain text); this
// module turns the source into lines of styled runs, which the page measures and draws. Port of app/src/renderer/markdown.ts.
// Supported: headings (# to ######), **bold**, *italic*, ~~strike~~, `code`, fenced code blocks, bullet / numbered / task lists (nested by
// indentation), > quotes, --- rules and [links](url). Lines are not wrapped: a paragraph is as wide as it was typed.
// Measuring is compatible with genobj.cpp's textBBox: a line is as wide as QFontMetricsF says at the rounded pixel size, scaled to the real size,
// and lines are `size * 1.2` tall, so a markdown box with no markup has the same size as a plain one.
#pragma once
#include "model.h"
#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <array>
#include <vector>

struct MdRun {
  QString text;
  bool bold = false, italic = false, strike = false, underline = false, mono = false, link = false;
};

struct MdMarker { QString text; double x = 0; };

struct MdLine {
  std::vector<MdRun> runs;
  double size = 12;          // font size of the line, in page units
  double x = 0;              // where the text starts (list items and quotes are indented)
  bool hasMarker = false;
  MdMarker marker;           // the bullet / number / checkbox before the text
  bool quote = false, code = false, rule = false;
  double gap = 0;            // space before the line; negative for a blank line, which is half a line tall
};

/** Inline markup of one line into runs. An opening mark with no closing one stays as typed. */
std::vector<MdRun> inlineRuns(const QString& src, const MdRun& base = MdRun());
/** The lines of a markdown source (unmeasured). */
std::vector<MdLine> parseMarkdown(const QString& src, double size);
/** Text box flagged as markdown by attribute xlMd="1". */
bool isMarkdown(const Attrs& attrs);
inline bool isMarkdown(const Text& t) { return isMarkdown(t.attrs); }

struct MdLayoutLine {
  MdLine line;
  double y = 0, h = 0;               // top of the line inside the box, and its height
  std::vector<double> widths;        // width of every run
  double width = 0;
};
struct MdLayout {
  std::vector<MdLayoutLine> lines;
  double width = 0, height = 0;
};

/** Lines placed one under the other with measured widths. `font` is the Xournal++ family name ("Sans" = the UI font). Cached by (size, font, text). */
MdLayout layoutMarkdown(const QString& text, double size, const QString& font);
/** Draw with the box's top left corner at `topLeft`. */
void drawMarkdown(QPainter& p, const QString& text, double size, const QString& font, QPointF topLeft, const QColor& color);
/** Size of the box: width x height in page units. */
QSizeF markdownSize(const QString& text, double size, const QString& font);
/** Boxes (x0, y0, x1, y1, relative to the box's top-left corner) of every occurrence of `needle` in the rendered text of a markdown source, one per line and
 *  run it touches. The markup characters are not in the rendered text, so a needle that crosses markup is not found: empty then (the caller marks the whole box). */
std::vector<std::array<double, 4>> markdownSpanBoxes(const QString& text, double size, const QString& font, const QString& needle);

