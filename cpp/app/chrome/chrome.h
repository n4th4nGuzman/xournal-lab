// The "chrome" area: everything around the page. Themes (classic / neon), welcome page, page thumbnails and layers panels, the paper bar of the Journal,
// the find bar, page navigation with history, presentation mode and the pen diagnostic. One Chrome object per window owns them all; main.cpp calls
// chromeStart() once the user's preferences are read. Tests reach the pieces through chromeOf(win).
#pragma once
#include "editor.h"
#include "mainwindow.h"
#include "docfactory.h"
#include "paper.h"
#include <QColor>
#include <QDialog>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QObject>
#include <QPointer>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <map>
#include <vector>

struct Prefs;
class QPushButton;
class MainWindow;
void installChrome(MainWindow&);
/** Called by main.cpp once the preferences are read: applies theme, effects and palette and shows the welcome page when it is due. */
void chromeStart(MainWindow&, const Prefs&, bool fileGivenOnCommandLine);

namespace chrome {

// ---- theme ----------------------------------------------------------------------------------------------------------------------------------
struct Neon {   // the synthwave colours of styles.css
  QColor line{0x3d, 0x2a, 0x7a}, pink{0xff, 0x2a, 0x9d}, cyan{0x05, 0xe1, 0xff}, purple{0xb9, 0x67, 0xff}, yellow{0xff, 0xe4, 0x5e}, green{0x2d, 0xff, 0xb2},
      orange{0xff, 0x8a, 0x3d}, red{0xff, 0x4d, 0x6d}, blue{0x4d, 0x7c, 0xff}, text{0xe9, 0xde, 0xfc};
};
const Neon& neon();
/** "full" or "reduced": prefs "auto" asks the system whether animations/effects are wanted (Windows: client-area animation). */
QString resolveEffects(const QString& pref);

/** A small painted icon of the area ("first", "prev", "plus", "trash", "pages", "layers", "paper", "theme", "find"...). */
QIcon glyph(const QString& name, const QColor& ink = QColor(0xe6, 0xe6, 0xe6));

class WelcomePage;
class ThumbsPanel;
class LayersPanel;
class PaperBar;
class FindBar;
class PenDiagnostic;

/** Undoable layer operations (core/docfactory.h functions inside an Action). Per-page view state (hidden, active layer) travels with the step. */
namespace layerops {
int add(Editor& ed, int page);                         // above the active layer; it becomes active. Returns its index
bool remove(Editor& ed, int page, int layer);          // false (and a notice) when it is the only layer
bool rename(Editor& ed, int page, int layer, const QString& name);
bool move(Editor& ed, int page, int layer, int delta);
}

class Chrome;
/** The "Capa: Capa 1 ▾" button of the first row: pick the active layer, show/hide, add, rename, delete (xppextras.cpp). */
QToolButton* makeLayerSelector(Chrome& c);
/** Dropdowns of the pen / highlighter / eraser buttons and the icons of the view commands (xppextras.cpp; called by installChrome). */
void installXppExtras(Chrome& c);

class Chrome : public QObject {
  Q_OBJECT
 public:
  explicit Chrome(MainWindow& w);
  ~Chrome() override;

  MainWindow& win;
  Editor& ed;

  // ---- theme and effects (applied live) ------------------------------------------------------------------------------------------------
  const QString& theme() const { return theme_; }
  const QString& effects() const { return effectsPref_; }
  bool reducedEffects() const { return fx_ == "reduced"; }
  /** Applies palette, style sheet, tinted icons, desk and page frame. Does not save. */
  void setTheme(const QString& theme, const QString& effects);
  void applyPrefs(const Prefs& p);
  /** The user's choice from the UI: applies and writes the preference (through the settings area, which owns prefs.json; only when it may write). */
  void chooseTheme(const QString& theme);
  void setPersist(bool on) { persist_ = on; }
  bool persist() const { return persist_; }
  void setShowWelcomeOnStart(bool on);
  bool showWelcomeOnStart() const { return welcomeOnStart_; }
  QColor toolbarBackground() const;

  // ---- pieces ---------------------------------------------------------------------------------------------------------------------------
  WelcomePage* welcome() const { return welcome_; }
  ThumbsPanel* thumbs() const { return thumbs_; }
  LayersPanel* layers() const { return layers_; }
  PaperBar* paperBar() const { return paperBar_; }
  FindBar* findBar() const { return find_; }
  QToolBar* navBar() const { return navBar_; }

  // ---- navigation with history ---------------------------------------------------------------------------------------------------------
  /** Go to a page; explicit jumps are recorded so Back/Forward can return (scrolling is not recorded). */
  void goToPage(int page, bool record = true);
  void historyBack();
  void historyForward();
  int historyBackCount() const { return int(back_.size()); }
  int historyForwardCount() const { return int(fwd_.size()); }
  void resetHistory() { back_.clear(); fwd_.clear(); updateNavActions(); }

