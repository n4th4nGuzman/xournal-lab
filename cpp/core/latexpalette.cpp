#include "latexpalette.h"
#include <QSet>
#include <QStringList>

std::vector<PaletteCategory> buildPaletteCategories();   // latexpalette_data.cpp (generated)

const std::vector<PaletteCategory>& paletteCategories() {
  static const std::vector<PaletteCategory> v = buildPaletteCategories();
  return v;
}

// Same normalisation as the TypeScript: decompose, drop combining marks, lower-case.
static QString norm(const QString& s) {
  const QString d = s.normalized(QString::NormalizationForm_D);
  QString out;
  for (const QChar c : d)
    if (!(c.unicode() >= 0x300 && c.unicode() <= 0x36F)) out += c;
  return out.toLower();
}

std::vector<PaletteItem> searchPalette(const QString& query) {
  const QStringList words = norm(query).split(QChar(' '), Qt::SkipEmptyParts);
  std::vector<PaletteItem> out;
  if (words.isEmpty()) return out;
  QSet<QString> seen;
  for (const PaletteCategory& cat : paletteCategories())
    for (const PaletteItem& it : cat.items) {
      const QString hay = norm(it.name + ' ' + it.snippet + ' ' + it.tex + ' ' + cat.title);
      bool all = true;
      for (const QString& w : words)
        if (!hay.contains(w)) { all = false; break; }
      if (all && !seen.contains(it.snippet)) { seen.insert(it.snippet); out.push_back(it); }
    }
  return out;
}

SnippetInsert snippetInsert(const QString& snippet) {
  const int caret = snippet.indexOf(kPaletteCaret);
  SnippetInsert r;
  r.text = snippet;
  r.text.remove(kPaletteCaret);
  r.caretBack = caret < 0 ? 0 : int(r.text.size()) - caret;
  return r;
}
