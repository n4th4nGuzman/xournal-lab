// The option panels of the plane, 3D space and figure tools (QWidgets shown in the popup of the caret next to each toolbar button). Port of the panels of
// app.ts (index.html: planePanel, space3Panel, figPanel) with the same fields, texts and tooltips. With a plane/space/figure selected a change rebuilds
// THAT object (one undo step per edit, like the TS); with nothing selected it changes the settings of the next one. Plane and 3D settings are kept in
// prefs.json ("plane", "space3") by Hub; the figure options are not stored, as in the Electron app.
#include "editor.h"
#include "hub.h"
#include "objects.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <functional>
#include <map>

namespace xlobjects {
namespace {

/** A number field that shows 1 and not 1.000000 (the TS inputs are type=number step=any) and commits on Enter, focus loss or the arrows. */
class NumBox : public QDoubleSpinBox {
 public:
  NumBox(double lo, double hi) {
    setLocale(QLocale::c());
    setDecimals(6);
    setRange(lo, hi);
    setKeyboardTracking(false);
    setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    setMinimumWidth(70);
  }
  QString textFromValue(double v) const override { return QString::number(v, 'g', 10); }
};

const char* kStyle =
    "QPushButton { min-height: 22px; padding: 2px 10px; }"
    "QCheckBox { min-height: 22px; }"
    "QComboBox, QLineEdit, QAbstractSpinBox { min-height: 22px; }"
    "QLabel#note { color: #9a9a9a; font-size: 11px; }"
    "QLabel#title { font-weight: bold; font-size: 13px; }"
    "QLabel#group { color: #b8b8b8; font-size: 11px; margin-top: 4px; }"
    "QPushButton#fig { min-height: 26px; padding: 2px 8px; text-align: center; }"
    "QPushButton#fig:checked { background: #2f7dd1; color: white; }";

class PanelBase : public QWidget {
 public:
  PanelBase(QWidget* parent, Editor& ed, const QString& title, int width) : QWidget(parent), ed_(ed), hub_(Hub::of(ed)) {
    setStyleSheet(kStyle);
    setFixedWidth(width);
    col_ = new QVBoxLayout(this);
    col_->setContentsMargins(12, 10, 12, 10);
    col_->setSpacing(4);
    auto* t = new QLabel(title, this);
    t->setObjectName("title");
    col_->addWidget(t);
  }

 protected:
  /** Call once the controls exist: the panel follows the selection, undo/redo and the settings changing elsewhere. */
  void follow() {
    connect(&hub_, &Hub::settingsChanged, this, [this] { refresh(); });
    connect(&ed_, &Editor::selectionChanged, this, [this] { refresh(); });
    connect(&ed_, &Editor::historyChanged, this, [this] { refresh(); });
    refresh();
  }
  /** Derived destructors call this first: the children are destroyed after the derived part is gone, and a focused field may still emit a change then. */
  void shutdown() {
    syncing_ = true;
    for (QObject* c : findChildren<QObject*>()) c->disconnect(this);
    disconnect(&hub_, nullptr, this, nullptr);
    disconnect(&ed_, nullptr, this, nullptr);
  }
  void showEvent(QShowEvent* e) override { QWidget::showEvent(e); refresh(); }
  void refresh() { if (syncing_) return; syncing_ = true; sync(); syncing_ = false; }
  virtual void sync() = 0;

  QLabel* note(const QString& text) {
    auto* l = new QLabel(text, this);
    l->setObjectName("note");
    l->setWordWrap(true);
    return l;
  }
  QLabel* mode() { mode_ = note(QString()); col_->addWidget(mode_); return mode_; }
  QCheckBox* check(const QString& text, const QString& tip = QString()) {
    auto* c = new QCheckBox(text, this);
    if (!tip.isEmpty()) c->setToolTip(tip);
    return c;
  }
  NumBox* num(double lo, double hi, const QString& tip = QString()) {
    auto* n = new NumBox(lo, hi);
    if (!tip.isEmpty()) n->setToolTip(tip);
    return n;
  }
  /** Run `fn` when a control is changed by the user (not while sync() fills it in). */
  template <class W, class Sig> void onEdit(W* w, Sig sig, std::function<void()> fn) {
    connect(w, sig, this, [this, fn = std::move(fn)] { if (!syncing_) fn(); });
  }
  void onNum(NumBox* n, std::function<void()> fn) { onEdit(n, QOverload<double>::of(&QDoubleSpinBox::valueChanged), std::move(fn)); }
  void onCheck(QCheckBox* c, std::function<void()> fn) { onEdit(c, &QCheckBox::toggled, std::move(fn)); }
  void onCombo(QComboBox* c, std::function<void()> fn) { onEdit(c, QOverload<int>::of(&QComboBox::currentIndexChanged), std::move(fn)); }
  void onText(QLineEdit* l, std::function<void()> fn) { onEdit(l, &QLineEdit::editingFinished, std::move(fn)); }

