// Real-pen input fixes, checked with synthetic events through the real window event path (see PARIDAD/winpen.md): a barrel button a driver also sends as a mouse click, the
// tool box and the pen (tablet events reach the QWindow first), the raw input trace of "Lápiz y tableta" and the wheel over form controls.
// Called from runSettingsChecks, which has already pointed the controller at a temporary prefs file. No real pen, no Windows Ink.
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;
class SettingsController;

void runInputFixChecks(SettingsController& c, MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check);
