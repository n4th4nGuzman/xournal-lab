// xl_qa_gen OUT.xopp : writes a document with one of everything the C++ app can generate (planes, every figure kind, number line, Venn, 3D axes,
// markdown, texts with awkward characters, images, teximage, glow, every special paper, named layers, pressure strokes). For the interchange check.
#include "docfactory.h"
#include "figures.h"
#include "genobj.h"
#include "mathobj.h"
#include "paper.h"
#include "plane.h"
#include "space3.h"
#include "xopp.h"
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>

static void add(Layer& l, std::vector<Element> es) { for (auto& e : es) l.elements.push_back(std::move(e)); }

static QString pngB64(int w, int h, QColor c) {
  QImage im(w, h, QImage::Format_ARGB32);
  im.fill(c);
  QPainter p(&im);
  p.setPen(Qt::white);
  p.drawLine(0, 0, w, h);
  p.end();
  QByteArray b;
  QBuffer buf(&b);
  buf.open(QIODevice::WriteOnly);
  im.save(&buf, "PNG");
  return QString::fromLatin1(b.toBase64());
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (argc < 2) return 2;
  Document d = blankDocument();
  GenStyle st;
  st.color = "#1a73e8";
  GenStyle glowSt = st;
  glowSt.glow = Glow{8, 0.6};

  // page 1: planes, number line, venn, 3D
  Layer& l1 = d.pages[0].layers[0];
  Frame fr{60, 60, 300, 260};
  add(l1, buildPlane("plA", DEFAULT_PLANE, fr, st));
  PlaneSpec ps = DEFAULT_PLANE; ps.sub = 4; ps.xlabel = "t"; ps.ylabel = "v(t)";
  add(l1, buildPlane("plB", ps, Frame{320, 60, 560, 260}, glowSt));
  add(l1, sets::buildMath("nl1", sets::defaultLine({60, 330}, {540, 330}), st));
  add(l1, sets::buildMath("vn2", sets::defaultVenn(2, {60, 380}, {300, 560}), st));
  add(l1, sets::buildMath("vn3", sets::defaultVenn(3, {320, 380}, {560, 560}), glowSt));
  add(l1, buildSpace3("sp1", placedSpec(DEFAULT_SPACE3, 1, {200, 700}), st));

  // page 2: every figure kind
  insertPage(d, 1, 0);
  Layer& l2 = d.pages[1].layers[0];
  int i = 0;
  for (const auto& it : figureCatalog()) {
    const double x = 40 + (i % 4) * 135, y = 40 + (i / 4) * 150;
    add(l2, buildFigure(QString("fg%1").arg(i), defaultSpec(it.kind, {x, y}, {x + 110, y + 110}), st));
    i++;
  }

  // page 3: texts, markdown, images, strokes, named layers
  insertPage(d, 2, 0);
  Page& p3 = d.pages[2];
  p3.layers[0].attrs.emplace_back("name", "Capa & <uno> \"q\"");
  Text t1;
  t1.attrs = {{"font", "Sans"}, {"size", "14"}, {"x", "50"}, {"y", "60"}, {"color", "#ff0000ff"}};
  t1.text = "Línea 1 áéíóú ñ ∑ ∫ < > & \" '\n  segunda línea con espacios\n\ttabulación";
  p3.layers[0].elements.emplace_back(t1);
  Text md = t1;
  md.attrs.emplace_back("xlMd", "1");
  md.attrs[3].second = "200";
  md.text = "# Título\n\n- uno\n- **dos** y *tres*\n> cita\n`code` y $x^2$\n";
  p3.layers[0].elements.emplace_back(md);
  Image im;
  im.tag = "image";
  im.attrs = {{"left", "50"}, {"top", "300"}, {"right", "150"}, {"bottom", "380"}};
  im.data = pngB64(40, 32, QColor("#336699"));
  p3.layers[0].elements.emplace_back(im);
  Image ti;
  ti.tag = "teximage";
  ti.attrs = {{"text", "\\frac{a}{b} & < > \""}, {"left", "200"}, {"top", "300"}, {"right", "300"}, {"bottom", "360"}};
  ti.data = pngB64(30, 20, QColor("#993366"));
  p3.layers[0].elements.emplace_back(ti);
  Stroke s;
  s.attrs = {{"tool", "pen"}, {"color", "#00aa00ff"}, {"capStyle", "round"}, {"glow", "6"}, {"glowAlpha", "0.5"}};
  s.widths = {1.41, 1.0, 2.25, 3.5};
  s.pts = {10, 10, 20, 15.5, 30.25, 22, 41.125, 30};
  p3.layers[0].elements.emplace_back(s);
  Stroke hl = s;
  hl.attrs = {{"tool", "highlighter"}, {"color", "#ffff0066"}, {"fill", "128"}};
  hl.widths = {12};
  hl.pts = {10, 100, 300, 100};
  p3.layers[0].elements.emplace_back(hl);
  Layer second;
  second.attrs.emplace_back("name", "Segunda");
  Stroke s2 = s;
  s2.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"style", "dash"}};
  s2.widths = {2};
  s2.pts = {0, 0, 1e-7, 123456789.5, -50.5, 3};
  second.elements.emplace_back(s2);
  p3.layers.push_back(second);
  p3.layers.push_back(Layer{});   // empty, unnamed

  // pages 4..: every special paper, then the built-in styles
  for (const PaperKind& k : paperKinds()) {
    insertPage(d, int(d.pages.size()), 0);
    Page& pg = d.pages.back();
    const auto spec = defaultSpec(k.id);
    pg.background = {{"type", "solid"}, {"color", "#ffffffff"}, {"style", k.fallback}, {"xlPaper", formatSpec(*spec)}};
  }
  for (const PaperStyle& ps2 : paperStyles()) {
    insertPage(d, int(d.pages.size()), 0);
    d.pages.back().background = {{"type", "solid"}, {"color", "#fff9c4ff"}, {"style", ps2.style}};
  }
  d.title = "QA título";
  QString err;
  if (!saveXopp(QString::fromLocal8Bit(argv[1]), d, &err)) { std::fprintf(stderr, "save failed: %s\n", qPrintable(err)); return 1; }
  const Counts c = countOf(d);
  std::printf("pages=%zu layers=%zu strokes=%zu texts=%zu images=%zu\n", c.pages, c.layers, c.strokes, c.texts, c.images);
  return 0;
}
