// The start screen (welcome.ts): new document, open, recovery copy, recent files, and a card per paper kind with a real thumbnail.
#include "docfactory.h"
#include "icons.h"
#include "pagerender.h"
#include "parts.h"
#include "svgicon.h"
#include "welcomedata.h"
#include <QApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QTimer>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <map>
#include <tuple>

namespace chrome {

QImage paperThumbnail(const QString& value, int width, double dpr) {
  static std::map<std::tuple<QString, int, int>, QImage> kept;   // the page is shown again and again (theme switch, clearing the list): its pictures are drawn once
  const auto key = std::make_tuple(value, width, int(dpr * 100));
  if (const auto it = kept.find(key); it != kept.end()) return it->second;
  Document doc = blankDocument();
  Page& page = doc.pages[0];
  if (value.startsWith("x:")) {
    const QString id = value.mid(2);
    const auto spec = defaultSpec(id);
    const PaperKind* kind = paperKind(id);
    if (spec && kind) { attrSet(page.background, "style", kind->fallback); attrSet(page.background, "xlPaper", formatSpec(*spec)); }
  } else attrSet(page.background, "style", value);
  const int w = int(std::lround(width * dpr)), h = std::max(1, int(std::lround(width * (page.height() / page.width()) * dpr)));
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.setDevicePixelRatio(dpr);
  img.fill(Qt::white);
  {
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = w / page.width();
    p.scale(width / page.width(), (h / dpr) / page.height());
    RenderOptions o;
    o.scale = s;
    renderPage(p, page, o);
  }
  kept[key] = img;
  return img;
}

namespace {
/** Keeps a child (the close button) in the top-right corner of its parent as the parent resizes. */
class GeomPin : public QObject {
 public:
  GeomPin(QWidget* child, QWidget* parent) : QObject(parent), child_(child) {}
 protected:
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() == QEvent::Resize || e->type() == QEvent::Show) { auto* p = static_cast<QWidget*>(o); child_->move(p->width() - child_->width(), 0); }
    return QObject::eventFilter(o, e);
  }
 private:
  QWidget* child_;
};
/** A folder path cut at its start ("…s\user\Downloads"): what `direction: rtl; text-overflow: ellipsis` shows in the Electron list. */
class ElideLeft : public QObject {
 public:
  explicit ElideLeft(QLabel* l) : QObject(l), l_(l) { apply(); }
 protected:
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == l_ && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) apply();
    return QObject::eventFilter(o, e);
  }
 private:
  void apply() { l_->setText(l_->fontMetrics().elidedText(l_->property("full").toString(), Qt::ElideLeft, std::max(0, l_->width()))); }
  QLabel* l_;
};
}  // namespace

WelcomePage::WelcomePage(Chrome& c, QWidget* parent) : QWidget(parent), c_(c) {
  setObjectName("welcome");
  setFocusPolicy(Qt::StrongFocus);
  setAutoFillBackground(false);
  if (parent) { parent->installEventFilter(this); setGeometry(parent->rect()); }
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  scroll_ = new QScrollArea(this);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setWidgetResizable(true);
  scroll_->setStyleSheet("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }");
  scroll_->viewport()->setAutoFillBackground(false);
  lay->addWidget(scroll_);
  recentSource = [this] { return c_.win.recentFiles(); };
  clearRecent = [this] {   // MainWindow owns the list and its menu: use its own "Borrar la lista" entry
    if (QMenu* file = c_.win.menu("file"))
      for (QAction* a : file->actions())
        if (a->menu() && a->text().contains("reciente")) for (QAction* b : a->menu()->actions()) if (b->text() == "Borrar la lista") b->trigger();
  };
  connect(&c.ed, &Editor::documentReplaced, this, [this] { hidePage(); });
  connect(&c, &Chrome::themeChanged, this, [this] { if (isShown()) rebuild(); update(); });
  hide();
}

