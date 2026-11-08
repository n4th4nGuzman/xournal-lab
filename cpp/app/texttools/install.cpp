// Entry point of the "texttools" area: text, formulas, images and shapes. Registers the tools (text T, LaTeX X, shapes G), the toolbar switches
// (Markdown, automatic shapes), the image commands (menu, clipboard, drag and drop) and the markdown measure the geometry module needs.
#include "documentview.h"
#include "editor.h"
#include "mainwindow.h"
#include "markdown.h"
#include "modules.h"
#include "texttools.h"
#include <QAction>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <cmath>

namespace texttools {

namespace {

const QColor kInk(0xe6, 0xe6, 0xe6);

void glyph(QPainter& p, const QString& n) {   // 32 x 32 logical units, like app/icons.cpp
  QPen pen(kInk, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  auto label = [&](const QString& t, int px, bool bold = true, const QString& family = QString()) {
    QFont f = p.font();
    if (!family.isEmpty()) f.setFamily(family);
    f.setPixelSize(px);
    f.setBold(bold);
    p.setFont(f);
    p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, t);
  };
  if (n == "text") {
    p.drawLine(QPointF(8, 8), QPointF(24, 8));
    p.drawLine(QPointF(16, 8), QPointF(16, 25));
    p.drawLine(QPointF(12.5, 25), QPointF(19.5, 25));
  } else if (n == "latex") {
    label(QStringLiteral("∑"), 24, false, QStringLiteral("Cambria Math"));
  } else if (n == "markdown") {
    label(QStringLiteral("M↓"), 15);
  } else if (n == "image") {
    p.drawRoundedRect(QRectF(4.5, 6.5, 23, 19), 3, 3);
    p.setBrush(kInk);
    p.drawEllipse(QPointF(11, 13), 2.2, 2.2);
    QPainterPath m;
    m.moveTo(6, 24); m.lineTo(13, 17); m.lineTo(18, 21.5); m.lineTo(22, 16.5); m.lineTo(26, 21);
    p.setBrush(Qt::NoBrush);
    p.drawPath(m);
  } else if (n == "shape" || n == "shape-rect") {
    p.drawRect(QRectF(6, 9, 20, 15));
  } else if (n == "shape-ellipse") {
    p.drawEllipse(QRectF(5, 8, 22, 16));
  } else if (n == "shape-line") {
    p.drawLine(QPointF(7, 25), QPointF(25, 7));
  } else if (n == "shape-arrow") {
    p.drawLine(QPointF(6, 25), QPointF(25, 8));
    QPainterPath h;
    h.moveTo(26, 7); h.lineTo(17.5, 9.5); h.lineTo(23.5, 15.5); h.closeSubpath();
    p.setBrush(kInk);
    p.drawPath(h);
  } else if (n == "shape-darrow") {
    p.drawLine(QPointF(8, 24), QPointF(24, 8));
    QPainterPath h;
    h.moveTo(26, 6); h.lineTo(17.5, 8.5); h.lineTo(23.5, 14.5); h.closeSubpath();
    h.moveTo(6, 26); h.lineTo(14.5, 23.5); h.lineTo(8.5, 17.5); h.closeSubpath();
    p.setBrush(kInk);
    p.drawPath(h);
  } else if (n == "shape-axes") {
    p.drawLine(QPointF(8, 25), QPointF(8, 7));
    p.drawLine(QPointF(8, 25), QPointF(26, 25));
    p.drawLine(QPointF(8, 7), QPointF(5.5, 11));
    p.drawLine(QPointF(8, 7), QPointF(10.5, 11));
    p.drawLine(QPointF(26, 25), QPointF(22, 22.5));
    p.drawLine(QPointF(26, 25), QPointF(22, 27.5));
  } else if (n == "recog") {
    // a wobbly circle that becomes a clean one
    p.setPen(QPen(kInk, 1.6, Qt::DotLine, Qt::RoundCap));
    QPainterPath w;
    w.moveTo(9, 9); w.cubicTo(3, 12, 4, 22, 10, 24); w.cubicTo(14, 26, 19, 22, 17, 17); w.cubicTo(15, 12, 12, 11, 9, 9);
    p.drawPath(w);
    p.setPen(pen);
    p.drawEllipse(QPointF(21, 11), 6, 6);
    p.drawLine(QPointF(15, 17), QPointF(17, 15));
  } else {
    p.drawRoundedRect(QRectF(5, 5, 22, 22), 5, 5);
    label(n.left(1).toUpper(), 15);
  }
}

/** The one place that decides how big a markdown box is: the geometry module (hit tests, selection frames) asks here. */
Rect markdownRect(const Text& t) {
  const double x = attrOr(t.attrs, "x", "0").toDouble(), y = attrOr(t.attrs, "y", "0").toDouble();
  const QSizeF sz = markdownSize(t.text, attrOr(t.attrs, "size", "12").toDouble(), attrOr(t.attrs, "font", "Sans"));
  return {x, y, x + sz.width(), y + sz.height()};
}

bool isImagePath(const QString& path) { return !QImageReader::imageFormat(path).isEmpty(); }

/** Drag and drop of pictures onto the page, and the double click that re-opens a selected formula. */
class ViewFilter : public QObject {
 public:
  explicit ViewFilter(MainWindow& win) : QObject(&win), win_(win) {}

