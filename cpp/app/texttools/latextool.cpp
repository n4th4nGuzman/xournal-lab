// LaTeX tool: a dialog with live preview and the categorized palette, and insertion/editing of formulas. Port of latex.ts. The formula is laid out by
// core/mathlayout (no KaTeX, no installed LaTeX) and stored as `<teximage text="TEX" left top right bottom>BASE64-PDF</teximage>` exactly like the
// Electron app, so either program re-opens and edits it from the `text` attribute.
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "latexpalette.h"
#include "mainwindow.h"
#include "mathlayout.h"
#include "texttools.h"
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QScreen>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <unordered_map>

namespace texttools {

namespace {

constexpr int kRecentMax = 16;

bool persistent() {
  // automated runs must not write the user's registry/settings
  const QStringList a = QCoreApplication::arguments();
  return !(a.contains("--selftest") || a.contains("--bench") || a.contains("--screenshot"));
}

/** The source box: auto-closes braces ("{" adds "}", typing "}" before one steps over it), Ctrl+Enter inserts, Esc cancels. */
class FormulaEdit : public QPlainTextEdit {
 public:
  using QPlainTextEdit::QPlainTextEdit;
  std::function<void()> onCommit, onCancel;

 protected:
  void keyPressEvent(QKeyEvent* e) override {
    if (e->key() == Qt::Key_Escape) { e->accept(); if (onCancel) onCancel(); return; }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) { e->accept(); if (onCommit) onCommit(); return; }
    const auto m = e->modifiers();
    const bool plain = !(m & (Qt::ControlModifier | Qt::AltModifier)) || ((m & Qt::ControlModifier) && (m & Qt::AltModifier));   // AltGr reports both
    if (plain && e->text() == "{") {
      textCursor().insertText(QStringLiteral("{}"));
      moveCursor(QTextCursor::Left);
      e->accept();
      return;
    }
    if (plain && e->text() == "}") {
      QTextCursor c = textCursor();
      if (!c.hasSelection() && c.position() < document()->characterCount() - 1 && document()->characterAt(c.position()) == QChar('}')) {
        c.movePosition(QTextCursor::Right);
        setTextCursor(c);
        e->accept();
        return;
      }
    }
    QPlainTextEdit::keyPressEvent(e);
  }
};

/** The typeset formula on paper colour (dark paper when the ink is light, so it is always visible). */
class Preview : public QWidget {
 public:
  using QWidget::QWidget;
  void set(const MathBox& box, const QColor& ink) { box_ = box; ink_ = ink; update(); }
  void clear() { box_ = MathBox(); update(); }
  const MathBox& box() const { return box_; }
  QSize sizeHint() const override { return {320, 130}; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool light = ink_.lightnessF() > 0.75;
    p.fillRect(rect(), light ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xfd, 0xfd, 0xfb));
    p.setPen(QColor(0x66, 0x66, 0x66));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!box_.ok || box_.empty || box_.width <= 0) return;
    const double avail = width() - 16.0, availH = height() - 12.0;
    const double s = std::min({96.0 / 72.0, avail / box_.width, availH / std::max(1.0, box_.height())});   // CSS px per pt, shrunk to fit
    p.translate((width() - box_.width * s) / 2, (height() - box_.height() * s) / 2 + box_.ascent * s);
    p.scale(s, s);
    drawMath(p, box_, QPointF(0, 0), ink_);
  }

 private:
  MathBox box_;
  QColor ink_ = Qt::black;
};

struct Ctx {
  int page = 0;
  QPointF pos;
  bool existing = false;
  int layer = 0, index = 0;
  QString data;   // the formula's PDF when it was opened: tells whether the element is still the same one
};

class LatexDialog : public QDialog {
 public:
  LatexDialog(Editor& ed, QWidget* parent) : QDialog(parent), ed_(ed) {
    setObjectName(QStringLiteral("latexDialog"));
    setWindowTitle(QStringLiteral("Fórmula LaTeX"));
    setWindowModality(Qt::NonModal);   // a window of its own that can sit beside the page (the chrome area places it and keeps the page from taking clicks while it is open)
    {   // 700 px high does not fit a 768 px screen once the title bar and the taskbar are counted
      const QScreen* sc = parent ? parent->screen() : QGuiApplication::primaryScreen();
      resize(860, sc ? std::min(700, sc->availableGeometry().height() - 90) : 700);
    }
    loadRecent();

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    auto* head = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("<b>Fórmula LaTeX</b>"));
    title->setStyleSheet(QStringLiteral("font-size: 15px"));
    auto* hint = new QLabel(QStringLiteral("Ctrl+Intro inserta · Esc cancela"));
    hint->setStyleSheet(QStringLiteral("color: #999"));
    head->addWidget(title);
    head->addStretch();
    head->addWidget(hint);
    root->addLayout(head);

