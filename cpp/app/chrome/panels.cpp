// The two side panels: page thumbnails and layers.
#include "docfactory.h"
#include "pagerender.h"
#include "parts.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace chrome {
namespace {
constexpr int kThumbW = 120;       // logical pixels
constexpr int kMaxCached = 300;    // thumbnails kept (each is about 120 x 170 x dpr^2 x 4 bytes)

/** Panel buttons are not QActions, so they are not tinted with the others: their icon is drawn again when the theme changes. */
QToolButton* panelButton(Chrome& c, QWidget* parent, const QString& name, const QString& glyphName, const QString& tip) {
  auto* b = new QToolButton(parent);
  b->setObjectName(name);
  b->setToolTip(tip);
  b->setIconSize(QSize(16, 16));
  b->setMinimumSize(0, 28);   // .paneTools button { flex: 1; padding: 3px 0 }
  b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto paint = [b, glyphName, &c] { b->setIcon(glyph(glyphName, c.theme() == "neon" ? neon().cyan : QColor(0xe6, 0xe6, 0xe6))); };
  paint();
  QObject::connect(&c, &Chrome::themeChanged, b, paint);
  return b;
}

QColor accent(const Chrome& c) { return c.theme() == "neon" ? neon().pink : QColor("#4a8af0"); }
}  // namespace

// ---- thumbnails ----------------------------------------------------------------------------------------------------------------------------
class ThumbDelegate : public QStyledItemDelegate {
 public:
  ThumbDelegate(ThumbsPanel& p) : QStyledItemDelegate(&p), panel_(p) {}
  QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override {
    const int page = i.row();
    const Document& d = panel_.c_.ed.doc();
    double ratio = 1.41;
    if (page >= 0 && page < int(d.pages.size())) ratio = d.pages[size_t(page)].height() / std::max(1.0, d.pages[size_t(page)].width());
    return QSize(o.rect.width() > 0 ? o.rect.width() : kThumbW + 24, int(std::lround(kThumbW * ratio)) + 40);
  }
  void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& idx) const override {
    const int page = idx.row();
    const bool cur = page == panel_.c_.ed.currentPage();
    const bool neonOn = panel_.c_.theme() == "neon";
    const Document& d = panel_.c_.ed.doc();
    if (page < 0 || page >= int(d.pages.size())) return;
    const Page& pg = d.pages[size_t(page)];
    const double ratio = pg.height() / std::max(1.0, pg.width());
    const QRect box(o.rect.center().x() - kThumbW / 2, o.rect.top() + 10, kThumbW, int(std::lround(kThumbW * ratio)));
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    if (o.state & QStyle::State_MouseOver) p->fillRect(o.rect.adjusted(3, 2, -3, -2), neonOn ? QColor(185, 103, 255, 40) : QColor(255, 255, 255, 14));
    if (neonOn && !panel_.c_.reducedEffects()) for (int g = 5; g >= 1; g--) { QColor c = cur ? neon().pink : neon().purple; c.setAlphaF(0.05 + 0.03 * (6 - g) / 5.0 * (cur ? 2 : 1)); p->setPen(QPen(c, 2)); p->drawRect(box.adjusted(-g, -g, g, g)); }
    const QPixmap pm = panel_.cached(page);
    if (pm.isNull()) { p->fillRect(box, Qt::white); const_cast<ThumbsPanel&>(panel_).request(page); }
    else p->drawPixmap(box, pm);
    if (cur) { p->setPen(QPen(accent(panel_.c_), 3)); p->setBrush(Qt::NoBrush); p->drawRect(box.adjusted(-1, -1, 1, 1)); }
    p->setPen(o.palette.color(QPalette::Text));
    QFont f = p->font(); f.setPointSizeF(f.pointSizeF() * 0.9); f.setBold(cur); p->setFont(f);
    const int numbering = panel_.c_.ed.xpp().sidebarNumbering;   // Preferencias > Ver: none / below the preview / in a circle / in a square on the preview
    if (numbering == 1) p->drawText(QRect(o.rect.left(), box.bottom() + 4, o.rect.width(), 20), Qt::AlignHCenter | Qt::AlignTop, QString::number(page + 1));
    else if (numbering >= 2) {
      const QRect badge(box.left() + 4, box.top() + 4, 22, 22);
      p->setRenderHint(QPainter::Antialiasing, true);
      p->setPen(Qt::NoPen);
      p->setBrush(QColor(0, 0, 0, 170));
      if (numbering == 2) p->drawEllipse(badge); else p->drawRect(badge);
      p->setPen(Qt::white);
      p->drawText(badge, Qt::AlignCenter, QString::number(page + 1));
    }
    p->restore();
  }
 private:
  ThumbsPanel& panel_;
};

