// The REAL chain selection -> generated handlers for every kind of generated object (plane, 3D space, the 16 figures, number line, set diagram):
// click a piece with the select tool, move / turn (handle and 90 degrees) / flip / resize it with real mouse events on the view, drag its anchors,
// undo and redo each step, copy / paste / duplicate, save and reopen, repair. The expected position of every control point is computed from the
// map the gesture applied (never from the code under test), and the stroke points themselves are compared too, so a wrong map shows up as such.
#include "chainselftest.h"
#include "docfactory.h"
#include "documentview.h"
#include "editor.h"
#include "figures.h"
#include "generated.h"
#include "mainwindow.h"
#include "mathobj.h"
#include "objects/hub.h"
#include "fill.h"
#include "mathstate.h"
#include "pagerender.h"
#include "plane.h"
#include "repair.h"
#include "selection/selectionimpl.h"
#include "space3.h"
#include "xopp.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <set>

namespace mathobj {
namespace {

struct Map {   // x' = a x + c y + e ; y' = b x + d y + f
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  V2 operator()(const V2& p) const { return {a * p[0] + c * p[1] + e, b * p[0] + d * p[1] + f}; }
  Map then(const Map& n) const {   // this, then n
    return {n.a * a + n.c * b, n.b * a + n.d * b, n.a * c + n.c * d, n.b * c + n.d * d, n.a * e + n.c * f + n.e, n.b * e + n.d * f + n.f};
  }
};
Map translate(double dx, double dy) { return {1, 0, 0, 1, dx, dy}; }
Map scaleAbout(double sx, double sy, double ax, double ay) { return {sx, 0, 0, sy, ax - sx * ax, ay - sy * ay}; }
Map rotateAbout(double th, double cx, double cy) {
  const double c = std::cos(th), s = std::sin(th);
  return {c, s, -s, c, cx - c * cx + s * cy, cy - s * cx - c * cy};
}
Map flipHAbout(double cx) { return {-1, 0, 0, 1, 2 * cx, 0}; }
Map flipVAbout(double cy) { return {1, 0, 0, -1, 0, 2 * cy}; }

bool near(double a, double b, double eps) { return std::abs(a - b) <= eps; }
void pump(int n = 4) { for (int i = 0; i < n; i++) { QApplication::processEvents(); QThread::msleep(1); } }

struct Probe {
  bool ok = false;
  std::vector<V2> pts;      // the control points as the OWNER of the kind reads them now
  std::vector<double> textSizes;   // sizes of the label texts, sorted
};

struct KindCase {
  QString name;       // for the report
  QString kind;       // the handler id the selection must report
  bool pointwise;     // control points can be compared one by one (a plane's frame is a box, compared as a box)
  std::function<std::vector<Element>(const QString&)> build;
};

ElementPtrs ptrsOf(Editor& ed, int page, int layer, const QString& id) {
  ElementPtrs p;
  for (const ElementRef& r : ed.membersOf(page, layer, id)) p.push_back(&ed.doc().pages[size_t(r.page)].layers[size_t(r.layer)].elements[size_t(r.index)]);
  return p;
}

Probe probeOf(const ElementPtrs& m, const QString& kind) {
  Probe pr;
  for (const Element* e : m)
    if (const auto* t = std::get_if<Text>(e)) { const QString* s = attrFind(t->attrs, "size"); pr.textSizes.push_back(s ? s->toDouble() : 0); }
  std::sort(pr.textSizes.begin(), pr.textSizes.end());
  if (kind == "plane") {
    const auto spec = planeSpecOf(m);
    const auto f = planeFrameOf(m);
    if (!spec || !f || !planeStyleOf(m)) return pr;
    pr.pts = {{f->x0, f->y0}, {f->x1, f->y0}, {f->x1, f->y1}, {f->x0, f->y1}};
  } else if (kind == "space3") {
    const auto spec = space3SpecOf(m);
    if (!spec || !space3StyleOf(m)) return pr;
    pr.pts.push_back(spec->o);
    for (const Space3Anchor& a : anchorsOf(*spec)) pr.pts.push_back(a.at);
  } else if (kind == "figure") {
    const auto r = figureSpecOf(m);
    if (!r) return pr;
    for (size_t i = 0; i + 1 < r->spec.pts.size(); i += 2) pr.pts.push_back({r->spec.pts[i], r->spec.pts[i + 1]});
  } else {
    const auto r = sets::readMath(m);
    if (!r) return pr;
    pr.pts = sets::mathAnchors(r->spec);
  }
  pr.ok = true;
  return pr;
}

struct Rig {
  MainWindow& win;
  Editor& ed;
  DocumentView& v;
  std::function<void(const QString&)> out;
  int fail = 0, passed = 0;
  int page = 0;

  void check(bool ok, const QString& what) {
    if (ok) { passed++; if (qEnvironmentVariableIsSet("XL_CHAIN_VERBOSE")) out("  PASS  " + what); }
    else { fail++; out("  FAIL  " + what); }
  }
  selx::RealSelection* sel() { return selx::RealSelection::of(ed); }

