// The option panels of the real line and of set diagrams: expressions written with a row of symbols, a live result (or the mistake, where it is), and the
// settings of the drawing. Port of mathpanel.ts. With the object selected, an edit rebuilds it (one undo step per commit: Enter, leaving the field, a
// button); with nothing selected the panel edits what the next one is made with.
#include "mathstate.h"
#include "mathtools.h"
#include "panelwidgets.h"
#include "setexpr.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace mathobj {
namespace {

using namespace sets;

const QStringList kLineSymbols{"(", ")", "[", "]", "{", "}", ",", "∞", "−∞", "π", "∪", "∩", "∖", "Δ", "′", "ℝ", "∅"};
const QStringList kVennSymbols{"(", ")", "∪", "∩", "∖", "Δ", "′", "U"};

const char* kSheet =
    "QLineEdit, QDoubleSpinBox, QComboBox { min-height: 22px; }"
    "QPushButton { min-height: 24px; padding: 2px 10px; }";

class MathPanel final : public QWidget {
 public:
  MathPanel(QWidget* parent, Editor& ed, bool line) : QWidget(parent), ed_(ed), line_(line) {
    setObjectName(line ? "linePanel" : "vennPanel");
    setStyleSheet(kSheet);
    setFixedWidth(400);
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(12, 10, 12, 10);
    auto again = [this] { if (!selfEdit_) { dirty_ = true; if (isVisible()) scheduleRebuild(); } };
    connect(&ed_, &Editor::selectionChanged, this, again);
    connect(&ed_, &Editor::historyChanged, this, again);
    rebuildNow();
  }

  /** Children that lose focus while the panel is destroyed would commit through half-deleted widgets. */
  ~MathPanel() override { closing_ = true; }

  void rebuildNow() {
    dirty_ = false;
    rebuildQueued_ = false;
    if (content_) { root_->removeWidget(content_); content_->hide(); content_->setParent(nullptr); content_->deleteLater(); }
    content_ = new QWidget(this);
    auto* v = new QVBoxLayout(content_);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    root_->addWidget(content_);
    rows_.clear();
    last_ = nullptr;
    if (line_) buildLine(v); else buildVenn(v);
    adjustSize();
  }

 protected:
  void showEvent(QShowEvent* e) override {
    QWidget::showEvent(e);
    if (dirty_) rebuildNow();
  }

 private:
  struct Row { QPointer<CommitEdit> expr, label; QPointer<QLabel> out, echo; };

  // ---------------------------------------------------------------------------------------------------- helpers
  void scheduleRebuild() {
    if (rebuildQueued_) return;
    rebuildQueued_ = true;
    QTimer::singleShot(0, this, [this] { if (rebuildQueued_) rebuildNow(); });
  }

  QLabel* note(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet("color: #a8a8a8; font-size: 11px;");
    return l;
  }

  static QLabel* outLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setContentsMargins(21, 0, 0, 0);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
  }

