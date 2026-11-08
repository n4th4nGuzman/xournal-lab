// Chrome: the page-navigation bar, history, presentation mode, theme preferences, new document with a paper, and the wiring of the panels and menus.
#include "chrome.h"
#include <QPointer>
#include "docfactory.h"
#include "icons.h"
#include "palettes.h"
#include "parts.h"
#include "prefs.h"
#include "settings/settings.h"
#include "texttools/texttools.h"
#include <QApplication>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImageReader>
#include <QInputDialog>
#include <QMimeData>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QIntValidator>
#include <QKeyEvent>
#include <QMenuBar>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace chrome {
namespace {
[[maybe_unused]] QString prefsPath() { return defaultPrefsPath(); }
// The page widget as the filter must see it: a QPointer, because while the window destroys its children the view is already gone but this
// application-wide filter still gets their last events (reading win.view() then crashed on exit, depending on what was still alive).
QPointer<QWidget> g_viewport;
}  // namespace

Chrome::Chrome(MainWindow& w) : QObject(&w), win(w), ed(w.editor()) {
  setObjectName("chrome");
  win.setProperty("chromeObject", QVariant::fromValue(static_cast<QObject*>(this)));
  qApp->installEventFilter(this);
  if (win.view()) g_viewport = win.view()->viewport();
  win.setAcceptDrops(true);   // a .xopp or an image dropped anywhere on the window (the children do not accept drops, so they reach it)
  retint_.setSingleShot(true);
  connect(&retint_, &QTimer::timeout, this, [this] { retintActions(); });
  connect(&ed, &Editor::toolRegistered, this, [this](const QString& id) { originals_.erase("tool." + id); retint_.start(30); });
  connect(&ed, &Editor::actionCreated, this, [this] { retint_.start(30); });
  connect(&ed, &Editor::documentReplaced, this, [this] { resetHistory(); });
}

Chrome::~Chrome() = default;

void Chrome::applyPrefs(const Prefs& p) {
  welcomeOnStart_ = p.welcome;
  setTheme(p.theme, p.effects);
  if (paperBar_) paperBar_->setPaletteId(p.palette);
}

void Chrome::chooseTheme(const QString& theme) {
  if (SettingsController* sc = SettingsController::of(win)) {   // prefs.json belongs to the settings area: it writes what changed and tells everyone (prefsApplied -> applyPrefs)
    Prefs p = sc->prefs();
    p.theme = theme == "neon" ? "neon" : "classic";
    sc->applyPrefs(p);
    return;
  }
  setTheme(theme, effectsPref_);
}

void Chrome::setShowWelcomeOnStart(bool on) {
  welcomeOnStart_ = on;
  if (SettingsController* sc = SettingsController::of(win)) {
    Prefs p = sc->prefs();
    p.welcome = on;
    sc->applyPrefs(p);
  }
}

// ---- navigation ---------------------------------------------------------------------------------------------------------------------------
void Chrome::goToPage(int page, bool record) {
  if (ed.pageCount() == 0) return;
  const int target = std::clamp(page, 0, ed.pageCount() - 1);
  const int from = ed.currentPage();
  if (record && target != from) { back_.push_back(from); fwd_.clear(); }
  win.view()->scrollToPage(target);
  ed.setCurrentPage(target);
  updateNavActions();
}

void Chrome::historyBack() {
  if (back_.empty()) return;
  const int p = back_.back();
  back_.pop_back();
  fwd_.push_back(ed.currentPage());
  goToPage(p, false);
}

void Chrome::historyForward() {
  if (fwd_.empty()) return;
  const int p = fwd_.back();
  fwd_.pop_back();
  back_.push_back(ed.currentPage());
  goToPage(p, false);
}

void Chrome::updateNavActions() {
  ed.action("nav.back")->setEnabled(!back_.empty());
  ed.action("nav.forward")->setEnabled(!fwd_.empty());
  if (pageEdit_) {
    if (!pageEdit_->hasFocus()) pageEdit_->setText(QString::number(ed.currentPage() + 1));
    pageTotal_->setText(QString::number(ed.pageCount()));
  }
}

