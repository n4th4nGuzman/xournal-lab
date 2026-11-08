// The glow button and its panel (Brillo), see glowpanel.cpp.
#pragma once

class Editor;
class QToolBar;
class QToolButton;

namespace selx {
/** A new "Brillo" button for `bar` (its panel is built the first time it opens). The caller adds it to the bar. */
QToolButton* makeGlowButton(Editor& ed, QToolBar* bar);
}  // namespace selx
