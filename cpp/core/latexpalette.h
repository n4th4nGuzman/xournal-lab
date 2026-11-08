// Symbols and structures offered by the formula dialog, as plain data (no widgets). Port of app/src/renderer/latexpalette.ts; the table itself is
// generated from that file (tools/gen_latexpalette.mjs) so both apps offer the same buttons.
#pragma once
#include <QString>
#include <vector>

struct PaletteItem {
  QString tex;       // LaTeX rendered as the button label
  QString snippet;   // inserted at the caret; contains kPaletteCaret where the caret goes afterwards (never inserted)
  QString name;      // Spanish name, also what the search looks at (together with the code)
};

struct PaletteCategory {
  QString id;        // "struct", "calc", "vcalc", "matrix", "greek", "ops", "rel", "arrows", "sets", "accents", "phys", "space"
  QString title;
  std::vector<PaletteItem> items;
};

/** The caret marker of snippets (U+00A6). */
inline const QChar kPaletteCaret = QChar(0x00A6);

const std::vector<PaletteCategory>& paletteCategories();
/** Items whose name or code contains every word of `query` (accents and case ignored), each snippet once, in category order. */
std::vector<PaletteItem> searchPalette(const QString& query);
/** The snippet without the caret marker, and the caret position counted back from its end (0 = at the end). */
struct SnippetInsert { QString text; int caretBack = 0; };
SnippetInsert snippetInsert(const QString& snippet);
