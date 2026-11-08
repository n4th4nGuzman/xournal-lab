#include "mainwindow.h"
#include "docfactory.h"
#include "icons.h"
#include "optionspanel.h"
#include "paper.h"
#include "chrome/svgicon.h"
#include "repair.h"
#include "palettes.h"
#include "sizes.h"
#include "xopp.h"
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QComboBox>
#include <QCursor>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif
#include <QTime>
#include <QStyle>
#include <QMouseEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QStyleOptionToolButton>
#include <functional>
#include <QToolButton>
#include <QWidgetAction>

namespace {

/** A tool button with a small caret in its bottom-right corner (Electron's .split > .splitOpt): the caret runs a command at once (the pen's opens Lápiz y tableta,
 *  a tool with options shows its panel); the rest of the button is the tool itself. */
class SplitButton : public QToolButton {
 public:
  using QToolButton::QToolButton;
  std::function<void()> onCaret;
  static constexpr int kBox = 14;
  QRect caretRect() const { return QRect(width() - kBox - 1, height() - kBox - 1, kBox, kBox); }
 protected:
  void mousePressEvent(QMouseEvent* e) override {
    if (e->button() == Qt::LeftButton && onCaret && caretRect().contains(e->position().toPoint())) { e->accept(); onCaret(); return; }
    QToolButton::mousePressEvent(e);
  }
  void paintEvent(QPaintEvent* e) override {
    QToolButton::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool neon = chrome::svgThemeNeon();
    const QRect r = caretRect();
    const bool hot = underMouse();
    p.setPen(QPen(isChecked() ? (neon ? QColor(0xff, 0x2a, 0x9d) : QColor(0x6a, 0xa0, 0xff)) : hot ? (neon ? QColor(0xff, 0x2a, 0x9d) : QColor(0x88, 0x88, 0x88)) : QColor(Qt::transparent), 1));
    p.setBrush(neon ? QColor(15, 8, 36, 204) : QColor(0, 0, 0, hot ? 178 : 115));
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    chrome::paintSvgIcon(p, "caret", QRectF(r).adjusted(2, 2, -2, -2), neon ? QColor(0x05, 0xe1, 0xff) : QColor(0xd8, 0xd8, 0xd8), QColor(), 2.4);
  }
};
/** Paints the colour swatches and the custom-colour box of the toolbar: a style-sheet-styled QToolButton never paints an icon bigger than its icon size, and the
 *  Electron circles (30 px in a 34 px button) are bigger. The button itself draws nothing (its box is transparent by style sheet). */
class SwatchPainter : public QObject {
 public:
  explicit SwatchPainter(QToolButton* b) : QObject(b), b_(b) {}
 protected:
  bool eventFilter(QObject*, QEvent* e) override {
    if (e->type() != QEvent::Paint) return false;
    QPainter p(b_);
    if (b_->objectName() == "customColor") chrome::paintColourBox(p, QRectF(b_->rect()), QColor(b_->property("xlColour").toString()));
    else if (const QAction* a = b_->defaultAction()) chrome::paintSwatch(p, QRectF(b_->rect()), QColor(a->data().toString()), a->isChecked(), b_->isEnabled());
    return true;
  }
 private:
  QToolButton* b_;
};
const QString kApp = QStringLiteral("Xournal Lab");

SizeTool sizeToolOf(const QString& id) {
  if (id == "pen") return SizeTool::Pen;
  if (id == "highlighter") return SizeTool::Highlighter;
  if (id == "eraser") return SizeTool::Eraser;
  if (id == "eraserPart") return SizeTool::EraserPart;
  if (id == "text") return SizeTool::Text;
  if (id == "shape") return SizeTool::Shape;
  if (id == "figure") return SizeTool::Figure;
  if (id == "plane") return SizeTool::Plane;
  if (id == "space3") return SizeTool::Space3;
  if (id == "numline") return SizeTool::NumLine;
  if (id == "venn") return SizeTool::Venn;
  return SizeTool::Other;
}

// The size buttons are always there: tools with their own sizes show those, every other tool (selection, hand, fill, formula...) shows the pen's,
// so a width can be chosen without going to the pen first.
SizeKind barSizeKind(const QString& toolId) {
  SizeKind k;
  return sizeKindOf(sizeToolOf(toolId), &k) ? k : SizeKind::Pen;
}

}  // namespace

