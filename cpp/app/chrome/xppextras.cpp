// Xournal++-style organisation of the window (PARIDAD/xpp_features.md): the layer selector and fullscreen button of the first row, the dropdown menus of the
// pen (sizes, line style, fill), the highlighter, the eraser, and icons for the view commands. The behaviour behind them is the Editor's: a menu only changes the
// drawing Style or the active tool, so keys, tablet buttons and the toolbar all agree.
#include "brush.h"
#include "chrome.h"
#include "docfactory.h"
#include "sizes.h"
#include "svgicon.h"
#include <QActionGroup>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWidgetAction>
#include <QMouseEvent>
#include <QPolygonF>

namespace chrome {

namespace {

/** The sample line shown next to a line style in the pen's dropdown (the four entries of Xournal++'s menu), drawn with the same dash units as the renderer. */
QIcon lineStyleIcon(const QString& style, const QColor& ink) {
  QPixmap pm(112, 24);
  pm.setDevicePixelRatio(2);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const double w = 2.4;
  QPen pen(ink, w, Qt::SolidLine, Qt::FlatCap);
  std::vector<double> units;   // page units of the renderer (dash 6/3, dash-dot 6/3/0.5/3, dot 0.5/3), scaled by 1.6 for the small sample
  if (style == "dash") units = {6, 3};
  else if (style == "dashdot") units = {6, 3, 0.5, 3};
  else if (style == "dot") units = {0.5, 3};
  if (!units.empty()) {
    QList<qreal> d;
    for (double u : units) d << u * 1.6 / w + (u < 1 ? 0.4 : 0);
    pen.setDashPattern(d);
  }
  p.setPen(pen);
  p.drawLine(QPointF(2, 6), QPointF(54, 6));
  return QIcon(pm);
}

/** A menu row with a picture before the text (a sample of the line style, a dot of the size): the application hides icons in menus (the Electron menus have a tick
 *  column), and a QMenu would shrink them to 16 px, so these rows paint themselves. The action keeps the text, the checked state and the click. */
class PictureRow : public QWidget {
 public:
  PictureRow(QAction* a, const QIcon& pic, QSize picSize, QWidget* parent) : QWidget(parent), a_(a), pic_(pic), picSize_(picSize) { setMouseTracking(true); }
  QSize sizeHint() const override { return QSize(26 + picSize_.width() + 10 + fontMetrics().horizontalAdvance(a_->text()) + 16, std::max(28, picSize_.height() + 10)); }
 protected:
  void enterEvent(QEnterEvent*) override { hot_ = true; update(); }
  void leaveEvent(QEvent*) override { hot_ = false; update(); }
  void mouseReleaseEvent(QMouseEvent* e) override {
    if (e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint())) return;
    if (auto* m = qobject_cast<QMenu*>(parentWidget())) m->close();
    a_->trigger();
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    if (hot_) p.fillRect(rect().adjusted(2, 1, -2, -1), pal.color(QPalette::Highlight));
    const QColor ink = hot_ ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::Text);
    if (a_->isChecked()) {   // the tick column of the other entries
      p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const double cy = height() / 2.0;
      p.drawPolyline(QPolygonF{QPointF(8, cy), QPointF(11.5, cy + 3.5), QPointF(17, cy - 4)});
    }
    p.drawPixmap(QRect(QPoint(26, (height() - picSize_.height()) / 2), picSize_), pic_.pixmap(picSize_, devicePixelRatioF()));
    p.setPen(ink);
    p.drawText(QRect(26 + picSize_.width() + 10, 0, width() - 26 - picSize_.width() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft, a_->text());
  }
 private:
  QAction* a_;
  QIcon pic_;
  QSize picSize_;
  bool hot_ = false;
};

/** Adds a checkable entry that paints `pic` before its text. The returned action is the one to connect to (a widget action: the menu hands the row its clicks). */
QAction* addPictureEntry(QMenu* m, QActionGroup* group, const QIcon& pic, QSize picSize, const QString& text, bool checked) {
  auto* wa = new QWidgetAction(m);
  wa->setText(text);
  wa->setCheckable(true);
  wa->setActionGroup(group);
  wa->setChecked(checked);
  wa->setDefaultWidget(new PictureRow(wa, pic, picSize, m));
  m->addAction(wa);
  return wa;
}

QString sizeName(SizeKind kind, size_t index, size_t count) {
  static const char* const pen[] = {"Muy fino", "Fino", "Medio", "Grueso", "Muy grueso", "Máximo"};
  static const char* const hl[] = {"Fino", "Medio", "Grueso", "Muy grueso"};
  static const char* const er[] = {"Muy pequeña", "Pequeña", "Media", "Grande", "Muy grande"};
  if (kind == SizeKind::Pen && count == 6) return QString::fromUtf8(pen[index]);
  if (kind == SizeKind::Highlighter && count == 4) return QString::fromUtf8(hl[index]);
  if (kind == SizeKind::Eraser && count == 5) return QString::fromUtf8(er[index]);
  return QString();
}

/** The size presets of a tool as checkable entries (the current size is checked); choosing one changes the drawing Style, as the preset buttons of the style row do. */
void addSizes(QMenu* m, MainWindow& win, SizeKind kind) {
  Editor& ed = win.editor();
  m->addSection(kind == SizeKind::Eraser ? QStringLiteral("Tamaño de la goma") : QStringLiteral("Grosor"));
  const Style st = ed.style();
  const double cur = kind == SizeKind::Pen ? st.penWidth : kind == SizeKind::Highlighter ? st.highlighterWidth : st.eraserRadius;
  auto* group = new QActionGroup(m);
  const auto& list = win.sizePresets().of(kind);
  for (size_t i = 0; i < list.size(); i++) {
    const double v = list[i];
    const QString name = sizeName(kind, i, list.size());
    QAction* a = addPictureEntry(m, group, chrome::sizeDotIcon(dotSize(kind, v)), QSize(20, 20), name.isEmpty() ? QString::number(v) : name + "  (" + QString::number(v) + ")", std::abs(v - cur) < 1e-6);
    QObject::connect(a, &QAction::triggered, m, [&ed, kind, v] {
      Style s = ed.style();
      if (kind == SizeKind::Pen) s.penWidth = v; else if (kind == SizeKind::Highlighter) s.highlighterWidth = v; else s.eraserRadius = v;
      ed.setStyle(s);
    });
  }
}

void addTips(QMenu* m, Editor& ed) {
  const BrushKind cur = ed.tablet().brush;
  QActionGroup* group = nullptr;
  for (const BrushInfo& b : brushes()) {
    QAction* cmd = ed.action("brush." + b.id);
    if (!cmd || !cmd->isEnabled()) continue;
    if (!group) { m->addSection(QStringLiteral("Punta")); group = new QActionGroup(m); }
    QAction* a = addPictureEntry(m, group, cmd->icon(), QSize(26, 26), b.name, b.kind == cur);
    a->setToolTip(b.hint);
    a->setObjectName("tip." + b.id);
    QObject::connect(a, &QAction::triggered, m, [cmd] { cmd->trigger(); });
  }
}

/** Xournal++'s "fill" option: the area a stroke outlines is filled at half opacity (the `fill` attribute of the stroke). */
void addFill(QMenu* m, Editor& ed) {
  m->addSection(QStringLiteral("Relleno"));
  auto* group = new QActionGroup(m);
  struct Opt { int alpha; const char* label; };
  for (const Opt& o : {Opt{-1, "Sin relleno"}, Opt{64, "Relleno suave (25 %)"}, Opt{128, "Relleno medio (50 %)"}, Opt{192, "Relleno fuerte (75 %)"}}) {
    QAction* a = m->addAction(QString::fromUtf8(o.label));
    a->setCheckable(true);
    a->setActionGroup(group);
    a->setChecked(ed.style().fillAlpha == o.alpha);
    const int alpha = o.alpha;
    QObject::connect(a, &QAction::triggered, m, [&ed, alpha] { Style s = ed.style(); s.fillAlpha = alpha; ed.setStyle(s); });
  }
}

void addLineStyles(QMenu* m, Editor& ed) {
  m->addSection(QStringLiteral("Estilo de línea"));
  auto* group = new QActionGroup(m);
  struct Opt { const char* id; const char* label; };
  for (const Opt& o : {Opt{"plain", "Estándar"}, Opt{"dash", "Discontinua"}, Opt{"dashdot", "Discontinua y punteada"}, Opt{"dot", "Punteada"}}) {
    QAction* a = addPictureEntry(m, group, lineStyleIcon(QString::fromLatin1(o.id), svgThemeNeon() ? QColor(0xe9, 0xde, 0xfc) : QColor(0xe0, 0xe0, 0xe0)), QSize(56, 12), QString::fromUtf8(o.label),
                                 ed.style().lineStyle == QLatin1String(o.id));
    a->setObjectName(QString::fromLatin1("lineStyle.") + o.id);
    const QString id = QString::fromLatin1(o.id);
    QObject::connect(a, &QAction::triggered, m, [&ed, id] { Style s = ed.style(); s.lineStyle = id; ed.setStyle(s); });
  }
}

}  // namespace

