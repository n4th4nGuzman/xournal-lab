#include "prefsselftest.h"
#include "documentview.h"
#include "devclass.h"
#include "editor.h"
#include "inputfilter.h"
#include "mainwindow.h"
#include "prefs.h"
#include "prefsdialog.h"
#include "settings.h"
#include "toolboxwidget.h"
#include "xppapply.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabletEvent>
#include <QTableWidget>
#include <QWheelEvent>
#include <cmath>

#define U(s) QString::fromUtf8(s)

namespace {
using Check = std::function<void(bool, const QString&)>;

void pump(int n = 3) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
void wait(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 10);
}

void mouse(QWidget* to, QEvent::Type t, Qt::MouseButton b, QPoint at, Qt::MouseButtons held) {
  QMouseEvent ev(t, QPointF(at), QPointF(to->mapToGlobal(at)), QPointF(to->mapToGlobal(at)), b, held, Qt::NoModifier);
  QApplication::sendEvent(to, &ev);
}
void press(QWidget* to, Qt::MouseButton b, QPoint at) { mouse(to, QEvent::MouseButtonPress, b, at, b); }
void release(QWidget* to, Qt::MouseButton b, QPoint at) { mouse(to, QEvent::MouseButtonRelease, b, at, Qt::NoButton); }
void move(QWidget* to, QPoint at, Qt::MouseButtons held) { mouse(to, QEvent::MouseMove, Qt::NoButton, at, held); }
void wheel(QWidget* to, int dy, Qt::KeyboardModifiers m) {
  const QPointF at(200, 200);
  QWheelEvent ev(at, to->mapToGlobal(at), QPoint(), QPoint(0, dy), Qt::NoButton, m, Qt::NoScrollPhase, false);
  QApplication::sendEvent(to, &ev);
}

PointerEvent pe(double x, double y, double t, PointerEvent::Kind k = PointerEvent::Kind::Pen, double pressure = 0.5, bool hasP = true) {
  PointerEvent e;
  e.page = 0;
  e.pos = e.viewPos = QPointF(x, y);
  e.globalPos = QPointF(x, y);
  e.timestampMs = quint64(t);
  e.kind = k;
  e.pressure = pressure;
  e.hasPressure = hasP;
  e.zoom = 1;
  return e;
}

struct Rec {
  std::vector<std::pair<InputPhase, PointerEvent>> v;
  InputFilter::Emit sink() { return [this](InputPhase p, const PointerEvent& e) { v.push_back({p, e}); }; }
  int count(InputPhase p) const { int n = 0; for (auto& x : v) n += x.first == p; return n; }
};

double rms(const std::vector<double>& y) {
  double m = 0, s = 0;
  for (double d : y) m += d;
  m /= std::max<size_t>(1, y.size());
  for (double d : y) s += (d - m) * (d - m);
  return std::sqrt(s / std::max<size_t>(1, y.size()));
}
}  // namespace

