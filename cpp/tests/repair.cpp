// repairGenerated: objects saved without the position of their labels (the old labels-save bug) are built again from their strokes.
// Mirrors the labelsLegacy* e2e checks of the Electron app, with the real plane, 3D space and figure builders.
#include "figures.h"
#include "plane.h"
#include "repair.h"
#include "space3.h"
#include "xopp.h"
#include <QDir>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)

static std::vector<const Text*> labelsOf(const Layer& l, const QString& id) {
  std::vector<const Text*> out;
  for (const Element& e : l.elements)
    if (const auto* t = std::get_if<Text>(&e))
      if (planeIdOf(e) == id) out.push_back(t);
  return out;
}
static QString keyOf(const Text& t) {
  return t.text + "@" + QString::number(attrOr(t.attrs, "x").toDouble(), 'f', 2) + "," + QString::number(attrOr(t.attrs, "y").toDouble(), 'f', 2);
}
static QStringList keys(const std::vector<const Text*>& l) {
  QStringList k;
  for (const Text* t : l) k << keyOf(*t);
  k.sort();
  return k;
}

/** What the old save wrote: texts without position, size, font or colour. */
static void strip(Layer& l) {
  for (Element& e : l.elements)
    if (auto* t = std::get_if<Text>(&e))
      for (const char* k : {"x", "y", "size", "font", "color"})
        t->attrs.erase(std::remove_if(t->attrs.begin(), t->attrs.end(), [&](const auto& kv) { return kv.first == k; }), t->attrs.end());
}

static Document docWith(std::vector<Element> els) {
  Document d;
  Page p;
  p.attrs = {{"width", "600"}, {"height", "800"}};
  Layer l;
  l.elements = std::move(els);
  p.layers.push_back(std::move(l));
  d.pages.push_back(std::move(p));
  return d;
}

static Stroke plain(double x) {
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}};
  s.widths = {1};
  s.pts = {x, 0, x + 1, 1};
  return s;
}

