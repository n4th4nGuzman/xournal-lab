// xl_xopptest FILE... : loads each file, prints what it holds, saves it to a temporary file and loads that again; both must be equal.
#include "xopp.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <cstdio>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  int bad = 0;
  for (int i = 1; i < argc; i++) {
    const QString path = QString::fromLocal8Bit(argv[i]);
    QElapsedTimer t;
    t.start();
    const LoadResult a = loadXopp(path);
    const double loadMs = double(t.nsecsElapsed()) / 1e6;
    if (!a.ok) { std::printf("FAIL %s: %s\n", qPrintable(path), qPrintable(a.error)); bad++; continue; }
    const Counts c = countOf(a.doc);
    std::printf("%s\n  paginas=%zu capas=%zu trazos=%zu textos=%zu imagenes=%zu puntos=%zu  carga=%.0f ms  omitidos=%d\n", qPrintable(path), c.pages, c.layers, c.strokes, c.texts, c.images, c.points, loadMs, int(a.skipped.size()));
    for (size_t p = 0; p < a.doc.pages.size(); p++) {
      const Page& pg = a.doc.pages[p];
      size_t n = 0;
      for (const Layer& l : pg.layers) n += l.elements.size();
      std::printf("  pag %zu: %gx%g fondo=%s capas=%zu elementos=%zu\n", p + 1, pg.width(), pg.height(), qPrintable(attrOr(pg.background, "type", "?")), pg.layers.size(), n);
    }
    const QString tmp = QDir::temp().filePath("xl_roundtrip.xopp");
    t.restart();
    QString err;
    if (!saveXopp(tmp, a.doc, &err)) { std::printf("  FAIL al guardar: %s\n", qPrintable(err)); bad++; continue; }
    const double saveMs = double(t.nsecsElapsed()) / 1e6;
    const LoadResult b = loadXopp(tmp);
    const bool same = b.ok && b.doc == a.doc;
    std::printf("  guardar=%.0f ms  ida y vuelta: %s\n", saveMs, same ? "IGUAL" : "DISTINTO");
    if (!same) bad++;
  }
  return bad ? 1 : 0;
}
