// The popup of a tool's options (the caret next to a toolbar button). A tool's panel is as big as its controls need; on a 768 px high screen some
// are taller than the screen, and a QMenu cannot scroll a widget action. ScrollingPanel wraps any panel: its size hint is the panel's, up to a
// maximum size the owner sets from the room left on the screen, and scroll bars take over beyond that.
#pragma once
#include <QScrollArea>
#include <QString>
#include <functional>

class MainWindow;
class Editor;

class ScrollingPanel : public QScrollArea {
  Q_OBJECT
 public:
  explicit ScrollingPanel(QWidget* panel, QWidget* parent = nullptr);
  /** Biggest the popup may be (pixels, 0 = no limit). The size hint is the panel's hint bounded by this. */
  bool setMaxSize(const QSize& px);   // true when it changed
  QSize maxSize() const { return max_; }
  bool scrollsVertically() const;
  bool scrollsHorizontally() const;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 private:
  QSize max_;
};

/** The room a popup that opens below `anchor` has on its screen (less the menu's own frame). Never under 240 px high: a popup that finds no room goes above instead. */
QSize roomBelow(const QWidget* anchor);
/** Self-test hook: pretend the screen is this high (0 = the real one), to check the 768 px case on a taller test screen. */
void setPretendScreenHeight(int px);

/** --selftest area "panelfit": every tool's options popup opened for real; it must fit the screen (scrolling when taller) and reach its last control. */
void runPanelFitChecks(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out);

/** --selftest area "toolbarfit": the toolbar rows at 1920 down to 700 px wide (prints the geometry, saves screenshots). */
void runToolbarFitChecks(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out, const QString& shots);
