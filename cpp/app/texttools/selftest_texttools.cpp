// Self test of the text / formula / image / shape area (called from --selftest). It mirrors the Electron e2e assertions of app.ts (sections E, F, H shapes,
// L formula dialog, N shape recognizer) but drives the REAL path: synthetic mouse events on the DocumentView viewport -> Editor -> tool, key events on the
// real widgets (text editor, formula box, palette). Synthetic events are not a physical pen.
#include "docfactory.h"
#include "documentview.h"
#include "editor.h"
#include "latexpalette.h"
#include "mainwindow.h"
#include "mathlayout.h"
#include "texrender.h"
#include "texttools.h"
#include "xopp.h"
#include <QApplication>
#include <QBuffer>
#include <QPainter>
#include "genobj.h"
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cmath>

namespace texttools {

using CheckFn = std::function<void(bool, const QString&)>;
using OutFn = std::function<void(const QString&)>;

namespace {

struct Rig {
  MainWindow& win;
  Editor& ed;
  DocumentView& v;
  const CheckFn& check;
  const OutFn& out;
  int page = 0;

  void pump(int n = 4) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
  void wait(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(4); }
  }
  QPointF view(const QPointF& p) const { return v.toView(page, p); }
  void mouse(QEvent::Type t, const QPointF& p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m = Qt::NoModifier) {
    QMouseEvent ev(t, view(p), v.viewport()->mapToGlobal(view(p)), b, bs, m);
    QApplication::sendEvent(v.viewport(), &ev);
  }
  void click(const QPointF& p) {
    mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    pump();
  }
  void drag(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers m = Qt::NoModifier, int steps = 5) {
    mouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, m);
    for (int i = 1; i <= steps; i++) mouse(QEvent::MouseMove, a + (b - a) * (double(i) / steps), Qt::NoButton, Qt::LeftButton, m);
    mouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, m);
    pump();
  }
  void key(QWidget* w, int k, Qt::KeyboardModifiers m = Qt::NoModifier, const QString& text = QString()) {
    QKeyEvent p(QEvent::KeyPress, k, m, text), r(QEvent::KeyRelease, k, m, text);
    QApplication::sendEvent(w, &p);
    QApplication::sendEvent(w, &r);
  }
  size_t count() const {
    size_t n = 0;
    for (const Layer& l : ed.doc().pages[size_t(page)].layers) n += l.elements.size();
    return n;
  }
  const Element* last() const {
    const auto& ls = ed.doc().pages[size_t(page)].layers;
    for (auto it = ls.rbegin(); it != ls.rend(); ++it) if (!it->elements.empty()) return &it->elements.back();
    return nullptr;
  }
  const Text* lastText() const { return last() ? std::get_if<Text>(last()) : nullptr; }
  const Stroke* lastStroke() const { return last() ? std::get_if<Stroke>(last()) : nullptr; }
  const Image* lastImage() const { return last() ? std::get_if<Image>(last()) : nullptr; }
};

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

template <class T> T* child(QWidget* root, const char* name) { return root ? root->findChild<T*>(QString::fromLatin1(name)) : nullptr; }

const Text* textBody(const Rig& r, const QString& body) {
  for (const Layer& l : r.ed.doc().pages[size_t(r.page)].layers)
    for (const Element& e : l.elements) if (const Text* t = std::get_if<Text>(&e)) if (t->text == body) return t;
  return nullptr;
}

