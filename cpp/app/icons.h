// Toolbar glyphs drawn with QPainter (no image files, so they scale to any display and need no Qt SVG plugin).
// Icons::make("pen") for the built-in names; any other name gives a rounded square with its first letter, so a new tool has an icon from day one.
#pragma once
#include <QIcon>
#include <QString>

namespace Icons {
QIcon make(const QString& name, const QString& fallbackLetter = QString());
}
