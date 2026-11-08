#include "titlebar.h"
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QWindow>

CaptionButton::CaptionButton(Kind k, QWidget* parent) : QAbstractButton(parent), kind_(k) {
  setFocusPolicy(Qt::NoFocus);
  setFixedSize(46, TitleBar::kHeight);
  setCursor(Qt::ArrowCursor);
}

void CaptionButton::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const bool hot = underMouse() || isDown();
  if (hot) p.fillRect(rect(), kind_ == Close ? QColor(0xe8, 0x11, 0x23) : hover_);
  QColor ink = kind_ == Close && hot ? QColor(Qt::white) : symbol_;
  p.setPen(QPen(ink, 1.0));
  p.setRenderHint(QPainter::Antialiasing, false);
  const int cx = width() / 2, cy = height() / 2;
  switch (kind_) {
    case Minimize: p.drawLine(cx - 5, cy, cx + 4, cy); break;
    case Maximize:
      if (restore_) {   // two overlapping squares, the back one only where it shows
        p.drawRect(QRect(cx - 5, cy - 2, 7, 7));
        p.drawLine(cx - 3, cy - 4, cx + 4, cy - 4);
        p.drawLine(cx + 4, cy - 4, cx + 4, cy + 3);
        p.drawLine(cx - 3, cy - 4, cx - 3, cy - 2);
        p.drawLine(cx + 2, cy + 3, cx + 4, cy + 3);
      } else p.drawRect(QRect(cx - 5, cy - 5, 9, 9));
      break;
    case Close:
      p.setRenderHint(QPainter::Antialiasing, true);
      p.drawLine(QPointF(cx - 5, cy - 5), QPointF(cx + 5, cy + 5));
      p.drawLine(QPointF(cx + 5, cy - 5), QPointF(cx - 5, cy + 5));
      break;
  }
}

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
  setObjectName("titlebar");
  setFixedHeight(kHeight);
  setAttribute(Qt::WA_StyledBackground, false);
  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(8, 0, 0, 0);
  lay->setSpacing(0);
  logo_ = new QLabel(QStringLiteral("✎"), this);
  logo_->setObjectName("tbLogo");
  menu_ = new QMenuBar(this);
  menu_->setNativeMenuBar(false);
  menu_->setObjectName("menubar");
  menu_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  menu_->setFixedHeight(kHeight);
  title_ = new TitleLabel(this);
  title_->setObjectName("tbTitle");
  title_->setAlignment(Qt::AlignCenter);
  title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  min_ = new CaptionButton(CaptionButton::Minimize, this);
  max_ = new CaptionButton(CaptionButton::Maximize, this);
  close_ = new CaptionButton(CaptionButton::Close, this);
  min_->setObjectName("capMin"); max_->setObjectName("capMax"); close_->setObjectName("capClose");
  min_->setToolTip(QStringLiteral("Minimizar")); max_->setToolTip(QStringLiteral("Maximizar")); close_->setToolTip(QStringLiteral("Cerrar"));
  lay->addWidget(logo_);
  lay->addWidget(menu_);
  lay->addWidget(title_, 1);
  lay->addWidget(min_);
  lay->addWidget(max_);
  lay->addWidget(close_);
  connect(min_, &QAbstractButton::clicked, this, [this] { if (window()) window()->showMinimized(); });
  connect(max_, &QAbstractButton::clicked, this, [this] {
    QWidget* w = window();
    if (!w) return;
    if (w->isMaximized() || w->isFullScreen()) w->showNormal(); else w->showMaximized();
  });
  connect(close_, &QAbstractButton::clicked, this, [this] { if (window()) window()->close(); });
  setTheme(false, false);
}

void TitleBar::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  if (!filtering_ && window()) { window()->installEventFilter(this); filtering_ = true; }
  syncWindowState();
}

bool TitleBar::eventFilter(QObject* o, QEvent* e) {
  if (o == window() && e->type() == QEvent::WindowStateChange) syncWindowState();
  return QWidget::eventFilter(o, e);
}

void TitleBar::syncWindowState() {
  const bool maxed = window() && (window()->isMaximized() || window()->isFullScreen());
  max_->setRestore(maxed);
  max_->setToolTip(maxed ? QStringLiteral("Restaurar") : QStringLiteral("Maximizar"));
}

void TitleBar::setTitle(const QString& t) { title_->setFullText(t); }

void TitleBar::setTheme(bool neon, bool glow) {
  Q_UNUSED(glow);
  bg_ = neon ? QColor(0x1e, 0x10, 0x43) : QColor(0x2b, 0x2b, 0x2b);
  line_ = neon ? QColor(0xff, 0x2a, 0x9d) : QColor(0x1b, 0x1b, 0x1b);
  const QColor fg = neon ? QColor(0xe9, 0xde, 0xfc) : QColor(0xdd, 0xdd, 0xdd);
  const QColor symbol = neon ? QColor(0x05, 0xe1, 0xff) : QColor(0xdd, 0xdd, 0xdd);
  const QString hot = neon ? "rgba(185,103,255,51)" : "#3d3d3d", hotLine = neon ? "#b967ff" : "#555555";
  menu_->setStyleSheet(QString("QMenuBar { background: transparent; color: %1; font-size: 12.5px; padding: 4px 0; spacing: 0; border: none; } "
                               "QMenuBar::item { padding: 3px 9px; margin: 0; background: transparent; border: 1px solid transparent; border-radius: 4px; } "
                               "QMenuBar::item:selected, QMenuBar::item:pressed { background: %2; border: 1px solid %3; }").arg(fg.name(), hot, hotLine));
  logo_->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; background: transparent; padding-right: 8px; }").arg(neon ? "#05e1ff" : "rgba(221,221,221,204)"));
  QColor titleInk = fg;
  titleInk.setAlphaF(0.65);
  title_->setStyleSheet(QString("QLabel { color: rgba(%1,%2,%3,166); font-size: 12.5px; background: transparent; padding: 0 12px; }").arg(fg.red()).arg(fg.green()).arg(fg.blue()));
  for (CaptionButton* b : {min_, max_, close_}) b->setColors(symbol, QColor(255, 255, 255, 26));
  update();
}

bool TitleBar::isCaptionAt(const QPoint& p) const {
  if (!rect().contains(p)) return false;
  QWidget* c = childAt(p);
  if (!c || c == this || c == logo_ || c == title_) return true;
  if (c == menu_) return menu_->actionAt(menu_->mapFrom(const_cast<TitleBar*>(this), p)) == nullptr;
  return false;
}

void TitleBar::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), bg_);
  p.fillRect(QRect(0, height() - 1, width(), 1), line_);
}

void TitleBar::mousePressEvent(QMouseEvent* e) {
  // Windows answers WM_NCHITTEST itself (HTCAPTION) and never gets here; elsewhere the window manager is asked to move the window
  if (e->button() == Qt::LeftButton && isCaptionAt(e->position().toPoint()) && window() && window()->windowHandle()) {
    window()->windowHandle()->startSystemMove();
    e->accept();
    return;
  }
  QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && isCaptionAt(e->position().toPoint())) { max_->click(); e->accept(); return; }
  QWidget::mouseDoubleClickEvent(e);
}
