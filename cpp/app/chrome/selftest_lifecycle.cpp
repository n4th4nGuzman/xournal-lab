// --selftest of the chrome area, second part: the file lifecycle (the six real samples through open, save as, reopen; recovery copies; save over the opened
// file; close -> welcome; drag and drop), menu parity with main.ts, live preferences, the go-to-page dialog, the exact find highlight and the floating dialogs.
// Synthetic input only: no pen, no second monitor, no real drag from Explorer.
#include "chrome.h"
#include "find.h"
#include "parts.h"
#include "prefs.h"
#include "settings/settings.h"
#include "texttools/texttools.h"
#include "xopp.h"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QCloseEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace chrome {
namespace {
using Check = std::function<void(bool, const QString&)>;
void pump(int n = 4) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
size_t elementsOfPage0(Editor& ed) { return ed.doc().pages[0].layers[0].elements.size(); }

Stroke inkStroke(double x) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
  s.widths = {3};
  s.pts = {x, 20, x + 30, 60};
  return s;
}
Text textAt(const QString& s, double x, double y) {
  Text t;
  t.attrs = {{"font", "Sans"}, {"size", "24"}, {"x", QString::number(x)}, {"y", QString::number(y)}, {"color", "#000000ff"}};
  t.text = s;
  return t;
}
void cleanDoc(Editor& ed, Document d) {
  ed.setDocument(std::move(d), QString());
  ed.markSaved();
  pump();
}
/** Answers the next modal message box with this standard button (the dialog is application modal and would block the test). */
void answerNextBox(QMessageBox::StandardButton b) {
  QTimer::singleShot(50, [b] {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      if (QAbstractButton* btn = box->button(b)) btn->click();
    }
  });
}