  void send(QEvent::Type t, const QPointF& p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m = {}) {
    const QPointF w = v.toView(page, p);
    QMouseEvent ev(t, w, v.viewport()->mapToGlobal(w), b, bs, m);
    QApplication::sendEvent(v.viewport(), &ev);
  }
  /** `m` is held while the pointer moves and is released (Shift pressed together with the press would start a new selection instead, as in the Electron app). */
  void drag(const QPointF& a, const QPointF& b, int steps = 5, Qt::KeyboardModifiers m = {}) {
    send(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= steps; k++) send(QEvent::MouseMove, a + (b - a) * (double(k) / steps), Qt::NoButton, Qt::LeftButton, m);
    send(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, m);
    pump(3);
  }
  void click(const QPointF& p) {
    send(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    pump(3);
  }
  void trigger(const char* id) { ed.action(id)->trigger(); pump(3); }

  const std::vector<Element>& els() const { return ed.doc().pages[size_t(page)].layers[0].elements; }
  std::vector<Element> snap() const { return els(); }
  QString idOf() const {
    for (const Element& e : els()) if (const auto id = planeIdOf(e)) return *id;
    return {};
  }
  /** A point on a visible stroke of the object where a user can grab it as a whole: the middle of a segment of its strokes, away from every
   *  anchor and every handle of the selection frame (those would start another gesture). */
  QPointF pieceOf(const QString& id) const {
    std::vector<QPointF> avoid;
    if (auto* s = selx::RealSelection::of(ed)) {
      avoid = s->anchors();
      const QRectF b = ed.selection().bbox();
      if (!b.isNull()) {
        for (double fx : {0.0, 0.5, 1.0}) for (double fy : {0.0, 0.5, 1.0}) if (fx != 0.5 || fy != 0.5) avoid.emplace_back(b.left() + fx * b.width(), b.top() + fy * b.height());
        avoid.emplace_back(b.center().x(), b.top() - selx::kRotOffsetPx);
      }
    }
    QPointF best;
    double bestClear = -1;
    for (const Element& e : els()) {
      const auto* s = std::get_if<Stroke>(&e);
      if (!s || planeIdOf(e) != std::optional<QString>(id) || s->pts.size() < 4) continue;
      const QString* col = attrFind(s->attrs, "color");
      if (col && col->size() >= 9 && col->right(2) == "00") continue;   // the invisible witness
      for (size_t i = 0; i + 3 < s->pts.size(); i += 2) {
        const QPointF q((s->pts[i] + s->pts[i + 2]) / 2, (s->pts[i + 1] + s->pts[i + 3]) / 2);
        double clear = 1e9;
        for (const QPointF& a : avoid) clear = std::min(clear, std::hypot(a.x() - q.x(), a.y() - q.y()));
        if (clear > bestClear) { bestClear = clear; best = q; }
        if (clear > 30) return q;
      }
    }
    return best;
  }
  /** Every stroke point of the first visible stroke, to compare with the map. */
  std::vector<double> firstStrokePts(const QString& id) const {
    for (const Element& e : els())
      if (const auto* s = std::get_if<Stroke>(&e)) if (planeIdOf(e) == std::optional<QString>(id)) return s->pts;
    return {};
  }
};

std::vector<KindCase> allKinds() {
  std::vector<KindCase> k;
  const GenStyle st{"#0044cc", 1.41, std::nullopt};
  k.push_back({"plane", "plane", false, [st](const QString& id) { return buildPlane(id, DEFAULT_PLANE, Frame{150, 150, 450, 400}, st); }});
  k.push_back({"space3", "space3", true, [st](const QString& id) {
    const Space3Spec s = placedSpec(DEFAULT_SPACE3, 1.0, V2{150, 160});
    return buildSpace3(id, s, st);
  }});
  for (const FigureCatalogItem& it : figureCatalog()) {
    const FigKind fk = it.kind;
    k.push_back({QString("figure %1").arg(figKindName(fk)), "figure", true, [st, fk](const QString& id) {
      FigureSpec s = defaultSpec(fk, V2{160, 170}, V2{420, 360});
      if (fk == FigKind::Segment) s.label = "AB";   // one with a label text
      return buildFigure(id, sanitizeFigure(s), st);
    }});
  }
  k.push_back({"number line", "line", true, [st](const QString& id) {
    sets::LineSpec l = sets::defaultLine({150, 250}, {450, 250});
    l.rows = {{"(-2, 3] U {4}", "A"}, {"[1, inf)", "B"}};
    return sets::buildMath(id, sets::sanitizeLine(l), st);
  }});
  k.push_back({"venn 2", "venn", true, [st](const QString& id) {
    sets::VennSpec s = sets::defaultVenn(2, {150, 150}, {450, 360});
    s.expr = "A ∩ B";
    s.lists = {"1, 2", "2, 3", "", "9"};
    return sets::buildMath(id, sets::sanitizeVenn(s), st);
  }});
  k.push_back({"venn 3", "venn", true, [st](const QString& id) {
    sets::VennSpec s = sets::defaultVenn(3, {150, 150}, {450, 400});
    s.expr = "(A ∪ B)'";
    return sets::buildMath(id, sets::sanitizeVenn(s), st);
  }});
  return k;
}

/** Fresh one-page document holding only the object; returns its id. */
QString setUp(Rig& r, const KindCase& kc) {
  Document doc = blankDocument();
  const QString id = newObjectId();
  for (Element& e : kc.build(id)) doc.pages[0].layers[0].elements.push_back(std::move(e));
  r.ed.setDocument(std::move(doc));
  r.page = 0;
  r.v.setZoom(1.0);
  r.v.scrollToPage(0);
  r.ed.setCurrentPage(0);
  r.ed.setTool("select");
  pump(6);
  return id;
}

/** Does the object, as the document holds it now, answer to its handler and to the panels' readers? */
void editable(Rig& r, const KindCase& kc, const QString& id, const QString& when, Probe* out) {
  const ElementPtrs m = ptrsOf(r.ed, r.page, 0, id);
  const GeneratedHandler* h = GeneratedRegistry::instance().find(m);
  r.check(h && h->kind() == kc.kind, QString("%1: %2: a handler of kind %3 claims it").arg(kc.name, when, kc.kind));
  const Probe p = probeOf(m, kc.kind);
  r.check(p.ok, QString("%1: %2: its definition can be read back").arg(kc.name, when));
  if (out) *out = p;
}

void expectPts(Rig& r, const KindCase& kc, const Probe& before, const Probe& now, const Map& t, const QString& when, double eps = 0.05) {
  if (!before.ok || !now.ok || before.pts.size() != now.pts.size()) { r.check(false, QString("%1: %2: control points readable and as many as before (%3 vs %4)").arg(kc.name, when).arg(before.pts.size()).arg(now.pts.size())); return; }
  if (kc.pointwise) {
    double worst = 0;
    for (size_t i = 0; i < now.pts.size(); i++) {
      const V2 e = t(before.pts[i]);
      worst = std::max({worst, std::abs(e[0] - now.pts[i][0]), std::abs(e[1] - now.pts[i][1])});
    }
    r.check(worst <= eps, QString("%1: %2: every control point is where the gesture's map puts it (worst error %3)").arg(kc.name, when).arg(worst, 0, 'f', 4));
  } else {
    auto box = [](const std::vector<V2>& p) { std::array<double, 4> b{1e18, 1e18, -1e18, -1e18}; for (const V2& q : p) { b[0] = std::min(b[0], q[0]); b[1] = std::min(b[1], q[1]); b[2] = std::max(b[2], q[0]); b[3] = std::max(b[3], q[1]); } return b; };
    std::vector<V2> mapped;
    for (const V2& q : before.pts) mapped.push_back(t(q));
    const auto a = box(mapped), b = box(now.pts);
    double worst = 0;
    for (int i = 0; i < 4; i++) worst = std::max(worst, std::abs(a[size_t(i)] - b[size_t(i)]));
    r.check(worst <= eps, QString("%1: %2: the frame is where the gesture's map puts it (worst error %3)").arg(kc.name, when).arg(worst, 0, 'f', 4));
  }
}

QRectF selBox(Rig& r) { return r.ed.selection().bbox(); }

struct Step {
  QString name;
  std::function<Map(Rig&, const QString& id)> run;   // performs the gesture, returns the map it applied
  bool rebuilds = false;                               // resize: the object is rebuilt (stroke points are new ones)
  bool skipForPlane = false;
};


std::vector<Step> steps() {
  std::vector<Step> s;
  s.push_back({"move by a drag of a piece", [](Rig& r, const QString& id) {
    const QPointF p = r.pieceOf(id);
    const QRectF b0 = selBox(r);
    r.drag(p, p + QPointF(37, 23));
    if (qEnvironmentVariable("XL_CHAIN_DEBUG") == "1") r.out(QString("     move: grab %1,%2 box %3,%4 %5x%6 -> box %7,%8").arg(p.x()).arg(p.y()).arg(b0.x()).arg(b0.y()).arg(b0.width()).arg(b0.height()).arg(selBox(r).x()).arg(selBox(r).y()));
    return translate(37, 23);
  }, false, false});
  s.push_back({"turn 90 degrees right (R)", [](Rig& r, const QString&) {
    const QRectF b = selBox(r);
    r.trigger("sel.rotR");
    return rotateAbout(M_PI / 2, b.center().x(), b.center().y());
  }, false, true});
  s.push_back({"flip horizontally (F)", [](Rig& r, const QString&) {
    const QRectF b = selBox(r);
    r.trigger("sel.flipH");
    return flipHAbout(b.center().x());
  }, false, false});
  s.push_back({"flip vertically (Shift+F)", [](Rig& r, const QString&) {
    const QRectF b = selBox(r);
    r.trigger("sel.flipV");
    return flipVAbout(b.center().y());
  }, false, false});
  s.push_back({"turn 30 degrees with the round handle", [](Rig& r, const QString&) {
    const QRectF b = selBox(r);
    const QPointF c = b.center();
    const QPointF h(c.x(), b.top() - selx::kRotOffsetPx);
    const double R = 120;
    r.send(QEvent::MouseButtonPress, h, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 6; k++) {
      const double ang = -M_PI / 2 + (M_PI / 6) * k / 6.0;
      r.send(QEvent::MouseMove, c + QPointF(R * std::cos(ang), R * std::sin(ang)), Qt::NoButton, Qt::LeftButton);
    }
    const double ang = -M_PI / 2 + M_PI / 6;
    r.send(QEvent::MouseButtonRelease, c + QPointF(R * std::cos(ang), R * std::sin(ang)), Qt::LeftButton, Qt::NoButton);
    pump(3);
    return rotateAbout(M_PI / 6, c.x(), c.y());
  }, false, true});
  s.push_back({"resize with the corner handle (rebuilds)", [](Rig& r, const QString&) {
    const QRectF b = selBox(r);
    const QPointF h(b.right(), b.bottom()), a(b.left(), b.top());
    const QPointF to = h + QPointF(60, 40);
    r.drag(h, to);
    const double sx = (to.x() - a.x()) / (h.x() - a.x()), sy = (to.y() - a.y()) / (h.y() - a.y());
    const double k = std::max(std::abs(sx), std::abs(sy));   // a selection holding text, or a corner handle, scales in proportion
    return scaleAbout(k, k, a.x(), a.y());
  }, true, false});
  return s;
}

/** The stroke points the gesture must have produced: the first stroke's points mapped. Returns the worst error. */
double strokeError(const std::vector<double>& before, const std::vector<double>& now, const Map& t) {
  if (before.size() != now.size() || before.empty()) return 1e9;
  double w = 0;
  for (size_t i = 0; i + 1 < before.size(); i += 2) {
    const V2 e = t({before[i], before[i + 1]});
    w = std::max({w, std::abs(e[0] - now[i]), std::abs(e[1] - now[i + 1])});
  }
  return w;
}

void chainFor(Rig& r, const KindCase& kc, Document* keep) {
  const QString id = setUp(r, kc);
  auto* sel = r.sel();
  if (!sel) { r.check(false, "the editor's selection is the real one"); return; }
  const QString name = kc.name;
  const size_t nMembers = r.ed.membersOf(r.page, 0, id).size();   // as created (a resize can change it: hatching, ticks)
  const std::vector<Element> pristine = r.snap();
  const int depth0 = r.ed.undoDepth();

  // ---- click a piece: the whole object is selected and a handler claims it
  const QPointF piece = r.pieceOf(id);
  r.click(piece);
  if (sel->count() != int(nMembers)) r.out(QString("     (click at %1,%2; selection bbox %3,%4 %5x%6)").arg(piece.x()).arg(piece.y()).arg(selBox(r).x()).arg(selBox(r).y()).arg(selBox(r).width()).arg(selBox(r).height()));
  r.check(sel->count() == int(nMembers) && sel->generatedKind() == kc.kind,
          QString("%1: clicking a piece selects the whole object (%2 of %3 pieces), claimed as '%4'").arg(name).arg(sel->count()).arg(nMembers).arg(sel->generatedKind()));
  Probe cur;
  editable(r, kc, id, "as created", &cur);
  const Probe first = cur;
  Map total;   // from the pristine object to now
  std::vector<std::vector<Element>> after;
  std::vector<Map> totals;

  const bool plane = kc.kind == "plane";
  for (const Step& st : steps()) {
    if (plane && st.skipForPlane) continue;   // a plane turned by anything but 180 degrees is no longer axis aligned: see the dedicated check below
    const std::vector<Element> pre = r.snap();
    const std::vector<double> prePts = r.firstStrokePts(id);
    const Probe preProbe = cur;
    const int d0 = r.ed.undoDepth();
    sel->select(r.page, r.ed.membersOf(r.page, 0, id));
    const Map t = st.run(r, id);
    total = total.then(t);
    r.check(r.ed.undoDepth() == d0 + 1, QString("%1: %2: one undo step").arg(name, st.name));
    r.check(sel->count() == int(r.ed.membersOf(r.page, 0, id).size()) && sel->generatedKind() == kc.kind, QString("%1: %2: the object is still selected as a whole and claimed (%3 pieces, kind '%4')").arg(name, st.name).arg(sel->count()).arg(sel->generatedKind()));
    editable(r, kc, id, st.name, &cur);
    expectPts(r, kc, preProbe, cur, t, st.name);
    if (!st.rebuilds) {
      const double e = strokeError(prePts, r.firstStrokePts(id), t);
      r.check(e < 0.05, QString("%1: %2: the first stroke's points are the mapped ones (worst error %3)").arg(name, st.name).arg(e, 0, 'f', 4));
    } else {
      // figures, lines and diagrams draw their labels at a fixed size; planes and spaces size them by the cell (the same formula as a fresh build)
      std::vector<double> want = preProbe.textSizes;
      if (kc.kind == "plane" || kc.kind == "space3") {
        const ElementPtrs m = ptrsOf(r.ed, r.page, 0, id);
        std::vector<Element> fresh;
        if (kc.kind == "plane") { if (auto sp = planeSpecOf(m)) fresh = buildPlane(id, *sp, *planeFrameOf(m), *planeStyleOf(m)); }
        else if (auto sp = space3SpecOf(m)) fresh = buildSpace3(id, *sp, *space3StyleOf(m));
        want.clear();
        for (const Element& e : fresh) if (const auto* t = std::get_if<Text>(&e)) { const QString* z = attrFind(t->attrs, "size"); want.push_back(z ? z->toDouble() : 0); }
        std::sort(want.begin(), want.end());
      }
      const bool same = cur.textSizes.size() == want.size() && std::equal(cur.textSizes.begin(), cur.textSizes.end(), want.begin(), [](double a, double b) { return std::abs(a - b) < 1e-6; });
      if (!same) { QStringList a, b; for (double z : preProbe.textSizes) a << QString::number(z); for (double z : cur.textSizes) b << QString::number(z); r.out("     sizes before: " + a.join(' ') + " | sizes after: " + b.join(' ')); }
      r.check(same, QString("%1: %2: labels, numbers and names keep their size").arg(name, st.name));
    }
    // undo / redo of that one step
    const std::vector<Element> done = r.snap();
    r.ed.undo(); pump(2);
    r.check(r.snap() == pre, QString("%1: %2: undo restores exactly").arg(name, st.name));
    r.check(sel->generatedKind() == kc.kind, QString("%1: %2: after undo the object is still a selected, claimed object").arg(name, st.name));
    r.ed.redo(); pump(2);
    r.check(r.snap() == done, QString("%1: %2: redo gives the same document").arg(name, st.name));
    after.push_back(done);
    totals.push_back(total);
  }

  // ---- a plane turned by 180 degrees (two quarter turns) is axis aligned again: still one editable plane
  if (plane) {
    const std::vector<Element> pre = r.snap();
    sel->select(r.page, r.ed.membersOf(r.page, 0, id));
    const Probe p0 = probeOf(ptrsOf(r.ed, r.page, 0, id), "plane");
    const QRectF b = selBox(r);   // the turn is about the centre of the whole selection (labels included), so the frame itself moves
    r.trigger("sel.rotR");
    sel->select(r.page, r.ed.membersOf(r.page, 0, id));
    const QRectF b2 = selBox(r);   // the second turn is about the centre of the turned selection
    r.trigger("sel.rotR");
    const Probe p1 = probeOf(ptrsOf(r.ed, r.page, 0, id), "plane");
    const GeneratedHandler* h = GeneratedRegistry::instance().find(ptrsOf(r.ed, r.page, 0, id));
    double worst = 0;
    const Map half = rotateAbout(M_PI / 2, b.center().x(), b.center().y()).then(rotateAbout(M_PI / 2, b2.center().x(), b2.center().y()));
    if (p0.ok && p1.ok) {
      std::vector<V2> a, c;
      for (const V2& q : p0.pts) a.push_back(half(q));
      for (const V2& q : p1.pts) c.push_back(q);
      auto lo = [](const std::vector<V2>& v, int k) { double m = 1e18; for (const V2& q : v) m = std::min(m, q[size_t(k)]); return m; };
      auto hi = [](const std::vector<V2>& v, int k) { double m = -1e18; for (const V2& q : v) m = std::max(m, q[size_t(k)]); return m; };
      for (int k = 0; k < 2; k++) worst = std::max({worst, std::abs(lo(a, k) - lo(c, k)), std::abs(hi(a, k) - hi(c, k))});
    }
    r.check(h && p1.ok && worst < 0.05, QString("plane: turned 180 degrees it is a plane again, its frame where the turn puts it (worst %1)").arg(worst, 0, 'f', 4));
    r.ed.undo(); r.ed.undo(); pump(2);
    r.check(r.snap() == pre, "plane: undo of both quarter turns restores exactly");
  }

  // ---- a plane turned by 90 degrees: ordinary ink that must not be rebuilt into a collapsed plane
  if (plane) {
    const std::vector<Element> pre = r.snap();
    sel->select(r.page, r.ed.membersOf(r.page, 0, id));
    r.trigger("sel.rotR");
    const ElementPtrs m = ptrsOf(r.ed, r.page, 0, id);
    const GeneratedHandler* h = GeneratedRegistry::instance().find(m);
    const auto f = planeFrameOf(m);
    const bool degenerate = f && (f->x1 - f->x0 < 1 || f->y1 - f->y0 < 1);
    r.check(!h || !degenerate, "plane: turned 90 degrees it is not offered for a rebuild from a collapsed frame");
    // resizing it must not wreck it either
    const QRectF b = selBox(r);
    const size_t n0 = r.ed.membersOf(r.page, 0, id).size();
    r.drag({b.right(), b.bottom()}, {b.right() + 50, b.bottom() + 30});
    const QRectF b2 = selBox(r);
    r.check(b2.width() > 20 && b2.height() > 20 && r.ed.membersOf(r.page, 0, id).size() >= n0 / 2, QString("plane: a turned plane that is resized keeps a real size (%1 x %2)").arg(b2.width()).arg(b2.height()));
    r.ed.undo(); r.ed.undo(); pump(2);
    r.check(r.snap() == pre, "plane: undo of the turn and the resize restores exactly");
  }

  // ---- anchors (kinds that have them): dragged with the real select tool, after all the transforms above
  sel->select(r.page, r.ed.membersOf(r.page, 0, id));
  const auto anchors = sel->anchors();
  if (kc.kind != "plane") {
    r.check(!anchors.empty(), QString("%1: the selection shows %2 anchors").arg(name).arg(anchors.size()));
    // the anchors must lie on the drawn object: each one is the position the owner reads for the control point (no offset after the transforms)
    const Probe now = probeOf(ptrsOf(r.ed, r.page, 0, id), kc.kind);
    if (kc.kind == "figure" || kc.kind == "line" || kc.kind == "venn") {
      double w = 0;
      for (size_t i = 0; i < std::min(anchors.size(), now.pts.size()); i++) w = std::max({w, std::abs(anchors[i].x() - now.pts[i][0]), std::abs(anchors[i].y() - now.pts[i][1])});
      r.check(w < 0.05 && anchors.size() == now.pts.size(), QString("%1: anchors coincide with the read control points after move/turn/flip/resize (worst %2)").arg(name).arg(w, 0, 'f', 4));
    }
    // drag the last anchor
    const size_t ai = anchors.size() - 1;
    const std::vector<Element> pre = r.snap();
    const int d0 = r.ed.undoDepth();
    const QPointF from = anchors[ai], to = from + QPointF(-22, 18);
    r.drag(from, to);
    const bool changed = r.snap() != pre;
    r.check(changed && r.ed.undoDepth() == d0 + 1, QString("%1: dragging an anchor edits the object in one undo step").arg(name));
    r.check(sel->count() == int(r.ed.membersOf(r.page, 0, id).size()) && sel->generatedKind() == kc.kind, QString("%1: after the anchor drag it is still selected and claimed").arg(name));
    const bool solidOrigin = false;
    (void)solidOrigin;
    if (changed && kc.kind != "figure") {
      const auto an2 = sel->anchors();
      r.check(ai < an2.size() && near(an2[ai].x(), to.x(), 1.0) && near(an2[ai].y(), to.y(), 1.0), QString("%1: the dragged anchor ends where the pointer was released").arg(name));
    }
    if (changed && kc.kind == "figure") {
      const auto an2 = sel->anchors();
      const bool solid = kc.name.contains("box") || kc.name.contains("pyramid") || kc.name.contains("cylinder") || kc.name.contains("cone") || kc.name.contains("sphere");
      if (!solid) r.check(ai < an2.size() && near(an2[ai].x(), to.x(), 1.0) && near(an2[ai].y(), to.y(), 1.0), QString("%1: the dragged anchor ends where the pointer was released (%2,%3 vs %4,%5)").arg(name).arg(an2.empty() ? 0 : an2[ai].x()).arg(an2.empty() ? 0 : an2[ai].y()).arg(to.x()).arg(to.y()));
    }
    Probe afterAnchor;
    editable(r, kc, id, "after an anchor drag", &afterAnchor);
    r.ed.undo(); pump(2);
    r.check(r.snap() == pre, QString("%1: undo of the anchor drag restores exactly").arg(name));
    r.ed.redo(); pump(2);
    r.check(r.ed.membersOf(r.page, 0, id).size() > 0 && sel->generatedKind() == kc.kind, QString("%1: redo of the anchor drag keeps it editable").arg(name));
  }

  // ---- the whole chain undone and redone
  const std::vector<Element> endState = r.snap();
  const int chainSteps = r.ed.undoDepth() - depth0;
  while (r.ed.undoDepth() > depth0) r.ed.undo();
  pump(2);
  r.check(r.snap() == pristine, QString("%1: undoing the whole chain returns the object exactly as it was created").arg(name));
  for (int i = 0; i < chainSteps; i++) r.ed.redo();
  pump(2);
  r.check(r.snap() == endState, QString("%1: redoing the whole chain returns to the final state").arg(name));

  // ---- copy, paste, duplicate: new independent objects that stay editable
  sel->select(r.page, r.ed.membersOf(r.page, 0, id));
  const Probe orig = probeOf(ptrsOf(r.ed, r.page, 0, id), kc.kind);
  const std::vector<Element> beforeCopy = r.snap();
  r.trigger("edit.duplicate");
  std::set<QString> ids;
  for (const Element& e : r.els()) if (const auto i = planeIdOf(e)) ids.insert(*i);
  r.check(ids.size() == 2, QString("%1: duplicate makes a second object with a fresh id (%2 ids)").arg(name).arg(ids.size()));
  QString copyId;
  for (const QString& i : ids) if (i != id) copyId = i;
  r.check(!copyId.isEmpty() && sel->count() == int(r.ed.membersOf(r.page, 0, copyId).size()) && sel->generatedKind() == kc.kind, QString("%1: the duplicate is selected and claimed as a whole").arg(name));
  if (!copyId.isEmpty()) {
    const Probe cp = probeOf(ptrsOf(r.ed, r.page, 0, copyId), kc.kind);
    r.check(cp.ok && r.ed.membersOf(r.page, 0, id).size() == r.ed.membersOf(r.page, 0, copyId).size(), QString("%1: both objects have all their pieces and their own definition").arg(name));
    // editing the copy (an anchor, or a 90 degree turn for a plane) must leave the original byte for byte alone
    const std::vector<Element> origPieces = [&] { std::vector<Element> o; for (const ElementRef& e : r.ed.membersOf(r.page, 0, id)) o.push_back(r.els()[size_t(e.index)]); return o; }();
    const auto an = sel->anchors();
    if (!an.empty() && kc.kind != "plane") r.drag(an.back(), an.back() + QPointF(15, 12));
    else r.trigger("sel.flipH");
    std::vector<Element> origNow;
    for (const ElementRef& e : r.ed.membersOf(r.page, 0, id)) origNow.push_back(r.els()[size_t(e.index)]);
    r.check(origNow == origPieces, QString("%1: editing the copy does not touch the original").arg(name));
    editable(r, kc, copyId, "the copy after an edit", nullptr);
    r.ed.undo(); pump(2);
  }
  r.ed.undo(); pump(2);   // the duplicate itself
  r.check(r.snap() == beforeCopy, QString("%1: undo of the duplicate leaves only the original").arg(name));
  // copy + paste twice
  sel->select(r.page, r.ed.membersOf(r.page, 0, id));
  r.trigger("edit.copy");
  r.trigger("edit.paste");
  r.trigger("edit.paste");
  std::set<QString> ids3;
  for (const Element& e : r.els()) if (const auto i = planeIdOf(e)) ids3.insert(*i);
  r.check(ids3.size() == 3, QString("%1: copy and two pastes give three distinct objects (%2)").arg(name).arg(ids3.size()));
  for (const QString& i : ids3) {
    const GeneratedHandler* h = GeneratedRegistry::instance().find(ptrsOf(r.ed, r.page, 0, i));
    r.check(h && h->kind() == kc.kind, QString("%1: object %2 is claimed by its handler").arg(name, i));
  }
  (void)orig;
  r.ed.undo(); r.ed.undo(); pump(2);
  r.check(r.snap() == beforeCopy, QString("%1: undo of both pastes").arg(name));

  if (keep) {
    std::vector<Element> fin = r.snap();
    appendPage(*keep);
    keep->pages.back().layers[0].elements = std::move(fin);
  }
}


// ---- the paint bucket and Riemann rectangles over the real generated objects (the README: the plane's axes and grid are not walls; figures and diagrams are ink)
struct Shot {
  QImage img;
  QColor at(double x, double y) const { return QColor::fromRgb(img.pixel(std::clamp(int(x), 0, img.width() - 1), std::clamp(int(y), 0, img.height() - 1))); }
  bool white(double x, double y) const { const QColor q = at(x, y); return q.red() > 240 && q.green() > 240 && q.blue() > 240; }
  bool greenish(double x, double y) const { const QColor q = at(x, y); return q.green() > q.red() + 40 && q.green() > q.blue() + 20; }   // the fill colour #00aa00 under or over a faint blue grid
};
Shot renderPageShot(Rig& r) {
  const Page& pg = r.ed.doc().pages[size_t(r.page)];
  Shot s;
  s.img = QImage(int(pg.width()), int(pg.height()), QImage::Format_ARGB32_Premultiplied);
  s.img.fill(Qt::white);
  QPainter p(&s.img);
  RenderOptions o;
  o.scale = 1;
  o.background = false;
  o.glow = false;
  renderLayers(p, pg, o);
  return s;
}
Stroke inkStroke(const std::vector<double>& pts) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "round"}};
  s.widths = {1.41};
  s.pts = pts;
  return s;
}

