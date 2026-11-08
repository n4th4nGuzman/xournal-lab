// Size presets shown in the toolbar for the pen, the highlighter, the eraser and text, and the user's changes to them. Port of sizes.ts.
#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <array>
#include <vector>

enum class SizeKind { Pen = 0, Highlighter = 1, Eraser = 2, Text = 3 };
constexpr int kSizeKinds = 4;

/** The tools of the editor as far as sizes care (the editor's own enum maps onto this one). */
enum class SizeTool { Pen, Highlighter, Eraser, EraserPart, Text, Shape, Figure, Plane, Space3, NumLine, Venn, Select, Lasso, Hand, Laser, Fill, LaTeX, VSpace, Other };

struct SizePresets { std::array<std::vector<double>, kSizeKinds> v; std::vector<double>& of(SizeKind k) { return v[int(k)]; } const std::vector<double>& of(SizeKind k) const { return v[int(k)]; } };

const char* sizeKindKey(SizeKind k);         // "pen", "highlighter", "eraser", "text": the keys of the saved JSON
const SizePresets& defaultSizes();
struct SizeLimits { double lo, hi; };
SizeLimits sizeLimits(SizeKind k);

/** Which presets a tool uses: the pen's serve shapes, planes and figures too; false for tools with no size. */
bool sizeKindOf(SizeTool tool, SizeKind* out);

/** Whatever was stored, as valid presets: a list that is not a list of sensible numbers goes back to its default. Any non-object gives the defaults. */
SizePresets sanitizeSizes(const QJsonValue& raw);
QJsonObject sizesToJson(const SizePresets& s);

/** Diameter in pixels of the dot that stands for a size on its button. */
int dotSize(SizeKind kind, double v);
