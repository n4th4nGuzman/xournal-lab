#include "toolboxmodel.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

const QString kToolboxSeparator = QStringLiteral("|");

const QStringList& toolboxGroups() {
  static const QStringList g = {QString::fromUtf8("Herramientas"), QString::fromUtf8("Edición"), "Archivo", QString::fromUtf8("Navegación"), QString::fromUtf8("Selección"), "Colores", "Separadores"};
  return g;
}

namespace {
const QString& gTools() { return toolboxGroups()[0]; }
const QString& gEdit() { return toolboxGroups()[1]; }
const QString& gFile() { return toolboxGroups()[2]; }
const QString& gNav() { return toolboxGroups()[3]; }
const QString& gSel() { return toolboxGroups()[4]; }

QString U(const char* s) { return QString::fromUtf8(s); }
ToolboxItem tool(const char* id, const char* icon, const char* label) {
  const QString i = QString::fromLatin1(id);
  return {"tool." + i, QString::fromLatin1(icon), U(label), gTools(), "button.tool[data-tool=" + i + "]"};
}
ToolboxItem bar(const char* id, const char* icon, const char* label, const QString& group) {
  const QString i = QString::fromLatin1(id);
  return {"bar:" + i, QString::fromLatin1(icon), U(label), group, "#" + i};
}
/** An item that does its work through a command of the application, and also stands for a toolbar button. */
ToolboxItem cmd(const char* id, const char* icon, const char* label, const QString& group, const char* button) {
  return {QString::fromLatin1(id), QString::fromLatin1(icon), U(label), group, QString::fromLatin1(button)};
}
}  // namespace

const std::vector<ToolboxItem>& toolboxCatalog() {
  static const std::vector<ToolboxItem> c = {
    tool("pen", "pen", "Lápiz"), tool("highlighter", "highlighter", "Resaltador"), tool("eraser", "eraser", "Goma (trazo entero)"), tool("eraserPart", "eraserPart", "Goma (parcial)"),
    tool("select", "select", "Seleccionar"), tool("lasso", "lasso", "Lazo"), tool("vspace", "vspace", "Espacio vertical"), tool("hand", "hand", "Mano"),
    tool("shape", "shape", "Formas"), tool("figure", "figure", "Figuras"), tool("numline", "numline", "Recta real"), tool("venn", "venn", "Diagrama de conjuntos"),
    tool("text", "text", "Texto"), tool("latex", "latex", "LaTeX"),
    tool("laser", "laser", "Puntero láser"), tool("fill", "fill", "Relleno"), tool("plane", "plane", "Plano cartesiano"), tool("space3", "space3", "Espacio 3D"),
    bar("recog", "recog", "Formas automáticas (activar o quitar)", gTools()),
    bar("image", "image", "Insertar imagen", gTools()),
    cmd("glow.toggle", "glow", "Brillo de los trazos", gTools(), "#btnGlow"),
    cmd("pen.settings", "tablet", "Lápiz y tableta", gTools(), "#btnTablet"),
    cmd("brush.pen", "pen", "Punta: Bolígrafo", gTools(), ""), cmd("brush.ink", "pen", "Punta: Tinta", gTools(), ""),
    cmd("brush.fountain", "pen", "Punta: Pluma", gTools(), ""), cmd("brush.brush", "pen", "Punta: Pincel", gTools(), ""),
    cmd("edit.undo", "undo", "Deshacer", gEdit(), "#undo"), cmd("edit.redo", "redo", "Rehacer", gEdit(), "#redo"),
    bar("cut", "cut", "Cortar", gEdit()), bar("copy", "copy", "Copiar", gEdit()), bar("paste", "paste", "Pegar", gEdit()), bar("dup", "dup", "Duplicar", gEdit()),
    bar("open", "open", "Abrir", gFile()), bar("save", "save", "Guardar", gFile()),
    bar("pgFirst", "first", "Primera página", gNav()), bar("pgPrev", "prev", "Página anterior", gNav()), bar("pgNext", "next", "Página siguiente", gNav()),
    bar("pgLast", "last", "Última página", gNav()),
    bar("histBack", "histBack", "Volver en el historial", gNav()), bar("histFwd", "histFwd", "Avanzar en el historial", gNav()),
    bar("pgNew", "pageAdd", "Nueva página después de la actual", gNav()), bar("pgBefore", "pageBefore", "Insertar página antes", gNav()),
    bar("pgAppend", "pageEnd", "Añadir página al final", gNav()),
    bar("pgDelete", "pageDel", "Eliminar la página", gNav()), bar("pgUp", "up", "Mover la página hacia arriba", gNav()), bar("pgDown", "down", "Mover la página hacia abajo", gNav()),
    cmd("view.zoomIn", "zoomIn", "Acercar", gNav(), "#zoomIn"), cmd("view.zoomOut", "zoomOut", "Alejar", gNav(), "#zoomOut"), bar("fit", "fit", "Ajustar al ancho", gNav()),
    bar("front", "front", "Traer al frente", gSel()), bar("back", "back", "Enviar al fondo", gSel()),
    bar("rotL", "rotL", "Rotar 90° a la izquierda", gSel()), bar("rotR", "rotR", "Rotar 90° a la derecha", gSel()),
    bar("flipH", "flipH", "Espejar horizontalmente", gSel()), bar("flipV", "flipV", "Espejar verticalmente", gSel()),
    {kToolboxSeparator, "", U("Separador (una línea entre botones)"), toolboxGroups()[6], ""},
  };
  return c;
}

