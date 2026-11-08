// Blank documents and pages, paper styles and sizes, and the page and layer operations of the Journal and Layers classes of the Electron app
// (journal.ts, layers.ts) as pure functions on a Document. None of them knows about undo: each returns what the application's own undo journal needs
// to reverse it (the removed page, the previous name, the index it landed on). The model has no "hidden" or "active layer" state; that is view
// state of the application.
#pragma once
#include "model.h"
#include <optional>
#include <vector>

struct PaperStyle { QString style, label; };
const std::vector<PaperStyle>& paperStyles();         // plain, lined (red margin), ruled, graph, dotted

struct PaperFormat { QString id; double width, height; QString label; };   // points (1 pt = 1/72 in)
const std::vector<PaperFormat>& paperFormats();       // A3, A4, A5, Letter, Legal
const PaperFormat* paperFormat(const QString& id);
constexpr double kMmToPt = 72.0 / 25.4;

/** A new empty page with the size and paper of `like` (PDF and image backgrounds fall back to plain white). One empty layer. */
Page blankPageLike(const Page& like);
/** One empty A4 page, written as Xournal++ 1.3.8 does. */
Document blankDocument();

// ---- pages
/** Insert an empty page at `index` (clamped to 0..pages), shaped like page `likeIndex` (-1 or out of range: the neighbour). Returns the index it landed on. */
int insertPage(Document& doc, int index, int likeIndex = -1);
int newPageAfter(Document& doc, int current);
int insertPageBefore(Document& doc, int current);
int appendPage(Document& doc);
/** Put a page (made elsewhere, or one that was removed) at `index`, clamped. Returns the index. */
int insertPageAt(Document& doc, int index, Page page);

struct RemovedPage { Page page; int index = 0; int focus = 0; };
/** Remove a page and keep it for the undo. The last remaining page cannot be deleted (nullopt). `focus` is the page to show afterwards. */
std::optional<RemovedPage> deletePage(Document& doc, int index);
/** Swap a page with its neighbour (delta -1 or +1). Returns its new index (the same when it cannot move). Undo: movePage(to, -delta). */
int movePage(Document& doc, int index, int delta);

// ---- layers
/** "Capa N" counted from the bottom, unless the layer has a saved name. */
QString layerName(const Layer& layer, int index);
/** A new empty layer right above layer `above` (the editor passes its active layer; -1 puts it at the bottom). Returns its index. */
int addLayer(Document& doc, int page, int above);
int insertLayerAt(Document& doc, int page, int index, Layer layer);
struct RemovedLayer { Layer layer; int index = 0; };
/** Delete a layer with its content. A page keeps at least one layer (nullopt). */
std::optional<RemovedLayer> removeLayer(Document& doc, int page, int index);
struct RenameResult { bool changed = false; std::optional<QString> before; };   // before: the saved name, none when the layer had no name attribute
/** An empty (after trimming) name removes the saved name. */
RenameResult renameLayer(Document& doc, int page, int index, const QString& name);
/** Restore a name as renameLayer reported it (nullopt: no name attribute). */
void restoreLayerName(Document& doc, int page, int index, const std::optional<QString>& before);
/** Move a layer up (+1, drawn later) or down (-1) in the stacking order. Returns false at the ends. */
bool moveLayer(Document& doc, int page, int index, int delta);
