#include "xppapply.h"
#include "chrome/svgicon.h"
#include "documentview.h"
#include "mainwindow.h"
#include "settings.h"
#include "viewcolors.h"
#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QTabletEvent>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>

namespace {
double nowMs() {
  static QElapsedTimer t = [] { QElapsedTimer q; q.start(); return q; }();
  return double(t.nsecsElapsed()) / 1e6;
}

QColor parseColor(const QString& s) {   // "#rrggbb" or "#aarrggbb" (Qt's own order)
  return s.isEmpty() ? QColor() : QColor(s);
}

QCursor dotCursor() {
  QPixmap pm(16, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setBrush(Qt::black);
  p.setPen(QPen(Qt::white, 1));
  p.drawEllipse(QPointF(8, 8), 3.2, 3.2);
  return QCursor(pm, 8, 8);
}

QCursor bigPenCursor() {
  QPixmap pm(32, 32);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath body;   // a pencil leaning to the right, its tip at the bottom-left corner (the hot spot)
  body.moveTo(2, 30); body.lineTo(6, 20); body.lineTo(22, 4); body.lineTo(28, 10); body.lineTo(12, 26); body.closeSubpath();
  p.setPen(QPen(Qt::black, 1.4));
  p.setBrush(QColor(255, 214, 0));
  p.drawPath(body);
  p.setBrush(QColor(60, 60, 60));
  p.drawPolygon(QPolygonF({QPointF(2, 30), QPointF(6, 20), QPointF(12, 26)}));
  return QCursor(pm, 2, 30);
}
}  // namespace

// A transparent layer over the page area for what is drawn on top of the pages: the border of the current page and the ring that follows the pointer.
class DecorOverlay : public QWidget {
 public:
  DecorOverlay(DocumentView* v, Editor& ed) : QWidget(v->viewport()), v_(v), ed_(ed) {
    setObjectName("xppDecor");
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setGeometry(v->viewport()->rect());
    v->viewport()->installEventFilter(this);
    connect(v->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateBorder(); });
    connect(v->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateBorder(); });
    connect(v, &DocumentView::zoomChanged, this, [this] { updateBorder(); });
    connect(&ed, &Editor::currentPageChanged, this, [this] { updateBorder(); });
    connect(&ed, &Editor::structureChanged, this, [this] { updateBorder(); });
    connect(&ed, &Editor::documentReplaced, this, [this] { updateBorder(); });
  }
  void setup(const XppPrefs& x) {
    border = parseColor(x.colorBorder);
    ring = x.highlightCursor && x.highlightRadius > 0;
    radius = x.highlightRadius;
    fill = parseColor(x.highlightColor);
    edge = parseColor(x.highlightBorderColor);
    edgeW = x.highlightBorderWidth;
    update();
  }
  bool active() const { return border.isValid() || ring; }
  QColor border, fill, edge;
  bool ring = false;
  double radius = 30, edgeW = 0;
  QPointF pos{-1000, -1000};
  QRectF ringRect() const { const double r = radius + edgeW + 2; return QRectF(pos.x() - r, pos.y() - r, 2 * r, 2 * r); }

 protected:
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == v_->viewport()) {
      switch (e->type()) {
        case QEvent::Resize: setGeometry(v_->viewport()->rect()); break;
        case QEvent::MouseMove: case QEvent::TabletMove: moveTo(static_cast<QSinglePointEvent*>(e)->position()); break;
        case QEvent::Leave: moveTo(QPointF(-1000, -1000)); break;
        default: break;
      }
    }
    return QWidget::eventFilter(o, e);
  }
  void paintEvent(QPaintEvent* ev) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (border.isValid() && ed_.currentPage() >= 0 && ed_.currentPage() < ed_.pageCount()) {
      const QRectF r = v_->pageRectOnScreen(ed_.currentPage());
      if (r.intersects(QRectF(ev->rect()).adjusted(-3, -3, 3, 3))) { p.setPen(QPen(border, 2)); p.setBrush(Qt::NoBrush); p.drawRect(r.adjusted(-1, -1, 1, 1)); }
    }
    if (ring && ringRect().intersects(QRectF(ev->rect()))) {
      p.setBrush(fill.isValid() ? fill : QColor(255, 255, 0, 128));
      if (edgeW > 0 && edge.isValid()) p.setPen(QPen(edge, edgeW)); else p.setPen(Qt::NoPen);
      p.drawEllipse(pos, radius, radius);
    }
  }

 private:
  void moveTo(const QPointF& np) {
    if (!ring) { pos = np; return; }
    const QRectF old = ringRect();
    pos = np;
    update(old.united(ringRect()).toAlignedRect());
  }
  void updateBorder() { if (border.isValid()) update(); }
  DocumentView* v_;
  Editor& ed_;
};

