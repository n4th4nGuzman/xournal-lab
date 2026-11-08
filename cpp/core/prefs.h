// The preferences file (prefs.json) the Electron app already wrote: the data part of app/src/renderer/prefs.ts plus main.ts' sanitizePrefs. The C++ app
// reads and writes the same file. Parsing is tolerant (a bad field takes its default) and additive: keys this version does not know are kept in
// `extra` and written back unchanged. Plane, 3D space, fill, tablet, sizes and toolbox are JSON *text* inside the JSON (as the Electron app stores
// them); the typed accessors at the end read and write them.
#pragma once
#include "brush.h"
#include "sizes.h"
#include "tabletcfg.h"
#include "toolboxmodel.h"
#include "xpppref.h"
#include <QJsonObject>
#include <QString>

struct Prefs {
  bool fingerDraw = false;          // touch input draws (otherwise fingers only scroll: palm rejection)
  bool saveOverOpened = false;      // "Save" on a freshly opened file overwrites it without asking
  int autosaveMinutes = 0;          // write a recovery copy every N minutes while there are unsaved changes (0 = off), at most 120
  QString theme = "classic";        // "classic" | "neon"
  QString palette = "classic";      // id of the pen colour preset (see palettes.h)
  QString effects = "auto";         // "auto" | "full" | "reduced"
  bool welcome = true;              // show the welcome page when the app starts
  QString plane, space3, fill;      // settings of the next plane / 3D space / paint bucket, as JSON text ("" = defaults)
  QString tablet;                   // pen tip, size, curve, buttons and key bindings as JSON text
  QString sizes;                    // size presets of the toolbar as JSON text
  bool textMd = true;               // new texts are markdown
  QString toolbox;                  // tool box layout as JSON text
  bool glowOn = false;              // glow for new pen and shape strokes
  double glowSize = 8;              // 1..40
  double glowIntensity = 0.8;       // 0.05..1
  XppPrefs xpp;                     // every other setting of the original Xournal++ preferences (one nested object, key "xpp")
  QJsonObject extra;                // every key of the file this version does not own, kept as read
  bool operator==(const Prefs&) const = default;
};

/** Whatever was stored, as valid preferences. A non-object gives the defaults. */
Prefs sanitizePrefs(const QJsonValue& raw);
/** The known keys (in the order of the Electron app) plus `extra`. */
QJsonObject prefsToJson(const Prefs& p);

/** Reads the file. A missing or unreadable file gives the defaults and false (first run); `error` says why when it is not just missing. */
bool loadPrefs(const QString& path, Prefs* out, QString* error = nullptr);
/** Writes the file through a temporary one and renames it over the target (never a half file). Creates the folder. */
bool savePrefs(const QString& path, const Prefs& p, QString* error = nullptr);

/** Where the Electron app keeps it: %APPDATA%/xournal-electron/prefs.json. */
QString defaultPrefsPath();

// The nested settings, each sanitised on the way in ("" or bad JSON gives the defaults).
SizePresets prefsSizes(const Prefs& p);
void setPrefsSizes(Prefs& p, const SizePresets& s);
TabletSettings prefsTablet(const Prefs& p);
void setPrefsTablet(Prefs& p, const TabletSettings& t);
ToolboxSettings prefsToolbox(const Prefs& p);
void setPrefsToolbox(Prefs& p, const ToolboxSettings& t);
