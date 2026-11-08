// Self test of the planes / 3D spaces / figures area (called from --selftest). It mirrors the Electron e2e assertions (app.ts: PL, S3, FIG) with the counts
// already verified in tests/plane_figures.cpp, but drives the REAL path: synthetic mouse events on the DocumentView viewport -> Editor -> tool -> Hub ->
// swapGenerated, the option panels through their widgets, the handlers by direct calls. Synthetic events are not a physical pen.
#include "documentview.h"
#include "generated.h"
#include "hub.h"
#include "mainwindow.h"
#include "objects.h"
#include "prefs.h"
#include <QApplication>
#include <QDir>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

namespace xlobjects {
namespace {

// A stand-in for the selection while the real one is not installed: it only remembers what was selected. When the real selection is there, it is used.
class RecordingSelection final : public Selection {
 public:
  bool isEmpty() const override { return items_.empty(); }
  void clear() override { items_.clear(); page_ = -1; }
  void select(int page, const std::vector<ElementRef>& items) override { items_ = items; page_ = items.empty() ? -1 : page; }
  bool contains(const ElementRef& e) const override { return std::find(items_.begin(), items_.end(), e) != items_.end(); }
  std::vector<ElementRef> items() const override { return items_; }
  int page() const override { return page_; }
  QRectF bbox() const override { return {}; }
  bool wouldHit(int, const QPointF&, double) const override { return false; }
  void refresh() override { clear(); }   // positions moved with the undo: the test selects again by id
  void paintOverlay(QPainter&, const OverlayInfo&) override {}
 private:
  std::vector<ElementRef> items_;
  int page_ = -1;
};

struct Rig {
  MainWindow& win;
  Editor& ed;
  DocumentView& v;
  Hub& hub;
  const SelfTestIO& io;
  int page = 0;

  void pump(int n = 4) { for (int i = 0; i < n; i++) QApplication::processEvents(); }
  QPointF view(const QPointF& p) const { return v.toView(page, p); }
  void mouse(QEvent::Type t, const QPointF& p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m = Qt::NoModifier) {
    QMouseEvent ev(t, view(p), v.viewport()->mapToGlobal(view(p)), b, bs, m);
    QApplication::sendEvent(v.viewport(), &ev);
  }
  void drag(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers m = Qt::NoModifier, int steps = 8) {
    mouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, m);
    for (int i = 1; i <= steps; i++) mouse(QEvent::MouseMove, a + (b - a) * (double(i) / steps), Qt::NoButton, Qt::LeftButton, m);
    mouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, m);
  }
  void click(const QPointF& p) { drag(p, p, Qt::NoModifier, 0); }
  void key(int k) {
    QKeyEvent ev(QEvent::KeyPress, k, Qt::NoModifier);
    QApplication::sendEvent(&v, &ev);
  }

  int layer() const { return ed.activeLayer(page); }
  const std::vector<Element>& els() const { return ed.doc().pages[size_t(page)].layers[size_t(layer())].elements; }
  std::vector<const Element*> mine() const {
    std::vector<const Element*> out;
    for (const Element& e : els()) if (planeIdOf(e)) out.push_back(&e);
    return out;
  }
  size_t count(const QString& role) const {
    size_t n = 0;
    for (const Element* e : mine()) if (roleOf(*e) == std::optional<QString>(role)) n++;
    return n;
  }
  size_t ids() const {
    std::vector<QString> seen;
    for (const Element* e : mine()) { const QString id = *planeIdOf(*e); if (std::find(seen.begin(), seen.end(), id) == seen.end()) seen.push_back(id); }
    return seen.size();
  }
  const Stroke* firstStroke(const QString& role) const {
    for (const Element* e : mine()) if (roleOf(*e) == std::optional<QString>(role)) if (const auto* s = std::get_if<Stroke>(e)) return s;
    return nullptr;
  }
  void removeMine() {   // the document is edited directly (not an undo step), like the TS e2e does between cases
    auto& v2 = ed.doc().pages[size_t(page)].layers[size_t(layer())].elements;
    v2.erase(std::remove_if(v2.begin(), v2.end(), [](const Element& e) { return planeIdOf(e).has_value(); }), v2.end());
    ed.invalidateIndex(page);
    ed.touch(page);
    ed.selection().clear();
  }
  void check(bool ok, const QString& what) const { io.check(ok, what); }
};

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

template <class W> W* childWith(QWidget* root, const QString& text) {
  for (W* w : root->findChildren<W*>()) if (w->text() == text) return w;
  return nullptr;
}