XppApplier::XppApplier(SettingsController& c) : QObject(&c), c_(c), win_(c.window()), ed_(c.editor()) {
  filter_ = std::make_shared<InputFilter>();
  filter_->onTap = [this](const PointerEvent& e) { if (x_.tapToolbox) emit ed_.toolboxRequested(e.globalPos.toPoint()); };
  ed_.setInputFilter(filter_);
  ed_.setSizeLookup([this](const QString& toolId, int index) -> double {
    SizeKind k;
    if (toolId == "pen") k = SizeKind::Pen;
    else if (toolId == "highlighter") k = SizeKind::Highlighter;
    else if (toolId == "eraser" || toolId == "eraserPart") k = SizeKind::Eraser;
    else return 0;
    const auto& v = win_.sizePresets().of(k);
    return index >= 0 && index < int(v.size()) ? v[size_t(index)] : 0;
  });
  win_.view()->viewport()->installEventFilter(this);
  win_.installEventFilter(this);
  win_.view()->setCursorMapper([this](const Tool&, const QCursor& own) {   // Preferencias > Ver: what the pen and the highlighter show over the page (option 0 = their own disc)
    const QString id = ed_.tool();
    if (id != "pen" && id != "highlighter") return own;
    static const QCursor dot = dotCursor(), pen = bigPenCursor();
    switch (x_.cursorIcon) {
      case 1: return QCursor(Qt::BlankCursor);
      case 2: return dot;
      case 3: return pen;
      case 4: return QCursor(Qt::ArrowCursor);
      case 5: return QCursor(Qt::CrossCursor);
      default: return own;
    }
  });
  connect(&ed_, &Editor::toolChanged, this, [this] { applyCursor(); });
  connect(&ed_, &Editor::documentReplaced, this, [this] {
    if (x_.forceFitOnLoad) QTimer::singleShot(0, this, [this] { win_.view()->zoomFitWidth(); });
  });
  connect(&ed_, &Editor::elementAppended, this, [this](int page, int, int, const QRectF&) { maybeAppendPage(page); });
  connect(&c_, &SettingsController::prefsApplied, this, [this] {   // the chrome area re-applies its theme on this signal: the user's background goes on top of it afterwards
    QTimer::singleShot(0, this, [this] {
      if (QColor bg = parseColor(x_.colorBackground); bg.isValid()) { DocumentView::DeskStyle d = win_.view()->deskStyle(); d.desk = bg; d.gradTop = QColor(); win_.view()->setDeskStyle(d); }
    });
  });
  // the presentation mode (the chrome area hides everything and goes fullscreen): Preferencias > Ver > Modo presentación says what stays
  connect(ed_.action("view.presentation"), &QAction::triggered, this, [this] { QTimer::singleShot(0, this, [this] { applyPresentationParts(); }); });
  menuAction_ = ed_.addCommand("view.menubar", QString::fromUtf8("Barra de menús"), QKeySequence("F10"), [this] {
    const bool show = !win_.menuBar()->isVisible();
    win_.menuBar()->setVisible(show);
    if (menuAction_) menuAction_->setChecked(show);
  });
  menuAction_->setCheckable(true);
  menuAction_->setChecked(true);
  menuAction_->setToolTip(QString::fromUtf8("Mostrar u ocultar la barra de menús (F10)"));
  if (QMenu* m = win_.menu("view")) m->addAction(menuAction_);
}

