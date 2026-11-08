#include "chrome/svgicon.h"
#include "tabletdialog.h"
#include "settings.h"
#include "tabletwidgets.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStandardItemModel>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
struct Express { const char *label, *key, *alt; };
// The keys of the remote as the driver should be told to send them: Ctrl+Alt+Shift+number can be typed in its field; F13 and up are the alternative when it lets you pick from a list.
const Express kExpress[] = {
  {"Tecla 1", "Ctrl+Alt+Shift+1", "F13"}, {"Tecla 2", "Ctrl+Alt+Shift+2", "F14"}, {"Tecla 3", "Ctrl+Alt+Shift+3", "F15"}, {"Tecla 4", "Ctrl+Alt+Shift+4", "F16"},
  {"Tecla 5", "Ctrl+Alt+Shift+5", "F17"}, {"Tecla 6", "Ctrl+Alt+Shift+6", "F18"}, {"Tecla 7", "Ctrl+Alt+Shift+7", "F19"}, {"Tecla 8", "Ctrl+Alt+Shift+8", "F20"},
  {"Dial ↻ (si el controlador lo deja como tecla)", "Ctrl+Alt+Shift+9", "F21"}, {"Dial ↺ (si el controlador lo deja como tecla)", "Ctrl+Alt+Shift+0", "F22"},
};
const char* kSuggested[] = {"tool.pen", "tool.eraser", "tool.highlighter", "tool.select", "edit.undo", "edit.redo", "brush.next", "color.next", "view.zoomIn", "view.zoomOut"};

