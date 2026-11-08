#include "hub.h"
#include "generated.h"
#include "prefs.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace xlobjects {

Hub& Hub::of(Editor& ed) {
  if (Hub* h = ed.findChild<Hub*>(QString(), Qt::FindDirectChildrenOnly)) return *h;
  return *new Hub(ed);
}

Hub::Hub(Editor& ed) : QObject(&ed), ed_(ed) {
  space3 = DEFAULT_SPACE3;
  space3.o = {0, 0};
}

PlaneStyle Hub::penStyle() const {
  const Style& s = ed_.style();
  PlaneStyle st;
  st.color = s.color;
  st.width = s.penWidth;
  if (s.glowOn) st.glow = Glow{s.glowSize, s.glowIntensity};
  return st;
}

// ---- the selected object --------------------------------------------------------------------------------------------------------------------
bool Hub::selectedObject(int* page, int* layer, QString* id) const {
  const auto items = ed_.selection().items();
  if (items.empty()) return false;
  const Document& doc = ed_.doc();
  const ElementRef& first = items.front();
  if (first.page < 0 || first.page >= int(doc.pages.size())) return false;
  const Page& pg = doc.pages[size_t(first.page)];
  if (first.layer < 0 || first.layer >= int(pg.layers.size())) return false;
  const auto& els = pg.layers[size_t(first.layer)].elements;
  auto idAt = [&](const ElementRef& r) -> std::optional<QString> {
    if (r.page != first.page || r.layer != first.layer || r.index < 0 || r.index >= int(els.size())) return std::nullopt;
    return planeIdOf(els[size_t(r.index)]);
  };
  const auto want = idAt(first);
  if (!want) return false;
  for (const ElementRef& r : items)
    if (idAt(r) != want) return false;
  *page = first.page; *layer = first.layer; *id = *want;
  return true;
}

std::optional<SelectedPlane> Hub::selectedPlane() const {
  int page, layer; QString id;
  if (!selectedObject(&page, &layer, &id)) return std::nullopt;
  const Members m = planeMembers(ed_.doc().pages[size_t(page)].layers[size_t(layer)], id);
  const auto spec = planeSpecOf(m);
  const auto frame = planeFrameOf(m);
  const auto style = planeStyleOf(m);
  if (!spec || !frame || !style) return std::nullopt;
  // a plane that was turned (not axis aligned any more) has no frame to rebuild from: the handler does not claim it
  if (const GeneratedHandler* h = GeneratedRegistry::instance().ofKind("plane"))
    if (!h->claims(m)) return std::nullopt;
  return SelectedPlane{page, layer, id, *spec, *frame, *style};
}

std::optional<SelectedSpace3> Hub::selectedSpace3() const {
  int page, layer; QString id;
  if (!selectedObject(&page, &layer, &id)) return std::nullopt;
  const Members m = planeMembers(ed_.doc().pages[size_t(page)].layers[size_t(layer)], id);
  if (!isSpace3(m)) return std::nullopt;
  const auto spec = space3SpecOf(m);
  const auto style = space3StyleOf(m);
  if (!spec || !style) return std::nullopt;
  return SelectedSpace3{page, layer, id, *spec, *style};
}

std::optional<SelectedFigure> Hub::selectedFigure() const {
  int page, layer; QString id;
  if (!selectedObject(&page, &layer, &id)) return std::nullopt;
  const Members m = planeMembers(ed_.doc().pages[size_t(page)].layers[size_t(layer)], id);
  if (!isFigure(m)) return std::nullopt;
  const auto read = figureSpecOf(m);
  if (!read) return std::nullopt;
  return SelectedFigure{page, layer, id, read->spec, read->style};
}

// ---- edits ----------------------------------------------------------------------------------------------------------------------------------
bool Hub::swapSelected(int page, int layer, const QString& id, std::vector<Element> fresh, const QString& label) {
  Editor::Swap sw = ed_.swapGenerated(page, layer, id, std::move(fresh));
  Action a;
  a.label = label; a.page = page; a.undo = sw.undo; a.redo = sw.redo;
  ed_.pushAction(std::move(a));
  ed_.selection().select(page, ed_.membersOf(page, layer, id));   // the rebuilt pieces replace the old ones: the selection follows
  ed_.notifySelectionChanged();
  return true;
}

bool Hub::updatePlane(const PlaneSpec& spec, bool restyle) {
  const auto p = selectedPlane();
  if (!p) return false;
  const PlaneSpec clean = sanitizePlane(spec);
  const Frame frame = clean.square ? squareFrame(clean, p->frame) : p->frame;
  return swapSelected(p->page, p->layer, p->id, buildPlane(p->id, clean, frame, restyle ? penStyle() : p->style), QStringLiteral("Cambiar plano"));
}

bool Hub::updateSpace3(const Space3Spec& spec, bool restyle) {
  const auto q = selectedSpace3();
  if (!q) return false;
  return swapSelected(q->page, q->layer, q->id, buildSpace3(q->id, sanitizeSpace3(spec), restyle ? penStyle() : q->style), QStringLiteral("Cambiar espacio 3D"));
}

bool Hub::updateFigure(const FigureSpec& spec, bool restyle) {
  const auto f = selectedFigure();
  if (!f) return false;
  return swapSelected(f->page, f->layer, f->id, buildFigure(f->id, sanitizeFigure(spec), restyle ? penStyle() : f->style), QStringLiteral("Cambiar figura"));
}

