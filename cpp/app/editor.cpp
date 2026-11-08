#include "editor.h"
#include "settings/inputfilter.h"
#include "docfactory.h"
#include "documentview.h"
#include "genobj.h"
#include "geometry.h"
#include "paper.h"
#include "penlog.h"
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

static constexpr int kMaxUndo = 500;   // each step keeps copies of what it changed: the oldest are let go

DocumentView* Tool::view() const { return ed_.view(); }

Editor::Editor(QObject* parent) : QObject(parent), selection_(std::make_unique<NullSelection>()) {
  doc_ = blankDocument();
  syncPageState();
}
Editor::~Editor() = default;

// ---- document ---------------------------------------------------------------------------------------------------------------------------
void Editor::syncPageState() {
  pageState_.resize(doc_.pages.size());
  indexes_.resize(doc_.pages.size());
}

void Editor::setDocument(Document d, const QString& path) {
  if (gesture_) pointerCancel();
  doc_ = std::move(d);
  if (doc_.pages.empty()) doc_ = blankDocument();
  path_ = path;
  undo_.clear(); redo_.clear(); groups_.clear();
  version_ = savedVersion_ = 0;
  mutations_++;
  pageState_.assign(doc_.pages.size(), PageState{});
  indexes_.assign(doc_.pages.size(), PageIndex{});
  current_ = 0;
  selection_->clear();
  emit documentReplaced();
  emit historyChanged();
  emit dirtyChanged(false);
  emit currentPageChanged(0);
}

void Editor::markSaved() {
  const bool was = dirty();
  savedVersion_ = version_;
  if (was) emit dirtyChanged(false);
}

void Editor::bumpVersion(int delta) {
  const bool was = dirty();
  version_ += delta;
  mutations_++;
  if (was != dirty()) emit dirtyChanged(dirty());
}

// ---- pages and layers -------------------------------------------------------------------------------------------------------------------
void Editor::setCurrentPage(int p) {
  p = std::clamp(p, 0, std::max(0, pageCount() - 1));
  if (p == current_) return;
  current_ = p;
  emit currentPageChanged(p);
}

int Editor::activeLayer(int page) const {
  if (page < 0 || size_t(page) >= pageState_.size()) return 0;
  const int n = int(doc_.pages[size_t(page)].layers.size());
  return std::clamp(pageState_[size_t(page)].activeLayer, 0, std::max(0, n - 1));
}
void Editor::setActiveLayer(int page, int layer) {
  if (page < 0 || size_t(page) >= pageState_.size()) return;
  pageState_[size_t(page)].activeLayer = layer;
  emit layersChanged(page);
}
bool Editor::layerVisible(int page, int layer) const {
  if (page < 0 || size_t(page) >= pageState_.size()) return true;
  const auto& h = pageState_[size_t(page)].hidden;
  return std::find(h.begin(), h.end(), layer) == h.end();
}
void Editor::setLayerVisible(int page, int layer, bool visible) {
  if (page < 0 || size_t(page) >= pageState_.size()) return;
  auto& h = pageState_[size_t(page)].hidden;
  const auto it = std::find(h.begin(), h.end(), layer);
  if (visible && it != h.end()) h.erase(it);
  else if (!visible && it == h.end()) h.push_back(layer);
  else return;
  touch(page);
  emit layersChanged(page);
}
std::vector<int> Editor::hiddenLayers(int page) const {
  if (page < 0 || size_t(page) >= pageState_.size()) return {};
  return pageState_[size_t(page)].hidden;
}

void Editor::structural(int focus, const QString& label, std::function<void()> doIt, std::function<void()> undoIt) {
  doIt();
  Action a;
  a.label = label; a.page = focus; a.structural = true;
  a.undo = [this, undoIt = std::move(undoIt), focus] { undoIt(); emit structureChanged(focus); };
  a.redo = [this, doIt, focus] { doIt(); emit structureChanged(focus); };
  pushAction(std::move(a));
  emit structureChanged(focus);
}

