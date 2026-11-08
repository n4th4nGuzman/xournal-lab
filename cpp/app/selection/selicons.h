// Toolbar icons of the selection area (cut, copy, paste, duplicate, front, back, rotate, mirror, lasso, vertical space): the drawings of app/src/renderer/icons.ts
// redrawn with QPainter on the same 24-unit grid (Qt Svg is not part of this build).
#pragma once
#include <QIcon>
#include <QString>

namespace selx {
/** The icon for one of: cut copy paste dup front back rotL rotR flipH flipV lasso vspace; a null icon for any other name. */
QIcon selIcon(const QString& name);
}  // namespace selx
