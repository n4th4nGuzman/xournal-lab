#include "chrome/svgicon.h"
#include "prefsdialog.h"
#include "palettes.h"
#include "settings.h"
#include "devclass.h"
#include <QCheckBox>
#include <algorithm>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDevice>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#define U(s) QString::fromUtf8(s)

namespace {
const char* kStyle = R"(
QLabel[dlg="note"] { color: #a8a8a8; font-size: 12px; }
QLabel[dlg="warn"] { color: #e0a030; font-size: 12px; }
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { min-height: 22px; padding: 0 6px; }
QCheckBox { spacing: 8px; padding: 2px 0; }
QPushButton { min-height: 24px; min-width: 76px; padding: 0 12px; border-radius: 4px; }
QPushButton[primary="true"] { background: #2f6fd0; border: 1px solid #6aa0ff; color: white; }
QGroupBox { border: 1px solid #555; border-radius: 6px; margin-top: 14px; padding: 10px 10px 8px 10px; font-weight: bold; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QRadioButton { font-weight: normal; }
QListWidget#prefsNav { border: 1px solid #555; border-radius: 6px; padding: 4px; outline: 0; }
QListWidget#prefsNav::item { padding: 7px 10px; border-radius: 4px; }
QListWidget#prefsNav::item:selected { background: #2f6fd0; color: white; }
QScrollArea { border: none; }
)";
}  // namespace

// ---- ColorButton ---------------------------------------------------------------------------------------------------------------------------
ColorButton::ColorButton(bool alpha, bool allowDefault, QWidget* parent) : QWidget(parent), alpha_(alpha) {
  auto* h = new QHBoxLayout(this);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(6);
  swatch_ = new QPushButton(this);
  connect(swatch_, &QPushButton::clicked, this, [this] { pick(); });
  h->addWidget(swatch_);
  if (allowDefault) {
    reset_ = new QPushButton(U("Predeterminado"), this);
    reset_->setToolTip(U("Vuelve al color del tema"));
    connect(reset_, &QPushButton::clicked, this, [this] { setValue(QString()); emit changed(); });
    h->addWidget(reset_);
  }
  paintSwatch();
}

void ColorButton::setValue(const QString& v) {
  const QColor c(v);
  value_ = v.isEmpty() || !c.isValid() ? QString() : c.name(alpha_ ? QColor::HexArgb : QColor::HexRgb);
  paintSwatch();
}

void ColorButton::paintSwatch() {
  const QColor c = value_.isEmpty() ? shown_ : QColor(value_);
  const bool dark = c.lightness() < 128 && c.alphaF() > 0.5;
  swatch_->setText(value_.isEmpty() ? unset_ : value_);
  swatch_->setStyleSheet(QString("QPushButton { background: rgba(%1,%2,%3,%4); color: %5; border: 1px solid #777; border-radius: 4px; min-width: 78px; }").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()).arg(dark ? "white" : "black"));
  if (reset_) reset_->setEnabled(!value_.isEmpty());
}

void ColorButton::pick() {
  const QColor start = value_.isEmpty() ? shown_ : QColor(value_);
  const QColor c = QColorDialog::getColor(start, this, U("Color"), alpha_ ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
  if (!c.isValid()) return;
  setValue(c.name(alpha_ ? QColor::HexArgb : QColor::HexRgb));
  emit changed();
}

// A combo box never asks for more width than a dozen characters: its list is as wide as the longest item, but the page must fit a small window.
static void fitCombo(QComboBox* c) {
  c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  c->setMinimumContentsLength(12);
  c->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

// ---- ButtonRow -----------------------------------------------------------------------------------------------------------------------------
ButtonRow::ButtonRow(const QList<QPair<QString, QString>>& tools, QWidget* parent) : QWidget(parent) {
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(4);
  tool = new QComboBox(this);
  for (const auto& t : tools) tool->addItem(t.second, t.first);
  fitCombo(tool);
  v->addWidget(tool);
  auto* h = new QHBoxLayout;
  h->setSpacing(6);
  h->addWidget(new QLabel(U("Color"), this));
  colorMode = new QComboBox(this);
  colorMode->addItem(U("No cambiar"));
  colorMode->addItem(U("Elegir…"));
  fitCombo(colorMode);
  colorMode->setMinimumContentsLength(8);
  h->addWidget(colorMode);
  color = new ColorButton(false, false, this);
  color->setValue("#000000");
  h->addWidget(color);
  h->addWidget(new QLabel(U("Grosor"), this));
  size = new QComboBox(this);
  for (const char* s : {"No cambiar", "Muy fino", "Fino", "Medio", "Grueso", "Muy grueso"}) size->addItem(U(s));
  fitCombo(size);
  size->setMinimumContentsLength(8);
  h->addWidget(size);
  h->addStretch(1);
  v->addLayout(h);
  color->setEnabled(false);
  connect(colorMode, &QComboBox::currentIndexChanged, this, [this](int i) { color->setEnabled(i == 1); });
}

void ButtonRow::load(const ButtonCfg& c) {
  const int i = tool->findData(c.tool);
  tool->setCurrentIndex(i < 0 ? 0 : i);
  colorMode->setCurrentIndex(c.color.isEmpty() ? 0 : 1);
  if (!c.color.isEmpty()) color->setValue(c.color);
  size->setCurrentIndex(std::clamp(c.size, 0, 5));
}

ButtonCfg ButtonRow::value() const {
  ButtonCfg c;
  c.tool = tool->currentData().toString();
  c.color = colorMode->currentIndex() == 1 ? color->value() : QString();
  if (c.color.size() != 7) c.color.clear();
  c.size = size->currentIndex();
  return c;
}

// ---- the dialog ---------------------------------------------------------------------------------------------------------------------------
QStringList PrefsDialog::pageTitles() {
  return {U("Cargar/guardar"), U("Sistema de entrada"), U("Ratón"), U("Lápiz"), U("Pantalla táctil"), U("Ver"), U("Escala"), U("Área de dibujo"), U("Valores predeterminados"), U("Grabación de audio"), U("LaTeX"), U("Idioma")};
}

PrefsDialog::PrefsDialog(SettingsController& c, QWidget* parent) : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint), c_(c) {
  setObjectName("prefsDlg");
  setWindowTitle("Preferencias");
  setModal(false);
  setStyleSheet(chrome::neonizeCss(QString::fromLatin1(kStyle)));
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(12, 12, 12, 12);
  v->setSpacing(8);
  auto* body = new QHBoxLayout;
  body->setSpacing(12);
  nav_ = new QListWidget(this);
  nav_->setObjectName("prefsNav");
  nav_->setFixedWidth(190);
  nav_->setFocusPolicy(Qt::NoFocus);
  nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  stack_ = new QStackedWidget(this);
  body->addWidget(nav_);
  body->addWidget(stack_, 1);
  v->addLayout(body, 1);
  auto* box = new QDialogButtonBox(this);
  auto* ok = box->addButton("Aceptar", QDialogButtonBox::AcceptRole);
  ok->setProperty("primary", true);
  ok->setDefault(true);
  box->addButton("Cancelar", QDialogButtonBox::RejectRole);
  connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  v->addWidget(box);
  connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);

  // the tools a button can select: every tool of the editor that has a button, the hand, and the tool box
  toolList_.append({QString(), U("No cambiar la herramienta")});
  for (const ToolInfo* t : c_.editor().toolInfos()) if (t->inToolbar && !t->label.isEmpty()) toolList_.append({t->id, t->label});
  if (std::none_of(toolList_.begin(), toolList_.end(), [](const auto& p) { return p.first == "hand"; })) toolList_.append({"hand", U("Mano")});
  toolList_.append({"toolbox", U("Cuadro de herramientas flotante")});

  buildSaveLoad();
  buildInput();
  buildMouse();
  buildStylus();
  buildTouch();
  buildView();
  buildZoom();
  buildDrawing();
  buildDefaults();
  buildAudio();
  buildLatex();
  buildLanguage();
  for (QComboBox* cb : findChildren<QComboBox*>()) if (cb->minimumContentsLength() == 0) fitCombo(cb);
  nav_->setCurrentRow(0);
  resize(860, 600);
}

int PrefsDialog::pageCount() const { return stack_->count(); }
void PrefsDialog::showPage(int i) { nav_->setCurrentRow(std::clamp(i, 0, pageCount() - 1)); }
int PrefsDialog::currentPage() const { return stack_->currentIndex(); }
QWidget* PrefsDialog::page(int i) const { return stack_->widget(i); }

QStringList PrefsDialog::disabledKeys() const {
  QStringList out;
  for (auto it = controls_.begin(); it != controls_.end(); ++it) if (disabled_.contains(it.value())) out << it.key();
  out.sort();
  return out;
}

QWidget* PrefsDialog::newPage(const QString& title) {
  auto* sa = new QScrollArea(stack_);
  sa->setWidgetResizable(true);
  sa->setFrameShape(QFrame::NoFrame);
  sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* w = new QWidget(sa);
  w->setObjectName("prefsPage");
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(2, 2, 10, 2);
  l->setSpacing(10);
  sa->setWidget(w);
  stack_->addWidget(sa);
  nav_->addItem(title);
  cur_ = l;
  return w;
}

void PrefsDialog::endPage() { if (cur_) cur_->addStretch(1); cur_ = nullptr; }

QVBoxLayout* PrefsDialog::group(const QString& title) {
  auto* g = new QGroupBox(title);
  auto* l = new QVBoxLayout(g);
  l->setSpacing(6);
  cur_->addWidget(g);
  return l;
}

QLabel* PrefsDialog::note(QVBoxLayout* l, const QString& text) {
  auto* n = new QLabel(text);
  n->setProperty("dlg", "note");
  n->setWordWrap(true);
  l->addWidget(n);
  return n;
}

QWidget* PrefsDialog::row(QVBoxLayout* l, const QString& label, std::initializer_list<QWidget*> ws) {
  auto* w = new QWidget;
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(8);
  if (!label.isEmpty()) {
    auto* lb = new QLabel(label);
    lb->setWordWrap(true);
    lb->setFixedWidth(190);
    h->addWidget(lb);
  }
  for (QWidget* x : ws) h->addWidget(x);
  h->addStretch(1);
  l->addWidget(w);
  return w;
}

void PrefsDialog::unsupported(QVBoxLayout* l, std::initializer_list<QWidget*> ws, const QString& why) {
  for (QWidget* w : ws) { w->setEnabled(false); disabled_.append(w); }
  auto* n = new QLabel(U("No disponible: ") + why);
  n->setProperty("dlg", "warn");
  n->setWordWrap(true);
  l->addWidget(n);
}

// A check box whose text wraps (a QCheckBox's own text never does, and a long one would make the whole page wider than the window): the box, and a label that toggles it.
QCheckBox* PrefsDialog::addCheck(QVBoxLayout* l, const QString& text) {
  auto* w = new QWidget;
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(6);
  auto* cb = new QCheckBox(w);
  auto* lb = new QLabel(text, w);
  lb->setWordWrap(true);
  lb->installEventFilter(this);
  lb->setProperty("xlCheck", QVariant::fromValue<QObject*>(cb));
  cb->installEventFilter(this);
  cb->setProperty("xlLabel", QVariant::fromValue<QObject*>(lb));
  h->addWidget(cb, 0, Qt::AlignTop);
  h->addWidget(lb, 1);
  cb->setToolTip(text);
  l->addWidget(w);
  return cb;
}

bool PrefsDialog::eventFilter(QObject* o, QEvent* e) {
  if (e->type() == QEvent::EnabledChange)   // the text of a check box follows its enabled state (it is a separate label so that it can wrap)
    if (auto* lb = qobject_cast<QLabel*>(o->property("xlLabel").value<QObject*>())) lb->setEnabled(static_cast<QWidget*>(o)->isEnabled());
  if (e->type() == QEvent::MouseButtonRelease) {
    if (auto* cb = qobject_cast<QCheckBox*>(o->property("xlCheck").value<QObject*>())) { if (cb->isEnabled()) cb->toggle(); return true; }
  }
  return QDialog::eventFilter(o, e);
}

QCheckBox* PrefsDialog::check(QVBoxLayout* l, const QString& key, const QString& label, bool XppPrefs::*m) {
  auto* cb = addCheck(l, label);
  reg(key, cb);
  binders_.push_back({[cb, m](const XppPrefs& x, const TabletSettings&) { cb->setChecked(x.*m); }, [cb, m](XppPrefs& x, TabletSettings&) { x.*m = cb->isChecked(); }});
  return cb;
}

QSpinBox* PrefsDialog::ispin(QVBoxLayout* l, const QString& key, const QString& label, int XppPrefs::*m, int lo, int hi, const QString& suffix) {
  auto* sp = new QSpinBox;
  sp->setRange(lo, hi);
  sp->setSuffix(suffix);
  sp->setMinimumWidth(96);
  row(l, label, {sp});
  reg(key, sp);
  binders_.push_back({[sp, m](const XppPrefs& x, const TabletSettings&) { sp->setValue(x.*m); }, [sp, m](XppPrefs& x, TabletSettings&) { x.*m = sp->value(); }});
  return sp;
}

QDoubleSpinBox* PrefsDialog::dspin(QVBoxLayout* l, const QString& key, const QString& label, double XppPrefs::*m, double lo, double hi, double step, int decimals, const QString& suffix) {
  auto* sp = new QDoubleSpinBox;
  sp->setRange(lo, hi);
  sp->setSingleStep(step);
  sp->setDecimals(decimals);
  sp->setSuffix(suffix);
  sp->setMinimumWidth(96);
  row(l, label, {sp});
  reg(key, sp);
  binders_.push_back({[sp, m](const XppPrefs& x, const TabletSettings&) { sp->setValue(x.*m); }, [sp, m](XppPrefs& x, TabletSettings&) { x.*m = sp->value(); }});
  return sp;
}

QComboBox* PrefsDialog::combo(QVBoxLayout* l, const QString& key, const QString& label, int XppPrefs::*m, const QStringList& items) {
  auto* cb = new QComboBox;
  for (int i = 0; i < items.size(); i++) cb->addItem(items[i], i);
  fitCombo(cb);
  row(l, label, {cb});
  reg(key, cb);
  if (m) binders_.push_back({[cb, m](const XppPrefs& x, const TabletSettings&) { cb->setCurrentIndex(std::clamp(x.*m, 0, cb->count() - 1)); }, [cb, m](XppPrefs& x, TabletSettings&) { x.*m = cb->currentIndex(); }});
  return cb;
}

QLineEdit* PrefsDialog::line(QVBoxLayout* l, const QString& key, const QString& label, QString XppPrefs::*m) {
  auto* le = new QLineEdit;
  le->setMinimumWidth(200);
  row(l, label, {le});
  reg(key, le);
  binders_.push_back({[le, m](const XppPrefs& x, const TabletSettings&) { le->setText(x.*m); }, [le, m](XppPrefs& x, TabletSettings&) { x.*m = le->text(); }});
  return le;
}

ColorButton* PrefsDialog::colorRow(QVBoxLayout* l, const QString& key, const QString& label, QString XppPrefs::*m, bool alpha, bool allowDefault, const QColor& shown) {
  auto* cb = new ColorButton(alpha, allowDefault, this);
  cb->setShown(shown);
  row(l, label, {cb});
  reg(key, cb);
  binders_.push_back({[cb, m](const XppPrefs& x, const TabletSettings&) { cb->setValue(x.*m); }, [cb, m](XppPrefs& x, TabletSettings&) { x.*m = cb->value(); }});
  return cb;
}

// The name of the button above its line (the line is as wide as a small window allows).
void PrefsDialog::addButtonRow(QVBoxLayout* l, const QString& label, ButtonRow* br) {
  auto* lb = new QLabel(label);
  lb->setStyleSheet("font-weight: 600;");
  l->addWidget(lb);
  auto* h = new QHBoxLayout;
  h->setContentsMargins(14, 0, 0, 4);
  h->addWidget(br);
  l->addLayout(h);
}

ButtonRow* PrefsDialog::buttonRow(QVBoxLayout* l, const QString& key, const QString& label, ButtonCfg XppPrefs::*m) {
  auto* br = new ButtonRow(toolList_, this);
  addButtonRow(l, label, br);
  reg(key, br);
  reg(key + ".tool", br->tool);
  reg(key + ".colorMode", br->colorMode);
  reg(key + ".color", br->color);
  reg(key + ".size", br->size);
  binders_.push_back({[br, m](const XppPrefs& x, const TabletSettings&) { br->load(x.*m); }, [br, m](XppPrefs& x, TabletSettings&) { x.*m = br->value(); }});
  return br;
}

// ---- 1. Cargar / guardar ---------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildSaveLoad() {
  newPage(pageTitles()[0]);
  {
    auto* l = group(U("Autoguardado"));
    autosaveOn = addCheck(l, U("Activar el autoguardado"));
    reg("save.autosaveOn", autosaveOn);
    autosave = ispin(l, "save.autosave", U("Cada"), &XppPrefs::autosaveInterval, 1, 120, U(" min"));
    connect(autosaveOn, &QCheckBox::toggled, autosave, &QWidget::setEnabled);
    note(l, U("El autoguardado escribe una copia de recuperación aparte: nunca toca tu archivo. Se abre con Archivo → Abrir copia de recuperación."));
  }
  {
    auto* l = group(U("Nombre predeterminado al guardar"));
    note(l, U("Este nombre se propone al guardar un documento nuevo."));
    line(l, "save.name", U("Nombre predeterminado:"), &XppPrefs::saveName);
    note(l, U("Marcadores: %a día abreviado · %A día · %b mes abreviado · %B mes · %c fecha y hora · %d día del mes (01-31) · %F fecha (2001-08-23) · %H hora 24 h · %I hora 12 h · "
              "%j día del año · %m mes (01-12) · %M minuto · %p AM/PM · %S segundo · %U semana (domingo) · %w día de la semana (0 = domingo) · %W semana (lunes) · %x fecha · %X hora · "
              "%y año de 2 cifras · %Y año · %Z zona horaria · %% un %. Los caracteres no válidos en un nombre de archivo se sustituyen por \"_\"."));
  }
  {
    auto* l = group(U("Nombre predeterminado al exportar a PDF"));
    note(l, U("Este nombre se propone al exportar un documento a PDF."));
    line(l, "save.pdfName", U("Nombre predeterminado:"), &XppPrefs::pdfName);
    note(l, U("Además de los marcadores de arriba: %{name} el nombre del PDF de fondo (si lo hay) · %{file} el nombre del archivo guardado."));
  }
  {
    auto* l = group(U("Guardado y apertura"));
    over = addCheck(l, U("Guardar sobre el archivo abierto sin preguntar (si no, la primera vez se pregunta dónde guardar)"));
    reg("save.over", over);
    welcome = addCheck(l, U("Mostrar la página de bienvenida al abrir (si no, se abre un documento en blanco)"));
    reg("save.welcome", welcome);
    check(l, "save.autoloadPdfXoj", U("Cargar automáticamente el cuaderno (.xopp) que está junto a un PDF al abrir el PDF"), &XppPrefs::autoloadPdfXoj);
    check(l, "save.autoloadMostRecent", U("Cargar automáticamente el archivo más reciente al iniciar la aplicación"), &XppPrefs::autoloadMostRecent);
  }
  endPage();
}

// ---- 2. Sistema de entrada ---------------------------------------------------------------------------------------------------------------------
void PrefsDialog::refreshDevices() {
  if (!devices_) return;
  struct Dev { QString key, name, type; };
  QList<Dev> list;
  QSet<QString> seen;
  auto typeName = [](QInputDevice::DeviceType t) {
    switch (t) {
      case QInputDevice::DeviceType::Mouse: return U("Ratón");
      case QInputDevice::DeviceType::TouchScreen: return U("Pantalla táctil");
      case QInputDevice::DeviceType::TouchPad: return U("Panel táctil");
      case QInputDevice::DeviceType::Puck: return U("Puck");
      case QInputDevice::DeviceType::Stylus: return U("Lápiz");
      case QInputDevice::DeviceType::Airbrush: return U("Aerógrafo");
      default: return U("Otro");
    }
  };
  for (const QInputDevice* d : QInputDevice::devices()) {
    if (d->type() == QInputDevice::DeviceType::Keyboard || d->type() == QInputDevice::DeviceType::Unknown) continue;
    const auto* pd = qobject_cast<const QPointingDevice*>(d);
    if (!pd) continue;
    const QString key = deviceKey(pd);
    if (seen.contains(key)) continue;
    seen.insert(key);
    list.append({key, d->name().isEmpty() ? U("(sin nombre)") : d->name(), typeName(d->type())});
  }
  for (auto it = devMap_.begin(); it != devMap_.end(); ++it)   // a device with a saved class that is not connected now keeps its row
    if (!seen.contains(it.key())) list.append({it.key(), it.key().section('|', 0, 0) + U(" (no conectado)"), QString()});
  devices_->setRowCount(0);
  const QStringList classes = {U("Automático (según el dispositivo)"), U("Desactivado"), U("Ratón"), U("Lápiz"), U("Borrador"), U("Pantalla táctil")};
  for (const Dev& d : list) {
    const int r = devices_->rowCount();
    devices_->insertRow(r);
    auto* n = new QTableWidgetItem(d.name);
    n->setData(Qt::UserRole, d.key);
    n->setFlags(n->flags() & ~Qt::ItemIsEditable);
    devices_->setItem(r, 0, n);
    auto* t = new QTableWidgetItem(d.type);
    t->setFlags(t->flags() & ~Qt::ItemIsEditable);
    devices_->setItem(r, 1, t);
    auto* cb = new QComboBox;
    const bool touchScreen = d.key.endsWith("|" + QString::number(int(QInputDevice::DeviceType::TouchScreen)));
    for (int i = 0; i < classes.size(); i++) if (!touchScreen || i <= 1) cb->addItem(classes[i], i);   // a touch screen can be left alone or ignored: that is all the finger path honours
    const int saved = devMap_.value(d.key, 0);
    cb->setCurrentIndex(std::max(0, cb->findData(saved)));
    connect(cb, &QComboBox::currentIndexChanged, this, [this, key = d.key](int i) { if (i <= 0) devMap_.remove(key); else devMap_.insert(key, i); });
    devices_->setCellWidget(r, 2, cb);
  }
  devices_->resizeRowsToContents();
}

void PrefsDialog::buildInput() {
  newPage(pageTitles()[1]);
  {
    auto* l = group(U("Dispositivos de entrada"));
    note(l, U("Elige cómo se trata cada dispositivo que Qt reconoce: como ratón, lápiz, borrador o pantalla táctil, o ignorarlo. «Automático» deja que decida lo que el dispositivo dice ser. "
              "Los dispositivos se detectan al abrir esta ventana; un lápiz solo aparece cuando el controlador lo anuncia."));
    devices_ = new QTableWidget(0, 3);
    devices_->setObjectName("deviceTable");
    devices_->setHorizontalHeaderLabels({U("Dispositivo"), U("Tipo"), U("Tratar como")});
    devices_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    devices_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    devices_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    devices_->verticalHeader()->hide();
    devices_->setSelectionMode(QAbstractItemView::NoSelection);
    devices_->setMinimumHeight(130);
    reg("input.devices", devices_);
    l->addWidget(devices_);
    auto* h = new QHBoxLayout;
    auto* refresh = new QPushButton(U("Buscar dispositivos"));
    connect(refresh, &QPushButton::clicked, this, [this] { refreshDevices(); });
    auto* test = new QPushButton(U("Probar el lápiz…"));
    test->setToolTip(U("Abre «Lápiz y tableta», con la hoja de prueba y la lectura de la presión"));
    connect(test, &QPushButton::clicked, this, [this] { c_.openTablet(); });
    h->addWidget(refresh);
    h->addWidget(test);
    h->addStretch(1);
    l->addLayout(h);
    binders_.push_back({[this](const XppPrefs& x, const TabletSettings&) { devMap_ = x.deviceClass; refreshDevices(); }, [this](XppPrefs& x, TabletSettings&) { x.deviceClass = devMap_; }});
  }
  {
    auto* l = group(U("Opciones"));
    check(l, "input.drawOutside", U("Dibujar fuera de la ventana (el trazo no se corta en el borde de la ventana)"), &XppPrefs::drawOutside);
    auto* merge = addCheck(l, U("Unir los eventos de los botones con los de la punta del lápiz"));
    merge->setChecked(true);
    reg("input.mergeButtons", merge);
    unsupported(l, {merge}, U("con Qt los botones del lápiz siempre llegan junto con los eventos de la punta (es lo que esta opción activa en Windows); no hay otro modo."));
    check(l, "input.pressureGuess", U("Inferir la presión (adivinarla por la velocidad del trazo cuando el dispositivo no la da)"), &XppPrefs::pressureGuess);
  }
  {
    auto* l = group(U("Estabilización de entrada"));
    note(l, U("Elige un algoritmo de estabilización de la entrada y sus parámetros. Actúa sobre el lápiz y el marcador; las distancias son píxeles de pantalla."));
    auto* avg = new QComboBox;
    for (const char* s : {"Ninguno", "Media aritmética", "Gaussiano según la velocidad"}) avg->addItem(U(s));
    row(l, U("Método de promediado"), {avg});
    reg("input.stabAvg", avg);
    auto* pre = new QComboBox;
    for (const char* s : {"Ninguno", "Zona muerta", "Inercia"}) pre->addItem(U(s));
    row(l, U("Preprocesador"), {pre});
    reg("input.stabPre", pre);
    auto* buf = ispin(l, "input.stabBuffer", U("Tamaño del búfer"), &XppPrefs::stabBuffer, 2, 100);
    auto* sigma = dspin(l, "input.stabSigma", U("σ"), &XppPrefs::stabSigma, 0.05, 5, 0.05, 2);
    auto* dead = dspin(l, "input.stabDeadzone", U("Radio de la zona muerta"), &XppPrefs::stabDeadzone, 0.1, 50, 0.1, 2);
    auto* drag = dspin(l, "input.stabDrag", U("Arrastre"), &XppPrefs::stabDrag, 0, 1, 0.05, 2);
    auto* mass = dspin(l, "input.stabMass", U("Masa"), &XppPrefs::stabMass, 1, 30, 0.5, 1);
    check(l, "input.stabCusp", U("Detección de cúspides (los giros bruscos reinician el promedio)"), &XppPrefs::stabCusp);
    check(l, "input.stabFinalize", U("Finalizar el trazo (termina donde estaba realmente el lápiz)"), &XppPrefs::stabFinalize);
    binders_.push_back({[avg, pre](const XppPrefs& x, const TabletSettings&) { avg->setCurrentIndex(int(x.stabAvg)); pre->setCurrentIndex(int(x.stabPre)); },
                        [avg, pre](XppPrefs& x, TabletSettings&) { x.stabAvg = StabAvg(avg->currentIndex()); x.stabPre = StabPre(pre->currentIndex()); }});
    auto refresh = [=] {   // only the parameters of the chosen methods can be edited
      const int a = avg->currentIndex(), p = pre->currentIndex();
      buf->setEnabled(a != 0);
      sigma->setEnabled(a == 2);
      dead->setEnabled(p == 1);
      drag->setEnabled(p == 2);
      mass->setEnabled(p == 2);
    };
    connect(avg, &QComboBox::currentIndexChanged, this, refresh);
    connect(pre, &QComboBox::currentIndexChanged, this, refresh);
    refresh();
  }
  endPage();
}

// ---- 3. Ratón -------------------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildMouse() {
  newPage(pageTitles()[2]);
  auto* l = group(U("Botones del ratón"));
  note(l, U("Define qué herramienta se selecciona mientras mantienes pulsado un botón del ratón (con el color y el grosor que quieras). Al soltarlo vuelve la herramienta anterior. "
            "«Mano» es el arrastre de la página; «Cuadro de herramientas flotante» lo abre al pulsar."));
  buttonRow(l, "mouse.left", U("Botón izquierdo"), &XppPrefs::mouseLeft);
  buttonRow(l, "mouse.middle", U("Botón central"), &XppPrefs::mouseMiddle);
  buttonRow(l, "mouse.right", U("Botón derecho"), &XppPrefs::mouseRight);
  buttonRow(l, "mouse.4", U("4.º botón del ratón"), &XppPrefs::mouse4);
  buttonRow(l, "mouse.5", U("5.º botón del ratón"), &XppPrefs::mouse5);
  note(l, U("Si a un botón adicional (4.º o 5.º) le diste una acción en «Lápiz y tableta → Teclas», esa acción manda sobre esta herramienta."));
  endPage();
}

