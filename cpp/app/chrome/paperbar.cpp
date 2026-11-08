// The paper bar of the Journal (bar3 of the Electron toolbar): paper style or special paper with its parameters, paper colour, "all pages", page format,
// orientation and a custom size in millimetres. Every change is one undo step through Editor::editBackgrounds / setPageSize.
#include "palettes.h"
#include "parts.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace chrome {
namespace {
QPixmap colorChip(const QColor& c) {
  QPixmap pm(40, 40);
  pm.setDevicePixelRatio(2);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(255, 255, 255, 130), 1));
  p.setBrush(c);
  p.drawRoundedRect(QRectF(2, 2, 16, 16), 3, 3);
  return pm;
}
QString hexOf(const Attrs& bg) {
  const QString c = attrOr(bg, "color", "#ffffffff");
  return c.size() >= 7 && c[0] == '#' ? c.left(7).toLower() : QStringLiteral("#ffffff");
}
}  // namespace

PaperBar::PaperBar(Chrome& c, QWidget* parent) : QToolBar("Papel", parent), c_(c) {
  setObjectName("paperbar");
  setMovable(false);
  setFloatable(false);
  Editor& ed = c.ed;
  auto label = [this](const QString& t) { auto* l = new QLabel(t, this); l->setContentsMargins(6, 0, 4, 0); return l; };
  addWidget(label("Fondo"));
  paper_ = new QComboBox(this);
  paper_->setObjectName("paperSel");
  paper_->setToolTip("Tipo de papel");
  for (const PaperStyle& ps : paperStyles()) paper_->addItem(ps.label, ps.style);
  paper_->insertSeparator(paper_->count());
  for (const PaperKind& k : paperKinds()) paper_->addItem(k.label, "x:" + k.id);
  paper_->setMinimumHeight(24);
  addWidget(paper_);
  params_ = new QWidget(this);
  params_->setObjectName("paperOpts");
  auto* pl = new QHBoxLayout(params_);
  pl->setContentsMargins(4, 0, 4, 0);
  addWidget(params_);
  spacingBox_ = new QWidget(this);
  spacingBox_->setObjectName("paperSpacingBox");
  auto* spl = new QHBoxLayout(spacingBox_);
  spl->setContentsMargins(4, 0, 4, 0);
  spacing_ = new QDoubleSpinBox(spacingBox_);
  spacing_->setObjectName("paperSpacing");
  spacing_->setToolTip("Separación entre las líneas, cuadros o puntos del papel");
  spacing_->setRange(kMinSpacing / kMmToPt, kMaxSpacing / kMmToPt);
  spacing_->setDecimals(1);
  spacing_->setSingleStep(0.5);
  spacing_->setSuffix(" mm");
  spacing_->setMinimumHeight(24);
  spl->addWidget(new QLabel("Espaciado", spacingBox_));
  spl->addWidget(spacing_);
  addWidget(spacingBox_);
  auto* opBox = new QWidget(this);
  auto* opl = new QHBoxLayout(opBox);
  opl->setContentsMargins(4, 0, 4, 0);
  opacity_ = new QDoubleSpinBox(opBox);
  opacity_->setObjectName("paperOpacity");
  opacity_->setToolTip("Qué tan marcadas se ven las líneas del papel");
  opacity_->setRange(kMinPaperOpacity * 100, 100);
  opacity_->setDecimals(0);
  opacity_->setSingleStep(5);
  opacity_->setSuffix(" %");
  opacity_->setMinimumHeight(24);
  opl->addWidget(new QLabel("Opacidad", opBox));
  opl->addWidget(opacity_);
  addWidget(opBox);
  // typing or clicking through values would otherwise leave one undo step per tick
  auto commit = [this](QDoubleSpinBox* s, void (PaperBar::*fn)(double)) {
    auto* t = new QTimer(s);
    t->setSingleShot(true);
    t->setInterval(350);
    connect(t, &QTimer::timeout, this, [this, s, fn] { (this->*fn)(s->value()); });
    connect(s, &QDoubleSpinBox::valueChanged, this, [this, t] { if (!syncing_) t->start(); });
  };
  commit(spacing_, &PaperBar::setSpacing);
  commit(opacity_, &PaperBar::setOpacity);
  color_ = new QToolButton(this);
  color_->setObjectName("bgColor");
  color_->setToolTip("Color del papel");
  color_->setIconSize(QSize(20, 20));
  color_->setMinimumSize(30, 28);
  addWidget(color_);
  swatchBox_ = new QWidget(this);
  swatchBox_->setObjectName("paperSwatches");
  swatchBox_->setToolTip("Colores de la paleta activa para el papel");
  auto* sl = new QHBoxLayout(swatchBox_);
  sl->setContentsMargins(4, 0, 4, 0);
  sl->setSpacing(4);
  addWidget(swatchBox_);
  all_ = new QCheckBox("todas las páginas", this);
  all_->setObjectName("applyAll");
  all_->setToolTip("Aplicar el fondo y el formato a todas las páginas");
  all_->setChecked(ed.action("journal.applyAll")->isChecked());
  addWidget(all_);
  addSeparator();
  addWidget(label("Papel"));
  format_ = new QComboBox(this);
  format_->setObjectName("formatSel");
  format_->setToolTip("Formato de papel");
  for (const PaperFormat& f : paperFormats()) format_->addItem(f.label, f.id);
  format_->setMinimumHeight(24);
  addWidget(format_);
  auto* orient = new QToolButton(this);
  orient->setObjectName("orient");
  orient->setIcon(c.electronIcon("orient"));
  connect(&c, &Chrome::themeChanged, orient, [orient, &c] { orient->setIcon(c.electronIcon("orient")); });
  orient->setToolTip("Girar la hoja (vertical / horizontal)");
  orient->setMinimumSize(30, 28);
  addWidget(orient);
  auto spin = [this](const QString& name, const QString& tip) {
    auto* s = new QDoubleSpinBox(this);
    s->setObjectName(name);
    s->setToolTip(tip);
    s->setRange(7, 5000);
    s->setDecimals(0);
    s->setSingleStep(1);
    s->setMinimumHeight(24);
    return s;
  };
  w_ = spin("fmtW", "Ancho en mm");
  h_ = spin("fmtH", "Alto en mm");
  addWidget(w_);
  addWidget(label("×"));
  addWidget(h_);
  addWidget(label("mm"));
  auto* apply = new QToolButton(this);
  apply->setObjectName("fmtApply");
  apply->setIcon(c.electronIcon("check"));
  connect(&c, &Chrome::themeChanged, apply, [apply, &c] { apply->setIcon(c.electronIcon("check")); });
  apply->setToolTip("Aplicar el tamaño personalizado");
  apply->setMinimumSize(30, 28);
  addWidget(apply);

  connect(paper_, &QComboBox::activated, this, [this] { if (!syncing_) choosePaper(paper_->currentData().toString()); });
  connect(color_, &QToolButton::clicked, this, [this] {
    const Page& pg = c_.ed.doc().pages[size_t(c_.ed.currentPage())];
    const QColor col = QColorDialog::getColor(QColor(hexOf(pg.background)), this, "Color del papel");
    if (col.isValid()) chooseColor(col.name());
  });
  connect(all_, &QCheckBox::toggled, this, [&ed](bool on) { ed.action("journal.applyAll")->setChecked(on); });
  connect(ed.action("journal.applyAll"), &QAction::toggled, this, [this](bool on) { all_->setChecked(on); });
  connect(format_, &QComboBox::activated, this, [this] {
    if (syncing_) return;
    const PaperFormat* f = paperFormat(format_->currentData().toString());
    if (!f) return;
    const Page& p = c_.ed.doc().pages[size_t(c_.ed.currentPage())];
    const bool landscape = p.width() > p.height();
    c_.ed.setPageSize(c_.ed.currentPage(), landscape ? f->height : f->width, landscape ? f->width : f->height, all_->isChecked());
  });
  connect(orient, &QToolButton::clicked, this, [this] {
    const Page& p = c_.ed.doc().pages[size_t(c_.ed.currentPage())];
    c_.ed.setPageSize(c_.ed.currentPage(), p.height(), p.width(), all_->isChecked());
  });
  connect(apply, &QToolButton::clicked, this, [this] { applyCustomSize(); });

  connect(&ed, &Editor::currentPageChanged, this, [this] { sync(); });
  connect(&ed, &Editor::documentReplaced, this, [this] { sync(); });
  connect(&ed, &Editor::appearanceChanged, this, [this] { sync(); });
  connect(&ed, &Editor::historyChanged, this, [this] { sync(); });
  connect(&ed, &Editor::structureChanged, this, [this] { sync(); });
  setPaletteId(QString::fromLatin1(kDefaultPalette));
  sync();
}

