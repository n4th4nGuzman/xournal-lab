// The toolbar icons of the Electron app (renderer/icons.ts: 24x24 line drawings), drawn by a small SVG subset renderer (path, circle, rect, g/transform,
// stroke-dasharray, opacity, fill) because the build has no Qt SVG module. An icon is a QIconEngine, so it is painted at the size and device pixel ratio
// the button asks for, in the colour of its state (normal / checked / disabled), with the neon halo when the theme wants one.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QString>

namespace chrome {

struct SvgLook {
  QColor ink{0xd8, 0xd8, 0xd8};          // normal state
  QColor activeInk{Qt::white};           // checked / pressed
  QColor buttonBg{0x26, 0x26, 0x26};     // what var(--btn-bg) is: the "front" and "back" icons fill their top square with it
  QColor activeBg{0x2f, 0x6f, 0xd0};
  double strokeWidth = 1.7;              // in 24-unit viewBox units (1.9 in neon)
  bool halo = false;                     // drop-shadow(0 0 3px currentColor)
  double disabledOpacity = 0.35;
};

/** True when the Electron icon set has a drawing with this name. */
bool hasSvgIcon(const QString& name);
/** The SVG body (inside <svg>) of an icon, empty when unknown. */
QString svgIconBody(const QString& name);
/** An icon that paints `name` for any size; nothing is rasterised until a button asks. */
QIcon svgIcon(const QString& name, const SvgLook& look);
/** Paints the drawing into `target` (24-unit viewBox scaled to the rectangle) with `ink`. Used by the engine and by the welcome page. */
void paintSvgIcon(QPainter& p, const QString& name, const QRectF& target, const QColor& ink, const QColor& buttonBg, double strokeWidth);
/** The Electron icon name of a command ("file.open" -> "open", "tool.pen" -> "pen"); empty when the command has no Electron icon. */
QString svgNameForAction(const QString& actionId);
/** What the painted-on-demand icons that are not drawings (colour swatches, size dots) need to know about the theme; set by Chrome::setTheme. */
void setSvgTheme(bool neon, bool halo);
bool svgThemeNeon();
/** A colour swatch of the toolbar (a 30 px circle in a 34 px button, as the Electron CSS), white ring when checked: painted at the centre of `r`. */
void paintSwatch(QPainter& p, const QRectF& r, const QColor& colour, bool checked, bool enabled);
/** The custom-colour button (<input type=color>: a 26 x 24 rounded rectangle in the colour) painted at the centre of `r`. */
void paintColourBox(QPainter& p, const QRectF& r, const QColor& colour);
/** The same drawings as small icons (tooltips, tests); the toolbar buttons draw them through XlStyle because a button never paints an icon bigger than its icon size. */
QIcon swatchIcon(const QColor& colour);
QIcon colourBoxIcon(const QColor& colour);
/** A size preset: a dot `px` wide (Electron's dotSize) in the ink colour, white when checked. */
QIcon sizeDotIcon(int px);
/** A widget style sheet written with the classic blue accents (#2f6fd0, #4a8af0, #6aa0ff) with the neon pink in their place while the neon theme is on. */
QString neonizeCss(QString css);
/** The neon colour of an icon by function (styles.css: open/save yellow, undo orange, cut/copy green...). */
QColor neonInkFor(const QString& svgName);

}  // namespace chrome
