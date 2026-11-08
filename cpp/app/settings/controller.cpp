#include "settings.h"
#include "icons.h"
#include "penlog.h"
#include "palettes.h"
#include "prefsdialog.h"
#include "tabletdialog.h"
#include "toolboxwidget.h"
#include "wheelguard.h"
#include "xppapply.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDrag>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <tuple>
#include <algorithm>
#include <cmath>

namespace {
QIcon swatchIcon(const QColor& c) {
  QPixmap pm(60, 60);
  pm.fill(Qt::transparent);
  pm.setDevicePixelRatio(2);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(255, 255, 255, 140), 2));
  p.setBrush(c);
  p.drawEllipse(QRectF(4, 4, 22, 22));
  return QIcon(pm);
}

double round2(double v) { return std::round(v * 100) / 100; }

KeyCombo keyComboOf(const QKeyEvent* k) {
  KeyCombo c;
  c.key = k->key();
  const quint32 vk = k->nativeVirtualKey();   // Windows: the virtual key of a digit or a letter is its Qt::Key, whatever Shift or AltGr produce
  c.physical = (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ? int(vk) : 0;
  const Qt::KeyboardModifiers m = k->modifiers();
  c.mods = {bool(m & Qt::ControlModifier), bool(m & Qt::ShiftModifier), bool(m & Qt::AltModifier), bool(m & Qt::MetaModifier)};
  return c;
}
Mods modsOf(Qt::KeyboardModifiers m) { return {bool(m & Qt::ControlModifier), bool(m & Qt::ShiftModifier), bool(m & Qt::AltModifier), bool(m & Qt::MetaModifier)}; }

/** The toolbar buttons the Electron toolbar calls by id, as the commands of this window. */
const std::pair<const char*, const char*> kBarActions[] = {
  {"recog", "tool.recognizer"}, {"image", "tool.image"}, {"cut", "edit.cut"}, {"copy", "edit.copy"}, {"paste", "edit.paste"}, {"dup", "edit.duplicate"},
  {"open", "file.open"}, {"save", "file.save"}, {"pgFirst", "nav.first"}, {"pgPrev", "nav.prev"}, {"pgNext", "nav.next"}, {"pgLast", "nav.last"},
  {"histBack", "nav.back"}, {"histFwd", "nav.forward"}, {"pgNew", "journal.newPage"}, {"pgBefore", "journal.insertBefore"}, {"pgAppend", "journal.append"},
  {"pgDelete", "journal.delete"}, {"pgUp", "journal.moveUp"}, {"pgDown", "journal.moveDown"}, {"fit", "view.fitWidth"},
  {"front", "sel.front"}, {"back", "sel.back"}, {"rotL", "sel.rotL"}, {"rotR", "sel.rotR"}, {"flipH", "sel.flipH"}, {"flipV", "sel.flipV"},
};
}  // namespace

SettingsController::SettingsController(MainWindow& win) : QObject(&win), win_(win), ed_(win.editor()) {
  setObjectName("settingsController");
  path_ = defaultPrefsPath();
  toolbox_ = prefsToolbox(prefs_);
  saveTimer_.setSingleShot(true);
  pressTimer_.setSingleShot(true);
  connect(&saveTimer_, &QTimer::timeout, this, [this] { saveNow(); });
  connect(&pressTimer_, &QTimer::timeout, this, [this] {   // long press on a size dot: the current size goes there
    QPointer<QToolButton> b = pressed_;
    pressed_ = nullptr;
    if (!b || !b->defaultAction()) return;
    const QVariant k = b->defaultAction()->property("xlSizeKind"), i = b->defaultAction()->property("xlSizeIndex");
    if (!k.isValid() || !i.isValid()) return;
    longPressed_ = true;
    b->setDown(false);
    savePresetFrom(SizeKind(k.toInt()), i.toInt());
  });
  xppApply_ = new XppApplier(*this);
  connect(&ed_, &Editor::styleChanged, this, [this] { onStyleChanged(); });
  connect(&ed_, &Editor::toolboxRequested, this, [this](const QPoint& p) { showToolbox(p); });
  connect(&ed_, &Editor::gestureFinished, this, [this] { if (restorePending_) endHold(); });
  ed_.setPenButtonBound([this](PenButton b) { return penButtonBound(b); });
  WheelGuard::install();   // before this filter, so the dial's bindings (below) see the wheel first
  qApp->installEventFilter(this);
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { if (saveTimer_.isActive()) saveNow(); });
  connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
    if (s != Qt::ApplicationActive) dropStale();   // a button released while another window had the focus never arrives here
    if (s == Qt::ApplicationInactive && toolboxW_ && !toolboxW_->isEditing()) toolboxW_->closeBox();   // the box is a quick pick: it goes away when the user leaves
  });
  // the first run of a real window reads the file once the event loop is up; headless modes (persistence off) never touch it
  QTimer::singleShot(0, this, [this] { if (!explicitPath_ && !loaded_ && win_.persistence()) loadNow(); });
}

SettingsController::~SettingsController() = default;

SettingsController* SettingsController::of(MainWindow& win) { return win.findChild<SettingsController*>("settingsController", Qt::FindDirectChildrenOnly); }

// ---- storage -------------------------------------------------------------------------------------------------------------------------------
void SettingsController::setStorage(const QString& path) {
  path_ = path;
  explicitPath_ = true;
  loaded_ = true;
}

