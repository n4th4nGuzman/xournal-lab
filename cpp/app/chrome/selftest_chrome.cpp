// --selftest checks of the chrome area, mirroring the e2e assertions of the Electron app (theme*, welcome*, sidebar/layers, paper*, find*, history,
// presentation) and driving the real widgets: clicks on buttons, key events, menu actions. Synthetic input, not a physical pen or touch screen.
#include "chrome.h"
#include "figures.h"
#include "palettes.h"
#include "parts.h"
#include "prefs.h"
#include "plane.h"
#include "repair.h"
#include "welcomedata.h"
#include "xopp.h"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenuBar>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QMenu>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <algorithm>

namespace chrome {
namespace {
using Check = std::function<void(bool, const QString&)>;

void pump(int n = 4) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
void key(QWidget* w, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, k, m), release(QEvent::KeyRelease, k, m);
  QApplication::sendEvent(w, &press);
  QApplication::sendEvent(w, &release);
}
bool hasInk(const QImage& img) {
  for (int y = 0; y < img.height(); y++)
    for (int x = 0; x < img.width(); x++) { const QColor c = img.pixelColor(x, y); if (c.alpha() > 0 && (c.red() < 254 || c.green() < 254 || c.blue() < 254)) return true; }
  return false;
}
QColor strongest(const QIcon& ic, QIcon::Mode mode = QIcon::Normal, QIcon::State st = QIcon::Off) {
  const QImage img = ic.pixmap(QSize(24, 24), 2.0, mode, st).toImage();
  QColor best;
  int bestA = -1;
  for (int y = 0; y < img.height(); y++)
    for (int x = 0; x < img.width(); x++) { const QColor c = img.pixelColor(x, y); if (c.alpha() > bestA) { bestA = c.alpha(); best = c; } }
  return best;
}
Text textBox(const QString& s, double x, double y) {
  Text t;
  t.attrs = {{"font", "Sans"}, {"size", "14"}, {"x", QString::number(x)}, {"y", QString::number(y)}, {"color", "#000000ff"}};
  t.text = s;
  return t;
}
Stroke lineStroke(double x) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
  s.widths = {8};   // thick enough to show in a 120 px thumbnail
  s.pts = {x, 20, x + 30, 60};
  return s;
}
/** A clean document the check owns: nothing asks about unsaved changes. */
void fresh(MainWindow& win, Editor& ed, Document d) {
  ed.setDocument(std::move(d), QString());
  ed.markSaved();
  pump();
  Q_UNUSED(win);
}
size_t elementsOf(const Document& d, int page, int layer) { return d.pages[size_t(page)].layers[size_t(layer)].elements.size(); }
}  // namespace

