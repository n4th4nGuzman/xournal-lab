// Number lines and set diagrams as objects: what building draws (the e2e of the Electron app, "MATH" block), the exact hatching checked point by point
// against the regions, anchors, build -> read round trips, reading after the pieces were moved / turned / scaled / mirrored, and save and reload as .xopp.
#include "mathobj.h"
#include "setexpr.h"
#include "xopp.h"
#include <QGuiApplication>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <map>

using namespace sets;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 50) { std::printf("FAIL line %d: %s  ", __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } } while (0)

static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

using Elements = std::vector<Element>;
static std::vector<const Element*> members(const Elements& els) {
  std::vector<const Element*> m;
  for (const Element& e : els) m.push_back(&e);
  return m;
}
static std::vector<const Stroke*> withRole(const Elements& els, const QString& role) {
  std::vector<const Stroke*> out;
  for (const Element& e : els)
    if (const auto* s = std::get_if<Stroke>(&e); s && roleOf(e) == role) out.push_back(s);
  return out;
}
static size_t countRole(const Elements& els, const QString& role) {
  size_t n = 0;
  for (const Element& e : els) n += roleOf(e) == role;
  return n;
}
static QStringList labels(const Elements& els) {
  QStringList out;
  for (const Element& e : els)
    if (const auto* t = std::get_if<Text>(&e); t && roleOf(e) == QString("setlabel")) out << t->text;
  return out;
}
static QStringList sorted(QStringList l) { l.sort(); return l; }

static GenStyle blue() { GenStyle s; s.color = "#0044cc"; s.width = 1.41; return s; }

static LineSpec lineWith(const QString& expr) {
  LineSpec s = defaultLine({100, 150}, {500, 150});
  s.rows[0].expr = expr;
  return s;
}
static V2 posOn(const LineSpec& s, double v) {
  const double k = (v - s.min) / (s.max - s.min);
  return {s.pts[0] + (s.pts[2] - s.pts[0]) * k, s.pts[1] + (s.pts[3] - s.pts[1]) * k};
}

// ---- strokes of a built line, classified like the e2e does
static std::vector<const Stroke*> dots(const Elements& e) {
  std::vector<const Stroke*> out;
  for (const Stroke* s : withRole(e, "setbody")) if (s->pts.size() == 4 && near(s->pts[0], s->pts[2], 0.02) && near(s->pts[1], s->pts[3], 0.02)) out.push_back(s);
  return out;
}
static std::vector<const Stroke*> rings(const Elements& e) {
  std::vector<const Stroke*> out;
  for (const Stroke* s : withRole(e, "setbody")) if (s->pts.size() == 50) out.push_back(s);
  return out;
}
static std::vector<const Stroke*> highlights(const Elements& e) {
  std::vector<const Stroke*> out;
  for (const Stroke* s : withRole(e, "setbody")) if (s->widths[0] > 1.41 * 2 && s->pts.size() == 4 && !near(s->pts[0], s->pts[2], 0.02)) out.push_back(s);
  return out;
}
static size_t ptsCount(const Elements& e, size_t n) {
  size_t k = 0;
  for (const Stroke* s : withRole(e, "setbody")) k += s->pts.size() == n;
  return k;
}