void Chrome::buildNavBar() {
  // The first row of the window (Electron's #bar): file, history, clipboard, side panels, page navigation, page operations, zoom, theme and paper.
  // The window owns the toolbar; this puts the buttons in the order of index.html.
  navBar_ = win.topBar();
  auto put = [this](QAction* a, const QString& id, const QString& tip) {
    if (!tip.isEmpty()) a->setToolTip(tip);
    a->setProperty("chromeId", id);
    navBar_->addAction(a);
    return a;
  };
  put(ed.action("file.open"), "file.open", "Abrir (Ctrl+O)");
  put(ed.action("file.save"), "file.save", "Guardar (Ctrl+S): la primera vez pregunta dónde");
  navBar_->addSeparator();
  put(ed.action("edit.undo"), "edit.undo", "Deshacer (Ctrl+Z)");
  put(ed.action("edit.redo"), "edit.redo", "Rehacer (Ctrl+Y)");
  navBar_->addSeparator();
  put(ed.action("edit.cut"), "edit.cut", "Cortar (Ctrl+X)");
  put(ed.action("edit.copy"), "edit.copy", "Copiar la selección (Ctrl+C)");
  put(ed.action("edit.paste"), "edit.paste", "Pegar (Ctrl+V)");
  put(ed.action("edit.duplicate"), "edit.duplicate", "Duplicar (Ctrl+D)");
  navBar_->addSeparator();
  // sidebars
  put(ed.action("view.sidebarPages"), "view.sidebarPages", "Barra lateral de miniaturas de páginas");
  put(ed.action("view.sidebarLayers"), "view.sidebarLayers", "Barra lateral de capas");
  navBar_->addSeparator();
  // pages
  put(ed.action("nav.first"), "nav.first", "Primera página (Ctrl+Inicio)");
  put(ed.action("nav.prev"), "nav.prev", "Página anterior (Re Pág)");
  pageEdit_ = new QLineEdit(navBar_);
  pageEdit_->setObjectName("pgNum");
  pageEdit_->setFixedWidth(50);
  pageEdit_->setAlignment(Qt::AlignCenter);
  pageEdit_->setToolTip("Ir a la página");
  pageEdit_->setValidator(new QIntValidator(1, 99999, pageEdit_));
  navBar_->addWidget(pageEdit_);
  auto* of = new QLabel("de", navBar_);
  of->setObjectName("pgOf");
  of->setContentsMargins(4, 0, 2, 0);
  navBar_->addWidget(of);
  pageTotal_ = new QLabel("0", navBar_);
  pageTotal_->setObjectName("pgTotal");
  QFont bold = pageTotal_->font();
  bold.setBold(true);
  pageTotal_->setFont(bold);
  pageTotal_->setContentsMargins(0, 0, 4, 0);
  navBar_->addWidget(pageTotal_);
  put(ed.action("nav.next"), "nav.next", "Página siguiente (Av Pág)");
  put(ed.action("nav.last"), "nav.last", "Última página (Ctrl+Fin)");
  put(ed.action("nav.back"), "nav.back", "Volver a la página anterior del historial (Alt+←)");
  put(ed.action("nav.forward"), "nav.forward", "Avanzar en el historial (Alt+→)");
  navBar_->addSeparator();
  // page operations
  put(ed.action("journal.newPage"), "journal.newPage", "Nueva página después de la actual");
  put(ed.action("journal.insertBefore"), "journal.insertBefore", "Insertar una página antes de la actual");
  put(ed.action("journal.append"), "journal.append", "Añadir una página al final");
  put(ed.action("journal.delete"), "journal.delete", "Eliminar la página actual");
  put(ed.action("journal.moveUp"), "journal.moveUp", "Mover la página hacia arriba");
  put(ed.action("journal.moveDown"), "journal.moveDown", "Mover la página hacia abajo");
  navBar_->addSeparator();
  // zoom, theme and paper
  put(ed.action("view.zoomOut"), "view.zoomOut", "Alejar (Ctrl+-)");
  navBar_->addWidget(win.zoomLabel());
  put(ed.action("view.zoomIn"), "view.zoomIn", "Acercar (Ctrl++)");
  put(ed.action("view.fitWidth"), "view.fitWidth", "Ajustar al ancho");
  QAction* th = ed.addCommand("chrome.theme", "Tema", {}, [this] { chooseTheme(theme_ == "neon" ? "classic" : "neon"); });
  put(th, "chrome.theme", "Cambiar de tema: clásico / neon");
  QAction* paper = ed.addCommand("chrome.paper", "Papel", {}, [] {});
  paper->setCheckable(true);
  put(paper, "chrome.paper", "Papel: fondo, color y formato de la hoja");
  auto* spacer = new QWidget(navBar_);   // Electron's #info has margin-left:auto: the message area is at the right end of the row
  spacer->setObjectName("barSpacer");
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  navBar_->addWidget(spacer);
  navBar_->addWidget(win.infoWidget());
  retint_.start(0);

  auto go = [this](int delta) { return [this, delta] { goToPage(ed.currentPage() + delta); }; };
  ed.addCommand("nav.first", "Primera página", QKeySequence("Ctrl+Home"), [this] { goToPage(0); });
  ed.addCommand("nav.prev", "Página anterior", QKeySequence(Qt::Key_PageUp), go(-1));
  ed.addCommand("nav.next", "Página siguiente", QKeySequence(Qt::Key_PageDown), go(+1));
  ed.addCommand("nav.last", "Última página", QKeySequence("Ctrl+End"), [this] { goToPage(ed.pageCount() - 1); });
  ed.addCommand("nav.back", "Atrás", QKeySequence("Alt+Left"), [this] { historyBack(); });
  ed.addCommand("nav.forward", "Adelante", QKeySequence("Alt+Right"), [this] { historyForward(); });
  ed.addCommand("nav.goto", "Ir a la página…", QKeySequence("Ctrl+G"), [this] { gotoDialog(); });
  connect(pageEdit_, &QLineEdit::editingFinished, this, [this] {
    bool ok = false;
    const int n = pageEdit_->text().toInt(&ok);
    if (ok) goToPage(n - 1);
    pageEdit_->setText(QString::number(ed.currentPage() + 1));
    if (win.view()) win.view()->setFocus();
  });
  connect(&ed, &Editor::currentPageChanged, this, [this] { updateNavActions(); });
  connect(&ed, &Editor::structureChanged, this, [this] { updateNavActions(); });
  connect(&ed, &Editor::documentReplaced, this, [this] { updateNavActions(); });
  updateNavActions();
}