void planeChecks(Rig& r) {
  Editor& ed = r.ed;
  Hub& hub = r.hub;
  r.io.out("\n== planes ==");
  hub.plane = DEFAULT_PLANE;
  ed.setTool("plane");
  r.check(ed.tool() == "plane", "plane: tool selected (key C registered with the toolbar)");
  const int depth0 = ed.undoDepth();
  r.drag({100, 100}, {400, 400});
  r.pump();
  r.check(r.mine().size() == 30 && r.ids() == 1, "plane: a drag makes ONE object of 30 pieces");
  r.check(r.count("axisx") == 1 && r.count("axisy") == 1 && r.count("grid") == 2 && r.count("label") == 22 && r.count("ticks") == 2 && r.count("arrow") == 2, "plane: axes, grid, labels, ticks and arrows as in the Electron app");
  r.check(ed.tool() == "select" && ed.returnTo() == "pen", "plane: after creating, Select with the pen to return to");
  r.check(ed.undoDepth() == depth0 + 1, "plane: creation is one undo step");
  const auto sel = hub.selectedPlane();
  r.check(sel && !ed.selection().isEmpty() && ed.selection().items().size() == r.mine().size(), "plane: the new object is selected as a whole");
  const auto fr = sel ? std::optional<Frame>(sel->frame) : std::nullopt;
  r.check(fr && near(fr->x0, 100, 1e-6) && near(fr->y0, 100, 1e-6) && near(fr->x1, 400, 1e-6) && near(fr->y1, 400, 1e-6), "plane: square cells fit the drag (grows from where it began by the longer side)");
  const Stroke* ax = r.firstStroke("axisx");
  r.check(ax && near(ax->pts[1], 250) && near(ax->pts[0], 100), "plane: axes cross at zero in the middle");
  r.check(ax && attrOr(ax->attrs, "color") == "#cc3300ff" && near(ax->widths[0], 2.26) && attrOr(ax->attrs, "glow") == "6" && attrOr(ax->attrs, "glowAlpha") == "0.8", "plane: colour, width and glow come from the pen");
  r.check(attrOr(r.firstStroke("grid")->attrs, "color") == "#cc330066", "plane: grid is lighter than the axes");
  r.check(GeneratedRegistry::instance().find(r.mine()) && GeneratedRegistry::instance().find(r.mine())->kind() == "plane", "plane: the handler claims it by its witness");

  ed.undo();
  r.check(r.mine().empty(), "plane: undo removes the whole object");
  ed.redo();
  r.check(r.mine().size() == 30, "plane: redo brings it back whole");

  // a click inserts nothing; the tool stays
  ed.selection().clear();
  ed.setTool("plane");
  const size_t before = r.els().size();
  r.click({200, 500});
  r.check(r.els().size() == before && ed.tool() == "plane", "plane: a click without drag inserts nothing and keeps the tool");
  // a drag along one axis only would be a flat plane
  hub.plane.square = false;
  r.drag({200, 500}, {400, 503});
  r.check(r.els().size() == before && ed.tool() == "plane", "plane: without square cells a drag along one axis makes nothing either");
  r.drag({200, 500}, {420, 600});
  const Stroke* ax2 = r.firstStroke("axisx");
  r.check(r.ids() == 2 && ax2, "plane: without square cells the drag is the frame");
  ed.undo();
  hub.plane = DEFAULT_PLANE;
  // hidden active layer
  ed.selection().clear();
  ed.setTool("plane");
  ed.setLayerVisible(r.page, r.layer(), false);
  r.drag({100, 500}, {300, 700});
  r.check(r.ids() == 1, "plane: a hidden active layer refuses with a notice");
  ed.setLayerVisible(r.page, r.layer(), true);

  // live editing through the panel
  QWidget* panel = makePlanePanel(nullptr, ed);
  panel->show();
  r.pump();
  hub.plane = DEFAULT_PLANE;
  const auto select = [&] { const auto id = planeIdOf(*r.mine().front()); ed.selection().select(r.page, ed.membersOf(r.page, r.layer(), *id)); };
  select();
  r.pump();
  const int d1 = ed.undoDepth();
  auto* axesOnly = childWith<QPushButton>(panel, "Solo ejes");
  r.check(axesOnly != nullptr, "plane panel: has the 'Solo ejes' button");
  if (axesOnly) axesOnly->click();
  r.check(r.count("grid") == 0 && r.count("axisx") == 1 && r.mine().size() == 28 && ed.undoDepth() == d1 + 1, "plane panel: 'Solo ejes' rebuilds the selected plane (one undo step)");
  ed.undo();
  select();
  r.check(r.count("grid") == 2 && r.mine().size() == 30, "plane panel: undo restores the grid");
  const auto spins = panel->findChildren<QDoubleSpinBox*>();
  r.check(spins.size() == 6, "plane panel: six number fields (range and steps)");
  if (spins.size() == 6) {
    select();
    r.pump();
    const int d2 = ed.undoDepth();
    spins[1]->setValue(10);   // x máx: valueChanged is what the arrows and Enter emit
    r.pump();
    const auto after = hub.selectedPlane();
    r.check(after && near(after->spec.xmax, 10) && ed.undoDepth() == d2 + 1 && r.ids() == 1, "plane panel: editing x máx rebuilds the selected plane in place (one step, same object)");
    r.check(after && near((after->frame.y1 - after->frame.y0) / (after->frame.x1 - after->frame.x0), 10.0 / 15.0, 1e-6), "plane panel: square cells keep the proportions of the new range");
    spins[1]->setValue(-9);   // x máx below x mín: refused, nothing changes
    r.pump();
    r.check(hub.selectedPlane() && near(hub.selectedPlane()->spec.xmax, 10), "plane panel: a range with min at or above max is refused");
    ed.undo();
    // with nothing selected the panel edits the defaults for the next plane and the document is untouched
    ed.selection().clear();
    r.pump();
    const size_t n = r.els().size();
    const int d3 = ed.undoDepth();
    spins[3]->setValue(8);   // y máx
    r.pump();
    r.check(near(hub.plane.ymax, 8) && r.els().size() == n && ed.undoDepth() == d3, "plane panel: nothing selected edits the next plane's settings only");
  }
  // restyle needs the pen style of now
  delete panel;
  r.removeMine();
  hub.plane = DEFAULT_PLANE;
}

