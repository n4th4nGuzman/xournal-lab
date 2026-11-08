#include "inputfilter.h"
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace {
double len(const QPointF& p) { return std::hypot(p.x(), p.y()); }
double timeOf(const PointerEvent& e) { return e.timestampMs ? double(e.timestampMs) : double(QDateTime::currentMSecsSinceEpoch()); }
}  // namespace

void InputFilter::configure(const XppPrefs& x, double dpi) {
  x_ = x;
  dpi_ = dpi > 20 ? dpi : 96;
}

PointerEvent InputFilter::atView(const PointerEvent& e, const QPointF& v) {
  PointerEvent o = e;
  const QPointF d = v - e.viewPos;
  o.viewPos = v;
  o.globalPos = e.globalPos + d;
  o.pos = e.pos + d / (e.zoom > 0 ? e.zoom : 1.0);
  return o;
}

// Pressure: the device's value is switched off, raised to a minimum and scaled (capped at 1: the pressure curve downstream is defined on 0..1); a device with no
// pressure (mouse, finger) can be given one guessed from how fast the pointer moves: slow = firm, fast = light, smoothed so the width does not jump.
PointerEvent InputFilter::pressure(const PointerEvent& e) {
  PointerEvent ev = e;
  const double t = timeOf(e);
  const bool pen = e.kind == PointerEvent::Kind::Pen || e.kind == PointerEvent::Kind::Eraser;
  if (pen && e.hasPressure) {
    if (!x_.pressureSens) { ev.hasPressure = false; ev.pressure = 0.5; }
    else ev.pressure = std::clamp(std::max(x_.minPressure, e.pressure * x_.pressureMult), 0.0, 1.0);
  } else if (x_.pressureGuess && x_.pressureSens) {
    double target = 0.5;
    if (haveLast_) {
      const double dt = std::max(1.0, t - lastT_);
      const double v = len(e.viewPos - lastView_) / dt;   // pixels per millisecond
      target = std::clamp(1.1 - 0.6 * std::sqrt(v), 0.15, 1.0);
      guess_ = 0.6 * guess_ + 0.4 * target;
    } else {
      guess_ = 0.5;
    }
    ev.hasPressure = true;
    ev.pressure = std::clamp(std::max(x_.minPressure, guess_ * x_.pressureMult), 0.0, 1.0);
  }
  lastView_ = e.viewPos;
  lastT_ = t;
  haveLast_ = true;
  return ev;
}

void InputFilter::down(const PointerEvent& e, bool ink, const Emit& out) {
  if (active_) cancel();
  active_ = true;
  ink_ = ink;
  haveLast_ = false;
  guess_ = 0.5;
  tapHeld_ = false;
  tapBuf_.clear();
  ignored_ = 0;
  downSent_ = false;
  stabStarted_ = false;
  const PointerEvent ev = pressure(e);
  if (ink_ && x_.tapEnabled) {
    tapHeld_ = true;
    tapBuf_.push_back({InputPhase::Down, ev});
    tapStartT_ = timeOf(e);
    tapLen_ = 0;
    return;
  }
  stage2(InputPhase::Down, ev, out);
}

void InputFilter::move(const PointerEvent& e, const Emit& out) {
  if (!active_) { out(InputPhase::Move, e); return; }
  const PointerEvent ev = pressure(e);
  if (tapHeld_) {
    tapLen_ += len(ev.viewPos - tapBuf_.back().second.viewPos) * 25.4 / dpi_;   // millimetres
    tapBuf_.push_back({InputPhase::Move, ev});
    if (tapLen_ > x_.tapLength || timeOf(e) - tapStartT_ > x_.tapTime) {   // long or slow enough to be drawing: nothing was lost, the held events go on
      tapHeld_ = false;
      for (auto& [ph, held] : tapBuf_) stage2(ph, held, out);
      tapBuf_.clear();
    }
    return;
  }
  stage2(InputPhase::Move, ev, out);
}

void InputFilter::up(const PointerEvent& e, const Emit& out) {
  if (!active_) { out(InputPhase::Up, e); return; }
  const PointerEvent ev = pressure(e);
  active_ = false;
  if (tapHeld_) {
    tapHeld_ = false;
    const double now = timeOf(e);
    const bool successive = now - lastTapEnd_ < x_.tapSuccessive;   // a second quick tap is a dot, not a tap action
    lastTapEnd_ = now;
    if (successive) {
      for (auto& [ph, held] : tapBuf_) stage2(ph, held, out);
      stage2(InputPhase::Up, ev, out);
    } else {
      taps_++;
      if (onTap) onTap(tapBuf_.front().second);
    }
    tapBuf_.clear();
    return;
  }
  stage2(InputPhase::Up, ev, out);
}