    auto* body = new QHBoxLayout;
    edit_ = new FormulaEdit;
    edit_->setObjectName(QStringLiteral("ltxEdit"));
    edit_->setPlaceholderText(QStringLiteral("Escribe LaTeX, por ejemplo: \\frac{a}{b}"));
    edit_->setMinimumHeight(120);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(12);
    edit_->setFont(mono);
    edit_->setTabChangesFocus(true);
    edit_->onCommit = [this] { commit(); };
    edit_->onCancel = [this] { closeDialog(); };
    preview_ = new Preview;
    preview_->setObjectName(QStringLiteral("ltxPreview"));
    preview_->setMinimumHeight(120);
    body->addWidget(edit_, 1);
    body->addWidget(preview_, 1);
    root->addLayout(body);

    err_ = new QLabel;
    err_->setObjectName(QStringLiteral("ltxErr"));
    err_->setWordWrap(true);
    err_->setStyleSheet(QStringLiteral("color: #ff7b72"));
    err_->setMinimumHeight(18);
    root->addWidget(err_);

    buildPalette(root);

    auto* opts = new QHBoxLayout;
    display_ = new QRadioButton(QStringLiteral("Centrada"));
    display_->setObjectName(QStringLiteral("ltxDisplay"));
    inline_ = new QRadioButton(QStringLiteral("En línea"));
    inline_->setObjectName(QStringLiteral("ltxInline"));
    display_->setChecked(true);
    size_ = new QComboBox;
    size_->setObjectName(QStringLiteral("ltxSize"));
    for (int s : {10, 12, 14, 18, 24, 32}) size_->addItem(QString::number(s), s);
    size_->setCurrentIndex(2);
    asImage_ = new QCheckBox(QStringLiteral("Guardar como imagen simple"));
    asImage_->setObjectName(QStringLiteral("ltxImage"));
    asImage_->setToolTip(QStringLiteral("Guarda un PNG normal: se ve igual en cualquier programa, pero no se podrá editar después"));
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
    cancel->setObjectName(QStringLiteral("ltxCancel"));
    ok_ = new QPushButton(QStringLiteral("Insertar"));
    ok_->setObjectName(QStringLiteral("ltxOk"));
    ok_->setDefault(false);
    ok_->setAutoDefault(false);
    cancel->setAutoDefault(false);
    ok_->setStyleSheet(QStringLiteral("QPushButton { background: #2f6fd0; color: white; font-weight: bold; } QPushButton:disabled { background: #3a3a3a; color: #888; }"));
    for (QWidget* w : {static_cast<QWidget*>(display_), static_cast<QWidget*>(inline_), static_cast<QWidget*>(size_), static_cast<QWidget*>(cancel), static_cast<QWidget*>(ok_), static_cast<QWidget*>(asImage_)}) w->setMinimumHeight(36);
    cancel->setMinimumWidth(100);
    ok_->setMinimumWidth(150);
    opts->addWidget(display_);
    opts->addWidget(inline_);
    opts->addSpacing(8);
    opts->addWidget(new QLabel(QStringLiteral("Tamaño")));
    opts->addWidget(size_);
    opts->addWidget(new QLabel(QStringLiteral("pt")));
    opts->addSpacing(8);
    opts->addWidget(asImage_);
    opts->addStretch();
    opts->addWidget(cancel);
    opts->addWidget(ok_);
    root->addLayout(opts);

    timer_.setSingleShot(true);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
    QObject::connect(edit_, &QPlainTextEdit::textChanged, this, [this] { timer_.start(60); });
    QObject::connect(display_, &QRadioButton::toggled, this, [this] { refresh(); });
    QObject::connect(size_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    QObject::connect(cancel, &QPushButton::clicked, this, [this] { closeDialog(); });
    QObject::connect(ok_, &QPushButton::clicked, this, [this] { commit(); });
    QObject::connect(&ed_, &Editor::styleChanged, this, [this] { if (isVisible()) refresh(); });
    QObject::connect(&ed_, &Editor::documentReplaced, this, [this] { closeDialog(); });
  }

  bool isOpenFor() const { return open_; }