  static void setNum(NumBox* n, double v) { if (std::abs(n->value() - v) > 1e-9) { QSignalBlocker b(n); n->setValue(v); } }
  static void setCheck(QCheckBox* c, bool v) { QSignalBlocker b(c); c->setChecked(v); }
  static void setText(QLineEdit* l, const QString& v) { if (l->text() != v) { QSignalBlocker b(l); l->setText(v); } }
  static void setCombo(QComboBox* c, const QVariant& data) { QSignalBlocker b(c); const int i = c->findData(data); if (i >= 0) c->setCurrentIndex(i); }

  Editor& ed_;
  Hub& hub_;
  QVBoxLayout* col_;
  QLabel* mode_ = nullptr;
  bool syncing_ = false;
};

// ---- plane ----------------------------------------------------------------------------------------------------------------------------------
class PlanePanel final : public PanelBase {
 public:
  ~PlanePanel() override { shutdown(); }
  PlanePanel(QWidget* parent, Editor& ed) : PanelBase(parent, ed, QStringLiteral("Plano cartesiano"), 380) {
    mode();
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(8);
    NumBox** boxes[] = {&xmin_, &xmax_, &ymin_, &ymax_, &xstep_, &ystep_};
    const char* names[] = {"x mín", "x máx", "y mín", "y máx", "escala x", "escala y"};
    const char* tips[] = {"", "", "", "", "Distancia entre marcas, números y líneas de la cuadrícula en el eje x", "Distancia entre marcas, números y líneas de la cuadrícula en el eje y"};
    for (int i = 0; i < 6; i++) {
      *boxes[i] = i < 4 ? num(-1e9, 1e9, tips[i]) : num(1e-6, 1e9, tips[i]);
      auto* lab = new QLabel(names[i], this);
      lab->setToolTip(tips[i]);
      g->addWidget(lab, i / 2, (i % 2) * 2);
      g->addWidget(*boxes[i], i / 2, (i % 2) * 2 + 1);
      onNum(*boxes[i], [this] { apply(); });
    }
    col_->addLayout(g);

    grid_ = check(QStringLiteral("Cuadrícula (desmárcala para dejar solo los ejes)"));
    col_->addWidget(grid_);
    auto* subRow = new QHBoxLayout;
    subRow->addWidget(new QLabel(QStringLiteral("Subcuadrícula"), this));
    sub_ = new QComboBox(this);
    sub_->addItem("no", 0);
    for (int n : {2, 4, 5, 10}) sub_->addItem(QString("%1 por casilla").arg(n), n);
    subRow->addWidget(sub_, 1);
    col_->addLayout(subRow);
    ticks_ = check(QStringLiteral("Marcas en los ejes"));
    numbers_ = check(QStringLiteral("Números"));
    arrows_ = check(QStringLiteral("Flechas en los extremos"));
    square_ = check(QStringLiteral("Casillas cuadradas (misma escala en x e y)"));
    for (QCheckBox* c : {ticks_, numbers_, arrows_, square_}) col_->addWidget(c);
    auto* names2 = new QHBoxLayout;
    names2->addWidget(new QLabel(QStringLiteral("Nombres de los ejes"), this));
    xl_ = new QLineEdit(this);
    yl_ = new QLineEdit(this);
    for (QLineEdit* l : {xl_, yl_}) { l->setMaxLength(12); l->setMinimumWidth(56); names2->addWidget(l, 1); }
    col_->addLayout(names2);

    auto* btns = new QHBoxLayout;
    auto* axesOnly = new QPushButton(QStringLiteral("Solo ejes"), this);
    axesOnly->setToolTip(QStringLiteral("Quitar la cuadrícula y dejar solo los ejes"));
    auto* restyle = new QPushButton(QStringLiteral("Estilo del lápiz"), this);
    restyle->setToolTip(QStringLiteral("Dibujar el plano seleccionado con el color, el grosor y el brillo del lápiz de ahora"));
    btns->addWidget(axesOnly);
    btns->addWidget(restyle);
    col_->addLayout(btns);
    col_->addWidget(note(QStringLiteral("Con un plano seleccionado, los cambios se aplican a él; si no, valen para el próximo. El color, el grosor y el brillo salen del lápiz. Arrastra las asas para redimensionarlo: la escala numérica se conserva.")));

    onCheck(grid_, [this] { apply(); });
    onCombo(sub_, [this] { apply(); });
    for (QCheckBox* c : {ticks_, numbers_, arrows_, square_}) onCheck(c, [this] { apply(); });
    onText(xl_, [this] { apply(); });
    onText(yl_, [this] { apply(); });
    connect(axesOnly, &QPushButton::clicked, this, [this] { setCheck(grid_, false); setCombo(sub_, 0); apply(); });
    connect(restyle, &QPushButton::clicked, this, [this] {
      if (!hub_.selectedPlane()) { ed_.notify(QStringLiteral("Selecciona un plano para darle el estilo del lápiz")); return; }
      apply(true);
    });
    follow();
  }

