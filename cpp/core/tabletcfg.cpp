#include "tabletcfg.h"
#include <QJsonObject>
#include <QKeySequence>
#include <algorithm>
#include <cmath>

const std::vector<PenActionInfo>& penActions() {
  static const std::vector<PenActionInfo> a = {
    {PenAction::None, "none", "Nada"},
    {PenAction::Pen, "pen", QString::fromUtf8("Lápiz")},
    {PenAction::Highlighter, "highlighter", "Resaltador"},
    {PenAction::Eraser, "eraser", "Goma (trazo entero)"},
    {PenAction::EraserPart, "eraserPart", "Goma (parcial)"},
    {PenAction::Select, "select", "Seleccionar"},
    {PenAction::Lasso, "lasso", "Lazo"},
    {PenAction::Fill, "fill", "Relleno"},
    {PenAction::Hand, "hand", "Mano (mover el lienzo mientras lo mantienes)"},
    {PenAction::Toolbox, "toolbox", "Cuadro de herramientas (al pulsar)"},
    {PenAction::Undo, "undo", "Deshacer (al pulsar)"},
    {PenAction::Redo, "redo", "Rehacer (al pulsar)"},
  };
  return a;
}
QString penActionId(PenAction a) { return penActions()[size_t(a)].id; }
bool penActionFromId(const QString& id, PenAction* out) {
  for (const PenActionInfo& p : penActions())
    if (p.id == id) { if (out) *out = p.action; return true; }
  return false;
}

const std::vector<AppAction>& appActions() {
  static const QString tools = QString::fromUtf8("Herramientas"), edit = QString::fromUtf8("Edición"), view = QString::fromUtf8("Vista y páginas"), pen = QString::fromUtf8("Lápiz");
  static const std::vector<AppAction> a = {
    {"tool.pen", QString::fromUtf8("Lápiz"), tools},
    {"tool.highlighter", "Resaltador", tools},
    {"tool.eraser", "Goma (trazo entero)", tools},
    {"tool.eraserPart", "Goma (parcial)", tools},
    {"tool.select", "Seleccionar", tools},
    {"tool.lasso", "Lazo", tools},
    {"tool.hand", "Mano", tools},
    {"tool.laser", QString::fromUtf8("Puntero láser"), tools},
    {"tool.fill", "Relleno", tools},
    {"tool.shape", "Formas", tools},
    {"tool.figure", "Figuras", tools},
    {"tool.numline", "Recta real", tools},
    {"tool.venn", "Diagrama de conjuntos", tools},
    {"tool.plane", "Plano cartesiano", tools},
    {"tool.space3", "Espacio 3D", tools},
    {"tool.text", "Texto", tools},
    {"tool.latex", "LaTeX", tools},
    {"tool.vspace", "Espacio vertical", tools},
    {"tool.previous", "Volver a la herramienta anterior", tools},
    {"toolbox.show", "Mostrar u ocultar el cuadro de herramientas", tools},
    {"edit.undo", "Deshacer", edit},
    {"edit.redo", "Rehacer", edit},
    {"edit.delete", QString::fromUtf8("Borrar la selección"), edit},
    {"file.save", "Guardar", edit},
    {"view.zoomIn", "Acercar", view},
    {"view.zoomOut", "Alejar", view},
    {"view.zoom100", QString::fromUtf8("Zoom 100 %"), view},
    {"view.fitWidth", "Ajustar al ancho", view},
    {"view.fullscreen", "Pantalla completa", view},
    {"view.presentation", QString::fromUtf8("Modo presentación"), view},
    {"nav.prev", QString::fromUtf8("Página anterior"), view},
    {"nav.next", QString::fromUtf8("Página siguiente"), view},
    {"journal.newPage", QString::fromUtf8("Página nueva"), view},
    {"pen.settings", QString::fromUtf8("Abrir «Lápiz y tableta»"), pen},
    {"pen.sizeUp", QString::fromUtf8("Grosor más grande"), pen},
    {"pen.sizeDown", QString::fromUtf8("Grosor más pequeño"), pen},
    {"brush.next", "Punta siguiente", pen},
    {"brush.prev", "Punta anterior", pen},
    {"color.next", "Color siguiente", pen},
    {"color.prev", "Color anterior", pen},
    {"glow.toggle", "Activar o quitar el brillo", pen},
  };
  return a;
}
bool isAppAction(const QString& id) {
  for (const AppAction& a : appActions())
    if (a.id == id) return true;
  return false;
}