// ---- sidebars -----------------------------------------------------------------------------------------------------------------------------
static QTabWidget* dockTabs(MainWindow& w) {
  auto* d = w.findChild<QDockWidget*>("dock");
  return d ? qobject_cast<QTabWidget*>(d->widget()) : nullptr;
}

bool Chrome::sidebarShows(const QString& id) const {
  auto* d = win.findChild<QDockWidget*>("dock");
  QTabWidget* t = dockTabs(win);
  return d && t && d->isVisible() && t->currentWidget() && t->currentWidget()->objectName() == id;
}

void Chrome::showSidebar(const QString& id) {
  QTabWidget* t = dockTabs(win);
  auto* d = win.findChild<QDockWidget*>("dock");
  if (!t || !d) return;
  for (int i = 0; i < t->count(); i++) if (t->widget(i)->objectName() == id) t->setCurrentIndex(i);
  ed.action("view.sidebar")->setChecked(true);
  d->show();
  if (thumbs_) thumbs_->syncCurrent();
  if (layers_) layers_->refresh();
}

void Chrome::toggleSidebar(const QString& id) {
  if (sidebarShows(id)) { ed.action("view.sidebar")->setChecked(false); if (auto* d = win.findChild<QDockWidget*>("dock")) d->hide(); }
  else showSidebar(id);
}

// ---- presentation --------------------------------------------------------------------------------------------------------------------------
void Chrome::setPresenting(bool on) {
  if (on == presenting_) return;
  presenting_ = on;
  auto* dock = win.findChild<QDockWidget*>("dock");
  if (on) {
    hidden_.toolbar = win.toolBar()->isVisible();   // the rows of the window all hide together (styleBar follows toolBar)
    hidden_.nav = navBar_->isVisible();
    hidden_.paper = paperBar_->isVisible();
    hidden_.find = find_->isVisible();
    hidden_.menu = win.titleBar()->isVisible();
    hidden_.dock = dock && dock->isVisible();
    hidden_.fullscreen = win.isFullScreen();
    hidden_.colorbar = win.colorBar()->isVisible();
    if (hidden_.colorbar) win.colorBar()->hide();   // only when it is shown: touching a hidden tool bar disturbs the others' visibility
    win.toolBar()->hide(); navBar_->hide(); paperBar_->hide(); find_->hide();
    win.titleBar()->hide();
    if (dock) dock->hide();
    if (!presentChip_) {
      presentChip_ = new QPushButton("Modo presentación · Esc para salir ×", win.view());
      presentChip_->setObjectName("presentChip");
      presentChip_->setCursor(Qt::PointingHandCursor);
      presentChip_->setToolTip("Salir del modo presentación (Esc)");
      connect(presentChip_, &QPushButton::clicked, this, [this] { setPresenting(false); });
    }
    presentChip_->adjustSize();
    presentChip_->move(win.view()->width() - presentChip_->width() - 24, 12);
    presentChip_->show();
    presentChip_->raise();
    // The chip is what the other parts of the window (Preferencias > "pantalla completa muestra...") look at to tell a presentation from a plain fullscreen, so it
    // must already be shown when the window goes fullscreen and still shown when it comes back.
    if (!hidden_.fullscreen) win.showFullScreen();
  } else {
    win.titleBar()->setVisible(hidden_.menu);
    win.toolBar()->setVisible(hidden_.toolbar);
    if (hidden_.colorbar && win.colorSidebar()) win.colorBar()->show();
    navBar_->setVisible(hidden_.nav);
    paperBar_->setVisible(hidden_.paper);
    find_->setVisible(hidden_.find);
    if (dock) dock->setVisible(hidden_.dock);
    if (!hidden_.fullscreen && win.isFullScreen()) win.showNormal();
    if (presentChip_) presentChip_->hide();
  }
}