MainWindow::MainWindow(Editor& ed, QWidget* parent) : QMainWindow(parent), ed_(ed) {
  setWindowTitle(kApp);
  setWindowFlag(Qt::FramelessWindowHint, true);   // the window draws its own title bar (TitleBar); Windows still gets caption, snap and resize edges from nativeEvent
  titleBar_ = new TitleBar(this);
  setMenuWidget(titleBar_);
  view_ = new DocumentView(ed_, this);
  setCentralWidget(view_);

  topBar_ = addToolBar(QStringLiteral("Archivo y páginas"));
  topBar_->setObjectName("navbar");
  topBar_->setMovable(false);
  topBar_->setFloatable(false);
  topBar_->setIconSize(QSize(20, 20));
  topBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  addToolBarBreak(Qt::TopToolBarArea);
  toolbar_ = addToolBar(QStringLiteral("Herramientas"));
  toolbar_->setObjectName("toolbar");
  toolbar_->setMovable(false);
  toolbar_->setIconSize(QSize(20, 20));
  toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  styleBar_ = addToolBar(QStringLiteral("Estilo"));
  styleBar_->setObjectName("stylebar");
  styleBar_->setMovable(false);
  styleBar_->setIconSize(QSize(20, 20));
  styleBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  connect(toolbar_, &QToolBar::visibilityChanged, styleBar_, &QToolBar::setVisible);   // one switch (View > toolbars, presentation mode) for both rows
  colorBar_ = new QToolBar(QStringLiteral("Colores"), this);   // Xournal++'s colour column; empty and hidden until setColorSidebar(true)
  colorBar_->setObjectName("colorbar");
  colorBar_->setMovable(false);
  colorBar_->setFloatable(false);
  colorBar_->setIconSize(QSize(20, 20));
  colorBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  addToolBar(Qt::LeftToolBarArea, colorBar_);
  colorBar_->hide();
  styleLead_ = new QAction(this);
  styleLead_->setVisible(false);
  sizeMarker_ = new QAction(this);
  sizeMarker_->setVisible(false);

  dock_ = new QDockWidget(QStringLiteral("Paneles"), this);
  dock_->setObjectName("dock");
  dockTabs_ = new QTabWidget(dock_);
  dockTabs_->setDocumentMode(true);
  dock_->setWidget(dockTabs_);
  dock_->setTitleBarWidget(new QWidget(dock_));            // Electron's side panel starts with its tab row: no "Paneles" title, no float button
  dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  dock_->setMinimumWidth(176);                             // #side { width: 176px }
  dock_->setMaximumWidth(176);
  dockTabs_->tabBar()->setExpanding(true);
  dockTabs_->tabBar()->setDrawBase(false);
  {   // the "×" of the tab row (#sideClose)
    auto* x = new QToolButton(dockTabs_);
    x->setObjectName("sideClose");
    x->setText(QStringLiteral("×"));
    x->setToolTip(QStringLiteral("Cerrar la barra lateral"));
    x->setAutoRaise(true);
    dockTabs_->setCornerWidget(x, Qt::TopRightCorner);
    connect(x, &QToolButton::clicked, this, [this] { ed_.action("view.sidebar")->setChecked(false); });
  }
  addDockWidget(Qt::LeftDockWidgetArea, dock_);
  dock_->hide();   // nothing to show until a panel is added

  zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
  zoomLabel_->setObjectName("zoomLabel");
  zoomLabel_->setAlignment(Qt::AlignCenter);
  zoomLabel_->setMinimumWidth(42);
  infoLabel_ = new InfoLabel(this);
  infoTimer_.setSingleShot(true);
  connect(&infoTimer_, &QTimer::timeout, this, [this] { updateInfoSummary(); });

  rebuildTimer_.setSingleShot(true);
  messageTimer_.setSingleShot(true);
  connect(&rebuildTimer_, &QTimer::timeout, this, [this] { rebuildToolbar(); rebuildToolsMenu(); });
  connect(&messageTimer_, &QTimer::timeout, this, [this] { infoLabel_->setMessage(QString()); });
  connect(&autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosaveNow);

  // every action the editor creates (now or later) also belongs to this window, so its shortcut works even when no menu shows it
  for (const auto& kv : ed_.actions()) addAction(kv.second);
  connect(&ed_, &Editor::actionCreated, this, [this](QAction* a) { addAction(a); });
  connect(&ed_, &Editor::notice, this, [this](const QString& m) { showMessage(m); });
  connect(&ed_, &Editor::toolRegistered, this, [this] { scheduleToolbarRebuild(); });
  connect(&ed_, &Editor::toolChanged, this, [this] { rebuildSizeButtons(); updateStatus(); });
  connect(&ed_, &Editor::currentPageChanged, this, [this] { updateStatus(); infoTimer_.start(120); });
  connect(&ed_, &Editor::structureChanged, this, [this] { updateStatus(); infoTimer_.start(120); });
  connect(&ed_, &Editor::documentReplaced, this, [this] { updateTitle(); updateStatus(); infoTimer_.start(0); });
  connect(&ed_, &Editor::dirtyChanged, this, [this] { updateTitle(); });
  connect(&ed_, &Editor::xppChanged, this, [this] { updateTitle(); });
  connect(&ed_, &Editor::currentPageChanged, this, [this] { if (ed_.xpp().titlePageNumber) updateTitle(); });
  connect(&ed_, &Editor::historyChanged, this, [this] {
    infoTimer_.start(250);   // a stroke changes the element count: coalesced, never on the pointer path
    ed_.action("edit.undo")->setEnabled(ed_.canUndo());
    ed_.action("edit.redo")->setEnabled(ed_.canRedo());
    ed_.action("edit.undo")->setToolTip(ed_.canUndo() ? QStringLiteral("Deshacer: ") + ed_.undoLabel() : QStringLiteral("Deshacer"));
    ed_.action("edit.redo")->setToolTip(ed_.canRedo() ? QStringLiteral("Rehacer: ") + ed_.redoLabel() : QStringLiteral("Rehacer"));
  });
  connect(&ed_, &Editor::styleChanged, this, [this] {
    for (QAction* a : colorActions_) a->setChecked(a->data().toString().compare(ed_.style().color, Qt::CaseInsensitive) == 0);
    rebuildSizeButtons();
  });
  connect(view_, &DocumentView::zoomChanged, this, [this] { updateStatus(); });

  buildActions();
  buildMenus();
  rebuildToolbar();
  rebuildToolsMenu();
  rebuildRecentMenu();
  updateTitle();
  updateStatus();
  updateInfoSummary();
  resize(1280, 900);
  ed_.setTool("pen");
}

MainWindow::~MainWindow() = default;

QAction* MainWindow::act(const QString& id, const QString& text, const QKeySequence& sc, std::function<void()> fn) { return ed_.addCommand(id, text, sc, std::move(fn)); }

QAction* MainWindow::stub(const QString& id, const QString& text, const QKeySequence& sc) {
  QAction* a = ed_.action(id);
  if (a->text().isEmpty()) a->setText(text);
  if (!sc.isEmpty() && a->shortcut().isEmpty()) a->setShortcut(sc);
  return a;   // stays disabled until an area calls Editor::addCommand with the same id
}