void fillChecks(Rig& r) {
  r.out("-- paint bucket and Riemann over generated objects");
  const Style savedStyle = r.ed.style();
  Style st = savedStyle;
  st.color = "#00aa00";
  st.penWidth = 1.41;
  st.glowOn = false;
  r.ed.setStyle(st);
  const FillOptions savedFill = state().fill;
  const GenStyle gs{"#0044cc", 1.41, std::nullopt};
  auto fresh = [&](std::vector<Element> els) {
    Document doc = blankDocument();
    for (Element& e : els) doc.pages[0].layers[0].elements.push_back(std::move(e));
    r.ed.setDocument(std::move(doc));
    r.page = 0;
    r.v.setZoom(1.0);
    r.v.scrollToPage(0);
    r.ed.setCurrentPage(0);
    pump(6);
  };
  auto images = [&] { int n = 0; for (const Element& e : r.els()) if (std::holds_alternative<Image>(e)) n++; return n; };

  // 1. a click inside a circle that a plane's axes and grid cross
  {
    std::vector<Element> els = buildPlane("plF0001", DEFAULT_PLANE, Frame{100, 100, 500, 500}, gs);
    std::vector<double> circle;
    for (int i = 0; i <= 90; i++) { const double a = 2 * M_PI * i / 90; circle.push_back(300 + 90 * std::cos(a)); circle.push_back(300 + 90 * std::sin(a)); }
    els.push_back(inkStroke(circle));
    fresh(std::move(els));
    state().fill = FillOptions{};
    state().fill.magnet = false;
    r.ed.setTool("fill");
    r.click({320, 320});
    const Shot sh = renderPageShot(r);
    r.check(images() == 1 && state().lastFill && !state().lastFill->wholePage, "fill: a click inside a circle crossed by a plane fills the circle only (not the whole page)");
    r.check(sh.greenish(320, 320) && sh.greenish(360, 320) && sh.greenish(260, 320) && sh.greenish(320, 270), "fill: the fill goes across the plane's grid lines and axes (they are not walls)");
    r.check(sh.white(320, 430) && sh.white(430, 320), "fill: it stops at the circle");
    r.ed.undo();
    r.check(images() == 0, "fill: undo takes the fill away");
  }
  // 2. Riemann rectangles on a plane: they stop at the drawn curve, not at the grid
  {
    std::vector<Element> els = buildPlane("plF0002", DEFAULT_PLANE, Frame{100, 100, 500, 500}, gs);
    std::vector<double> hump;
    for (int i = 0; i <= 60; i++) { hump.push_back(130 + i / 60.0 * 240); hump.push_back(300 - 90 * std::sin(M_PI * i / 60.0)); }
    els.push_back(inkStroke(hump));
    fresh(std::move(els));
    state().fill = FillOptions{};
    state().fill.kind = FillKind::Riemann;
    state().fill.rule = RiemannRule::Upper;
    state().fill.count = 6;
    state().fill.outline = true;
    state().fill.magnet = true;
    state().lastRiemann.reset();
    r.ed.setTool("fill");
    const int d0 = r.ed.undoDepth();
    r.drag({130, 300}, {370, 300}, 8);
    const auto rm = state().lastRiemann;
    auto expected = [](double x0, double x1) { double m = 0; for (double x = x0; x <= x1; x += 0.5) m = std::max(m, 90 * std::sin(M_PI * (x - 130) / 240)); return m; };
    bool heights = rm && rm->drawn == 6 && rm->n == 6;
    double worst = 0;
    if (heights) for (size_t i = 0; i < rm->cols.size(); i++) worst = std::max(worst, std::abs(rm->cols[i].h - expected(130 + double(i) * 40, 170 + double(i) * 40)));
    r.check(heights && worst < 4, QString("riemann on a plane: six rectangles reach the curve, not a grid line (worst height error %1)").arg(worst, 0, 'f', 2));
    r.check(r.ed.undoDepth() == d0 + 1, "riemann on a plane: one undo step");
    r.ed.undo();
    r.check(images() == 0, "riemann on a plane: undo");
  }
  // 3. the lines of a figure are ordinary ink (walls); the same for a set diagram's circles
  {
    FigureSpec fs = defaultSpec(FigKind::Polygon, V2{100, 100}, V2{300, 300});
    fresh(buildFigure("fgF0001", sanitizeFigure(fs), gs));
    // inside point: the centroid of the control points
    const auto rd = figureSpecOf(ptrsOf(r.ed, 0, 0, "fgF0001"));
    double cx = 0, cy = 0;
    size_t n = 0;
    if (rd) for (size_t i = 0; i + 1 < rd->spec.pts.size(); i += 2) { cx += rd->spec.pts[i]; cy += rd->spec.pts[i + 1]; n++; }
    if (n) { cx /= double(n); cy /= double(n); }
    state().fill = FillOptions{};
    state().fill.magnet = false;
    r.ed.setTool("fill");
    r.click({cx, cy});
    const Shot sh = renderPageShot(r);
    r.check(images() == 1 && state().lastFill && !state().lastFill->wholePage && sh.greenish(cx, cy) && sh.white(30, 30) && sh.white(450, 450), "fill: a closed polygon figure is a wall: the click fills its inside only");
    r.ed.undo();
  }
  {
    sets::VennSpec vs = sets::defaultVenn(2, {100, 100}, {400, 340});
    vs.expr = "A ∩ B";
    fresh(sets::buildMath("vnF0001", sets::sanitizeVenn(vs), gs));
    state().fill = FillOptions{};
    state().fill.magnet = false;
    r.ed.setTool("fill");
    // the centre of circle A, away from the hatching of A ∩ B
    const auto an = sets::mathAnchors(sets::sanitizeVenn(vs));
    const V2 ca = an[2];   // centre of circle A: after the two universe corners
    r.click({ca[0] - 30, ca[1]});
    r.check(images() == 1 && state().lastFill && !state().lastFill->wholePage, "fill: the circles of a set diagram are walls (the fill stays inside circle A, the hatching is paper)");
    r.ed.undo();
  }
  state().fill = savedFill;
  state().lastFill.reset();
  state().lastRiemann.reset();
  r.ed.setStyle(savedStyle);
}