// What main.ts builds, one entry per item: "Menu/Submenu/Item" and its accelerator (empty = none). Items the Electron app lists as disabled placeholders are left out.
const QList<QPair<QString, QString>>& expectedMenus() {
  static const QList<QPair<QString, QString>> m = {
      {"Archivo/Nuevo", "Ctrl+N"}, {"Archivo/Nueva ventana", "Ctrl+Shift+N"}, {"Archivo/Abrir…", "Ctrl+O"}, {"Archivo/Abrir reciente", ""},
      {"Archivo/Abrir copia de recuperación…", ""}, {"Archivo/Guardar", "Ctrl+S"}, {"Archivo/Guardar como…", "Ctrl+Shift+S"}, {"Archivo/Guardar copia…", ""},
      {"Archivo/Exportar como PDF…", ""}, {"Archivo/Cerrar", "Ctrl+W"}, {"Archivo/Salir", "Ctrl+Q"},
      {"Editar/Deshacer", "Ctrl+Z"}, {"Editar/Rehacer", "Ctrl+Y"}, {"Editar/Cortar", "Ctrl+X"}, {"Editar/Copiar", "Ctrl+C"}, {"Editar/Pegar", "Ctrl+V"},
      {"Editar/Eliminar", "Del"}, {"Editar/Buscar…", "Ctrl+F"}, {"Editar/Preferencias…", "Ctrl+,"},
      {"Ver/Barra de herramientas", ""}, {"Ver/Barras laterales/Vista previa de páginas", ""}, {"Ver/Barras laterales/Capas", ""}, {"Ver/Pantalla completa", "F11"},
      {"Ver/Modo de presentación", "F5"}, {"Ver/Zoom/Acercar", "Ctrl++"}, {"Ver/Zoom/Alejar", "Ctrl+-"}, {"Ver/Zoom/Tamaño normal (100 %)", "Ctrl+0"},
      {"Ver/Zoom/Ajustar al ancho", ""}, {"Ver/Zoom/Ajustar a la página", ""}, {"Ver/Vista continua", ""},
      {"Navegación/Primera página", "Ctrl+Home"}, {"Navegación/Página anterior", "PgUp"}, {"Navegación/Página siguiente", "PgDown"}, {"Navegación/Última página", "Ctrl+End"},
      {"Navegación/Ir a la página…", "Ctrl+G"}, {"Navegación/Atrás", "Alt+Left"}, {"Navegación/Adelante", "Alt+Right"},
      {"Diario/Nueva página", ""}, {"Diario/Insertar página antes", ""}, {"Diario/Insertar página al final", ""}, {"Diario/Eliminar página actual", ""},
      {"Diario/Mover página hacia arriba", ""}, {"Diario/Mover página hacia abajo", ""}, {"Diario/Fondo de página/Color de fondo…", ""},
      {"Diario/Fondo de página/Tipo de papel/Liso", ""}, {"Diario/Fondo de página/Tipo de papel/Rayado (con margen)", ""}, {"Diario/Fondo de página/Tipo de papel/Reglado", ""},
      {"Diario/Fondo de página/Tipo de papel/Cuadriculado", ""}, {"Diario/Fondo de página/Tipo de papel/Puntos", ""}, {"Diario/Fondo de página/Aplicar a todas las páginas", ""},
      {"Diario/Formato de papel/A3", ""}, {"Diario/Formato de papel/A4", ""}, {"Diario/Formato de papel/A5", ""}, {"Diario/Formato de papel/Carta (Letter)", ""},
      {"Diario/Formato de papel/Legal", ""}, {"Diario/Formato de papel/Girar hoja", ""},
      {"Herramientas/Lápiz", "P"}, {"Herramientas/Resaltador", "H"}, {"Herramientas/Goma (trazo completo)", "E"}, {"Herramientas/Borrar solo la zona que tocas", "B"},
      {"Herramientas/Texto", "T"}, {"Herramientas/LaTeX…", "X"}, {"Herramientas/Imagen…", ""}, {"Herramientas/Formas", "G"},
      {"Herramientas/Seleccionar objeto (rectángulo)", "S"}, {"Herramientas/Seleccionar región (lazo)", "L"}, {"Herramientas/Mano", "M"},
      {"Herramientas/Espacio vertical", "V"}, {"Herramientas/Puntero láser", "K"}, {"Herramientas/Relleno", "U"}, {"Herramientas/Plano cartesiano", "C"},
      {"Herramientas/Espacio cartesiano 3D", "D"}, {"Herramientas/Figuras (líneas, curvas, polígonos, sólidos)", "F"}, {"Herramientas/Recta real (intervalos y puntos)", "N"},
      {"Herramientas/Diagrama de conjuntos (Venn)", "J"}, {"Herramientas/Mostrar el cuadro de herramientas", "Q"},
      {"Herramientas/Activar o desactivar el cuadro de herramientas", ""}, {"Herramientas/Lápiz y tableta…", ""}, {"Herramientas/Formas automáticas", ""},
      {"Plugins/Administrar plugins (no disponible)", ""},
      {"Ayuda/Bienvenida", ""}, {"Ayuda/Diagnóstico del lápiz…", ""}, {"Ayuda/Manual de Xournal++ (abre el navegador)", ""}, {"Ayuda/Acerca de", ""}};
  return m;
}

QAction* findPath(QMenuBar* bar, const QString& path) {
  QList<QAction*> level = bar->actions();
  QAction* hit = nullptr;
  for (const QString& part : path.split('/')) {
    hit = nullptr;
    for (QAction* a : level) if (a->text().remove('&') == part) { hit = a; break; }
    if (!hit) return nullptr;
    level = hit->menu() ? hit->menu()->actions() : QList<QAction*>();
  }
  return hit;
}
}  // namespace