void space3Checks(Rig& r) {
  Editor& ed = r.ed;
  Hub& hub = r.hub;
  r.io.out("\n== 3D spaces ==");
  Style s = ed.style();
  s.color = "#0044cc"; s.penWidth = 1.41; s.glowOn = false;
  ed.setStyle(s);
  hub.space3 = DEFAULT_SPACE3;
  hub.space3.o = {0, 0};
  ed.setTool("space3");
  const int depth0 = ed.undoDepth();
  const size_t before = r.els().size();
  r.click({300, 300});
  r.check(r.els().size() == before && ed.tool() == "space3", "space3: a click without drag inserts nothing");
  r.drag({150, 150}, {450, 450});
  r.pump();
  r.check(r.ids() == 1 && r.count("axis3x") == 1 && r.count("axis3y") == 1 && r.count("axis3z") == 1 && r.count("grid3") == 2 && r.count("ticks") == 3 && r.count("arrow") == 3 && r.count("label") == 21, "space3: three axes, grid on xy, ticks, arrows and 21 labels");
  r.check(ed.tool() == "select" && ed.undoDepth() == depth0 + 1, "space3: Select afterwards, one undo step");
  const auto q = hub.selectedSpace3();
  r.check(q && hub.selectedPlane() == std::nullopt && ed.selection().items().size() == r.mine().size(), "space3: selected as a whole and not taken for a 2D plane");
  r.check(q && attrOr(r.firstStroke("axis3x")->attrs, "color") == "#0044ccff", "space3: style from the pen");
  const auto* h = GeneratedRegistry::instance().find(r.mine());
  r.check(h && h->kind() == "space3", "space3: the handler claims it");
  if (h && q) {
    const auto anchors = h->anchors(r.mine());
    r.check(anchors.size() == 6, "space3: six anchors (the end of each half of each axis)");
    // drag the anchor of the +z end 40 units to the right
    int zi = -1;
    const auto all = anchorsOf(q->spec);
    for (size_t i = 0; i < all.size(); i++) if (all[i].axis == 2 && all[i].side == AnchorSide::Plus) zi = int(i);
    const QPointF to(anchors[size_t(zi)].x() + 40, anchors[size_t(zi)].y());
    const auto rebuilt = h->dragAnchor(r.mine(), q->id, zi, to, false);
    r.check(rebuilt.has_value(), "space3: dragging an anchor rebuilds the object");
    if (rebuilt) {
      std::vector<const Element*> m2;
      for (const Element& e : *rebuilt) m2.push_back(&e);
      const auto moved = space3SpecOf(m2);
      r.check(moved && vectorAngle(moved->u[2]).deg < 89.5 && near(vectorAngle(moved->u[0]).deg, vectorAngle(q->spec.u[0]).deg, 1e-6), "space3: only that axis turned");
    }
    const auto same = h->rebuildAfterTransform(r.mine(), q->id);
    r.check(same && same->size() == r.mine().size(), "space3: rebuildAfterTransform keeps every piece");
  }

  QWidget* panel = makeSpace3Panel(nullptr, ed);
  panel->show();
  r.pump();
  const auto select = [&] { const auto id = planeIdOf(*r.mine().front()); ed.selection().select(r.page, ed.membersOf(r.page, r.layer(), *id)); };
  select();
  r.pump();
  const auto spins = panel->findChildren<QDoubleSpinBox*>();
  r.check(spins.size() == 21, "space3 panel: 3 axes x 7 number fields (range, scale, angle and length of each half)");
  if (spins.size() == 21 && hub.selectedSpace3()) {
    const Space3Spec orig = hub.selectedSpace3()->spec;
    const int d1 = ed.undoDepth();
    spins[2 * 7 + 3]->setValue(80);   // z angle +
    r.pump();
    const auto now = hub.selectedSpace3();
    r.check(now && near(vectorAngle(now->spec.u[2]).deg, 80, 0.06) && ed.undoDepth() == d1 + 1 && r.ids() == 1, "space3 panel: editing the z angle turns that axis (one step)");
    r.check(now && near(now->spec.u[0][0], orig.u[0][0], 1e-9) && near(now->spec.u[0][1], orig.u[0][1], 1e-9) && near(now->spec.u[1][1], orig.u[1][1], 1e-9), "space3 panel: axes whose fields were not touched keep their exact vectors (no rounding drift)");
    spins[0 * 7 + 1]->setValue(-9);   // x máx below x mín
    r.pump();
    r.check(hub.selectedSpace3() && hub.selectedSpace3()->spec.xmax == orig.xmax, "space3 panel: min at or above max is refused");
    ed.undo();
    auto* reset = childWith<QPushButton>(panel, "Vista por defecto");
    select();
    if (reset) reset->click();
    r.pump();
    const auto dv = hub.selectedSpace3();
    r.check(dv && near(vectorAngle(dv->spec.u[2]).deg, 90, 0.06) && near(vectorAngle(dv->spec.u[0]).deg, 215, 0.06), "space3 panel: 'Vista por defecto' restores z up and x down-left");
  }
  delete panel;
  r.removeMine();
  hub.space3 = DEFAULT_SPACE3;
  hub.space3.o = {0, 0};
}