// ---- commands ------------------------------------------------------------------------------------------------------------------------------
void MainWindow::buildActions() {
  auto cur = [this] { return ed_.currentPage(); };
  act("file.new", "Nuevo", QKeySequence::New, [this] { newDocument(); })->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  act("file.open", "Abrir…", QKeySequence::Open, [this] {
    if (!maybeSave()) return;
    const QString p = QFileDialog::getOpenFileName(this, "Abrir", lastDir_, "Xournal++ (*.xopp)");
    if (!p.isEmpty()) openFile(p);
  })->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  act("file.openAutosave", "Abrir copia de recuperación…", {}, [this] {
    if (!maybeSave()) return;
    const QString p = QFileDialog::getOpenFileName(this, "Abrir copia de recuperación", autosaveDir(), "Xournal++ (*.xopp)");
    if (!p.isEmpty()) openFile(p);
  });
  act("file.save", "Guardar", QKeySequence::Save, [this] { save(false); })->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  act("file.saveAs", "Guardar como…", QKeySequence("Ctrl+Shift+S"), [this] { save(true); });
  act("file.saveCopy", "Guardar copia…", {}, [this] {
    const QString p = QFileDialog::getSaveFileName(this, "Guardar copia", (lastDir_.isEmpty() ? QString() : lastDir_ + "/") + suggestedSaveName(true), "Xournal++ (*.xopp)");   // the path and the "unsaved" state stay
    if (p.isEmpty()) return;
    QString err;
    const QString path = p.endsWith(".xopp", Qt::CaseInsensitive) ? p : p + ".xopp";
    if (!saveXopp(path, ed_.doc(), &err)) QMessageBox::critical(this, kApp, "No se pudo guardar la copia:\n" + err);
    else showMessage("Copia guardada: " + path);
  });
  stub("file.exportPdf", "Exportar como PDF…");
  act("file.close", "Cerrar", QKeySequence("Ctrl+W"), [this] { close(); });
  act("file.quit", "Salir", QKeySequence("Ctrl+Q"), [this] { close(); });

  act("edit.undo", "Deshacer", QKeySequence::Undo, [this] { ed_.undo(); })->setIcon(Icons::make("undo"));
  act("edit.redo", "Rehacer", QKeySequence("Ctrl+Y"), [this] { ed_.redo(); })->setIcon(Icons::make("redo"));
  ed_.action("edit.redo")->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
  ed_.action("edit.undo")->setEnabled(false);
  ed_.action("edit.redo")->setEnabled(false);
  stub("edit.cut", "Cortar", QKeySequence::Cut);
  stub("edit.copy", "Copiar", QKeySequence::Copy);
  stub("edit.paste", "Pegar", QKeySequence::Paste);
  stub("edit.delete", "Eliminar", QKeySequence::Delete);
  stub("edit.find", "Buscar…", QKeySequence::Find);
  stub("edit.prefs", "Preferencias…", QKeySequence("Ctrl+,"));

  act("view.zoomIn", "Acercar", QKeySequence("Ctrl++"), [this] { view_->zoomBy(1.0 + ed_.xpp().zoomStep / 100.0); })->setIcon(Icons::make("zoomin"));
  ed_.action("view.zoomIn")->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
  act("view.zoomOut", "Alejar", QKeySequence("Ctrl+-"), [this] { view_->zoomBy(1.0 / (1.0 + ed_.xpp().zoomStep / 100.0)); })->setIcon(Icons::make("zoomout"));
  act("view.zoom100", "Tamaño normal (100 %)", QKeySequence("Ctrl+0"), [this] { view_->zoom100(); });
  act("view.fitWidth", "Ajustar al ancho", {}, [this] { view_->setAutoFitWidth(true); })->setIcon(Icons::make("fit"));
  act("view.fitPage", "Ajustar a la página", {}, [this] { view_->zoomFitPage(); });
  QAction* cont = act("view.continuous", "Vista continua", {}, [] {});
  cont->setCheckable(true);
  cont->setChecked(true);
  connect(cont, &QAction::toggled, this, [this](bool on) { view_->setContinuous(on); updateStatus(); });
  QAction* two = act("view.twoPages", "Dos páginas", {}, [] {});
  two->setCheckable(true);
  two->setToolTip(QStringLiteral("Vista de dos páginas: de dos en dos, como un libro abierto"));
  connect(two, &QAction::toggled, this, [this](bool on) { view_->setPairedPages(on); updateStatus(); });
  QAction* cs = act("view.colorSidebar", "Barra de colores", {}, [] {});
  cs->setCheckable(true);
  cs->setToolTip(QStringLiteral("Columna de colores y botones de vista al borde de la ventana"));
  connect(cs, &QAction::toggled, this, [this](bool on) { setColorSidebar(on); });
  QAction* csr = act("view.colorSidebarRight", "Barra de colores a la derecha (zurdos)", {}, [] {});
  csr->setCheckable(true);
  connect(csr, &QAction::toggled, this, [this](bool on) { setColorSidebarRight(on); });
  QAction* tb = act("view.toolbars", "Barra de herramientas", {}, [] {});
  tb->setCheckable(true);
  tb->setChecked(true);
  connect(tb, &QAction::toggled, this, [this](bool on) { toolbar_->setVisible(on); });
  QAction* fs = act("view.fullscreen", "Pantalla completa", QKeySequence("F11"), [] {});
  fs->setCheckable(true);
  connect(fs, &QAction::toggled, this, [this](bool on) { if (on) showFullScreen(); else showNormal(); });
  QAction* sb = act("view.sidebar", "Barra lateral", {}, [] {});
  sb->setCheckable(true);
  connect(sb, &QAction::toggled, this, [this](bool on) { dock_->setVisible(on && dockTabs_->count() > 0); });
  stub("view.presentation", "Modo de presentación", QKeySequence("F5"));
  stub("view.sidebarPages", "Vista previa de páginas");
  stub("view.sidebarLayers", "Capas");

  act("nav.first", "Primera página", QKeySequence("Ctrl+Home"), [this] { view_->scrollToPage(0); });
  act("nav.prev", "Página anterior", QKeySequence(Qt::Key_PageUp), [this, cur] { view_->scrollToPage(std::max(0, cur() - 1)); });
  act("nav.next", "Página siguiente", QKeySequence(Qt::Key_PageDown), [this, cur] { view_->scrollToPage(std::min(ed_.pageCount() - 1, cur() + 1)); });
  act("nav.last", "Última página", QKeySequence("Ctrl+End"), [this] { view_->scrollToPage(ed_.pageCount() - 1); });
  act("nav.goto", "Ir a la página…", QKeySequence("Ctrl+G"), [this, cur] {
    bool ok = false;
    const int n = QInputDialog::getInt(this, "Ir a la página", "Página:", cur() + 1, 1, ed_.pageCount(), 1, &ok);
    if (ok) view_->scrollToPage(n - 1);
  });
  stub("nav.back", "Atrás", QKeySequence("Alt+Left"));
  stub("nav.forward", "Adelante", QKeySequence("Alt+Right"));

  act("journal.newPage", "Nueva página", {}, [this, cur] { ed_.insertPage(cur() + 1, cur()); })->setIcon(Icons::make("page_add"));
  act("journal.insertBefore", "Insertar página antes", {}, [this, cur] { ed_.insertPage(cur(), cur()); });
  act("journal.append", "Insertar página al final", {}, [this] { ed_.insertPage(ed_.pageCount(), ed_.pageCount() - 1); });
  act("journal.delete", "Eliminar página actual", {}, [this, cur] { ed_.deletePage(cur()); });
  act("journal.moveUp", "Mover página hacia arriba", {}, [this, cur] { ed_.movePage(cur(), -1); });
  act("journal.moveDown", "Mover página hacia abajo", {}, [this, cur] { ed_.movePage(cur(), +1); });
  act("journal.bgColor", "Color de fondo…", {}, [this, cur] {
    const QColor c = QColorDialog::getColor(Qt::white, this, "Color de fondo");
    if (c.isValid()) ed_.setPaperColor(cur(), c.name(), applyAll_);
  });
  QAction* all = act("journal.applyAll", "Aplicar a todas las páginas", {}, [] {});
  all->setCheckable(true);
  connect(all, &QAction::toggled, this, [this](bool on) { applyAll_ = on; });
  act("journal.rotate", "Girar hoja", {}, [this, cur] {
    const Page& p = ed_.doc().pages[size_t(cur())];
    ed_.setPageSize(cur(), p.height(), p.width(), applyAll_);
  });
  for (const PaperStyle& ps : paperStyles()) {
    const QString style = ps.style;
    act("journal.paper:" + style, ps.label, {}, [this, cur, style] { ed_.setPaperStyle(cur(), style, applyAll_); });
  }
  // Xournal++'s isometric papers (its `style` values "isodotted" / "isograph"); kept out of paperStyles(), which the welcome page and the paper bar count
  for (const auto& iso : {std::pair<const char*, const char*>{"isodotted", "Puntos isométricos"}, {"isograph", "Cuadrícula isométrica"}}) {
    const QString style = QString::fromLatin1(iso.first);
    act("journal.paper:" + style, QString::fromUtf8(iso.second), {}, [this, cur, style] { ed_.setPaperStyle(cur(), style, applyAll_); });
  }
  // Xournal++'s "next / previous annotated page": the nearest page that has any element in any layer (looked at in place, nothing is copied)
  auto annotated = [this](int i) {
    for (const Layer& l : ed_.doc().pages[size_t(i)].layers) if (!l.elements.empty()) return true;
    return false;
  };
  act("nav.nextAnnotated", "Siguiente página con anotaciones", QKeySequence("Ctrl+Alt+PgDown"), [this, cur, annotated] {
    for (int i = cur() + 1; i < ed_.pageCount(); i++) if (annotated(i)) { view_->scrollToPage(i); return; }
    showMessage("No hay más páginas con anotaciones");
  });
  act("nav.prevAnnotated", "Página anterior con anotaciones", QKeySequence("Ctrl+Alt+PgUp"), [this, cur, annotated] {
    for (int i = cur() - 1; i >= 0; i--) if (annotated(i)) { view_->scrollToPage(i); return; }
    showMessage("No hay páginas anteriores con anotaciones");
  });
  for (const PaperKind& k : paperKinds()) {
    const QString id = k.id;
    act("journal.paper:x:" + id, k.label, {}, [this, cur, id] {
      const PaperKind* kind = paperKind(id);
      if (!kind || ed_.pageCount() == 0) return;
      // Same rule as the paper bar and Electron's choosePaper: choosing the kind the page already has keeps its options.
      const auto cur0 = paperOfBackground(ed_.doc().pages[size_t(cur())].background);
      const auto spec = cur0 && cur0->kind == id ? cur0 : defaultSpec(id);
      if (!spec) return;
      std::vector<int> pages;
      for (int i = 0; i < ed_.pageCount(); i++) if (applyAll_ || i == cur()) pages.push_back(i);
      ed_.editBackgrounds(pages, "Tipo de papel", [&](Attrs& bg) {
        if (attrOr(bg, "type", "solid") != "solid") return;
        attrSet(bg, "style", kind->fallback);
        attrSet(bg, "xlPaper", formatSpec(*spec));
      });
    });
  }
  for (const PaperFormat& f : paperFormats()) {
    const double w = f.width, h = f.height;
    act("journal.format:" + f.id, f.label, {}, [this, cur, w, h] { ed_.setPageSize(cur(), w, h, applyAll_); });
  }

  stub("tool.toolbox", "Mostrar el cuadro de herramientas", QKeySequence("Q"));
  stub("pen.settings", "Lápiz y tableta…");
  stub("help.welcome", "Bienvenida");
  stub("help.pen", "Diagnóstico del lápiz…");
  act("help.about", "Acerca de", {}, [this] {
    QMessageBox::about(this, "Acerca de Xournal Lab",
      "<b>Xournal Lab (C++ / Qt " + QString::fromLatin1(qVersion()) + ")</b><br>Visor y editor experimental de documentos .xopp (formato de Xournal++ 1.3.8).<br><br>"
      "Proyecto independiente. No es Xournal++ ni está afiliado a su proyecto. Xournal++ es software libre (GPL-2.0).");
  });
  ed_.addCommand("help.manual", "Manual de Xournal++ (abre el navegador)", {}, [] { QDesktopServices::openUrl(QUrl("https://xournalpp.github.io/guide/overview/")); });
}

