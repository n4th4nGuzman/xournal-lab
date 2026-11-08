// Text tool: a click on the page opens an editor in place (a QPlainTextEdit laid over the page at the current zoom, with the text's own font, size and
// colour); a click on an existing text edits it. Ctrl+Enter or a click away confirms, Esc cancels, an emptied text disappears. Everything the editor
// did is ONE undo step. Port of Inserts.openText/commitText in inserts.ts. The page keeps the Markdown source as the text (attr xlMd=1); the formatted
// result appears when the box is confirmed, as in the Electron app.
#include "documentview.h"
#include "editor.h"
#include "genobj.h"
#include "markdown.h"
#include "pagerender.h"
#include "texttools.h"
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QWidget>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace texttools {

TextState& state() { static TextState s; return s; }

namespace {

class TextEdit : public QPlainTextEdit {
 public:
  using QPlainTextEdit::QPlainTextEdit;
  std::function<void()> onAccept, onCancel, onBlur;
  int tabSpaces = 0;   // Preferencias > Valores predeterminados: Tab writes this many spaces (0 = a real tab)

 protected:
  void keyPressEvent(QKeyEvent* e) override {
    if (e->key() == Qt::Key_Tab && tabSpaces > 0 && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) { e->accept(); insertPlainText(QString(tabSpaces, ' ')); return; }
    if (e->key() == Qt::Key_Escape) { e->accept(); if (onCancel) onCancel(); return; }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) { e->accept(); if (onAccept) onAccept(); return; }
    QPlainTextEdit::keyPressEvent(e);
  }
  void focusOutEvent(QFocusEvent* e) override {
    QPlainTextEdit::focusOutEvent(e);
    // a popup menu or another window coming forward does not mean the writer is done
    if (e->reason() == Qt::PopupFocusReason || e->reason() == Qt::ActiveWindowFocusReason) return;
    if (onBlur) QTimer::singleShot(0, this, [cb = onBlur] { cb(); });
  }
};

/** Markdown live preview: the box as it will look once confirmed, in a card just below the editor (the Electron app only shows it after confirming). */
class MdPreview : public QWidget {
 public:
  using QWidget::QWidget;
  void set(const QString& text, double size, const QString& font, const QColor& ink, double zoom) {
    text_ = text; size_ = size; font_ = font; ink_ = ink; zoom_ = zoom;
    const QSizeF sz = markdownSize(text, size, font);
    resize(int(std::ceil(sz.width() * zoom)) + 16, int(std::ceil(sz.height() * zoom)) + 12);
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const bool light = ink_.lightnessF() > 0.75;   // a light ink needs a dark card to be seen
    p.fillRect(rect(), light ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xfd, 0xfd, 0xfb));
    p.setPen(QColor(0x4a, 0x90, 0xe2));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    p.translate(8, 6);
    p.scale(zoom_, zoom_);
    drawMarkdown(p, text_, size_, font_, QPointF(0, 0), ink_);
  }

 private:
  QString text_, font_;
  double size_ = 12, zoom_ = 1;
  QColor ink_ = Qt::black;
};

class TextTool;
TextTool* g_tool = nullptr;

class TextTool : public Tool {
 public:
  explicit TextTool(Editor& ed) : Tool(ed) {
    g_tool = this;
    conn_ = {
      QObject::connect(&ed, &Editor::documentReplaced, &ed, [this] { abandon(); }),
      QObject::connect(&ed, &Editor::structureChanged, &ed, [this] { if (s_.edit) commit(false); }),
    };
  }
  ~TextTool() override {
    for (auto& c : conn_) QObject::disconnect(c);
    if (g_tool == this) g_tool = nullptr;
    abandon();
  }

  QCursor cursor() const override { return Qt::IBeamCursor; }
  void deactivate() override { commit(false); }

  void pointerDown(const PointerEvent& e) override {
    if (e.page < 0) return;
    commit(false);
    open(e.page, e.pos);
  }

  QPlainTextEdit* textEditor() const { return s_.edit; }

  bool markdown() const { return s_.edit ? s_.md : state().markdown; }

  void setMarkdown(bool on) {
    state().markdown = on;
    if (s_.edit) { s_.md = on; place(); }
    if (state().markdownChanged) state().markdownChanged(on);
  }