int Editor::insertPage(int index, int likeIndex) {
  const int n = pageCount();
  const int idx = std::clamp(index, 0, n);
  const int like = std::clamp(likeIndex < 0 ? std::min(idx, n - 1) : likeIndex, 0, n - 1);
  const Page page = blankPageLike(doc_.pages[size_t(like)]);
  structural(idx, QStringLiteral("Nueva página"),
    [this, idx, page] { doc_.pages.insert(doc_.pages.begin() + idx, page); pageState_.insert(pageState_.begin() + idx, PageState{}); indexes_.insert(indexes_.begin() + idx, PageIndex{}); },
    [this, idx] { doc_.pages.erase(doc_.pages.begin() + idx); pageState_.erase(pageState_.begin() + idx); indexes_.erase(indexes_.begin() + idx); });
  return idx;
}

bool Editor::deletePage(int index) {
  if (pageCount() <= 1) { notify(QStringLiteral("No se puede eliminar la única página del documento")); return false; }
  if (index < 0 || index >= pageCount()) return false;
  auto held = std::make_shared<Page>();
  auto heldState = std::make_shared<PageState>();
  selection_->clear();
  structural(std::min(index, pageCount() - 2), QStringLiteral("Eliminar página"),
    [this, index, held, heldState] {
      *held = std::move(doc_.pages[size_t(index)]); *heldState = pageState_[size_t(index)];
      doc_.pages.erase(doc_.pages.begin() + index); pageState_.erase(pageState_.begin() + index); indexes_.erase(indexes_.begin() + index);
    },
    [this, index, held, heldState] {
      doc_.pages.insert(doc_.pages.begin() + index, std::move(*held)); pageState_.insert(pageState_.begin() + index, *heldState); indexes_.insert(indexes_.begin() + index, PageIndex{});
    });
  return true;
}

bool Editor::movePage(int index, int delta) {
  const int to = index + delta;
  if (index < 0 || index >= pageCount() || to < 0 || to >= pageCount() || delta == 0) return false;
  auto swapPages = [this](int a, int b) {
    std::swap(doc_.pages[size_t(a)], doc_.pages[size_t(b)]);
    std::swap(pageState_[size_t(a)], pageState_[size_t(b)]);
    std::swap(indexes_[size_t(a)], indexes_[size_t(b)]);
  };
  structural(to, QStringLiteral("Mover página"), [swapPages, index, to] { swapPages(index, to); }, [swapPages, index, to] { swapPages(index, to); });
  return true;
}

void Editor::editBackgrounds(const std::vector<int>& pages, const QString& label, const std::function<void(Attrs&)>& fn) {
  std::vector<std::pair<int, Attrs>> before;
  std::vector<std::pair<int, Attrs>> after;
  for (int p : pages) {
    if (p < 0 || p >= pageCount()) continue;
    Attrs& bg = doc_.pages[size_t(p)].background;
    before.emplace_back(p, bg);
    fn(bg);
    after.emplace_back(p, bg);
  }
  if (before.empty()) return;
  auto apply = [this](const std::vector<std::pair<int, Attrs>>& v) {
    for (const auto& [p, a] : v) if (p < pageCount()) doc_.pages[size_t(p)].background = a;
    emit appearanceChanged();
  };
  emit appearanceChanged();
  Action a;
  a.label = label; a.page = before.front().first; a.repaintAll = true;
  a.undo = [apply, before] { apply(before); };
  a.redo = [apply, after] { apply(after); };
  pushAction(std::move(a));
}

static std::vector<int> targetPages(const Document& d, int page, bool all) {
  std::vector<int> v;
  if (all) for (size_t i = 0; i < d.pages.size(); i++) v.push_back(int(i));
  else v.push_back(page);
  return v;
}

void Editor::setPaperStyle(int page, const QString& style, bool all) {
  std::vector<int> pages;
  for (int p : targetPages(doc_, page, all)) if (p >= 0 && p < pageCount() && attrOr(doc_.pages[size_t(p)].background, "type", "solid") == "solid") pages.push_back(p);
  editBackgrounds(pages, QStringLiteral("Tipo de papel"), [&](Attrs& bg) {
    attrSet(bg, "style", style);
    bg.erase(std::remove_if(bg.begin(), bg.end(), [](const auto& kv) { return kv.first == "xlPaper"; }), bg.end());
    setPaperSpacing(bg, 0);   // a spacing belongs to the style it was chosen for; the opacity stays
  });
}

void Editor::setPaperColor(int page, const QString& rgb, bool all) {
  std::vector<int> pages;
  for (int p : targetPages(doc_, page, all)) if (p >= 0 && p < pageCount() && attrOr(doc_.pages[size_t(p)].background, "type", "solid") == "solid") pages.push_back(p);
  editBackgrounds(pages, QStringLiteral("Color de fondo"), [&](Attrs& bg) { attrSet(bg, "color", rgb + "ff"); });
}

