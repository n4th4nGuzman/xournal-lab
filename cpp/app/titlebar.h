// The window's own title bar (the Electron app draws its own too: index.html #titlebar): logo, the menu bar, the centred title and the minimise / maximise /
// close buttons, 32 px high, in the colours of the theme. MainWindow is frameless; on Windows it answers WM_NCHITTEST with this bar's caption area, so
// dragging, double-click to maximise, Aero snap and the resize edges are the system's own (see MainWindow::nativeEvent).
#pragma once
#include <QAbstractButton>
#include <QLabel>
#include <QMenuBar>
#include <QWidget>

class CaptionButton;

class TitleBar : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kHeight = 32;
  explicit TitleBar(QWidget* parent = nullptr);
  QMenuBar* menuBar() const { return menu_; }
  void setTitle(const QString& t);
  QString title() const { return title_->fullText(); }
  /** Colours of the theme ("classic" or "neon"); `glow` is the neon text-shadow of the logo (off with reduced effects). */
  void setTheme(bool neon, bool glow);
  /** Is this point (in this widget's coordinates) a place to drag the window by: the bar itself, the logo, the title, the gaps of the menu bar? */
  bool isCaptionAt(const QPoint& p) const;
  CaptionButton* minimizeButton() const { return min_; }
  CaptionButton* maximizeButton() const { return max_; }
  CaptionButton* closeButton() const { return close_; }
  void syncWindowState();   // the maximise button shows "restore" while the window is maximised

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
  void showEvent(QShowEvent*) override;

 private:
  class TitleLabel : public QLabel {
   public:
    using QLabel::QLabel;
    void setFullText(const QString& t) { full_ = t; elide(); }
    QString fullText() const { return full_; }
   protected:
    void resizeEvent(QResizeEvent* e) override { QLabel::resizeEvent(e); elide(); }
   private:
    void elide() { QLabel::setText(fontMetrics().elidedText(full_, Qt::ElideRight, std::max(0, width() - 4))); }
    QString full_;
  };
  QLabel* logo_ = nullptr;
  QMenuBar* menu_ = nullptr;
  TitleLabel* title_ = nullptr;
  CaptionButton *min_ = nullptr, *max_ = nullptr, *close_ = nullptr;
  QColor bg_{0x2b, 0x2b, 0x2b}, line_{0x1b, 0x1b, 0x1b};
  bool filtering_ = false;
};

class CaptionButton : public QAbstractButton {
  Q_OBJECT
 public:
  enum Kind { Minimize, Maximize, Close };
  CaptionButton(Kind k, QWidget* parent);
  QSize sizeHint() const override { return QSize(46, TitleBar::kHeight); }
  void setRestore(bool r) { restore_ = r; update(); }
  bool isRestore() const { return restore_; }
  void setColors(const QColor& symbol, const QColor& hover) { symbol_ = symbol; hover_ = hover; update(); }
  Kind kind() const { return kind_; }
 protected:
  void paintEvent(QPaintEvent*) override;
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }
 private:
  Kind kind_;
  bool restore_ = false;
  QColor symbol_{0xdd, 0xdd, 0xdd}, hover_{255, 255, 255, 26};
};
