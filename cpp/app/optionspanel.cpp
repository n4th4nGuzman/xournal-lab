#include "optionspanel.h"
#include <QScreen>
#include <QScrollBar>
#include <algorithm>

namespace {
int g_pretendHeight = 0;
constexpr int kMenuFrame = 12;   // QMenu's own border and padding around a widget action (measured: popup = panel + 10)
}

ScrollingPanel::ScrollingPanel(QWidget* panel, QWidget* parent) : QScrollArea(parent) {
  setObjectName(panel->objectName().isEmpty() ? QStringLiteral("scrollingPanel") : panel->objectName());   // callers look the popup up by the name of the panel
  setFrameShape(QFrame::NoFrame);
  setWidgetResizable(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
  setWidget(panel);
  viewport()->setAutoFillBackground(false);
  panel->setAutoFillBackground(false);
}

bool ScrollingPanel::setMaxSize(const QSize& px) {
  if (px == max_) return false;
  max_ = px;
  updateGeometry();
  return true;
}

bool ScrollingPanel::scrollsVertically() const { return max_.height() > 0 && widget() && widget()->sizeHint().height() > max_.height(); }
bool ScrollingPanel::scrollsHorizontally() const { return max_.width() > 0 && widget() && widget()->sizeHint().width() > max_.width(); }

QSize ScrollingPanel::sizeHint() const {
  const QSize c = widget() ? widget()->sizeHint() : QSize();
  const int bar = verticalScrollBar()->sizeHint().width(), hbar = horizontalScrollBar()->sizeHint().height();
  int w = c.width(), h = c.height();
  if (scrollsVertically()) { h = max_.height(); w += bar; }
  if (scrollsHorizontally()) { w = max_.width(); h = std::min(h + hbar, max_.height() > 0 ? max_.height() : h + hbar); }
  return QSize(w, h);
}

QSize ScrollingPanel::minimumSizeHint() const { return QSize(std::min(160, sizeHint().width()), std::min(120, sizeHint().height())); }

QSize roomBelow(const QWidget* anchor) {
  const QScreen* s = anchor->screen();
  if (!s) return QSize(1000, 600);
  const QRect avail = s->availableGeometry();
  const int availH = g_pretendHeight > 0 ? std::min(g_pretendHeight, avail.height()) : avail.height();
  const int bottom = anchor->isVisible() ? anchor->mapToGlobal(QPoint(0, anchor->height())).y() - avail.top() : 70;
  return QSize(std::max(240, avail.width() - kMenuFrame - 8), std::clamp(availH - bottom - kMenuFrame - 8, 240, availH - 24));
}

void setPretendScreenHeight(int px) { g_pretendHeight = px; }
