// Themes. "classic" is the dark grey of the Electron app; "neon" is the synthwave theme of styles.css (same layout, other colours, halos on icons and
// pages). Both are a palette (so every stock widget and dialog follows) plus one style sheet built from the same tokens.
#include "chrome.h"
#include "prefs.h"
#include "svgicon.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace chrome {

const Neon& neon() {
  static const Neon n;
  return n;
}

QString resolveEffects(const QString& pref) {
  if (pref == "full") return "full";
  if (pref == "reduced") return "reduced";
#ifdef Q_OS_WIN
  BOOL animations = TRUE;   // "Show animations in Windows" off = the user asked for less motion
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations) return "reduced";
#endif
  return "full";
}

namespace {
struct Tokens {
  QColor window, base, text, disabled, button, highlight;
  QColor bar1, bar2, bar3, barLine;            // toolbars (three rows in the Electron app)
  QColor hoverBg, hoverBorder, activeBg, activeBorder, activeText;
  QColor panel, panelLine, inputBg, inputText, inputBorder, menuBg, menuBorder, accent;
};

Tokens classicTokens() {
  Tokens t;
  t.window = "#2b2b2b"; t.base = "#222222"; t.text = "#dddddd"; t.disabled = "#808080"; t.button = "#3a3a3a"; t.highlight = "#2f7dd1";
  t.bar1 = "#2b2b2b"; t.bar2 = "#262626"; t.bar3 = "#232323"; t.barLine = "#1b1b1b";
  t.hoverBg = "#3d3d3d"; t.hoverBorder = "#555555"; t.activeBg = "#2f6fd0"; t.activeBorder = "#4a8af0"; t.activeText = "#ffffff";
  t.panel = "#262626"; t.panelLine = "#1b1b1b"; t.inputBg = "#333333"; t.inputText = "#eeeeee"; t.inputBorder = "#555555";
  t.menuBg = "#2b2b2b"; t.menuBorder = "#1b1b1b"; t.accent = "#4a8af0";
  return t;
}

Tokens neonTokens() {
  const Neon& n = neon();
  Tokens t;
  t.window = "#170c36"; t.base = "#0f0824"; t.text = n.text; t.disabled = "#6a5a96"; t.button = "#1e1043"; t.highlight = n.pink;
  t.bar1 = "#1e1043"; t.bar2 = "#170c36"; t.bar3 = "#160b34"; t.barLine = n.pink;
  t.hoverBg = QColor(185, 103, 255, 46); t.hoverBorder = n.purple; t.activeBg = QColor(255, 42, 157, 60); t.activeBorder = n.pink; t.activeText = "#ffffff";
  t.panel = "#140a2e"; t.panelLine = n.purple; t.inputBg = "#0f0824"; t.inputText = n.cyan; t.inputBorder = n.line;
  t.menuBg = "#170c36"; t.menuBorder = n.pink; t.accent = n.pink;
  return t;
}

QString css(const QColor& c) {
  return c.alpha() == 255 ? c.name() : QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QColor iconColour(const QString& id) {
  const Neon& n = neon();
  auto in = [&](std::initializer_list<const char*> l) { for (const char* s : l) if (id == s) return true; return false; };
  if (in({"file.open", "file.save", "file.new", "tool.highlighter", "chrome.theme"})) return n.yellow;
  if (in({"edit.undo", "edit.redo", "nav.back", "nav.forward", "chrome.back", "chrome.forward"})) return n.orange;
  if (in({"edit.cut", "edit.copy", "edit.paste", "journal.newPage", "journal.insertBefore", "journal.append", "chrome.check"})) return n.green;
  if (in({"tool.pen", "tool.eraser", "tool.eraserPart"})) return n.pink;
  if (in({"tool.laser", "journal.delete"})) return n.red;
  if (in({"view.zoomIn", "view.zoomOut", "view.fitWidth", "view.fitPage"})) return n.blue;
  if (in({"tool.select", "tool.lasso", "tool.vspace", "tool.hand", "nav.first", "nav.prev", "nav.next", "nav.last", "journal.moveUp", "journal.moveDown"})) return n.cyan;
  if (id.startsWith("view.sidebar") || id.startsWith("chrome.") || id.startsWith("tool.")) return n.purple;
  return n.cyan;
}
}  // namespace

QColor Chrome::toolbarBackground() const { return (theme_ == "neon" ? neonTokens() : classicTokens()).bar1; }

QIcon Chrome::themedIcon(const QString& actionId, const QIcon& original) const {
  if (theme_ != "neon" || original.isNull()) return original;
  const QColor ink = iconColour(actionId);
  const QSize sz(24, 24);
  auto make = [&](QColor c, double alpha, bool halo) {
    const QPixmap src = original.pixmap(sz, 2.0);
    QPixmap tinted(src.size());
    tinted.setDevicePixelRatio(src.devicePixelRatio());
    tinted.fill(Qt::transparent);
    {
      QPainter p(&tinted);
      p.drawPixmap(0, 0, src);
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(tinted.rect(), c);
    }
    QPixmap out(src.size());
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setOpacity(alpha);
    if (halo)   // the drop-shadow of the CSS: a faint copy pushed around in a ring
      for (int a = 0; a < 8; a++) {
        const double ang = a * M_PI / 4;
        p.setOpacity(0.16 * alpha);
        p.drawPixmap(QPointF(std::cos(ang) * 1.6, std::sin(ang) * 1.6), tinted);
      }
    p.setOpacity(alpha);
    p.drawPixmap(0, 0, tinted);
    return out;
  };
  QIcon ic;
  ic.addPixmap(make(ink, 1.0, !reducedEffects()), QIcon::Normal);
  ic.addPixmap(make(QColor("#fff"), 1.0, !reducedEffects()), QIcon::Normal, QIcon::On);   // checked: white on the pink button, as the CSS
  ic.addPixmap(make(ink, 0.3, false), QIcon::Disabled);
  return ic;
}

QIcon Chrome::electronIcon(const QString& svgName) const {
  SvgLook look;
  if (theme_ == "neon") {
    look.ink = neonInkFor(svgName);
    look.buttonBg = QColor("#170c36");
    look.activeBg = QColor("#3a1050");
    look.strokeWidth = 1.9;
    look.halo = !reducedEffects();
  }
  return svgIcon(svgName, look);
}

void Chrome::tintAction(QAction* a, const QString& id) {
  if (a->isSeparator()) return;
  const QString sn = svgNameForAction(id);
  if (!sn.isEmpty() && hasSvgIcon(sn)) { a->setIcon(electronIcon(sn)); return; }   // the icon set of the Electron app, drawn for the theme
  if (a->menu()) return;
  if (!originals_.count(id)) originals_[id] = a->icon();
  const QIcon& orig = originals_[id];
  if (orig.isNull()) return;
  a->setIcon(theme_ == "neon" ? themedIcon(id, orig) : orig);
}

void Chrome::retintActions() {
  for (const auto& kv : ed.actions()) {
    QAction* a = kv.second;
    if (a->icon().isNull() && !originals_.count(kv.first) && svgNameForAction(kv.first).isEmpty()) continue;
    tintAction(a, kv.first);
  }
  for (QAction* a : navBar_ ? navBar_->actions() : QList<QAction*>()) {
    const QString id = a->property("chromeId").toString();
    if (!id.isEmpty()) tintAction(a, id);
  }
}

QString Chrome::styleSheet() const {
  const bool neonOn = theme_ == "neon";
  const Tokens t = neonOn ? neonTokens() : classicTokens();
  const bool glow = neonOn && !reducedEffects();
  QString s;
  auto add = [&](const QString& rule) { s += rule + "\n"; };
  add(QString("QMainWindow, QDialog { background: %1; }").arg(css(t.window)));
  // The three bars of the window as index.html styles them: #bar (first line), #bar2 (second line) and the optional #bar3 (paper), 12 px text, 2 px gaps.
  const QString g1 = neonOn ? "qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1e1043,stop:1 #160b34)" : css(t.bar1);
  const QString g2 = neonOn ? "qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #170c36,stop:1 #120a26)" : css(t.bar2);
  add(QString("QToolBar { background: %1; border: none; border-bottom: 1px solid %2; padding: 3px 8px 2px 8px; spacing: 2px; font-size: 12px; }").arg(g1, css(t.barLine)));
  add(QString("QToolBar#toolbar, QToolBar#stylebar { background: %1; border-bottom-color: %2; padding: 4px 8px 3px 8px; }").arg(g2, css(neonOn ? neon().cyan : t.barLine)));
  add(QString("QToolBar#navbar { background: %1; border-bottom-color: %2; }").arg(g1, css(t.barLine)));
  add(QString("QToolBar#paperbar { background: %1; border-bottom-color: %2; }").arg(css(t.bar3), css(neonOn ? neon().purple : t.barLine)));
  add(QString("QToolBar#findbar { background: %1; border-bottom-color: %2; }").arg(css(neonOn ? QColor("#170c36") : QColor("#303030")), css(neonOn ? neon().green : t.barLine)));
  const QString sep = neonOn ? "qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(185,103,255,0),stop:0.5 #b967ff,stop:1 rgba(185,103,255,0))" : "#484848";
  add(QString("QToolBar::separator { background: %1; width: 1px; margin: 6px 5px; }").arg(sep));
  add(QString("QToolBar#navbar::separator { margin: 4px 5px; }"));
  add(QString("QToolButton { background: transparent; color: %1; border: 1px solid transparent; border-radius: 5px; padding: 0; min-width: 25px; min-height: 25px; }").arg(css(neonOn ? neon().cyan : QColor("#d8d8d8"))));
  add("QToolBar#navbar QToolButton { min-width: 25px; min-height: 25px; }");
  add("QToolBar#toolbar QToolButton, QToolBar#stylebar QToolButton { min-width: 29px; min-height: 29px; }");
  add(QString("QToolButton:hover { background: %1; border-color: %2; }").arg(css(t.hoverBg), css(neonOn ? neon().purple : QColor("#555555"))));
  add(QString("QToolButton:checked, QToolButton:pressed { background: %1; border-color: %2; color: %3; }").arg(css(t.activeBg), css(t.activeBorder), css(t.activeText)));
  add(QString("QToolButton:disabled { color: %1; background: transparent; border-color: transparent; }").arg(css(t.disabled)));
  add("QToolButton[xlSwatch=\"true\"], QToolButton[xlSwatch=\"true\"]:hover, QToolButton[xlSwatch=\"true\"]:checked, QToolButton#customColor, QToolButton#customColor:hover { background: transparent; border-color: transparent; }");
  add("QToolBar#stylebar QToolButton[xlSwatch=\"true\"] { min-width: 31px; }");   // 34 px swatches 4 px apart (#swatches { gap: 4px })
  add("QToolBar#stylebar QToolButton#customColor { min-width: 26px; min-height: 24px; }");
  add("QToolBar#stylebar QToolButton[xlSize=\"true\"] { min-width: 19px; min-height: 23px; }");
  add("QToolButton::menu-indicator { image: none; width: 0; }");
  add("QToolButton::menu-button { border: none; width: 14px; }");
  add("QLabel#pgOf { padding: 0 2px 0 4px; } QLabel#pgTotal { padding: 0 4px 0 0; } QLabel#zoomLabel { padding: 0 2px; }");
  add(QString("QLabel#pgTotal, QLabel#pgOf, QLabel#zoomLabel { color: %1; font-size: 12px; }").arg(css(neonOn ? neon().yellow : t.text)));
  add(QString("QLabel#info { color: %1; font-size: 12px; }").arg(css(neonOn ? neon().purple : QColor(0xdd, 0xdd, 0xdd, 178))));
  add(QString("QToolBar QLineEdit { padding: 2px 4px; min-height: 18px; }"));
  // narrow windows: MainWindow::arrangeToolbars picks tier 1 or 2 so the rows still fit before the chevron has to hide buttons
  for (const char* id : {"navbar", "toolbar", "stylebar"}) {   // id selectors outrank the property selector, so each bar gets its own tier rule
    add(QString("QToolBar#%1[tier=\"1\"] QToolButton { min-width: 20px; min-height: 22px; padding: 0; } QToolBar#%1[tier=\"2\"] QToolButton { min-width: 16px; min-height: 18px; padding: 0; } "
                "QToolBar#%1[tier=\"1\"], QToolBar#%1[tier=\"2\"] { padding: 2px 4px; spacing: 1px; }").arg(id));
  }
  // the overflow chevron of a toolbar: Qt places it assuming its own 17 px extent, the rule above made it 34 px and half of it fell outside the bar (and the window)
  add("QToolButton#qt_toolbar_ext_button { min-width: 11px; min-height: 20px; padding: 2px 2px; border-radius: 3px; }");
  add(QString("QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px; font-size: 12.5px; } QMenu::item { padding: 5px 22px 5px 12px; border-radius: 4px; margin: 0; } "
              "QMenu::item:selected { background: %4; color: %6; } QMenu::item:disabled { color: %5; } QMenu::separator { height: 1px; background: %3; margin: 4px 6px; }")
          .arg(css(t.menuBg), css(t.text), css(neonOn ? neon().pink : QColor("#555555")), css(neonOn ? QColor(255, 42, 157, 89) : QColor("#2f6fd0")), css(t.disabled), neonOn ? "#ffffff" : "#ffffff"));
  // the side panel (#side): 176 px, a tab row of flat tabs and a "×", 4 equal buttons under it
  add(QString("QDockWidget QTabBar::tab { padding: 4px 6px; margin: 0 2px 0 0; font-size: 13px; border-radius: 4px 4px 0 0; } QToolButton#sideClose { min-width: 20px; min-height: 20px; padding: 0; font-size: 16px; }"));
  add(QString("QDockWidget QToolButton { background: %1; border: 1px solid %2; border-radius: 4px; padding: 3px 0; min-width: 10px; min-height: 22px; } QDockWidget QToolButton:hover { border-color: %3; }")
          .arg(css(neonOn ? QColor("#1e1043") : QColor("#484848")), css(neonOn ? neon().line : QColor("#5a5a5a")), css(neonOn ? neon().cyan : QColor("#8a8a8a"))));
  add("QDockWidget QToolButton#sideClose { background: transparent; border-color: transparent; }");
  add(QString("QDockWidget { color: %1; } QDockWidget::title { background: %2; padding: 5px 8px; border-bottom: 1px solid %3; }").arg(css(t.text), css(t.panel), css(t.panelLine)));
  add(QString("QTabWidget::pane { border: none; background: %1; } QTabBar::tab { background: %2; color: %3; border: 1px solid %4; border-bottom: none; border-radius: 4px 4px 0 0; padding: 7px 16px; margin-right: 2px; } "
              "QTabBar::tab:selected { background: %5; color: #fff; border-color: %6; }")
          .arg(css(neonOn ? QColor("#0f0824") : QColor("#262626")), css(neonOn ? QColor("#1e1043") : QColor("#333333")), css(neonOn ? neon().purple : QColor("#cccccc")),
               css(neonOn ? neon().line : QColor("#444444")), css(neonOn ? QColor("#2a1560") : QColor("#3b3b3b")), css(neonOn ? neon().pink : QColor("#555555"))));
  add(QString("QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 6px; min-height: 18px; selection-background-color: %4; }")
          .arg(css(t.inputBg), css(t.inputText), css(t.inputBorder), css(t.highlight)));
  add(QString("QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %1; }").arg(css(neonOn ? neon().cyan : t.accent)));
  add(QString("QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %3; border: 1px solid %4; }").arg(css(t.menuBg), css(t.text), css(t.activeBg), css(t.inputBorder)));
  add(QString("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px 12px; min-height: 18px; } QPushButton:hover { border-color: %4; } QPushButton:default { border-color: %5; }")
          .arg(css(neonOn ? QColor("#1e1043") : QColor("#444444")), css(neonOn ? neon().cyan : QColor("#eeeeee")), css(t.inputBorder), css(neonOn ? neon().cyan : t.hoverBorder), css(t.accent)));
  add(QString("QListWidget { background: %1; color: %2; border: none; outline: none; }").arg(css(neonOn ? QColor("#0f0824") : t.panel), css(t.text)));
  add(QString("QScrollBar:vertical { background: %1; width: %2px; margin: 0; } QScrollBar::handle:vertical { background: %3; border-radius: 5px; min-height: 30px; margin: 2px; } "
              "QScrollBar:horizontal { background: %1; height: %2px; margin: 0; } QScrollBar::handle:horizontal { background: %3; border-radius: 5px; min-width: 30px; margin: 2px; } "
              "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; } QScrollBar::handle:hover { background: %4; }")
          .arg(css(neonOn ? QColor("#0f0824") : QColor("#2b2b2b")), QString::number(neonOn ? 14 : 14), css(neonOn ? QColor("#4a2a9a") : QColor("#5a5a5a")), css(neonOn ? neon().pink : QColor("#7a7a7a"))));
  add(QString("QToolTip { background: %1; color: %2; border: 1px solid %3; padding: 3px; }").arg(css(t.menuBg), css(t.text), css(t.menuBorder)));
  add(QString("QCheckBox, QRadioButton { color: %1; spacing: 8px; } QLabel { color: %1; }").arg(css(t.text)));   // the indicators are drawn by XlStyle (main.cpp)
  add(QString("QPushButton#presentChip { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; padding: 5px 12px; }").arg(css(neonOn ? QColor(23, 12, 54, 230) : QColor(0, 0, 0, 160)), css(neonOn ? neon().pink : QColor("#888888")), css(t.text)));
  (void)glow;
  return s;
}

void Chrome::applyPalette() {
  const bool neonOn = theme_ == "neon";
  const Tokens t = neonOn ? neonTokens() : classicTokens();
  QPalette p;
  p.setColor(QPalette::Window, t.window);
  p.setColor(QPalette::WindowText, t.text);
  p.setColor(QPalette::Base, t.base);
  p.setColor(QPalette::AlternateBase, t.window);
  p.setColor(QPalette::Text, t.text);
  p.setColor(QPalette::Button, t.button);
  p.setColor(QPalette::ButtonText, t.text);
  p.setColor(QPalette::ToolTipBase, t.menuBg);
  p.setColor(QPalette::ToolTipText, t.text);
  p.setColor(QPalette::Highlight, t.highlight);
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::Link, neonOn ? neon().cyan : QColor("#6aa6ff"));
  p.setColor(QPalette::Disabled, QPalette::Text, t.disabled);
  p.setColor(QPalette::Disabled, QPalette::ButtonText, t.disabled);
  p.setColor(QPalette::Disabled, QPalette::WindowText, t.disabled);
  qApp->setPalette(p);
}