 protected:
  void sync() override {
    const auto sel = hub_.selectedPlane();
    const PlaneSpec& s = sel ? sel->spec : hub_.plane;
    setNum(xmin_, s.xmin); setNum(xmax_, s.xmax); setNum(ymin_, s.ymin); setNum(ymax_, s.ymax);
    setNum(xstep_, s.xstep); setNum(ystep_, s.ystep);
    setCheck(grid_, s.grid);
    setCombo(sub_, s.sub);
    sub_->setEnabled(s.grid);
    setCheck(ticks_, s.ticks); setCheck(numbers_, s.numbers); setCheck(arrows_, s.arrows); setCheck(square_, s.square);
    setText(xl_, s.xlabel); setText(yl_, s.ylabel);
    mode_->setText(sel ? QStringLiteral("Editando el plano seleccionado.") : QStringLiteral("Valores para el próximo plano: elige la herramienta y arrastra un recuadro."));
  }

 private:
  void apply(bool restyle = false) {
    if (!(xmin_->value() < xmax_->value()) || !(ymin_->value() < ymax_->value())) {   // keep what there was
      ed_.notify(!(xmin_->value() < xmax_->value()) ? QStringLiteral("x mín tiene que ser menor que x máx") : QStringLiteral("y mín tiene que ser menor que y máx"));
      refresh();
      return;
    }
    PlaneSpec raw;
    raw.xmin = xmin_->value(); raw.xmax = xmax_->value(); raw.ymin = ymin_->value(); raw.ymax = ymax_->value();
    raw.xstep = xstep_->value(); raw.ystep = ystep_->value();
    raw.grid = grid_->isChecked(); raw.sub = sub_->currentData().toInt();
    raw.ticks = ticks_->isChecked(); raw.numbers = numbers_->isChecked(); raw.arrows = arrows_->isChecked(); raw.square = square_->isChecked();
    raw.xlabel = xl_->text(); raw.ylabel = yl_->text();
    const PlaneSpec spec = sanitizePlane(raw);
    hub_.plane = spec;
    hub_.updatePlane(spec, restyle);
    hub_.persistPlane();
    refresh();
  }

