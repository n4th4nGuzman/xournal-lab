// Planes, 3D spaces and figures: counts of generated pieces, stored attributes, build -> save -> load -> read round trips, reading after an affine
// transform of every piece, anchors, and sanitising garbage. Mirrors the e2e checks of the Electron app (planeHasAllParts, s3Created, figBoxEdges, ...).
#include "figures.h"
#include "plane.h"
#include "space3.h"
#include "xopp.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

static size_t countRole(const std::vector<Element>& els, const char* role) {
  size_t n = 0;
  for (const Element& e : els) if (roleOf(e) == QString(role)) n++;
  return n;
}
static std::vector<const Stroke*> strokesOf(const std::vector<Element>& els, const char* role) {
  std::vector<const Stroke*> out;
  for (const Element& e : els)
    if (const auto* s = std::get_if<Stroke>(&e)) if (roleOf(e) == QString(role)) out.push_back(s);
  return out;
}
static Members membersOf(const std::vector<Element>& els) {
  Members m;
  for (const Element& e : els) m.push_back(&e);
  return m;
}

/** Save and load the pieces through the .xopp XML, like a real file. */
static std::vector<Element> roundTrip(const std::vector<Element>& els, const QString& id) {
  Document doc;
  Page pg;
  Layer ly;
  ly.elements = els;
  pg.layers.push_back(ly);
  doc.pages.push_back(pg);
  const LoadResult r = parseXml(serializeXml(doc));
  if (!r.ok || r.doc.pages.empty() || r.doc.pages[0].layers.empty()) { std::printf("round trip failed to load: %s\n", qPrintable(r.error)); failures++; return {}; }
  const Layer& l = r.doc.pages[0].layers[0];
  std::vector<Element> out;
  for (size_t i : planeMemberIndices(l, id)) out.push_back(l.elements[i]);
  return out;
}

/** What moving, turning, flipping or scaling the selection does: the map applied to every stroke (the witness included). */
static void transform(std::vector<Element>& els, const Affine& t) {
  for (Element& e : els)
    if (auto* s = std::get_if<Stroke>(&e)) s->pts = mapPoints(t, s->pts);
}

static const PlaneStyle STYLE{"#cc3300", 2.26, Glow{6, 0.8}};