bool WelcomePage::eventFilter(QObject* o, QEvent* e) {
  if (o == parent() && e->type() == QEvent::Resize) setGeometry(static_cast<QWidget*>(o)->rect());
  return QWidget::eventFilter(o, e);
}

void WelcomePage::showPage() {
  hidden_ = false;
  if (parentWidget()) setGeometry(parentWidget()->rect());
  rebuild();
  show();
  raise();
  if (QPushButton* b = content_->findChild<QPushButton*>("wNew")) b->setFocus();
  c_.win.refreshTitle();   // no document yet: the title bar says just "Xournal Lab"
}

void WelcomePage::hidePage() {
  hidden_ = true;
  hide();
  c_.win.refreshTitle();
  if (c_.win.view()) c_.win.view()->setFocus();
}

void WelcomePage::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { hidePage(); e->accept(); return; }
  QWidget::keyPressEvent(e);
}

void WelcomePage::paintEvent(QPaintEvent*) {
  QPainter p(this);
  if (c_.theme() == "neon") {
    QRadialGradient g(QPointF(width() * 0.5, -height() * 0.1), std::max(width(), height()) * 0.9);
    g.setColorAt(0, "#2b1560"); g.setColorAt(0.55, "#120a26"); g.setColorAt(1, "#0b0618");
    p.fillRect(rect(), QBrush(g));
  } else p.fillRect(rect(), QColor("#333333"));
}

