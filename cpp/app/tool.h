// What a tool is: the interface the canvas drives and the description the toolbar is built from. See API.md for how to add one.
#pragma once
#include <QCursor>
#include <QIcon>
#include <QKeySequence>
#include <QPainter>
#include <QPointF>
#include <QString>
#include <functional>
#include <memory>

class Editor;
class DocumentView;
class QKeyEvent;
class QWidget;

/** One sample of the pointer, already converted to page coordinates. One of these exists per Qt tablet/mouse event: none is dropped or merged. */
struct PointerEvent {
  enum class Kind { Mouse, Pen, Eraser, Touch };
  int page = -1;               // the page the gesture started on (during a gesture it never changes, even when the pointer leaves it); -1 = no page under a hover
  QPointF pos;                 // page units (points), relative to that page's top-left corner; may lie outside the page
  QPointF viewPos;             // viewport pixels (for tools that work in screen space, like the hand)
  QPointF globalPos;           // screen coordinates (where to open a popup)
  double pressure = 0.5;       // 0..1; a mouse reports 0.5 and hasPressure = false
  bool hasPressure = false;
  double tiltX = 0, tiltY = 0; // degrees, 0 when the device does not report them
  double rotation = 0;         // degrees, barrel twist
  Kind kind = Kind::Mouse;
  bool pressedBarrel = false;  // pen barrel button 1 held (Qt reports it as the right button)
  bool pressedBarrel2 = false; // pen barrel button 2 (middle button)
  Qt::MouseButtons buttons;    // everything held right now, tip contact included
  Qt::KeyboardModifiers mods;
  quint64 timestampMs = 0;     // the device's own clock when it gives one (milliseconds; only differences mean anything)
  double zoom = 1.0;           // CSS pixels per page unit: convert a size in screen pixels with px / zoom
  bool shift() const { return mods & Qt::ShiftModifier; }
  bool ctrl() const { return mods & Qt::ControlModifier; }
  bool alt() const { return mods & Qt::AltModifier; }
};

/** Passed to paintOverlay: the painter is already in the coordinates of page `page` (units = points, origin = page's top-left corner). */
struct OverlayInfo {
  int page = 0;
  double zoom = 1.0;           // painter scale; divide pixel sizes by it (pen.setWidthF(1.5 / zoom))
  QRectF visible;              // the part of this page on screen, page units: do not draw outside it
};

class Tool {
 public:
  explicit Tool(Editor& ed) : ed_(ed) {}
  virtual ~Tool() = default;

  /** The tool became the current one / stopped being it. Also called around a single gesture when a pen button borrows the tool for one stroke. */
  virtual void activate() {}
  virtual void deactivate() {}

  /** The gesture: down, any number of moves while held, then up (or cancel). Moves without a button come through hover(). */
  virtual void pointerDown(const PointerEvent&) {}
  virtual void pointerMove(const PointerEvent&) {}
  virtual void pointerUp(const PointerEvent&) {}
  virtual void pointerCancel() {}
  virtual void hover(const PointerEvent&) {}
  /** The pointer left the canvas (cursor trails and rings should go away). */
  virtual void pointerLeft() {}

  /** Return true when the key was used. Shortcuts that belong to menus are QActions and never get here. */
  virtual bool keyPress(QKeyEvent*) { return false; }
  virtual bool keyRelease(QKeyEvent*) { return false; }

  /** Anchors, previews, rings: drawn above the live layer, once per visible page. Must cost O(what it draws). Ask for repaints with DocumentView::invalidateOverlay. */
  virtual void paintOverlay(QPainter&, const OverlayInfo&) {}

  virtual QCursor cursor() const { return Qt::CrossCursor; }

 protected:
  Editor& editor() const { return ed_; }
  DocumentView* view() const;   // may be null in tests without a window

 private:
  Editor& ed_;
};

using ToolFactory = std::function<std::unique_ptr<Tool>(Editor&)>;
using OptionsPanelFactory = std::function<QWidget*(QWidget* parent, Editor&)>;

/** Everything the toolbar, the Herramientas menu and the "tool before" logic need to know about a tool. */
struct ToolInfo {
  QString id;                  // "pen", "plane"...: also the command id "tool.<id>"
  QString label;               // Spanish menu text
  QString tooltip;             // Spanish; the shortcut is appended automatically
  QKeySequence shortcut;       // single letters are fine: they do not fire while a text field has focus
  QIcon icon;                  // see Icons::make(name) for the built-in glyphs
  ToolFactory factory;         // instantiated lazily the first time the tool is selected
  OptionsPanelFactory options; // optional: a caret on the toolbar button opens this widget in a popup (created once, on first open)
  QString optionsCommand;      // optional, instead of `options`: the caret runs this command at once (the pen: "pen.settings" opens Lápiz y tableta, as in the Electron app)
  bool inserter = false;       // makes an object from a click or drag: after it, the editor selects the object and remembers the tool (returnTo)
  bool inToolbar = true;
  int order = 1000;            // position in toolbar and menu (ascending); built-ins use 0..99
};