static void testPlane() {
  const QString id = "plTEST";
  const Frame f{100, 100, 400, 400};
  const auto els = buildPlane(id, DEFAULT_PLANE, f, STYLE);
  CHECK(els.size() >= 24);
  CHECK(countRole(els, "axisx") == 1 && countRole(els, "axisy") == 1 && countRole(els, "grid") == 2);
  CHECK(countRole(els, "label") == 22);                       // -5..5 without 0 on each axis (20), plus the two names
  CHECK(countRole(els, "ticks") == 2 && countRole(els, "arrow") == 2 && countRole(els, "subgrid") == 0);
  CHECK(els.size() == 30);
  for (const Element& e : els) CHECK(planeIdOf(e) == id);

  const auto ax = strokesOf(els, "axisx")[0], ay = strokesOf(els, "axisy")[0];
  CHECK(ax->pts.size() == 4 && ay->pts.size() == 4);
  CHECK(near(ax->pts[1], 250) && near(ay->pts[0], 250) && near(ax->pts[0], 100) && near(ax->pts[2], 400));   // axes cross at 0 in the middle
  CHECK(attrOr(ax->attrs, "color") == "#cc3300ff" && near(ax->widths[0], 2.26));
  CHECK(attrOr(strokesOf(els, "grid")[0]->attrs, "color") == "#cc330066" && near(strokesOf(els, "grid")[0]->widths[0], 2.26 * 0.5));
  for (const Stroke* t : strokesOf(els, "ticks")) CHECK(near(t->widths[0], 2.26 * 0.6));
  for (const Stroke* a : strokesOf(els, "arrow")) {
    const auto b = genjs::bboxOf(a->pts);
    CHECK(a->widths[0] < 2.26 && std::max(b[2] - b[0], b[3] - b[1]) <= 8);
  }
  CHECK(attrOr(ax->attrs, "glow") == "6" && attrOr(ax->attrs, "glowAlpha") == "0.8");

  bool has[5] = {false, false, false, false, false};
  const QString minus5 = QString(QChar(0x2212)) + "5";
  for (const Element& e : els)
    if (const auto* t = std::get_if<Text>(&e)) {
      has[0] |= t->text == minus5; has[1] |= t->text == "5"; has[2] |= t->text == "0"; has[3] |= t->text == "x"; has[4] |= t->text == "y";
    }
  CHECK(has[0] && has[1] && !has[2] && has[3] && has[4]);

  const Members m = membersOf(els);
  const auto spec = planeSpecOf(m);
  CHECK(spec && spec->xmax == 5 && spec->xstep == 1);
  CHECK(decodeSpec(attrOr(ax->attrs, "xlPlaneSpec"))->xmax == 5);
  const auto fr = planeFrameOf(m);
  CHECK(fr && near(fr->x0, 100) && near(fr->y0, 100) && near(fr->x1, 400) && near(fr->y1, 400));
  const auto st = planeStyleOf(m);
  CHECK(st && st->color == "#cc3300" && near(st->width, 2.26) && st->glow && near(st->glow->size, 6));
  CHECK(attrOr(ax->attrs, "xlPlaneSpec").startsWith("{\"xmin\":-5,\"xmax\":5,\"ymin\":-5,\"ymax\":5,\"xstep\":1,\"ystep\":1,\"grid\":1,\"sub\":0"));

  // save and load: the same pieces, the same settings
  const auto back = roundTrip(els, id);
  CHECK(back.size() == els.size());
  const Members bm = membersOf(back);
  const auto spec2 = planeSpecOf(bm);
  CHECK(spec2 && encodeSpec(*spec2) == encodeSpec(*spec));
  const auto fr2 = planeFrameOf(bm);
  CHECK(fr2 && near(fr2->x0, 100, 1e-3) && near(fr2->x1, 400, 1e-3));

  // move + scale + rotate (quarter turn) of every piece: the frame read from the axes is the bounding box of the new axes
  auto moved = els;
  transform(moved, Affine{1, 0, 0, 1, 50, 30});
  const auto frm = planeFrameOf(membersOf(moved));
  CHECK(frm && near(frm->x0, 150) && near(frm->y0, 130) && near(frm->x1, 450) && near(frm->y1, 430));
  auto scaled = els;
  transform(scaled, Affine{2, 0, 0, 1, 0, 0});
  const auto frs = planeFrameOf(membersOf(scaled));
  CHECK(frs && near(frs->x0, 200) && near(frs->x1, 800) && near(frs->y1, 400));
  CHECK(planeSpecOf(membersOf(scaled)) && encodeSpec(*planeSpecOf(membersOf(scaled))) == encodeSpec(*spec));

  // only axes, subgrid, other range, axes on the edge
  PlaneSpec p = *spec;
  p.grid = false;
  CHECK(buildPlane(id, p, f, STYLE).size() == els.size() - 2 && countRole(buildPlane(id, p, f, STYLE), "grid") == 0);
  p = *spec; p.sub = 5;
  CHECK(countRole(buildPlane(id, p, f, STYLE), "subgrid") == 2);
  p = *spec; p.xmin = -10; p.xmax = 10; p.xstep = 2;
  const auto big = buildPlane(id, p, f, STYLE);
  bool m10 = false, m65 = false;
  for (const Element& e : big) if (const auto* t = std::get_if<Text>(&e)) { m10 |= t->text == QString(QChar(0x2212)) + "10"; m65 |= t->text == QString(QChar(0x2212)) + "6.5"; }
  CHECK(m10 && !m65);
  p = *spec; p.xmin = 2; p.xmax = 8; p.ymin = 10; p.ymax = 14;
  const auto edge = buildPlane(id, p, f, STYLE);
  CHECK(near(strokesOf(edge, "axisx")[0]->pts[1], 400) && near(strokesOf(edge, "axisy")[0]->pts[0], 100));   // y = ymin is the bottom edge, x = xmin the left
  p = *spec; p.xlabel.clear(); p.ylabel.clear(); p.numbers = false; p.ticks = false; p.arrows = false;
  CHECK(buildPlane(id, p, f, STYLE).size() == 4);

  // square frame: fits and centres
  PlaneSpec ps = *spec; ps.xmin = -10; ps.xmax = 10; ps.xstep = 2;
  const Frame sq = squareFrame(ps, {100, 100, 400, 400});
  CHECK(near(sq.x1 - sq.x0, 300) && near(sq.y1 - sq.y0, 150) && near(sq.y0, 175));

  // sanitise garbage
  PlaneSpec g;
  g.xmin = std::nan(""); g.xmax = INFINITY; g.xstep = 0; g.ystep = -3; g.sub = 3; g.xlabel = "abcdefghijklmnopqrstuvwxyz";
  const PlaneSpec sg = sanitizePlane(g);
  CHECK(sg.xstep > 1e-6 && sg.xmax > sg.xmin);   // a zero step is clamped to 1e-6, then widened until there are at most 120 lines
  CHECK((sg.xmax - sg.xmin) / sg.xstep <= 120 && sg.sub == 0 && sg.xlabel.size() == 12 && sg.ystep > 0);
  PlaneSpec r;
  r.xmin = 3.3; r.xmax = 3.3; r.xstep = 0.5;
  const PlaneSpec sr = sanitizePlane(r);
  CHECK(near(sr.xmin, 3.0) && near(sr.xmax, 3.5));
  PlaneSpec many;
  many.xmin = 0; many.xmax = 1000; many.sub = 10;
  const PlaneSpec sm = sanitizePlane(many);
  CHECK((sm.xmax - sm.xmin) / sm.xstep <= 120 && ((sm.xmax - sm.xmin) / sm.xstep + (sm.ymax - sm.ymin) / sm.ystep) * sm.sub <= 600);
  CHECK(!decodeSpec("not json") && !decodeSpec("") && decodeSpec("{\"xmax\":\"zz\"}")->grid == true && decodeSpec("{\"xmax\":\"zz\"}")->xmax == 5);
  CHECK(decodeSpec("{\"xmax\":7,\"grid\":0}") && !decodeSpec("{\"xmax\":7,\"grid\":0}")->grid && decodeSpec("{\"xmax\":7,\"grid\":0}")->xmax == 7);
}

