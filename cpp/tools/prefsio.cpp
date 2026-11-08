// xl_prefsio IN OUT : loads a prefs.json with the C++ loader and saves it with the C++ writer (both paths given explicitly; never the user's file by default).
// Used by tools/interchange/prefs_check.mjs, which compares the Electron app's sanitizePrefs on the input and on the output.
#include "prefs.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc < 3) { std::fprintf(stderr, "usage: xl_prefsio IN OUT\n"); return 2; }
  Prefs p;
  QString err;
  loadPrefs(QString::fromLocal8Bit(argv[1]), &p, &err);
  if (!err.isEmpty()) std::printf("load note: %s\n", qPrintable(err));
  if (!savePrefs(QString::fromLocal8Bit(argv[2]), p, &err)) { std::fprintf(stderr, "save failed: %s\n", qPrintable(err)); return 1; }
  return 0;
}
