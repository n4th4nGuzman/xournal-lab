// Opens the options popup of every tool for real (the caret of its toolbar button) and checks that it fits on the screen. The offscreen screen is
// 800 x 800: narrower than a laptop in width (the 817 px wide 3D panel must scroll sideways) and, in the second pass, made to pretend to be 768 px
// high through setPretendScreenHeight (the 725 px figures panel must then scroll). Each pass also scrolls to the end and checks that the last control
// of the tallest panel is reachable.
#include "optionspanel.h"
#include "editor.h"
#include "mainwindow.h"
#include "settings/prefsdialog.h"
#include "settings/settings.h"
#include "settings/tabletdialog.h"
#include <QApplication>
#include <QMenu>
#include <QScreen>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

namespace {
void pump(int n = 6) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
QToolButton* caretButton(MainWindow& win, QAction* a) {
  for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>()) if (b->defaultAction() == a && b->menu()) return b;
  return nullptr;
}
// where the popup opens: under its button, or under the toolbar when the button sits in the overflow
QPoint popupAt(MainWindow& win, QToolButton* b) { return b->isVisible() ? b->mapToGlobal(QPoint(0, b->height())) : win.mapToGlobal(QPoint(40, win.toolBar()->geometry().bottom())); }

using Check = std::function<void(bool, const QString&)>;
using Out = std::function<void(const QString&)>;

void pass(MainWindow& win, Editor& ed, int screenH, const Check& check, const Out& out) {
  setPretendScreenHeight(screenH);
  const QRect avail = win.screen()->availableGeometry();
  const int bottomLimit = screenH > 0 ? std::min(avail.bottom(), screenH - 1) : avail.bottom();
  const QString tag = QString("[%1]").arg(screenH > 0 ? QString("%1 px high").arg(screenH) : QString("real screen"));
  int opened = 0, scrolled = 0;
  for (const ToolInfo* t : ed.toolInfos()) {
    if (!t->inToolbar || !t->options) continue;
    auto* btn = caretButton(win, ed.action("tool." + t->id));
    if (!btn) { check(false, QString("panelfit %1: %2 has no caret menu").arg(tag, t->id)); continue; }
    QMenu* menu = btn->menu();
    menu->popup(popupAt(win, btn));
    pump();
    auto* wa = menu->actions().isEmpty() ? nullptr : qobject_cast<QWidgetAction*>(menu->actions().first());
    auto* sp = wa ? qobject_cast<ScrollingPanel*>(wa->defaultWidget()) : nullptr;
    check(sp != nullptr, QString("panelfit %1: the popup of %2 is a ScrollingPanel").arg(tag, t->id));
    if (!sp) { menu->hide(); continue; }
    const QSize natural = sp->widget()->sizeHint();
    const QRect g = menu->geometry();
    out(QString("  %1 %2: panel %3 x %4, popup %5 x %6 at (%7,%8), %9%10").arg(tag, t->id).arg(natural.width()).arg(natural.height()).arg(g.width()).arg(g.height()).arg(g.x()).arg(g.y())
            .arg(sp->scrollsVertically() ? "scrolls vertically" : "fits").arg(sp->scrollsHorizontally() ? " + horizontally" : ""));
    check(g.top() >= avail.top() && g.bottom() <= bottomLimit && g.left() >= avail.left() && g.right() <= avail.right(),
          QString("panelfit %1: the popup of %2 lies inside the screen (%3,%4 %5x%6)").arg(tag, t->id).arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
    // the last control must be reachable: scroll to the end and look at the lowest child
    if (sp->scrollsVertically() || sp->scrollsHorizontally()) {
      scrolled++;
      sp->verticalScrollBar()->setValue(sp->verticalScrollBar()->maximum());
      sp->horizontalScrollBar()->setValue(sp->horizontalScrollBar()->maximum());
      pump(2);
      const QWidget* last = nullptr;
      int low = -1, right = -1;
      for (QWidget* c : sp->widget()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!c->isVisibleTo(sp->widget())) continue;
        if (c->geometry().bottom() > low) { low = c->geometry().bottom(); last = c; }
        right = std::max(right, c->geometry().right());
      }
      bool reach = last != nullptr;
      if (last) {
        const QPoint br = last->mapTo(sp->viewport(), QPoint(last->width() - 1, last->height() - 1));
        reach = br.y() <= sp->viewport()->height() && br.y() >= 0;
        if (sp->scrollsVertically() && !reach) out(QString("    lowest control ends at %1, viewport %2").arg(br.y()).arg(sp->viewport()->height()));
      }
      check(reach, QString("panelfit %1: scrolled to the end, the lowest control of %2 is inside its viewport").arg(tag, t->id));
    }
    menu->hide();
    pump(2);
    opened++;
  }
  check(opened >= 7, QString("panelfit %1: %2 option popups opened").arg(tag).arg(opened));
  if (screenH > 0) check(scrolled >= 1, QString("panelfit %1: at least one panel needed its scroll bar (%2 did)").arg(tag).arg(scrolled));
}
}  // namespace

