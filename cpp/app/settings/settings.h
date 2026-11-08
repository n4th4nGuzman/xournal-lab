// The settings area: owner of the user's preferences (the prefs.json shared with the Electron app), the tool box, the pen/tablet dialog, the preferences
// dialog, the size presets and the colour palettes. Port of toolbox.ts (UI), tabletui.ts, prefs.ts (PrefsDialog), sizes.ts, palettes.ts and the glue of app.ts.
//
// SettingsController is the one place that interprets keys, wheel and extra mouse buttons bound to actions (core/tabletcfg.h), applies a setting live to
// the Editor / MainWindow and writes the file. Other areas reach it with SettingsController::of(win) (a child of the window) and may change a preference
// of their own with mutatePrefs(), which re-reads the file first so nothing the Electron app wrote meanwhile is lost.
#pragma once
#include "editor.h"
#include "mainwindow.h"
#include "prefs.h"
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <functional>

class ToolboxWidget;
class TabletDialog;
class PrefsDialog;
class XppApplier;
class QToolButton;

class SettingsController : public QObject {
  Q_OBJECT
 public:
  explicit SettingsController(MainWindow& win);
  ~SettingsController() override;
  static SettingsController* of(MainWindow& win);

  MainWindow& window() const { return win_; }
  Editor& editor() const { return ed_; }
  const Prefs& prefs() const { return prefs_; }

  // ---- storage ---------------------------------------------------------------------------------------------------------------------------
  /** Where the file is: %APPDATA%/xournal-electron/prefs.json unless a test chose another. Writing is allowed when the window persists (a real run) or a path was chosen on purpose. */
  void setStorage(const QString& path);
  const QString& storagePath() const { return path_; }
  bool writable() const;
  /** Read the file and apply everything (startup). A missing file is the first run: defaults. */
  void loadNow();
  bool saveNow();
  void scheduleSave();
  /** Change one of the preferences another area owns: the file is read again first, `fn` edits it, it is written back. Does nothing when not writable. */
  void mutatePrefs(const std::function<void(Prefs&)>& fn);

  // ---- applying (each updates the stored copy, applies live and schedules the save) -------------------------------------------------------------
  void applyPrefs(const Prefs& p, bool persist = true);          // the preferences dialog
  void applyTablet(const TabletSettings& t, bool persist = true);
  void applyToolbox(const ToolboxSettings& s, bool persist = true);
  void applySizes(const SizePresets& s, bool persist = true);
  void applyPalette(const QString& id, bool persist = true);

  ToolboxSettings toolboxSettings() const { return toolbox_; }
  SizePresets sizes() const { return win_.sizePresets(); }
  QString paletteId() const { return prefs_.palette; }
  QStringList paletteColors() const;
  /** Save the current size of the tool's kind into preset `index` (right click / Alt+click / long press on a size dot). */
  bool savePresetFrom(SizeKind kind, int index);

  // ---- what bound things do --------------------------------------------------------------------------------------------------------------
  /** Run a command id as the tablet keys, the tool box and the menu do (tool.*, edit.*, pen.sizeUp, color.next, toolbox.show...). False when nothing could be done. */
  bool runAction(const QString& id);
  /** A tool box item: a toolbar button ("bar:save"), a colour ("color:#ff0000") or any action id. */
  void runToolboxItem(const QString& id);
  bool itemActive(const QString& id) const;
  /** The QAction id behind a tool box item ("bar:pgNext" -> "nav.next", "tool.pen" -> "tool.pen"); empty for colours and separators. */
  static QString actionIdOf(const QString& item);
  /** The tool box item a toolbar button stands for (a tool, a colour swatch, a command), empty when it has none. */
  QString itemOfAction(QAction* a) const;
  QIcon iconOfItem(const QString& id, const QString& iconName, const QString& label) const;

