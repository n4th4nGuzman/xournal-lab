// Renders every page of every sample: not blank, clipping gives the same pixels inside the clip, and the time per page. Also checks the paper spec
// round trip and the width list convention.
#include "paper.h"
#include "pagerender.h"
#include "xopp.h"
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static QImage draw(const Page& page, double scale, const RenderOptions& base, const QImage* under = nullptr) {
  QImage img = under ? *under : QImage(int(std::ceil(page.width() * scale)), int(std::ceil(page.height() * scale)), QImage::Format_ARGB32_Premultiplied);
  if (!under) img.fill(Qt::transparent);
  QPainter p(&img);
  p.scale(scale, scale);
  RenderOptions o = base;
  o.scale = scale;
  renderPage(p, page, o);
  return img;
}

static bool blank(const QImage& img) {
  // blank = a single colour: a real page has at least background lines or ink
  const QRgb first = img.pixel(0, 0);
  for (int y = 0; y < img.height(); y += 2)
    for (int x = 0; x < img.width(); x += 2)
      if (img.pixel(x, y) != first) return false;
  return true;
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  // paper spec
  {
    const auto s = parseSpec("cart;quad=1;unit=2;bogus=3;sub=zzz");
    CHECK(s && s->params.at("quad") == "1" && s->params.at("unit") == "2" && s->params.at("sub") == "off", "parseSpec keeps valid, defaults invalid");
    CHECK(s && formatSpec(*s) == "cart;quad=1;unit=2;sub=off;labels=on", "formatSpec %s", s ? qPrintable(formatSpec(*s)) : "-");
    CHECK(!parseSpec("nonsense"), "unknown kind");
    CHECK(withParam(*s, "sub", "on").params.at("sub") == "on", "withParam");
    for (const PaperKind& k : paperKinds()) {
      Page pg;
      pg.attrs = {{"width", "595.27"}, {"height", "841.89"}};
      pg.background = {{"type", "solid"}, {"color", "#ffffffff"}, {"style", k.fallback}, {"xlPaper", formatSpec(*defaultSpec(k.id))}};
      const QImage img = draw(pg, 1.0, RenderOptions());
      CHECK(!blank(img), "paper %s is blank", qPrintable(k.id));
    }
  }
  // widths: first value is the base, then one per segment
  {
    Stroke s;
    s.widths = {2.0, 3.0, 4.0};
    CHECK(strokeWidthAt(s, 0) == 3.0 && strokeWidthAt(s, 1) == 4.0 && strokeWidthAt(s, 5) == 2.0, "strokeWidthAt");
    CHECK(colorOf("#ff000080").alpha() == 128 && colorOf("#00ff00").green() == 255, "colorOf");
  }

  const QDir dir("G:/xournal-lab/samples");
  const QStringList files = dir.entryList({"*.xopp"}, QDir::Files);
  CHECK(!files.isEmpty(), "no samples found");
  double total1 = 0, total2 = 0;
  int pages = 0;
  for (const QString& f : files) {
    const LoadResult r = loadXopp(dir.filePath(f));
    CHECK(r.ok, "%s: %s", qPrintable(f), qPrintable(r.error));
    if (!r.ok) continue;
    double ms1 = 0, ms2 = 0;
    for (size_t pi = 0; pi < r.doc.pages.size(); pi++) {
      const Page& page = r.doc.pages[pi];
      QElapsedTimer t;
      t.start();
      const QImage a = draw(page, 1.0, RenderOptions());
      ms1 += t.nsecsElapsed() / 1e6;
      t.restart();
      const QImage b = draw(page, 2.0, RenderOptions());
      ms2 += t.nsecsElapsed() / 1e6;
      pages++;
      CHECK(!blank(a), "%s page %zu blank at scale 1", qPrintable(f), pi);
      CHECK(!blank(b), "%s page %zu blank at scale 2", qPrintable(f), pi);

      // Clip: the same pixels inside the clip as the full render (elements outside are skipped, none inside may change).
      for (double scale : {1.0, 2.0}) {
        const QImage& full = scale == 1.0 ? a : b;
        const QRectF clip(page.width() * 0.2, page.height() * 0.3, page.width() * 0.5, page.height() * 0.3);
        // The compared area is the device rectangle strictly inside the clip (a fractional clip edge is a half covered pixel).
        const QRect dev = QRectF(clip.x() * scale, clip.y() * scale, clip.width() * scale, clip.height() * scale).toAlignedRect().adjusted(1, 1, -1, -1);
        RenderOptions o;
        o.clip = clip;
        QImage base(full.size(), full.format());
        base.fill(Qt::transparent);
        const QImage c = draw(page, scale, o, &base);
        int bad = 0;
        for (int y = dev.top(); y <= dev.bottom() && y < full.height(); y++)
          for (int x = dev.left(); x <= dev.right() && x < full.width(); x++)
            if (full.pixel(x, y) != c.pixel(x, y)) bad++;
        CHECK(bad == 0, "%s page %zu scale %g: %d pixels differ inside the clip", qPrintable(f), pi, scale, bad);
      }
    }
    total1 += ms1; total2 += ms2;
    std::printf("%-40s %2zu pages  %7.1f ms/page at 1x  %7.1f ms/page at 2x\n", qPrintable(f), r.doc.pages.size(),
                ms1 / std::max<size_t>(1, r.doc.pages.size()), ms2 / std::max<size_t>(1, r.doc.pages.size()));
  }
  if (pages) std::printf("all: %d pages, %.1f ms/page at 1x, %.1f ms/page at 2x (cold glow/image caches included)\n", pages, total1 / pages, total2 / pages);

  // warm: the second pass reuses the caches
  if (!files.isEmpty()) {
    const LoadResult r = loadXopp(dir.filePath(files.back()));
    QElapsedTimer t;
    t.start();
    for (const Page& pg : r.doc.pages) draw(pg, 1.0, RenderOptions());
    std::printf("warm run of %s at 1x: %.1f ms/page\n", qPrintable(files.back()), t.nsecsElapsed() / 1e6 / std::max<size_t>(1, r.doc.pages.size()));
  }
  std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
  return failures ? 1 : 0;
}