bool Chrome::floatingDialogOpen() const {
  for (const char* n : {"latexDialog", "prefsDlg", "tabletDlg", "penDlg"})
    if (auto* d = win.findChild<QDialog*>(n)) if (d->isVisible()) return true;
  return false;
}

void Chrome::placeFloating(QDialog* d, const QString& key, const QSize& fallback) {
  SettingsController* sc = SettingsController::of(win);
  if (sc && sc->hasGeometry(key)) {
    sc->restoreGeometry(d, key, fallback);   // a monitor that is gone leaves it inside a screen that exists
    d->setProperty("xlAutoGeom", QRect());
    return;
  }
  const QRect g = win.isVisible() ? win.frameGeometry() : QGuiApplication::primaryScreen()->availableGeometry();
  const QRect avail = (win.screen() ? win.screen() : QGuiApplication::primaryScreen())->availableGeometry();
  const QSize sz = fallback.boundedTo(QSize(int(g.width() * 0.94), int(g.height() * 0.6)));
  const bool clickWasTop = lastPress_.y() < g.center().y();   // the page part being written stays in view: the dialog goes to the other half
  const int margin = 24;
  QPoint p(g.center().x() - sz.width() / 2, clickWasTop ? g.bottom() - sz.height() - margin : g.top() + 90);
  p.setX(std::clamp(p.x(), avail.left(), std::max(avail.left(), avail.right() - sz.width())));
  p.setY(std::clamp(p.y(), avail.top(), std::max(avail.top(), avail.bottom() - sz.height())));
  d->resize(sz);
  d->move(p);
  d->setProperty("xlAutoGeom", d->geometry());
  // the layout may still grow the window to its minimum size: that is not the user's doing, so the reference is taken once it has settled
  QTimer::singleShot(0, d, [d] { if (d->isVisible() && d->property("xlAutoGeom").toRect() != QRect()) d->setProperty("xlAutoGeom", d->geometry()); });
}

void Chrome::rememberFloating(QDialog* d, const QString& key) {
  const QRect autoPlace = d->property("xlAutoGeom").toRect();
  if (!autoPlace.isNull() && d->geometry() == autoPlace) return;   // left where it was put: next time it is placed again from the click
  if (SettingsController* sc = SettingsController::of(win)) sc->rememberGeometry(d, key);
}