  void openWith(const Ctx& c, const QString& source) {
    ctx_ = c;
    open_ = true;
    edit_->setPlainText(source);
    size_->setCurrentIndex(std::max(0, size_->findData(lastSize_)));
    (lastDisplay_ ? display_ : inline_)->setChecked(true);
    asImage_->setChecked(false);
    asImage_->setEnabled(!c.existing);
    ok_->setText(c.existing ? QStringLiteral("Aplicar cambios") : QStringLiteral("Insertar"));
    err_->clear();
    search_->clear();
    refresh();
    if (QWidget* p = parentWidget()) move(p->geometry().center() - QPoint(width() / 2, height() / 2));
    show();
    raise();
    activateWindow();
    edit_->setFocus();
    edit_->moveCursor(QTextCursor::End);
  }

  void closeDialog() {
    open_ = false;
    timer_.stop();
    hide();
    if (DocumentView* v = ed_.view()) v->setFocus();
  }

 protected:
  void keyPressEvent(QKeyEvent* e) override {
    if (e->key() == Qt::Key_Escape) {
      if (search_->hasFocus() && !search_->text().isEmpty()) search_->clear();   // Esc in the search only clears it
      else if (!search_->hasFocus()) closeDialog();
      e->accept();
      return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) { commit(); e->accept(); return; }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { e->accept(); return; }   // a plain Enter must not trigger a button
    QDialog::keyPressEvent(e);
  }
  void closeEvent(QCloseEvent* e) override { open_ = false; timer_.stop(); QDialog::closeEvent(e); }
  void reject() override { closeDialog(); }

 private:
  bool display() const { return display_->isChecked(); }
  double sizePt() const { return size_->currentData().toDouble(); }
  QColor ink() const { return QColor(ed_.style().color); }