bool SettingsController::writable() const { return loaded_ && (explicitPath_ || win_.persistence()); }

void SettingsController::loadNow() {
  Prefs p;
  loadPrefs(path_, &p);   // a missing file is the first run: the defaults
  prefs_ = baseline_ = p;
  loaded_ = true;
  applyAll();
}

// Preferencias > Cargar/guardar: start with the most recent file instead of the welcome page (only in a real run, and never when the command line named a file).
void SettingsController::autoloadMostRecent() {
  if (autoloaded_) return;
  autoloaded_ = true;
  if (!prefs_.xpp.autoloadMostRecent || !win_.persistence() || explicitPath_) return;
  for (const QString& a : QCoreApplication::arguments().mid(1)) if (!a.startsWith('-')) return;
  if (!ed_.path().isEmpty() || ed_.dirty()) return;
  const QStringList recent = win_.recentFiles();
  if (recent.isEmpty() || !QFileInfo::exists(recent.first())) return;
  QTimer::singleShot(0, this, [this, f = recent.first()] { win_.openFile(f); });
}

void SettingsController::applyAll() {
  toolbox_ = prefsToolbox(prefs_);
  const TabletSettings t = prefsTablet(prefs_);
  Style s = ed_.style();
  s.penWidth = t.size;
  s.glowOn = prefs_.glowOn;
  s.glowSize = prefs_.glowSize;
  s.glowIntensity = prefs_.glowIntensity;
  inStyle_ = true;
  ed_.setTablet(t);
  ed_.setStyle(s);
  inStyle_ = false;
  ed_.setFingerDraw(prefs_.fingerDraw);
  win_.setSaveOverOpened(prefs_.saveOverOpened);
  win_.setAutosaveMinutes(prefs_.autosaveMinutes);
  win_.setSizePresets(prefsSizes(prefs_));
  applyPalette(prefs_.palette, false);
  xppApply_->apply(prefs_.xpp);
  emit prefsApplied();
  autoloadMostRecent();
}

bool SettingsController::saveNow() {
  if (!writable()) return false;
  saveTimer_.stop();
  Prefs cur;
  QString err;
  if (!loadPrefs(path_, &cur, &err) && !err.isEmpty()) QFile::copy(path_, path_ + ".bad");   // unreadable: keep a copy before writing a good one
  // only what this area changed since the last read/write: whatever the Electron app, the chrome area (theme) or another area wrote meanwhile stays as it is on disk
  auto take = [&](auto Prefs::*f) { if (!(prefs_.*f == baseline_.*f)) cur.*f = prefs_.*f; };
  take(&Prefs::fingerDraw); take(&Prefs::saveOverOpened); take(&Prefs::autosaveMinutes); take(&Prefs::theme); take(&Prefs::palette); take(&Prefs::effects);
  take(&Prefs::welcome); take(&Prefs::tablet); take(&Prefs::sizes); take(&Prefs::toolbox); take(&Prefs::glowOn); take(&Prefs::glowSize); take(&Prefs::glowIntensity); take(&Prefs::xpp);
  if (!savePrefs(path_, cur, &err)) {
    win_.showMessage(QString::fromUtf8("No se pudieron guardar las preferencias: ") + err, 8000);
    return false;
  }
  prefs_ = cur;       // what was written is now what we know
  baseline_ = cur;
  return true;
}

void SettingsController::scheduleSave() { if (writable()) saveTimer_.start(400); }

void SettingsController::mutatePrefs(const std::function<void(Prefs&)>& fn) {
  if (!writable()) return;
  Prefs cur;
  QString err;
  if (!loadPrefs(path_, &cur, &err) && !err.isEmpty()) QFile::copy(path_, path_ + ".bad");
  fn(cur);
  savePrefs(path_, cur, &err);
}

// ---- applying ------------------------------------------------------------------------------------------------------------------------------
void SettingsController::onStyleChanged() {
  if (inStyle_) return;
  const Style& s = ed_.style();
  bool dirty = false;
  TabletSettings t = ed_.tablet();
  const double w = std::clamp(s.penWidth, 0.1, 60.0);
  if (std::abs(t.size - w) > 1e-9) {   // a size dot of the toolbar changed the pen's width: it is the tablet's reference size too
    t.size = w;
    setPrefsTablet(prefs_, t);
    inStyle_ = true;
    ed_.setTablet(t);
    inStyle_ = false;
    dirty = true;
  }
  if (prefs_.glowOn != s.glowOn || std::abs(prefs_.glowSize - s.glowSize) > 1e-9 || std::abs(prefs_.glowIntensity - s.glowIntensity) > 1e-9) {
    prefs_.glowOn = s.glowOn;
    prefs_.glowSize = s.glowSize;
    prefs_.glowIntensity = s.glowIntensity;
    dirty = true;
  }
  if (dirty) scheduleSave();
}

void SettingsController::applyTablet(const TabletSettings& in, bool persist) {
  TabletSettings t = in;
  t.size = std::clamp(t.size, 0.1, 60.0);
  setPrefsTablet(prefs_, t);
  Style s = ed_.style();
  s.penWidth = t.size;
  inStyle_ = true;
  ed_.setTablet(t);
  ed_.setStyle(s);
  inStyle_ = false;
  if (persist) scheduleSave();
}