  NumBox *xmin_, *xmax_, *ymin_, *ymax_, *xstep_, *ystep_;
  QCheckBox *grid_, *ticks_, *numbers_, *arrows_, *square_;
  QComboBox* sub_;
  QLineEdit *xl_, *yl_;
};

// ---- 3D space -------------------------------------------------------------------------------------------------------------------------------
double round1(double v) { return std::round(v * 10) / 10; }

class Space3Panel final : public PanelBase {
 public:
  ~Space3Panel() override { shutdown(); }
  Space3Panel(QWidget* parent, Editor& ed) : PanelBase(parent, ed, QStringLiteral("Espacio cartesiano 3D"), 700) {
    mode();
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(4);
    const char* heads[] = {"", "mín", "máx", "escala", "ángulo +°", "largo +", "ángulo −°", "largo −", "nombre"};
    const char* tips[] = {"", "", "", "", "Ángulo de la mitad positiva del eje", "Largo en la hoja de una unidad de la mitad positiva", "Ángulo de la mitad negativa del eje", "Largo en la hoja de una unidad de la mitad negativa", ""};
    for (int c = 0; c < 9; c++) {
      auto* l = new QLabel(heads[c], this);
      l->setToolTip(tips[c]);
      l->setAlignment(Qt::AlignCenter);
      g->addWidget(l, 0, c);
    }
    const char* axisNames[] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
      auto* ax = new QLabel(QString("<b>%1</b>").arg(axisNames[i]), this);
      ax->setAlignment(Qt::AlignCenter);
      g->addWidget(ax, i + 1, 0);
      min_[i] = num(-1e9, 1e9);
      max_[i] = num(-1e9, 1e9);
      step_[i] = num(1e-6, 1e9);
      ang_[i] = num(-3600, 3600, tips[4]);
      len_[i] = num(2, 1e5, tips[5]);
      angN_[i] = num(-3600, 3600, tips[6]);
      lenN_[i] = num(2, 1e5, tips[7]);
      name_[i] = new QLineEdit(this);
      name_[i]->setMaxLength(12);
      name_[i]->setMinimumWidth(44);
      QWidget* ws[] = {min_[i], max_[i], step_[i], ang_[i], len_[i], angN_[i], lenN_[i], name_[i]};
      for (int c = 0; c < 8; c++) g->addWidget(ws[c], i + 1, c + 1);
      for (NumBox* n : {min_[i], max_[i], step_[i], ang_[i], len_[i], angN_[i], lenN_[i]}) onNum(n, [this] { apply(); });
      onText(name_[i], [this] { apply(); });
    }
    col_->addLayout(g);

    auto* gr = new QHBoxLayout;
    gr->addWidget(new QLabel(QStringLiteral("Cuadrícula en el plano"), this));
    gridXY_ = check("xy"); gridXZ_ = check("xz"); gridYZ_ = check("yz");
    for (QCheckBox* c : {gridXY_, gridXZ_, gridYZ_}) { gr->addWidget(c); onCheck(c, [this] { apply(); }); }
    gr->addStretch(1);
    col_->addLayout(gr);
    ticks_ = check(QStringLiteral("Marcas en los ejes"));
    numbers_ = check(QStringLiteral("Números"));
    arrows_ = check(QStringLiteral("Flechas en los extremos"));
    for (QCheckBox* c : {ticks_, numbers_, arrows_}) { col_->addWidget(c); onCheck(c, [this] { apply(); }); }

    auto* btns = new QHBoxLayout;
    auto* reset = new QPushButton(QStringLiteral("Vista por defecto"), this);
    reset->setToolTip(QStringLiteral("Volver a los ángulos y largos habituales (z arriba, y a la derecha, x hacia ti)"));
    auto* restyle = new QPushButton(QStringLiteral("Estilo del lápiz"), this);
    restyle->setToolTip(QStringLiteral("Dibujar el espacio seleccionado con el color, el grosor y el brillo del lápiz de ahora"));
    btns->addWidget(reset);
    btns->addWidget(restyle);
    btns->addStretch(1);
    col_->addLayout(btns);
    col_->addWidget(note(QStringLiteral("Con un espacio seleccionado aparecen círculos al final de cada mitad de cada eje (la positiva y la negativa): arrástralos para estirar y girar solo esa mitad (Mayús conserva su largo). El ángulo se mide desde la derecha, en sentido antihorario.")));

    connect(reset, &QPushButton::clicked, this, [this] {
      const auto u = defaultVectors();
      for (int i = 0; i < 3; i++) {
        const Angle v = vectorAngle(u[size_t(i)]);
        setNum(ang_[i], round1(v.deg)); setNum(len_[i], round1(v.len));
        setNum(angN_[i], round1(std::fmod(v.deg + 180, 360))); setNum(lenN_[i], round1(v.len));
      }
      forceView_ = true;
      apply();
    });
    connect(restyle, &QPushButton::clicked, this, [this] {
      if (!hub_.selectedSpace3()) { ed_.notify(QStringLiteral("Selecciona un espacio 3D para darle el estilo del lápiz")); return; }
      apply(true);
    });
    follow();
  }

