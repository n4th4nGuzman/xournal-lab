// Checks of the settings area for --selftest: tool box, tablet dialog, key bindings, preferences, size presets and palettes, driven through the real Qt
// event path (synthetic events: no real pen, no second monitor). Screenshots go to `shots`. Never touches the user's prefs.json (a temporary file is used).
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;

void runSettingsChecks(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out, const QString& shots);
