#include "chrome/svgicon.h"
#include "toolboxwidget.h"
#include "settings.h"
#include "wrapbox.h"
#include "icons.h"
#include "palettes.h"
#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int kItem = 52;        // big enough for a finger or a pen on a tablet
constexpr int kBoxW = 540;       // width of a zone: 9 buttons per line
const char* kMime = "application/x-xl-toolbox";

const char* kStyle = R"(
#tbxRoot { background: #2b2b2b; border: 1px solid #6a6a6a; }
QLabel { color: #eeeeee; }
QLabel[tbx="hint"] { color: #a8a8a8; font-size: 12px; }
QLabel[tbx="group"] { color: #a8a8a8; font-size: 11px; }
QToolButton[tbx="item"] { background: #383838; border: 1px solid #4a4a4a; border-radius: 8px; padding: 0; }
QToolButton[tbx="item"]:hover { background: #454545; }
QToolButton[tbx="item"]:disabled { background: #2f2f2f; border-color: #3a3a3a; }
QToolButton[tbx="item"][active="true"] { background: #2f6fd0; border-color: #6aa0ff; }
QToolButton[tbx="item"][used="true"] { background: #2d2d2d; border-color: #3c3c3c; }
QToolButton[tbx="zdel"] { color: #ff9a9a; background: #3a2a2a; border: 1px solid #6a3a3a; border-radius: 6px; font-size: 22px; font-weight: bold; }
QPushButton[tbx="small"], QPushButton[tbx="chip"] { background: #3a3a3a; color: #eeeeee; border: 1px solid #555555; border-radius: 6px; padding: 4px 14px; min-height: 28px; }
QPushButton[tbx="chip"] { border-radius: 15px; padding: 3px 12px; min-height: 24px; font-size: 12px; }
QPushButton[tbx="small"]:hover, QPushButton[tbx="chip"]:hover { background: #484848; }
QPushButton[tbx="small"][on="true"], QPushButton[tbx="chip"][on="true"] { background: #2f6fd0; border-color: #6aa0ff; color: #ffffff; }
QPushButton[tbx="small"]:disabled { color: #777777; }
QLineEdit { background: #333333; color: #eeeeee; border: 1px solid #555555; border-radius: 6px; padding: 6px; font-size: 13px; }
QCheckBox { color: #eeeeee; spacing: 8px; }
QCheckBox::indicator { width: 20px; height: 20px; }
QScrollArea { border: 0; background: transparent; }
)";

QString plain(const QString& v) {
  QString d = v.normalized(QString::NormalizationForm_D), out;
  for (const QChar c : d) if (c.category() != QChar::Mark_NonSpacing) out += c;
  return out.toLower();
}

void repolish(QWidget* w) { w->style()->unpolish(w); w->style()->polish(w); }

QPushButton* smallBtn(QWidget* parent, const QString& text, const char* kind = "small") {
  auto* b = new QPushButton(text, parent);
  b->setProperty("tbx", kind);
  b->setCursor(Qt::PointingHandCursor);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

/** A drag that began on a button or separator of the box (or of the toolbar). When nothing accepted it, `ignored` runs (taken out of the panel and let go elsewhere). */
void beginDrag(QWidget* src, const ToolboxDrag& d, const std::function<void()>& ignored) {
  auto* drag = new QDrag(src);
  drag->setMimeData(ToolboxWidget::makeMime(d));
  drag->setPixmap(src->grab());
  drag->setHotSpot(QPoint(src->width() / 2, src->height() / 2));
  const Qt::DropAction r = drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
  if (r == Qt::IgnoreAction && ignored) ignored();
}

class TbxButton : public QToolButton {
 public:
  TbxButton(QWidget* p, const ToolboxDrag& d) : QToolButton(p), drag_(d) {}
  std::function<void()> onIgnored;
  bool draggable = false;
  ToolboxDrag drag_;

 protected:
  void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) press_ = e->position().toPoint(); QToolButton::mousePressEvent(e); }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (draggable && (e->buttons() & Qt::LeftButton) && (e->position().toPoint() - press_).manhattanLength() >= QApplication::startDragDistance()) {
      setDown(false);
      beginDrag(this, drag_, onIgnored);
      return;
    }
    QToolButton::mouseMoveEvent(e);
  }

 private:
  QPoint press_;
};

class TbxSep : public QWidget {
 public:
  TbxSep(QWidget* p, const ToolboxDrag& d, bool editing) : QWidget(p), drag_(d), editing_(editing) {
    setFixedSize(editing ? 13 : 11, 34);
    setCursor(editing && d.row >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
  }
  std::function<void()> onIgnored;
  bool draggable = false;

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setPen(QPen(editing_ ? QColor("#bbbbbb") : QColor("#888888"), editing_ ? 2 : 1));
    p.drawLine(width() / 2, 2, width() / 2, height() - 3);
  }
  void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) press_ = e->position().toPoint(); }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (draggable && (e->buttons() & Qt::LeftButton) && (e->position().toPoint() - press_).manhattanLength() >= QApplication::startDragDistance()) beginDrag(this, drag_, onIgnored);
  }

 private:
  ToolboxDrag drag_;
  bool editing_;
  QPoint press_;
};

/** The title bar: dragging it moves the box. */
class HeadBar : public QWidget {
 public:
  explicit HeadBar(QWidget* p) : QWidget(p) { setCursor(Qt::SizeAllCursor); }

 protected:
  void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) off_ = e->globalPosition().toPoint() - window()->pos(); }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (!(e->buttons() & Qt::LeftButton)) return;
    QPoint p = e->globalPosition().toPoint() - off_;
    const QRect a = (QGuiApplication::screenAt(e->globalPosition().toPoint()) ? QGuiApplication::screenAt(e->globalPosition().toPoint()) : QGuiApplication::primaryScreen())->availableGeometry();
    p.setX(std::clamp(p.x(), a.left() - window()->width() + 60, a.right() - 40));
    p.setY(std::clamp(p.y(), a.top(), a.bottom() - 30));
    window()->move(p);
  }

 private:
  QPoint off_;
};
}  // namespace

QMimeData* ToolboxWidget::makeMime(const ToolboxDrag& d) {
  QJsonObject o{{"id", d.id}};
  if (d.row >= 0) { o["row"] = d.row; o["index"] = d.index; }
  auto* m = new QMimeData;
  m->setData(kMime, QJsonDocument(o).toJson(QJsonDocument::Compact));
  return m;
}

std::optional<ToolboxDrag> ToolboxWidget::parseMime(const QMimeData* m) {
  if (!m || !m->hasFormat(kMime)) return std::nullopt;
  const QJsonObject o = QJsonDocument::fromJson(m->data(kMime)).object();
  if (!o.value("id").isString() || !isToolboxItemId(o.value("id").toString())) return std::nullopt;
  ToolboxDrag d;
  d.id = o.value("id").toString();
  if (o.value("row").isDouble() && o.value("index").isDouble()) { d.row = o.value("row").toInt(); d.index = o.value("index").toInt(); }
  return d;
}

ToolboxWidget::ToolboxWidget(SettingsController& c, QWidget* parent) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), c_(c) {
  setObjectName("tbxRoot");
  setAttribute(Qt::WA_StyledBackground);
  setFocusPolicy(Qt::StrongFocus);   // Esc and Q are read by the box itself: its own window is not the one the window's shortcuts belong to
  setWindowTitle(QString::fromUtf8("Cuadro de herramientas"));
  setStyleSheet(chrome::neonizeCss(QString::fromLatin1(kStyle)));
  group_ = toolboxGroups().first();
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(10, 8, 10, 10);
  layout_->setSpacing(6);
  auto* head = new HeadBar(this);
  auto* hl = new QHBoxLayout(head);
  hl->setContentsMargins(0, 0, 0, 0);
  hl->setSpacing(8);
  title_ = new QLabel(head);
  title_->setStyleSheet("font-weight: bold; font-size: 14px;");
  title_->setToolTip(QString::fromUtf8("Arrastra para mover el cuadro"));
  title_->setAttribute(Qt::WA_TransparentForMouseEvents);
  editBtn_ = smallBtn(head, "Editar");
  editBtn_->setToolTip(QString::fromUtf8("Arrastra botones desde el catálogo o desde la barra de herramientas, ordénalos y pásalos de una zona a otra; sácalos del cuadro para quitarlos"));
  connect(editBtn_, &QPushButton::clicked, this, [this] { setEditing(!editing_); });
  auto* x = smallBtn(head, QString::fromUtf8("✕"));
  x->setToolTip(QString::fromUtf8("Cerrar (Esc)"));
  x->setFixedWidth(42);
  connect(x, &QPushButton::clicked, this, [this] { closeBox(); });
  hl->addWidget(title_, 1);
  hl->addWidget(editBtn_);
  hl->addWidget(x);
  layout_->addWidget(head);
}

bool ToolboxWidget::open(const QPoint& globalPos) {
  s_ = c_.toolboxSettings();
  if (!s_.enabled) return false;
  at_ = globalPos;
  editing_ = false;
  render();
  show();
  place();
  raise();
  activateWindow();
  return true;
}

void ToolboxWidget::closeBox() {
  if (!isVisible()) { editing_ = false; return; }
  const bool wasActive = isActiveWindow();
  editing_ = false;
  hide();
  if (wasActive && parentWidget()) parentWidget()->activateWindow();   // the page gets the keyboard back
}

void ToolboxWidget::setEditing(bool on) {
  editing_ = on;
  render();
  place();
}

void ToolboxWidget::outsidePress(const QPointF& globalPos) {
  if (!isVisible() || editing_) return;
  if (frameGeometry().contains(globalPos.toPoint())) return;
  if (QWidget* pop = QApplication::activePopupWidget()) {   // a menu or a combo list of the box (or one the box's controls opened) is part of it
    if (pop->frameGeometry().contains(globalPos.toPoint())) return;
    for (const QWidget* w = pop; w; w = w->parentWidget()) if (w == this) return;
  }
  closeBox();
}

void ToolboxWidget::keyPressEvent(QKeyEvent* e) {
  const bool typing = qobject_cast<QLineEdit*>(focusWidget()) != nullptr;
  if (e->key() == Qt::Key_Escape) { closeBox(); return; }
  if (!typing && e->key() == Qt::Key_Q && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier))) { closeBox(); return; }
  QWidget::keyPressEvent(e);
}

void ToolboxWidget::keepOnScreen() {
  QScreen* sc = QGuiApplication::screenAt(frameGeometry().center());
  if (!sc) sc = QGuiApplication::primaryScreen();
  const QRect a = sc->availableGeometry();
  const int x = std::clamp(pos().x(), a.left() + 4, std::max(a.left() + 4, a.right() - width() - 4));
  const int y = std::clamp(pos().y(), a.top() + 4, std::max(a.top() + 4, a.bottom() - height() - 4));
  move(x, y);
}

void ToolboxWidget::place() {
  adjustSize();
  QScreen* sc = QGuiApplication::screenAt(at_);
  if (!sc) sc = QGuiApplication::primaryScreen();
  const QRect a = sc->availableGeometry();
  const int x = std::clamp(at_.x() - width() / 2, a.left() + 4, std::max(a.left() + 4, a.right() - width() - 4));
  const int y = std::clamp(at_.y() - 24, a.top() + 4, std::max(a.top() + 4, a.bottom() - height() - 4));
  move(x, y);
}

void ToolboxWidget::commit() {
  c_.applyToolbox(s_);
  render();
  adjustSize();       // growing or shrinking keeps the top left where the user put it, inside the screen
  keepOnScreen();
}

// ---- model edits ---------------------------------------------------------------------------------------------------------------------------
bool ToolboxWidget::dropOn(int row, const ToolboxDrag& d, int index) {
  if (!toolboxDrop(s_, d, row, index)) return false;
  commit();
  return true;
}
void ToolboxWidget::removeAt(int row, int index) { if (toolboxRemoveAt(s_, row, index)) commit(); }
void ToolboxWidget::removeZone(int row) { if (toolboxRemoveRow(s_, row)) commit(); }
void ToolboxWidget::addZone() { if (toolboxAddRow(s_)) commit(); }
void ToolboxWidget::restoreDefault() { s_.rows = defaultToolbox().rows; commit(); }

// ---- drawing -------------------------------------------------------------------------------------------------------------------------------
QWidget* ToolboxWidget::buttonFor(const ToolboxItem& item, int row, int index) {
  const bool inZone = row >= 0;
  ToolboxDrag d{item.id, inZone ? row : -1, inZone ? index : -1};
  if (item.id == kToolboxSeparator) {
    auto* s = new TbxSep(this, d, editing_);
    s->setToolTip(item.label);
    if (editing_) {
      s->draggable = true;
      if (inZone) s->onIgnored = [this, row, index] { removeAt(row, index); };
    }
    return s;
  }
  auto* b = new TbxButton(this, d);
  b->setProperty("tbx", "item");
  b->setFixedSize(kItem, kItem);
  b->setProperty("wrapSize", QSize(kItem, kItem));
  b->setIconSize(QSize(30, 30));
  b->setIcon(c_.iconOfItem(item.id, item.icon, item.label));
  b->setToolTip(item.label);
  b->setFocusPolicy(Qt::NoFocus);
  b->setCursor(editing_ ? Qt::OpenHandCursor : Qt::PointingHandCursor);
  if (inZone && !editing_ && c_.itemActive(item.id)) b->setProperty("active", true);
  if (editing_) {
    b->draggable = true;
    if (inZone) b->onIgnored = [this, row, index] { removeAt(row, index); };
  } else if (inZone) {
    const QString aid = SettingsController::actionIdOf(item.id);
    if (!aid.isEmpty()) {   // something that does nothing yet (a tool another area has not registered) is shown dimmed
      const auto it = c_.editor().actions().find(aid);
      const bool ok = it != c_.editor().actions().end() && it->second->isEnabled();
      if (!ok && aid != "edit.undo" && aid != "edit.redo") { b->setEnabled(false); b->setToolTip(item.label + QString::fromUtf8(" (aún no disponible)")); }
    }
    connect(b, &QToolButton::clicked, this, [this, id = item.id] {
      c_.runToolboxItem(id);
      if (s_.closeOnPick) closeBox(); else render();
    });
  }
  return b;
}

void ToolboxWidget::render() {
  if (body_) { layout_->removeWidget(body_); body_->hide(); body_->deleteLater(); }
  zones_.clear();
  pool_ = chips_ = nullptr;
  search_ = nullptr;
  title_->setText(editing_ ? QString::fromUtf8("Editar el cuadro") : QString::fromUtf8("Herramientas"));
  editBtn_->setText(editing_ ? "Listo" : "Editar");
  editBtn_->setProperty("on", editing_);
  repolish(editBtn_);

  body_ = new QWidget(this);
  auto* v = new QVBoxLayout(body_);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(5);
  const QStringList colors = c_.paletteColors();
  int shown = 0;
  for (int ri = 0; ri < int(s_.rows.size()); ri++) {
    struct Known { ToolboxItem item; int index; };
    std::vector<Known> items;
    for (int i = 0; i < s_.rows[size_t(ri)].size(); i++)
      if (auto it = toolboxItemById(s_.rows[size_t(ri)][i], colors)) items.push_back({*it, i});
    if (items.empty() && !editing_) continue;
    shown += int(items.size());
    auto* box = new WrapBox(kBoxW, body_);
    box->setMinRowHeight(kItem + 8);
    Zone z;
    z.box = box;
    z.row = ri;
    for (const Known& k : items) { box->add(buttonFor(k.item, ri, k.index)); z.modelIndex.push_back(k.index); }
    if (items.empty()) {
      auto* l = new QLabel(QString::fromUtf8("Arrastra aquí los botones"), box);
      l->setProperty("tbx", "hint");
      l->setContentsMargins(8, 0, 0, 0);
      l->setFixedSize(220, kItem);
      box->add(l);
    }
    if (editing_) {
      box->setDropTarget(true);
      auto* del = new QToolButton(box);
      del->setProperty("tbx", "zdel");
      del->setText(QString::fromUtf8("×"));
      del->setFixedSize(30, 30);
      del->setToolTip(QString::fromUtf8("Quitar esta zona y sus botones"));
      del->setCursor(Qt::PointingHandCursor);
      connect(del, &QToolButton::clicked, this, [this, ri] { removeZone(ri); });
      box->setCorner(del);
      const std::vector<int> mi = z.modelIndex;
      box->accepts = [](const QMimeData* m) { return parseMime(m).has_value(); };
      box->onDrop = [this, ri, mi](const QMimeData* m, int at) {
        const auto d = parseMime(m);
        if (!d) return false;
        const int idx = at >= 0 && at < int(mi.size()) ? mi[size_t(at)] : -1;
        return dropOn(ri, *d, idx);
      };
    }
    box->relayout();
    zones_.push_back(z);
    v->addWidget(box);
  }
  if (!shown && !editing_) { auto* l = new QLabel(QString::fromUtf8("Vacío: pulsa Editar"), body_); l->setProperty("tbx", "hint"); v->addWidget(l); }

  if (editing_) {
    auto* add = smallBtn(body_, "+ Zona");
    add->setToolTip(QString::fromUtf8("Añadir una zona (una fila) más"));
    add->setEnabled(int(s_.rows.size()) < kToolboxMaxRows);
    connect(add, &QPushButton::clicked, this, [this] { addZone(); });
    v->addWidget(add, 0, Qt::AlignLeft);

    auto* cat = new QLabel(QString::fromUtf8("CATÁLOGO"), body_);
    cat->setProperty("tbx", "group");
    v->addWidget(cat);
    search_ = new QLineEdit(body_);
    search_->setPlaceholderText(QString::fromUtf8("Buscar un botón…"));
    search_->setClearButtonEnabled(true);
    search_->setText(query_);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) { query_ = t; renderPool(); });
    v->addWidget(search_);
    chips_ = new WrapBox(kBoxW, body_);
    v->addWidget(chips_);
    auto* scroll = new QScrollArea(body_);
    scroll->setFixedSize(kBoxW + 6, 232);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pool_ = new WrapBox(kBoxW - 14, scroll);
    pool_->setMinRowHeight(kItem + 8);
    pool_->setDropTarget(true);
    pool_->setBorderColor(QColor("#666666"));
    pool_->fill_ = QColor(255, 255, 255, 14);
    pool_->accepts = [](const QMimeData* m) { const auto d = parseMime(m); return d && d->row >= 0; };
    pool_->onDrop = [this](const QMimeData* m, int) {   // a button of a zone let go on the catalogue is taken out
      const auto d = parseMime(m);
      if (!d || d->row < 0) return false;
      removeAt(d->row, d->index);
      return true;
    };
    scroll->setWidget(pool_);
    v->addWidget(scroll);
    renderPool();

    auto* opts = new QWidget(body_);
    auto* ol = new QHBoxLayout(opts);
    ol->setContentsMargins(0, 4, 0, 0);
    ol->setSpacing(8);
    auto* cb = new QCheckBox(QString::fromUtf8("Cerrar al elegir"), opts);
    cb->setChecked(s_.closeOnPick);
    connect(cb, &QCheckBox::toggled, this, [this](bool on) { s_.closeOnPick = on; c_.applyToolbox(s_); });
    auto* reset = smallBtn(opts, "Restaurar");
    connect(reset, &QPushButton::clicked, this, [this] { restoreDefault(); });
    auto* off = smallBtn(opts, "Desactivar el cuadro");
    off->setToolTip(QString::fromUtf8("Se vuelve a activar desde Herramientas ▸ Activar o desactivar el cuadro de herramientas"));
    connect(off, &QPushButton::clicked, this, [this] { s_.enabled = false; c_.applyToolbox(s_); closeBox(); });
    ol->addWidget(cb, 1);
    ol->addWidget(reset);
    ol->addWidget(off);
    v->addWidget(opts);
  }
  layout_->addWidget(body_);
  body_->show();
}

