// The main window: menus (Spanish, same entries and shortcuts as the Electron app), toolbar built from the tool registry, status bar, the page area and a
// side dock for panels. Everything it does is a command on the Editor (QAction by id), so tablet keys and the feature areas reach the same things.
#pragma once
#include "documentview.h"
#include "editor.h"
#include "sizes.h"
#include "titlebar.h"
#include <QActionGroup>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTabWidget>
#include <QPointer>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <functional>

/** The message area at the end of the first toolbar row: its size does not depend on the text (a message must not change how many buttons fit), long text is elided. */
class InfoLabel : public QLabel {
 public:
  explicit InfoLabel(QWidget* parent = nullptr) : QLabel(parent) { setObjectName("info"); setAlignment(Qt::AlignRight | Qt::AlignVCenter); setMinimumWidth(0); setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred); }
  /** A transient message (Editor::notice, save results) wins over the document summary ("2 págs · 1.246 elementos · pág. 1") shown the rest of the time. */
  void setMessage(const QString& t) { msg_ = t; refresh(); }
  void setSummary(const QString& t) { sum_ = t; refresh(); }
  QString message() const { return msg_; }
  QString summary() const { return sum_; }
  QSize sizeHint() const override { return QSize(width_, 24); }
  QSize minimumSizeHint() const override { return QSize(width_, 24); }
  void setWidth(int w) { width_ = w; updateGeometry(); elide(); }
 protected:
  void resizeEvent(QResizeEvent* e) override { QLabel::resizeEvent(e); elide(); }
 private:
  void refresh() { full_ = msg_.isEmpty() ? sum_ : msg_; setToolTip(full_); elide(); }
  void elide() { setText(fontMetrics().elidedText(full_, Qt::ElideLeft, std::max(0, width() - 4))); }
  QString full_, msg_, sum_;
  int width_ = 200;
};

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(Editor& ed, QWidget* parent = nullptr);
  ~MainWindow() override;

  Editor& editor() { return ed_; }
  DocumentView* view() { return view_; }

  /** The menu with this key: "file", "edit", "view", "nav", "journal", "tools", "help" (more can be created with addMenu). Add items with menu->addAction(editor.action(id)). */
  QMenu* menu(const QString& key) const { return menus_.value(key); }
  QMenu* addMenu(const QString& key, const QString& title, const QString& before = QString());
  /** A tab in the side dock (page thumbnails, layers...). The dock is shown from the first panel on and toggled from Ver > Barras laterales. */
  void addSidePanel(const QString& id, const QString& title, QWidget* w);
  /** The window's own title bar (menu bar, title, minimise / maximise / close); the window has no system frame. */
  TitleBar* titleBar() const { return titleBar_; }
  /** The menu bar lives inside the title bar (it hides QMainWindow::menuBar(), which would make a second one). */
  QMenuBar* menuBar() const { return titleBar_->menuBar(); }
  /** First toolbar row: file, history, clipboard, side panels, page navigation, page operations, zoom, theme, paper. Chrome fills it (see Chrome::buildNavBar). */
  QToolBar* topBar() const { return topBar_; }
  /** The zoom percentage and the message area shown at the end of the first row (the Electron toolbar has them in place of a status bar). */
  QLabel* zoomLabel() const { return zoomLabel_; }
  QWidget* infoWidget() const { return infoLabel_; }
  /** The text of the info area right now (the last message, until it times out). */
  QString infoText() const;
  /** The title bar text: "Xournal Lab" with no document, else "<name> — Xournal Lab" (a dot first while there are unsaved changes), as the Electron title bar. */
  QString titleBarText() const;
  void refreshTitle() { updateTitle(); }
  /** Tools row (second row of the window). */
  QToolBar* toolBar() const { return toolbar_; }
  /** Second row of the main toolbar: colours, palette, size presets, zoom, new page. It shares the line of `toolBar()` while both fit and goes below it when the window is narrower. */
  QToolBar* styleBar() const { return styleBar_; }
  /** An invisible marker action in the style row: widgets that belong at the start of that row (the glow button) are inserted before it. */
  QAction* styleLeadMarker() const { return styleLead_; }
  /** The button of an action of either toolbar row (null when it has none). */
  QWidget* toolbarWidget(QAction* a) const {
    for (QToolBar* tb : {toolbar_, styleBar_, topBar_}) if (QWidget* w = tb->widgetForAction(a)) return w;
    return nullptr;
  }
  /** Actions that stay in the toolbar after the tool buttons across rebuilds (a null entry is a separator). Add before the first rebuild (the registration of a tool schedules one). */
  QList<QAction*> toolbarExtras;
  /** A short message at the end of the first toolbar row (Electron's #info); there is no status bar. */
  void showMessage(const QString& msg, int ms = 4000);

  // files
  bool openFile(const QString& path);
  bool save(bool saveAs);
  bool maybeSave();                  // false = the user cancelled
  void newDocument();
  /** Replaces the document by a blank one without asking (the caller already handled the unsaved changes: Close does, then shows the welcome page). */
  void blankNow();
  /** Writes the document to `path` and makes it the document's path (what Save as does after the dialog). False (message) when it could not be written. */
  bool saveTo(const QString& path);
  /** Writes the recovery copy now when there are changes since the last one (the timer calls it). Never touches the user's file. */
  bool autosaveNow();
  QString autosaveDir() const;
  void setAutosaveDir(const QString& dir) { autosaveDir_ = dir; }   // tests: never %APPDATA%
  QString fileName() const { return fileName_; }
  /** The name shown in the title for a document that has no .xopp yet (the PDF being annotated). */
  void setDisplayName(const QString& name) { fileName_ = name; updateTitle(); }
  QString suggestedSaveName(bool copy) const;
  /** Where this window's own settings (recent files, remembered places of dialogs) live: the registry by default; a test points it at an .ini file of its own. */
  QSettings settings() const;
  void setSettingsFile(const QString& iniPath) { settingsFile_ = iniPath; }
  void setPersistence(bool on) { persist_ = on; }   // off for --selftest/--bench: no recent list, no settings
  bool persistence() const { return persist_; }
  /** The colour buttons of the toolbar (settings area: the palette in use) and the size presets of the pen/highlighter/eraser buttons. */
  void setPaletteColors(const QStringList& colors);
  void setSizePresets(const SizePresets& s);
  const SizePresets& sizePresets() const { return sizePresets_; }

  // ---- the colour sidebar (Xournal++'s thin colour column) ----------------------------------------------------------------------------------
  /** A vertical bar with the palette colours, the custom colour box and the view buttons (two pages, presentation, 100 %, fit, zoom out/in). While it is on the
   *  colour dots of the style row are not shown (one set of colours, never two). Hooks for the Preferences dialog: call these; the choice is remembered in
   *  `settings()` (only while `persistence()`), under "ui/colorSidebar" and "ui/colorSidebarRight". */
  void setColorSidebar(bool on);
  bool colorSidebar() const { return colorBarOn_; }
  /** Left-handed users: the bar goes to the right edge. */
  void setColorSidebarRight(bool right);
  bool colorSidebarRight() const { return colorBarRight_; }
  QToolBar* colorBar() const { return colorBar_; }
  /** Reads the two choices above from `settings()` (called once at start by chromeStart; the default is on, left). */
  void loadUiState();
  /** A dropdown for a tool button of the tools row: the caret of that button opens the menu, filled each time it opens. Replaces the caret command of the tool. */
  void setToolMenu(const QString& toolId, std::function<void(QMenu*)> fill);
  bool hasToolMenu(const QString& toolId) const { return toolMenus_.contains(toolId); }
  /** "Save" on a file that was opened overwrites it without asking (preference). */
  void setSaveOverOpened(bool on) { saveOverOpened_ = on; }
  void setAutosaveMinutes(int minutes);
  QStringList recentFiles() const;

 signals:
  void toolbarRebuilt();   // the toolbar's buttons were recreated (settings area re-wires drag and long press)
  void colorSidebarChanged();   // shown, hidden or moved to the other side

 protected:
  void closeEvent(QCloseEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  bool event(QEvent*) override;
  void showEvent(QShowEvent*) override;
  bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;

 private:
  void buildActions();
  void buildMenus();
  void scheduleToolbarRebuild();
  void rebuildToolbar();
  void arrangeToolbars();
  void applyToolbarTier(int tier);
  void measureToolbarTiers();
  void rebuildToolsMenu();
  void rebuildSizeButtons();
  void rebuildRecentMenu();
  void updateTitle();
  void updateStatus();
  void updateInfoSummary();
  void addRecent(const QString& path);
  QAction* act(const QString& id, const QString& text, const QKeySequence& sc, std::function<void()> fn);
  QAction* stub(const QString& id, const QString& text, const QKeySequence& sc = QKeySequence());

  Editor& ed_;
  DocumentView* view_;
  TitleBar* titleBar_ = nullptr;
  QToolBar* topBar_ = nullptr;
  QToolBar* toolbar_ = nullptr;
  QToolBar* styleBar_ = nullptr;
  QAction* styleLead_ = nullptr;
  bool frameFixed_ = false;
  bool styleOnOwnRow_ = false;
  int toolbarTier_ = 0;                 // 0 full size, 1 compact, 2 very compact (see arrangeToolbars)
  int tierWidth_[3][3] = {};            // hint width of the tools row, the style row and the first row at each tier; valid when tierWidth_[0][0] > 0
  bool measuringTiers_ = false;
  int tierSignature_ = -1;              // actions in both rows when the tiers were measured: another count means the buttons changed
  QDockWidget* dock_ = nullptr;
  QTabWidget* dockTabs_ = nullptr;
  QLabel* zoomLabel_ = nullptr;
  InfoLabel* infoLabel_ = nullptr;
  QPointer<QWidget> sizeCombo_;
  QMap<QString, QMenu*> menus_;
  QMenu* recentMenu_ = nullptr;
  QMenu* toolsMenu_ = nullptr;
  QList<QAction*> toolsMenuDynamic_;
  QActionGroup *colorGroup_ = nullptr, *sizeGroup_ = nullptr;
  QList<QAction*> colorActions_, sizeActions_;
  QTimer rebuildTimer_, autosaveTimer_, messageTimer_, infoTimer_;
  QString fileName_;                  // shown in the title; empty = "Sin título"
  QString lastDir_;
  int autosavedAt_ = -1;
  QString autosaveDir_;
  QString settingsFile_;
  bool persist_ = true;
  bool applyAll_ = false;
  QAction* sizeSection_ = nullptr;   // set once the style row was built; sizeMarker_ is where the size buttons go
  QAction* sizeMarker_ = nullptr;
  QStringList paletteColors_;
  QToolBar* colorBar_ = nullptr;
  bool colorBarOn_ = false, colorBarRight_ = false;
  QMap<QString, std::function<void(QMenu*)>> toolMenus_;
  QToolButton* makeCustomColorButton(QWidget* parent);
  void rebuildColorBar();
  void placeColorBar();
  SizePresets sizePresets_ = defaultSizes();
  bool saveOverOpened_ = false;
};