// ---- menus -----------------------------------------------------------------------------------------------------------------------------------
QMenu* MainWindow::addMenu(const QString& key, const QString& title, const QString& before) {
  QMenu* m = new QMenu(title, this);
  if (before.isEmpty() || !menus_.contains(before)) menuBar()->addMenu(m);
  else menuBar()->insertMenu(menus_[before]->menuAction(), m);
  menus_[key] = m;
  return m;
}

void MainWindow::buildMenus() {
  auto add = [this](QMenu* m, std::initializer_list<const char*> ids) {
    for (const char* id : ids) { if (id[0] == '-') m->addSeparator(); else m->addAction(ed_.action(QString::fromLatin1(id))); }
  };
  QMenu* file = addMenu("file", "&Archivo");
  add(file, {"file.new", "file.open"});
  recentMenu_ = file->addMenu("Abrir reciente");
  connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);   // a file moved or deleted since the list was built is not offered
  add(file, {"file.openAutosave", "-", "file.save", "file.saveAs", "file.saveCopy", "-", "file.exportPdf", "-", "file.close", "file.quit"});

  QMenu* edit = addMenu("edit", "&Editar");
  add(edit, {"edit.undo", "edit.redo", "-", "edit.cut", "edit.copy", "edit.paste", "edit.delete", "-", "edit.find", "edit.prefs"});

  QMenu* v = addMenu("view", "&Ver");
  add(v, {"view.toolbars", "view.colorSidebar", "view.colorSidebarRight"});
  QMenu* side = v->addMenu("Barras laterales");
  add(side, {"view.sidebarPages", "view.sidebarLayers"});
  add(v, {"view.fullscreen", "view.presentation"});
  QMenu* zoom = v->addMenu("Zoom");
  add(zoom, {"view.zoomIn", "view.zoomOut", "view.zoom100", "view.fitWidth", "view.fitPage"});
  add(v, {"view.continuous", "view.twoPages"});

  QMenu* nav = addMenu("nav", "&Navegación");
  add(nav, {"nav.first", "nav.prev", "nav.next", "nav.last", "nav.goto", "-", "nav.back", "nav.forward", "-", "nav.nextAnnotated", "nav.prevAnnotated"});

  QMenu* j = addMenu("journal", "&Diario");
  add(j, {"journal.newPage", "journal.insertBefore", "journal.append", "journal.delete", "journal.moveUp", "journal.moveDown", "-"});
  QMenu* bg = j->addMenu("Fondo de página");
  bg->addAction(ed_.action("journal.bgColor"));
  QMenu* paper = bg->addMenu("Tipo de papel");
  for (const PaperStyle& ps : paperStyles()) paper->addAction(ed_.action("journal.paper:" + ps.style));
  paper->addAction(ed_.action("journal.paper:isodotted"));
  paper->addAction(ed_.action("journal.paper:isograph"));
  paper->addSeparator();
  for (const PaperKind& k : paperKinds()) paper->addAction(ed_.action("journal.paper:x:" + k.id));
  bg->addAction(ed_.action("journal.applyAll"));
  QMenu* fmt = j->addMenu("Formato de papel");
  for (const PaperFormat& f : paperFormats()) fmt->addAction(ed_.action("journal.format:" + f.id));
  fmt->addSeparator();
  fmt->addAction(ed_.action("journal.rotate"));

  toolsMenu_ = addMenu("tools", "&Herramientas");

  QMenu* help = addMenu("help", "A&yuda");
  add(help, {"help.welcome", "help.pen", "help.manual", "help.about"});
}

void MainWindow::rebuildToolsMenu() {
  for (QAction* a : toolsMenuDynamic_) toolsMenu_->removeAction(a);
  toolsMenuDynamic_.clear();
  toolsMenu_->clear();
  QAction* firstSep = nullptr;
  int last = -1;
  for (const ToolInfo* t : ed_.toolInfos()) {
    if (t->label.isEmpty()) continue;
    if (last >= 0 && t->order / 10 != last / 10) toolsMenu_->addSeparator();
    last = t->order;
    toolsMenu_->addAction(ed_.action("tool." + t->id));
    if (t->id == "latex") toolsMenu_->addAction(ed_.action("tool.image"));   // main.ts lists the picture command right after the formula tool
  }
  Q_UNUSED(firstSep);
  toolsMenu_->addSeparator();
  toolsMenu_->addAction(ed_.action("tool.toolbox"));
  toolsMenu_->addAction(ed_.action("tool.toolboxEnable"));
  toolsMenu_->addAction(ed_.action("pen.settings"));
  toolsMenu_->addAction(ed_.action("tool.recognizer"));
}

void MainWindow::scheduleToolbarRebuild() { rebuildTimer_.start(0); }