static const QRegularExpression& colorId() {
  static const QRegularExpression re(QStringLiteral("^color:#[0-9a-f]{6}$"), QRegularExpression::CaseInsensitiveOption);
  return re;
}

bool isToolboxItemId(const QString& id) {
  static const QSet<QString> ids = [] {
    QSet<QString> s;
    for (const ToolboxItem& i : toolboxCatalog()) s.insert(i.id);
    return s;
  }();
  return ids.contains(id) || colorId().match(id).hasMatch();
}

std::vector<ToolboxItem> catalogWith(const QStringList& colors) {
  std::vector<ToolboxItem> out = toolboxCatalog();
  for (const QString& c : colors) {
    const QString lc = c.toLower();
    out.push_back({"color:" + lc, "", "Color " + lc, toolboxGroups()[5], "#swatches .sw[data-color=\"" + c + "\"]"});
  }
  return out;
}

std::optional<ToolboxItem> toolboxItemById(const QString& id, const QStringList& colors) {
  for (const ToolboxItem& i : catalogWith(colors))
    if (i.id == id) return i;
  if (colorId().match(id).hasMatch()) return ToolboxItem{id, "", "Color " + id.mid(6), toolboxGroups()[5], ""};
  return std::nullopt;
}

ToolboxSettings defaultToolbox() {
  const QString s = kToolboxSeparator;
  return {true,
          {{"tool.pen", "tool.highlighter", "tool.eraser", "tool.eraserPart", s, "tool.select", "tool.lasso", "tool.hand"},
           {"tool.text", "tool.latex", "tool.shape", "tool.fill"},
           {"edit.undo", "edit.redo"}},
          true};
}

ToolboxSettings sanitizeToolbox(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  QSet<QString> seen;
  // items repeated anywhere in the layout are dropped (a separator may repeat); at most kToolboxMaxPerRow per zone
  auto clean = [&](const QJsonValue& list) {
    QStringList out;
    if (!list.isArray()) return out;
    for (const QJsonValue& v : list.toArray()) {
      if (!v.isString() || !isToolboxItemId(v.toString())) continue;
      const QString id = v.toString();
      if (id != kToolboxSeparator) {
        if (seen.contains(id)) continue;
        seen.insert(id);
      }
      out << id;
    }
    return out.mid(0, kToolboxMaxPerRow);
  };
  ToolboxSettings s;
  bool have = false;
  if (o.value("rows").isArray()) {
    const QJsonArray a = o.value("rows").toArray();
    for (int i = 0; i < a.size() && i < kToolboxMaxRows; i++) s.rows.push_back(clean(a[i]));
    have = true;
  } else if (o.value("items").isArray()) {
    s.rows.push_back(clean(o.value("items")));
    have = true;
  }
  if (have && s.rows.empty()) s.rows.push_back({});
  if (!have) s.rows = defaultToolbox().rows;
  s.enabled = !(o.value("enabled").isBool() && !o.value("enabled").toBool());
  s.closeOnPick = !(o.value("closeOnPick").isBool() && !o.value("closeOnPick").toBool());
  return s;
}

QJsonObject toolboxToJson(const ToolboxSettings& s) {
  QJsonArray rows;
  for (const QStringList& r : s.rows) rows.append(QJsonArray::fromStringList(r));
  QJsonObject o;
  o["enabled"] = s.enabled;
  o["rows"] = rows;
  o["closeOnPick"] = s.closeOnPick;
  return o;
}

// ------------------------------------------------------------------------------------------------------------------------------- edits
bool toolboxDrop(ToolboxSettings& s, const ToolboxDrag& d, int row, int index) {
  if (row < 0 || row >= int(s.rows.size()) || !isToolboxItemId(d.id)) return false;
  auto rows = s.rows;
  int at = index < 0 ? int(rows[row].size()) : index;
  auto take = [&](int r, int i) {
    rows[r].removeAt(i);
    if (r == row && i < at) at--;
  };
  if (d.row >= 0 && d.index >= 0 && d.row < int(rows.size()) && d.index < rows[d.row].size() && rows[d.row][d.index] == d.id) take(d.row, d.index);
  else if (d.id != kToolboxSeparator)
    for (int r = 0; r < int(rows.size()); r++) {
      const int i = int(rows[r].indexOf(d.id));
      if (i >= 0) take(r, i);
    }
  if (rows[row].size() >= kToolboxMaxPerRow) return false;
  rows[row].insert(std::max(0, std::min(at, int(rows[row].size()))), d.id);
  s.rows = std::move(rows);
  return true;
}

bool toolboxRemoveAt(ToolboxSettings& s, int row, int index) {
  if (row < 0 || row >= int(s.rows.size()) || index < 0 || index >= s.rows[row].size()) return false;
  s.rows[row].removeAt(index);
  return true;
}

bool toolboxRemoveRow(ToolboxSettings& s, int row) {
  if (row < 0 || row >= int(s.rows.size())) return false;
  s.rows.erase(s.rows.begin() + row);
  if (s.rows.empty()) s.rows.push_back({});
  return true;
}

bool toolboxAddRow(ToolboxSettings& s) {
  if (int(s.rows.size()) >= kToolboxMaxRows) return false;
  s.rows.push_back({});
  return true;
}