  // ---- palette ----------------------------------------------------------------------------------------------------------------------------
  void buildPalette(QVBoxLayout* root) {
    auto* head = new QHBoxLayout;
    tabs_ = new QTabBar;
    tabs_->setObjectName(QStringLiteral("ltxTabs"));
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true);
    tabs_->setDrawBase(false);
    tabs_->addTab(QStringLiteral("Recientes"));
    for (const PaletteCategory& c : paletteCategories()) tabs_->addTab(c.title);
    searchTab_ = tabs_->addTab(QString());   // current while searching: a bar can have no current tab only by selecting one that is not shown
    tabs_->setTabVisible(searchTab_, false);
    search_ = new QLineEdit;
    search_->setObjectName(QStringLiteral("ltxSearch"));
    search_->setPlaceholderText(QStringLiteral("Buscar: raíz, integral, alfa…"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(190);
    search_->setMinimumHeight(32);
    head->addWidget(tabs_, 1);
    head->addWidget(search_);
    root->addLayout(head);

    stack_ = new QStackedWidget;
    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("ltxList"));
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setUniformItemSizes(true);
    list_->setWrapping(true);
    list_->setIconSize(QSize(kIconW, kIconH));
    list_->setGridSize(QSize(kIconW + 10, kIconH + 8));
    list_->setSpacing(2);
    list_->setMinimumHeight(190);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    empty_ = new QLabel;
    empty_->setObjectName(QStringLiteral("ltxEmpty"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QStringLiteral("color: #999"));
    stack_->addWidget(list_);
    stack_->addWidget(empty_);
    root->addWidget(stack_, 1);

    QObject::connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
      if (i < 0 || i == searchTab_ || blockTab_) return;
      tab_ = i;
      search_->clear();
      renderGrid();
    });
    QObject::connect(search_, &QLineEdit::textChanged, this, [this] { renderGrid(); });
    QObject::connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
      PaletteItem p;
      p.snippet = it->data(Qt::UserRole).toString();
      p.tex = it->data(Qt::UserRole + 1).toString();
      p.name = it->data(Qt::UserRole + 2).toString();
      insertSnippet(p.snippet);
      remember(p);
    });
    tab_ = 1;   // "Estructuras", as the Electron app opens
    tabs_->setCurrentIndex(tab_);
    renderGrid();
  }

  static constexpr int kIconW = 66, kIconH = 44;

  QIcon iconFor(const PaletteItem& it) {
    const QColor fg = palette().color(QPalette::Text);
    const QString key = it.tex + QChar(1) + fg.name();
    auto f = icons_.find(key);
    if (f != icons_.end()) return f->second;
    const MathBox box = layoutMath(it.tex, 15.0, false);
    QIcon icon;
    if (box.ok && !box.empty) {
      const qreal dpr = 2;
      QPixmap pm(int(kIconW * dpr), int(kIconH * dpr));
      pm.setDevicePixelRatio(dpr);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      const double s = std::min({1.0, (kIconW - 8.0) / box.width, (kIconH - 6.0) / std::max(1.0, box.height())});
      p.translate((kIconW - box.width * s) / 2, (kIconH - box.height() * s) / 2 + box.ascent * s);
      p.scale(s, s);
      drawMath(p, box, QPointF(0, 0), fg);
      p.end();
      icon = QIcon(pm);
    }
    icons_.emplace(key, icon);
    return icon;
  }

  void renderGrid() {
    const QString q = search_->text().trimmed();
    std::vector<PaletteItem> items;
    if (!q.isEmpty()) items = searchPalette(q);
    else if (tab_ == 0) items = recent_;
    else if (tab_ - 1 < int(paletteCategories().size())) items = paletteCategories()[size_t(tab_ - 1)].items;
    {   // while searching, no tab is the current one
      blockTab_ = true;
      tabs_->setCurrentIndex(q.isEmpty() ? tab_ : searchTab_);
      blockTab_ = false;
    }
    list_->setUpdatesEnabled(false);
    list_->clear();
    for (const PaletteItem& it : items) {
      auto* li = new QListWidgetItem;
      const QIcon ic = iconFor(it);
      if (ic.isNull()) li->setText(it.tex);
      else li->setIcon(ic);
      li->setSizeHint(QSize(kIconW + 8, kIconH + 6));
      li->setToolTip(it.name + QStringLiteral("\n") + QString(it.snippet).remove(kPaletteCaret));
      li->setData(Qt::UserRole, it.snippet);
      li->setData(Qt::UserRole + 1, it.tex);
      li->setData(Qt::UserRole + 2, it.name);
      list_->addItem(li);
    }
    list_->setUpdatesEnabled(true);
    if (items.empty()) {
      empty_->setText(q.isEmpty() ? QStringLiteral("Aún no has usado ningún símbolo") : QStringLiteral("Nada con ese nombre: prueba otra palabra (raíz, integral, alfa, matriz…)"));
      stack_->setCurrentWidget(empty_);
    } else {
      stack_->setCurrentWidget(list_);
    }
  }

  void insertSnippet(const QString& snippet) {
    const SnippetInsert ins = snippetInsert(snippet);
    QTextCursor c = edit_->textCursor();
    c.insertText(ins.text);                                   // replaces the selection
    c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, ins.caretBack);
    edit_->setTextCursor(c);
    edit_->setFocus();
  }

  void remember(const PaletteItem& it) {
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const PaletteItem& r) { return r.snippet == it.snippet; }), recent_.end());
    recent_.insert(recent_.begin(), it);
    if (int(recent_.size()) > kRecentMax) recent_.resize(kRecentMax);
    if (persistent()) {
      QJsonArray a;
      for (const PaletteItem& r : recent_) a.append(QJsonObject{{"tex", r.tex}, {"snippet", r.snippet}, {"name", r.name}});
      QSettings().setValue(QStringLiteral("latexRecent"), QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
    }
  }

  void loadRecent() {
    if (!persistent()) return;
    const QJsonArray a = QJsonDocument::fromJson(QSettings().value(QStringLiteral("latexRecent")).toString().toUtf8()).array();
    for (const QJsonValue& v : a) {
      const QJsonObject o = v.toObject();
      if (!o.value("snippet").isString() || !o.value("tex").isString()) continue;
      recent_.push_back({o.value("tex").toString(), o.value("snippet").toString(), o.value("name").toString()});
      if (int(recent_.size()) >= kRecentMax) break;
    }
  }

  // ---- live preview ---------------------------------------------------------------------------------------------------------------------------
  void refresh() {
    timer_.stop();
    const QString src = edit_->toPlainText();
    if (src.trimmed().isEmpty()) { preview_->clear(); err_->clear(); ok_->setEnabled(false); return; }
    const MathBox box = layoutMath(src, sizePt(), display());
    if (!box.ok) {
      err_->setText(box.error);
      ok_->setEnabled(false);
      preview_->clear();
      return;
    }
    err_->clear();
    ok_->setEnabled(!box.empty);
    preview_->set(box, ink());
  }

  // ---- inserting ---------------------------------------------------------------------------------------------------------------------------------
  void commit() {
    if (!open_ || !ok_->isEnabled()) return;
    const QString tex = edit_->toPlainText();
    const bool disp = display();
    const double pt = sizePt();
    const bool done = ctx_.existing ? replaceExisting(tex, disp, pt) : insertNew(tex, disp, pt);
    if (!done) { ok_->setEnabled(true); return; }
    lastDisplay_ = disp;
    lastSize_ = int(pt);
    closeDialog();
  }

  bool insertNew(const QString& tex, bool disp, double pt) {
    const int page = ctx_.page;
    if (page < 0 || page >= ed_.pageCount()) { err_->setText(QStringLiteral("No hay documento abierto")); return false; }
    TexImageResult r = makeTexImage(tex, pt, disp, ink(), 0, 0);
    if (!r.ok) { err_->setText(r.error); return false; }
    const Page& pg = ed_.doc().pages[size_t(page)];
    const double w = r.box.width, h = r.box.height();
    const double left = std::max(2.0, std::min(ctx_.pos.x(), pg.width() - w - 2));   // inside the page, as the Electron app keeps it
    const double top = std::max(2.0, std::min(ctx_.pos.y(), pg.height() - h - 2));
    Element el;
    if (asImage_->isChecked()) {
      Image im;
      im.tag = QStringLiteral("image");
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      r.bitmap.save(&buf, "PNG");
      im.data = QString::fromLatin1(png.toBase64());
      setImageBox(im, {left, top, left + w, top + h});
      el = std::move(im);
    } else {
      setImageBox(r.el, {left, top, left + w, top + h});
      el = std::move(r.el);
    }
    const int layer = ed_.activeLayer(page);
    if (!ed_.layerVisible(page, layer)) {
      ed_.setLayerVisible(page, layer, true);
      ed_.notify(QStringLiteral("La capa activa estaba oculta: se ha vuelto a mostrar"));
    }
    const int idx = ed_.commitAdd(page, layer, std::move(el), QStringLiteral("Fórmula"));
    ed_.afterCreate("latex");
    ed_.selection().select(page, {ElementRef{page, layer, idx}});
    return true;
  }

  /** Where the formula being edited is now (it may have moved if the page changed meanwhile); -1 when it is gone. */
  bool locate(int& layer, int& index) const {
    const auto& layers = ed_.doc().pages[size_t(ctx_.page)].layers;
    auto same = [&](int l, int i) {
      if (l < 0 || l >= int(layers.size()) || i < 0 || i >= int(layers[size_t(l)].elements.size())) return false;
      const Image* im = std::get_if<Image>(&layers[size_t(l)].elements[size_t(i)]);
      return im && isTexImage(*im) && im->data == ctx_.data;
    };
    if (same(ctx_.layer, ctx_.index)) { layer = ctx_.layer; index = ctx_.index; return true; }
    for (int l = 0; l < int(layers.size()); l++)
      for (int i = 0; i < int(layers[size_t(l)].elements.size()); i++)
        if (same(l, i)) { layer = l; index = i; return true; }
    return false;
  }

  bool replaceExisting(const QString& tex, bool disp, double pt) {
    if (ctx_.page < 0 || ctx_.page >= ed_.pageCount()) { err_->setText(QStringLiteral("No hay documento abierto")); return false; }
    int layer = 0, index = 0;
    if (!locate(layer, index)) { err_->setText(QStringLiteral("La fórmula ya no está en la página")); return false; }
    const Image before = std::get<Image>(ed_.doc().pages[size_t(ctx_.page)].layers[size_t(layer)].elements[size_t(index)]);
    const Rect b = imageBox(before);
    TexImageResult r = makeTexImage(tex, pt, disp, ink(), b[0], b[1]);
    if (!r.ok) { err_->setText(r.error); return false; }
    Image after = before;   // keeps its turn and mirror (xlRot / xlFlip) and everything else it carried
    attrSet(after.attrs, "text", tex);
    after.data = r.el.data;
    setImageBox(after, {b[0], b[1], b[0] + r.box.width, b[1] + r.box.height()});
    const int page = ctx_.page;
    Editor& ed = ed_;
    ed.replaceElements(page, layer, index, 1, {Element(after)});
    Action a;
    a.label = QStringLiteral("Editar fórmula");
    a.page = page;
    a.undo = [&ed, page, layer, index, before] { ed.replaceElements(page, layer, index, 1, {Element(before)}); };
    a.redo = [&ed, page, layer, index, after] { ed.replaceElements(page, layer, index, 1, {Element(after)}); };
    ed.pushAction(std::move(a));
    ed.selection().select(page, {ElementRef{page, layer, index}});
    return true;
  }

 public:
  static int lastSize_;
  static bool lastDisplay_;

 private:
  Editor& ed_;
  Ctx ctx_;
  bool open_ = false, blockTab_ = false;
  FormulaEdit* edit_ = nullptr;
  Preview* preview_ = nullptr;
  QLabel *err_ = nullptr, *empty_ = nullptr;
  QPushButton* ok_ = nullptr;
  QRadioButton *display_ = nullptr, *inline_ = nullptr;
  QComboBox* size_ = nullptr;
  QCheckBox* asImage_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QLineEdit* search_ = nullptr;
  QListWidget* list_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QTimer timer_;
  int tab_ = 1, searchTab_ = -1;
  std::vector<PaletteItem> recent_;
  std::unordered_map<QString, QIcon> icons_;
};
int LatexDialog::lastSize_ = 14;
bool LatexDialog::lastDisplay_ = true;

