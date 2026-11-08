// Draws a sheet of formulas to a PNG so the layout can be looked at: xl_render_formulas OUT.png [FORMULAS.txt] [--size N] [--inline] [--cols N]
// One formula per line in the text file (lines starting with # are skipped); see tools/formulas_sheet.txt for the sheet of tricky cases.
// `--palette CATEGORY` draws every button label of a palette category instead (id as in latexpalette.h, or "all").
#include "latexpalette.h"
#include "mathlayout.h"
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTextStream>
#include <cstdio>


static QString fitLabel(const QString& s) { return s.size() > 90 ? s.left(87) + "..." : s; }

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QString out, file, palette;
  double size = 20;
  bool display = true;
  int cols = 0;
  for (int i = 1; i < argc; i++) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == "--size" && i + 1 < argc) size = QString::fromLocal8Bit(argv[++i]).toDouble();
    else if (a == "--inline") display = false;
    else if (a == "--cols" && i + 1 < argc) cols = QString::fromLocal8Bit(argv[++i]).toInt();
    else if (a == "--palette" && i + 1 < argc) palette = QString::fromLocal8Bit(argv[++i]);
    else if (out.isEmpty()) out = a;
    else file = a;
  }
  if (out.isEmpty()) { std::fprintf(stderr, "usage: xl_render_formulas OUT.png [FORMULAS.txt] [--size N] [--inline] [--cols N] [--palette ID|all]\n"); return 2; }
  QStringList lines;
  if (!palette.isEmpty()) {
    for (const PaletteCategory& c : paletteCategories())
      if (palette == "all" || c.id == palette) for (const PaletteItem& it : c.items) lines << it.tex;
    if (!cols) cols = 8;
  } else if (!file.isEmpty()) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot read %s\n", qPrintable(file)); return 2; }
    for (const QString& l : QString::fromUtf8(f.readAll()).split('\n')) if (!l.trimmed().isEmpty() && !l.startsWith('#')) lines << l.trimmed();
  } else {
    std::fprintf(stderr, "give a formulas file (tools/formulas_sheet.txt) or --palette ID\n");
    return 2;
  }
  if (!cols) cols = 1;
  struct Cell { MathBox box; QString src; };
  std::vector<Cell> cells;
  int bad = 0;
  for (const QString& l : lines) {
    cells.push_back({layoutMath(l, size, display), l});
    if (!cells.back().box.ok) { bad++; std::fprintf(stderr, "ERROR %s -> %s\n", qPrintable(l), qPrintable(cells.back().box.error)); }
  }
  const double pad = 10, scale = 2.0;
  const bool labels = palette.isEmpty();
  // rows of `cols` cells; each row as tall as its tallest formula
  std::vector<double> colW(size_t(cols), 0.0);
  std::vector<double> rowH, rowA;
  for (size_t i = 0; i < cells.size(); i++) {
    const size_t r = i / size_t(cols), c = i % size_t(cols);
    if (rowH.size() <= r) { rowH.push_back(0); rowA.push_back(0); }
    colW[c] = std::max(colW[c], std::max(cells[i].box.width, 40.0));
    rowA[r] = std::max(rowA[r], std::max(cells[i].box.ascent, 14.0));
    rowH[r] = std::max(rowH[r], std::max(cells[i].box.descent, 8.0));
  }
  for (size_t r = 0; r < rowH.size(); r++) rowH[r] += rowA[r];
  double W = pad, H = pad;
  for (double w : colW) W += w + pad;
  for (size_t r = 0; r < rowH.size(); r++) H += rowH[r] + pad + (labels ? 11 : 0);
  QImage img(int(W * scale), int(H * scale), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter p(&img);
  p.scale(scale, scale);
  p.setRenderHint(QPainter::Antialiasing);
  double y = pad;
  for (size_t r = 0; r < rowH.size(); r++) {
    double x = pad;
    for (int c = 0; c < cols; c++) {
      const size_t i = r * size_t(cols) + size_t(c);
      if (i < cells.size()) {
        const Cell& cell = cells[i];
        if (cell.box.ok) {
          drawMath(p, cell.box, QPointF(x, y + rowA[r]), Qt::black);
          if (std::getenv("XL_BOXES")) { p.setPen(QColor(255, 0, 0, 90)); p.drawRect(QRectF(x, y + rowA[r] - cell.box.ascent, cell.box.width, cell.box.height())); p.drawLine(QPointF(x, y + rowA[r]), QPointF(x + cell.box.width, y + rowA[r])); }
        } else {
          p.setPen(Qt::red);
          p.setFont(QFont("Segoe UI", 7));
          p.drawText(QPointF(x, y + rowA[r]), fitLabel(cell.box.error));
        }
        if (labels) {
          p.setPen(QColor(120, 120, 120));
          QFont f("Consolas");
          f.setPixelSize(7);
          p.setFont(f);
          p.drawText(QPointF(x, y + rowH[r] + 8), fitLabel(cell.src));
        }
      }
      x += colW[size_t(c)] + pad;
    }
    y += rowH[r] + pad + (labels ? 11 : 0);
  }
  p.end();
  if (!img.save(out)) { std::fprintf(stderr, "cannot write %s\n", qPrintable(out)); return 1; }
  std::printf("%d formulas, %d errors -> %s (%dx%d)\n", int(cells.size()), bad, qPrintable(out), img.width(), img.height());
  return bad ? 1 : 0;
}