ThumbsPanel::ThumbsPanel(Chrome& c, QWidget* parent) : QWidget(parent), c_(c) {
  setObjectName("thumbsPanel");
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);
  auto* tools = new QHBoxLayout;
  tools->setContentsMargins(6, 6, 6, 6);
  Editor& ed = c.ed;
  QToolButton* add = panelButton(c, this, "add", "plus", "Nueva página después de la actual");
  QToolButton* del = panelButton(c, this, "delete", "trash", "Eliminar la página actual");
  QToolButton* up = panelButton(c, this, "up", "up", "Subir la página");
  QToolButton* down = panelButton(c, this, "down", "down", "Bajar la página");
  for (QToolButton* b : {add, del, up, down}) tools->addWidget(b);
  tools->setSpacing(4);
  lay->addLayout(tools);
  connect(add, &QToolButton::clicked, this, [&ed] { ed.insertPage(ed.currentPage() + 1, ed.currentPage()); });
  connect(del, &QToolButton::clicked, this, [&ed] { ed.deletePage(ed.currentPage()); });
  connect(up, &QToolButton::clicked, this, [&ed] { ed.movePage(ed.currentPage(), -1); });
  connect(down, &QToolButton::clicked, this, [&ed] { ed.movePage(ed.currentPage(), +1); });

  list_ = new QListWidget(this);
  list_->setObjectName("thumbList");
  list_->setUniformItemSizes(false);
  list_->setResizeMode(QListView::Adjust);   // the rows follow the width of the panel (176 px): without it they keep the width of the first layout
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list_->setSelectionMode(QAbstractItemView::NoSelection);
  list_->setFocusPolicy(Qt::NoFocus);
  list_->setMouseTracking(true);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  list_->setItemDelegate(new ThumbDelegate(*this));
  list_->setMinimumWidth(kThumbW + 34);
  lay->addWidget(list_, 1);

  connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { c_.goToPage(list_->row(it)); });
  connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) { contextMenu(p); });
  renderTimer_.setSingleShot(true);
  connect(&renderTimer_, &QTimer::timeout, this, [this] {
    if (pending_.isEmpty() || inflight_ >= 0) return;   // one worker job at a time; its result restarts this timer
    // the page nearest the top of what is visible first: it is what the eye reaches first
    const int top = list_->indexAt(QPoint(5, 5)).row();
    int best = *pending_.begin();
    for (int p : pending_) if (std::abs(p - top) < std::abs(best - top)) best = p;
    pending_.remove(best);
    startAsync(best);
  });
  dirtyTimer_.setSingleShot(true);
  connect(&dirtyTimer_, &QTimer::timeout, this, [this] {   // the page changed (debounced: a stroke is many events): forget the old picture, the row asks again when painted
    if (dirtyAll_) cache_.clear(); else for (int p : dirtyPages_) cache_.erase(p);
    dirtyAll_ = false;
    dirtyPages_.clear();
    list_->viewport()->update();
  });

  connect(&ed, &Editor::documentReplaced, this, [this] { invalidate(-1, true); rebuild(); });
  connect(&ed, &Editor::structureChanged, this, [this] { invalidate(-1, true); rebuild(); });
  connect(&ed, &Editor::appearanceChanged, this, [this] { invalidate(-1); });
  connect(&ed, &Editor::contentChanged, this, [this](int page, const QRectF&) { invalidate(page < 0 ? -1 : page); });
  connect(&ed, &Editor::elementAppended, this, [this](int page, int, int, const QRectF&) { invalidate(page); });
  connect(&ed, &Editor::layersChanged, this, [this](int page) { invalidate(page); });
  connect(&ed, &Editor::currentPageChanged, this, [this] { syncCurrent(); });
  connect(&c, &Chrome::themeChanged, this, [this] { list_->viewport()->update(); });
  pool_ = new QThreadPool(this);
  pool_->setMaxThreadCount(1);
  rebuild();
}

