// The options of the Fill tool: side, kind, Riemann rectangles, colours, angle, opacity, gap, magnet. Port of the "paint bucket options" of app.ts.
// The options belong to the tool (state().fill); this area never writes the preferences: `state().fillChanged` lets the settings area persist them.
#include "mathstate.h"
#include "mathtools.h"
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace mathobj {
namespace {

class FillPanel final : public QWidget {
 public:
  FillPanel(QWidget* parent, Editor&) : QWidget(parent) {
    setObjectName("fillPanel");
    setFixedWidth(380);
    setStyleSheet("QComboBox { min-height: 22px; } QPushButton { min-height: 24px; } QSlider { min-height: 22px; }");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(5);
    v->addWidget(new QLabel("<b>Relleno a partir de una línea</b>", this));

    auto* side = new QHBoxLayout;
    side->addWidget(new QLabel("Lado", this));
    left_ = new QRadioButton("izquierda", this);
    right_ = new QRadioButton("derecha", this);
    left_->setObjectName("sideLeft");
    right_->setObjectName("sideRight");
    side->addWidget(left_);
    side->addWidget(right_);
    side->addStretch(1);
    v->addLayout(side);

    kind_ = combo("fillKind", {{"Color sólido", "solid"}, {"Degradado lineal", "linear"}, {"Degradado radial", "radial"}, {"Rectángulos (suma de Riemann)", "riemann"}});
    v->addLayout(row("Tipo", kind_));
    rowRule_ = new QWidget(this);
    rule_ = combo("fillRule", {{"por encima (suma superior)", "upper"}, {"por debajo (suma inferior)", "lower"}, {"punto medio", "mid"}, {"extremo izquierdo", "left"}, {"extremo derecho", "right"}});
    rowRule_->setLayout(row("Rectángulos", rule_));
    rowRule_->setToolTip("Por encima: cada rectángulo llega hasta lo más alto de la curva en su columna (la cubre). Por debajo: hasta lo más bajo (queda debajo de ella). Los demás usan la altura de la curva en el punto indicado de la columna");
    v->addWidget(rowRule_);
    rowBase_ = new QWidget(this);
    base_ = combo("fillBase", {{"línea perfectamente recta", "line"}, {"sigue la curva que dibujo", "curve"}});
    rowBase_->setLayout(row("Base", base_));
    rowBase_->setToolTip("Recta: arrastras y la base es una línea perfectamente recta de principio a fin (Mayús: ángulos de 15°; casi horizontal o vertical se endereza sola con el imán). Curva: los rectángulos siguen la curva que dibujas, cada uno apoyado sobre su tramo y mirando al lado elegido");
    v->addWidget(rowBase_);
    rowBy_ = new QWidget(this);
    by_ = combo("fillBy", {{"cantidad", "count"}, {"ancho", "width"}});
    count_ = slider("fillCount", 1, 200, 1);
    width_ = slider("fillWidth", 2, 100, 1);
    byV_ = new QLabel(this);
    byV_->setMinimumWidth(34);
    {
      auto* h = new QHBoxLayout(rowBy_);
      h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel("Definir por", rowBy_));
      h->addWidget(by_);
      h->addWidget(count_, 1);
      h->addWidget(width_, 1);
      h->addWidget(byV_);
    }
    v->addWidget(rowBy_);
    outline_ = new QCheckBox("Contorno de cada rectángulo (trazos del lápiz)", this);
    outline_->setObjectName("fillOutline");
    v->addWidget(outline_);

    rowC2_ = new QWidget(this);
    color2_ = new QPushButton(this);
    color2_->setObjectName("fillColor2");
    color2_->setFixedSize(48, 28);
    {
      auto* h = new QHBoxLayout(rowC2_);
      h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel("Segundo color", rowC2_));
      h->addWidget(color2_);
      h->addStretch(1);
    }
    v->addWidget(rowC2_);
    angle_ = slider("fillAngle", 0, 345, 15);
    angleV_ = new QLabel(this);
    rowAngle_ = sliderRow("Ángulo", angle_, angleV_);
    v->addWidget(rowAngle_);
    opacity_ = slider("fillOpacity", 5, 100, 5);
    opacityV_ = new QLabel(this);
    v->addWidget(sliderRow("Opacidad", opacity_, opacityV_));
    gap_ = slider("fillGap", 0, 32, 1);   // half units: 0 .. 16 in steps of 0.5
    gapV_ = new QLabel(this);
    {
      QWidget* r = sliderRow("Cerrar huecos hasta", gap_, gapV_);
      r->setToolTip("Ancho máximo de una abertura que el relleno no puede cruzar. MÁS ALTO = cierra aberturas más grandes (el relleno se escapa menos), pero también tapa pasillos estrechos");
      v->addWidget(r);
    }
    magnet_ = new QCheckBox("Imán: cerrar aberturas y pegarse al borde solo", this);
    magnet_->setObjectName("fillMagnet");
    magnet_->setToolTip("Si el relleno se escapa por una abertura, la va cerrando sola hasta el límite de abajo; si el clic cae un píxel fuera de una forma cerrada, rellena la forma; y los extremos de la línea arrastrada se pegan a la curva cercana. Mayús al soltar lo desactiva");
    v->addWidget(magnet_);
    max_ = slider("fillMax", 2, 40, 1);
    maxV_ = new QLabel(this);
    rowMax_ = sliderRow("Imán hasta", max_, maxV_);
    v->addWidget(rowMax_);
    keep_ = new QCheckBox("Dejar la línea dibujada como trazo", this);
    keep_->setObjectName("fillKeep");
    v->addWidget(keep_);
    auto* note = new QLabel("Clic: relleno clásico (si la forma tiene una abertura, sale por ella). Arrastrar: la flecha verde marca el sentido y los trazos cortos verdes indican el lado que se rellena. El relleno es una imagen bajo los trazos de la capa: se puede mover, borrar o deshacer. Usa el color del lápiz.", this);
    note->setWordWrap(true);
    note->setStyleSheet("color: #a8a8a8; font-size: 11px;");
    v->addWidget(note);

    for (QComboBox* c : {kind_, rule_, base_, by_}) connect(c, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    for (QSlider* s : {count_, width_, angle_, opacity_, gap_, max_}) connect(s, &QSlider::valueChanged, this, [this] { changed(); });
    for (QCheckBox* c : {outline_, magnet_, keep_}) connect(c, &QCheckBox::toggled, this, [this] { changed(); });
    for (QRadioButton* r : {left_, right_}) connect(r, &QRadioButton::toggled, this, [this] { changed(); });
    connect(color2_, &QPushButton::clicked, this, [this] {
      const QColor c = QColorDialog::getColor(QColor(state().fill.color2), this, "Segundo color");
      if (c.isValid()) { color2Pick_ = c.name(); changed(); }
    });
    sync();
  }

  void showEvent(QShowEvent* e) override {
    QWidget::showEvent(e);
    sync();
  }

 private:
  QComboBox* combo(const QString& name, const std::vector<std::pair<QString, QString>>& items) {
    auto* c = new QComboBox(this);
    c->setObjectName(name);
    for (const auto& [text, data] : items) c->addItem(text, data);
    return c;
  }
  QSlider* slider(const QString& name, int lo, int hi, int step) {
    auto* s = new QSlider(Qt::Horizontal, this);
    s->setObjectName(name);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setPageStep(step * 5);
    s->setMinimumWidth(110);
    return s;
  }
  QHBoxLayout* row(const QString& label, QWidget* w) {
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(new QLabel(label, this));
    h->addWidget(w, 1);
    return h;
  }
  QWidget* sliderRow(const QString& label, QSlider* s, QLabel* value) {
    auto* w = new QWidget(this);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(new QLabel(label, w));
    h->addWidget(s, 1);
    value->setMinimumWidth(44);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(value);
    return w;
  }

  /** The widgets to the options (what syncFillUi does). */
  void sync() {
    syncing_ = true;
    const FillOptions& o = state().fill;
    left_->setChecked(o.side == FillSide::Left);
    right_->setChecked(o.side == FillSide::Right);
    auto pick = [](QComboBox* c, const QString& data) { c->setCurrentIndex(std::max(0, c->findData(data))); };
    const QString kind = fillToJson(o)["kind"].toString();
    pick(kind_, kind);
    pick(rule_, fillToJson(o)["rule"].toString());
    pick(base_, fillToJson(o)["base"].toString());
    pick(by_, fillToJson(o)["by"].toString());
    count_->setValue(o.count);
    width_->setValue(int(std::lround(o.width)));
    outline_->setChecked(o.outline);
    color2_->setStyleSheet(QString("background: %1; border: 1px solid #777; border-radius: 4px;").arg(o.color2));
    color2Pick_.clear();
    angle_->setValue(int(std::lround(o.angle)));
    opacity_->setValue(int(std::lround(o.opacity * 100)));
    gap_->setValue(int(std::lround(o.gap * 2)));
    magnet_->setChecked(o.magnet);
    max_->setValue(int(std::lround(o.maxGap)));
    keep_->setChecked(o.keepLine);
    angleV_->setText(QString("%1°").arg(int(std::lround(o.angle))));
    opacityV_->setText(QString("%1 %").arg(int(std::lround(o.opacity * 100))));
    gapV_->setText(QString::number(o.gap));
    maxV_->setText(QString::number(o.maxGap));
    const bool rm = o.kind == FillKind::Riemann;
    rowC2_->setVisible(o.kind != FillKind::Solid && !rm);
    rowAngle_->setVisible(o.kind == FillKind::Linear);
    rowRule_->setVisible(rm);
    rowBase_->setVisible(rm);
    rowBy_->setVisible(rm);
    outline_->setVisible(rm);
    rowMax_->setVisible(o.magnet);
    count_->setVisible(o.by == RiemannBy::Count);
    width_->setVisible(o.by == RiemannBy::Width);
    byV_->setText(o.by == RiemannBy::Count ? QString::number(o.count) : QString::number(o.width));
    syncing_ = false;
    layout()->activate();
    adjustSize();
  }

  /** The options from the widgets (what fillChanged does): sanitised, then the widgets follow what was kept. */
  void changed() {
    if (syncing_) return;
    FillOptions o = state().fill;
    QJsonObject j = fillToJson(o);
    j["side"] = right_->isChecked() ? "right" : "left";
    j["kind"] = kind_->currentData().toString();
    if (!color2Pick_.isEmpty()) j["color2"] = color2Pick_;
    j["angle"] = angle_->value();
    j["opacity"] = opacity_->value() / 100.0;
    j["gap"] = gap_->value() / 2.0;
    j["keepLine"] = keep_->isChecked();
    j["magnet"] = magnet_->isChecked();
    j["maxGap"] = max_->value();
    j["rule"] = rule_->currentData().toString();
    j["base"] = base_->currentData().toString();
    j["by"] = by_->currentData().toString();
    j["count"] = count_->value();
    j["width"] = width_->value();
    j["outline"] = outline_->isChecked();
    state().fill = sanitizeFill(j);
    sync();
    if (state().fillChanged) state().fillChanged(state().fill);
  }

  bool syncing_ = false;
  QString color2Pick_;
  QRadioButton *left_, *right_;
  QComboBox *kind_, *rule_, *base_, *by_;
  QSlider *count_, *width_, *angle_, *opacity_, *gap_, *max_;
  QLabel *byV_, *angleV_, *opacityV_, *gapV_, *maxV_;
  QCheckBox *outline_, *magnet_, *keep_;
  QPushButton* color2_;
  QWidget *rowRule_, *rowBase_, *rowBy_, *rowC2_, *rowAngle_, *rowMax_;
};

}  // namespace

QWidget* makeFillPanel(QWidget* parent, Editor& ed) { return new FillPanel(parent, ed); }

}  // namespace mathobj