std::vector<int> PaperBar::targets() const {
  std::vector<int> pages;
  for (int i = 0; i < c_.ed.pageCount(); i++) if (all_->isChecked() || i == c_.ed.currentPage()) pages.push_back(i);
  return pages;
}

void PaperBar::setPaperLook(const QString& fallback, const std::optional<PaperSpec>& spec) {
  const QString raw = spec ? formatSpec(*spec) : QString();
  c_.ed.editBackgrounds(targets(), "Tipo de papel", [&](Attrs& bg) {
    if (attrOr(bg, "type", "solid") != "solid") return;   // a PDF or picture page keeps its background
    attrSet(bg, "style", fallback);
    bg.erase(std::remove_if(bg.begin(), bg.end(), [](const auto& kv) { return kv.first == "xlPaper"; }), bg.end());
    if (spec) attrSet(bg, "xlPaper", raw);
    setPaperSpacing(bg, 0);   // a spacing belongs to the style it was chosen for; the opacity stays
  });
}

void PaperBar::setSpacing(double mm) {
  c_.ed.editBackgrounds(targets(), "Espaciado del papel", [&](Attrs& bg) {
    if (attrOr(bg, "type", "solid") == "solid" && !paperOfBackground(bg) && builtinSpacing(attrOr(bg, "style", "plain")) > 0) setPaperSpacing(bg, mm * kMmToPt);
  });
}

