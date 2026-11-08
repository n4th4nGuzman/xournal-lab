// Text, formulas, images and shapes (the "texttools" area). What the other areas may need from here is declared in this header; everything else is local.
#pragma once
#include "geometry.h"
#include <QIcon>
#include <QImage>
#include <QPointF>
#include <QString>
#include <functional>

class Editor;
class MainWindow;
class QPlainTextEdit;
class QWidget;

namespace texttools {

/** State shared by the text tool, its toolbar switch and the selftest. */
struct TextState {
  bool markdown = true;                       // new texts are markdown (the Electron app starts with it on)
  ShapeKind shape = ShapeKind::Rect;          // what the shape tool draws
  std::function<void(bool)> markdownChanged;  // the toolbar switch follows the text being edited
};
TextState& state();

// ---- images (images.cpp) ----------------------------------------------------------------------------------------------------------------
/** Insert a picture on the current page as a base64 PNG (at most 2000 px a side), selected afterwards. false (with a notice) when it cannot be read. */
bool insertImage(Editor& ed, const QImage& img);
bool insertImageFile(Editor& ed, const QString& path);
/** True when the clipboard holds a picture (and not text): what a paste should insert as an image. */
bool clipboardHasImage();
bool pasteImageFromClipboard(Editor& ed);

// ---- text (texttool.cpp) ----------------------------------------------------------------------------------------------------------------
/** Finishes the text being written, if any (Ctrl+Enter / click away). */
void commitOpenText(bool cancel = false);
/** The in-place editor of the text being written, or null. */
QPlainTextEdit* openTextEditor();
bool textMarkdown();                        // of the open text, or of the next ones
void setTextMarkdown(bool on);              // for the open text and the next ones

// ---- formulas (latextool.cpp) -----------------------------------------------------------------------------------------------------------
/** Opens the formula dialog for a click at `pos` of `page`: edits the formula under the point, or starts a new one. */
void openFormulaAt(MainWindow& win, int page, const QPointF& pos);
/** Edits the formula at (layer, index) of the page. */
void editFormula(MainWindow& win, int page, int layer, int index);
QWidget* formulaDialog();                   // null until first opened
bool formulaDialogOpen();

// ---- registration pieces (install.cpp) --------------------------------------------------------------------------------------------------
void registerTextTool(Editor& ed);
void registerLatexTool(Editor& ed, MainWindow& win);
void registerShapeTool(Editor& ed);

/** Checks for --selftest (selftest_texttools.cpp). Replaces the open document with a blank one. */
void runTextToolsSelfTest(MainWindow& win, Editor& ed, const std::function<void(bool, const QString&)>& check, const std::function<void(const QString&)>& out, const QString& shotsDir);

/** The drawing icons of this area. */
QIcon icon(const QString& name);

}  // namespace texttools