// ---- pen style: glow survives every rebuild, and "Estilo del lápiz" gives the selected object the pen of the moment
void styleChecks(Rig& r) {
  r.out("-- pen style: glow, resize and 'Estilo del lapiz'");
  const Style savedStyle = r.ed.style();
  const GenStyle glowing{"#0044cc", 1.41, Glow{3, 0.6}};
  const auto kinds = [&] {
    std::vector<KindCase> k;
    k.push_back({"plane", "plane", false, [glowing](const QString& id) { return buildPlane(id, DEFAULT_PLANE, Frame{150, 150, 450, 400}, glowing); }});
    k.push_back({"space3", "space3", true, [glowing](const QString& id) { return buildSpace3(id, placedSpec(DEFAULT_SPACE3, 1.0, V2{150, 160}), glowing); }});
    k.push_back({"figure box", "figure", true, [glowing](const QString& id) { return buildFigure(id, sanitizeFigure(defaultSpec(FigKind::Box, V2{160, 170}, V2{420, 360})), glowing); }});
    k.push_back({"figure arrow", "figure", true, [glowing](const QString& id) { return buildFigure(id, sanitizeFigure(defaultSpec(FigKind::Arrow, V2{160, 170}, V2{420, 360})), glowing); }});
    k.push_back({"number line", "line", true, [glowing](const QString& id) { sets::LineSpec l = sets::defaultLine({150, 250}, {450, 250}); l.rows = {{"(-2, 3]", "A"}}; return sets::buildMath(id, sets::sanitizeLine(l), glowing); }});
    k.push_back({"venn", "venn", true, [glowing](const QString& id) { sets::VennSpec v = sets::defaultVenn(2, {150, 150}, {450, 360}); v.expr = "A ∩ B"; return sets::buildMath(id, sets::sanitizeVenn(v), glowing); }});
    return k;
  }();
  // every visible stroke carries the glow; the invisible witness need not
  auto glowEverywhere = [&](const QString& id, double size, const QString& color) {
    int visible = 0, withGlow = 0, wrongColor = 0;
    for (const Element& e : r.els()) {
      const auto* s = std::get_if<Stroke>(&e);
      if (!s || planeIdOf(e) != std::optional<QString>(id)) continue;
      const QString* col = attrFind(s->attrs, "color");
      if (col && col->right(2) == "00") continue;
      if (roleOf(e).value_or(QString()).contains("grid")) continue;   // the light grid never glows (the Electron builders draw it plain too)
      visible++;
      const QString* g = attrFind(s->attrs, "glow");
      if (g && std::abs(g->toDouble() - size) < 1e-6) withGlow++;
      if (!col || col->left(7) != color) wrongColor++;
    }
    return std::array<int, 3>{visible, withGlow, wrongColor};
  };
  for (const KindCase& kc : kinds) {
    const QString id = setUp(r, kc);
    auto* sel = r.sel();
    r.click(r.pieceOf(id));
    const auto g0 = glowEverywhere(id, 3, "#0044cc");
    r.check(g0[0] > 0 && g0[0] == g0[1], QString("%1: built with glow, every visible stroke has it (%2 of %3)").arg(kc.name).arg(g0[1]).arg(g0[0]));
    // resize: the rebuild keeps the glow
    sel->select(r.page, r.ed.membersOf(r.page, 0, id));
    steps().back().run(r, id);
    const auto g1 = glowEverywhere(id, 3, "#0044cc");
    r.check(g1[0] > 0 && g1[0] == g1[1], QString("%1: after a resize the rebuilt strokes keep the glow (%2 of %3)").arg(kc.name).arg(g1[1]).arg(g1[0]));
    // "Estilo del lapiz": the pen of the moment (another colour, glow 5)
    Style st = savedStyle;
    st.color = "#cc2200";
    st.penWidth = 2.0;
    st.glowOn = true;
    st.glowSize = 5;
    st.glowIntensity = 0.5;
    r.ed.setStyle(st);
    sel->select(r.page, r.ed.membersOf(r.page, 0, id));
    const ElementPtrs m = ptrsOf(r.ed, r.page, 0, id);
    bool ok = false;
    auto& hub = xlobjects::Hub::of(r.ed);
    if (kc.kind == "plane") { if (const auto sp = planeSpecOf(m)) ok = hub.updatePlane(*sp, true); }
    else if (kc.kind == "space3") { if (const auto sp = space3SpecOf(m)) ok = hub.updateSpace3(*sp, true); }
    else if (kc.kind == "figure") { if (const auto rd = figureSpecOf(m)) ok = hub.updateFigure(rd->spec, true); }
    else if (const auto rd = sets::readMath(m)) ok = updateMath(r.ed, rd->spec, true);
    const auto g2 = glowEverywhere(id, 5, "#cc2200");
    r.check(ok && g2[0] > 0 && g2[1] == g2[0] && g2[2] == 0, QString("%1: 'Estilo del lapiz' gives colour and glow of the pen to every stroke (%2 glow, %3 wrong colour of %4)").arg(kc.name).arg(g2[1]).arg(g2[2]).arg(g2[0]));
    bool textsOk = true;
    for (const Element& e : r.els()) if (const auto* t = std::get_if<Text>(&e)) if (planeIdOf(e) == std::optional<QString>(id)) { const QString* c = attrFind(t->attrs, "color"); textsOk = textsOk && c && c->left(7) == "#cc2200"; }
    r.check(textsOk, QString("%1: its numbers and labels take the colour too").arg(kc.name));
    r.check(sel->generatedKind() == kc.kind, QString("%1: still one selected object after restyling").arg(kc.name));
    r.ed.undo(); pump(2);
    const auto g3 = glowEverywhere(id, 3, "#0044cc");
    r.check(g3[0] == g3[1] && g3[2] == 0, QString("%1: undo of the restyle brings the old look back").arg(kc.name));
    r.ed.setStyle(savedStyle);
  }
  r.ed.setStyle(savedStyle);
}

