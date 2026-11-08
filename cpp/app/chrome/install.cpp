// Entry points of the chrome area: installChrome (called by modules.cpp) builds everything; chromeStart (called by main.cpp after the preferences are read)
// applies the user's theme and shows the welcome page.
#include "chrome.h"
#include "modules.h"
#include "paper.h"
#include "parts.h"
#include "prefs.h"
#include "settings/settings.h"
#include <QDockWidget>

// ---- entry points --------------------------------------------------------------------------------------------------------------------------
chrome::Chrome* chromeOf(MainWindow& w) { return qobject_cast<chrome::Chrome*>(w.property("chromeObject").value<QObject*>()); }

void installChrome(MainWindow& win) {
  using namespace chrome;
  auto* c = new Chrome(win);
  Editor& ed = win.editor();
  c->buildNavBar();
  c->thumbs_ = new ThumbsPanel(*c);
  c->layers_ = new LayersPanel(*c);
  win.addSidePanel("pages", "Páginas", c->thumbs_);
  win.addSidePanel("layers", "Capas", c->layers_);
  if (auto* d = win.findChild<QDockWidget*>("dock")) d->hide();   // closed until the user asks (as the Electron app)
  ed.action("view.sidebar")->setChecked(false);

  c->paperBar_ = new PaperBar(*c, &win);
  win.addToolBarBreak(Qt::TopToolBarArea);
  win.addToolBar(Qt::TopToolBarArea, c->paperBar_);
  c->paperBar_->hide();
  c->find_ = new FindBar(*c, &win);
  win.addToolBarBreak(Qt::TopToolBarArea);
  win.addToolBar(Qt::TopToolBarArea, c->find_);
  c->find_->hide();
  QObject::connect(ed.action("chrome.paper"), &QAction::toggled, c->paperBar_, [c](bool on) { c->paperBar_->setVisible(on); if (on) c->paperBar_->sync(); });
  c->welcome_ = new WelcomePage(*c, win.view());
  c->diag_ = new PenDiagnostic(*c, &win);
  c->installMenus();
  installXppExtras(*c);

  // the Journal's menu entries use the same code as the bar (a special paper keeps its parameters when it is chosen again for the same kind)
  for (const PaperKind& k : paperKinds()) {
    const QString id = k.id;
    ed.addCommand("journal.paper:x:" + id, k.label, {}, [c, id] { c->paperBar()->choosePaper("x:" + id); });
  }
  ed.addCommand("journal.bgColor", "Color de fondo…", {}, [c] {
    c->paperBar()->colorButton()->click();
  });
  c->setTheme("classic", "auto");
  if (SettingsController* sc = SettingsController::of(win))   // the settings area applies a change (dialog, toolbar button, welcome page): the look follows at once, no file round trip
    QObject::connect(sc, &SettingsController::prefsApplied, c, [c, sc] { c->applyPrefs(sc->prefs()); });
  c->updateNavActions();
}

void chromeStart(MainWindow& win, const Prefs& p, bool fileGiven) {
  chrome::Chrome* c = chromeOf(win);
  if (!c) return;
  c->applyPrefs(p);
  win.loadUiState();   // the colour sidebar: on and at the left unless the user chose otherwise
  if (p.welcome && !fileGiven) c->welcome()->showPage();
  // the blank document the window starts with is fitted to the width too, once the (maximised) window has its size: opening a file does it by itself
  if (!fileGiven) QTimer::singleShot(0, c, [c] { if (c->ed.undoDepth() == 0 && c->win.view()) { c->win.view()->zoomFitWidth(); c->win.view()->scrollToPage(0); } });
}
