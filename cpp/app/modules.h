// The feature areas of the application. Each install function is defined in app/<area>/install.cpp, owned by that area; it registers its
// tools (Editor::registerTool), commands (Editor::addCommand), generated-object handlers (GeneratedRegistry), side panels and menu entries.
// Order matters only where noted: the selection goes first, because the object tools register handlers it will ask.
#pragma once
class MainWindow;
struct Prefs;

void installSelection(MainWindow&);      // select/lasso/vspace tools, handles, clipboard, delete, find, z-order      (app/selection/)
void installObjects(MainWindow&);        // plane, 3D space, figures: tools, option panels, handlers                  (app/objects/)
void installMathObjects(MainWindow&);    // number line, set diagrams, fill/Riemann: tools, panels, handlers          (app/mathobjects/)
void installTextTools(MainWindow&);      // text, LaTeX, images, shapes, ruler                                        (app/texttools/)
void installSettings(MainWindow&);       // toolbox, tablet dialog, preferences, size presets, palettes                (app/settings/)
void installChrome(MainWindow&);         // themes/glow, welcome page, page and layer side panels, journal/paper, floating dialogs (app/chrome/)
void installPdf(MainWindow&);            // PDF backgrounds and annotation, export PDF/PNG, print, formulas saved as PDF (app/pdf/)

/** Called by main.cpp (real application only) after the preferences are read: theme, effects, palette and the welcome page. Defined in app/chrome/install.cpp. */
void chromeStart(MainWindow&, const Prefs&, bool fileGivenOnCommandLine);

void installModules(MainWindow& win);    // calls all of the above (app/modules.cpp)