static const PlaneStyle STYLE3{"#0044cc", 1.41, std::nullopt};

static void testSpace3() {
  const QString id = "plS3";
  Space3Spec base;
  base.o = {450, 250};
  const auto els = buildSpace3(id, base, STYLE3);
  CHECK(countRole(els, "axis3x") == 1 && countRole(els, "axis3y") == 1 && countRole(els, "axis3z") == 1);
  CHECK(countRole(els, "grid3") == 2 && countRole(els, "ticks") == 3 && countRole(els, "arrow") == 3 && countRole(els, "label") >= 21);
  CHECK(countRole(els, "label") == 21);
  const Members m = membersOf(els);
  CHECK(isSpace3(m));

  const auto sp = *space3SpecOf(m);
  CHECK(near(sp.o[0], 450, 1e-6) && near(sp.o[1], 250, 1e-6));
  const auto az = strokesOf(els, "axis3z")[0];
  {
    const V2 p0 = project(sp, 0, 0, sp.zmin), pc = project(sp, 0, 0, 0), p1 = project(sp, 0, 0, sp.zmax);
    CHECK(az->pts.size() == 6 && near(az->pts[0], p0[0]) && near(az->pts[1], p0[1]) && near(az->pts[2], pc[0]) && near(az->pts[3], pc[1]) && near(az->pts[4], p1[0]) && near(az->pts[5], p1[1]));
  }
  CHECK(near(vectorAngle(sp.u[0]).deg, 215, 0.01) && near(vectorAngle(sp.u[1]).deg, 0, 0.01) && near(vectorAngle(sp.u[2]).deg, 90, 0.01));
  for (int i = 0; i < 3; i++) CHECK(near(sp.n[i][0], sp.u[i][0], 1e-9) && near(sp.n[i][1], sp.u[i][1], 1e-9));
  CHECK(attrOr(strokesOf(els, "axis3x")[0]->attrs, "color") == "#0044ccff" && near(strokesOf(els, "axis3x")[0]->widths[0], 1.41));
  CHECK(attrFind(strokesOf(els, "axis3x")[0]->attrs, "xlSpace3") && attrFind(strokesOf(els, "axis3x")[0]->attrs, "xlPlaneW"));
  CHECK(!attrFind(az->attrs, "xlSpace3"));
  const auto style = space3StyleOf(m);
  CHECK(style && style->color == "#0044cc" && near(style->width, 1.41));

  // six anchors, each at the end of its half
  const auto anchors = anchorsOf(sp);
  CHECK(anchors.size() == 6);
  for (const auto& a : anchors) {
    double c[3] = {0, 0, 0};
    c[a.axis] = a.value;
    const V2 q = project(sp, c[0], c[1], c[2]);
    CHECK(near(a.at[0], q[0]) && near(a.at[1], q[1]));
  }

  // dragging the end of the +y half to 45 degrees and a third of the drag's length
  const auto yPlus = *std::find_if(anchors.begin(), anchors.end(), [](const Space3Anchor& a) { return a.axis == 1 && a.side == AnchorSide::Plus; });
  const V2 target{yPlus.at[0] + 100 - (yPlus.at[0] - sp.o[0]) + 0, sp.o[1] - 100};   // 100 right of the origin, 100 up
  const Space3Spec turned = withAnchor(sp, 1, AnchorSide::Plus, {sp.o[0] + 100, sp.o[1] - 100}, false);
  (void)target;
  const Angle vy = vectorAngle(turned.u[1]);
  CHECK(near(vy.deg, 45, 0.8) && near(vy.len, std::hypot(100, 100) / 3, 1) && near(turned.o[0], sp.o[0]) && near(turned.o[1], sp.o[1]));
  CHECK(near(turned.u[0][0], sp.u[0][0], 1e-9) && near(turned.u[2][1], sp.u[2][1], 1e-9));
  CHECK(near(turned.n[1][0], sp.n[1][0], 1e-9) && near(turned.n[1][1], sp.n[1][1], 1e-9));   // the negative half stays
  {   // the half's end follows the pointer exactly
    const V2 end = project(turned, 0, turned.ymax, 0);
    CHECK(near(end[0], sp.o[0] + 100) && near(end[1], sp.o[1] - 100));
  }
  // shift: only the angle follows
  const Space3Spec kept = withAnchor(sp, 1, AnchorSide::Plus, {sp.o[0] + 10, sp.o[1] - 80}, true);
  CHECK(near(vectorAngle(kept.u[1]).len, vectorAngle(sp.u[1]).len, 1e-6) && vectorAngle(kept.u[1]).deg > 70 && vectorAngle(kept.u[1]).deg < 90);
  // a half shorter than two points per unit is refused
  CHECK(withAnchor(sp, 1, AnchorSide::Plus, {sp.o[0] + 1, sp.o[1]}, false).u[1] == sp.u[1]);
  // the negative half moves alone
  const auto yMinus = *std::find_if(anchors.begin(), anchors.end(), [](const Space3Anchor& a) { return a.axis == 1 && a.side == AnchorSide::Minus; });
  const Space3Spec neg = withAnchor(sp, 1, AnchorSide::Minus, {yMinus.at[0] - 60, yMinus.at[1] + 50}, false);
  CHECK(near(project(neg, 0, neg.ymin, 0)[0], yMinus.at[0] - 60) && near(project(neg, 0, neg.ymin, 0)[1], yMinus.at[1] + 50));
  CHECK(near(neg.u[1][0], sp.u[1][0], 1e-9) && near(project(neg, 0, neg.ymax, 0)[0], yPlus.at[0]));

  // the stroke geometry reads back after the half-edit rebuild
  const auto els2 = buildSpace3(id, neg, STYLE3);
  const auto derived = *space3SpecOf(membersOf(els2));
  for (int i = 0; i < 3; i++) CHECK(near(derived.u[i][0], neg.u[i][0], 1e-6) && near(derived.n[i][1], neg.n[i][1], 1e-6));
  CHECK(near(derived.o[0], neg.o[0], 1e-6) && near(derived.o[1], neg.o[1], 1e-6));

  // an old space without `n` has straight axes; garbage ranges
  const auto legacy = *decodeSpace3("{\"u\":[[1,2],[3,4],[5,6]]}");
  for (int i = 0; i < 3; i++) CHECK(near(legacy.n[i][0], legacy.u[i][0]) && near(legacy.n[i][1], legacy.u[i][1]));
  Space3Spec half = sp;
  half.ymin = 0; half.zmax = 0;
  const auto ha = anchorsOf(half);
  CHECK(std::count_if(ha.begin(), ha.end(), [](const Space3Anchor& a) { return a.axis == 1; }) == 1 &&
        std::count_if(ha.begin(), ha.end(), [](const Space3Anchor& a) { return a.axis == 2 && a.side == AnchorSide::Plus; }) == 0 &&
        std::count_if(ha.begin(), ha.end(), [](const Space3Anchor& a) { return a.axis == 2 && a.side == AnchorSide::Minus; }) == 1);
  Space3Spec bad;
  bad.xmin = std::nan(""); bad.xmax = 1e9; bad.ystep = 0; bad.zmin = 5; bad.zmax = 5; bad.o = {INFINITY, 0}; bad.names[0] = "abcdefghijklmnopqrstuvwxyz";
  const Space3Spec sb = sanitizeSpace3(bad);
  CHECK(sb.xmin == -3 || sb.xmin <= 0);
  CHECK((sb.xmax - sb.xmin) / sb.xstep <= 40 && (sb.ymax - sb.ymin) / sb.ystep <= 40 && sb.zmax > sb.zmin && sb.o[0] == 0 && sb.names[0].size() == 12);
  CHECK(!decodeSpace3("garbage") && !decodeSpace3(""));
  CHECK(encodeSpace3(sp).startsWith("{\"xmin\":-3,\"xmax\":3,\"xstep\":1,\"ymin\":-3"));

  // extents and placing
  const auto ext = extentOf(sp);
  CHECK(ext[2] > ext[0] && ext[3] > ext[1]);
  const Space3Spec placed = placedSpec(Space3Spec(), 2, {150, 150});
  const auto pe = extentOf(placed);
  CHECK(near(pe[0], 150, 1e-9) && near(pe[1], 150, 1e-9));
  CHECK(near(vectorAngle(placed.u[1]).len, 68, 1e-9));

  // save + load, then every piece moved: the geometry and the settings are read back
  const auto back = roundTrip(els, id);
  CHECK(back.size() == els.size() && isSpace3(membersOf(back)));
  const auto re = *space3SpecOf(membersOf(back));
  CHECK(near(re.u[0][0], sp.u[0][0], 1e-4) && near(re.u[2][1], sp.u[2][1], 1e-4) && near(re.o[0], sp.o[0], 1e-3));
  auto moved = els;
  transform(moved, Affine{1, 0, 0, 1, 60, 40});
  const auto mv = *space3SpecOf(membersOf(moved));
  CHECK(near(mv.o[0], sp.o[0] + 60, 0.5) && near(mv.o[1], sp.o[1] + 40, 0.5) && near(mv.u[0][0], sp.u[0][0], 1e-6));
  auto grown = els;
  transform(grown, Affine{1.5, 0, 0, 1.5, 0, 0});
  const auto gr = *space3SpecOf(membersOf(grown));
  CHECK(near(std::hypot(gr.u[1][0], gr.u[1][1]), 1.5 * std::hypot(sp.u[1][0], sp.u[1][1]), 1e-6) && gr.xmax == sp.xmax);

  // other grids: xz and yz
  Space3Spec all = sp;
  all.gridXZ = true; all.gridYZ = true;
  CHECK(countRole(buildSpace3(id, all, STYLE3), "grid3") == 6);
  all.gridXY = all.gridXZ = all.gridYZ = false;
  CHECK(countRole(buildSpace3(id, all, STYLE3), "grid3") == 0);
}