void Editor::setPageSize(int page, double w, double h, bool all) {
  struct Old { int p; Attrs attrs; };
  auto before = std::make_shared<std::vector<Old>>();
  for (int p : targetPages(doc_, page, all)) if (p >= 0 && p < pageCount()) before->push_back({p, doc_.pages[size_t(p)].attrs});
  structural(page, QStringLiteral("Formato de papel"),
    [this, before, w, h] { for (const Old& o : *before) { attrSet(doc_.pages[size_t(o.p)].attrs, "width", fmt8(w)); attrSet(doc_.pages[size_t(o.p)].attrs, "height", fmt8(h)); } },
    [this, before] { for (const Old& o : *before) doc_.pages[size_t(o.p)].attrs = o.attrs; });
}

// ---- element primitives ----------------------------------------------------------------------------------------------------------------
QRectF Editor::paddedRect(const Element& e) {
  const Rect b = elementBBox(e);
  if (!std::isfinite(b[0]) || !std::isfinite(b[2]) || !std::isfinite(b[1]) || !std::isfinite(b[3])) return QRectF(0, 0, 1e-3, 1e-3);
  double pad = 2;
  if (const auto* s = std::get_if<Stroke>(&e)) if (const QString* g = attrFind(s->attrs, "glow")) pad += 2 * g->toDouble();   // the halo reaches beyond the line
  return QRectF(QPointF(b[0] - pad, b[1] - pad), QPointF(b[2] + pad, b[3] + pad));
}

PageIndex& Editor::indexRef(int page) { return indexes_[size_t(page)]; }

const PageIndex& Editor::index(int page) {
  PageIndex& ix = indexRef(page);
  ix.ensure(doc_.pages[size_t(page)]);
  return ix;
}

void Editor::invalidateIndex(int page) { if (page >= 0 && size_t(page) < indexes_.size()) indexes_[size_t(page)].invalidate(); }

int Editor::addElement(int page, int layer, Element e) {
  auto& els = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
  els.push_back(std::move(e));
  const int idx = int(els.size()) - 1;
  indexRef(page).inserted(doc_.pages[size_t(page)], layer, idx);
  selection_->onSplices(page, layer, {{idx, 0, 1}});
  emit elementAppended(page, layer, idx, paddedRect(els.back()));
  return idx;
}

void Editor::insertElement(int page, int layer, int index, Element e) {
  auto& els = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
  index = std::clamp(index, 0, int(els.size()));
  els.insert(els.begin() + index, std::move(e));
  indexRef(page).inserted(doc_.pages[size_t(page)], layer, index);
  selection_->onSplices(page, layer, {{index, 0, 1}});
  emit contentChanged(page, paddedRect(els[size_t(index)]));
}

Element Editor::takeElement(int page, int layer, int index) {
  auto& els = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
  Element e = std::move(els[size_t(index)]);
  els.erase(els.begin() + index);
  indexRef(page).removed(layer, index);
  selection_->onSplices(page, layer, {{index, 1, 0}});
  emit contentChanged(page, paddedRect(e));
  return e;
}

void Editor::replaceElements(int page, int layer, int index, int count, std::vector<Element> with) {
  std::vector<Repl> one(1);
  one[0].index = index;
  one[0].count = count;
  one[0].with = std::move(with);
  applyReplacements(page, layer, std::move(one));
}