void runPanelFitChecks(MainWindow& win, Editor& ed, const Check& check, const Out& out) {
  win.resize(1366, 768);
  win.show();
  pump();
  out(QString("panelfit: test screen %1 x %2").arg(win.screen()->size().width()).arg(win.screen()->size().height()));
  pass(win, ed, 0, check, out);
  pass(win, ed, 768, check, out);
  setPretendScreenHeight(0);
  // the dialogs of the settings area: top-level windows, so what matters is the height they ask for against a 768 px screen (taskbar and title bar take ~70 px)
  if (SettingsController* sc = SettingsController::of(win)) {
    for (QDialog* d : {static_cast<QDialog*>(sc->prefsDialog()), static_cast<QDialog*>(sc->tabletDialog())}) {
      d->adjustSize();
      const QSize h = d->sizeHint(), m = d->minimumSizeHint();
      out(QString("  dialog %1: size hint %2 x %3, minimum %4 x %5, size %6 x %7").arg(d->metaObject()->className()).arg(h.width()).arg(h.height()).arg(m.width()).arg(m.height()).arg(d->width()).arg(d->height()));
      check(m.height() <= 768 - 80, QString("panelfit: the smallest %1 (%2 px high) fits a 768 px screen with its title bar and the taskbar").arg(d->metaObject()->className()).arg(m.height()));
    }
  }
}

// ---- toolbar at laptop widths -------------------------------------------------------------------------------------------------------------------------
void runToolbarFitChecks(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out, const QString& shots) {
  Q_UNUSED(ed);
  win.show();
  pump();
  for (int w : {1920, 1366, 1920, 1280, 1100, 1024, 900, 800, 700}) {
    win.resize(w, 768);
    pump(8);
    QToolBar* tb = win.toolBar();
    QToolBar* sb = win.styleBar();
    int hidden = 0, total = 0;
    for (QAction* a : tb->actions()) { if (a->isSeparator()) continue; total++; QWidget* wd = tb->widgetForAction(a); if (wd && !wd->isVisible()) hidden++; }
    out(QString("toolbar @%1: window %2, tools row %3 x %4 (hint %5), %6 of %7 buttons hidden; style row %8 x %9 (hint %10) at y=%11")
            .arg(w).arg(win.width()).arg(tb->width()).arg(tb->height()).arg(tb->sizeHint().width()).arg(hidden).arg(total).arg(sb->width()).arg(sb->height()).arg(sb->sizeHint().width()).arg(sb->y()));
    for (QToolBar* t : {tb, sb}) if (auto* ext = t->findChild<QToolButton*>("qt_toolbar_ext_button")) out(QString("   %1 chevron: visible=%2 geometry=%3,%4 %5x%6, orientation hint width %7, layout min %8").arg(t->objectName().isEmpty() ? t->windowTitle() : t->objectName()).arg(ext->isVisible()).arg(ext->x()).arg(ext->y()).arg(ext->width()).arg(ext->height()).arg(t->sizeHint().width()).arg(t->minimumSizeHint().width()));
    // from 1920 down to 800 px every button of the tools row is on screen (no chevron needed); at any width the chevron, when shown, lies inside its bar
    if (w >= 800) check(hidden == 0, QString("toolbar @%1: all %2 buttons of the tools row are visible (%3 hidden)").arg(w).arg(total).arg(hidden));
    for (QToolBar* t : {tb, sb}) if (auto* ext = t->findChild<QToolButton*>("qt_toolbar_ext_button"))
      if (ext->isVisible()) check(ext->geometry().right() <= t->width(), QString("toolbar @%1: the overflow chevron of %2 lies inside the bar (right edge %3, bar %4)").arg(w).arg(t->windowTitle()).arg(ext->geometry().right()).arg(t->width()));
    if (w >= 800) for (QAction* a : tb->actions()) { if (QWidget* wd = tb->widgetForAction(a); wd && wd->isVisible()) { if (wd->geometry().right() > tb->width()) { check(false, QString("toolbar @%1: a button sticks out of the tools row").arg(w)); break; } } }
    if (!shots.isEmpty()) win.grab().save(QString("%1/toolbar_%2.png").arg(shots).arg(w));
  }
}