void WelcomePage::rebuild() {
  const bool neonOn = c_.theme() == "neon";
  const Neon& n = neon();
  // the colours and sizes of styles.css (#welcome, .wPage, .wLink, .wRecent, .wPaper...): body text 13 px, links 14.5, h2 19, h1 34
  const QString link = neonOn ? n.cyan.name() : "#6aa6ff";
  const QString text = neonOn ? n.text.name() : "#dddddd";
  auto alpha = [&](const QColor& c, double a) { return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(int(std::lround(a * 255))); };
  const QColor base = neonOn ? n.text : QColor("#dddddd");
  const QString dim = neonOn ? n.purple.name() : alpha(base, 0.65);          // subtitle, tips
  const QString faint = neonOn ? alpha(n.text, 0.55) : alpha(base, 0.55);     // "Aún no has abierto...", shortcut hints
  const QString dirInk = alpha(base, 0.5);
  const QString hoverBg = neonOn ? "rgba(185,103,255,0.16)" : "rgba(255,255,255,0.08)";
  const QString hoverBorder = neonOn ? n.purple.name() : "#555555";
  const QString css = QString(
      "QLabel { color: %1; font-size: 13px; background: transparent; }"
      "QLabel#wTitle { font-size: 34px; font-weight: 600; color: %2; }"
      "QLabel#wSub { font-size: 17px; color: %3; }"
      "QLabel[class=\"h2\"] { font-size: 19px; font-weight: 600; color: %4; }"
      "QLabel[class=\"dim\"] { color: %5; }"
      "QPushButton[class=\"wLink\"] { min-height: 31px; max-height: 31px; } QPushButton[class=\"wRecent\"] { min-height: 25px; max-height: 25px; } QPushButton[class=\"wSmall\"] { min-height: 26px; max-height: 26px; }"
      "QPushButton[class=\"wPaper\"] { min-height: 0; } QPushButton#wClose { min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px; }"
      "QPushButton[class=\"wLink\"], QPushButton[class=\"wRecent\"], QPushButton[class=\"wSmall\"] { background: transparent; color: %6; border: 1px solid transparent; border-radius: 6px; text-align: left; padding: 0; }"
      "QPushButton[class=\"wLink\"]:hover, QPushButton[class=\"wRecent\"]:hover, QPushButton[class=\"wSmall\"]:hover { background: %7; border-color: %8; }"
      "QPushButton[class=\"wLink\"]:focus, QPushButton[class=\"wRecent\"]:focus { border: 2px solid #ffffff; }"
      "QPushButton[class=\"wSmall\"] { color: %1; font-size: 12px; padding: 0 8px; }"
      "QPushButton[class=\"wPaper\"] { background: %9; color: %1; border: 1px solid %10; border-radius: 8px; padding: 0; }"
      "QPushButton[class=\"wPaper\"]:hover, QPushButton[class=\"wPaper\"]:focus { border-color: %11; background: %7; }"
      "QPushButton#wClose { background: transparent; color: %1; border: 1px solid transparent; border-radius: 6px; font-size: 16px; padding: 0; }"
      "QPushButton#wClose:hover { background: rgba(255,255,255,0.1); border-color: #666666; }"
      "QCheckBox { color: %1; font-size: 13.5px; spacing: 8px; }")
      .arg(text, neonOn ? n.cyan.name() : "#eeeeee", dim, neonOn ? n.pink.name() : "#dddddd", faint, link, hoverBg, hoverBorder,
           neonOn ? "#170c36" : "rgba(255,255,255,0.04)", neonOn ? n.line.name() : "#555555", neonOn ? n.pink.name() : "#8ab4f8");

  delete content_;
  content_ = new QWidget;
  content_->setStyleSheet(css);
  auto* outer = new QHBoxLayout(content_);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* page = new QWidget(content_);
  page->setMaximumWidth(980);   // .wPage { max-width: 980px; margin: 0 auto; padding: 56px 32px 40px }
  outer->addStretch(1);
  outer->addWidget(page, 100);
  outer->addStretch(1);
  auto* root = new QVBoxLayout(page);
  root->setContentsMargins(32, 56, 32, 40);
  root->setSpacing(0);

  // header: h1 + subtitle, and the close button (only while there is a document under the page)
  auto* headBox = new QWidget(page);
  auto* head = new QVBoxLayout(headBox);
  head->setContentsMargins(0, 0, 0, 0);
  head->setSpacing(4);
  auto* h1 = new QLabel("Xournal Lab", headBox); h1->setObjectName("wTitle");
  auto* sub = new QLabel("Notas a mano, matemáticas y física", headBox); sub->setObjectName("wSub");
  head->addWidget(h1); head->addWidget(sub);
  auto* close = new QPushButton(QStringLiteral("✕"), headBox);
  close->setObjectName("wClose");
  close->setToolTip("Cerrar la bienvenida (Esc)");
  close->setFixedSize(32, 32);
  close->setCursor(Qt::PointingHandCursor);
  const bool hasDocument = !(c_.win.fileName().isEmpty() && !c_.ed.dirty() && c_.ed.undoDepth() == 0);   // Electron shows the button only over a document
  close->setVisible(hasDocument);
  close->move(0, 0);
  close->raise();
  connect(close, &QPushButton::clicked, this, [this] { hidePage(); });
  headBox->installEventFilter(new GeomPin(close, headBox));   // keeps the button at the right edge of the header
  root->addWidget(headBox);
  root->addSpacing(26);

  auto h2 = [&](const QString& t, bool first) {
    auto* l = new QLabel(t, page);
    l->setProperty("class", "h2");
    l->setIndent(0);   // a label with a style sheet padding gets a default indent otherwise
    l->setStyleSheet(QString("padding-top: %1px; padding-bottom: 10px; padding-left: 0px; padding-right: 0px;").arg(first ? 0 : 22));   // .wCol h2 { margin: 22px 0 10px } (the first one has no top margin); a style sheet resets setContentsMargins, so padding
    return l;
  };
  const SvgLook look = [&] {
    SvgLook l;
    l.ink = QColor(link);
    l.buttonBg = Qt::transparent;
    l.strokeWidth = neonOn ? 1.9 : 1.7;
    l.halo = neonOn && !c_.reducedEffects();
    return l;
  }();
  auto linkBtn = [&](const QString& obj, const QString& svg, const QString& label, const QString& hint, std::function<void()> run) {
    auto* b = new QPushButton(page);
    b->setObjectName(obj);
    b->setProperty("class", "wLink");
    b->setCursor(Qt::PointingHandCursor);
    auto* hl = new QHBoxLayout(b);
    hl->setContentsMargins(8, 6, 8, 6);   // padding 6px 8px; gap 10px
    hl->setSpacing(10);
    auto* ic = new QLabel(b);
    ic->setPixmap(svgIcon(svg, look).pixmap(QSize(20, 20), devicePixelRatioF()));
    ic->setFixedSize(20, 20);
    auto* tx = new QLabel(label, b);
    tx->setStyleSheet("font-size: 14.5px; color: " + link + ";");
    hl->addWidget(ic); hl->addWidget(tx, 1);
    if (!hint.isEmpty()) { auto* k = new QLabel(hint, b); k->setStyleSheet("font-size: 12px; color: " + faint + ";"); hl->addWidget(k); }
    for (QLabel* l : b->findChildren<QLabel*>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(b, &QPushButton::clicked, this, [run = std::move(run)] { run(); });
    return b;
  };

  auto* cols = new QHBoxLayout;
  cols->setSpacing(44);   // .wCols { grid-template-columns: minmax(260px, 1fr) minmax(320px, 1.4fr); gap: 44px }
  auto* left = new QVBoxLayout;
  left->setSpacing(2);    // .wLink margin 1px 0
  auto* right = new QVBoxLayout;
  right->setSpacing(0);
  cols->addLayout(left, 10);
  cols->addLayout(right, 14);
  root->addLayout(cols);

  Editor& ed = c_.ed;
  left->addWidget(h2("Empezar", true));
  left->addWidget(linkBtn("wNew", "pageAdd", "Nuevo documento", "Ctrl+N", [this, &ed] { ed.action("file.new")->trigger(); }));
  left->addWidget(linkBtn("wOpen", "open", "Abrir archivo…", "Ctrl+O", [&ed] { ed.action("file.open")->trigger(); }));
  left->addWidget(linkBtn("wAutosave", "undo", "Abrir copia de recuperación…", "", [&ed] { ed.action("file.openAutosave")->trigger(); }));
  left->addWidget(h2("Recientes", false));
  const std::vector<RecentFile> recent = existingRecent(recentSource ? recentSource() : QStringList());
  if (recent.empty()) {
    auto* none = new QLabel("Aún no has abierto ningún archivo.", page);
    none->setProperty("class", "dim");
    none->setIndent(0);
    none->setStyleSheet("padding: 4px 8px;");   // .wDim { margin: 4px 8px }
    left->addWidget(none);
  }
  int shown = 0;
  for (const RecentFile& f : recent) {
    if (shown++ >= 8) break;
    auto* b = new QPushButton(page);
    b->setObjectName("wRecent:" + QString::number(shown));
    b->setProperty("class", "wRecent");
    b->setProperty("path", f.path);
    b->setToolTip(f.path);
    b->setCursor(Qt::PointingHandCursor);
    auto* hl = new QHBoxLayout(b);
    hl->setContentsMargins(8, 5, 8, 5);   // padding 5px 8px; gap 12px; name 13px in the link colour, folder 12.5px at half strength, cut at the left
    hl->setSpacing(12);
    auto* nm = new QLabel(f.name, b); nm->setStyleSheet("font-size: 13px; color: " + link + ";");
    auto* dir = new QLabel(b); dir->setStyleSheet("font-size: 12.5px; color: " + dirInk + ";");
    dir->setProperty("full", f.dir);
    dir->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    dir->installEventFilter(new ElideLeft(dir));
    hl->addWidget(nm); hl->addWidget(dir, 1);
    for (QLabel* l : b->findChildren<QLabel*>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QString path = f.path;
    connect(b, &QPushButton::clicked, this, [this, path] { if (c_.win.maybeSave()) c_.win.openFile(path); });
    left->addWidget(b);
  }
  if (!recent.empty()) {
    auto* clear = new QPushButton("Borrar la lista", page);
    clear->setObjectName("wClear");
    clear->setProperty("class", "wSmall");
    clear->setCursor(Qt::PointingHandCursor);
    clear->setFixedHeight(28);
    connect(clear, &QPushButton::clicked, this, [this] { if (clearRecent) clearRecent(); QTimer::singleShot(0, this, [this] { rebuild(); }); });
    left->addSpacing(6);
    left->addWidget(clear, 0, Qt::AlignLeft);
  }
  left->addStretch(1);

  right->addWidget(h2("Empezar con un papel", true));
  auto* grid = new QGridLayout;
  grid->setSpacing(12);
  grid->setContentsMargins(0, 0, 0, 0);
  std::vector<std::pair<QString, QString>> special;
  for (const PaperKind& k : paperKinds()) special.emplace_back(k.id, k.label);
  int i = 0;
  const double dpr = devicePixelRatioF();
  for (const StartPaper& sp : startPapers(special)) {
    auto* card = new QPushButton(page);
    card->setObjectName("wPaper:" + sp.value);
    card->setProperty("class", "wPaper");
    card->setToolTip("Nuevo documento con papel " + sp.label.toLower());
    card->setCursor(Qt::PointingHandCursor);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(6, 10, 6, 8);   // padding 10px 6px 8px; gap 6px
    cl->setSpacing(6);
    auto* pic = new QLabel(card);
    pic->setPixmap(QPixmap::fromImage(paperThumbnail(sp.value, 66, dpr)));
    pic->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* cap = new QLabel(sp.label, card);
    cap->setStyleSheet("font-size: 12.5px;");
    cap->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    cap->setWordWrap(true);
    cl->addWidget(pic); cl->addWidget(cap);
    cl->addStretch(1);
    for (QLabel* l : card->findChildren<QLabel*>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    card->setMinimumSize(96, int(66 * 1.414) + 10 + 8 + 6 + 17);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    const QString value = sp.value;
    connect(card, &QPushButton::clicked, this, [this, value] { hidePage(); c_.newWithPaper(value); });
    grid->addWidget(card, i / 4, i % 4);
    i++;
  }
  for (int col = 0; col < 4; col++) grid->setColumnStretch(col, 1);
  right->addLayout(grid);
  right->addWidget(h2("Apariencia", false));
  right->addWidget(linkBtn("wTheme", "theme", QString("Tema: %1 — cambiar").arg(c_.theme() == "neon" ? "Neon (synthwave)" : "Clásico"), "", [this] {
    c_.chooseTheme(c_.theme() == "neon" ? "classic" : "neon");
  }));
  auto* tips = new QLabel(page);
  tips->setWordWrap(true);
  tips->setIndent(0);
  tips->setStyleSheet("color: " + dim + "; font-size: 13px; padding: 14px 0 0 0;");   // .wTips { margin: 14px 0 0; padding-left: 18px; font-size: 13px; line-height: 1.7 }
  tips->setTextFormat(Qt::RichText);
  tips->setText("<ul style='margin-top: 0; margin-bottom: 0; margin-left: 0; -qt-list-indent: 1;'><li style='line-height:22px'>P lápiz · H resaltador · E goma · U relleno · X fórmula LaTeX</li>"
                "<li style='line-height:22px'>Arrastra con el relleno una línea para pintar a partir de ella o hacer sumas de Riemann</li>"
                "<li style='line-height:22px'>Los papeles de matemáticas y física están en Diario → Fondo de página → Tipo de papel</li></ul>");
  right->addWidget(tips);
  right->addStretch(1);

  root->addSpacing(34);   // .wFoot { margin-top: 34px }
  auto* show = new QCheckBox("Mostrar la bienvenida al iniciar", page);
  show->setObjectName("wShow");
  show->setChecked(c_.showWelcomeOnStart());
  connect(show, &QCheckBox::toggled, this, [this](bool on) { c_.setShowWelcomeOnStart(on); });
  root->addWidget(show);
  root->addStretch(1);
  scroll_->setWidget(content_);
}

}  // namespace chrome