 protected:
  void sync() override {
    const auto sel = hub_.selectedSpace3();
    const Space3Spec& s = sel ? sel->spec : hub_.space3;
    const double lo[] = {s.xmin, s.ymin, s.zmin}, hi[] = {s.xmax, s.ymax, s.zmax}, st[] = {s.xstep, s.ystep, s.zstep};
    for (int i = 0; i < 3; i++) {
      setNum(min_[i], lo[i]); setNum(max_[i], hi[i]); setNum(step_[i], st[i]);
      const Angle v = vectorAngle(s.u[size_t(i)]);
      setNum(ang_[i], round1(v.deg)); setNum(len_[i], round1(v.len));
      const Angle vn = vectorAngle({-s.n[size_t(i)][0], -s.n[size_t(i)][1]});   // the way the negative half points on the page
      setNum(angN_[i], round1(vn.deg)); setNum(lenN_[i], round1(vn.len));
      setText(name_[i], s.names[size_t(i)]);
    }
    setCheck(gridXY_, s.gridXY); setCheck(gridXZ_, s.gridXZ); setCheck(gridYZ_, s.gridYZ);
    setCheck(ticks_, s.ticks); setCheck(numbers_, s.numbers); setCheck(arrows_, s.arrows);
    mode_->setText(sel ? QStringLiteral("Editando el espacio seleccionado.") : QStringLiteral("Valores para el próximo espacio: elige la herramienta y arrastra."));
  }

 private:
  void apply(bool restyle = false) {
    const char* axis[] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++)
      if (!(min_[i]->value() < max_[i]->value())) {
        ed_.notify(QString("%1 mín tiene que ser menor que %1 máx").arg(axis[i]));
        refresh();
        forceView_ = false;
        return;
      }
    const auto sel = hub_.selectedSpace3();
    const Space3Spec base = sel ? sel->spec : hub_.space3;
    Space3Spec raw = base;
    raw.xmin = min_[0]->value(); raw.xmax = max_[0]->value(); raw.xstep = step_[0]->value();
    raw.ymin = min_[1]->value(); raw.ymax = max_[1]->value(); raw.ystep = step_[1]->value();
    raw.zmin = min_[2]->value(); raw.zmax = max_[2]->value(); raw.zstep = step_[2]->value();
    raw.gridXY = gridXY_->isChecked(); raw.gridXZ = gridXZ_->isChecked(); raw.gridYZ = gridYZ_->isChecked();
    raw.ticks = ticks_->isChecked(); raw.numbers = numbers_->isChecked(); raw.arrows = arrows_->isChecked();
    for (int i = 0; i < 3; i++) {
      const size_t k = size_t(i);
      raw.names[k] = name_[i]->text();
      // the fields show angles and lengths with one decimal: an axis whose fields were not touched keeps its exact vector instead of the rounded one
      const Angle bu = vectorAngle(base.u[k]), bn = vectorAngle({-base.n[k][0], -base.n[k][1]});
      const bool uSame = !forceView_ && std::abs(ang_[i]->value() - round1(bu.deg)) < 1e-9 && std::abs(len_[i]->value() - round1(bu.len)) < 1e-9;
      const bool nSame = !forceView_ && std::abs(angN_[i]->value() - round1(bn.deg)) < 1e-9 && std::abs(lenN_[i]->value() - round1(bn.len)) < 1e-9;
      if (!uSame) raw.u[k] = axisVector(ang_[i]->value(), std::max(2.0, len_[i]->value()));
      if (!nSame) { const V2 w = axisVector(angN_[i]->value(), std::max(2.0, lenN_[i]->value())); raw.n[k] = {-w[0], -w[1]}; }
    }
    forceView_ = false;
    const Space3Spec spec = sanitizeSpace3(raw);
    hub_.space3 = spec;
    hub_.space3.o = {0, 0};
    hub_.updateSpace3(spec, restyle);
    hub_.persistSpace3();
    refresh();
  }

