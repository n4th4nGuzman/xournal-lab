// Parity audit of the "select" area (README: tool box, selection chapter, clipboard, undo/redo, keyboard): checks that go through the REAL Qt key and mouse
// path (qt_handleKeyEvent / qt_handleMouseEvent: shortcut map, focus, grabs, QDrag) instead of sending events to a chosen widget.
// Replaces the open document with its own fixture (call it near the end).
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;

namespace selx {
void runParityChecks(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out,
                     const QString& shotsDir);
}