  // ---- presentation mode ---------------------------------------------------------------------------------------------------------------
  bool presenting() const { return presenting_; }
  void setPresenting(bool on);

  // ---- documents ------------------------------------------------------------------------------------------------------------------------
  /** File > Cerrar: asks about unsaved changes, leaves a blank document behind and shows the welcome page (the Electron app has no document at all then). */
  void closeDocument();
  /** What a drop on the window does: a .xopp opens (asking about unsaved changes), an image is inserted; anything else says why not. Returns true when handled. */
  bool handleDrop(const QStringList& localFiles, const QImage& image = QImage());
  /** Navigation > Ir a la página…: a dialog with the page number (Intro goes, Esc cancels). */
  void gotoDialog();
  /** The system title bar of every top-level window follows the theme (Windows: dark mode and caption colours); false where the platform has no such thing. */
  bool applyTitleBar(QWidget* w);
  /** The four big dialogs (LaTeX, Preferences, pen and tablet, pen diagnostic) are windows of their own: movable, resizable, they may sit on another monitor. While one is
   *  open the page ignores presses (so a formula is not reopened, a tool not used by accident) but scrolls; the toolbars keep working. */
  bool floatingDialogOpen() const;
  void placeFloating(QDialog* d, const QString& key, const QSize& fallback);   // the remembered place (kept on a monitor that still exists) or beside the page, away from the last click
  void rememberFloating(QDialog* d, const QString& key);                        // only a place the user chose (moved or resized) is kept
  /** A new document whose first page has this paper: a built-in style ("graph") or a special paper ("x:log"). */
  void newWithPaper(const QString& value);
  /** The side panels (page thumbnails or layers): shows the dock on that tab, or hides it when it already shows it. */
  void toggleSidebar(const QString& panelId);
  void showSidebar(const QString& panelId);
  bool sidebarShows(const QString& panelId) const;

  /** The icon of an action for the current theme (neon: tinted by function, with a halo unless effects are reduced). */
  QIcon themedIcon(const QString& actionId, const QIcon& original) const;
  /** An icon of the Electron icon set (svgicon.h) drawn for the current theme. */
  QIcon electronIcon(const QString& svgName) const;

 signals:
  void themeChanged(const QString& theme);

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 public:
  // wiring (called by installChrome)
  void buildNavBar();
  void updateNavActions();
  void installMenus();
  void retintActions();
  void tintAction(QAction* a, const QString& id);
  QString styleSheet() const;
  void applyPalette();
  void applyDeskAndPaper();

  // state (public: the pieces and the self test read it)
  QString theme_ = "classic", effectsPref_ = "auto", fx_ = "full";
  bool persist_ = true, welcomeOnStart_ = true, presenting_ = false;
  WelcomePage* welcome_ = nullptr;
  ThumbsPanel* thumbs_ = nullptr;
  LayersPanel* layers_ = nullptr;
  PaperBar* paperBar_ = nullptr;
  FindBar* find_ = nullptr;
  PenDiagnostic* diag_ = nullptr;
  QToolBar* navBar_ = nullptr;
  QLineEdit* pageEdit_ = nullptr;
  QLabel* pageTotal_ = nullptr;
  std::vector<int> back_, fwd_;
  QTimer retint_;
  std::map<QString, QIcon> originals_;
  // presentation: what was visible before
  struct Hidden { bool toolbar = false, nav = false, paper = false, menu = false, dock = false, status = false, find = false, fullscreen = false, colorbar = false; } hidden_;
  QPushButton* presentChip_ = nullptr;
  QPoint lastPress_;   // global position of the last press on the page: dialogs open on the other half of the window
};

/** What --selftest hands to the area: how to report a result and a line, and the folder for screenshots. */
struct TestIO {
  std::function<void(bool, const QString&)> check;
  std::function<void(const QString&)> out;
  QString shots;
};
void runChromeSelfTest(MainWindow& win, Editor& ed, const TestIO& io);   // selftest_chrome.cpp
void runXppSelfTest(MainWindow& win, Editor& ed, const TestIO& io);   // selftest_xpp.cpp: line styles, dropdowns, colour sidebar, layer selector, paired pages (called after the lifecycle checks)
void runChromeLifecycleSelfTest(MainWindow& win, Editor& ed, const TestIO& io);   // selftest_lifecycle.cpp (called at the end of runChromeSelfTest)

}  // namespace chrome

/** The Chrome of this window (null before installChrome ran). */
chrome::Chrome* chromeOf(MainWindow& w);