static PenAction penActionOr(const QJsonValue& v, PenAction d) {
  PenAction a;
  return v.isString() && penActionFromId(v.toString(), &a) ? a : d;
}

TabletSettings sanitizeTablet(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  TabletSettings d, t;
  const QJsonValue keys = o.value("keys");
  if (keys.isObject()) {
    const QJsonObject ko = keys.toObject();
    for (auto it = ko.begin(); it != ko.end(); ++it) {
      const QString combo = it.key();
      if (it.value().isString() && isAppAction(it.value().toString()) && combo.size() > 0 && combo.size() < 40 && t.keys.size() < 64) t.keys.insert(combo, it.value().toString());
    }
  }
  BrushKind b;
  t.brush = o.value("brush").isString() && brushFromId(o.value("brush").toString(), &b) ? b : BrushKind::Pen;
  const QJsonValue sz = o.value("size");
  t.size = sz.isDouble() && std::isfinite(sz.toDouble()) ? std::min(60.0, std::max(0.1, sz.toDouble())) : d.size;
  t.curve = sanitizeCurve(o.value("curve"));
  t.penLook = o.value("penLook").toString() == "electron" ? PenLook::Electron : PenLook::Proto;
  t.barrel = penActionOr(o.value("barrel"), d.barrel);
  t.barrel2 = penActionOr(o.value("barrel2"), d.barrel2);
  t.eraser = penActionOr(o.value("eraser"), d.eraser);
  return t;
}

QJsonObject tabletToJson(const TabletSettings& t) {
  QJsonObject o, keys;
  o["brush"] = brushId(t.brush);
  o["size"] = t.size;
  o["curve"] = curveToJson(t.curve);
  if (t.penLook == PenLook::Electron) o["penLook"] = "electron";   // the default is not written: the file stays what the Electron app expects
  o["barrel"] = penActionId(t.barrel);
  o["barrel2"] = penActionId(t.barrel2);
  o["eraser"] = penActionId(t.eraser);
  for (auto it = t.keys.begin(); it != t.keys.end(); ++it) keys.insert(it.key(), it.value());
  o["keys"] = keys;
  return o;
}

// -------------------------------------------------------------------------------------------------------------------------------- combos
// Qt::Key values, spelled out so this file needs no widgets.
namespace K {
constexpr int Escape = 0x01000000, Tab = 0x01000001, Backtab = 0x01000002, Backspace = 0x01000003, Return = 0x01000004, Enter = 0x01000005, Insert = 0x01000006,
              Delete = 0x01000007, Pause = 0x01000008, Print = 0x01000009, Home = 0x01000010, End = 0x01000011, Left = 0x01000012, Up = 0x01000013, Right = 0x01000014,
              Down = 0x01000015, PageUp = 0x01000016, PageDown = 0x01000017, Shift = 0x01000020, Control = 0x01000021, Meta = 0x01000022, Alt = 0x01000023,
              CapsLock = 0x01000024, NumLock = 0x01000025, ScrollLock = 0x01000026, F1 = 0x01000030, F35 = 0x01000052, Super_L = 0x01000053, Super_R = 0x01000054,
              Menu = 0x01000055, Hyper_L = 0x01000056, Hyper_R = 0x01000057, Help = 0x01000058, Clear = 0x0100000b, AltGr = 0x01001103, Dead_First = 0x01001250,
              Dead_Last = 0x01001263, Unknown = 0x01ffffff;
}

