// What the number line, the set diagram and the fill tool share: the defaults of "the next one", the selected object as the panels need it, and the
// two edits (create, update) that go through Editor::swapGenerated. Port of the math part of editor.ts (selectedMath, finishMath, updateMath).
#pragma once
#include "editor.h"
#include "fill.h"
#include "generated.h"
#include "mathobj.h"
#include <functional>
#include <optional>

namespace mathobj {

struct State {
  sets::LineSpec line = sets::defaultLine();    // what the panel shows (and the next line is made with) when nothing is selected
  sets::VennSpec venn = sets::defaultVenn(2);
  FillOptions fill;
  /** The last fill made (measurements and checks, like lastFill / lastRiemann of the Electron editor). */
  std::optional<FillStats> lastFill;
  std::optional<RiemannInfo> lastRiemann;
  /** Opens the options panel of a tool: "numline", "venn" or "fill" (set by install; a no-op headless). */
  std::function<void(const QString&)> openPanel;
  /** Called after the user changed the fill options in the panel (the settings area can persist them; this area never writes the preferences). */
  std::function<void(const FillOptions&)> fillChanged;
};
State& state();

/** The pen's colour, width and glow: the style a line or diagram is drawn with. */
GenStyle styleFromPen(const Editor& ed);

/** The line or diagram that is selected (and nothing else), read back as it is now. */
struct Selected {
  int page = 0, layer = 0;
  QString id;
  sets::MathSpec spec;
  GenStyle style;
};
std::optional<Selected> selectedMath(Editor& ed);

/** "line" or "venn". */
QString kindOf(const sets::MathSpec& spec);
/** The definition as text, to tell whether an edit changed anything. */
QString specJson(const sets::MathSpec& spec);

/** Make a new object at the top of the active layer of `page`: one undo step, then select it, return to the previous tool later and open its panel. */
bool createMath(Editor& ed, int page, const sets::MathSpec& spec, const QString& toolId);
/** Change the selected object (with `restyle`: give it the pen's style). One undo step; the object stays selected. False when nothing suitable is selected. */
bool updateMath(Editor& ed, const sets::MathSpec& spec, bool restyle = false);

/** The anchor of the selected object under a page point (screen radius 10), if any. */
std::optional<int> mathAnchorAt(Editor& ed, QPointF p, double zoom);

}  // namespace mathobj
