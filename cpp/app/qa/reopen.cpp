#include "reopen.h"
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include <QApplication>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
static void memory(double* wsMb, double* privMb) {
  PROCESS_MEMORY_COUNTERS_EX c{};
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof c);
  *wsMb = double(c.WorkingSetSize) / 1048576.0;
  *privMb = double(c.PrivateUsage) / 1048576.0;
}
#else
static void memory(double* wsMb, double* privMb) { *wsMb = *privMb = 0; }
#endif

int runReopenCheck(MainWindow& win, const QStringList& files, int rounds) {
  if (files.isEmpty()) { std::fprintf(stderr, "reopen: give at least one file\n"); return 2; }
  win.setPersistence(false);
  win.resize(1280, 900);
  win.show();
  QApplication::processEvents();
  double ws, priv;
  memory(&ws, &priv);
  std::fprintf(stderr, "reopen: start            ws %7.1f MB  private %7.1f MB\n", ws, priv);
  for (int r = 1; r <= rounds; r++) {
    for (const QString& f : files) {
      if (!win.openFile(f)) return 1;
      for (int i = 0; i < 4; i++) QApplication::processEvents();
      win.view()->renderVisibleNow();
      for (int i = 0; i < 4; i++) QApplication::processEvents();
    }
    win.blankNow();   // what "New" does: the last document is released
    for (int i = 0; i < 4; i++) QApplication::processEvents();
    memory(&ws, &priv);
    std::fprintf(stderr, "reopen: round %-3d        ws %7.1f MB  private %7.1f MB\n", r, ws, priv);
  }
  return 0;
}
