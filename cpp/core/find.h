// Search over the text boxes and LaTeX formulas of a document. Port of the logic of app/src/renderer/find.ts (the bar and the stepping are the
// window's business; `FindState` keeps the stepping so both stay trivial).
#pragma once
#include "model.h"
#include <array>
#include <functional>
#include <optional>
#include <vector>

struct FindHit {
  int page = 0, layer = 0, element = 0;     // indices into the document
  std::vector<int> positions;               // start of every non-overlapping match inside the element's text (UTF-16 offsets, as QString counts)
  int length = 0;                           // length of the needle in the text (what a highlight covers)
};

struct FindOptions {
  bool matchCase = false;
  /** Layers to leave out (the editor's hidden layers); null searches all. */
  std::function<bool(int page, int layer)> skipLayer;
};

/** The searchable text of an element: a text box's text, or the LaTeX source of a teximage; nullopt for strokes and pictures. */
std::optional<QString> searchableText(const Element& e);

/** Every element that contains `query`, in page, layer and element order (an empty query finds nothing). */
std::vector<FindHit> findText(const Document& doc, const QString& query, const FindOptions& opt = {});

/** Box of the hit's element (x0, y0, x1, y1) for scrolling it into view, or nullopt when it has none. */
std::optional<std::array<double, 4>> findHitBox(const Document& doc, const FindHit& h);

/** The exact boxes of every match inside a plain text hit (one per line touched, page units): what a highlight should cover. Empty for formulas and markdown texts,
 *  whose source offsets do not map onto glyphs: the caller then marks the whole box (findHitBox). */
std::vector<std::array<double, 4>> findSpanBoxes(const Document& doc, const FindHit& h);

/** The current hit and stepping with wrap-around ("n de m"). */
struct FindState {
  std::vector<FindHit> hits;
  int index = -1;
  void run(const Document& doc, const QString& query, const FindOptions& opt = {});   // collects and goes to the first
  const FindHit* step(int delta);                                                     // +1 or -1; null when there are no hits
  const FindHit* current() const { return index >= 0 && index < int(hits.size()) ? &hits[size_t(index)] : nullptr; }
};