bool Hub::create(int page, const QString& tool, std::vector<Element> fresh, const QString& label, bool reselect) {
  if (page < 0 || page >= ed_.pageCount() || fresh.empty()) return false;
  const int layer = ed_.activeLayer(page);
  if (!ed_.layerVisible(page, layer)) {
    const QString what = tool == "plane" ? "el plano" : tool == "space3" ? "el espacio" : "la figura";
    ed_.notify("La capa activa está oculta: muéstrala para dibujar " + what);
    return false;
  }
  const QString id = planeIdOf(fresh.front()).value_or(QString());
  Editor::Swap sw = ed_.swapGenerated(page, layer, QStringLiteral("none"), std::move(fresh));
  Action a;
  a.label = label; a.page = page; a.undo = sw.undo; a.redo = sw.redo;
  ed_.pushAction(std::move(a));
  if (!reselect) return true;   // the user is already changing the tool
  ed_.afterCreate(tool);
  ed_.selection().select(page, ed_.membersOf(page, layer, id));
  ed_.notifySelectionChanged();
  return true;
}

// ---- figures made point by point ------------------------------------------------------------------------------------------------------------
std::optional<FigureSpec> Hub::figureOfClicks(V2 pointer) const {
  std::vector<double> pts = figPending;
  pts.push_back(pointer[0]);
  pts.push_back(pointer[1]);
  if (pts.size() < 4) return std::nullopt;
  FigureSpec s;
  s.kind = figKind == FigKind::Polygon && pts.size() < 6 ? FigKind::Polyline : figKind;   // two points of a polygon are a line
  s.pts = std::move(pts);
  s.heads = figOpts.heads; s.dash = figOpts.dash; s.n = figOpts.n; s.m = figOpts.m; s.hidden = figOpts.hidden; s.label = figOpts.label;
  return sanitizeFigure(s);
}

FigureSpec Hub::figureOfDrag(V2 a, V2 b, bool shift, double zoom) const {
  const bool tiny = std::abs(b[0] - a[0]) * zoom < 12 && std::abs(b[1] - a[1]) * zoom < 12;
  const bool solid = isSolid(figKind);
  const V2 end = tiny ? V2{a[0] + (solid ? 0 : 100), a[1] - (solid ? 90 : 0)} : b;   // a click makes a figure of the default size
  FigureSpec s = defaultSpec(figKind, a, end, shift);
  s.heads = figOpts.heads; s.dash = figOpts.dash; s.n = figOpts.n; s.m = figOpts.m; s.hidden = figOpts.hidden; s.label = figOpts.label;
  return sanitizeFigure(s);   // an arrow with no heads chosen gets one at the end
}

bool Hub::endFigure(bool make, bool reselect) {
  const std::vector<double> pts = std::move(figPending);
  const int page = figPage;
  figPending.clear();
  figPage = -1;
  emit settingsChanged();
  if (pts.empty()) return false;
  if (make) {
    const bool polygon = figKind == FigKind::Polygon;
    if (pts.size() >= (polygon ? 6u : 4u)) {
      FigureSpec s;
      s.kind = figKind; s.pts = pts;
      s.heads = figOpts.heads; s.dash = figOpts.dash; s.n = figOpts.n; s.m = figOpts.m; s.hidden = figOpts.hidden; s.label = figOpts.label;
      create(page, QStringLiteral("figure"), buildFigure(newPlaneId(), sanitizeFigure(s), penStyle()), QStringLiteral("Figura"), reselect);
    } else {
      ed_.notify(QStringLiteral("Faltan puntos: haz clic en al menos ") + (polygon ? "3" : "2") + " sitios");
    }
  }
  return true;
}

bool Hub::undoFigurePoint() {
  if (figPending.empty()) return false;
  figPending.resize(figPending.size() >= 2 ? figPending.size() - 2 : 0);
  if (figPending.empty()) figPage = -1;
  emit settingsChanged();
  return true;
}

// ---- persistence ----------------------------------------------------------------------------------------------------------------------------
void Hub::setPrefsPath(const QString& path) {
  prefsPath_ = path;
  if (path.isEmpty()) return;
  Prefs p;
  if (!loadPrefs(path, &p)) return;
  if (const auto s = decodeSpec(p.plane)) plane = *s;
  if (const auto s = decodeSpace3(p.space3)) { space3 = *s; space3.o = {0, 0}; }
  emit settingsChanged();
}

// read the file again right before writing: the settings area or the Electron app may have changed other keys since; unknown keys are kept by savePrefs
static void persistKey(const QString& path, const std::function<void(Prefs&)>& set) {
  if (path.isEmpty()) return;
  Prefs p;
  QString err;
  if (!loadPrefs(path, &p, &err) && !err.isEmpty()) return;   // an unreadable file is left alone, never replaced by defaults
  set(p);
  savePrefs(path, p, &err);
}

void Hub::persistPlane() {
  const QString text = encodeSpec(plane);
  persistKey(prefsPath_, [&](Prefs& p) { p.plane = text; });
}

void Hub::persistSpace3() {
  Space3Spec s = space3;
  s.o = {0, 0};
  const QString text = encodeSpace3(s);
  persistKey(prefsPath_, [&](Prefs& p) { p.space3 = text; });
}

}  // namespace xlobjects
