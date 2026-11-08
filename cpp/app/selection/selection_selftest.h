// Checks of the selection area for --selftest: the Electron e2e assertions for select, lasso, move, resize, rotate, mirror, copy/paste, z-order and
// vertical space, driven through real Qt mouse and key events, plus the generated-object anchors (with a fake handler) and a 20000-stroke measurement.
// Replaces the open document with its own (call it last).
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;

namespace selx {
void runSelectionChecks(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out,
                        const QString& shotsDir);
}