// ---- cost of a fill on a dense page (the README: ~130 ms for an A4); where the time goes, and that it does not grow with what the fill leaves alone
void fillCostChecks(Rig& r) {
  r.out("-- fill on a dense page");
  Document doc = blankDocument();
  std::vector<double> circle;
  for (int i = 0; i <= 90; i++) { const double a = 2 * M_PI * i / 90; circle.push_back(300 + 90 * std::cos(a)); circle.push_back(300 + 90 * std::sin(a)); }
  auto& els = doc.pages[0].layers[0].elements;
  els.push_back(inkStroke(circle));
  const int N = 8000;
  for (int i = 0; i < N; i++) {
    std::vector<double> pts;
    const double x = 20 + (i % 100) * 5.2, y = 460 + (i / 100) * 4.0;
    for (int k = 0; k < 20; k++) { pts.push_back(x + k * 0.2); pts.push_back(y + std::sin(k * 0.7)); }
    els.push_back(inkStroke(pts));
  }
  r.ed.setDocument(std::move(doc));
  r.page = 0;
  r.v.setZoom(1.0);
  r.v.scrollToPage(0);
  r.ed.setCurrentPage(0);
  pump(8);
  const FillOptions saved = state().fill;
  state().fill = FillOptions{};
  state().fill.magnet = false;
  r.ed.setTool("fill");
  const Page& pg = r.ed.doc().pages[0];
  QElapsedTimer t;
  t.start();
  FillRequest req;
  req.opt = state().fill;
  req.penColor = "#00aa00";
  const FillResult fr = runFill(pg.layers[0], pg.width(), pg.height(), {300, 300}, req);
  const double runMs = double(t.nsecsElapsed()) / 1e6;
  t.restart();
  r.click({300, 300});
  const double clickMs = double(t.nsecsElapsed()) / 1e6;
  r.out(QString("   %1 strokes: runFill alone %2 ms (stats %3), whole click through the tool and the event loop %4 ms").arg(N).arg(runMs, 0, 'f', 1).arg(fr.stats.ms, 0, 'f', 1).arg(clickMs, 0, 'f', 1));
  r.check(fr.ok, "fill on a dense page works");
  r.ed.undo();
  state().fill = saved;
  state().lastFill.reset();
}