void runChromeSelfTest(MainWindow& win, Editor& ed, const TestIO& io) {
  const Check& check = io.check;
  auto out = io.out;
  Chrome* c = chromeOf(win);
  check(c != nullptr, "chrome: installChrome ran");
  if (!c) return;
  c->setPersist(false);   // never write the user's prefs.json from a test
  const QString shots = io.shots.isEmpty() ? QDir::currentPath() : io.shots;
  QDir().mkpath(shots);
  out("\n== chrome checks: themes, welcome, panels, journal, find, history, presentation ==");
  // A document of eight pages with content on every page, built here so the checks do not depend on what the earlier checks left open.
  Document sampleDoc = blankDocument();
  for (int i = 1; i < 8; i++) sampleDoc.pages.push_back(sampleDoc.pages[0]);
  for (size_t i = 0; i < sampleDoc.pages.size(); i++)
    for (int k = 0; k < 6; k++) sampleDoc.pages[i].layers[0].elements.push_back(lineStroke(40.0 * k + 5.0 * double(i)));
  fresh(win, ed, sampleDoc);

  // ---- themes ----------------------------------------------------------------------------------------------------------------------------
  {
    check(c->theme() == "classic", "theme: starts classic");
    const QColor penClassic = strongest(ed.action("tool.pen")->icon());
    c->showSidebar("pages");
    pump();
    win.grab().save(shots + "/chrome_classic.png");
    c->setTheme("neon", "full");
    pump();
    check(c->theme() == "neon" && win.property("chromeTheme").toString() == "neon", "theme: switches to neon live");
    check(qApp->palette().color(QPalette::Window) == QColor("#170c36") && qApp->styleSheet().contains("#ff2a9d"), "theme: neon palette and style sheet are applied");
    const QColor penNeon = strongest(ed.action("tool.pen")->icon());
    check(penNeon != penClassic && penNeon.red() > 200 && penNeon.green() < 120, QString("theme: neon changes the icon colour (pen %1 -> %2)").arg(penClassic.name(), penNeon.name()));
    check(strongest(ed.action("file.save")->icon()) != strongest(ed.action("tool.select")->icon()), "theme: neon icons have their own colours by function");
    check(win.view()->deskStyle().gradTop.isValid() && win.view()->deskStyle().frame.isValid() && win.view()->deskStyle().glow.isValid(), "theme: neon desk gradient, page outline and halo (effects full)");
    const RenderOptions& ro = win.view()->renderBase();
    check(!ro.darkPaper && ro.paperColorOverride.isEmpty() && ro.glow, "theme: the page stays white in neon (as the Electron app), glow halos of strokes stay on");
    {   // the page really is white at the centre of the first page
      win.view()->renderVisibleNow();
      const QImage snap = win.view()->snapshot();
      const QRectF r = win.view()->pageRectOnScreen(ed.currentPage());
      int whites = 0, n = 0;
      for (int y = int(r.top()) + 20; y < int(r.bottom()) - 20 && y < snap.height(); y += 40)
        for (int x = int(r.left()) + 20; x < int(r.right()) - 20 && x < snap.width(); x += 40) { n++; if (snap.pixelColor(x, y) == QColor(Qt::white)) whites++; }
      check(n > 0 && whites * 3 > n, QString("theme: neon page pixels are white (%1 of %2 samples)").arg(whites).arg(n));
    }
    win.grab().save(shots + "/chrome_neon.png");
    c->setTheme("neon", "reduced");
    check(c->reducedEffects() && !win.view()->deskStyle().glow.isValid() && win.view()->deskStyle().frame.isValid(), "theme: reduced effects drop the halos but keep the outline");
    check(strongest(ed.action("tool.pen")->icon()).red() > 200, "theme: reduced effects keep the neon colours");
    check(resolveEffects("full") == "full" && resolveEffects("reduced") == "reduced" && (resolveEffects("auto") == "full" || resolveEffects("auto") == "reduced"), "theme: effects auto resolves from the machine");
    Prefs p;
    p.theme = "neon"; p.effects = "full"; p.palette = "neondreams"; p.welcome = false;
    c->applyPrefs(p);
    check(c->theme() == "neon" && !c->reducedEffects() && !c->showWelcomeOnStart(), "theme: applyPrefs sets theme, effects and welcome from prefs");
    check(c->paperBar()->swatches().size() >= 5 && c->paperBar()->swatches()[0]->property("rgb").toString() == paletteById("neondreams").papers.value(0).toLower(), "theme: the paper swatches follow the palette pref");
    c->setTheme("classic", "auto");
    pump();
    check(c->theme() == "classic" && qApp->palette().color(QPalette::Window) == QColor("#2b2b2b") && !win.view()->deskStyle().gradTop.isValid(), "theme: back to classic restores palette and desk");
    check(strongest(ed.action("tool.pen")->icon()) == penClassic, "theme: classic icons are the originals again");
    c->applyPrefs(Prefs());   // defaults: palette classic, welcome on
  }

  // ---- welcome page ----------------------------------------------------------------------------------------------------------------------
  {
    QTemporaryDir tmp;
    WelcomePage* w = c->welcome();
    static QStringList recent;   // static: the page keeps these callbacks after the block, and the later blocks show it again
    static bool cleared;
    recent.clear();
    cleared = false;
    w->recentSource = [&] { return recent; };
    w->clearRecent = [&] { recent.clear(); cleared = true; };
    ed.markSaved();
    w->showPage();
    pump();
    check(w->isShown() && w->button("wNew") && w->button("wOpen") && w->button("wAutosave"), "welcome: shown, with the three start links");
    {   // a pen on the welcome page: the view below must not draw (it used to take the tablet event, so Qt never turned it into a click)
      static QPointingDevice* pen = new QPointingDevice("chrome pen", 311, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
      QPushButton* b = w->button("wNew");
      const QPointF at = b->rect().center();
      const size_t before = countOf(ed.doc()).strokes;
      QTabletEvent down(QEvent::TabletPress, pen, at, b->mapToGlobal(at.toPoint()), 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
      down.ignore();
      QApplication::sendEvent(b, &down);
      QTabletEvent move(QEvent::TabletMove, pen, at + QPointF(30, 5), b->mapToGlobal((at + QPointF(30, 5)).toPoint()), 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::NoButton, Qt::LeftButton);
      QApplication::sendEvent(b, &move);
      QTabletEvent up(QEvent::TabletRelease, pen, at, b->mapToGlobal(at.toPoint()), 0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
      QApplication::sendEvent(b, &up);
      pump();
      check(!down.isAccepted() && countOf(ed.doc()).strokes == before, "welcome: a pen press on its buttons is left unaccepted (Qt makes the click) and the page below draws nothing");
    }
    const int nPapers = int(startPapers([] { std::vector<std::pair<QString, QString>> v; for (const PaperKind& k : paperKinds()) v.emplace_back(k.id, k.label); return v; }()).size());
    check(w->paperCardCount() == nPapers && nPapers >= 11, QString("welcome: one paper card per kind (%1)").arg(nPapers));
    int withInk = 0;
    for (const StartPaper& sp : startPapers([] { std::vector<std::pair<QString, QString>> v; for (const PaperKind& k : paperKinds()) v.emplace_back(k.id, k.label); return v; }())) {
      if (sp.value == "plain") continue;
      const QImage img = paperThumbnail(sp.value, 66, 1.0);
      if (hasInk(img)) withInk++; else { int lo = 255; for (int y = 0; y < img.height(); y++) for (int x = 0; x < img.width(); x++) lo = std::min(lo, img.pixelColor(x, y).red()); out(QString("  thumbnail %1: darkest pixel %2").arg(sp.value).arg(lo)); }
    }
    check(withInk == nPapers - 1, QString("welcome: every paper thumbnail (except plain) is drawn with real ink (%1 of %2)").arg(withInk).arg(nPapers - 1));
    check(w->recentPaths().isEmpty() && !w->findChildren<QPushButton*>("wClear").size(), "welcome: no recents, no clear button");
    win.grab().save(shots + "/chrome_welcome_classic.png");
    c->setTheme("neon", "full");
    pump();
    win.grab().save(shots + "/chrome_welcome_neon.png");
    c->setTheme("classic", "auto");

    // recent files: a real file listed, opened by clicking it
    const QString file = tmp.filePath("reciente.xopp");
    saveXopp(file, sampleDoc, nullptr);
    recent = {file, tmp.filePath("missing.xopp")};
    w->showPage();
    pump();
    check(w->recentPaths() == QStringList{file}, "welcome: lists the recent file that exists and skips the missing one");
    w->findChild<QPushButton*>("wRecent:1")->click();
    pump();
    check(!w->isShown() && ed.pageCount() == int(sampleDoc.pages.size()) && countOf(ed.doc()).strokes == countOf(sampleDoc).strokes, "welcome: a recent file opens and the page closes");
    w->showPage();
    check(w->button("wClear") != nullptr, "welcome: the clear button appears with recents");
    w->button("wClear")->click();
    pump(8);
    check(cleared && w->recentPaths().isEmpty(), "welcome: clearing the list empties it");
    // closing
    key(w, Qt::Key_Escape);
    check(!w->isShown(), "welcome: Esc closes the overlay");
    w->showPage();
    w->findChild<QPushButton*>("wClose")->click();
    check(!w->isShown(), "welcome: the cross closes the overlay");
    // help menu entry
    ed.action("help.welcome")->trigger();
    check(w->isShown(), "welcome: Ayuda > Bienvenida shows it");
    // the start-up preference
    QCheckBox* show = w->findChild<QCheckBox*>("wShow");
    const bool before = show->isChecked();
    show->setChecked(!before);
    check(c->showWelcomeOnStart() == !before, "welcome: the show-on-start box changes the preference");
    show->setChecked(before);
    // theme toggle
    const QString th = c->theme();
    w->button("wTheme")->click();
    pump();
    check(c->theme() != th, "welcome: the theme link toggles the theme");
    w->button("wTheme")->click();
    pump();
    check(c->theme() == th, "welcome: and toggles it back");
    // papers
    w->showPage();
    ed.markSaved();
    static_cast<QPushButton*>(w->card("x:log"))->click();
    pump();
    {
      const Attrs& bg = ed.doc().pages[0].background;
      check(!w->isShown() && attrOr(bg, "xlPaper") == "log;base=10;axes=y;cycles=5" && attrOr(bg, "style") == "graph" && ed.pageCount() == 1 && !ed.dirty() && ed.undoDepth() == 0,
            "welcome: a special paper card starts a clean document on that paper (xlPaper + look-alike style)");
      QString err;
      const QString f2 = tmp.filePath("log.xopp");
      saveXopp(f2, ed.doc(), &err);
      const LoadResult back = loadXopp(f2);
      check(back.ok && paperOfBackground(back.doc.pages[0].background) && paperOfBackground(back.doc.pages[0].background)->kind == "log", "welcome: the special paper survives save and load");
    }
    w->showPage();
    static_cast<QPushButton*>(w->card("graph"))->click();
    pump();
    check(attrOr(ed.doc().pages[0].background, "style") == "graph" && !attrFind(ed.doc().pages[0].background, "xlPaper"), "welcome: a built-in paper card sets only the style");
    w->showPage();
    static_cast<QPushButton*>(w->card("plain"))->click();
    pump();
    check(attrOr(ed.doc().pages[0].background, "style") == "plain" && countOf(ed.doc()).strokes == 0, "welcome: the plain card is a blank document");
    w->showPage();
    w->button("wNew")->click();
    pump();
    check(!w->isShown() && ed.pageCount() == 1, "welcome: new document link");
    w->hidePage();
  }

  // ---- thumbnails and navigation history (on the real sample) ----------------------------------------------------------------------------
  fresh(win, ed, sampleDoc);
  {
    ThumbsPanel* t = c->thumbs();
    c->showSidebar("pages");
    pump();
    const int n = ed.pageCount();
    check(win.findChild<QDockWidget*>("dock")->isVisible() && c->sidebarShows("pages") && t->list()->count() == n, QString("sidebar: pages panel shown with one row per page (%1)").arg(n));
    QElapsedTimer tm;
    tm.start();
    win.grab().save(shots + "/chrome_sidebar_pages.png");   // paints the visible rows: they ask for their pictures
    const int visibleAsked = t->renderPending();
    pump();
    check(t->cached(0).size().width() > 0 && hasInk(t->cached(0).toImage()), "sidebar: the first thumbnail is rendered with real content");
    out(QString("  sidebar: first paint asked for %1 thumbnails (of %2 pages) in %3 ms").arg(visibleAsked).arg(n).arg(tm.elapsed()));
    {   // lazy: a long document renders only the rows that are painted
      Document longDoc = blankDocument();
      for (int i = 0; i < 59; i++) longDoc.pages.push_back(longDoc.pages[0]);
      const int before = t->renderedTotal();
      ed.setDocument(longDoc, QString());
      ed.markSaved();
      pump(20);
      win.grab();
      t->renderPending();
      pump();
      const int made = t->renderedTotal() - before;
      check(made > 0 && made < 20, QString("sidebar: thumbnails are lazy (%1 of 60 pages rendered after the first paint)").arg(made));
      ed.setDocument(sampleDoc, QString());
      ed.markSaved();
      pump(8);
    }
    c->resetHistory();
    ed.setCurrentPage(0);
    const int target = std::min(2, n - 1);
    t->list()->itemClicked(t->list()->item(target));
    pump();
    check(ed.currentPage() == target && c->historyBackCount() == (target != 0 ? 1 : 0), "sidebar: clicking a thumbnail goes to that page and records history");
    // history
    ed.action("nav.back")->trigger();
    check(ed.currentPage() == 0 && c->historyForwardCount() == 1, "history: Atrás returns to the previous page");
    ed.action("nav.forward")->trigger();
    check(ed.currentPage() == target && c->historyBackCount() == 1 && c->historyForwardCount() == 0, "history: Adelante goes on");
    ed.action("nav.back")->trigger();
    c->goToPage(std::min(1, n - 1));
    check(c->historyForwardCount() == 0 && ed.action("nav.forward")->isEnabled() == false && ed.action("nav.back")->isEnabled(), "history: a new jump clears the forward list; the actions follow");
    c->navBar()->findChild<QLineEdit*>("pgNum")->setText(QString::number(std::min(3, n)));
    emit c->navBar()->findChild<QLineEdit*>("pgNum")->editingFinished();
    check(ed.currentPage() == std::min(3, n) - 1, "navigation: the page number box goes to that page");
    ed.action("nav.last")->trigger();
    check(ed.currentPage() == n - 1 && c->navBar()->findChild<QLabel*>("pgTotal")->text() == QString::number(n), "navigation: last page; the total is shown");
    ed.action("nav.first")->trigger();
    check(ed.currentPage() == 0, "navigation: first page");
    ed.setDocument(sampleDoc, QString());
    check(c->historyBackCount() == 0, "history: opening a document resets it");
    ed.markSaved();
    // page buttons act on the current page and are undoable
    const int n0 = ed.pageCount();
    t->tool("add")->click();
    check(ed.pageCount() == n0 + 1 && t->list()->count() == n0 + 1, "sidebar: the plus button adds a page after the current one (list follows)");
    ed.undo();
    check(ed.pageCount() == n0 && t->list()->count() == n0, "sidebar: undo removes it");
    ed.setCurrentPage(1);
    t->tool("down")->click();
    check(ed.doc().pages.size() == size_t(n0), "sidebar: move down keeps the page count");
    ed.undo();
    t->tool("delete")->click();
    check(ed.pageCount() == n0 - 1, "sidebar: the trash button deletes the current page");
    ed.undo();
    check(ed.pageCount() == n0 && t->list()->count() == n0, "sidebar: undo of a delete brings the page back");
    ed.markSaved();
  }

  // ---- layers panel (a document the check builds) ------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    fresh(win, ed, d);
    for (int i = 0; i < 3; i++) ed.addElement(0, 0, Element(lineStroke(10.0 * i)));
    layerops::add(ed, 0);
    layerops::add(ed, 0);
    ed.addElement(0, 2, Element(lineStroke(100)));
    ed.markSaved();
    LayersPanel* lp = c->layers();
    c->showSidebar("layers");
    lp->refresh();
    pump();
    check(c->sidebarShows("layers") && lp->list()->count() == 3, "layers: three layers listed");
    check(lp->layerOfRow(0) == 2 && lp->layerOfRow(2) == 0, "layers: the top layer is listed first");
    check(ed.activeLayer(0) == 2, "layers: the new layer became the active one");
    win.grab().save(shots + "/chrome_sidebar_layers.png");
    // active + hide
    lp->setActive(0);
    check(ed.activeLayer(0) == 0, "layers: clicking sets the active layer");
    lp->toggleHidden(1);
    check(!ed.layerVisible(0, 1), "layers: the eye hides a layer (view state only)");
    // add below hidden: indices shift
    ed.setActiveLayer(0, 0);
    ed.markSaved();
    const int depth = ed.undoDepth();
    lp->tool("add")->click();
    pump(8);
    check(lp->list()->count() == 4 && ed.activeLayer(0) == 1 && ed.hiddenLayers(0) == std::vector<int>{2}, "layers: add puts a layer above the active one and the hidden layer's index follows it");
    check(ed.undoDepth() == depth + 1, "layers: adding is one undo step");
    ed.undo();
    check(ed.doc().pages[0].layers.size() == 3 && ed.hiddenLayers(0) == std::vector<int>{1} && ed.activeLayer(0) == 0, "layers: undo restores the layers, the hidden one and the active one");
    ed.redo();
    check(ed.doc().pages[0].layers.size() == 4 && ed.hiddenLayers(0) == std::vector<int>{2}, "layers: redo");
    ed.undo();
    lp->scheduleRefresh();
    pump();
    // rename
    check(layerName(ed.doc().pages[0].layers[0], 0) == "Capa 1", "layers: default names count from the bottom");
    QListWidgetItem* row = lp->list()->item(2);   // layer 0
    row->setText("Dibujo");
    pump();
    check(attrOr(ed.doc().pages[0].layers[0].attrs, "name") == "Dibujo" && lp->list()->item(2)->text() == "Dibujo", "layers: editing a name renames the layer");
    ed.undo();
    check(!attrFind(ed.doc().pages[0].layers[0].attrs, "name"), "layers: undo of a rename removes the saved name again");
    ed.redo();
    layerops::rename(ed, 0, 0, "  ");
    check(!attrFind(ed.doc().pages[0].layers[0].attrs, "name"), "layers: an empty name removes the saved name");
    ed.undo();
    // move
    const size_t topCount = elementsOf(ed.doc(), 0, 2), bottomCount = elementsOf(ed.doc(), 0, 0);
    ed.setActiveLayer(0, 2);
    lp->tool("down")->click();
    check(elementsOf(ed.doc(), 0, 1) == topCount && ed.activeLayer(0) == 1, "layers: move down swaps the layer (the active one follows it)");
    ed.undo();
    check(elementsOf(ed.doc(), 0, 2) == topCount && elementsOf(ed.doc(), 0, 0) == bottomCount, "layers: undo of a move");
    // delete with content
    ed.setActiveLayer(0, 0);
    lp->tool("delete")->click();
    check(ed.doc().pages[0].layers.size() == 2, "layers: delete removes the active layer with its content");
    ed.undo();
    check(ed.doc().pages[0].layers.size() == 3 && elementsOf(ed.doc(), 0, 0) == bottomCount && ed.hiddenLayers(0) == std::vector<int>{1}, "layers: undo brings the strokes and the hidden flag back");
    // the last layer cannot be deleted
    Document one = blankDocument();
    fresh(win, ed, one);
    check(!layerops::remove(ed, 0, 0) && ed.doc().pages[0].layers.size() == 1, "layers: a page keeps at least one layer");
    // visibility affects the render: a hidden layer is not drawn in the thumbnail
    fresh(win, ed, blankDocument());
    ed.addElement(0, 0, Element(lineStroke(60)));
    ed.setLayerVisible(0, 0, false);
    c->thumbs()->invalidate(-1);
    for (QElapsedTimer w; !w.isValid() || w.elapsed() < 400;) { if (!w.isValid()) w.start(); pump(2); }
    c->thumbs()->request(0);
    c->thumbs()->renderPending();
    check(!hasInk(c->thumbs()->cached(0).toImage()), "layers: a hidden layer is not drawn in the page thumbnail");
    ed.setLayerVisible(0, 0, true);
    c->thumbs()->invalidate(-1);
    for (QElapsedTimer w; !w.isValid() || w.elapsed() < 400;) { if (!w.isValid()) w.start(); pump(2); }   // the picture is dropped after a short delay (a stroke is many events)
    c->thumbs()->request(0);
    c->thumbs()->renderPending();
    check(hasInk(c->thumbs()->cached(0).toImage()), "layers: and it is drawn again when shown");
  }

  // ---- journal / paper bar -----------------------------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    fresh(win, ed, d);
    ed.insertPage(1, 0);
    ed.insertPage(2, 0);
    ed.markSaved();
    PaperBar* pb = c->paperBar();
    ed.action("chrome.paper")->setChecked(true);
    pump();
    check(pb->isVisible(), "paper: the toolbar button opens the paper bar");
    check(pb->paperCombo()->count() == int(paperStyles().size()) + int(paperKinds().size()) + 1 && pb->formatCombo()->count() == int(paperFormats().size()), "paper: the lists offer every style, special paper and format");
    check(pb->paperCombo()->currentData().toString() == "plain" && pb->formatCombo()->currentData().toString() == "A4", "paper: shows the page's own paper and format");
    pb->setPaletteId("classic");
    c->goToPage(0, false);
    pump();
    const int d0 = ed.undoDepth();
    pb->choosePaper("graph");
    out(QString("  paper debug: style0=%1 style1=%2 depth %3 -> %4").arg(attrOr(ed.doc().pages[0].background, "style"), attrOr(ed.doc().pages[1].background, "style")).arg(d0).arg(ed.undoDepth()));
    check(attrOr(ed.doc().pages[0].background, "style") == "graph" && attrOr(ed.doc().pages[1].background, "style") == "plain" && ed.undoDepth() == d0 + 1, "paper: a style applies to the current page only, as one undo step");
    {   // the page on screen really changes: the tiles are drawn again with the new paper
      win.view()->renderVisibleNow();
      pump();
      win.view()->renderVisibleNow();
      const QImage snap = win.view()->snapshot();
      const QRectF r = win.view()->pageRectOnScreen(0);
      int grey = 0;
      for (int y = int(r.top()) + 10; y < int(r.bottom()) - 10 && y < snap.height(); y += 3)
        for (int x = int(r.left()) + 10; x < int(r.right()) - 10 && x < snap.width(); x += 3) { const QColor px = snap.pixelColor(x, y); if (px != QColor(Qt::white)) grey++; }
      check(grey > 100, QString("paper: the visible page is drawn with the new paper at once (%1 ruled pixels)").arg(grey));
    }
    ed.undo();
    check(attrOr(ed.doc().pages[0].background, "style") == "plain", "paper: undo");
    pb->applyAll()->setChecked(true);
    check(ed.action("journal.applyAll")->isChecked(), "paper: the 'all pages' box and the menu entry are one switch");
    pb->choosePaper("dotted");
    bool all = true;
    for (const Page& p : ed.doc().pages) all = all && attrOr(p.background, "style") == "dotted";
    check(all, "paper: 'all pages' applies the style to every page");
    ed.undo();
    pb->applyAll()->setChecked(false);
    pb->choosePaper("x:log");
    {
      const Attrs& bg = ed.doc().pages[0].background;
      check(attrOr(bg, "xlPaper") == "log;base=10;axes=y;cycles=5" && attrOr(bg, "style") == "graph", "paper: a special paper writes xlPaper and a close built-in look");
      check(pb->paperCombo()->currentData().toString() == "x:log" && pb->paramsBox()->findChildren<QComboBox*>().size() == 3, "paper: the select shows it and its three parameters appear");
    }
    QComboBox* base = nullptr;
    for (QComboBox* cb : pb->paramsBox()->findChildren<QComboBox*>()) if (cb->property("param").toString() == "base") base = cb;
    check(base != nullptr, "paper: the log paper has a base parameter");
    if (base) {
      base->setCurrentIndex(base->findData("e"));
      emit base->activated(base->currentIndex());
      check(attrOr(ed.doc().pages[0].background, "xlPaper").contains("base=e"), "paper: changing a parameter rewrites xlPaper");
      check(pb->paramsBox()->findChildren<QComboBox*>().size() == 3, "paper: the parameters stay after the change");
      ed.undo();
      check(attrOr(ed.doc().pages[0].background, "xlPaper").contains("base=10"), "paper: undo of a parameter");
    }
    pb->choosePaper("x:log");   // same kind again keeps its parameters
    ed.action("journal.paper:x:polar")->trigger();
    check(attrOr(ed.doc().pages[0].background, "xlPaper").startsWith("polar"), "paper: the Journal menu entry of a special paper uses the same path");
    pb->choosePaper("lined");
    check(!attrFind(ed.doc().pages[0].background, "xlPaper") && attrOr(ed.doc().pages[0].background, "style") == "lined" && pb->paramsBox()->findChildren<QComboBox*>().isEmpty(), "paper: a built-in style removes the special paper and its parameters");
    // spacing and opacity of the lines
    {
      pb->setSpacing(10);
      check(std::abs(paperSpacingOf(ed.doc().pages[0].background) - 10 * kMmToPt) < 0.01, "paper: the spacing of a built-in style is saved on the background");
      ed.undo();
      check(paperSpacingOf(ed.doc().pages[0].background) == 0, "paper: undo of the spacing");
      pb->setSpacing(10);
      pb->setOpacity(40);
      check(std::abs(paperOpacityOf(ed.doc().pages[0].background) - 0.4) < 1e-9 && pb->opacityPct()->isEnabled(), "paper: the opacity is saved on the background");
      pb->choosePaper("graph");
      check(paperSpacingOf(ed.doc().pages[0].background) == 0 && std::abs(paperOpacityOf(ed.doc().pages[0].background) - 0.4) < 1e-9,
            "paper: changing the style resets the spacing and keeps the opacity");
      pb->choosePaper("x:log");
      pb->setSpacing(10);
      check(paperSpacingOf(ed.doc().pages[0].background) == 0 && !pb->spacingMm()->isVisibleTo(pb), "paper: special papers have no spacing (their parameters own it)");
      pb->setOpacity(100);
      check(!attrFind(ed.doc().pages[0].background, "xlOpacity"), "paper: full opacity removes the attribute");
      pb->choosePaper("lined");
      Attrs junk;
      attrSet(junk, "xlSpacing", "nan");
      attrSet(junk, "xlOpacity", "-3");
      check(paperSpacingOf(junk) == 0 && paperOpacityOf(junk) == 1, "paper: unusable spacing and opacity values are ignored");
      QImage a(200, 200, QImage::Format_ARGB32), b(200, 200, QImage::Format_ARGB32), c(200, 200, QImage::Format_ARGB32);
      a.fill(Qt::white); b.fill(Qt::white); c.fill(Qt::white);
      { QPainter p(&a); drawBuiltinStyle(p, 200, 200, "graph"); }
      { QPainter p(&b); drawBuiltinStyle(p, 200, 200, "graph", 40); }
      { QPainter p(&c); p.setOpacity(0.3); drawBuiltinStyle(p, 200, 200, "graph"); }
      check(a != b && a != c && qGray(c.pixel(0, 0)) > qGray(a.pixel(0, 0)), "paper: spacing moves the lines and a lower opacity makes them paler");
    }
    // zoom: the fit to width follows the view until another zoom is chosen, and the zoom reaches 2000 %
    {
      DocumentView& dv = *win.view();
      ed.action("view.fitWidth")->trigger();
      const double fitted = dv.zoom();
      check(dv.autoFitWidth(), "zoom: 'fit to width' stays on");
      win.resize(win.width() - 160, win.height());
      QApplication::processEvents();
      check(dv.autoFitWidth() && std::abs(dv.zoom() - fitted) > 1e-3, "zoom: the fit follows a narrower window");
      dv.zoomBy(1.1);
      check(!dv.autoFitWidth(), "zoom: a manual zoom ends the automatic fit");
      dv.setZoom(100);
      check(std::abs(dv.zoom() - 20.0) < 1e-6, "zoom: the maximum is 2000 %");
      ed.action("view.fitWidth")->trigger();
      win.resize(win.width() + 160, win.height());
      QApplication::processEvents();
    }
    // colour
    check(pb->swatches().size() >= 5, "paper: swatches follow the palette");
    pb->swatches().at(1)->click();
    check(attrOr(ed.doc().pages[0].background, "color").startsWith(pb->swatches().at(1)->property("rgb").toString()), "paper: a swatch sets the paper colour");
    ed.undo();
    // format
    pb->formatCombo()->setCurrentIndex(pb->formatCombo()->findData("A5"));
    emit pb->formatCombo()->activated(pb->formatCombo()->currentIndex());
    check(std::abs(ed.doc().pages[0].width() - paperFormat("A5")->width) < 0.01 && std::abs(ed.doc().pages[1].width() - paperFormat("A4")->width) < 0.01, "paper: a format resizes the current page");
    ed.undo();
    ed.action("journal.rotate")->trigger();
    check(ed.doc().pages[0].width() > ed.doc().pages[0].height(), "paper: rotate swaps width and height");
    pb->formatCombo()->setCurrentIndex(pb->formatCombo()->findData("A5"));
    emit pb->formatCombo()->activated(pb->formatCombo()->currentIndex());
    check(ed.doc().pages[0].width() > ed.doc().pages[0].height() && std::abs(ed.doc().pages[0].width() - paperFormat("A5")->height) < 0.01, "paper: a format keeps the page's orientation");
    pb->widthMm()->setValue(100);
    pb->heightMm()->setValue(150);
    pb->applyCustomSize();
    check(std::abs(ed.doc().pages[0].width() - 100 * kMmToPt) < 0.01 && std::abs(ed.doc().pages[0].height() - 150 * kMmToPt) < 0.01, "paper: a custom size in millimetres");
    check(pb->formatCombo()->currentIndex() == -1, "paper: a custom size matches no format");
    ed.undo(); ed.undo(); ed.undo();
    // new page like the current one
    pb->choosePaper("graph");
    ed.setCurrentPage(0);
    ed.action("journal.newPage")->trigger();
    check(ed.pageCount() == 4 && attrOr(ed.doc().pages[1].background, "style") == "graph", "journal: a new page is shaped like the current one (paper included)");
    ed.undo(); ed.undo();
    pb->choosePaper("x:cornell");
    win.grab().save(shots + "/chrome_paperbar.png");
    ed.action("chrome.paper")->setChecked(false);
    check(!pb->isVisible(), "paper: the button closes the bar");
  }

  // ---- find ------------------------------------------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    fresh(win, ed, d);
    ed.insertPage(1, 0);
    ed.insertPage(2, 0);
    ed.addElement(0, 0, Element(textBox("Hola mundo", 60, 100)));
    ed.addElement(0, 0, Element(lineStroke(10)));
    ed.addElement(1, 0, Element(textBox("otra hola", 60, 700)));
    ed.addElement(2, 0, Element(textBox("HOLA al final", 60, 400)));
    ed.addElement(2, 0, Element(textBox("nada", 60, 500)));
    ed.markSaved();
    FindBar* f = c->findBar();
    ed.action("edit.find")->trigger();
    pump();
    check(f->isOpen(), "find: Ctrl+F (edit.find) opens the bar");
    f->setQuery("hola");
    f->run();
    check(f->matches() == 3 && f->countText() == "1 de 3", "find: case-insensitive matches in page order (1 de 3)");
    f->step(1);
    check(f->position() == 1 && f->countText() == "2 de 3" && c->win.view()->visiblePages().size() > 0, "find: next goes to the second match");
    f->step(1); f->step(1);
    check(f->position() == 0, "find: stepping wraps around");
    f->step(-1);
    check(f->position() == 2, "find: previous wraps backwards");
    {
      const auto vis = win.view()->visiblePages();
      check(std::find(vis.begin(), vis.end(), 2) != vis.end(), "find: the page of the match is scrolled into view");
      check(ed.tool() == "select" || !ed.hasTool("select"), "find: switches to the select tool (as the Electron app)");
    }
    f->caseBox()->setChecked(true);
    f->setQuery("hola");
    f->run();
    check(f->matches() == 1, "find: 'distinguir mayúsculas' narrows the matches (only \"otra hola\")");
    f->caseBox()->setChecked(false);
    ed.setLayerVisible(0, 0, false);
    f->run();
    check(f->matches() == 2, "find: hidden layers are not searched");
    ed.setLayerVisible(0, 0, true);
    f->setQuery("zzz");
    f->run();
    check(f->matches() == 0 && f->countText() == "Sin resultados", "find: no results message");
    f->setQuery("hola");
    f->run();
    win.view()->scrollToPage(0);
    pump();
    win.grab().save(shots + "/chrome_find.png");
    key(f->input(), Qt::Key_Escape);
    check(!f->isOpen() && f->matches() == 0, "find: Esc closes the bar and forgets the matches");
  }

  // ---- presentation ----------------------------------------------------------------------------------------------------------------------
  {
    check(!c->presenting() && win.toolBar()->isVisible(), "presentation: starts off with the toolbar visible");
    ed.action("view.presentation")->trigger();
    pump();
    check(c->presenting() && !win.toolBar()->isVisible() && !c->navBar()->isVisible() && !win.titleBar()->isVisible(), "presentation: hides the bars (title bar included)");
    QPushButton* chip = win.view()->findChild<QPushButton*>("presentChip");
    check(chip && chip->isVisible(), "presentation: shows a way out");
    key(win.view(), Qt::Key_Escape);
    check(!c->presenting() && win.toolBar()->isVisible() && c->navBar()->isVisible() && win.titleBar()->isVisible(), "presentation: Esc leaves it and restores the bars");
    ed.action("view.presentation")->trigger();
    chip->click();
    check(!c->presenting(), "presentation: the chip leaves it");
  }

  // ---- pen diagnostic ------------------------------------------------------------------------------------------------------------------------
  {
    std::vector<PenSample> ints, fracs;
    for (int i = 0; i < 50; i++) {
      ints.push_back({"pointermove", "pen", 100.0 + i, 50.0 + i, 0.3 + 0.01 * i, 1000.0 + 8 * i});
      fracs.push_back({"pointermove", "pen", 100.37 + i * 1.13, 50.61 + i * 0.9, 0.5, 1000.0 + 8 * i});
    }
    const PenReport a = analyzePen(ints, 1.0), b = analyzePen(fracs, 1.0), e = analyzePen({}, 1.0);
    check(a.verdict.contains("ENTERAS") && b.verdict.contains("DECIMALES") && e.verdict.startsWith("Sin datos"), "diagnostic: tells whole-pixel input from sub-pixel input from no data");
    check(a.samples == 50 && a.medianIntervalMs == 8 && a.eventsPerSecond == 125 && a.distinctPressures == 50 && b.distinctPressures == 1, "diagnostic: interval, rate and pressure levels");
    check(analyzePen(ints, 1.5).verdict.contains("DECIMALES"), "diagnostic: whole logical pixels at 150 % scale are fractional physical pixels");
    ed.action("help.pen")->trigger();
    pump();
    auto* dlg = win.findChild<PenDiagnostic*>("penDlg");
    check(dlg && dlg->isVisible() && dlg->reportText().contains("Dibuja"), "diagnostic: Ayuda > Diagnóstico del lápiz opens the dialog");
    dlg->feed(fracs);
    check(dlg->reportText().contains("Muestras: 50"), "diagnostic: the report shows the samples");
    dlg->close();
  }

  // ---- repair on open --------------------------------------------------------------------------------------------------------------------
  {
    QTemporaryDir tmp;
    PlaneSpec ps = DEFAULT_PLANE;
    ps.xlabel = "x";
    Layer l;
    l.elements = buildPlane("pl123456", ps, Frame{50, 100, 350, 400}, GenStyle{"#cc2288", 1.41, std::nullopt});
    for (Element& e : l.elements)
      if (auto* t = std::get_if<Text>(&e)) for (const char* k : {"x", "y", "size", "font", "color"}) t->attrs.erase(std::remove_if(t->attrs.begin(), t->attrs.end(), [&](const auto& kv) { return kv.first == k; }), t->attrs.end());
    Document d = blankDocument();
    d.pages[0].layers[0] = l;
    const QString f = tmp.filePath("legacy.xopp");
    saveXopp(f, d, nullptr);
    fresh(win, ed, blankDocument());
    check(win.openFile(f), "repair: the legacy file opens");
    check(win.infoText().contains("Se rehicieron 1 objeto"), "repair: the message area says how many objects were rebuilt");   // there is no status bar (Electron's #info)
    bool placed = true;
    for (const Element& e : ed.doc().pages[0].layers[0].elements) if (const auto* t = std::get_if<Text>(&e)) placed = placed && attrFind(t->attrs, "x") && attrFind(t->attrs, "y");
    check(placed, "repair: the labels have a position again");
    check(!ed.dirty(), "repair: opening still leaves the document clean (the user saves to keep the fix)");
  }

  // ---- menus ---------------------------------------------------------------------------------------------------------------------------
  {
    const QStringList titles = [&] { QStringList t; for (QAction* a : win.menuBar()->actions()) t << a->text().remove('&'); return t; }();
    check(titles == QStringList{"Archivo", "Editar", "Ver", "Navegación", "Diario", "Herramientas", "Plugins", "Ayuda"}, "menus: the top-level menus are those of the Electron app: " + titles.join(", "));
    auto has = [&](const QString& menu, const QString& label) {
      QMenu* m = win.menu(menu);
      return m && std::any_of(m->actions().begin(), m->actions().end(), [&](QAction* a) { return a->text().remove('&') == label || (a->menu() && a->menu()->title().remove('&') == label); });
    };
    check(has("file", "Nueva ventana") && has("view", "Tema") && has("help", "Bienvenida") && has("help", "Diagnóstico del lápiz…"), "menus: new window, theme, welcome and pen diagnostic entries exist");
    check(ed.action("view.sidebarPages")->isEnabled() && ed.action("view.sidebarLayers")->isEnabled() && ed.action("view.presentation")->isEnabled() && ed.action("nav.back")->text() == "Atrás", "menus: the placeholders we own are implemented");
    if (c->sidebarShows("layers")) ed.action("view.sidebarLayers")->trigger();
    ed.action("view.sidebarLayers")->trigger();
    const bool shown = c->sidebarShows("layers");
    ed.action("view.sidebarLayers")->trigger();
    check(shown && !c->sidebarShows("layers"), "menus: the sidebar entry toggles its panel (a second use hides it)");
  }
  fresh(win, ed, sampleDoc);
  c->setTheme("classic", "auto");
  runChromeLifecycleSelfTest(win, ed, io);
  runXppSelfTest(win, ed, io);
  fresh(win, ed, sampleDoc);
}

}  // namespace chrome