void ToolboxWidget::setPoolGroup(const QString& g) { group_ = g; query_.clear(); if (search_) search_->clear(); renderPool(); }

void ToolboxWidget::renderPool() {
  if (!pool_ || !chips_) return;
  const QString q = plain(query_.trimmed());
  // the chips (categories) are rebuilt with the pool: the current one is lit unless a search is on
  for (QWidget* w : chips_->items()) w->deleteLater();
  chips_->items_.clear();
  for (const QString& g : toolboxGroups()) {
    auto* chip = smallBtn(chips_, g, "chip");
    chip->setProperty("on", q.isEmpty() && g == group_);
    connect(chip, &QPushButton::clicked, this, [this, g] { setPoolGroup(g); });
    chips_->add(chip);
  }
  chips_->relayout();
  for (QWidget* w : pool_->items()) { w->hide(); w->deleteLater(); }
  pool_->items_.clear();
  const auto all = catalogWith(c_.paletteColors());
  QSet<QString> used;
  for (const QStringList& r : s_.rows) for (const QString& id : r) used.insert(id);
  int n = 0;
  for (const ToolboxItem& it : all) {
    const bool match = q.isEmpty() ? it.group == group_ : plain(it.label + " " + it.group).contains(q);
    if (!match) continue;
    QWidget* b = buttonFor(it, -1, 0);
    if (it.id != kToolboxSeparator && used.contains(it.id)) b->setProperty("used", true);
    pool_->add(b);
    n++;
  }
  if (!n) {
    auto* l = new QLabel(q.isEmpty() ? QString::fromUtf8("Sin botones") : QString::fromUtf8("Nada se llama así"), pool_);
    l->setProperty("tbx", "hint");
    l->setFixedSize(300, kItem);
    pool_->add(l);
  }
  pool_->relayout();
}

