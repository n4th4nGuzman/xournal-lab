// --selftest checks of the Xournal++-style organisation (PARIDAD/xpp_features.md): pen line styles and fill written as Xournal++ writes them and drawn by the
// renderer and the exports, the dropdowns of the pen / highlighter / eraser, the colour sidebar (left, right, one set of dots), the layer selector, paired
// pages and the new shapes. Synthetic input only: no physical pen, no official Xournal++, display scale 1.
#include "brush.h"
#include "chrome.h"
#include "docfactory.h"
#include "pageexport.h"
#include "texttools/texttools.h"
#include "xopp.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <cmath>

namespace chrome {
namespace {

void pump(int n = 6) { for (int i = 0; i < n; i++) QApplication::processEvents(); }

Stroke* lastStroke(Editor& ed, int page) {
  auto& layers = ed.doc().pages[size_t(page)].layers;
  const int l = std::clamp(ed.activeLayer(page), 0, int(layers.size()) - 1);
  auto& els = layers[size_t(l)].elements;
  for (size_t i = els.size(); i-- > 0;) if (auto* s = std::get_if<Stroke>(&els[i])) return s;
  return nullptr;
}

/** A horizontal drag with the real tool path (mouse: one width for the stroke). */
void drag(Editor& ed, DocumentView& v, int page, QPointF a, QPointF b) {
  ed.pointerDown(v.makeEvent(page, a, 0.5, PointerEvent::Kind::Mouse));
  for (int i = 1; i < 20; i++) ed.pointerMove(v.makeEvent(page, a + (b - a) * (i / 20.0), 0.5, PointerEvent::Kind::Mouse));
  ed.pointerUp(v.makeEvent(page, b, 0.5, PointerEvent::Kind::Mouse));
}

/** How many separate dark runs a row of pixels crosses (a solid line is 1, a dashed one many). */
int runsOnRow(const QImage& img, int y) {
  int runs = 0;
  bool in = false;
  for (int x = 0; x < img.width(); x++) {
    const bool dark = qGray(img.pixel(x, y)) < 128;
    if (dark && !in) runs++;
    in = dark;
  }
  return runs;
}

/** Runs on the line of a horizontal stroke drawn at page y=60 from x=20 to x=220, rendered at 288 dpi (4 px per unit). */
int runsOfStyle(const Document& doc) {
  BackgroundResolver res;
  ExportOptions o;
  o.background = false;
  QImage img = renderPageImage(doc, 0, 288, o, res);
  if (img.isNull()) return -1;
  QImage flat(img.size(), QImage::Format_ARGB32);
  flat.fill(Qt::white);
  QPainter p(&flat);
  p.drawImage(0, 0, img);
  p.end();
  return runsOnRow(flat, 60 * 4);
}

Document lineDoc(const QString& style, const QString& fill = QString()) {
  Document d = blankDocument();
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "butt"}};
  if (!style.isEmpty()) s.attrs.emplace_back("style", style);
  if (!fill.isEmpty()) s.attrs.emplace_back("fill", fill);
  s.widths = {2};
  s.pts = {20, 60, 220, 60};
  d.pages[0].layers[0].elements.push_back(s);
  return d;
}

QAction* menuAction(QMenu* m, const QString& objectName) {
  for (QAction* a : m->actions()) if (a->objectName() == objectName) return a;
  return nullptr;
}
QAction* menuActionText(QMenu* m, const QString& startsWith) {
  for (QAction* a : m->actions()) if (a->text().startsWith(startsWith)) return a;
  return nullptr;
}
QMenu* dropdownOf(MainWindow& win, Editor& ed, const QString& tool) {
  pump(10);
  QToolButton* b = nullptr;   // a tool with a dropdown sits in the bar as a widget of its own, found by its action
  for (QToolButton* t : win.toolBar()->findChildren<QToolButton*>()) if (t->defaultAction() == ed.action("tool." + tool)) b = t;
  QMenu* menu = b ? b->findChild<QMenu*>("dropdown") : nullptr;
  if (!menu) return nullptr;
  QMetaObject::invokeMethod(menu, "aboutToShow");   // what showing the popup does: the menu is filled
  return menu;
}
int swatchButtons(QToolBar* bar) {
  int n = 0;
  for (QToolButton* b : bar->findChildren<QToolButton*>()) if (b->property("xlSwatch").toBool()) n++;
  return n;
}

}  // namespace