QPointer<LatexDialog> g_dialog;

LatexDialog& dialogFor(MainWindow& win) {
  if (!g_dialog) g_dialog = new LatexDialog(win.editor(), &win);
  return *g_dialog;
}

class LatexTool : public Tool {
 public:
  LatexTool(Editor& ed, MainWindow& win) : Tool(ed), win_(win) {}
  QCursor cursor() const override { return Qt::CrossCursor; }
  // The dialog is modal: opening it while the button is still down could leave the view without its release, so it opens when the click ends
  void pointerDown(const PointerEvent& e) override { page_ = e.page; pos_ = e.pos; }
  void pointerUp(const PointerEvent&) override {
    const int page = page_;
    page_ = -1;
    if (page >= 0) openFormulaAt(win_, page, pos_);
  }
  void pointerCancel() override { page_ = -1; }

 private:
  MainWindow& win_;
  int page_ = -1;
  QPointF pos_;
};

}  // namespace

void openFormulaAt(MainWindow& win, int page, const QPointF& pos) {
  Editor& ed = win.editor();
  if (page < 0 || page >= ed.pageCount()) return;
  commitOpenText();
  // the topmost visible formula under the point
  std::vector<PageIndex::Hit> hits;
  ed.index(page).queryPoint(pos, 0, hits);
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    if (!ed.layerVisible(page, it->first)) continue;
    const auto& els = ed.doc().pages[size_t(page)].layers[size_t(it->first)].elements;
    if (it->second < 0 || it->second >= int(els.size())) continue;
    const Image* im = std::get_if<Image>(&els[size_t(it->second)]);
    if (im && isTexImage(*im) && texContains(*im, pos.x(), pos.y())) { editFormula(win, page, it->first, it->second); return; }
  }
  Ctx c;
  c.page = page;
  c.pos = pos;
  dialogFor(win).openWith(c, QString());
}