// ---- 4. Lápiz --------------------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildStylus() {
  newPage(pageTitles()[3]);
  {
    auto* l = group(U("Sensibilidad a la presión"));
    note(l, U("La sensibilidad a la presión permite trazar líneas de distinto grosor según cuánto aprietes el lápiz. Si tu tableta no lo admite, no tiene efecto."));
    check(l, "pen.pressureSens", U("Activar la sensibilidad a la presión"), &XppPrefs::pressureSens);
    note(l, U("Algunos dispositivos dan presiones inesperadamente pequeñas o grandes. Se arregla con una presión mínima o escalando la presión."));
    dspin(l, "pen.minPressure", U("Presión mínima:"), &XppPrefs::minPressure, 0.01, 1, 0.01, 2);
    dspin(l, "pen.pressureMult", U("Multiplicador de presión:"), &XppPrefs::pressureMult, 0.5, 4, 0.1, 2);
    auto* look = new QComboBox;
    look->addItem(U("Prototipo (el que probaste con el lápiz real)"), int(PenLook::Proto));
    look->addItem(U("Como en la app Electron"), int(PenLook::Electron));
    row(l, U("Aspecto del lápiz normal"), {look});
    reg("pen.look", look);
    binders_.push_back({[look](const XppPrefs&, const TabletSettings& t) { look->setCurrentIndex(look->findData(int(t.penLook))); }, [look](XppPrefs&, TabletSettings& t) { t.penLook = PenLook(look->currentData().toInt()); }});
    auto* tablet = new QPushButton(U("Curva de presión y prueba…"));
    tablet->setToolTip(U("Abre «Lápiz y tableta»: punta, tamaño, curva de presión con lectura en vivo, hoja de prueba y teclas del mando"));
    connect(tablet, &QPushButton::clicked, this, [this] { c_.openTablet(); });
    row(l, QString(), {tablet});
  }
  {
    auto* l = group(U("Solución para artefactos"));
    note(l, U("Con algunos equipos los trazos tienen defectos al principio. Se evita ignorando los primeros eventos (puntos) cuando el lápiz toca la pantalla."));
    auto* on = check(l, "pen.ignoreFirst", U("Ignorar los primeros"), &XppPrefs::ignoreFirst);
    auto* n = ispin(l, "pen.ignoreFirstN", QString(), &XppPrefs::ignoreFirstN, 1, 1000, U(" eventos"));
    connect(on, &QCheckBox::toggled, n, &QWidget::setEnabled);
    n->setEnabled(false);
  }
  {
    auto* l = group(U("Botones del lápiz"));
    note(l, U("Especifica las herramientas que se seleccionan mientras pulsas un botón del lápiz o usas el borrador. Al soltarlo vuelve la herramienta anterior. "
              "«Cuadro de herramientas», «Deshacer» y «Rehacer» actúan al pulsar."));
    QList<QPair<QString, QString>> acts;
    for (const PenActionInfo& a : penActions()) acts.append({a.id, a.action == PenAction::None ? U("No cambiar la herramienta") : a.label});
    struct B { const char* key; QString label; PenAction TabletSettings::*tool; ButtonCfg XppPrefs::*cfg; };
    const B bs[] = {{"pen.button1", U("Botón 1"), &TabletSettings::barrel, &XppPrefs::stylus1}, {"pen.button2", U("Botón 2"), &TabletSettings::barrel2, &XppPrefs::stylus2}, {"pen.eraser", U("Borrador"), &TabletSettings::eraser, &XppPrefs::stylusEraser}};
    for (const B& b : bs) {
      auto* br = new ButtonRow(acts, this);
      addButtonRow(l, b.label, br);
      reg(b.key, br);
      reg(QString(b.key) + ".tool", br->tool);
      reg(QString(b.key) + ".colorMode", br->colorMode);
      reg(QString(b.key) + ".color", br->color);
      reg(QString(b.key) + ".size", br->size);
      binders_.push_back({[br, b](const XppPrefs& x, const TabletSettings& t) { ButtonCfg c = x.*(b.cfg); c.tool = penActionId(t.*(b.tool)); br->load(c); },
                          [br, b](XppPrefs& x, TabletSettings& t) {
                            const ButtonCfg c = br->value();
                            PenAction a = PenAction::None;
                            penActionFromId(c.tool, &a);
                            t.*(b.tool) = a;
                            ButtonCfg k = c;
                            k.tool.clear();   // the tool lives in the tablet settings
                            x.*(b.cfg) = k;
                          }});
    }
    note(l, U("El color y el grosor se aplican a la herramienta que elija el botón mientras lo mantienes."));
  }
  {
    auto* l = group(U("Borrador"));
    combo(l, "pen.eraserVisibility", U("El borrador del lápiz es visible"), &XppPrefs::eraserVisibility, {U("Nunca"), U("Siempre"), U("Al flotar"), U("Al tocar")});
    note(l, U("Se refiere al círculo que marca el tamaño del borrador cuando usas el extremo borrador del lápiz."));
  }
  endPage();
}

