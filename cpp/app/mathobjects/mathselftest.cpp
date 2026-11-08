#include "mathselftest.h"
#include "documentview.h"
#include "editor.h"
#include "fill.h"
#include "generated.h"
#include "mainwindow.h"
#include "mathstate.h"
#include "mathtools.h"
#include "pagerender.h"
#include "setexpr.h"
#include "xopp.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QRadioButton>
#include <QThread>
#include <QWidgetAction>
#include <QDoubleSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <cmath>
#include <set>

namespace mathobj {
namespace {

using namespace sets;

/** Stand-in for the selection, used only when the application has none yet (the real one is a drop-in for it). */
class MiniSelection final : public Selection {
 public:
  explicit MiniSelection(Editor& ed) : ed_(ed) {}
  bool isEmpty() const override { return items_.empty(); }
  void clear() override { items_.clear(); page_ = -1; }
  void select(int page, const std::vector<ElementRef>& items) override { items_ = items; page_ = page; }
  bool contains(const ElementRef& e) const override { return std::find(items_.begin(), items_.end(), e) != items_.end(); }
  std::vector<ElementRef> items() const override { return items_; }
  int page() const override { return page_; }
  QRectF bbox() const override { return {}; }
  bool wouldHit(int, const QPointF&, double) const override { return false; }
  void refresh() override {
    std::vector<ElementRef> keep;
    for (const ElementRef& r : items_)
      if (r.page < ed_.pageCount() && size_t(r.layer) < ed_.doc().pages[size_t(r.page)].layers.size() && size_t(r.index) < ed_.doc().pages[size_t(r.page)].layers[size_t(r.layer)].elements.size()) keep.push_back(r);
    items_ = keep;
  }
  void paintOverlay(QPainter&, const OverlayInfo&) override {}
 private:
  Editor& ed_;
  std::vector<ElementRef> items_;
  int page_ = -1;
};

struct Ctx {
  MainWindow& win;
  Editor& ed;
  DocumentView& v;
  int page;
  std::function<void(const QString&)> out;
  int baseDepth = 0;   // undo depth right after the scratch page was made: wipe() goes back to it
  int fail = 0;

