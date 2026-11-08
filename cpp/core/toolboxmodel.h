// The tool box as data: the catalogue of what can be put in it, its zones and separators, the saved layout and the edits the user makes to it.
// Port of the non-widget part of app/src/renderer/toolbox.ts. The saved JSON is the same as the Electron app's ({"enabled", "items"|"rows", "closeOnPick"}).
#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

struct ToolboxSettings {
  bool enabled = true;
  std::vector<QStringList> rows;   // the zones, top to bottom; each a list of item ids in order. "|" is a separator
  bool closeOnPick = true;
  bool operator==(const ToolboxSettings&) const = default;
};

struct ToolboxItem {
  QString id;
  QString icon;      // name of the icon, or empty for a colour swatch or the separator
  QString label;
  QString group;     // one of toolboxGroups()
  QString bar;       // the toolbar button this item stands for (the Electron CSS selector; the Qt window maps ids to its own actions), may be empty
};

const QStringList& toolboxGroups();      // Herramientas, Edición, Archivo, Navegación, Selección, Colores, Separadores
extern const QString kToolboxSeparator;  // "|"
constexpr int kToolboxMaxRows = 6, kToolboxMaxPerRow = 20;

/** Everything that can be put in the tool box, except the colours (they depend on the palette). */
const std::vector<ToolboxItem>& toolboxCatalog();
/** A catalogue id: an item of the catalogue, the separator, or "color:#rrggbb" (any case). */
bool isToolboxItemId(const QString& id);
/** The catalogue with the colours of the palette in use as the "Colores" group (ids are lower case, `colors` are "#rrggbb"). */
std::vector<ToolboxItem> catalogWith(const QStringList& colors);
/** The item for an id of a saved layout, or nullopt when it is unknown (a colour outside the palette still gets a swatch item). */
std::optional<ToolboxItem> toolboxItemById(const QString& id, const QStringList& colors);

ToolboxSettings defaultToolbox();
/** Whatever was stored, as valid settings: unknown or repeated items are dropped (a separator may repeat). A list of items from before the zones is one zone. */
ToolboxSettings sanitizeToolbox(const QJsonValue& raw);
QJsonObject toolboxToJson(const ToolboxSettings& s);

// Edits (the panel's drag and drop, without the dragging).
/** Where a dragged item comes from: `row`/`index` set when it was taken from the panel itself. */
struct ToolboxDrag { QString id; int row = -1; int index = -1; };
/** Put an item in a zone before `index` (-1: last). From another place of the panel it moves, from outside it is added (a repeated button moves too). */
bool toolboxDrop(ToolboxSettings& s, const ToolboxDrag& d, int row, int index = -1);
bool toolboxRemoveAt(ToolboxSettings& s, int row, int index);
/** Remove a zone and its buttons; the last zone stays (emptied). */
bool toolboxRemoveRow(ToolboxSettings& s, int row);
bool toolboxAddRow(ToolboxSettings& s);       // false at kToolboxMaxRows