  static QLabel* echoLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setTextFormat(Qt::RichText);
    l->setContentsMargins(21, 0, 0, 0);
    l->hide();
    return l;
  }

  CommitEdit* text(QWidget* parent, const QString& value, const QString& placeholder, int width = 0) {
    auto* e = new CommitEdit(parent);
    e->setText(value);
    e->setPlaceholderText(placeholder);
    if (width) e->setFixedWidth(width);
    e->onFocus = [this, e] { last_ = e; };
    return e;
  }

  QWidget* field(QLayout* into, const QString& label, QWidget* w, int labelWidth = 96) {
    auto* row = new QWidget(content_);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    auto* l = new QLabel(label, row);
    l->setFixedWidth(labelWidth);
    h->addWidget(l);
    w->setParent(row);
    h->addWidget(w, 1);
    into->addWidget(row);
    return row;
  }

  QCheckBox* check(QLayout* into, const QString& label, bool on, const QString& name) {
    auto* c = new QCheckBox(label, content_);
    c->setObjectName(name);
    c->setChecked(on);
    into->addWidget(c);
    return c;
  }

  QPushButton* button(const QString& label, const QString& tip, const QString& name) {
    auto* b = new QPushButton(label, content_);
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
  }

  /** The row of symbols: they write into the field that was last used and never take its focus or caret. */
  void symbols(QVBoxLayout* v, const QStringList& list) {
    auto* bar = new QWidget(content_);
    bar->setObjectName("symbols");
    auto* flow = new FlowLayout(bar);
    for (const QString& s : list) {
      auto* b = new QToolButton(bar);
      b->setText(s);
      b->setToolTip("Insertar " + s);
      b->setFocusPolicy(Qt::NoFocus);
      b->setFixedSize(34, 32);
      QFont f = b->font();
      f.setPixelSize(16);
      b->setFont(f);
      connect(b, &QToolButton::clicked, this, [this, s] {
        if (!last_) return;
        last_->insert(s == "′" ? QString("'") : s);
        last_->setFocus();
      });
      flow->addWidget(b);
    }
    v->addWidget(bar);
  }

  void setOut(QLabel* out, QLabel* echo, const QString& expr, const QString& msg, bool error) {
    out->setProperty("err", error);
    out->setText(msg);
    out->setStyleSheet(error ? "color: #ff7a7a;" : "color: #cfcfcf;");
    // where the mistake is: the text typed, with the offending piece marked
    const Parsed p = error ? parseSetExpr(expr) : Parsed{true, nullptr, {}};
    if (error && !p.ok) {
      const int a = std::clamp(p.err.pos, 0, int(expr.size())), b = std::clamp(p.err.pos + std::max(1, p.err.len), a, int(expr.size()));
      const QString mid = a < b ? expr.mid(a, b - a).toHtmlEscaped() : QStringLiteral("&nbsp;&nbsp;");
      echo->setText(QString("<span style='font-family:Consolas,monospace;color:#bbb;'>%1<span style='background:#7a1f1f;color:#fff;text-decoration:underline;'>%2</span>%3</span>")
                        .arg(expr.left(a).toHtmlEscaped(), mid, expr.mid(b).toHtmlEscaped()));
      echo->show();
    } else {
      echo->hide();
    }
  }

  // ------------------------------------------------------------------------------------------------- real line
  LineSpec readLine() const {
    LineSpec s = base_line_;
    s.min = min_->value(); s.max = max_->value(); s.step = step_->value(); s.every = int(every_->value());
    s.ticks = ticks_->isChecked(); s.numbers = nums_->isChecked(); s.arrows = arrows_->isChecked();
    s.brackets = marks_->currentData().toString() == "brackets";
    s.rows = collectRows();
    return sanitizeLine(s);
  }

  std::vector<LineRow> collectRows() const {
    std::vector<LineRow> r;
    for (const Row& row : rows_) r.push_back({row.expr->text(), row.label->text()});
    return r;
  }

  void showLineResults() {
    if (closing_) return;
    const LineSpec spec = readLine();
    const auto res = lineSets(spec);
    for (size_t i = 0; i < res.size() && i < rows_.size(); i++) {
      const bool err = !res[i].error.isEmpty();
      setOut(rows_[i].out, rows_[i].echo, rows_[i].expr->text(), err ? res[i].error : res[i].set ? "= " + formatSet(*res[i].set) : QString(), err);
    }
  }

  void applyLine(const std::optional<std::vector<LineRow>>& rows = std::nullopt, bool restyle = false) {
    if (closing_) return;
    LineSpec spec = readLine();
    if (rows) { spec.rows = *rows; spec = sanitizeLine(spec); }
    state().line = spec;
    state().line.pts = defaultLine().pts;
    const auto cur = selectedMath(ed_);
    if (cur && std::holds_alternative<LineSpec>(cur->spec)) {
      spec.pts = std::get<LineSpec>(cur->spec).pts;   // where the object is now wins over where it was when the panel was built
      if (restyle || specJson(spec) != specJson(cur->spec)) {
        selfEdit_ = true;
        updateMath(ed_, spec, restyle);
        selfEdit_ = false;
      }
    }
    base_line_ = spec;
    if (spec.rows.size() != rows_.size()) { scheduleRebuild(); return; }
    for (auto [box, v] : {std::pair<NumBox*, double>{min_, spec.min}, {max_, spec.max}, {step_, spec.step}, {every_, double(spec.every)}}) {
      QSignalBlocker b(box);
      box->setValue(v);
    }
    showLineResults();
  }

  void buildLine(QVBoxLayout* v) {
    const auto sel = selectedMath(ed_);
    base_line_ = sel && std::holds_alternative<LineSpec>(sel->spec) ? std::get<LineSpec>(sel->spec) : state().line;
    auto* title = new QLabel("<b>Recta real</b>", content_);
    v->addWidget(title);
    v->addWidget(note(content_, "Escribe conjuntos de números y se marcan sobre la recta: (−2, 3] ∪ {5} · [1, ∞) · R ∖ {2}. Vale U, n, inf, oo… Intro aplica."));
    for (size_t i = 0; i < base_line_.rows.size(); i++) {
      const LineRow& row = base_line_.rows[i];
      auto* box = new QWidget(content_);
      auto* bv = new QVBoxLayout(box);
      bv->setContentsMargins(0, 0, 0, 0);
      bv->setSpacing(1);
      auto* top = new QHBoxLayout;
      top->setSpacing(5);
      auto* idx = new QLabel(QString::number(i + 1), box);
      idx->setFixedWidth(16);
      idx->setAlignment(Qt::AlignCenter);
      idx->setStyleSheet("color: #888;");
      Row r;
      r.expr = text(box, row.expr, i == 0 ? "(-2, 3] ∪ {5}" : "[0, ∞)");
      r.expr->setObjectName(QString("expr%1").arg(i));
      r.expr->setStyleSheet("font-size: 14px;");
      r.label = text(box, row.label, "rótulo", 74);
      r.label->setObjectName(QString("label%1").arg(i));
      r.out = outLabel(box);
      r.out->setObjectName(QString("out%1").arg(i));
      r.echo = echoLabel(box);
      top->addWidget(idx);
      top->addWidget(r.expr, 1);
      top->addWidget(r.label);
      if (i > 0) {
        auto* del = new QToolButton(box);
        del->setText("×");
        del->setObjectName(QString("del%1").arg(i));
        del->setToolTip("Quitar esta fila");
        del->setFocusPolicy(Qt::NoFocus);
        del->setFixedSize(28, 28);
        del->setStyleSheet("color: #ee8888; font-size: 15px;");
        connect(del, &QToolButton::clicked, this, [this, i] {
          auto rows = collectRows();
          rows.erase(rows.begin() + long(i));
          applyLine(rows);
        });
        top->addWidget(del);
      }
      bv->addLayout(top);
      bv->addWidget(r.out);
      bv->addWidget(r.echo);
      v->addWidget(box);
      rows_.push_back(r);
      connect(r.expr, &QLineEdit::textChanged, this, [this] { showLineResults(); });
      connect(r.expr, &QLineEdit::editingFinished, this, [this] { applyLine(); });
      connect(r.label, &QLineEdit::editingFinished, this, [this] { applyLine(); });
    }
    auto* add = button("+ Fila", "Otra recta debajo, para comparar (intersecciones, uniones…)", "addRow");
    add->setEnabled(int(base_line_.rows.size()) < MAX_ROWS);
    connect(add, &QPushButton::clicked, this, [this] {
      auto rows = collectRows();
      rows.push_back({QString(), QString()});
      focusRow_ = int(rows.size()) - 1;
      applyLine(rows);
    });
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(add);
    addRow->addStretch(1);
    v->addLayout(addRow);
    symbols(v, kLineSymbols);
    last_ = rows_.front().expr;

    auto* axis = new QGridLayout;
    axis->setHorizontalSpacing(12);
    axis->setVerticalSpacing(4);
    auto num = [&](NumBox*& slot, const QString& label, double value, const QString& name, int r, int c) {
      slot = new NumBox(content_);
      slot->setObjectName(name);
      slot->setValue(value);
      auto* l = new QLabel(label, content_);
      auto* h = new QHBoxLayout;
      h->setSpacing(8);
      l->setFixedWidth(70);
      h->addWidget(l);
      h->addWidget(slot, 1);
      axis->addLayout(h, r, c);
      connect(slot, &QDoubleSpinBox::valueChanged, this, [this] { applyLine(); });
    };
    num(min_, "Desde", base_line_.min, "min", 0, 0);
    num(max_, "Hasta", base_line_.max, "max", 0, 1);
    num(step_, "Paso", base_line_.step, "step", 1, 0);
    num(every_, "Número cada", base_line_.every, "every", 1, 1);
    every_->setDecimals(0);
    every_->setRange(1, 100);
    every_->setValue(base_line_.every);
    v->addLayout(axis);

    marks_ = new QComboBox(content_);
    marks_->setObjectName("marks");
    marks_->addItem("Círculos (abierto ○ / cerrado ●)", "circles");
    marks_->addItem("Corchetes y paréntesis", "brackets");
    marks_->setCurrentIndex(base_line_.brackets ? 1 : 0);
    connect(marks_, &QComboBox::currentIndexChanged, this, [this] { applyLine(); });
    field(v, "Extremos", marks_);

    auto* checks = new QHBoxLayout;
    ticks_ = check(checks, "Marcas", base_line_.ticks, "ticks");
    nums_ = check(checks, "Números", base_line_.numbers, "numbers");
    arrows_ = check(checks, "Flechas", base_line_.arrows, "arrows");
    checks->addStretch(1);
    for (QCheckBox* c : {ticks_, nums_, arrows_}) connect(c, &QCheckBox::toggled, this, [this] { applyLine(); });
    v->addLayout(checks);

    auto* style = button("Estilo del lápiz", "Dar a la recta seleccionada el color, grosor y brillo del lápiz", "restyle");
    connect(style, &QPushButton::clicked, this, [this] { applyLine(std::nullopt, true); });
    auto* sr = new QHBoxLayout;
    sr->addWidget(style);
    sr->addStretch(1);
    v->addLayout(sr);
    showLineResults();
    if (focusRow_ >= 0 && size_t(focusRow_) < rows_.size()) { rows_[size_t(focusRow_)].expr->setFocus(); last_ = rows_[size_t(focusRow_)].expr; }
    focusRow_ = -1;
  }

  // ------------------------------------------------------------------------------------------------- diagrams
  VennSpec readVenn() const {
    VennSpec s = base_venn_;
    for (size_t i = 0; i < names_.size() && i < s.names.size(); i++) s.names[i] = names_[i]->text();
    s.expr = vexpr_->text();
    s.universe = universe_->isChecked();
    s.dense = dense_->isChecked();
    s.lists.clear();
    for (const auto& l : lists_) s.lists.push_back(l->text());
    return sanitizeVenn(s);
  }

  void showVennResult() {
    if (closing_) return;
    const Shading sh = shadedRegions(readVenn());
    setOut(vout_, vecho_, vexpr_->text(), sh.error, !sh.error.isEmpty());
  }

  void applyVenn(const std::function<void(VennSpec&)>& over = nullptr, bool restyle = false) {
    if (closing_) return;
    VennSpec spec = readVenn();
    if (over) over(spec);
    spec = sanitizeVenn(spec);
    state().venn = spec;
    state().venn.pts = defaultVenn(spec.k).pts;
    const auto cur = selectedMath(ed_);
    if (cur && std::holds_alternative<VennSpec>(cur->spec)) {
      if (!over) spec.pts = std::get<VennSpec>(cur->spec).pts;
      if (restyle || specJson(spec) != specJson(cur->spec)) {
        selfEdit_ = true;
        updateMath(ed_, spec, restyle);
        selfEdit_ = false;
      }
    }
    const bool restructure = spec.k != base_venn_.k;
    base_venn_ = spec;
    if (restructure) { scheduleRebuild(); return; }
    showVennResult();
  }

  void buildVenn(QVBoxLayout* v) {
    const auto sel = selectedMath(ed_);
    base_venn_ = sel && std::holds_alternative<VennSpec>(sel->spec) ? std::get<VennSpec>(sel->spec) : state().venn;
    const VennSpec& base = base_venn_;
    v->addWidget(new QLabel("<b>Diagrama de conjuntos</b>", content_));
    v->addWidget(note(content_, "Escribe qué región sombrear con los nombres de los conjuntos: A ∩ B′ · (A ∪ B)′ · A ∖ B · A Δ B. ′ es el complemento (también A^c o ¬A)."));
    k_ = new QComboBox(content_);
    k_->setObjectName("k");
    k_->addItem("2 conjuntos", 2);
    k_->addItem("3 conjuntos", 3);
    k_->setCurrentIndex(base.k == 3 ? 1 : 0);
    field(v, "Conjuntos", k_);

    auto* namesBox = new QWidget(content_);
    auto* nh = new QHBoxLayout(namesBox);
    nh->setContentsMargins(0, 0, 0, 0);
    nh->setSpacing(6);
    names_.clear();
    for (int i = 0; i < base.k; i++) {
      CommitEdit* n = text(namesBox, base.names[size_t(i)], QString(), 46);
      n->setObjectName(QString("name%1").arg(i));
      nh->addWidget(n);
      names_.push_back(n);
      connect(n, &QLineEdit::editingFinished, this, [this] { applyVenn(); });
    }
    nh->addStretch(1);
    field(v, "Nombres", namesBox);

    vexpr_ = text(content_, base.expr, "A ∩ B′");
    vexpr_->setObjectName("vexpr");
    vexpr_->setStyleSheet("font-size: 14px;");
    field(v, "Sombrear", vexpr_);
    vout_ = new QLabel(content_);
    vout_->setObjectName("vout");
    vout_->setWordWrap(true);
    vecho_ = echoLabel(content_);
    v->addWidget(vout_);
    v->addWidget(vecho_);

    const auto& nm = base.names;
    QStringList presets;
    if (base.k == 2)
      presets = {nm[0] + " ∪ " + nm[1], nm[0] + " ∩ " + nm[1], nm[0] + " ∖ " + nm[1], nm[1] + " ∖ " + nm[0], nm[0] + "′", nm[0] + " Δ " + nm[1], "(" + nm[0] + " ∪ " + nm[1] + ")′", "(" + nm[0] + " ∩ " + nm[1] + ")′"};
    else
      presets = {nm[0] + " ∪ " + nm[1] + " ∪ " + nm[2], nm[0] + " ∩ " + nm[1] + " ∩ " + nm[2], nm[0] + " ∩ " + nm[1], "(" + nm[0] + " ∩ " + nm[1] + ") ∪ " + nm[2] + "′",
                 nm[0] + " ∖ (" + nm[1] + " ∪ " + nm[2] + ")", "(" + nm[0] + " ∪ " + nm[1] + " ∪ " + nm[2] + ")′"};
    auto* quick = new QWidget(content_);
    quick->setObjectName("presets");
    auto* flow = new FlowLayout(quick);
    for (const QString& s : presets) {
      auto* b = new QPushButton(s, quick);
      b->setObjectName("preset");
      b->setFocusPolicy(Qt::NoFocus);
      b->setMinimumHeight(28);
      connect(b, &QPushButton::clicked, this, [this, s] { vexpr_->setText(s); applyVenn(); });
      flow->addWidget(b);
    }
    v->addWidget(quick);
    symbols(v, kVennSymbols);
    last_ = vexpr_;

    lists_.clear();
    for (int i = 0; i <= base.k; i++) {
      CommitEdit* t = text(content_, i < int(base.lists.size()) ? base.lists[size_t(i)] : QString(), i < base.k ? "1, 2, 3" : "5, 8");
      t->setObjectName(QString("list%1").arg(i));
      lists_.push_back(t);
      field(v, i < base.k ? "Elementos de " + base.names[size_t(i)] : QStringLiteral("Fuera de todos"), t);
      connect(t, &QLineEdit::editingFinished, this, [this] { applyVenn(); });
    }

    auto* checks = new QHBoxLayout;
    universe_ = check(checks, "Universo U", base.universe, "universe");
    dense_ = check(checks, "Trama densa", base.dense, "dense");
    checks->addStretch(1);
    for (QCheckBox* c : {universe_, dense_}) connect(c, &QCheckBox::toggled, this, [this] { applyVenn(); });
    v->addLayout(checks);
    auto* style = button("Estilo del lápiz", "Dar al diagrama seleccionado el color, grosor y brillo del lápiz", "restyle");
    connect(style, &QPushButton::clicked, this, [this] { applyVenn(nullptr, true); });
    auto* sr = new QHBoxLayout;
    sr->addWidget(style);
    sr->addStretch(1);
    v->addLayout(sr);

    connect(vexpr_, &QLineEdit::textChanged, this, [this] { showVennResult(); });
    connect(vexpr_, &QLineEdit::editingFinished, this, [this] { applyVenn(); });
    connect(k_, &QComboBox::currentIndexChanged, this, [this] {
      const VennSpec was = readVenn();   // still the old k: the shape of the diagram is carried to the new count
      const int kk = k_->currentData().toInt() == 3 ? 3 : 2;
      const double x0 = was.pts[0], y0 = was.pts[1], x1 = was.pts[2], y1 = was.pts[3];
      const VennSpec shaped = defaultVenn(kk, {std::min(x0, x1), std::min(y0, y1)}, {std::max(x0, x1), std::max(y0, y1)});
      applyVenn([&](VennSpec& s) {
        s.k = kk;
        s.pts = shaped.pts;
        std::vector<QString> lists(was.lists.begin(), was.lists.begin() + std::min<long>(long(was.lists.size()), std::min(was.k, kk)));
        while (int(lists.size()) < kk + 1) lists.push_back(QString());
        lists.resize(size_t(kk + 1));
        s.lists = lists;
      });
    });
    showVennResult();
  }

  Editor& ed_;
  bool line_;
  QVBoxLayout* root_ = nullptr;
  QWidget* content_ = nullptr;
  QPointer<QLineEdit> last_;
  bool selfEdit_ = false, dirty_ = true, rebuildQueued_ = false, closing_ = false;
  int focusRow_ = -1;
  // line
  LineSpec base_line_;
  std::vector<Row> rows_;
  NumBox *min_ = nullptr, *max_ = nullptr, *step_ = nullptr, *every_ = nullptr;
  QComboBox* marks_ = nullptr;
  QCheckBox *ticks_ = nullptr, *nums_ = nullptr, *arrows_ = nullptr;
  // venn
  VennSpec base_venn_;
  QComboBox* k_ = nullptr;
  std::vector<QPointer<CommitEdit>> names_, lists_;
  QPointer<CommitEdit> vexpr_;
  QLabel *vout_ = nullptr, *vecho_ = nullptr;
  QCheckBox *universe_ = nullptr, *dense_ = nullptr;
};

}  // namespace

QWidget* makeMathPanel(QWidget* parent, Editor& ed, const QString& kind) { return new MathPanel(parent, ed, kind == "line"); }

}  // namespace mathobj