  NumBox *min_[3], *max_[3], *step_[3], *ang_[3], *len_[3], *angN_[3], *lenN_[3];
  QLineEdit* name_[3];
  QCheckBox *gridXY_, *gridXZ_, *gridYZ_, *ticks_, *numbers_, *arrows_;
  bool forceView_ = false;
};

// ---- figures --------------------------------------------------------------------------------------------------------------------------------
class FigurePanel final : public PanelBase {
 public:
  ~FigurePanel() override { shutdown(); }
  FigurePanel(QWidget* parent, Editor& ed) : PanelBase(parent, ed, QStringLiteral("Figuras"), 400) {
    // the gallery by groups, in the order of the catalogue
    QString group;
    QGridLayout* row = nullptr;
    int n = 0;
    for (const FigureCatalogItem& item : figureCatalog()) {
      if (QString::fromUtf8(item.group) != group) {
        group = QString::fromUtf8(item.group);
        auto* t = new QLabel(group, this);
        t->setObjectName("group");
        col_->addWidget(t);
        row = new QGridLayout;
        row->setSpacing(4);
        col_->addLayout(row);
        n = 0;
      }
      auto* b = new QPushButton(QString::fromUtf8(item.name), this);
      b->setObjectName("fig");
      b->setCheckable(true);
      b->setToolTip(item.clicks ? QStringLiteral("Un clic por punto; doble clic o Intro termina") : QStringLiteral("Arrastra para dibujarla"));
      row->addWidget(b, n / 3, n % 3);
      n++;
      buttons_[item.kind] = b;
      const FigKind kind = item.kind;
      connect(b, &QPushButton::clicked, this, [this, kind] {
        hub_.figKind = kind;
        if (!hub_.selectedFigure()) { hub_.endFigure(false); ed_.setTool(QStringLiteral("figure")); }
        hub_.changed();
        refresh();
      });
    }

    auto* r1 = new QHBoxLayout;
    r1->addWidget(new QLabel(QStringLiteral("Puntas de flecha"), this));
    heads_ = new QComboBox(this);
    heads_->addItem("ninguna", int(Heads::None));
    heads_->addItem("al final", int(Heads::End));
    heads_->addItem("en ambos extremos", int(Heads::Both));
    r1->addWidget(heads_, 1);
    col_->addLayout(r1);
    auto* r2 = new QHBoxLayout;
    r2->addWidget(new QLabel(QStringLiteral("Trazo"), this));
    dash_ = new QComboBox(this);
    dash_->addItem("continuo", int(Dash::Solid));
    dash_->addItem("rayas", int(Dash::Dash));
    dash_->addItem("puntos", int(Dash::Dot));
    r2->addWidget(dash_, 1);
    col_->addLayout(r2);
    auto* r3 = new QHBoxLayout;
    nLabel_ = new QLabel(QStringLiteral("Lados / meridianos"), this);
    n_ = new QSpinBox(this);
    n_->setRange(3, 12);
    mLabel_ = new QLabel(QStringLiteral("Paralelos"), this);
    m_ = new QSpinBox(this);
    m_->setRange(0, 9);
    for (QSpinBox* s : {n_, m_}) s->setKeyboardTracking(false);
    r3->addWidget(nLabel_); r3->addWidget(n_); r3->addSpacing(8); r3->addWidget(mLabel_); r3->addWidget(m_); r3->addStretch(1);
    col_->addLayout(r3);
    auto* r4 = new QHBoxLayout;
    hLabel_ = new QLabel(QStringLiteral("Aristas ocultas"), this);
    hidden_ = new QComboBox(this);
    hidden_->addItem("a rayas", int(Hidden::Dash));
    hidden_->addItem("no mostrar", int(Hidden::None));
    hidden_->addItem("como las demás", int(Hidden::Solid));
    r4->addWidget(hLabel_);
    r4->addWidget(hidden_, 1);
    col_->addLayout(r4);
    auto* r5 = new QHBoxLayout;
    r5->addWidget(new QLabel(QStringLiteral("Rótulo"), this));
    label_ = new QLineEdit(this);
    label_->setMaxLength(40);
    r5->addWidget(label_, 1);
    col_->addLayout(r5);
    auto* restyle = new QPushButton(QStringLiteral("Estilo del lápiz"), this);
    restyle->setToolTip(QStringLiteral("Dar a la figura seleccionada el color, grosor y brillo del lápiz"));
    auto* rb = new QHBoxLayout;
    rb->addWidget(restyle);
    rb->addStretch(1);
    col_->addLayout(rb);
    col_->addWidget(note(QStringLiteral("Con una figura seleccionada, los cambios se aplican a ella; si no, valen para la próxima. Sus círculos se arrastran para editarla (Mayús: ángulos de 15°; en un sólido, conserva el largo de la arista). El color, el grosor y el brillo salen del lápiz.")));

    onCombo(heads_, [this] { apply(); });
    onCombo(dash_, [this] { apply(); });
    onCombo(hidden_, [this] { apply(); });
    onEdit(n_, QOverload<int>::of(&QSpinBox::valueChanged), [this] { apply(); });
    onEdit(m_, QOverload<int>::of(&QSpinBox::valueChanged), [this] { apply(); });
    onText(label_, [this] { apply(); });
    connect(restyle, &QPushButton::clicked, this, [this] {
      if (!hub_.selectedFigure()) { ed_.notify(QStringLiteral("Selecciona una figura para darle el estilo del lápiz")); return; }
      apply(true);
    });
    follow();
  }

