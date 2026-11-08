// Planes, 3D spaces and figures: tools, option panels and the GeneratedHandlers the selection uses. See hub.h for where the state lives.
#include "handlers.h"
#include "hub.h"
#include "mainwindow.h"
#include "modules.h"
#include "objects.h"
#include "prefs.h"
#include <QCoreApplication>

void installObjects(MainWindow& win) {
  Editor& ed = win.editor();
  xlobjects::Hub& hub = xlobjects::Hub::of(ed);
  // The settings of the next plane / 3D space come from (and go back to) the prefs.json the Electron app shares. Automated runs use the defaults and
  // never touch the user's file.
  const QStringList args = QCoreApplication::arguments();
  const bool headless = args.contains("--selftest") || args.contains("--bench") || args.contains("--screenshot");
  if (!headless) hub.setPrefsPath(defaultPrefsPath());
  xlobjects::registerObjectHandlers();
  xlobjects::registerObjectTools(ed);
}
