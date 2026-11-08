// Colour presets for the pen. Port of app/src/renderer/palettes.ts. The first one is the original Xournal++ set.
#pragma once
#include <QString>
#include <QStringList>
#include <vector>

struct Palette {
  QString id, name;
  QStringList colors;
  QString paper;          // a paper colour the preset was designed for (only suggested, never applied on its own); null when none
  QStringList papers;     // paper colours that go well with these inks
};

const std::vector<Palette>& palettes();
extern const char* const kDefaultPalette;     // "classic"
/** The palette with that id, or the first one. */
const Palette& paletteById(const QString& id);