void SettingsController::applyToolbox(const ToolboxSettings& s, bool persist) {
  toolbox_ = s;
  setPrefsToolbox(prefs_, s);
  if (persist) scheduleSave();
}

void SettingsController::applySizes(const SizePresets& s, bool persist) {
  win_.setSizePresets(s);
  setPrefsSizes(prefs_, s);
  if (persist) scheduleSave();
}

QStringList SettingsController::paletteColors() const { return paletteById(prefs_.palette).colors; }

void SettingsController::applyPalette(const QString& id, bool persist) {
  const Palette& pal = paletteById(id);
  prefs_.palette = pal.id;
  win_.setPaletteColors(pal.colors);   // the current colour stays selected when the preset has it
  if (QAction* a = ed_.action("palette.menu")) a->setToolTip(QString::fromUtf8("Paleta: %1. Clic para cambiarla").arg(pal.name));
  if (persist) scheduleSave();
  // a preset designed for a dark paper only suggests it: the paper is never changed on its own
  if (persist && !pal.paper.isEmpty()) ed_.notify(QString::fromUtf8("%1: pensada para papel oscuro (%2); cámbialo en Diario ▸ Fondo de página ▸ Color de fondo").arg(pal.name, pal.paper));
  emit prefsApplied();
}

void SettingsController::applyPrefs(const Prefs& p, bool persist) {
  const bool palette = p.palette != prefs_.palette;
  prefs_ = p;
  ed_.setFingerDraw(p.fingerDraw);
  win_.setSaveOverOpened(p.saveOverOpened);
  win_.setAutosaveMinutes(p.autosaveMinutes);
  xppApply_->apply(p.xpp);
  if (palette) applyPalette(p.palette, false);
  if (persist) scheduleSave();
  emit prefsApplied();
}

bool SettingsController::savePresetFrom(SizeKind kind, int index) {
  SizePresets s = win_.sizePresets();
  auto& v = s.of(kind);
  if (index < 0 || index >= int(v.size())) return false;
  const Style st = ed_.style();
  const double cur = kind == SizeKind::Pen ? st.penWidth : kind == SizeKind::Highlighter ? st.highlighterWidth : st.eraserRadius;
  const SizeLimits lim = sizeLimits(kind);
  v[size_t(index)] = round2(std::clamp(cur, lim.lo, lim.hi));   // outside the limits the whole list would be taken as corrupt and reset
  applySizes(s);
  ed_.notify(QString::fromUtf8("Tamaño %1 guardado en ese botón").arg(v[size_t(index)]));
  return true;
}

// ---- what bound things do ------------------------------------------------------------------------------------------------------------------
QString SettingsController::actionIdOf(const QString& item) {
  if (item == kToolboxSeparator || item.startsWith("color:")) return QString();
  if (item.startsWith("bar:")) {
    const QString b = item.mid(4);
    for (const auto& kv : kBarActions) if (b == QLatin1String(kv.first)) return QString::fromLatin1(kv.second);
    return QString();
  }
  return item;
}

QString SettingsController::itemOfAction(QAction* a) const {
  if (!a) return QString();
  const QString d = a->data().toString();
  if (d.size() == 7 && d.startsWith('#')) return "color:" + d.toLower();
  const QString id = a->objectName();
  if (id.isEmpty()) return QString();
  for (const ToolboxItem& it : toolboxCatalog()) if (!it.id.isEmpty() && it.id != kToolboxSeparator && actionIdOf(it.id) == id) return it.id;
  return QString();
}

QIcon SettingsController::iconOfItem(const QString& id, const QString& iconName, const QString& label) const {
  if (id.startsWith("color:")) return swatchIcon(QColor(id.mid(6)));
  const QString aid = actionIdOf(id);
  if (aid.startsWith("tool.")) {
    if (const ToolInfo* t = ed_.toolInfo(aid.mid(5))) if (!t->icon.isNull()) return t->icon;
  }
  const auto it = ed_.actions().find(aid);
  if (it != ed_.actions().end() && !it->second->icon().isNull()) return it->second->icon();
  return Icons::make(iconName, label.left(1).toUpper());
}

bool SettingsController::itemActive(const QString& id) const {
  if (id.startsWith("tool.")) return ed_.tool() == id.mid(5);
  if (id.startsWith("color:")) return ed_.style().color.compare(id.mid(6), Qt::CaseInsensitive) == 0;
  if (id == "glow.toggle") return ed_.style().glowOn;
  BrushKind tip;
  if (id.startsWith("brush.") && brushFromId(id.mid(6), &tip)) return ed_.tablet().brush == tip;
  return false;
}

void SettingsController::nudgePen(double factor) {
  TabletSettings t = ed_.tablet();
  t.size = std::clamp(round2(ed_.style().penWidth * factor), 0.1, 60.0);
  applyTablet(t);
}