ThumbsPanel::~ThumbsPanel() { pool_->waitForDone(); }

QToolButton* ThumbsPanel::tool(const QString& name) const { return findChild<QToolButton*>(name); }

QPixmap ThumbsPanel::cached(int page) const {
  const auto it = cache_.find(page);
  return it == cache_.end() ? QPixmap() : it->second;
}

void ThumbsPanel::request(int page) {
  if (cache_.count(page) || pending_.contains(page)) return;
  pending_.insert(page);
  if (!renderTimer_.isActive()) renderTimer_.start(0);
}

void ThumbsPanel::invalidate(int page, bool now) {
  if (page < 0) { dirtyAll_ = true; epoch_++; } else { dirtyPages_.insert(page); pageGen_[page]++; }
  if (now || (!isVisible() && !list_->isVisible())) {   // nobody sees it: forget now, nothing to repaint
    if (dirtyAll_) cache_.clear(); else for (int p : dirtyPages_) cache_.erase(p);
    dirtyAll_ = false;
    dirtyPages_.clear();
    pending_.clear();
    list_->viewport()->update();
    return;
  }
  dirtyTimer_.start(250);
}

namespace {
/** The thumbnail of `pg` (only its visible layers) as an image: no widgets, no document access, so it can run on any thread. */
QImage drawThumb(const Page& pg, const RenderOptions& base, double dpr) {
  const double ratio = pg.height() / std::max(1.0, pg.width());
  const int w = int(std::lround(kThumbW * dpr)), h = std::max(1, int(std::lround(kThumbW * ratio * dpr)));
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.setDevicePixelRatio(dpr);
  img.fill(Qt::white);
  QPainter qp(&img);
  qp.setRenderHint(QPainter::Antialiasing, true);
  qp.setRenderHint(QPainter::SmoothPixmapTransform, true);
  qp.scale(w / (pg.width() * dpr), h / (pg.height() * dpr));   // page units -> logical thumbnail pixels
  RenderOptions o = base;
  o.scale = w / std::max(1.0, pg.width());
  o.lite = true;
  renderBackground(qp, pg, o);
  renderLayers(qp, pg, o, {});
  return img;
}
}  // namespace

void ThumbsPanel::storeThumb(int page, QImage img) {
  if (cache_.size() > size_t(kMaxCached)) cache_.erase(cache_.begin() == cache_.find(page) ? std::next(cache_.begin()) : cache_.begin());
  cache_[page] = QPixmap::fromImage(std::move(img));
  rendered_++;
  if (QListWidgetItem* it = list_->item(page)) list_->viewport()->update(list_->visualItemRect(it));
}

void ThumbsPanel::renderOne(int page) {
  const Document& d = c_.ed.doc();
  if (page < 0 || page >= int(d.pages.size())) return;
  const Page& pg = d.pages[size_t(page)];
  const std::vector<int> hidden = c_.ed.hiddenLayers(page);
  Page vis;   // the visible layers only: renderLayers then has nothing to filter
  vis.attrs = pg.attrs;
  vis.background = pg.background;
  for (size_t l = 0; l < pg.layers.size(); l++)
    if (std::find(hidden.begin(), hidden.end(), int(l)) == hidden.end()) vis.layers.push_back(pg.layers[l]);
  storeThumb(page, drawThumb(vis, c_.win.view() ? c_.win.view()->renderBase() : RenderOptions(), list_->devicePixelRatioF()));
}