static void testLines() {
  // lineCreated: axis, marks and numbers
  const LineSpec def = defaultLine({100, 150}, {500, 150});
  const Elements e = buildLine("plTEST", def, blue());
  CHECK(withRole(e, "setwitness").size() == 1 && countRole(e, "setbody") >= 1 && countRole(e, "setmark") >= 11 && countRole(e, "setlabel") >= 11, "lineCreated");
  CHECK(isMath(members(e)), "isMath");
  CHECK(sorted(labels(e)) == sorted(QStringList{"−1", "−2", "−3", "−4", "−5", "0", "1", "2", "3", "4", "5"}), "lineNumbersTexts: %s", qPrintable(labels(e).join(",")));
  CHECK(attrOr(withRole(e, "setbody")[0]->attrs, "color") == "#0044ccff" && !planeIdOf(e.back()).value_or("").isEmpty(), "lineStyleFromPen");
  // every piece belongs to one object
  bool one = true;
  for (const Element& el : e) one = one && planeIdOf(el) == QString("plTEST");
  CHECK(one, "all pieces carry the object id");
  // the witness is invisible
  CHECK(attrOr(withRole(e, "setwitness")[0]->attrs, "color") == "#0044cc00", "witness is invisible");

  // lineEnds: read back
  {
    const auto r = readMath(members(e));
    CHECK(r && std::holds_alternative<LineSpec>(r->spec), "readMath finds a line");
    if (r) {
      const LineSpec& s = std::get<LineSpec>(r->spec);
      CHECK(near(s.pts[0], 100, 1e-3) && near(s.pts[1], 150, 1e-3) && near(s.pts[2], 500, 1e-3) && near(s.pts[3], 150, 1e-3), "lineEnds");
      CHECK(r->style.color == "#0044cc" && near(r->style.width, 1.41), "style read back: %s %g", qPrintable(r->style.color), r->style.width);
    }
  }

  {  // lineExprKept, open circle, closed dots, highlights, a gap in the axis, infinity to the end
    const LineSpec s = lineWith("(-2, 3] ∪ {4} ∪ [4.5, inf)");
    const Elements el = buildLine("pl", s, blue());
    const auto sets_ = lineSets(s);
    CHECK(sets_[0].set && formatSet(*sets_[0].set) == "(−2, 3] ∪ {4} ∪ [4.5, ∞)", "panel text");
    const auto rg = rings(el);
    CHECK(rg.size() == 1 && near(rg[0]->pts[0] + (rg[0]->pts[24] - rg[0]->pts[0]) / 2, posOn(s, -2)[0], 0.01), "lineOpenCircleAt");
    const auto cl = dots(el);
    bool allDots = cl.size() == 3;
    for (double v : {3.0, 4.0, 4.5}) {
      bool found = false;
      for (const Stroke* d : cl) found = found || (near(d->pts[0], posOn(s, v)[0], 0.02) && near(d->pts[1], posOn(s, v)[1], 0.02));
      allDots = allDots && found;
    }
    CHECK(allDots, "lineClosedDotsAt");
    const auto hi = highlights(el);
    bool wide = hi.size() == 2;
    for (const Stroke* h : hi) wide = wide && h->widths[0] > 2.5 * 1.41 - 0.01;
    CHECK(wide, "lineHighlights (%zu)", hi.size());
    const double x = posOn(s, -2)[0];
    bool noInside = true;
    for (const Stroke* a : withRole(el, "setbody"))
      if (a->widths[0] == 1.41 && a->pts.size() == 4 && std::min(a->pts[0], a->pts[2]) < x - 1 && std::max(a->pts[0], a->pts[2]) > x + 1) noInside = false;
    CHECK(noInside, "lineLineHasAGapInsideACircle");
    const Stroke* right = nullptr;
    for (const Stroke* h : hi) if (std::max(h->pts[0], h->pts[2]) > posOn(s, 5)[0] + 10 && std::min(h->pts[0], h->pts[2]) > posOn(s, 4)[0]) right = h;
    CHECK(right && std::max(right->pts[0], right->pts[2]) >= s.pts[2] + 10, "lineInfinityRunsToTheEnd");
  }
  {
    Elements el = buildLine("pl", lineWith("R \\ {2}"), blue());
    CHECK(rings(el).size() == 1 && dots(el).empty() && highlights(el).size() == 2, "lineHoleIsOneCircle");
    el = buildLine("pl", lineWith("(1,3)"), blue());
    CHECK(rings(el).size() == 2 && dots(el).empty() && highlights(el).size() == 1, "lineOpenInterval");
    el = buildLine("pl", lineWith("[1,3]"), blue());
    CHECK(dots(el).size() == 2 && rings(el).empty() && highlights(el).size() == 1, "lineClosedInterval");
    el = buildLine("pl", lineWith("[1,3)"), blue());
    CHECK(dots(el).size() == 1 && rings(el).size() == 1, "lineHalfOpen");
    // a mistake is reported apart; the row draws as empty
    const LineSpec bad = lineWith("(1,");
    const auto r = lineSets(bad);
    CHECK(!r[0].set && r[0].error.contains("paréntesis"), "lineErrorShown: %s", qPrintable(r[0].error));
    el = buildLine("pl", bad, blue());
    CHECK(rings(el).empty() && dots(el).empty() && highlights(el).empty(), "an erroneous row draws nothing");
    // brackets and parentheses
    LineSpec b = lineWith("[1,3]");
    b.brackets = true;
    el = buildLine("pl", b, blue());
    CHECK(rings(el).empty() && dots(el).empty() && ptsCount(el, 8) == 2, "lineBrackets");
    b.rows[0].expr = "(1,3)";
    el = buildLine("pl", b, blue());
    CHECK(ptsCount(el, 22) == 2, "lineParentheses");
  }
  {  // second row, its own set, range and step, tick cap
    LineSpec s = lineWith("(-2,3]");
    s.rows.push_back({"[0,5]", "B"});
    const Elements el = buildLine("pl", s, blue());
    std::vector<double> ys;
    for (const Stroke* a : withRole(el, "setbody"))
      if (a->pts.size() == 4 && a->widths[0] <= 1.5) { const double y = std::round(a->pts[1] * 10) / 10; if (std::find(ys.begin(), ys.end(), y) == ys.end()) ys.push_back(y); }
    CHECK(ys.size() == 2 && near(*std::min_element(ys.begin(), ys.end()), 150 - 40, 0.2), "lineSecondRow (%zu levels)", ys.size());
    CHECK(labels(el).contains("B"), "row label drawn");
    const auto sets_ = lineSets(s);
    CHECK(formatSet(*sets_[0].set) == "(−2, 3]" && formatSet(*sets_[1].set) == "[0, 5]", "rows have their own set");
    LineSpec r = lineWith("");
    r.min = 0; r.max = 10; r.step = 2;
    CHECK(labels(buildLine("pl", r, blue())).join(",") == "0,2,4,6,8,10", "lineRange");
    r.step = 0.001;
    CHECK(countRole(buildLine("pl", r, blue()), "setmark") <= 250, "lineTicksCapped");
    LineSpec ev = lineWith("");
    ev.every = 2;
    CHECK(labels(buildLine("pl", ev, blue())).join(",") == "−5,−3,−1,1,3,5", "a number every 2 marks: %s", qPrintable(labels(buildLine("pl", ev, blue())).join(",")));
  }
  {  // sanitize: an invalid spec becomes a usable one
    LineSpec s;
    s.pts = {1, 2};
    s.min = 4; s.max = 2; s.every = 0;
    const LineSpec t = sanitizeLine(s);
    CHECK(t.pts.size() == 4 && t.max == 5 && t.every == 1 && t.rows.size() == 1 && t.step >= (t.max - t.min) / 400, "sanitizeLine");
  }
  {  // anchors
    const LineSpec s = lineWith("(-2, 3]");
    CHECK(lineAnchors(s).size() == 2, "two anchors");
    const LineSpec m = moveLineAnchor(s, 1, {620, 150}, false);
    CHECK(near(m.pts[2], 620) && near(m.pts[0], s.pts[0]) && m.rows[0].expr == "(-2, 3]", "lineAnchorDrag");
    const LineSpec sh = moveLineAnchor(s, 1, {400, 300}, true);   // atan2(150, 300) = 26.6 deg -> 30 deg
    CHECK(near(std::atan2(sh.pts[3] - sh.pts[1], sh.pts[2] - sh.pts[0]), M_PI / 6, 1e-9), "shift snaps the angle to 15 degrees");
    const LineSpec tiny = moveLineAnchor(s, 1, {110, 150}, false);
    CHECK(tiny.pts == s.pts, "an axis shorter than 20 is refused");
  }
  {  // no arrows: infinity gets its own head
    LineSpec s = lineWith("[1,inf)");
    const size_t withArrows = countRole(buildLine("pl", s, blue()), "setarrow");
    s.arrows = false;
    const size_t without = countRole(buildLine("pl", s, blue()), "setarrow");
    CHECK(withArrows == 2 && without == 1, "arrows %zu / %zu", withArrows, without);
  }
}

