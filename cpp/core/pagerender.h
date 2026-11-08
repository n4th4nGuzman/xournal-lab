// Drawing a page to a QPainter: background (solid colour, paper pattern, picture, PDF page), then the layers' strokes, texts and images.
// Used by the canvas (per tile), by PDF/PNG export and by thumbnails. Port of app/src/renderer/render.ts (+ the paper drawing of paper.ts).
// Contract (other code only calls this header):
#pragma once
#include "model.h"
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <functional>
#include <atomic>
#include <array>

struct RenderOptions {
  QRectF clip;                 // page coordinates; elements outside are skipped (empty = everything)
  double scale = 1.0;          // device pixels per page unit, so line widths and glow blur can be chosen for what is on screen
  bool lite = false;           // draw fast and rough (zooming or scrolling); finished frames use lite = false
  bool cullPieces = false;     // tiles: pieces of a stroke that cannot touch `clip` are not drawn (a long stroke costs what is visible of it); equal to the full draw up to a few pixels' antialiasing
  bool glow = true;            // neon halos of strokes that have the glow attributes
  bool background = true;      // false when the caller draws the background itself or wants ink only (export without background, fill masks)
  bool darkPaper = false;      // theme where the "white" page is dark: the pen colour black/white swap is NOT done, only the paper colour changes
  QString paperColorOverride;  // "#rrggbb" or empty: the paper colour of the theme when the page background is the default white
};

/** Page background only. A PDF/picture background is drawn from `bgImage` (the caller rasterises and caches it at the right size), if given. */
void renderBackground(QPainter& p, const Page& page, const RenderOptions& o, const QImage* bgImage = nullptr);
/** One element. Images are decoded once and cached by the element's data (the cache is bounded). */
void renderElement(QPainter& p, const Element& e, const RenderOptions& o);
/** Every visible layer in order. `hiddenLayers` are layer indices not to draw. */
void renderLayers(QPainter& p, const Page& page, const RenderOptions& o, const std::vector<int>& hiddenLayers = {});
/** Draws exactly these elements, in the order given (the caller passes z order: layer, then position), skipping those whose render bounds miss o.clip.
 *  This is renderLayers without the scan of every element: the caller got the candidates from a spatial index. `cancel` (optional) is polled between
 *  elements and stops the drawing early when set (the result is then incomplete and must be dropped). Safe to call from a worker thread. */
void renderElementSeq(QPainter& p, const RenderOptions& o, const std::vector<const Element*>& els, const std::atomic<bool>* cancel = nullptr);
/** Background + layers: a whole page. */
void renderPage(QPainter& p, const Page& page, const RenderOptions& o, const QImage* bgImage = nullptr);

/** Stroke width in page units at segment i (i = 0 is the first segment), from the width list as Xournal++ writes it. */
double strokeWidthAt(const Stroke& s, size_t segment);
/** QColor of an attribute value "#rrggbb" or "#rrggbbaa" (alpha last, as Xournal++ writes it). */
QColor colorOf(const QString& hex);

// --- additions (add-only) ---
/** Page-unit rectangle an element can touch (strokes padded by width and glow reach, texts by their box); used to skip elements outside a clip. */
QRectF elementRenderBounds(const Element& e);
/** Drops the cached decoded images and glow bitmaps (a document was closed). */
void clearRenderCaches();

/** Turns the bytes of a PDF (a formula saved as vector PDF in a <teximage>) into a transparent picture; null when it cannot. Called on the rendering thread. */
using PdfImageDecoder = QImage (*)(const QByteArray& pdf, int widthPx);   // widthPx <= 0: the default size
/** Installs the rasteriser for `<teximage>` PDFs (app/pdf does it with rasterizePdfFormula). Without one a dashed placeholder is drawn. Drops the image cache. */
void setPdfImageDecoder(PdfImageDecoder decoder);

/**
 * Supplies the picture of a non-solid page background (PDF page, image) when the caller of renderBackground gave none: called with the page's <background>
 * attributes, its width and height in points and the drawing scale (device pixels per point). It runs on any thread that renders (tile workers too) and
 * must not block: return what is ready (any size) or a null image after starting the work. app/pdf installs it. Not called when `bgImage` is given.
 */
using BackgroundImageProvider = std::function<QImage(const Attrs& background, double widthPt, double heightPt, double scale)>;
void setBackgroundImageProvider(BackgroundImageProvider provider);

/**
 * Page-unit boxes (x0, y0, x1, y1), one per line touched, of the characters [pos, pos+len) of a plain text box, laid out exactly as the renderer draws it.
 * Empty when the span cannot be placed (markdown texts have their own layout and no one-to-one mapping from source offsets; out-of-range spans).
 */
std::vector<std::array<double, 4>> textSpanBoxes(const Text& t, int pos, int len);
