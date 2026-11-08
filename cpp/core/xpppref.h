// The preferences the original Xournal++ has and this app adds on top of the ones prefs.json already carried (see PARIDAD/prefs_xpp.md for the table of every
// original setting). They are stored under ONE additive key, "xpp", an object inside prefs.json: the Electron app ignores keys it does not know (it drops them
// when it writes the file itself, which is the same for every key it does not own; this C++ app keeps unknown keys). Parsing is tolerant: a bad field takes its
// default and ranges are the ones of the original dialog. Pure data, no widgets.
#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>

/** What a mouse button, a stylus button or the eraser end selects while held (the original's "ButtonConfig"). An empty field = "don't change". */
struct ButtonCfg {
  QString tool;       // a tool id of the Editor ("pen", "hand", "eraser"...), "" = unchanged. Stylus buttons keep their tool in the tablet settings (PenAction) instead.
  QString color;      // "#rrggbb" or "" (unchanged)
  int size = 0;       // 0 unchanged, 1..5 = very fine, fine, medium, thick, very thick (the size presets of the tool)
  bool operator==(const ButtonCfg&) const = default;
};

/** What an input device is treated as (the original's "device class"). Auto = by what Qt says it is. */
enum class DeviceClass { Auto = 0, Disabled = 1, Mouse = 2, Pen = 3, Eraser = 4, Touch = 5 };
enum class StabAvg { None = 0, Arithmetic = 1, VelocityGaussian = 2 };
enum class StabPre { None = 0, Deadzone = 1, Inertia = 2 };

struct XppPrefs {
  // ---- Cargar / guardar -------------------------------------------------------------------------------------------------------------
  int autosaveInterval = 3;             // minutes while autosave is on (the on/off state is Prefs::autosaveMinutes > 0)
  QString saveName = "%F-Nota-%H-%M";   // proposed name of a new document
  QString pdfName = "%{name}_anotado";  // proposed name of a PDF export
  bool autoloadPdfXoj = true;           // opening a PDF that has a .xopp next to it opens that .xopp
  bool autoloadMostRecent = false;      // start with the most recent file instead of the welcome page
  // ---- Sistema de entrada ------------------------------------------------------------------------------------------------------------
  bool drawOutside = true;              // a stroke goes on when the pen leaves the window
  bool pressureGuess = false;           // guess a pressure from the speed when the device gives none
  QMap<QString, int> deviceClass;       // device key -> DeviceClass (see devclass); absent = by what the device is
  StabAvg stabAvg = StabAvg::None;
  StabPre stabPre = StabPre::None;
  int stabBuffer = 20;                  // 2..100
  double stabSigma = 0.5;               // 0.05..5
  double stabDeadzone = 1.3;            // 0.1..50 (pixels of the screen)
  double stabDrag = 0.4;                // 0..1
  double stabMass = 5;                  // 1..30
  bool stabCusp = true;
  bool stabFinalize = true;
  // ---- Ratón -------------------------------------------------------------------------------------------------------------------------
  ButtonCfg mouseLeft, mouseMiddle = {"hand", {}, 0}, mouseRight, mouse4, mouse5;   // the middle button drags the page, as it always did
  // ---- Lápiz -------------------------------------------------------------------------------------------------------------------------
  bool pressureSens = true;
  double minPressure = 0.05;            // 0.01..1
  double pressureMult = 1.0;            // 0.5..4
  bool ignoreFirst = false;
  int ignoreFirstN = 1;                 // 1..1000
  ButtonCfg stylus1, stylus2, stylusEraser;   // colour and size only; the tool is TabletSettings::barrel / barrel2 / eraser
  int eraserVisibility = 1;             // 0 never, 1 always, 2 when floating (hovering), 3 when touching
  // ---- Pantalla táctil ---------------------------------------------------------------------------------------------------------------
  bool zoomGestures = true;
  double zoomStartThreshold = 0;        // % beyond the first distance of the two fingers before zooming starts (0..100)
  bool handRecognition = true;          // a finger that touches while the pen is near is ignored
  double handTimeout = 0.7;             // seconds the pen counts as near after its last event (0.5..10)
  bool disableInertia = false;          // no flick scrolling
  // ---- Ver ---------------------------------------------------------------------------------------------------------------------------
  bool menubarAtStart = true;
  bool titlePath = false, titlePageNumber = false;
  QString colorBorder, colorBackground, colorSelection, colorActiveSelection;   // "#rrggbb[aa]" or "" = the theme's own
  bool pageShadow = true;
  int cursorIcon = 0;                   // 0 disc as wide as the stroke (this app's own, the default), 1 none, 2 small dot, 3 big pen, 4 system arrow, 5 cross
  bool highlightCursor = false;
  double highlightRadius = 30;          // 0..30 as in the original dialog
  QString highlightColor = "#80ffff00";   // #aarrggbb as Qt writes it
  double highlightBorderWidth = 0;      // 0..30
  QString highlightBorderColor = "#800000ff";
  bool sidebarRight = false, scrollbarLeft = false;
  int sidebarNumbering = 1;             // 0 none, 1 below preview, 2 circle, 3 square
  bool hideHScroll = false, hideVScroll = false;
  bool fsMenubar = false, fsToolbar = true, fsSidebar = true;                 // (kept for the original's names; applied by the presentation mode)
  bool prMenubar = false, prToolbar = false, prSidebar = false, prFullscreen = true;
  // ---- Escala ------------------------------------------------------------------------------------------------------------------------
  double zoomStep = 20;                 // % per zoom step (0.1..50); the original's default is 10, this app always zoomed by 20 %
  double zoomStepScroll = 19.72;        // % per wheel notch with Ctrl (0.1..50); the original's default is 2, this app always zoomed by exp(0.0015 * 120) = 1.1972 per notch
  bool forceFitOnLoad = false;
  // ---- Área de dibujo ----------------------------------------------------------------------------------------------------------------
  bool addHSpace = false, addVSpace = false, unlimitedScroll = false;
  int hSpaceLeft = 150, hSpaceRight = 150, vSpaceAbove = 150, vSpaceBelow = 150;   // pixels, 0..2000
  int lastPageAppend = 0;               // 0 disabled, 1 when scrolling to the end of the last page, 2 when drawing on the last page
  bool tapEnabled = false;
  int tapTime = 150, tapSuccessive = 500;   // ms, 0..1000
  double tapLength = 1;                 // mm, 0.01..100
  bool tapToolbox = false;              // a dropped tap opens the floating tool box
  // ---- Valores predeterminados -------------------------------------------------------------------------------------------------------
  ButtonCfg defaultTool;                // tool, colour and size of the "Predeterminado" button (the original's BUTTON_DEFAULT); empty = leave as is
  int laserFadeMs = 1400;               // 0..4000 (the Electron app's lifetime; the original's default is 500)
  bool tabSpaces = false;
  int tabSpaceCount = 4;                // 1..8
  bool operator==(const XppPrefs&) const = default;
};

XppPrefs sanitizeXpp(const QJsonValue& raw);
QJsonObject xppToJson(const XppPrefs& x);

/** The proposed file name of a template with strftime-like placeholders (%a %A %b %B %c %d %F %H %I %j %m %M %p %S %U %w %W %x %X %y %Y %Z %%), plus %{name} (the PDF's
 *  name or the document's) and %{file} (the saved file's name) for the PDF export. Characters a file name cannot hold become "_". Names of days and months are Spanish. */
QString expandNameTemplate(const QString& tpl, const QDateTime& now, const QString& name = QString(), const QString& file = QString());