const char* kStyle = R"(
QLabel[dlg="h3"] { font-size: 13px; font-weight: bold; color: #ffffff; padding-top: 10px; }
QLabel[dlg="note"] { color: #a8a8a8; font-size: 12px; }
QLabel[dlg="kbd"] { background: #3a3a3a; border: 1px solid #5a5a5a; border-radius: 5px; padding: 4px 8px; font-family: Consolas, monospace; }
QComboBox, QDoubleSpinBox { min-height: 22px; padding: 0 6px; }
QPushButton { min-height: 24px; padding: 0 12px; border-radius: 4px; }
QPushButton[primary="true"] { background: #2f6fd0; border: 1px solid #6aa0ff; color: white; }
QPushButton[on="true"] { background: #b8860b; color: white; }
QSlider::groove:horizontal { height: 6px; background: #444444; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #2f6fd0; border-radius: 3px; }
QSlider::handle:horizontal { background: #8ab4ff; width: 14px; height: 14px; margin: -4px 0; border-radius: 7px; }
QProgressBar { background: #333333; border: 0; border-radius: 6px; max-height: 14px; min-height: 14px; }
QProgressBar::chunk { background: #2f9bff; border-radius: 6px; }
QProgressBar[mapped="true"]::chunk { background: #ff5a8a; }
QScrollArea { border: 0; }
)";

QLabel* tagged(const QString& text, const char* kind, QWidget* parent) {
  auto* l = new QLabel(text, parent);
  l->setProperty("dlg", kind);
  if (QString(kind) == "note") l->setWordWrap(true);
  return l;
}

/** Actions grouped under non-selectable headers, as the optgroups of the Electron dialog. */
void fillActionCombo(QComboBox* cb, bool withNone) {
  auto* model = new QStandardItemModel(cb);
  if (withNone) { auto* none = new QStandardItem(QString::fromUtf8("— nada —")); none->setData(QString(), Qt::UserRole); model->appendRow(none); }
  QString group;
  for (const AppAction& a : appActions()) {
    if (a.group != group) {
      group = a.group;
      auto* h = new QStandardItem(group);
      h->setFlags(Qt::NoItemFlags);
      QFont f = h->font();
      f.setBold(true);
      h->setFont(f);
      model->appendRow(h);
    }
    auto* it = new QStandardItem("    " + a.label);
    it->setData(a.id, Qt::UserRole);
    model->appendRow(it);
  }
  cb->setModel(model);
  cb->view()->setMinimumWidth(320);
}

void selectData(QComboBox* cb, const QString& data) {
  for (int i = 0; i < cb->count(); i++)
    if (cb->itemData(i, Qt::UserRole).toString() == data && (cb->model()->flags(cb->model()->index(i, 0)) & Qt::ItemIsEnabled)) { cb->setCurrentIndex(i); return; }
}
QString actionLabel(const QString& id) {
  for (const AppAction& a : appActions()) if (a.id == id) return a.label;
  return id;
}
}  // namespace

TabletDialog::TabletDialog(SettingsController& c, QWidget* parent) : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint), c_(c) {
  setObjectName("tabletDlg");
  setWindowTitle(QString::fromUtf8("Lápiz y tableta"));
  setModal(false);
  setStyleSheet(chrome::neonizeCss(QString::fromLatin1(kStyle)));
  build();
}

BrushCard* TabletDialog::card(BrushKind k) const { auto it = cards_.find(k); return it == cards_.end() ? nullptr : it->second; }
QString TabletDialog::readoutText() const { return readout_->text(); }

void TabletDialog::build() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(10, 8, 10, 10);
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  body_ = new QVBoxLayout(content);
  body_->setContentsMargins(6, 0, 12, 6);
  body_->setSpacing(6);
  scroll->setWidget(content);
  outer->addWidget(scroll, 1);

  // ---- tip and size
  body_->addWidget(tagged(QString::fromUtf8("Punta y tamaño"), "h3", content));
  auto* cardRow = new QHBoxLayout;
  for (const BrushInfo& b : brushes()) {
    auto* card = new BrushCard(b, content);
    cards_[b.kind] = card;
    connect(card, &QAbstractButton::clicked, this, [this, k = b.kind] { s_.brush = k; commit(); });
    cardRow->addWidget(card);
  }
  body_->addLayout(cardRow);
  auto* sizeRow = new QHBoxLayout;
  sizeRange_ = new QSlider(Qt::Horizontal, content);
  sizeRange_->setRange(4, 480);   // 0.2 .. 24 in steps of 0.05
  sizeNum_ = new QDoubleSpinBox(content);
  sizeNum_->setRange(0.1, 60);
  sizeNum_->setSingleStep(0.05);
  sizeNum_->setDecimals(2);
  sizeNum_->setMinimumWidth(120);
  connect(sizeRange_, &QSlider::valueChanged, this, [this](int v) { if (!building_) setSize(v / 20.0); });
  connect(sizeNum_, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!building_) setSize(v); });
  sizeRow->addWidget(new QLabel(QString::fromUtf8("Grosor"), content));
  sizeRow->addWidget(sizeRange_, 1);
  sizeRow->addWidget(sizeNum_);
  body_->addLayout(sizeRow);
  body_->addWidget(tagged(QString::fromUtf8("El grosor es el de referencia: cada punta lo lleva más fino o más grueso según presión, velocidad o dirección. En un lápiz óptico con presión se nota más; con el ratón, la tinta y el pincel responden a la velocidad."), "note", content));

  // ---- pressure
  body_->addWidget(tagged(QString::fromUtf8("Presión"), "h3", content));
  auto* pr = new QHBoxLayout;
  curve_ = new CurveWidget(content);
  curve_->changed = [this](const PressureCurve& cv) { if (!building_) { s_.curve = cv; commit(); } };
  pr->addWidget(curve_, 0, Qt::AlignTop);
  auto* side = new QVBoxLayout;
  side->addWidget(new QLabel(QString::fromUtf8("Ajuste"), content));
  preset_ = new QComboBox(content);
  preset_->addItem(QString::fromUtf8("Curva personalizada"), -1);
  int pi = 0;
  for (const CurvePreset& p : curvePresets()) preset_->addItem(p.name, pi++);
  connect(preset_, &QComboBox::activated, this, [this](int) {
    const int i = preset_->currentData().toInt();
    if (i >= 0 && i < int(curvePresets().size())) { s_.curve = curvePresets()[size_t(i)].curve; commit(); }
  });
  side->addWidget(preset_);
  side->addWidget(new QLabel(QString::fromUtf8("Zona muerta (presión mínima que cuenta)"), content));
  dead_ = new QSlider(Qt::Horizontal, content);
  dead_->setRange(0, 300);   // 0 .. 0.3
  connect(dead_, &QSlider::valueChanged, this, [this](int v) { if (!building_) { s_.curve.min = v / 1000.0; commit(); } });
  side->addWidget(dead_);
  side->addWidget(new QLabel(QString::fromUtf8("Grosor del bolígrafo según la presión"), content));
  look_ = new QComboBox(content);
  look_->addItem(QString::fromUtf8("Como el prototipo (por defecto): 30 % a 100 % del grosor"), 0);
  look_->addItem(QString::fromUtf8("Como la versión Electron: 1,5 × presión × grosor"), 1);
  look_->setToolTip(QString::fromUtf8("Solo afecta a la punta Bolígrafo con lápiz óptico. Las demás puntas llevan su propia curva."));
  connect(look_, &QComboBox::activated, this, [this](int) { s_.penLook = look_->currentData().toInt() == 1 ? PenLook::Electron : PenLook::Proto; commit(); });
  side->addWidget(look_);
  side->addWidget(new QLabel(QString::fromUtf8("Presión del lápiz"), content));
  bar_ = new QProgressBar(content);
  bar_->setRange(0, 1000);
  bar_->setTextVisible(false);
  side->addWidget(bar_);
  side->addWidget(new QLabel(QString::fromUtf8("Presión con la curva"), content));
  barMapped_ = new QProgressBar(content);
  barMapped_->setRange(0, 1000);
  barMapped_->setTextVisible(false);
  barMapped_->setProperty("mapped", true);
  side->addWidget(barMapped_);
  readout_ = new QLabel(QString::fromUtf8("Mueve el lápiz sobre la tableta (o toca la pantalla) para ver la presión."), content);
  readout_->setWordWrap(true);
  readout_->setStyleSheet("font-family: Consolas, monospace; font-size: 12px; color: #cfcfcf;");
  readout_->setMinimumHeight(96);
  readout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  side->addWidget(readout_);
  side->addWidget(new QLabel(QString::fromUtf8("Entrada sin procesar (últimos eventos; cópialo si un botón no responde)"), content));
  trace_ = new QPlainTextEdit(content);
  trace_->setReadOnly(true);
  trace_->setMaximumBlockCount(12);
  trace_->setLineWrapMode(QPlainTextEdit::NoWrap);
  trace_->setFocusPolicy(Qt::ClickFocus);
  trace_->setStyleSheet("font-family: Consolas, monospace; font-size: 11px;");
  trace_->setPlaceholderText(QString::fromUtf8("Pulsa un botón del lápiz o toca con la punta: cada pulsación y suelta aparece aquí."));
  trace_->setFixedHeight(trace_->fontMetrics().lineSpacing() * 12 + 14);
  side->addWidget(trace_);
  side->addStretch(1);
  pr->addLayout(side, 1);
  body_->addLayout(pr);
  body_->addWidget(new QLabel(QString::fromUtf8("Hoja de prueba (dibuja aquí con la punta elegida)"), content));
  pad_ = new PadWidget(content);
  body_->addWidget(pad_);
  auto* clear = new QPushButton(QString::fromUtf8("Limpiar la hoja de prueba"), content);
  clear->setAutoDefault(false);
  connect(clear, &QPushButton::clicked, pad_, &PadWidget::clear);
  body_->addWidget(clear, 0, Qt::AlignLeft);

  // ---- pen buttons
  body_->addWidget(tagged(QString::fromUtf8("Botones del lápiz"), "h3", content));
  auto* grid = new QGridLayout;
  grid->setColumnStretch(1, 1);
  const char* labels[3] = {"Botón lateral 1 (el más cercano a la punta)", "Botón lateral 2", "Extremo borrador"};
  for (int i = 0; i < 3; i++) {
    penSel_[i] = new QComboBox(content);
    for (const PenActionInfo& a : penActions()) penSel_[i]->addItem(a.label, a.id);
    connect(penSel_[i], &QComboBox::activated, this, [this, i](int) {
      PenAction a;
      if (!penActionFromId(penSel_[i]->currentData().toString(), &a)) return;
      (i == 0 ? s_.barrel : i == 1 ? s_.barrel2 : s_.eraser) = a;
      commit();
    });
    grid->addWidget(new QLabel(QString::fromUtf8(labels[i]), content), i, 0);
    grid->addWidget(penSel_[i], i, 1);
  }
  body_->addLayout(grid);
  body_->addWidget(tagged(QString::fromUtf8("Una herramienta se usa mientras mantienes pulsado el botón y vuelve a la anterior al soltarlo. Deshacer y rehacer actúan al pulsar. Si el controlador XP-Pen reasigna los botones a otras funciones (clic derecho, teclas, etc.), aquí no llegan como botones: déjalos en «clic derecho» / «borrador» en el controlador, o usa los atajos de teclado de abajo."), "note", content));

  // ---- express keys of the remote
  body_->addWidget(tagged(QString::fromUtf8("Teclas exprés del mando (Tecla 1, Tecla 2…)"), "h3", content));
  body_->addWidget(tagged(QString::fromUtf8("El controlador de XP-Pen no deja que una tecla se identifique sola: a cada una hay que ponerle una combinación de teclado. Usa estas, que puedes teclear en su campo (mantén Ctrl, Alt y Mayús y pulsa el número) y casi nunca chocan con nada; aquí eliges qué hace cada una. Si el controlador te deja elegir la tecla de una lista, también valen F13 a F22."), "note", content));
  auto* exGrid = new QGridLayout;
  exGrid->setColumnStretch(0, 1);
  int row = 0;
  for (const Express& e : kExpress) {
    auto* cb = new QComboBox(content);
    fillActionCombo(cb, true);
    const QString key = QString::fromUtf8(e.key), alt = QString::fromUtf8(e.alt);
    connect(cb, &QComboBox::activated, this, [this, cb, key, alt](int) {
      for (const QString& k : {key, alt}) {
        const QString v = cb->currentData(Qt::UserRole).toString();
        if (!v.isEmpty()) s_.keys[k] = v; else s_.keys.remove(k);
      }
      commit();
    });
    express_.push_back(cb);
    QString shown = QString::fromUtf8(e.key);
    shown.replace("Shift", QString::fromUtf8("Mayús"));
    exGrid->addWidget(new QLabel(QString::fromUtf8(e.label) + QString::fromUtf8("  →  en el controlador pulsa ") + shown, content), row, 0);
    exGrid->addWidget(cb, row, 1);
    row++;
  }
  body_->addLayout(exGrid);
  suggest_ = new QPushButton(QString::fromUtf8("Proponer una asignación"), content);
  suggest_->setAutoDefault(false);
  suggest_->setToolTip(QString::fromUtf8("Rellena las teclas vacías con una asignación cómoda para dibujar"));
  connect(suggest_, &QPushButton::clicked, this, [this] {
    for (size_t i = 0; i < express_.size(); i++) {
      const QString key = QString::fromUtf8(kExpress[i].key), alt = QString::fromUtf8(kExpress[i].alt);
      if (!s_.keys.contains(key)) { s_.keys[key] = kSuggested[i]; s_.keys[alt] = kSuggested[i]; }
    }
    commit();
  });
  body_->addWidget(suggest_, 0, Qt::AlignLeft);

  // ---- bindings
  body_->addWidget(tagged(QString::fromUtf8("Atajos de teclado (teclas de la tableta y del mando ACK05)"), "h3", content));
  body_->addWidget(tagged(QString::fromUtf8("El controlador convierte cada tecla del mando en una combinación (por ejemplo Ctrl+Alt+1). Aquí eliges una acción, pulsas «Asignar» y luego pulsas la tecla del mando o giras su rueda (también vale la rueda con Ctrl, Alt o Mayús): queda enlazada. Estos atajos tienen prioridad sobre los de siempre."), "note", content));
  auto* add = new QHBoxLayout;
  action_ = new QComboBox(content);
  fillActionCombo(action_, false);
  action_->setCurrentIndex(1);   // the first real action, not the group header
  picked_ = action_->currentData(Qt::UserRole).toString();
  connect(action_, &QComboBox::activated, this, [this](int) { picked_ = action_->currentData(Qt::UserRole).toString(); });
  capture_ = new QPushButton(QString::fromUtf8("Asignar tecla…"), content);
  capture_->setAutoDefault(false);
  connect(capture_, &QPushButton::clicked, this, [this] { if (c_.capturing()) stopCapture(); else startCapture(); });
  add->addWidget(action_, 1);
  add->addWidget(capture_);
  body_->addLayout(add);
  keyList_ = new QWidget(content);
  new QGridLayout(keyList_);
  body_->addWidget(keyList_);
  body_->addStretch(1);

  auto* foot = new QHBoxLayout;
  foot->addStretch(1);
  auto* close = new QPushButton("Cerrar", this);
  close->setProperty("primary", true);
  close->setAutoDefault(false);
  connect(close, &QPushButton::clicked, this, &QDialog::close);
  foot->addWidget(close);
  outer->addLayout(foot);
}

void TabletDialog::openDialog() {
  s_ = c_.editor().tablet();
  size_ = c_.editor().style().penWidth;
  s_.size = size_;
  levels_.clear();
  recent_.clear();
  trace_->clear();
  lastMask_[0] = lastMask_[1] = 0;
  lastKey_ = 0;
  curve_->setRecent(recent_);
  pad_->clear();
  paintAll();
  c_.restoreGeometry(this, "tablet", QSize(900, 880));
  show();
  raise();
  activateWindow();
}

void TabletDialog::hideEvent(QHideEvent* e) {
  stopCapture();
  c_.rememberGeometry(this, "tablet");
  QDialog::hideEvent(e);
}

void TabletDialog::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { e->accept(); return; }   // Enter must not close it while a field has the focus
  QDialog::keyPressEvent(e);
}

void TabletDialog::commit() {
  s_.size = size_;
  c_.applyTablet(s_);
  paintAll();
}

void TabletDialog::paintAll() {
  building_ = true;
  for (auto& [kind, card] : cards_) { card->setChecked(kind == s_.brush); card->setLook(size_, s_.curve); }
  sizeRange_->setValue(int(std::lround(std::min(24.0, size_) * 20)));
  sizeNum_->setValue(std::round(size_ * 100) / 100);
  const PenAction pens[3] = {s_.barrel, s_.barrel2, s_.eraser};
  for (int i = 0; i < 3; i++) penSel_[i]->setCurrentIndex(std::max(0, penSel_[i]->findData(penActionId(pens[i]))));
  int idx = -1, i = 0;
  for (const CurvePreset& p : curvePresets()) {
    const PressureCurve& a = p.curve;
    if (std::abs(a.x1 - s_.curve.x1) < 1e-6 && std::abs(a.y1 - s_.curve.y1) < 1e-6 && std::abs(a.x2 - s_.curve.x2) < 1e-6 && std::abs(a.y2 - s_.curve.y2) < 1e-6 && std::abs(a.min - s_.curve.min) < 1e-6) { idx = i; break; }
    i++;
  }
  preset_->setCurrentIndex(preset_->findData(idx));
  dead_->setValue(int(std::lround(s_.curve.min * 1000)));
  look_->setCurrentIndex(s_.penLook == PenLook::Electron ? 1 : 0);
  curve_->setCurve(s_.curve);
  pad_->setConfig(s_.brush, size_, s_.curve);
  for (size_t k = 0; k < express_.size(); k++) selectData(express_[k], s_.keys.value(QString::fromUtf8(kExpress[k].key)));
  building_ = false;
  paintKeys();
}

void TabletDialog::paintKeys() {
  auto* g = qobject_cast<QGridLayout*>(keyList_->layout());
  while (QLayoutItem* it = g->takeAt(0)) { if (it->widget()) { it->widget()->hide(); it->widget()->deleteLater(); } delete it; }
  keyRows_ = 0;
  g->setColumnStretch(1, 1);
  if (s_.keys.isEmpty()) { g->addWidget(tagged(QString::fromUtf8("Aún no hay atajos propios."), "note", keyList_), 0, 0, 1, 3); return; }
  int r = 0;
  for (auto it = s_.keys.begin(); it != s_.keys.end(); ++it, ++r) {
    auto* kbd = tagged(it.key(), "kbd", keyList_);
    auto* del = new QPushButton("Quitar", keyList_);
    del->setAutoDefault(false);
    connect(del, &QPushButton::clicked, this, [this, combo = it.key()] { removeKey(combo); });
    g->addWidget(kbd, r, 0);
    g->addWidget(new QLabel(actionLabel(it.value()), keyList_), r, 1);
    g->addWidget(del, r, 2);
    keyRows_++;
  }
}

void TabletDialog::removeKey(const QString& combo) { s_.keys.remove(combo); commit(); }
void TabletDialog::setCurve(const PressureCurve& cv) { s_.curve = cv; commit(); }
void TabletDialog::setSize(double v) { if (!std::isfinite(v) || v <= 0) return; size_ = std::clamp(v, 0.1, 60.0); commit(); }
void TabletDialog::setBrush(BrushKind k) { s_.brush = k; commit(); }
void TabletDialog::setPenAction(int which, PenAction a) { (which == 0 ? s_.barrel : which == 1 ? s_.barrel2 : s_.eraser) = a; commit(); }

void TabletDialog::startCapture() {
  picked_ = action_->currentData(Qt::UserRole).toString();
  if (picked_.isEmpty()) return;
  capture_->setText(QString::fromUtf8("Pulsa la tecla o gira la rueda… (Esc cancela)"));
  capture_->setProperty("on", true);
  capture_->style()->unpolish(capture_);
  capture_->style()->polish(capture_);
  c_.beginCapture([this](const QString& combo) {
    const bool ok = !combo.isEmpty();
    stopCapture();
    if (ok) { s_.keys[combo] = picked_; commit(); }
  });
}

void TabletDialog::stopCapture() {
  if (c_.capturing()) c_.endCapture();
  capture_->setText(QString::fromUtf8("Asignar tecla…"));
  capture_->setProperty("on", false);
  capture_->style()->unpolish(capture_);
  capture_->style()->polish(capture_);
}

void TabletDialog::sample(const QString& device, double pressure, double tiltX, double tiltY, int buttons, PenButton button) {
  if (!isVisible()) return;
  const PressureMap map(s_.curve);
  const double mapped = map.apply(pressure);
  bar_->setValue(int(std::lround(pressure * 1000)));
  barMapped_->setValue(int(std::lround(mapped * 1000)));
  if (pressure > 0) {
    levels_.insert(int(std::lround(pressure * 100000)));
    recent_.push_back({pressure, mapped});
    if (recent_.size() > 60) recent_.erase(recent_.begin());
    curve_->setRecent(recent_);
  }
  const QString b = button == PenButton::Eraser ? QString::fromUtf8("  (extremo borrador)") : button == PenButton::Barrel ? QString::fromUtf8("  (botón lateral 1)") : button == PenButton::Barrel2 ? QString::fromUtf8("  (botón lateral 2)") : QString();
  readout_->setText(QString::fromUtf8("Dispositivo: %1\nPresión: %2 %  →  %3 % con la curva\nInclinación: X %4°  Y %5°\nBotones: %6%7\nNiveles de presión distintos vistos: %8")
                        .arg(device.isEmpty() ? "-" : device).arg(pressure * 100, 0, 'f', 1).arg(mapped * 100, 0, 'f', 1).arg(int(tiltX)).arg(int(tiltY)).arg(buttons).arg(b).arg(levels_.size()));
}

QString TabletDialog::buttonWords(int mask) {
  QStringList w;
  if (mask & Qt::LeftButton) w << QString::fromUtf8("izq");
  if (mask & Qt::RightButton) w << QString::fromUtf8("der");
  if (mask & Qt::MiddleButton) w << QString::fromUtf8("central");
  if (mask & Qt::BackButton) w << QString::fromUtf8("atrás");
  if (mask & Qt::ForwardButton) w << QString::fromUtf8("adelante");
  if (mask & ~(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton | Qt::BackButton | Qt::ForwardButton)) w << QString::fromUtf8("otro");
  return w.isEmpty() ? QString::fromUtf8("ninguno") : w.join('+');
}

QString TabletDialog::traceText() const { return trace_->toPlainText(); }

void TabletDialog::logInput(bool tablet, const QString& phase, int button, int buttons, const QString& source, const QString& device, qulonglong key) {
  if (!isVisible()) return;
  if (key == lastKey_) return;
  lastKey_ = key;
  const bool move = phase == QLatin1String("mueve");
  int& last = lastMask_[tablet ? 1 : 0];
  if (move && buttons == last) return;
  last = buttons;
  trace_->appendPlainText(QString::fromUtf8("%1 %2  botón=%3  botones=%4  sintético=%5  disp=%6")
                              .arg(tablet ? "Tablet" : "Ratón ", -6).arg(phase, -7).arg(buttonWords(button), -8).arg(buttonWords(buttons), -14).arg(source, -7).arg(device.isEmpty() ? "-" : device));
}