void figureChecks(Rig& r) {
  Editor& ed = r.ed;
  Hub& hub = r.hub;
  r.io.out("\n== figures ==");
  hub.figOpts = FigOpts{Heads::None, Dash::Solid, 5, 5, Hidden::Dash, QString()};
  const auto make = [&](FigKind kind, QPointF a, QPointF b) {
    r.removeMine();
    hub.figKind = kind;
    ed.setTool("figure");
    r.drag(a, b);
    r.pump();
  };
  const auto body = [&] { return r.count("figbody"); };
  const auto cur = [&] { return hub.selectedFigure(); };

  const int d0 = ed.undoDepth();
  make(FigKind::Segment, {100, 100}, {300, 180});
  r.check(body() == 1 && r.firstStroke("figbody")->pts.size() == 4 && cur() && ed.tool() == "select" && ed.undoDepth() == d0 + 1, "figure: a drag makes a segment as one undo step, then Select");
  r.check(cur() && near(cur()->spec.pts[0], 100, 1e-3) && near(cur()->spec.pts[1], 100, 1e-3) && near(cur()->spec.pts[2], 300, 1e-3) && near(cur()->spec.pts[3], 180, 1e-3), "figure: control points are the drag ends");
  r.check(attrOr(r.firstStroke("figbody")->attrs, "color") == "#0044ccff" && attrOr(r.firstStroke("figwitness")->attrs, "color") == "#0044cc00", "figure: style from the pen, witness invisible");
  const auto* h = GeneratedRegistry::instance().find(r.mine());
  r.check(h && h->kind() == "figure" && h->anchors(r.mine()).size() == 2, "figure: handler claims it, two anchors");
  if (h && cur()) {
    const auto rb = h->dragAnchor(r.mine(), cur()->id, 1, QPointF(360, 90), false);
    std::vector<const Element*> m2;
    if (rb) for (const Element& e : *rb) m2.push_back(&e);
    const auto read = rb ? figureSpecOf(m2) : std::nullopt;
    r.check(read && near(read->spec.pts[2], 360, 1e-3) && near(read->spec.pts[3], 90, 1e-3) && near(read->spec.pts[0], 100, 1e-3), "figure: dragAnchor moves only that control point");
    r.check(!h->dragAnchor(r.mine(), cur()->id, 1, QPointF(300, 180), false), "figure: dragging to where it already is changes nothing");
    const auto same = h->rebuildAfterTransform(r.mine(), cur()->id);
    r.check(same && same->size() == r.mine().size(), "figure: rebuildAfterTransform keeps every piece");
  }
  ed.undo();
  r.check(r.mine().empty(), "figure: undo removes the whole figure");

  // arrow: head by default; dash does not dash the head
  hub.figOpts.dash = Dash::Dash;
  make(FigKind::Arrow, {100, 100}, {300, 100});
  r.check(r.count("figarrow") == 1 && cur() && cur()->spec.heads == Heads::End && attrOr(r.firstStroke("figbody")->attrs, "style") == "dash" && !attrFind(r.firstStroke("figarrow")->attrs, "style"), "figure: an arrow has a head of its own; a dashed line keeps a solid head");
  hub.figOpts.dash = Dash::Solid;
  hub.figOpts.heads = Heads::Both;
  make(FigKind::Segment, {100, 100}, {300, 100});
  r.check(r.count("figarrow") == 2, "figure: heads at both ends");
  hub.figOpts.heads = Heads::None;
  make(FigKind::Segment, {100, 100}, {300, 140});
  ed.undo();
  r.removeMine();
  hub.figKind = FigKind::Segment;
  ed.setTool("figure");
  r.drag({100, 100}, {300, 140}, Qt::ShiftModifier);
  r.pump();
  if (cur()) {
    const auto p = cur()->spec.pts;   // a copy: cur() returns a temporary
    const double step = M_PI / 12, ang = std::atan2(p[3] - p[1], p[2] - p[0]) / step;
    r.check(near(ang, std::round(ang), 1e-6), "figure: Shift snaps the angle to steps of 15 degrees");
  } else r.check(false, "figure: Shift drag made a figure");

  // a polygon by clicks
  r.removeMine();
  hub.figKind = FigKind::Polygon;
  ed.setTool("figure");
  r.click({100, 400});
  r.check(r.mine().empty() && hub.figPending.size() == 2 && ed.tool() == "figure", "figure: the first click only starts the polygon (nothing inserted yet)");
  r.click({300, 400});
  r.click({300, 550});
  r.click({150, 550});
  r.check(hub.figPending.size() == 8, "figure: one point per click");
  r.key(Qt::Key_Backspace);
  r.check(hub.figPending.size() == 6, "figure: Backspace removes the last point");
  r.click({150, 550});
  r.click({150, 550});   // the same place again: done
  r.pump();
  const Stroke* poly = r.firstStroke("figbody");
  r.check(body() == 1 && cur() && cur()->spec.kind == FigKind::Polygon && poly && poly->pts.size() == 10 && near(poly->pts[0], poly->pts[8], 1e-3) && near(poly->pts[1], poly->pts[9], 1e-3) && hub.figPending.empty() && ed.tool() == "select", "figure: a second click on the last point finishes a closed polygon");
  // Enter finishes, Esc cancels, leaving the tool finishes
  r.removeMine();
  hub.figKind = FigKind::Polyline;
  ed.setTool("figure");
  r.click({100, 400}); r.click({200, 450}); r.click({300, 400});
  r.key(Qt::Key_Return);
  r.pump();
  r.check(cur() && cur()->spec.kind == FigKind::Polyline && cur()->spec.pts.size() == 6 && hub.figPending.empty(), "figure: Enter finishes a polyline");
  r.removeMine();
  ed.setTool("figure");
  r.click({100, 500}); r.click({200, 550});
  r.key(Qt::Key_Escape);
  r.check(hub.figPending.empty() && r.mine().empty() && ed.tool() == "figure", "figure: Esc cancels the figure being clicked and keeps the tool");
  r.click({100, 500}); r.click({200, 550});
  ed.setTool("select");
  r.pump();
  r.check(hub.figPending.empty() && r.count("figbody") == 1 && ed.tool() == "select", "figure: switching tool finishes it");
  r.removeMine();
  ed.setTool("figure");
  r.click({100, 500});
  ed.setTool("select");
  r.check(hub.figPending.empty() && r.mine().empty(), "figure: with too few points, leaving the tool drops it");
  r.removeMine();

  // regular polygon, curves
  hub.figOpts.n = 6;
  make(FigKind::Regular, {300, 300}, {380, 300});
  const Stroke* hx = r.firstStroke("figbody");
  bool sides = hx && hx->pts.size() == 14;
  if (sides) for (int k = 0; k < 6; k++) sides = sides && near(std::hypot(hx->pts[size_t(2 * k + 2)] - hx->pts[size_t(2 * k)], hx->pts[size_t(2 * k + 3)] - hx->pts[size_t(2 * k + 1)]), 80, 1e-3);
  r.check(sides && near(hx->pts[0], 380, 1e-3), "figure: a regular hexagon has six equal sides");
  hub.figOpts.n = 5;
  make(FigKind::Bezier2, {100, 300}, {400, 300});
  r.check(r.firstStroke("figbody")->pts.size() > 40 && cur() && cur()->spec.pts.size() == 6, "figure: quadratic Bezier with three control points");
  make(FigKind::Bezier3, {100, 300}, {400, 300});
  r.check(cur() && cur()->spec.pts.size() == 8, "figure: cubic Bezier with four control points");
  r.removeMine();
  hub.figKind = FigKind::Spline;
  ed.setTool("figure");
  const QPointF through[] = {{100, 600}, {200, 520}, {300, 640}, {400, 560}};
  for (const QPointF& p : through) r.click(p);
  r.click(through[3]);
  r.pump();
  const Stroke* sp = r.firstStroke("figbody");
  bool all = sp != nullptr;
  if (sp) for (const QPointF& p : through) {
    bool hit = false;
    for (size_t i = 0; i + 1 < sp->pts.size(); i += 2) if (std::hypot(sp->pts[i] - p.x(), sp->pts[i + 1] - p.y()) < 1e-3) hit = true;
    all = all && hit;
  }
  r.check(all, "figure: the spline passes through every clicked point");
  make(FigKind::Arc, {200, 300}, {300, 300});
  r.check(r.firstStroke("figbody") && r.firstStroke("figbody")->pts.size() > 30, "figure: arc");
  make(FigKind::Angle, {200, 300}, {320, 300});
  r.check(r.count("figmark") == 1 && r.firstStroke("figbody")->pts.size() == 6, "figure: angle with its mark");
  make(FigKind::Dim, {100, 300}, {300, 300});
  r.check(r.count("figarrow") == 2 && r.count("figmark") == 2, "figure: dimension line with an arrow and a tick at each end");
  // solids
  make(FigKind::Box, {200, 400}, {200, 310});
  r.check(body() == 9 && r.count("fighidden") == 3 && cur() && cur()->spec.pts.size() == 8, "figure: cube with nine solid and three dashed hidden edges");
  const auto ob = cur() ? cur()->spec.pts : std::vector<double>{};
  if (cur() && ob.size() == 8) {   // the origin carries the whole solid
    const auto* fh = GeneratedRegistry::instance().find(r.mine());
    const auto rb = fh->dragAnchor(r.mine(), cur()->id, 0, QPointF(ob[0] + 40, ob[1] + 10), false);
    std::vector<const Element*> m2;
    if (rb) for (const Element& e : *rb) m2.push_back(&e);
    const auto read = rb ? figureSpecOf(m2) : std::nullopt;
    r.check(read && near(read->spec.pts[0] - ob[0], 40, 1e-3) && near(read->spec.pts[2] - ob[2], 40, 1e-3) && near(read->spec.pts[7] - ob[7], 10, 1e-3), "figure: the origin of a solid moves the whole solid");
  }
  hub.figOpts.hidden = Hidden::None;
  make(FigKind::Box, {200, 400}, {200, 310});
  r.check(r.count("fighidden") == 0 && body() == 9, "figure: hidden edges can be left out");
  hub.figOpts.hidden = Hidden::Dash;
  make(FigKind::Pyramid, {200, 400}, {200, 310});
  r.check(body() + r.count("fighidden") == 8 && r.count("fighidden") >= 1, "figure: pyramid");
  make(FigKind::Cylinder, {200, 400}, {200, 310});
  r.check(body() >= 4 && r.count("fighidden") >= 1, "figure: cylinder");
  make(FigKind::Cone, {200, 400}, {200, 310});
  r.check(body() >= 3 && r.count("fighidden") >= 1, "figure: cone");
  hub.figOpts.n = 8; hub.figOpts.m = 4;
  make(FigKind::Sphere, {200, 400}, {200, 310});
  r.check(body() >= 6 && r.count("fighidden") >= 3, "figure: sphere with meridians and parallels");
  hub.figOpts.label = "v";
  hub.figOpts.heads = Heads::None;
  make(FigKind::Segment, {100, 100}, {300, 100});
  r.check(r.count("figlabel") == 1, "figure: a label near the figure");
  hub.figOpts.label = QString();
  // a click makes a figure of the default size
  make(FigKind::Segment, {150, 700}, {150, 700});
  r.check(cur() && near(cur()->spec.pts[2] - cur()->spec.pts[0], 100, 1e-3), "figure: a click makes a segment of the default size");

  // the panel
  QWidget* panel = makeFigurePanel(nullptr, ed);
  panel->show();
  r.pump();
  int gallery = 0;
  for (auto* b : panel->findChildren<QPushButton*>()) if (b->objectName() == "fig") gallery++;
  r.check(gallery == 16, "figure panel: the gallery has the 16 kinds");
  r.removeMine();
  ed.setTool("pen");
  if (auto* cono = childWith<QPushButton>(panel, "Cono")) cono->click();
  r.check(hub.figKind == FigKind::Cone && ed.tool() == "figure", "figure panel: choosing a kind in the gallery selects it and the tool");
  // edit the selected figure live
  make(FigKind::Segment, {100, 100}, {300, 100});
  r.pump();
  const int d1 = ed.undoDepth();
  if (auto* combos = panel->findChildren<QComboBox*>().value(1)) {   // Trazo
    combos->setCurrentIndex(combos->findData(int(Dash::Dot)));
    r.pump();
    r.check(cur() && cur()->spec.dash == Dash::Dot && ed.undoDepth() == d1 + 1 && attrOr(r.firstStroke("figbody")->attrs, "style") == "dot", "figure panel: changing the dash edits the selected figure (one step)");
  }
  r.removeMine();
  ed.selection().clear();
  r.pump();
  const FigOpts keep = hub.figOpts;
  if (auto* combos = panel->findChildren<QComboBox*>().value(1)) {
    combos->setCurrentIndex(combos->findData(int(Dash::Dash)));
    r.check(hub.figOpts.dash == Dash::Dash && r.mine().empty() && keep.dash != Dash::Dash, "figure panel: nothing selected edits the options of the next figure");
  }
  delete panel;
  hub.figOpts = FigOpts{Heads::None, Dash::Solid, 5, 3, Hidden::Dash, QString()};
  hub.figKind = FigKind::Segment;
  r.removeMine();
}

