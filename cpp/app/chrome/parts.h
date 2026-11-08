// The widgets of the chrome area (declared here so the self test can drive them; everything else about them is private to their .cpp).
#pragma once
#include <QThreadPool>
#include <cstdint>
#include "chrome.h"
#include "find.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QToolButton>

namespace chrome {

// ---- welcome page --------------------------------------------------------------------------------------------------------------------------
/** A small picture of a page with this paper ("graph" or "x:log"), drawn by core/pagerender, the code that draws real pages. */
QImage paperThumbnail(const QString& value, int width, double dpr);

class WelcomePage : public QWidget {
  Q_OBJECT
 public:
  WelcomePage(Chrome& c, QWidget* parent);
  bool isShown() const { return isVisible() && !hidden_; }
  void showPage();
  void hidePage();
  /** Where the recent files come from and how to clear them (tests replace both so the user's list is never touched). */
  std::function<QStringList()> recentSource;
  std::function<void()> clearRecent;
  // what the page shows now (for tests)
  int paperCardCount() const;
  QStringList recentPaths() const;
  QWidget* card(const QString& value) const;
  QPushButton* button(const QString& objectName) const;

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void paintEvent(QPaintEvent*) override;

 private:
  void rebuild();
  Chrome& c_;
  bool hidden_ = true;
  QWidget* content_ = nullptr;
  QScrollArea* scroll_ = nullptr;
};

// ---- side panels ---------------------------------------------------------------------------------------------------------------------------
class ThumbDelegate;
class ThumbsPanel : public QWidget {
  Q_OBJECT
 public:
  ThumbsPanel(Chrome& c, QWidget* parent = nullptr);
  ~ThumbsPanel() override;
  QListWidget* list() const { return list_; }
  /** The cached picture of a page, or a null pixmap while it has not been rendered (the delegate asks for it when the row is painted). */
  QPixmap cached(int page) const;
  void request(int page);
  /** Render everything that was asked for, now (tests, screenshots). Returns how many pages were drawn. */
  int renderPending();
  int renderedTotal() const { return rendered_; }
  void invalidate(int page, bool now = false);   // -1 = all; `now`: drop the pictures at once (rows moved) instead of after the debounce
  void rebuild();
  void syncCurrent();
  QToolButton* tool(const QString& name) const;

 private:
  void renderOne(int page);                 // on the GUI thread (renderPending, tests)
  void startAsync(int page);                // on a worker from a copy of the visible layers: a dense page must not freeze the window
  void storeThumb(int page, QImage img);
  void contextMenu(const QPoint& pos);
  Chrome& c_;
  QListWidget* list_;
  std::map<int, QPixmap> cache_;
  QSet<int> pending_;
  QTimer renderTimer_, dirtyTimer_;
  QSet<int> dirtyPages_;
  bool dirtyAll_ = false;
  int rendered_ = 0;
  QThreadPool* pool_ = nullptr;             // one low priority thread
  uint64_t epoch_ = 0;                      // bumped when every picture is forgotten: results of older jobs are dropped
  std::map<int, uint64_t> pageGen_;         // bumped when one page changes
  int inflight_ = -1;                       // the page a worker is drawing (one job at a time), -1 = none
  friend class ThumbDelegate;
};

class LayerList;
class LayerDelegate;
class LayersPanel : public QWidget {
  Q_OBJECT
 public:
  LayersPanel(Chrome& c, QWidget* parent = nullptr);
  QListWidget* list() const { return list_; }
  void refresh();                        // rebuild the rows now
  void scheduleRefresh() { timer_.start(0); }
  QToolButton* tool(const QString& name) const;
  /** Row -> layer index (rows list the top layer first). */
  int layerOfRow(int row) const;
  void toggleHidden(int layer);
  void setActive(int layer);