bool SettingsController::runAction(const QString& item) {
  const QString id = actionIdOf(item);
  if (id.isEmpty()) return false;
  if (id == "tool.previous") { if (ed_.previousTool().isEmpty()) return false; ed_.setTool(ed_.previousTool()); return true; }
  if (id == "toolbox.show" || id == "tool.toolbox") { toggleToolbox(QCursor::pos()); return true; }
  if (id == "pen.sizeUp") { nudgePen(1.25); return true; }
  if (id == "pen.sizeDown") { nudgePen(0.8); return true; }
  if (id == "brush.next" || id == "brush.prev") {
    TabletSettings t = ed_.tablet();
    const int n = int(brushes().size()), d = id == "brush.next" ? 1 : -1;
    t.brush = brushes()[size_t((int(t.brush) + d + n) % n)].kind;
    applyTablet(t);
    return true;
  }
  if (id == "color.next" || id == "color.prev") {
    const QStringList cs = paletteColors();
    if (cs.isEmpty()) return false;
    int i = -1;
    for (int k = 0; k < cs.size(); k++) if (cs[k].left(7).compare(ed_.style().color, Qt::CaseInsensitive) == 0) i = k;
    const int d = id == "color.next" ? 1 : -1, n = int(cs.size());
    Style s = ed_.style();
    s.color = cs[i < 0 ? (d > 0 ? 0 : n - 1) : (i + d + n) % n].left(7);
    ed_.setStyle(s);
    return true;
  }
  if (id.startsWith("tool.")) {
    if (ed_.hasTool(id.mid(5))) { ed_.setTool(id.mid(5)); return true; }
    // tool.recognizer, tool.image, tool.markdown... are commands of the toolbar that only share the prefix: they run below
    if (ed_.actions().find(id) == ed_.actions().end() || !ed_.actions().find(id)->second->isEnabled()) { ed_.notify(QString::fromUtf8("Esa herramienta aún no está disponible")); return false; }
  }
  const auto it = ed_.actions().find(id);
  if (id == "glow.toggle" && (it == ed_.actions().end() || !it->second->isEnabled())) {   // the chrome area owns the glow panel; until it exists the style flag is all there is
    Style s = ed_.style();
    s.glowOn = !s.glowOn;
    ed_.setStyle(s);
    return true;
  }
  if (it == ed_.actions().end() || !it->second->isEnabled()) { ed_.notify(QString::fromUtf8("Esa acción aún no está disponible")); return false; }
  it->second->trigger();
  return true;
}

// A bound button that selects a tool holds it only while pressed (Xournal++ behaviour for a pen button assigned to e.g. the hand): the tool before comes back on
// release. Anything that is not a tool (undo, zoom, size...) fires once on press. While a stroke is on, the press must not switch the tool under it: it is ignored.
void SettingsController::pressBound(int button, bool tablet, const QString& item) {
  const QString id = actionIdOf(item);
  const QString toolId = id.startsWith("tool.") ? id.mid(5) : QString();
  if (toolId.isEmpty() || !ed_.hasTool(toolId)) { runAction(item); return; }
  if (ed_.gestureActive()) return;
  if (held_.empty()) { holdBefore_ = ed_.tool(); holdSet_.clear(); restorePending_ = false; }
  if (!runAction(item)) return;
  held_.push_back({button, tablet});
  if (ed_.tool() == toolId) holdSet_ = toolId;
}

bool SettingsController::releaseBound(int button, bool tablet) {
  for (auto it = held_.begin(); it != held_.end(); ++it) {
    if (it->button != button || it->tablet != tablet) continue;
    held_.erase(it);
    if (held_.empty()) endHold();
    return true;
  }
  return false;
}

void SettingsController::dropStale() {
  endHold();
  swallowTablet_ = 0;
  swallowRelease_ = 0;
  barrelRan_ = Qt::NoButton;
  penMouseHeld_ = Qt::NoButton;
}

// A barrel button of the pen reaches here from the tablet events and, with some drivers, also as a real mouse click: whichever comes first runs the bound action (or
// completes a capture), the other is only swallowed. False when nothing consumed it (not bound and not being captured).
bool SettingsController::barrelPress(int b, bool tablet, const Mods& mods) {
  const QString c = penCombo(b, mods);
  if (c.isNull()) return false;
  const bool echo = barrelRan_.testFlag(Qt::MouseButton(b));
  auto swallow = [&] { (tablet ? swallowTablet_ : swallowRelease_) = b; };
  if (capture_) {
    swallow();
    if (!echo) { barrelRan_ |= Qt::MouseButton(b); auto cb = std::move(capture_); capture_ = nullptr; cb(c); }
    return true;
  }
  const QString id = dialogActive() ? QString() : ed_.tablet().keys.value(c);
  if (id.isEmpty()) return false;
  swallow();
  if (!echo) { barrelRan_ |= Qt::MouseButton(b); pressBound(b, true, id); }
  return true;
}

void SettingsController::endHold() {
  held_.clear();
  if (holdSet_.isEmpty()) { restorePending_ = false; return; }
  if (ed_.tool() != holdSet_) { holdSet_.clear(); restorePending_ = false; return; }   // another tool was picked meanwhile: that one stays
  if (ed_.gestureActive()) { restorePending_ = true; return; }                        // the drag with the held tool finishes first
  const QString back = holdBefore_;
  holdSet_.clear();
  restorePending_ = false;
  if (!back.isEmpty() && ed_.hasTool(back)) ed_.setTool(back);
}

bool SettingsController::penButtonBound(PenButton b) const {
  const QString name = penCombo(b == PenButton::Barrel ? int(Qt::RightButton) : b == PenButton::Barrel2 ? int(Qt::MiddleButton) : 0, Mods());
  if (name.isNull()) return false;
  const auto& keys = ed_.tablet().keys;
  for (auto it = keys.begin(); it != keys.end(); ++it) if (it.key() == name || it.key().endsWith('+' + name)) return true;
  return false;
}

