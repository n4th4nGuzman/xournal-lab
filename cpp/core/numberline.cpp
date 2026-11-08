#include "numberline.h"
#include "setexpr.h"
#include "setsjson.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace sets {

using namespace detail;

static constexpr int MAX_TICKS = 120;
static constexpr double PAD = 14;
static constexpr double ROW_GAP = 40;

LineSpec defaultLine(V2 a, V2 b) {
  LineSpec s;
  s.pts = {a[0], a[1], b[0], b[1]};
  s.rows = {LineRow{}};
  return s;
}

static double clampNum(double v, double d, double lo, double hi) { return std::isfinite(v) ? std::min(hi, std::max(lo, v)) : d; }

LineSpec sanitizeLine(const LineSpec& raw) {
  const LineSpec d = defaultLine();
  LineSpec s;
  if (raw.pts.size() >= 4) {
    for (int i = 0; i < 4; i++) s.pts.push_back(clampNum(raw.pts[i], d.pts[i], -1e6, 1e6));
  } else s.pts = d.pts;
  s.min = clampNum(raw.min, d.min, -1e9, 1e9);
  s.max = clampNum(raw.max, d.max, -1e9, 1e9);
  if (!(s.max > s.min)) s.max = s.min + 1;
  for (size_t i = 0; i < raw.rows.size() && int(i) < MAX_ROWS; i++) s.rows.push_back({raw.rows[i].expr.left(200), raw.rows[i].label.left(40)});
  if (s.rows.empty()) s.rows.push_back({});
  s.step = std::max((s.max - s.min) / 400, clampNum(raw.step, d.step, 1e-6, 1e9));
  s.every = int(std::floor(clampNum(double(raw.every), 1, 1, 50) + 0.5));
  s.ticks = raw.ticks;
  s.numbers = raw.numbers;
  s.arrows = raw.arrows;
  s.brackets = raw.brackets;
  return s;
}

LineSpec sanitizeLine(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  const LineSpec d = defaultLine();
  LineSpec s;
  const QJsonArray pts = o.value("pts").toArray();
  s.pts = d.pts;
  if (pts.size() >= 4)
    for (int i = 0; i < 4; i++) s.pts[i] = numberOr(pts[i], d.pts[i], -1e6, 1e6);
  s.min = numberOr(o.value("min"), d.min);
  s.max = numberOr(o.value("max"), d.max);
  s.step = numberOr(o.value("step"), d.step, 1e-6, 1e9);
  s.every = int(std::floor(numberOr(o.value("every"), 1, 1, 50) + 0.5));
  s.ticks = o.value("ticks") != QJsonValue(false);
  s.numbers = o.value("numbers") != QJsonValue(false);
  s.arrows = o.value("arrows") != QJsonValue(false);
  s.brackets = o.value("marks").toString() == "brackets";
  for (const QJsonValue& r : o.value("rows").toArray()) {
    const QJsonObject q = r.toObject();
    s.rows.push_back({q.value("expr").toString(), q.value("label").toString()});
  }
  return sanitizeLine(s);   // the clamping (rows, ranges) is the same for both entries
}

QString lineSpecJson(const LineSpec& s) {
  QString rows = "[";
  for (size_t i = 0; i < s.rows.size(); i++)
    rows += QString(i ? "," : "") + "{\"expr\":" + jsStr(s.rows[i].expr) + ",\"label\":" + jsStr(s.rows[i].label) + "}";
  rows += "]";
  return "{\"kind\":\"line\",\"pts\":" + jsNums(s.pts) + ",\"min\":" + jsNum(s.min) + ",\"max\":" + jsNum(s.max) + ",\"step\":" + jsNum(s.step) +
         ",\"every\":" + jsNum(s.every) + ",\"ticks\":" + jsBool(s.ticks) + ",\"numbers\":" + jsBool(s.numbers) + ",\"arrows\":" + jsBool(s.arrows) +
         ",\"marks\":" + jsStr(s.brackets ? "brackets" : "circles") + ",\"rows\":" + rows + "}";
}

