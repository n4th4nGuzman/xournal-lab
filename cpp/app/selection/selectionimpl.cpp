#include "texttools/texttools.h"
#include "selectionimpl.h"
#include "documentview.h"
#include "pagerender.h"
#include "xopp.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QPainterPath>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <cmath>

namespace selx {

namespace {

constexpr double kClickSlop = 3;     // page units: smaller rectangles are clicks
constexpr double kHitPad = 4;
constexpr double kLiftSlopPx = 3;    // a drag below this is still a click on the frame: nothing is lifted
constexpr int kMaxBitmapPx = 4096;
constexpr double kPi = 3.14159265358979323846;
const char* kMime = "application/x-xournal-lab-elements";
QColor kBlue(0x2f, 0x6f, 0xd0);   // the selection's colour (Preferencias > Ver can change it: setSelectionColor)

bool finite(const Rect& r) { return std::isfinite(r[0]) && std::isfinite(r[1]) && std::isfinite(r[2]) && std::isfinite(r[3]); }
QRectF toQ(const Rect& r) { return QRectF(QPointF(r[0], r[1]), QPointF(r[2], r[3])); }
Rect fromQ(const QRectF& r) { return {r.left(), r.top(), r.right(), r.bottom()}; }
QTransform toTransform(const Aff& m) { return QTransform(m.a, m.b, m.c, m.d, m.e, m.f); }

void handlePoint(Handle h, const Rect& b, double zoom, double* ox, double* oy) {
  const double mx = (b[0] + b[2]) / 2, my = (b[1] + b[3]) / 2;
  double x, y;
  if (h == Handle::Rot) { x = mx; y = b[1] - kRotOffsetPx / zoom; }
  else {
    const bool w = h == Handle::NW || h == Handle::W || h == Handle::SW, e = h == Handle::NE || h == Handle::E || h == Handle::SE;
    const bool n = h == Handle::NW || h == Handle::N || h == Handle::NE, s = h == Handle::SW || h == Handle::S || h == Handle::SE;
    x = w ? b[0] : e ? b[2] : mx;
    y = n ? b[1] : s ? b[3] : my;
  }
  *ox = x;
  *oy = y;
}
bool west(Handle h) { return h == Handle::NW || h == Handle::W || h == Handle::SW; }
bool east(Handle h) { return h == Handle::NE || h == Handle::E || h == Handle::SE; }
bool north(Handle h) { return h == Handle::NW || h == Handle::N || h == Handle::NE; }
bool south(Handle h) { return h == Handle::SW || h == Handle::S || h == Handle::SE; }
bool corner(Handle h) { return (west(h) || east(h)) && (north(h) || south(h)); }

/** The fixed point opposite a handle: the far corner, or the far edge's midpoint. */
void anchorOf(Handle h, const Rect& b, double& x, double& y) {
  const double mx = (b[0] + b[2]) / 2, my = (b[1] + b[3]) / 2;
  x = west(h) ? b[2] : east(h) ? b[0] : mx;
  y = north(h) ? b[3] : south(h) ? b[1] : my;
}
double clampMag(double v) { return std::abs(v) < kMinScale ? (v < 0 ? -kMinScale : kMinScale) : v; }

bool isIdentity(const Aff& m) {
  const double e = 1e-9;
  return std::abs(m.a - 1) < e && std::abs(m.d - 1) < e && std::abs(m.b) < e && std::abs(m.c) < e && std::abs(m.e) < 1e-4 && std::abs(m.f) < 1e-4;
}

QCursor cursorFor(Handle h) {
  switch (h) {
    case Handle::NW: case Handle::SE: return Qt::SizeFDiagCursor;
    case Handle::NE: case Handle::SW: return Qt::SizeBDiagCursor;
    case Handle::N: case Handle::S: return Qt::SizeVerCursor;
    case Handle::E: case Handle::W: return Qt::SizeHorCursor;
    case Handle::Rot: return Qt::OpenHandCursor;
    default: return Qt::ArrowCursor;
  }
}

bool visibleLayer(Editor& ed, int page, int layer) { return ed.layerVisible(page, layer); }

/** A label as a path (QPainter fonts only take whole pixel sizes, and the painter is scaled to page units). */
void drawLabel(QPainter& p, const QPointF& centre, const QString& text, double sizePx, double pxUnit, const QColor& fill, bool bold, bool outline) {
  QFont f(QStringLiteral("Segoe UI"));
  f.setPixelSize(100);
  f.setBold(bold);
  QPainterPath path;
  path.addText(0, 0, f, text);
  const QRectF br = path.boundingRect();
  const double s = sizePx * pxUnit / 100.0;
  QTransform t;
  t.translate(centre.x(), centre.y());
  t.scale(s, s);
  t.translate(-br.center().x(), -br.center().y());
  path = t.map(path);
  if (outline) { p.setPen(QPen(QColor(255, 255, 255, 242), 4 * pxUnit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush); p.drawPath(path); }
  p.setPen(Qt::NoPen);
  p.setBrush(fill);
  p.drawPath(path);
}

}  // namespace

void setSelectionColor(const QColor& c) { kBlue = c.isValid() ? c : QColor(0x2f, 0x6f, 0xd0); }

// ============================================================================================================================================
RealSelection::RealSelection(Editor& ed) : ed_(ed) {}
RealSelection::~RealSelection() = default;

const Element& RealSelection::elementAt(const ElementRef& r) const { return ed_.doc().pages[size_t(r.page)].layers[size_t(r.layer)].elements[size_t(r.index)]; }
Element& RealSelection::elementAt(const ElementRef& r) { return ed_.doc().pages[size_t(r.page)].layers[size_t(r.layer)].elements[size_t(r.index)]; }

uint64_t RealSelection::fingerprint(const Element& e) {
  // Geometry is left out on purpose: it changes with every transform, and undo/redo of a transform must keep the element selected.
  uint64_t h = uint64_t(e.index()) * 0x9e3779b97f4a7c15ull;
  auto mix = [&h](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
  if (const auto* s = std::get_if<Stroke>(&e)) { mix(s->pointCount()); mix(qHash(s->color())); mix(qHash(s->tool())); }
  else if (const auto* t = std::get_if<Text>(&e)) { mix(qHash(t->text)); mix(qHash(attrOr(t->attrs, "font"))); }
  else if (const auto* i = std::get_if<Image>(&e)) { mix(qHash(i->tag)); mix(uint64_t(i->data.size())); }
  return h;
}

// ---- selection state ---------------------------------------------------------------------------------------------------------------------
const Rect& RealSelection::box() const {
  if (boxValid_) return box_;
  std::optional<Rect> u;
  for (const ElementRef& r : items_) {
    const Rect b = elementBBox(elementAt(r));
    if (finite(b)) u = unionRect(u, b);
  }
  box_ = u ? *u : Rect{INFINITY, INFINITY, -INFINITY, -INFINITY};
  boxValid_ = true;
  return box_;
}

QRectF RealSelection::bbox() const {
  if (items_.empty()) return {};
  const Rect& b = box();
  return finite(b) ? toQ(b) : QRectF();
}

QRectF RealSelection::frameArea(const Rect& b, double zoom) const {
  const double pad = (kHandlePx + 4) / zoom;
  QRectF r = toQ(b).adjusted(-pad, -pad - kRotOffsetPx / zoom, pad, pad);
  return r;
}

void RealSelection::invalidate(int page, const QRectF& r) const {
  if (page < 0 || r.isNull()) return;
  if (DocumentView* v = ed_.view()) v->invalidateOverlay(page, r);
}

void RealSelection::changed(const QRectF& oldArea) {
  const double z = ed_.view() ? ed_.view()->zoom() : 1.0;
  invalidate(page_, oldArea);
  if (!items_.empty()) { const Rect& b = box(); if (finite(b)) invalidate(page_, frameArea(b, z)); }
  ed_.notifySelectionChanged();
}

void RealSelection::expandPlanes(int page, std::vector<ElementRef>& items) const {
  std::set<std::pair<int, QString>> ids;
  for (const ElementRef& r : items) if (const auto id = planeIdOf(elementAt(r))) ids.insert({r.layer, *id});
  if (ids.empty()) return;
  for (const auto& [layer, id] : ids)
    for (const ElementRef& m : ed_.membersOf(page, layer, id)) items.push_back(m);
}

void RealSelection::setItems(int page, std::vector<ElementRef> items, bool expand) {
  page_ = page;
  if (expand) expandPlanes(page, items);
  std::sort(items.begin(), items.end(), [](const ElementRef& a, const ElementRef& b) { return a.layer != b.layer ? a.layer < b.layer : a.index < b.index; });
  items.erase(std::unique(items.begin(), items.end()), items.end());
  items_ = std::move(items);
  recompute();
}

void RealSelection::recompute() {
  boxValid_ = false;
  fp_.clear();
  fp_.reserve(items_.size());
  planes_.clear();
  gen_ = {};
  anchors_.clear();
  size_t withId = 0;
  for (const ElementRef& r : items_) {
    const Element& e = elementAt(r);
    fp_.push_back(fingerprint(e));
    if (const auto id = planeIdOf(e)) { planes_.insert({r.layer, *id}); withId++; }
  }
  if (planes_.size() != 1 || withId != items_.size()) return;
  const auto& [layer, id] = *planes_.begin();
  if (int(ed_.membersOf(page_, layer, id).size()) != int(items_.size())) return;
  ElementPtrs ptrs;
  ptrs.reserve(items_.size());
  for (const ElementRef& r : items_) ptrs.push_back(&elementAt(r));
  if (const GeneratedHandler* h = GeneratedRegistry::instance().find(ptrs)) {
    gen_ = {h, id, layer};
    anchors_ = h->anchors(ptrs);
  }
}

void RealSelection::clear() {
  stopGhost();
  if (drag_.lift.done && page_ >= 0 && page_ < ed_.pageCount()) restoreLift(drag_);
  const bool had = !items_.empty() || drag_.mode != Mode::None;
  const int page = page_;
  QRectF area = drag_.dirty;
  if (!items_.empty() && ed_.view()) { const Rect& b = box(); if (finite(b)) area = area.united(frameArea(b, ed_.view()->zoom())); }
  items_.clear(); fp_.clear(); planes_.clear(); gen_ = {}; anchors_.clear();
  page_ = -1;
  boxValid_ = false;
  drag_ = Drag();
  if (had) { invalidate(page, area); ed_.notifySelectionChanged(); }
}

void RealSelection::select(int page, const std::vector<ElementRef>& items) {
  if (page < 0 || page >= ed_.pageCount()) { clear(); return; }
  QRectF old;
  if (!items_.empty() && ed_.view() && page_ == page) { const Rect& b = box(); if (finite(b)) old = frameArea(b, ed_.view()->zoom()); }
  const int oldPage = page_;
  std::vector<ElementRef> valid;
  for (const ElementRef& r : items) {
    if (r.page != page || r.layer < 0 || r.layer >= int(ed_.doc().pages[size_t(page)].layers.size())) continue;
    if (r.index < 0 || r.index >= int(ed_.doc().pages[size_t(page)].layers[size_t(r.layer)].elements.size())) continue;
    valid.push_back(r);
  }
  if (oldPage >= 0 && oldPage != page) invalidate(oldPage, QRectF(0, 0, 1e5, 1e5));
  drag_ = Drag();
  setItems(page, std::move(valid), true);
  changed(old);
  reveal();
}

bool RealSelection::contains(const ElementRef& e) const {
  if (e.page != page_) return false;
  return std::binary_search(items_.begin(), items_.end(), e, [](const ElementRef& a, const ElementRef& b) { return a.layer != b.layer ? a.layer < b.layer : a.index < b.index; });
}

void RealSelection::onSplices(int page, int layer, const std::vector<ElementSplice>& sp) {
  // The references follow their elements through every edit the Editor reports, so undo, redo and any other edit of the page cannot leave them on
  // a neighbour that merely looks alike. Nothing here is O(elements of the page): a stroke appended after the selection is a binary search.
  if (own_ || drag_.lift.done || items_.empty() || page != page_ || sp.empty()) return;
  const auto before = [](const ElementRef& a, const ElementRef& b) { return a.layer != b.layer ? a.layer < b.layer : a.index < b.index; };
  const auto lo = std::lower_bound(items_.begin(), items_.end(), ElementRef{page, layer, 0}, before);
  const auto hi = std::lower_bound(lo, items_.end(), ElementRef{page, layer + 1, 0}, before);
  if (lo == hi || (hi - 1)->index < sp.front().at) return;   // nothing of this layer at or after the first edit
  const size_t first = size_t(lo - items_.begin()), last = size_t(hi - items_.begin());
  int shift = 0;
  size_t s = 0;
  bool lost = false;
  std::vector<char> dead(last - first, 0);
  for (size_t i = first; i < last; i++) {
    int& idx = items_[i].index;
    while (s < sp.size() && sp[s].at + sp[s].removed <= idx) { shift += sp[s].inserted - sp[s].removed; s++; }
    if (s < sp.size() && sp[s].at <= idx) {   // inside a replaced run: an in-place edit keeps the element, anything else removed it
      if (sp[s].removed == sp[s].inserted) idx += shift; else { dead[i - first] = 1; lost = true; }
    } else idx += shift;
  }
  if (!lost) return;
  const bool hasFp = fp_.size() == items_.size();   // the vertical-space gesture holds its items without fingerprints
  size_t w = first;
  for (size_t i = first; i < items_.size(); i++) {
    if (i < last && dead[i - first]) continue;
    items_[w] = items_[i];
    if (hasFp) fp_[w] = fp_[i];
    w++;
  }
  items_.resize(w);
  if (hasFp) fp_.resize(w);
}

void RealSelection::refresh() {
  if (drag_.lift.done || (items_.empty() && planes_.empty())) return;
  if (page_ < 0 || page_ >= ed_.pageCount()) { clear(); return; }
  const Page& pg = ed_.doc().pages[size_t(page_)];
  QRectF old;
  if (ed_.view() && boxValid_) { if (finite(box_)) old = frameArea(box_, ed_.view()->zoom()); }   // the frame as it was drawn, before the edit moved things
  const auto planes = planes_;
  std::vector<ElementRef> keep;
  // positions were followed through the edits (onSplices); the fingerprint only guards against edits that did not go through the Editor's primitives
  for (size_t i = 0; i < items_.size(); i++) {
    const ElementRef& r = items_[i];
    if (r.layer >= int(pg.layers.size()) || r.index >= int(pg.layers[size_t(r.layer)].elements.size())) continue;
    if (fingerprint(pg.layers[size_t(r.layer)].elements[size_t(r.index)]) != fp_[i]) continue;
    keep.push_back(r);
  }
  // A generated object that was rebuilt (undo or redo of an edit to it) stays selected as the object it is.
  for (const auto& [layer, id] : planes)
    if (layer < int(pg.layers.size())) for (const ElementRef& m : ed_.membersOf(page_, layer, id)) keep.push_back(m);
  if (keep.empty()) { clear(); return; }
  setItems(page_, std::move(keep), false);
  changed(old);
  reveal();
}

// ---- hit testing the frame ---------------------------------------------------------------------------------------------------------------
Handle RealSelection::handleAt(double x, double y, const Rect& b, double zoom) const {
  const double tol = kHandlePx * grab_ / zoom;   // a pen or a finger needs a bigger target than a mouse (grab_ follows the device of the last press)
  static const Handle order[] = {Handle::NW, Handle::N, Handle::NE, Handle::E, Handle::SE, Handle::S, Handle::SW, Handle::W, Handle::Rot};
  for (Handle h : order) {
    double hx, hy;
    handlePoint(h, b, zoom, &hx, &hy);
    if (std::abs(x - hx) <= tol && std::abs(y - hy) <= tol) return h;
  }
  return Handle::None;
}

int RealSelection::anchorAt(double x, double y, double zoom) const {
  if (!gen_.h) return -1;
  const double tol = gen_.h->anchorHitPx() / zoom;
  for (size_t i = 0; i < anchors_.size(); i++)
    if (std::abs(x - anchors_[i].x()) <= tol && std::abs(y - anchors_[i].y()) <= tol) return int(i);
  return -1;
}

bool RealSelection::wouldHit(int page, const QPointF& pos, double zoom) const {
  if (items_.empty() || page != page_) return false;
  const Rect& b = box();
  if (!finite(b)) return false;
  if (anchorAt(pos.x(), pos.y(), zoom) >= 0 || handleAt(pos.x(), pos.y(), b, zoom) != Handle::None) return true;
  return pointInRect(pos.x(), pos.y(), {b[0] - 2, b[1] - 2, b[2] + 2, b[3] + 2});
}

QCursor RealSelection::cursorAt(int page, const QPointF& pos, double zoom) const {
  if (items_.empty() || page != page_) return Qt::ArrowCursor;
  const Rect& b = box();
  if (!finite(b)) return Qt::ArrowCursor;
  if (anchorAt(pos.x(), pos.y(), zoom) >= 0) return Qt::PointingHandCursor;
  const Handle h = handleAt(pos.x(), pos.y(), b, zoom);
  if (h != Handle::None) return cursorFor(h);
  return pointInRect(pos.x(), pos.y(), b) ? QCursor(Qt::SizeAllCursor) : QCursor(Qt::ArrowCursor);
}

// ---- gestures ----------------------------------------------------------------------------------------------------------------------------
void RealSelection::down(const PointerEvent& e, bool lasso, bool newSelection) {
  grab_ = e.kind == PointerEvent::Kind::Touch ? 2.5 : (e.kind == PointerEvent::Kind::Pen || e.kind == PointerEvent::Kind::Eraser) ? 1.5 : 1.0;
  if (e.page < 0 || e.page >= ed_.pageCount()) return;
  if (drag_.mode != Mode::None) cancel();
  stopGhost();
  if (!items_.empty() && e.page == page_ && !newSelection) {
    const Rect b = box();
    if (finite(b)) {
      const int a = anchorAt(e.pos.x(), e.pos.y(), e.zoom);
      if (a >= 0) { startAnchor(e, a); return; }
      const Handle h = handleAt(e.pos.x(), e.pos.y(), b, e.zoom);
      if (h == Handle::Rot) { startRotate(e, b); return; }
      if (h != Handle::None) { startResize(e, h, b); return; }
      if (pointInRect(e.pos.x(), e.pos.y(), {b[0] - 2, b[1] - 2, b[2] + 2, b[3] + 2})) { startMove(e, b); return; }
    }
  }
  // a new selection: the old one is let go at once, like the TS
  QRectF old;
  if (!items_.empty() && ed_.view()) { const Rect& b = box(); if (finite(b)) old = frameArea(b, e.zoom); }
  const int oldPage = page_;
  items_.clear(); fp_.clear(); planes_.clear(); gen_ = {}; anchors_.clear();
  boxValid_ = false;
  page_ = e.page;
  drag_ = Drag();
  drag_.mode = lasso ? Mode::Lasso : Mode::Rect;
  drag_.page = e.page;
  drag_.zoom = e.zoom;
  drag_.down = e.pos;
  drag_.x1 = e.pos.x(); drag_.y1 = e.pos.y();
  if (lasso) { drag_.poly = {e.pos.x(), e.pos.y()}; drag_.polyBox = {e.pos.x(), e.pos.y(), e.pos.x(), e.pos.y()}; }
  invalidate(oldPage, old);
  ed_.notifySelectionChanged();
}

void RealSelection::startMove(const PointerEvent& e, const Rect& b) {
  drag_ = Drag();
  drag_.mode = Mode::Move; drag_.page = e.page; drag_.zoom = e.zoom; drag_.down = e.pos; drag_.bbox = b; drag_.m = translateAff(0, 0);
}

void RealSelection::startResize(const PointerEvent& e, Handle h, const Rect& b) {
  drag_ = Drag();
  drag_.mode = Mode::Resize; drag_.page = e.page; drag_.zoom = e.zoom; drag_.down = e.pos; drag_.bbox = b; drag_.handle = h;
  anchorOf(h, b, drag_.ax, drag_.ay);
  bool hasText = false;
  for (const ElementRef& r : items_) {
    const Element& el = elementAt(r);
    if (std::holds_alternative<Text>(el) || (std::holds_alternative<Image>(el) && isTexImage(std::get<Image>(el)))) { hasText = true; break; }
  }
  drag_.uniform = corner(h) || hasText;   // text keeps its shape, so a selection with text scales in proportion
  drag_.m = scaleAff(1, 1, drag_.ax, drag_.ay);
}

void RealSelection::startRotate(const PointerEvent& e, const Rect& b) {
  drag_ = Drag();
  drag_.mode = Mode::Rotate; drag_.page = e.page; drag_.zoom = e.zoom; drag_.down = e.pos; drag_.bbox = b;
  drag_.cx = (b[0] + b[2]) / 2; drag_.cy = (b[1] + b[3]) / 2;
  drag_.a0 = drag_.prev = std::atan2(e.pos.y() - drag_.cy, e.pos.x() - drag_.cx);
  drag_.r0px = std::max(kProtractorMinRadiusPx, std::hypot(e.pos.x() - drag_.cx, e.pos.y() - drag_.cy) * e.zoom);
  drag_.m = rotateAff(0, drag_.cx, drag_.cy);
}

void RealSelection::startAnchor(const PointerEvent& e, int i) {
  drag_ = Drag();
  drag_.mode = Mode::Anchor; drag_.page = e.page; drag_.zoom = e.zoom; drag_.down = e.pos; drag_.anchorI = i;
  drag_.bbox = box();
}

void RealSelection::startVspace(const PointerEvent& e) {
  if (e.page < 0 || e.page >= ed_.pageCount()) return;
  if (drag_.mode != Mode::None) cancel();
  const Page& pg = ed_.doc().pages[size_t(e.page)];
  const double y = e.pos.y();
  std::vector<ElementRef> refs;
  std::optional<Rect> u;
  for (size_t l = 0; l < pg.layers.size(); l++) {
    if (!visibleLayer(ed_, e.page, int(l))) continue;
    const auto& els = pg.layers[l].elements;
    for (size_t i = 0; i < els.size(); i++) {
      const Rect b = elementBBox(els[i]);
      if (finite(b) && b[1] >= y) { refs.push_back({e.page, int(l), int(i)}); u = unionRect(u, b); }
    }
  }
  if (!u) { ed_.notify(QStringLiteral("No hay nada debajo de esa línea")); return; }
  items_ = std::move(refs);
  fp_.clear(); planes_.clear(); gen_ = {}; anchors_.clear();
  page_ = e.page;
  boxValid_ = false;
  drag_ = Drag();
  drag_.mode = Mode::Move; drag_.page = e.page; drag_.zoom = e.zoom; drag_.down = e.pos; drag_.bbox = *u; drag_.m = translateAff(0, 0);
  drag_.vspace = true; drag_.vy = y; drag_.minDy = y - (*u)[1]; drag_.maxDy = pg.height() - (*u)[3]; drag_.pageW = pg.width();
}

void RealSelection::lift(Drag& d, const Rect& bbox) {
  Lift& L = d.lift;
  // pad like the tile renderer does so a glow is not cut
  double pad = 2;
  for (const ElementRef& r : items_)
    if (const auto* s = std::get_if<Stroke>(&elementAt(r))) if (const QString* g = attrFind(s->attrs, "glow")) pad = std::max(pad, 2 + 2 * g->toDouble());
  L.rbox = {bbox[0] - pad, bbox[1] - pad, bbox[2] + pad, bbox[3] + pad};
  const double w = L.rbox[2] - L.rbox[0], h = L.rbox[3] - L.rbox[1];
  const double dpr = ed_.view() ? ed_.view()->devicePixelRatioF() : 1.0;
  const double k = std::min(d.zoom * dpr, kMaxBitmapPx / std::max({w, h, 1.0}));
  QElapsedTimer profTimer;
  profTimer.start();
  L.bitmap = QPixmap(std::max(1, int(std::ceil(w * k))), std::max(1, int(std::ceil(h * k))));
  L.bitmap.fill(Qt::transparent);
  {
    // Drawing thousands of strokes is what costs here. Slices of the selection (in z order) are drawn on worker threads into layers of their own and composed in
    // the same order: source-over is associative, so the picture is the one a single pass makes. A highlighter multiplies with what is under it, which a
    // separate layer cannot do, so a selection holding one is drawn in one pass.
    const size_t n = items_.size();
    bool multiply = false;
    for (const ElementRef& r : items_)
      if (const auto* st = std::get_if<Stroke>(&elementAt(r))) if (st->tool() == "highlighter") { multiply = true; break; }
    const qint64 bytes = qint64(L.bitmap.width()) * L.bitmap.height() * 4;
    const int threads = std::clamp(std::min(QThread::idealThreadCount() - 1, int(std::max<qint64>(1, (128ll << 20) / std::max<qint64>(1, bytes)))), 1, 6);
    RenderOptions o;
    o.clip = toQ(L.rbox);
    o.scale = k;
    auto setup = [&](QPainter& qp) {
      qp.setRenderHint(QPainter::Antialiasing, true);
      qp.setRenderHint(QPainter::TextAntialiasing, true);
      qp.setRenderHint(QPainter::SmoothPixmapTransform, true);
      qp.scale(k, k);
      qp.translate(-L.rbox[0], -L.rbox[1]);
    };
    if (n < 600 || threads < 2 || multiply) {
      QPainter qp(&L.bitmap);
      setup(qp);
      for (const ElementRef& r : items_) renderElement(qp, elementAt(r), o);
    } else {
      std::vector<QImage> layers(static_cast<size_t>(threads));
      QThreadPool pool;
      pool.setMaxThreadCount(threads);
      for (int t = 0; t < threads; t++)
        pool.start([&, t] {
          QImage img(L.bitmap.size(), QImage::Format_ARGB32_Premultiplied);
          img.fill(Qt::transparent);
          QPainter qp(&img);
          setup(qp);
          for (size_t i = n * size_t(t) / size_t(threads); i < n * size_t(t + 1) / size_t(threads); i++) renderElement(qp, elementAt(items_[i]), o);
          qp.end();
          layers[size_t(t)] = std::move(img);
        });
      pool.waitForDone();   // the document is not touched until every slice is drawn
      QPainter qp(&L.bitmap);
      for (const QImage& img : layers) qp.drawImage(0, 0, img);
    }
  }
  const double profBitmap = double(profTimer.nsecsElapsed()) / 1e6;
  profTimer.restart();
  // take the elements out of the document: one pass per layer
  own_ = true;
  size_t k0 = 0;
  while (k0 < items_.size()) {
    const int layer = items_[k0].layer;
    std::vector<Editor::Repl> repls;
    size_t j = k0;
    for (; j < items_.size() && items_[j].layer == layer; j++) { Editor::Repl r; r.index = items_[j].index; r.count = 1; repls.push_back(std::move(r)); }
    L.inverse.push_back({layer, ed_.applyReplacements(page_, layer, std::move(repls))});
    k0 = j;
  }
  own_ = false;
  L.done = true;
  if (qEnvironmentVariableIsSet("XL_SEL_PROF")) fprintf(stderr, "lift: %zu items, bitmap %dx%d %.1f ms, removal %.1f ms\n", items_.size(), L.bitmap.width(), L.bitmap.height(), profBitmap, double(profTimer.nsecsElapsed()) / 1e6);
}

void RealSelection::restoreLift(Drag& d) {
  if (!d.lift.done) return;
  own_ = true;
  for (auto& [layer, repls] : d.lift.inverse) ed_.applyReplacements(page_, layer, std::move(repls));
  own_ = false;
  d.lift = Lift();
}

QRectF RealSelection::previewArea(const Drag& d) const {
  const double z = d.zoom;
  QRectF a;
  if (d.mode == Mode::Anchor) {
    std::optional<Rect> u;
    auto add = [&](const Element& el) { const QRectF q = elementRenderBounds(el); if (q.isValid()) u = unionRect(u, fromQ(q)); };
    if (d.cur) for (const Element& el : *d.cur) add(el);
    else for (const auto& [layer, repls] : d.lift.inverse) for (const auto& r : repls) for (const Element& el : r.with) add(el);
    return u ? toQ(*u).adjusted(-4, -4, 4, 4) : QRectF();
  }
  a = toQ(transformRect(d.lift.rbox, d.m));
  a = a.united(toQ(transformRect(d.bbox, d.m)).adjusted(-3 / z, -3 / z, 3 / z, 3 / z));
  if (d.vspace) a = a.united(QRectF(0, d.vy - 3 / z, d.pageW, 6 / z));
  if (d.mode == Mode::Rotate) {
    const double R = (d.r0px + 40) / z;
    a = a.united(QRectF(d.cx - R, d.cy - R, 2 * R, 2 * R));
  }
  return a;
}

void RealSelection::move(const PointerEvent& e) {
  Drag& d = drag_;
  if (d.mode == Mode::None) return;
  const double x = e.pos.x(), y = e.pos.y();
  if (d.mode == Mode::Rect) {
    const QRectF old = QRectF(QPointF(d.down), QPointF(d.x1, d.y1)).normalized();
    d.x1 = x; d.y1 = y;
    invalidate(d.page, old.united(QRectF(d.down, QPointF(x, y)).normalized()).adjusted(-2, -2, 2, 2));
    return;
  }
  if (d.mode == Mode::Lasso) {
    const size_t n = d.poly.size();
    const double dx = x - d.poly[n - 2], dy = y - d.poly[n - 1];
    if (dx * dx + dy * dy >= 0.25) {
      d.poly.push_back(x); d.poly.push_back(y);
      d.polyBox = {std::min(d.polyBox[0], x), std::min(d.polyBox[1], y), std::max(d.polyBox[2], x), std::max(d.polyBox[3], y)};
      invalidate(d.page, toQ(d.polyBox).adjusted(-2, -2, 2, 2));   // the fill changes inside the whole polygon
    }
    return;
  }
  updateTransformPreview(d, e);
}

void RealSelection::updateTransformPreview(Drag& d, const PointerEvent& e) {
  const double x = e.pos.x(), y = e.pos.y();
  if (!d.lift.done) {
    // nothing is lifted until the pointer really drags: a click on the frame must not cost a page re-render
    if (std::hypot(x - d.down.x(), y - d.down.y()) * d.zoom < kLiftSlopPx) return;
    lift(d, d.bbox);
  }
  d.moved = true;
  if (d.mode == Mode::Anchor) {
    ElementPtrs ptrs;
    for (const auto& [layer, repls] : d.lift.inverse) for (const auto& r : repls) ptrs.push_back(&r.with[0]);
    const QRectF before = previewArea(d);
    d.cur = gen_.h ? gen_.h->dragAnchor(ptrs, gen_.id, d.anchorI, e.pos, e.shift()) : std::nullopt;
    invalidate(d.page, before.united(previewArea(d)));
    return;
  }
  if (d.mode == Mode::Move) {
    d.m = d.vspace ? translateAff(0, std::min(d.maxDy, std::max(d.minDy, y - d.down.y()))) : translateAff(x - d.down.x(), y - d.down.y());
  } else if (d.mode == Mode::Resize) {
    double hx, hy;
    handlePoint(d.handle, d.bbox, d.zoom, &hx, &hy);
    const bool moveX = east(d.handle) || west(d.handle), moveY = north(d.handle) || south(d.handle);
    // dragging past the opposite side gives a negative factor: a mirror
    double sx = moveX && hx != d.ax ? clampMag((x - d.ax) / (hx - d.ax)) : 1;
    double sy = moveY && hy != d.ay ? clampMag((y - d.ay) / (hy - d.ay)) : 1;
    if (d.uniform) {
      const double s = moveX && moveY ? std::max(std::abs(sx), std::abs(sy)) : std::abs(moveX ? sx : sy);
      auto sign = [](double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : 1.0; };
      sx = (moveX ? sign(sx) : 1) * s;
      sy = (moveY ? sign(sy) : 1) * s;
    }
    d.m = scaleAff(sx, sy, d.ax, d.ay);
  } else if (d.mode == Mode::Rotate) {
    // accumulate the angle so turns beyond 180 degrees keep counting
    const double ang = std::atan2(y - d.cy, x - d.cx);
    double delta = ang - d.prev;
    while (delta > kPi) delta -= 2 * kPi;
    while (delta <= -kPi) delta += 2 * kPi;
    d.total += delta;
    d.prev = ang;
    const double th = e.shift() ? std::round(d.total / kSnapStep) * kSnapStep : d.total;
    d.shown = th;
    d.snapped = e.shift();
    d.m = rotateAff(th, d.cx, d.cy);
  }
  const QRectF area = previewArea(d);
  invalidate(d.page, area.united(d.dirty));
  d.dirty = area;
}

void RealSelection::up(const PointerEvent& e) {
  if (drag_.mode == Mode::None) return;
  Drag d = std::move(drag_);
  drag_ = Drag();
  const Mode mode = d.mode;
  // the pointer's final position counts even if no move event carried it
  if (mode == Mode::Rect) { d.x1 = e.pos.x(); d.y1 = e.pos.y(); invalidate(d.page, QRectF(d.down, QPointF(d.x1, d.y1)).normalized().adjusted(-4, -4, 4, 4)); finishRect(d, e.pos.x(), e.pos.y()); return; }
  if (mode == Mode::Lasso) { invalidate(d.page, toQ(d.polyBox).adjusted(-4, -4, 4, 4)); finishLasso(d); return; }
  updateTransformPreview(d, e);   // the final position counts even when no move event carried it
  if (mode == Mode::Anchor) finishAnchor(d);
  else finishTransform(d);
}

void RealSelection::cancel() {
  if (drag_.mode == Mode::None) return;
  Drag d = std::move(drag_);
  drag_ = Drag();
  const bool wasVspace = d.vspace;
  restoreLift(d);
  invalidate(d.page, d.dirty.united(d.mode == Mode::Rect ? QRectF(d.down, QPointF(d.x1, d.y1)).normalized().adjusted(-4, -4, 4, 4) : QRectF()));
  if (d.mode == Mode::Lasso) invalidate(d.page, toQ(d.polyBox).adjusted(-4, -4, 4, 4));
  if (wasVspace) { items_.clear(); page_ = -1; boxValid_ = false; }
  else changed(QRectF());
}

// ---- finishing gestures ------------------------------------------------------------------------------------------------------------------
void RealSelection::finishRect(const Drag& d, double x, double y) {
  const Rect r = {std::min(d.down.x(), x), std::min(d.down.y(), y), std::max(d.down.x(), x), std::max(d.down.y(), y)};
  const int page = d.page;
  const Page& pg = ed_.doc().pages[size_t(page)];
  std::vector<ElementRef> found;
  std::vector<PageIndex::Hit> hits;
  if (r[2] - r[0] < kClickSlop && r[3] - r[1] < kClickSlop) {
    // a click selects the topmost element under the pointer
    ed_.index(page).queryPoint(QPointF(x, y), kHitPad, hits);
    for (size_t i = hits.size(); i-- > 0;) {
      const auto [layer, idx] = hits[i];
      if (!visibleLayer(ed_, page, layer)) continue;
      if (hitElement(pg.layers[size_t(layer)].elements[size_t(idx)], x, y, kHitPad)) { found.push_back({page, layer, idx}); break; }
    }
  } else {
    ed_.index(page).query(toQ(r), 0, hits);
    for (const auto& [layer, idx] : hits) {
      if (!visibleLayer(ed_, page, layer)) continue;
      if (inRect(pg.layers[size_t(layer)].elements[size_t(idx)], r)) found.push_back({page, layer, idx});
    }
  }
  setFound(page, std::move(found));
}

void RealSelection::finishLasso(const Drag& d) {
  const int page = d.page;
  const Page& pg = ed_.doc().pages[size_t(page)];
  std::vector<ElementRef> found;
  if (d.poly.size() >= 6) {
    std::vector<PageIndex::Hit> hits;
    ed_.index(page).query(toQ(d.polyBox), 0, hits);
    for (const auto& [layer, idx] : hits) {
      if (!visibleLayer(ed_, page, layer)) continue;
      if (inPolygon(pg.layers[size_t(layer)].elements[size_t(idx)], d.poly)) found.push_back({page, layer, idx});
    }
  }
  setFound(page, std::move(found));
}

void RealSelection::setFound(int page, std::vector<ElementRef> found) {
  if (found.empty()) { page_ = -1; items_.clear(); fp_.clear(); planes_.clear(); gen_ = {}; anchors_.clear(); boxValid_ = false; ed_.notifySelectionChanged(); return; }
  setItems(page, std::move(found), true);
  changed(QRectF());
}

void RealSelection::finishTransform(Drag& d) {
  const bool wasVspace = d.vspace;
  const int page = page_;
  if (!d.lift.done) {   // a click on the frame: nothing was lifted, nothing changes
    if (wasVspace) { items_.clear(); page_ = -1; boxValid_ = false; ed_.notifySelectionChanged(); }
    else changed(d.dirty);
    return;
  }
  const bool isChange = !isIdentity(d.m);
  if (!isChange) {
    restoreLift(d);
    invalidate(page, d.dirty);
    if (wasVspace) { items_.clear(); page_ = -1; boxValid_ = false; ed_.notifySelectionChanged(); }
    else changed(d.dirty);
    return;
  }
  own_ = true;
  ReplSet fwd, inv;
  size_t k = 0;
  for (auto& [layer, repls] : d.lift.inverse) {
    std::vector<Editor::Repl> f, iv;
    f.reserve(repls.size()); iv.reserve(repls.size());
    for (Editor::Repl& r : repls) {
      const int orig = items_[k++].index;
      Element before = r.with[0];
      Element after = before;
      transformElement(after, d.m);
      Editor::Repl rf, ri;
      rf.index = ri.index = orig; rf.count = ri.count = 1;
      rf.with.push_back(after);
      ri.with.push_back(std::move(before));
      f.push_back(std::move(rf)); iv.push_back(std::move(ri));
      r.with[0] = std::move(after);   // what goes back into the document now
    }
    ed_.applyReplacements(page, layer, std::move(repls));
    fwd.push_back({layer, std::move(f)});
    inv.push_back({layer, std::move(iv)});
  }
  own_ = false;
  startGhost(page, std::move(d.lift.bitmap), d.lift.rbox, d.m, previewArea(d));
  d.lift = Lift();
  std::optional<Editor::Swap> sw;
  if (d.mode == Mode::Resize && gen_.h) sw = rebuildGenerated(page);   // ticks, numbers and arrows keep their size (rebuildSelectedPlane)
  const QString label = wasVspace ? QStringLiteral("Espacio vertical") : d.mode == Mode::Move ? QStringLiteral("Mover") : d.mode == Mode::Resize ? QStringLiteral("Redimensionar") : QStringLiteral("Rotar");
  push(label, std::move(fwd), std::move(inv), sw);
  if (wasVspace) { items_.clear(); page_ = -1; boxValid_ = false; invalidate(page, d.dirty); ed_.notifySelectionChanged(); return; }
  if (sw) { /* items_ were replaced by rebuildGenerated */ }
  else recompute();
  changed(d.dirty);
}

std::optional<Editor::Swap> RealSelection::rebuildGenerated(int page) {
  const Gen g = gen_;
  std::vector<ElementRef> now = ed_.membersOf(page, g.layer, g.id);
  ElementPtrs ptrs;
  for (const ElementRef& r : now) ptrs.push_back(&elementAt(r));
  auto fresh = g.h->rebuildAfterTransform(ptrs, g.id);
  if (!fresh) { recompute(); return std::nullopt; }
  Editor::Swap sw = ed_.swapGenerated(page, g.layer, g.id, std::move(*fresh));
  setItems(page, ed_.membersOf(page, g.layer, g.id), false);
  return sw;
}

void RealSelection::finishAnchor(Drag& d) {
  const int page = page_;
  if (!d.lift.done) { changed(d.dirty); return; }
  restoreLift(d);   // the originals go back where they were; the swap below replaces them
  if (!d.cur || !gen_.h) { changed(d.dirty); return; }
  const Gen g = gen_;
  Editor::Swap sw = ed_.swapGenerated(page, g.layer, g.id, std::move(*d.cur));
  push(QStringLiteral("Editar ancla"), {}, {}, sw);
  setItems(page, ed_.membersOf(page, g.layer, g.id), false);
  changed(d.dirty);
}

void RealSelection::startGhost(int page, QPixmap pm, const Rect& rbox, const Aff& m, const QRectF& area) {
  stopGhost();
  if (!ed_.view()) return;
  ghost_ = {page, std::move(pm), rbox, m, area};
  if (!ghostTimer_) {
    ghostTimer_ = std::make_unique<QTimer>();
    ghostTimer_->setInterval(25);
    QObject::connect(ghostTimer_.get(), &QTimer::timeout, ghostTimer_.get(), [this] {
      // the tiles are current when nothing is queued; never keep the picture longer than a few seconds
      if (!ed_.view() || !ed_.view()->hasPendingTiles() || ghostAge_.elapsed() > 4000) stopGhost();
    });
  }
  ghostAge_.restart();
  ghostTimer_->start();
}

void RealSelection::stopGhost() {
  if (ghostTimer_) ghostTimer_->stop();
  if (ghost_.page < 0) return;
  const int page = ghost_.page;
  const QRectF area = ghost_.area;
  ghost_ = Ghost();
  invalidate(page, area);
}

// ---- history -----------------------------------------------------------------------------------------------------------------------------
void RealSelection::push(const QString& label, ReplSet fwd, ReplSet inv, std::optional<Editor::Swap> sw, std::function<void(bool)> after) {
  pushShared(label, std::make_shared<ReplSet>(std::move(fwd)), std::make_shared<ReplSet>(std::move(inv)), std::move(sw), std::move(after));
}

void RealSelection::pushShared(const QString& label, std::shared_ptr<ReplSet> F, std::shared_ptr<ReplSet> I, std::optional<Editor::Swap> sw, std::function<void(bool)> after) {
  Editor* ed = &ed_;
  const int page = page_;
  auto run = [ed, page](const std::shared_ptr<ReplSet>& s) { for (const auto& [layer, r] : *s) ed->applyReplacements(page, layer, r); };
  Action a;
  a.label = label;
  a.page = page;
  a.undo = [=] { if (sw) sw->undo(); run(I); if (after) after(true); };
  a.redo = [=] { run(F); if (sw) sw->redo(); if (after) after(false); };
  ed_.pushAction(std::move(a));
}

void RealSelection::performAndPush(const QString& label, ReplSet fwd, std::function<void(bool)> after) {
  ReplSet inv;
  for (const auto& [layer, r] : fwd) inv.push_back({layer, ed_.applyReplacements(page_, layer, r)});
  push(label, std::move(fwd), std::move(inv), std::nullopt, std::move(after));
}

ReplSet RealSelection::sameIndexSet(std::vector<Element> after) const {
  ReplSet fwd;
  size_t k = 0;
  while (k < items_.size()) {
    const int layer = items_[k].layer;
    std::vector<Editor::Repl> repls;
    for (; k < items_.size() && items_[k].layer == layer; k++) {
      Editor::Repl r;
      r.index = items_[k].index; r.count = 1;
      r.with.push_back(std::move(after[k]));
      repls.push_back(std::move(r));
    }
    fwd.push_back({layer, std::move(repls)});
  }
  return fwd;
}

void RealSelection::afterInPlaceEdit(const QRectF& old) {
  recompute();
  const double z = ed_.view() ? ed_.view()->zoom() : 1.0;
  if (!old.isNull()) invalidate(page_, old.adjusted(-(kHandlePx + 4) / z, -(kHandlePx + 4 + kRotOffsetPx) / z, (kHandlePx + 4) / z, (kHandlePx + 4) / z));
  changed(QRectF());
}

void RealSelection::editSameIndex(const QString& label, std::vector<Element> after) {
  const QRectF old = bbox();
  performAndPush(label, sameIndexSet(std::move(after)));
  afterInPlaceEdit(old);
  reveal();
}

// ---- operations --------------------------------------------------------------------------------------------------------------------------
void RealSelection::selectAll(int page) {
  if (page < 0 || page >= ed_.pageCount()) return;
  QRectF old;
  if (!items_.empty() && ed_.view() && page_ == page) { const Rect& b = box(); if (finite(b)) old = frameArea(b, ed_.view()->zoom()); }
  const Page& pg = ed_.doc().pages[size_t(page)];
  std::vector<ElementRef> refs;
  for (size_t l = 0; l < pg.layers.size(); l++) {
    if (!visibleLayer(ed_, page, int(l))) continue;
    const auto& els = pg.layers[l].elements;
    for (size_t i = 0; i < els.size(); i++) if (finite(elementBBox(els[i]))) refs.push_back({page, int(l), int(i)});
  }
  if (refs.empty()) { clear(); return; }
  if (page_ >= 0 && page_ != page) invalidate(page_, QRectF(0, 0, 1e5, 1e5));
  setItems(page, std::move(refs), false);
  changed(old);
}

bool RealSelection::copy() {
  if (items_.empty()) return false;
  Document doc;
  Page pg;
  const Page& src = ed_.doc().pages[size_t(page_)];
  pg.attrs = src.attrs;
  pg.background = src.background;
  Layer layer;
  layer.elements.reserve(items_.size());
  QStringList texts;
  for (const ElementRef& r : items_) {
    layer.elements.push_back(elementAt(r));
    if (const auto* t = std::get_if<Text>(&layer.elements.back())) texts << t->text;
  }
  pg.layers.push_back(std::move(layer));
  doc.pages.push_back(std::move(pg));
  const QByteArray xml = serializeXml(doc);
  auto* md = new QMimeData;
  md->setData(kMime, xml);
  if (!texts.isEmpty()) md->setText(texts.join('\n'));
  QGuiApplication::clipboard()->setMimeData(md);
  lastCopy_ = xml;
  clipPage_ = page_;
  pasteCount_ = 0;
  const int n = int(items_.size());
  ed_.notify(QStringLiteral("Copiado: %1 elemento%2").arg(n).arg(n == 1 ? "" : "s"));
  return true;
}

void RealSelection::cut() { if (copy()) deleteSelected(); }

bool RealSelection::paste() {
  const QMimeData* md = QGuiApplication::clipboard()->mimeData();
  if (!md) return false;
  const int page = ed_.currentPage();
  if (page < 0 || page >= ed_.pageCount()) return false;
  std::vector<Element> clones;
  double off = 0;
  if (md->hasFormat(kMime)) {
    const QByteArray xml = md->data(kMime);
    const LoadResult lr = parseXml(xml);
    if (!lr.ok) { ed_.notify(QStringLiteral("No se pudo leer el portapapeles")); return false; }
    for (const Page& p : lr.doc.pages) for (const Layer& l : p.layers) for (const Element& e : l.elements) clones.push_back(e);
    if (clones.empty()) return false;
    if (xml != lastCopy_) { lastCopy_ = xml; clipPage_ = -1; pasteCount_ = 0; }   // data from elsewhere: it starts at the first offset
    const int n = ++pasteCount_;
    off = 12.0 * (page == clipPage_ ? n : n - 1);   // repeated pastes shift diagonally so they do not overlap
    // a pasted plane, figure, line or diagram is a new object: its pieces must not share the group of the original
    std::map<QString, QString> fresh;
    for (Element& c : clones) {
      const auto id = planeIdOf(c);
      if (!id) continue;
      if (!fresh.count(*id)) fresh[*id] = newObjectId();
      attrSet(attrsOf(c), "xlPlane", fresh[*id]);
    }
    if (off != 0) for (Element& c : clones) transformElement(c, translateAff(off, off));
  } else if (md->hasImage()) {
    return texttools::pasteImageFromClipboard(ed_);   // the picture inserter owns pictures (it also finishes a text being typed and goes back to the previous tool on Esc)
  } else {
    return false;
  }
  int layer = ed_.activeLayer(page);
  if (!ed_.layerVisible(page, layer)) { ed_.setLayerVisible(page, layer, true); ed_.notify(QStringLiteral("La capa activa estaba oculta: se volvió a mostrar")); }
  auto& els = ed_.doc().pages[size_t(page)].layers[size_t(layer)].elements;
  const int at = int(els.size()), n = int(clones.size());
  Editor::Repl r;
  r.index = at; r.count = 0; r.with = std::move(clones);
  ReplSet fwd;
  fwd.push_back({layer, {std::move(r)}});
  const int keepPage = page_;
  page_ = page;   // push() records the page of the action
  performAndPush(QStringLiteral("Pegar"), std::move(fwd));
  page_ = keepPage;
  ed_.setTool(QStringLiteral("select"));
  std::vector<ElementRef> refs;
  for (int i = 0; i < n; i++) refs.push_back({page, layer, at + i});
  select(page, refs);
  return true;
}

void RealSelection::duplicate() { if (copy()) paste(); }

void RealSelection::deleteSelected() {
  if (items_.empty() || drag_.mode != Mode::None) return;
  ReplSet fwd;
  size_t k = 0;
  while (k < items_.size()) {
    const int layer = items_[k].layer;
    std::vector<Editor::Repl> repls;
    for (; k < items_.size() && items_[k].layer == layer; k++) { Editor::Repl r; r.index = items_[k].index; r.count = 1; repls.push_back(std::move(r)); }
    fwd.push_back({layer, std::move(repls)});
  }
  performAndPush(QStringLiteral("Eliminar"), std::move(fwd));
  clear();
}

void RealSelection::reorder(bool front) {
  if (items_.empty() || drag_.mode != Mode::None) return;
  ReplSet fwd;
  std::vector<ElementRef> after;
  size_t k = 0;
  while (k < items_.size()) {
    const int layer = items_[k].layer;
    const auto& els = ed_.doc().pages[size_t(page_)].layers[size_t(layer)].elements;
    std::vector<int> idx;
    for (; k < items_.size() && items_[k].layer == layer; k++) idx.push_back(items_[k].index);
    const int total = int(els.size()), c = int(idx.size());
    // already in place when the chosen elements are one block at the end (front) or the start (back)
    const bool contiguous = idx.back() - idx.front() == c - 1;
    const bool inPlace = contiguous && (front ? idx.back() == total - 1 : idx.front() == 0);
    for (int i = 0; i < c; i++) after.push_back({page_, layer, front ? total - c + i : i});
    if (inPlace) continue;
    std::vector<Editor::Repl> repls;
    std::vector<Element> picked;
    for (int i : idx) picked.push_back(els[size_t(i)]);
    if (!front) { Editor::Repl ins; ins.index = 0; ins.count = 0; ins.with = std::move(picked); repls.push_back(std::move(ins)); }
    for (int i : idx) { Editor::Repl r; r.index = i; r.count = 1; repls.push_back(std::move(r)); }
    if (front) { Editor::Repl ins; ins.index = total; ins.count = 0; ins.with = std::move(picked); repls.push_back(std::move(ins)); }
    fwd.push_back({layer, std::move(repls)});
  }
  if (fwd.empty()) return;
  // positions change, so undo/redo must say where the elements are (the fingerprint check of refresh() cannot guess it)
  Editor* ed = &ed_;
  const int page = page_;
  auto atUndo = std::make_shared<std::vector<ElementRef>>(items_);
  auto atRedo = std::make_shared<std::vector<ElementRef>>(after);
  performAndPush(front ? QStringLiteral("Traer al frente") : QStringLiteral("Enviar al fondo"), std::move(fwd), [ed, page, atUndo, atRedo](bool undo) {
    if (RealSelection* s = RealSelection::of(*ed)) s->select(page, undo ? *atUndo : *atRedo);
  });
  setItems(page_, std::move(after), false);
  changed(QRectF());
}

void RealSelection::quick(Quick kind) {
  if (items_.empty() || drag_.mode != Mode::None) return;
  const Rect b = box();
  if (!finite(b)) return;
  const double cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
  const Aff m = kind == Quick::RotR ? rotateAff(kPi / 2, cx, cy) : kind == Quick::RotL ? rotateAff(-kPi / 2, cx, cy) : flipAff(kind == Quick::FlipH, cx, cy);
  std::vector<Element> after;
  after.reserve(items_.size());
  bool hasText = false;
  for (const ElementRef& r : items_) {
    after.push_back(elementAt(r));
    transformElement(after.back(), m);
    hasText |= std::holds_alternative<Text>(after.back());
  }
  const QString label = kind == Quick::RotR ? QStringLiteral("Rotar a la derecha") : kind == Quick::RotL ? QStringLiteral("Rotar a la izquierda") : kind == Quick::FlipH ? QStringLiteral("Espejar horizontalmente") : QStringLiteral("Espejar verticalmente");
  editSameIndex(label, std::move(after));
  if (hasText) ed_.notify(QStringLiteral("El texto no se puede rotar ni espejar (el formato .xopp no lo permite): se reubica pero sigue derecho. Las fórmulas LaTeX sí se giran."));
}

void RealSelection::nudge(double dx, double dy) {
  if (items_.empty() || drag_.mode != Mode::None) return;
  const Aff m = translateAff(dx, dy);
  std::vector<Element> after;
  after.reserve(items_.size());
  for (const ElementRef& r : items_) { after.push_back(elementAt(r)); transformElement(after.back(), m); }
  const QRectF old = bbox();
  ReplSet fwd = sameIndexSet(std::move(after));
  // A held key repeats: the steps of one burst are ONE undo step. A step folds into the one on top of the stack while nothing else was pushed
  // since (same depth and change counter), the same elements are selected and the document was not saved in between.
  const bool fold = nudge_ && nudge_->age.elapsed() < 1500 && nudge_->depth == ed_.undoDepth() && nudge_->mutations == ed_.changeCount() && ed_.dirty() && nudge_->items == items_;
  if (fold) {
    for (const auto& [layer, r] : fwd) ed_.applyReplacements(page_, layer, r);
    *nudge_->fwd = std::move(fwd);
    nudge_->age.restart();
  } else {
    auto F = std::make_shared<ReplSet>(std::move(fwd));
    auto I = std::make_shared<ReplSet>();
    for (const auto& [layer, r] : *F) I->push_back({layer, ed_.applyReplacements(page_, layer, r)});
    pushShared(QStringLiteral("Mover"), F, I, std::nullopt, nullptr);
    nudge_ = Nudge{F, I, ed_.undoDepth(), ed_.changeCount(), items_, {}};
    nudge_->age.start();
  }
  afterInPlaceEdit(old);
  reveal();
}

void RealSelection::reveal() {
  DocumentView* v = ed_.view();
  if (!v || items_.empty() || drag_.mode != Mode::None || page_ < 0 || page_ >= ed_.pageCount()) return;
  const QRectF b = bbox();
  if (b.isNull()) return;
  const QRectF box = QRectF(v->toView(page_, b.topLeft()), v->toView(page_, b.bottomRight())).normalized();
  const double W = v->viewport()->width(), H = v->viewport()->height(), margin = 24, top = margin + kRotOffsetPx;   // the rotation handle hangs above the frame
  // per axis: only when none of the frame is on screen (a frame that is partly visible is where the user can see it; scrolling it would move the page under the pen)
  auto delta = [&](double lo, double hi, double size, double loMargin) {
    if (hi >= 0 && lo <= size) return 0.0;
    const bool fits = hi - lo <= size - loMargin - margin;
    return fits ? (lo < 0 ? lo - loMargin : hi - (size - margin)) : lo - loMargin;
  };
  const double dx = delta(box.left(), box.right(), W, margin), dy = delta(box.top(), box.bottom(), H, top);
  if (dx != 0 || dy != 0) v->scrollByPixels(dx, dy);
}

int RealSelection::setGlow(const std::optional<Glow>& g) {
  if (items_.empty() || drag_.mode != Mode::None) return 0;
  std::vector<Element> after;
  after.reserve(items_.size());
  int n = 0;
  for (const ElementRef& r : items_) {
    after.push_back(elementAt(r));
    auto* s = std::get_if<Stroke>(&after.back());
    if (!s || s->tool() == "highlighter") continue;
    Attrs kept;
    for (auto& a : s->attrs) if (a.first != "glow" && a.first != "glowAlpha") kept.push_back(a);
    s->attrs = std::move(kept);
    if (g) {
      s->attrs.emplace_back("glow", QString::number(std::round(g->size * 100) / 100));
      s->attrs.emplace_back("glowAlpha", QString::number(std::round(g->intensity * 100) / 100));
    }
    n++;
  }
  if (n == 0) return 0;
  editSameIndex(g ? QStringLiteral("Aplicar brillo") : QStringLiteral("Quitar brillo"), std::move(after));
  return n;
}

// ---- drawing -----------------------------------------------------------------------------------------------------------------------------
void RealSelection::paintOverlay(QPainter& p, const OverlayInfo& info) {
  if (items_.empty() || info.page != page_ || drag_.mode != Mode::None) return;
  const Rect& b = box();
  if (!finite(b)) return;
  const double z = info.zoom, px = 1 / z;
  p.save();
  p.setBrush(Qt::NoBrush);
  QPen frame(kBlue, px);
  frame.setDashPattern({5.0, 3.0});
  p.setPen(frame);
  p.drawRect(toQ(b));
  const double s = kHandlePx * px;
  p.setPen(QPen(kBlue, 1.5 * px));
  const double mx = (b[0] + b[2]) / 2;
  double rx, ry;
  handlePoint(Handle::Rot, b, z, &rx, &ry);
  p.drawLine(QPointF(mx, b[1]), QPointF(rx, ry));
  p.setBrush(Qt::white);
  static const Handle sizes[] = {Handle::NW, Handle::N, Handle::NE, Handle::E, Handle::SE, Handle::S, Handle::SW, Handle::W};
  for (Handle h : sizes) {
    double hx, hy;
    handlePoint(h, b, z, &hx, &hy);
    p.drawRect(QRectF(hx - s / 2, hy - s / 2, s, s));
  }
  p.drawEllipse(QPointF(rx, ry), s * 0.65, s * 0.65);
  for (const QPointF& a : anchors_) p.drawEllipse(a, 5 * px, 5 * px);
  p.restore();
}

void RealSelection::paintDrag(QPainter& p, const OverlayInfo& info) {
  const Drag& d = drag_;
  if (d.mode == Mode::None && ghost_.page == info.page && !ghost_.pm.isNull()) {
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setWorldTransform(toTransform(ghost_.m), true);
    p.drawPixmap(toQ(ghost_.rbox), ghost_.pm, QRectF(ghost_.pm.rect()));
    p.restore();
    return;
  }
  if (d.mode == Mode::None || info.page != d.page) return;
  const double z = info.zoom, px = 1 / z;
  p.save();
  if (d.mode == Mode::Rect) {
    const QRectF r = QRectF(d.down, QPointF(d.x1, d.y1)).normalized();
    QPen pen(kBlue, px);
    pen.setDashPattern({5.0, 3.0});
    p.setPen(pen);
    p.setBrush(QColor(47, 111, 208, 31));
    p.drawRect(r);
  } else if (d.mode == Mode::Lasso) {
    QPen pen(kBlue, px);
    pen.setDashPattern({5.0, 3.0});
    p.setPen(pen);
    p.setBrush(QColor(47, 111, 208, 31));
    QPolygonF poly;
    poly.reserve(int(d.poly.size() / 2));
    for (size_t i = 0; i + 1 < d.poly.size(); i += 2) poly << QPointF(d.poly[i], d.poly[i + 1]);
    p.drawPolygon(poly);
  } else if (d.mode == Mode::Anchor) {
    if (d.lift.done) {
      RenderOptions o;
      o.scale = z * (ed_.view() ? ed_.view()->devicePixelRatioF() : 1.0);
      if (d.cur) for (const Element& el : *d.cur) renderElement(p, el, o);
      else for (const auto& [layer, repls] : d.lift.inverse) for (const auto& r : repls) for (const Element& el : r.with) renderElement(p, el, o);
    }
  } else if (d.lift.done) {
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setWorldTransform(toTransform(d.m), true);
    p.drawPixmap(toQ(d.lift.rbox), d.lift.bitmap, QRectF(d.lift.bitmap.rect()));
    p.restore();
    if (d.vspace) {
      QPen pen(kBlue, 1.5 * px);
      pen.setDashPattern({4.0, 2.7});
      p.setPen(pen);
      p.drawLine(QPointF(0, d.vy), QPointF(d.pageW, d.vy));
    } else {
      // dashed outline of the box after the map (stays crisp, unlike drawing inside the transform)
      const auto a = applyAff(d.m, d.bbox[0], d.bbox[1]), b = applyAff(d.m, d.bbox[2], d.bbox[1]), c = applyAff(d.m, d.bbox[2], d.bbox[3]), e = applyAff(d.m, d.bbox[0], d.bbox[3]);
      QPen pen(kBlue, px);
      pen.setDashPattern({5.0, 3.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawPolygon(QPolygonF({QPointF(a[0], a[1]), QPointF(b[0], b[1]), QPointF(c[0], c[1]), QPointF(e[0], e[1])}));
    }
    if (d.mode == Mode::Rotate) drawProtractor(p, d, z);
  }
  p.restore();
}

/** Protractor: dial with 15-degree ticks measured from the start direction, both rays, and the swept arc with its value. */
void RealSelection::drawProtractor(QPainter& p, const Drag& d, double zoom) const {
  const double px = 1 / zoom, R = d.r0px * px, th = d.shown;
  const double cx = d.cx, cy = d.cy, a0 = d.a0;
  const QColor blue = d.snapped ? QColor(0xff, 0x80, 0x00) : kBlue;
  auto pt = [&](double a, double r) { return QPointF(cx + std::cos(a) * r, cy + std::sin(a) * r); };
  p.save();
  p.setPen(QPen(QColor(47, 111, 208, 153), px));
  p.setBrush(QColor(47, 111, 208, 13));
  p.drawEllipse(QPointF(cx, cy), R, R);
  for (int k = 0; k < 24; k++) {
    const double a = a0 + k * kSnapStep;
    const double len = (k % 6 == 0 ? 14 : k % 3 == 0 ? 10 : 6) * px;
    p.setPen(QPen(QColor(47, 111, 208, 153), (k % 6 == 0 ? 1.6 : 1.0) * px));
    p.drawLine(pt(a, R), pt(a, R - len));
  }
  for (int deg : {0, 90, 180, 270}) drawLabel(p, pt(a0 + deg * kPi / 180, R + 13 * px), QStringLiteral("%1°").arg(deg), 11, px, QColor(30, 60, 120, 230), false, false);
  QPen start(QColor(90, 90, 90, 217), px);
  start.setDashPattern({4.0, 3.0});
  p.setPen(start);
  p.drawLine(QPointF(cx, cy), pt(a0, R));
  p.setPen(QPen(blue, 2 * px));
  p.drawLine(QPointF(cx, cy), pt(a0 + th, R));
  // swept sector and arc (screen angles run clockwise, Qt's arcs counter-clockwise)
  const double ra = R * kProtractorArc;
  const double sweep = std::abs(th) > 2 * kPi ? (th < 0 ? -2 * kPi : 2 * kPi) : th;
  const QRectF arcRect(cx - ra, cy - ra, 2 * ra, 2 * ra);
  QPainterPath sector;
  sector.moveTo(cx, cy);
  sector.arcTo(arcRect, -a0 * 180 / kPi, -sweep * 180 / kPi);
  sector.closeSubpath();
  p.setPen(Qt::NoPen);
  p.setBrush(d.snapped ? QColor(255, 128, 0, 56) : QColor(47, 111, 208, 56));
  p.drawPath(sector);
  QPainterPath arc;
  arc.moveTo(pt(a0, ra));
  arc.arcTo(arcRect, -a0 * 180 / kPi, -sweep * 180 / kPi);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(blue, 2.5 * px));
  p.drawPath(arc);
  const double deg = th * 180 / kPi;
  const QString txt = std::abs(deg) < 0.05 ? QStringLiteral("0°")
                                           : QStringLiteral("%1° %2").arg(d.snapped ? QString::number(std::abs(std::lround(deg))) : QString::number(std::abs(deg), 'f', 1), deg > 0 ? QStringLiteral("↻") : QStringLiteral("↺"));
  drawLabel(p, pt(a0 + sweep / 2, R * 0.8), txt, 15, px, d.snapped ? QColor(0xb3, 0x59, 0x00) : QColor(0x17, 0x3f, 0x8a), true, true);
  p.setPen(Qt::NoPen);
  p.setBrush(blue);
  p.drawEllipse(QPointF(cx, cy), 3 * px, 3 * px);
  p.restore();
}

}  // namespace selx