std::vector<Editor::Repl> Editor::applyReplacements(int page, int layer, std::vector<Repl> repls) {
  auto& els = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
  std::vector<Repl> inverse;
  inverse.reserve(repls.size());
  std::vector<ElementSplice> splices;   // what the selection needs to keep following its elements
  splices.reserve(repls.size());
  for (const Repl& r : repls) splices.push_back({r.index, r.count, int(r.with.size())});
  QRectF dirty;
  auto grow = [&](const Element& e) { const QRectF r = paddedRect(e); dirty = dirty.isNull() ? r : dirty.united(r); };
  int shift = 0;
  for (Repl& r : repls) {
    Repl inv;
    inv.index = r.index + shift;
    inv.count = int(r.with.size());
    for (int i = 0; i < r.count; i++) { grow(els[size_t(r.index + i)]); inv.with.push_back(els[size_t(r.index + i)]); }
    for (const Element& e : r.with) grow(e);
    shift += int(r.with.size()) - r.count;
    inverse.push_back(std::move(inv));
  }
  // one pass: elements between the replacements are moved once, the replaced ones are dropped, the new ones go in
  std::vector<Element> out;
  out.reserve(size_t(std::max<long>(0, long(els.size()) + shift)));
  size_t pos = 0;
  for (Repl& r : repls) {
    for (; pos < size_t(r.index); pos++) out.push_back(std::move(els[pos]));
    for (Element& e : r.with) out.push_back(std::move(e));
    pos = size_t(r.index + r.count);
  }
  for (; pos < els.size(); pos++) out.push_back(std::move(els[pos]));
  els.swap(out);
  selection_->onSplices(page, layer, splices);
  PageIndex& ix = indexRef(page);
  if (ix.valid()) {
    if (repls.size() > 4096) ix.invalidate();
    else {
      for (const Repl& r : inverse) {   // inverse.index = position after the earlier replacements, inverse.count = how many new elements stand there now
        const int at = r.index;
        for (size_t i = 0; i < r.with.size(); i++) ix.removed(layer, at);
        for (int i = 0; i < r.count; i++) ix.inserted(doc_.pages[size_t(page)], layer, at + i);
      }
    }
  }
  if (!dirty.isNull()) emit contentChanged(page, dirty);
  return inverse;
}

void Editor::elementEdited(int page, int layer, int index, const QRectF& before) {
  const Element& e = doc_.pages[size_t(page)].layers[size_t(layer)].elements[size_t(index)];
  indexRef(page).replaced(doc_.pages[size_t(page)], layer, index);
  const QRectF now = paddedRect(e);
  emit contentChanged(page, before.isNull() ? now : now.united(before));
}

void Editor::touch(int page, const QRectF& rect) {
  if (rect.isNull()) invalidateIndex(page);   // "everything may have changed" also means the index cannot be trusted
  emit contentChanged(page, rect);
}

int Editor::commitAdd(int page, int layer, Element e, const QString& label) {
  const int idx = addElement(page, layer, e);
  Action a;
  a.label = label; a.page = page;
  a.undo = [this, page, layer, idx] { takeElement(page, layer, idx); };
  a.redo = [this, page, layer, idx, e = std::move(e)] { insertElement(page, layer, idx, e); };
  pushAction(std::move(a));
  return idx;
}

std::vector<ElementRef> Editor::membersOf(int page, int layer, const QString& id) const {
  std::vector<ElementRef> out;
  const auto& els = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
  for (size_t i = 0; i < els.size(); i++) if (planeIdOf(els[i]) == std::optional<QString>(id)) out.push_back({page, layer, int(i)});
  return out;
}

Editor::Swap Editor::swapGenerated(int page, int layer, const QString& id, std::vector<Element> fresh) {
  auto& els = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
  struct Old { int index; Element el; };
  auto old = std::make_shared<std::vector<Old>>();
  for (size_t i = 0; i < els.size(); i++) if (planeIdOf(els[i]) == std::optional<QString>(id)) old->push_back({int(i), els[i]});
  const int at = old->empty() ? int(els.size()) : old->front().index;
  auto freshV = std::make_shared<std::vector<Element>>(std::move(fresh));
  auto redo = [this, page, layer, id, at, freshV, old] {
    auto& e = doc_.pages[size_t(page)].layers[size_t(layer)].elements;
    // remove the old pieces from the back, then put the new ones where the first old one was
    for (size_t k = old->size(); k-- > 0;) takeElement(page, layer, (*old)[k].index);
    replaceElements(page, layer, std::min(at, int(e.size())), 0, *freshV);
  };
  auto undo = [this, page, layer, at, freshV, old] {
    const int pos = std::min(at, int(doc_.pages[size_t(page)].layers[size_t(layer)].elements.size()));
    replaceElements(page, layer, pos, int(freshV->size()), {});
    for (const Old& o : *old) insertElement(page, layer, o.index, o.el);
  };
  redo();
  return {undo, redo};
}

