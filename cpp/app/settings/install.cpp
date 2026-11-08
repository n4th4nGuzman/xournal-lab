#include "modules.h"
#include "palettes.h"
#include "brush.h"
#include "settings.h"
#include "tabletwidgets.h"
#include "xppapply.h"
#include <QActionGroup>
#include <QCursor>
#include <QMenu>
#include <QPainter>

// Owned by the "settings" area: the tool box, the pen/tablet dialog, the preferences dialog, size presets and palettes (see settings.h).
namespace {
QIcon paletteIcon() {
  QPixmap pm(64, 64);
  pm.fill(Qt::transparent);
  pm.setDevicePixelRatio(2);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor cols[4] = {QColor("#e0147f"), QColor("#0aa5c4"), QColor("#f5b800"), QColor("#388e3c")};
  for (int i = 0; i < 4; i++) {
    p.setPen(QPen(QColor(255, 255, 255, 150), 1));
    p.setBrush(cols[i]);
    p.drawEllipse(QRectF(3 + (i % 2) * 13, 3 + (i / 2) * 13, 11, 11));
  }
  return QIcon(pm);
}

QIcon brushIcon(BrushKind kind) {
  QPixmap pm(64, 64);
  pm.setDevicePixelRatio(2);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const PressureMap map(kLinearCurve);
  BrushEngine engine(kind, 2.4, map, true);
  std::vector<double> pts, widths;
  for (const DemoSample& s : demoStroke(32, 24)) {
    pts.push_back(s.x);
    pts.push_back(s.y);
    widths.push_back(pts.size() == 2 ? engine.first() : engine.next(s.x, s.y, s.p, s.t));
    if (pts.size() == 2) engine.next(s.x, s.y, s.p, s.t);
  }
  p.translate(0, 4);
  drawStrokeWidths(p, pts, engine.finish(pts, widths), 1.0, QColor("#eeeeee"));
  return QIcon(pm);
}
}  // namespace

void installSettings(MainWindow& win) {
  Editor& ed = win.editor();
  auto* c = new SettingsController(win);

  ed.addCommand("edit.prefs", QString::fromUtf8("Preferencias…"), QKeySequence("Ctrl+,"), [c] { c->openPrefs(); });
  ed.addCommand("tool.toolbox", "Mostrar el cuadro de herramientas", QKeySequence("Q"), [c] { c->toggleToolbox(QCursor::pos()); });
  ed.addCommand("tool.toolboxEnable", "Activar o desactivar el cuadro de herramientas", QKeySequence(), [c] { c->setToolboxEnabled(!c->toolboxSettings().enabled); });
  ed.addCommand("pen.settings", QString::fromUtf8("Lápiz y tableta…"), QKeySequence(), [c] { c->openTablet(); });
  for (const BrushInfo& b : brushes()) {
    QAction* a = ed.addCommand("brush." + b.id, QString::fromUtf8("Punta: ") + b.name, QKeySequence(), [c, &ed, kind = b.kind] {
      TabletSettings t = ed.tablet();
      t.brush = kind;
      t.size = ed.style().penWidth;
      c->applyTablet(t);
    });
    a->setIcon(brushIcon(b.kind));
    a->setToolTip(b.hint);
  }
  ed.addCommand("tool.default", QString::fromUtf8("Herramienta predeterminada"), QKeySequence(), [c] { c->xppApplier()->selectDefaultTool(); })->setToolTip(QString::fromUtf8("Elige la herramienta, el color y el grosor de Preferencias > Valores predeterminados"));
  if (QMenu* tools = win.menu("tools")) tools->addAction(ed.action("tool.default"));
  QAction* pal = ed.addCommand("palette.menu", QString::fromUtf8("Paleta de colores…"), QKeySequence(), [c] { c->popupPaletteMenu(); });
  pal->setIcon(paletteIcon());
  pal->setToolTip(QString::fromUtf8("Paleta de colores. Clic para cambiarla"));

  // Ver > Paleta de colores: the same choice from the menu bar
  if (QMenu* view = win.menu("view")) {
    QMenu* pm = view->addMenu(QString::fromUtf8("Paleta de colores"));
    auto* group = new QActionGroup(pm);
    for (const Palette& p : palettes()) {
      QAction* a = pm->addAction(p.name);
      a->setCheckable(true);
      a->setData(p.id);
      group->addAction(a);
      QObject::connect(a, &QAction::triggered, c, [c, id = p.id] { c->applyPalette(id); });
    }
    QObject::connect(pm, &QMenu::aboutToShow, c, [c, pm] { for (QAction* a : pm->actions()) a->setChecked(a->data().toString() == c->paletteId()); });
  }
}