bool Chrome::eventFilter(QObject* o, QEvent* e) {
  if (e->type() == QEvent::Show && o->isWidgetType() && static_cast<QWidget*>(o)->isWindow()) {
    auto* w = static_cast<QWidget*>(o);
    applyTitleBar(w);   // dialogs and floating panels too
    if (auto* d = qobject_cast<QDialog*>(w)) {
      if (d->objectName() == "latexDialog" && !e->spontaneous()) placeFloating(d, "latex", QSize(860, 640));
      else if (d->objectName() == "penDlg" && !e->spontaneous()) placeFloating(d, "pen", QSize(560, 520));
    }
  }
  if (e->type() == QEvent::Hide && o->isWidgetType() && !e->spontaneous())
    if (auto* d = qobject_cast<QDialog*>(static_cast<QWidget*>(o)))
      { if (d->objectName() == "latexDialog") rememberFloating(d, "latex"); else if (d->objectName() == "penDlg") rememberFloating(d, "pen"); }
  // the page: remember where the last press was, and let none through while a floating dialog is open
  if (g_viewport && o == g_viewport.data()) {
    switch (e->type()) {
      case QEvent::MouseButtonPress: case QEvent::MouseButtonDblClick: case QEvent::TabletPress: case QEvent::TouchBegin:
        lastPress_ = e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick ? static_cast<QMouseEvent*>(e)->globalPosition().toPoint()
                   : e->type() == QEvent::TabletPress ? static_cast<QTabletEvent*>(e)->globalPosition().toPoint() : lastPress_;
        if (floatingDialogOpen()) return true;
        break;
      case QEvent::MouseButtonRelease: case QEvent::MouseMove: case QEvent::TabletMove: case QEvent::TabletRelease: case QEvent::TouchUpdate: case QEvent::TouchEnd:
        if (floatingDialogOpen() && (e->type() != QEvent::MouseMove || static_cast<QMouseEvent*>(e)->buttons() != Qt::NoButton)) return true;
        break;
      default: break;
    }
  }
  if (o == &win) {
    const auto type = e->type();
    if (type == QEvent::DragEnter || type == QEvent::DragMove) {
      auto* d = static_cast<QDropEvent*>(e);
      const QMimeData* m = d->mimeData();
      bool wanted = m->hasImage();   // a picture, or a .xopp / picture file: anything else shows the "no drop" cursor
      for (const QUrl& u : m->urls())
        if (u.isLocalFile() && (u.toLocalFile().endsWith(".xopp", Qt::CaseInsensitive) || QImageReader::imageFormat(u.toLocalFile()) != QByteArray())) wanted = true;
      if (wanted) { d->acceptProposedAction(); return true; }
      return false;
    } else if (type == QEvent::Drop) {
      auto* d = static_cast<QDropEvent*>(e);
      const QMimeData* m = d->mimeData();
      QStringList files;
      for (const QUrl& u : m->urls()) if (u.isLocalFile()) files << u.toLocalFile();
      const QImage img = files.isEmpty() && m->hasImage() ? qvariant_cast<QImage>(m->imageData()) : QImage();
      if (handleDrop(files, img)) { d->acceptProposedAction(); return true; }
    }
  }
  // Leaving presentation must always work, also from a text field or a dialog
  if (presenting_ && e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) { setPresenting(false); return true; }
  return QObject::eventFilter(o, e);
}

// ---- documents ----------------------------------------------------------------------------------------------------------------------------
void Chrome::newWithPaper(const QString& value) {
  const int before = ed.changeCount();
  win.newDocument();                        // asks about unsaved changes itself
  if (ed.changeCount() == before) return;   // cancelled
  Page& pg = ed.doc().pages[0];
  if (value.startsWith("x:")) {
    const PaperKind* k = paperKind(value.mid(2));
    const auto spec = defaultSpec(value.mid(2));
    if (k && spec) { attrSet(pg.background, "style", k->fallback); attrSet(pg.background, "xlPaper", formatSpec(*spec)); }
  } else attrSet(pg.background, "style", value);
  ed.touch(0);   // a new document with a paper is still a clean document: no undo step, nothing to save yet
  if (paperBar_) paperBar_->sync();
}

void Chrome::closeDocument() {
  if (!win.maybeSave()) return;
  win.blankNow();
  welcome_->showPage();
}

bool Chrome::handleDrop(const QStringList& files, const QImage& image) {
  if (files.isEmpty()) return !image.isNull() && texttools::insertImage(ed, image);
  const QString f = files.first();   // like the Electron app: the first file
  const bool isImage = QImageReader::imageFormat(f) != QByteArray();
  if (isImage && !f.endsWith(".xopp", Qt::CaseInsensitive)) { welcome_->hidePage(); return texttools::insertImageFile(ed, f); }
  if (!f.endsWith(".xopp", Qt::CaseInsensitive)) { win.showMessage("Solo se pueden abrir archivos .xopp (o soltar una imagen)", 6000); return true; }
  if (win.maybeSave()) win.openFile(f);
  return true;
}

