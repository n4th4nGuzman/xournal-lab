#include "modules.h"

void installModules(MainWindow& win) {
  installSelection(win);
  installObjects(win);
  installMathObjects(win);
  installTextTools(win);
  installSettings(win);
  installChrome(win);
  installPdf(win);
}