void SettingsController::runToolboxItem(const QString& id) {
  if (id.startsWith("color:")) {
    Style s = ed_.style();
    s.color = id.mid(6).toLower();
    ed_.setStyle(s);
    return;
  }
  runAction(id);
}

// ---- windows -------------------------------------------------------------------------------------------------------------------------------
ToolboxWidget* SettingsController::toolbox() { if (!toolboxW_) toolboxW_ = new ToolboxWidget(*this, &win_); return toolboxW_; }
TabletDialog* SettingsController::tabletDialog() { if (!tabletW_) tabletW_ = new TabletDialog(*this, &win_); return tabletW_; }
PrefsDialog* SettingsController::prefsDialog() { if (!prefsW_) prefsW_ = new PrefsDialog(*this, &win_); return prefsW_; }

void SettingsController::showToolbox(const QPoint& at) {
  if (!toolbox_.enabled) {
    ed_.notify(QString::fromUtf8("El cuadro de herramientas está desactivado (Herramientas ▸ Activar o desactivar el cuadro de herramientas)"));
    return;
  }
  toolbox()->open(at);
}

void SettingsController::toggleToolbox(const QPoint& at) {
  if (toolboxW_ && toolboxW_->isOpen()) toolboxW_->closeBox();
  else showToolbox(win_.geometry().contains(at) ? at : win_.geometry().center());
}

void SettingsController::setToolboxEnabled(bool on) {
  ToolboxSettings s = toolbox_;
  s.enabled = on;
  applyToolbox(s);
  if (!on && toolboxW_) toolboxW_->closeBox();
  ed_.notify(on ? QString::fromUtf8("Cuadro de herramientas activado") : QString::fromUtf8("Cuadro de herramientas desactivado"));
}

void SettingsController::openTablet() { tabletDialog()->openDialog(); }
void SettingsController::openPrefs() { prefsDialog()->openDialog(); }

void SettingsController::popupPaletteMenu() {
  auto* menu = new QMenu(&win_);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto* group = new QActionGroup(menu);
  for (const Palette& p : palettes()) {
    QPixmap pm(int(std::min<qsizetype>(p.colors.size(), 8)) * 18 + 4, 22);
    pm.fill(Qt::transparent);
    QPainter pa(&pm);
    pa.setRenderHint(QPainter::Antialiasing);
    int x = 2;
    for (int i = 0; i < p.colors.size() && i < 8; i++, x += 18) { pa.setPen(QPen(QColor(255, 255, 255, 120), 1)); pa.setBrush(QColor(p.colors[i])); pa.drawEllipse(QRectF(x, 3, 15, 15)); }
    pa.end();
    QAction* a = menu->addAction(QIcon(pm), p.name);
    a->setCheckable(true);
    a->setChecked(p.id == prefs_.palette);
    group->addAction(a);
    connect(a, &QAction::triggered, this, [this, id = p.id] { applyPalette(id); });
  }
  QWidget* w = win_.toolbarWidget(ed_.action("palette.menu"));
  menu->popup(w ? w->mapToGlobal(QPoint(0, w->height())) : QCursor::pos());
}

// ---- capture / bindings --------------------------------------------------------------------------------------------------------------------
void SettingsController::beginCapture(Capture cb) { capture_ = std::move(cb); }
void SettingsController::endCapture() { capture_ = nullptr; }

bool SettingsController::dialogActive() const {
  const QWidget* w = QApplication::activeWindow();
  return w && ((tabletW_ && w == tabletW_) || (prefsW_ && w == prefsW_));
}

bool SettingsController::inTextInput() const {
  const QWidget* f = QApplication::focusWidget();
  if (!f) return false;
  if (qobject_cast<const QLineEdit*>(f) || qobject_cast<const QAbstractSpinBox*>(f) || qobject_cast<const QTextEdit*>(f) || qobject_cast<const QPlainTextEdit*>(f)) return true;
  const auto* cb = qobject_cast<const QComboBox*>(f);
  return cb && cb->isEditable();
}

