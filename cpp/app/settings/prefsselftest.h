// Checks of the preferences dialog organised as the original Xournal++ one: every page, round trips, Cancelar / Aceptar, and that each setting reaches the
// behaviour it names (synthetic events through the real Qt path: no real pen, no touch screen, no second monitor). Called from runSettingsChecks.
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;
class SettingsController;

void runPrefsPageChecks(SettingsController& c, MainWindow& win, Editor& ed, const QString& prefsPath, const std::function<void(bool, const QString&)>& check,
                        const std::function<void(const QString&)>& out, const QString& shots);
