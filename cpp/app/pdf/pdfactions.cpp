#include "pdfactions.h"
#include "editor.h"
#include "mainwindow.h"
#include "pdfbg.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QCoreApplication>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#ifdef XL_HAVE_PRINT
#include <QPageLayout>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#endif

namespace pdfui {

namespace {
const QString kApp = QStringLiteral("Xournal Lab");

QString exportDir(MainWindow& win) {
  QSettings s("XournalLab", "XournalLabCpp");
  const QString saved = s.value("pdf/exportDir").toString();
  Editor& ed = win.editor();
  if (!ed.path().isEmpty()) return QFileInfo(ed.path()).absolutePath();
  const QString pdf = documentPdfFile(ed.doc());
  if (!pdf.isEmpty() && QFileInfo(pdf).exists()) return QFileInfo(pdf).absolutePath();
  if (!saved.isEmpty() && QFileInfo(saved).isDir()) return saved;
  return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString baseName(MainWindow& win) {
  Editor& ed = win.editor();
  if (!ed.path().isEmpty()) return QFileInfo(ed.path()).completeBaseName();
  const QString pdf = documentPdfFile(ed.doc());
  if (!pdf.isEmpty()) return QFileInfo(pdf).completeBaseName();
  return QStringLiteral("documento");
}

/** The proposed name of a PDF export: the template of Preferencias > Cargar/guardar. %{name} is the PDF being annotated (the document's own name when there is none), %{file} the saved file. */
QString exportPdfName(MainWindow& win) {
  Editor& ed = win.editor();
  const QString pdf = documentPdfFile(ed.doc());
  const QString pdfBase = pdf.isEmpty() ? QString() : QFileInfo(pdf).completeBaseName();
  const QString fileBase = ed.path().isEmpty() ? QString() : QFileInfo(ed.path()).completeBaseName();
  return expandNameTemplate(ed.xpp().pdfName, QDateTime::currentDateTime(), pdfBase.isEmpty() ? baseName(win) : pdfBase, fileBase);
}

/** Progress feedback that never freezes the window for long: one callback per page, the event loop runs in between. */
class Progress {
 public:
  Progress(MainWindow& win, const QString& label, bool show) {
    if (!show) return;
    dlg_ = std::make_unique<QProgressDialog>(label, "Cancelar", 0, 100, &win);
    dlg_->setWindowTitle(kApp);
    dlg_->setWindowModality(Qt::WindowModal);
    dlg_->setMinimumDuration(300);   // short exports never flash a dialog
    dlg_->setAutoClose(false);
    dlg_->setAutoReset(false);
  }
  ExportProgress callback() {
    return [this](int done, int total) {
      if (!dlg_) return true;
      dlg_->setMaximum(std::max(1, total));
      dlg_->setValue(done);
      dlg_->setLabelText(QString("Página %1 de %2…").arg(std::min(done + 1, total)).arg(total));
      QCoreApplication::processEvents();   // repaints the dialog and delivers the click on Cancelar
      return !dlg_->wasCanceled();
    };
  }
  ~Progress() { if (dlg_) dlg_->close(); }

 private:
  std::unique_ptr<QProgressDialog> dlg_;
};

void reportWarnings(MainWindow& win, const ExportResult& r, bool interactive) {
  if (r.warnings.isEmpty()) return;
  const QString text = "Algunas páginas se escribieron sin su fondo:\n\n" + r.warnings.mid(0, 8).join("\n") + (r.warnings.size() > 8 ? QString("\n… y %1 más").arg(r.warnings.size() - 8) : QString());
  if (interactive) QMessageBox::warning(&win, kApp, text);
  else win.showMessage(text.section('\n', 2, 2), 8000);
}

/** The questions common to PDF and PNG export. */
class ExportDialog : public QDialog {
 public:
  ExportDialog(QWidget* parent, bool png, int pages, int current) : QDialog(parent), pages_(pages) {
    setWindowTitle(png ? "Exportar como imagen PNG" : "Exportar como PDF");
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    lay->addLayout(form);
    all_ = new QRadioButton(QString("Todas las páginas (%1)").arg(pages));
    cur_ = new QRadioButton(QString("Solo la página actual (%1)").arg(current + 1));
    some_ = new QRadioButton("Estas páginas:");
    all_->setChecked(true);
    range_ = new QLineEdit;
    range_->setPlaceholderText("1,3-5");
    range_->setEnabled(false);
    connect(some_, &QRadioButton::toggled, range_, &QWidget::setEnabled);
    lay->insertWidget(0, all_);
    lay->insertWidget(1, cur_);
    lay->insertWidget(2, some_);
    lay->insertWidget(3, range_);
    bg_ = new QCheckBox("Incluir el fondo (papel, cuadrícula y páginas del PDF)");
    bg_->setChecked(true);
    lay->addWidget(bg_);
    dpi_ = new QComboBox;
    if (png) {
      for (int d : {72, 100, 150, 200, 300}) dpi_->addItem(QString("%1 ppp").arg(d), d);
      dpi_->setCurrentIndex(2);
      form->addRow("Resolución:", dpi_);
    } else {
      for (int d : {100, 150, 200, 300}) dpi_->addItem(QString("%1 ppp").arg(d), d);
      dpi_->setCurrentIndex(1);
      form->addRow("Resolución del fondo PDF:", dpi_);
      auto* note = new QLabel("Trazos, textos y fórmulas se escriben como vectores; las páginas de un PDF de fondo se escriben como imagen a esta resolución.");
      note->setWordWrap(true);
      lay->addWidget(note);
    }
    error_ = new QLabel;
    error_->setStyleSheet("color:#c0392b");
    lay->addWidget(error_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Continuar…");
    bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    connect(bb, &QDialogButtonBox::accepted, this, &ExportDialog::validate);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    current_ = current;
  }
  std::vector<int> pages;
  bool background() const { return bg_->isChecked(); }
  double dpi() const { return dpi_->currentData().toDouble(); }

 private:
  void validate() {
    QString err;
    if (all_->isChecked()) pages.clear();
    else if (cur_->isChecked()) pages = {current_};
    else {
      pages = parsePageRange(range_->text(), pages_, &err);
      if (pages.empty()) { error_->setText(err); return; }
    }
    accept();
  }
  int pages_, current_ = 0;
  QRadioButton *all_, *cur_, *some_;
  QLineEdit* range_;
  QCheckBox* bg_;
  QComboBox* dpi_;
  QLabel* error_;
};

}  // namespace

bool saveExportDialogShot(MainWindow& win, bool png, const QString& file) {
  ExportDialog dlg(&win, png, win.editor().pageCount(), win.editor().currentPage());
  dlg.show();
  QCoreApplication::processEvents();
  return dlg.grab().save(file);
}

PdfBackgrounds* backgrounds(MainWindow& win) { return win.findChild<PdfBackgrounds*>(); }

QStringList searchDirs(MainWindow& win) {
  if (PdfBackgrounds* b = backgrounds(win)) return b->searchDirs();
  return {};
}

bool openPdfFile(MainWindow& win, const QString& path, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "no se pudo abrir " + path; return false; }
  const QByteArray bytes = f.readAll();
  PdfDocument pdf;
  QString err;
  if (!pdf.open(bytes, &err)) { if (error) *error = err; return false; }
  Document doc = documentForPdf(pdf, path);
  for (Page& p : doc.pages)
    if (p.width() < 1 || p.height() < 1) { attrSet(p.attrs, "width", "595.27559"); attrSet(p.attrs, "height", "841.88976"); }   // a PDF page without a size: A4
  win.editor().setDocument(std::move(doc), QString());   // Save asks where to write the annotations; the PDF itself is never touched
  win.setDisplayName(QFileInfo(path).fileName());
  if (win.view()) { win.view()->zoomFitWidth(); win.view()->scrollToPage(0); }
  win.showMessage(QString("PDF abierto: %1 páginas. Las anotaciones se guardan en un .xopp que enlaza al PDF.").arg(pdf.pageCount()), 8000);
  return true;
}

ExportOptions exportOptions(MainWindow& win, bool background, double bgDpi, const std::vector<int>& pages) {
  ExportOptions o;
  o.background = background;
  o.bgDpi = bgDpi;
  o.pages = pages;
  Editor* ed = &win.editor();
  o.hiddenLayers = [ed](int p) { return ed->hiddenLayers(p); };
  return o;
}

ExportResult exportPdfTo(MainWindow& win, const QString& path, const ExportOptions& o, bool withProgress) {
  BackgroundResolver res;
  res.setSearchDirs(searchDirs(win));
  Progress prog(win, "Exportando PDF…", withProgress);
  return exportPdf(win.editor().doc(), path, o, res, prog.callback());
}

ExportResult exportPngTo(MainWindow& win, const QString& path, double dpi, const ExportOptions& o, bool withProgress) {
  BackgroundResolver res;
  res.setSearchDirs(searchDirs(win));
  Progress prog(win, "Exportando imágenes…", withProgress);
  return exportPng(win.editor().doc(), path, dpi, o, res, prog.callback());
}

#ifdef XL_HAVE_PRINT
ExportResult printTo(MainWindow& win, QPrinter& printer, const ExportOptions& o, bool withProgress) {
  ExportResult r;
  const Document& doc = win.editor().doc();
  std::vector<int> pages = o.pages;
  if (pages.empty()) for (int i = 0; i < int(doc.pages.size()); i++) pages.push_back(i);
  BackgroundResolver res;
  res.setSearchDirs(searchDirs(win));
  Progress prog(win, "Imprimiendo…", withProgress);
  const ExportProgress cb = prog.callback();
  QPainter p;
  if (!p.begin(&printer)) { r.error = "no se pudo iniciar la impresión"; return r; }
  const int total = int(pages.size());
  if (!cb(0, total)) { p.end(); r.cancelled = true; return r; }
  for (int i = 0; i < total; i++) {
    if (i > 0 && !printer.newPage()) { r.error = "la impresora rechazó la página " + QString::number(i + 1); p.end(); return r; }
    const QRectF target(QPointF(0, 0), printer.pageRect(QPrinter::DevicePixel).size());
    paintPage(p, doc, pages[size_t(i)], target, int(target.width()), o, res, &r.warnings);
    if (!cb(i + 1, total)) { p.end(); r.cancelled = true; return r; }
  }
  p.end();
  r.ok = true;
  r.pages = total;
  return r;
}
#endif

void commandLocatePdf(MainWindow& win, const QString& file) {
  PdfBackgrounds* bgs = backgrounds(win);
  if (!bgs) return;
  const QString name = QFileInfo(file).fileName();
  const QString chosen = QFileDialog::getOpenFileName(&win, "Buscar " + name, exportDir(win), "PDF (*.pdf)");
  if (chosen.isEmpty()) return;
  if (QFileInfo(chosen).fileName().compare(name, Qt::CaseInsensitive) == 0) {   // same file in another folder: look there too
    bgs->addSearchDir(QFileInfo(chosen).absolutePath());
    return;
  }
  // another name: the first PDF page names the file, so point it at the chosen one (undoable, like any background edit)
  Editor& ed = win.editor();
  std::vector<int> pagesToEdit;
  for (int i = 0; i < ed.pageCount(); i++)
    if (attrOr(ed.doc().pages[size_t(i)].background, "filename") == file) pagesToEdit.push_back(i);
  if (pagesToEdit.empty()) { bgs->addSearchDir(QFileInfo(chosen).absolutePath()); return; }
  ed.editBackgrounds(pagesToEdit, "Localizar PDF", [chosen](Attrs& a) { attrSet(a, "domain", "absolute"); attrSet(a, "filename", chosen); });
  bgs->addSearchDir(QFileInfo(chosen).absolutePath());
}

void commandOpenPdf(MainWindow& win) {
  if (!win.maybeSave()) return;
  const QString dir = exportDir(win);
  const QString path = QFileDialog::getOpenFileName(&win, "Anotar PDF", dir, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  // Preferencias > Cargar/guardar: a notebook saved next to the PDF with the same name is opened instead (its pages already link to the PDF)
  const QFileInfo pdfInfo(path);
  const QString notebook = pdfInfo.absolutePath() + "/" + pdfInfo.completeBaseName() + ".xopp";
  if (win.editor().xpp().autoloadPdfXoj && QFileInfo::exists(notebook) && win.openFile(notebook)) return;
  QString err;
  if (!openPdfFile(win, path, &err)) QMessageBox::warning(&win, kApp, "No se pudo abrir el PDF:\n" + err);
}

static void rememberDir(const QString& path) {
  QSettings("XournalLab", "XournalLabCpp").setValue("pdf/exportDir", QFileInfo(path).absolutePath());
}

void commandExportPdf(MainWindow& win) {
  Editor& ed = win.editor();
  ed.flushPending();   // a text still being written belongs to the document
  ExportDialog dlg(&win, false, ed.pageCount(), ed.currentPage());
  if (dlg.exec() != QDialog::Accepted) return;
  QString path = QFileDialog::getSaveFileName(&win, "Exportar como PDF", exportDir(win) + "/" + exportPdfName(win) + ".pdf", "PDF (*.pdf)");
  if (path.isEmpty()) return;
  if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";
  rememberDir(path);
  const ExportResult r = exportPdfTo(win, path, exportOptions(win, dlg.background(), dlg.dpi(), dlg.pages), true);
  if (r.cancelled) { win.showMessage("Exportación cancelada"); return; }
  if (!r.ok) { QMessageBox::critical(&win, kApp, "No se pudo exportar el PDF:\n" + r.error); return; }
  win.showMessage(QString("PDF exportado: %1 (%2 páginas, %3 MB)").arg(path).arg(r.pages).arg(double(r.bytes) / 1048576.0, 0, 'f', 1), 8000);
  reportWarnings(win, r, true);
}

void commandExportAs(MainWindow& win) {
  Editor& ed = win.editor();
  ed.flushPending();
  ExportDialog dlg(&win, true, ed.pageCount(), ed.currentPage());
  if (dlg.exec() != QDialog::Accepted) return;
  const bool single = dlg.pages.size() == 1 || (dlg.pages.empty() && ed.pageCount() == 1);
  QString path = QFileDialog::getSaveFileName(&win, "Exportar como PNG", exportDir(win) + "/" + baseName(win) + ".png",
                                              single ? "PNG (*.png)" : "PNG (*.png) — se numeran las páginas (*.png)");
  if (path.isEmpty()) return;
  if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
  rememberDir(path);
  const ExportResult r = exportPngTo(win, path, dlg.dpi(), exportOptions(win, dlg.background(), dlg.dpi(), dlg.pages), true);
  if (r.cancelled) { win.showMessage("Exportación cancelada"); return; }
  if (!r.ok) { QMessageBox::critical(&win, kApp, "No se pudo exportar:\n" + r.error); return; }
  win.showMessage(r.files.size() == 1 ? "Imagen exportada: " + r.files.first() : QString("%1 imágenes exportadas (%2-001.png …)").arg(r.files.size()).arg(QFileInfo(path).completeBaseName()), 8000);
  reportWarnings(win, r, true);
}

void commandPrint(MainWindow& win) {
#ifdef XL_HAVE_PRINT
  Editor& ed = win.editor();
  ed.flushPending();
  QPrinter printer(QPrinter::HighResolution);
  if (ed.pageCount() > 0) printer.setPageOrientation(ed.doc().pages[size_t(std::clamp(ed.currentPage(), 0, ed.pageCount() - 1))].width() > ed.doc().pages[size_t(std::clamp(ed.currentPage(), 0, ed.pageCount() - 1))].height() ? QPageLayout::Landscape : QPageLayout::Portrait);
  QPrintDialog dlg(&printer, &win);
  dlg.setWindowTitle("Imprimir");
  dlg.setOption(QAbstractPrintDialog::PrintCurrentPage, true);
  dlg.setOption(QAbstractPrintDialog::PrintPageRange, true);
  dlg.setMinMax(1, ed.pageCount());
  if (dlg.exec() != QDialog::Accepted) return;
  std::vector<int> pages;
  switch (printer.printRange()) {
    case QPrinter::CurrentPage: pages = {ed.currentPage()}; break;
    case QPrinter::PageRange:
      for (int i = std::max(1, printer.fromPage()); i <= std::min(ed.pageCount(), printer.toPage()); i++) pages.push_back(i - 1);
      break;
    default: break;
  }
  const ExportResult r = printTo(win, printer, exportOptions(win, true, 150, pages), true);
  if (r.cancelled) { win.showMessage("Impresión cancelada"); return; }
  if (!r.ok) { QMessageBox::critical(&win, kApp, "No se pudo imprimir:\n" + r.error); return; }
  win.showMessage(QString("Enviadas %1 páginas a la impresora").arg(r.pages), 6000);
  reportWarnings(win, r, true);
#else
  QMessageBox::information(&win, kApp, "Esta compilación no incluye Qt PrintSupport.");
#endif
}

}  // namespace pdfui