// ---- Shift while dragging an anchor: a 3D axis keeps its length and only turns; a segment goes in steps of 15 degrees; a number line keeps 15 degree steps
void shiftChecks(Rig& r) {
  r.out("-- Shift on anchors");
  const GenStyle gs{"#0044cc", 1.41, std::nullopt};
  {
    KindCase kc{"space3", "space3", true, [gs](const QString& id) { return buildSpace3(id, placedSpec(DEFAULT_SPACE3, 1.0, V2{150, 160}), gs); }};
    const QString id = setUp(r, kc);
    r.click(r.pieceOf(id));
    const auto before = space3SpecOf(ptrsOf(r.ed, 0, 0, id));
    const auto an = r.sel()->anchors();
    // anchor 0 is the positive half of x; drag it far away with Shift
    const QPointF to = an[0] + QPointF(60, -50);
    r.drag(an[0], to, 5, Qt::ShiftModifier);
    const auto after = space3SpecOf(ptrsOf(r.ed, 0, 0, id));
    r.check(before && after, "shift: space3 still readable");
    if (before && after) {
      const double l0 = std::hypot(before->u[0][0], before->u[0][1]), l1 = std::hypot(after->u[0][0], after->u[0][1]);
      const double a0 = std::atan2(before->u[0][1], before->u[0][0]), a1 = std::atan2(after->u[0][1], after->u[0][0]);
      r.check(near(l0, l1, 0.01) && std::abs(a0 - a1) > 0.1, QString("shift: a 3D axis keeps its length and turns (length %1 -> %2, angle %3 -> %4 rad)").arg(l0).arg(l1).arg(a0).arg(a1));
    }
  }
  {
    KindCase kc{"segment", "figure", true, [gs](const QString& id) { return buildFigure(id, sanitizeFigure(defaultSpec(FigKind::Segment, V2{200, 200}, V2{400, 260})), gs); }};
    const QString id = setUp(r, kc);
    r.click(r.pieceOf(id));
    const auto an = r.sel()->anchors();
    r.drag(an[1], QPointF(an[0].x() + 150, an[0].y() + 70), 5, Qt::ShiftModifier);
    const auto rd = figureSpecOf(ptrsOf(r.ed, 0, 0, id));
    bool ok = false;
    double deg = 0;
    if (rd && rd->spec.pts.size() >= 4) {
      deg = std::atan2(rd->spec.pts[3] - rd->spec.pts[1], rd->spec.pts[2] - rd->spec.pts[0]) * 180 / M_PI;
      ok = std::abs(deg / 15 - std::round(deg / 15)) < 0.01;
    }
    r.check(ok, QString("shift: a segment end goes in steps of 15 degrees (%1)").arg(deg));
  }
  {
    KindCase kc{"line", "line", true, [gs](const QString& id) { return sets::buildMath(id, sets::sanitizeLine(sets::defaultLine({150, 300}, {450, 300})), gs); }};
    const QString id = setUp(r, kc);
    r.click(r.pieceOf(id));
    const auto an = r.sel()->anchors();
    r.drag(an[1], QPointF(an[0].x() + 250, an[0].y() + 100), 5, Qt::ShiftModifier);
    const auto rd = sets::readMath(ptrsOf(r.ed, 0, 0, id));
    bool ok = false;
    double deg = 0;
    if (rd) { const auto& l = std::get<sets::LineSpec>(rd->spec); deg = std::atan2(l.pts[3] - l.pts[1], l.pts[2] - l.pts[0]) * 180 / M_PI; ok = std::abs(deg / 15 - std::round(deg / 15)) < 0.01; }
    r.check(ok, QString("shift: a number line end goes in steps of 15 degrees (%1)").arg(deg));
  }
}

