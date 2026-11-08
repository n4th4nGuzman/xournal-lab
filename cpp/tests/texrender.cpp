// What a formula looks like on the canvas: the picture is typeset again from the TeX source in the PDF's ink colour, scaled to the box of the file, and
// the page renderer uses it (instead of the placeholder) for a teximage that has no readable bitmap. Also: markdown texts are drawn by the markdown module.
#include "mathlayout.h"
#include "pagerender.h"
#include "texrender.h"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static int inkPixels(const QImage& img, const QColor& c, int tol = 40) {
  int n = 0;
  for (int y = 0; y < img.height(); y++)
    for (int x = 0; x < img.width(); x++) {
      const QColor p = img.pixelColor(x, y);
      if (p.alpha() > 200 && std::abs(p.red() - c.red()) < tol && std::abs(p.green() - c.green()) < tol && std::abs(p.blue() - c.blue()) < tol) n++;
    }
  return n;
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  for (const QColor ink : {QColor("#000000"), QColor("#d32f2f"), QColor("#1565c0"), QColor("#ffee88")}) {
    const TexImageResult r = makeTexImage(QStringLiteral("\\frac{a+b}{c}=\\sqrt{2}"), 14, true, ink, 10, 20);
    CHECK(r.ok, "makeTexImage: %s", qPrintable(r.error));
    if (!r.ok) continue;
    const QColor got = texInkColor(r.el.data);
    CHECK(std::abs(got.red() - ink.red()) <= 1 && std::abs(got.green() - ink.green()) <= 1 && std::abs(got.blue() - ink.blue()) <= 1, "ink colour %s read back as %s", qPrintable(ink.name()), qPrintable(got.name()));
    const QImage bmp = texBitmap(r.el);
    CHECK(!bmp.isNull(), "bitmap for %s", qPrintable(ink.name()));
    if (bmp.isNull()) continue;
    CHECK(inkPixels(bmp, ink) > 150, "the bitmap is drawn in %s", qPrintable(ink.name()));
    CHECK(std::abs(double(bmp.width()) / bmp.height() - r.box.width / r.box.height()) < 0.03, "bitmap keeps the proportions of the box");
  }

  // display or inline: the layout whose proportions match the box is chosen
  for (bool display : {true, false}) {
    const TexImageResult r = makeTexImage(QStringLiteral("\\sum_{i=1}^{n} \\frac{1}{i}"), 14, display, QColor("#000000"), 0, 0);
    const QImage bmp = texBitmap(r.el);
    CHECK(!bmp.isNull() && std::abs(double(bmp.width()) / bmp.height() - r.box.width / r.box.height()) < 0.03, "display=%d proportions", display);
  }

  // anything that is not a formula with a source gives nothing
  Image plain;
  plain.tag = "image";
  CHECK(texBitmap(plain).isNull(), "a plain image is not a formula");
  Image nosrc;
  nosrc.tag = "teximage";
  nosrc.attrs = {{"left", "0"}, {"top", "0"}, {"right", "10"}, {"bottom", "10"}};
  CHECK(texBitmap(nosrc).isNull(), "a teximage without source gives no bitmap");
  Image bad = nosrc;
  bad.attrs.insert(bad.attrs.begin(), {"text", "\\frac{"});
  CHECK(texBitmap(bad).isNull(), "a broken source gives no bitmap (and no crash)");
  CHECK(texInkColor("not a pdf") == QColor(0, 0, 0), "an unreadable PDF means black ink");

  // the page renderer draws it where the box is, in the ink colour, and rotated with xlRot
  const TexImageResult f = makeTexImage(QStringLiteral("x^2+y^2"), 24, true, QColor("#1565c0"), 40, 50);
  Page page;
  page.attrs = {{"width", "200"}, {"height", "150"}};
  Layer layer;
  layer.elements.push_back(f.el);
  page.layers.push_back(layer);
  RenderOptions o;
  o.background = false;
  o.scale = 2;
  QImage canvas(400, 300, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  {
    QPainter p(&canvas);
    p.scale(2, 2);
    renderLayers(p, page, o);
  }
  CHECK(inkPixels(canvas, QColor("#1565c0")) > 60, "renderLayers draws the formula instead of the placeholder");
  int outside = 0;
  for (int y = 0; y < canvas.height(); y++)
    for (int x = 0; x < canvas.width(); x++)
      if (canvas.pixelColor(x, y).alpha() > 0 && (x < 2 * 40 - 3 || x > 2 * (40 + f.box.width) + 3 || y < 2 * 50 - 3 || y > 2 * (50 + f.box.height()) + 3)) outside++;
  CHECK(outside == 0, "nothing is drawn outside the formula's box (%d pixels)", outside);

  // a turned formula (xlRot, as the selection tool sets it) is drawn turned: its ink is taller than wide
  {
    Image turned = f.el;
    attrSet(turned.attrs, "xlRot", QString::number(M_PI / 2, 'g', 12));
    Page pg;
    pg.attrs = {{"width", "200"}, {"height", "150"}};
    Layer l;
    l.elements.push_back(turned);
    pg.layers.push_back(l);
    QImage img(200, 150, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    RenderOptions ro;
    ro.background = false;
    renderLayers(p, pg, ro);
    p.end();
    int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
    for (int y = 0; y < img.height(); y++) for (int x = 0; x < img.width(); x++) if (img.pixelColor(x, y).alpha() > 40) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    CHECK(x1 >= 0 && (y1 - y0) > (x1 - x0), "a formula turned 90 degrees is taller than wide (%d x %d)", x1 - x0, y1 - y0);
  }

  // markdown texts go through the markdown drawing: a heading is bigger than the same text without markup
  auto textPage = [](bool md) {
    Page pg;
    pg.attrs = {{"width", "300"}, {"height", "100"}};
    Layer l;
    Text t;
    t.attrs = {{"font", "Sans"}, {"size", "12"}, {"x", "10"}, {"y", "10"}, {"color", "#000000ff"}};
    if (md) t.attrs.emplace_back("xlMd", "1");
    t.text = QStringLiteral("# Titulo");
    l.elements.push_back(t);
    pg.layers.push_back(l);
    return pg;
  };
  auto bbox = [&](const Page& pg) {
    QImage img(300, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    RenderOptions ro;
    ro.background = false;
    renderLayers(p, pg, ro);
    p.end();
    int x1 = 0;
    for (int y = 0; y < img.height(); y++) for (int x = 0; x < img.width(); x++) if (img.pixelColor(x, y).alpha() > 0) x1 = std::max(x1, x);
    return x1;
  };
  CHECK(bbox(textPage(true)) > bbox(textPage(false)) + 8, "a markdown heading is drawn larger (and without its #)");

  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("texrender: ok\n");
  return 0;
}
