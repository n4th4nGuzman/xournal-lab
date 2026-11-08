// Windows pen and touch behaviour that the app does not want, in one place (Windows only; every call is a no-op elsewhere and on the offscreen platform):
//  * the ripple / "wave" that Windows draws under the pen or finger on contact, the tap / press-and-hold / right-tap circles and the barrel-button visual,
//  * the flicks, press-and-hold and touch-switch gestures the tablet service reads from the pen (WM_TABLET_QUERYSYSTEMGESTURESTATUS),
//  * the handwriting panel ("Panel de escritura a mano") that pops up when the pen clicks a text field: the text fields give up their input-method context, so
//    the system has no text-input target to attach it to. Keys still arrive as ordinary key events (dead keys, ñ, Alt codes), only IME composition is gone.
// Nothing here writes the registry or any system setting; it is all per window / per process.
#pragma once
#include <QtGlobal>

class QWidget;

namespace winpen {

/** Installs the application-wide filters (once; later calls do nothing). Call after the QApplication exists and before the first window is shown. */
void install();

/** The value the tablet service is told for every window: no press-and-hold, no pen-tap / barrel feedback, no touch UI, no flicks, no smooth scrolling. */
quint32 tabletGestureFlags();

/** Switches the Windows feedback visuals off for one top-level window (SetWindowFeedbackSetting, loaded at run time: Windows 8+) and marks it for the tablet service.
 *  Returns how many feedback kinds were switched off; 0 off Windows. Normally called by the filter on Show / WinIdChange, public for the checks. */
int applyToWindow(QWidget* window);

/** True for the widgets a pen click can raise the handwriting panel for: line edits, spin boxes, editable combos and the rich / plain text editors (and their viewports). */
bool isTextInput(const QWidget* w);

/** Takes the input-method context away from a text input and hints that it wants no prediction. Idempotent; true when something changed. */
bool quietTextInput(QWidget* w);

struct Stats { int windows = 0; int textInputs = 0; };
/** How many top-level windows and text inputs the filters have treated so far (checks). */
Stats stats();

}  // namespace winpen