// ---- history ------------------------------------------------------------------------------------------------------------------------------
void Editor::pushAction(Action a) {
  if (!groups_.empty()) { groups_.back().items.push_back(std::move(a)); return; }
  undo_.push_back(std::move(a));
  if (int(undo_.size()) > kMaxUndo) undo_.erase(undo_.begin());
  redo_.clear();
  bumpVersion(+1);
  emit historyChanged();
}

void Editor::beginGroup(const QString& label) { groups_.push_back({label, {}}); }

void Editor::endGroup() {
  if (groups_.empty()) return;
  Group g = std::move(groups_.back());
  groups_.pop_back();
  if (g.items.empty()) return;
  Action a;
  a.label = g.label.isEmpty() ? g.items.front().label : g.label;
  a.page = g.items.front().page;
  for (const Action& i : g.items) { a.structural |= i.structural; a.repaintAll |= i.repaintAll; if (i.page != a.page) a.repaintAll = true; }
  auto items = std::make_shared<std::vector<Action>>(std::move(g.items));
  a.undo = [items] { for (size_t k = items->size(); k-- > 0;) (*items)[k].undo(); };
  a.redo = [items] { for (Action& i : *items) i.redo(); };
  pushAction(std::move(a));
}

void Editor::afterHistory(const Action& a) {
  if (a.repaintAll) emit appearanceChanged();
  selection_->refresh();
  if (a.page >= 0) emit focusRequested(std::min(a.page, std::max(0, pageCount() - 1)));
  emit historyChanged();
}

void Editor::undo() {
  if (undo_.empty() || gesture_) return;
  Action a = std::move(undo_.back());
  undo_.pop_back();
  a.undo();
  bumpVersion(-1);
  afterHistory(a);
  redo_.push_back(std::move(a));
  emit historyChanged();
}

void Editor::redo() {
  if (redo_.empty() || gesture_) return;
  Action a = std::move(redo_.back());
  redo_.pop_back();
  a.redo();
  bumpVersion(+1);
  afterHistory(a);
  undo_.push_back(std::move(a));
  emit historyChanged();
}

// ---- tools --------------------------------------------------------------------------------------------------------------------------------
void Editor::registerTool(ToolInfo info) {
  const QString id = info.id;
  auto& e = tools_[id];
  if (e.instance && tool_ == id) e.instance->deactivate();
  e.instance.reset();   // a re-registration replaces the instance
  e.info = std::move(info);
  if (!e.info.label.isEmpty()) {
    QAction* a = addCommand("tool." + id, e.info.label, e.info.shortcut, [this, id] { setTool(id); });
    a->setCheckable(true);
    a->setChecked(tool_ == id);
    a->setIcon(e.info.icon);
    a->setToolTip(e.info.tooltip.isEmpty() ? e.info.label : e.info.tooltip);
  }
  emit toolRegistered(id);
  if (tool_ == id && e.info.factory) { toolInstance(id)->activate(); emit toolChanged(id); }
}

const ToolInfo* Editor::toolInfo(const QString& id) const {
  const auto it = tools_.find(id);
  return it == tools_.end() ? nullptr : &it->second.info;
}

std::vector<const ToolInfo*> Editor::toolInfos() const {
  std::vector<const ToolInfo*> v;
  for (const auto& kv : tools_) v.push_back(&kv.second.info);
  std::sort(v.begin(), v.end(), [](const ToolInfo* a, const ToolInfo* b) { return a->order != b->order ? a->order < b->order : a->id < b->id; });
  return v;
}

Tool* Editor::toolInstance(const QString& id) {
  const auto it = tools_.find(id);
  if (it == tools_.end() || !it->second.info.factory) return nullptr;
  if (!it->second.instance) it->second.instance = it->second.info.factory(*this);
  return it->second.instance.get();
}

void Editor::setTool(const QString& idArg) {
  const QString id = idArg;   // a copy: callers pass returnTo() or previousTool(), which this function changes
  if (!tools_.count(id) || !tools_[id].info.factory) { notify(QStringLiteral("Esa herramienta todavía no está disponible")); return; }
  if (gesture_) return;
  setHoverTool(nullptr);
  returnTo_.clear();
  if (id != "select" && id != "lasso") selection_->clear();
  if (id != tool_) {
    if (Tool* old = toolInstance(tool_)) old->deactivate();
    toolBefore_ = tool_;
    tool_ = id;
    if (Tool* t = toolInstance(id)) t->activate();
  }
  for (const auto& kv : tools_) if (QAction* a = actions_.count("tool." + kv.first) ? actions_["tool." + kv.first] : nullptr) a->setChecked(kv.first == tool_);
  emit toolChanged(tool_);
  emit selectionChanged();
}