 private:
  Chrome& c_;
  QListWidget* list_;
  QTimer timer_;
  bool refreshing_ = false;
  friend class LayerList;
  friend class LayerDelegate;
};

// ---- the Journal's paper bar -----------------------------------------------------------------------------------------------------------------
class PaperBar : public QToolBar {
  Q_OBJECT
 public:
  PaperBar(Chrome& c, QWidget* parent);
  void sync();                            // show the paper, colour and size of the current page
  QComboBox* paperCombo() const { return paper_; }
  QComboBox* formatCombo() const { return format_; }
  QCheckBox* applyAll() const { return all_; }
  QWidget* paramsBox() const { return params_; }
  QToolButton* colorButton() const { return color_; }
  QDoubleSpinBox* widthMm() const { return w_; }
  QDoubleSpinBox* heightMm() const { return h_; }
  void choosePaper(const QString& value);
  void applyCustomSize();
  void chooseColor(const QString& rgb);
  QList<QToolButton*> swatches() const;
  void setPaletteId(const QString& id);
  QDoubleSpinBox* spacingMm() const { return spacing_; }    // distance between lines / dots of a built-in style; hidden for the other papers
  QDoubleSpinBox* opacityPct() const { return opacity_; }   // strength of the lines, 5 to 100 %
  void setSpacing(double mm);                               // one undo step each (the spin boxes commit after a short pause)
  void setOpacity(double percent);

 private:
  void renderParams(const std::optional<PaperSpec>& spec);
  void setPaperLook(const QString& fallback, const std::optional<PaperSpec>& spec);
  std::vector<int> targets() const;
  Chrome& c_;
  QComboBox *paper_, *format_;
  QWidget* params_;
  QCheckBox* all_;
  QToolButton* color_;
  QWidget* swatchBox_;
  QDoubleSpinBox *w_, *h_;
  QDoubleSpinBox *spacing_, *opacity_;
  QWidget* spacingBox_;
  QString paramsKey_ = " ";
  bool syncing_ = false;
};

// ---- find bar ------------------------------------------------------------------------------------------------------------------------------
class FindOverlay;
class FindBar : public QToolBar {
  Q_OBJECT
 public:
  FindBar(Chrome& c, QWidget* parent);
  bool isOpen() const { return isVisible(); }
  void open();
  void close();
  void run();
  void step(int delta);
  void setQuery(const QString& q);
  int matches() const { return int(state_.hits.size()); }
  int position() const { return state_.index; }
  QString countText() const;
  QLineEdit* input() const { return input_; }
  QCheckBox* caseBox() const { return case_; }
  const FindHit* current() const { return state_.current(); }
  const FindState& state() const { return state_; }

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void reveal();
  Chrome& c_;
  QLineEdit* input_;
  QCheckBox* case_;
  QLabel* count_;
  FindState state_;
  FindOverlay* overlay_;
  QTimer debounce_;
};

// ---- pen diagnostic ------------------------------------------------------------------------------------------------------------------------
struct PenSample { QString type, pointerType; double x = 0, y = 0, pressure = 0, t = 0; };
struct PenReport {
  int samples = 0;
  QString pointerTypes;
  double fractionalPhysical = 0;
  int distinctFractions = 0;
  double medianIntervalMs = 0;
  int eventsPerSecond = 0, distinctPressures = 0;
  double dpr = 1;
  QString verdict;
};
/** Summarise how precisely input arrives (the analyze() of diagnostic.ts). Pure, so it is tested with made-up samples. */
PenReport analyzePen(const std::vector<PenSample>& samples, double dpr);
QString penReportText(const PenReport& r);

class PenDiagnostic : public QDialog {
  Q_OBJECT
 public:
  PenDiagnostic(Chrome& c, QWidget* parent);
  void openDialog();
  QString reportText() const;
  /** What the canvas does with a stroke; tests feed it samples. */
  void feed(const std::vector<PenSample>& samples);
  class PenCanvas* canvas() const { return canvas_; }

 private:
  QLabel* report_;
  PenCanvas* canvas_;
};

}  // namespace chrome