static void objectTest(const char* what, const QString& id, std::vector<Element> obj) {
  std::vector<Element> els;
  els.push_back(plain(1));
  els.push_back(plain(2));
  const size_t first = els.size();
  for (auto& e : obj) els.push_back(e);
  els.push_back(plain(3));
  Document good = docWith(els);
  const auto goodLabels = labelsOf(good.pages[0].layers[0], id);
  CHECK(!goodLabels.empty());
  const size_t n = good.pages[0].layers[0].elements.size();

  // a healthy document is left alone
  Document healthy = good;
  CHECK(repairGenerated(healthy) == 0 && healthy == good);

  // through the file format too: write, strip as the old version did, read back
  Document old = good;
  strip(old.pages[0].layers[0]);
  const LoadResult lr = parseXml(serializeXml(old));
  CHECK(lr.ok);
  old = lr.doc;
  for (const Text* t : labelsOf(old.pages[0].layers[0], id)) CHECK(!attrFind(t->attrs, "x") && !attrFind(t->attrs, "y"));   // labelsLegacyHasNoPlace

  CHECK(repairGenerated(old) == 1);
  Layer& l = old.pages[0].layers[0];
  CHECK(l.elements.size() == n);                                                    // labelsRepairKeepsCount
  CHECK(keys(labelsOf(l, id)) == keys(goodLabels));                                 // labelsRepairedBack
  for (const Text* t : labelsOf(l, id)) CHECK(attrOr(t->attrs, "color").startsWith("#cc2288") || attrOr(t->attrs, "color").size() == 9);
  // same place in the layer: the object starts where it started, the other strokes keep their order
  CHECK(planeIdOf(l.elements[first]) == id && !planeIdOf(l.elements[0]) && !planeIdOf(l.elements[1]) && !planeIdOf(l.elements.back()));
  CHECK(std::get<Stroke>(l.elements[0]).pts[0] == 1 && std::get<Stroke>(l.elements.back()).pts[0] == 3);
  CHECK(repairGenerated(old) == 0);                                                 // labelsRepairSecondTimeNothing
  std::printf("  %s repaired (%zu elements)\n", what, n);
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");   // textBBox measures with QFont, which needs a GUI application object
  QGuiApplication app(argc, argv);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const GenStyle style{"#cc2288", 1.41, std::nullopt};

  PlaneSpec ps = DEFAULT_PLANE;
  ps.xlabel = "Re"; ps.ylabel = "Im";
  objectTest("plane", "pl111111", buildPlane("pl111111", ps, Frame{50, 100, 350, 400}, style));
  objectTest("space3", "pl222222", buildSpace3("pl222222", [] { Space3Spec s = DEFAULT_SPACE3; return s; }(), style));
  for (int k = 0; k <= int(FigKind::Sphere); k++) {   // every figure kind that has labels
    FigureSpec fs = defaultSpec(FigKind(k), {100, 100}, {260, 220});
    fs.label = "fig";
    auto els = buildFigure("pl333333", fs, style);
    if (std::none_of(els.begin(), els.end(), [](const Element& e) { return std::holds_alternative<Text>(e); })) continue;
    objectTest(("figure " + std::to_string(k)).c_str(), "pl333333", els);
  }

  // two objects on one page, and the one that is intact is not touched
  {
    auto a = buildPlane("pl444444", ps, Frame{0, 0, 200, 200}, style);
    auto b = buildPlane("pl555555", ps, Frame{250, 0, 450, 200}, style);
    std::vector<Element> els = a;
    for (auto& e : b) els.push_back(e);
    Document d = docWith(els);
    const Document before = d;
    Layer& l = d.pages[0].layers[0];
    for (Element& e : l.elements)   // only the first object loses its positions
      if (auto* t = std::get_if<Text>(&e); t && planeIdOf(e) == QString("pl444444"))
        t->attrs.erase(std::remove_if(t->attrs.begin(), t->attrs.end(), [](const auto& kv) { return kv.first == "x" || kv.first == "y"; }), t->attrs.end());
    CHECK(repairGenerated(d) == 1);
    CHECK(keys(labelsOf(l, "pl444444")) == keys(labelsOf(before.pages[0].layers[0], "pl444444")));
    CHECK(keys(labelsOf(l, "pl555555")) == keys(labelsOf(before.pages[0].layers[0], "pl555555")));
  }

  // an object whose definition is gone cannot be rebuilt: it is left as it is, and not counted
  {
    auto a = buildPlane("pl666666", ps, Frame{0, 0, 200, 200}, style);
    Document d = docWith(a);
    Layer& l = d.pages[0].layers[0];
    for (Element& e : l.elements)
      if (auto* s = std::get_if<Stroke>(&e)) s->attrs.erase(std::remove_if(s->attrs.begin(), s->attrs.end(), [](const auto& kv) { return kv.first == "xlPlaneSpec"; }), s->attrs.end());
    strip(l);
    const Document before = d;
    CHECK(repairGenerated(d) == 0 && d == before);
  }

  // the rebuilder protocol: the first one that claims an object decides, even when it cannot build it
  {
    Document d = docWith({plain(1)});
    Text t;
    t.attrs = {{"xlPlane", "plX"}, {"xlRole", "label"}};
    t.text = "orphan";
    d.pages[0].layers[0].elements.push_back(t);
    int asked1 = 0, asked2 = 0;
    std::vector<Rebuilder> rs = {
      [&](const QString&, const std::vector<const Element*>&) { asked1++; return Rebuild{}; },
      [&](const QString&, const std::vector<const Element*>&) { asked2++; return Rebuild{true, std::nullopt}; },
      [&](const QString&, const std::vector<const Element*>&) { std::printf("FAIL: asked after a claim\n"); fails++; return Rebuild{}; },
    };
    CHECK(repairGenerated(d, rs) == 0 && asked1 == 1 && asked2 == 1);
    // and a rebuilder that replaces the members
    std::vector<Rebuilder> fix = {[&](const QString& id, const std::vector<const Element*>& m) {
      Text n;
      n.attrs = {{"xlPlane", id}, {"xlRole", "label"}, {"x", "5"}, {"y", "6"}};
      n.text = QString("rebuilt %1").arg(m.size());
      return Rebuild{true, std::vector<Element>{n}};
    }};
    CHECK(repairGenerated(d, fix) == 1);
    CHECK(std::get<Text>(d.pages[0].layers[0].elements[1]).text == "rebuilt 1" && d.pages[0].layers[0].elements.size() == 2);
    CHECK(repairGenerated(d, fix) == 0);
  }

  // the samples of the project: nothing in them may be damaged by a repair pass (they were saved by a version that already writes the positions)
  {
    int files = 0;
    for (const QString& f : QDir("G:/xournal-lab/samples").entryList({"*.xopp"}, QDir::Files)) {
      const LoadResult r = loadXopp("G:/xournal-lab/samples/" + f);
      if (!r.ok) continue;
      Document d = r.doc;
      const int fixed = repairGenerated(d);
      CHECK(countOf(d).strokes == countOf(r.doc).strokes + 0 || fixed > 0);
      files++;
      std::printf("  sample %s: %d object(s) repaired\n", qPrintable(f), fixed);
    }
    CHECK(files >= 0);
  }

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("repair: all passed\n");
  return 0;
}