// ---- 5. Pantalla táctil --------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildTouch() {
  newPage(pageTitles()[4]);
  {
    auto* l = group(U("Dibujo táctil"));
    note(l, U("Usa la pantalla táctil como un lápiz sensible al multitoque. Desactivado, un dedo solo desplaza la página y se ignora la palma."));
    finger = addCheck(l, U("Activar el dibujo táctil (los dedos dibujan)"));
    reg("touch.drawing", finger);
  }
  {
    auto* l = group(U("Reconocimiento de la mano"));
    note(l, U("Si el equipo no reconoce la mano, la app ignora la pantalla táctil mientras el lápiz está cerca de la pantalla."));
    auto* on = check(l, "touch.handRecognition", U("Activar el reconocimiento interno de la mano"), &XppPrefs::handRecognition);
    auto* t = dspin(l, "touch.handTimeout", U("Tiempo de espera"), &XppPrefs::handTimeout, 0.5, 10, 0.1, 1, U(" s (después se reactiva la pantalla táctil)"));
    connect(on, &QCheckBox::toggled, t, &QWidget::setEnabled);
    auto* method = new QComboBox;
    method->addItem(U("Automático (Qt)"));
    reg("touch.method", method);
    row(l, U("Método de desactivación"), {method});
    unsupported(l, {method}, U("la app decide por los eventos del lápiz de Qt; no usa X11 ni órdenes externas, así que los métodos «X11» y «Personalizado» (con sus órdenes de activar y desactivar) no tienen equivalente en Windows."));
  }
  {
    auto* l = group(U("Gestos de zoom"));
    note(l, U("Usa el pellizco para acercar y alejar la página."));
    auto* on = check(l, "touch.zoomGestures", U("Activar los gestos de zoom"), &XppPrefs::zoomGestures);
    auto* th = dspin(l, "touch.zoomStart", U("Empezar a hacer zoom tras una distancia"), &XppPrefs::zoomStartThreshold, 0, 100, 1, 0, U(" % mayor que la distancia inicial entre los dos toques"));
    connect(on, &QCheckBox::toggled, th, &QWidget::setEnabled);
  }
  {
    auto* l = group(U("Desplazamiento táctil"));
    note(l, U("La app desplaza con un dedo y sigue un momento tras un gesto rápido (inercia). Desactivarlo evita saltos si tu pantalla lo percibe así."));
    check(l, "touch.disableInertia", U("Desactivar el desplazamiento por inercia"), &XppPrefs::disableInertia);
  }
  endPage();
}