void ThumbsPanel::startAsync(int page) {
  const Document& d = c_.ed.doc();
  if (page < 0 || page >= int(d.pages.size())) { if (!pending_.isEmpty()) renderTimer_.start(0); return; }
  const Page& pg = d.pages[size_t(page)];
  const std::vector<int> hidden = c_.ed.hiddenLayers(page);
  auto vis = std::make_shared<Page>();   // a copy: the document may change while the worker draws
  vis->attrs = pg.attrs;
  vis->background = pg.background;
  for (size_t l = 0; l < pg.layers.size(); l++)
    if (std::find(hidden.begin(), hidden.end(), int(l)) == hidden.end()) vis->layers.push_back(pg.layers[l]);
  const RenderOptions base = c_.win.view() ? c_.win.view()->renderBase() : RenderOptions();
  const double dpr = list_->devicePixelRatioF();
  const uint64_t epoch = epoch_, gen = pageGen_[page];
  inflight_ = page;
  pool_->start([this, vis, base, dpr, page, epoch, gen] {
    QThread::currentThread()->setPriority(QThread::LowPriority);
    QImage img = drawThumb(*vis, base, dpr);
    QMetaObject::invokeMethod(this, [this, img = std::move(img), page, epoch, gen]() mutable {
      inflight_ = -1;
      if (epoch == epoch_ && gen == pageGen_[page] && page < list_->count()) storeThumb(page, std::move(img));
      else request(page);   // it changed while it was drawn: draw it again (the row asks again when painted, this covers rows already on screen)
      if (!pending_.isEmpty()) renderTimer_.start(0);
    }, Qt::QueuedConnection);
  });
}

int ThumbsPanel::renderPending() {
  int n = 0;
  renderTimer_.stop();
  if (inflight_ >= 0) { pool_->waitForDone(); QCoreApplication::sendPostedEvents(this, QEvent::MetaCall); }   // the worker's result lands first
  const QSet<int> todo = pending_;
  pending_.clear();
  for (int p : todo) { renderOne(p); n++; }
  return n;
}

void ThumbsPanel::rebuild() {
  list_->clear();
  for (int i = 0; i < c_.ed.pageCount(); i++) list_->addItem(new QListWidgetItem);
  syncCurrent();
}

void ThumbsPanel::syncCurrent() {
  list_->viewport()->update();
  const int cur = c_.ed.currentPage();
  if (cur >= 0 && cur < list_->count()) list_->scrollToItem(list_->item(cur), QAbstractItemView::EnsureVisible);
}

void ThumbsPanel::contextMenu(const QPoint& pos) {
  const QModelIndex idx = list_->indexAt(pos);
  if (!idx.isValid()) return;
  c_.goToPage(idx.row(), false);   // the actions work on the current page
  Editor& ed = c_.ed;
  QMenu m(this);
  m.addAction("Nueva página después", [&ed] { ed.insertPage(ed.currentPage() + 1, ed.currentPage()); });
  m.addAction("Insertar página antes", [&ed] { ed.insertPage(ed.currentPage(), ed.currentPage()); });
  m.addSeparator();
  m.addAction("Mover hacia arriba", [&ed] { ed.movePage(ed.currentPage(), -1); })->setEnabled(idx.row() > 0);
  m.addAction("Mover hacia abajo", [&ed] { ed.movePage(ed.currentPage(), +1); })->setEnabled(idx.row() < ed.pageCount() - 1);
  m.addSeparator();
  m.addAction("Eliminar página", [&ed] { ed.deletePage(ed.currentPage()); })->setEnabled(ed.pageCount() > 1);
  m.exec(list_->viewport()->mapToGlobal(pos));
}

