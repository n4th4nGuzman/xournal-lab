#include "canvas.h"
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// --bench: what one input event costs (add the point + paint the dirty area) with 0, 2000 and 20000 strokes already on the page.
static int bench() {
  Canvas c;
  c.resize(1600, 1000);
  c.show();
  QApplication::processEvents();
  for (int total : {0, 2000, 20000}) {
    c.clearAll();
    QElapsedTimer t0;
    t0.start();
    if (total) c.addRandomStrokes(total);
    c.repaint();
    const double build = double(t0.nsecsElapsed()) / 1e6;
    std::vector<double> ms;
    c.liveBegin({300, 500, 0.5f});
    for (int i = 1; i <= 2000; i++) {
      QElapsedTimer t;
      t.start();
      const float x = 300.f + float(i) * 0.6f;
      c.liveAdd({x, float(500 + 120 * std::sin(i * 0.03)), 0.3f + 0.6f * std::abs(std::sin(float(i) * 0.01f))});
      c.repaint();  // forces the paint now so its cost is counted
      ms.push_back(double(t.nsecsElapsed()) / 1e6);
    }
    QElapsedTimer te;
    te.start();
    c.liveEnd();
    const double endMs = double(te.nsecsElapsed()) / 1e6;
    QElapsedTimer tu;
    tu.start();
    c.undo();
    const double undoMs = double(tu.nsecsElapsed()) / 1e6;
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double m : ms) sum += m;
    std::printf("strokes=%6d  build=%8.1f ms | per event: mean %.3f  p50 %.3f  p99 %.3f  max %.3f ms | liveEnd %.2f ms | undo(rebuild) %.1f ms\n",
                total, build, sum / double(ms.size()), ms[ms.size() / 2], ms[size_t(double(ms.size()) * 0.99)], ms.back(), endMs, undoMs);
    std::fflush(stdout);
  }
  return 0;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  for (int i = 1; i < argc; i++)
    if (QString(argv[i]) == "--bench") return bench();
  Canvas c;
  c.setWindowTitle("Xournal Lab C++ - prototipo del lienzo (S: 2000 trazos de prueba, Ctrl+Z, C limpiar, +/- grosor)");
  c.resize(1400, 900);
  c.show();
  return app.exec();
}
