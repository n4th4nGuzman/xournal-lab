// Search over texts and LaTeX formulas.
#include "find.h"
#include <QGuiApplication>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)

static Text text(const QString& s, double x = 10, double y = 20) {
  Text t;
  t.attrs = {{"font", "Sans"}, {"size", "12"}, {"x", QString::number(x)}, {"y", QString::number(y)}, {"color", "#000000ff"}};
  t.text = s;
  return t;
}
static Image tex(const QString& latex, const QString& rot = QString()) {
  Image im;
  im.tag = "teximage";
  im.attrs = {{"left", "10"}, {"top", "20"}, {"right", "50"}, {"bottom", "40"}, {"text", latex}};
  if (!rot.isEmpty()) im.attrs.emplace_back("xlRot", rot);
  return im;
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);   // text boxes are measured with QFont
  Document d;
  d.pages.resize(2);
  d.pages[0].layers.resize(2);
  d.pages[1].layers.resize(1);
  Stroke s;
  s.pts = {0, 0, 1, 1};
  s.widths = {1};
  d.pages[0].layers[0].elements = {s, text("Hola mundo, hola"), tex("\\frac{HOLA}{2}")};
  d.pages[0].layers[1].elements = {text("nada aqui"), text("HoLa")};
  d.pages[1].layers[0].elements = {text("x hola"), Image{"image", {}, "AAAA"}};

  auto hits = findText(d, "hola");
  CHECK(hits.size() == 4);   // case-insensitive: text, formula, "HoLa"; "hola" twice in the first text is one hit
  CHECK(hits[0].page == 0 && hits[0].layer == 0 && hits[0].element == 1 && hits[0].positions == std::vector<int>({0, 12}) && hits[0].length == 4);
  CHECK(hits[1].element == 2 && hits[1].positions == std::vector<int>({6}));                       // inside the LaTeX source
  CHECK(hits[2].layer == 1 && hits[2].element == 1 && hits[3].page == 1);                         // page, layer, element order
  CHECK(findText(d, "hola", {true, nullptr}).size() == 2 && findText(d, "HOLA", {true, nullptr}).size() == 1 && findText(d, "HOLA", {true, nullptr})[0].element == 2);
  CHECK(findText(d, "").empty() && findText(d, "zzz").empty());
  CHECK(findText(d, "ola").size() == 4);
  CHECK(findText(d, "aaa").empty());   // a picture is not text
  FindOptions skip;
  skip.skipLayer = [](int p, int l) { return p == 0 && l == 1; };   // a hidden layer
  CHECK(findText(d, "hola", skip).size() == 3);
  CHECK(findText(d, "a m").size() == 1 && findText(d, "a m")[0].positions == std::vector<int>({3}));
  {   // non-overlapping occurrences
    Document e;
    e.pages.resize(1); e.pages[0].layers.resize(1);
    e.pages[0].layers[0].elements = {text("aaaa")};
    CHECK(findText(e, "aa")[0].positions == std::vector<int>({0, 2}));
  }

  // boxes to scroll to
  const auto box = findHitBox(d, hits[0]);
  CHECK(box && (*box)[0] == 10 && (*box)[1] == 20 && (*box)[2] > 10 && (*box)[3] > 20);
  const auto tb = findHitBox(d, hits[1]);
  CHECK(tb && *tb == std::array<double, 4>({10, 20, 50, 40}));
  d.pages[0].layers[0].elements[2] = tex("hola", "1.5707963267948966");   // turned 90 degrees: the box is the turned one
  const auto rb = findHitBox(d, hits[1]);
  CHECK(rb && std::abs((*rb)[2] - (*rb)[0] - 20) < 1e-9 && std::abs((*rb)[3] - (*rb)[1] - 40) < 1e-9);
  FindHit gone; gone.page = 7;
  CHECK(!findHitBox(d, gone));

  // stepping wraps around
  FindState st;
  CHECK(!st.step(1) && !st.current());
  st.run(d, "hola");
  CHECK(st.hits.size() == 4 && st.index == 0 && st.current() == &st.hits[0]);
  CHECK(st.step(-1) == &st.hits[3] && st.step(1) == &st.hits[0] && st.step(1) == &st.hits[1]);
  st.run(d, "zzz");
  CHECK(st.index == -1 && !st.current() && !st.step(1));

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("find: all passed\n");
  return 0;
}