// ---- screenshots to look at: the selection frame and the anchors of each kind, after a move and a turn
void screenshotChecks(Rig& r, const QString& shots) {
  QDir().mkpath(shots);
  auto kinds = allKinds();
  const QStringList want{"space3", "figure box", "figure cylinder", "number line", "venn 3", "plane"};
  for (const KindCase& kc : kinds) {
    if (!want.contains(kc.name)) continue;
    const QString id = setUp(r, kc);
    r.click(r.pieceOf(id));
    steps()[0].run(r, id);                                           // moved
    if (kc.name != "plane") steps()[4].run(r, id);                   // and turned 30 degrees with the handle
    pump(6);
    r.v.renderVisibleNow();
    pump(4);
    QString n = kc.name;
    n.replace(' ', '_');
    r.check(r.win.grab().save(shots + "/chain_" + n + ".png"), "screenshot " + n);
  }
}

// ---- UI-level items of the Electron e2e (PL / PF blocks) that need the real plane tool and fill tool on the real page
void planeToolChecks(Rig& r) {
  r.out("-- plane tool gestures, sharp corners, Riemann base on an axis");
  const GenStyle gs{"#0044cc", 1.41, std::nullopt};
  {
    // the corners of a generated polyline stay corners (they used to come out rounded through the midpoints)
    Stroke s = inkStroke({100, 100, 100, 200, 200, 200});
    s.widths = {6.0};
    attrSet(s.attrs, "xlPlane", "plSharp");
    attrSet(s.attrs, "xlRole", "figbody");
    Document doc = blankDocument();
    doc.pages[0].layers[0].elements.push_back(s);
    r.ed.setDocument(std::move(doc));
    r.page = 0;
    const Shot sh = renderPageShot(r);
    r.check(!sh.white(100, 200) && !sh.white(100, 197) && sh.white(110, 190), "generated strokes keep sharp corners (the vertex is inked, the inside of the corner is not)");
  }
  {
    // a plane dragged toward the upper left grows from where the drag began, by the longer side
    Document doc = blankDocument();
    r.ed.setDocument(std::move(doc));
    r.page = 0;
    r.v.setZoom(1.0);
    r.v.scrollToPage(0);
    r.ed.setCurrentPage(0);
    pump(4);
    xlobjects::Hub::of(r.ed).plane = DEFAULT_PLANE;
    r.ed.setTool("plane");
    r.drag({400, 350}, {100, 150}, 8);
    const ElementPtrs m = ptrsOf(r.ed, 0, 0, r.idOf());
    const auto f = planeFrameOf(m);
    r.check(f && near(f->x1, 400, 0.05) && near(f->y1, 350, 0.05) && near(f->x1 - f->x0, f->y1 - f->y0, 0.05) && near(f->x1 - f->x0, 300, 0.05),
            QString("plane: dragged to the upper left it grows from the start point by the longer side (frame %1,%2 - %3,%4)").arg(f ? f->x0 : 0).arg(f ? f->y0 : 0).arg(f ? f->x1 : 0).arg(f ? f->y1 : 0));
    r.ed.undo();
  }
  {
    // Riemann: a base dragged near the x axis of a plane snaps onto the axis
    std::vector<Element> els = buildPlane("plF0003", DEFAULT_PLANE, Frame{100, 100, 500, 500}, gs);
    std::vector<double> hump;
    for (int i = 0; i <= 60; i++) { hump.push_back(130 + i / 60.0 * 240); hump.push_back(300 - 90 * std::sin(M_PI * i / 60.0)); }
    els.push_back(inkStroke(hump));
    Document doc = blankDocument();
    for (Element& e : els) doc.pages[0].layers[0].elements.push_back(std::move(e));
    r.ed.setDocument(std::move(doc));
    r.page = 0;
    r.v.scrollToPage(0);
    pump(4);
    const FillOptions saved = state().fill;
    state().fill = FillOptions{};
    state().fill.kind = FillKind::Riemann;
    state().fill.count = 6;
    state().fill.magnet = true;
    state().lastRiemann.reset();
    r.ed.setTool("fill");
    r.drag({130, 303}, {370, 303}, 8);   // the axis is at y = 300: three units off, well inside the magnet's reach
    const auto rm = state().lastRiemann;
    bool onAxis = rm && !rm->cols.empty();
    if (onAxis) for (const RiemannCol& c : rm->cols) onAxis = onAxis && near(c.a[1], 300, 0.6) && near(c.b[1], 300, 0.6);
    r.check(onAxis, "riemann: a base dragged near the x axis of a plane sits exactly on the axis");
    state().fill = saved;
    state().lastRiemann.reset();
  }
}

}  // namespace

