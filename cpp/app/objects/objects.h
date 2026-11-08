// What the other files of the objects area need from each other (everything here lives in namespace xlobjects).
#pragma once
#include <QIcon>
#include <QString>
#include <functional>

class Editor;
class MainWindow;
class QWidget;

namespace xlobjects {

void registerObjectTools(Editor& ed);                       // tools.cpp: "plane", "space3", "figure" with their option panels
QIcon objectIcon(const QString& name);                      // icons.cpp: "plane", "space3", "figure" glyphs (same drawing as the Electron icons)
QWidget* makePlanePanel(QWidget* parent, Editor& ed);       // panels.cpp: the popups opened from the carets
QWidget* makeSpace3Panel(QWidget* parent, Editor& ed);
QWidget* makeFigurePanel(QWidget* parent, Editor& ed);

/** What --selftest hands to the area's checks: how to report a result and a line, and the folder for screenshots. */
struct SelfTestIO {
  std::function<void(bool, const QString&)> check;
  std::function<void(const QString&)> out;
  QString shots;
};
void runObjectsSelfTest(MainWindow& win, Editor& ed, const SelfTestIO& io);   // selftest_objects.cpp

}  // namespace xlobjects
