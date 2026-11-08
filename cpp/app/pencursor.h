// The pen / highlighter cursor that shows the tool: a disc as wide as the stroke it will draw (at the current zoom) in the tool colour, with a contrasting outline.
#pragma once
#include <QColor>
#include <QCursor>

namespace pencursor {
constexpr int kMinDiameter = 5;     // logical pixels: a hairline pen still shows where it is
constexpr int kMaxDiameter = 96;    // operating systems limit the size of a cursor
/** The diameter the disc gets for a stroke `width` page units wide at `zoom` (logical pixels). */
int diameterFor(double width, double zoom);
/** Disc of `diameter` logical pixels filled with `fill` (a translucent fill for a highlighter), outlined in black or white, whichever contrasts, with a faint outer ring
 *  of the other so it shows on any paper. `dpr` makes the bitmap sharp on a scaled screen; the hot spot is the centre. The last cursor made is reused when asked again. */
QCursor disc(const QColor& fill, int diameter, double dpr);
}