std::vector<LineSetResult> lineSets(const LineSpec& spec) {
  std::vector<LineSetResult> out;
  for (const LineRow& r : spec.rows) {
    LineSetResult res;
    if (!r.expr.trimmed().isEmpty()) {
      const Parsed p = parseSetExpr(r.expr);
      if (!p.ok) res.error = p.err.msg;
      else {
        const auto e = evalReal(p.expr);
        if (e.ok) res.set = e.value; else res.error = e.msg;
      }
    }
    out.push_back(std::move(res));
  }
  return out;
}

namespace {

struct Frame { V2 p0, u, up; double len, scale; };

Frame frameOf(const LineSpec& spec) {
  const double x0 = spec.pts[0], y0 = spec.pts[1], x1 = spec.pts[2], y1 = spec.pts[3];
  double len = std::hypot(x1 - x0, y1 - y0);
  if (len == 0) len = 1;
  const V2 u{(x1 - x0) / len, (y1 - y0) / len};
  return {{x0, y0}, u, {u[1], -u[0]}, len, len / (spec.max - spec.min)};
}

V2 at(const Frame& f, double s, double off = 0) { return {f.p0[0] + f.u[0] * s + f.up[0] * off, f.p0[1] + f.u[1] * s + f.up[1] * off}; }

using Range = std::pair<double, double>;

/** The ranges of [a, b] that are left when the gaps are taken out. */
std::vector<Range> without(double a, double b, const std::vector<Range>& gaps) {
  std::vector<Range> out{{a, b}};
  for (const auto& [g0, g1] : gaps) {
    std::vector<Range> next;
    for (const auto& [s, e] : out) {
      if (g1 <= s || g0 >= e) { next.push_back({s, e}); continue; }
      if (g0 > s) next.push_back({s, g0});
      if (g1 < e) next.push_back({g1, e});
    }
    out = std::move(next);
  }
  std::vector<Range> kept;
  for (const auto& r : out)
    if (r.second - r.first > 0.05) kept.push_back(r);
  return kept;
}

struct Marker { double v; bool closed; int side; };   // side: +1 when the interval goes toward larger values from here

/** The marks of a set inside the visible range, one per value (a point both pieces leave out is one open circle). */
std::vector<Marker> markersOf(const IntervalSet& set, double min, double max) {
  std::map<QString, Marker> byValue;
  auto add = [&](double v, bool closed, int side) {
    if (!std::isfinite(v) || v < min - 1e-9 || v > max + 1e-9) return;
    const QString k = QString::number(v == 0 ? 0.0 : v, 'f', 9);
    auto it = byValue.find(k);
    if (it == byValue.end()) byValue[k] = {v, closed, side};
    else it->second.closed = it->second.closed || closed;
  };
  for (const Piece& p : set) {
    if (p.lo == p.hi) add(p.lo, true, 1);
    else { add(p.lo, p.loClosed, 1); add(p.hi, p.hiClosed, -1); }
  }
  std::vector<Marker> out;
  for (const auto& kv : byValue) out.push_back(kv.second);
  std::stable_sort(out.begin(), out.end(), [](const Marker& a, const Marker& b) { return a.v < b.v; });
  return out;
}

std::vector<double> circle(V2 c, double r) {
  std::vector<double> out;
  for (int i = 0; i <= 24; i++) {
    out.push_back(c[0] + std::cos(double(i) / 24 * M_PI * 2) * r);
    out.push_back(c[1] + std::sin(double(i) / 24 * M_PI * 2) * r);
  }
  return out;
}

void append(std::vector<double>& to, V2 p) { to.push_back(p[0]); to.push_back(p[1]); }
std::vector<double> seg(V2 a, V2 b) { return {a[0], a[1], b[0], b[1]}; }

}  // namespace