// ---- layers --------------------------------------------------------------------------------------------------------------------------------
class LayerList : public QListWidget {
 public:
  LayerList(LayersPanel& p) : QListWidget(&p), panel_(p) {}
 protected:
  void mousePressEvent(QMouseEvent* e) override {
    const QListWidgetItem* it = itemAt(e->position().toPoint());
    if (it && e->button() == Qt::LeftButton) {
      const int layer = it->data(Qt::UserRole).toInt();
      if (e->position().toPoint().x() < 40) { panel_.toggleHidden(layer); return; }
      panel_.setActive(layer);
    }
    QListWidget::mousePressEvent(e);
  }
 private:
  LayersPanel& panel_;
};

class LayerDelegate : public QStyledItemDelegate {
 public:
  LayerDelegate(LayersPanel& p) : QStyledItemDelegate(&p), panel_(p) {}
  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(100, 40); }
  void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& idx) const override {
    Chrome& c = panel_.c_;
    const int layer = idx.data(Qt::UserRole).toInt();
    const int page = c.ed.currentPage();
    const bool active = layer == c.ed.activeLayer(page);
    const bool hidden = !c.ed.layerVisible(page, layer);
    const bool neonOn = c.theme() == "neon";
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = o.rect.adjusted(3, 2, -3, -2);
    if (active) { p->setPen(neonOn ? QPen(neon().pink, 1) : Qt::NoPen); p->setBrush(neonOn ? QColor(255, 42, 157, 64) : QColor("#2f6fd0")); p->drawRoundedRect(r, 4, 4); }
    else if (o.state & QStyle::State_MouseOver) { p->setPen(Qt::NoPen); p->setBrush(neonOn ? QColor(185, 103, 255, 46) : QColor("#454545")); p->drawRoundedRect(r, 4, 4); }
    const QColor fg = o.palette.color(QPalette::Text);
    const QPoint eye(r.left() + 20, r.center().y());
    p->setPen(QPen(neonOn ? neon().cyan : fg, 1.6));
    p->setBrush(hidden ? Qt::NoBrush : QBrush(neonOn ? neon().cyan : fg));
    p->drawEllipse(eye, 6, 6);
    p->setOpacity(hidden ? 0.45 : 1.0);
    QFont f = p->font();
    f.setStrikeOut(hidden);
    p->setFont(f);
    p->setPen(active && !neonOn ? Qt::white : fg);
    const QRect nameR(r.left() + 40, r.top(), r.width() - 40 - 40, r.height());
    p->drawText(nameR, Qt::AlignVCenter | Qt::AlignLeft, p->fontMetrics().elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameR.width()));
    p->setOpacity(0.6);
    f.setStrikeOut(false); f.setPointSizeF(f.pointSizeF() * 0.85); p->setFont(f);
    p->drawText(QRect(r.right() - 40, r.top(), 36, r.height()), Qt::AlignVCenter | Qt::AlignRight, idx.data(Qt::UserRole + 1).toString());
    p->restore();
  }
 private:
  LayersPanel& panel_;
};