void MainWindow::rebuildToolbar() {
  const auto old = toolbar_->actions() + styleBar_->actions();
  for (QAction* a : old) {
    toolbar_->removeAction(a);
    styleBar_->removeAction(a);
    if (a == styleLead_ || a == sizeMarker_ || toolbarExtras.contains(a)) continue;   // kept across rebuilds (their widgets too)
    if (qobject_cast<QWidgetAction*>(a) || a->isSeparator()) a->deleteLater();
  }
  colorActions_.clear();
  sizeActions_.clear();
  // the first row (file, history, clipboard, panels, pages, zoom) belongs to Chrome::buildNavBar; this window builds the tools row and the style row, the
  // second line of the Electron toolbar (#bar2): tools | glow, palette, colours, sizes | selection actions
  QHash<QAction*, QString> idOf;
  for (const auto& kv : ed_.actions()) idOf.insert(kv.second, kv.first);
  // commands that sit among the tools (Electron lists them in the middle of the tool row): after which tool each goes
  static const QMap<QString, QString> after = {{"tool.recognizer", "shape"}, {"tool.markdown", "text"}, {"tool.image", "latex"}};
  QList<QAction*> extras;
  for (QAction* x : toolbarExtras) if (x) extras << x;
  auto addExtrasAfter = [&](const QString& toolId) {
    for (QAction* x : QList<QAction*>(extras)) if (after.value(idOf.value(x)) == toolId) { toolbar_->addAction(x); extras.removeOne(x); }
  };

  int lastGroup = -1;
  for (const ToolInfo* t : ed_.toolInfos()) {
    if (!t->inToolbar || t->label.isEmpty()) continue;
    const int group = t->order < 10 ? 0 : t->order < 20 ? 1 : 2;   // pens | selection tools | everything else (Electron's separators)
    if (lastGroup >= 0 && group != lastGroup) toolbar_->addSeparator();
    lastGroup = group;
    QAction* a = ed_.action("tool." + t->id);
    if (toolMenus_.contains(t->id)) {
      // a tool with a dropdown (pen: sizes, line styles, fill; eraser: sizes, type...): the caret opens a menu rebuilt each time it opens
      auto* btn = new SplitButton(toolbar_);
      btn->setDefaultAction(a);
      auto* menu = new QMenu(btn);
      const auto fill = toolMenus_.value(t->id);
      connect(menu, &QMenu::aboutToShow, this, [menu, fill] { menu->clear(); fill(menu); });
      // not btn->setMenu(): a button with a menu opens it after a long press, and a press on the icon (drag in the toolbox editor, a held pen) must only select the tool
      menu->setObjectName("dropdown");
      btn->onCaret = [btn, menu] { menu->popup(btn->mapToGlobal(QPoint(0, btn->height()))); };
      btn->setProperty("xlCaret", true);
      btn->setProperty("xlDropdown", true);
      toolbar_->addWidget(btn);
    } else if (!t->options && !t->optionsCommand.isEmpty()) {
      auto* btn = new SplitButton(toolbar_);
      btn->setDefaultAction(a);
      const QString cmd = t->optionsCommand;
      btn->onCaret = [this, cmd] { if (QAction* c = ed_.action(cmd); c && c->isEnabled()) c->trigger(); };
      btn->setProperty("xlCaret", true);
      toolbar_->addWidget(btn);
    } else if (!t->options) {
      toolbar_->addAction(a);
    } else {
      // a tool with options: the button and a caret that opens its panel
      auto* btn = new SplitButton(toolbar_);
      btn->setDefaultAction(a);
      auto* menu = new QMenu(btn);
      auto* wa = new QWidgetAction(menu);
      const OptionsPanelFactory make = t->options;
      connect(menu, &QMenu::aboutToShow, this, [this, btn, menu, wa, make] {
        // built on first open, kept after. A menu asks a widget action for its widget when the action is ADDED, so an action that gets its widget later
        // has to be added again (otherwise the popup is an empty 58 x 33 box)
        // every panel goes inside a ScrollingPanel: one taller than the room under the button (a 768 px screen) scrolls instead of running off the screen
        if (!wa->defaultWidget()) { wa->setDefaultWidget(new ScrollingPanel(make(menu, ed_), menu)); menu->removeAction(wa); menu->addAction(wa); }
        if (auto* sp = qobject_cast<ScrollingPanel*>(wa->defaultWidget())) { /* a menu keeps the size it computed: re-adding makes it ask again */ if (sp->setMaxSize(roomBelow(btn))) { menu->removeAction(wa); menu->addAction(wa); } }
      });
      menu->addAction(wa);
      btn->setMenu(menu);
      btn->setPopupMode(QToolButton::DelayedPopup);
      btn->onCaret = [btn] { btn->showMenu(); };
      btn->setProperty("xlCaret", true);
      toolbar_->addWidget(btn);
    }
    addExtrasAfter(t->id);
  }
  for (QAction* x : extras) toolbar_->addAction(x);   // a command whose tool is not registered (yet) still has its button

  // the style row
  const QStringList colors = paletteColors_.isEmpty() ? paletteById(QString::fromLatin1(kDefaultPalette)).colors : paletteColors_;
  colorGroup_ = new QActionGroup(styleBar_);
  styleBar_->addSeparator();
  styleBar_->addAction(styleLead_);   // the glow button is inserted before this marker (selection/install.cpp)
  styleBar_->addAction(ed_.action("palette.menu"));   // implemented by the settings area (disabled until then)
  for (const QString& c : colors) {
    QAction* a = new QAction(chrome::swatchIcon(QColor(c)), c, colorGroup_);
    a->setCheckable(true);
    a->setData(c.left(7));
    a->setToolTip("Color " + c);
    a->setChecked(c.left(7).compare(ed_.style().color, Qt::CaseInsensitive) == 0);
    connect(a, &QAction::triggered, this, [this, c] { Style s = ed_.style(); s.color = c.left(7); ed_.setStyle(s); });
    if (!colorBarOn_) {   // with the colour sidebar on the dots live there (same actions), not in two places
      styleBar_->addAction(a);
      if (auto* w = qobject_cast<QToolButton*>(styleBar_->widgetForAction(a))) { w->setProperty("xlSwatch", true); w->installEventFilter(new SwatchPainter(w)); }   // a 30 px circle, no box (theme.cpp)
    }
    colorActions_ << a;
  }
  if (!colorBarOn_) {   // <input type=color id=customColor>: any colour; it becomes the drawing colour
    auto* wa = new QWidgetAction(styleBar_);
    wa->setDefaultWidget(makeCustomColorButton(styleBar_));
    styleBar_->addAction(wa);
  }
  sizeSection_ = sizeMarker_;
  styleBar_->addAction(sizeMarker_);   // the size buttons go in before this marker
  {   // <select id=width>: the sizes of the tool, with the current one
    auto* wa = new QWidgetAction(styleBar_);
    auto* cb = new QComboBox(styleBar_);
    cb->setObjectName("sizeCombo");
    cb->setToolTip(QStringLiteral("Grosor, tamaño de letra o tamaño de la goma"));
    cb->setMinimumWidth(62);
    cb->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(cb, &QComboBox::activated, this, [this, cb](int i) {
      const SizeKind kind = barSizeKind(ed_.tool());
      if (i < 0) return;
      const double v = cb->itemData(i).toDouble();
      Style s = ed_.style();
      if (kind == SizeKind::Pen) s.penWidth = v; else if (kind == SizeKind::Highlighter) s.highlighterWidth = v; else if (kind == SizeKind::Text) s.textSize = v; else s.eraserRadius = v;
      ed_.setStyle(s);
    });
    wa->setDefaultWidget(cb);
    styleBar_->addAction(wa);
    sizeCombo_ = cb;
  }
  styleBar_->addSeparator();   // the selection actions (selection/install.cpp) follow
  rebuildSizeButtons();
  rebuildColorBar();
  emit toolbarRebuilt();   // the modules put their buttons (glow, selection actions) in the style row now
  tierWidth_[0][0] = 0;    // the buttons changed: measure the tiers again, with all of them
  arrangeToolbars();
}

