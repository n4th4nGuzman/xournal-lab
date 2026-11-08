// The tool box as data: the sanitize cases of the Electron e2e (tbxSanitize*), the catalogue, and the drag-and-drop edits.
#include "toolboxmodel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)
static QJsonValue parse(const QByteArray& j) { const auto d = QJsonDocument::fromJson(j); return d.isObject() ? QJsonValue(d.object()) : QJsonValue(d.array()); }
using Rows = std::vector<QStringList>;

int main() {
  // tbxSanitize
  const ToolboxSettings bad = sanitizeToolbox(parse(R"({"enabled":"x","rows":[["tool.pen","nope","tool.pen",5,"edit.undo","|","|"],"x",["color:#FF0000","color:red","bar:save","tool.pen"]],"closeOnPick":false})"));
  CHECK(bad.enabled && !bad.closeOnPick);
  CHECK(bad.rows == (Rows{{"tool.pen", "edit.undo", "|", "|"}, {}, {"color:#FF0000", "bar:save"}}));
  // tbxSanitizeOldFormat
  CHECK(sanitizeToolbox(parse(R"({"items":["tool.pen","edit.undo","tool.pen"]})")).rows == (Rows{{"tool.pen", "edit.undo"}}));
  // tbxSanitizeDefaults
  CHECK(sanitizeToolbox(QJsonValue()).rows.size() == 3 && !sanitizeToolbox(parse(R"({"enabled":false})")).enabled);
  CHECK(sanitizeToolbox(parse(R"({"rows":[]})")).rows.size() == 1);
  {
    QJsonArray many;
    for (int i = 0; i < 20; i++) many.append(QJsonArray{"tool.pen"});
    CHECK(sanitizeToolbox(QJsonObject{{"rows", many}}).rows.size() == 6);
  }
  CHECK(sanitizeToolbox(QJsonValue()) == defaultToolbox() && sanitizeToolbox(QJsonValue("x")) == defaultToolbox());
  CHECK(sanitizeToolbox(parse(R"({"rows":"x","items":"y"})")) == defaultToolbox());
  {   // at most 20 per zone; separators may repeat and never count as duplicates
    QJsonArray row;
    for (int i = 0; i < 30; i++) row.append("|");
    CHECK(sanitizeToolbox(QJsonObject{{"rows", QJsonArray{row}}}).rows[0].size() == 20);
  }
  CHECK(sanitizeToolbox(parse(R"({"rows":[["tool.pen"],["tool.pen","tool.text"]]})")).rows == (Rows{{"tool.pen"}, {"tool.text"}}));   // repeats across zones too
  // the layout the user's prefs.json holds (from before the zones)
  CHECK(sanitizeToolbox(parse(R"({"enabled":true,"items":["tool.pen","tool.highlighter","tool.eraser","tool.select","tool.lasso","tool.text","tool.vspace","tool.latex","tool.shape","tool.fill","tool.hand","edit.undo","edit.redo","tool.plane"],"closeOnPick":true})")).rows[0].size() == 14);
  // round trip
  const ToolboxSettings rt = sanitizeToolbox(toolboxToJson(bad));
  CHECK(rt == bad);
  CHECK(sanitizeToolbox(toolboxToJson(defaultToolbox())) == defaultToolbox());

  // the catalogue
  const auto& cat = toolboxCatalog();
  QSet<QString> ids;
  for (const auto& i : cat) ids.insert(i.id);
  CHECK(ids.size() == int(cat.size()) && cat.size() == 56);
  CHECK(isToolboxItemId("brush.pen") && isToolboxItemId("brush.ink") && isToolboxItemId("brush.fountain") && isToolboxItemId("brush.brush") && !isToolboxItemId("brush.nope"));
  CHECK(isToolboxItemId("tool.pen") && isToolboxItemId("bar:pgNext") && isToolboxItemId("|") && isToolboxItemId("color:#AbCdEf") && isToolboxItemId("view.zoomIn"));
  CHECK(!isToolboxItemId("color:#abc") && !isToolboxItemId("tool.nope") && !isToolboxItemId(""));
  CHECK(toolboxGroups().size() == 7 && toolboxGroups()[1] == QString::fromUtf8("Edición"));
  for (const auto& i : cat) CHECK(toolboxGroups().contains(i.group) && !i.label.isEmpty());
  CHECK(toolboxItemById("tool.pen", {})->label == QString::fromUtf8("Lápiz") && toolboxItemById("tool.pen", {})->icon == "pen");
  CHECK(!toolboxItemById("tool.nope", {}));
  CHECK(toolboxItemById("color:#ff0000", {})->group == "Colores");   // a colour outside the palette still gets a swatch
  const auto withColors = catalogWith({"#FF0000", "#00ff00"});
  CHECK(withColors.size() == cat.size() + 2 && withColors[cat.size()].id == "color:#ff0000" && withColors[cat.size()].label == "Color #ff0000");
  for (const auto& rowId : defaultToolbox().rows) for (const QString& id : rowId) CHECK(isToolboxItemId(id));

  // edits: drag from the catalogue, within a zone, between zones, out of the panel
  ToolboxSettings s = defaultToolbox();
  CHECK(toolboxDrop(s, {"bar:save"}, 2));                                           // added last
  CHECK(s.rows[2] == QStringList({"edit.undo", "edit.redo", "bar:save"}));
  CHECK(toolboxDrop(s, {"bar:save"}, 2, 0));                                        // a repeated button moves instead of duplicating
  CHECK(s.rows[2] == QStringList({"bar:save", "edit.undo", "edit.redo"}));
  CHECK(toolboxDrop(s, {"edit.redo", 2, 2}, 2, 0));                                 // within a zone, to the front
  CHECK(s.rows[2] == QStringList({"edit.redo", "bar:save", "edit.undo"}));
  CHECK(toolboxDrop(s, {"edit.redo", 2, 0}, 2, 3));                                 // to the end: the index shifts after taking it out
  CHECK(s.rows[2] == QStringList({"bar:save", "edit.undo", "edit.redo"}));
  CHECK(toolboxDrop(s, {"tool.pen", 0, 0}, 1, 1));                                  // between zones
  CHECK(s.rows[0].first() != "tool.pen" && s.rows[1][1] == "tool.pen" && s.rows[1].size() == 5);
  const int sepsBefore = int(s.rows[0].count("|"));
  CHECK(toolboxDrop(s, {"|"}, 0, 0) && s.rows[0].count("|") == sepsBefore + 1);     // a separator is always new
  CHECK(toolboxDrop(s, {"|", 0, 0}, 1, 0) && s.rows[0].count("|") == sepsBefore && s.rows[1].first() == "|");   // ...unless dragged from the panel
  CHECK(!toolboxDrop(s, {"tool.nope"}, 0) && !toolboxDrop(s, {"tool.hand"}, 9));
  CHECK(toolboxRemoveAt(s, 2, 0) && s.rows[2] == QStringList({"edit.undo", "edit.redo"}) && !toolboxRemoveAt(s, 2, 5) && !toolboxRemoveAt(s, 7, 0));
  {   // a full zone refuses another item and nothing changes
    ToolboxSettings full;
    full.rows = {QStringList()};
    for (const auto& i : cat) if (i.id != "|" && full.rows[0].size() < 20) full.rows[0] << i.id;
    const ToolboxSettings snap = full;
    CHECK(!toolboxDrop(full, {"color:#112233"}, 0) && full == snap);
  }
  CHECK(toolboxAddRow(s) && s.rows.size() == 4);
  CHECK(toolboxAddRow(s) && toolboxAddRow(s) && !toolboxAddRow(s) && s.rows.size() == 6);
  CHECK(toolboxRemoveRow(s, 5) && s.rows.size() == 5 && !toolboxRemoveRow(s, 5));
  ToolboxSettings one; one.rows = {QStringList{"tool.pen"}};
  CHECK(toolboxRemoveRow(one, 0) && one.rows.size() == 1 && one.rows[0].isEmpty());   // the last zone stays

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("toolboxmodel: all passed\n");
  return 0;
}
