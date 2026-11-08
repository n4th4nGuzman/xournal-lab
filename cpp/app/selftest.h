// --selftest and --bench: headless measurements and checks of the page pipeline (see main.cpp for the command line).
#pragma once
#include <QString>
#include <QStringList>

class MainWindow;
class Editor;

struct SelfTestOptions {
  QStringList files;
  QString shotsDir = ".";          // where screenshots go
  QString logPath;                 // the report is also written here (a GUI-subsystem exe has no console when started from some shells)
  bool benchOnly = false;          // --bench: only the pointer-event cost table
};

/** Returns the process exit code: 0 when every check passed. */
int runSelfTest(MainWindow& win, Editor& ed, const SelfTestOptions& o);