  bool eventFilter(QObject*, QEvent* ev) override {
    switch (ev->type()) {
      case QEvent::DragEnter:
      case QEvent::DragMove: {
        auto* d = static_cast<QDropEvent*>(ev);
        if (acceptsDrop(d->mimeData())) { d->acceptProposedAction(); return true; }
        return false;
      }
      case QEvent::Drop: {
        auto* d = static_cast<QDropEvent*>(ev);
        if (!acceptsDrop(d->mimeData())) return false;
        Editor& ed = win_.editor();
        const QMimeData* m = d->mimeData();
        bool done = false;
        for (const QUrl& u : m->urls()) if (u.isLocalFile() && isImagePath(u.toLocalFile())) { done = insertImageFile(ed, u.toLocalFile()); break; }
        if (!done && m->hasImage()) insertImage(ed, qvariant_cast<QImage>(m->imageData()));
        d->acceptProposedAction();
        return true;
      }
      case QEvent::MouseButtonDblClick: {
        Editor& ed = win_.editor();
        if (ed.tool() != "select" && ed.tool() != "lasso") return false;
        const auto items = ed.selection().items();
        if (items.size() != 1) return false;
        const ElementRef r = items.front();
        if (r.page < 0 || r.page >= ed.pageCount()) return false;
        const auto& layers = ed.doc().pages[size_t(r.page)].layers;
        if (r.layer < 0 || r.layer >= int(layers.size()) || r.index < 0 || r.index >= int(layers[size_t(r.layer)].elements.size())) return false;
        const Image* im = std::get_if<Image>(&layers[size_t(r.layer)].elements[size_t(r.index)]);
        if (!im || !isTexImage(*im)) return false;
        pending_ = r;   // the dialog is modal: it opens when the button is released
        return true;
      }
      case QEvent::MouseButtonRelease: {
        if (pending_.page < 0) return false;
        const ElementRef r = pending_;
        pending_ = ElementRef{-1, 0, 0};
        QTimer::singleShot(0, this, [this, r] { editFormula(win_, r.page, r.layer, r.index); });
        return false;
      }
      default: return false;
    }
  }

 private:
  static bool acceptsDrop(const QMimeData* m) {
    if (!m) return false;
    if (m->hasImage()) return true;
    for (const QUrl& u : m->urls()) if (u.isLocalFile() && isImagePath(u.toLocalFile())) return true;
    return false;
  }
  MainWindow& win_;
  ElementRef pending_{-1, 0, 0};
};

QString g_lastImageDir;

}  // namespace

QIcon icon(const QString& name) {
  QIcon ic;
  for (int px : {24, 32, 48}) {
    QPixmap pm(px * 2, px * 2);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(2);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 32.0, px / 32.0);
    glyph(p, name);
    p.end();
    ic.addPixmap(pm);
  }
  return ic;
}

}  // namespace texttools

void installTextTools(MainWindow& win) {
  using namespace texttools;
  Editor& ed = win.editor();

  setMarkdownMeasure(&markdownRect);   // elementBBox, hit tests and selection frames of markdown texts

  registerShapeTool(ed);
  registerTextTool(ed);
  registerLatexTool(ed, win);
  ed.addFlusher([] { commitOpenText(false); });   // save / export / print take a text that is still being typed

  // ---- toolbar switches that are not tools (the toolbar rebuild that the registrations above schedule picks them up) ----------------------
  QAction* md = ed.addCommand("tool.markdown", "Markdown en los textos", {}, [] { setTextMarkdown(!textMarkdown()); });
  md->setCheckable(true);
  md->setIcon(icon("markdown"));
  md->setChecked(state().markdown);
  md->setToolTip("Markdown en los textos: con él activado, # Título, **negrita**, *cursiva*, `código`, listas con - y 1., citas con > y líneas con --- se dibujan con formato; el texto sigue siendo editable en markdown");
  state().markdownChanged = [md](bool on) { md->setChecked(on); };

  QAction* recog = ed.addCommand("tool.recognizer", "Formas automáticas", {}, [&ed] { ed.setRecognizeShapes(!ed.recognizeShapes()); });
  recog->setCheckable(true);
  recog->setIcon(icon("recog"));
  recog->setChecked(ed.recognizeShapes());
  recog->setToolTip("Formas automáticas: el lápiz convierte líneas, elipses, rectángulos y triángulos dibujados a mano en la figura limpia");
  QObject::connect(&ed, &Editor::recognizeChanged, recog, [recog](bool on) { recog->setChecked(on); });

  QAction* image = ed.addCommand("tool.image", "Imagen…", {}, [&win] {
    Editor& e = win.editor();
    if (e.pageCount() == 0) return;
    const QString p = QFileDialog::getOpenFileName(&win, QStringLiteral("Insertar imagen"), g_lastImageDir,
                                                   QStringLiteral("Imágenes (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;Todos los archivos (*)"));
    if (p.isEmpty()) return;
    g_lastImageDir = QFileInfo(p).absolutePath();
    insertImageFile(e, p);
  });
  image->setIcon(icon("image"));
  image->setToolTip("Insertar imagen (también Ctrl+V o arrastrar)");
  if (QMenu* file = win.menu("file")) file->insertAction(ed.action("file.exportPdf"), image);

  win.toolbarExtras << nullptr << md << recog << image;

  // ---- clipboard: a picture on the clipboard is inserted by the paste command (the selection's own paste handles what the app copied) --------
  QAction* paste = ed.action("edit.paste");
  // (the selection's paste command hands a clipboard picture to pasteImageFromClipboard: one handler, so a picture is inserted once)
  paste->setEnabled(true);

  // ---- drag and drop of pictures, double click on a selected formula ------------------------------------------------------------------------
  if (DocumentView* v = win.view()) {
    v->viewport()->setAcceptDrops(true);
    v->viewport()->installEventFilter(new ViewFilter(win));
  }
}
