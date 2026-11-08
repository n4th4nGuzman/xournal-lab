// Puts the preferences of the original Xournal++ that are not the Editor's own into effect: the pointer filter, the pen cursor, the cursor highlight, the colours
// of the page border / background / selection, scroll bars and which side the sidebar and the vertical bar are on, the menu bar (F10), extra space around the pages,
// zoom to fit on load, the empty last page, the options of fullscreen and presentation mode, the default tool. Owned by the SettingsController; every setting
// that reaches the Editor does so through Editor::setXpp, the rest is applied to the widgets here.
#pragma once
#include "inputfilter.h"
#include "xpppref.h"
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <memory>

class MainWindow;
class Editor;
class DocumentView;
class QWidget;
class QAction;
class SettingsController;
class DecorOverlay;

class XppApplier : public QObject {
  Q_OBJECT
 public:
  explicit XppApplier(SettingsController& c);
  ~XppApplier() override;
  /** Everything of `x` takes effect now. The menu bar is hidden at the first call only (the "at start" setting). */
  void apply(const XppPrefs& x);
  /** The "Predeterminado" button: the tool, colour and size chosen in Valores predeterminados. */
  void selectDefaultTool();
  InputFilter* filter() const { return filter_.get(); }
  int appendedPages() const { return appended_; }
  void rearmStart() { first_ = true; }   // tests: the next apply() is the first one again (the menu bar "at start" setting)
  // for the self test
  QWidget* overlay() const;
  QString penCursorKind() const;

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void applyCursor();
  void updateOverlay();
  void applyFullscreenParts(bool entering);
  void applyPresentationParts();
  void maybeAppendPage(int page);
  bool lastPageIsPdf() const;

  SettingsController& c_;
  MainWindow& win_;
  Editor& ed_;
  std::shared_ptr<InputFilter> filter_;
  XppPrefs x_;
  bool first_ = true;
  QPointer<DecorOverlay> overlay_;
  QAction* menuAction_ = nullptr;
  QColor deskDefault_;
  bool deskCaptured_ = false;
  double lastAppendMs_ = -1e9;
  int appended_ = 0;
  struct Parts { bool title = true, tool = true, dock = true; bool saved = false; } parts_;
};
