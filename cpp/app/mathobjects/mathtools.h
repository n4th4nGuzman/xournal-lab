// The tools of this area and their option panels (see install.cpp for the wiring).
#pragma once
#include <QIcon>
#include <QString>
#include <QWidget>

class Editor;

namespace mathobj {

/** Tools "numline", "venn" and "fill". */
void registerMathTools(Editor& ed);
void registerFillTool(Editor& ed);

/** Toolbar glyphs, drawn like the ones of the Electron app (24 unit viewBox) in the colour of the other icons. */
QIcon toolIcon(const QString& name);

/** The option panels (built once by the toolbar caret; they keep themselves in sync with the selection). `kind` is "line" or "venn". */
QWidget* makeMathPanel(QWidget* parent, Editor& ed, const QString& kind);
QWidget* makeFillPanel(QWidget* parent, Editor& ed);

}  // namespace mathobj
