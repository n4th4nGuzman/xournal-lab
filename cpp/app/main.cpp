// Xournal Lab (C++ / Qt 6). Command line:
//   xl_app [file.xopp]                         the application
//   xl_app --selftest [file.xopp] [--shots DIR] [--log FILE]   headless checks + timings (use QT_QPA_PLATFORM=offscreen)
//   xl_app --bench                              only the table of pointer-event costs with 0 / 8000 / 20000 elements on the page
//   xl_app --screenshot out.png [--page N] [--zoom Z] [file.xopp]   window grab of a page, then exit
#include "crashlog.h"
#include "editor.h"
#include "mainwindow.h"
#include "modules.h"
#include "qa/reopen.h"
#include "selftest.h"
#include "wheelguard.h"
#include "winpen.h"
#include "tools/tools.h"
#include <QApplication>
#include <QFileInfo>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "chrome/chrome.h"
#include "chrome/parts.h"
#include "chrome/svgicon.h"
#include <QToolButton>
#include <QMenu>
#include <QMenuBar>
#include <QStyleFactory>
#include <QTimer>
#include "prefs.h"
#include "settings/prefsdialog.h"
#include "settings/settings.h"
#include "xopp.h"

/** Fusion without the always-underlined mnemonics (the Electron menu bar shows none; Alt+letter still opens a menu). */
class XlStyle : public QProxyStyle {
 public:
  XlStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
  /** Check boxes and radio buttons as Chromium draws them with `color-scheme: dark`: 13 px, a grey outline, the system accent colour when on. */
  void drawPrimitive(PrimitiveElement pe, const QStyleOption* opt, QPainter* p, const QWidget* w) const override {
    if (pe != PE_IndicatorCheckBox && pe != PE_IndicatorRadioButton) { QProxyStyle::drawPrimitive(pe, opt, p, w); return; }
    const bool on = opt->state & State_On, enabled = opt->state & State_Enabled, hot = opt->state & State_MouseOver;
    const QPointF c = QRectF(opt->rect).center();
    const QRectF r(c.x() - 6.5, c.y() - 6.5, 13, 13);
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    if (!enabled) p->setOpacity(0.5);
    const QColor accent = systemAccent();
    p->setPen(QPen(on ? accent : (hot ? QColor(0xbb, 0xbb, 0xbb) : QColor(0x8a, 0x8a, 0x8a)), 1));
    p->setBrush(on ? accent : QColor(0x1f, 0x1f, 0x1f));
    if (pe == PE_IndicatorCheckBox) p->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5); else p->drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
    if (on) {
      p->setPen(QPen(Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p->setBrush(Qt::NoBrush);
      if (pe == PE_IndicatorCheckBox) { QPainterPath tick; tick.moveTo(r.left() + 3.0, r.top() + 6.8); tick.lineTo(r.left() + 5.5, r.top() + 9.2); tick.lineTo(r.left() + 10.0, r.top() + 4.0); p->drawPath(tick); }
      else { p->setPen(Qt::NoPen); p->setBrush(Qt::white); p->drawEllipse(c, 2.6, 2.6); }
    }
    p->restore();
  }
  static QColor systemAccent() {
    static const QColor accent = [] {
      QColor c(0x2f, 0x6f, 0xd0);
#ifdef Q_OS_WIN
      using Fn = HRESULT(WINAPI*)(DWORD*, BOOL*);
      if (HMODULE m = LoadLibraryW(L"dwmapi.dll"))
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "DwmGetColorizationColor")))) { DWORD v = 0; BOOL opaque = FALSE; if (SUCCEEDED(fn(&v, &opaque))) c = QColor((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff); }
#endif
      return c;
    }();
    return accent;
  }
  int styleHint(StyleHint h, const QStyleOption* o = nullptr, const QWidget* w = nullptr, QStyleHintReturn* r = nullptr) const override {
    if (h == SH_UnderlineShortcut) return 0;
    return QProxyStyle::styleHint(h, o, w, r);
  }
};

static void applyDarkTheme(QApplication& app) {
  app.setStyle(new XlStyle);
  QPalette p;
  const QColor base(0x2b, 0x2b, 0x2b), text(0xdd, 0xdd, 0xdd);
  p.setColor(QPalette::Window, base);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, QColor(0x22, 0x22, 0x22));
  p.setColor(QPalette::AlternateBase, base);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, QColor(0x3a, 0x3a, 0x3a));
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::ToolTipBase, QColor(0x20, 0x20, 0x20));
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::Highlight, QColor(0x2f, 0x7d, 0xd1));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
  app.setPalette(p);
}

int main(int argc, char** argv) {
  installCrashLog();
#ifdef Q_OS_WIN
  SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);   // a plugin DLL that cannot be loaded (e.g. Qt6Pdf next to another libstdc++ on PATH) must not pop up a system box over the window: Qt reports it itself
