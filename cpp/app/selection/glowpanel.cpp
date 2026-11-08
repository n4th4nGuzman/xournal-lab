// The "Brillo" button of the toolbar and its panel (Electron: btnGlow / glowPanel in app.ts): the switch for new strokes, the size and the intensity of the glow, a
// preview drawn by the same renderer the page uses, and the two buttons that put the glow on / take it off the selected strokes.
#include "glowpanel.h"
#include "editor.h"
#include "mainwindow.h"
#include "pagerender.h"
#include "selicons.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>

namespace selx {
namespace {

/** The preview: a curve with the glow of the current settings, drawn with the page's own stroke renderer so it looks like what the pen will leave. */
class GlowDemo : public QWidget {
 public:
  explicit GlowDemo(Editor& ed, QWidget* parent) : QWidget(parent), ed_(ed) { setFixedSize(220, 58); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1b, 0x1b, 0x1b));
    p.setRenderHint(QPainter::Antialiasing);
    const Style& s = ed_.style();
    Stroke st;
    st.attrs = {{"tool", "pen"}, {"color", s.color + "ff"}, {"capStyle", "round"}};
    if (s.glowOn) {
      st.attrs.emplace_back("glow", QString::number(s.glowSize));
      st.attrs.emplace_back("glowAlpha", QString::number(s.glowIntensity));
    }
    // M8 28 C 40 4, 70 4, 100 22 S 160 36, 192 12 of the Electron demo, sampled
    auto bez = [](double t, double a, double b, double c, double d) { const double u = 1 - t; return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d; };
    const double sx = width() / 200.0, sy = height() / 40.0;
    for (int i = 0; i <= 40; i++) {
      const double t = i / 40.0;
      st.pts.push_back(bez(t, 8, 40, 70, 100) * sx);
      st.pts.push_back(bez(t, 28, 4, 4, 22) * sy);
      st.widths.push_back(2.4);
    }
    for (int i = 1; i <= 40; i++) {
      const double t = i / 40.0;
      st.pts.push_back(bez(t, 100, 130, 160, 192) * sx);
      st.pts.push_back(bez(t, 22, 40, 36, 12) * sy);
      st.widths.push_back(2.4);
    }
    RenderOptions o;
    o.scale = devicePixelRatioF();
    o.darkPaper = true;   // the preview ground is dark: the pen colour is shown as it is
    renderElement(p, Element(st), o);
  }

 private:
  Editor& ed_;
};

class GlowPanel : public QWidget {
 public:
  GlowPanel(Editor& ed, QWidget* parent) : QWidget(parent), ed_(ed) {
    setObjectName("glowPanel");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(8);
    on_ = new QCheckBox(QString::fromUtf8("Brillo en los trazos nuevos"), this);
    QFont b = on_->font();
    b.setBold(true);
    on_->setFont(b);
    on_->setMinimumHeight(32);
    v->addWidget(on_);
    auto row = [&](const QString& label, QSlider*& s, QLabel*& val, int lo, int hi, int step) {
      auto* h = new QHBoxLayout;
      h->setSpacing(8);
      h->addWidget(new QLabel(label, this));
      s = new QSlider(Qt::Horizontal, this);
      s->setRange(lo, hi);
      s->setSingleStep(step);
      s->setPageStep(step * 2);
      s->setMinimumHeight(32);   // a finger or a pen can hold it
      h->addWidget(s, 1);
      val = new QLabel(this);
      val->setMinimumWidth(44);
      val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      h->addWidget(val);
      v->addLayout(h);
    };
    row(QString::fromUtf8("Tamaño"), size_, sizeV_, 1, 40, 1);
    row(QString::fromUtf8("Intensidad"), inten_, intenV_, 5, 100, 5);
    demo_ = new GlowDemo(ed, this);
    v->addWidget(demo_, 0, Qt::AlignHCenter);
    auto* btns = new QHBoxLayout;
    apply_ = new QPushButton(QString::fromUtf8("Aplicar a la selección"), this);
    apply_->setToolTip(QString::fromUtf8("Poner este brillo a los trazos seleccionados"));
    remove_ = new QPushButton(QString::fromUtf8("Quitar de la selección"), this);
    remove_->setToolTip(QString::fromUtf8("Quitar el brillo de los trazos seleccionados"));
    for (QPushButton* p : {apply_, remove_}) { p->setMinimumHeight(36); btns->addWidget(p); }
    v->addLayout(btns);

    connect(on_, &QCheckBox::toggled, this, [this] { push(); });
    connect(size_, &QSlider::valueChanged, this, [this] { push(); });
    connect(inten_, &QSlider::valueChanged, this, [this] { push(); });
    connect(apply_, &QPushButton::clicked, this, [this] { ed_.action("sel.glowApply")->trigger(); });
    connect(remove_, &QPushButton::clicked, this, [this] { ed_.action("sel.glowRemove")->trigger(); });
    connect(&ed_, &Editor::styleChanged, this, [this] { sync(); });
    sync();
  }

 private:
  void sync() {
    if (busy_) return;
    busy_ = true;
    const Style& s = ed_.style();
    on_->setChecked(s.glowOn);
    size_->setValue(int(std::lround(s.glowSize)));
    inten_->setValue(int(std::lround(s.glowIntensity * 100 / 5)) * 5);
    sizeV_->setText(QString::number(size_->value()));
    intenV_->setText(QString::number(inten_->value()) + " %");
    busy_ = false;
    update();
    if (demo_) demo_->update();
  }
  void push() {
    if (busy_) return;
    Style s = ed_.style();
    s.glowOn = on_->isChecked();
    s.glowSize = size_->value();
    s.glowIntensity = inten_->value() / 100.0;
    ed_.setStyle(s);   // emits styleChanged: sync() redraws the labels and the preview; the settings area stores it
  }
  Editor& ed_;
  QCheckBox* on_ = nullptr;
  QSlider *size_ = nullptr, *inten_ = nullptr;
  QLabel *sizeV_ = nullptr, *intenV_ = nullptr;
  QWidget* demo_ = nullptr;
  QPushButton *apply_ = nullptr, *remove_ = nullptr;
  bool busy_ = false;
};

}  // namespace

QToolButton* makeGlowButton(Editor& ed, QToolBar* bar) {
  auto* btn = new QToolButton(bar);
  btn->setObjectName("btnGlow");
  btn->setToolTip(QString::fromUtf8("Brillo (glow) de los trazos: interruptor, tamaño e intensidad"));
  btn->setPopupMode(QToolButton::InstantPopup);
  btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
  auto refresh = [btn, &ed] { btn->setIcon(selIcon(ed.style().glowOn ? "glowOn" : "glow")); };
  refresh();
  QObject::connect(&ed, &Editor::styleChanged, btn, refresh);
  auto* menu = new QMenu(btn);
  auto* wa = new QWidgetAction(menu);
  wa->setDefaultWidget(new GlowPanel(ed, menu));   // before the action joins the menu: a menu asks its widget actions for their widget when they are added
  menu->addAction(wa);
  btn->setMenu(menu);
  return btn;
}

}  // namespace selx