void persistenceChecks(Rig& r) {
  r.io.out("\n== settings in prefs.json ==");
  QTemporaryDir dir;
  const QString path = dir.filePath("prefs.json");
  Prefs p;
  p.theme = "neon";
  p.extra.insert("someFutureKey", 42);
  QString err;
  r.check(savePrefs(path, p, &err), "prefs: temporary file written " + err);
  Hub& hub = r.hub;
  hub.setPrefsPath(path);
  PlaneSpec ps = DEFAULT_PLANE;
  ps.xmax = 9;
  r.ed.selection().clear();
  hub.plane = sanitizePlane(ps);
  QWidget* panel = makePlanePanel(nullptr, r.ed);
  panel->show();
  r.pump();
  const auto spins = panel->findChildren<QDoubleSpinBox*>();
  if (!spins.isEmpty()) spins[0]->setValue(-7);   // x mín
  r.pump();
  Prefs back;
  loadPrefs(path, &back);
  const auto stored = decodeSpec(back.plane);
  r.check(stored && near(stored->xmin, -7) && near(stored->xmax, 9), "prefs: the plane settings are saved through the prefs API when the panel changes them");
  r.check(back.theme == "neon" && back.extra.value("someFutureKey").toInt() == 42, "prefs: other settings and unknown keys survive the save");
  hub.space3 = DEFAULT_SPACE3;
  QWidget* p3 = makeSpace3Panel(nullptr, r.ed);
  p3->show();
  r.pump();
  const auto s3 = p3->findChildren<QDoubleSpinBox*>();
  if (s3.size() > 1) s3[1]->setValue(5);   // x máx
  r.pump();
  Prefs back2;
  loadPrefs(path, &back2);
  const auto st3 = decodeSpace3(back2.space3);
  r.check(st3 && near(st3->xmax, 5) && st3->o[0] == 0 && back2.plane == back.plane && back2.extra.value("someFutureKey").toInt() == 42, "prefs: the 3D settings are saved too, without disturbing the rest");
  // a new Hub reading the file gets them at startup
  hub.plane = DEFAULT_PLANE;
  hub.setPrefsPath(path);
  r.check(near(hub.plane.xmin, -7) && near(hub.space3.xmax, 5), "prefs: the settings are read back at startup");
  delete panel;
  delete p3;
  hub.setPrefsPath(QString());
  hub.plane = DEFAULT_PLANE;
  hub.space3 = DEFAULT_SPACE3;
  hub.space3.o = {0, 0};
}