  /** Finish the open text. `cancel` keeps an existing text unchanged and drops a new one. */
  void commit(bool cancel) {
    if (!s_.edit || s_.done) return;
    Editor& ed = editor();
    QString text = s_.edit->toPlainText();
    while (!text.isEmpty() && text.back().isSpace()) text.chop(1);   // trailing blanks are not text
    TextEdit* w = s_.edit;
    w->hide();
    w->deleteLater();
    if (s_.preview) { delete s_.preview.data(); }
    if (DocumentView* v = view()) v->setFocus();
    const Session s = std::move(s_);
    s_ = Session();
    if (state().markdownChanged) state().markdownChanged(state().markdown);
    if (s.page < 0 || s.page >= ed.pageCount()) return;
    const auto& layers = ed.doc().pages[size_t(s.page)].layers;
    if (layers.empty()) return;
    const int page = s.page, layer = std::clamp(s.layer, 0, int(layers.size()) - 1);

    if (s.existing) {
      const Text before = s.orig;
      const bool mdBefore = isMarkdown(before);
      const bool mdAfter = cancel ? mdBefore : s.md;
      if (!cancel && text.isEmpty()) {   // emptied: the box is deleted (it was lifted out of the page while editing)
        const int idx = s.index;
        Action a;
        a.label = QStringLiteral("Borrar texto");
        a.page = page;
        a.undo = [&ed, page, layer, idx, before] { ed.insertElement(page, layer, idx, Element(before)); };
        a.redo = [&ed, page, layer, idx] { ed.takeElement(page, layer, idx); };
        ed.pushAction(std::move(a));
        return;
      }
      Text after = before;
      if (!cancel && text != before.text) after.text = text;
      setMd(after.attrs, mdAfter);
      const int idx = std::min(s.index, int(layers[size_t(layer)].elements.size()));
      ed.insertElement(page, layer, idx, Element(after));
      if (after.text != before.text || mdAfter != mdBefore) {
        Action a;
        a.label = QStringLiteral("Editar texto");
        a.page = page;
        a.undo = [&ed, page, layer, idx, before] { ed.replaceElements(page, layer, idx, 1, {Element(before)}); };
        a.redo = [&ed, page, layer, idx, after] { ed.replaceElements(page, layer, idx, 1, {Element(after)}); };
        ed.pushAction(std::move(a));
      }
      return;
    }
    if (cancel || text.isEmpty()) return;
    Text t;
    t.attrs = {{"font", "Sans"}, {"size", fmt8(s.size)}, {"x", fmt8(s.x)}, {"y", fmt8(s.y)}, {"color", s.colorHex + "ff"}};
    if (s.md) t.attrs.emplace_back("xlMd", "1");
    t.text = text;
    if (!ed.layerVisible(page, layer)) {
      ed.setLayerVisible(page, layer, true);
      ed.notify(QStringLiteral("La capa activa estaba oculta: se ha vuelto a mostrar"));
    }
    ed.commitAdd(page, layer, Element(std::move(t)), QStringLiteral("Texto"));
  }

 private:
  struct Session {
    int page = -1, layer = 0, index = -1;
    bool existing = false, md = false, done = false;
    Text orig;
    double x = 0, y = 0, size = 12;
    QString colorHex = QStringLiteral("#000000"), family = QStringLiteral("Sans");
    QPointer<TextEdit> edit;
    QPointer<MdPreview> preview;
  };

  static void setMd(Attrs& a, bool on) {
    a.erase(std::remove_if(a.begin(), a.end(), [](const auto& p) { return p.first == "xlMd"; }), a.end());
    if (on) a.emplace_back("xlMd", "1");
  }

  void abandon() {
    if (s_.edit) { s_.edit->hide(); s_.edit->deleteLater(); }
    if (s_.preview) { delete s_.preview.data(); }
    s_ = Session();
  }

  void open(int page, const QPointF& pos) {
    Editor& ed = editor();
    DocumentView* v = view();
    if (!v || page >= ed.pageCount()) return;
    s_ = Session();
    s_.page = page;
    s_.layer = ed.activeLayer(page);
    s_.x = pos.x(); s_.y = pos.y();
    s_.size = ed.style().textSize;
    s_.colorHex = ed.style().color;
    s_.md = state().markdown;

    // the topmost visible text under the point (the index narrows it to the few boxes that can be there)
    std::vector<PageIndex::Hit> hits;
    ed.index(page).queryPoint(pos, 0, hits);
    QString text;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
      if (!ed.layerVisible(page, it->first)) continue;
      const auto& els = ed.doc().pages[size_t(page)].layers[size_t(it->first)].elements;
      if (it->second < 0 || it->second >= int(els.size())) continue;
      const Text* t = std::get_if<Text>(&els[size_t(it->second)]);
      if (!t || !pointInRect(pos.x(), pos.y(), textRect(*t))) continue;
      s_.existing = true;
      s_.orig = *t;
      s_.layer = it->first;
      s_.index = it->second;
      s_.x = attrOr(t->attrs, "x", "0").toDouble();
      s_.y = attrOr(t->attrs, "y", "0").toDouble();
      s_.size = attrOr(t->attrs, "size", "12").toDouble();
      s_.colorHex = colorOf(attrOr(t->attrs, "color", "#000000ff")).name(QColor::HexRgb);
      s_.family = attrOr(t->attrs, "font", "Sans");
      s_.md = isMarkdown(*t);
      text = t->text;
      break;
    }
    if (!(s_.size > 0)) s_.size = 12;

    auto* w = new TextEdit(v->viewport());
    w->tabSpaces = editor().xpp().tabSpaces ? editor().xpp().tabSpaceCount : 0;
    w->setObjectName(QStringLiteral("xlTextEdit"));
    w->setFrameShape(QFrame::NoFrame);
    w->setLineWrapMode(QPlainTextEdit::NoWrap);
    w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    w->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    w->document()->setDocumentMargin(0);
    w->setPlainText(text);
    w->onAccept = [this] { commit(false); };
    w->onCancel = [this] { commit(true); };
    w->onBlur = [this, self = QPointer<TextEdit>(w)] { if (self && s_.edit == self) commit(false); };   // only if it is still the box being written
    s_.edit = w;
    if (s_.existing) ed.takeElement(page, s_.layer, s_.index);   // hidden while editing, so it is not drawn twice
    if (state().markdownChanged) state().markdownChanged(s_.md);   // the switch shows this box's mode; the default for new texts is not touched

