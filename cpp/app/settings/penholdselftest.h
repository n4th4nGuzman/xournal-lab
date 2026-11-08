// Hold-to-use pen buttons and the Windows pen niceties (app/winpen.*), checked with synthetic tablet / mouse events through the real window event path.
// Called from runSettingsChecks, which has already pointed the controller at a temporary prefs file. No real pen, no Windows Ink: see PARIDAD/winpen.md.
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;
class SettingsController;

void runPenHoldChecks(SettingsController& c, MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check);