void SettingsController::startToolbarDrag(QToolButton* b) {
  QAction* a = b->defaultAction();
  const QString item = itemOfAction(a);
  if (item.isEmpty()) return;
  b->setDown(false);
  auto* drag = new QDrag(b);
  drag->setMimeData(ToolboxWidget::makeMime({item, -1, -1}));
  drag->setPixmap(b->grab());
  drag->setHotSpot(QPoint(b->width() / 2, b->height() / 2));
  drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

void SettingsController::traceRaw(QEvent* e) {
  if (!tabletW_) return;
  const QEvent::Type ty = e->type();
  if (ty == QEvent::TabletPress || ty == QEvent::TabletMove || ty == QEvent::TabletRelease || ty == QEvent::TabletEnterProximity || ty == QEvent::TabletLeaveProximity) {
    auto* t = static_cast<QTabletEvent*>(e);
    const QString phase = ty == QEvent::TabletPress ? "pulsa" : ty == QEvent::TabletRelease ? "suelta" : ty == QEvent::TabletMove ? "mueve" : ty == QEvent::TabletEnterProximity ? "entra" : "sale";
    const QString dev = (t->pointerType() == QPointingDevice::PointerType::Eraser ? QString("borrador ") : QString()) + (t->pointingDevice() ? t->pointingDevice()->name() : QString());
    tabletW_->logInput(true, phase, int(t->button()), int(t->buttons()), "no", dev, qHashMulti(0, int(ty), qulonglong(t->timestamp()), int(t->button()), int(t->buttons()), int(t->globalPosition().x()), int(t->globalPosition().y())) | 1);
    return;
  }
  auto* m = static_cast<QMouseEvent*>(e);
  const QString phase = ty == QEvent::MouseButtonPress ? "pulsa" : ty == QEvent::MouseButtonRelease ? "suelta" : "mueve";
  const QString src = m->source() == Qt::MouseEventNotSynthesized ? "no" : m->source() == Qt::MouseEventSynthesizedBySystem ? "sistema" : m->source() == Qt::MouseEventSynthesizedByQt ? "Qt" : "app";
  tabletW_->logInput(false, phase, int(m->button()), int(m->buttons()), src, m->pointingDevice() ? m->pointingDevice()->name() : QString(),
                     qHashMulti(0, int(ty), qulonglong(m->timestamp()), int(m->button()), int(m->buttons()), int(m->globalPosition().x()), int(m->globalPosition().y())) | 1);
}

static void tracePenEvent(QObject* o, QEvent* e) {
  const QEvent::Type t = e->type();
  if (t == QEvent::TabletPress || t == QEvent::TabletRelease || t == QEvent::TabletMove) {
    auto* q = static_cast<QTabletEvent*>(e);
    if (t == QEvent::TabletMove) {   // moves only when the buttons or the contact change, or every 25th
      static int lastB = -1, lastC = -1, n = 0;
      const int b = int(q->buttons()), c = q->pressure() > 0;
      if (b == lastB && c == lastC && (++n % 25)) return;
      lastB = b; lastC = c;
    }
    penlog::write(QString("%1 button=%2 buttons=%3 pressure=%4 target=%5").arg(t == QEvent::TabletPress ? "TabletPress" : t == QEvent::TabletRelease ? "TabletRelease" : "TabletMove")
                      .arg(int(q->button())).arg(int(q->buttons())).arg(q->pressure(), 0, 'f', 2).arg(o ? o->metaObject()->className() : "?"));
  } else if (t == QEvent::TabletEnterProximity || t == QEvent::TabletLeaveProximity) {
    penlog::write(t == QEvent::TabletEnterProximity ? "TabletEnterProximity" : "TabletLeaveProximity");
  } else if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease) {
    auto* m = static_cast<QMouseEvent*>(e);
    if (m->source() == Qt::MouseEventNotSynthesized || m->button() != Qt::LeftButton)
      penlog::write(QString("%1 button=%2 buttons=%3 source=%4 target=%5").arg(t == QEvent::MouseButtonPress ? "MousePress" : "MouseRelease").arg(int(m->button())).arg(int(m->buttons()))
                        .arg(int(m->source())).arg(o ? o->metaObject()->className() : "?"));
  }
}