void runXppSelfTest(MainWindow& win, Editor& ed, const TestIO& io) {
  const auto& check = io.check;
  QString shots = qEnvironmentVariable("XL_XPP_SHOTS", io.shots);
  if (!shots.isEmpty()) QDir().mkpath(shots);
  QTemporaryDir tmp;
  Chrome* c = chromeOf(win);
  DocumentView& v = *win.view();

  // ---- line styles in the file and in the renderer ---------------------------------------------------------------------------------------
  {
    const int solid = runsOfStyle(lineDoc(QString())), dash = runsOfStyle(lineDoc("dash")), dd = runsOfStyle(lineDoc("dashdot")), dot = runsOfStyle(lineDoc("dot"));
    check(solid == 1, QString("xpp line style: a plain stroke is one run (%1)").arg(solid));
    check(dash >= 18 && dash <= 25, QString("xpp line style: dash {6,3} over 200 units gives about 22 dashes (%1)").arg(dash));
    check(dd > 20 && dd <= 45, QString("xpp line style: dash-dot has the long and the short mark (%1 runs)").arg(dd));
    check(dot > 45, QString("xpp line style: dot {0.5,3} gives many short marks (%1)").arg(dot));
    const int cust = runsOfStyle(lineDoc("cust: 10.00 10.00"));
    check(cust >= 8 && cust <= 12, QString("xpp line style: Xournal++'s custom pattern 'cust: 10.00 10.00' is honoured (%1)").arg(cust));
    check(runsOfStyle(lineDoc("cust: nonsense")) == 1 && runsOfStyle(lineDoc("cust: 0 0")) == 1, "xpp line style: a malformed custom pattern draws the stroke solid, not nothing");
    check(runsOfStyle(lineDoc(QString(), "128")) == 1, "xpp fill: a filled open line still draws its line");
  }
  {   // export: PNG and vector PDF of a dashed stroke
    Document d = lineDoc("dash");
    BackgroundResolver res;
    ExportOptions o;
    const QString png = tmp.filePath("dash.png"), pdf = tmp.filePath("dash.pdf");
    const ExportResult r1 = exportPng(d, png, 144, o, res), r2 = exportPdf(d, pdf, o, res);
    check(r1.ok && QFileInfo(png).size() > 0 && r2.ok && QFileInfo(pdf).size() > 0, "xpp line style: PNG and PDF export of a dashed stroke");
    QImage img(png);
    QImage flat(img.size(), QImage::Format_ARGB32);
    flat.fill(Qt::white);
    QPainter p(&flat);
    p.drawImage(0, 0, img);
    p.end();
    check(runsOnRow(flat, 120) >= 18, "xpp line style: the exported PNG is dashed");
  }
  {   // .xopp round trip with the attribute Xournal++ uses
    Document d = lineDoc("dashdot", "100");
    const QString path = tmp.filePath("style.xopp");
    QString err;
    check(saveXopp(path, d, &err), "xpp round trip: saved");
    const LoadResult lr = loadXopp(path);
    const Stroke* s = std::get_if<Stroke>(&lr.doc.pages[0].layers[0].elements[0]);
    check(s && attrOr(s->attrs, "style") == "dashdot" && attrOr(s->attrs, "fill") == "100", "xpp round trip: style and fill survive a save and a load");
  }

  // ---- drawing with the real pen tool ----------------------------------------------------------------------------------------------------
  ed.setDocument(blankDocument(), QString());
  ed.markSaved();
  pump();
  ed.setTool("pen");
  {
    Style s = ed.style();
    s.color = "#cc0000"; s.lineStyle = "plain"; s.fillAlpha = -1;
    ed.setStyle(s);
    drag(ed, v, 0, {100, 100}, {300, 100});
    const Stroke* st = lastStroke(ed, 0);
    check(st && !attrFind(st->attrs, "style") && !attrFind(st->attrs, "fill"), "xpp pen: the standard style writes no style attribute (files stay as before)");
    for (const char* style : {"dash", "dashdot", "dot"}) {
      s = ed.style(); s.lineStyle = style; ed.setStyle(s);
      drag(ed, v, 0, {100, 130}, {300, 130});
      st = lastStroke(ed, 0);
      check(st && attrOr(st->attrs, "style") == style, QString("xpp pen: the real pen tool writes style=\"%1\"").arg(style));
    }
    s = ed.style(); s.lineStyle = "plain"; s.fillAlpha = 128; ed.setStyle(s);
    drag(ed, v, 0, {100, 160}, {300, 160});
    st = lastStroke(ed, 0);
    check(st && attrOr(st->attrs, "fill") == "128" && !attrFind(st->attrs, "style"), "xpp pen: fill is written as the fill attribute");
    s = ed.style(); s.fillAlpha = -1; ed.setStyle(s);
  }

  // ---- dropdown of the pen --------------------------------------------------------------------------------------------------------------------
  {
    QMenu* m = dropdownOf(win, ed, "pen");
    check(m != nullptr, "xpp pen dropdown: the pen button has a menu behind its caret");
    if (m) {
      const BrushKind tip0 = ed.tablet().brush;
      check(menuAction(m, "tip.pen") && menuAction(m, "tip.ink") && menuAction(m, "tip.fountain") && menuAction(m, "tip.brush"), "xpp pen dropdown: the four tips (bolígrafo, tinta, pluma, pincel) are listed");
      if (QAction* t = menuAction(m, "tip.brush")) t->trigger();
      check(ed.tablet().brush == BrushKind::Brush, "xpp pen dropdown: choosing 'Pincel' changes the tip");
      QMetaObject::invokeMethod(m, "aboutToShow");
      QAction* tb = menuAction(m, "tip.brush");
      check(tb && tb->isChecked() && !menuAction(m, "tip.pen")->isChecked(), "xpp pen dropdown: the tip in use is checked when it opens again");
      if (QAction* t = menuAction(m, QString("tip.") + brushId(tip0).toLatin1().constData())) t->trigger();
      QAction* dash = menuAction(m, "lineStyle.dash");
      check(dash && menuAction(m, "lineStyle.plain") && menuAction(m, "lineStyle.dashdot") && menuAction(m, "lineStyle.dot"), "xpp pen dropdown: four line styles (estándar, discontinua, discontinua y punteada, punteada)");
      if (dash) dash->trigger();
      check(ed.style().lineStyle == "dash", "xpp pen dropdown: choosing 'Discontinua' sets the drawing style");
      QMetaObject::invokeMethod(m, "aboutToShow");
      QAction* d2 = menuAction(m, "lineStyle.dash");
      check(d2 && d2->isChecked() && !menuAction(m, "lineStyle.plain")->isChecked(), "xpp pen dropdown: the current style is checked when it opens again");
      if (QAction* fine = menuActionText(m, "Fino")) fine->trigger();
      check(std::abs(ed.style().penWidth - 0.85) < 1e-9, "xpp pen dropdown: a size entry sets the pen width");
      if (QAction* f = menuActionText(m, "Relleno medio")) f->trigger();
      check(ed.style().fillAlpha == 128, "xpp pen dropdown: fill 50 %");
      if (QAction* f = menuActionText(m, "Sin relleno")) f->trigger();
      check(ed.style().fillAlpha == -1, "xpp pen dropdown: no fill");
      check(menuActionText(m, "Lápiz y tableta") != nullptr, "xpp pen dropdown: the pen and tablet dialog is still one click away");
      if (QAction* p = menuActionText(m, "Estándar")) p->trigger();
      check(ed.style().lineStyle == "plain", "xpp pen dropdown: back to the standard style");
      if (QAction* med = menuActionText(m, "Medio")) med->trigger();
    }
    QMenu* hl = dropdownOf(win, ed, "highlighter");
    check(hl && menuActionText(hl, "Media (50 %)") && menuActionText(hl, "Grueso"), "xpp highlighter dropdown: sizes and opacity");
    if (hl) {
      menuActionText(hl, "Fuerte")->trigger();
      check(ed.style().highlighterAlpha == "bf", "xpp highlighter dropdown: opacity 75 %");
      menuActionText(hl, "Media (50 %)")->trigger();
      check(ed.style().highlighterAlpha == "7f", "xpp highlighter dropdown: opacity 50 % is the file's default");
    }
    QMenu* er = dropdownOf(win, ed, "eraser");
    check(er && menuActionText(er, "Borrar el trazo entero") && menuActionText(er, "Borrar solo la zona"), "xpp eraser dropdown: sizes and the two eraser types");
    if (er) {
      menuActionText(er, "Borrar solo la zona")->trigger();
      check(ed.tool() == "eraserPart", "xpp eraser dropdown: the type entry switches the tool");
      if (QAction* big = menuActionText(er, "Grande")) big->trigger();
      check(ed.style().eraserRadius == 32, "xpp eraser dropdown: a size entry sets the eraser radius");
    }
    ed.setTool("pen");
  }

  // ---- shapes: double arrow and axes ----------------------------------------------------------------------------------------------------------
  {
    const auto da = shapePoints(ShapeKind::DoubleArrow, 10, 10, 110, 10, false, 1.41);
    const auto ax = shapePoints(ShapeKind::Axes, 10, 10, 110, 90, false, 1.41);
    check(da.size() == 16 && std::abs(da[8] - 110) < 1e-9 && std::abs(da[2] - 10) < 1e-9, "xpp shapes: a double arrow has a head at each end");
    check(ax.size() == 18 && std::abs(ax[8] - 10) < 1e-9 && std::abs(ax[9] - 90) < 1e-9 && std::abs(ax[10] - 110) < 1e-9, "xpp shapes: the axes meet at the bottom-left of the dragged box");
    ed.setTool("shape");
    texttools::state().shape = ShapeKind::DoubleArrow;
    Style s = ed.style(); s.lineStyle = "dash"; ed.setStyle(s);
    drag(ed, v, 0, {400, 400}, {500, 400});
    const Stroke* st = lastStroke(ed, 0);
    check(st && st->pointCount() == 8 && attrOr(st->attrs, "style") == "dash", "xpp shapes: the double arrow tool draws one stroke and uses the pen's line style");
    texttools::state().shape = ShapeKind::Rect;
    s = ed.style(); s.lineStyle = "plain"; ed.setStyle(s);
    ed.setTool("pen");
  }

  // ---- colour sidebar ------------------------------------------------------------------------------------------------------------------
  {
    win.setColorSidebar(false);
    pump(10);
    const int topBefore = swatchButtons(win.styleBar());
    check(topBefore >= 5 && !win.colorBar()->isVisible() && win.colorBar()->actions().isEmpty(), "xpp colour sidebar: off = the dots are in the style row, the bar is empty");
    win.setColorSidebar(true);
    pump(10);
    check(win.colorSidebar() && !win.colorBar()->isHidden() && win.toolBarArea(win.colorBar()) == Qt::LeftToolBarArea, "xpp colour sidebar: on = a bar at the left edge");
    const int side = swatchButtons(win.colorBar()), top = swatchButtons(win.styleBar());
    check(side == topBefore && top == 0, QString("xpp colour sidebar: one set of dots (sidebar %1, style row %2, before %3)").arg(side).arg(top).arg(topBefore));
    check(win.colorBar()->orientation() == Qt::Vertical, "xpp colour sidebar: the column is vertical");
    check(win.colorBar()->findChild<QToolButton*>("customColor") != nullptr, "xpp colour sidebar: the custom colour box is there");
    for (const char* id : {"view.twoPages", "view.presentation", "view.zoom100", "view.fitWidth", "view.zoomOut", "view.zoomIn"})
      check(win.colorBar()->actions().contains(ed.action(id)), QString("xpp colour sidebar: view button %1").arg(id));
    // clicking a dot in the sidebar selects the drawing colour
    QToolButton* second = nullptr;
    int n = 0;
    for (QAction* a : win.colorBar()->actions()) if (auto* b = qobject_cast<QToolButton*>(win.colorBar()->widgetForAction(a)); b && b->property("xlSwatch").toBool() && ++n == 2) second = b;
    const QString want = second && second->defaultAction() ? second->defaultAction()->data().toString() : QString();
    if (second) second->click();
    check(second && ed.style().color.compare(want, Qt::CaseInsensitive) == 0, QString("xpp colour sidebar: a click on a dot sets the colour (%1)").arg(want));
    if (!shots.isEmpty()) { if (c) c->setTheme("neon", "full"); pump(10); win.grab().save(shots + "/sidebar_left_neon.png"); if (c) c->setTheme("classic", "full"); pump(10); win.grab().save(shots + "/sidebar_left_classic.png"); }
    win.setColorSidebarRight(true);
    pump(10);
    check(win.toolBarArea(win.colorBar()) == Qt::RightToolBarArea && !win.colorBar()->isHidden(), "xpp colour sidebar: left-handed = the bar moves to the right edge");
    if (!shots.isEmpty()) win.grab().save(shots + "/sidebar_right_classic.png");
    win.setColorSidebarRight(false);
    check(win.toolBarArea(win.colorBar()) == Qt::LeftToolBarArea, "xpp colour sidebar: and back to the left");
    // the View menu entries drive the same hooks
    ed.action("view.colorSidebar")->setChecked(false);
    check(!win.colorSidebar() && win.colorBar()->isHidden(), "xpp colour sidebar: the View menu entry hides it");
    ed.action("view.colorSidebar")->setChecked(true);
    check(win.colorSidebar(), "xpp colour sidebar: and shows it");
    // presentation hides it and brings it back
    if (c) {
      c->setPresenting(true);
      pump();
      const bool hid = win.colorBar()->isHidden();
      c->setPresenting(false);
      pump();
      check(hid && !win.colorBar()->isHidden(), "xpp colour sidebar: presentation mode hides it and restores it");
    }
    // persistence: only while persistence is on, in the window's own settings (a temp .ini here, never the registry)
    const bool persisted = win.persistence();
    win.setSettingsFile(tmp.filePath("ui.ini"));
    win.setPersistence(true);
    win.setColorSidebarRight(true);
    win.setColorSidebar(false);
    win.settings().sync();
    check(win.settings().value("ui/colorSidebarRight").toBool() && !win.settings().value("ui/colorSidebar", true).toBool(), "xpp colour sidebar: the choice is remembered in the window settings");
    win.setColorSidebarRight(false);
    win.setColorSidebar(true);
    {   // a later start reads what was stored (here written by hand: the setters above have just stored left/on)
      QSettings st = win.settings();
      st.setValue("ui/colorSidebarRight", true);
      st.setValue("ui/colorSidebar", false);
      st.sync();
    }
    win.loadUiState();
    check(!win.colorSidebar() && win.colorSidebarRight(), "xpp colour sidebar: loadUiState restores the stored side and visibility");
    win.setPersistence(persisted);
    win.setColorSidebarRight(false);
    win.setColorSidebar(false);   // the other checks run with the dots in the style row, like before
    pump(10);
  }

  // ---- layer selector ------------------------------------------------------------------------------------------------------------------
  {
    ed.setDocument(blankDocument(), QString());
    ed.markSaved();
    pump();
    auto* b = win.findChild<QToolButton*>("layerSelector");   // at the end of the tools row
    check(b != nullptr && b->text() == "Capa 1", QString("xpp layers: the tools row has the layer selector (%1)").arg(b ? b->text() : QString("missing")));
    if (b) {
      layerops::add(ed, 0);
      pump();
      check(b->text() == "Capa 2", QString("xpp layers: the selector follows the active layer (%1)").arg(b->text()));
      QMenu* m = b->menu();
      QMetaObject::invokeMethod(m, "aboutToShow");
      QAction* first = menuActionText(m, "Capa 1");
      check(first && menuActionText(m, "Capa 2") && menuActionText(m, "Nueva capa") && menuActionText(m, "Ocultar la capa activa"), "xpp layers: the menu lists the layers (top first) and the operations");
      if (first) first->trigger();
      pump();
      check(ed.activeLayer(0) == 0 && b->text() == "Capa 1", "xpp layers: choosing a layer makes it the active one");
      QMetaObject::invokeMethod(m, "aboutToShow");
      menuActionText(m, "Ocultar la capa activa")->trigger();
      check(!ed.layerVisible(0, 0), "xpp layers: hide the active layer from the menu");
      QMetaObject::invokeMethod(m, "aboutToShow");
      check(menuActionText(m, "Capa 1  (oculta)") != nullptr, "xpp layers: a hidden layer is marked");
      menuActionText(m, "Mostrar la capa activa")->trigger();
      QMetaObject::invokeMethod(m, "aboutToShow");
      menuActionText(m, "Nueva capa")->trigger();
      pump();
      check(ed.doc().pages[0].layers.size() == 3, "xpp layers: 'Nueva capa' adds one");
      QMetaObject::invokeMethod(m, "aboutToShow");
      menuActionText(m, "Eliminar la capa activa")->trigger();
      check(ed.doc().pages[0].layers.size() == 2, "xpp layers: 'Eliminar' removes the active one");
      ed.undo(); ed.undo(); ed.undo();
      pump();
    }
    QToolButton* fsBtn = qobject_cast<QToolButton*>(win.toolBar()->widgetForAction(ed.action("view.fullscreen")));
    check(fsBtn != nullptr, "xpp fullscreen: a button at the end of the tools row");
    if (fsBtn) {
      fsBtn->click();
      pump(10);
      const bool full = win.isFullScreen();
      fsBtn->click();
      pump(10);
      check(full && !win.isFullScreen() && win.toolBar()->isVisible(), "xpp fullscreen: the button enters fullscreen and a second click comes back with the bars");
    }
  }

  // ---- paired pages --------------------------------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    for (int i = 0; i < 3; i++) appendPage(d);   // 4 pages
    ed.setDocument(std::move(d), QString());
    ed.markSaved();
    pump();
    ed.action("view.continuous")->setChecked(true);
    ed.action("view.twoPages")->setChecked(true);
    pump(10);
    const QRectF r0 = v.pageRectOnScreen(0), r1 = v.pageRectOnScreen(1), r2 = v.pageRectOnScreen(2);
    check(v.pairedPages() && std::abs(r0.top() - r1.top()) < 1.5 && r1.left() > r0.right() - 1 && r2.top() > r0.bottom() - 1 && std::abs(r2.left() - r0.left()) < 1.5,
          QString("xpp two pages: 1-2 side by side, 3 starts the next row (%1,%2 | %3,%4 | %5,%6)").arg(r0.left()).arg(r0.top()).arg(r1.left()).arg(r1.top()).arg(r2.left()).arg(r2.top()));
    v.zoomFitWidth();
    pump(10);
    const QRectF f1 = v.pageRectOnScreen(1);
    check(f1.right() <= v.viewport()->width() + 1 && v.pageRectOnScreen(0).left() >= -1, "xpp two pages: fit width fits the pair");
    // drawing on the right-hand page lands on that page, in its own coordinates
    ed.setTool("pen");
    const QPointF mid = v.toView(1, QPointF(100, 100));
    check(v.pageAt(mid) == 1 && v.pageAt(v.toView(0, QPointF(100, 100))) == 0, "xpp two pages: hit test tells the facing pages apart");
    drag(ed, v, 1, {100, 100}, {200, 100});
    check(ed.doc().pages[1].layers[0].elements.size() == 1 && ed.doc().pages[0].layers[0].elements.empty(), "xpp two pages: a stroke on the right-hand page goes to that page");
    if (!shots.isEmpty()) win.grab().save(shots + "/two_pages.png");
    ed.action("view.continuous")->setChecked(false);
    pump(10);
    check(!v.pageRectOnScreen(0).isEmpty() && !v.pageRectOnScreen(1).isEmpty(), "xpp two pages: the single-screen view shows the pair of the current page");
    ed.action("view.continuous")->setChecked(true);
    ed.action("view.twoPages")->setChecked(false);
    pump(10);
    const QRectF s0 = v.pageRectOnScreen(0), s1 = v.pageRectOnScreen(1);
    check(!v.pairedPages() && std::abs(s0.left() - s1.left()) < 1.5 && s1.top() > s0.bottom() - 1, "xpp two pages: off = one column again");
    ed.setDocument(blankDocument(), QString());
    ed.markSaved();
    v.zoom100();   // the checks that follow assume the zoom they always had
    pump();
  }
  // ---- isometric papers and annotated-page navigation ----------------------------------------------------------------------------------
  {
    ed.setDocument(blankDocument(), QString());
    ed.markSaved();
    pump();
    auto ink = [&](const QString& style) {
      ed.action("journal.paper:" + style)->trigger();
      BackgroundResolver res;
      ExportOptions o;
      const QImage img = renderPageImage(ed.doc(), 0, 72, o, res);
      int dark = 0;
      for (int y = 0; y < img.height(); y += 1) for (int x = 0; x < img.width(); x += 1) if (qGray(img.pixel(x, y)) < 235) dark++;
      return dark;
    };
    const int plain = ink("plain"), iso = ink("isograph"), dots = ink("isodotted");
    check(attrOr(ed.doc().pages[0].background, "style") == "isodotted" && plain == 0, "xpp paper: isometric dots are saved as style=\"isodotted\" (Xournal++'s value) and plain paper is blank");
    check(iso > 1500, QString("xpp paper: the isometric grid draws lines (%1 px)").arg(iso));
    check(dots > 500 && dots < iso, QString("xpp paper: isometric dots draw fewer pixels than the grid (%1 < %2)").arg(dots).arg(iso));
    ed.undo(); ed.undo(); ed.undo();
    Document d = blankDocument();
    for (int i = 0; i < 3; i++) appendPage(d);
    d.pages[2].layers[0].elements.push_back(lineDoc("").pages[0].layers[0].elements[0]);
    ed.setDocument(std::move(d), QString());
    ed.markSaved();
    pump();
    ed.action("nav.nextAnnotated")->trigger();
    pump();
    check(ed.currentPage() == 2, QString("xpp navigation: next annotated page skips the empty ones (page %1)").arg(ed.currentPage() + 1));
    ed.action("nav.nextAnnotated")->trigger();
    check(ed.currentPage() == 2, "xpp navigation: no further annotated page stays put");
    ed.action("nav.prevAnnotated")->trigger();
    pump();
    check(ed.currentPage() == 2, "xpp navigation: no previous annotated page stays put");
    ed.setDocument(blankDocument(), QString());
    ed.markSaved();
    pump();
  }
  if (!shots.isEmpty() && QFileInfo::exists(shots + "/xpp_style.xopp")) {   // the file with every style (written by hand in Xournal++'s format), drawn by this app
    const LoadResult lr = loadXopp(shots + "/xpp_style.xopp");
    ed.setDocument(lr.doc, QString());
    ed.markSaved();
    pump();
    v.zoomFitWidth();
    v.scrollToPage(0);
    v.renderVisibleNow();
    pump(10);
    win.grab().save(shots + "/cpp_styles_file.png");
    check(countOf(ed.doc()).strokes == 6, "xpp file: the hand-written file with dash, dash-dot, dot, custom pattern and fills loads with its 6 strokes");
    ed.setDocument(blankDocument(), QString());
    ed.markSaved();
    v.zoom100();
    pump();
  }
  if (!shots.isEmpty()) {
    // the pen dropdown, as the user sees it
    if (QMenu* m = dropdownOf(win, ed, "pen")) { m->adjustSize(); m->grab().save(shots + "/pen_dropdown.png"); }
    if (QMenu* m = dropdownOf(win, ed, "eraser")) { m->adjustSize(); m->grab().save(shots + "/eraser_dropdown.png"); }
    if (QMenu* m = dropdownOf(win, ed, "highlighter")) { m->adjustSize(); m->grab().save(shots + "/highlighter_dropdown.png"); }
  }
}

}  // namespace chrome