static const PlaneStyle STYLEF{"#0044cc", 1.41, std::nullopt};

static void testFigures() {
  const QString id = "plFIG";
  auto make = [&](FigKind k, V2 a, V2 b, auto tweak) {
    FigureSpec s = defaultSpec(k, a, b);
    tweak(s);
    return buildFigure(id, s, STYLEF);
  };
  auto none = [](FigureSpec&) {};

  // a segment: one body, the ends where they were dragged, the witness invisible
  const auto seg = make(FigKind::Segment, {100, 100}, {300, 180}, none);
  CHECK(countRole(seg, "figbody") == 1 && strokesOf(seg, "figbody")[0]->pts.size() == 4 && countRole(seg, "figwitness") == 1);
  const auto fr = *figureSpecOf(membersOf(seg));
  CHECK(near(fr.spec.pts[0], 100, 1e-3) && near(fr.spec.pts[1], 100, 1e-3) && near(fr.spec.pts[2], 300, 1e-3) && near(fr.spec.pts[3], 180, 1e-3));
  CHECK(attrOr(strokesOf(seg, "figbody")[0]->attrs, "color") == "#0044ccff" && near(strokesOf(seg, "figbody")[0]->widths[0], 1.41));
  CHECK(attrOr(strokesOf(seg, "figwitness")[0]->attrs, "color") == "#0044cc00");
  CHECK(fr.style.color == "#0044cc" && near(fr.style.width, 1.41));

  // moving the anchors
  const auto moved = moveFigureAnchor(fr.spec, 1, {360, 90}, false);
  CHECK(near(moved.pts[2], 360) && near(moved.pts[3], 90) && near(moved.pts[0], 100));
  const auto snapped = moveFigureAnchor(fr.spec, 1, {300, 140}, true);   // 15 degree steps around the other end
  CHECK(near(std::fmod(std::atan2(snapped.pts[3] - 100, snapped.pts[2] - 100) / (M_PI / 12) + 100, 1.0), 0, 1e-9) || near(std::fmod(std::atan2(snapped.pts[3] - 100, snapped.pts[2] - 100) / (M_PI / 12), 1.0), 0, 1e-9));
  CHECK(near(std::hypot(snapped.pts[2] - 100, snapped.pts[3] - 100), std::hypot(200, 40), 1e-9));

  // flipped, rotated, scaled: the control points follow the strokes
  {
    auto els = seg;
    transform(els, Affine{-1, 0, 0, 1, 700, 0});   // flipH about the page's vertical axis at x = 350
    const auto r1 = *figureSpecOf(membersOf(els));
    const auto& b = strokesOf(els, "figbody")[0]->pts;
    CHECK(near(r1.spec.pts[0], b[0], 1e-6) && near(r1.spec.pts[1], b[1], 1e-6) && near(r1.spec.pts[2], b[2], 1e-6) && near(r1.spec.pts[3], b[3], 1e-6));
    transform(els, Affine{0, 1, -1, 0, 500, 0});   // a quarter turn
    const auto r2 = *figureSpecOf(membersOf(els));
    const auto& b2 = strokesOf(els, "figbody")[0]->pts;
    CHECK(near(r2.spec.pts[0], b2[0], 1e-6) && near(r2.spec.pts[1], b2[1], 1e-6) && near(r2.spec.pts[2], b2[2], 1e-6) && near(r2.spec.pts[3], b2[3], 1e-6));
    CHECK(near(std::hypot(r2.spec.pts[2] - r2.spec.pts[0], r2.spec.pts[3] - r2.spec.pts[1]), std::hypot(200, 80), 1e-6));
    // after a transform an anchor still edits it
    const auto edited = moveFigureAnchor(r2.spec, 1, {r2.spec.pts[2] + 25, r2.spec.pts[3] + 25}, false);
    const auto rebuilt = buildFigure(id, edited, r2.style);
    CHECK(near(strokesOf(rebuilt, "figbody")[0]->pts[2], r2.spec.pts[2] + 25, 1e-6));
  }

  // arrow: one head with the tip at the end; dashed line, solid head; both heads
  const auto arrow = make(FigKind::Arrow, {100, 100}, {300, 100}, [](FigureSpec& s) { s.dash = Dash::Dash; });
  const auto heads = strokesOf(arrow, "figarrow");
  CHECK(heads.size() == 1 && heads[0]->pts.size() == 6 && near(heads[0]->pts[2], 300) && near(heads[0]->pts[3], 100) && figureSpecOf(membersOf(arrow))->spec.heads == Heads::End);
  CHECK(attrOr(strokesOf(arrow, "figbody")[0]->attrs, "style") == "dash" && !attrFind(heads[0]->attrs, "style"));
  CHECK(countRole(make(FigKind::Segment, {100, 100}, {300, 100}, [](FigureSpec& s) { s.heads = Heads::Both; }), "figarrow") == 2);

  // polygon-ish kinds
  CHECK(strokesOf(make(FigKind::Regular, {200, 200}, {260, 200}, [](FigureSpec& s) { s.n = 6; }), "figbody")[0]->pts.size() == 14);   // 6 vertices and back to the first
  CHECK(strokesOf(make(FigKind::Polygon, {100, 100}, {200, 100}, none), "figbody")[0]->pts.size() == 8);
  CHECK(strokesOf(make(FigKind::Polyline, {100, 100}, {200, 100}, none), "figbody")[0]->pts.size() == 4);
  {   // a spline passes through its control points
    FigureSpec sp;
    sp.kind = FigKind::Spline;
    sp.pts = {100, 300, 150, 250, 220, 330, 300, 280};
    const auto els = buildFigure(id, sp, STYLEF);
    const auto& pts = strokesOf(els, "figbody")[0]->pts;
    bool all = true;
    for (size_t k = 0; k < sp.pts.size(); k += 2) {
      bool found = false;
      for (size_t i = 0; i + 1 < pts.size(); i += 2) if (std::hypot(pts[i] - sp.pts[k], pts[i + 1] - sp.pts[k + 1]) < 1e-3) found = true;
      all &= found;
    }
    CHECK(all);
  }
  {   // curves: first and last sample are the end points
    const auto b2 = strokesOf(make(FigKind::Bezier2, {100, 100}, {300, 100}, none), "figbody")[0]->pts;
    CHECK(near(b2[0], 100) && near(b2[b2.size() - 2], 300));
    const auto b3 = strokesOf(make(FigKind::Bezier3, {100, 100}, {300, 100}, none), "figbody")[0]->pts;
    CHECK(near(b3[0], 100) && near(b3[b3.size() - 2], 300) && b3.size() > 40);
  }
  {   // an arc starts at the first drag point and ends on the circle of radius 100
    const auto arc = strokesOf(make(FigKind::Arc, {200, 300}, {300, 300}, none), "figbody")[0]->pts;
    CHECK(near(arc[0], 300, 1e-3) && near(arc[1], 300, 1e-3) && near(std::hypot(arc[arc.size() - 2] - 200, arc[arc.size() - 1] - 300), 100, 1e-3) && arc.size() > 30);
  }
  {   // an angle has a mark; at 90 degrees the mark is a square
    const auto ang = make(FigKind::Angle, {200, 300}, {320, 300}, none);
    CHECK(countRole(ang, "figmark") == 1 && strokesOf(ang, "figbody")[0]->pts.size() == 6);
    FigureSpec right = defaultSpec(FigKind::Angle, {200, 300}, {320, 300});
    right.pts[4] = 200; right.pts[5] = 180;
    const auto el = buildFigure(id, right, STYLEF);
    const auto mk = strokesOf(el, "figmark")[0];
    CHECK(mk->pts.size() == 6 && near(std::hypot(mk->pts[2] - 200, mk->pts[3] - 300), M_SQRT2 * std::hypot(mk->pts[0] - 200, mk->pts[1] - 300), 1e-2));
  }
  {
    const auto dim = make(FigKind::Dim, {100, 300}, {300, 300}, none);
    CHECK(countRole(dim, "figarrow") == 2 && countRole(dim, "figmark") == 2);
  }

  // wireframe solids
  const auto box = make(FigKind::Box, {200, 400}, {200, 310}, none);
  {
    const auto hid = strokesOf(box, "fighidden");
    bool dashed = true;
    for (const Stroke* h : hid) dashed &= attrOr(h->attrs, "style") == "dash" && attrOr(h->attrs, "color") == "#0044cc99";
    CHECK(countRole(box, "figbody") == 9 && hid.size() == 3 && dashed && figureSpecOf(membersOf(box))->spec.pts.size() == 8);
  }
  {   // the origin carries the whole solid; a tip changes one edge
    const auto o = figureSpecOf(membersOf(box))->spec;
    const auto m = moveFigureAnchor(o, 0, {o.pts[0] + 40, o.pts[1] + 10}, false);
    CHECK(near(m.pts[0] - o.pts[0], 40) && near(m.pts[2] - o.pts[2], 40) && near(m.pts[7] - o.pts[7], 10));
    const auto t = moveFigureAnchor(o, 2, {o.pts[4] + 15, o.pts[5]}, false);
    CHECK(near(t.pts[4] - o.pts[4], 15) && near(t.pts[2], o.pts[2]) && near(t.pts[0], o.pts[0]));
    CHECK(moveFigureAnchor(o, 2, {o.pts[0] + 1, o.pts[1]}, false).pts == o.pts);   // too short: refused
  }
  CHECK(countRole(make(FigKind::Box, {200, 400}, {200, 310}, [](FigureSpec& s) { s.hidden = Hidden::None; }), "fighidden") == 0);
  CHECK(countRole(make(FigKind::Box, {200, 400}, {200, 310}, [](FigureSpec& s) { s.hidden = Hidden::None; }), "figbody") == 9);
  {
    const auto hs = make(FigKind::Box, {200, 400}, {200, 310}, [](FigureSpec& s) { s.hidden = Hidden::Solid; });
    CHECK(attrOr(strokesOf(hs, "fighidden")[0]->attrs, "color") == "#0044ccff" && !attrFind(strokesOf(hs, "fighidden")[0]->attrs, "style"));
  }
  const auto pyr = make(FigKind::Pyramid, {200, 400}, {200, 310}, none);
  CHECK(countRole(pyr, "figbody") + countRole(pyr, "fighidden") == 8 && countRole(pyr, "fighidden") >= 1);
  const auto cyl = make(FigKind::Cylinder, {200, 400}, {200, 310}, none);
  CHECK(countRole(cyl, "figbody") >= 4 && countRole(cyl, "fighidden") >= 1);
  const auto cone = make(FigKind::Cone, {200, 400}, {200, 310}, none);
  CHECK(countRole(cone, "figbody") >= 3 && countRole(cone, "fighidden") >= 1);
  const auto sph = make(FigKind::Sphere, {200, 400}, {200, 310}, [](FigureSpec& s) { s.n = 8; s.m = 4; });
  CHECK(countRole(sph, "figbody") >= 6 && countRole(sph, "fighidden") >= 3);
  {
    const Stroke* o = nullptr;
    for (const Stroke* s : strokesOf(sph, "figbody")) if (!o || s->pts.size() > o->pts.size()) o = s;
    CHECK(o && near(o->pts[0], o->pts[o->pts.size() - 2]) && near(o->pts[1], o->pts[o->pts.size() - 1]));
  }
  {
    const V2 sz = defaultSize(FigKind::Box);
    CHECK(sz[0] > 0 && sz[1] > 0 && defaultSize(FigKind::Segment) == V2({120, 80}));
  }

  // label, saving and loading, ordinary ink
  const auto lab = make(FigKind::Segment, {100, 100}, {300, 100}, [](FigureSpec& s) { s.label = "v"; });
  CHECK(countRole(lab, "figlabel") == 1);
  for (const Element& e : lab) if (const auto* t = std::get_if<Text>(&e)) if (roleOf(e) == QString("figlabel")) CHECK(t->text == "v");
  for (FigKind k : {FigKind::Box, FigKind::Cylinder, FigKind::Cone, FigKind::Sphere, FigKind::Pyramid, FigKind::Arc, FigKind::Angle, FigKind::Dim, FigKind::Regular, FigKind::Spline}) {
    const auto els = make(k, {200, 400}, {200, 310}, [](FigureSpec& s) { s.label = "A"; });
    const auto orig = figureSpecOf(membersOf(els))->spec;
    const auto back = roundTrip(els, id);
    const auto re = figureSpecOf(membersOf(back));
    bool same = back.size() == els.size() && re && re->spec.kind == k && re->spec.pts.size() == orig.pts.size();
    if (same) for (size_t i = 0; i < orig.pts.size(); i++) same &= near(re->spec.pts[i], orig.pts[i], 1e-2);
    CHECK(same);
    // moved and scaled as a whole (a similarity and an arbitrary affine map): every control point follows
    auto t = els;
    const Affine a{1.3, 0.2, -0.4, 0.9, 25, -12};
    transform(t, a);
    const auto r2 = figureSpecOf(membersOf(t));
    const auto expect = mapPoints(a, orig.pts);
    bool follows = r2.has_value();
    if (r2) for (size_t i = 0; i < expect.size(); i++) follows &= near(r2->spec.pts[i], expect[i], 1e-6);
    CHECK(follows);
  }
  {
    const auto els = make(FigKind::Box, {200, 400}, {200, 310}, none);
    const QByteArray xml = serializeXml([&] { Document d; Page p; Layer l; l.elements = els; p.layers.push_back(l); d.pages.push_back(p); return d; }());
    CHECK(xml.contains("xlRole=\"figbody\"") && xml.contains("tool=\"pen\"") && xml.contains("style=\"dash\"") && xml.contains("xlFig="));
  }
  CHECK(FigKind::Box != FigKind::Segment && figureCatalog().size() == 16 && figureCatalog()[5].clicks && figureCatalog()[5].kind == FigKind::Spline);
  CHECK(isFigure(membersOf(box)) && !isFigure(membersOf(buildPlane("x", DEFAULT_PLANE, {0, 0, 100, 100}, STYLE))));

  // the witness never degenerates for a horizontal segment (its third point is moved off the line)
  FigureSpec zero;
  zero.pts = {50, 50, 150, 50};
  const auto w = witnessOf(zero);
  CHECK(w.size() == 6 && affineFrom(w, w).has_value());

  // sanitise garbage
  {
    FigureSpec g;
    g.kind = FigKind::Polygon;
    g.pts = {1, 2, std::nan(""), 4, 5};
    g.n = 99; g.m = -4; g.label = QString(100, 'x');
    const FigureSpec s = sanitizeFigure(g);
    CHECK(s.pts.size() == 6 && s.pts[2] == 0 && s.n == 12 && s.m == 0 && s.label.size() == 40);
    CHECK(near(s.pts[4], s.pts[2]) && near(s.pts[5], s.pts[3] + 30 * 2));   // padded: the last point shifted by 30 per point already there
    FigureSpec many;
    many.kind = FigKind::Segment;
    many.pts.assign(100, 7);
    CHECK(sanitizeFigure(many).pts.size() == 4);
    FigureSpec arrowNone;
    arrowNone.kind = FigKind::Arrow;
    CHECK(sanitizeFigure(arrowNone).heads == Heads::End);
    const auto j = QJsonDocument::fromJson("{\"kind\":\"nope\",\"pts\":[\"a\",1,2],\"n\":\"x\",\"hidden\":\"zz\",\"dash\":\"dot\"}").object();
    const FigureSpec fj = figureFromJson(j);
    CHECK(fj.kind == FigKind::Segment && fj.pts.size() == 4 && fj.pts[0] == 0 && fj.pts[1] == 1 && fj.n == 4 && fj.hidden == Hidden::Dash && fj.dash == Dash::Dot);
    const FigureSpec sj = figureFromJson(QJsonDocument::fromJson("{\"kind\":\"sphere\"}").object());
    CHECK(sj.n == 8 && sj.m == 3 && sj.pts.size() == 8);
    CHECK(figureFromJson(QJsonDocument::fromJson(encodeFigure(s).toUtf8()).object()).pts == s.pts);
    CHECK(encodeFigure(defaultSpec(FigKind::Segment, {0, 0}, {10, 0})) == "{\"kind\":\"segment\",\"pts\":[0,0,10,0],\"heads\":\"none\",\"dash\":\"solid\",\"n\":4,\"m\":3,\"hidden\":\"dash\",\"label\":\"\"}");
  }
  CHECK(!figureSpecOf(membersOf(buildPlane("x", DEFAULT_PLANE, {0, 0, 100, 100}, STYLE))));
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);   // text sizes come from the font metrics
  testPlane();
  testSpace3();
  testFigures();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