bool SettingsController::eventFilter(QObject* o, QEvent* e) {
  if (penlog::on()) tracePenEvent(o, e);
  switch (e->type()) {
    case QEvent::ShortcutOverride: {
      // a bound key wins over the usual shortcuts (and while capturing, every key is for the capture): let it arrive as a KeyPress
      auto* k = static_cast<QKeyEvent*>(e);
      if (capture_) { e->accept(); return false; }
      if (dialogActive() || inTextInput()) return false;
      const QString c = comboText(keyComboOf(k));
      if (!c.isNull() && ed_.tablet().keys.contains(c)) e->accept();
      return false;
    }
    case QEvent::KeyPress: {
      auto* k = static_cast<QKeyEvent*>(e);
      if (capture_) {
        if (k->key() == Qt::Key_Escape) { auto cb = std::move(capture_); capture_ = nullptr; cb(QString()); return true; }
        const QString c = comboText(keyComboOf(k));
        if (c.isNull()) return true;   // a modifier on its own: wait for the key
        auto cb = std::move(capture_);
        capture_ = nullptr;
        cb(c);
        return true;
      }
      if (dialogActive() || inTextInput()) return false;
      const QString c = comboText(keyComboOf(k));
      if (c.isNull()) return false;
      const QString id = ed_.tablet().keys.value(c);
      if (id.isEmpty()) return false;
      if (!k->isAutoRepeat()) runAction(id);
      return true;
    }
    case QEvent::Wheel: {
      auto* w = static_cast<QWheelEvent*>(e);
      const QString c = wheelCombo(w->angleDelta().y(), w->angleDelta().x(), modsOf(w->modifiers()));
      if (c.isNull()) return false;
      if (capture_) { auto cb = std::move(capture_); capture_ = nullptr; cb(c); return true; }
      if (dialogActive()) return false;   // the dial scrolls the dialogs as usual
      const QString id = ed_.tablet().keys.value(c);
      if (id.isEmpty()) return false;
      runAction(id);
      return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove: {
      auto* m = static_cast<QMouseEvent*>(e);
      if (tabletW_ && tabletW_->isVisible()) traceRaw(e);
      if (auto* menu = qobject_cast<QMenu*>(o); menu && toolboxW_ && toolboxW_->isOpen() && toolboxW_->isEditing() && m->source() == Qt::MouseEventNotSynthesized) {
        // customise mode: an entry of a menu can be dragged into the box too
        if (e->type() == QEvent::MouseButtonPress && m->button() == Qt::LeftButton) menuPress_ = m->position().toPoint();
        else if (e->type() == QEvent::MouseMove && (m->buttons() & Qt::LeftButton) && !menuPress_.isNull() && (m->position().toPoint() - menuPress_).manhattanLength() >= QApplication::startDragDistance()) {
          const QString item = itemOfAction(menu->actionAt(menuPress_));
          menuPress_ = QPoint();
          if (!item.isEmpty()) {
            auto* drag = new QDrag(menu);
            drag->setMimeData(ToolboxWidget::makeMime({item, -1, -1}));
            QPixmap pm(qMax(120, menu->fontMetrics().horizontalAdvance(menu->actionAt(m->position().toPoint()) ? menu->actionAt(m->position().toPoint())->text() : QString()) + 24), 30);
            pm.fill(QColor(47, 111, 208, 200));
            drag->setPixmap(pm);
            QTimer::singleShot(0, menu, [menu] { menu->close(); });
            drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
            return true;
          }
        } else if (e->type() == QEvent::MouseButtonRelease) menuPress_ = QPoint();
      }
      if (m->source() != Qt::MouseEventNotSynthesized && !qobject_cast<QToolButton*>(o)) return false;
      const bool press = e->type() == QEvent::MouseButtonPress, release = e->type() == QEvent::MouseButtonRelease;
      QWidget* w = qobject_cast<QWidget*>(o);
      if (!w) return false;
      if (tabletW_ && tabletW_->isVisible() && e->type() != QEvent::MouseButtonRelease && m->source() == Qt::MouseEventNotSynthesized)
        tabletW_->sample("mouse", (m->buttons() & Qt::LeftButton) ? 0.5 : 0.0, 0, 0, int(m->buttons()), PenButton::None);
      if (press && toolboxW_ && toolboxW_->isOpen() && m->source() == Qt::MouseEventNotSynthesized) toolboxW_->outsidePress(m->globalPosition());

      // extra mouse buttons bound to an action (a driver can send them): the press runs it and the release is swallowed. While the pen is near, a middle or right
      // click is the pen's barrel button (Editor::notePen), looked up like a tablet event of that button.
      if (m->source() == Qt::MouseEventNotSynthesized) {
        const Qt::MouseButton b = m->button();
        if (release) {
          const bool pen = penMouseHeld_.testFlag(b);
          if (pen) { penMouseHeld_ &= ~Qt::MouseButtons(b); barrelRan_ &= ~Qt::MouseButtons(b); }
          const bool held = releaseBound(int(b), pen);
          if (swallowRelease_ == int(b)) { swallowRelease_ = 0; return true; }
          if (held) return true;
        }
        if (press && (b == Qt::MiddleButton || b == Qt::RightButton || b == Qt::BackButton || b == Qt::ForwardButton)) {
          if ((b == Qt::MiddleButton || b == Qt::RightButton) && ed_.penRecent()) {
            penMouseHeld_ |= b;
            if (barrelPress(int(b), false, modsOf(m->modifiers()))) return true;
          }
          if (b != Qt::RightButton) {   // not the pen's button (or one nothing is bound to): a binding on the mouse button of the same name still applies
            const QString c = mouseCombo(int(b), modsOf(m->modifiers()));
            if (!c.isNull()) {
              if (capture_) { auto cb = std::move(capture_); capture_ = nullptr; penMouseHeld_ &= ~Qt::MouseButtons(b); swallowRelease_ = int(b); cb(c); return true; }
              const QString id = dialogActive() ? QString() : ed_.tablet().keys.value(c);
              if (!id.isEmpty()) { penMouseHeld_ &= ~Qt::MouseButtons(b); swallowRelease_ = int(b); pressBound(int(b), false, id); return true; }
            }
          }
        }
      }

      auto* tb = qobject_cast<QToolButton*>(o);
      if (!tb || (!qobject_cast<QToolBar*>(tb->parentWidget()) || tb->window() != &win_) || !tb->defaultAction()) return false;
      // size dots: right click, Alt+click or a long press stores the current size there; a click uses it
      QAction* a = tb->defaultAction();
      if (a->property("xlSizeKind").isValid()) {
        const SizeKind kind = SizeKind(a->property("xlSizeKind").toInt());
        const int idx = a->property("xlSizeIndex").toInt();
        if (press && (m->button() == Qt::RightButton || (m->button() == Qt::LeftButton && (m->modifiers() & Qt::AltModifier)))) {
          swallowRelease_ = int(m->button());
          savePresetFrom(kind, idx);
          return true;
        }
        if (press && m->button() == Qt::LeftButton) { pressed_ = tb; pressPos_ = m->globalPosition().toPoint(); longPressed_ = false; pressTimer_.start(650); return false; }
        if (e->type() == QEvent::MouseMove && pressed_ && (m->globalPosition().toPoint() - pressPos_).manhattanLength() > 12) { pressTimer_.stop(); pressed_ = nullptr; return false; }
        if (release) {
          pressTimer_.stop();
          pressed_ = nullptr;
          if (longPressed_) { longPressed_ = false; return true; }   // the long press already acted: no click
        }
        return false;
      }
      // customise mode of the tool box: the toolbar's own buttons can be dragged into it
      if (toolboxW_ && toolboxW_->isOpen() && toolboxW_->isEditing()) {
        if (press && m->button() == Qt::LeftButton) { dragBtn_ = tb; pressPos_ = m->globalPosition().toPoint(); return false; }
        if (e->type() == QEvent::MouseMove && dragBtn_ == tb && (m->buttons() & Qt::LeftButton) && (m->globalPosition().toPoint() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
          dragBtn_ = nullptr;
          startToolbarDrag(tb);
          return true;
        }
        if (release) dragBtn_ = nullptr;
      }
      return false;
    }
    case QEvent::TabletPress:
    case QEvent::TabletMove: {
      auto* t = static_cast<QTabletEvent*>(e);
      ed_.notePen(e->type());
      if (tabletW_ && tabletW_->isVisible()) {
        traceRaw(e);
        PenPointer pp;
        pp.isPen = true;
        pp.eraserEnd = t->pointerType() == QPointingDevice::PointerType::Eraser;
        pp.rightButton = t->buttons() & Qt::RightButton;
        pp.middleButton = t->buttons() & Qt::MiddleButton;
        const QString dev = t->pointerType() == QPointingDevice::PointerType::Eraser ? "pen (borrador)" : "pen";
        tabletW_->sample(dev, t->pressure(), t->xTilt(), t->yTilt(), int(t->buttons()), penButtonOf(pp));
      }
      if (e->type() == QEvent::TabletPress) {
        // a barrel button of the pen bound to an action (or being captured): the press runs it and the release is swallowed
        if (barrelPress(int(t->button()), true, modsOf(t->modifiers()))) { t->accept(); return true; }
        if (toolboxW_ && toolboxW_->isOpen()) toolboxW_->outsidePress(t->globalPosition());
      }
      return false;
    }
    case QEvent::TabletRelease: {
      auto* t = static_cast<QTabletEvent*>(e);
      ed_.notePen(e->type());
      if (tabletW_ && tabletW_->isVisible()) traceRaw(e);
      barrelRan_ &= ~Qt::MouseButtons(t->button());
      const bool held = releaseBound(int(t->button()), true);
      if (swallowTablet_ && swallowTablet_ == int(t->button())) { swallowTablet_ = 0; t->accept(); return true; }
      if (held) { t->accept(); return true; }
      return false;
    }
    case QEvent::TabletEnterProximity:
      ed_.notePen(e->type());
      if (tabletW_ && tabletW_->isVisible()) traceRaw(e);
      return false;
    case QEvent::TabletLeaveProximity:   // out of range: a button released out there never reports it
      ed_.notePen(e->type());
      if (tabletW_ && tabletW_->isVisible()) traceRaw(e);
      dropStale();
      return false;
    case QEvent::WindowDeactivate:
      if (o == &win_) dropStale();
      return false;
    case QEvent::TouchBegin:
      if (toolboxW_ && toolboxW_->isOpen()) {
        const auto& pts = static_cast<QTouchEvent*>(e)->points();
        if (!pts.isEmpty()) toolboxW_->outsidePress(pts.first().globalPosition());
      }
      return false;
    default:
      return false;
  }
}

// ---- remembered geometry -------------------------------------------------------------------------------------------------------------------
namespace { QHash<QString, QByteArray>& session() { static QHash<QString, QByteArray> s; return s; } }

void SettingsController::rememberGeometry(QWidget* w, const QString& key) {
  session()[key] = w->saveGeometry();
  if (win_.persistence()) win_.settings().setValue("geom/" + key, w->saveGeometry());
}

void SettingsController::forgetGeometry(const QString& key) {
  session().remove(key);
  if (win_.persistence()) win_.settings().remove("geom/" + key);
}

bool SettingsController::hasGeometry(const QString& key) const {
  if (!session().value(key).isEmpty()) return true;
  return win_.persistence() && win_.settings().value("geom/" + key).toByteArray().size() > 0;
}

void SettingsController::restoreGeometry(QWidget* w, const QString& key, const QSize& fallback) {
  QByteArray g = session().value(key);
  if (g.isEmpty() && win_.persistence()) g = win_.settings().value("geom/" + key).toByteArray();
  if (!g.isEmpty() && w->restoreGeometry(g)) {
    // a monitor that is gone would leave it out of reach: it must still overlap some screen
    const QRect r = w->frameGeometry();
    for (QScreen* s : QGuiApplication::screens()) {
      const QRect i = s->availableGeometry().intersected(r);
      if (i.width() >= 120 && i.height() >= 60) return;
    }
  }
  QScreen* sc = win_.screen() ? win_.screen() : QGuiApplication::primaryScreen();
  const QRect a = sc->availableGeometry();
  const QSize sz = fallback.boundedTo(QSize(int(a.width() * 0.94), int(a.height() * 0.92)));
  w->resize(sz);
  QPoint p = win_.frameGeometry().center() - QPoint(sz.width() / 2, sz.height() / 2);
  p.setX(std::clamp(p.x(), a.left(), std::max(a.left(), a.right() - sz.width())));
  p.setY(std::clamp(p.y(), a.top(), std::max(a.top(), a.bottom() - sz.height())));
  w->move(p);
}

#include "moc_settings.cpp"   // settings.h has no .cpp of its own name: AUTOMOC is told to process it here
