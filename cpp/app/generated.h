// Generated objects (planes, 3D spaces, figures, number lines, set diagrams) as the editor sees them: a set of ordinary strokes/texts sharing xlPlane=<id>.
// Each kind has a handler. The selection asks the registry what is selected (anchors to drag, how to rebuild after a resize); the tool and panel of the
// kind implement and register the handler. This is the C++ form of the TS chain selectedPlane/selectedSpace3/selectedMath/selectedFigure + rebuildSelectedPlane.
#pragma once
#include "model.h"
#include <QPointF>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

using ElementPtrs = std::vector<const Element*>;

class GeneratedHandler {
 public:
  virtual ~GeneratedHandler() = default;
  /** Short id of the kind: "plane", "space3", "figure", "line", "venn". */
  virtual QString kind() const = 0;
  /** Are these members (all pieces of one xlPlane id in a layer) an object of this kind? Decided by the witness stroke / the specific roles, never by guessing. */
  virtual bool claims(const ElementPtrs& members) const = 0;
  /** Handles to draw and drag, in page units. Empty when the kind has no anchors (a plane is resized by the selection frame). */
  virtual std::vector<QPointF> anchors(const ElementPtrs& members) const = 0;
  /** Drag anchor `i` to `p` (shift = constrain/snap): the rebuilt members, or nullopt when nothing changes. The caller swaps them in with Editor::swapGenerated. */
  virtual std::optional<std::vector<Element>> dragAnchor(const ElementPtrs& members, const QString& id, int i, QPointF p, bool shift) const = 0;
  /** The pieces were moved/turned/scaled as ordinary elements: read the definition back and rebuild so ticks, labels and arrows keep their size (rebuildSelectedPlane). */
  virtual std::optional<std::vector<Element>> rebuildAfterTransform(const ElementPtrs& members, const QString& id) const = 0;
  /** Anchor radius in screen pixels for hit testing. */
  virtual double anchorHitPx() const { return 9; }
};

class GeneratedRegistry {
 public:
  static GeneratedRegistry& instance();
  /** Handlers are asked in registration order; the first that claims decides. Registering a kind that exists replaces it. */
  void add(std::unique_ptr<GeneratedHandler> h);
  const GeneratedHandler* find(const ElementPtrs& members) const;
  const GeneratedHandler* ofKind(const QString& kind) const;
 private:
  std::vector<std::unique_ptr<GeneratedHandler>> handlers_;
};
