#include "selection_selftest.h"
#include "docfactory.h"
#include "documentview.h"
#include "editor.h"
#include "generated.h"
#include "genobj.h"
#include "mainwindow.h"
#include "selectionimpl.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <random>

namespace selx {

namespace {

using Check = std::function<void(bool, const QString&)>;
using Out = std::function<void(const QString&)>;

double nowMs() { static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }(); return double(t.nsecsElapsed()) / 1e6; }
void pump(int n = 3) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
/** What the event loop does during a held gesture: the 8 ms flush timer re-renders the dirty visible tiles (the queue of lazy tiles waits for the end). */
double flushDuringGesture() {
  const double t0 = nowMs();
  QThread::msleep(12);
  QApplication::processEvents(QEventLoop::AllEvents, 50);
  return nowMs() - t0;
}
double settle(DocumentView& v, int timeoutMs = 30000) {
  const double t0 = nowMs();
  while (nowMs() - t0 < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!v.hasPendingTiles()) break;
    QThread::msleep(1);
  }
  return nowMs() - t0;
}

/** A fake generated object ("fake"): three strokes with role "fakeseg"; anchor 0 is the last point of the first piece. */
class FakeHandler : public GeneratedHandler {
 public:
  QString kind() const override { return QStringLiteral("fake"); }
  bool claims(const ElementPtrs& m) const override {
    if (m.size() != 3) return false;
    for (const Element* e : m) if (roleOf(*e) != std::optional<QString>("fakeseg")) return false;
    return true;
  }
  std::vector<QPointF> anchors(const ElementPtrs& m) const override {
    const Stroke& s = std::get<Stroke>(*m[0]);
    return {QPointF(s.pts[s.pts.size() - 2], s.pts[s.pts.size() - 1])};
  }
  std::optional<std::vector<Element>> dragAnchor(const ElementPtrs& m, const QString&, int, QPointF p, bool) const override {
    std::vector<Element> out;
    for (const Element* e : m) out.push_back(*e);
    Stroke& s = std::get<Stroke>(out[0]);
    s.pts[s.pts.size() - 2] = p.x();
    s.pts[s.pts.size() - 1] = p.y();
    return out;
  }
  std::optional<std::vector<Element>> rebuildAfterTransform(const ElementPtrs& m, const QString&) const override {
    std::vector<Element> out;
    for (const Element* e : m) { out.push_back(*e); attrSet(attrsOf(out.back()), "xlRebuilt", "1"); }
    return out;
  }
};

Stroke line(double x0, double y0, double x1, double y1, double w = 2.0) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "round"}};
  s.widths = {w};
  s.pts = {x0, y0, (x0 + x1) / 2, (y0 + y1) / 2, x1, y1};
  return s;
}

struct Drv {
  DocumentView& v;
  int page = 0;
  QPointF vp(const QPointF& p) const { return v.toView(page, p); }
  void send(QEvent::Type t, const QPointF& p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m) {
    const QPointF w = vp(p);
    QMouseEvent ev(t, w, v.viewport()->mapToGlobal(w), b, bs, m);
    QApplication::sendEvent(v.viewport(), &ev);
  }
  void press(const QPointF& p, Qt::KeyboardModifiers m = {}) { send(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, m); }
  void move(const QPointF& p, Qt::KeyboardModifiers m = {}) { send(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton, m); }
  void release(const QPointF& p, Qt::KeyboardModifiers m = {}) { send(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, m); }
  void hover(const QPointF& p) { send(QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton, {}); }
  void drag(const QPointF& a, const QPointF& b, int steps = 4, Qt::KeyboardModifiers m = {}) {
    press(a, m);
    for (int k = 1; k <= steps; k++) move(a + (b - a) * (double(k) / steps), m);
    release(b, m);
  }
  void key(int k, Qt::KeyboardModifiers m = {}) {
    QKeyEvent ev(QEvent::KeyPress, k, m);
    QApplication::sendEvent(&v, &ev);
  }
};

bool near(double a, double b, double t = 1e-6) { return std::abs(a - b) < t; }

}  // namespace

