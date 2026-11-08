// Hand (drag to pan), laser pointer (a fading trail that is never saved) and the placeholder Select tool the selection area replaces.
#include "documentview.h"
#include "editor.h"
#include "icons.h"
#include "tools.h"
#include <QElapsedTimer>
#include <QTimer>
#include <algorithm>
#include <deque>

namespace {

class HandTool : public Tool {
 public:
  using Tool::Tool;
  QCursor cursor() const override { return dragging_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }
  void pointerDown(const PointerEvent& e) override { dragging_ = true; last_ = e.viewPos; if (view()) view()->applyCursor(); }
  void pointerMove(const PointerEvent& e) override {
    if (!dragging_ || !view()) return;
    const QPointF d = last_ - e.viewPos;
    last_ = e.viewPos;
    view()->scrollByPixels(d.x(), d.y());
  }
  void pointerUp(const PointerEvent&) override { dragging_ = false; }
  void pointerCancel() override { dragging_ = false; }
  void deactivate() override { dragging_ = false; }   // a drag belongs to the tip touching: nothing carries over when the tool is given back
 private:
  bool dragging_ = false;
  QPointF last_;
};

class LaserTool : public Tool {
 public:
  explicit LaserTool(Editor& ed) : Tool(ed) {
    clock_.start();
    timer_.setInterval(16);
    QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { prune(); });
  }
  QCursor cursor() const override { return Qt::BlankCursor; }
  void deactivate() override { clearTrail(); }
  void pointerLeft() override { /* the trail fades by itself */ }
  void pointerDown(const PointerEvent& e) override { push(e); }
  void pointerMove(const PointerEvent& e) override { push(e); }
  void hover(const PointerEvent&) override {}   // only while pressed: hovering shows nothing (as in the Electron app)
  void pointerUp(const PointerEvent& e) override { push(e); }

  void paintOverlay(QPainter& p, const OverlayInfo& info) override {
    if (info.page != page_ || trail_.empty()) return;
    const double z = info.zoom, now = double(clock_.elapsed());
    p.setBrush(Qt::NoBrush);
    for (size_t i = 1; i < trail_.size(); i++) {   // as the Electron app: red, width 2 + 4a screen pixels, fading linearly over lifeMs(), with a soft halo
      const double a = std::max(0.0, 1 - (now - trail_[i].t) / lifeMs());
      if (a <= 0) continue;
      p.setPen(QPen(QColor(255, 40, 40, int(80 * a)), (10 * a + 6) / z, Qt::SolidLine, Qt::RoundCap));   // halo (the canvas shadowBlur)
      p.drawLine(trail_[i - 1].p, trail_[i].p);
      p.setPen(QPen(QColor(255, 40, 40, int(255 * a)), (2 + 4 * a) / z, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(trail_[i - 1].p, trail_[i].p);
    }
    const double a = std::max(0.0, 1 - (now - trail_.back().t) / lifeMs());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 40, 40, int(70 * a)));
    p.drawEllipse(trail_.back().p, 11 / z, 11 / z);
    p.setBrush(QColor(255, 40, 40, int(255 * a)));
    p.drawEllipse(trail_.back().p, 6 / z, 6 / z);
  }

 private:
  struct Sample { QPointF p; double t; };
  double lifeMs() const { return std::max(1, editor().xpp().laserFadeMs); }   // Preferencias > Valores predeterminados; default 1400 = the Electron app's LASER_TTL_MS

  void push(const PointerEvent& e) {
    if (e.page < 0 || e.kind == PointerEvent::Kind::Touch) return;   // a finger scrolls, it does not point
    if (e.page != page_) trail_.clear();
    page_ = e.page;
    trail_.push_back({e.pos, double(clock_.elapsed())});
    if (trail_.size() > 160) trail_.pop_front();
    dirty();
    if (!timer_.isActive()) timer_.start();
  }
  QRectF bounds() const {
    if (trail_.empty() || !view()) return QRectF();
    QRectF r(trail_.front().p, trail_.front().p);
    for (const Sample& s : trail_) r = r.united(QRectF(s.p, s.p));
    const double m = 16 / view()->zoom();
    return r.adjusted(-m, -m, m, m);
  }
  void prune() {
    const double now = double(clock_.elapsed());
    const QRectF before = bounds();
    bool changed = false;
    while (!trail_.empty() && now - trail_.front().t > lifeMs()) { trail_.pop_front(); changed = true; }
    if (trail_.empty()) { if (view() && !before.isNull()) view()->invalidateOverlay(page_, before); timer_.stop(); return; }   // the last of it faded: repaint where it was
    if (changed || !trail_.empty()) dirty();
  }
  void dirty() {
    if (!view() || trail_.empty()) return;
    view()->invalidateOverlay(page_, bounds());
  }
  void clearTrail() {
    if (!trail_.empty()) { dirty(); trail_.clear(); }
    timer_.stop();
  }

  std::deque<Sample> trail_;
  int page_ = -1;
  QElapsedTimer clock_;
  QTimer timer_;
};

class SelectPlaceholder : public Tool {
 public:
  using Tool::Tool;
  QCursor cursor() const override { return Qt::ArrowCursor; }
};

}  // namespace

void registerHandTool(Editor& ed) {
  ToolInfo i;
  i.id = "hand"; i.label = "Mano"; i.tooltip = "Mano: arrastra para mover la página"; i.shortcut = QKeySequence("M"); i.icon = Icons::make("hand"); i.order = 13;
  i.factory = [](Editor& e) { return std::make_unique<HandTool>(e); };
  ed.registerTool(i);
}

void registerLaserTool(Editor& ed) {
  ToolInfo i;
  i.id = "laser"; i.label = "Puntero láser"; i.tooltip = "Puntero láser: una estela que se desvanece (no se guarda)"; i.shortcut = QKeySequence("K"); i.icon = Icons::make("laser"); i.order = 25;
  i.factory = [](Editor& e) { return std::make_unique<LaserTool>(e); };
  ed.registerTool(i);
}

void registerSelectPlaceholder(Editor& ed) {
  ToolInfo i;
  i.id = "select"; i.label = "Seleccionar objeto (rectángulo)"; i.tooltip = "Seleccionar (pendiente)"; i.shortcut = QKeySequence("S"); i.icon = Icons::make("select"); i.order = 10;
  i.factory = [](Editor& e) { return std::make_unique<SelectPlaceholder>(e); };
  ed.registerTool(i);
}

void registerBuiltinTools(Editor& ed) {
  registerInkTools(ed);
  registerEraserTools(ed);
  registerSelectPlaceholder(ed);
  registerHandTool(ed);
  registerLaserTool(ed);
}
