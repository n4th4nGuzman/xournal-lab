// State shared by the tools, the option panels and the handlers of planes, 3D spaces and figures: the settings of the NEXT object, the figure being
// clicked point by point, and "the selected object" read back from the document. It is the C++ form of the plane/space3/figure part of the TS Editor
// (planeSpec, space3Spec, figKind, figOpts, figPending, selectedPlane..., updatePlane...). One Hub per Editor, owned by it as a child QObject.
#pragma once
#include "editor.h"
#include "figures.h"
#include "plane.h"
#include "space3.h"
#include <QObject>
#include <optional>

namespace xlobjects {

/** What the figure tool makes besides the kind and the points (the TS figOpts). */
struct FigOpts {
  Heads heads = Heads::None;
  Dash dash = Dash::Solid;
  int n = 5, m = 3;
  Hidden hidden = Hidden::Dash;
  QString label;
};

// A generated object that is selected as a whole, with where it lives (page, layer) and what is needed to rebuild it.
struct SelectedPlane { int page, layer; QString id; PlaneSpec spec; Frame frame; PlaneStyle style; };
struct SelectedSpace3 { int page, layer; QString id; Space3Spec spec; PlaneStyle style; };
struct SelectedFigure { int page, layer; QString id; FigureSpec spec; PlaneStyle style; };

class Hub : public QObject {
  Q_OBJECT
 public:
  static Hub& of(Editor& ed);   // the one of this editor (created on first use)
  explicit Hub(Editor& ed);

  Editor& editor() const { return ed_; }

  // ---- settings for the next object (what the panels edit when nothing is selected) ----
  PlaneSpec plane = DEFAULT_PLANE;
  Space3Spec space3;            // o is always {0, 0}: the position comes from the gesture
  FigKind figKind = FigKind::Segment;
  FigOpts figOpts;
  /** Points clicked so far for a figure made by successive clicks (x, y, ...), and the page they were clicked on. */
  std::vector<double> figPending;
  int figPage = -1;

  /** The pen's colour, width and glow: the style every generated object is drawn with. */
  PlaneStyle penStyle() const;

  // ---- the selected object (null when the selection is not exactly one object of that kind) ----
  std::optional<SelectedPlane> selectedPlane() const;
  std::optional<SelectedSpace3> selectedSpace3() const;
  std::optional<SelectedFigure> selectedFigure() const;

  // ---- one undo step each; false when nothing is selected / the layer is hidden ----
  /** New settings for the selected object (with `restyle` it takes the pen's style). The selection follows the rebuilt pieces. */
  bool updatePlane(const PlaneSpec& spec, bool restyle = false);
  bool updateSpace3(const Space3Spec& spec, bool restyle = false);
  bool updateFigure(const FigureSpec& spec, bool restyle = false);
  /** Insert a new object on the active layer of `page` as one undo step; `tool` is the inserter ("plane", "space3", "figure") for Editor::afterCreate.
   *  With `reselect` false the tool is already being changed by the user: nothing is selected. Returns false (with a notice) when that layer is hidden. */
  bool create(int page, const QString& tool, std::vector<Element> fresh, const QString& label, bool reselect = true);
  /** Finish (make = true) or drop the figure being clicked. Returns whether there was one. */
  bool endFigure(bool make, bool reselect = true);
  bool undoFigurePoint();
  /** The figure the tool would make from the clicks so far plus the pointer position (null when there are too few points). */
  std::optional<FigureSpec> figureOfClicks(V2 pointer) const;
  /** The figure a drag from a to b describes for the current kind and options (a click makes one of the default size). */
  FigureSpec figureOfDrag(V2 a, V2 b, bool shift, double zoom) const;

  // ---- persistence: prefs.json keys "plane" and "space3" (JSON text), the same ones the Electron app keeps ----
  /** Where to read and write; an empty path (the default) turns persistence off. Reads the stored settings now. */
  void setPrefsPath(const QString& path);
  const QString& prefsPath() const { return prefsPath_; }
  void persistPlane();
  void persistSpace3();

  /** Panels and overlays refresh when the settings or the pending clicks change. */
  void changed() { emit settingsChanged(); }

 signals:
  void settingsChanged();

 private:
  bool swapSelected(int page, int layer, const QString& id, std::vector<Element> fresh, const QString& label);
  bool activeLayerUsable(int page, int* layer);
  /** Items of the selection if they are all pieces of one object: page, layer, id. */
  bool selectedObject(int* page, int* layer, QString* id) const;

  Editor& ed_;
  QString prefsPath_;
};

}  // namespace xlobjects