// every kind on one page, panels, and a preview mid-drag: images to LOOK at
void screenshots(Rig& r, const QString& dir) {
  Editor& ed = r.ed;
  Hub& hub = r.hub;
  QDir().mkpath(dir);
  r.io.out("\n== screenshots ==");
  Style s = ed.style();
  s.color = "#0044cc"; s.penWidth = 1.41; s.glowOn = false;
  ed.setStyle(s);
  r.removeMine();
  hub.plane = DEFAULT_PLANE;
  hub.create(r.page, "plane", buildPlane(newPlaneId(), DEFAULT_PLANE, Frame{40, 60, 280, 300}, hub.penStyle()), "p", false);
  PlaneSpec ps = DEFAULT_PLANE;
  ps.xmin = 0; ps.xmax = 8; ps.ymin = 0; ps.ymax = 4; ps.sub = 5; ps.square = false;
  hub.create(r.page, "plane", buildPlane(newPlaneId(), sanitizePlane(ps), Frame{310, 80, 560, 220}, hub.penStyle()), "p", false);
  Space3Spec sp = placedSpec(sanitizeSpace3(DEFAULT_SPACE3), 1.0, V2{60, 330});
  hub.create(r.page, "space3", buildSpace3(newPlaneId(), sp, hub.penStyle()), "s", false);
  const auto& cat = figureCatalog();
  for (size_t i = 0; i < cat.size(); i++) {
    hub.figKind = cat[i].kind;
    const double x = 40 + double(i % 4) * 135, y = 560 + double(i / 4) * 62;
    FigureSpec spec = hub.figureOfDrag({x, y + 50}, {x + 100, y + 6}, false, 1);
    if (cat[i].clicks) spec = sanitizeFigure(FigureSpec{cat[i].kind, {x, y + 40, x + 30, y + 5, x + 60, y + 35, x + 100, y + 8}, Heads::None, Dash::Solid, 5, 3, Hidden::Dash, QString()});
    hub.create(r.page, "figure", buildFigure(newPlaneId(), spec, hub.penStyle()), "f", false);
  }
  hub.figKind = FigKind::Segment;
  ed.setTool("pen");
  ed.selection().clear();
  r.v.setZoom(1.2);
  r.v.scrollToPage(r.page);
  r.pump();
  r.v.renderVisibleNow();
  r.pump();
  r.check(r.win.grab().save(dir + "/objects_page.png"), "screenshot: " + dir + "/objects_page.png (a plane, a plane with subgrid, a 3D space and the 16 figure kinds)");

  // a drag in progress: the preview of the plane tool on the overlay
  ed.setTool("plane");
  r.mouse(QEvent::MouseButtonPress, {330, 250}, Qt::LeftButton, Qt::LeftButton);
  for (int i = 1; i <= 6; i++) r.mouse(QEvent::MouseMove, QPointF(330 + 40 * i, 250 + 30 * i), Qt::NoButton, Qt::LeftButton);
  r.pump();
  r.check(r.win.grab().save(dir + "/objects_preview.png"), "screenshot: " + dir + "/objects_preview.png (plane preview mid-drag)");
  r.mouse(QEvent::MouseButtonRelease, {570, 430}, Qt::LeftButton, Qt::NoButton);
  r.pump();
  ed.undo();

  const struct { QWidget* w; const char* name; } panels[] = {
      {makePlanePanel(nullptr, ed), "objects_panel_plane.png"}, {makeSpace3Panel(nullptr, ed), "objects_panel_space3.png"}, {makeFigurePanel(nullptr, ed), "objects_panel_figure.png"}};
  for (const auto& p : panels) {
    p.w->show();
    r.pump();
    r.check(p.w->grab().save(dir + "/" + p.name), QString("screenshot: ") + dir + "/" + p.name + QString(" (%1 x %2)").arg(p.w->width()).arg(p.w->height()));
    delete p.w;
  }
}