int chainSelfTest(MainWindow& win, Editor& ed, const QString& shots, const std::function<void(const QString&)>& out) {
  out("\n== chain: select -> generated handlers for every object kind (real mouse events on the view; not a physical pen) ==");
  Rig r{win, ed, *win.view(), out};
  win.resize(1400, 950);
  win.show();
  pump(6);
  if (!r.sel()) { out("  FAIL  the real selection is not installed"); return 1; }
  const Style savedStyle = ed.style();
  const auto kinds = allKinds();
  if (!qEnvironmentVariable("XL_CHAIN_PRISTINE").isEmpty()) {   // the builders' output with the ids and parameters of the Electron side (make in math_entry.ts)
    Document pristine = blankDocument();
    pristine.pages.clear();
    for (size_t i = 0; i < kinds.size(); i++) {
      appendPage(pristine);
      pristine.pages.back().layers[0].elements = kinds[i].build(QString("plE%1").arg(int(i), 3, 10, QChar('0')));
    }
    QString e;
    saveXopp(qEnvironmentVariable("XL_CHAIN_PRISTINE"), pristine, &e);
  }
  Document keep = blankDocument();
  keep.pages.clear();
  for (const KindCase& kc : kinds) {
    const int f0 = r.fail;
    chainFor(r, kc, &keep);
    out(QString("  %1 %2").arg(r.fail == f0 ? "ok   " : "FAILED", kc.name));
  }

  // ---- save, reopen, repair
  out("-- save, reopen, repair (every kind, after the chain)");
  QTemporaryDir tmp;
  const QString path = tmp.path() + "/chain.xopp";
  QString err;
  r.check(saveXopp(path, keep, &err), "save the document with every kind of object: " + err);
  LoadResult lr = loadXopp(path);
  r.check(lr.ok && lr.doc == keep, "reopening gives the same document");
  if (!qEnvironmentVariable("XL_CHAIN_SAVE").isEmpty()) {
    // for the interchange check (tools/interchange/run_math_interchange.mjs): the file, and what the C++ readers see in it
    const QString f = qEnvironmentVariable("XL_CHAIN_SAVE");
    saveXopp(f, keep, &err);
    QJsonArray arr;
    for (size_t i = 0; i < keep.pages.size() && i < kinds.size(); i++) {
      ElementPtrs m;
      for (const Element& e : keep.pages[i].layers[0].elements) m.push_back(&e);
      const Probe pr = probeOf(m, kinds[i].kind);
      QJsonArray pts;
      for (const V2& q : pr.pts) { pts.append(q[0]); pts.append(q[1]); }
      arr.append(QJsonObject{{"page", int(i)}, {"id", kinds[i].name}, {"kind", kinds[i].kind}, {"ok", pr.ok}, {"pts", pts}});
    }
    QFile jf(f + ".probe.json");
    if (jf.open(QIODevice::WriteOnly)) jf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
  }
  // objects made by the Electron app (tools/interchange/run_math_interchange.mjs): read by the C++ handlers, same definitions
  if (!qEnvironmentVariable("XL_CHAIN_ELECTRON").isEmpty()) {
    const QString f = qEnvironmentVariable("XL_CHAIN_ELECTRON");
    QFile jf(f + ".probe.json");
    const LoadResult el = loadXopp(f);
    r.check(el.ok && jf.open(QIODevice::ReadOnly), "the file the Electron app wrote opens in C++");
    if (el.ok && jf.isOpen()) {
      const QJsonArray arr = QJsonDocument::fromJson(jf.readAll()).array();
      r.check(int(arr.size()) == int(el.doc.pages.size()), QString("one object per page (%1 pages)").arg(el.doc.pages.size()));
      ed.setDocument(el.doc);
      ed.setTool("select");
      for (int i = 0; i < arr.size() && size_t(i) < el.doc.pages.size(); i++) {
        const QJsonObject o = arr[i].toObject();
        const QString kind = o["kind"].toString(), name = o["id"].toString();
        ElementPtrs m;
        for (const Element& e : el.doc.pages[size_t(i)].layers[0].elements) m.push_back(&e);
        const GeneratedHandler* h = GeneratedRegistry::instance().find(m);
        r.check(h && h->kind() == kind, QString("Electron-made %1: the C++ handler '%2' claims it (got '%3')").arg(name, kind, h ? h->kind() : QString("none")));
        const Probe pr = probeOf(m, kind);
        const QJsonArray want = o["pts"].toArray();
        bool same = pr.ok && int(pr.pts.size() * 2) == int(want.size());
        double worst = 0;
        for (size_t k = 0; same && k < pr.pts.size(); k++) {
          worst = std::max({worst, std::abs(pr.pts[k][0] - want[int(2 * k)].toDouble()), std::abs(pr.pts[k][1] - want[int(2 * k + 1)].toDouble())});
        }
        r.check(same && worst < 0.01, QString("Electron-made %1: C++ reads the same control points (worst %2, %3 vs %4 numbers)").arg(name).arg(worst).arg(pr.pts.size() * 2).arg(want.size()));
        // and the select tool treats it as one editable object
        r.page = i;
        win.view()->scrollToPage(i);
        pump(3);
        r.click(r.pieceOf(r.idOf()));
        r.check(r.sel()->generatedKind() == kind, QString("Electron-made %1: a click selects it as '%2' (got '%3')").arg(name, kind, r.sel()->generatedKind()));
        if (r.sel()->generatedKind() == kind && kind != "plane") {
          const auto an = r.sel()->anchors();
          if (!an.empty()) {
            const std::vector<Element> pre = r.els();
            r.drag(an.back(), an.back() + QPointF(14, 11));
            r.check(r.els() != pre, QString("Electron-made %1: its last anchor can be dragged").arg(name));
            r.ed.undo(); pump(2);
          }
        }
        ed.selection().clear();
      }
    }
  }
  if (lr.ok) {
    ed.setDocument(lr.doc);
    ed.setTool("select");
    for (size_t i = 0; i < kinds.size() && i < lr.doc.pages.size(); i++) {
      r.page = int(i);
      win.view()->scrollToPage(int(i));
      pump(3);
      std::set<QString> ids;
      for (const Element& e : lr.doc.pages[i].layers[0].elements) if (const auto id = planeIdOf(e)) ids.insert(*id);
      const QString id = ids.empty() ? QString() : *ids.begin();
      const QPointF p = r.pieceOf(id);
      r.click(p);
      r.check(ids.size() == 1 && r.sel()->generatedKind() == kinds[i].kind && r.sel()->count() == int(ed.membersOf(int(i), 0, id).size()),
              QString("reopened: %1: a click selects it as a '%2' object (got '%3')").arg(kinds[i].name, kinds[i].kind, r.sel()->generatedKind()));
      ed.selection().clear();
    }
    // labels stripped like the old bug, then repaired
    Document old = lr.doc;
    int withText = 0;
    std::vector<Probe> before;
    for (size_t i = 0; i < old.pages.size(); i++) {
      auto& els = old.pages[i].layers[0].elements;
      bool any = false;
      if (kinds[i].kind != "plane" && kinds[i].kind != "space3" && kinds[i].kind != "figure") continue;   // only these were ever written without label positions
      for (Element& e : els)
        if (auto* t = std::get_if<Text>(&e)) {
          Attrs kept;
          for (auto& a : t->attrs) if (a.first != "x" && a.first != "y" && a.first != "font" && a.first != "size" && a.first != "color") kept.push_back(a);
          t->attrs = kept;
          any = true;
        }
      if (any) withText++;
    }
    const int fixed = repairGenerated(old);
    r.check(fixed == withText, QString("repair: every object that lost its label positions is rebuilt (%1 of %2)").arg(fixed).arg(withText));
    r.check(repairGenerated(old) == 0, "repair: a second pass changes nothing");
    int bad = 0;
    for (size_t i = 0; i < old.pages.size(); i++) {
      std::set<QString> ids;
      for (const Element& e : old.pages[i].layers[0].elements) {
        if (const auto id = planeIdOf(e)) ids.insert(*id);
        if (lacksPlace(e) && std::holds_alternative<Text>(e)) bad++;
      }
      // the repaired object is as editable as the original and sits where it was
      ElementPtrs m;
      for (const Element& e : old.pages[i].layers[0].elements) m.push_back(&e);
      ElementPtrs orig;
      for (const Element& e : lr.doc.pages[i].layers[0].elements) orig.push_back(&e);
      const Probe a = probeOf(m, kinds[i].kind), b = probeOf(orig, kinds[i].kind);
      bool same = a.ok && b.ok && a.pts.size() == b.pts.size();
      for (size_t k = 0; same && k < a.pts.size(); k++) same = near(a.pts[k][0], b.pts[k][0], 0.05) && near(a.pts[k][1], b.pts[k][1], 0.05);
      if (!same) { r.check(false, QString("repair: %1 reads back the same control points").arg(kinds[i].name)); }
    }
    r.check(bad == 0, "repair: no label is left without a position");
  }

  fillChecks(r);
  styleChecks(r);
  shiftChecks(r);
  screenshotChecks(r, shots);
  planeToolChecks(r);
  fillCostChecks(r);
  if (!qEnvironmentVariable("XL_CHAIN_FILE").isEmpty()) {   // a real file of the user (read only): what the handlers make of every object in it
    const LoadResult lf = loadXopp(qEnvironmentVariable("XL_CHAIN_FILE"));
    r.check(lf.ok, "file opens");
    if (lf.ok) {
      Document d = lf.doc;
      int broken = 0, objects = 0, claimed = 0;
      for (const Page& pg : d.pages) for (const Layer& l : pg.layers) {
        std::set<QString> ids;
        for (const Element& e : l.elements) if (const auto id = planeIdOf(e)) { ids.insert(*id); if (lacksPlace(e) && std::holds_alternative<Text>(e)) broken++; }
        for (const QString& id : ids) { ElementPtrs m; for (const Element& e : l.elements) if (planeIdOf(e) == std::optional<QString>(id)) m.push_back(&e); objects++; if (GeneratedRegistry::instance().find(m)) claimed++; }
      }
      const int fixed = repairGenerated(d);
      int left = 0;
      for (const Page& pg : d.pages) for (const Layer& l : pg.layers) for (const Element& e : l.elements) if (planeIdOf(e) && lacksPlace(e) && std::holds_alternative<Text>(e)) left++;
      out(QString("  FILE %1 pages, %2 generated objects, %3 claimed by a handler, %4 texts without position, repaired %5 objects, %6 texts left without position").arg(d.pages.size()).arg(objects).arg(claimed).arg(broken).arg(fixed).arg(left));
    }
  }
  ed.setStyle(savedStyle);
  out(QString("chain: %1 checks passed, %2 failed").arg(r.passed).arg(r.fail));
  return r.fail;
}

}  // namespace mathobj