// ---- text -----------------------------------------------------------------------------------------------------------------------------------
void textChecks(Rig& r, const QString& shots) {
  Editor& ed = r.ed;
  ed.setTool("text");
  const size_t n0 = r.count();
  r.click({200, 150});
  QPlainTextEdit* w = openTextEditor();
  r.check(w != nullptr, "text tool: a click opens the in-place editor");
  r.check(w && w->isVisible() && w->font().pixelSize() >= 8, "text: the editor is visible with the text's font size at this zoom");
  if (!w) return;
  r.check(!w->testAttribute(Qt::WA_InputMethodEnabled) && !w->viewport()->testAttribute(Qt::WA_InputMethodEnabled), "text: the editor has no input-method context (no Windows handwriting panel over it); typed characters still arrive as key events");
  w->setPlainText("Hola\nmundo");
  commitOpenText();
  r.pump();
  const Text* t = r.lastText();
  r.check(t && t->text == "Hola\nmundo" && r.count() == n0 + 1, "text: confirming creates the text with its line break");
  r.check(t && attrOr(t->attrs, "font") == "Sans" && attrOr(t->attrs, "color") == "#000000ff" && attrOr(t->attrs, "size") == "12" && attrOr(t->attrs, "xlMd") == "1" &&
              near(attrOr(t->attrs, "x").toDouble(), 200) && near(attrOr(t->attrs, "y").toDouble(), 150),
          "text: attributes are font Sans, size 12, colour, position and xlMd=1 (markdown is on by default)");
  r.check(openTextEditor() == nullptr, "text: the editor is gone after confirming");

  r.click({205, 155});
  w = openTextEditor();
  r.check(w && w->toPlainText() == "Hola\nmundo" && r.count() == n0, "text: a click on it re-opens it with its content (the original is lifted out while editing)");
  if (w) {
    w->setPlainText("Hola editado");
    r.key(w, Qt::Key_Return, Qt::ControlModifier);   // Ctrl+Enter confirms
    r.pump();
  }
  t = r.lastText();
  r.check(t && t->text == "Hola editado" && r.count() == n0 + 1 && openTextEditor() == nullptr, "text: Ctrl+Enter confirms the edit in place (no duplicate)");
  const int depth = ed.undoDepth();
  ed.undo();
  t = r.lastText();
  r.check(t && t->text == "Hola\nmundo" && r.count() == n0 + 1, "text: undo restores the previous text");
  ed.redo();
  r.check(r.lastText() && r.lastText()->text == "Hola editado", "text: redo applies the edit again");
  ed.undo();

  r.click({205, 155});
  w = openTextEditor();
  if (w) { w->setPlainText("descartado"); r.key(w, Qt::Key_Escape); r.pump(); }
  r.check(r.lastText() && r.lastText()->text == "Hola\nmundo" && r.count() == n0 + 1 && ed.undoDepth() == depth - 1, "text: Esc cancels and leaves no undo step");

  // emptied: the box is deleted, and undo brings it back
  r.click({205, 155});
  w = openTextEditor();
  if (w) { w->setPlainText("  \n"); commitOpenText(); r.pump(); }
  r.check(r.count() == n0, "text: emptying a text deletes it");
  ed.undo();
  r.check(r.count() == n0 + 1 && r.lastText() && r.lastText()->text == "Hola\nmundo", "text: undoing the deletion restores it");

  // an empty new text is not created
  r.click({400, 300});
  w = openTextEditor();
  if (w) { commitOpenText(); r.pump(); }
  r.check(r.count() == n0 + 1, "text: an empty new text is not created");

  // click away: a click elsewhere confirms the open text and opens a new editor there
  r.click({300, 400});
  w = openTextEditor();
  if (w) w->setPlainText("primero");
  r.click({420, 520});
  r.check(r.count() == n0 + 2 && r.lastText() && r.lastText()->text == "primero" && openTextEditor() != nullptr, "text: clicking elsewhere confirms the open text and opens another");
  {   // save / export / print finish a text that is still being typed
    const size_t before = r.count();
    if (QPlainTextEdit* e = openTextEditor()) e->setPlainText("sin confirmar");
    ed.flushPending();
    r.check(openTextEditor() == nullptr && r.count() == before + 1 && r.lastText() && r.lastText()->text == "sin confirmar", "text: flushPending (save, export, print) commits the text being typed");
    ed.undo();   // leave the page as the next checks expect it
  }
  commitOpenText();

  // markdown switch: off for this box only, then undo
  r.click({205, 155});
  w = openTextEditor();
  r.check(textMarkdown() && ed.action("tool.markdown")->isChecked(), "markdown: the switch shows the box's mode (on)");
  QGuiApplication::processEvents(QEventLoop::AllEvents);   // previews of finished texts are deleted later
  QWidget* pv = r.v.viewport()->findChild<QWidget*>("xlMdPreview");
  r.check(pv && pv->isVisible() && pv->width() > 20 && w && pv->y() > w->y(), QString("markdown: a live preview of the formatted box shows under the editor while writing (found %1, visible %2, y %3 vs %4, w %5)").arg(pv != nullptr).arg(pv && pv->isVisible()).arg(pv ? pv->y() : -1).arg(w ? w->y() : -1).arg(pv ? pv->width() : -1));
  if (w) { const QString keep = w->toPlainText(); w->setPlainText("# Titulo **fuerte**\n- uno"); QGuiApplication::processEvents(); const int h1 = pv ? pv->height() : 0; w->setPlainText(keep); QGuiApplication::processEvents(); r.check(pv && h1 > pv->height(), "markdown: the preview follows what is typed (a heading and a list are taller than the original)"); }
  setTextMarkdown(false);
  r.check(pv && !pv->isVisible(), "markdown: the preview goes away when markdown is switched off for the box");
  r.check(!ed.action("tool.markdown")->isChecked(), "markdown: the switch follows when toggled while writing");
  commitOpenText();
  r.pump();
  r.check(textBody(r, "Hola\nmundo") && attrFind(textBody(r, "Hola\nmundo")->attrs, "xlMd") == nullptr && r.count() == n0 + 2, "markdown: turning it off while editing removes xlMd from that text");
  r.check(ed.action("tool.markdown")->isChecked() == false, "markdown: the setting stays off for the next texts (it is the toggle, not the box)");
  ed.undo();
  r.check(textBody(r, "Hola\nmundo") && attrOr(textBody(r, "Hola\nmundo")->attrs, "xlMd") == "1", "markdown: undo restores the markdown flag");
  r.click({300, 600});
  w = openTextEditor();
  if (w) { w->setPlainText("plano"); commitOpenText(); }
  r.check(r.lastText() && attrFind(r.lastText()->attrs, "xlMd") == nullptr, "markdown: with the switch off a new text is plain");
  setTextMarkdown(true);

  // size presets: the next text takes the style's text size and colour
  Style s = ed.style();
  const Style saved = s;
  s.textSize = 24;
  s.color = "#d32f2f";
  ed.setStyle(s);
  r.click({100, 650});
  w = openTextEditor();
  if (w) { w->setPlainText("# Título\n**negrita** y *cursiva*\n- uno\n- dos\n> cita"); commitOpenText(); }
  r.check(r.lastText() && attrOr(r.lastText()->attrs, "size") == "24" && attrOr(r.lastText()->attrs, "color") == "#d32f2fff", "text: size and colour come from the style");
  const Rect rb = r.lastText() ? textRect(*r.lastText()) : Rect{};
  Text plain = r.lastText() ? *r.lastText() : Text();
  plain.attrs.erase(std::remove_if(plain.attrs.begin(), plain.attrs.end(), [](const auto& a) { return a.first == "xlMd"; }), plain.attrs.end());
  const Rect pb = textBBox(plain);
  r.check(rb[2] - rb[0] > 0 && rb[3] - rb[1] > 0 && std::abs((rb[3] - rb[1]) - (pb[3] - pb[1])) > 1, "markdown: the box is measured by the markdown layout (not the plain one)");
  // the editor over the page at another zoom, to look at: it must sit exactly where the text is drawn
  r.v.setZoom(1.6);
  r.pump(6);
  r.v.scrollByPixels(0, 700);
  r.pump(6);
  r.v.renderVisibleNow();
  r.click({110, 660});
  if (QPlainTextEdit* e = openTextEditor()) {
    r.pump(4);
    r.v.renderVisibleNow();   // the lifted text must be gone from the page under the editor
    r.pump(2);
    const QPointF vp = r.view({100, 650});
    r.check(std::abs(e->x() - vp.x()) <= 3 && std::abs(e->y() - vp.y()) <= 3, "text: the editor sits at the text's position at zoom 160 %");
    r.check(e->font().pixelSize() == int(std::lround(24 * r.v.zoom())), "text: the editor's font is the text's size times the zoom");
    r.v.viewport()->grab().save(shots + "/texttools_text_editing.png");
    commitOpenText(true);
  }
  r.v.zoomFitWidth();
  r.v.scrollToPage(0);
  r.pump(6);
  ed.setStyle(saved);
}