QToolButton* MainWindow::makeCustomColorButton(QWidget* parent) {
  auto* b = new QToolButton(parent);
  b->setObjectName("customColor");
  b->setToolTip(QStringLiteral("Color personalizado"));
  b->setProperty("xlColour", ed_.style().color);   // drawn by SwatchPainter as a 26 x 24 box in this colour
  b->installEventFilter(new SwatchPainter(b));
  b->setAutoRaise(true);
  connect(b, &QToolButton::clicked, this, [this, b] {
    const QColor c = QColorDialog::getColor(QColor(ed_.style().color), this, QStringLiteral("Color personalizado"));
    if (!c.isValid()) return;
    Style s = ed_.style();
    s.color = c.name();
    ed_.setStyle(s);
    b->setProperty("xlColour", c.name());
    b->update();
  });
  connect(&ed_, &Editor::styleChanged, b, [this, b] { b->setProperty("xlColour", ed_.style().color); b->update(); });
  return b;
}

// ---- the colour sidebar -------------------------------------------------------------------------------------------------------------------
void MainWindow::rebuildColorBar() {
  if (!colorBar_) return;
  const auto old = colorBar_->actions();
  colorBar_->clear();
  for (QAction* a : old) if (qobject_cast<QWidgetAction*>(a) || a->isSeparator()) a->deleteLater();
  if (!colorBarOn_) return;
  for (QAction* a : colorActions_) {
    colorBar_->addAction(a);
    if (auto* w = qobject_cast<QToolButton*>(colorBar_->widgetForAction(a))) { w->setProperty("xlSwatch", true); w->installEventFilter(new SwatchPainter(w)); }
  }
  auto* wa = new QWidgetAction(colorBar_);
  wa->setDefaultWidget(makeCustomColorButton(colorBar_));
  colorBar_->addAction(wa);
  colorBar_->addSeparator();
  // the view buttons of Xournal++'s sidebar: two pages, presentation, 100 %, fit, zoom out, zoom in
  for (const char* id : {"view.twoPages", "view.presentation", "view.zoom100", "view.fitWidth", "view.zoomOut", "view.zoomIn"})
    if (QAction* a = ed_.action(QString::fromLatin1(id))) colorBar_->addAction(a);
}

void MainWindow::placeColorBar() {
  if (!colorBar_) return;
  removeToolBar(colorBar_);
  addToolBar(colorBarRight_ ? Qt::RightToolBarArea : Qt::LeftToolBarArea, colorBar_);
  colorBar_->setVisible(colorBarOn_);
}

void MainWindow::setColorSidebar(bool on) {
  if (on == colorBarOn_) return;
  colorBarOn_ = on;
  if (QAction* a = ed_.action("view.colorSidebar")) { QSignalBlocker b(a); a->setChecked(on); }
  if (persist_) settings().setValue("ui/colorSidebar", on);
  rebuildToolbar();   // the dots leave or join the style row
  placeColorBar();
  emit colorSidebarChanged();
}

void MainWindow::setColorSidebarRight(bool right) {
  if (right == colorBarRight_) return;
  colorBarRight_ = right;
  if (QAction* a = ed_.action("view.colorSidebarRight")) { QSignalBlocker b(a); a->setChecked(right); }
  if (persist_) settings().setValue("ui/colorSidebarRight", right);
  placeColorBar();
  emit colorSidebarChanged();
}

void MainWindow::loadUiState() {
  if (!persist_) return;
  const QSettings s = settings();
  setColorSidebarRight(s.value("ui/colorSidebarRight", false).toBool());
  setColorSidebar(s.value("ui/colorSidebar", true).toBool());
}

void MainWindow::setToolMenu(const QString& toolId, std::function<void(QMenu*)> fill) {
  toolMenus_.insert(toolId, std::move(fill));
  scheduleToolbarRebuild();
}

void MainWindow::applyToolbarTier(int tier) {
  // the same size for every toolbar of the window (the navigation row too): icon size and, through the style sheet, button padding
  static const int icon[3] = {20, 18, 16};
  for (QToolBar* tb : findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
    if (tb->property("tier").toInt() == tier && tb->iconSize().width() == icon[tier]) continue;
    tb->setProperty("tier", tier);
    tb->setIconSize(QSize(icon[tier], icon[tier]));
    for (QWidget* w : tb->findChildren<QWidget*>()) { w->style()->unpolish(w); w->style()->polish(w); }
    tb->style()->unpolish(tb);
    tb->style()->polish(tb);
    if (tb->layout()) tb->layout()->invalidate();
  }
}

void MainWindow::measureToolbarTiers() {
  measuringTiers_ = true;
  const int keep = toolbarTier_;
  for (int t = 0; t < 3; t++) {
    applyToolbarTier(t);
    for (QToolBar* tb : {toolbar_, styleBar_, topBar_}) {   // a hint cached before the buttons were created and polished is stale
      tb->ensurePolished();
      for (QWidget* w : tb->findChildren<QWidget*>()) w->ensurePolished();
      if (tb->layout()) { tb->layout()->invalidate(); tb->layout()->activate(); }
    }
    tierWidth_[t][0] = toolbar_->sizeHint().width();
    tierWidth_[t][1] = styleBar_->sizeHint().width();
    tierWidth_[t][2] = topBar_->sizeHint().width();
  }
  applyToolbarTier(keep);
  measuringTiers_ = false;
}

void MainWindow::arrangeToolbars() {
  if (!toolbar_ || !styleBar_ || !topBar_ || measuringTiers_) return;   // events during construction / while measuring
  // A row that does not fit loses its buttons behind a chevron, so the buttons shrink first: full size, compact (18 px icons, tighter padding), very compact
  // (16 px). The tools and the style rows share one line while both fit (the Electron toolbar: two lines in all); otherwise each has its own line. Under the
  // smallest size the chevron takes over.
  const int signature = int(toolbar_->actions().size() * 1000000 + styleBar_->actions().size() * 1000 + topBar_->actions().size());
  if (tierWidth_[0][0] <= 0 || signature != tierSignature_) { tierSignature_ = signature; measureToolbarTiers(); }
  const int room = width() - 24;
  int tier = 2;
  for (int t = 0; t < 3; t++) if (std::max({tierWidth_[t][0], tierWidth_[t][1], tierWidth_[t][2]}) <= room) { tier = t; break; }
  const bool oneLine = tierWidth_[tier][0] + tierWidth_[tier][1] <= room;
  if (tier != toolbarTier_) { toolbarTier_ = tier; applyToolbarTier(tier); }
  const bool own = !oneLine;
  if (own == styleOnOwnRow_ && own == bool(toolBarBreak(styleBar_))) return;
  styleOnOwnRow_ = own;
  if (own) insertToolBarBreak(styleBar_); else removeToolBarBreak(styleBar_);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
  QMainWindow::resizeEvent(e);
  arrangeToolbars();
}

bool MainWindow::event(QEvent* e) {
  const bool r = QMainWindow::event(e);
  if (e->type() == QEvent::Polish || e->type() == QEvent::StyleChange) { if (!measuringTiers_) tierWidth_[0][0] = 0; arrangeToolbars(); }   // theme changes change button sizes
  return r;
}

void MainWindow::setPaletteColors(const QStringList& colors) {
  if (colors == paletteColors_) return;
  paletteColors_ = colors;
  if (sizeSection_) rebuildToolbar();   // after the first build; before it the constructor uses them
}

void MainWindow::setSizePresets(const SizePresets& s) {
  sizePresets_ = s;
  rebuildSizeButtons();
}

