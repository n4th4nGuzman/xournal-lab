// Undoable layer operations. The document part is core/docfactory.h; the per-page view state (which layers are hidden, which one is active) lives in the
// Editor by layer INDEX, so every step carries that state before and after: indices shift when a layer is inserted, removed or moved.
#include "chrome.h"
#include "docfactory.h"

namespace chrome::layerops {
namespace {
struct ViewState { std::vector<int> hidden; int active = 0; };

ViewState capture(Editor& ed, int page) { return {ed.hiddenLayers(page), ed.activeLayer(page)}; }

void restore(Editor& ed, int page, const ViewState& s) {
  for (int i : ed.hiddenLayers(page)) ed.setLayerVisible(page, i, true);
  for (int i : s.hidden) ed.setLayerVisible(page, i, false);
  ed.setActiveLayer(page, s.active);
}

/** Positions of the elements of the changed layers moved: the spatial index and the tiles of the page are stale. */
void refresh(Editor& ed, int page) {
  ed.invalidateIndex(page);
  ed.touch(page);
}

std::vector<int> shifted(const std::vector<int>& hidden, const std::function<int(int)>& f) {
  std::vector<int> out;
  for (int h : hidden) { const int j = f(h); if (j >= 0) out.push_back(j); }
  return out;
}

/** One undo step: `doIt` and `undoIt` change the document only; the view state is set from the snapshots. */
void push(Editor& ed, int page, const QString& label, ViewState before, ViewState after, std::function<void()> doIt, std::function<void()> undoIt) {
  Action a;
  a.label = label;
  a.page = page;
  a.undo = [&ed, page, before, undoIt = std::move(undoIt)] { undoIt(); refresh(ed, page); restore(ed, page, before); };
  a.redo = [&ed, page, after, doIt] { doIt(); refresh(ed, page); restore(ed, page, after); };
  ed.pushAction(std::move(a));
}
}  // namespace

int add(Editor& ed, int page) {
  if (page < 0 || page >= ed.pageCount()) return -1;
  const ViewState before = capture(ed, page);
  const int idx = addLayer(ed.doc(), page, before.active);
  ViewState after;
  after.hidden = shifted(before.hidden, [idx](int h) { return h >= idx ? h + 1 : h; });
  after.active = idx;
  refresh(ed, page);
  restore(ed, page, after);
  push(ed, page, QStringLiteral("Nueva capa"), before, after,
       [&ed, page, idx] { insertLayerAt(ed.doc(), page, idx, Layer{}); },
       [&ed, page, idx] { removeLayer(ed.doc(), page, idx); });
  return idx;
}

bool remove(Editor& ed, int page, int layer) {
  if (page < 0 || page >= ed.pageCount()) return false;
  const int n = int(ed.doc().pages[size_t(page)].layers.size());
  if (layer < 0 || layer >= n) return false;
  if (n <= 1) { ed.notify(QStringLiteral("Una página necesita al menos una capa")); return false; }
  const ViewState before = capture(ed, page);
  ed.selection().clear();
  auto removed = removeLayer(ed.doc(), page, layer);
  if (!removed) return false;
  ViewState after;
  after.hidden = shifted(before.hidden, [layer](int h) { return h == layer ? -1 : h > layer ? h - 1 : h; });
  after.active = before.active == layer ? n - 2 : before.active > layer ? before.active - 1 : before.active;   // the top layer, as the TS (no active = the top one)
  refresh(ed, page);
  restore(ed, page, after);
  auto held = std::make_shared<Layer>(std::move(removed->layer));
  push(ed, page, QStringLiteral("Eliminar capa"), before, after,
       [&ed, page, layer, held] { auto r = removeLayer(ed.doc(), page, layer); if (r) *held = std::move(r->layer); },
       [&ed, page, layer, held] { insertLayerAt(ed.doc(), page, layer, *held); });
  return true;
}

bool rename(Editor& ed, int page, int layer, const QString& name) {
  if (page < 0 || page >= ed.pageCount()) return false;
  const RenameResult r = renameLayer(ed.doc(), page, layer, name);
  if (!r.changed) return false;
  const QString clean = name.trimmed();
  const std::optional<QString> before = r.before;
  Action a;
  a.label = QStringLiteral("Renombrar capa");
  a.page = page;
  a.undo = [&ed, page, layer, before] { restoreLayerName(ed.doc(), page, layer, before); emit ed.layersChanged(page); };
  a.redo = [&ed, page, layer, clean] { renameLayer(ed.doc(), page, layer, clean); emit ed.layersChanged(page); };
  ed.pushAction(std::move(a));
  emit ed.layersChanged(page);
  return true;
}

bool move(Editor& ed, int page, int layer, int delta) {
  if (page < 0 || page >= ed.pageCount()) return false;
  const int to = layer + delta;
  const ViewState before = capture(ed, page);
  if (!moveLayer(ed.doc(), page, layer, delta)) return false;
  auto swapIdx = [layer, to](int i) { return i == layer ? to : i == to ? layer : i; };
  ViewState after;
  after.hidden = shifted(before.hidden, swapIdx);
  after.active = swapIdx(before.active);
  refresh(ed, page);
  restore(ed, page, after);
  push(ed, page, QStringLiteral("Mover capa"), before, after,
       [&ed, page, layer, delta] { moveLayer(ed.doc(), page, layer, delta); },
       [&ed, page, layer, delta] { moveLayer(ed.doc(), page, layer + delta, -delta); });
  return true;
}

}  // namespace chrome::layerops