// ---- shapes -----------------------------------------------------------------------------------------------------------------------------------
void shapeChecks(Rig& r) {
  Editor& ed = r.ed;
  ed.setTool("shape");
  const size_t n0 = r.count();
  state().shape = ShapeKind::Rect;
  r.drag({100, 500}, {200, 560});
  const Stroke* s = r.lastStroke();
  Rect b = s ? bboxOfPoints(s->pts) : Rect{};
  r.check(s && s->pts.size() == 10 && near(b[0], 100) && near(b[1], 500) && near(b[2], 200) && near(b[3], 560), "shape: a rectangle is 5 points over the dragged box");
  r.check(s && s->tool() == "pen" && s->widths.size() == 1, "shape: stored as an ordinary pen stroke with one width");
  r.check(s && attrOr(s->attrs, "xlShape") == "1", "shape: marked xlShape so that its corners are drawn sharp");
  state().shape = ShapeKind::Ellipse;
  r.drag({300, 500}, {380, 560});
  s = r.lastStroke();
  b = s ? bboxOfPoints(s->pts) : Rect{};
  r.check(s && s->pts.size() == 146 && near(b[0], 300, 1e-6) && near(b[2], 380, 1e-6) && near(b[1], 500, 1e-6) && near(b[3], 560, 1e-6), "shape: an ellipse has 73 points and fills its box");
  state().shape = ShapeKind::Line;
  r.drag({100, 600}, {150, 640}, Qt::ShiftModifier);
  s = r.lastStroke();
  const double ang = s ? std::atan2(s->pts[3] - s->pts[1], s->pts[2] - s->pts[0]) * 180 / M_PI : 1;
  r.check(s && near(std::fmod(ang, 15.0), 0, 1e-6) && near(std::hypot(s->pts[2] - s->pts[0], s->pts[3] - s->pts[1]), std::hypot(50, 40), 1e-6), "shape: Shift snaps a line to 15 degrees keeping its length");
  state().shape = ShapeKind::Rect;
  r.drag({100, 650}, {180, 700}, Qt::ShiftModifier);
  s = r.lastStroke();
  b = s ? bboxOfPoints(s->pts) : Rect{};
  r.check(s && near(b[2] - b[0], b[3] - b[1]) && near(b[2] - b[0], 80), "shape: Shift makes the rectangle a square");
  state().shape = ShapeKind::Arrow;
  r.drag({300, 650}, {400, 650});
  s = r.lastStroke();
  r.check(s && s->pts.size() == 10 && near(s->pts[2], s->pts[6]) && near(s->pts[3], s->pts[7]) && s->pts[4] < s->pts[2] && s->pts[8] < s->pts[2] && s->pts[5] != s->pts[9], "shape: an arrow has its head at the end");
  r.check(r.count() == n0 + 5, "shape: five shapes added");
  const size_t before = r.count();
  r.click({50, 750});
  r.check(r.count() == before, "shape: a click without dragging makes nothing");
  // the preview lives in the overlay while dragging
  r.mouse(QEvent::MouseButtonPress, {100, 300}, Qt::LeftButton, Qt::LeftButton);
  r.mouse(QEvent::MouseMove, {160, 340}, Qt::NoButton, Qt::LeftButton);
  r.pump();
  const QImage during = r.v.viewport()->grab().toImage();
  r.mouse(QEvent::MouseButtonRelease, {160, 340}, Qt::LeftButton, Qt::NoButton);
  r.pump();
  r.check(!during.isNull(), "shape: overlay preview paints while dragging");
  for (int k = 0; k < 6; k++) ed.undo();
  r.check(r.count() == n0, "shape: every shape is one undo step");
  // the options panel (caret) switches the kind
  const ToolInfo* ti = ed.toolInfo("shape");
  r.check(ti && ti->options && ti->shortcut == QKeySequence("G") && ti->inserter, "shape: registered with key G, a caret panel and as an inserter");
}