void PaperBar::setOpacity(double percent) {
  c_.ed.editBackgrounds(targets(), "Opacidad del papel", [&](Attrs& bg) {
    if (attrOr(bg, "type", "solid") == "solid") setPaperOpacity(bg, percent / 100);
  });
}

void PaperBar::choosePaper(const QString& value) {
  if (c_.ed.pageCount() == 0) return;
  if (!value.startsWith("x:")) { c_.ed.setPaperStyle(c_.ed.currentPage(), value, all_->isChecked()); return; }
  const PaperKind* k = paperKind(value.mid(2));
  if (!k) return;
  const auto cur = paperOfBackground(c_.ed.doc().pages[size_t(c_.ed.currentPage())].background);
  setPaperLook(k->fallback, cur && cur->kind == k->id ? cur : defaultSpec(k->id));
}

void PaperBar::chooseColor(const QString& rgb) {
  c_.ed.setPaperColor(c_.ed.currentPage(), rgb, all_->isChecked());
  sync();
}

void PaperBar::applyCustomSize() {
  const double w = w_->value(), h = h_->value();
  if (w >= 20 && h >= 20) c_.ed.setPageSize(c_.ed.currentPage(), w * kMmToPt, h * kMmToPt, all_->isChecked());
}

QList<QToolButton*> PaperBar::swatches() const { return swatchBox_->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly); }

