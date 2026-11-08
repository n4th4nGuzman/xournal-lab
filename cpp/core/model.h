// The document as Xournal++ keeps it: pages of layers of strokes, texts and images. Every attribute of the file is kept in order, so what we do not
// interpret (our own xlPlane/xlRole/xlSet/xlFig, or anything a newer Xournal++ writes) survives a load and save.
#pragma once
#include <QString>
#include <utility>
#include <variant>
#include <vector>

using Attrs = std::vector<std::pair<QString, QString>>;

/** First value of the attribute, or null when absent (an empty attribute is not the same as a missing one). */
const QString* attrFind(const Attrs& a, const QString& name);
QString attrOr(const Attrs& a, const QString& name, const QString& fallback = QString());
void attrSet(Attrs& a, const QString& name, const QString& value);

struct Stroke {
  Attrs attrs;                  // tool, color, width (first value of the list), capStyle, fill, and the custom ones
  std::vector<double> widths;   // the width attribute: the base width, then one per segment when the pen had pressure
  std::vector<double> pts;      // x0, y0, x1, y1, ...
  QString tool() const { return attrOr(attrs, "tool", "pen"); }
  QString color() const { return attrOr(attrs, "color", "#000000ff"); }
  size_t pointCount() const { return pts.size() / 2; }
};

struct Text {
  Attrs attrs;                  // font, size, x, y, color, ...
  QString text;
};

/** <image> and <teximage>: the picture is a base64 PNG kept as written (decoded only when drawn). */
struct Image {
  QString tag;
  Attrs attrs;                  // left, top, right, bottom, (text for teximage)
  QString data;
};

using Element = std::variant<Stroke, Text, Image>;

/**
 * An XML element this version does not interpret (audio timestamps, whatever a newer Xournal++ adds), kept as read so saving does not lose it.
 * Text is a node of its own (isText) so mixed content keeps its order.
 */
struct RawNode {
  bool isText = false;
  QString name;                 // element name as written (with its prefix, if any); empty for text
  QString text;                 // the characters, for a text node
  Attrs attrs;
  std::vector<RawNode> kids;
  bool operator==(const RawNode&) const = default;
};
/** A raw node and where it sat: `before` = how many known children (elements of a layer, layers of a page, pages of the document) preceded it. */
struct RawAt {
  size_t before = 0;
  RawNode node;
  bool operator==(const RawAt&) const = default;
};

struct Layer {
  Attrs attrs;                  // name
  std::vector<Element> elements;
  std::vector<RawAt> extra;     // unknown children (e.g. <timestamp>, <audio>), written back at their position
};

/** Largest page side the app lays out, in points (200 in, the PDF limit): bigger values in a file are drawn at this size, the attribute is kept. */
inline constexpr double kMaxPageSize = 14400;

struct Page {
  Attrs attrs;                  // width, height
  Attrs background;             // type, color, style, and for pdf pages the file and page number
  std::vector<Layer> layers;
  std::vector<RawAt> extra;     // unknown children other than <background> and <layer>
  double width() const;
  double height() const;
};

struct Document {
  Attrs rootAttrs;              // creator, fileversion
  QString title;
  QString preview;              // base64 PNG of the first page, passed through
  std::vector<Page> pages;
  std::vector<RawAt> extra;     // unknown children of <xournal> other than <title>, <preview> and <page>
};

bool operator==(const Stroke&, const Stroke&);
bool operator==(const Text&, const Text&);
bool operator==(const Image&, const Image&);
bool operator==(const Layer&, const Layer&);
bool operator==(const Page&, const Page&);
bool operator==(const Document&, const Document&);

/** What a document holds, for reports and checks. */
struct Counts { size_t pages = 0, layers = 0, strokes = 0, texts = 0, images = 0, points = 0; };
Counts countOf(const Document&);