// ---- shape recognizer ---------------------------------------------------------------------------------------------------------------------------
void recognizeChecks(Rig& r) {
  Editor& ed = r.ed;
  ed.setRecognizeShapes(true);
  r.check(ed.action("tool.recognizer")->isChecked(), "recognizer: the toolbar switch follows the setting");
  auto noise = [](int i) { return std::sin(i * 12.9898) * 0.8; };
  auto hand = [&](const std::vector<QPointF>& pts) {
    ed.setTool("pen");
    r.mouse(QEvent::MouseButtonPress, pts.front(), Qt::LeftButton, Qt::LeftButton);
    for (size_t k = 1; k < pts.size(); k++) r.mouse(QEvent::MouseMove, pts[k], Qt::NoButton, Qt::LeftButton);
    r.mouse(QEvent::MouseButtonRelease, pts.back(), Qt::LeftButton, Qt::NoButton);
    r.pump();
  };
  std::vector<QPointF> circle;
  for (int i = 0; i <= 90; i++) { const double a = i / 88.0 * M_PI * 2; circle.push_back({350 + std::cos(a) * 40 + noise(i), 300 + std::sin(a) * 40 + noise(i + 7)}); }
  hand(circle);
  Rect b = r.lastStroke() ? bboxOfPoints(r.lastStroke()->pts) : Rect{};
  r.check(r.lastStroke() && r.lastStroke()->pts.size() == 146 && near(b[0], 310, 6) && near(b[2], 390, 6) && r.lastStroke()->widths.size() == 1, "recognizer: a hand-drawn circle becomes the clean ellipse");
  r.check(r.lastStroke() && attrOr(r.lastStroke()->attrs, "xlShape") == "1", "recognizer: the clean shape is marked xlShape (sharp corners)");
  std::vector<QPointF> rect;
  const QPointF corners[] = {{280, 160}, {420, 160}, {420, 220}, {280, 220}, {280, 163}};
  for (int c = 0; c < 4; c++) for (int i = 0; i < 20; i++) { const double t = i / 20.0; rect.push_back(corners[c] + (corners[c + 1] - corners[c]) * t + QPointF(noise(c * 20 + i), noise(c * 20 + i + 3))); }
  rect.push_back({281, 164});
  hand(rect);
  b = r.lastStroke() ? bboxOfPoints(r.lastStroke()->pts) : Rect{};
  r.check(r.lastStroke() && r.lastStroke()->pts.size() == 10 && near(b[0], 280, 6) && near(b[2], 420, 6), "recognizer: a hand-drawn rectangle becomes the clean rectangle");
  std::vector<QPointF> line;
  for (int i = 0; i <= 40; i++) line.push_back({280 + i * 3.5, 400 + i * 0.2 + noise(i)});
  hand(line);
  r.check(r.lastStroke() && r.lastStroke()->pts.size() == 4 && near(r.lastStroke()->pts[1], r.lastStroke()->pts[3], 1e-9), "recognizer: a nearly straight line snaps to a horizontal line");
  std::vector<QPointF> tri;
  const QPointF tc[] = {{450, 420}, {505, 490}, {395, 490}, {450, 422}};
  for (int c = 0; c < 3; c++) for (int i = 0; i < 20; i++) { const double t = i / 20.0; tri.push_back(tc[c] + (tc[c + 1] - tc[c]) * t + QPointF(noise(c * 20 + i), noise(c * 20 + i + 5))); }
  tri.push_back({451, 424});
  hand(tri);
  r.check(r.lastStroke() && r.lastStroke()->pts.size() == 8, "recognizer: a hand-drawn triangle becomes the clean triangle");
  std::vector<QPointF> scribble;
  for (int i = 0; i < 40; i++) scribble.push_back({280 + i * 3.0, 540.0 + (i % 2 ? 14 : -14) + (i % 5)});
  hand(scribble);
  r.check(r.lastStroke() && r.lastStroke()->pts.size() > 20, "recognizer: a scribble is left untouched");
  ed.setRecognizeShapes(false);
  const size_t n = r.count();
  hand(circle);
  r.check(r.count() == n + 1 && r.lastStroke() && r.lastStroke()->pts.size() != 146, "recognizer: switched off, the circle stays as drawn");
  for (int k = 0; k < 6; k++) ed.undo();
}