void editFormula(MainWindow& win, int page, int layer, int index) {
  Editor& ed = win.editor();
  if (page < 0 || page >= ed.pageCount()) return;
  const auto& layers = ed.doc().pages[size_t(page)].layers;
  if (layer < 0 || layer >= int(layers.size()) || index < 0 || index >= int(layers[size_t(layer)].elements.size())) return;
  const Image* im = std::get_if<Image>(&layers[size_t(layer)].elements[size_t(index)]);
  if (!im || !isTexImage(*im)) return;
  commitOpenText();
  Ctx c;
  c.page = page;
  c.existing = true;
  c.layer = layer;
  c.index = index;
  c.data = im->data;
  const Rect b = imageBox(*im);
  c.pos = QPointF(b[0], b[1]);
  dialogFor(win).openWith(c, attrOr(im->attrs, "text"));
}

QWidget* formulaDialog() { return g_dialog; }
bool formulaDialogOpen() { return g_dialog && g_dialog->isOpenFor(); }

void registerLatexTool(Editor& ed, MainWindow& win) {
  ToolInfo i;
  i.id = "latex"; i.label = "LaTeX…"; i.tooltip = "LaTeX: clic para escribir una fórmula; clic sobre una fórmula para editarla";
  i.shortcut = QKeySequence("X");
  i.icon = icon("latex");
  i.inserter = true;
  i.order = 22;
  i.factory = [&win](Editor& e) { return std::make_unique<LatexTool>(e, win); };
  ed.registerTool(i);
}

}  // namespace texttools