    place();
    QObject::connect(w->document(), &QTextDocument::contentsChanged, w, [this] { place(); });
    for (QScrollBar* sb : {v->verticalScrollBar(), v->horizontalScrollBar()}) QObject::connect(sb, &QScrollBar::valueChanged, w, [this] { place(); });
    QObject::connect(v, &DocumentView::zoomChanged, w, [this] { place(); });
    w->show();
    w->setFocus();
    w->moveCursor(QTextCursor::End);
    QTimer::singleShot(0, w, [w] { if (w->isVisible()) w->setFocus(); });
  }

  // font, position and size of the editor for the current zoom and scroll: the box grows with what is typed
  void place() {
    TextEdit* w = s_.edit;
    DocumentView* v = view();
    if (!w || !v || placing_) return;
    placing_ = true;
    const double z = v->zoom();
    const double px = std::max(4.0, s_.size * z), lh = px * 1.2;
    QFont f;
    f.setFamilies(s_.family.isEmpty() || s_.family == "Sans" ? QStringList{"Segoe UI", "Arial", "Sans Serif"} : QStringList{s_.family, "Sans Serif"});
    f.setPixelSize(int(std::lround(px)));
    f.setHintingPreference(QFont::PreferNoHinting);
    if (w->font() != f) w->setFont(f);
    w->setStyleSheet(QStringLiteral("QPlainTextEdit { color: %1; background: rgba(120,160,255,28); border: 1px dashed #4a90e2; padding: 0; selection-background-color: #2f7dd1; selection-color: white; }").arg(s_.colorHex));
    // fixed line pitch of 1.2 em, as the page draws it
    QTextBlockFormat bf;
    bf.setLineHeight(lh, QTextBlockFormat::FixedHeight);
    QTextCursor c(w->document());
    c.select(QTextCursor::Document);
    if (std::abs(c.blockFormat().lineHeight() - lh) > 1e-6) { QSignalBlocker b(w->document()); c.mergeBlockFormat(bf); }
    const QFontMetricsF fm(w->font());
    double width = 0;
    for (QTextBlock b = w->document()->begin(); b.isValid(); b = b.next()) width = std::max(width, fm.horizontalAdvance(b.text()));
    const int lines = std::max(1, w->document()->blockCount());
    w->resize(int(std::ceil(std::max(width + 10, 60.0))) + 2, int(std::ceil(lines * lh)) + 4);
    const QPointF vp = v->toView(s_.page, QPointF(s_.x, s_.y));
    w->move(vp.toPoint() - QPoint(1, 1));
    placePreview(w, v, z);
    placing_ = false;
  }

  // the live preview of a markdown box: below the editor (above it when there is no room), only while the box is markdown and has something typed
  void placePreview(TextEdit* w, DocumentView* v, double z) {
    const QString text = w->toPlainText();
    if (!s_.md || text.trimmed().isEmpty()) { if (s_.preview) s_.preview->hide(); return; }
    if (!s_.preview) s_.preview = new MdPreview(v->viewport());
    MdPreview* pv = s_.preview;
    pv->setAttribute(Qt::WA_TransparentForMouseEvents);
    pv->set(text, s_.size, s_.family, QColor(s_.colorHex), z);
    int y = w->geometry().bottom() + 8;
    if (y + pv->height() > v->viewport()->height()) y = std::max(0, w->y() - pv->height() - 8);
    pv->move(std::max(0, std::min(w->x(), v->viewport()->width() - pv->width())), y);
    pv->setObjectName(QStringLiteral("xlMdPreview"));
    pv->show();
    pv->raise();
  }

  Session s_;
  bool placing_ = false;
  std::vector<QMetaObject::Connection> conn_;
};

}  // namespace

void commitOpenText(bool cancel) { if (g_tool) g_tool->commit(cancel); }
QPlainTextEdit* openTextEditor() { return g_tool ? g_tool->textEditor() : nullptr; }
bool textMarkdown() { return g_tool ? g_tool->markdown() : state().markdown; }
void setTextMarkdown(bool on) {
  if (g_tool) g_tool->setMarkdown(on);
  else { state().markdown = on; if (state().markdownChanged) state().markdownChanged(on); }
}

void registerTextTool(Editor& ed) {
  ToolInfo i;
  i.id = "text"; i.label = "Texto"; i.tooltip = "Texto: clic en la página; clic en un texto para editarlo. Ctrl+Intro o clic fuera confirma, Esc cancela";
  i.shortcut = QKeySequence("T");
  i.icon = icon("text");
  i.inserter = true;
  i.order = 21;
  i.factory = [](Editor& e) { return std::make_unique<TextTool>(e); };
  ed.registerTool(i);
}

}  // namespace texttools
