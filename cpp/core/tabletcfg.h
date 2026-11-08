// Settings of the pen and the graphics tablet (XP-Pen Deco Pro MW Gen 2, ACK05 remote and the like): tip, pressure curve, what the pen's buttons and
// eraser end do, and which key combination triggers which action. Port of the data part of app/src/renderer/tablet.ts. Qt events are reduced to the
// plain structs below so the combination strings stay exactly those of the Electron app ("Ctrl++", "F13", "Ctrl+Rueda arriba"...): the saved
// bindings of the user are interchangeable between both apps.
#pragma once
#include "brush.h"
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <vector>

/** What a pen button or the eraser end does while it is held (a tool) or when it is pressed (undo, redo). */
enum class PenAction { None, Pen, Highlighter, Eraser, EraserPart, Select, Lasso, Fill, Hand, Toolbox, Undo, Redo };
struct PenActionInfo { PenAction action; QString id, label; };   // id is the saved name ("eraserPart", "toolbox"...)
const std::vector<PenActionInfo>& penActions();
QString penActionId(PenAction a);
bool penActionFromId(const QString& id, PenAction* out);

struct AppAction { QString id, label, group; };
/** Actions a key combination can trigger. The application owns what each one does. */
const std::vector<AppAction>& appActions();
bool isAppAction(const QString& id);

struct TabletSettings {
  BrushKind brush = BrushKind::Pen;
  double size = 1.41;                       // reference width of the pen
  PressureCurve curve = kLinearCurve;
  PenLook penLook = PenLook::Proto;         // width of the plain pen from pressure: the prototype's (default) or the Electron app's; saved as "penLook" only when Electron
  PenAction barrel = PenAction::Toolbox;    // first barrel button (the one nearest the tip)
  PenAction barrel2 = PenAction::Hand;      // second barrel button
  PenAction eraser = PenAction::Eraser;     // the eraser end of the pen
  QMap<QString, QString> keys;              // combination text (see comboText) to action id
  bool operator==(const TabletSettings&) const = default;
};

/** Whatever was stored, as valid settings: unknown values fall back to the defaults, bad bindings are dropped. Any non-object gives the defaults. */
TabletSettings sanitizeTablet(const QJsonValue& raw);
QJsonObject tabletToJson(const TabletSettings& t);

struct Mods { bool ctrl = false, shift = false, alt = false, meta = false; };

/**
 * A key press. `key` is the Qt::Key of the event (event->key()); `physical` is the Qt::Key of the physical key when it is a digit or a letter
 * (0 if unknown), so Shift or AltGr do not change the name of "1" or "A" (the Electron app reads KeyboardEvent.code for the same reason).
 */
struct KeyCombo { int key = 0; int physical = 0; Mods mods; };

/** The combination a key press stands for, like "Ctrl+Shift+F5", or a null QString for a modifier on its own. */
QString comboText(const KeyCombo& k);

/** Pointer facts of a pen event as Qt reports them. */
struct PenPointer { bool isPen = false; bool eraserEnd = false; bool rightButton = false; bool middleButton = false; };
enum class PenButton { None, Eraser, Barrel, Barrel2 };
PenButton penButtonOf(const PenPointer& e);

/** A wheel turn; `delta` follows Qt (angleDelta: positive = away from the user = up). Null QString when delta is 0. */
QString wheelCombo(double deltaY, double deltaX, const Mods& m);
/** Extra mouse buttons a driver can send, as Qt::MouseButton values (MiddleButton=4, BackButton=8, ForwardButton=16); the main and right buttons are never bound. */
QString mouseCombo(int qtMouseButton, const Mods& m);
/** A pen's barrel button as a tablet event reports it (RightButton=2 "Botón lateral 1", MiddleButton=4 "Botón lateral 2", Back/Forward as for the mouse); null QString for anything else. */
QString penCombo(int qtMouseButton, const Mods& m);