// ---- images --------------------------------------------------------------------------------------------------------------------------------------
void imageChecks(Rig& r, const QString& tmp) {
  Editor& ed = r.ed;
  ed.setTool("pen");
  QImage img(160, 80, QImage::Format_ARGB32);
  img.fill(QColor("#3a7bd5"));
  { QPainter p(&img); p.fillRect(10, 10, 40, 20, Qt::white); }
  const size_t n0 = r.count();
  const bool ok = insertImage(ed, img);
  const Image* im = r.lastImage();
  r.check(ok && im && im->tag == "image" && im->data.startsWith("iVBOR") && r.count() == n0 + 1, "image: inserted as a base64 PNG");
  Rect b = im ? imageBox(*im) : Rect{};
  r.check(im && near(b[2] - b[0], 120, 1e-4) && near((b[3] - b[1]) / (b[2] - b[0]), 0.5, 1e-4) && near((b[0] + b[2]) / 2, ed.doc().pages[size_t(r.page)].width() / 2, 1e-3), "image: 120 pt wide (160 px at 96 dpi), proportions kept, centred");
  r.check(im && attrFind(im->attrs, "left") && im->attrs[0].first == "left" && im->attrs.size() == 4, "image: attributes are left, top, right, bottom");
  const bool realSel = dynamic_cast<NullSelection*>(&ed.selection()) == nullptr;
  if (realSel) r.check(ed.selection().items().size() == 1, "image: selected after inserting");
  r.check(ed.tool() == "select" && !ed.returnTo().isEmpty(), "image: the editor goes to Seleccionar and remembers the tool before (Esc returns)");
  ed.undo();
  r.check(r.count() == n0, "image: one undo step removes it");
  ed.redo();
  ed.undo();

  // very large and tiny pictures
  QImage big(3000, 1500, QImage::Format_RGB32);
  big.fill(Qt::green);
  insertImage(ed, big);
  r.check(r.lastImage() && QImage::fromData(QByteArray::fromBase64(r.lastImage()->data.toLatin1())).width() == 2000, "image: a larger picture is shrunk to 2000 px a side");
  ed.undo();
  r.check(!insertImage(ed, QImage()), "image: a null picture is refused");

  // from a file and by drag and drop
  const QString png = tmp + "/dropped.png";
  img.save(png);
  r.check(insertImageFile(ed, png) && r.count() == n0 + 1, "image: insert from a file");
  ed.undo();
  QMimeData md;
  md.setUrls({QUrl::fromLocalFile(png)});
  QDragEnterEvent enter(r.v.viewport()->rect().center(), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(r.v.viewport(), &enter);
  r.check(enter.isAccepted(), "image: a picture file dragged over the page is accepted");
  QDropEvent drop(r.v.viewport()->rect().center(), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(r.v.viewport(), &drop);
  r.pump();
  r.check(r.count() == n0 + 1 && r.lastImage(), "image: dropping a picture file on the page inserts it");
  ed.undo();
  QMimeData notimg;
  notimg.setUrls({QUrl::fromLocalFile(tmp + "/nada.txt")});
  QDragEnterEvent enter2(r.v.viewport()->rect().center(), Qt::CopyAction, &notimg, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(r.v.viewport(), &enter2);
  QDropEvent drop2(r.v.viewport()->rect().center(), Qt::CopyAction, &notimg, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(r.v.viewport(), &drop2);
  r.check(!enter2.isAccepted(), "image: something that is not a picture is not accepted by the page (other handlers may take it)");
  r.check(r.count() == n0, "image: dropping something that is not a picture does nothing here");

  // clipboard
  QGuiApplication::clipboard()->setImage(img);
  r.check(clipboardHasImage(), "image: the clipboard reports a picture");
  r.ed.action("edit.paste")->trigger();
  r.pump();
  r.check(r.count() == n0 + 1 && r.lastImage(), QString("image: Pegar with a picture on the clipboard inserts it exactly once (%1 new elements)").arg(int(r.count() - n0)));
  while (r.count() > n0) ed.undo();
  QGuiApplication::clipboard()->clear();
  ed.setTool("pen");
}

// ---- formulas ------------------------------------------------------------------------------------------------------------------------------------
void formulaChecks(Rig& r, const QString& shots) {
  Editor& ed = r.ed;
  const size_t n0 = r.count();
  ed.setTool("latex");
  QElapsedTimer topen;
  topen.start();
  r.click({150, 200});
  r.pump();
  r.out(QString("latex: first click to dialog on screen (builds the dialog and the first palette tab) %1 ms").arg(topen.elapsed()));
  QWidget* dlg = formulaDialog();
  auto* edit = child<QPlainTextEdit>(dlg, "ltxEdit");
  auto* ok = child<QPushButton>(dlg, "ltxOk");
  auto* err = child<QLabel>(dlg, "ltxErr");
  r.check(dlg && formulaDialogOpen() && dlg->isVisible() && ok && !ok->isEnabled() && edit, "latex: a click opens the dialog; Insertar is disabled while the source is empty");
  if (!dlg || !edit || !ok || !err) return;

  edit->setPlainText("\\frac{1}{2}");
  r.wait(160);
  r.check(err->text().isEmpty() && ok->isEnabled() && child<QWidget>(dlg, "ltxPreview"), "latex: live preview renders and enables Insertar");
  edit->setPlainText("\\frac{");
  r.wait(160);
  r.check(!err->text().isEmpty() && !ok->isEnabled(), "latex: a source with an error shows the message and blocks Insertar");
  const QString firstError = err->text();
  edit->setPlainText("");
  edit->setFocus();
  r.key(edit, Qt::Key_BraceLeft, Qt::NoModifier, "{");
  r.check(edit->toPlainText() == "{}" && edit->textCursor().position() == 1, "latex: typing { adds the closing brace with the caret between");
  r.key(edit, Qt::Key_BraceRight, Qt::NoModifier, "}");
  r.check(edit->toPlainText() == "{}" && edit->textCursor().position() == 2, "latex: typing } before a } steps over it");
  edit->setPlainText("");

  // palette
  const auto& cats = paletteCategories();
  size_t total = 0;
  for (const auto& c : cats) total += c.items.size();
  auto* tabs = child<QTabBar>(dlg, "ltxTabs");
  auto* list = child<QListWidget>(dlg, "ltxList");
  auto* search = child<QLineEdit>(dlg, "ltxSearch");
  r.check(total >= 330 && cats.size() >= 10, QString("latex: the palette has %1 symbols in %2 categories").arg(total).arg(cats.size()));
  r.check(tabs && tabs->count() == int(cats.size()) + 2 && !tabs->isTabVisible(tabs->count() - 1), "latex: one tab per category plus Recientes (and a hidden one used while searching)");
  int vc = -1;
  for (size_t i = 0; i < cats.size(); i++) if (cats[i].id == "vcalc") vc = int(i);
  r.check(vc == 2 && cats[size_t(vc)].items.size() >= 70, "latex: the vector-calculus tab is third and has 70+ entries");
  if (!tabs || !list || !search) return;
  r.check(list->count() == int(cats[0].items.size()) && tabs->currentIndex() == 1, "latex: opens on the first category, one button per item");
  list->setCurrentRow(-1);
  edit->setPlainText("");
  edit->setFocus();
  emit list->itemClicked(list->item(0));   // first entry: the fraction
  r.check(edit->toPlainText() == "\\frac{}{}" && edit->textCursor().position() == 6, "latex: a palette button inserts at the caret and puts it where the snippet says");
  auto clickSnippet = [&](const QString& snippet) {
    for (int i = 0; i < list->count(); i++) if (list->item(i)->data(Qt::UserRole).toString() == snippet) { emit list->itemClicked(list->item(i)); return true; }
    return false;
  };
  edit->setPlainText("");
  r.check(clickSnippet(QString("\\sqrt[") + kPaletteCaret + "]{}") && edit->toPlainText() == "\\sqrt[]{}" && edit->textCursor().position() == 6, "latex: indexed root puts the caret in the index");
  edit->setPlainText("2");
  QTextCursor c = edit->textCursor();
  c.setPosition(1);
  edit->setTextCursor(c);
  r.check(clickSnippet(QString("^{") + kPaletteCaret + "}") && edit->toPlainText() == "2^{}" && edit->textCursor().position() == 3, "latex: the exponent goes at the caret, not at the end");
  tabs->setCurrentIndex(0);
  r.check(list->count() >= 2 && list->item(0)->data(Qt::UserRole).toString() == QString("^{") + kPaletteCaret + "}", "latex: Recientes lists the latest used first");
  QElapsedTimer tm;
  tm.start();
  tabs->setCurrentIndex(vc + 1);
  r.out(QString("latex: first show of the vector-calculus tab (cold: %1 formulas laid out and drawn) %2 ms").arg(list->count()).arg(tm.elapsed()));
  r.check(list->count() == int(cats[size_t(vc)].items.size()) && tabs->currentIndex() == vc + 1, "latex: the vector-calculus tab shows all its items");
  tm.restart();
  int greek = -1;
  for (size_t i = 0; i < cats.size(); i++) if (cats[i].id == "greek") greek = int(i);
  tabs->setCurrentIndex(greek + 1);
  r.check(list->count() == 40 && !list->item(0)->icon().isNull(), "latex: the Greek tab has 40 buttons drawn as typeset maths");
  tabs->setCurrentIndex(vc + 1);
  r.out(QString("latex: palette tab switch (vcalc, %1 icons, warm) %2 ms").arg(list->count()).arg(tm.elapsed()));
  bool allIcons = true;
  for (int i = 0; i < list->count(); i++) if (list->item(i)->icon().isNull()) allIcons = false;
  r.check(allIcons, "latex: every vector-calculus button is typeset (no fallback text)");
  const std::vector<QString> wanted = {"fraccion", "exponente", "subindice", "raiz cubica", "binomial", "valor absoluto", "norma", "matriz 3", "determinante", "integral definida", "integral doble",
                                       "limite", "derivada parcial", "sumatoria", "productoria", "gradiente", "alfa", "omega mayuscula", "implica", "pertenece", "reales", "vector", "negrita", "espacio fino"};
  QStringList missing;
  for (const QString& q : wanted) if (searchPalette(q).empty()) missing << q;
  r.check(missing.isEmpty(), "latex: the search finds the core structures" + (missing.isEmpty() ? QString() : " (missing: " + missing.join(", ") + ")"));
  search->setText("integral");
  r.pump();
  r.check(list->count() >= 6 && tabs->currentIndex() == tabs->count() - 1, "latex: searching shows matches and unselects the tabs");
  search->setText("zzzz");
  r.pump();
  auto* empty = child<QLabel>(dlg, "ltxEmpty");
  r.check(empty && empty->isVisible() && empty->text().startsWith("Nada con ese nombre"), "latex: an empty search says so");
  search->setFocus();
  r.key(dlg, Qt::Key_Escape);
  r.check(search->text().isEmpty() && dlg->isVisible(), "latex: Esc in the search clears it without closing the dialog");
  tabs->setCurrentIndex(1);

  // insert with Ctrl+Enter
  edit->setPlainText("\\alpha + \\beta");
  r.wait(160);
  QElapsedTimer ti;
  ti.start();
  r.key(edit, Qt::Key_Return, Qt::ControlModifier);
  r.pump();
  const double insMs = ti.elapsed();
  const Image* im = r.lastImage();
  r.check(!formulaDialogOpen() && im && im->tag == "teximage" && attrOr(im->attrs, "text") == "\\alpha + \\beta" && r.count() == n0 + 1, "latex: Ctrl+Enter inserts the formula and closes the dialog");
  r.out(QString("latex: insert (layout + PDF + bitmap + undo step) %1 ms").arg(insMs, 0, 'f', 0));
  r.check(im && im->attrs.size() == 5 && im->attrs[0].first == "text" && im->attrs[1].first == "left" && im->attrs[4].first == "bottom" && im->data.startsWith("JVBERi"),
          "latex: a teximage with the Electron attributes (text, left, top, right, bottom) and a PDF payload");
  Rect fb = im ? imageBox(*im) : Rect{};
  r.check(im && fb[0] >= 2 && near(fb[0], 150, 1e-3) && near(fb[1], 200, 1e-3) && fb[2] > fb[0] && fb[3] > fb[1], "latex: the box starts at the click");
  r.check(ed.tool() == "select" && !ed.returnTo().isEmpty(), "latex: after inserting the editor goes to Seleccionar and remembers the tool before");
  if (!(dynamic_cast<NullSelection*>(&ed.selection()))) r.check(ed.selection().items().size() == 1, "latex: the new formula is selected");
  QElapsedTimer tb;
  tb.start();
  const QImage bmp = texBitmap(*im);
  r.check(!bmp.isNull() && std::abs(double(bmp.width()) / bmp.height() - (fb[2] - fb[0]) / (fb[3] - fb[1])) < 0.05, "latex: the canvas can draw the formula again from its source (proportions match)");
  r.out(QString("latex: canvas bitmap from source %1 ms").arg(tb.elapsed()));
  r.check(texInkColor(im->data) == QColor(0, 0, 0), "latex: the ink colour is recovered from the PDF");

  // reopen with a click (tool latex) and edit
  const Image first = *im;
  const int firstIdx = int(r.ed.doc().pages[size_t(r.page)].layers.back().elements.size()) - 1;
  ed.setTool("latex");
  r.click({(fb[0] + fb[2]) / 2, (fb[1] + fb[3]) / 2});
  r.check(formulaDialogOpen() && edit->toPlainText() == "\\alpha + \\beta" && ok->text() == "Aplicar cambios" && !child<QCheckBox>(dlg, "ltxImage")->isEnabled(), "latex: clicking a formula re-opens it with its source and 'Aplicar cambios'");
  edit->setPlainText("x^2");
  r.wait(160);
  r.key(edit, Qt::Key_Return, Qt::ControlModifier);
  r.pump();
  im = r.lastImage();
  Rect eb = im ? imageBox(*im) : Rect{};
  r.check(im && attrOr(im->attrs, "text") == "x^2" && im->data != first.data && near(eb[0], fb[0]) && near(eb[1], fb[1]) && eb[2] - eb[0] < fb[2] - fb[0] && r.count() == n0 + 1, "latex: editing re-renders in place (same corner, new size, same element)");
  ed.undo();
  im = r.lastImage();
  r.check(im && *im == first, "latex: undo restores the previous source, picture and box exactly");
  ed.redo();
  r.check(r.lastImage() && attrOr(r.lastImage()->attrs, "text") == "x^2", "latex: redo applies the edit");
  ed.undo();

  // Esc cancels
  ed.setTool("latex");
  r.click({(fb[0] + fb[2]) / 2, (fb[1] + fb[3]) / 2});
  edit->setPlainText("zzz");
  r.key(edit, Qt::Key_Escape);
  r.check(!formulaDialogOpen() && r.count() == n0 + 1 && attrOr(r.lastImage()->attrs, "text") == "\\alpha + \\beta", "latex: Esc cancels and changes nothing");

  // double click on the selected formula (tool Seleccionar) re-opens it
  if (!dynamic_cast<NullSelection*>(&ed.selection())) {
    const int lastLayer = int(r.ed.doc().pages[size_t(r.page)].layers.size()) - 1;
    ed.setTool("select");
    ed.selection().select(r.page, {ElementRef{r.page, lastLayer, firstIdx}});
    const QPointF c((fb[0] + fb[2]) / 2, (fb[1] + fb[3]) / 2);
    r.mouse(QEvent::MouseButtonDblClick, c, Qt::LeftButton, Qt::LeftButton);
    r.mouse(QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton);
    r.pump();
    r.check(formulaDialogOpen() && edit->toPlainText() == "\\alpha + \\beta" && ok->text() == "Aplicar cambios", "latex: double click on the selected formula re-opens it for editing");
    r.key(edit, Qt::Key_Escape);
  }

  // a turned and mirrored formula keeps its pose when edited, and is hit where it is drawn
  {
    const int layer = int(r.ed.doc().pages[size_t(r.page)].layers.size()) - 1;
    Image turned = *r.lastImage();
    setTexPose(turned, {M_PI / 2, true});
    r.ed.replaceElements(r.page, layer, firstIdx, 1, {Element(turned)});
    const double cx = (fb[0] + fb[2]) / 2, cy = (fb[1] + fb[3]) / 2, hw = (fb[2] - fb[0]) / 2;
    ed.setTool("latex");
    r.click({cx, cy + hw * 0.9});   // inside the turned picture, outside the upright box
    r.check(formulaDialogOpen(), "latex: a turned formula is re-opened by a click where it is drawn");
    edit->setPlainText("y^2");
    r.wait(160);
    r.key(edit, Qt::Key_Return, Qt::ControlModifier);
    r.pump();
    const TexPose pose = r.lastImage() ? texPose(*r.lastImage()) : TexPose{};
    r.check(r.lastImage() && near(pose.rot, M_PI / 2) && pose.flip && attrOr(r.lastImage()->attrs, "text") == "y^2", "latex: editing keeps xlRot and xlFlip");
    ed.undo();   // the edit; the pose itself was put there by hand
    r.ed.replaceElements(r.page, layer, firstIdx, 1, {Element(first)});
  }

  // "guardar como imagen simple"
  ed.setTool("latex");
  r.click({150, 300});
  edit->setPlainText("\\sqrt{2}");
  child<QRadioButton>(dlg, "ltxInline")->setChecked(true);
  child<QCheckBox>(dlg, "ltxImage")->setChecked(true);
  r.wait(160);
  r.key(edit, Qt::Key_Return, Qt::ControlModifier);
  r.pump();
  r.check(r.lastImage() && r.lastImage()->tag == "image" && r.lastImage()->data.startsWith("iVBOR") && attrFind(r.lastImage()->attrs, "text") == nullptr, "latex: 'Guardar como imagen simple' stores a plain PNG image");
  ed.undo();

  // display and size are remembered for the next formula
  ed.setTool("latex");
  r.click({150, 340});
  auto* inl = child<QRadioButton>(dlg, "ltxInline");
  auto* sz = child<QComboBox>(dlg, "ltxSize");
  r.check(inl && inl->isChecked() && sz && sz->currentText() == "14", "latex: the last display mode and size are kept");
  sz->setCurrentIndex(4);
  edit->setPlainText("\\int_0^1 x\\,dx");
  r.wait(160);

  // formula in the page next to texts, and dialog screenshots
  edit->setPlainText("\\oint_C \\vec F\\cdot d\\vec r=\\iint_S (\\nabla\\times\\vec F)\\cdot d\\vec S");
  r.wait(200);
  tabs->setCurrentIndex(vc + 1);
  dlg->resize(900, 720);
  r.pump(8);
  r.wait(100);
  dlg->grab().save(shots + "/texttools_formula_dialog.png");
  r.key(dlg, Qt::Key_Escape);
  r.check(!formulaDialogOpen(), "latex: the dialog is closed with Esc");
  while (r.count() > n0) ed.undo();
}

// a page with texts (plain and markdown), formulas, shapes and a picture, drawn by the real renderer, for a look by eye
void showcase(Rig& r, const QString& shots) {
  Editor& ed = r.ed;
  auto text = [&](double x, double y, double size, const QString& color, const QString& body, bool md) {
    Text t;
    t.attrs = {{"font", "Sans"}, {"size", fmt8(size)}, {"x", fmt8(x)}, {"y", fmt8(y)}, {"color", color}};
    if (md) t.attrs.emplace_back("xlMd", "1");
    t.text = body;
    ed.commitAdd(r.page, 0, Element(std::move(t)), "Texto");
  };
  text(60, 60, 20, "#1a237eff", R"(# Texto con Markdown
**negrita**, *cursiva*, ~~tachado~~ y `código`
- primer punto
- segundo punto
1. uno
2. dos
> una cita
---)", true);
  text(60, 250, 14, "#000000ff", "Texto plano de dos líneas\nsegunda línea", false);
  const QStringList formulas = {R"(\frac{-b\pm\sqrt{b^2-4ac}}{2a})", R"(\int_0^\infty e^{-x^2}\,dx=\frac{\sqrt\pi}{2})",
                                R"(\begin{pmatrix}a&b\\c&d\end{pmatrix}\begin{pmatrix}x\\y\end{pmatrix})",
                                R"(\oint_C \vec F\cdot d\vec r=\iint_S(\nabla\times\vec F)\cdot d\vec S)"};
  double y = 330;
  const QColor colors[] = {QColor("#000000"), QColor("#d32f2f"), QColor("#1565c0"), QColor("#2e7d32")};
  for (int i = 0; i < formulas.size(); i++) {
    TexImageResult t = makeTexImage(formulas[i], i == 3 ? 12 : 16, true, colors[i], 60, y);
    if (!t.ok) { r.check(false, "showcase formula: " + t.error); continue; }
    ed.commitAdd(r.page, 0, Element(std::move(t.el)), "Fórmula");
    y += 70;
  }
  Image im;
  im.tag = "image";
  QImage pic(240, 140, QImage::Format_ARGB32);
  pic.fill(QColor("#cfe8ff"));
  { QPainter p(&pic); p.setRenderHint(QPainter::Antialiasing); p.setBrush(QColor("#ffb300")); p.setPen(Qt::NoPen); p.drawEllipse(QPoint(60, 45), 25, 25); p.setBrush(QColor("#43a047")); p.drawPolygon(QPolygon({QPoint(0, 140), QPoint(90, 60), QPoint(150, 110), QPoint(200, 70), QPoint(240, 140)})); }
  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  pic.save(&buf, "PNG");
  im.data = QString::fromLatin1(png.toBase64());
  setImageBox(im, {330, 330, 330 + 180, 330 + 105});
  ed.commitAdd(r.page, 0, Element(std::move(im)), "Imagen");
  ed.setTool("shape");
  state().shape = ShapeKind::Ellipse;
  r.drag({330, 470}, {430, 520});
  state().shape = ShapeKind::Arrow;
  r.drag({440, 495}, {520, 495});
  state().shape = ShapeKind::Rect;
  r.drag({330, 540}, {430, 590});
  ed.setTool("pen");
  r.v.setZoom(1.2);
  r.v.scrollToPage(0);
  r.pump(8);
  r.v.renderVisibleNow();
  r.pump(4);
  r.win.grab().save(shots + "/texttools_page.png");
  r.check(true, "showcase page saved to texttools_page.png (look at it)");
}

}  // namespace

void runTextToolsSelfTest(MainWindow& win, Editor& ed, const CheckFn& check, const OutFn& out, const QString& shotsDir) {
  out("\n== text, formulas, images and shapes (synthetic events; not a physical pen) ==");
  QTemporaryDir tmp;
  QDir().mkpath(shotsDir);
  ed.setDocument(blankDocument());
  DocumentView& v = *win.view();
  v.zoomFitWidth();
  v.scrollToPage(0);
  QApplication::processEvents();
  Rig r{win, ed, v, check, out};
  r.pump(8);
  const QString shots = shotsDir;
  textChecks(r, shots);
  shapeChecks(r);
  recognizeChecks(r);
  imageChecks(r, tmp.path());
  formulaChecks(r, shots);
  showcase(r, shots);
  ed.setTool("pen");
  ed.setRecognizeShapes(false);
}

}  // namespace texttools
