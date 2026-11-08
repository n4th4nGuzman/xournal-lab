// Helpers shared by the pen checks (penchecks.cpp, penaudit.cpp): event-loop pumping, image comparison.
#pragma once
#include "documentview.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <algorithm>
#include <cmath>

namespace penutil {

inline void pump(int n = 3) { for (int i = 0; i < n; i++) QApplication::processEvents(); }

/** Event loop until the view has nothing left to do (tiles, workers, patches), as the real one would while idle. */
inline void settle(DocumentView& v, int timeoutMs = 30000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!v.hasPendingTiles()) break;
    QThread::msleep(1);
  }
}

struct Diff { int maxd = 0; double mean = 0, ssim = 1; int over16 = 0; long pixels = 0; };

inline double lum(QRgb c) { return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c); }

inline Diff compare(QImage a, QImage b, QRect r) {
  a = a.convertToFormat(QImage::Format_RGB32);
  b = b.convertToFormat(QImage::Format_RGB32);
  r = r.intersected(a.rect()).intersected(b.rect());
  Diff d;
  double sum = 0;
  for (int y = r.top(); y <= r.bottom(); y++) {
    const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = r.left(); x <= r.right(); x++) {
      const int m = std::max({std::abs(qRed(pa[x]) - qRed(pb[x])), std::abs(qGreen(pa[x]) - qGreen(pb[x])), std::abs(qBlue(pa[x]) - qBlue(pb[x]))});
      d.maxd = std::max(d.maxd, m);
      if (m > 16) d.over16++;
      sum += m;
      d.pixels++;
    }
  }
  d.mean = d.pixels ? sum / double(d.pixels) : 0;
  // mean structural similarity of the luminance over 7x7 windows (stride 3)
  const double C1 = 6.5025, C2 = 58.5225;
  double total = 0;
  long windows = 0;
  for (int y = r.top(); y + 7 <= r.bottom() + 1; y += 3)
    for (int x = r.left(); x + 7 <= r.right() + 1; x += 3) {
      double ma = 0, mb = 0, saa = 0, sbb = 0, sab = 0;
      for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++) {
          const double la = lum(a.pixel(x + i, y + j)), lb = lum(b.pixel(x + i, y + j));
          ma += la; mb += lb; saa += la * la; sbb += lb * lb; sab += la * lb;
        }
      ma /= 49; mb /= 49;
      const double va = saa / 49 - ma * ma, vb = sbb / 49 - mb * mb, cov = sab / 49 - ma * mb;
      total += ((2 * ma * mb + C1) * (2 * cov + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2));
      windows++;
    }
  d.ssim = windows ? total / double(windows) : 1;
  return d;
}

inline QString fmtDiff(const Diff& d) { return QString("max %1, mean %2, pixels over 16: %3 of %4, SSIM %5").arg(d.maxd).arg(d.mean, 0, 'f', 4).arg(d.over16).arg(d.pixels).arg(d.ssim, 0, 'f', 5); }


}  // namespace penutil