void Chrome::applyDeskAndPaper() {
  DocumentView* v = win.view();
  if (!v) return;
  DocumentView::DeskStyle d;
  if (theme_ == "neon") {
    d.desk = "#120a26";
    d.gradTop = "#2b1560"; d.gradMid = "#120a26"; d.gradBottom = "#0b0618";
    d.frame = neon().pink;
    if (!reducedEffects()) d.glow = neon().pink;
  }
  v->setDeskStyle(d);
  // The page itself stays white in both themes (the Electron app keeps the paper white too), so the paper overrides of RenderOptions stay at their
  // defaults; only the halo of glowing strokes follows "effects": a stroke saved with glow attributes is content, so it is always drawn.
  RenderOptions o = v->renderBase();
  if (o.glow != true || o.darkPaper || !o.paperColorOverride.isEmpty()) { o.glow = true; o.darkPaper = false; o.paperColorOverride.clear(); v->setRenderBase(o); }
}

void Chrome::setTheme(const QString& theme, const QString& effects) {
  const QString th = theme == "neon" ? "neon" : "classic";
  const QString fx = effects == "full" || effects == "reduced" ? effects : "auto";
  if (th == theme_ && fx == effectsPref_ && !qApp->styleSheet().isEmpty()) return;
  const bool changed = th != theme_;
  theme_ = th;
  effectsPref_ = fx;
  fx_ = resolveEffects(fx);
  if (!qApp->style() || (!qApp->style()->inherits("QProxyStyle") && qApp->style()->name().compare("fusion", Qt::CaseInsensitive) != 0)) qApp->setStyle(QStyleFactory::create("Fusion"));
  applyPalette();
  setSvgTheme(th == "neon", th == "neon" && fx_ != "reduced");
  qApp->setStyleSheet(styleSheet());
  win.titleBar()->setTheme(th == "neon", th == "neon" && fx_ != "reduced");
  retintActions();
  applyDeskAndPaper();
  for (QWidget* w : QApplication::topLevelWidgets()) if (w->isVisible()) applyTitleBar(w);   // windows shown later are done when they show
  win.setProperty("chromeTheme", theme_);
  if (changed) emit themeChanged(theme_);
}

}  // namespace chrome