  // ---- windows ---------------------------------------------------------------------------------------------------------------------------
  ToolboxWidget* toolbox();
  TabletDialog* tabletDialog();
  PrefsDialog* prefsDialog();
  XppApplier* xppApplier() const { return xppApply_; }
  void showToolbox(const QPoint& globalPos);        // open (or move) it there; says so when it is switched off
  void toggleToolbox(const QPoint& globalPos);
  void setToolboxEnabled(bool on);
  void openTablet();
  void openPrefs();
  void popupPaletteMenu();

  /** While capturing, the next key, wheel turn or extra mouse button anywhere is handed to `cb` as its combination text instead of running (empty = Esc, cancelled). */
  using Capture = std::function<void(const QString& combo)>;
  void beginCapture(Capture cb);
  void endCapture();
  bool capturing() const { return bool(capture_); }

  /** Remembered place of a dialog (QSettings, only for a persisting window); restore() keeps it on a screen that still exists. */
  void restoreGeometry(QWidget* w, const QString& key, const QSize& fallback);
  /** True when a place for this key was remembered (this session or an earlier one): a dialog without one gets a place of the caller's choosing. */
  bool hasGeometry(const QString& key) const;
  void forgetGeometry(const QString& key);   // the next open places it afresh
  void rememberGeometry(QWidget* w, const QString& key);

 signals:
  void prefsApplied();   // any preference changed (theme, effects and welcome are applied by the chrome area, which listens to this)

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void onStyleChanged();
  void applyAll();
  void autoloadMostRecent();
  bool inTextInput() const;
  bool dialogActive() const;
  void startToolbarDrag(QToolButton* b);
  void nudgePen(double factor);

  MainWindow& win_;
  Editor& ed_;
  Prefs prefs_, baseline_;   // baseline_: as last read or written, to save only what this area changed
  ToolboxSettings toolbox_;
  QString path_;
  bool explicitPath_ = false, loaded_ = false, inStyle_ = false, autoloaded_ = false;
  QTimer saveTimer_, pressTimer_;
  ToolboxWidget* toolboxW_ = nullptr;
  TabletDialog* tabletW_ = nullptr;
  PrefsDialog* prefsW_ = nullptr;
  XppApplier* xppApply_ = nullptr;
  Capture capture_;
  int swallowTablet_ = 0;                  // the same for a barrel button of the pen (tablet events)
  int swallowRelease_ = 0;                // a Qt::MouseButton whose release must not reach the page (its press ran an action)
  // Hold-to-use: a bound barrel / extra mouse button that selects a tool keeps it only while pressed (Xournal++), then the tool before comes back.
  struct HeldButton { int button; bool tablet; };
  std::vector<HeldButton> held_;           // bound buttons that are down and hold a tool
  QString holdBefore_, holdSet_;           // the tool before the first hold, and the one the hold selected (holdSet_ empty = nothing to give back)
  bool restorePending_ = false;            // released while a gesture (the drag with that tool) was still on: give it back when it ends
  Qt::MouseButtons barrelRan_, penMouseHeld_;   // barrel buttons whose bound action already ran (a driver may report one twice: tablet event and mouse click) / mouse clicks taken for the pen's buttons
  bool barrelPress(int button, bool tablet, const Mods& mods);   // the pen's button as a bound key / a capture: true when it was consumed
  void dropStale();                        // focus lost or the pen left the range: nothing is held, swallowed or remembered as down any more
  void traceRaw(QEvent* e);                // the raw input trace of the Lápiz y tableta dialog (only while it is shown)
  void pressBound(int button, bool tablet, const QString& item);
  bool releaseBound(int button, bool tablet);   // true when it was one of the held buttons
  void endHold();                          // every held button is up, or the state went stale (focus lost, pen out of range)
  bool penButtonBound(PenButton b) const;
  QToolButton* pressed_ = nullptr;         // size dot held down (long press)
  QPoint pressPos_, menuPress_;
  QToolButton* dragBtn_ = nullptr;         // toolbar button pressed in customise mode: a drag may start
  bool longPressed_ = false;
};