void MainWindow::rebuildSizeButtons() {
  if (!sizeSection_) return;
  for (QAction* a : sizeActions_) { styleBar_->removeAction(a); a->deleteLater(); }
  sizeActions_.clear();
  sizeGroup_ = nullptr;
  auto* combo = qobject_cast<QComboBox*>(sizeCombo_.data());
  const SizeKind kind = barSizeKind(ed_.tool());
  sizeGroup_ = new QActionGroup(styleBar_);
  const Style st = ed_.style();
  const double cur = kind == SizeKind::Pen ? st.penWidth : kind == SizeKind::Highlighter ? st.highlighterWidth : kind == SizeKind::Text ? st.textSize : st.eraserRadius;
  int index = 0;
  std::vector<double> list;
  for (double v : sizePresets_.of(kind)) {
    list.push_back(v);
    QAction* a = new QAction(chrome::sizeDotIcon(dotSize(kind, v)), QString::number(v), sizeGroup_);
    a->setProperty("xlSizeKind", int(kind));    // the settings area edits a preset by long press / right click on its button
    a->setProperty("xlSizeIndex", index++);
    a->setCheckable(true);
    a->setChecked(std::abs(v - cur) < 1e-6);
    a->setToolTip((kind == SizeKind::Eraser ? QStringLiteral("Radio ") : kind == SizeKind::Text ? QStringLiteral("Tamaño ") : QStringLiteral("Grosor ")) + QString::number(v) +
                  QStringLiteral(" · clic: usar este tamaño · clic derecho, Alt+clic o pulsación larga: guardar aquí el tamaño actual (") + QString::number(+cur, 'g', 4) + ")");
    connect(a, &QAction::triggered, this, [this, kind, v] {
      Style s = ed_.style();
      if (kind == SizeKind::Pen) s.penWidth = v; else if (kind == SizeKind::Highlighter) s.highlighterWidth = v; else if (kind == SizeKind::Text) s.textSize = v; else s.eraserRadius = v;
      ed_.setStyle(s);
    });
    styleBar_->insertAction(sizeSection_, a);
    if (QWidget* w = styleBar_->widgetForAction(a)) w->setProperty("xlSize", true);   // #sizePresets button: 24 x 28 (theme.cpp)
    sizeActions_ << a;
  }
  if (combo) {   // the presets, plus the current size when it is none of them (a size chosen in the pen dialog shows too)
    bool known = false;
    for (double v : list) if (std::abs(v - cur) < 1e-6) known = true;
    if (!known) list.push_back(cur);
    std::sort(list.begin(), list.end());
    combo->blockSignals(true);
    combo->clear();
    for (double v : list) combo->addItem(QString::number(std::round(v * 100) / 100), v);
    for (int i = 0; i < combo->count(); i++) if (std::abs(combo->itemData(i).toDouble() - cur) < 1e-6) combo->setCurrentIndex(i);
    combo->setEnabled(true);
    combo->blockSignals(false);
  }
}

void MainWindow::addSidePanel(const QString& id, const QString& title, QWidget* w) {
  w->setObjectName(id);
  dockTabs_->addTab(w, title);
  dock_->show();
  ed_.action("view.sidebar")->setChecked(true);
}

void MainWindow::showMessage(const QString& msg, int ms) {
  infoLabel_->setMessage(msg);
  messageTimer_.start(ms);
}

QString MainWindow::infoText() const { return infoLabel_->message(); }

QString MainWindow::titleBarText() const {
  auto* welcome = findChild<QWidget*>("welcome");
  const bool noDocument = fileName_.isEmpty() && !ed_.dirty() && welcome && !welcome->isHidden();   // isHidden: true only when hidden on purpose (the window may not be shown yet)   // Electron has no document yet: the bar says just the name
  if (noDocument) return kApp;
  QString name = fileName_.isEmpty() ? QStringLiteral("Sin título") : fileName_;
  if (ed_.xpp().titlePath && !ed_.path().isEmpty()) name = QDir::toNativeSeparators(ed_.path());   // Preferencias > Ver: the whole path of the file
  if (ed_.xpp().titlePageNumber) name += QStringLiteral(" · pág. %1/%2").arg(ed_.currentPage() + 1).arg(ed_.pageCount());
  return QString(ed_.dirty() ? "● " : "") + name + " — " + kApp;
}

void MainWindow::updateTitle() {
  setWindowTitle((fileName_.isEmpty() ? QStringLiteral("Sin título") : fileName_) + (ed_.dirty() ? "*" : "") + " - " + kApp);
  titleBar_->setTitle(titleBarText());
}

void MainWindow::updateInfoSummary() {
  // Electron's #info: "2 págs · 1.246 elementos · pág. 1"
  const Document& d = ed_.doc();
  qint64 elements = 0;
  for (const Page& p : d.pages) for (const Layer& l : p.layers) elements += qint64(l.elements.size());
  auto grouped = [](qint64 n) {   // Intl "es": 1.246 (a dot every three digits, also for four digits)
    QString digits = QString::number(n), out;
    for (int i = 0; i < digits.size(); i++) { if (i > 0 && (digits.size() - i) % 3 == 0) out += '.'; out += digits[i]; }
    return out;
  };
  infoLabel_->setSummary(QString("%1 págs · %2 elementos · pág. %3").arg(grouped(qint64(d.pages.size())), grouped(elements), grouped(ed_.currentPage() + 1)));
}

void MainWindow::updateStatus() {
  zoomLabel_->setText(QString("%1%").arg(int(std::lround(view_->zoom() * 100))));
}

// ---- files -----------------------------------------------------------------------------------------------------------------------------------
QSettings MainWindow::settings() const {
  return settingsFile_.isEmpty() ? QSettings("XournalLab", "XournalLabCpp") : QSettings(settingsFile_, QSettings::IniFormat);
}

QStringList MainWindow::recentFiles() const {
  if (!persist_) return {};
  QSettings s = settings();
  QStringList l = s.value("recent").toStringList();
  l.erase(std::remove_if(l.begin(), l.end(), [](const QString& p) { return !QFileInfo::exists(p); }), l.end());   // moved or deleted: not offered
  return l;
}

void MainWindow::addRecent(const QString& path) {
  if (!persist_) return;
  QSettings s = settings();
  QStringList l = s.value("recent").toStringList();
  l.removeAll(path);
  l.prepend(path);
  while (l.size() > 10) l.removeLast();
  s.setValue("recent", l);
  rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
  recentMenu_->clear();
  const QStringList l = recentFiles();
  if (l.isEmpty()) { recentMenu_->addAction("(vacío)")->setEnabled(false); return; }
  for (const QString& p : l) {
    QAction* a = recentMenu_->addAction(QFileInfo(p).fileName());
    a->setToolTip(p);
    connect(a, &QAction::triggered, this, [this, p] { if (maybeSave()) openFile(p); });
  }
  recentMenu_->addSeparator();
  connect(recentMenu_->addAction("Borrar la lista"), &QAction::triggered, this, [this] {
    settings().remove("recent");
    rebuildRecentMenu();
  });
}

bool MainWindow::openFile(const QString& path) {
  LoadResult r = loadXopp(path);
  if (!r.ok) {
    if (persist_) QMessageBox::warning(this, kApp, "No se pudo abrir el archivo:\n" + r.error);
    else fprintf(stderr, "open failed: %s\n", qPrintable(r.error));
    return false;
  }
  const int repaired = repairGenerated(r.doc);   // labels of planes, 3D spaces and figures saved without a position (the old bug)
  ed_.setDocument(std::move(r.doc), saveOverOpened_ ? path : QString());   // unless the preference says otherwise, Save on an opened file asks where to write, so the original is never overwritten silently
  fileName_ = QFileInfo(path).fileName();
  lastDir_ = QFileInfo(path).absolutePath();
  addRecent(path);
  updateTitle();
  if (repaired > 0) showMessage(QString("Se rehicieron %1 objeto(s) (planos, espacios o figuras) cuyas etiquetas se habían guardado sin posición; guarda para conservar el arreglo").arg(repaired), 12000);
  else if (!r.skipped.isEmpty()) showMessage(QString("Se omitieron %1 elementos que esta versión no lee").arg(r.skipped.size()), 8000);
  return true;
}