 protected:
  void sync() override {
    const auto sel = hub_.selectedFigure();
    const FigKind kind = sel ? sel->spec.kind : hub_.figKind;
    FigOpts o = hub_.figOpts;
    if (sel) o = FigOpts{sel->spec.heads, sel->spec.dash, sel->spec.n, sel->spec.m, sel->spec.hidden, sel->spec.label};
    for (const auto& [k, b] : buttons_) { QSignalBlocker blk(b); b->setChecked(k == kind); }
    setCombo(heads_, int(o.heads)); setCombo(dash_, int(o.dash)); setCombo(hidden_, int(o.hidden));
    { QSignalBlocker b(n_); n_->setValue(o.n); }
    { QSignalBlocker b(m_); m_->setValue(o.m); }
    setText(label_, o.label);
    // a row that does not apply to this kind stays visible (the popup does not resize) but greyed out
    const bool nOk = kind == FigKind::Regular || kind == FigKind::Sphere, hOk = isSolid(kind);
    for (QWidget* w : {static_cast<QWidget*>(nLabel_), static_cast<QWidget*>(n_), static_cast<QWidget*>(mLabel_), static_cast<QWidget*>(m_)}) w->setEnabled(nOk);
    for (QWidget* w : {static_cast<QWidget*>(hLabel_), static_cast<QWidget*>(hidden_)}) w->setEnabled(hOk);
  }

 private:
  void apply(bool restyle = false) {
    const auto sel = hub_.selectedFigure();
    FigureSpec base;
    if (sel) base = sel->spec;
    else { base.kind = hub_.figKind; base.pts = {0, 0, 0, 40, 40, 40, 40, 0}; }
    base.heads = Heads(heads_->currentData().toInt());
    base.dash = Dash(dash_->currentData().toInt());
    base.n = n_->value();
    base.m = m_->value();
    base.hidden = Hidden(hidden_->currentData().toInt());
    base.label = label_->text();
    const FigureSpec clean = sanitizeFigure(base);
    hub_.figOpts = FigOpts{clean.heads, clean.dash, clean.n, clean.m, clean.hidden, clean.label};
    if (sel) hub_.updateFigure(clean, restyle);
    hub_.changed();
    refresh();
  }

  std::map<FigKind, QPushButton*> buttons_;
  QComboBox *heads_, *dash_, *hidden_;
  QSpinBox *n_, *m_;
  QLabel *nLabel_, *mLabel_, *hLabel_;
  QLineEdit* label_;
};

}  // namespace

QWidget* makePlanePanel(QWidget* parent, Editor& ed) { return new PlanePanel(parent, ed); }
QWidget* makeSpace3Panel(QWidget* parent, Editor& ed) { return new Space3Panel(parent, ed); }
QWidget* makeFigurePanel(QWidget* parent, Editor& ed) { return new FigurePanel(parent, ed); }

}  // namespace xlobjects
