// What a formula (<teximage>) looks like on the canvas. The file keeps a one-page vector PDF that Qt Gui cannot rasterise, so the canvas draws the
// formula again from its TeX source (the `text` attribute) with core/mathlayout, in the ink colour found inside the PDF, scaled to the box the file
// gives it (so a formula saved by the Electron app occupies exactly the same place). The bitmaps are cached by the content of the element.
#pragma once
#include "model.h"
#include <QColor>
#include <QImage>

/** Bitmap of a teximage at about 4 pixels per point, or a null image when it cannot be laid out (no source, parse error, no GUI application). */
QImage texBitmap(const Image& im);
/** Ink colour of the formula's PDF (first fill colour of its page); black when the PDF is not readable. */
QColor texInkColor(const QString& pdfBase64);
/** Drops the cached bitmaps. */
void clearTexCache();