void Editor::afterCreate(const QString& from) {
  const QString before = toolBefore_;
  const ToolInfo* bi = before.isEmpty() ? nullptr : toolInfo(before);
  const bool ok = bi && !bi->inserter && before != "select" && before != "lasso";
  const QString back = ok ? before : QStringLiteral("pen");
  // an inserter calls this from its own pointerUp, while the gesture is still registered, and setTool refuses to switch mid-gesture
  Tool* const gesture = gesture_;
  gesture_ = nullptr;
  setTool("select");
  gesture_ = gesture;
  returnTo_ = (from == "select" || from == "lasso") ? QString() : back;
}

void Editor::escape() {
  if (!returnTo_.isEmpty() && tool_ == "select") { const QString t = returnTo_; setTool(t); return; }
  selection_->clear();
  emit selectionChanged();
}

// ---- style, tablet, selection ----------------------------------------------------------------------------------------------------------
void Editor::setStyle(const Style& s) { if (s == style_) return; style_ = s; emit styleChanged(); }
void Editor::setTablet(const TabletSettings& t) { tablet_ = t; emit styleChanged(); }
void Editor::setSelection(std::unique_ptr<Selection> s) { selection_ = s ? std::move(s) : std::make_unique<NullSelection>(); emit selectionChanged(); }

// ---- commands -----------------------------------------------------------------------------------------------------------------------------
QAction* Editor::action(const QString& id) {
  auto it = actions_.find(id);
  if (it != actions_.end()) return it->second;
  auto* a = new QAction(this);
  a->setObjectName(id);
  a->setEnabled(false);
  actions_[id] = a;
  emit actionCreated(a);
  return a;
}

QAction* Editor::addCommand(const QString& id, const QString& text, const QKeySequence& shortcut, std::function<void()> run) {
  QAction* a = action(id);
  a->disconnect(this);
  if (!text.isEmpty()) a->setText(text);
  a->setShortcut(shortcut);
  if (run) connect(a, &QAction::triggered, this, [run = std::move(run)] { run(); });
  a->setEnabled(true);
  return a;
}

// ---- input ----------------------------------------------------------------------------------------------------------------------------------
static QString toolOfPenAction(PenAction a) {
  switch (a) {
    case PenAction::Pen: return "pen";
    case PenAction::Highlighter: return "highlighter";
    case PenAction::Eraser: return "eraser";
    case PenAction::EraserPart: return "eraserPart";
    case PenAction::Select: return "select";
    case PenAction::Lasso: return "lasso";
    case PenAction::Fill: return "fill";
    case PenAction::Hand: return "hand";
    default: return QString();
  }
}

void Editor::notePen(QEvent::Type t) {
  penClock_.start();
  penSeen_ = true;
  penGone_ = t == QEvent::TabletLeaveProximity;
}

PenButton Editor::mappedButton(const PointerEvent& e) const {
  PenPointer pp;
  pp.isPen = e.kind == PointerEvent::Kind::Pen || e.kind == PointerEvent::Kind::Eraser;
  pp.eraserEnd = e.kind == PointerEvent::Kind::Eraser;
  pp.rightButton = e.pressedBarrel;
  pp.middleButton = e.pressedBarrel2;
  const PenButton b = penButtonOf(pp);
  if ((b == PenButton::Barrel || b == PenButton::Barrel2) && penBound_ && penBound_(b)) return PenButton::None;
  return b;
}

const ButtonCfg* Editor::buttonConfigFor(const PointerEvent& e) const {
  if (e.kind == PointerEvent::Kind::Mouse) {
    if (e.buttons.testFlag(Qt::RightButton)) return &xpp_.mouseRight;
    if (e.buttons.testFlag(Qt::MiddleButton)) return &xpp_.mouseMiddle;
    if (e.buttons.testFlag(Qt::BackButton)) return &xpp_.mouse4;
    if (e.buttons.testFlag(Qt::ForwardButton)) return &xpp_.mouse5;
    return &xpp_.mouseLeft;
  }
  switch (mappedButton(e)) {
    case PenButton::Eraser: return &xpp_.stylusEraser;
    case PenButton::Barrel: return &xpp_.stylus1;
    case PenButton::Barrel2: return &xpp_.stylus2;
    case PenButton::None: break;
  }
  return nullptr;
}