// ---- Venn
static bool checkShading(const VennSpec& s, const Elements& el) {
  const Parsed p = parseSetExpr(s.expr);
  if (!p.ok) return false;
  QStringList names;
  for (int i = 0; i < s.k; i++) names << s.names[size_t(i)];
  const auto reg = evalRegions(p.expr, names);
  if (!reg.ok) return false;
  const auto segs = withRole(el, "sethatch");
  if (segs.empty()) { for (bool b : reg.value) if (b) return false; return true; }
  // 1) the middle and the ends (pulled in a little) of each stretch are in shaded regions
  for (const Stroke* q : segs) {
    const double ax = q->pts[0], ay = q->pts[1], bx = q->pts[2], by = q->pts[3];
    for (double t : {0.5, 0.05, 0.95}) if (!reg.value[size_t(maskAt(s, {ax + (bx - ax) * t, ay + (by - ay) * t}))]) return false;
  }
  // 2) points of the shaded regions (not near a border) have a stretch within half the spacing; points of the others have none close
  const double x0 = std::min(s.pts[0], s.pts[2]), y0 = std::min(s.pts[1], s.pts[3]), x1 = std::max(s.pts[0], s.pts[2]), y1 = std::max(s.pts[1], s.pts[3]);
  auto border = [&](double px, double py) {
    double d = std::min({px - x0, x1 - px, py - y0, y1 - py});
    for (int i = 0; i < s.k; i++) {
      const double cx = s.pts[size_t(4 + 4 * i)], cy = s.pts[size_t(5 + 4 * i)], r = std::hypot(s.pts[size_t(6 + 4 * i)] - cx, s.pts[size_t(7 + 4 * i)] - cy);
      d = std::min(d, std::abs(std::hypot(px - cx, py - cy) - r));
    }
    return d;
  };
  auto distToSegs = [&](double px, double py) {
    double d = INFINITY;
    for (const Stroke* q : segs) {
      const double ax = q->pts[0], ay = q->pts[1], dx = q->pts[2] - ax, dy = q->pts[3] - ay, L = dx * dx + dy * dy;
      const double t = std::max(0.0, std::min(1.0, ((px - ax) * dx + (py - ay) * dy) / (L == 0 ? 1 : L)));
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
  return tested > 20 && bad <= tested / 100 && unshadedHit == 0;
}

static VennSpec venn2() { return defaultVenn(2, {60, 300}, {520, 560}); }

static void testVenn() {
  VennSpec s = venn2();
  Elements el = buildVenn("plV", s, blue());
  CHECK(withRole(el, "setwitness").size() == 1 && countRole(el, "setbody") == 3, "vennCreated");
  CHECK(sorted(labels(el)) == sorted(QStringList{"A", "B", "U"}), "vennLabels: %s", qPrintable(labels(el).join(",")));
  {  // geometry: circles inside the universe, overlapping but not nested
    const double x0 = std::min(s.pts[0], s.pts[2]), y0 = std::min(s.pts[1], s.pts[3]), x1 = std::max(s.pts[0], s.pts[2]), y1 = std::max(s.pts[1], s.pts[3]);
    double cx[2], cy[2], r[2];
    bool inside = true;
    for (int i = 0; i < 2; i++) {
      cx[i] = s.pts[size_t(4 + 4 * i)]; cy[i] = s.pts[size_t(5 + 4 * i)];
      r[i] = std::hypot(s.pts[size_t(6 + 4 * i)] - cx[i], s.pts[size_t(7 + 4 * i)] - cy[i]);
      inside = inside && cx[i] - r[i] > x0 && cx[i] + r[i] < x1 && cy[i] - r[i] > y0 && cy[i] + r[i] < y1;
    }
    const double d = std::hypot(cx[0] - cx[1], cy[0] - cy[1]);
    CHECK(near(x0, 60, 0.01) && near(y1, 560, 0.01) && inside && d < r[0] + r[1] && d > std::abs(r[0] - r[1]), "vennGeometry");
  }
  // shading of eight expressions on two sets, point by point
  for (const char* e : {"A ∩ B", "A ∪ B", "A ∖ B", "B ∖ A", "A'", "(A ∪ B)'", "A Δ B", "A' ∩ B'", "A ∩ B'", "A", "U"}) {
    s.expr = e;
    CHECK(checkShading(s, buildVenn("plV", s, blue())), "shading of %s", e);
  }
  s.dense = true;
  for (const char* e : {"A ∩ B", "(A ∪ B)'"}) { s.expr = e; CHECK(checkShading(s, buildVenn("plV", s, blue())), "dense shading of %s", e); }
  s.dense = false;
  {  // the hatching is finer when dense, and its strokes are semi-transparent ordinary strokes
    s.expr = "A ∪ B";
    const size_t light = withRole(buildVenn("plV", s, blue()), "sethatch").size();
    s.dense = true;
    const Elements dn = buildVenn("plV", s, blue());
    CHECK(withRole(dn, "sethatch").size() > light * 1.5 && light > 5, "dense hatch has more lines (%zu vs %zu)", withRole(dn, "sethatch").size(), light);
    CHECK(attrOr(withRole(dn, "sethatch")[0]->attrs, "color") == "#0044ccb0", "hatch colour has alpha b0");
    s.dense = false;
  }
  {  // an intersection hatch stays within the lens
    s.expr = "A ∩ B";
    const Elements lens = buildVenn("plV", s, blue());
    double lo = INFINITY, hi = -INFINITY;
    for (const Stroke* q : withRole(lens, "sethatch")) for (size_t i = 0; i < 4; i += 2) { lo = std::min(lo, q->pts[i]); hi = std::max(hi, q->pts[i]); }
    CHECK(withRole(lens, "sethatch").size() > 3 && lo > std::min(s.pts[4], s.pts[8]) - 1 && hi < std::max(s.pts[4], s.pts[8]) + 200, "vennInterIsALens");
  }
  {  // a mistake: no shading, and it is reported
    s.expr = "A ∩ D";
    const Shading sh = shadedRegions(s);
    CHECK(sh.shaded.empty() && sh.error.contains("«D»"), "vennErrorShown: %s", qPrintable(sh.error));
    CHECK(withRole(buildVenn("plV", s, blue()), "sethatch").empty(), "no hatch on error");
    s.expr = "A ∩ B'";
    CHECK(shadedRegions(s).error.isEmpty() && !withRole(buildVenn("plV", s, blue()), "sethatch").empty(), "vennErrorGoes");
  }
  {  // elements
    s.expr = "A ∪ B";
    s.lists = {"1, 2", "2; 3, 4", "9"};
    const auto els = elementsOf(s);
    auto maskOf = [&](const QString& l) { for (const auto& e : els) if (e.label == l) return e.mask; return -1; };
    CHECK(maskOf("1") == 1 && maskOf("2") == 3 && maskOf("3") == 2 && maskOf("9") == 0 && els.size() == 5, "vennElementsMasks");
    const auto placed = placeElements(s);
    bool inReg = placed.size() == 5;
    for (const auto& p : placed) inReg = inReg && maskAt(s, p.at) == maskOf(p.label);
    CHECK(inReg, "vennElementsInRegions");
    std::vector<V2> in2;
    for (const auto& p : placed) if (maskAt(s, p.at) == 2) in2.push_back(p.at);
    CHECK(in2.size() == 2 && std::hypot(in2[0][0] - in2[1][0], in2[0][1] - in2[1][1]) > 12, "vennElementsApart");
    el = buildVenn("plV", s, blue());
    const QStringList texts = labels(el);
    bool drawn = true;
    for (const char* x : {"1", "2", "3", "4", "9"}) drawn = drawn && texts.contains(x);
    CHECK(drawn && countRole(el, "setmark") == 5, "vennElementsDrawn");
    // the elements follow the circles when one moves
    VennSpec m = moveVennAnchor(s, 2, {s.pts[4] - 60, s.pts[5] + 40}, false);
    bool follow = true;
    for (const auto& p : placeElements(m)) {
      int want = -1;
      for (const auto& e : elementsOf(m)) if (e.label == p.label) want = e.mask;
      follow = follow && maskAt(m, p.at) == want;
    }
    CHECK(follow, "vennElementsFollow");
  }
  {  // anchors
    const VennSpec s0 = venn2();
    CHECK(vennAnchors(s0).size() == 6, "2 corners + 2 circles x (centre, radius point)");
    const VennSpec mv = moveVennAnchor(s0, 2, {s0.pts[4] - 60, s0.pts[5] + 40}, false);
    CHECK(near(mv.pts[4], s0.pts[4] - 60, 0.05) && near(mv.pts[5], s0.pts[5] + 40, 0.05) && near(mv.pts[6] - mv.pts[4], s0.pts[6] - s0.pts[4], 1e-6) && near(mv.pts[8], s0.pts[8], 1e-6), "vennMoveCircle");
    const double rl = std::hypot(s0.pts[6] - s0.pts[4], s0.pts[7] - s0.pts[5]);
    const VennSpec rd = moveVennAnchor(s0, 3, {s0.pts[6] + (s0.pts[6] - s0.pts[4]) / rl * 20, s0.pts[7] + (s0.pts[7] - s0.pts[5]) / rl * 20}, false);
    CHECK(std::hypot(rd.pts[6] - rd.pts[4], rd.pts[7] - rd.pts[5]) > rl + 5 && near(rd.pts[4], s0.pts[4]), "vennRadius");
    const VennSpec co = moveVennAnchor(s0, 1, {s0.pts[2] + 40, s0.pts[3] + 20}, false);
    CHECK(near(co.pts[2], s0.pts[2] + 40, 0.05) && near(co.pts[0], s0.pts[0]), "vennUniverseCorner");
    CHECK(moveVennAnchor(s0, 1, {s0.pts[0] + 10, s0.pts[3]}, false).pts == s0.pts, "a universe that would be too small is refused");
    CHECK(moveVennAnchor(s0, 3, {s0.pts[4] + 3, s0.pts[5]}, false).pts == s0.pts, "a radius that would be too small is refused");
  }
  {  // names, 3 sets, universe off
    VennSpec s3 = venn2();
    s3.names[0] = "P";
    el = buildVenn("plV", s3, blue());
    CHECK(labels(el).contains("P") && !labels(el).contains("A"), "vennNames");
    s3.expr = "P ∩ B";
    CHECK(shadedRegions(s3).error.isEmpty() && !withRole(buildVenn("plV", s3, blue()), "sethatch").empty(), "vennNamedExpr");
    s3 = defaultVenn(3, {60, 300}, {520, 600});
    s3.names[0] = "P";
    el = buildVenn("plV", s3, blue());
    CHECK(s3.k == 3 && countRole(el, "setbody") == 4 && s3.pts.size() == 16 && labels(el).contains("C"), "vennThree");
    s3.expr = "(P ∩ B) ∪ C'";
    CHECK(checkShading(s3, buildVenn("plV", s3, blue())), "vennThreeShading");
    s3.expr = "P ∖ (B ∪ C)";
    CHECK(checkShading(s3, buildVenn("plV", s3, blue())), "vennThreeShading2");
    for (const char* e : {"A ∩ B ∩ C", "A Δ B Δ C", "(A ∪ B ∪ C)'", "B ∩ C ∖ A"}) {
      VennSpec t = defaultVenn(3, {60, 300}, {520, 600});
      t.expr = e;
      CHECK(checkShading(t, buildVenn("plV", t, blue())), "three sets: %s", e);
    }
    const double cx = (s3.pts[4] + s3.pts[8] + s3.pts[12]) / 3, cy = (s3.pts[5] + s3.pts[9] + s3.pts[13]) / 3;
    CHECK(maskAt(s3, {cx, cy}) == 7, "vennThreeGeometry: all three overlap in the middle");
    s3.universe = false;
    el = buildVenn("plV", s3, blue());
    CHECK(!labels(el).contains("U") && countRole(el, "setbody") == 3, "vennNoUniverse");
  }
}

// ---- reading after what the editor can do to the pieces
static Affine rot(double deg, double sx, double tx, double ty, bool mirror) {
  const double a = deg * M_PI / 180, c = std::cos(a) * sx, s = std::sin(a) * sx;
  return Affine{c, s, mirror ? s : -s, mirror ? -c : c, tx, ty};   // x' = a x + c y + e ; y' = b x + d y + f
}

static void transformAll(Elements& els, const Affine& T) {
  for (Element& e : els) {
    if (auto* s = std::get_if<Stroke>(&e)) s->pts = mapPoints(T, s->pts);
    else if (auto* t = std::get_if<Text>(&e)) {
      const auto m = mapPoints(T, {attrOr(t->attrs, "x").toDouble(), attrOr(t->attrs, "y").toDouble()});
      attrSet(t->attrs, "x", fmt8(m[0]));
      attrSet(t->attrs, "y", fmt8(m[1]));
    }
  }
}

static void testRoundTrips() {
  struct Case { const char* name; MathSpec spec; };
  LineSpec l1 = lineWith("(-2, 3] ∪ {4} ∪ [4.5, inf)");
  l1.rows.push_back({"R \\ {1}", "fila 2"});
  l1.brackets = true;
  l1.min = -3; l1.max = 8; l1.step = 0.5; l1.every = 2;
  LineSpec l2 = lineWith("[π/2, 2π)");
  l2.arrows = false; l2.ticks = false; l2.numbers = false;
  VennSpec v1 = venn2();
  v1.expr = "(A ∪ B)' ∪ (A ∩ B)";
  v1.lists = {"1, 2", "2; 3, 4", "9"};
  v1.names = {"P", "Q", "C"};
  v1.dense = true;
  VennSpec v2 = defaultVenn(3, {60, 300}, {520, 600});
  v2.expr = "(A ∩ B) ∪ C'";
  v2.universe = false;
  v2.lists = {"a", "b", "c", "d"};
  const std::vector<Case> cases{{"line rows", l1}, {"line plain", l2}, {"default line", defaultLine()}, {"venn 2", v1}, {"venn 3", v2}, {"default venn", defaultVenn(2)}};

  GenStyle style = blue();
  style.glow = Glow{6, 0.5};
  const Affine transforms[] = {rot(0, 1, 0, 0, false), rot(0, 1, 123.5, -40.25, false), rot(90, 1, 300, 20, false), rot(-90, 1, 0, 500, false), rot(37, 1.5, 11, 12, false),
                               rot(0, 1, 600, 0, true), rot(215, 0.4, -20, 80, true), Affine{1, 0, 0.3, 0.8, 5, 6} /* skew */};
  for (const Case& cs : cases) {
    const MathSpec spec = sanitizeMath(cs.spec);
    for (size_t ti = 0; ti < std::size(transforms); ti++) {
      Elements els = buildMath("plRT", spec, style);
      transformAll(els, transforms[ti]);
      const auto r = readMath(members(els));
      CHECK(r.has_value(), "%s / %zu: not read back", cs.name, ti);
      if (!r) continue;
      CHECK(r->spec.index() == spec.index(), "%s / %zu: kind", cs.name, ti);
      CHECK(near(r->style.width, 1.41) && r->style.color == "#0044cc" && r->style.glow && near(r->style.glow->size, 6) && near(r->style.glow->intensity, 0.5), "%s / %zu: style", cs.name, ti);
      std::vector<double> want, got;
      if (const auto* l = std::get_if<LineSpec>(&spec)) {
        want = mapPoints(transforms[ti], l->pts);
        const auto& g = std::get<LineSpec>(r->spec);
        got = g.pts;
        LineSpec a = g, b = *l;
        a.pts = b.pts = {};
        CHECK(lineSpecJson(a) == lineSpecJson(b), "%s / %zu: the rest of the spec changed\n %s\n %s", cs.name, ti, qPrintable(lineSpecJson(a)), qPrintable(lineSpecJson(b)));
      } else {
        const auto& v = std::get<VennSpec>(spec);
        want = mapPoints(transforms[ti], v.pts);
        const auto& g = std::get<VennSpec>(r->spec);
        got = g.pts;
        VennSpec a = g, b = v;
        a.pts = b.pts = {};
        CHECK(vennSpecJson(a) == vennSpecJson(b), "%s / %zu: the rest of the spec changed", cs.name, ti);
      }
      bool ok = want.size() == got.size();
      for (size_t i = 0; ok && i < want.size(); i++) ok = near(want[i], got[i], 1e-6);
      CHECK(ok, "%s / %zu: points not recovered", cs.name, ti);
    }
  }

  // a pure translation: rebuilding from what is read gives exactly the pieces that were moved
  for (const Case& cs : cases) {
    const MathSpec spec = sanitizeMath(cs.spec);
    Elements moved = buildMath("plRT", spec, style);
    transformAll(moved, rot(0, 1, 77.25, -33.5, false));
    const auto r = readMath(members(moved));
    if (!r) { CHECK(false, "%s: translation not read", cs.name); continue; }
    const Elements again = buildMath("plRT", r->spec, r->style);
    bool same = again.size() == moved.size();
    for (size_t i = 0; same && i < again.size(); i++) {
      const auto* a = std::get_if<Stroke>(&again[i]);
      const auto* b = std::get_if<Stroke>(&moved[i]);
      if (!a != !b) same = false;
      else if (a) {
        same = a->pts.size() == b->pts.size() && roleOf(again[i]) == roleOf(moved[i]);
        for (size_t k = 0; same && k < a->pts.size(); k++) same = near(a->pts[k], b->pts[k], 1e-6);
      } else same = std::get<Text>(again[i]).text == std::get<Text>(moved[i]).text;
    }
    CHECK(same, "%s: rebuilding after a translation gives the moved pieces", cs.name);
  }

  // moving an anchor and then reading: the spec follows
  {
    const MathSpec spec = defaultLine({100, 150}, {500, 150});
    const MathSpec moved = moveMathAnchor(spec, 1, {620, 150}, false);
    CHECK(near(std::get<LineSpec>(moved).pts[2], 620) && mathAnchors(spec).size() == 2, "moveMathAnchor on a line");
    const MathSpec v = moveMathAnchor(venn2(), 0, {50, 290}, false);
    CHECK(near(std::get<VennSpec>(v).pts[0], 50) && mathAnchors(v).size() == 6, "moveMathAnchor on a venn");
  }

  // an invertible shear is readable; foreign strokes are not math
  {
    Elements els = buildLine("pl", defaultLine(), blue());
    transformAll(els, Affine{1, 0, 1, 1, 0, 0});   // x' = x + y, y' = y
    CHECK(readMath(members(els)).has_value(), "an invertible map is readable");
    Elements plain;
    plain.push_back(Stroke{});
    CHECK(!isMath(members(plain)) && !readMath(members(plain)), "an ordinary stroke is not math");
  }
}

// ---- the stored JSON: key order and values, so Electron reads what we write and the other way round
static void testStoredJson() {
  const Elements e = buildLine("plJ", lineWith("(1,3]"), blue());
  const QString j = attrOr(withRole(e, "setwitness")[0]->attrs, "xlSet");
  CHECK(j.startsWith("{\"kind\":\"line\",\"spec\":{\"kind\":\"line\",\"pts\":[100,150,500,150],\"min\":-5,\"max\":5,\"step\":1,\"every\":1,\"ticks\":true,\"numbers\":true,\"arrows\":true,\"marks\":\"circles\",\"rows\":[{\"expr\":\"(1,3]\",\"label\":\"\"}]},\"w\":[100,150,500,150,"), "line JSON: %s", qPrintable(j));
  CHECK(j.endsWith("],\"width\":1.41}"), "line JSON end: %s", qPrintable(j));
  const Elements v = buildVenn("plJ", venn2(), blue());
  const QString jv = attrOr(withRole(v, "setwitness")[0]->attrs, "xlSet");
  CHECK(jv.startsWith("{\"kind\":\"venn\",\"spec\":{\"kind\":\"venn\",\"pts\":[60,300,520,560,"), "venn JSON: %s", qPrintable(jv));
  CHECK(jv.contains("\"k\":2,\"names\":[\"A\",\"B\",\"C\"],\"expr\":\"\",\"universe\":true,\"hatch\":\"light\",\"lists\":[\"\",\"\",\"\"]},\"w\":[60,300,520,300,60,560],\"width\":1.41}"), "venn JSON tail: %s", qPrintable(jv));
  // witness: invisible, three reference points
  const Stroke* w = withRole(v, "setwitness")[0];
  CHECK(w->pts.size() == 6 && attrOr(w->attrs, "color") == "#0044cc00", "venn witness");
  // a file written by the Electron app (witness with the same text) is read
  Elements fake;
  Stroke ws = *w;
  fake.push_back(ws);
  CHECK(isMath(members(fake)) && readMath(members(fake)).has_value(), "witness alone is enough to read");
  // text that is not JSON, a different kind, or missing fields: not math, never a crash
  for (const char* bad : {"", "{", "[]", "{\"kind\":\"plane\"}", "{\"kind\":\"line\"}", "{\"kind\":\"line\",\"spec\":{},\"w\":[1,2]}", "null", "{\"kind\":\"line\",\"spec\":{},\"w\":[0,0,1,1,2,2],\"width\":1}"}) {
    Stroke b = *w;
    attrSet(b.attrs, "xlSet", bad);
    Elements one{b};
    CHECK(!readMath(members(one)).has_value(), "malformed xlSet '%s' is not read", bad);
  }
}

// ---- save as .xopp and open again
static void testFile() {
  Document doc;
  Page page;
  page.attrs = {{"width", "595"}, {"height", "842"}};
  Layer layer;
  LineSpec ls = lineWith("(-2, 3] ∪ {5}");
  ls.rows[0].label = "x ∈ ℝ";
  for (Element& e : buildLine("plF1", ls, blue())) layer.elements.push_back(std::move(e));
  VennSpec vs = venn2();
  vs.expr = "A Δ B";
  vs.lists = {"1", "2", "ñ \"q\" \\"};
  for (Element& e : buildVenn("plF2", vs, blue())) layer.elements.push_back(std::move(e));
  page.layers.push_back(std::move(layer));
  doc.pages.push_back(std::move(page));
  const QByteArray xml = serializeXml(doc);
  CHECK(xml.contains("xlSet=\"") && xml.contains("xlRole=\"setwitness\""), "the attribute and role are in the file");
  const LoadResult back = parseXml(xml);
  CHECK(back.ok, "reload: %s", qPrintable(back.error));
  if (!back.ok) return;
  std::map<QString, std::vector<const Element*>> byId;
  for (const Element& e : back.doc.pages[0].layers[0].elements) if (auto id = planeIdOf(e)) byId[*id].push_back(&e);
  CHECK(byId.size() == 2, "two objects after reload");
  const auto l = readMath(byId["plF1"]);
  const auto v = readMath(byId["plF2"]);
  CHECK(l && std::holds_alternative<LineSpec>(l->spec) && v && std::holds_alternative<VennSpec>(v->spec), "both read back after reload");
  if (l && v) {
    CHECK(lineSpecJson(std::get<LineSpec>(l->spec)) == lineSpecJson(sanitizeLine(ls)), "line spec survives the file");
    CHECK(vennSpecJson(std::get<VennSpec>(v->spec)) == vennSpecJson(sanitizeVenn(vs)), "venn spec survives the file (quotes, backslash, accents)");
    CHECK(l->style.color == "#0044cc", "colour survives");
  }
  CHECK(back.doc.pages[0].layers[0].elements == doc.pages[0].layers[0].elements, "the pieces are identical after save and load");
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  testLines();
  testVenn();
  testRoundTrips();
  testStoredJson();
  testFile();
  std::printf("sets_objects: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
