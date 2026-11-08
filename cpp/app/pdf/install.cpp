#include "modules.h"
#include "mainwindow.h"
#include "pagerender.h"
#include "pdfactions.h"
#include "pdfbg.h"
#include "pdfraster.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

// The "pdf" area (see ARCHITECTURE.md and API.md). PDF pages as page backgrounds (annotating a PDF), formulas saved as vector PDF, export to
// PDF and PNG, print.
namespace {

QAction* neighbour(QMenu* menu, QAction* a, int delta) {
  const QList<QAction*> all = menu->actions();
  const int i = int(all.indexOf(a)) + delta;
  return i >= 0 && i < all.size() ? all[i] : nullptr;
}

}  // namespace

void installPdf(MainWindow& win) {
  Editor& ed = win.editor();

  // Formulas saved as a vector PDF (<teximage>): drawn with pdfium at 4 px per point, true ink colour, cached by content in core/pagerender.
  if (pdfRasterAvailable()) setPdfImageDecoder(&rasterizePdfFormula);

  // PDF / picture page backgrounds: the provider answers renderBackground on any thread; pages are rasterised on a worker at the zoom's size.
  auto* bgs = new PdfBackgrounds(win);
  setBackgroundImageProvider([bgs](const Attrs& bg, double w, double h, double scale) { return bgs->provide(bg, w, h, scale); });
  QObject::connect(bgs, &PdfBackgrounds::problem, &win, [&win](const QString& m) { win.showMessage(m, 10000); });
  // a PDF that is nowhere: offer to locate it (Xournal++ does too). Not in automated runs, which must never block on a dialog.
  QObject::connect(bgs, &PdfBackgrounds::missing, &win, [&win](const QString& file) {
    if (!win.persistence()) return;
    QTimer::singleShot(0, &win, [&win, file] {
      // window-modal but not a nested event loop: nothing waits on it, so automated runs and a closing window are never blocked
      auto* box = new QMessageBox(QMessageBox::Question, "Xournal Lab", "No se encuentra el PDF «" + QFileInfo(file).fileName() + "» de este documento.", QMessageBox::NoButton, &win);
      box->setAttribute(Qt::WA_DeleteOnClose);
      QPushButton* find = box->addButton("Buscar el PDF…", QMessageBox::AcceptRole);
      box->addButton("Ahora no", QMessageBox::RejectRole);
      QObject::connect(box, &QDialog::finished, &win, [&win, file, box, find] { if (box->clickedButton() == find) pdfui::commandLocatePdf(win, file); });
      box->setWindowModality(Qt::WindowModal);
      box->open();
    });
  });

  QAction* openPdf = ed.addCommand("file.openPdf", "Anotar PDF…", QKeySequence(), [&win] { pdfui::commandOpenPdf(win); });
  ed.addCommand("file.exportPdf", "Exportar como PDF…", QKeySequence(), [&win] { pdfui::commandExportPdf(win); });
  QAction* exportAs = ed.addCommand("file.exportAs", "Exportar como imagen PNG…", QKeySequence(), [&win] { pdfui::commandExportAs(win); });
  QAction* print = ed.addCommand("file.print", "Imprimir…", QKeySequence::Print, [&win] { pdfui::commandPrint(win); });

  if (QMenu* file = win.menu("file")) {
    // "Anotar PDF…" goes with the open commands (before the separator that precedes Guardar); the other two follow "Exportar como PDF…"
    QAction* save = ed.action("file.save");
    QAction* sep = neighbour(file, save, -1);
    file->insertAction(sep && sep->isSeparator() ? sep : save, openPdf);
    QAction* after = neighbour(file, ed.action("file.exportPdf"), +1);
    file->insertAction(after, exportAs);
    file->insertAction(after, print);
  }
}