void InputFilter::cancel() {
  active_ = false;
  tapHeld_ = false;
  tapBuf_.clear();
  stabStarted_ = false;
}

// The first N events of a stylus stroke are dropped (the press counts as the first): the stroke starts at the first event after them. A stroke that ends
// before that leaves nothing.
void InputFilter::stage2(InputPhase ph, const PointerEvent& e, const Emit& out) {
  if (!(x_.ignoreFirst && ink_ && e.kind == PointerEvent::Kind::Pen)) { stage3(ph, e, out); return; }
  if (ph == InputPhase::Down) { ignored_ = 1; downSent_ = false; return; }
  if (!downSent_) {
    if (ph == InputPhase::Up) return;
    if (++ignored_ <= x_.ignoreFirstN) return;
    downSent_ = true;
    stage3(InputPhase::Down, e, out);
    return;
  }
  stage3(ph, e, out);
}

// Input stabilization. The preprocessor decides where the "pen" is (dead zone: it stays until the pointer leaves a circle, then trails it; inertia: a mass on a
// spring with drag follows the pointer), the averaging method smooths the points it produces. Sizes are in screen pixels.
QPointF InputFilter::smooth(const QPointF& p, double speed) {
  if (x_.stabCusp && hist_.size() >= 2) {   // a sharp turn restarts the average, so corners stay corners
    const QPointF a = hist_.back() - hist_[hist_.size() - 2], b = p - hist_.back();
    const double la = len(a), lb = len(b);
    if (la > 0.5 && lb > 0.5 && (a.x() * b.x() + a.y() * b.y()) / (la * lb) < 0) hist_.clear();
  }
  hist_.push_back(p);
  const size_t cap = size_t(std::max(2, x_.stabBuffer));
  if (hist_.size() > cap) hist_.erase(hist_.begin(), hist_.begin() + long(hist_.size() - cap));
  if (x_.stabAvg == StabAvg::None) return p;
  QPointF sum;
  double wsum = 0;
  const double s = std::max(0.3, x_.stabSigma * double(cap) / (1.0 + speed / 2.0));   // slower = smoother
  for (size_t i = 0; i < hist_.size(); i++) {
    const double j = double(hist_.size() - 1 - i);   // 0 = the newest
    const double w = x_.stabAvg == StabAvg::Arithmetic ? 1.0 : std::exp(-(j * j) / (2 * s * s));
    sum += hist_[i] * w;
    wsum += w;
  }
  return wsum > 0 ? sum / wsum : p;
}

void InputFilter::stage3(InputPhase ph, const PointerEvent& e, const Emit& out) {
  const bool stab = ink_ && (x_.stabAvg != StabAvg::None || x_.stabPre != StabPre::None);
  if (!stab) { out(ph, e); return; }
  if (ph == InputPhase::Down) {
    hist_.assign(1, e.viewPos);
    acc_ = lastOut_ = rawLast_ = e.viewPos;
    vel_ = QPointF();
    stabStarted_ = true;
    out(ph, e);
    return;
  }
  if (!stabStarted_) { out(ph, e); return; }
  if (ph == InputPhase::Move) {
    const QPointF raw = e.viewPos;
    const double speed = len(raw - rawLast_);
    QPointF p = raw;
    if (x_.stabPre == StabPre::Deadzone) {
      const double d = len(raw - acc_);
      if (d <= x_.stabDeadzone) { rawLast_ = raw; return; }
      p = acc_ + (raw - acc_) * (1.0 - x_.stabDeadzone / d);
      acc_ = p;
    } else if (x_.stabPre == StabPre::Inertia) {
      vel_ = (vel_ + (raw - acc_) / std::max(1.0, x_.stabMass)) * (1.0 - std::clamp(x_.stabDrag, 0.0, 0.95));
      acc_ += vel_;
      p = acc_;
    }
    p = smooth(p, speed);
    rawLast_ = raw;
    lastRawEv_ = e;
    lastOut_ = p;
    out(ph, atView(e, p));
    return;
  }
  // Up
  stabStarted_ = false;
  if (x_.stabFinalize) {   // the stroke ends where the pen really was, not where the smoothing had got to
    if (len(e.viewPos - lastOut_) > 0.01) out(InputPhase::Move, e);
    out(InputPhase::Up, e);
  } else {
    out(InputPhase::Up, atView(e, lastOut_));
  }
}