void timing(Rig& r) {
  Editor& ed = r.ed;
  r.io.out("\n== pointer cost of the previews (one pointerMove: rebuild + invalidate; no paint) ==");
  r.removeMine();
  for (const QString& tool : {QString("plane"), QString("space3"), QString("figure")}) {
    ed.setTool(tool);
    r.hub.figKind = FigKind::Box;
    const PointerEvent down = r.v.makeEvent(r.page, {100, 100}, 0.5, PointerEvent::Kind::Mouse);
    ed.pointerDown(down);
    std::vector<double> ms;
    for (int i = 0; i < 400; i++) {
      PointerEvent e = r.v.makeEvent(r.page, QPointF(100 + 0.6 * i, 100 + 0.5 * i), 0.5, PointerEvent::Kind::Mouse);
      QElapsedTimer t;
      t.start();
      ed.pointerMove(e);
      ms.push_back(double(t.nsecsElapsed()) / 1e6);
    }
    ed.pointerCancel();
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double m : ms) sum += m;
    r.io.out(QString("   %1: mean %2 ms  p50 %3  p99 %4  max %5").arg(tool).arg(sum / double(ms.size()), 0, 'f', 3).arg(ms[ms.size() / 2], 0, 'f', 3).arg(ms[size_t(double(ms.size()) * 0.99)], 0, 'f', 3).arg(ms.back(), 0, 'f', 3));
  }
  ed.setTool("pen");
}

}  // namespace

