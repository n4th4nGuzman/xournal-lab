// Tablet settings and the combination strings: they must be exactly those the Electron app saved in prefs.json ("Ctrl++", "F13", ...).
#include "tabletcfg.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)
static QJsonValue parse(const char* j) { const auto d = QJsonDocument::fromJson(j); return d.isObject() ? QJsonValue(d.object()) : QJsonValue(d.array()); }

int main() {
  // settings from anything (tabSanitize, tabSanitizeNull)
  const TabletSettings bad = sanitizeTablet(parse(R"({"brush":"laser","size":-5,"curve":{"x1":9,"y1":"a"},"barrel":"bogus","eraser":3,
      "keys":{"Ctrl+1":"tool.pen","X":"nope","":"tool.pen"}})"));
  CHECK(bad.brush == BrushKind::Pen && bad.size == 0.1 && bad.curve.x1 == 1 && bad.curve.y1 == 0.25 && bad.barrel == PenAction::Toolbox && bad.eraser == PenAction::Eraser);
  CHECK(bad.keys.size() == 1 && bad.keys.value("Ctrl+1") == "tool.pen");
  CHECK(sanitizeTablet(QJsonValue()) == TabletSettings() && sanitizeTablet(QJsonValue("x")) == TabletSettings() && sanitizeTablet(QJsonValue()).keys.isEmpty());
  CHECK(sanitizeTablet(parse(R"({"barrel2":"hand"})")).barrel2 == PenAction::Hand);   // panActionListed
  CHECK(sanitizeTablet(parse(R"({"size":999})")).size == 60);
  CHECK(sanitizeTablet(parse(R"({"keys":{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa":"tool.pen"}})")).keys.isEmpty());   // 40 characters or more
  {   // at most 64 bindings
    QJsonObject keys;
    for (int i = 0; i < 100; i++) keys.insert(QString("K%1").arg(i, 3, 10, QChar('0')), "tool.pen");
    CHECK(sanitizeTablet(QJsonObject{{"keys", keys}}).keys.size() == 64);
  }
  // round trip
  TabletSettings t;
  t.brush = BrushKind::Fountain; t.size = 3.5; t.curve = curvePresets()[1].curve; t.barrel = PenAction::Lasso; t.barrel2 = PenAction::None; t.eraser = PenAction::EraserPart;
  t.keys.insert("Ctrl++", "view.zoomIn"); t.keys.insert("F13", "tool.pen");
  CHECK(sanitizeTablet(tabletToJson(t)) == t);
  // the real tablet JSON of the user's prefs.json
  const TabletSettings real = sanitizeTablet(parse(R"({"brush":"pen","size":2.26,"curve":{"x1":0.25,"y1":0.25,"x2":0.75,"y2":0.75,"min":0},"barrel":"toolbox","barrel2":"hand",
      "eraser":"eraser","keys":{"Ctrl++":"view.zoomIn","Ctrl+-":"view.zoomOut","F13":"tool.pen","F14":"tool.eraser","F15":"tool.highlighter","F16":"tool.select","F17":"edit.undo",
      "F18":"edit.redo","F19":"brush.next","F20":"color.next","F21":"view.zoomIn","F22":"view.zoomOut"}})"));
  CHECK(real.keys.size() == 12 && real.size == 2.26);

  // pen actions and the action catalogue
  CHECK(penActions().size() == 12 && penActionId(PenAction::EraserPart) == "eraserPart" && penActionId(PenAction::Redo) == "redo");
  PenAction a;
  CHECK(penActionFromId("hand", &a) && a == PenAction::Hand && !penActionFromId("laser", &a));
  for (size_t i = 0; i < penActions().size(); i++) CHECK(size_t(penActions()[i].action) == i);   // the enum order is the table order
  CHECK(appActions().size() == 41 && isAppAction("glow.toggle") && isAppAction("tool.previous") && !isAppAction("tool.nope"));
  for (const auto& id : real.keys) CHECK(isAppAction(id));

  // key combinations: Ctrl++, Ctrl+-, F13..F22 (the ones the real file holds)
  const int F1 = 0x01000030;
  Mods ctrl; ctrl.ctrl = true;
  CHECK(comboText({'+', 0, ctrl}) == "Ctrl++");
  CHECK(comboText({'-', 0, ctrl}) == "Ctrl+-");
  for (int n = 13; n <= 22; n++) CHECK(comboText({F1 + n - 1, 0, {}}) == QString("F%1").arg(n));
  CHECK(comboText({F1 + 4, 0, {true, true, false, false}}) == "Ctrl+Shift+F5");
  CHECK(comboText({'A', 'A', {true, false, true, true}}) == "Ctrl+Alt+Meta+A");   // order: Ctrl, Alt, Shift, Meta
  CHECK(comboText({'a', 0, {}}) == "A");
  CHECK(comboText({0x21, '1', {false, true, false, false}}) == "Shift+1");        // Shift+1 types "!", the physical key names it
  CHECK(comboText({' ', 0, {}}) == "Espacio");
  CHECK(comboText({0x01000013, 0, {}}) == "ArrowUp" && comboText({0x01000004, 0, {}}) == "Enter" && comboText({0x01000016, 0, ctrl}) == "Ctrl+PageUp");
  CHECK(comboText({0x01000021, 0, ctrl}).isNull() && comboText({0x01000020, 0, {}}).isNull() && comboText({0x01000024, 0, {}}).isNull());   // modifiers alone
  CHECK(comboText({0x01ffffff, 0, {}}).isNull());

  // pen buttons
  CHECK(penButtonOf({false, false, true, false}) == PenButton::None);   // not a pen
  CHECK(penButtonOf({true, true, false, false}) == PenButton::Eraser);
  CHECK(penButtonOf({true, false, true, false}) == PenButton::Barrel);
  CHECK(penButtonOf({true, false, false, true}) == PenButton::Barrel2);
  CHECK(penButtonOf({true, false, false, false}) == PenButton::None);
  CHECK(penButtonOf({true, true, true, true}) == PenButton::Eraser);    // the eraser end wins

  // wheel and mouse
  CHECK(wheelCombo(120, 0, ctrl) == "Ctrl+Rueda arriba" && wheelCombo(-120, 0, {}) == "Rueda abajo" && wheelCombo(0, 0, {}).isNull());
  CHECK(wheelCombo(0, -120, {}) == "Rueda abajo");
  CHECK(mouseCombo(4, {}) == QString::fromUtf8("Botón central") && mouseCombo(8, ctrl) == QString::fromUtf8("Ctrl+Botón atrás") && mouseCombo(16, {}) == QString::fromUtf8("Botón adelante"));
  CHECK(mouseCombo(1, {}).isNull() && mouseCombo(2, {}).isNull());

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("tablet: all passed\n");
  return 0;
}