  void check(bool ok, const QString& what) {
    out(QString(ok ? "  PASS  " : "  FAIL  ") + what);
    if (!ok) fail++;
  }
  int layer() const { return ed.activeLayer(page); }
  const std::vector<Element>& els() const { return ed.doc().pages[size_t(page)].layers[size_t(layer())].elements; }
};

void pump(int n = 4) {
  for (int i = 0; i < n; i++) { QApplication::processEvents(); QThread::msleep(2); }
}

bool near(double a, double b, double eps = 1e-3) { return std::abs(a - b) <= eps; }

// ---- reading what is on the page -------------------------------------------------------------------------------------------------------
std::vector<const Stroke*> strokesOf(const Ctx& c, const QString& role) {
  std::vector<const Stroke*> r;
  for (const Element& e : c.els())
    if (planeIdOf(e) && roleOf(e) == std::optional<QString>(role))
      if (const auto* s = std::get_if<Stroke>(&e)) r.push_back(s);
  return r;
}
std::vector<const Text*> textsOf(const Ctx& c, const QString& role) {
  std::vector<const Text*> r;
  for (const Element& e : c.els())
    if (planeIdOf(e) && roleOf(e) == std::optional<QString>(role))
      if (const auto* t = std::get_if<Text>(&e)) r.push_back(t);
  return r;
}
size_t mineCount(const Ctx& c) {
  size_t n = 0;
  for (const Element& e : c.els()) if (planeIdOf(e)) n++;
  return n;
}
QStringList labelsOf(const Ctx& c) {
  QStringList l;
  for (const Text* t : textsOf(c, "setlabel")) l << t->text;
  l.sort();
  return l;
}
std::optional<Selected> cur(const Ctx& c) { return selectedMath(c.ed); }
LineSpec lineSpec(const Ctx& c) { return std::get<LineSpec>(cur(c)->spec); }
VennSpec vennSpec(const Ctx& c) { return std::get<VennSpec>(cur(c)->spec); }
bool isKind(const Ctx& c, const QString& k) { const auto s = cur(c); return s && kindOf(s->spec) == k; }

// ---- gestures ---------------------------------------------------------------------------------------------------------------------------
void drag(Ctx& c, const std::vector<QPointF>& pts, Qt::KeyboardModifiers mods = {}) {
  PointerEvent e = c.v.makeEvent(c.page, pts.front(), 0.5, PointerEvent::Kind::Mouse);
  e.mods = mods;
  c.ed.pointerDown(e);
  for (size_t i = 1; i < pts.size(); i++) {
    PointerEvent m = c.v.makeEvent(c.page, pts[i], 0.5, PointerEvent::Kind::Mouse);
    m.mods = mods;
    c.ed.pointerMove(m);
  }
  PointerEvent u = c.v.makeEvent(c.page, pts.back(), 0.5, PointerEvent::Kind::Mouse);
  u.mods = mods;
  c.ed.pointerUp(u);
  pump(3);
}

void sendKey(QWidget* w, int key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier), release(QEvent::KeyRelease, key, Qt::NoModifier);
  QApplication::sendEvent(w, &press);
  QApplication::sendEvent(w, &release);
}

/** Type into a field and press Enter, as the user does. */
void enter(QLineEdit* e, const QString& text) {
  e->setFocus();
  e->setText(text);
  sendKey(e, Qt::Key_Return);
  pump(3);
}

// ---- the panels --------------------------------------------------------------------------------------------------------------------------
QToolButton* buttonOf(Ctx& c, const QString& tool) {
  QAction* a = c.ed.action("tool." + tool);
  for (QToolButton* b : c.win.toolBar()->findChildren<QToolButton*>()) if (b->defaultAction() == a && b->menu()) return b;
  return nullptr;
}
QWidget* panel(Ctx& c, const QString& tool, const QString& name) {
  QToolButton* b = buttonOf(c, tool);
  if (!b) return nullptr;
  if (!b->menu()->isVisible()) b->menu()->popup(b->mapToGlobal(QPoint(0, b->height())));
  pump(4);
  auto* wa = b->menu()->findChild<QWidgetAction*>();
  QWidget* w = wa ? wa->defaultWidget() : nullptr;
  return w && w->objectName() == name ? w : nullptr;
}
void closePanels(Ctx& c) {
  for (const char* t : {"numline", "venn", "fill"}) if (QToolButton* b = buttonOf(c, t)) b->menu()->hide();
  pump(2);
}
template <class T> T* child(QWidget* p, const QString& name) { return p ? p->findChild<T*>(name) : nullptr; }
QAbstractButton* symbol(QWidget* p, const QString& text) {
  for (QAbstractButton* b : p->findChildren<QAbstractButton*>()) if (b->text() == text && b->toolTip().startsWith("Insertar")) return b;
  return nullptr;
}

// ---- a rendering of the layer, to look at pixels ------------------------------------------------------------------------------------------
struct Shot {
  QImage img;
  QRgb at(double x, double y) const { return img.pixel(std::clamp(int(x), 0, img.width() - 1), std::clamp(int(y), 0, img.height() - 1)); }
  bool is(double x, double y, int r, int g, int b, int tol = 40) const {
    const QColor q = QColor::fromRgb(at(x, y));
    return std::abs(q.red() - r) < tol && std::abs(q.green() - g) < tol && std::abs(q.blue() - b) < tol;
  }
  bool white(double x, double y) const { return is(x, y, 255, 255, 255, 8); }
};
Shot render(const Ctx& c) {
  const Page& pg = c.ed.doc().pages[size_t(c.page)];
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

void addInk(Ctx& c, const std::vector<QPointF>& pts, const QString& color = "#000000ff", double width = 1.41) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", color}, {"capStyle", "round"}};
  s.widths = {width};
  for (const QPointF& p : pts) { s.pts.push_back(p.x()); s.pts.push_back(p.y()); }
  c.ed.commitAdd(c.page, c.layer(), Element(std::move(s)), "Trazo");
}

std::vector<QPointF> line(double y, double x0 = 100, double x1 = 500, double step = 25) {
  std::vector<QPointF> o;
  for (double x = x0; x <= x1; x += step) o.push_back({x, y});
  return o;
}

/** Back to the empty scratch page by undoing everything the checks did (no stale history is left behind). */
void wipe(Ctx& c) {
  c.ed.selection().clear();
  while (c.ed.undoDepth() > c.baseDepth) c.ed.undo();
  pump(2);
}

QString shotName(const QString& dir, const QString& n) { return dir + "/" + n; }

// ===================================================================================================================================================
void lineChecks(Ctx& c) {
  Editor& ed = c.ed;
  c.out("\n-- real line");
  ed.setTool("numline");
  QElapsedTimer tm;
  tm.start();
  drag(c, {{100, 150}, {300, 150}, {500, 150}});
  const double createMs = double(tm.nsecsElapsed()) / 1e6;
  c.out(QString("   drag + create + select: %1 ms").arg(createMs, 0, 'f', 1));
  c.check(ed.tool() == "select" && ed.returnTo() == "pen" && isKind(c, "line") && strokesOf(c, "setwitness").size() == 1 && !strokesOf(c, "setbody").empty() &&
              strokesOf(c, "setmark").size() >= 11 && textsOf(c, "setlabel").size() >= 11,
          "lineCreated: Select with the pen to return to, one witness, body, marks and numbers");
  {
    const LineSpec s = lineSpec(c);
    c.check(near(s.pts[0], 100) && near(s.pts[1], 150) && near(s.pts[2], 500) && near(s.pts[3], 150), "lineEnds: the ends of the axis are where the drag went");
  }
  c.check(ed.selection().items().size() == mineCount(c) && mineCount(c) > 0, "lineIsOneObject: every piece is selected, nothing else");
  c.check(buttonOf(c, "numline") && buttonOf(c, "numline")->menu()->isVisible(), "lineAutoOpensPanel: the panel of the line opened by itself after drawing it");
  QWidget* lp = panel(c, "numline", "linePanel");
  c.check(strokesOf(c, "setbody")[0]->color() == "#0044ccff" && !isGenerated(*strokesOf(c, "setbody")[0]) && isGenerated(*strokesOf(c, "setwitness")[0]), "lineStyleFromPen: pen colour; body is ink for a fill, the witness is not");
  {
    QStringList want{"0", "1", "2", "3", "4", "5", "−1", "−2", "−3", "−4", "−5"};
    want.sort();
    c.check(labelsOf(c) == want, "lineNumbersTexts: -5..5");
  }
  if (!lp) return;

  auto exprs = [&] { return std::vector<QLineEdit*>{child<QLineEdit>(lp, "expr0"), child<QLineEdit>(lp, "expr1"), child<QLineEdit>(lp, "expr2")}; };
  auto expr = [&](int i) { return exprs()[size_t(i)]; };
  auto out = [&](int i) { return child<QLabel>(lp, QString("out%1").arg(i)); };
  auto pos = [&](const LineSpec& s, double v) { const double k = (v - s.min) / (s.max - s.min); return QPointF(s.pts[0] + (s.pts[2] - s.pts[0]) * k, s.pts[1] + (s.pts[3] - s.pts[1]) * k); };
  auto dots = [&] { std::vector<const Stroke*> r; for (auto* s : strokesOf(c, "setbody")) if (s->pts.size() == 4 && near(s->pts[0], s->pts[2], 0.02) && near(s->pts[1], s->pts[3], 0.02)) r.push_back(s); return r; };
  auto rings = [&] { std::vector<const Stroke*> r; for (auto* s : strokesOf(c, "setbody")) if (s->pts.size() == 50) r.push_back(s); return r; };
  auto hi = [&] { std::vector<const Stroke*> r; for (auto* s : strokesOf(c, "setbody")) if (s->widths[0] > 1.41 * 2 && s->pts.size() == 4 && !near(s->pts[0], s->pts[2], 0.02)) r.push_back(s); return r; };

  enter(expr(0), "(-2, 3] ∪ {4} ∪ [4.5, inf)");
  {
    const LineSpec s = lineSpec(c);
    c.check(s.rows[0].expr == "(-2, 3] ∪ {4} ∪ [4.5, inf)" && out(0)->text() == "= (−2, 3] ∪ {4} ∪ [4.5, ∞)", "lineExprKept: the text typed is the definition; the panel shows the normalised set");
    const auto r = rings(), d = dots();
    c.check(r.size() == 1 && near(r[0]->pts[0] + (r[0]->pts[24] - r[0]->pts[0]) / 2, pos(s, -2).x(), 0.01), "lineOpenCircleAt: one open circle on -2");
    bool all = d.size() == 3;
    for (double v : {3.0, 4.0, 4.5}) {
      bool f = false;
      for (auto* q : d) f = f || (near(q->pts[0], pos(s, v).x(), 0.02) && near(q->pts[1], pos(s, v).y(), 0.02));
      all = all && f;
    }
    c.check(all, "lineClosedDotsAt: closed dots on 3, 4 and 4.5");
    c.check(hi().size() == 2, "lineHighlights: two thick pieces over the axis");
    const double x = pos(s, -2).x();
    bool gap = true;
    for (auto* q : strokesOf(c, "setbody"))
      if (q->widths[0] == 1.41 && q->pts.size() == 4 && std::min(q->pts[0], q->pts[2]) < x - 1 && std::max(q->pts[0], q->pts[2]) > x + 1) gap = false;
    c.check(gap, "lineLineHasAGapInsideACircle");
    const Stroke* rightEnd = nullptr;
    for (auto* q : hi()) if (std::max(q->pts[0], q->pts[2]) > pos(s, 5).x() + 10 && std::min(q->pts[0], q->pts[2]) > pos(s, 4).x()) rightEnd = q;
    c.check(rightEnd && std::max(rightEnd->pts[0], rightEnd->pts[2]) >= s.pts[2] + 10, "lineInfinityRunsToTheEnd: [4.5, inf) runs to the arrow");
  }
  enter(expr(0), "R \\ {2}");
  c.check(rings().size() == 1 && dots().empty() && hi().size() == 2, "lineHoleIsOneCircle");
  enter(expr(0), "(1,3)");
  c.check(rings().size() == 2 && dots().empty() && hi().size() == 1, "lineOpenInterval");
  enter(expr(0), "[1,3]");
  c.check(dots().size() == 2 && rings().empty() && hi().size() == 1, "lineClosedInterval");
  enter(expr(0), "[1,3)");
  c.check(dots().size() == 1 && rings().size() == 1, "lineHalfOpen");
  // a mistake is told where it is, and the drawing keeps what it had
  enter(expr(0), "(1,");
  c.check(out(0)->property("err").toBool() && out(0)->text().contains("paréntesis") && lineSpec(c).rows[0].expr == "(1,", "lineErrorShown: red message, the expression kept");
  {
    QLabel* echo = nullptr;
    for (QLabel* l : lp->findChildren<QLabel*>()) if (l->textFormat() == Qt::RichText && l->isVisibleTo(lp) && l->text().contains("background")) echo = l;
    c.check(echo != nullptr, "lineErrorMarked: the place of the mistake is marked in the text");
  }
  enter(expr(0), "[1,3]");
  c.check(!out(0)->property("err").toBool() && dots().size() == 2, "lineErrorGoes");
  // brackets and parentheses
  auto* marks = child<QComboBox>(lp, "marks");
  marks->setCurrentIndex(1);
  pump(3);
  {
    int n8 = 0;
    for (auto* q : strokesOf(c, "setbody")) if (q->pts.size() == 8) n8++;
    c.check(rings().empty() && dots().empty() && n8 == 2, "lineBrackets");
  }
  enter(expr(0), "(1,3)");
  {
    int n22 = 0;
    for (auto* q : strokesOf(c, "setbody")) if (q->pts.size() == 22) n22++;
    c.check(n22 == 2, "lineParentheses");
  }
  child<QComboBox>(lp, "marks")->setCurrentIndex(0);
  pump(3);
  // more rows, to compare
  child<QPushButton>(lp, "addRow")->click();
  pump(4);
  lp = panel(c, "numline", "linePanel");
  enter(expr(1), "[0,5]");
  enter(expr(0), "(-2,3]");
  {
    const LineSpec s = lineSpec(c);
    std::set<double> ys;
    double mn = 1e9;
    for (auto* q : strokesOf(c, "setbody")) if (q->pts.size() == 4 && q->widths[0] <= 1.5) { const double y = std::round(q->pts[1] * 10) / 10; ys.insert(y); mn = std::min(mn, y); }
    c.check(s.rows.size() == 2 && ys.size() == 2 && std::abs(mn - 110) < 0.2, "lineSecondRow: the second axis is one row up");
    c.check(out(0)->text() == "= (−2, 3]" && out(1)->text() == "= [0, 5]", "lineRowsHaveTheirOwnSet");
  }
  enter(expr(1), "");
  child<QToolButton>(lp, "del1") ? child<QToolButton>(lp, "del1")->click() : void();
  pump(4);
  lp = panel(c, "numline", "linePanel");
  c.check(lineSpec(c).rows.size() == 1 && child<QToolButton>(lp, "del1") == nullptr, "lineRowRemoved: the × button takes the row out");

  // axis settings
  auto num = [&](const char* n) { return child<QDoubleSpinBox>(lp, n); };
  num("min")->setValue(0); pump(3);
  num("max")->setValue(10); pump(3);
  num("step")->setValue(2); pump(3);
  {
    const LineSpec s = lineSpec(c);
    QStringList want{"0", "2", "4", "6", "8", "10"}, got;
    for (const Text* t : textsOf(c, "setlabel")) got << t->text;
    want.sort(); got.sort();
    c.check(s.min == 0 && s.max == 10 && s.step == 2 && got == want, "lineRange: 0..10 step 2 writes 0,2,...,10");
  }
  num("step")->setValue(0.001); pump(3);
  c.check(strokesOf(c, "setmark").size() <= 250, "lineTicksCapped: a tiny step does not draw thousands of marks");
  num("step")->setValue(1); pump(3);
  num("min")->setValue(-5); pump(3);
  num("max")->setValue(5); pump(3);
  // the symbol buttons write into the field that has the cursor
  {
    QLineEdit* ex = expr(0);
    ex->setFocus();
    ex->setText("");
    for (const char* s : {"(", "∞", ",", "∞", ")"}) symbol(lp, s)->click();
    c.check(ex->text() == "(∞,∞)", "lineSymbolButtons: ( ∞ , ∞ ) written at the cursor");
    ex->setText("");
    for (const char* s : {"[", "−∞", ",", "π", "]"}) symbol(lp, s)->click();
    c.check(ex->text() == "[−∞,π]", "lineSymbolButtonsMore");
    ex->setText("");
    symbol(lp, "′")->click();
    c.check(ex->text() == "'", "lineSymbolPrime: ′ is typed as the apostrophe the language reads");
    enter(ex, "(-2, 3]");
  }
  c.check(child<QPushButton>(lp, "restyle") != nullptr && child<QCheckBox>(lp, "ticks") && child<QCheckBox>(lp, "numbers") && child<QCheckBox>(lp, "arrows"), "line panel has the three checks and the pen-style button");
  {
    child<QCheckBox>(lp, "numbers")->setChecked(false);
    pump(3);
    const bool noNums = textsOf(c, "setlabel").empty() && !lineSpec(c).numbers;
    child<QCheckBox>(lp, "numbers")->setChecked(true);
    child<QCheckBox>(lp, "arrows")->setChecked(false);
    pump(3);
    const bool noArrows = strokesOf(c, "setarrow").empty();
    child<QCheckBox>(lp, "arrows")->setChecked(true);
    pump(3);
    c.check(noNums && noArrows && !strokesOf(c, "setarrow").empty(), "lineChecks: numbers and arrows switch off and on");
  }
}

ElementPtrs piecesOfSelected(const Ctx& c) {
  ElementPtrs m;
  const auto s = cur(c);
  if (!s) return m;
  for (const Element& e : c.els()) if (planeIdOf(e) == std::optional<QString>(s->id)) m.push_back(&e);
  return m;
}

/** What the selection does when an anchor is dragged: ask the registry, swap the rebuilt pieces in as one step. */
bool dragAnchor(Ctx& c, int i, QPointF to, bool shift = false) {
  const auto s = cur(c);
  const ElementPtrs mem = piecesOfSelected(c);
  const GeneratedHandler* h = GeneratedRegistry::instance().find(mem);
  if (!s || !h) return false;
  auto fresh = h->dragAnchor(mem, s->id, i, to, shift);
  if (!fresh) return false;
  const Editor::Swap sw = c.ed.swapGenerated(s->page, s->layer, s->id, *fresh);
  Action a;
  a.label = "Mover asa";
  a.page = s->page;
  a.undo = sw.undo;
  a.redo = sw.redo;
  c.ed.pushAction(std::move(a));
  c.ed.selection().select(s->page, c.ed.membersOf(s->page, s->layer, s->id));
  return true;
}

/** Rotate every piece a quarter turn about a point (on copies, as the pieces the selection would have moved), then let the handler rebuild them. */
void rotateAndRebuild(Ctx& c, QPointF centre) {
  const auto s = cur(c);
  auto rot = [&](double x, double y) { return QPointF(centre.x() - (y - centre.y()), centre.y() + (x - centre.x())); };
  std::vector<Element> turned;
  for (const Element* e : piecesOfSelected(c)) {
    Element copy = *e;
    if (auto* st = std::get_if<Stroke>(&copy)) for (size_t i = 0; i + 1 < st->pts.size(); i += 2) { const QPointF q = rot(st->pts[i], st->pts[i + 1]); st->pts[i] = q.x(); st->pts[i + 1] = q.y(); }
    if (auto* t = std::get_if<Text>(&copy)) { const QPointF q = rot(attrOr(t->attrs, "x").toDouble(), attrOr(t->attrs, "y").toDouble()); attrSet(t->attrs, "x", fmt8(q.x())); attrSet(t->attrs, "y", fmt8(q.y())); }
    turned.push_back(std::move(copy));
  }
  ElementPtrs mem;
  for (const Element& e : turned) mem.push_back(&e);
  const GeneratedHandler* h = GeneratedRegistry::instance().find(mem);
  if (!h) return;
  auto fresh = h->rebuildAfterTransform(mem, s->id);
  if (!fresh) return;
  const Editor::Swap sw = c.ed.swapGenerated(s->page, s->layer, s->id, *fresh);
  Action a;
  a.label = "Girar";
  a.page = s->page;
  a.undo = sw.undo;
  a.redo = sw.redo;
  c.ed.pushAction(std::move(a));
  c.ed.selection().select(s->page, c.ed.membersOf(s->page, s->layer, s->id));
}

std::optional<MathRead> savedAndRead(Ctx& c) {
  QTemporaryDir dir;
  const QString f = dir.filePath("math.xopp");
  if (!saveXopp(f, c.ed.doc())) return std::nullopt;
  const LoadResult lr = loadXopp(f);
  if (!lr.ok) return std::nullopt;
  ElementPtrs m;
  for (const Element& e : lr.doc.pages[size_t(c.page)].layers[size_t(c.layer())].elements) if (planeIdOf(e)) m.push_back(&e);
  return readMath(m);
}

void lineAnchorChecks(Ctx& c) {
  Editor& ed = c.ed;
  c.out("\n-- real line: anchors, turning, file, undo");
  const GeneratedHandler* h = GeneratedRegistry::instance().ofKind("line");
  c.check(h != nullptr && GeneratedRegistry::instance().ofKind("venn") != nullptr, "both handlers are in the GeneratedRegistry");
  {
    const ElementPtrs mem = piecesOfSelected(c);
    c.check(GeneratedRegistry::instance().find(mem) == h && h->anchors(mem).size() == 2, "the registry finds the line handler for its pieces: two anchors");
    c.check(mathAnchorAt(ed, h->anchors(mem)[1] + QPointF(3, 2), 1.0) == std::optional<int>(1) && !mathAnchorAt(ed, QPointF(300, 400), 1.0), "mathAnchorAt: the end under the pointer, nothing elsewhere");
  }
  const LineSpec s0 = lineSpec(c);
  c.check(dragAnchor(c, 1, {620, 150}), "dragAnchor of the right end moves it");
  {
    const LineSpec s1 = lineSpec(c);
    c.check(near(s1.pts[2], 620, 0.01) && near(s1.pts[0], s0.pts[0], 1e-6) && s1.rows[0].expr == "(-2, 3]", "lineAnchorDrag: only that end moved, the expressions stay");
  }
  ed.undo();
  c.check(near(lineSpec(c).pts[2], 500, 0.01), "lineAnchorUndo");
  c.check(!dragAnchor(c, 1, {500, 150}), "dragging an anchor to where it is changes nothing");
  // moved, turned: what is read back follows the strokes
  rotateAndRebuild(c, {300, 150});
  {
    const LineSpec s = lineSpec(c);
    c.check(std::abs(s.pts[2] - s.pts[0]) < 1e-3 && std::abs(s.pts[3] - s.pts[1]) > 350 && s.rows[0].expr == "(-2, 3]", "lineRotated: a quarter turn is read back and the rows kept");
    const Stroke* ax = nullptr;
    for (auto* q : strokesOf(c, "setbody")) if (q->pts.size() == 4 && q->widths[0] <= 1.5 && std::hypot(q->pts[2] - q->pts[0], q->pts[3] - q->pts[1]) > 200) ax = q;
    c.check(ax && std::abs(ax->pts[2] - ax->pts[0]) < 1e-3, "lineRotatedStrokesAgree: the rebuilt pieces lie along the turned axis");
  }
  ed.undo();
  c.check(near(lineSpec(c).pts[2], 500, 0.01), "lineTurnUndo");
  // saved and opened again
  {
    const auto re = savedAndRead(c);
    const LineSpec orig = lineSpec(c);
    const auto* ls = re ? std::get_if<LineSpec>(&re->spec) : nullptr;
    c.check(ls && ls->rows[0].expr == "(-2, 3]" && near(ls->pts[2], orig.pts[2], 1e-2) && re->style.color == "#0044cc", "lineRoundTrip: saved and read back from the .xopp (expressions, ends, pen colour)");
  }
  // undo / redo of the whole object
  {
    const size_t before = mineCount(c);
    const int depth = ed.undoDepth();
    while (ed.undoDepth() > depth - 3 && ed.canUndo()) ed.undo();   // the row edits
    while (ed.canRedo()) ed.redo();
    c.check(mineCount(c) == before && selectedMath(ed).has_value(), "lineRedoWhole");
  }
}

// ===================================================================================================================================================
bool checkShading(Ctx& c, const QStringList& names) {
  const VennSpec s = vennSpec(c);
  const Parsed p = parseSetExpr(s.expr);
  if (!p.ok) return false;
  const auto reg = evalRegions(p.expr, names);
  if (!reg.ok) return false;
  const auto segs = strokesOf(c, "sethatch");
  if (segs.empty()) return std::none_of(reg.value.begin(), reg.value.end(), [](bool b) { return b; });
  for (const Stroke* q : segs) {
    const double ax = q->pts[0], ay = q->pts[1], bx = q->pts[2], by = q->pts[3];
    for (double t : {0.5, 0.05, 0.95}) if (!reg.value[size_t(maskAt(s, {ax + (bx - ax) * t, ay + (by - ay) * t}))]) return false;
  }
  const double x0 = std::min(s.pts[0], s.pts[2]), y0 = std::min(s.pts[1], s.pts[3]), x1 = std::max(s.pts[0], s.pts[2]), y1 = std::max(s.pts[1], s.pts[3]);
  struct Circ { double x, y, r; };
  std::vector<Circ> cs;
  for (int i = 0; i < s.k; i++) cs.push_back({s.pts[size_t(4 + 4 * i)], s.pts[size_t(5 + 4 * i)], std::hypot(s.pts[size_t(6 + 4 * i)] - s.pts[size_t(4 + 4 * i)], s.pts[size_t(7 + 4 * i)] - s.pts[size_t(5 + 4 * i)])});
  auto border = [&](double px, double py) {
    double b = std::min({px - x0, x1 - px, py - y0, y1 - py});
    for (const Circ& q : cs) b = std::min(b, std::abs(std::hypot(px - q.x, py - q.y) - q.r));
    return b;
  };
  auto distToSegs = [&](double px, double py) {
    double d = 1e18;
    for (const Stroke* q : segs) {
      const double ax = q->pts[0], ay = q->pts[1], dx = q->pts[2] - ax, dy = q->pts[3] - ay, L = dx * dx + dy * dy ? dx * dx + dy * dy : 1;
      const double t = std::clamp(((px - ax) * dx + (py - ay) * dy) / L, 0.0, 1.0);
      d = std::min(d, std::hypot(px - ax - t * dx, py - ay - t * dy));
    }
    return d;
  };
  int tested = 0, bad = 0, unshadedHit = 0;
  for (double px = x0 + 3; px < x1; px += 7)
    for (double py = y0 + 3; py < y1; py += 7) {
      const bool shaded = reg.value[size_t(maskAt(s, {px, py}))];
      if (border(px, py) < 4.5) continue;
      if (shaded) { tested++; if (distToSegs(px, py) > 5) bad++; }
      else if (distToSegs(px, py) < 1.5) unshadedHit++;
    }
  return tested > 20 && bad <= tested * 0.01 && unshadedHit == 0;
}

void vennChecks(Ctx& c) {
  Editor& ed = c.ed;
  c.out("\n-- set diagram");
  wipe(c);
  closePanels(c);
  ed.setTool("venn");
  QElapsedTimer tm;
  tm.start();
  drag(c, {{60, 300}, {300, 450}, {520, 560}});
  c.out(QString("   drag + create + select: %1 ms").arg(double(tm.nsecsElapsed()) / 1e6, 0, 'f', 1));
  const bool opened = buttonOf(c, "venn") && buttonOf(c, "venn")->menu()->isVisible();
  QWidget* vp = panel(c, "venn", "vennPanel");
  c.check(ed.tool() == "select" && ed.returnTo() == "pen" && isKind(c, "venn") && vennSpec(c).k == 2 && strokesOf(c, "setwitness").size() == 1 && strokesOf(c, "setbody").size() == 3 && vp && opened,
          "vennCreated: Select, pen to return to, witness, universe and two circles, panel open");
  c.check(labelsOf(c) == QStringList({"A", "B", "U"}), "vennLabels: A, B and U");
  {
    const VennSpec s = vennSpec(c);
    const double r0 = std::hypot(s.pts[6] - s.pts[4], s.pts[7] - s.pts[5]), r1 = std::hypot(s.pts[10] - s.pts[8], s.pts[11] - s.pts[9]), d = std::hypot(s.pts[4] - s.pts[8], s.pts[5] - s.pts[9]);
    c.check(near(std::min(s.pts[0], s.pts[2]), 60, 0.01) && near(std::max(s.pts[1], s.pts[3]), 560, 0.01) && d < r0 + r1 && d > std::abs(r0 - r1), "vennGeometry: the universe is the dragged box, the two circles overlap");
  }
  if (!vp) return;
  auto vexpr = [&] { return child<QLineEdit>(vp, "vexpr"); };
  for (const auto& [e, name] : std::vector<std::pair<QString, QString>>{{"A ∩ B", "Inter"}, {"A ∪ B", "Union"}, {"A ∖ B", "Diff"}, {"B ∖ A", "DiffRev"}, {"A'", "Complement"}, {"(A ∪ B)'", "Outside"}, {"A Δ B", "SymDiff"}, {"A' ∩ B'", "DeMorganB"}}) {
    enter(vexpr(), e);
    c.check(checkShading(c, {"A", "B"}), "venn" + name + "Shading: " + e);
  }
  {
    bool allGen = !strokesOf(c, "sethatch").empty();
    for (auto* q : strokesOf(c, "sethatch")) allGen = allGen && isGenerated(*q);
    c.check(allGen && !isGenerated(*strokesOf(c, "setbody")[0]), "vennHatchStrokesSpecial: hatching is paper for a fill, the circles are walls");
  }
  enter(vexpr(), "A ∩ D");
  c.check(child<QLabel>(vp, "vout")->property("err").toBool() && child<QLabel>(vp, "vout")->text().contains("«D»") && strokesOf(c, "sethatch").empty(), "vennErrorShown: the unknown name D is named, nothing shaded");
  enter(vexpr(), "A ∩ B'");
  c.check(!child<QLabel>(vp, "vout")->property("err").toBool() && !strokesOf(c, "sethatch").empty(), "vennErrorGoes");
  {
    const auto presets = vp->findChildren<QPushButton*>("preset");
    presets[0]->click();
    pump(3);
    c.check(vennSpec(c).expr == "A ∪ B" && presets.size() >= 6, "vennPresets: the ready expressions apply");
  }
  // elements
  enter(child<QLineEdit>(vp, "list0"), "1, 2");
  enter(child<QLineEdit>(vp, "list1"), "2; 3, 4");
  enter(child<QLineEdit>(vp, "list2"), "9");
  {
    const VennSpec s = vennSpec(c);
    const auto els = elementsOf(s);
    auto maskOf = [&](const QString& l) { for (auto& e : els) if (e.label == l) return e.mask; return -1; };
    c.check(maskOf("1") == 1 && maskOf("2") == 3 && maskOf("3") == 2 && maskOf("9") == 0 && els.size() == 5, "vennElementsMasks: 2 is in both sets, 9 outside");
    const auto placed = placeElements(s);
    bool in = placed.size() == 5;
    for (auto& p : placed) in = in && maskAt(s, p.at) == maskOf(p.label);
    c.check(in, "vennElementsInRegions");
    QStringList texts;
    for (const Text* t : textsOf(c, "setlabel")) texts << t->text;
    c.check(texts.contains("1") && texts.contains("4") && texts.contains("9") && strokesOf(c, "setmark").size() == 5, "vennElementsDrawn: five elements, five marks");
  }
  // the anchors: a circle moves with its radius point, the radius point sets the radius, a corner moves the universe
  {
    const ElementPtrs mem = piecesOfSelected(c);
    const GeneratedHandler* h = GeneratedRegistry::instance().find(mem);
    c.check(h && h->kind() == "venn", "the registry finds the venn handler");
    const VennSpec s0 = vennSpec(c);
    const auto anchors = h->anchors(mem);
    auto nearest = [&](QPointF p) { int b = 0; double bd = 1e18; for (size_t i = 0; i < anchors.size(); i++) { const double d = std::hypot(anchors[i].x() - p.x(), anchors[i].y() - p.y()); if (d < bd) { bd = d; b = int(i); } } return b; };
    const QPointF centre(s0.pts[4], s0.pts[5]);
    c.check(dragAnchor(c, nearest(centre), centre + QPointF(-60, 40)), "dragging the centre of circle A");
    {
      const VennSpec s1 = vennSpec(c);
      c.check(near(s1.pts[4], centre.x() - 60, 0.05) && near(s1.pts[5], centre.y() + 40, 0.05) && near(s1.pts[6] - s1.pts[4], s0.pts[6] - s0.pts[4], 1e-6) && near(s1.pts[8], s0.pts[8], 1e-6), "vennMoveCircle: the circle carries its radius point, the other one stays");
      bool in = true;
      const auto els = elementsOf(s1);
      for (auto& p : placeElements(s1)) for (auto& e : els) if (e.label == p.label) in = in && maskAt(s1, p.at) == e.mask;
      c.check(in, "vennElementsFollow: the elements are placed again inside their regions");
    }
    ed.undo();
    c.check(near(vennSpec(c).pts[4], s0.pts[4], 1e-6), "vennMoveUndo");
    const QPointF rp(s0.pts[6], s0.pts[7]);
    const double rl = std::hypot(rp.x() - centre.x(), rp.y() - centre.y());
    const QPointF dir((rp.x() - centre.x()) / rl, (rp.y() - centre.y()) / rl);
    c.check(dragAnchor(c, nearest(rp), rp + dir * 20), "dragging the radius point");
    {
      const VennSpec s2 = vennSpec(c);
      c.check(std::hypot(s2.pts[6] - s2.pts[4], s2.pts[7] - s2.pts[5]) > rl + 5 && near(s2.pts[4], s0.pts[4], 1e-6), "vennRadius: the radius grows, the centre stays");
    }
    ed.undo();
    const QPointF corner(s0.pts[2], s0.pts[3]);
    c.check(dragAnchor(c, nearest(corner), corner + QPointF(40, 20)), "dragging a corner of the universe");
    c.check(near(vennSpec(c).pts[2], corner.x() + 40, 0.05) && near(vennSpec(c).pts[0], s0.pts[0], 1e-6), "vennUniverseCorner");
    ed.undo();
  }
  // names, 3 sets, the universe off
  {
    enter(child<QLineEdit>(vp, "name0"), "P");
    c.check(vennSpec(c).names[0] == "P" && labelsOf(c).contains("P") && !labelsOf(c).contains("A"), "vennNames: A is now P in the drawing");
    enter(child<QLineEdit>(vp, "vexpr"), "P ∩ B");
    c.check(!child<QLabel>(vp, "vout")->property("err").toBool() && !strokesOf(c, "sethatch").empty(), "vennNamedExpr");
    child<QComboBox>(vp, "k")->setCurrentIndex(1);
    pump(5);
    vp = panel(c, "venn", "vennPanel");
    const VennSpec s3 = vennSpec(c);
    c.check(s3.k == 3 && strokesOf(c, "setbody").size() == 4 && s3.pts.size() == 16 && labelsOf(c).contains("C") && vp->findChildren<QPushButton*>("preset").size() >= 5, "vennThree: a third set (C), its presets and its list field");
    enter(child<QLineEdit>(vp, "vexpr"), "(P ∩ B) ∪ C'");
    c.check(checkShading(c, {"P", "B", "C"}), "vennThreeShading");
    enter(child<QLineEdit>(vp, "vexpr"), "P ∖ (B ∪ C)");
    c.check(checkShading(c, {"P", "B", "C"}), "vennThreeShading2");
    {
      const VennSpec s = vennSpec(c);
      const double mx = (s.pts[4] + s.pts[8] + s.pts[12]) / 3, my = (s.pts[5] + s.pts[9] + s.pts[13]) / 3;
      c.check(maskAt(s, {mx, my}) == 7, "vennThreeGeometry: the three circles share a middle");
    }
    child<QCheckBox>(vp, "universe")->setChecked(false);
    pump(3);
    c.check(!labelsOf(c).contains("U") && strokesOf(c, "setbody").size() == 3 && !vennSpec(c).universe, "vennNoUniverse");
    child<QCheckBox>(vp, "universe")->setChecked(true);
    pump(3);
  }
  // turned, saved and opened again
  {
    const VennSpec b = vennSpec(c);
    rotateAndRebuild(c, {300, 400});
    c.check(isKind(c, "venn") && vennSpec(c).expr == "P ∖ (B ∪ C)" && vennSpec(c).k == 3, "vennRotated: read back after a quarter turn");
    ed.undo();
    const auto re = savedAndRead(c);
    const VennSpec orig = vennSpec(c);
    const auto* vs = re ? std::get_if<VennSpec>(&re->spec) : nullptr;
    bool same = vs && vs->expr == orig.expr && vs->lists == orig.lists && vs->names == orig.names && near(vs->pts[4], orig.pts[4], 1e-2);
    c.check(same && re->style.color == "#0044cc" && near(re->style.width, 1.41, 1e-3), "vennRoundTrip: the .xopp gives the same diagram and the pen style back");
    (void)b;
  }
  {
    const size_t before = strokesOf(c, "sethatch").size();
    child<QCheckBox>(vp, "dense")->setChecked(true);
    pump(3);
    c.check(strokesOf(c, "sethatch").size() > before * 1.4 && vennSpec(c).dense, "vennDenseHatch: the dense hatching has many more strokes");
  }
  {
    Style st = ed.style();
    st.color = "#cc2288";
    ed.setStyle(st);
    child<QPushButton>(vp, "restyle")->click();
    pump(3);
    c.check(strokesOf(c, "setbody")[0]->color() == "#cc2288ff", "vennRestyle: Estilo del lápiz takes the pen colour");
    st.color = "#0044cc";
    ed.setStyle(st);
  }
  {
    bool walls = true;
    for (auto* q : strokesOf(c, "setbody")) walls = walls && !isGenerated(*q);
    c.check(walls && !textsOf(c, "setlabel").empty(), "vennFillWalls: for a fill the circles are walls");
  }
}

// ===================================================================================================================================================
void uiChecks(Ctx& c) {
  Editor& ed = c.ed;
  c.out("\n-- tools, menu, keys, panels, defaults");
  wipe(c);
  closePanels(c);
  ed.setTool("pen");
  c.check(ed.action("tool.numline")->shortcut() == QKeySequence("N") && ed.action("tool.venn")->shortcut() == QKeySequence("J") && ed.action("tool.fill")->shortcut() == QKeySequence("U"), "mathKeys: N, J and U select the tools");
  ed.action("tool.numline")->trigger();
  const QString k1 = ed.tool();
  ed.action("tool.venn")->trigger();
  c.check(k1 == "numline" && ed.tool() == "venn", "mathKeys: the tool actions select the tools");
  {
    bool inMenu = false;
    for (QAction* a : c.win.menu("tools")->actions()) if (a->text().contains("Recta real") || a->text().contains("Diagrama de conjuntos")) inMenu = true;
    c.check(inMenu, "mathMenu: Herramientas has the line and the diagram");
  }
  c.check(buttonOf(c, "numline") && buttonOf(c, "venn") && buttonOf(c, "fill") && !buttonOf(c, "numline")->icon().isNull(), "mathButtons: each tool has a toolbar button with a caret and an icon");
  QToolButton* bn = buttonOf(c, "numline");
  panel(c, "numline", "linePanel");
  c.check(bn->menu()->isVisible(), "mathPanelFromTheArrow");
  bn->menu()->hide();
  panel(c, "venn", "vennPanel");
  c.check(buttonOf(c, "venn")->menu()->isVisible() && panel(c, "venn", "vennPanel") != nullptr, "mathPanelFromTheArrow: the diagram has its own panel");
  buttonOf(c, "venn")->menu()->hide();
  pump(2);
  c.check(!buttonOf(c, "venn")->menu()->isVisible(), "mathPanelEsc: the panel closes");
  // the panel with nothing selected edits what the next one is made with
  ed.selection().clear();
  ed.notifySelectionChanged();
  state().line = defaultLine();
  QWidget* lp = panel(c, "numline", "linePanel");
  enter(child<QLineEdit>(lp, "expr0"), "[0, 4)");
  ed.setTool("numline");
  c.check(state().line.rows[0].expr == "[0, 4)", "mathDefaults: the panel wrote the defaults of the next line");
  drag(c, {{100, 250}, {250, 250}, {400, 250}});
  {
    int dots = 0, rings = 0;
    for (auto* s : strokesOf(c, "setbody")) { if (s->pts.size() == 4 && near(s->pts[0], s->pts[2], 0.02) && near(s->pts[1], s->pts[3], 0.02)) dots++; if (s->pts.size() == 50) rings++; }
    c.check(lineSpec(c).rows[0].expr == "[0, 4)" && dots == 1 && rings == 1, "mathDefaultsCarry: the next line is made with what was typed");
  }
  lp = panel(c, "numline", "linePanel");
  child<QLineEdit>(lp, "expr0")->setFocus();
  sendKey(child<QLineEdit>(lp, "expr0"), Qt::Key_Return);
  pump(4);
  c.check(buttonOf(c, "numline")->menu()->isVisible(), "mathEnterKeepsThePanel: Enter in a field applies and does not close the panel");
  // a click does not insert; a click with the tool makes nothing
  closePanels(c);
  wipe(c);
  ed.setTool("numline");
  const size_t n0 = mineCount(c);
  QStringList notes;
  QObject ctxo;
  QObject::connect(&ed, &Editor::notice, &ctxo, [&](const QString& m) { notes << m; });
  drag(c, {{200, 300}});
  c.check(mineCount(c) == n0 && ed.tool() == "numline" && !notes.isEmpty() && notes.last().contains("un clic solo no inserta nada"), "a click with the line tool inserts nothing and says so");
  ed.setTool("venn");
  drag(c, {{200, 300}, {203, 302}});
  c.check(mineCount(c) == n0, "a tiny drag with the diagram tool inserts nothing either");
  // drag with Shift steps the angle by 15 degrees; a short drag is the default line around the point
  ed.setTool("numline");
  drag(c, {{100, 400}, {300, 440}}, Qt::ShiftModifier);
  {
    const LineSpec s = lineSpec(c);
    const double ang = std::atan2(s.pts[3] - s.pts[1], s.pts[2] - s.pts[0]) * 180 / M_PI;
    c.check(near(std::fmod(ang + 360, 15), 0, 1e-6) || near(std::fmod(ang + 360, 15), 15, 1e-6), "Shift: the line goes at a multiple of 15 degrees");
  }
  wipe(c);
  ed.setTool("numline");
  drag(c, {{300, 500}, {310, 500}, {330, 505}});
  c.check(near(lineSpec(c).pts[0], 100, 1e-6) && near(lineSpec(c).pts[2], 500, 1e-6), "a drag shorter than 40 makes the default line (400 long) around the start point");
  // the active layer hidden: nothing is made, the user is told
  wipe(c);
  ed.setTool("numline");
  ed.setLayerVisible(c.page, c.layer(), false);
  notes.clear();
  drag(c, {{100, 150}, {500, 150}});
  c.check(mineCount(c) == 0 && !notes.isEmpty() && notes.last().contains("oculta"), "a hidden active layer: nothing is drawn and the notice says why");
  ed.setLayerVisible(c.page, c.layer(), true);
  // Esc goes back to the tool before; a click on the empty page returns too
  ed.setTool("highlighter");
  ed.setTool("venn");
  drag(c, {{60, 300}, {400, 520}});
  c.check(ed.tool() == "select" && ed.returnTo() == "highlighter", "after a diagram: Select, remembering the highlighter");
  ed.escape();
  c.check(ed.tool() == "highlighter", "Esc returns to the tool before");
  ed.setTool("pen");
  closePanels(c);
}

// ===================================================================================================================================================
void fillChecks(Ctx& c) {
  Editor& ed = c.ed;
  c.out("\n-- fill and Riemann");
  wipe(c);
  closePanels(c);
  c.v.setZoom(1);
  Style st = ed.style();
  st.color = "#00aa00";
  ed.setStyle(st);
  state().fill = FillOptions{};
  state().fill.magnet = false;
  state().fill.gap = 1.5;
  QStringList notes;
  QObject ctxo;
  QObject::connect(&ed, &Editor::notice, &ctxo, [&](const QString& m) { notes << m; });
  auto imgs = [&] { int n = 0; for (const Element& e : c.els()) if (const auto* i = std::get_if<Image>(&e)) if (attrFind(i->attrs, "xlFill")) n++; return n; };
  auto outlines = [&] { int n = 0; for (const Element& e : c.els()) if (const auto* s = std::get_if<Stroke>(&e)) if (attrFind(s->attrs, "xlRiemann")) n++; return n; };

  // 1. a hump above a baseline; fill upward from the baseline, stopping at the hump
  std::vector<QPointF> hump;
  for (int i = 0; i <= 80; i++) { const double x = 100 + i / 80.0 * 400; hump.push_back({x, 500 - 150 * std::sin(M_PI * i / 80.0)}); }
  addInk(c, hump);
  ed.setTool("fill");
  const size_t before = c.els().size();
  const int depth = ed.undoDepth();
  drag(c, line(500));
  {
    const auto lf = state().lastFill;
    const Shot sh = render(c);
    c.check(imgs() == 1 && c.els().size() == before + 1 && ed.undoDepth() == depth + 1, "fillAddedOneImage: one image, one undo step");
    c.check(lf && lf->path && !lf->wholePage, "fillPathMode: a dragged line fills from it, bounded");
    c.check(sh.is(300, 450, 0, 170, 0) && sh.is(300, 380, 0, 170, 0) && sh.is(180, 495, 0, 170, 0), "fillUnderTheHump: the area under the hump is filled with the pen colour");
    c.check(sh.white(300, 300) && sh.white(300, 340), "fillStopsAtTheCurve: above the hump stays white");
    c.check(sh.white(300, 520) && sh.white(300, 560), "fillStaysOnItsSide: nothing below the baseline");
    if (lf) c.out(QString("   fill from a dragged line over %1 pixels: %2 ms").arg(lf->pixels).arg(lf->ms, 0, 'f', 1));
    c.check(std::holds_alternative<Image>(c.els()[0]), "fillUnderInk: the image goes under the strokes of the layer");
    const auto* im = std::get_if<Image>(&c.els()[0]);
    c.check(im && QImage::fromData(QByteArray::fromBase64(im->data.toLatin1())).width() > 10, "fillPng: the fill is a PNG that decodes");
  }
  ed.undo();
  c.check(imgs() == 0 && c.els().size() == before, "fillUndo");
  ed.redo();
  c.check(imgs() == 1, "fillRedo");
  ed.undo();
  state().fill.side = FillSide::Right;
  drag(c, line(500));
  c.check(render(c).is(300, 560, 0, 170, 0) && render(c).white(300, 450), "fillRightSide: the other side of the same line");
  ed.undo();
  state().fill.side = FillSide::Left;

  // 2. Riemann rectangles on a hump
  wipe(c);
  std::vector<QPointF> hump2;
  for (int i = 0; i <= 60; i++) { const double x = 130 + i / 60.0 * 240; hump2.push_back({x, 250 - 90 * std::sin(M_PI * i / 60.0)}); }
  addInk(c, hump2);
  state().fill.kind = FillKind::Riemann;
  state().fill.rule = RiemannRule::Upper;
  state().fill.base = RiemannBase::Line;
  state().fill.by = RiemannBy::Count;
  state().fill.count = 6;
  state().fill.outline = true;
  state().fill.magnet = true;
  state().lastRiemann.reset();
  const size_t b2 = c.els().size();
  const int d2 = ed.undoDepth();
  drag(c, {{130, 250}, {370, 250}});
  {
    const auto rm = state().lastRiemann;
    auto expected = [](double x0, double x1) { double m = 0; for (double x = x0; x <= x1; x += 0.5) m = std::max(m, 90 * std::sin(M_PI * (x - 130) / 240)); return m; };
    bool heights = rm && rm->drawn == 6 && rm->n == 6;
    if (heights) for (size_t i = 0; i < rm->cols.size(); i++) heights = heights && std::abs(rm->cols[i].h - expected(130 + double(i) * 40, 170 + double(i) * 40)) < 4;
    c.check(heights, "riemannUpperHeights: six rectangles as high as the highest point of the curve in their column");
    c.check(imgs() == 1 && outlines() == 6 && c.els().size() == b2 + 7 && ed.undoDepth() == d2 + 1, "riemannOneUndoStep: image + six outlines are one step");
    const Shot sh = render(c);
    c.check(sh.is(150, 240, 0, 170, 0) && sh.white(150, 100) && sh.white(150, 270), "riemannUpperCoversCurve: filled between the base and the curve only");
    if (state().lastFill) c.out(QString("   riemann (6 columns): %1 ms").arg(state().lastFill->ms, 0, 'f', 1));
  }
  ed.undo();
  c.check(imgs() == 0 && outlines() == 0 && c.els().size() == b2, "riemannUndoAll");
  ed.redo();
  c.check(imgs() == 1 && outlines() == 6, "riemannRedoAll");
  ed.undo();
  {
    const auto upper = state().lastRiemann;
    state().fill.rule = RiemannRule::Lower;
    drag(c, {{130, 250}, {370, 250}});
    const auto lower = state().lastRiemann;
    bool under = upper && lower && !lower->cols.empty();
    if (under) for (size_t i = 0; i < lower->cols.size(); i++) for (const auto& u : upper->cols) if (std::abs(u.a[0] - lower->cols[i].a[0]) < 0.5) under = under && lower->cols[i].h <= u.h + 1e-6;
    c.check(under && lower->drawn < 6 + 1, "riemannLowerStaysUnder: the lower sum is never taller than the upper one");
    ed.undo();
  }
  state().fill.rule = RiemannRule::Upper;
  state().fill.by = RiemannBy::Width;
  state().fill.width = 40;
  drag(c, {{130, 250}, {370, 250}});
  c.check(state().lastRiemann && state().lastRiemann->drawn == 6 && near(state().lastRiemann->width, 40, 0.5), "riemannByWidth: 240 / 40 = 6 columns");
  ed.undo();
  state().fill.by = RiemannBy::Count;
  state().fill.outline = false;
  drag(c, {{130, 250}, {370, 250}});
  c.check(outlines() == 0 && imgs() == 1, "riemannNoOutline");
  ed.undo();
  state().fill.outline = true;
  notes.clear();
  drag(c, {{130, 100}, {370, 100}});
  c.check(imgs() == 0 && !notes.isEmpty() && notes.last().contains("No hay curva"), "riemannNoCurveNoRectangles: with no curve over the base nothing is made and the notice says why");
  notes.clear();
  drag(c, {{200, 250}});
  c.check(imgs() == 0 && !notes.isEmpty() && notes.last().contains("arrastra una línea base"), "riemannClickNeedsBaseline");
  state().fill.opacity = 0.5;
  drag(c, {{130, 250}, {370, 250}});
  c.check(render(c).is(150, 240, 127, 212, 127, 30), "riemannOpacity: half opacity over the paper");
  {
    const auto re = savedAndRead(c);   // (not a math object: only checks that the file saves and loads with the fill in it)
    (void)re;
    QTemporaryDir dir;
    const QString f = dir.filePath("fill.xopp");
    bool ok = saveXopp(f, ed.doc());
    const LoadResult lr = loadXopp(f);
    int fills = 0, riem = 0;
    if (lr.ok) for (const Element& e : lr.doc.pages[size_t(c.page)].layers[size_t(c.layer())].elements) { if (const auto* i = std::get_if<Image>(&e)) if (attrFind(i->attrs, "xlFill")) fills++; if (const auto* s = std::get_if<Stroke>(&e)) if (attrFind(s->attrs, "xlRiemann")) riem++; }
    c.check(ok && lr.ok && fills == 1 && riem == 6, "riemannRoundTrip: the fill (xlFill) and the outlines (xlRiemann) survive the file");
  }
  state().fill.opacity = 1;
  state().fill.kind = FillKind::Solid;

  // 3. click fill
  wipe(c);
  state().fill.magnet = false;
  auto circle = [&](double cx, double cy, double r, double from = 0, double to = 2 * M_PI) {
    std::vector<QPointF> o;
    const int n = 90;
    for (int i = 0; i <= n; i++) { const double a = from + (to - from) * i / n; o.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r}); }
    return o;
  };
  addInk(c, circle(250, 250, 70));
  drag(c, {{250, 250}});
  {
    const auto lf = state().lastFill;
    const Shot sh = render(c);
    c.check(lf && !lf->path && !lf->wholePage && sh.is(250, 250, 0, 170, 0) && sh.is(210, 210, 0, 170, 0) && sh.white(250, 150) && sh.white(350, 250), "fillClickMode: a click fills the closed shape and stops at its line");
    const auto* im = std::get_if<Image>(&c.els()[0]);
    c.check(im && attrOr(im->attrs, "right").toDouble() - attrOr(im->attrs, "left").toDouble() < 200, "fillClickSmallImage: the image is the size of the shape, not of the page");
    if (lf) c.out(QString("   click fill: %1 ms, %2 pixels").arg(lf->ms, 0, 'f', 1).arg(lf->pixels));
  }
  ed.undo();
  // open shape leaks; the magnet closes a small opening; Shift turns the magnet off for one fill
  wipe(c);
  addInk(c, circle(250, 250, 70, 0.114, 2 * M_PI));   // an 8-unit opening
  notes.clear();
  drag(c, {{250, 250}});
  c.check(state().lastFill && state().lastFill->wholePage && std::any_of(notes.begin(), notes.end(), [](const QString& n) { return n.contains("abertura"); }), "fillOpenShapeLeaks: the fill escapes through the opening and the notice says to raise the gap");
  ed.undo();
  state().fill.magnet = true;
  drag(c, {{250, 250}});
  c.check(state().lastFill && !state().lastFill->wholePage && render(c).is(250, 250, 0, 170, 0) && render(c).white(350, 250), "fillMagnetClosesSmallOpening: with the magnet the same shape is filled bounded");
  ed.undo();
  drag(c, {{250, 250}}, Qt::ShiftModifier);
  c.check(state().lastFill && state().lastFill->wholePage, "fillShiftFillsAnyway: Shift turns the magnet off for that fill");
  ed.undo();
  state().fill.magnet = false;
  // gradients
  wipe(c);
  addInk(c, circle(250, 250, 70));
  state().fill.kind = FillKind::Linear;
  state().fill.color2 = "#0000ff";
  state().fill.angle = 0;
  drag(c, {{250, 250}});
  {
    const Shot sh = render(c);
    const QColor l = QColor::fromRgb(sh.at(195, 250)), r = QColor::fromRgb(sh.at(305, 250));
    c.check(l.green() > 100 && r.blue() > 100 && r.green() < l.green() - 40, "fillLinearGradient: from the pen colour on the left to the second colour on the right");
  }
  ed.undo();
  state().fill.kind = FillKind::Radial;
  drag(c, {{250, 250}});
  {
    const Shot sh = render(c);
    const QColor mid = QColor::fromRgb(sh.at(250, 250)), edge = QColor::fromRgb(sh.at(250, 190));
    c.check(mid != edge && std::abs(mid.green() - edge.green()) + std::abs(mid.blue() - edge.blue()) > 40, "fillRadialGradient: centre and edge differ");
  }
  ed.undo();
  state().fill.kind = FillKind::Solid;
  state().fill.opacity = 0.5;
  drag(c, {{250, 250}});
  c.check(render(c).is(250, 250, 127, 212, 127, 30), "fillOpacity: half opacity");
  ed.undo();
  state().fill.opacity = 1;
  // a later fill goes over an earlier one (the image is placed after the earlier fills, under the ink)
  drag(c, {{250, 250}});
  st.color = "#cc0000";
  ed.setStyle(st);
  drag(c, {{250, 250}});
  c.check(render(c).is(250, 250, 204, 0, 0), "fillLaterOnTop: the latest fill wins over the same area");
  ed.undo();
  ed.undo();
  st.color = "#00aa00";
  ed.setStyle(st);
  // keep the dragged line as a stroke: one undo step with the fill
  wipe(c);
  addInk(c, hump);
  state().fill.keepLine = true;
  const size_t b3 = c.els().size();
  const int d3 = ed.undoDepth();
  drag(c, line(500));
  c.check(c.els().size() == b3 + 2 && ed.undoDepth() == d3 + 1, "fillKeepLine: the line is kept as a stroke");
  ed.undo();
  c.check(c.els().size() == b3, "fillKeepLineUndoneTogether");
  state().fill.keepLine = false;
  // a plane's axes and grid are not walls (they are generated ink)
  {
    const FillOptions o = sanitizeFill(QString("{\"opacity\":7,\"gap\":-3,\"count\":9999,\"kind\":\"nope\",\"side\":\"right\"}"));
    c.check(o.opacity == 1 && o.gap == 0 && o.count == 400 && o.kind == FillKind::Solid && o.side == FillSide::Right, "fillSanitize: bad values take their defaults or are clamped");
  }
  // the panel
  wipe(c);
  {
    state().fill = FillOptions{};
    QWidget* fp = panel(c, "fill", "fillPanel");
    c.check(fp && buttonOf(c, "fill")->menu()->isVisible(), "fillPanelOpens: the caret of the fill tool opens the panel");
    if (fp) {
      QStringList seen;
      FillOptions got;
      state().fillChanged = [&](const FillOptions& f) { got = f; seen << "x"; };
      auto* right = fp->findChild<QAbstractButton*>("sideRight");
      right->click();
      auto* kind = child<QComboBox>(fp, "fillKind");
      kind->setCurrentIndex(kind->findData("linear"));
      pump(3);
      c.check(state().fill.side == FillSide::Right && state().fill.kind == FillKind::Linear && fp->findChild<QWidget*>("fillAngle")->isVisibleTo(fp) && fp->findChild<QWidget*>("fillColor2")->isVisibleTo(fp) && !seen.isEmpty(), "fillPanelSetsOptions: side and kind change the options; angle and second colour appear for a linear gradient");
      kind->setCurrentIndex(kind->findData("riemann"));
      pump(3);
      c.check(state().fill.kind == FillKind::Riemann && fp->findChild<QWidget*>("fillRule")->isVisibleTo(fp) && fp->findChild<QWidget*>("fillBase")->isVisibleTo(fp) && fp->findChild<QWidget*>("fillBy")->isVisibleTo(fp) && fp->findChild<QWidget*>("fillOutline")->isVisibleTo(fp) && !fp->findChild<QWidget*>("fillAngle")->isVisibleTo(fp), "fillPanelRiemann: the Riemann rows appear and the gradient rows go");
      child<QSlider>(fp, "fillOpacity")->setValue(50);
      child<QSlider>(fp, "fillGap")->setValue(7);   // half units
      child<QComboBox>(fp, "fillRule")->setCurrentIndex(child<QComboBox>(fp, "fillRule")->findData("mid"));
      auto* magnet = fp->findChild<QAbstractButton*>("fillMagnet");
      magnet->setChecked(false);
      pump(3);
      c.check(near(state().fill.opacity, 0.5) && near(state().fill.gap, 3.5) && state().fill.rule == RiemannRule::Mid && !state().fill.magnet && !fp->findChild<QWidget*>("fillMax")->isVisibleTo(fp), "fillPanelValues: opacity, gap, rule and magnet reach the options; the magnet's reach hides with it");
      state().fillChanged = nullptr;
    }
    closePanels(c);
    c.check(!buttonOf(c, "fill")->menu()->isVisible(), "fillPanelEscCloses");
    state().fill = FillOptions{};
  }
  ed.action("tool.fill")->trigger();
  c.check(ed.tool() == "fill", "fillMenu: the fill action selects the tool");
  {
    bool inMenu = false;
    for (QAction* a : c.win.menu("tools")->actions()) if (a->text().contains("Relleno")) inMenu = true;
    c.check(inMenu, "fillInMenu");
  }
  ed.setTool("pen");
}