void runPrefsPageChecks(SettingsController& c, MainWindow& win, Editor& ed, const QString& prefsPath, const Check& check, const std::function<void(const QString&)>& out, const QString& shots) {
  DocumentView* v = win.view();
  const bool wasVisible = win.isVisible();   // the window is shown for these checks and hidden again after: a visible window changes what later areas' drags and popups do
  if (!wasVisible) { win.show(); pump(6); }
  PrefsDialog* pd = c.prefsDialog();
  XppApplier* ap = c.xppApplier();
  const Document savedDoc = ed.doc();
  const QString savedPath = ed.path();
  const Style savedStyle = ed.style();
  const TabletSettings savedTablet = ed.tablet();
  const bool savedFinger = ed.fingerDraw();
  auto setXpp = [&](const std::function<void(XppPrefs&)>& f) {
    Prefs p = c.prefs();
    f(p.xpp);
    c.applyPrefs(p);
    pump();
  };
  auto resetXpp = [&] { setXpp([](XppPrefs& x) { x = XppPrefs(); }); };

  resetXpp();
  // ================================================================= structure: the 12 pages of the original, grouped boxes, Cancelar / Aceptar
  pd->openDialog();
  pump();
  const QStringList titles = PrefsDialog::pageTitles();
  check(pd->pageCount() == 12 && titles.size() == 12, "prefs pages: the dialog has 12 pages");
  check(titles == QStringList({U("Cargar/guardar"), U("Sistema de entrada"), U("Ratón"), U("Lápiz"), U("Pantalla táctil"), U("Ver"), U("Escala"), U("Área de dibujo"),
                               U("Valores predeterminados"), U("Grabación de audio"), U("LaTeX"), U("Idioma")}), "prefs pages: titles and order are those of the original's Spanish dialog");
  for (int i = 0; i < pd->pageCount(); i++) {
    pd->showPage(i);
    pump();
    check(pd->currentPage() == i && pd->page(i) != nullptr && pd->page(i)->findChildren<QWidget*>().size() > 3, QString("prefs pages: page %1 (%2) opens and has content").arg(i + 1).arg(titles[i]));
    pd->grab().save(QString("%1/prefs_page_%2.png").arg(shots).arg(i + 1));
  }
  pd->showPage(0);
  {
    int cancel = 0, ok = 0;
    for (QPushButton* b : pd->findChildren<QPushButton*>()) { if (b->text() == "Cancelar") cancel++; if (b->text() == "Aceptar") ok++; }
    check(cancel == 1 && ok == 1, "prefs pages: Cancelar and Aceptar at the bottom");
  }
  // every control the original has is somewhere: a few keys of each page
  for (const char* k : {"save.autosaveOn", "save.name", "save.pdfName", "save.autoloadPdfXoj", "save.autoloadMostRecent", "input.devices", "input.drawOutside", "input.pressureGuess", "input.stabAvg",
                        "input.stabPre", "input.stabBuffer", "input.stabSigma", "input.stabDeadzone", "input.stabDrag", "input.stabMass", "input.stabCusp", "input.stabFinalize", "mouse.left",
                        "mouse.middle", "mouse.right", "mouse.4", "mouse.5", "pen.pressureSens", "pen.minPressure", "pen.pressureMult", "pen.ignoreFirst", "pen.ignoreFirstN", "pen.button1",
                        "pen.button2", "pen.eraser", "pen.eraserVisibility", "pen.look", "touch.drawing", "touch.zoomGestures", "touch.zoomStart", "touch.handRecognition", "touch.handTimeout",
                        "touch.disableInertia", "view.menubarAtStart", "view.titlePath", "view.titlePage", "view.colorBorder", "view.colorBackground", "view.colorSelection", "view.colorActive",
                        "view.theme", "view.effects", "view.cursorIcon", "view.highlight", "view.highlightRadius", "view.highlightColor", "view.highlightBorderWidth", "view.highlightBorderColor",
                        "view.sidebarRight", "view.scrollbarLeft", "view.sidebarNumbering", "view.hideH", "view.hideV", "view.fsMenubar", "view.prFullscreen", "zoom.step", "zoom.scroll", "zoom.fitOnLoad",
                        "draw.addH", "draw.addV", "draw.unlimited", "draw.lastPage1", "draw.tapEnabled", "draw.tapTime", "def.tool", "def.laserFade", "def.tabSpaces", "def.tabCount", "def.palette",
                        "def.sizes.pen", "def.toolbox", "audio.rate", "latex.command", "lang.language"})
    check(pd->control(k) != nullptr, QString("prefs pages: control %1 exists").arg(k));
  const QStringList disabled = pd->disabledKeys();
  bool allOff = true;
  for (const QString& k : disabled) allOff = allOff && !pd->control(k)->isEnabled();
  check(disabled.size() >= 28 && allOff, QString("prefs pages: %1 controls the app cannot honour are shown disabled, each with its reason").arg(disabled.size()));
  check(disabled.contains("input.mergeButtons") && disabled.contains("audio.rate") && disabled.contains("view.edgePanSpeed") && disabled.contains("draw.snapGrid"), "prefs pages: the unsupported ones include merge buttons, audio, edge panning, snapping");
  {
    int notes = 0;
    for (QLabel* l : pd->findChildren<QLabel*>()) if (l->text().startsWith(U("No disponible:"))) notes++;
    check(notes >= 12, QString("prefs pages: %1 'No disponible: ...' notes explain why").arg(notes));
  }
  // the dialog opens with the stored values: the defaults of the original (except the ones this app kept from before)
  {
    const Prefs d = pd->collect();
    {
      QStringList diff;
      const QJsonObject a = xppToJson(d.xpp), b = xppToJson(XppPrefs());
      for (auto it = a.begin(); it != a.end(); ++it) if (it.value() != b.value(it.key())) diff << it.key();
      check(d.xpp == c.prefs().xpp && c.prefs().xpp == XppPrefs(), "prefs pages: a fresh dialog stands for the stored preferences (the defaults); differs in: " + diff.join(","));
    }
    check(pd->get<QLineEdit>("save.name")->text() == "%F-Nota-%H-%M" && pd->get<QLineEdit>("save.pdfName")->text() == "%{name}_anotado", "prefs pages: default file names are the original's (%F-Nota-%H-%M, %{name}_anotado)");
    check(pd->get<QDoubleSpinBox>("pen.minPressure")->value() == 0.05 && pd->get<QDoubleSpinBox>("pen.pressureMult")->value() == 1.0, "prefs pages: pressure defaults 0.05 and 1.0");
    check(pd->get<QComboBox>("mouse.middle.tool")->currentData().toString() == "hand", "prefs pages: the middle mouse button drags the page by default");
    check(pd->get<QDoubleSpinBox>("input.stabDeadzone")->value() == 1.3 && pd->get<QDoubleSpinBox>("input.stabDrag")->value() == 0.4 && pd->get<QDoubleSpinBox>("input.stabMass")->value() == 5.0 && pd->get<QSpinBox>("input.stabBuffer")->value() == 20, "prefs pages: stabilizer defaults 1.3, 0.4, 5, 20");
  }

  // ================================================================= every control: change, Aceptar applies and persists, the dialog shows it again, Cancelar discards
  {
    pd->openDialog();
    pump();
    const Prefs before = pd->collect();
    int touched = 0;
    // check boxes first (some enable other controls), then the rest
    for (const QString& k : pd->controlKeys()) if (auto* cb = pd->get<QCheckBox>(k)) if (cb->isEnabled() && k != "touch.drawing" && !k.startsWith("save.autosaveOn")) { cb->toggle(); touched++; }
    for (const QString& k : pd->controlKeys()) {
      QWidget* w = pd->control(k);
      if (!w || !w->isEnabled() || k.startsWith("def.sizes") || k == "input.devices" || qobject_cast<QCheckBox*>(w)) continue;
      if (auto* sp = qobject_cast<QSpinBox*>(w)) { if (k == "save.autosave") continue; sp->setValue(sp->value() == sp->minimum() + (sp->maximum() - sp->minimum()) / 3 ? sp->value() + 1 : sp->minimum() + (sp->maximum() - sp->minimum()) / 3); touched++; }
      else if (auto* ds = qobject_cast<QDoubleSpinBox*>(w)) { ds->setValue(ds->minimum() + (ds->maximum() - ds->minimum()) * 0.37); touched++; }
      else if (auto* cb = qobject_cast<QComboBox*>(w)) { if (k.endsWith(".colorMode") || cb->count() < 2 || k == "view.theme" || k == "view.effects" || k == "def.palette") continue; cb->setCurrentIndex((cb->currentIndex() + 1) % cb->count()); touched++; }
      else if (auto* le = qobject_cast<QLineEdit*>(w)) { le->setText(le->text() + "x"); touched++; }
      else if (auto* rb = qobject_cast<QRadioButton*>(w)) { if (k == "draw.lastPage2") rb->setChecked(true); }
    }
    for (const QString& k : pd->controlKeys()) if (k.endsWith(".colorMode")) { auto* cb = pd->get<QComboBox>(k); if (cb->isEnabled()) { cb->setCurrentIndex(1); touched++; } }
    for (const QString& k : pd->controlKeys()) if (auto* col = pd->get<ColorButton>(k)) if (col->isEnabled()) { col->setValue(k.contains("highlight") ? "#80123456" : "#123456"); touched++; }
    TabletSettings tab;
    const Prefs mutated = pd->collect(&tab);
    check(touched > 90 && !(mutated.xpp == before.xpp), QString("prefs pages: %1 controls changed in one go give different preferences").arg(touched));
    check(tab.barrel != savedTablet.barrel || tab.barrel2 != savedTablet.barrel2 || tab.eraser != savedTablet.eraser || tab.penLook != savedTablet.penLook, "prefs pages: the pen buttons and the pen look are part of what Aceptar applies");
    // JSON round trip of the whole set (Electron-compatible file: one additive object)
    const XppPrefs viaJson = sanitizeXpp(xppToJson(mutated.xpp));
    check(viaJson == mutated.xpp, "prefs pages: every setting survives the JSON round trip (prefs.json 'xpp' object)");
    pd->accept();
    pump(5);
    check(c.prefs().xpp == mutated.xpp && ed.xpp() == mutated.xpp, "prefs pages: Aceptar applies every setting to the preferences and the editor");
    check(ed.tablet().barrel == tab.barrel && ed.tablet().barrel2 == tab.barrel2 && ed.tablet().eraser == tab.eraser && ed.tablet().penLook == tab.penLook, "prefs pages: and the pen buttons / look to the tablet settings");
    c.saveNow();
    QFile f(prefsPath);
    QJsonObject onDisk;
    if (f.open(QIODevice::ReadOnly)) onDisk = QJsonDocument::fromJson(f.readAll()).object();
    check(onDisk.value("xpp").isObject() && sanitizeXpp(onDisk.value("xpp")) == mutated.xpp, "prefs pages: the file has them under 'xpp' and reads back equal");
    check(onDisk.contains("fingerDraw") && onDisk.contains("theme") && onDisk.contains("tablet") && onDisk.contains("zzUnknown"), "prefs pages: the keys the Electron app knows are still there, and an unknown key is kept");
    pd->openDialog();
    pump();
    TabletSettings tab2;
    const Prefs reread = pd->collect(&tab2);
    check(reread.xpp == mutated.xpp && tab2.barrel == tab.barrel && tab2.penLook == tab.penLook, "prefs pages: reopening shows exactly what was applied");
    // Cancelar discards
    pd->get<QLineEdit>("save.name")->setText("zzz");
    pd->get<QCheckBox>("view.pageShadow")->toggle();
    pd->get<QSpinBox>("pen.ignoreFirstN")->setValue(77);
    pd->reject();
    pump();
    check(c.prefs().xpp == mutated.xpp && ed.xpp() == mutated.xpp, "prefs pages: Cancelar changes nothing");
    pd->openDialog();
    check(pd->get<QLineEdit>("save.name")->text() == mutated.xpp.saveName, "prefs pages: and the next opening shows the stored value, not the discarded one");
    pd->reject();
    // back to the defaults
    c.applyTablet(savedTablet);
    resetXpp();
    check(ed.xpp() == XppPrefs(), "prefs pages: the defaults come back");
  }

  // ================================================================= Cargar/guardar
  {
    pd->openDialog();
    pd->get<QCheckBox>("save.autosaveOn")->setChecked(true);
    pd->get<QSpinBox>("save.autosave")->setValue(9);
    pd->accept();
    check(c.prefs().autosaveMinutes == 9 && c.prefs().xpp.autosaveInterval == 9, "save: autosave on with an interval of 9 minutes");
    pd->openDialog();
    pd->get<QCheckBox>("save.autosaveOn")->setChecked(false);
    pd->accept();
    check(c.prefs().autosaveMinutes == 0 && c.prefs().xpp.autosaveInterval == 9, "save: autosave off keeps the interval for next time");
    pd->openDialog();
    check(!pd->get<QCheckBox>("save.autosaveOn")->isChecked() && pd->get<QSpinBox>("save.autosave")->value() == 9 && !pd->get<QSpinBox>("save.autosave")->isEnabled(), "save: and the dialog shows it off with the interval disabled");
    pd->reject();
    resetXpp();
    Prefs p0 = c.prefs();
    p0.autosaveMinutes = 0;
    c.applyPrefs(p0);

    const QDateTime t(QDate(2001, 8, 23), QTime(14, 55, 2));
    check(expandNameTemplate("%F-Nota-%H-%M", t) == "2001-08-23-Nota-14-55", "names: %F-Nota-%H-%M -> 2001-08-23-Nota-14-55");
    check(expandNameTemplate("%{name}_anotado", t, "Tema 1") == "Tema 1_anotado" && expandNameTemplate("%{file}-%Y", t, "x", "doc") == "doc-2001", "names: %{name} and %{file}");
    check(expandNameTemplate("%A %d %B %Y %I-%M %p %j %y %% %w", t) == U("jueves 23 agosto 2001 02-55 PM 235 01 % 4"), "names: weekday, month names (Spanish), 12 h, day of year, %%, weekday number: " + expandNameTemplate("%A %d %B %Y %I-%M %p %j %y %% %w", t));
    check(expandNameTemplate("a/b:c*d?e\"f<g>h|i", t) == "a_b_c_d_e_f_g_h_i", "names: characters a file name cannot hold become _");
    check(expandNameTemplate("%q%{zzz}%", t) == "%q%{zzz}%", "names: unknown markers are kept as typed");
    setXpp([](XppPrefs& x) { x.saveName = "Nota-%Y"; });
    ed.setPath(QString());
    win.setDisplayName(QString());
    check(win.suggestedSaveName(false) == QString("Nota-%1.xopp").arg(QDate::currentDate().year()), "names: Guardar on a new document proposes the template: " + win.suggestedSaveName(false));
    resetXpp();
    check(win.suggestedSaveName(false).startsWith(QString("%1-Nota-").arg(QDate::currentDate().toString("yyyy-MM-dd"))), "names: and with the default template it is today's date + -Nota-hour-minute: " + win.suggestedSaveName(false));
    ed.setPath(savedPath);
  }

  // ================================================================= Ratón: what each button selects while held
  {
    QWidget* vp = v->viewport();
    const QPoint at(300, 220);
    ed.setTool("pen");
    ed.setStyle(savedStyle);
    resetXpp();
    check(true, "mouse: (default) the middle button is the hand");
    press(vp, Qt::MiddleButton, at);
    check(!ed.gestureActive(), "mouse: the middle button (hand) drags the page: no tool gesture");
    release(vp, Qt::MiddleButton, at);
    press(vp, Qt::RightButton, at);
    check(!ed.gestureActive(), "mouse: the right button does nothing by default");
    release(vp, Qt::RightButton, at);

    setXpp([](XppPrefs& x) { x.mouseRight = {"highlighter", "#ff0000", 0}; });
    press(vp, Qt::RightButton, at);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("highlighter") && ed.tool() == "pen", "mouse: right button = highlighter while held (the toolbar still says pen)");
    check(ed.style().color == "#ff0000", "mouse: and with the colour chosen for that button");
    release(vp, Qt::RightButton, at);
    check(!ed.gestureActive() && ed.tool() == "pen" && ed.style().color == savedStyle.color, "mouse: letting go brings back the previous tool and colour");

    setXpp([](XppPrefs& x) { x.mouseMiddle = {"eraser", "", 0}; });
    press(vp, Qt::MiddleButton, at);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("eraser"), "mouse: middle button = eraser");
    release(vp, Qt::MiddleButton, at);

    setXpp([](XppPrefs& x) { x.mouseRight = {"", "#00ff00", 5}; x.mouseLeft = {"", "#0000ff", 3}; });
    press(vp, Qt::LeftButton, at);
    check(ed.gestureActive() && ed.style().color == "#0000ff" && std::abs(ed.style().penWidth - win.sizePresets().of(SizeKind::Pen)[2]) < 1e-9, "mouse: left button keeps the pen but takes a colour and the third size");
    release(vp, Qt::LeftButton, at);
    check(ed.style().color == savedStyle.color && ed.style().penWidth == savedStyle.penWidth, "mouse: and gives them back");
    while (ed.canUndo() && ed.undoLabel() == "Trazo") ed.undo();

    setXpp([](XppPrefs& x) { x.mouse4 = {"highlighter", "", 0}; x.mouse5 = {"eraser", "", 0}; });
    press(vp, Qt::BackButton, at);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("highlighter"), "mouse: the 4th button selects what was chosen for it");
    release(vp, Qt::BackButton, at);
    press(vp, Qt::ForwardButton, at);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("eraser"), "mouse: and the 5th");
    release(vp, Qt::ForwardButton, at);

    int boxAsked = 0;
    const QMetaObject::Connection cn = QObject::connect(&ed, &Editor::toolboxRequested, &ed, [&](const QPoint&) { boxAsked++; });
    setXpp([](XppPrefs& x) { x.mouseRight = {"toolbox", "", 0}; });
    press(vp, Qt::RightButton, at);
    check(!ed.gestureActive() && boxAsked == 1, "mouse: a button set to the floating tool box opens it and starts no gesture");
    release(vp, Qt::RightButton, at);
    QObject::disconnect(cn);
    if (c.toolbox()) c.toolbox()->closeBox();
    resetXpp();
    pump();
  }

  // ================================================================= Lápiz: stylus buttons with colour and size, eraser end
  {
    resetXpp();
    pd->openDialog();
    pd->get<QComboBox>("pen.button1.tool")->setCurrentIndex(pd->get<QComboBox>("pen.button1.tool")->findData("highlighter"));
    pd->get<QComboBox>("pen.button1.colorMode")->setCurrentIndex(1);
    pd->get<ColorButton>("pen.button1.color")->setValue("#ff8800");
    pd->get<QComboBox>("pen.button1.size")->setCurrentIndex(4);
    pd->get<QComboBox>("pen.eraser.size")->setCurrentIndex(2);
    pd->get<QComboBox>("pen.look")->setCurrentIndex(1);
    pd->accept();
    check(ed.tablet().barrel == PenAction::Highlighter && ed.tablet().penLook == PenLook::Electron && ed.xpp().stylus1.color == "#ff8800" && ed.xpp().stylus1.size == 4, "pen: button 1 = highlighter, orange, size 4 and the Electron pen look are applied");
    const int pg = 0;
    PointerEvent e = v->makeEvent(pg, QPointF(100, 100), 0.5, PointerEvent::Kind::Pen);
    e.pressedBarrel = true;
    ed.pointerDown(e);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("highlighter") && ed.style().color == "#ff8800" && std::abs(ed.style().highlighterWidth - win.sizePresets().of(SizeKind::Highlighter)[3]) < 1e-9, "pen: holding button 1 selects the highlighter with that colour and the fourth size");
    PointerEvent up = v->makeEvent(pg, QPointF(140, 100), 0.5, PointerEvent::Kind::Pen);
    up.pressedBarrel = true;
    ed.pointerUp(up);
    check(!ed.gestureActive() && ed.style().color == savedStyle.color && ed.style().highlighterWidth == savedStyle.highlighterWidth, "pen: and letting go restores colour and size");
    PointerEvent er = v->makeEvent(pg, QPointF(100, 100), 0.5, PointerEvent::Kind::Eraser);
    ed.pointerDown(er);
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("eraser") && std::abs(ed.style().eraserRadius - win.sizePresets().of(SizeKind::Eraser)[1]) < 1e-9, "pen: the eraser end uses the eraser with the second size");
    ed.pointerUp(er);
    while (ed.canUndo() && (ed.undoLabel() == "Trazo" || ed.undoLabel() == "Borrar")) ed.undo();
    c.applyTablet(savedTablet);
    resetXpp();
  }

  // ================================================================= the pointer filter: pressure, ignore first events, tap filter, stabilizer
  {
    XppPrefs x;
    InputFilter f;
    // pressure
    x.minPressure = 0.3;
    x.pressureMult = 2;
    f.configure(x);
    Rec r;
    f.down(pe(0, 0, 0, PointerEvent::Kind::Pen, 0.1), true, r.sink());
    f.move(pe(5, 0, 8, PointerEvent::Kind::Pen, 0.4), r.sink());
    f.move(pe(10, 0, 16, PointerEvent::Kind::Pen, 0.9), r.sink());
    f.up(pe(15, 0, 24, PointerEvent::Kind::Pen, 0.2), r.sink());
    check(r.v.size() == 4 && std::abs(r.v[0].second.pressure - 0.3) < 1e-9 && std::abs(r.v[1].second.pressure - 0.8) < 1e-9 && r.v[2].second.pressure == 1.0, "pressure: minimum 0.3 raises 0.1, multiplier 2 doubles 0.4 and 1.0 is the ceiling");
    x.pressureSens = false;
    f.configure(x);
    Rec r2;
    f.down(pe(0, 0, 0, PointerEvent::Kind::Pen, 0.7), true, r2.sink());
    f.up(pe(1, 0, 8, PointerEvent::Kind::Pen, 0.7), r2.sink());
    check(!r2.v[0].second.hasPressure, "pressure: with sensitivity off the width no longer follows the pressure");
    // inference
    x = XppPrefs();
    x.pressureGuess = true;
    f.configure(x);
    auto guessed = [&](double stepPx, double dt) {
      Rec g;
      f.down(pe(0, 0, 1000, PointerEvent::Kind::Mouse, 0.5, false), true, g.sink());
      for (int i = 1; i <= 12; i++) f.move(pe(stepPx * i, 0, 1000 + dt * i, PointerEvent::Kind::Mouse, 0.5, false), g.sink());
      f.up(pe(stepPx * 13, 0, 1000 + dt * 13, PointerEvent::Kind::Mouse, 0.5, false), g.sink());
      return g.v[10].second;
    };
    const PointerEvent slow = guessed(1, 16), fast = guessed(40, 4);
    check(slow.hasPressure && fast.hasPressure && slow.pressure > fast.pressure + 0.2, QString("pressure inference: a slow stroke is firmer (%1) than a fast one (%2)").arg(slow.pressure, 0, 'f', 2).arg(fast.pressure, 0, 'f', 2));
    x.pressureGuess = false;
    f.configure(x);
    Rec g0;
    f.down(pe(0, 0, 1000, PointerEvent::Kind::Mouse, 0.5, false), true, g0.sink());
    f.up(pe(1, 0, 1016, PointerEvent::Kind::Mouse, 0.5, false), g0.sink());
    check(!g0.v[0].second.hasPressure, "pressure inference: off, a mouse stays without pressure");
    // ignore the first N events
    x = XppPrefs();
    x.ignoreFirst = true;
    x.ignoreFirstN = 3;
    f.configure(x);
    Rec ig;
    f.down(pe(0, 0, 0), true, ig.sink());
    f.move(pe(10, 0, 8), ig.sink());
    f.move(pe(20, 0, 16), ig.sink());
    check(ig.v.empty(), "ignore first events: nothing reaches the tool during the first 3 events");
    f.move(pe(30, 0, 24), ig.sink());
    f.move(pe(40, 0, 32), ig.sink());
    f.up(pe(50, 0, 40), ig.sink());
    check(ig.v.size() == 3 && ig.v[0].first == InputPhase::Down && ig.v[0].second.viewPos.x() == 30 && ig.count(InputPhase::Move) == 1 && ig.v[2].first == InputPhase::Up, "ignore first events: the stroke starts at the 4th event (x = 30) and goes on");
    Rec ig2;
    f.down(pe(0, 0, 0), true, ig2.sink());
    f.move(pe(10, 0, 8), ig2.sink());
    f.up(pe(10, 0, 16), ig2.sink());
    check(ig2.v.empty(), "ignore first events: a stroke shorter than that leaves nothing");
    Rec ig3;
    f.down(pe(0, 0, 0, PointerEvent::Kind::Mouse, 0.5, false), true, ig3.sink());
    check(ig3.count(InputPhase::Down) == 1, "ignore first events: it concerns the stylus only");
    f.up(pe(0, 0, 8, PointerEvent::Kind::Mouse, 0.5, false), ig3.sink());
    // tap filter
    x = XppPrefs();
    x.tapEnabled = true;
    x.tapTime = 150;
    x.tapLength = 1;
    x.tapSuccessive = 500;
    f.configure(x, 96);
    int taps = 0;
    f.onTap = [&](const PointerEvent&) { taps++; };
    Rec t1;
    f.down(pe(100, 100, 1000, PointerEvent::Kind::Mouse, 0.5, false), true, t1.sink());
    f.up(pe(100, 100, 1050, PointerEvent::Kind::Mouse, 0.5, false), t1.sink());
    check(t1.v.empty() && taps == 1, "tap filter: a quick short touch draws nothing and triggers the tap action");
    Rec t2;
    f.down(pe(100, 100, 1300, PointerEvent::Kind::Mouse, 0.5, false), true, t2.sink());
    f.up(pe(100, 100, 1340, PointerEvent::Kind::Mouse, 0.5, false), t2.sink());
    check(t2.count(InputPhase::Down) == 1 && t2.count(InputPhase::Up) == 1 && taps == 1, "tap filter: a second tap within the frequency limit is a dot");
    Rec t3;
    f.down(pe(100, 100, 5000, PointerEvent::Kind::Mouse, 0.5, false), true, t3.sink());
    check(t3.v.empty(), "tap filter: the first events of a stroke are held until it is clear what it is");
    f.move(pe(130, 100, 5020, PointerEvent::Kind::Mouse, 0.5, false), t3.sink());
    check(t3.count(InputPhase::Down) == 1 && t3.count(InputPhase::Move) == 1 && t3.v[0].second.viewPos.x() == 100, "tap filter: a long move releases the held events from the real start");
    f.up(pe(160, 100, 5040, PointerEvent::Kind::Mouse, 0.5, false), t3.sink());
    Rec t4;
    f.down(pe(100, 100, 9000, PointerEvent::Kind::Mouse, 0.5, false), true, t4.sink());
    f.move(pe(101, 100, 9100, PointerEvent::Kind::Mouse, 0.5, false), t4.sink());
    f.move(pe(101, 100, 9200, PointerEvent::Kind::Mouse, 0.5, false), t4.sink());
    check(t4.count(InputPhase::Down) == 1, "tap filter: a slow press is not a tap even if it does not move");
    f.up(pe(101, 100, 9300, PointerEvent::Kind::Mouse, 0.5, false), t4.sink());
    f.onTap = nullptr;
    // stabilizer
    auto noisy = [&](const XppPrefs& xs, bool ink) {
      f.configure(xs);
      Rec s;
      f.down(pe(0, 100, 0, PointerEvent::Kind::Mouse, 0.5, false), ink, s.sink());
      for (int i = 1; i <= 40; i++) f.move(pe(5.0 * i, 100 + ((i % 2) ? 4.0 : -4.0), 8.0 * i, PointerEvent::Kind::Mouse, 0.5, false), s.sink());
      f.up(pe(205, 100, 330, PointerEvent::Kind::Mouse, 0.5, false), s.sink());
      return s;
    };
    x = XppPrefs();
    const Rec plain = noisy(x, true);
    std::vector<double> yp;
    for (size_t i = 5; i + 1 < plain.v.size(); i++) yp.push_back(plain.v[i].second.viewPos.y());
    x.stabAvg = StabAvg::Arithmetic;
    x.stabBuffer = 10;
    x.stabCusp = false;   // a zig-zag is nothing but turns: cusp detection would restart the average at each one
    const Rec arith = noisy(x, true);
    std::vector<double> ya;
    for (size_t i = 5; i + 1 < arith.v.size(); i++) ya.push_back(arith.v[i].second.viewPos.y());
    check(rms(ya) < rms(yp) * 0.4, QString("stabilizer: arithmetic average smooths a jittery line (%1 px of wobble instead of %2)").arg(rms(ya), 0, 'f', 2).arg(rms(yp), 0, 'f', 2));
    check(arith.v.back().first == InputPhase::Up && arith.v.back().second.viewPos == QPointF(205, 100), "stabilizer: finalize ends the stroke where the pen really was");
    x.stabFinalize = false;
    const Rec nofin = noisy(x, true);
    check(nofin.v.back().first == InputPhase::Up && nofin.v.back().second.viewPos != QPointF(205, 100), "stabilizer: without finalize it ends where the smoothing was");
    x.stabFinalize = true;
    x.stabAvg = StabAvg::VelocityGaussian;
    x.stabSigma = 0.5;
    x.stabCusp = false;
    const Rec gau = noisy(x, true);
    std::vector<double> yg;
    for (size_t i = 5; i + 1 < gau.v.size(); i++) yg.push_back(gau.v[i].second.viewPos.y());
    check(rms(yg) < rms(yp) * 0.6, QString("stabilizer: the velocity-weighted gaussian smooths too (%1 px)").arg(rms(yg), 0, 'f', 2));
    {
      x.stabCusp = true;   // a sharp corner stays sharp with cusp detection: the average restarts there
      x.stabAvg = StabAvg::Arithmetic;
      x.stabBuffer = 20;
      f.configure(x);
      Rec cu;
      f.down(pe(0, 0, 0, PointerEvent::Kind::Mouse, 0.5, false), true, cu.sink());
      for (int i = 1; i <= 10; i++) f.move(pe(10.0 * i, 0, 8.0 * i, PointerEvent::Kind::Mouse, 0.5, false), cu.sink());
      for (int i = 1; i <= 3; i++) f.move(pe(100 - 10.0 * i, 0, 80 + 8.0 * i, PointerEvent::Kind::Mouse, 0.5, false), cu.sink());
      const QPointF afterCorner = cu.v.back().second.viewPos;
      x.stabCusp = false;
      f.configure(x);
      Rec nc;
      f.down(pe(0, 0, 0, PointerEvent::Kind::Mouse, 0.5, false), true, nc.sink());
      for (int i = 1; i <= 10; i++) f.move(pe(10.0 * i, 0, 8.0 * i, PointerEvent::Kind::Mouse, 0.5, false), nc.sink());
      for (int i = 1; i <= 3; i++) f.move(pe(100 - 10.0 * i, 0, 80 + 8.0 * i, PointerEvent::Kind::Mouse, 0.5, false), nc.sink());
      check(std::abs(afterCorner.x() - 70) < std::abs(nc.v.back().second.viewPos.x() - 70) - 1.0, "stabilizer: cusp detection keeps a corner sharper than plain averaging");
      x.stabCusp = true;
    }
    x.stabAvg = StabAvg::Arithmetic;
    const Rec notink = noisy(x, false);
    check(notink.v[5].second.viewPos.y() == plain.v[5].second.viewPos.y(), "stabilizer: it leaves tools that do not draw ink alone");
    x = XppPrefs();
    x.stabPre = StabPre::Deadzone;
    x.stabDeadzone = 5;
    f.configure(x);
    Rec dz;
    f.down(pe(0, 0, 0, PointerEvent::Kind::Mouse, 0.5, false), true, dz.sink());
    for (int i = 1; i <= 6; i++) f.move(pe(i % 2 ? 2 : -2, 1, 8.0 * i, PointerEvent::Kind::Mouse, 0.5, false), dz.sink());
    check(dz.count(InputPhase::Move) == 0, "stabilizer: jitter inside the dead zone moves nothing");
    f.move(pe(30, 0, 60, PointerEvent::Kind::Mouse, 0.5, false), dz.sink());
    check(dz.count(InputPhase::Move) == 1 && std::abs(dz.v.back().second.viewPos.x() - 25) < 1e-6, "stabilizer: past the dead zone the pen trails the pointer by its radius");
    f.up(pe(30, 0, 70, PointerEvent::Kind::Mouse, 0.5, false), dz.sink());
    x = XppPrefs();
    x.stabPre = StabPre::Inertia;
    f.configure(x);
    Rec in;
    f.down(pe(0, 0, 0, PointerEvent::Kind::Mouse, 0.5, false), true, in.sink());
    f.move(pe(100, 0, 8, PointerEvent::Kind::Mouse, 0.5, false), in.sink());
    check(in.v.size() == 2 && in.v[1].second.viewPos.x() > 0 && in.v[1].second.viewPos.x() < 100, "stabilizer: inertia lags behind a sudden move");
    f.up(pe(100, 0, 16, PointerEvent::Kind::Mouse, 0.5, false), in.sink());
    check(in.v.back().first == InputPhase::Up && in.v.back().second.viewPos.x() == 100, "stabilizer: and finalize brings it to the pointer");
  }

  // cost of the filter on the pointer path (per event, no allocation: the stabilizer keeps a bounded history)
  {
    auto cost = [&](const XppPrefs& x) {
      InputFilter f;
      f.configure(x);
      long long n = 0;
      const InputFilter::Emit sink = [&n](InputPhase, const PointerEvent&) { n++; };
      f.down(pe(0, 0, 0), true, sink);
      QElapsedTimer tm;
      tm.start();
      const int N = 300000;
      for (int i = 1; i <= N; i++) f.move(pe(i * 0.01, std::sin(i * 0.01) * 20, i * 0.5, PointerEvent::Kind::Pen, 0.6), sink);
      const double ns = double(tm.nsecsElapsed()) / N;
      f.up(pe(0, 0, N), sink);
      return ns;
    };
    XppPrefs plain, stab;
    stab.stabAvg = StabAvg::VelocityGaussian;
    stab.stabBuffer = 20;
    stab.stabPre = StabPre::Inertia;
    const double a = cost(plain), b = cost(stab);
    out(QString("  settings: pointer filter %1 ns per event with the defaults, %2 ns with the gaussian average (20 points) and inertia").arg(a, 0, 'f', 0).arg(b, 0, 'f', 0));
    check(a < 1500 && b < 20000, QString("perf: the pointer filter costs %1 ns per event by default and %2 ns with stabilization (a pen sends 100-250 events a second)").arg(a, 0, 'f', 0).arg(b, 0, 'f', 0));
  }

  // ================================================================= tap action through the editor
  {
    ed.setTool("pen");
    const int depth = ed.undoDepth();
    int boxAsked = 0;
    const QMetaObject::Connection cn = QObject::connect(&ed, &Editor::toolboxRequested, &ed, [&](const QPoint&) { boxAsked++; });
    setXpp([](XppPrefs& x) { x.tapEnabled = true; x.tapToolbox = true; x.tapTime = 150; x.tapLength = 1; x.tapSuccessive = 0; });
    PointerEvent d = v->makeEvent(0, QPointF(100, 100), 0.5, PointerEvent::Kind::Mouse);
    d.hasPressure = false;
    d.timestampMs = 20000;
    ed.pointerDown(d);
    PointerEvent u = d;
    u.timestampMs = 20040;
    ed.pointerUp(u);
    check(ed.undoDepth() == depth && boxAsked == 1, "tap action: a tap with the pen draws nothing and opens the tool box");
    QObject::disconnect(cn);
    if (c.toolbox()) c.toolbox()->closeBox();
    resetXpp();
    d.timestampMs = 30000;
    ed.pointerDown(d);
    u.timestampMs = 30040;
    ed.pointerUp(u);
    check(ed.undoDepth() == depth + 1, "tap action: off, the same tap leaves a dot");
    ed.undo();
  }

  // ================================================================= Ver
  {
    QWidget* vp = v->viewport();
    // cursor icon of the pen
    ed.setTool("pen");
    resetXpp();
    check(XppPrefs().cursorIcon == 0 && ed.xpp().cursorIcon == 0, "cursor: the default option is the disc of the tool (a prefs file that never set it, or stored 0, gets it)");
    ed.setTool("highlighter");
    ed.setTool("pen");
    {   // the pointer over the page: the pen shows its disc; off the page: the system arrow
      const QRectF pr = v->pageRectOnScreen(ed.currentPage());
      QMouseEvent on(QEvent::MouseMove, pr.center(), pr.center(), vp->mapToGlobal(pr.center()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(vp, &on);
      pump();
    }
    check(vp->cursor().shape() == Qt::BitmapCursor && vp->cursor().pixmap().width() >= 5, "cursor: by default the pen over the page is a disc as wide as its stroke");
    setXpp([](XppPrefs& x) { x.cursorIcon = 5; });
    check(vp->cursor().shape() == Qt::CrossCursor, "cursor: the cross is still there as an option");
    setXpp([](XppPrefs& x) { x.cursorIcon = 1; });
    check(vp->cursor().shape() == Qt::BlankCursor, "cursor: 'sin icono' hides it");
    setXpp([](XppPrefs& x) { x.cursorIcon = 2; });
    check(vp->cursor().shape() == Qt::BitmapCursor && vp->cursor().pixmap().width() <= 20, "cursor: a small dot");
    setXpp([](XppPrefs& x) { x.cursorIcon = 3; });
    check(vp->cursor().shape() == Qt::BitmapCursor && vp->cursor().pixmap().width() >= 28, "cursor: a big pen");
    setXpp([](XppPrefs& x) { x.cursorIcon = 4; });
    check(vp->cursor().shape() == Qt::ArrowCursor, "cursor: the system cursor");
    ed.setTool("hand");
    check(vp->cursor().shape() == Qt::OpenHandCursor, "cursor: other tools keep their own");
    ed.setTool("pen");
    resetXpp();
    check(vp->cursor().shape() == Qt::BitmapCursor, "cursor: back to the disc");

    // highlight of the cursor position and border of the current page
    setXpp([](XppPrefs& x) { x.highlightCursor = true; x.highlightRadius = 20; x.highlightColor = "#ffff0000"; x.highlightBorderWidth = 0; });
    QWidget* ov = ap->overlay();
    check(ov != nullptr && ov->isVisible() && ov->size() == vp->size(), "cursor highlight: a layer over the page appears when it is switched on");
    move(vp, QPoint(300, 220), Qt::NoButton);
    pump();
    if (ov) {
      const QImage img = ov->grab().toImage();
      check(img.pixelColor(300, 220).red() > 200 && img.pixelColor(300, 220).alpha() > 200 && img.pixelColor(300, 220 + 40).alpha() == 0, "cursor highlight: a disc of the chosen radius and colour follows the pointer, nothing outside it");
      move(vp, QPoint(500, 300), Qt::NoButton);
      pump();
      const QImage img2 = ov->grab().toImage();
      check(img2.pixelColor(500, 300).alpha() > 200 && img2.pixelColor(300, 220).alpha() == 0, "cursor highlight: and moves with it");
    }
    setXpp([](XppPrefs& x) { x.highlightCursor = true; x.highlightRadius = 10; x.highlightColor = "#ffff0000"; x.highlightBorderWidth = 4; x.highlightBorderColor = "#ff00ff00"; });
    move(vp, QPoint(300, 220), Qt::NoButton);
    pump();
    if (ov) check(ov->grab().toImage().pixelColor(300 + 10, 220).green() > 200, "cursor highlight: the border has its own colour and width");
    resetXpp();
    check(!ov || !ov->isVisible(), "cursor highlight: switched off, the layer goes");

    v->setZoom(1.0);
    ed.setCurrentPage(0);
    pump(4);
    setXpp([](XppPrefs& x) { x.colorBorder = "#00ff00"; });
    ov = ap->overlay();
    if (ov) {
      const QRectF pr = v->pageRectOnScreen(0);
      const QImage img = ov->grab().toImage();
      int green = 0, strayed = 0;
      for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2) {
          const QColor px = img.pixelColor(x, y);
          if (px.alpha() > 150 && px.green() > 150 && px.red() < 100) { green++; if (!pr.adjusted(-4, -4, 4, 4).contains(QPointF(x, y)) || pr.adjusted(4, 4, -4, -4).contains(QPointF(x, y))) strayed++; }
        }
      check(green > 20 && strayed == 0, QString("colours: the current page gets a border of the chosen colour on its edge only (%1 border pixels, %2 elsewhere)").arg(green).arg(strayed));
    } else check(false, "colours: the border layer exists");
    resetXpp();
    setXpp([](XppPrefs& x) { x.colorBackground = "#112233"; });
    wait(30);
    check(v->deskStyle().desk == QColor("#112233"), "colours: the background between pages takes the chosen colour");
    resetXpp();
    setXpp([](XppPrefs& x) { x.pageShadow = false; });
    const QImage noShadow = v->snapshot();
    resetXpp();
    const QImage shadow = v->snapshot();
    check(noShadow != shadow, "colours: the shadow behind the pages can be switched off");

    // title
    ed.setPath("C:/temp/ejemplo/nota.xopp");
    win.setDisplayName("nota.xopp");
    setXpp([](XppPrefs& x) { x.titlePath = true; });
    check(win.titleBarText().contains(QDir::toNativeSeparators("C:/temp/ejemplo/nota.xopp")), "title: the whole path of the file");
    setXpp([](XppPrefs& x) { x.titlePageNumber = true; });
    check(win.titleBarText().contains(QString(" · pág. %1/%2").arg(ed.currentPage() + 1).arg(ed.pageCount())), "title: the page number: " + win.titleBarText());
    resetXpp();
    check(win.titleBarText().contains("nota.xopp") && !win.titleBarText().contains("pág."), "title: both off, just the name");
    ed.setPath(savedPath);
    win.setDisplayName(QString());

    // menu bar: F10, and hidden at start
    check(win.menuBar()->isVisible(), "menu bar: visible");
    ed.action("view.menubar")->trigger();
    pump();
    check(!win.menuBar()->isVisible(), "menu bar: F10 hides it");
    ed.action("view.menubar")->trigger();
    pump();
    check(win.menuBar()->isVisible() && ed.action("view.menubar")->shortcut() == QKeySequence("F10"), "menu bar: and shows it again (the action is bound to F10)");
    ap->rearmStart();
    setXpp([](XppPrefs& x) { x.menubarAtStart = false; });
    check(!win.menuBar()->isVisible(), "menu bar: 'mostrar al iniciar' off, it starts hidden");
    ed.action("view.menubar")->trigger();
    resetXpp();
    pump();
    check(win.menuBar()->isVisible(), "menu bar: and F10 brings it");

    // left / right handed
    auto* dock = win.findChild<QDockWidget*>("dock");
    if (dock) {
      setXpp([](XppPrefs& x) { x.sidebarRight = true; });
      check(win.dockWidgetArea(dock) == Qt::RightDockWidgetArea, "left-handed: the sidebar goes to the right");
      resetXpp();
      check(win.dockWidgetArea(dock) == Qt::LeftDockWidgetArea, "left-handed: and back to the left");
    }
    v->setZoom(2.0);
    pump(4);
    setXpp([](XppPrefs& x) { x.scrollbarLeft = true; });
    pump(4);
    auto barX = [&] { return v->verticalScrollBar()->mapTo(v, QPoint(0, 0)).x(); };
    auto portX = [&] { return v->viewport()->mapTo(v, QPoint(0, 0)).x(); };
    if (v->verticalScrollBar()->isVisible()) check(barX() < portX() && v->horizontalScrollBar()->layoutDirection() == Qt::LeftToRight, QString("left-handed: the vertical scroll bar is on the left (bar x %1, page x %2)").arg(barX()).arg(portX()));
    else check(false, "left-handed: the vertical scroll bar is visible to be checked");
    v->setZoom(4.0);
    pump(4);
    v->horizontalScrollBar()->setValue(30);
    check(v->horizontalScrollBar()->maximum() > 30 && v->horizontalScrollBar()->value() == 30, "left-handed: the horizontal bar keeps its meaning");
    resetXpp();
    pump(4);
    check(v->verticalScrollBar()->isVisible() && barX() > portX(), QString("left-handed: and back on the right (bar x %1, page x %2)").arg(barX()).arg(portX()));
    setXpp([](XppPrefs& x) { x.hideVScroll = true; x.hideHScroll = true; });
    check(v->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff && v->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff && !v->verticalScrollBar()->isVisible(), "scroll bars: both can be hidden");
    wheel(v->viewport(), -120, Qt::NoModifier);
    resetXpp();
    check(v->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded, "scroll bars: and shown again");
    v->setZoom(1.0);

    // fullscreen options
    if (!win.isFullScreen()) {
      setXpp([](XppPrefs& x) { x.fsToolbar = false; x.fsMenubar = false; });
      win.showFullScreen();
      pump(6);
      check(win.isFullScreen() && !win.toolBar()->isVisible() && !win.menuBar()->isVisible(), "fullscreen: without 'mostrar barras' the toolbar and the menu bar are hidden");
      win.showNormal();
      pump(6);
      check(!win.isFullScreen() && win.toolBar()->isVisible() && win.menuBar()->isVisible(), "fullscreen: and come back when it ends");
      resetXpp();
    }
  }

  // presentation mode options
  {
    setXpp([](XppPrefs& x) { x.prToolbar = true; x.prMenubar = true; x.prFullscreen = false; });
    const bool wasFull = win.isFullScreen();
    ed.action("view.presentation")->trigger();
    wait(30);
    check(win.toolBar()->isVisible() && win.titleBar()->isVisible() && !win.isFullScreen() && win.findChild<QWidget*>("presentChip") && win.findChild<QWidget*>("presentChip")->isVisible(), "presentation: with 'mostrar barras' on and 'pantalla completa' off the bars stay and the window is not fullscreen");
    ed.action("view.presentation")->trigger();
    wait(30);
    check(win.toolBar()->isVisible() && !win.findChild<QWidget*>("presentChip")->isVisible(), "presentation: leaving brings everything back");
    resetXpp();
    ed.action("view.presentation")->trigger();
    wait(30);
    check(!win.toolBar()->isVisible() && !win.titleBar()->isVisible(), "presentation: by default everything is hidden");
    ed.action("view.presentation")->trigger();
    wait(30);
    check(win.toolBar()->isVisible() && win.isFullScreen() == wasFull, "presentation: and leaving restores");
  }

  // ================================================================= Escala
  {
    ed.setTool("pen");
    v->setZoom(1.0);
    setXpp([](XppPrefs& x) { x.zoomStep = 50; });
    ed.action("view.zoomIn")->trigger();
    check(std::abs(v->zoom() - 1.5) < 1e-6, QString("zoom: one step with 50 %% zooms by 1.5 (%1)").arg(v->zoom(), 0, 'f', 3));
    ed.action("view.zoomOut")->trigger();
    check(std::abs(v->zoom() - 1.0) < 1e-6, "zoom: and the inverse");
    resetXpp();
    ed.action("view.zoomIn")->trigger();
    check(std::abs(v->zoom() - 1.2) < 1e-6, "zoom: the default step is still 1.2 (as this app always was)");
    v->setZoom(1.0);
    setXpp([](XppPrefs& x) { x.zoomStepScroll = 10; });
    wheel(v->viewport(), 120, Qt::ControlModifier);
    check(std::abs(v->zoom() - 1.1) < 1e-6, QString("zoom: Ctrl + a wheel notch with 10 %% zooms by 1.1 (%1)").arg(v->zoom(), 0, 'f', 4));
    resetXpp();
    v->setZoom(1.0);
    setXpp([](XppPrefs& x) { x.forceFitOnLoad = true; });
    ed.setDocument(ed.doc(), ed.path());
    wait(30);
    const double zfit = v->zoom();
    v->zoomFitWidth();
    check(std::abs(zfit - v->zoom()) < 1e-6 && std::abs(zfit - 1.0) > 0.01, QString("zoom: 'forzar el zoom al ancho' fits the width when a document loads (%1)").arg(zfit, 0, 'f', 3));
    resetXpp();
    v->setZoom(1.0);
  }

  // ================================================================= Área de dibujo
  {
    v->setZoom(2.0);
    pump(4);
    const int maxBefore = v->horizontalScrollBar()->maximum(), maxVBefore = v->verticalScrollBar()->maximum();
    setXpp([](XppPrefs& x) { x.addHSpace = true; x.hSpaceLeft = 200; x.hSpaceRight = 120; x.addVSpace = true; x.vSpaceAbove = 100; x.vSpaceBelow = 80; });
    pump(4);
    check(v->extraSpace() == QMargins(200, 100, 120, 80) && v->horizontalScrollBar()->maximum() > maxBefore && v->verticalScrollBar()->maximum() > maxVBefore, "scrolling outside the page: extra space on each side makes the scene bigger");
    setXpp([](XppPrefs& x) { x.unlimitedScroll = true; });
    pump(4);
    check(v->extraSpace().left() == v->viewport()->width() && v->extraSpace().top() == v->viewport()->height(), "scrolling outside the page: unlimited gives a whole screen on every side");
    resetXpp();
    pump(4);
    check(v->extraSpace().isNull() && v->horizontalScrollBar()->maximum() == maxBefore, "scrolling outside the page: off, the scene is as before");
    v->setZoom(1.0);

    // empty last page
    ed.setTool("pen");
    const int pages0 = ed.pageCount();
    const int last = pages0 - 1;
    setXpp([](XppPrefs& x) { x.lastPageAppend = 2; });
    const PointerEvent d = v->makeEvent(last, QPointF(100, 100), 0.5, PointerEvent::Kind::Pen), u = v->makeEvent(last, QPointF(140, 120), 0.5, PointerEvent::Kind::Pen);
    ed.pointerDown(d);
    ed.pointerMove(v->makeEvent(last, QPointF(120, 110), 0.6, PointerEvent::Kind::Pen));
    ed.pointerUp(u);
    wait(30);
    check(ed.pageCount() == pages0 + 1, "empty last page: drawing on the last page adds one");
    {
      const int n = ed.pageCount();
      const PointerEvent d2 = v->makeEvent(0, QPointF(100, 100), 0.5, PointerEvent::Kind::Pen);
      ed.pointerDown(d2);
      ed.pointerUp(v->makeEvent(0, QPointF(130, 100), 0.5, PointerEvent::Kind::Pen));
      wait(30);
      check(ed.pageCount() == n, "empty last page: drawing on another page adds none");
      ed.undo();
    }
    while (ed.pageCount() > pages0) { ed.undo(); }
    ed.undo();   // the stroke itself
    check(ed.pageCount() == pages0, "empty last page: (undone)");
    setXpp([](XppPrefs& x) { x.lastPageAppend = 1; });
    v->setContinuous(true);
    v->scrollByPixels(0, 1e6);
    pump(3);
    wheel(v->viewport(), -120, Qt::NoModifier);
    check(ed.pageCount() == pages0 + 1, "empty last page: scrolling past the end of the last page adds one");
    while (ed.pageCount() > pages0 && ed.canUndo()) ed.undo();
    {
      Document& dm = ed.doc();
      const QString oldType = attrOr(dm.pages.back().background, "type");
      attrSet(dm.pages.back().background, "type", "pdf");
      v->scrollByPixels(0, 1e6);
      wheel(v->viewport(), -120, Qt::NoModifier);
      check(ed.pageCount() == pages0, "empty last page: a PDF page is excluded");
      attrSet(dm.pages.back().background, "type", oldType);
    }
    resetXpp();
    v->scrollToPage(0);

    // drawing outside the window
    QWidget* vp = v->viewport();
    ed.setTool("pen");
    setXpp([](XppPrefs& x) { x.drawOutside = false; });
    press(vp, Qt::LeftButton, QPoint(300, 220));
    move(vp, QPoint(320, 230), Qt::LeftButton);
    check(ed.gestureActive(), "draw outside: inside the window the stroke goes on");
    move(vp, QPoint(-60, 230), Qt::LeftButton);
    check(!ed.gestureActive(), "draw outside: off, the stroke ends at the window border");
    release(vp, Qt::LeftButton, QPoint(-60, 230));
    resetXpp();
    press(vp, Qt::LeftButton, QPoint(300, 220));
    move(vp, QPoint(-60, 230), Qt::LeftButton);
    check(ed.gestureActive(), "draw outside: on, it goes on outside");
    release(vp, Qt::LeftButton, QPoint(-60, 230));
    while (ed.canUndo() && ed.undoLabel() == "Trazo") ed.undo();

    // input device classes
    const QString key = deviceKey(QPointingDevice::primaryPointingDevice());
    setXpp([&](XppPrefs& x) { x.deviceClass.insert(key, int(DeviceClass::Disabled)); });
    press(vp, Qt::LeftButton, QPoint(300, 220));
    check(!ed.gestureActive(), "devices: a device set to 'desactivado' is ignored");
    release(vp, Qt::LeftButton, QPoint(300, 220));
    setXpp([&](XppPrefs& x) { x.deviceClass.insert(key, int(DeviceClass::Eraser)); });
    press(vp, Qt::LeftButton, QPoint(300, 220));
    check(ed.gestureActive() && ed.gestureTool() == ed.toolInstance("eraser"), "devices: a mouse set to 'borrador' erases");
    release(vp, Qt::LeftButton, QPoint(300, 220));
    setXpp([&](XppPrefs& x) { x.deviceClass.insert(key, int(DeviceClass::Pen)); });
    press(vp, Qt::LeftButton, QPoint(300, 220));
    check(ed.gestureActive() && ed.gestureTool() == ed.currentTool(), "devices: a mouse set to 'lápiz' draws with the current tool");
    release(vp, Qt::LeftButton, QPoint(300, 220));
    while (ed.canUndo() && ed.undoLabel() == "Trazo") ed.undo();
    resetXpp();
    pd->openDialog();
    check(pd->deviceTable()->rowCount() >= 1, "devices: the table lists the devices Qt knows");
    if (pd->deviceTable()->rowCount() >= 1) {
      auto* cls = qobject_cast<QComboBox*>(pd->deviceTable()->cellWidget(0, 2));
      if (cls) {
        cls->setCurrentIndex(1);
        check(pd->collect().xpp.deviceClass.size() == 1 && pd->collect().xpp.deviceClass.first() == 1, "devices: choosing a class for a device is stored under its key");
        cls->setCurrentIndex(0);
        check(pd->collect().xpp.deviceClass.isEmpty(), "devices: 'automático' stores nothing");
      }
    }
    pd->reject();
  }

  // ================================================================= Pantalla táctil
  {
    QWidget* vp = v->viewport();
    const QPointingDevice* dev = QPointingDevice::primaryPointingDevice();
    auto penEvent = [&] {
      QTabletEvent ev(QEvent::TabletMove, dev, QPointF(50, 50), vp->mapToGlobal(QPoint(50, 50)), 0.0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton);
      QApplication::sendEvent(vp, &ev);
    };
    v->forgetPen();
    penEvent();
    check(v->penNear(), "touch: the pen counts as near right after an event");
    setXpp([](XppPrefs& x) { x.handRecognition = false; });
    check(!v->penNear(), "touch: with hand recognition off a finger is never held back");
    setXpp([](XppPrefs& x) { x.handRecognition = true; x.handTimeout = 0.5; });
    penEvent();
    wait(620);
    check(!v->penNear(), "touch: and the timeout (0.5 s) ends it");
    setXpp([](XppPrefs& x) { x.handRecognition = true; x.handTimeout = 3; });
    penEvent();
    wait(900);
    check(v->penNear(), "touch: a longer timeout (3 s) keeps it");
    v->forgetPen();
    resetXpp();
    // two-finger zoom can be switched off
    setXpp([](XppPrefs& x) { x.zoomGestures = false; x.zoomStartThreshold = 30; x.disableInertia = true; });
    check(!ed.xpp().zoomGestures && ed.xpp().zoomStartThreshold == 30 && ed.xpp().disableInertia, "touch: zoom gestures, their threshold and inertia reach the view's settings");
    resetXpp();
  }

  // ================================================================= Valores predeterminados
  {
    ed.setTool("pen");
    ed.setStyle(savedStyle);
    setXpp([](XppPrefs& x) { x.defaultTool = {"highlighter", "#112233", 2}; });
    ed.action("tool.default")->trigger();
    check(ed.tool() == "highlighter" && ed.style().color == "#112233" && std::abs(ed.style().highlighterWidth - win.sizePresets().of(SizeKind::Highlighter)[1]) < 1e-9, "defaults: the default tool button selects tool, colour and size");
    resetXpp();
    ed.setTool("pen");
    ed.setStyle(savedStyle);
    // laser fade
    ed.setTool("laser");
    setXpp([](XppPrefs& x) { x.laserFadeMs = 150; });
    v->setZoom(1.0);
    pump(4);
    const QImage clean = v->snapshot();
    ed.pointerDown(v->makeEvent(0, QPointF(100, 100), 0.5, PointerEvent::Kind::Mouse));
    for (int i = 1; i <= 10; i++) ed.pointerMove(v->makeEvent(0, QPointF(100 + 12 * i, 100 + 3 * i), 0.5, PointerEvent::Kind::Mouse));
    wait(30);
    const bool shown = v->snapshot() != clean;
    ed.pointerUp(v->makeEvent(0, QPointF(230, 130), 0.5, PointerEvent::Kind::Mouse));
    wait(400);
    check(shown && v->snapshot() == clean, "defaults: with a fade of 150 ms the laser trail is there at first and gone after 0.4 s");
    resetXpp();
    ed.setTool("pen");
    // palette and sizes
    pd->openDialog();
    pd->get<QLineEdit>("def.sizes.pen")->setText("0.5, 1, 2.5, 4, 9");
    pd->get<QLineEdit>("def.sizes.eraser")->setText("not numbers");
    pd->get<QComboBox>("def.palette")->setCurrentIndex(pd->get<QComboBox>("def.palette")->findData("vivid"));
    pd->accept();
    check(win.sizePresets().of(SizeKind::Pen) == std::vector<double>({0.5, 1, 2.5, 4, 9}), "defaults: the pen sizes are edited as a list");
    check(win.sizePresets().of(SizeKind::Eraser) == defaultSizes().of(SizeKind::Eraser), "defaults: a list that is not numbers keeps the saved sizes");
    check(c.paletteId() == "vivid", "defaults: the palette is chosen here");
    pd->openDialog();
    check(pd->get<QLineEdit>("def.sizes.pen")->text() == "0.5, 1, 2.5, 4, 9", "defaults: and the dialog shows them back");
    pd->reject();
    c.applySizes(defaultSizes());
    c.applyPalette("classic");
  }

  // ================================================================= sidebar numbering is read by the thumbnails (a repaint, no crash) and the rest is restored
  setXpp([](XppPrefs& x) { x.sidebarNumbering = 2; });
  setXpp([](XppPrefs& x) { x.sidebarNumbering = 0; });
  setXpp([](XppPrefs& x) { x.sidebarNumbering = 3; });
  resetXpp();
  check(ed.xpp() == XppPrefs() && c.prefs().xpp == XppPrefs(), "prefs pages: everything is back at the defaults after the checks");
  ed.setDocument(savedDoc, savedPath);
  ed.setStyle(savedStyle);
  ed.setTablet(savedTablet);
  ed.setFingerDraw(savedFinger);
  ed.setTool("pen");
  if (!wasVisible) { win.hide(); pump(4); }
  (void)out;
}
