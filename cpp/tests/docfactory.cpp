// Blank documents, paper styles and sizes, page and layer operations (journal.ts / layers.ts) with the undo information they return.
#include "docfactory.h"
#include "xopp.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)

static Stroke dot(double x) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
  s.widths = {1};
  s.pts = {x, x};
  return s;
}
static QString mark(const Page& p) { return attrOr(p.attrs, "mark"); }
static Document threePages() {
  Document d = blankDocument();
  insertPage(d, 1);
  insertPage(d, 2);
  for (int i = 0; i < 3; i++) attrSet(d.pages[size_t(i)].attrs, "mark", QString(char('A' + i)));
  return d;
}
static QString order(const Document& d) {
  QString s;
  for (const Page& p : d.pages) s += mark(p);
  return s;
}

int main() {
  // constants
  CHECK(paperStyles().size() == 5 && paperStyles()[1].style == "lined" && paperStyles()[1].label == "Rayado (con margen)");
  CHECK(paperFormats().size() == 5 && paperFormat("A4")->width == 595.275591 && paperFormat("Legal")->height == 1008 && !paperFormat("B7"));
  CHECK(std::fabs(kMmToPt - 2.834645669) < 1e-8);

  // blankDocument is what Xournal++ 1.3.8 writes, and survives the file format
  const Document b = blankDocument();
  CHECK(b.pages.size() == 1 && b.pages[0].layers.size() == 1 && b.pages[0].layers[0].elements.empty());
  CHECK(attrOr(b.rootAttrs, "creator") == "xournalpp 1.3.8" && attrOr(b.rootAttrs, "fileversion") == "4" && b.title == "Xournal++ document - see https://xournalpp.github.io/");
  CHECK(attrOr(b.pages[0].attrs, "width") == "595.27559" && attrOr(b.pages[0].attrs, "height") == "841.88976");   // fmt8: 8 significant digits
  CHECK(attrOr(b.pages[0].background, "type") == "solid" && attrOr(b.pages[0].background, "color") == "#ffffffff" && attrOr(b.pages[0].background, "style") == "plain");
  CHECK(std::fabs(b.pages[0].width() - 595.27559) < 1e-9);
  QTemporaryDir dir;
  const QString f = dir.filePath("blank.xopp");
  QString err;
  CHECK(saveXopp(f, b, &err));
  const LoadResult lr = loadXopp(f);
  CHECK(lr.ok && lr.doc == b);

  // blankPageLike: same size and paper; pdf and image backgrounds fall back to plain white
  Page lined = b.pages[0];
  lined.attrs = {{"width", "612"}, {"height", "792"}};
  lined.background = {{"type", "solid"}, {"color", "#fff8dcff"}, {"style", "lined"}};
  lined.layers[0].elements.push_back(dot(5));
  const Page like = blankPageLike(lined);
  CHECK(like.width() == 612 && like.height() == 792 && like.background == lined.background && like.layers.size() == 1 && like.layers[0].elements.empty() && like.layers[0].attrs.empty());
  Page pdf = lined;
  pdf.background = {{"type", "pdf"}, {"domain", "absolute"}, {"filename", "x.pdf"}, {"pageno", "3"}};
  const Page fromPdf = blankPageLike(pdf);
  CHECK(attrOr(fromPdf.background, "type") == "solid" && attrOr(fromPdf.background, "color") == "#ffffffff" && attrOr(fromPdf.background, "style") == "plain" && fromPdf.background.size() == 3);
  CHECK(blankPageLike(Page{}).width() == 612);   // no attributes: the default size of the model

  // insert
  Document d = threePages();
  CHECK(order(d) == "ABC");
  CHECK(newPageAfter(d, 0) == 1 && d.pages.size() == 4 && mark(d.pages[1]).isEmpty() && order(d) == "A" + QString() + "BC");
  d = threePages();
  CHECK(insertPageBefore(d, 0) == 0 && mark(d.pages[0]).isEmpty() && mark(d.pages[1]) == "A");
  d = threePages();
  CHECK(appendPage(d) == 3 && d.pages.size() == 4 && mark(d.pages[3]).isEmpty());
  d = threePages();
  d.pages[2].attrs = {{"width", "300"}, {"height", "400"}, {"mark", "C"}};
  CHECK(appendPage(d) == 3 && d.pages[3].width() == 300 && d.pages[3].height() == 400);       // shaped like the page it follows
  CHECK(insertPage(d, 99) == 4 && insertPage(d, -5) == 0);                                  // indices are clamped
  Document empty;
  CHECK(insertPage(empty, 0) == 0 && empty.pages.size() == 1 && empty.pages[0].layers.size() == 1);
  d = threePages();
  Page keep = blankPageLike(d.pages[0]);
  attrSet(keep.attrs, "mark", "Z");
  CHECK(insertPageAt(d, 1, keep) == 1 && order(d) == "AZBC");

  // delete, with undo
  d = threePages();
  d.pages[1].layers[0].elements.push_back(dot(1));
  const Document before = d;
  auto removed = deletePage(d, 1);
  CHECK(removed && removed->index == 1 && removed->focus == 1 && order(d) == "AC" && mark(removed->page) == "B" && removed->page.layers[0].elements.size() == 1);
  insertPageAt(d, removed->index, std::move(removed->page));
  CHECK(d == before);
  auto last = deletePage(d, 2);
  CHECK(last && last->focus == 1);                                  // the page before the removed last one
  CHECK(!deletePage(d, 7));
  Document one = blankDocument();
  CHECK(!deletePage(one, 0) && one.pages.size() == 1);              // the only page stays

  // move, with undo
  d = threePages();
  CHECK(movePage(d, 0, 1) == 1 && order(d) == "BAC");
  CHECK(movePage(d, 1, -1) == 0 && order(d) == "ABC");
  CHECK(movePage(d, 0, -1) == 0 && movePage(d, 2, 1) == 2 && order(d) == "ABC");
  CHECK(movePage(d, 9, 1) == 9);

  // layers
  d = blankDocument();
  Page& p = d.pages[0];
  CHECK(layerName(p.layers[0], 0) == "Capa 1");
  CHECK(addLayer(d, 0, 0) == 1 && addLayer(d, 0, 0) == 1 && p.layers.size() == 3);   // always right above the given layer
  CHECK(addLayer(d, 0, -1) == 0 && p.layers.size() == 4);
  CHECK(addLayer(d, 5, 0) == -1);
  for (size_t i = 0; i < p.layers.size(); i++) p.layers[i].elements.push_back(dot(double(i)));   // i-th layer holds a dot at i
  auto dotOf = [&](int i) { return std::get<Stroke>(p.layers[size_t(i)].elements[0]).pts[0]; };
  CHECK(moveLayer(d, 0, 0, 1) && dotOf(0) == 1 && dotOf(1) == 0);
  CHECK(moveLayer(d, 0, 1, -1) && dotOf(0) == 0 && dotOf(1) == 1);   // undo of a move is the opposite move
  CHECK(!moveLayer(d, 0, 0, -1) && !moveLayer(d, 0, 3, 1) && !moveLayer(d, 0, 9, 1));
  const Document withLayers = d;
  auto gone = removeLayer(d, 0, 2);
  CHECK(gone && gone->index == 2 && p.layers.size() == 3 && std::get<Stroke>(gone->layer.elements[0]).pts[0] == 2);
  insertLayerAt(d, 0, gone->index, std::move(gone->layer));
  CHECK(d == withLayers);
  Document single = blankDocument();
  CHECK(!removeLayer(single, 0, 0) && single.pages[0].layers.size() == 1);   // a page keeps one layer
  CHECK(!removeLayer(d, 0, 9) && !removeLayer(d, 3, 0));

  // rename and its undo
  d = blankDocument();
  RenameResult r = renameLayer(d, 0, 0, "  Fondo ");
  CHECK(r.changed && !r.before && attrOr(d.pages[0].layers[0].attrs, "name") == "Fondo" && layerName(d.pages[0].layers[0], 0) == "Fondo");
  r = renameLayer(d, 0, 0, "Fondo");
  CHECK(!r.changed && r.before && *r.before == "Fondo");
  r = renameLayer(d, 0, 0, "Tinta");
  CHECK(r.changed && r.before && *r.before == "Fondo");
  restoreLayerName(d, 0, 0, r.before);
  CHECK(attrOr(d.pages[0].layers[0].attrs, "name") == "Fondo");
  r = renameLayer(d, 0, 0, "   ");                       // empty: the saved name goes away
  CHECK(r.changed && !attrFind(d.pages[0].layers[0].attrs, "name") && layerName(d.pages[0].layers[0], 0) == "Capa 1");
  restoreLayerName(d, 0, 0, r.before);
  CHECK(attrOr(d.pages[0].layers[0].attrs, "name") == "Fondo");
  restoreLayerName(d, 0, 0, std::nullopt);
  CHECK(!attrFind(d.pages[0].layers[0].attrs, "name"));
  CHECK(!renameLayer(d, 0, 0, "").changed);              // nothing to remove
  CHECK(!renameLayer(d, 4, 0, "x").changed && !renameLayer(d, 0, 4, "x").changed);

  // layers and a name survive a file round trip
  d = threePages();
  addLayer(d, 1, 0);
  renameLayer(d, 1, 1, "Notas");
  const QString f2 = dir.filePath("ops.xopp");
  CHECK(saveXopp(f2, d, &err));
  const LoadResult l2 = loadXopp(f2);
  CHECK(l2.ok && l2.doc == d && layerName(l2.doc.pages[1].layers[1], 1) == "Notas");

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("docfactory: all passed\n");
  return 0;
}
