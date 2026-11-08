// Special papers for maths and physics classes (millimetre, Cartesian plane, trigonometric, polar, logarithmic, isometric, Cornell) and the built-in
// page styles (lined, ruled, graph, dotted). Port of app/src/renderer/paper.ts. A page's paper is a PaperSpec saved on the <background> as the extra
// attribute xlPaper="kind;key=value;..."; the ordinary `style` attribute carries a close built-in look (`fallback`) so programs that do not know
// xlPaper still show something sensible. Pure drawing in page units (points).
#pragma once
#include "model.h"
#include <QColor>
#include <QPainter>
#include <map>
#include <optional>
#include <vector>

struct PaperSpec {
  QString kind;
  std::map<QString, QString> params;
};

struct PaperParam {
  QString key;
  QString label;
  std::vector<std::pair<QString, QString>> options;   // (value, label); the first one is the default
};

struct PaperKind {
  QString id;
  QString label;
  QString fallback;                                   // value of the plain `style` attribute written next to xlPaper
  std::vector<PaperParam> params;
};

const std::vector<PaperKind>& paperKinds();
const PaperKind* paperKind(const QString& id);

std::optional<PaperSpec> defaultSpec(const QString& kind);
/** Parses "kind;key=value;...". Unknown kinds give nullopt; unknown or invalid parameters fall back to the defaults. */
std::optional<PaperSpec> parseSpec(const QString& raw);
QString formatSpec(const PaperSpec& s);
PaperSpec withParam(const PaperSpec& s, const QString& key, const QString& value);

/** The special paper of a page background (its xlPaper attribute), if it has a known one. */
std::optional<PaperSpec> paperOfBackground(const Attrs& background);

/** Draws the lines of a special paper over an already painted background colour (the colour decides light or dark ink). */
void drawPaper(QPainter& p, double w, double h, const QColor& bg, const PaperSpec& spec);
/** The built-in styles: lined, ruled, graph, dotted, isograph, isodotted (anything else draws nothing). `spacing` is the distance between lines or dots in points;
 *  0 keeps the style's own. */
void drawBuiltinStyle(QPainter& p, double w, double h, const QString& style, double spacing = 0);
/** The distance in points between the lines (or dots) of a built-in style, 0 for a style that has none. */
double builtinSpacing(const QString& style);

// Two optional attributes of a solid background, apart from the paper itself: xlSpacing (points, built-in styles only) and xlOpacity (0.05 to 1, any paper).
// Programs that do not know them draw the paper at its default look.
constexpr double kMinSpacing = 4, kMaxSpacing = 140, kMinPaperOpacity = 0.05;
/** The custom spacing of a background in points, 0 when it has none (or an unusable one). */
double paperSpacingOf(const Attrs& background);
/** The opacity of the paper's lines, 1 when none is set (or an unusable one). */
double paperOpacityOf(const Attrs& background);
/** Sets or (for the default value, 0 / 1) removes the attribute. */
void setPaperSpacing(Attrs& background, double points);
void setPaperOpacity(Attrs& background, double opacity);