void runObjectsSelfTest(MainWindow& win, Editor& ed, const SelfTestIO& io) {
  io.out("\n== planes, 3D spaces, figures (app/objects) ==");
  const bool realSelection = dynamic_cast<NullSelection*>(&ed.selection()) == nullptr;
  if (!realSelection) ed.setSelection(std::make_unique<RecordingSelection>());
  io.out(realSelection ? "   using the installed selection" : "   the real selection is not installed: a recording stand-in is used");

  const int depth0 = ed.undoDepth();
  const int pagesBefore = ed.pageCount();
  const Style savedStyle = ed.style();
  const int page = ed.insertPage(ed.pageCount(), 0);
  Rig r{win, ed, *win.view(), Hub::of(ed), io, page};
  r.v.setZoom(1.0);
  r.v.scrollToPage(page);
  r.pump();
  r.v.renderVisibleNow();

  Style s = ed.style();
  s.color = "#cc3300"; s.penWidth = 2.26; s.glowOn = true; s.glowSize = 6; s.glowIntensity = 0.8;
  ed.setStyle(s);
  planeChecks(r);
  space3Checks(r);
  figureChecks(r);
  persistenceChecks(r);
  timing(r);
  screenshots(r, io.shots);

  ed.setTool("pen");
  ed.selection().clear();
  ed.deletePage(page);   // the test page and everything on it (the history is not unwound: the cases edit the page directly between steps)
  ed.setStyle(savedStyle);
  r.v.zoomFitWidth();
  r.pump();
  io.check(ed.pageCount() == pagesBefore && depth0 <= ed.undoDepth(), "objects: the test page was removed again");
  if (!realSelection) ed.setSelection(nullptr);
}

}  // namespace xlobjects
