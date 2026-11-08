// Find bar (find.ts): searches the text boxes and LaTeX formulas with core/find.h, steps through the matches with wrap-around, scrolls each into view and
// marks every match on the page (the current one stronger).
#include "parts.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>

namespace chrome {

/** Transparent layer over the page area that outlines every match. It lives in the viewport's own coordinates and follows scroll and zoom. */
class FindOverlay : public QWidget {
 public:
  FindOverlay(Chrome& c, FindBar& bar) : QWidget(c.win.view()->viewport()), c_(c), bar_(bar) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    parentWidget()->installEventFilter(this);
    setGeometry(parentWidget()->rect());
    DocumentView* v = c.win.view();
    connect(v->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { update(); });
    connect(v->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { update(); });
    connect(v, &DocumentView::zoomChanged, this, [this] { update(); });
    connect(&c.ed, &Editor::structureChanged, this, [this] { update(); });
    hide();
  }
 protected:
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == parentWidget() && e->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(o, e);
  }
  void paintEvent(QPaintEvent* ev) override {
    DocumentView* v = c_.win.view();
    const FindState& st = bar_.state();
    if (st.hits.empty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool neonOn = c_.theme() == "neon";
    const QColor hitFill = neonOn ? QColor(5, 225, 255, 55) : QColor(255, 214, 0, 80);
    const QColor hitLine = neonOn ? neon().cyan : QColor(230, 170, 0);
    QColor curFill = neonOn ? QColor(255, 42, 157, 90) : QColor(255, 120, 0, 110);
    QColor curLine = neonOn ? neon().pink : QColor(255, 90, 0);
    if (QColor ac(c_.ed.xpp().colorActiveSelection); ac.isValid()) { curLine = ac; curFill = ac; curFill.setAlpha(100); }   // Preferencias > Ver: colour of the match being looked at
    const std::vector<int> vis = v->visiblePages();
    for (size_t i = 0; i < st.hits.size(); i++) {
      const FindHit& h = st.hits[i];
      if (std::find(vis.begin(), vis.end(), h.page) == vis.end()) continue;
      const bool cur = int(i) == st.index;
      p.setBrush(cur ? curFill : hitFill);
      p.setPen(QPen(cur ? curLine : hitLine, cur ? 2.5 : 1.2));
      // a plain text is marked exactly where the matched characters are; a formula or markdown text (no glyph mapping) is marked as a whole box
      std::vector<std::array<double, 4>> boxes = findSpanBoxes(c_.ed.doc(), h);
      const bool exact = !boxes.empty();
      if (!exact) { const auto b = findHitBox(c_.ed.doc(), h); if (!b) continue; boxes.push_back(*b); }
      for (const auto& b : boxes) {
        const QPointF a = v->toView(h.page, QPointF(b[0], b[1])), z = v->toView(h.page, QPointF(b[2], b[3]));
        const QRectF r = QRectF(a, z).normalized().adjusted(exact ? -1 : -3, exact ? -1 : -3, exact ? 1 : 3, exact ? 1 : 3);
        if (!r.intersects(QRectF(ev->rect()))) continue;
        p.drawRoundedRect(r, 3, 3);
      }
    }
  }
 private:
  Chrome& c_;
  FindBar& bar_;
};

