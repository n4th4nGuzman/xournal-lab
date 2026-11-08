// The tools that ship with the application foundation. The other tools live in their own files next to these and register themselves from
// registerBuiltinTools() (one line each) or from anywhere else that has the Editor.
#pragma once
class Editor;

void registerBuiltinTools(Editor& ed);

// each registers its own tool(s); defined in the file of the same name
void registerInkTools(Editor& ed);      // pen, highlighter
void registerEraserTools(Editor& ed);   // eraser, eraserPart
void registerHandTool(Editor& ed);
void registerLaserTool(Editor& ed);
void registerSelectPlaceholder(Editor& ed);   // replaced by the selection tool (same id "select")