QString Editor::toolForGesture(const PointerEvent& e) const {
  if (e.kind == PointerEvent::Kind::Mouse) {   // mouse buttons: Preferencias > Ratón
    const ButtonCfg* c = buttonConfigFor(e);
    if (c && !c->tool.isEmpty() && c->tool != "toolbox" && tools_.count(c->tool) && tools_.at(c->tool).info.factory) return c->tool;
    return tool_;
  }
  PenAction a = PenAction::None;
  switch (mappedButton(e)) {
    case PenButton::Eraser: a = tablet_.eraser; break;
    case PenButton::Barrel: a = tablet_.barrel; break;
    case PenButton::Barrel2: a = tablet_.barrel2; break;
    case PenButton::None: break;
  }
  if (a == PenAction::None || a == PenAction::Toolbox || a == PenAction::Undo || a == PenAction::Redo) return tool_;
  const QString t = toolOfPenAction(a);
  return tools_.count(t) && tools_.at(t).info.factory ? t : tool_;   // a mapped tool that does not exist (yet) falls back to the current one
}

// The filter (stabilizer, pressure rules, tap filter) sits between the device and the tools; without one the events go straight through.
void Editor::pointerDown(const PointerEvent& e) {
  if (gesture_) return;   // a second pointer while one is down is ignored
  if (!filter_) { dispatchDown(e); return; }
  const QString id = toolForGesture(e);
  filter_->down(e, id == "pen" || id == "highlighter", [this](InputPhase ph, const PointerEvent& x) {
    if (ph == InputPhase::Down) dispatchDown(x); else if (ph == InputPhase::Move) dispatchMove(x); else dispatchUp(x);
  });
}
void Editor::pointerMove(const PointerEvent& e) {
  if (!filter_) { dispatchMove(e); return; }
  filter_->move(e, [this](InputPhase ph, const PointerEvent& x) {
    if (ph == InputPhase::Down) dispatchDown(x); else if (ph == InputPhase::Move) dispatchMove(x); else dispatchUp(x);
  });
}
void Editor::pointerUp(const PointerEvent& e) {
  if (!filter_) { dispatchUp(e); return; }
  filter_->up(e, [this](InputPhase ph, const PointerEvent& x) {
    if (ph == InputPhase::Down) dispatchDown(x); else if (ph == InputPhase::Move) dispatchMove(x); else dispatchUp(x);
  });
}

void Editor::dispatchDown(const PointerEvent& e) {
  if (gesture_) return;
  const PenButton b = mappedButton(e);
  if (penlog::on())
    penlog::write(QString("dispatchDown kind=%1 barrel=%2 barrel2=%3 mapped=%4 prefs(barrel=%5 barrel2=%6 eraser=%7) tool=%8 toolForGesture=%9").arg(int(e.kind)).arg(e.pressedBarrel).arg(e.pressedBarrel2)
                      .arg(int(b)).arg(int(tablet_.barrel)).arg(int(tablet_.barrel2)).arg(int(tablet_.eraser)).arg(tool_).arg(toolForGesture(e)));
  setHoverTool(nullptr);
  const bool firedWhileHovering = barrelFired_;
  if (b != PenButton::None && runPressAction(e)) {   // actions that fire on press and start no stroke (already run while hovering, when the button went down)
    if (!firedWhileHovering) barrelFired_ = false;   // it ran now, on the tip: no hovering press to remember
    return;
  }
  const ButtonCfg* cfg = buttonConfigFor(e);
  if (cfg && e.kind == PointerEvent::Kind::Mouse && cfg->tool == "toolbox") { emit toolboxRequested(e.globalPos.toPoint()); return; }
  QString id = toolForGesture(e);
  if (id == "select" && b == PenButton::None && !returnTo_.isEmpty() && !selection_->wouldHit(e.page, e.pos, e.zoom)) {
    setTool(returnTo_);   // a click on the empty page: the gesture goes on with the tool before
    id = tool_;
  }
  Tool* t = toolInstance(id);
  if (!t) return;
  if (cfg && (!cfg->color.isEmpty() || cfg->size > 0)) {   // the button asks for a colour and / or a size while it is held
    gestureStyle_ = style_;
    gestureStyleSaved_ = true;
    if (!cfg->color.isEmpty()) style_.color = cfg->color;
    if (cfg->size > 0 && sizeLookup_) {
      const double v = sizeLookup_(id, cfg->size - 1);
      if (v > 0) {
        if (id == "pen") style_.penWidth = v;
        else if (id == "highlighter") style_.highlighterWidth = v;
        else if (id == "eraser" || id == "eraserPart") style_.eraserRadius = v;
      }
    }
  }
  gesture_ = t;
  gestureId_ = id;
  gestureBorrowed_ = id != tool_;
  if (gestureBorrowed_) t->activate();
  t->pointerDown(e);
}