#endif
  QApplication app(argc, argv);
  app.setApplicationName("XournalLabCpp");
  app.setOrganizationName("XournalLab");
  app.setWindowIcon(QIcon(QStringLiteral(":/icon/xournal-lab-256.png")));   // the .exe's own icon comes from xl_app.rc
  applyDarkTheme(app);
  winpen::install();   // no pen / touch ripples, no handwriting panel over the text fields (Windows only; before the first window)
  WheelGuard::install();   // a wheel turn over an unfocused combo box, spin box or slider scrolls the form instead of changing the value
  QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus);   // the Electron menus show a tick column, not icons
  {
    QFont f(QStringLiteral("Segoe UI"));   // body { font: 13px system-ui }
    f.setPixelSize(13);
    app.setFont(f);
  }

  QStringList files;
  QString screenshot, uiShot, uiTheme = "classic";
  QSize uiSize(1920, 1080);
  bool uiWelcome = false;
  QString uiOpen;
  QStringList uiRecent;
  int page = -1;
  double zoom = -1;
  bool selftest = false, bench = false;
  int reopen = 0;
  SelfTestOptions st;
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); i++) {
    const QString& a = args[i];
    auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
    if (a == "--selftest") selftest = true;
    else if (a == "--bench") bench = true;
    else if (a == "--reopen") reopen = next().toInt();
    else if (a == "--screenshot") screenshot = next();
    else if (a == "--uishot") uiShot = next();   // --uishot out.png [--size 1920x1080] [--theme neon] [--welcome] [--recent a.xopp;b.xopp]: the window as the user sees it, then exit
    else if (a == "--size") { const QStringList wh = next().split('x'); if (wh.size() == 2) uiSize = QSize(wh[0].toInt(), wh[1].toInt()); }
    else if (a == "--theme") uiTheme = next();
    else if (a == "--welcome") uiWelcome = true;
    else if (a == "--open") uiOpen = next();   // with --uishot: prefs | tablet | sidebar-pages | sidebar-layers | toolbox | paper | find | opt:<toolId> | menu:<index>: opens it and grabs the window and every popup
    else if (a == "--recent") uiRecent = next().split(';', Qt::SkipEmptyParts);
    else if (a == "--page") page = next().toInt();
    else if (a == "--zoom") zoom = next().toDouble();
    else if (a == "--shots") st.shotsDir = next();
    else if (a == "--log") st.logPath = next();
    else if (a.endsWith(".xopp", Qt::CaseInsensitive)) files << a;
  }

  Editor ed;
  registerBuiltinTools(ed);
  MainWindow win(ed);
  installModules(win);

  if (reopen > 0) return runReopenCheck(win, files, reopen);

  if (selftest || bench) {
    win.setPersistence(false);   // automated runs neither read nor write the user's recent list
    st.files = files;
    st.benchOnly = bench;
    return runSelfTest(win, ed, st);
  }

  if (!uiShot.isEmpty()) {
    win.setPersistence(false);
    chrome::Chrome* c = chromeOf(win);
    if (c) { c->setPersist(false); c->setTheme(uiTheme, "full"); }
    win.setColorSidebar(!qEnvironmentVariableIsSet("XL_NO_COLORBAR"));   // the real app starts with the colour sidebar on (chromeStart > loadUiState)
    win.resize(uiSize);
    if (!files.isEmpty()) win.openFile(files.first());
    win.show();
    if (c && uiWelcome) {
      c->welcome()->recentSource = [uiRecent] { return uiRecent; };
      c->welcome()->showPage();
    }
    for (int i = 0; i < 12; i++) QApplication::processEvents();
    if (!uiOpen.isEmpty() && c) {
      QTimer::singleShot(1200, [&win, uiShot] {   // the dialogs and menus below may run their own event loop: the grab is timed
        int n = 0;
        for (QWidget* tw : QApplication::topLevelWidgets())
          if (tw->isVisible() && tw != &win && tw->width() > 20 && tw->height() > 20) tw->grab().save(uiShot.left(uiShot.size() - 4) + "_top" + QString::number(n++) + ".png");
        win.grab().save(uiShot);
        QApplication::quit();
      });
      Editor& e = c->ed;
      if (uiOpen == "prefs" || uiOpen.startsWith("prefs:")) {   // prefs, or prefs:N for page N (1 = Cargar/guardar ... 12 = Idioma)
        e.action("edit.prefs")->trigger();
        if (uiOpen.size() > 6) if (SettingsController* sc = SettingsController::of(win)) sc->prefsDialog()->showPage(uiOpen.mid(6).toInt() - 1);
      }
      else if (uiOpen == "tablet") e.action("pen.settings")->trigger();
      else if (uiOpen == "sidebar-pages") { c->showSidebar("pages"); QTimer::singleShot(300, [c] { for (int i = 0; i < 40 && c->thumbs()->renderPending() > 0; i++) {} c->thumbs()->list()->viewport()->update(); }); }
      else if (uiOpen == "sidebar-layers") c->showSidebar("layers");
      else if (uiOpen == "toolbox") { win.show(); QTimer::singleShot(100, [&win] { win.editor().action("tool.toolbox")->trigger(); }); }
      else if (uiOpen == "paper") e.action("chrome.paper")->trigger();
      else if (uiOpen == "find") e.action("edit.find")->trigger();
      else if (uiOpen.startsWith("menu:")) { QMenu* m = win.menuBar()->actions().value(uiOpen.mid(5).toInt()) ? win.menuBar()->actions().value(uiOpen.mid(5).toInt())->menu() : nullptr; if (m) QTimer::singleShot(100, [m, &win] { m->popup(win.mapToGlobal(QPoint(40, 36))); }); }
      else if (uiOpen.startsWith("opt:")) {
        QAction* a = e.action("tool." + uiOpen.mid(4));
        for (QToolButton* b : win.toolBar()->findChildren<QToolButton*>()) {
          if (b->defaultAction() != a) continue;
          if (b->menu()) QTimer::singleShot(100, [b] { b->showMenu(); });
          else if (QMenu* dd = b->findChild<QMenu*>("dropdown")) QTimer::singleShot(100, [b, dd] { dd->popup(b->mapToGlobal(QPoint(0, b->height()))); });   // the pen / eraser / highlighter dropdowns
        }
      }
      QApplication::exec();
      return 0;
    }
    if (!files.isEmpty()) { win.view()->zoomFitWidth(); win.view()->renderVisibleNow(); }
    for (int i = 0; i < 6; i++) QApplication::processEvents();
    if (qEnvironmentVariableIsSet("XL_UIDUMP")) {   // geometry of the bars and their buttons, to compare with the Electron CSS (30 / 34 px buttons)
      for (QToolBar* tb : {win.topBar(), win.toolBar(), win.styleBar()}) {
        fprintf(stderr, "%s geom %d,%d %dx%d hint %dx%d\n", qPrintable(tb->objectName()), tb->x(), tb->y(), tb->width(), tb->height(), tb->sizeHint().width(), tb->sizeHint().height());
        int n = 0;
        for (QWidget* w : tb->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) if (w->isVisible() && n++ < 6) fprintf(stderr, "   %s %s %d,%d %dx%d\n", w->metaObject()->className(), qPrintable(w->objectName()), w->x(), w->y(), w->width(), w->height());
      }
    }
    if (qEnvironmentVariableIsSet("XL_UIDUMP") && c)
      for (QPushButton* b : c->welcome()->findChildren<QPushButton*>(QString(), Qt::FindChildrenRecursively)) if (b->objectName().startsWith("w") && !b->objectName().startsWith("wPaper")) fprintf(stderr, "welcome %s %d,%d %dx%d min %dx%d\n", qPrintable(b->objectName()), b->x(), b->y(), b->width(), b->height(), b->minimumHeight(), b->maximumHeight());
    return win.grab().save(uiShot) ? 0 : 1;
  }

  if (!screenshot.isEmpty()) {
    win.setPersistence(false);
    win.resize(1280, 900);
    if (!files.isEmpty()) win.openFile(files.first());
    win.show();
    QApplication::processEvents();
    if (zoom > 0) win.view()->setZoom(zoom);
    if (page >= 0) win.view()->scrollToPage(page);
    for (int i = 0; i < 5; i++) QApplication::processEvents();
    win.view()->renderVisibleNow();
    QApplication::processEvents();
    return win.grab().save(screenshot) ? 0 : 1;
  }

  {   // the user's preferences (read only: the file is shared with the Electron app): pen tip and buttons, finger drawing, autosave
    Prefs p;
    if (loadPrefs(defaultPrefsPath(), &p)) {
      const TabletSettings t = prefsTablet(p);
      ed.setTablet(t);
      Style s = ed.style();
      s.penWidth = t.size;
      s.glowOn = p.glowOn; s.glowSize = p.glowSize; s.glowIntensity = p.glowIntensity;
      ed.setStyle(s);
      ed.setFingerDraw(p.fingerDraw);
      win.setAutosaveMinutes(p.autosaveMinutes);
    }
    chromeStart(win, p, !files.isEmpty());   // theme, effects, palette, welcome page (chrome area)
  }
  win.showMaximized();   // as the Electron app: maximised from the first frame (no small window first)
  if (!files.isEmpty()) QTimer::singleShot(0, [&] { win.openFile(files.first()); });
  return app.exec();
}
