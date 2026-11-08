// Pen diagnostic (diagnostic.ts): a box to draw one slow, curved stroke in, and a report of how precisely the input arrives. Qt delivers tablet and mouse
// positions to the widget itself, so "physical pixels" here are logical position times the display scale.
#include "parts.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QTabletEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace chrome {
namespace {
double frac(double v) { return std::abs(v - std::round(v)); }
}

PenReport analyzePen(const std::vector<PenSample>& samples, double dpr) {
  PenReport r;
  r.samples = int(samples.size());
  r.dpr = dpr;
  std::vector<const PenSample*> base;
  for (const PenSample& s : samples) if (s.type == "pointermove") base.push_back(&s);
  if (base.empty()) for (const PenSample& s : samples) base.push_back(&s);
  int fracPhys = 0;
  QSet<int> fractions, pressures;
  std::vector<double> dts;
  std::map<QString, int> types;
  for (size_t i = 0; i < base.size(); i++) {
    const PenSample& s = *base[i];
    if (frac(s.x * dpr) > 1e-4 || frac(s.y * dpr) > 1e-4) fracPhys++;
    fractions.insert(int(std::lround(frac(s.x) * 100)));
    pressures.insert(int(std::lround(s.pressure * 10000)));
    types[s.pointerType]++;
    if (i > 0) { const double d = s.t - base[i - 1]->t; if (d > 0) dts.push_back(d); }
  }
  std::sort(dts.begin(), dts.end());
  r.medianIntervalMs = dts.empty() ? 0 : std::round(dts[dts.size() / 2] * 100) / 100;
  r.eventsPerSecond = r.medianIntervalMs > 0 ? int(std::lround(1000 / r.medianIntervalMs)) : 0;
  r.fractionalPhysical = base.empty() ? 0 : std::round(1000.0 * fracPhys / double(base.size())) / 1000;
  r.distinctFractions = int(fractions.size());
  r.distinctPressures = int(pressures.size());
  QStringList ts;
  for (const auto& [k, v] : types) ts << QString("%1: %2").arg(k).arg(v);
  r.pointerTypes = ts.isEmpty() ? "-" : ts.join(", ");
  // What matters is whether positions have a fractional part in PHYSICAL pixels: on a scaled screen the logical pixels are fractional anyway.
  if (base.empty()) r.verdict = "Sin datos: dibuja un trazo dentro del recuadro.";
  else if (r.fractionalPhysical > 0.3) r.verdict = "La entrada llega con DECIMALES incluso en píxeles físicos: tiene precisión de sub-píxel. Los escalones a zoom bajo no vendrían de aquí.";
  else r.verdict = QString("Posiciones ENTERAS en píxeles físicos%1: el sistema redondea la entrada a píxeles de pantalla. Esa puede ser la fuente de los escalones a zoom bajo.")
                       .arg(dpr != 1 ? QString(" (en píxeles lógicos son múltiplos de 1/%1)").arg(dpr) : QString());
  return r;
}

QString penReportText(const PenReport& r) {
  auto pct = [](double v) { return QString("%1 %").arg(int(std::lround(v * 100))); };
  return QStringList{
      r.verdict,
      "",
      QString("Muestras: %1   (%2)").arg(r.samples).arg(r.pointerTypes),
      QString("Con decimales en píxeles físicos: %1").arg(pct(r.fractionalPhysical)),
      QString("Valores distintos de la parte decimal de X: %1").arg(r.distinctFractions),
      QString("Intervalo mediano entre eventos: %1 ms  (~%2 eventos/s)").arg(r.medianIntervalMs).arg(r.eventsPerSecond),
      QString("Niveles distintos de presión: %1").arg(r.distinctPressures),
      QString("Escala de pantalla (devicePixelRatio): %1").arg(r.dpr),
  }.join("\n");
}

class PenCanvas : public QWidget {
 public:
  explicit PenCanvas(PenDiagnostic* d) : QWidget(d), dlg_(d) {
    setMinimumHeight(220);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    clock_.start();
  }
  std::vector<PenSample> samples;
  std::vector<QPointF> path;
  bool down = false;
  void begin() { samples.clear(); path.clear(); update(); }
  void finish() { down = false; dlg_->feed(samples); }
 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (size_t i = 1; i < path.size(); i++) p.drawLine(path[i - 1], path[i]);
  }
  void record(const QPointF& pos, const QString& type, double pressure, double t) {
    samples.push_back({"pointermove", type, pos.x(), pos.y(), pressure, t});
    path.push_back(pos);
    update();
  }
  void tabletEvent(QTabletEvent* e) override {
    e->accept();
    const QString type = e->pointerType() == QPointingDevice::PointerType::Eraser ? "eraser" : "pen";
    if (e->type() == QEvent::TabletPress) { down = true; begin(); record(e->position(), type, e->pressure(), double(e->timestamp())); }
    else if (e->type() == QEvent::TabletMove && down) record(e->position(), type, e->pressure(), double(e->timestamp()));
    else if (e->type() == QEvent::TabletRelease && down) finish();
  }
  void mousePressEvent(QMouseEvent* e) override {
    if (e->source() != Qt::MouseEventNotSynthesized) return;
    down = true; begin(); record(e->position(), "mouse", 0.5, double(e->timestamp()));
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (down && e->source() == Qt::MouseEventNotSynthesized) record(e->position(), "mouse", 0.5, double(e->timestamp()));
  }
  void mouseReleaseEvent(QMouseEvent*) override { if (down) finish(); }
 private:
  PenDiagnostic* dlg_;
  QElapsedTimer clock_;
};

PenDiagnostic::PenDiagnostic(Chrome& c, QWidget* parent) : QDialog(parent) {
  Q_UNUSED(c);
  setWindowTitle("Diagnóstico del lápiz");
  setObjectName("penDlg");
  setModal(false);   // like the floating dialogs of the Electron app: the document stays visible behind it
  resize(560, 520);
  auto* lay = new QVBoxLayout(this);
  canvas_ = new PenCanvas(this);
  canvas_->setObjectName("penCanvas");
  lay->addWidget(canvas_);
  report_ = new QLabel(this);
  report_->setObjectName("penReport");
  report_->setWordWrap(true);
  report_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  report_->setFont(QFont("Consolas", 10));
  report_->setMinimumHeight(150);
  report_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  lay->addWidget(report_, 1);
  auto* row = new QHBoxLayout;
  row->addStretch(1);
  auto* copy = new QPushButton("Copiar informe", this);
  auto* close = new QPushButton("Cerrar", this);
  close->setDefault(true);
  row->addWidget(copy);
  row->addWidget(close);
  lay->addLayout(row);
  connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(report_->text()); });
  connect(close, &QPushButton::clicked, this, &QDialog::close);
}

void PenDiagnostic::openDialog() {
  canvas_->begin();
  report_->setText("Dibuja con el lápiz (o el ratón) un trazo lento y curvo dentro del recuadro.");
  show();
  raise();
  activateWindow();
}

QString PenDiagnostic::reportText() const { return report_->text(); }

void PenDiagnostic::feed(const std::vector<PenSample>& samples) {
  report_->setText(penReportText(analyzePen(samples, devicePixelRatioF())));
}

}  // namespace chrome
