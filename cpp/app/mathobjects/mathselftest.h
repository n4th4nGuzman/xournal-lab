// Checks of the mathobjects area (the MATH, fill and Riemann blocks of the Electron e2e): synthetic events through the real tool path and the real panels.
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;

namespace mathobj {
/** Returns the number of failed checks. `out` prints one line of the report; screenshots go to `shots`. Works on a scratch page it removes again. */
int mathObjectsSelfTest(MainWindow& win, Editor& ed, const QString& shots, const std::function<void(const QString&)>& out);
}
