// Between the device and the tools: the pointer rules of the preferences "Lápiz" (pressure on/off, minimum, multiplier, ignore the first events, inference of a
// pressure from the speed), "Sistema de entrada" (input stabilization: averaging, dead zone, inertia, cusp detection, finalizing) and "Área de dibujo" (the tap
// filter that drops strokes too short and too quick to be drawing). Settings-area code: the Editor only holds the hook (Editor::setInputFilter) and sends every
// down / move / up of a gesture through it. Observable behaviour of the original's options, our own algorithms (no code of the original is used).
#pragma once
#include "tool.h"
#include "xpppref.h"
#include <functional>
#include <vector>

enum class InputPhase { Down, Move, Up };

class InputFilter {
 public:
  using Emit = std::function<void(InputPhase, const PointerEvent&)>;

  /** `dpi` converts the tap filter's millimetres to screen pixels. */
  void configure(const XppPrefs& x, double dpi = 96);
  /** `ink`: the gesture draws with the pen or the highlighter (the stabilizer and the tap filter only touch those). */
  void down(const PointerEvent& e, bool ink, const Emit& out);
  void move(const PointerEvent& e, const Emit& out);
  void up(const PointerEvent& e, const Emit& out);
  void cancel();

  /** Called with the first event of a stroke the tap filter dropped (the "Acción al toque" of the preferences). */
  std::function<void(const PointerEvent&)> onTap;
  int droppedTaps() const { return taps_; }
  bool busy() const { return active_; }

 private:
  PointerEvent pressure(const PointerEvent& e);
  void stage2(InputPhase ph, const PointerEvent& e, const Emit& out);   // ignore the first events
  void stage3(InputPhase ph, const PointerEvent& e, const Emit& out);   // stabilizer
  static PointerEvent atView(const PointerEvent& e, const QPointF& v);
  QPointF smooth(const QPointF& raw, double speed);

  XppPrefs x_;
  double dpi_ = 96;
  bool active_ = false, ink_ = false;
  // pressure
  QPointF lastView_;
  double lastT_ = 0, guess_ = 0.5;
  bool haveLast_ = false;
  // tap filter
  bool tapHeld_ = false;
  std::vector<std::pair<InputPhase, PointerEvent>> tapBuf_;
  double tapStartT_ = 0, tapLen_ = 0, lastTapEnd_ = -1e12;
  int taps_ = 0;
  // ignore the first events
  int ignored_ = 0;
  bool downSent_ = false;
  // stabilizer
  std::vector<QPointF> hist_;
  QPointF acc_, vel_, lastOut_, rawLast_;
  bool stabStarted_ = false;
  PointerEvent lastRawEv_;
};
