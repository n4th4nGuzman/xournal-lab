// Files saved before the labels of planes, 3D spaces and figures were written with their position, size and colour: those texts come back at the top
// left corner of the page, all on top of each other, and no longer belong to what they described. The strokes of such an object still hold its
// definition, so the whole object is built again from them (in the same place of the layer). Port of app/src/renderer/repair.ts.
//
// The builders of planes, spaces and figures belong to their own modules, so repair does not know them: the caller passes them as Rebuilders (in the
// order they are tried, as the TypeScript does: plane, 3D space, figure).
#pragma once
#include "model.h"
#include <functional>
#include <optional>
#include <vector>

struct Rebuild {
  bool claimed = false;                     // the members are an object of this kind (the next rebuilder is not asked)
  std::optional<std::vector<Element>> built;  // the new elements; none when the definition could not be read (the object is then left as it is)
};
/** `members` are the elements of one object (same xlPlane id) in the layer, in order. */
using Rebuilder = std::function<Rebuild(const QString& id, const std::vector<const Element*>& members)>;

/** True for a text that has neither x nor y attribute (the signature of the old bug). */
bool lacksPlace(const Element& e);

/** Build again the objects whose labels have no position. Returns how many objects were repaired; a second call returns 0. */
int repairGenerated(Document& doc, const std::vector<Rebuilder>& rebuilders);

/** The rebuilders of the objects this app makes, in the order of the TypeScript: Cartesian plane, 3D space, figure. */
std::vector<Rebuilder> standardRebuilders();
inline int repairGenerated(Document& doc) { return repairGenerated(doc, standardRebuilders()); }