std::vector<double> witnessOf(const LineSpec& spec) {
  const Frame f = frameOf(spec);
  const V2 p = at(f, 0, 30);
  std::vector<double> w(spec.pts.begin(), spec.pts.begin() + 4);
  append(w, p);
  return w;
}

std::vector<Element> buildLine(const QString& id, const LineSpec& rawSpec, const GenStyle& style) {
  const LineSpec spec = sanitizeLine(rawSpec);
  const Frame f = frameOf(spec);
  const double w = std::max(0.3, style.width);
  const QString& color = style.color;
  const double hiW = w * 2.6;
  const double R = std::min(7.0, std::max(3.6, 3 + w * 1.6));
  std::vector<Element> out;
  auto line = [&](const std::vector<double>& pts, const char* role, double width) {
    out.push_back(strokeEl(pts, color, "ff", width, role, id, Dash::Solid, style.glow));
  };
  const auto sets_ = lineSets(spec);

  // marks every `step`, as many as can be told apart
  double step = spec.step;
  while ((spec.max - spec.min) / step > MAX_TICKS) step *= 2;
  const long k0 = long(std::ceil(spec.min / step - 1e-9)), k1 = long(std::floor(spec.max / step + 1e-9));
  const bool circles = !spec.brackets;

  for (size_t ri = 0; ri < spec.rows.size(); ri++) {
    const LineRow& row = spec.rows[ri];
    const double off = double(ri) * ROW_GAP;
    const std::optional<IntervalSet>& set = sets_[ri].set;
    const std::vector<Marker> markers = set ? markersOf(*set, spec.min, spec.max) : std::vector<Marker>{};
    std::vector<Range> gaps;
    if (circles)
      for (const Marker& m : markers)
        if (!m.closed) { const double s = (m.v - spec.min) * f.scale; gaps.push_back({s - R, s + R}); }

    // the line: in pieces, so an open circle has nothing inside it
    for (const auto& [s0, s1] : without(-PAD, f.len + PAD, gaps)) line(seg(at(f, s0, off), at(f, s1, off)), "setbody", ri == 0 ? w : w * 0.7);
    if (spec.arrows) {
      line(arrowHead(at(f, f.len + PAD, off), at(f, 0, off), 8), "setarrow", w);
      line(arrowHead(at(f, -PAD, off), at(f, f.len, off), 8), "setarrow", w);
    }
    // marks and numbers
    if (spec.ticks) {
      const double tl = ri == 0 ? 5 : 3;
      for (long k = k0; k <= k1; k++) {
        const double s = (double(k) * step - spec.min) * f.scale;
        bool inGap = false;
        for (const auto& g : gaps) if (s > g.first && s < g.second) { inGap = true; break; }
        if (inGap) continue;
        line(seg(at(f, s, off + tl), at(f, s, off - tl)), "setmark", w * 0.8);
        if (ri == 0 && spec.numbers && (k - k0) % spec.every == 0) out.push_back(textEl(formatNumber(double(k) * step), at(f, s, off - tl - 9), 10, color, id, "setlabel"));
      }
    }
    // what the set is: thick pieces over the line, with the ends marked
    if (set) {
      for (const Piece& p : *set) {
        if (p.hi < spec.min - 1e-9 || p.lo > spec.max + 1e-9 || p.lo == p.hi) continue;
        const bool leftOpenEnd = p.lo < spec.min - 1e-9, rightOpenEnd = p.hi > spec.max + 1e-9;   // runs off the page (or to infinity)
        const double s0 = leftOpenEnd ? -PAD : (p.lo - spec.min) * f.scale, s1 = rightOpenEnd ? f.len + PAD : (p.hi - spec.min) * f.scale;
        auto trim = [&](bool m, bool closed) { return circles && m && !closed ? R : 0.0; };
        const double a = s0 + trim(!leftOpenEnd, p.loClosed), b = s1 - trim(!rightOpenEnd, p.hiClosed);
        if (b > a) line(seg(at(f, a, off), at(f, b, off)), "setbody", hiW);
        if (!spec.arrows) {
          if (rightOpenEnd) line(arrowHead(at(f, f.len + PAD, off), at(f, f.len - 30, off), 9), "setarrow", w);
          if (leftOpenEnd) line(arrowHead(at(f, -PAD, off), at(f, 30, off), 9), "setarrow", w);
        }
      }
      for (const Marker& m : markers) {
        const double s = (m.v - spec.min) * f.scale;
        const V2 c = at(f, s, off);
        if (circles) {
          if (m.closed) line({c[0], c[1], c[0] + 0.01, c[1]}, "setbody", R * 2);
          else line(circle(c, R), "setbody", w);
          continue;
        }
        // a bracket where the end belongs to the set, a parenthesis where it does not; both open toward the piece
        const V2 d{f.u[0] * m.side, f.u[1] * m.side};
        const double H = 9, hook = 5;
        const V2 top = at(f, s, off + H), bot = at(f, s, off - H);
        if (m.closed) {
          line({top[0] + d[0] * hook, top[1] + d[1] * hook, top[0], top[1], bot[0], bot[1], bot[0] + d[0] * hook, bot[1] + d[1] * hook}, "setbody", w * 1.3);
        } else {
          const V2 ctl{c[0] - d[0] * 7, c[1] - d[1] * 7};
          std::vector<double> pts;
          for (int i = 0; i <= 10; i++) {
            const double t = i / 10.0, q = 1 - t;
            pts.push_back(q * q * top[0] + 2 * q * t * ctl[0] + t * t * bot[0]);
            pts.push_back(q * q * top[1] + 2 * q * t * ctl[1] + t * t * bot[1]);
          }
          line(pts, "setbody", w * 1.3);
        }
      }
    }
    if (!row.label.isEmpty()) out.push_back(textEl(row.label, at(f, -PAD - 4, off + 16), 12, color, id, "setlabel"));
  }

  const std::vector<double> wit = witnessOf(spec);
  const QString json = "{\"kind\":\"line\",\"spec\":" + lineSpecJson(spec) + ",\"w\":" + jsNums(wit) + ",\"width\":" + jsNum(std::round(w * 1000) / 1000) + "}";
  out.push_back(makeWitness(wit, "setwitness", SET_ATTR, json, color, id));
  return out;
}