// ---- the real Qt event path and what the pointer path costs ----------------------------------------------------------------------------
void sendMouse(DocumentView& v, QEvent::Type type, const QPointF& viewPos, Qt::MouseButton button, Qt::MouseButtons buttons) {
  QMouseEvent ev(type, viewPos, v.viewport()->mapToGlobal(viewPos), button, buttons, Qt::NoModifier);
  QApplication::sendEvent(v.viewport(), &ev);
}

struct Stat { double mean = 0, p99 = 0, max = 0; };
Stat statOf(std::vector<double> v) {
  Stat s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  for (double x : v) s.mean += x;
  s.mean /= double(v.size());
  s.p99 = v[std::min(v.size() - 1, size_t(double(v.size()) * 0.99))];
  s.max = v.back();
  return s;
}
QString fmtStat(const Stat& s) { return QString("mean %1  p99 %2  max %3 ms").arg(s.mean, 0, 'f', 3).arg(s.p99, 0, 'f', 3).arg(s.max, 0, 'f', 3); }

void eventAndTiming(Ctx& c) {
  Editor& ed = c.ed;
  DocumentView& v = c.v;
  c.out("\n-- Qt events and cost");
  wipe(c);
  closePanels(c);
  v.setZoom(1);
  v.scrollToPage(c.page);
  pump(4);
  // a mouse drag delivered to the viewport (DocumentView -> Editor -> tool), not a direct call
  ed.setTool("numline");
  sendMouse(v, QEvent::MouseButtonPress, v.toView(c.page, {100, 300}), Qt::LeftButton, Qt::LeftButton);
  for (int i = 1; i <= 10; i++) sendMouse(v, QEvent::MouseMove, v.toView(c.page, {100 + i * 40.0, 300}), Qt::NoButton, Qt::LeftButton);
  sendMouse(v, QEvent::MouseButtonRelease, v.toView(c.page, {500, 300}), Qt::LeftButton, Qt::NoButton);
  pump(4);
  c.check(isKind(c, "line") && ed.tool() == "select" && near(lineSpec(c).pts[2], 500, 0.6), "a mouse drag on the viewport draws a line, selects it and goes to Select");
  closePanels(c);
  wipe(c);

  // per-event cost with a dense page: the preview is built from the gesture, never from what the page holds
  const int layer = c.layer();
  const int N = 8000;
  {
    std::vector<Element> many;
    many.reserve(size_t(N));
    for (int i = 0; i < N; i++) {
      Stroke s;
      s.attrs = {{"tool", "pen"}, {"color", "#303030ff"}, {"capStyle", "round"}};
      s.widths = {1.4};
      const double x = 20 + (i * 37) % 540, y = 20 + (i * 91) % 790;
      for (int k = 0; k < 20; k++) { s.pts.push_back(x + k * 2.0); s.pts.push_back(y + 8 * std::sin(k * 0.5)); }
      many.push_back(std::move(s));
    }
    ed.replaceElements(c.page, layer, 0, 0, std::move(many));
  }
  v.renderVisibleNow();
  pump(4);
  auto timeDrag = [&](const QString& tool, QPointF a, QPointF b, int moves, const QString& label) {
    ed.setTool(tool);
    std::vector<double> handle, both;
    ed.pointerDown(v.makeEvent(c.page, a, 0.5, PointerEvent::Kind::Mouse));
    pump(3);   // the first paint after the content changed re-renders the tiles of the dense page: not the tool's cost
    for (int i = 1; i <= moves; i++) {
      const QPointF p = a + (b - a) * (double(i) / moves);
      const PointerEvent ev = v.makeEvent(c.page, p, 0.5, PointerEvent::Kind::Mouse);
      QElapsedTimer tm;
      tm.start();
      ed.pointerMove(ev);
      handle.push_back(double(tm.nsecsElapsed()) / 1e6);
      QApplication::processEvents(QEventLoop::AllEvents);   // the repaint of the dirty rectangle
      both.push_back(double(tm.nsecsElapsed()) / 1e6);
    }
    QElapsedTimer up;
    up.start();
    ed.pointerUp(v.makeEvent(c.page, b, 0.5, PointerEvent::Kind::Mouse));
    const double upMs = double(up.nsecsElapsed()) / 1e6;
    pump(3);
    c.out(QString("   %1, page with %2 strokes, %3 moves").arg(label).arg(N).arg(moves));
    c.out("      handle:       " + fmtStat(statOf(handle)));
    c.out("      handle+paint: " + fmtStat(statOf(both)));
    c.out(QString("      release (build + swap + select): %1 ms").arg(upMs, 0, 'f', 2));
    closePanels(c);
  };
  state().line = defaultLine();
  state().line.rows = {{"(-2, 3] ∪ {4} ∪ [4.5, ∞)", "A"}, {"[0, 5)", "B"}};
  timeDrag("numline", {80, 120}, {520, 160}, 60, "line preview (2 rows)");
  state().venn = defaultVenn(3);
  state().venn.expr = "(A ∩ B) ∪ C'";
  state().venn.lists = {"1, 2, 3", "3, 4, 5", "5, 6, 1", "9"};
  timeDrag("venn", {60, 220}, {540, 520}, 60, "diagram preview (3 sets, hatching, elements)");
  c.out("   (the line preview is rebuilt per event; the diagram preview recomputes its hatching, so it is the heavier one)");
  // fill on the dense page: the walls are every stroke of the layer
  state().fill = FillOptions{};
  state().fill.magnet = false;
  ed.setTool("fill");
  {
    ed.pointerDown(v.makeEvent(c.page, {300, 400}, 0.5, PointerEvent::Kind::Mouse));
    QElapsedTimer ft;
    ft.start();
    ed.pointerUp(v.makeEvent(c.page, {300, 400}, 0.5, PointerEvent::Kind::Mouse));
    const double fillMs = double(ft.nsecsElapsed()) / 1e6;
    c.out(QString("   click fill, page with %1 strokes: pointer release %2 ms (of which runFill %3 ms; the rest inserts the image and records the step)").arg(N).arg(fillMs, 0, 'f', 1).arg(state().lastFill ? state().lastFill->ms : -1, 0, 'f', 1));
    pump(3);
  }
  state().fill = FillOptions{};
  // remove the dense content again: the checks that follow expect a clean page
  while (ed.undoDepth() > c.baseDepth) ed.undo();
  ed.replaceElements(c.page, layer, 0, int(c.els().size()), {});
  ed.selection().clear();
  ed.setTool("pen");
  pump(3);
}

