// The mouse wheel must not silently reconfigure a form: a combo box, a spin box, a slider or a dial changes with the wheel only while it has the keyboard focus (given by a
// click or by Tab, never by the wheel itself). Otherwise the wheel goes on to the enclosing widget, normally the scroll area of the dialog or panel, which scrolls.
// One application-wide filter, installed once (idempotent); it must be installed before the settings controller's filter so that the dial's wheel bindings run first.
#pragma once

class QWidget;

namespace WheelGuard {
void install();
/** The combo box / spin box / slider / dial that `w` is or is part of (the line edit of a spin box), nullptr for anything else. */
QWidget* valueWidgetOf(QWidget* w);
}  // namespace WheelGuard