std::vector<V2> lineAnchors(const LineSpec& spec) { return {{spec.pts[0], spec.pts[1]}, {spec.pts[2], spec.pts[3]}}; }

LineSpec moveLineAnchor(const LineSpec& spec, int i, V2 p, bool shift) {
  const V2 other = i == 0 ? V2{spec.pts[2], spec.pts[3]} : V2{spec.pts[0], spec.pts[1]};
  V2 q = p;
  if (shift) {
    const double dx = p[0] - other[0], dy = p[1] - other[1], len = std::hypot(dx, dy);
    const double a = std::floor(std::atan2(dy, dx) / (M_PI / 12) + 0.5) * (M_PI / 12);
    q = {other[0] + std::cos(a) * len, other[1] + std::sin(a) * len};
  }
  if (std::hypot(q[0] - other[0], q[1] - other[1]) < 20) return spec;
  LineSpec out = spec;
  out.pts[2 * i] = q[0];
  out.pts[2 * i + 1] = q[1];
  return out;
}

std::optional<LineRead> readLine(const std::vector<const Element*>& members) {
  const Stroke* wit = findWitness(members, "setwitness", SET_ATTR);
  if (!wit) return std::nullopt;
  const auto stored = readStored(*wit, "line");
  if (!stored) return std::nullopt;
  LineSpec spec = sanitizeLine(stored->obj.value("spec"));
  const auto T = affineFrom(stored->w, wit->pts);
  if (!T) return std::nullopt;
  spec.pts = mapPoints(*T, spec.pts);
  return LineRead{spec, styleOfBody(members, stored->width)};
}

}  // namespace sets
