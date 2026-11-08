// Owned by the "mathobjects" area: the real number line, set diagrams (Venn), and the Fill / Riemann tool, with their panels and generated-object handlers.
#include "editor.h"
#include "mainwindow.h"
#include "mathhandlers.h"
#include "mathstate.h"
#include "mathtools.h"
#include "modules.h"
#include "prefs.h"
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

void installMathObjects(MainWindow& win) {
  Editor& ed = win.editor();
  mathobj::registerMathHandlers();
  mathobj::registerMathTools(ed);
  mathobj::registerFillTool(ed);

  // What the user chose for the paint bucket last time (read only: the file is shared with the Electron app; saving belongs to the settings area).
  Prefs p;
  if (loadPrefs(defaultPrefsPath(), &p) && !p.fill.isEmpty()) mathobj::state().fill = sanitizeFill(p.fill);

  // A new line or diagram opens its panel (the expression is what is written next): the caret popup of its toolbar button.
  mathobj::state().openPanel = [&win](const QString& toolId) {
    QTimer::singleShot(0, &win, [&win, toolId] {
      QAction* a = win.editor().action("tool." + toolId);
      for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>())
        if (b->defaultAction() == a && b->menu()) {
          if (!b->menu()->isVisible()) b->menu()->popup(b->mapToGlobal(QPoint(0, b->height())));
          return;
        }
    });
  };
}