FindBar::FindBar(Chrome& c, QWidget* parent) : QToolBar("Buscar", parent), c_(c) {
  setObjectName("findbar");
  setMovable(false);
  setFloatable(false);
  auto* l = new QLabel("Buscar", this);
  l->setContentsMargins(6, 0, 6, 0);
  addWidget(l);
  input_ = new QLineEdit(this);
  input_->setObjectName("findQ");
  input_->setPlaceholderText("Texto o fórmula…");
  input_->setMinimumWidth(280);
  input_->setMinimumHeight(24);
  input_->installEventFilter(this);
  addWidget(input_);
  auto btn = [this](const QString& name, const QString& text, const QString& tip) {
    auto* b = new QToolButton(this);
    b->setObjectName(name);
    if (name == "findPrev" || name == "findNext" || name == "findClose") b->setIcon(glyph(name == "findPrev" ? "prev" : name == "findNext" ? "next" : "cross")); else b->setText(text);
    b->setToolTip(tip);
    b->setMinimumSize(40, 34);
    addWidget(b);
    return b;
  };
  QToolButton* prev = btn("findPrev", "◀", "Anterior (Mayús+Intro)");
  QToolButton* next = btn("findNext", "▶", "Siguiente (Intro)");
  count_ = new QLabel(this);
  count_->setObjectName("findCount");
  count_->setMinimumWidth(90);
  count_->setContentsMargins(8, 0, 8, 0);
  addWidget(count_);
  case_ = new QCheckBox("distinguir mayúsculas", this);
  addWidget(case_);
  QToolButton* close = btn("findClose", "×", "Cerrar (Esc)");

  debounce_.setSingleShot(true);
  connect(&debounce_, &QTimer::timeout, this, [this] { run(); });
  connect(input_, &QLineEdit::textChanged, this, [this] { debounce_.start(120); });
  connect(case_, &QCheckBox::toggled, this, [this] { run(); });
  connect(prev, &QToolButton::clicked, this, [this] { step(-1); });
  connect(next, &QToolButton::clicked, this, [this] { step(1); });
  connect(close, &QToolButton::clicked, this, [this] { this->close(); });
  connect(&c.ed, &Editor::documentReplaced, this, [this] { state_ = FindState(); count_->clear(); overlay_->update(); });
  overlay_ = new FindOverlay(c, *this);
  hide();
  c.ed.addCommand("edit.find", "Buscar…", QKeySequence::Find, [this] { open(); });
}

bool FindBar::eventFilter(QObject* o, QEvent* e) {
  if (o == input_ && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { debounce_.stop(); step(k->modifiers() & Qt::ShiftModifier ? -1 : 1); return true; }
    if (k->key() == Qt::Key_Escape) { close(); return true; }
  }
  return QToolBar::eventFilter(o, e);
}

void FindBar::open() {
  show();
  input_->setFocus();
  input_->selectAll();
  if (!input_->text().isEmpty()) run();
}

void FindBar::close() {
  hide();
  state_ = FindState();
  count_->clear();
  overlay_->hide();
  if (c_.win.view()) c_.win.view()->setFocus();
}

void FindBar::setQuery(const QString& q) { input_->setText(q); debounce_.stop(); }

QString FindBar::countText() const { return count_->text(); }

void FindBar::run() {
  state_ = FindState();
  const QString q = input_->text();
  if (q.isEmpty()) { count_->clear(); overlay_->hide(); return; }
  FindOptions opt;
  opt.matchCase = case_->isChecked();
  Editor& ed = c_.ed;
  opt.skipLayer = [&ed](int page, int layer) { return !ed.layerVisible(page, layer); };   // hidden layers are not searched
  state_.run(ed.doc(), q, opt);
  if (state_.hits.empty()) { count_->setText("Sin resultados"); overlay_->hide(); return; }
  reveal();
}

void FindBar::step(int delta) {
  if (state_.hits.empty()) { run(); return; }
  state_.step(delta);
  reveal();
}

void FindBar::reveal() {
  const FindHit* h = state_.current();
  if (!h) return;
  count_->setText(QString("%1 de %2").arg(state_.index + 1).arg(state_.hits.size()));
  DocumentView* v = c_.win.view();
  overlay_->show();
  overlay_->raise();
  const auto b = findHitBox(c_.ed.doc(), *h);
  if (!b) return;
  if (c_.ed.hasTool("select")) c_.ed.setTool("select");
  const std::vector<int> vis = v->visiblePages();
  if (std::find(vis.begin(), vis.end(), h->page) == vis.end()) v->scrollToPage(h->page);
  // bring the box into the visible part: a third of the way down from the top when it is outside
  const QPointF a = v->toView(h->page, QPointF((*b)[0], (*b)[1])), z = v->toView(h->page, QPointF((*b)[2], (*b)[3]));
  const int vh = v->viewport()->height();
  if (a.y() < 0 || z.y() > vh) v->scrollByPixels(0, a.y() - vh / 3.0);
  if (a.x() < 0 || z.x() > v->viewport()->width()) v->scrollByPixels(a.x() - v->viewport()->width() / 4.0, 0);
  c_.ed.selection().select(h->page, {ElementRef{h->page, h->layer, h->element}});
  overlay_->update();
}

}  // namespace chrome
