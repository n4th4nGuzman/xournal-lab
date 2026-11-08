// Small widgets the option panels of this area share. Header-only so the panels stay in one place each.
#pragma once
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QStyle>
#include <functional>

namespace mathobj {

/** A text field that keeps Enter for itself: in a popup, an Enter the field ignores would reach the menu and close the panel. */
class CommitEdit final : public QLineEdit {
 public:
  using QLineEdit::QLineEdit;
  std::function<void()> onFocus;
 protected:
  void keyPressEvent(QKeyEvent* e) override {
    QLineEdit::keyPressEvent(e);
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) e->accept();
  }
  void focusInEvent(QFocusEvent* e) override {
    QLineEdit::focusInEvent(e);
    if (onFocus) onFocus();
  }
};

/** Number field: no trailing zeros, applies on Enter, on leaving it and on the arrows (never per key). */
class NumBox final : public QDoubleSpinBox {
 public:
  explicit NumBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
    setDecimals(6);
    setRange(-1e9, 1e9);
    setKeyboardTracking(false);
    setSingleStep(1);
    setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  }
  QString textFromValue(double v) const override { return QString::number(v, 'g', 8); }
 protected:
  void keyPressEvent(QKeyEvent* e) override {
    QDoubleSpinBox::keyPressEvent(e);
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) e->accept();
  }
};

/** Left to right, wrapping to the next row (the Qt "flow layout" example, trimmed): symbol buttons and expression presets. */
class FlowLayout final : public QLayout {
 public:
  explicit FlowLayout(QWidget* parent = nullptr, int hSpacing = 3, int vSpacing = 3) : QLayout(parent), h_(hSpacing), v_(vSpacing) { setContentsMargins(0, 0, 0, 0); }
  ~FlowLayout() override { while (QLayoutItem* it = takeAt(0)) delete it; }
  void addItem(QLayoutItem* it) override { items_.push_back(it); }
  int count() const override { return int(items_.size()); }
  QLayoutItem* itemAt(int i) const override { return i >= 0 && i < count() ? items_[size_t(i)] : nullptr; }
  QLayoutItem* takeAt(int i) override {
    if (i < 0 || i >= count()) return nullptr;
    QLayoutItem* it = items_[size_t(i)];
    items_.erase(items_.begin() + i);
    return it;
  }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
  void setGeometry(const QRect& r) override { QLayout::setGeometry(r); doLayout(r, false); }
  QSize sizeHint() const override { return minimumSize(); }
  QSize minimumSize() const override {
    QSize s;
    for (QLayoutItem* it : items_) s = s.expandedTo(it->minimumSize());
    const QMargins m = contentsMargins();
    return s + QSize(m.left() + m.right(), m.top() + m.bottom());
  }
 private:
  int doLayout(const QRect& rect, bool testOnly) const {
    const QMargins m = contentsMargins();
    const QRect eff = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = eff.x(), y = eff.y(), lineH = 0;
    for (QLayoutItem* it : items_) {
      const QSize sz = it->sizeHint();
      if (x + sz.width() > eff.right() + 1 && lineH > 0) { x = eff.x(); y += lineH + v_; lineH = 0; }
      if (!testOnly) it->setGeometry(QRect(QPoint(x, y), sz));
      x += sz.width() + h_;
      lineH = std::max(lineH, sz.height());
    }
    return y + lineH - rect.y() + m.bottom();
  }
  std::vector<QLayoutItem*> items_;
  int h_, v_;
};

}  // namespace mathobj