void Editor::restoreGestureStyle() {
  if (!gestureStyleSaved_) return;
  gestureStyleSaved_ = false;
  style_ = gestureStyle_;
}

void Editor::dispatchMove(const PointerEvent& e) { if (gesture_) gesture_->pointerMove(e); }

void Editor::dispatchUp(const PointerEvent& e) {
  if (!gesture_) return;
  Tool* t = gesture_;
  const bool borrowed = gestureBorrowed_;
  t->pointerUp(e);
  gesture_ = nullptr;   // cleared after the tool's own handling, which may undo/commit actions
  restoreGestureStyle();
  if (borrowed) t->deactivate();
  emit gestureFinished();
}

void Editor::pointerCancel() {
  if (filter_) filter_->cancel();
  if (!gesture_) return;
  Tool* t = gesture_;
  const bool borrowed = gestureBorrowed_;
  t->pointerCancel();
  gesture_ = nullptr;
  restoreGestureStyle();
  if (borrowed) t->deactivate();
  emit gestureFinished();
}

// A pen button held while the pen only hovers already selects its mapped tool (hand, eraser...), as in Xournal++: the cursor and the hover follow it, and it is given
// back the moment the button is released or the pen leaves. The tool is activated on loan; the gesture that may follow starts from the same mapping.
void Editor::setHoverTool(Tool* t) {
  if (t == hoverTool_) return;
  Tool* old = hoverTool_;
  hoverTool_ = t;
  if (old) old->deactivate();
  if (t) t->activate();
  emit hoverToolChanged();
}

// The press-time actions of a pen button (undo, redo, tool box) run when the button goes down, with the pen only hovering: they must not wait for the tip to touch.
// `barrelFired_` remembers that this press already ran, so the tip landing afterwards (dispatchDown) does not run it again; the release of the button clears it.
bool Editor::runPressAction(const PointerEvent& e) {
  const PenButton b = mappedButton(e);
  if (b == PenButton::None) { barrelFired_ = false; return false; }
  const PenAction a = b == PenButton::Eraser ? tablet_.eraser : b == PenButton::Barrel ? tablet_.barrel : tablet_.barrel2;
  if (a != PenAction::Undo && a != PenAction::Redo && a != PenAction::Toolbox) return false;
  if (barrelFired_) return true;
  barrelFired_ = true;
  if (a == PenAction::Undo) undo();
  else if (a == PenAction::Redo) redo();
  else emit toolboxRequested(e.globalPos.toPoint());
  return true;
}

void Editor::hover(const PointerEvent& e) {
  if (gesture_) return;
  Tool* t = nullptr;
  if (e.kind == PointerEvent::Kind::Pen || e.kind == PointerEvent::Kind::Eraser) {
    if (e.pressedBarrel || e.pressedBarrel2) runPressAction(e); else barrelFired_ = false;
    const QString id = toolForGesture(e);
    if (id != tool_) t = toolInstance(id);
  }
  setHoverTool(t);
  if (Tool* h = t ? t : currentTool()) h->hover(e);
}
void Editor::pointerLeft() {
  if (gesture_) return;
  setHoverTool(nullptr);
  if (Tool* t = currentTool()) t->pointerLeft();
}

bool Editor::keyPress(QKeyEvent* e) {
  Tool* t = gesture_ ? gesture_ : currentTool();
  if (t && t->keyPress(e)) return true;
  if (e->key() == Qt::Key_Escape) { escape(); return true; }
  return false;
}
bool Editor::keyRelease(QKeyEvent* e) {
  Tool* t = gesture_ ? gesture_ : currentTool();
  return t && t->keyRelease(e);
}