QToolButton* makeLayerSelector(Chrome& c) {
  Editor& ed = c.ed;
  auto* b = new QToolButton(&c.win);
  b->setObjectName("layerSelector");
  b->setToolTip(QStringLiteral("Capa activa de esta página: elige otra, crea, oculta, renombra o elimina capas"));
  b->setPopupMode(QToolButton::InstantPopup);
  b->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto* menu = new QMenu(b);
  b->setMenu(menu);
  auto refresh = [&ed, b] {
    const int page = ed.currentPage();
    if (page < 0 || page >= ed.pageCount()) { b->setText(QStringLiteral("Capa")); return; }
    const Page& pg = ed.doc().pages[size_t(page)];
    const int k = std::clamp(ed.activeLayer(page), 0, std::max(0, int(pg.layers.size()) - 1));
    b->setText(pg.layers.empty() ? QStringLiteral("Capa") : layerName(pg.layers[size_t(k)], k));   // just the name: the first row is tight at 1400 px (the tooltip says what it is)
  };
  QObject::connect(menu, &QMenu::aboutToShow, menu, [&ed, menu] {
    menu->clear();
    const int page = ed.currentPage();
    if (page < 0 || page >= ed.pageCount()) return;
    const Page& pg = ed.doc().pages[size_t(page)];
    const int active = ed.activeLayer(page);
    auto* group = new QActionGroup(menu);
    for (int k = int(pg.layers.size()) - 1; k >= 0; k--) {   // the top layer first, like the layers panel
      const bool hidden = !ed.layerVisible(page, k);
      QAction* a = menu->addAction(layerName(pg.layers[size_t(k)], k) + (hidden ? QStringLiteral("  (oculta)") : QString()));
      a->setCheckable(true);
      a->setActionGroup(group);
      a->setChecked(k == active);
      QObject::connect(a, &QAction::triggered, menu, [&ed, page, k] { ed.setActiveLayer(page, k); });
    }
    menu->addSeparator();
    QAction* vis = menu->addAction(ed.layerVisible(page, active) ? QStringLiteral("Ocultar la capa activa") : QStringLiteral("Mostrar la capa activa"));
    QObject::connect(vis, &QAction::triggered, menu, [&ed, page, active] { ed.setLayerVisible(page, active, !ed.layerVisible(page, active)); });
    QObject::connect(menu->addAction(QStringLiteral("Nueva capa")), &QAction::triggered, menu, [&ed, page] { layerops::add(ed, page); });
    QObject::connect(menu->addAction(QStringLiteral("Renombrar la capa activa…")), &QAction::triggered, menu, [&ed, page, active, menu] {
      if (page >= ed.pageCount() || active >= int(ed.doc().pages[size_t(page)].layers.size())) return;
      bool ok = false;
      const QString name = QInputDialog::getText(menu->parentWidget(), QStringLiteral("Renombrar capa"), QStringLiteral("Nombre:"), QLineEdit::Normal,
                                                 layerName(ed.doc().pages[size_t(page)].layers[size_t(active)], active), &ok);
      if (ok && !name.trimmed().isEmpty()) layerops::rename(ed, page, active, name.trimmed());
    });
    QObject::connect(menu->addAction(QStringLiteral("Eliminar la capa activa")), &QAction::triggered, menu, [&ed, page, active] { layerops::remove(ed, page, active); });
  });
  QObject::connect(&ed, &Editor::currentPageChanged, b, refresh);
  QObject::connect(&ed, &Editor::documentReplaced, b, refresh);
  QObject::connect(&ed, &Editor::structureChanged, b, refresh);
  QObject::connect(&ed, &Editor::historyChanged, b, refresh);
  QObject::connect(&ed, &Editor::layersChanged, b, refresh);
  refresh();
  return b;
}