LayersPanel::LayersPanel(Chrome& c, QWidget* parent) : QWidget(parent), c_(c) {
  setObjectName("layersPanel");
  Editor& ed = c.ed;
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);
  auto* tools = new QHBoxLayout;
  tools->setContentsMargins(6, 6, 6, 6);
  QToolButton* add = panelButton(c, this, "add", "plus", "Nueva capa sobre la activa");
  QToolButton* del = panelButton(c, this, "delete", "trash", "Eliminar la capa activa con todo su contenido");
  QToolButton* up = panelButton(c, this, "up", "up", "Subir la capa activa (se dibuja encima)");
  QToolButton* down = panelButton(c, this, "down", "down", "Bajar la capa activa");
  for (QToolButton* b : {add, del, up, down}) tools->addWidget(b);
  tools->setSpacing(4);
  lay->addLayout(tools);
  connect(add, &QToolButton::clicked, this, [&ed] { layerops::add(ed, ed.currentPage()); });
  connect(del, &QToolButton::clicked, this, [&ed] { layerops::remove(ed, ed.currentPage(), ed.activeLayer(ed.currentPage())); });
  connect(up, &QToolButton::clicked, this, [&ed] { layerops::move(ed, ed.currentPage(), ed.activeLayer(ed.currentPage()), +1); });
  connect(down, &QToolButton::clicked, this, [&ed] { layerops::move(ed, ed.currentPage(), ed.activeLayer(ed.currentPage()), -1); });

  list_ = new LayerList(*this);
  list_->setObjectName("layerList");
  list_->setItemDelegate(new LayerDelegate(*this));
  list_->setMouseTracking(true);
  list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  list_->setSelectionMode(QAbstractItemView::NoSelection);
  list_->setMinimumWidth(kThumbW + 34);
  lay->addWidget(list_, 1);
  auto* note = new QLabel("Mostrar u ocultar una capa solo afecta a la pantalla: el archivo guarda todas.", this);
  note->setWordWrap(true);
  note->setContentsMargins(8, 6, 8, 8);
  note->setStyleSheet("font-size: 11px; opacity: .6;");
  lay->addWidget(note);

  connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {   // the inline editor finished: a new name (empty = the saved name goes)
    if (refreshing_) return;
    const int layer = it->data(Qt::UserRole).toInt();
    layerops::rename(c_.ed, c_.ed.currentPage(), layer, it->text());
    scheduleRefresh();
  });
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
  connect(&ed, &Editor::documentReplaced, this, [this] { scheduleRefresh(); });
  connect(&ed, &Editor::structureChanged, this, [this] { scheduleRefresh(); });
  connect(&ed, &Editor::historyChanged, this, [this] { scheduleRefresh(); });
  connect(&ed, &Editor::currentPageChanged, this, [this] { scheduleRefresh(); });
  connect(&ed, &Editor::layersChanged, this, [this] { scheduleRefresh(); });
  connect(&ed, &Editor::contentChanged, this, [this] { if (isVisible()) scheduleRefresh(); });
  connect(&ed, &Editor::elementAppended, this, [this] { if (isVisible()) scheduleRefresh(); });
  connect(&c, &Chrome::themeChanged, this, [this] { list_->viewport()->update(); });
  refresh();
}

QToolButton* LayersPanel::tool(const QString& name) const { return findChild<QToolButton*>(name); }

int LayersPanel::layerOfRow(int row) const {
  const QListWidgetItem* it = list_->item(row);
  return it ? it->data(Qt::UserRole).toInt() : -1;
}

void LayersPanel::toggleHidden(int layer) {
  Editor& ed = c_.ed;
  ed.selection().clear();   // hidden content cannot be selected
  ed.setLayerVisible(ed.currentPage(), layer, !ed.layerVisible(ed.currentPage(), layer));
}

void LayersPanel::setActive(int layer) { c_.ed.setActiveLayer(c_.ed.currentPage(), layer); }

void LayersPanel::refresh() {
  if (list_->viewport()->findChild<QLineEdit*>()) return;   // do not pull the editor from under the user
  refreshing_ = true;
  list_->clear();
  const Editor& ed = c_.ed;
  const int page = ed.currentPage();
  if (page >= 0 && page < ed.pageCount()) {
    const Page& pg = ed.doc().pages[size_t(page)];
    for (int k = int(pg.layers.size()) - 1; k >= 0; k--) {   // the top layer first, as in a drawing program
      auto* it = new QListWidgetItem(layerName(pg.layers[size_t(k)], k));
      it->setData(Qt::UserRole, k);
      it->setData(Qt::UserRole + 1, int(pg.layers[size_t(k)].elements.size()));
      it->setFlags(it->flags() | Qt::ItemIsEditable);
      it->setToolTip("Doble clic para renombrar");
      list_->addItem(it);
    }
  }
  refreshing_ = false;
}

}  // namespace chrome
