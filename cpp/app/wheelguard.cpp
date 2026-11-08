#include "wheelguard.h"
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDial>
#include <QEvent>
#include <QSlider>
#include <QWheelEvent>
#include <QWidget>

namespace WheelGuard {
namespace {

bool isValueWidget(const QWidget* w) { return qobject_cast<const QComboBox*>(w) || qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QSlider*>(w) || qobject_cast<const QDial*>(w); }

class Filter : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() != QEvent::Polish && e->type() != QEvent::Wheel) return false;
    auto* w = qobject_cast<QWidget*>(o);
    if (!w) return false;
    if (e->type() == QEvent::Polish) {
      // WheelFocus would let the first turn of the wheel take the focus (and then change the value): the focus comes from a click or Tab only
      if (isValueWidget(w) && w->focusPolicy() != Qt::NoFocus && w->focusPolicy() != Qt::StrongFocus) w->setFocusPolicy(Qt::StrongFocus);
      return false;
    }
    QWidget* target = valueWidgetOf(w);
    if (!target || target->hasFocus() || !target->isEnabled()) return false;
    if (w->window()->windowType() == Qt::Popup) return false;   // the list of an open combo box scrolls by itself
    // not for the control: the wheel goes to the widgets around it, nearest first, until one takes it (normally the viewport of the form's scroll area), as if the control
    // were not there. Walked here because only the window system's own events propagate by themselves.
    auto* we = static_cast<QWheelEvent*>(e);
    bool taken = false;
    for (QWidget* p = target->parentWidget(); p && !taken; p = p->isWindow() ? nullptr : p->parentWidget()) {
      QWheelEvent fwd(QPointF(p->mapFromGlobal(we->globalPosition().toPoint())), we->globalPosition(), we->pixelDelta(), we->angleDelta(), we->buttons(), we->modifiers(), we->phase(),
                      we->inverted(), we->source(), we->pointingDevice());
      fwd.setTimestamp(we->timestamp());
      fwd.setAccepted(false);
      QCoreApplication::sendEvent(p, &fwd);
      taken = fwd.isAccepted();
    }
    e->setAccepted(taken);
    return true;
  }
};

}  // namespace

QWidget* valueWidgetOf(QWidget* w) {
  for (int depth = 0; w && depth < 3; depth++, w = w->parentWidget()) {   // the line edit of a spin box or an editable combo is a child of it
    if (isValueWidget(w)) return w;
    if (w->isWindow()) break;
  }
  return nullptr;
}

void install() {
  static bool done = false;
  if (done || !QCoreApplication::instance()) return;
  done = true;
  qApp->installEventFilter(new Filter(qApp));
}

}  // namespace WheelGuard
