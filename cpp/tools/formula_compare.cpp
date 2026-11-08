// Compares the formula layout engine with the PDFs the Electron app (KaTeX + printToPDF) made for the same source:
//   xl_formula_compare CORPUS.json PDFDIR OUT_PREFIX [--inline] [--per N]
// CORPUS.json = [{"tex": "...", "cat": "...", "name": "..."}, ...]; PDFDIR holds N.pdf (index in the corpus) and meta.json written by the Electron side.
// Writes OUT_PREFIX-NN.png sheets (left: the Electron PDF drawn by pdfium, right: the C++ layout) and prints the size ratio of every pair, worst first.
#include "mathlayout.h"
#include "pdfraster.h"
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "windows");   // the real font database (offscreen has other fonts)
  QGuiApplication app(argc, argv);
  QStringList pos;
  bool display = true;
  int per = 14;
  for (int i = 1; i < argc; i++) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == "--inline") display = false;
    else if (a == "--per" && i + 1 < argc) per = QString::fromLocal8Bit(argv[++i]).toInt();
    else pos << a;
  }
  if (pos.size() < 3) { std::fprintf(stderr, "usage: xl_formula_compare CORPUS.json PDFDIR OUT_PREFIX [--inline] [--per N]\n"); return 2; }
  QFile f(pos[0]);
  if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot read corpus\n"); return 2; }
  const QJsonArray items = QJsonDocument::fromJson(f.readAll()).array();
  QFile mf(pos[1] + "/meta.json");
  QJsonArray meta;
  if (mf.open(QIODevice::ReadOnly)) meta = QJsonDocument::fromJson(mf.readAll()).array();

  struct Row { int i; QString tex; QImage ref, mine; double wr = 0, hr = 0; QString err; };
  std::vector<Row> rows;
  const double refPx = 3.0, myPx = 3.0;   // pixels per point on the sheet
  for (int i = 0; i < items.size(); i++) {
    Row r;
    r.i = i;
    r.tex = items[i].toObject().value("tex").toString();
    QFile pf(pos[1] + "/" + QString::number(i) + ".pdf");
    if (pf.open(QIODevice::ReadOnly)) {
      const QByteArray pdf = pf.readAll();
      const PdfInfo info = pdfInfo(pdf);
      if (info.ok) r.ref = rasterizePdfPage(pdf, 1, std::max(8, int(std::lround(info.widthPt * refPx))), Qt::white);
      const MathBox b = layoutMath(r.tex, 14 * 1.21, display);
      if (!b.ok) r.err = b.error;
      else if (!b.empty) {
        r.mine = renderMathImage(b, Qt::black, myPx);
        if (info.ok && info.widthPt > 0 && info.heightPt > 0) { r.wr = b.width / info.widthPt; r.hr = b.height() / info.heightPt; }
      }
    } else r.err = "no PDF from the Electron side (KaTeX rejected it?)";
    rows.push_back(std::move(r));
  }
  std::vector<const Row*> order;
  for (const Row& r : rows) order.push_back(&r);
  auto badness = [](const Row* r) { return r->err.isEmpty() ? std::abs(std::log(std::max(0.01, r->wr))) + std::abs(std::log(std::max(0.01, r->hr))) : 99.0; };
  std::sort(order.begin(), order.end(), [&](const Row* a, const Row* b) { return badness(a) > badness(b); });
  for (const Row* r : order) {
    if (r->err.isEmpty() && badness(r) < 0.1) break;
    std::printf("#%d w x%.2f h x%.2f %s %s\n", r->i, r->wr, r->hr, qPrintable(r->err.left(60)), qPrintable(r->tex.left(70)));
  }
  int sheet = 0;
  for (size_t at = 0; at < rows.size(); at += size_t(per)) {
    const size_t end = std::min(rows.size(), at + size_t(per));
    int W = 0, H = 6;
    for (size_t k = at; k < end; k++) {
      W = std::max(W, rows[k].ref.width() + rows[k].mine.width() + 120);
      H += std::max({rows[k].ref.height(), rows[k].mine.height(), 20}) + 10;
    }
    QImage img(std::max(W, 400), H, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    int y = 3;
    for (size_t k = at; k < end; k++) {
      const Row& r = rows[k];
      const int h = std::max({r.ref.height(), r.mine.height(), 20});
      p.setPen(QColor(200, 0, 0));
      p.setFont(QFont("Consolas", 8));
      p.drawText(2, y + 12, QString::number(r.i));
      const int x0 = 36, x1 = x0 + std::max(r.ref.width(), 60) + 40;
      if (!r.ref.isNull()) p.drawImage(x0, y, r.ref);
      p.setPen(QColor(190, 190, 255));
      p.drawLine(x1 - 20, y, x1 - 20, y + h);
      if (!r.mine.isNull()) p.drawImage(x1, y, r.mine);
      else { p.setPen(Qt::red); p.drawText(x1, y + 12, r.err.left(80)); }
      p.setPen(QColor(235, 235, 235));
      p.drawLine(0, y + h + 5, img.width(), y + h + 5);
      y += h + 10;
    }
    p.end();
    img.save(QString("%1-%2.png").arg(pos[2]).arg(sheet++, 2, 10, QChar('0')));
  }
  std::printf("%d formulas, %d sheets\n", int(rows.size()), sheet);
  return 0;
}
