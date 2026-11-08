#include "docfactory.h"
#include "genobj.h"
#include <algorithm>

const std::vector<PaperStyle>& paperStyles() {
  static const std::vector<PaperStyle> s = {
    {"plain", "Liso"}, {"lined", "Rayado (con margen)"}, {"ruled", "Reglado"}, {"graph", "Cuadriculado"}, {"dotted", "Puntos"},
  };
  return s;
}

const std::vector<PaperFormat>& paperFormats() {
  static const std::vector<PaperFormat> f = {
    {"A3", 841.889764, 1190.551181, "A3"},
    {"A4", 595.275591, 841.889764, "A4"},
    {"A5", 419.527559, 595.275591, "A5"},
    {"Letter", 612, 792, "Carta (Letter)"},
    {"Legal", 612, 1008, "Legal"},
  };
  return f;
}

const PaperFormat* paperFormat(const QString& id) {
  for (const PaperFormat& f : paperFormats())
    if (f.id == id) return &f;
  return nullptr;
}

static Attrs plainWhite() { return {{"type", "solid"}, {"color", "#ffffffff"}, {"style", "plain"}}; }

Page blankPageLike(const Page& like) {
  Page p;
  p.attrs = {{"width", fmt8(like.width())}, {"height", fmt8(like.height())}};
  p.background = attrOr(like.background, "type") == "solid" ? like.background : plainWhite();
  p.layers.push_back(Layer{});
  return p;
}

Document blankDocument() {
  const PaperFormat& a4 = *paperFormat("A4");
  Document d;
  Page p;
  p.attrs = {{"width", fmt8(a4.width)}, {"height", fmt8(a4.height)}};
  p.background = plainWhite();
  p.layers.push_back(Layer{});
  d.rootAttrs = {{"creator", "xournalpp 1.3.8"}, {"fileversion", "4"}};
  d.title = "Xournal++ document - see https://xournalpp.github.io/";
  d.pages.push_back(std::move(p));
  return d;
}

// ------------------------------------------------------------------------------------------------------------------------------- pages
int insertPageAt(Document& doc, int index, Page page) {
  const int idx = std::max(0, std::min(index, int(doc.pages.size())));
  doc.pages.insert(doc.pages.begin() + idx, std::move(page));
  return idx;
}

int insertPage(Document& doc, int index, int likeIndex) {
  if (doc.pages.empty()) return insertPageAt(doc, 0, blankDocument().pages[0]);
  const int last = int(doc.pages.size()) - 1;
  const int like = likeIndex < 0 ? std::min(index, last) : likeIndex;
  Page page = blankPageLike(doc.pages[std::max(0, std::min(like, last))]);
  return insertPageAt(doc, index, std::move(page));
}
int newPageAfter(Document& doc, int current) { return insertPage(doc, current + 1, current); }
int insertPageBefore(Document& doc, int current) { return insertPage(doc, current, current); }
int appendPage(Document& doc) { return insertPage(doc, int(doc.pages.size()), int(doc.pages.size()) - 1); }

std::optional<RemovedPage> deletePage(Document& doc, int index) {
  if (doc.pages.size() <= 1 || index < 0 || index >= int(doc.pages.size())) return std::nullopt;
  RemovedPage r;
  r.page = std::move(doc.pages[index]);
  r.index = index;
  doc.pages.erase(doc.pages.begin() + index);
  r.focus = std::min(index, int(doc.pages.size()) - 1);   // the TS says "pages.length - 2" counted before the removal
  return r;
}

int movePage(Document& doc, int index, int delta) {
  const int to = index + delta;
  if (index < 0 || index >= int(doc.pages.size()) || to < 0 || to >= int(doc.pages.size())) return index;
  std::swap(doc.pages[index], doc.pages[to]);
  return to;
}

// ------------------------------------------------------------------------------------------------------------------------------ layers
static std::vector<Layer>* layersOf(Document& doc, int page) {
  return page >= 0 && page < int(doc.pages.size()) ? &doc.pages[page].layers : nullptr;
}

QString layerName(const Layer& layer, int index) {
  const QString n = attrOr(layer.attrs, "name");
  return n.isEmpty() ? QString::fromUtf8("Capa ") + QString::number(index + 1) : n;
}

int insertLayerAt(Document& doc, int page, int index, Layer layer) {
  auto* ls = layersOf(doc, page);
  if (!ls) return -1;
  const int at = std::max(0, std::min(index, int(ls->size())));
  ls->insert(ls->begin() + at, std::move(layer));
  return at;
}

int addLayer(Document& doc, int page, int above) { return insertLayerAt(doc, page, above + 1, Layer{}); }

std::optional<RemovedLayer> removeLayer(Document& doc, int page, int index) {
  auto* ls = layersOf(doc, page);
  if (!ls || ls->size() <= 1 || index < 0 || index >= int(ls->size())) return std::nullopt;
  RemovedLayer r;
  r.layer = std::move((*ls)[index]);
  r.index = index;
  ls->erase(ls->begin() + index);
  return r;
}

static void dropAttr(Attrs& a, const QString& name) {
  a.erase(std::remove_if(a.begin(), a.end(), [&](const auto& kv) { return kv.first == name; }), a.end());
}

RenameResult renameLayer(Document& doc, int page, int index, const QString& name) {
  RenameResult r;
  auto* ls = layersOf(doc, page);
  if (!ls || index < 0 || index >= int(ls->size())) return r;
  Layer& layer = (*ls)[index];
  const QString clean = name.trimmed();
  const QString* prev = attrFind(layer.attrs, "name");
  if (prev) r.before = *prev;
  if ((prev ? *prev : QString()) == clean) return r;
  if (!clean.isEmpty()) attrSet(layer.attrs, "name", clean);
  else dropAttr(layer.attrs, "name");
  r.changed = true;
  return r;
}

void restoreLayerName(Document& doc, int page, int index, const std::optional<QString>& before) {
  auto* ls = layersOf(doc, page);
  if (!ls || index < 0 || index >= int(ls->size())) return;
  Layer& layer = (*ls)[index];
  if (before) attrSet(layer.attrs, "name", *before);
  else dropAttr(layer.attrs, "name");
}

bool moveLayer(Document& doc, int page, int index, int delta) {
  auto* ls = layersOf(doc, page);
  const int j = index + delta;
  if (!ls || index < 0 || index >= int(ls->size()) || j < 0 || j >= int(ls->size())) return false;
  std::swap((*ls)[index], (*ls)[j]);
  return true;
}