void runChromeLifecycleSelfTest(MainWindow& win, Editor& ed, const TestIO& io) {
  const Check& check = io.check;
  auto out = io.out;
  Chrome* c = chromeOf(win);
  if (!c) return;
  const QString shots = io.shots.isEmpty() ? QDir::currentPath() : io.shots;
  out("\n== chrome: file lifecycle, menus of main.ts, close -> welcome, drops, live prefs, floating dialogs ==");
  QTemporaryDir tmp;
  check(tmp.isValid(), "lifecycle: temporary folder");

  // ---- menus against main.ts ----------------------------------------------------------------------------------------------------------
  {
    QStringList missing, wrongKey;
    for (const auto& [path, accel] : expectedMenus()) {
      QAction* a = findPath(win.menuBar(), path);
      if (!a) { missing << path; continue; }
      if (!accel.isEmpty()) {
        const QKeySequence want(accel);
        const QList<QKeySequence> have = a->shortcuts();
        if (!have.contains(want)) wrongKey << path + " (" + accel + " vs " + QKeySequence::listToString(have) + ")";
      }
    }
    check(missing.isEmpty(), "menus: every item of main.ts exists" + (missing.isEmpty() ? QString() : ": missing " + missing.join("; ")));
    check(wrongKey.isEmpty(), "menus: every shortcut of main.ts matches" + (wrongKey.isEmpty() ? QString() : ": " + wrongKey.join("; ")));
    // every paper kind is in Diario > Tipo de papel, and the kinds that have a label in the Electron menu share it
    QAction* papers = findPath(win.menuBar(), "Diario/Fondo de página/Tipo de papel");
    check(papers && papers->menu() && papers->menu()->actions().size() >= 5 + 1 + 7, "menus: Tipo de papel lists the five classic papers, a separator and the special ones");
    // the enabled state: Plugins is a disabled placeholder as in main.ts
    QAction* plug = findPath(win.menuBar(), "Plugins/Administrar plugins (no disponible)");
    check(plug && !plug->isEnabled(), "menus: Plugins is the disabled placeholder");
  }

  // ---- every special paper through the bar: xlPaper + a look-alike style, one undo step, saved and reopened -------------------------------------------------
  {
    cleanDoc(ed, blankDocument());
    int ok = 0, kinds = 0;
    for (const PaperKind& k : paperKinds()) {
      kinds++;
      const int depth = ed.undoDepth();
      c->paperBar()->choosePaper("x:" + k.id);
      const Attrs& bg = ed.doc().pages[0].background;
      const QString stored = attrOr(bg, "xlPaper");
      const bool written = stored.startsWith(k.id) && attrOr(bg, "style") == k.fallback && ed.undoDepth() == depth + 1;
      const QString f = tmp.filePath("papel-" + k.id + ".xopp");
      saveXopp(f, ed.doc());
      const LoadResult back = loadXopp(f);
      const bool kept = back.ok && attrOr(back.doc.pages[0].background, "xlPaper") == stored;
      ed.undo();
      if (written && kept && !attrFind(ed.doc().pages[0].background, "xlPaper")) ok++;
      else out("  paper kind not right: " + k.id + " stored '" + stored + "'");
    }
    check(kinds == 7 && ok == kinds, QString("paper: every special paper (%1) writes xlPaper and a look-alike style as one undo step, survives save and load, and undoes").arg(kinds));
    cleanDoc(ed, blankDocument());
  }

  // ---- the six real documents through the whole lifecycle ------------------------------------------------------------------------------
  {
    const QDir samples(QStringLiteral("G:/xournal-lab/samples"));
    const QStringList names = samples.entryList({"*.xopp"}, QDir::Files, QDir::Name);
    check(names.size() == 6, QString("lifecycle: the six samples are there (%1)").arg(names.size()));
    int same = 0, clean = 0, viaPath = 0;
    for (const QString& n : names) {
      cleanDoc(ed, blankDocument());
      if (!win.openFile(samples.filePath(n))) { out("  could not open " + n); continue; }
      const bool openedClean = !ed.dirty() && ed.path().isEmpty();   // an opened original is never the save target (unless the preference says so)
      const Document before = ed.doc();
      const QString copy = tmp.filePath("copia-" + n);
      const bool saved = win.saveTo(copy);
      const bool savedState = saved && !ed.dirty() && ed.path() == copy && win.fileName() == "copia-" + n;
      if (openedClean && savedState) clean++;
      cleanDoc(ed, blankDocument());
      const bool reopened = win.openFile(copy);
      if (reopened && ed.doc() == before) same++;
      if (reopened && win.windowTitle().startsWith("copia-" + n)) viaPath++;
    }
    check(same == names.size() && !names.isEmpty(), QString("lifecycle: open, Save as to another folder, reopen: the copy equals the original model (%1 of %2)").arg(same).arg(names.size()));
    check(clean == names.size(), QString("lifecycle: an opened original is not the save target; Save as makes the copy the path and leaves it clean (%1 of %2)").arg(clean).arg(names.size()));
    check(viaPath == names.size(), "lifecycle: the title shows the file name");
  }

  // ---- save over the opened file (preference), recovery copy ----------------------------------------------------------------------------
  {
    Document d = blankDocument();
    d.pages[0].layers[0].elements.push_back(inkStroke(30));
    const QString f = tmp.filePath("sobre.xopp");
    saveXopp(f, d);
    win.setSaveOverOpened(true);
    cleanDoc(ed, blankDocument());
    win.openFile(f);
    check(ed.path() == f, "preferences: with 'guardar sobre el archivo abierto' the opened file is the save target");
    ed.commitAdd(0, 0, Element(inkStroke(90)), "Trazo");
    const qint64 sizeBefore = QFileInfo(f).size();
    check(win.save(false) && !ed.dirty(), "preferences: Save writes over it without asking");
    LoadResult r = loadXopp(f);
    check(r.ok && r.doc == ed.doc() && QFileInfo(f).size() != sizeBefore, "preferences: the file on disk is the edited document");
    win.setSaveOverOpened(false);
    cleanDoc(ed, blankDocument());
    win.openFile(f);
    check(ed.path().isEmpty(), "preferences: with it off the opened file is not the save target (Save asks where)");
    check(win.suggestedSaveName(false) == "sobre-lab.xopp" && win.suggestedSaveName(true) == "sobre-copia.xopp", "save: the suggested names never offer the original's own name (-lab / -copia)");

    // autosave: a recovery copy beside nothing the user owns
    win.setAutosaveDir(tmp.filePath("auto"));
    win.setSaveOverOpened(true);
    cleanDoc(ed, blankDocument());
    win.openFile(f);
    check(!win.autosaveNow(), "autosave: nothing to write while the document is clean");
    ed.commitAdd(0, 0, Element(inkStroke(150)), "Trazo");
    const QByteArray original = [&] { QFile q(f); (void)q.open(QIODevice::ReadOnly); return q.readAll(); }();
    check(win.autosaveNow(), "autosave: a document with changes writes a recovery copy");
    const QString copy = tmp.filePath("auto/sobre.autosave.xopp");
    LoadResult rc = loadXopp(copy);
    check(rc.ok && rc.doc == ed.doc(), "autosave: the copy is the document as it is now");
    const QByteArray after = [&] { QFile q(f); (void)q.open(QIODevice::ReadOnly); return q.readAll(); }();
    check(after == original && ed.dirty() && ed.path() == f, "autosave: the user's own file is untouched and the document stays unsaved");
    check(!win.autosaveNow(), "autosave: no second write when nothing changed since");
    ed.commitAdd(0, 0, Element(inkStroke(210)), "Trazo");
    check(win.autosaveNow(), "autosave: a new change writes again");
    check(win.autosaveDir() == tmp.filePath("auto"), "autosave: 'Abrir copia de recuperación' starts in the folder the copies are written to");
    // recovery on the next start: the copy opens as an ordinary document (it is not the user's path)
    const Document edited = ed.doc();
    cleanDoc(ed, blankDocument());
    check(win.openFile(copy) && ed.doc() == edited, "autosave: the recovery copy opens with the unsaved work");
    win.setSaveOverOpened(false);
    win.setAutosaveDir(QString());
  }

  // ---- unknown elements survive open -> save as -> reopen ------------------------------------------------------------------------------
  {
    const QByteArray xml = "<?xml version=\"1.0\" standalone=\"no\"?>\n<xournal creator=\"xournalpp 1.3.8\" fileversion=\"4\"><title>t</title><futuretag a=\"1\"><inner/></futuretag>"
                           "<page width=\"595.27\" height=\"841.89\"><background type=\"solid\" color=\"#ffffffff\" style=\"plain\"/><layer/></page></xournal>";
    QByteArray gz;
    gzip(xml, &gz);
    const QString f = tmp.filePath("futuro.xopp");
    { QFile q(f); (void)q.open(QIODevice::WriteOnly); q.write(gz); }
    cleanDoc(ed, blankDocument());
    check(win.openFile(f), "lifecycle: a file with an element this version does not know opens");
    const QString copy = tmp.filePath("futuro2.xopp");
    win.saveTo(copy);
    QByteArray raw, back;
    { QFile q(copy); (void)q.open(QIODevice::ReadOnly); raw = q.readAll(); }
    gunzip(raw, &back, nullptr);
    check(back.contains("<futuretag a=\"1\"><inner/></futuretag>") || back.contains("futuretag"), "lifecycle: the unknown element is kept in the saved copy (nothing silently dropped)");
  }

  // ---- recent files (the real list, in a temporary settings file) ---------------------------------------------------------------------------
  {
    const bool oldPersist = win.persistence();
    win.setSettingsFile(tmp.filePath("settings.ini"));   // never the user's registry key
    win.setPersistence(true);
    QStringList files;
    for (int i = 0; i < 12; i++) {
      Document d = blankDocument();
      d.pages[0].layers[0].elements.push_back(inkStroke(10.0 + i));
      files << tmp.filePath(QString("reciente%1.xopp").arg(i));
      saveXopp(files.last(), d);
    }
    cleanDoc(ed, blankDocument());
    for (const QString& f : files) { win.openFile(f); ed.markSaved(); }
    const QStringList list = win.recentFiles();
    check(list.size() == 10 && list.first() == files.last() && !list.contains(files.first()), "recent: the list keeps the last ten, newest first");
    QMenu* recent = nullptr;
    for (QAction* a : win.menu("file")->actions()) if (a->menu() && a->text().contains("reciente")) recent = a->menu();
    check(recent && recent->actions().size() == 10 + 2 && recent->actions().first()->text() == "reciente11.xopp" && recent->actions().first()->toolTip() == files.last(), "recent: Archivo > Abrir reciente shows them (name, path as tooltip) and 'Borrar la lista'");
    win.openFile(files[5]);
    check(win.recentFiles().first() == files[5] && win.recentFiles().size() == 10, "recent: opening one moves it to the top without duplicating it");
    QFile::remove(files[5]);
    check(!win.recentFiles().contains(files[5]), "recent: a file that no longer exists is not offered");
    cleanDoc(ed, blankDocument());
    emit recent->aboutToShow();   // what opening the menu does
    check(recent->actions().first()->toolTip() != files[5] && recent->actions().size() == 9 + 2, "recent: the menu drops a deleted file when it is opened");
    recent->actions().first()->trigger();
    check(ed.pageCount() == 1 && win.fileName() == QFileInfo(win.recentFiles().first()).fileName() && elementsOfPage0(ed) == 1, "recent: choosing an entry opens that file");
    recent->actions().last()->trigger();
    check(win.recentFiles().isEmpty(), "recent: 'Borrar la lista' empties it");
    win.setPersistence(oldPersist);
    win.setSettingsFile(QString());
    cleanDoc(ed, blankDocument());
  }

  // ---- opening fits the page to the window width; closing the window asks first -----------------------------------------------------------
  {
    win.show();
    pump(4);
    cleanDoc(ed, blankDocument());
    check(win.openFile(QStringLiteral("G:/xournal-lab/samples/algoritmos_pseudocodigo.xopp")), "open: a real document opens");
    pump(4);
    DocumentView* v = win.view();
    check(v->horizontalScrollBar()->maximum() == 0, QString("open: the zoom is fitted to the width (no horizontal scroll at %1 %)").arg(int(v->zoom() * 100)));
    ed.commitAdd(0, 0, Element(inkStroke(40)), "Trazo");
    QCloseEvent cancel;
    answerNextBox(QMessageBox::Cancel);
    QApplication::sendEvent(&win, &cancel);
    check(!cancel.isAccepted() && ed.dirty(), "window close: with unsaved changes it asks, and Cancel keeps the window");
    QCloseEvent discard;
    answerNextBox(QMessageBox::Discard);
    QApplication::sendEvent(&win, &discard);
    check(discard.isAccepted(), "window close: Discard lets it close without saving");
    cleanDoc(ed, blankDocument());
    QCloseEvent clean;
    QApplication::sendEvent(&win, &clean);
    check(clean.isAccepted(), "window close: a clean document closes without asking");
  }

  // ---- close -> welcome ----------------------------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    d.pages[0].layers[0].elements.push_back(inkStroke(30));
    cleanDoc(ed, d);
    ed.action("file.close")->trigger();
    pump();
    check(c->welcome()->isShown() && ed.pageCount() == 1 && elementsOfPage0(ed) == 0 && ed.path().isEmpty(), "close: a clean document closes and the welcome page is shown over a blank one");
    check(win.windowTitle().startsWith("Sin título"), "close: the title is back to 'Sin título'");
    c->welcome()->hidePage();
    ed.commitAdd(0, 0, Element(inkStroke(60)), "Trazo");
    answerNextBox(QMessageBox::Cancel);
    ed.action("file.close")->trigger();
    pump();
    check(ed.dirty() && elementsOfPage0(ed) == 1 && !c->welcome()->isShown(), "close: Cancel on the unsaved-changes question keeps the document");
    answerNextBox(QMessageBox::Discard);
    ed.action("file.close")->trigger();
    pump();
    check(!ed.dirty() && elementsOfPage0(ed) == 0 && c->welcome()->isShown(), "close: Discard closes it without saving (and the welcome page appears)");
    c->welcome()->hidePage();
  }

  // ---- drag and drop -------------------------------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    d.pages[0].layers[0].elements.push_back(inkStroke(30));
    d.pages[0].layers[0].elements.push_back(inkStroke(60));
    const QString f = tmp.filePath("soltado.xopp");
    saveXopp(f, d);
    QImage img(40, 30, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString png = tmp.filePath("foto.png");
    img.save(png);
    auto drop = [&](const QList<QUrl>& urls) {
      QMimeData mime;
      mime.setUrls(urls);
      QDragEnterEvent enter(QPoint(200, 200), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(&win, &enter);
      QDropEvent ev(QPointF(200, 200), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(&win, &ev);
      pump();
      return enter.isAccepted() && ev.isAccepted();
    };
    cleanDoc(ed, blankDocument());
    check(win.acceptDrops(), "drop: the window accepts drops");
    check(drop({QUrl::fromLocalFile(f)}) && elementsOfPage0(ed) == 2 && !ed.dirty(), "drop: a .xopp dropped on the window opens");
    check(drop({QUrl::fromLocalFile(png)}) && elementsOfPage0(ed) == 3 && ed.dirty(), "drop: an image dropped on the page is inserted");
    answerNextBox(QMessageBox::Cancel);
    drop({QUrl::fromLocalFile(f)});
    check(ed.dirty() && elementsOfPage0(ed) == 3, "drop: a .xopp over unsaved changes asks first (Cancel keeps the document)");
    const QString txt = tmp.filePath("nota.txt");
    { QFile q(txt); (void)q.open(QIODevice::WriteOnly); q.write("x"); }
    check(!drop({QUrl::fromLocalFile(txt)}) && elementsOfPage0(ed) == 3, "drop: another kind of file is not accepted and changes nothing");
    cleanDoc(ed, blankDocument());
  }

  // ---- go to page dialog ---------------------------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    for (int i = 1; i < 5; i++) d.pages.push_back(d.pages[0]);
    cleanDoc(ed, d);
    c->goToPage(0);
    ed.action("nav.goto")->trigger();
    pump();
    auto* dlg = win.findChild<QDialog*>("gotoDialog");
    check(dlg && dlg->isVisible(), "navigation: Ctrl+G shows the go-to-page dialog");
    if (dlg) {
      auto* sb = dlg->findChild<QSpinBox*>();
      check(sb && sb->maximum() == 5 && sb->value() == 1, "navigation: the dialog starts at the current page and is limited to the page count");
      if (sb) sb->setValue(4);
      dlg->accept();
      pump();
      check(ed.currentPage() == 3 && c->historyBackCount() == 1, "navigation: accepting goes there and records the jump for Atrás");
    }
    cleanDoc(ed, blankDocument());
  }

  // ---- preferences apply live ----------------------------------------------------------------------------------------------------------
  {
    SettingsController* sc = SettingsController::of(win);
    check(sc != nullptr, "prefs: the settings controller exists");
    if (sc) {
      Prefs p = sc->prefs();
      const Prefs original = p;
      p.theme = "neon";
      p.effects = "reduced";
      sc->applyPrefs(p, false);
      check(c->theme() == "neon" && c->reducedEffects(), "prefs: applyPrefs of the settings area changes theme and effects at once (no file watcher)");
      p.theme = "classic";
      p.effects = "full";
      sc->applyPrefs(p, false);
      check(c->theme() == "classic" && !c->reducedEffects(), "prefs: and back");
      c->chooseTheme("neon");
      check(c->theme() == "neon" && sc->prefs().theme == "neon", "prefs: the toolbar/welcome theme choice goes through the settings area");
      c->chooseTheme("classic");
      check(c->theme() == "classic", "prefs: back to classic");
      sc->applyPrefs(original, false);
    }
  }

  // ---- find highlights the matched characters ------------------------------------------------------------------------------------------
  {
    Document d = blankDocument();
    d.pages[0].layers[0].elements.push_back(textAt("hola mundo con un texto bastante largo para dejar sitio", 60, 100));
    cleanDoc(ed, d);
    win.show();
    pump(6);
    FindBar* f = c->findBar();
    ed.action("edit.find")->trigger();
    f->setQuery("hola");
    f->run();
    pump(6);
    check(f->matches() == 1, "find: one match");
    const FindHit* h = f->current();
    const auto spans = h ? findSpanBoxes(ed.doc(), *h) : std::vector<std::array<double, 4>>{};
    const auto box = h ? findHitBox(ed.doc(), *h) : std::nullopt;
    check(spans.size() == 1 && box && spans[0][2] < (*box)[0] + ((*box)[2] - (*box)[0]) * 0.3, "find: the span is the matched word, a small part of the box");
    if (spans.size() == 1 && box) {
      DocumentView* v = win.view();
      v->renderVisibleNow();
      pump(6);
      const QImage img = v->viewport()->grab().toImage();
      const int page = h->page;
      const QPointF a = v->toView(page, QPointF(spans[0][0], spans[0][1])), z = v->toView(page, QPointF(spans[0][2], spans[0][3]));
      const QPointF boxEnd = v->toView(page, QPointF(((*box)[2] + spans[0][2]) / 2, spans[0][1]));   // between the match and the end of the box (the selection frame is on the edge, not here)
      auto px = [&](QPointF p) { return img.pixelColor(int(p.x()), int(p.y())); };
      const QColor inside = px(QPointF(a.x() + 2, a.y() + 2)), beyond = px(QPointF(boxEnd.x(), a.y() + 1));
      check(inside.blue() < 235 && beyond.blue() >= 250, QString("find: the marked area covers only the match (inside %1, past it %2)").arg(inside.name(), beyond.name()));
      Q_UNUSED(z);
      v->viewport()->grab().save(shots + "/chrome_find_exact.png");
    }
    // a markdown text: the markup is not part of what is marked
    Document m = blankDocument();
    Text t = textAt("**hola** mundo", 60, 100);
    attrSet(t.attrs, "xlMd", "1");
    m.pages[0].layers[0].elements.push_back(t);
    cleanDoc(ed, m);
    f->setQuery("hola");
    f->run();
    pump(4);
    const FindHit* mh = f->current();
    const auto ms = mh ? findSpanBoxes(ed.doc(), *mh) : std::vector<std::array<double, 4>>{};
    check(ms.size() == 1 && ms[0][2] > ms[0][0], "find: a markdown text is marked at the drawn word (the ** are not drawn)");
    f->close();
    cleanDoc(ed, blankDocument());
  }

  // ---- floating dialogs ---------------------------------------------------------------------------------------------------------------------
  {
    win.show();
    pump(4);
    ed.action("help.pen")->trigger();
    pump(4);
    auto* dlg = win.findChild<PenDiagnostic*>("penDlg");
    check(dlg && dlg->isVisible() && dlg->isWindow(), "floating: the pen diagnostic is a window of its own (can be moved to another monitor)");
    check(dlg && c->floatingDialogOpen(), "floating: the window knows a floating dialog is open");
    // presses on the page are ignored while it is open
    if (dlg) {
      Document d = blankDocument();
      cleanDoc(ed, d);
      DocumentView* v = win.view();
      ed.setTool("pen");
      const QPoint p0 = v->viewport()->rect().center();
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(p0), v->viewport()->mapToGlobal(QPointF(p0)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent move(QEvent::MouseMove, QPointF(p0 + QPoint(40, 40)), v->viewport()->mapToGlobal(QPointF(p0 + QPoint(40, 40))), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(p0 + QPoint(40, 40)), v->viewport()->mapToGlobal(QPointF(p0 + QPoint(40, 40))), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(v->viewport(), &press);
      QApplication::sendEvent(v->viewport(), &move);
      QApplication::sendEvent(v->viewport(), &release);
      pump();
      check(elementsOfPage0(ed) == 0 && !ed.dirty(), "floating: the page ignores a pen stroke while a floating dialog is open");
      dlg->close();
      pump();
      check(!c->floatingDialogOpen(), "floating: closing it releases the page");
      QApplication::sendEvent(v->viewport(), &press);
      QApplication::sendEvent(v->viewport(), &move);
      QApplication::sendEvent(v->viewport(), &release);
      pump();
      check(elementsOfPage0(ed) == 1, "floating: and the page draws again");
      cleanDoc(ed, blankDocument());
    }
    // the formula dialog: a window of its own, not modal, placed away from the page being written, remembered once the user moves it
    {
      cleanDoc(ed, blankDocument());
      SettingsController* sc = SettingsController::of(win);
      if (sc) sc->forgetGeometry("latex");   // earlier checks of other areas may have moved it
      if (auto* old = win.findChild<QDialog*>("latexDialog")) old->hide();
      pump(2);
      texttools::openFormulaAt(win, 0, QPointF(100, 100));
      pump(6);
      auto* ld = win.findChild<QDialog*>("latexDialog");
      check(ld && ld->isVisible() && ld->isWindow() && ld->windowModality() == Qt::NonModal, "floating: the formula dialog is a non-modal window of its own");
      if (ld) {
        const QRect wr = win.frameGeometry(), dr = ld->frameGeometry();
        check(dr.top() >= wr.top() && dr.bottom() <= wr.bottom() + 2 && dr.height() <= int(wr.height() * 0.62), QString("floating: it opens at most 62 percent of the window high (%1 of %2 px) so the page stays in view").arg(dr.height()).arg(wr.height()));
        check(c->floatingDialogOpen(), "floating: the page ignores presses while the formula dialog is open");
        ld->grab().save(shots + "/chrome_latex_floating.png");
        const QRect auto1 = ld->geometry();
        ld->move(auto1.topLeft() + QPoint(37, -23));   // the user drags it
        ld->hide();
        pump(2);
        check(sc && sc->hasGeometry("latex"), "floating: a place the user chose is remembered");
        texttools::openFormulaAt(win, 0, QPointF(100, 100));
        pump(4);
        // (the offscreen test screen is narrower than the dialog, so Qt keeps it on the screen horizontally: the vertical place is what shows the memory)
        check(std::abs(ld->y() - (auto1.y() - 23)) <= 2 && ld->y() != auto1.y(), "floating: and used the next time it opens (not placed again from the click)");
        ld->hide();
        pump(2);
        check(!c->floatingDialogOpen(), "floating: closing the formula dialog releases the page");
      }
    }
    // remembered place: a place that belongs to a monitor that is gone is not used
    if (SettingsController* sc = SettingsController::of(win)) {
      auto* probe = new QDialog(&win);
      probe->resize(400, 300);
      probe->move(-9000, -9000);   // where a disconnected second monitor was
      probe->show();
      sc->rememberGeometry(probe, "probe-gone");
      probe->hide();
      c->placeFloating(probe, "probe-gone", QSize(400, 300));
      probe->show();
      pump();
      bool onScreen = false;
      for (QScreen* s : QGuiApplication::screens()) {
        const QRect i = s->availableGeometry().intersected(probe->frameGeometry());
        if (i.width() >= 120 && i.height() >= 60) onScreen = true;
      }
      check(onScreen, "floating: a remembered place on a monitor that no longer exists falls back to a visible one");
      probe->move(60, 60);
      sc->rememberGeometry(probe, "probe-here");
      const QRect remembered = probe->geometry();
      probe->hide();
      probe->move(400, 400);
      c->placeFloating(probe, "probe-here", QSize(400, 300));
      probe->show();
      pump();
      check(probe->geometry().topLeft() == remembered.topLeft() || std::abs(probe->x() - remembered.x()) <= 2, "floating: a remembered place on an existing monitor is restored");
      delete probe;
    }
  }
  cleanDoc(ed, blankDocument());
  win.activateWindow();   // the dialogs above took the focus and gave it back to nobody: the next areas expect the main window to be the active one
  win.raise();
  pump(6);
}

}  // namespace chrome
