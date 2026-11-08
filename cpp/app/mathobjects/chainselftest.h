// Checks of the whole chain selection -> generated handlers for every kind of generated object, with real mouse events on the view.
#pragma once
#include <QString>
#include <functional>

class MainWindow;
class Editor;

namespace mathobj {
/** Returns the number of failed checks. It replaces the open document with its own fixtures (run it last). XL_CHAIN_VERBOSE=1 prints the passes. */
int chainSelfTest(MainWindow& win, Editor& ed, const QString& shots, const std::function<void(const QString&)>& out);
}