void Chrome::gotoDialog() {
  if (ed.pageCount() == 0) return;
  auto* dlg = new QInputDialog(&win);
  dlg->setObjectName("gotoDialog");
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle("Ir a la página");
  dlg->setLabelText(QString("Página (1 a %1):").arg(ed.pageCount()));
  dlg->setInputMode(QInputDialog::IntInput);
  dlg->setIntRange(1, ed.pageCount());
  dlg->setIntValue(ed.currentPage() + 1);
  dlg->setOkButtonText("Ir");
  dlg->setCancelButtonText("Cancelar");
  connect(dlg, &QInputDialog::intValueSelected, this, [this](int n) { goToPage(n - 1); if (win.view()) win.view()->setFocus(); });
  dlg->open();
  if (auto* sb = dlg->findChild<QSpinBox*>()) { sb->setMinimumHeight(40); sb->selectAll(); }
}

bool Chrome::applyTitleBar(QWidget* w) {
#ifdef Q_OS_WIN
  if (!w || !w->isWindow() || QGuiApplication::platformName() != QLatin1String("windows")) return false;
  using SetAttr = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  static const SetAttr setAttr = [] { HMODULE m = LoadLibraryW(L"dwmapi.dll"); return m ? reinterpret_cast<SetAttr>(reinterpret_cast<void*>(GetProcAddress(m, "DwmSetWindowAttribute"))) : nullptr; }();
  if (!setAttr) return false;
  const HWND h = reinterpret_cast<HWND>(w->winId());
  const bool neonOn = theme_ == "neon";
  const BOOL dark = TRUE;   // both themes are dark: the buttons and the frame are drawn for a dark caption (Windows 10 2004+ and 11)
  const bool ok = SUCCEEDED(setAttr(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark));
  const COLORREF caption = neonOn ? RGB(0x1e, 0x10, 0x43) : RGB(0x2b, 0x2b, 0x2b), text = neonOn ? RGB(0x05, 0xe1, 0xff) : RGB(0xdd, 0xdd, 0xdd), border = neonOn ? RGB(0xff, 0x2a, 0x9d) : RGB(0x1b, 0x1b, 0x1b);
  setAttr(h, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);   // Windows 11 only: ignored elsewhere
  setAttr(h, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof text);
  setAttr(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
  return ok;
#else
  Q_UNUSED(w);
  return false;
#endif
}

void Chrome::installMenus() {
  QMenu* file = win.menu("file");
  QAction* nw = ed.addCommand("file.newWindow", "Nueva ventana", QKeySequence("Ctrl+Shift+N"), [] { QProcess::startDetached(QApplication::applicationFilePath(), {}); });
  if (file) {
    const auto acts = file->actions();
    const int at = int(acts.indexOf(ed.action("file.open")));
    if (at >= 0) file->insertAction(ed.action("file.open"), nw); else file->addAction(nw);
  }
  QMenu* plugins = win.addMenu("plugins", "&Plugins", "help");
  QAction* pm = plugins->addAction("Administrar plugins (no disponible)");
  pm->setEnabled(false);

  // the theme is a View entry too (the toolbar button and the welcome page toggle it)
  if (QMenu* view = win.menu("view")) {
    QMenu* tm = view->addMenu("Tema");
    auto* grp = new QActionGroup(tm);
    for (const auto& [id, label] : {std::pair<QString, QString>{"classic", "Clásico (oscuro)"}, {"neon", "Neon (synthwave, con brillo)"}}) {
      QAction* a = tm->addAction(label);
      a->setCheckable(true);
      a->setActionGroup(grp);
      a->setData(id);
      connect(a, &QAction::triggered, this, [this, id = id] { chooseTheme(id); });
    }
    connect(this, &Chrome::themeChanged, tm, [tm](const QString& t) { for (QAction* a : tm->actions()) a->setChecked(a->data().toString() == t); });
    tm->actions().first()->setChecked(true);
  }
  ed.addCommand("file.close", "Cerrar", QKeySequence("Ctrl+W"), [this] { closeDocument(); });
  ed.addCommand("help.welcome", "Bienvenida", {}, [this] { welcome_->showPage(); });
  ed.addCommand("help.pen", "Diagnóstico del lápiz…", {}, [this] { diag_->openDialog(); });
  ed.addCommand("view.presentation", "Modo de presentación", QKeySequence("F5"), [this] { setPresenting(!presenting_); });
  ed.addCommand("view.sidebarPages", "Vista previa de páginas", {}, [this] { toggleSidebar("pages"); });
  ed.addCommand("view.sidebarLayers", "Capas", {}, [this] { toggleSidebar("layers"); });
  // the main toolbar toggle also covers the page bar
  connect(ed.action("view.toolbars"), &QAction::toggled, this, [this](bool on) { navBar_->setVisible(on); if (!on) paperBar_->hide(); });
}

}  // namespace chrome