XppApplier::~XppApplier() = default;

QWidget* XppApplier::overlay() const { return overlay_; }

QString XppApplier::penCursorKind() const {
  switch (x_.cursorIcon) {
    case 1: return "none"; case 2: return "dot"; case 3: return "pen"; case 4: return "system"; case 5: return "cross"; default: return "tool";
  }
}

void XppApplier::apply(const XppPrefs& x) {
  x_ = x;
  ed_.setXpp(x);
  const QScreen* scr = win_.screen() ? win_.screen() : QGuiApplication::primaryScreen();
  filter_->configure(x, scr ? scr->logicalDotsPerInch() : 96);
  DocumentView* v = win_.view();
  v->setPageShadow(x.pageShadow);
  // scroll bars
  v->setHorizontalScrollBarPolicy(x.hideHScroll ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
  v->setVerticalScrollBarPolicy(x.hideVScroll ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
  const Qt::LayoutDirection want = x.scrollbarLeft ? Qt::RightToLeft : Qt::LeftToRight;
  if (v->layoutDirection() != want) {
    v->setLayoutDirection(want);                                       // QAbstractScrollArea puts the vertical bar on the left in a right-to-left layout...
    v->horizontalScrollBar()->setLayoutDirection(Qt::LeftToRight);     // ...and the value of the horizontal bar keeps its meaning
    v->viewport()->setLayoutDirection(Qt::LeftToRight);
  }
  // sidebar
  if (auto* dock = win_.findChild<QDockWidget*>("dock")) {
    const Qt::DockWidgetArea area = x.sidebarRight ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
    if (win_.dockWidgetArea(dock) != area) { const bool vis = dock->isVisible(); win_.addDockWidget(area, dock); dock->setVisible(vis); }
  }
  for (QListView* l : win_.findChildren<QListView*>()) l->viewport()->update();   // numbering of the thumbnails
  // extra space around the pages
  QMargins m;
  if (x.unlimitedScroll) m = QMargins(v->viewport()->width(), v->viewport()->height(), v->viewport()->width(), v->viewport()->height());
  else m = QMargins(x.addHSpace ? x.hSpaceLeft : 0, x.addVSpace ? x.vSpaceAbove : 0, x.addHSpace ? x.hSpaceRight : 0, x.addVSpace ? x.vSpaceBelow : 0);
  v->setExtraSpace(m);
  // colours
  selx::setSelectionColor(parseColor(x.colorSelection));
  if (QColor bg = parseColor(x.colorBackground); bg.isValid()) { DocumentView::DeskStyle d = v->deskStyle(); d.desk = bg; d.gradTop = QColor(); v->setDeskStyle(d); }
  v->viewport()->update();
  // decor overlay (current page border, ring at the pointer)
  updateOverlay();
  applyCursor();
  if (first_) {
    first_ = false;
    if (!x.menubarAtStart) { win_.menuBar()->setVisible(false); menuAction_->setChecked(false); }
  }
}

void XppApplier::updateOverlay() {
  const bool needed = parseColor(x_.colorBorder).isValid() || (x_.highlightCursor && x_.highlightRadius > 0);
  if (needed && !overlay_) { overlay_ = new DecorOverlay(win_.view(), ed_); overlay_->show(); }
  if (overlay_) {
    overlay_->setup(x_);
    if (needed) { overlay_->show(); overlay_->raise(); } else overlay_->hide();
  }
}

void XppApplier::applyCursor() {
  DocumentView* v = win_.view();
  v->applyCursor();   // the mapper installed in the constructor puts the chosen pen cursor over a pen's
}

void XppApplier::applyFullscreenParts(bool entering) {
  const bool presenting = [&] { auto* chip = win_.findChild<QPushButton*>("presentChip"); return chip && chip->isVisible(); }();
  if (presenting) return;   // the presentation mode brings its own options
  if (entering) {
    if (parts_.saved) return;
    parts_.title = win_.titleBar()->isVisible();
    parts_.tool = win_.toolBar()->isVisible();
    auto* dock = win_.findChild<QDockWidget*>("dock");
    parts_.dock = dock && dock->isVisible();
    parts_.saved = true;
    if (!x_.fsMenubar) win_.menuBar()->setVisible(false);
    if (!x_.fsToolbar) win_.toolBar()->hide();
    if (dock && !x_.fsSidebar) dock->hide();
  } else if (parts_.saved) {
    parts_.saved = false;
    win_.menuBar()->setVisible(menuAction_ ? menuAction_->isChecked() : true);
    win_.toolBar()->setVisible(parts_.tool);
    if (auto* dock = win_.findChild<QDockWidget*>("dock")) dock->setVisible(parts_.dock);
  }
}

void XppApplier::applyPresentationParts() {
  auto* chip = win_.findChild<QPushButton*>("presentChip");
  if (!chip || !chip->isVisible()) return;   // it was the way out
  if (x_.prMenubar) win_.titleBar()->show();
  if (x_.prToolbar) { win_.toolBar()->show(); win_.topBar()->show(); }
  if (x_.prSidebar) if (auto* dock = win_.findChild<QDockWidget*>("dock")) dock->show();
  if (!x_.prFullscreen && win_.isFullScreen()) win_.showNormal();
}

bool XppApplier::lastPageIsPdf() const {
  const int n = ed_.pageCount();
  if (n <= 0) return false;
  return attrOr(ed_.doc().pages[size_t(n - 1)].background, "type") == "pdf";
}

void XppApplier::maybeAppendPage(int page) {
  if (x_.lastPageAppend != 2 || page != ed_.pageCount() - 1 || lastPageIsPdf()) return;
  const int n = ed_.pageCount();
  QTimer::singleShot(0, this, [this, n] {   // after the stroke's own step is on the history
    if (ed_.pageCount() != n) return;
    ed_.insertPage(n, n - 1);
    appended_++;
  });
}

bool XppApplier::eventFilter(QObject* o, QEvent* e) {
  DocumentView* v = win_.view();
  if (o == v->viewport()) {
    switch (e->type()) {
      case QEvent::Wheel: {   // scrolling past the end of the last page adds a page (Preferencias > Área de dibujo)
        if (x_.lastPageAppend != 1 || ed_.gestureActive() || lastPageIsPdf()) break;
        auto* w = static_cast<QWheelEvent*>(e);
        QScrollBar* sb = v->verticalScrollBar();
        if (w->angleDelta().y() < 0 && !(w->modifiers() & Qt::ControlModifier) && sb->value() >= sb->maximum() && nowMs() - lastAppendMs_ > 600 && v->continuous()) {
          lastAppendMs_ = nowMs();
          ed_.insertPage(ed_.pageCount(), ed_.pageCount() - 1);
          appended_++;
        }
        break;
      }
      default: break;
    }
  } else if (o == &win_ && e->type() == QEvent::WindowStateChange) {
    applyFullscreenParts(win_.isFullScreen());
  }
  return QObject::eventFilter(o, e);
}

void XppApplier::selectDefaultTool() {
  const ButtonCfg& d = x_.defaultTool;
  if (!d.tool.isEmpty() && ed_.hasTool(d.tool)) ed_.setTool(d.tool);
  Style s = ed_.style();
  if (!d.color.isEmpty()) s.color = d.color;
  if (d.size > 0) {
    const QString id = ed_.tool();
    SizeKind k;
    const bool known = id == "pen" ? (k = SizeKind::Pen, true) : id == "highlighter" ? (k = SizeKind::Highlighter, true) : (id == "eraser" || id == "eraserPart") ? (k = SizeKind::Eraser, true) : false;
    if (known) {
      const auto& v = win_.sizePresets().of(k);
      if (d.size - 1 < int(v.size())) { const double w = v[size_t(d.size - 1)]; if (k == SizeKind::Pen) s.penWidth = w; else if (k == SizeKind::Highlighter) s.highlighterWidth = w; else s.eraserRadius = w; }
    }
  }
  ed_.setStyle(s);
}