QString MainWindow::suggestedSaveName(bool copy) const {
  // like the Electron app: never the opened original's own name (one click from overwriting it): <name>-lab.xopp, <name>-copia.xopp for a copy; a file this session saved keeps its name
  const QString base = QFileInfo(fileName_.isEmpty() ? QStringLiteral("documento") : fileName_).completeBaseName();
  if (copy) return base + "-copia.xopp";
  if (fileName_.isEmpty() && ed_.path().isEmpty()) return expandNameTemplate(ed_.xpp().saveName, QDateTime::currentDateTime()) + ".xopp";   // Preferencias > Cargar/guardar: the proposed name of a new document
  const bool opened = ed_.path().isEmpty() && !fileName_.isEmpty() && QFileInfo(fileName_).suffix().compare("pdf", Qt::CaseInsensitive) != 0;   // a PDF being annotated has no original .xopp to protect
  return opened ? base + "-lab.xopp" : base + ".xopp";
}

bool MainWindow::save(bool saveAs) {
  QString path = ed_.path();
  if (saveAs || path.isEmpty()) {
    const QString suggested = suggestedSaveName(false);
    path = QFileDialog::getSaveFileName(this, "Guardar como", lastDir_.isEmpty() ? suggested : lastDir_ + "/" + suggested, "Xournal++ (*.xopp)");
    if (path.isEmpty()) return false;
    if (!path.endsWith(".xopp", Qt::CaseInsensitive)) path += ".xopp";
  }
  return saveTo(path);
}

bool MainWindow::saveTo(const QString& path) {
  ed_.flushPending();   // a text still being typed is part of what is saved
  QString err;
  if (!saveXopp(path, ed_.doc(), &err)) {
    if (persist_) QMessageBox::critical(this, kApp, "No se pudo guardar:\n" + err);
    else fprintf(stderr, "save failed: %s\n", qPrintable(err));
    return false;
  }
  ed_.setPath(path);
  ed_.markSaved();
  fileName_ = QFileInfo(path).fileName();
  lastDir_ = QFileInfo(path).absolutePath();
  addRecent(path);
  updateTitle();
  showMessage("Guardado: " + path);
  return true;
}

bool MainWindow::maybeSave() {
  if (!ed_.dirty()) return true;
  const auto b = QMessageBox::warning(this, kApp, "Hay cambios sin guardar.", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (b == QMessageBox::Cancel) return false;
  if (b == QMessageBox::Save) return save(false);
  return true;
}

void MainWindow::newDocument() {
  if (!maybeSave()) return;
  blankNow();
}

void MainWindow::blankNow() {
  ed_.setDocument(blankDocument(), QString());
  fileName_.clear();
  updateTitle();
}

void MainWindow::showEvent(QShowEvent* e) {
  QMainWindow::showEvent(e);
#ifdef Q_OS_WIN
  if (!frameFixed_ && QGuiApplication::platformName() == QLatin1String("windows")) {
    // A frameless Qt window is a plain popup: without WS_THICKFRAME / WS_CAPTION the system offers no resize edges, no Aero snap and no maximise animation.
    // They are added back and the whole non-client area is then taken away again in WM_NCCALCSIZE, so nothing of the frame is ever drawn.
    frameFixed_ = true;
    const HWND h = reinterpret_cast<HWND>(winId());
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    st |= WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    SetWindowLongPtrW(h, GWL_STYLE, st);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  }
#endif
}

bool MainWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
  if (type == "windows_generic_MSG" && titleBar_) {
    MSG* m = static_cast<MSG*>(message);
    const HWND h = m->hwnd;
    switch (m->message) {
      case WM_NCCALCSIZE:
        if (m->wParam) {
          auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(m->lParam);
          if (IsZoomed(h)) {   // a maximised window overhangs its monitor by the frame width: the client area is the work area
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) p->rgrc[0] = mi.rcWork;
          }
          *result = 0;
          return true;   // no non-client area: the page is the whole window
        }
        break;
      case WM_NCHITTEST: {
        if (isFullScreen()) break;
        POINT pt{GET_X_LPARAM(m->lParam), GET_Y_LPARAM(m->lParam)};
        RECT wr;
        GetWindowRect(h, &wr);
        const double dpr = devicePixelRatioF();
        const int edge = std::max(5, int(std::lround(6 * dpr)));
        const bool maxed = IsZoomed(h);
        const bool left = pt.x - wr.left < edge, right = wr.right - pt.x <= edge, top = pt.y - wr.top < edge, bottom = wr.bottom - pt.y <= edge;
        if (!maxed) {
          if (top && left) { *result = HTTOPLEFT; return true; }
          if (top && right) { *result = HTTOPRIGHT; return true; }
          if (bottom && left) { *result = HTBOTTOMLEFT; return true; }
          if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
          if (left) { *result = HTLEFT; return true; }
          if (right) { *result = HTRIGHT; return true; }
          if (top) { *result = HTTOP; return true; }
          if (bottom) { *result = HTBOTTOM; return true; }
        }
        if (pt.y > wr.top + 8 + int(std::lround(TitleBar::kHeight * dpr))) break;   // the page area (the pen, the mouse): nothing more to ask, this is hit-tested on every move
        POINT cp = pt;
        ScreenToClient(h, &cp);
        const QPoint local(int(std::lround(cp.x / dpr)), int(std::lround(cp.y / dpr)));
        if (titleBar_->isVisible() && local.y() >= 0 && local.y() < titleBar_->height() && titleBar_->isCaptionAt(titleBar_->mapFrom(this, local))) { *result = HTCAPTION; return true; }
        break;
      }
      default: break;
    }
  }
#else
  Q_UNUSED(type); Q_UNUSED(message); Q_UNUSED(result);
#endif
  return QMainWindow::nativeEvent(type, message, result);
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (!maybeSave()) { e->ignore(); return; }
  e->accept();
}

void MainWindow::setAutosaveMinutes(int minutes) {
  autosaveTimer_.stop();
  if (minutes > 0) autosaveTimer_.start(std::min(minutes, 120) * 60 * 1000);
}

QString MainWindow::autosaveDir() const {
  return autosaveDir_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave" : autosaveDir_;
}

bool MainWindow::autosaveNow() {
  if (!ed_.dirty() || ed_.changeCount() == autosavedAt_) return false;
  const QString dir = autosaveDir();
  QDir().mkpath(dir);
  const QString name = (fileName_.isEmpty() ? QStringLiteral("sin-titulo") : QFileInfo(fileName_).completeBaseName()) + ".autosave.xopp";
  QString err;
  if (!saveXopp(dir + "/" + name, ed_.doc(), &err)) return false;   // never touches the user's file: ed_.path() and the dirty state stay as they are
  autosavedAt_ = ed_.changeCount();
  showMessage("Copia de recuperación guardada (" + QTime::currentTime().toString("HH:mm:ss") + ")");
  return true;
}