void PaperBar::setPaletteId(const QString& id) {
  const Palette& pal = paletteById(id);
  qDeleteAll(swatches());
  QStringList all = pal.papers;   // the curated papers first, then the inks
  for (const QString& c : pal.colors) if (!all.contains(c.left(7), Qt::CaseInsensitive)) all << c.left(7);
  all.removeDuplicates();
  auto* lay = static_cast<QHBoxLayout*>(swatchBox_->layout());
  for (const QString& c : all) {
    auto* b = new QToolButton(swatchBox_);
    b->setProperty("rgb", c.toLower());
    b->setToolTip("Papel " + c);
    b->setCheckable(true);
    b->setFixedSize(26, 26);
    b->setStyleSheet(QString("QToolButton { background: %1; border: 2px solid #666; border-radius: 13px; padding: 0; } QToolButton:checked { border-color: #fff; } QToolButton:hover { border-color: #aaa; }").arg(c));
    connect(b, &QToolButton::clicked, this, [this, c] { chooseColor(c.left(7).toLower()); });
    lay->addWidget(b);
  }
  sync();
}

void PaperBar::renderParams(const std::optional<PaperSpec>& spec) {
  const QString key = spec ? formatSpec(*spec) : QString();
  if (key == paramsKey_) return;   // rebuilding while the user has a list open would close it
  paramsKey_ = key;
  auto* lay = static_cast<QHBoxLayout*>(params_->layout());
  while (QLayoutItem* it = lay->takeAt(0)) { delete it->widget(); delete it; }
  const PaperKind* k = spec ? paperKind(spec->kind) : nullptr;
  if (!spec || !k) return;
  for (const PaperParam& pm : k->params) {
    auto* l = new QLabel(pm.label, params_);
    auto* sel = new QComboBox(params_);
    sel->setProperty("param", pm.key);
    sel->setMinimumHeight(24);
    for (const auto& [v, lab] : pm.options) sel->addItem(lab, v);
    const auto it = spec->params.find(pm.key);
    const int idx = sel->findData(it != spec->params.end() ? it->second : pm.options.front().first);
    sel->setCurrentIndex(std::max(0, idx));
    const PaperSpec snapshot = *spec;
    const QString fallback = k->fallback, pkey = pm.key;
    connect(sel, &QComboBox::activated, this, [this, sel, snapshot, fallback, pkey] { setPaperLook(fallback, withParam(snapshot, pkey, sel->currentData().toString())); });
    lay->addWidget(l);
    lay->addWidget(sel);
  }
}

void PaperBar::sync() {
  if (c_.ed.pageCount() == 0 || c_.ed.currentPage() >= c_.ed.pageCount()) return;
  syncing_ = true;
  const Page& pg = c_.ed.doc().pages[size_t(c_.ed.currentPage())];
  const auto spec = paperOfBackground(pg.background);
  const QString value = spec ? "x:" + spec->kind : attrOr(pg.background, "style", "plain");
  paper_->setCurrentIndex(std::max(0, paper_->findData(value)));
  renderParams(spec);
  const QString style = attrOr(pg.background, "style", "plain");
  const bool solid = attrOr(pg.background, "type", "solid") == "solid";
  const double own = !spec && solid ? builtinSpacing(style) : 0;
  spacingBox_->setVisible(own > 0);
  if (own > 0) { const double custom = paperSpacingOf(pg.background); spacing_->setValue(std::round((custom > 0 ? custom : own) / kMmToPt * 10) / 10); }
  opacity_->setEnabled(solid && (spec || style != "plain"));
  opacity_->setValue(std::round(paperOpacityOf(pg.background) * 100));
  const QString hex = hexOf(pg.background);
  color_->setIcon(QIcon(colorChip(QColor(hex))));
  for (QToolButton* b : swatches()) b->setChecked(b->property("rgb").toString() == hex);
  int fmt = -1;
  for (int i = 0; i < format_->count(); i++) {
    const PaperFormat* f = paperFormat(format_->itemData(i).toString());
    if (!f) continue;
    const bool same = (std::abs(f->width - pg.width()) < 0.6 && std::abs(f->height - pg.height()) < 0.6) || (std::abs(f->height - pg.width()) < 0.6 && std::abs(f->width - pg.height()) < 0.6);
    if (same) { fmt = i; break; }
  }
  format_->setCurrentIndex(fmt);
  w_->setValue(std::round(pg.width() / kMmToPt));
  h_->setValue(std::round(pg.height() / kMmToPt));
  syncing_ = false;
}

}  // namespace chrome