static bool isModifierKey(int k) {
  return k == K::Shift || k == K::Control || k == K::Meta || k == K::Alt || k == K::AltGr || k == K::CapsLock || k == K::Super_L || k == K::Super_R ||
         k == K::Hyper_L || k == K::Hyper_R || k == K::Unknown || (k >= K::Dead_First && k <= K::Dead_Last) || k == 0;
}

/** The name the browser gives the key (KeyboardEvent.key), which is what the saved bindings use. */
static QString keyName(const KeyCombo& e) {
  if ((e.physical >= '0' && e.physical <= '9') || (e.physical >= 'A' && e.physical <= 'Z')) return QString(QChar(e.physical));
  const int k = e.key;
  if (k == ' ') return QStringLiteral("Espacio");
  if ((k > ' ' && k < 0x7f) || (k >= 0xa0 && k <= 0xff)) return QString(QChar(k)).toUpper();
  if (k >= K::F1 && k <= K::F35) return QStringLiteral("F") + QString::number(k - K::F1 + 1);
  switch (k) {
    case K::Escape: return "Escape";
    case K::Tab: case K::Backtab: return "Tab";
    case K::Backspace: return "Backspace";
    case K::Return: case K::Enter: return "Enter";
    case K::Insert: return "Insert";
    case K::Delete: return "Delete";
    case K::Pause: return "Pause";
    case K::Print: return "PrintScreen";
    case K::Home: return "Home";
    case K::End: return "End";
    case K::Left: return "ArrowLeft";
    case K::Up: return "ArrowUp";
    case K::Right: return "ArrowRight";
    case K::Down: return "ArrowDown";
    case K::PageUp: return "PageUp";
    case K::PageDown: return "PageDown";
    case K::NumLock: return "NumLock";
    case K::ScrollLock: return "ScrollLock";
    case K::Menu: return "ContextMenu";
    case K::Help: return "Help";
    case K::Clear: return "Clear";
    default: return QKeySequence(k).toString(QKeySequence::PortableText);   // media keys and the like
  }
}

static QString joinCombo(const Mods& m, const QString& last) {
  QStringList parts;
  if (m.ctrl) parts << "Ctrl";
  if (m.alt) parts << "Alt";
  if (m.shift) parts << "Shift";
  if (m.meta) parts << "Meta";
  parts << last;
  return parts.join('+');
}

QString comboText(const KeyCombo& k) {
  if (isModifierKey(k.key)) return QString();
  return joinCombo(k.mods, keyName(k));
}

PenButton penButtonOf(const PenPointer& e) {
  if (!e.isPen) return PenButton::None;
  if (e.eraserEnd) return PenButton::Eraser;
  if (e.rightButton) return PenButton::Barrel;
  if (e.middleButton) return PenButton::Barrel2;
  return PenButton::None;
}

QString wheelCombo(double deltaY, double deltaX, const Mods& m) {
  const double d = deltaY != 0 ? deltaY : deltaX;
  if (d == 0) return QString();
  return joinCombo(m, d > 0 ? QStringLiteral("Rueda arriba") : QStringLiteral("Rueda abajo"));
}

QString penCombo(int b, const Mods& m) {
  const QString name = b == 2 ? QString::fromUtf8("Botón lateral 1") : b == 4 ? QString::fromUtf8("Botón lateral 2") : QString();
  return name.isNull() ? mouseCombo(b, m) : joinCombo(m, name);
}

QString mouseCombo(int b, const Mods& m) {
  const QString name = b == 4 ? QString::fromUtf8("Botón central") : b == 8 ? QString::fromUtf8("Botón atrás") : b == 16 ? QString::fromUtf8("Botón adelante") : QString();
  return name.isNull() ? QString() : joinCombo(m, name);
}