// ---- a page to look at and the panels --------------------------------------------------------------------------------------------------
void screenshots(Ctx& c, const QString& shots) {
  Editor& ed = c.ed;
  c.out("\n-- screenshots");
  QDir().mkpath(shots);
  wipe(c);
  closePanels(c);
  {
    Style st = ed.style();
    st.color = "#0044cc";
    ed.setStyle(st);
  }
  state().line = defaultLine();
  state().venn = defaultVenn(2);
  state().fill = FillOptions{};
  c.v.zoomFitPage();
  c.v.scrollToPage(c.page);
  // a line with two rows and a diagram with elements, made with the tools
  state().line.rows = {{"(-2, 3] ∪ {4} ∪ [4.5, ∞)", "A"}, {"[0, 5)", "B"}};
  ed.setTool("numline");
  drag(c, {{80, 120}, {300, 120}, {520, 120}});
  closePanels(c);
  state().venn = defaultVenn(2);
  state().venn.expr = "A ∩ B'";
  state().venn.lists = {"1, 2, 3", "3, 4, 5", "9"};
  ed.setTool("venn");
  drag(c, {{60, 220}, {300, 340}, {540, 470}});
  closePanels(c);
  ed.setTool("pen");
  // fill under a hump with Riemann rectangles
  std::vector<QPointF> hump;
  for (int i = 0; i <= 60; i++) { const double x = 100 + i / 60.0 * 300; hump.push_back({x, 700 - 110 * std::sin(M_PI * i / 60.0)}); }
  addInk(c, hump);
  state().fill.kind = FillKind::Riemann;
  state().fill.count = 10;
  state().fill.opacity = 0.55;
  ed.setTool("fill");
  drag(c, {{100, 700}, {400, 700}});
  state().fill = FillOptions{};
  ed.setTool("pen");
  c.v.scrollToPage(c.page);
  c.v.renderVisibleNow();
  pump(6);
  c.out(c.win.grab().save(shotName(shots, "math_page.png")) ? "   saved math_page.png (window with a line, a diagram and Riemann rectangles)" : "   FAILED to save math_page.png");

  // the panels, each with its object selected
  ed.selection().clear();
  ed.notifySelectionChanged();
  {
    wipe(c);
    ed.setTool("numline");
    state().line.rows = {{"(-2, 3] ∪ {4} ∪ [4.5, ∞)", "A"}, {"R ∖ {2}", ""}};
    drag(c, {{80, 120}, {520, 120}});
    closePanels(c);
    QWidget* p = makeMathPanel(nullptr, ed, "line");
    p->show();
    pump(4);
    c.out(p->grab().save(shotName(shots, "panel_line.png")) ? QString("   saved panel_line.png (%1 x %2)").arg(p->width()).arg(p->height()) : "   FAILED panel_line.png");
    delete p;
    // an error in the second row
    QWidget* q = makeMathPanel(nullptr, ed, "line");
    q->show();
    pump(3);
    enter(child<QLineEdit>(q, "expr1"), "[1, 3) ∪ (4,");
    c.out(q->grab().save(shotName(shots, "panel_line_error.png")) ? "   saved panel_line_error.png" : "   FAILED panel_line_error.png");
    delete q;
  }
  {
    wipe(c);
    state().venn = defaultVenn(3);
    state().venn.expr = "(A ∩ B) ∪ C'";
    state().venn.lists = {"1, 2", "2, 3", "3, 4", "9"};
    ed.setTool("venn");
    drag(c, {{60, 220}, {540, 520}});
    closePanels(c);
    QWidget* p = makeMathPanel(nullptr, ed, "venn");
    p->show();
    pump(4);
    c.out(p->grab().save(shotName(shots, "panel_venn.png")) ? QString("   saved panel_venn.png (%1 x %2)").arg(p->width()).arg(p->height()) : "   FAILED panel_venn.png");
    delete p;
    c.v.zoomFitPage();
    c.v.scrollToPage(c.page);
    c.v.renderVisibleNow();
    pump(4);
    c.win.grab().save(shotName(shots, "math_venn3.png"));
  }
  for (const auto& [kind, name] : std::vector<std::pair<FillKind, QString>>{{FillKind::Solid, "panel_fill_solid.png"}, {FillKind::Linear, "panel_fill_linear.png"}, {FillKind::Riemann, "panel_fill_riemann.png"}}) {
    state().fill = FillOptions{};
    state().fill.kind = kind;
    QWidget* p = makeFillPanel(nullptr, ed);
    p->show();
    pump(4);
    c.out(p->grab().save(shotName(shots, name)) ? QString("   saved %1 (%2 x %3)").arg(name).arg(p->width()).arg(p->height()) : "   FAILED " + name);
    delete p;
  }
  state().fill = FillOptions{};
}

}  // namespace

