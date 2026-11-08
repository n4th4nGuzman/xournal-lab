// TeX-subset formula layout and drawing without KaTeX or an installed LaTeX: the formula is parsed, laid out with the TeXbook's rules (appendix G:
// styles, fractions on the math axis, script shifts, big-operator limits, stretchy delimiters) and turned into vector shapes. Glyph outlines come
// from the system's math font (Cambria Math on Windows, with fallbacks), so the result is a list of filled/stroked paths that draws identically to a
// window, to a PNG and to a PDF. Requires a QGuiApplication (font database). Units are points: one em equals `sizePt`.
//
// Interchange with the Electron app: a formula in a .xopp is `<teximage text="TEX" left top right bottom>BASE64-PDF</teximage>` (the picture is a
// one-page vector PDF, not a PNG). makeTexImage writes exactly that, so the Electron app re-opens and edits the formula from the `text` attribute.
#pragma once
#include "model.h"
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <memory>
#include <vector>

/** One drawing primitive of a laid-out formula: a path scaled by `scale` and moved by (tx, ty), filled or stroked. y grows downwards from the baseline. */
struct MathPrim {
  std::shared_ptr<const QPainterPath> path;
  double scale = 1, tx = 0, ty = 0;
  bool stroke = false;
  double lineWidth = 0;          // stroke only, in the final units
  QColor color;                  // valid when the formula asked for its own colour (\color); otherwise the colour given when drawing
};

struct MathBox {
  bool ok = false;
  bool empty = false;            // blank source: nothing to draw (and ok is true)
  QString error;                 // Spanish message when !ok, with the position in the source when known
  int errorPos = -1;             // index into the source of the offending token, or -1
  double width = 0;              // the box includes a small margin and every bit of ink, so nothing is clipped
  double ascent = 0, descent = 0; // above / below the baseline; the total height is ascent + descent
  double sizePt = 0;
  bool display = true;
  std::vector<MathPrim> prims;   // x from 0 to width, y from -ascent to descent
  double height() const { return ascent + descent; }
};

/** Parse and lay out `tex`. Never throws: a parse error gives ok=false and a Spanish message (shown by the live preview). */
MathBox layoutMath(const QString& tex, double sizePt, bool display);
/** Same formula scaled so that the whole box is `heightPt` tall (to show a formula saved by another program in the box it occupies). */
MathBox layoutMathToHeight(const QString& tex, bool display, double heightPt);

/** Draw with the baseline's left end at `origin` (page units). Shapes without their own colour use `color`. */
void drawMath(QPainter& p, const MathBox& box, QPointF origin, const QColor& color);

struct MathImageInfo {
  double widthPt = 0, heightPt = 0;  // the formula's box in points (what goes in right-left / bottom-top)
  double baselinePt = 0;             // distance from the top of the picture to the baseline
  int widthPx = 0, heightPx = 0;
};
/** A transparent picture of the formula, `pxPerPt` pixels per point (the Electron app uses 4, at most 2048 wide). */
QImage renderMathImage(const MathBox& box, const QColor& color, double pxPerPt, MathImageInfo* info = nullptr);
/** A one-page vector PDF exactly as large as the box, in points, ink in `color` over nothing (as the Electron app expects). */
QByteArray renderMathPdf(const MathBox& box, const QColor& color);

struct TexImageResult {
  bool ok = false;
  QString error;
  Image el;                      // tag "teximage"; attrs in the order text, left, top, right, bottom; data = base64 PDF
  QImage bitmap;                 // preview of the same formula for the canvas (4 px per point)
  MathBox box;
};
/** Everything the formula tool does on "Insertar": layout, PDF, the element and a bitmap. `left`/`top` are the box's corner in page units. */
TexImageResult makeTexImage(const QString& tex, double sizePt, bool display, const QColor& color, double left, double top);
