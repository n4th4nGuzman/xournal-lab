// A box that places its child widgets left to right and wraps them at a fixed width, and sets its own height to fit. The tool box zones and its
// catalogue use it: the width never changes, so no heightForWidth negotiation is needed. In customise mode it is a drop target and shows where the
// dragged button would land.
#pragma once
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <vector>

class WrapBox : public QWidget {   // no Q_OBJECT: nothing here needs the meta-object system
 public:
  explicit WrapBox(int width, QWidget* parent = nullptr) : QWidget(parent), width_(width) {
    setFixedWidth(width);
  }
  void add(QWidget* w) { w->setParent(this); items_.push_back(w); }
  /** A corner widget (the delete button of a zone) kept out of the wrapping area. */
  void setCorner(QWidget* w) { corner_ = w; if (w) w->setParent(this); }
  void setMinRowHeight(int h) { minH_ = h; }
  void setDropTarget(bool on) { setAcceptDrops(on); dashed_ = on; update(); }
  void setBorderColor(const QColor& c) { border_ = c; }
  void relayout() {
    const int pad = 4, gap = 4, right = corner_ ? 34 : 0;
    int x = pad, y = pad, rowH = 0;
    for (QWidget* w : items_) {
      // an item that names its size ("wrapSize") gets it: a style sheet may change the minimum size of a button behind setFixedSize's back
      const QSize want = w->property("wrapSize").toSize();
      const QSize fs = want.isValid() ? want : (w->minimumSize() == w->maximumSize() ? w->minimumSize() : w->sizeHint().expandedTo(w->minimumSize()));
      if (x > pad && x + fs.width() > width_ - pad - right) { x = pad; y += rowH + gap; rowH = 0; }
      w->setGeometry(x, y, fs.width(), fs.height());
      w->show();
      x += fs.width() + gap;
      rowH = std::max(rowH, fs.height());
    }
    const int h = std::max(minH_, y + rowH + pad);
    setFixedHeight(h);
    if (corner_) { corner_->move(width_ - corner_->width() - 3, 3); corner_->raise(); }
  }
  /** Position of the item (in `items`) a drop at `p` lands before; -1 = at the end. */
  int indexAt(const QPoint& p) const {
    for (int i = 0; i < int(items_.size()); i++) if (items_[size_t(i)]->geometry().adjusted(-2, -2, 2, 2).contains(p)) return i;
    return -1;
  }
  const std::vector<QWidget*>& items() const { return items_; }
  /** What a drop at `local` does (dropEvent calls this; the self test too, since a synthetic QDropEvent does not reach dropEvent on every platform). */
  bool dropAt(const QMimeData* m, const QPoint& local) { return onDrop && onDrop(m, indexAt(local)); }

  /** Called with the dragged mime data and the item index it was dropped before (-1 = last). Returns true when it was taken. */
  std::function<bool(const QMimeData*, int)> onDrop;
  std::function<bool(const QMimeData*)> accepts;

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    if (fill_.alpha()) p.fillRect(rect(), fill_);
    if (dashed_) {
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(border_, 1, Qt::DashLine));
      p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    }
    if (mark_ >= -1 && marking_) {
      QRect r;
      if (mark_ >= 0 && mark_ < int(items_.size())) { r = items_[size_t(mark_)]->geometry(); p.fillRect(QRect(r.left() - 4, r.top(), 3, r.height()), QColor("#6aa0ff")); }
      else if (!items_.empty()) { r = items_.back()->geometry(); p.fillRect(QRect(r.right() + 2, r.top(), 3, r.height()), QColor("#6aa0ff")); }
    }
  }
  void dragEnterEvent(QDragEnterEvent* e) override { if (!accepts || accepts(e->mimeData())) e->acceptProposedAction(); else e->ignore(); }
  void dragMoveEvent(QDragMoveEvent* e) override {
    if (accepts && !accepts(e->mimeData())) { e->ignore(); return; }
    e->acceptProposedAction();
    const int m = indexAt(e->position().toPoint());
    if (!marking_ || m != mark_) { marking_ = true; mark_ = m; update(); }
  }
  void dragLeaveEvent(QDragLeaveEvent*) override { marking_ = false; update(); }
  void dropEvent(QDropEvent* e) override {
    marking_ = false;
    update();
    if (dropAt(e->mimeData(), e->position().toPoint())) e->acceptProposedAction(); else e->ignore();
  }

 private:
  int width_, minH_ = 0;
  std::vector<QWidget*> items_;
  QWidget* corner_ = nullptr;
  bool dashed_ = false, marking_ = false;
  int mark_ = -1;
  QColor border_ = QColor("#e8a400"), fill_ = Qt::transparent;
  friend class ToolboxWidget;
};