int mathObjectsSelfTest(MainWindow& win, Editor& ed, const QString& shots, const std::function<void(const QString&)>& out) {
  out("\n== mathobjects: real line, set diagrams, fill and Riemann (synthetic events through the real tool and panel path; not a physical pen) ==");
  DocumentView& v = *win.view();
  const bool ownSelection = dynamic_cast<NullSelection*>(&ed.selection()) != nullptr;
  if (ownSelection) {
    ed.setSelection(std::make_unique<MiniSelection>(ed));
    out("   (no selection implementation installed: using a minimal stand-in that holds the selected pieces)");
  }
  // a scratch page at the end, removed afterwards; the user's pen settings and the area's defaults are restored
  const Style savedStyle = ed.style();
  const State savedState = state();
  const int pagesBefore = ed.pageCount();
  const int depth0 = ed.undoDepth();
  const int page = ed.insertPage(pagesBefore, pagesBefore - 1);
  ed.setCurrentPage(page);
  Ctx c{win, ed, v, page, out};
  c.baseDepth = ed.undoDepth();
  {
    Style st = savedStyle;
    st.color = "#0044cc";
    st.penWidth = 1.41;
    st.glowOn = false;
    ed.setStyle(st);
  }
  state().line = defaultLine();
  state().venn = defaultVenn(2);
  state().fill = FillOptions{};
  v.setZoom(1);
  v.scrollToPage(page);
  pump(6);
  ed.setTool("pen");
  lineChecks(c);
  lineAnchorChecks(c);
  vennChecks(c);
  uiChecks(c);
  fillChecks(c);
  eventAndTiming(c);
  screenshots(c, shots);
  // clean up
  closePanels(c);
  wipe(c);
  ed.setTool("pen");
  ed.setStyle(savedStyle);
  state().line = savedState.line;
  state().venn = savedState.venn;
  state().fill = savedState.fill;
  state().lastFill.reset();
  state().lastRiemann.reset();
  ed.selection().clear();
  while (ed.undoDepth() > depth0) ed.undo();   // the scratch page goes too
  if (ownSelection) ed.setSelection(nullptr);
  v.zoomFitWidth();
  v.scrollToPage(0);
  pump(3);
  out(c.fail ? QString("mathobjects: %1 CHECK(S) FAILED").arg(c.fail) : QStringLiteral("mathobjects: all checks passed"));
  return c.fail;
}

}  // namespace mathobj