void installXppExtras(Chrome& c) {
  MainWindow& win = c.win;
  Editor& ed = c.ed;
  // icons of the view commands (the sidebar buttons and the first row use them; the neon theme tints them like every other action)
  ed.action("view.twoPages")->setIcon(glyph("twopage"));
  ed.action("view.presentation")->setIcon(glyph("present"));
  ed.action("view.zoom100")->setIcon(glyph("zoom100"));
  ed.action("view.fullscreen")->setIcon(glyph("fullscreen"));
  ed.action("view.fullscreen")->setToolTip(QStringLiteral("Pantalla completa (F11)"));

  // Xournal++'s "Capa Capa 1 ▾" and fullscreen button, at the end of the tools row (the first row is the one that runs out of room at 1400 px)
  auto* sep = new QAction(&win);
  sep->setSeparator(true);
  auto* layerAction = new QWidgetAction(&win);
  layerAction->setDefaultWidget(makeLayerSelector(c));
  win.toolbarExtras << sep << layerAction << ed.action("view.fullscreen");

  // dropdowns of the tool buttons
  win.setToolMenu("pen", [&win](QMenu* m) {
    Editor& e = win.editor();
    addTips(m, e);
    addSizes(m, win, SizeKind::Pen);
    addLineStyles(m, e);
    addFill(m, e);
    m->addSeparator();
    QAction* settings = m->addAction(QStringLiteral("Lápiz y tableta…"));
    QObject::connect(settings, &QAction::triggered, m, [&e] { if (QAction* a = e.action("pen.settings"); a && a->isEnabled()) a->trigger(); });
  });
  win.setToolMenu("highlighter", [&win](QMenu* m) {
    Editor& e = win.editor();
    addSizes(m, win, SizeKind::Highlighter);
    m->addSection(QStringLiteral("Opacidad"));
    auto* group = new QActionGroup(m);
    for (const auto& [label, hex] : {std::pair<QString, QString>{"Suave (25 %)", "40"}, {"Media (50 %)", "7f"}, {"Fuerte (75 %)", "bf"}}) {
      QAction* a = m->addAction(label);
      a->setCheckable(true);
      a->setActionGroup(group);
      a->setChecked(e.style().highlighterAlpha.compare(hex, Qt::CaseInsensitive) == 0);
      QObject::connect(a, &QAction::triggered, m, [&e, hex = hex] { Style s = e.style(); s.highlighterAlpha = hex; e.setStyle(s); });
    }
    addFill(m, e);
  });
  for (const char* id : {"eraser", "eraserPart"})
    win.setToolMenu(QString::fromLatin1(id), [&win](QMenu* m) {
      Editor& e = win.editor();
      addSizes(m, win, SizeKind::Eraser);
      m->addSection(QStringLiteral("Tipo de goma"));
      auto* group = new QActionGroup(m);
      for (const auto& [label, tool] : {std::pair<QString, QString>{"Borrar el trazo entero", "eraser"}, {"Borrar solo la zona que tocas", "eraserPart"}}) {
        QAction* a = m->addAction(label);
        a->setCheckable(true);
        a->setActionGroup(group);
        a->setChecked(e.tool() == tool);
        QObject::connect(a, &QAction::triggered, m, [&e, tool = tool] { e.setTool(tool); });
      }
    });
}

}  // namespace chrome