// ---- inspection ----------------------------------------------------------------------------------------------------------------------------
QStringList ToolboxWidget::zoneIds(int i) const {
  QStringList out;
  for (QWidget* w : zones_[size_t(i)].box->items()) {
    if (dynamic_cast<TbxSep*>(w)) out << kToolboxSeparator;
    else if (auto* b = dynamic_cast<TbxButton*>(w)) out << b->drag_.id;
  }
  return out;
}

QWidget* ToolboxWidget::itemWidget(const QString& id) const {
  for (const Zone& z : zones_)
    for (QWidget* w : z.box->items())
      if (auto* b = dynamic_cast<TbxButton*>(w)) if (b->drag_.id == id) return b;
  return nullptr;
}

QStringList ToolboxWidget::poolIds() const {
  QStringList out;
  if (!pool_) return out;
  for (QWidget* w : pool_->items()) {
    if (dynamic_cast<TbxSep*>(w)) out << kToolboxSeparator;
    else if (auto* b = dynamic_cast<TbxButton*>(w)) out << b->drag_.id;
  }
  return out;
}

QPushButton* ToolboxWidget::smallButton(const QString& text) const {
  if (editBtn_->text() == text) return editBtn_;
  if (!body_) return nullptr;
  for (QPushButton* b : body_->findChildren<QPushButton*>()) if (b->text() == text) return b;   // not the stale children of the previous render
  return nullptr;
}