void runSelectionChecks(MainWindow& win, Editor& ed, const Check& check, const Out& out, const QString& shots) {
  out("\n== selection checks (real Qt mouse and key events on the view; not a physical pen) ==");
  DocumentView& v = *win.view();
  Drv d{v};
  GeneratedRegistry::instance().add(std::make_unique<FakeHandler>());

  // ---- fixture: page 0 = 12 diagonal strokes (4 columns x 3 rows), a fake generated object (3 pieces), one text; page 1 stays empty for the 20000 test
  Document doc = blankDocument();
  appendPage(doc);
  doc.pages[0].layers.reserve(4);   // the hidden-layer check below adds a layer: the references into this vector must stay valid
  {
    Layer& L = doc.pages[0].layers[0];
    for (int i = 0; i < 12; i++) { const double x = 60 + (i % 4) * 50, y = 60 + (i / 4) * 50; L.elements.emplace_back(line(x, y, x + 30, y + 30)); }
    L.elements.emplace_back(strokeEl({300, 100, 400, 160}, "#000000", "ff", 2.0, "fakeseg", "plTEST1"));
    L.elements.emplace_back(strokeEl({300, 160, 400, 100}, "#000000", "ff", 2.0, "fakeseg", "plTEST1"));
    L.elements.emplace_back(strokeEl({300, 130, 400, 130}, "#000000", "ff", 2.0, "fakeseg", "plTEST1"));
    Text t;
    t.text = QStringLiteral("Hola");
    t.attrs = {{"font", "Sans"}, {"size", "14"}, {"x", "300"}, {"y", "300"}, {"color", "#000000ff"}};
    L.elements.emplace_back(std::move(t));
  }
  ed.setDocument(std::move(doc));
  win.resize(1400, 950);
  win.show();
  v.setZoom(1.0);
  v.scrollToPage(0);
  settle(v);
  pump();
  auto* sel = RealSelection::of(ed);
  check(sel != nullptr, "the editor's selection is the real one (RealSelection)");
  if (!sel) return;
  Page& pg = ed.doc().pages[0];
  auto& els = pg.layers[0].elements;
  auto snap = [&] { return std::vector<Element>(els.begin(), els.end()); };
  const std::vector<Element> pristine = snap();
  auto S = [&](size_t i) -> Stroke& { return std::get<Stroke>(els[i]); };
  const int total = int(els.size());   // 16

  // ---- selecting: rectangle, click, lasso, plane as one object
  ed.setTool("select");
  d.drag({5, 5}, {260, 100}, 6);
  check(sel->count() == 4, QString("rectangle selects the 4 strokes of the top row (got %1)").arg(sel->count()));
  check(ed.selection().page() == 0 && !ed.selection().bbox().isNull(), "selection reports its page and box");
  d.press({125, 125}); d.release({125, 125});
  check(sel->count() == 1 && sel->contains({0, 0, 5}), "a click selects the topmost element under the pointer (stroke 5)");
  d.press({700, 700}); d.release({700, 700});
  check(sel->isEmpty(), "a click on empty paper clears the selection");
  d.press({350, 130}); d.release({350, 130});
  check(sel->count() == 3 && sel->generatedKind() == "fake", "clicking one piece of a generated object selects the whole object (all xlPlane members), claimed by its handler");
  d.drag({280, 90}, {330, 120}, 3);   // rectangle that only touches part of it
  check(sel->count() == 3, "a rectangle that touches part of a generated object selects it whole");
  ed.setTool("lasso");
  {
    const std::vector<QPointF> poly = {{5, 5}, {260, 5}, {260, 100}, {5, 100}};
    d.press(poly[0]);
    for (size_t i = 1; i < poly.size(); i++) d.move(poly[i]);
    d.release(poly[0]);
  }
  check(sel->count() == 4, QString("lasso selects the strokes with a point inside (got %1)").arg(sel->count()));
  ed.setTool("select");

  // ---- move, undo, redo
  d.drag({5, 5}, {260, 100}, 6);
  const Rect b0 = {sel->bbox().left(), sel->bbox().top(), sel->bbox().right(), sel->bbox().bottom()};
  const QPointF centre((b0[0] + b0[2]) / 2, (b0[1] + b0[3]) / 2);
  const auto before = snap();
  const std::vector<double> p0 = S(0).pts;
  const int depth0 = ed.undoDepth();
  d.press(centre);
  for (int k = 1; k <= 3; k++) d.move(centre + QPointF(10.0 * k, 40.0 / 3 * k));
  check(els.size() == size_t(total) - 4, "while dragging the selected strokes are lifted out of the document (drawn from the cached pixmap)");
  flushDuringGesture();
  {
    // the tiles under the lifted strokes are re-rendered without them, so no ghost stays at the old place while the preview moves
    const QImage img = v.viewport()->grab().toImage();
    const QPointF oldPos = v.toView(0, QPointF(75, 75)), newPos = v.toView(0, QPointF(105, 115));
    const int lumOld = qGray(img.pixel(int(oldPos.x()), int(oldPos.y()))), lumNew = qGray(img.pixel(int(newPos.x()), int(newPos.y())));
    check(lumOld > 235, QString("while dragging, nothing is left behind at the old place (luminance %1)").arg(lumOld));
    check(lumNew < 120, QString("while dragging, the preview is drawn at the new place (luminance %1)").arg(lumNew));
  }
  d.release(centre + QPointF(30, 40));
  check(els.size() == size_t(total), "released: they are back in the document");
  check(near(S(0).pts[0], p0[0] + 30) && near(S(0).pts[1], p0[1] + 40), "move by (30, 40)");
  check(ed.undoDepth() == depth0 + 1, "the whole drag is one undo step");
  check(sel->count() == 4 && sel->contains({0, 0, 3}), "the moved elements stay selected");
  ed.undo();
  check(snap() == before, "undo restores the exact elements");
  check(sel->count() == 4, "the selection survives undo");
  ed.redo();
  check(near(S(0).pts[0], p0[0] + 30), "redo moves again");
  ed.undo();

  // a click on the frame without dragging changes nothing and pushes nothing
  const int depthClick = ed.undoDepth();
  d.press(centre); d.release(centre);
  check(ed.undoDepth() == depthClick && snap() == before, "a click on the selected body is not a move");

  // ---- arrow keys, delete, undo
  v.setFocus();
  d.key(Qt::Key_Right);
  check(near(S(0).pts[0], p0[0] + 1) && near(S(0).pts[1], p0[1]), "arrow key nudges the selection by one unit");
  d.key(Qt::Key_Down, Qt::ShiftModifier);
  check(near(S(0).pts[1], p0[1] + 10), "Shift+arrow nudges by ten units");
  ed.undo();
  check(snap() == before, "the arrow-key steps of one burst are ONE undo step (repeat nudges fold into the first)");
  d.key(Qt::Key_Delete);
  check(els.size() == size_t(total) - 4 && sel->isEmpty(), "Delete removes the selection");
  ed.undo();
  check(snap() == before && els.size() == size_t(total), "undo of delete puts every element back where it was");
  ed.action("edit.selectAll")->trigger();
  check(sel->count() == total, QString("Select all selects every visible element of the page (%1 of %2)").arg(sel->count()).arg(total));
  ed.action("edit.delete")->trigger();
  check(els.empty(), "edit.delete command deletes");
  ed.undo();
  check(snap() == before, "undo restores after select-all delete");
  sel->clear();

  // ---- resize by the SE handle (corner: in proportion), undo, redo; mirror past the opposite side; edge handle stretches freely
  d.drag({5, 5}, {260, 100}, 6);
  {
    const QRectF bb = sel->bbox();
    const double rx0 = bb.left(), ry0 = bb.top(), rx1 = bb.right(), ry1 = bb.bottom();
    const std::vector<double> op = S(0).pts;
    const double w0 = S(0).widths[0];
    d.press({rx1, ry1});
    for (int k = 1; k <= 4; k++) d.move({rx1 + 0.125 * k * (rx1 - rx0), ry1 + 0.125 * k * (ry1 - ry0)});
    d.release({rx1 + 0.5 * (rx1 - rx0), ry1 + 0.5 * (ry1 - ry0)});
    check(near((S(0).pts[0] - rx0) / (op[0] - rx0), 1.5, 1e-6), "SE handle scales by 1.5 (x)");
    check(near(S(0).widths[0] / w0, 1.5, 1e-6), "line width follows the scale factor");
    check(els.size() == size_t(total), "resize keeps the element count");
    ed.undo();
    check(snap() == before, "resize: undo is exact");
    ed.redo();
    check(near((S(0).pts[0] - rx0) / (op[0] - rx0), 1.5, 1e-6), "resize: redo");
    ed.undo();
    // edge handle E stretches only x; dragging it past the fixed side mirrors
    const QRectF b1 = sel->bbox();
    const double midY = (b1.top() + b1.bottom()) / 2;
    d.drag({b1.right(), midY}, {b1.left() - 100, midY}, 4);
    check(S(0).pts[0] < b1.left() && near(S(0).pts[1], op[1]), "dragging the E handle past the opposite side mirrors horizontally (y untouched)");
    ed.undo();
    check(snap() == before, "mirror by drag: undo exact");
  }

  // ---- quick mirror / rotate by 90 (commands and keys)
  {
    const QRectF bb = sel->bbox();
    const double cx = bb.center().x(), cy = bb.center().y();
    const std::vector<double> o = S(0).pts;
    const double ow = S(0).widths[0];
    ed.action("sel.flipH")->trigger();
    check(near(S(0).pts[0], 2 * cx - o[0]) && near(S(0).pts[1], o[1]), "mirror horizontally about the box centre");
    check(near(S(0).widths[0], ow), "mirror keeps the width");
    ed.undo();
    check(snap() == before, "mirror: undo exact");
    ed.action("sel.rotR")->trigger();
    check(near(S(0).pts[0], cx + cy - o[1]) && near(S(0).pts[1], o[0] + cy - cx), "rotate 90 degrees clockwise (formula of the Electron app)");
    ed.undo();
    d.key(Qt::Key_R, Qt::ShiftModifier);
    check(near(S(0).pts[0], cx - cy + o[1]) && near(S(0).pts[1], -o[0] + cy + cx), "Shift+R rotates 90 degrees counter-clockwise");
    ed.undo();
    d.key(Qt::Key_F, Qt::ShiftModifier);
    check(near(S(0).pts[1], 2 * cy - o[1]) && near(S(0).pts[0], o[0]), "Shift+F mirrors vertically");
    ed.undo();
    check(snap() == before, "quick transforms: undo exact");
  }

  // ---- rotate by the round handle with the protractor; Shift snaps to 15 degrees
  {
    const QRectF bb = sel->bbox();
    const QPointF c = bb.center();
    const QPointF rot(c.x(), bb.top() - kRotOffsetPx);   // zoom 1
    const std::vector<double> o = S(0).pts;
    const double R = 80;
    auto at = [&](double ang) { return c + QPointF(std::cos(ang) * R, std::sin(ang) * R); };
    const double a0 = -3.14159265358979323846 / 2;
    d.press(rot);
    for (int k = 1; k <= 3; k++) d.move(at(a0 + k * 3.14159265358979323846 / 6));
    v.viewport()->grab().save(shots + "/selection_rotate.png");
    d.release(at(a0 + 3.14159265358979323846 / 2));
    check(near(S(0).pts[0], c.x() + c.y() - o[1], 1e-3) && near(S(0).pts[1], o[0] + c.y() - c.x(), 1e-3), "rotation handle: a quarter turn of the pointer turns the selection by 90 degrees");
    ed.undo();
    check(snap() == before, "rotation: undo exact");
    d.press(rot);
    d.move(at(a0 + 1.5), Qt::ShiftModifier);
    d.release(at(a0 + 1.5), Qt::ShiftModifier);   // 85.9 degrees -> snapped to 90
    check(near(S(0).pts[0], c.x() + c.y() - o[1], 1e-6) && near(S(0).pts[1], o[0] + c.y() - c.x(), 1e-6), "Shift snaps the rotation to 15 degree steps (86 degrees -> 90)");
    ed.undo();
    // the angle accumulates past 180 degrees
    d.press(rot);
    for (int k = 1; k <= 12; k++) d.move(at(a0 + k * 3.14159265358979323846 / 6));   // a full turn in 12 steps of 30 degrees
    d.release(at(a0 + 2 * 3.14159265358979323846 - 1e-9));
    check(near(S(0).pts[0], o[0], 1e-3) && near(S(0).pts[1], o[1], 1e-3), "a full turn comes back (the angle accumulates beyond 180 degrees)");
    ed.undo();
    check(snap() == before, "full turn: undo exact");
  }
  sel->clear();

  // ---- clipboard: copy / paste / duplicate, cut
  {
    d.drag({5, 5}, {260, 100}, 6);
    const int k = sel->count();
    const std::vector<double> o = S(0).pts;
    ed.action("edit.copy")->trigger();
    ed.action("edit.paste")->trigger();
    check(int(els.size()) == total + k && sel->count() == k, "paste adds the copies and selects them");
    check(near(S(total).pts[0], o[0] + 12) && near(S(total).pts[1], o[1] + 12), "paste on the same page offsets by 12 units diagonally");
    ed.action("edit.paste")->trigger();
    check(near(S(total + k).pts[0], o[0] + 24), "a second paste shifts again");
    ed.undo(); ed.undo();
    check(snap() == before, "paste: undo removes the copies exactly");
    ed.redo(); ed.undo();
    d.drag({5, 5}, {260, 100}, 6);
    ed.action("edit.duplicate")->trigger();
    check(int(els.size()) == total + k, "duplicate adds a copy");
    ed.undo();
    d.drag({5, 5}, {260, 100}, 6);
    ed.action("edit.cut")->trigger();
    check(int(els.size()) == total - k, "cut removes the selection");
    const auto* md = QApplication::clipboard()->mimeData();
    check(md && md->hasFormat("application/x-xournal-lab-elements"), "the system clipboard carries the elements under the app's own mime type");
    ed.action("edit.paste")->trigger();
    check(int(els.size()) == total && sel->count() == k, "paste after cut brings them back");
    ed.undo(); ed.undo();
    check(snap() == before, "cut + paste: both undone exactly");
    sel->clear();

    // a pasted generated object is a NEW object (fresh xlPlane id) and the original keeps its own
    d.press({350, 130}); d.release({350, 130});
    ed.action("edit.copy")->trigger();
    ed.action("edit.paste")->trigger();
    std::set<QString> ids;
    int nfake = 0;
    for (const Element& e : els) if (roleOf(e) == std::optional<QString>("fakeseg")) { nfake++; ids.insert(*planeIdOf(e)); }
    check(nfake == 6 && ids.size() == 2, QString("pasted object has a fresh xlPlane id (%1 pieces, %2 distinct ids)").arg(nfake).arg(ids.size()));
    check(sel->count() == 3 && sel->generatedKind() == "fake" && planeIdOf(els[size_t(total)]) != planeIdOf(els[size_t(total) - 4 + 0]), "the pasted copy is selected as one generated object");
    ed.undo();
    check(snap() == before, "paste of a generated object: undo exact");
    sel->clear();
  }

  // ---- z-order
  {
    sel->select(0, {{0, 0, 5}});
    ed.action("sel.front")->trigger();
    check(near(S(total - 1).pts[0], std::get<Stroke>(before[5]).pts[0]), "bring to front moves the element to the top of its layer");
    check(sel->contains({0, 0, total - 1}) && sel->count() == 1, "the element is still selected after reordering");
    ed.undo();
    check(snap() == before, "front: undo exact");
    ed.action("sel.back")->trigger();
    check(near(S(0).pts[0], std::get<Stroke>(before[5]).pts[0]), "send to back moves the element to the bottom");
    ed.undo();
    check(snap() == before, "back: undo exact");
    sel->clear();
  }

  // ---- glow of the selection
  {
    sel->select(0, {{0, 0, 0}, {0, 0, 1}});
    Style st = ed.style();
    st.glowSize = 10; st.glowIntensity = 0.5;
    ed.setStyle(st);
    ed.action("sel.glowApply")->trigger();
    check(attrFind(S(0).attrs, "glow") && attrFind(S(1).attrs, "glowAlpha") && attrFind(S(0).attrs, "glow")->toDouble() == 10, "apply glow to the selection writes glow/glowAlpha");
    ed.action("sel.glowRemove")->trigger();
    check(!attrFind(S(0).attrs, "glow") && !attrFind(S(1).attrs, "glowAlpha"), "remove glow takes the attributes away");
    ed.undo(); ed.undo();
    check(snap() == before, "glow: both steps undone exactly");
    sel->clear();
  }

  // ---- generated object: anchors, anchor drag (one undo step), rebuild after a resize in the same step
  {
    d.press({350, 130}); d.release({350, 130});
    check(sel->anchors().size() == 1 && near(sel->anchors()[0].x(), 400) && near(sel->anchors()[0].y(), 160), "a selected generated object shows the anchors its handler reports");
    const int dp = ed.undoDepth();
    d.press({400, 160});
    for (int k = 1; k <= 3; k++) d.move({400.0 + 50 * k / 3, 160.0 + 40 * k / 3});
    check(els.size() == size_t(total) - 3, "while an anchor is dragged the object is lifted and the rebuilt version is previewed");
    v.viewport()->grab().save(shots + "/selection_anchor.png");
    d.release({450, 200});
    check(near(S(12).pts[2], 450) && near(S(12).pts[3], 200), "anchor drag: the handler's rebuild replaces the object");
    check(ed.undoDepth() == dp + 1, "anchor drag is exactly one undo step");
    check(sel->count() == 3 && sel->generatedKind() == "fake" && sel->anchors().size() == 1 && near(sel->anchors()[0].x(), 450), "the rebuilt object stays selected, anchors follow");
    ed.undo();
    check(snap() == before, "anchor drag: undo exact");
    check(sel->count() == 3 && near(sel->anchors()[0].x(), 400), "after undo the object is still selected as the object it is");
    ed.redo();
    check(near(S(12).pts[2], 450), "anchor drag: redo");
    ed.undo();
    // resize by a frame handle: rebuildAfterTransform is called and swapped in, same undo step
    const QRectF bb = sel->bbox();
    const int dp2 = ed.undoDepth();
    d.press({bb.left(), bb.top()});
    for (int k = 1; k <= 3; k++) d.move({bb.left() - 51.0 * k / 3, bb.top() - 31.0 * k / 3});
    d.release({bb.left() - 51, bb.top() - 31});
    bool rebuilt = true;
    for (size_t i = 12; i < 15; i++) rebuilt = rebuilt && attrFind(attrsOf(els[i]), "xlRebuilt");
    check(rebuilt, "after a resize of a generated object the handler's rebuild is applied");
    check(ed.undoDepth() == dp2 + 1, "resize + rebuild is one undo step");
    check(sel->count() == 3 && sel->generatedKind() == "fake", "the rebuilt object is selected");
    ed.undo();
    check(snap() == before, "resize + rebuild: undo exact (no rebuilt attribute left)");
    ed.redo();
    check(attrFind(attrsOf(els[13]), "xlRebuilt") != nullptr, "resize + rebuild: redo");
    ed.undo();
    sel->clear();
  }

  // ---- the "wouldHit" contract: with an object just made, a click on empty paper goes back to the tool before
  {
    ed.setTool("pen");
    ed.setTool("select");   // pretend an inserter ran from the pen: afterCreate remembers the pen
    ed.setTool("pen");
    sel->select(0, {{0, 0, 12}, {0, 0, 13}, {0, 0, 14}});
    ed.afterCreate("plane");
    check(ed.tool() == "select" && ed.returnTo() == "pen", "afterCreate switches to select and remembers the tool before");
    check(sel->wouldHit(0, {350, 130}, 1.0) && !sel->wouldHit(0, {700, 700}, 1.0), "wouldHit: true on the object, false on empty paper");
    d.press({350, 130}); d.release({350, 130});
    check(ed.tool() == "select" && sel->count() == 3, "a click on the object stays in select and keeps the selection");
    const size_t n = els.size();
    d.press({700, 700}); d.release({700, 700});
    check(ed.tool() == "pen" && sel->isEmpty(), "a click on empty paper returns to the previous tool");
    while (els.size() > n) { ed.undo(); }   // the pen dot
    ed.setTool("select");
    sel->select(0, {{0, 0, 12}});
    ed.escape();
    check(sel->isEmpty(), "Esc clears the selection");
  }

  // ---- hidden layers are not selectable
  {
    Layer extra;
    extra.elements.emplace_back(line(10, 400, 40, 430));
    ed.doc().pages[0].layers.push_back(extra);
    ed.invalidateIndex(0);
    ed.setLayerVisible(0, 1, false);
    ed.action("edit.selectAll")->trigger();
    check(sel->count() == total, "select all skips hidden layers");
    d.drag({0, 390}, {60, 440}, 3);
    check(sel->isEmpty(), "rectangle selection skips hidden layers");
    ed.setLayerVisible(0, 1, true);
    d.drag({0, 390}, {60, 440}, 3);
    check(sel->count() == 1, "the same rectangle selects once the layer is visible");
    sel->clear();
    ed.doc().pages[0].layers.pop_back();
    ed.invalidateIndex(0);
    ed.setLayerVisible(0, 1, true);
  }

  // ---- vertical space
  {
    const double lineY = 95;
    auto topOf = [](const Element& e) { return elementBBox(e)[1]; };
    auto botOf = [](const Element& e) { return elementBBox(e)[3]; };
    std::vector<size_t> below, above;
    for (size_t i = 0; i < els.size(); i++) (topOf(els[i]) >= lineY ? below : above).push_back(i);
    ed.setTool("vspace");
    auto vdrag = [&](double dy) {
      const int dp = ed.undoDepth();
      d.press({40, lineY});
      for (int k = 1; k <= 4; k++) d.move({40, lineY + dy * k / 4});
      d.release({40, lineY + dy});
      return ed.undoDepth() == dp + 1;
    };
    bool one = vdrag(30);
    bool moves = !below.empty(), keeps = true;
    for (size_t i : below) moves = moves && near(topOf(els[i]), elementBBox(before[i])[1] + 30);
    for (size_t i : above) keeps = keeps && els[i] == before[i];
    check(one && moves && keeps, QString("vertical space: everything below the line moves by 30, the rest stays, one undo step (%1 below, %2 above)").arg(below.size()).arg(above.size()));
    check(sel->isEmpty(), "vertical space leaves no selection");
    ed.undo();
    check(snap() == before, "vertical space: undo exact");
    one = vdrag(100000);
    double maxBot = 0;
    for (size_t i : below) maxBot = std::max(maxBot, botOf(els[i]));
    check(one && maxBot <= pg.height() + 1e-6 && maxBot > pg.height() - 1, "vertical space cannot be pushed off the bottom of the page");
    ed.undo();
    one = vdrag(-100000);
    double minTop = 1e9;
    for (size_t i : below) minTop = std::min(minTop, topOf(els[i]));
    check(one && near(minTop, lineY), "vertical space cannot be squeezed into the line");
    ed.undo();
    check(snap() == before, "vertical space: final state exact");
    const int dp = ed.undoDepth();
    d.press({40, 841}); d.release({40, 841});
    check(ed.undoDepth() == dp && snap() == before && sel->isEmpty(), "nothing below the line: harmless (no undo step, no selection)");
    ed.setTool("select");
  }

  // ---- hover cursors
  {
    d.drag({5, 5}, {260, 100}, 6);
    const QRectF bb = sel->bbox();
    d.hover({bb.right(), bb.bottom()});
    check(v.viewport()->cursor().shape() == Qt::SizeFDiagCursor, "hovering the SE handle shows the diagonal resize cursor");
    d.hover(bb.center());
    check(v.viewport()->cursor().shape() == Qt::SizeAllCursor, "hovering the body shows the move cursor");
    d.hover({700, 700});
    check(v.viewport()->cursor().shape() == Qt::ArrowCursor, "hovering empty paper shows the arrow");
    v.renderVisibleNow();
    v.viewport()->grab().save(shots + "/selection_frame.png");
    sel->clear();
  }
  check(snap() == before && snap() == pristine, "every functional check above left the document as it was");

  // ---- 20000 strokes: the measurement
  {
    out("\n-- 20000 strokes on page 1 (cold: tiles are not warmed) --");
    ed.setTool("select");
    Page& p1 = ed.doc().pages[1];
    Layer& L = p1.layers.back();
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> ux(20, p1.width() - 20), uy(20, p1.height() - 20), ua(0, 6.2831), ud(-0.25, 0.25);
    L.elements.reserve(20000);
    for (int i = 0; i < 20000; i++) {
      Stroke s = line(0, 0, 1, 1);
      s.pts.clear();
      double x = ux(rng), y = uy(rng), a = ua(rng);
      for (int k = 0; k < 60; k++) { a += ud(rng); x = std::clamp(x + std::cos(a) * 2.5, 5.0, p1.width() - 5); y = std::clamp(y + std::sin(a) * 2.5, 5.0, p1.height() - 5); s.pts.push_back(x); s.pts.push_back(y); }
      L.elements.emplace_back(std::move(s));
    }
    ed.invalidateIndex(1);
    ed.touch(1);
    d.page = 1;
    v.scrollToPage(1);
    settle(v);
    const int N = int(L.elements.size());
    auto fmt = [](double x) { return QString::number(x, 'f', 1); };
    // click select
    double t0 = nowMs();
    d.press({300, 400}); d.release({300, 400});
    out(QString("click select (index point query, plane expansion): %1 ms, %2 selected").arg(fmt(nowMs() - t0)).arg(sel->count()));
    // rectangle selection of half the page
    sel->clear();
    t0 = nowMs();
    d.press({5, 5}); d.move({590, 420}); d.release({590, 420});
    const double tRect = nowMs() - t0;
    const int half = sel->count();
    out(QString("rectangle select of the top half: %1 ms, %2 strokes").arg(fmt(tRect)).arg(half));
    // everything
    sel->clear();
    t0 = nowMs();
    d.press({2, 2}); d.move({593, 840}); d.release({593, 840});
    out(QString("rectangle select of the whole page: %1 ms, %2 strokes").arg(fmt(nowMs() - t0)).arg(sel->count()));
    check(sel->count() == N, "rectangle over the whole page selects all 20000 strokes");
    // lasso
    sel->clear();
    ed.setTool("lasso");
    t0 = nowMs();
    d.press({5, 5}); d.move({590, 5}); d.move({590, 420}); d.move({5, 420}); d.release({5, 5});
    out(QString("lasso select of the top half: %1 ms, %2 strokes").arg(fmt(nowMs() - t0)).arg(sel->count()));
    ed.setTool("select");

    // move of the top half (about 10000 strokes), event by event with a paint after each (what the event loop does)
    sel->clear();
    d.press({5, 5}); d.move({590, 420}); d.release({590, 420});
    {
      const QRectF bb = sel->bbox();
      const QPointF c = bb.center();
      v.resetStats();
      const auto beforeBig = std::vector<Element>(L.elements.begin(), L.elements.end());
      d.press(c);
      QElapsedTimer tm;
      tm.start();
      d.move(c + QPointF(4, 4));
      const double tLift = double(tm.nsecsElapsed()) / 1e6;
      pump();
      const double tPaint1 = double(tm.nsecsElapsed()) / 1e6 - tLift;
      const double tTiles = flushDuringGesture();
      std::vector<double> ev;
      for (int k = 2; k <= 60; k++) {
        tm.restart();
        d.move(c + QPointF(4.0 * k, 3.0 * k));
        pump(1);
        ev.push_back(double(tm.nsecsElapsed()) / 1e6);
      }
      std::sort(ev.begin(), ev.end());
      double mean = 0;
      for (double x : ev) mean += x;
      mean /= double(ev.size());
      out(QString("move of %1 strokes: first move (lift = pixmap + removal from the document) %2 ms, its first paint %3 ms, tile re-render that follows %4 ms (%5 tiles, %6 ms in renderTile)").arg(half).arg(fmt(tLift)).arg(fmt(tPaint1)).arg(fmt(tTiles)).arg(v.stats().tilesRendered).arg(fmt(v.stats().renderMs)));
      v.resetStats();
      out(QString("  then 59 moves (event + overlay paint): mean %1 ms, p50 %2 ms, max %3 ms; worst paintEvent %4 ms").arg(fmt(mean)).arg(fmt(ev[ev.size() / 2])).arg(fmt(ev.back())).arg(fmt(v.stats().worstPaintMs)));
      tm.restart();
      d.release(c + QPointF(240, 180));
      const double tCommit = double(tm.nsecsElapsed()) / 1e6;
      const double tTiles2 = settle(v);
      out(QString("  release (transform %1 strokes, one Action, reinsert): %2 ms, tile re-render that follows %3 ms (%4 tiles, %5 ms in renderTile)").arg(half).arg(fmt(tCommit)).arg(fmt(tTiles2)).arg(v.stats().tilesRendered).arg(fmt(v.stats().renderMs)));
      tm.restart();
      ed.undo();
      const double tUndo = double(tm.nsecsElapsed()) / 1e6;
      settle(v);
      check(std::vector<Element>(L.elements.begin(), L.elements.end()) == beforeBig, "20000 strokes: undo of moving 10000 of them is exact");
      tm.restart();
      ed.redo();
      const double tRedo = double(tm.nsecsElapsed()) / 1e6;
      settle(v);
      ed.undo();
      settle(v);
      out(QString("  undo %1 ms, redo %2 ms (model + index; the tiles are re-rendered after, as above)").arg(fmt(tUndo)).arg(fmt(tRedo)));
      check(sel->count() == half, "the selection survives the undo of the move");
    }
    // resize of the same selection by the SE handle
    {
      const QRectF bb = sel->bbox();
      v.resetStats();
      QElapsedTimer tm;
      d.press({bb.right(), bb.bottom()});
      tm.start();
      d.move({bb.right() + 5, bb.bottom() + 5});
      const double tLift = double(tm.nsecsElapsed()) / 1e6;
      pump();
      flushDuringGesture();
      std::vector<double> ev;
      for (int k = 2; k <= 40; k++) {
        tm.restart();
        d.move({bb.right() + 3.0 * k, bb.bottom() + 3.0 * k});
        pump(1);
        ev.push_back(double(tm.nsecsElapsed()) / 1e6);
      }
      std::sort(ev.begin(), ev.end());
      out(QString("resize of %1 strokes: first move %2 ms; 39 moves p50 %3 ms, max %4 ms").arg(half).arg(fmt(tLift)).arg(fmt(ev[ev.size() / 2])).arg(fmt(ev.back())));
      tm.restart();
      d.release({bb.right() + 120, bb.bottom() + 120});
      out(QString("  release: %1 ms").arg(fmt(double(tm.nsecsElapsed()) / 1e6)));
      settle(v);
      ed.undo();
      settle(v);
    }
    sel->clear();
    // leave the page light again
    L.elements.clear();
    ed.invalidateIndex(1);
    ed.touch(1);
    d.page = 0;
    v.scrollToPage(0);
    settle(v);
  }
}

}  // namespace selx
