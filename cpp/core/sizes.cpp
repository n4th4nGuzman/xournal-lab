#include "sizes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <cmath>

static const char* const kKeys[kSizeKinds] = {"pen", "highlighter", "eraser", "text"};
const char* sizeKindKey(SizeKind k) { return kKeys[int(k)]; }

const SizePresets& defaultSizes() {
  static const SizePresets d = {{{{0.5, 0.85, 1.41, 2.26, 3.5, 6}, {2.83, 5.66, 11.32, 18}, {4, 8, 16, 32, 56}, {10, 12, 14, 18, 24, 36}}}};
  return d;
}

SizeLimits sizeLimits(SizeKind k) {
  static const SizeLimits l[kSizeKinds] = {{0.1, 60}, {0.5, 100}, {1, 200}, {4, 200}};
  return l[int(k)];
}

bool sizeKindOf(SizeTool tool, SizeKind* out) {
  SizeKind k;
  switch (tool) {
    case SizeTool::Pen: case SizeTool::Shape: case SizeTool::Figure: case SizeTool::Plane: case SizeTool::Space3: case SizeTool::NumLine: case SizeTool::Venn: k = SizeKind::Pen; break;
    case SizeTool::Highlighter: k = SizeKind::Highlighter; break;
    case SizeTool::Eraser: case SizeTool::EraserPart: k = SizeKind::Eraser; break;
    case SizeTool::Text: k = SizeKind::Text; break;
    default: return false;
  }
  if (out) *out = k;
  return true;
}

SizePresets sanitizeSizes(const QJsonValue& raw) {
  const QJsonObject o = raw.isObject() ? raw.toObject() : QJsonObject();
  SizePresets out;
  for (int i = 0; i < kSizeKinds; i++) {
    const SizeKind k = SizeKind(i);
    const QJsonValue v = o.value(QString::fromLatin1(kKeys[i]));
    const SizeLimits lim = sizeLimits(k);
    bool ok = v.isArray() && v.toArray().size() >= 2 && v.toArray().size() <= 8;
    if (ok)
      for (const QJsonValue& n : v.toArray())
        if (!n.isDouble() || !std::isfinite(n.toDouble()) || n.toDouble() < lim.lo || n.toDouble() > lim.hi) { ok = false; break; }
    if (ok) {
      for (const QJsonValue& n : v.toArray()) out.v[i].push_back(QString::number(n.toDouble(), 'f', 2).toDouble());   // +n.toFixed(2)
    } else out.v[i] = defaultSizes().v[i];
  }
  return out;
}

QJsonObject sizesToJson(const SizePresets& s) {
  QJsonObject o;
  for (int i = 0; i < kSizeKinds; i++) {
    QJsonArray a;
    for (double d : s.v[i]) a.append(d);
    o.insert(QString::fromLatin1(kKeys[i]), a);
  }
  return o;
}

int dotSize(SizeKind kind, double v) {
  const double d = kind == SizeKind::Pen ? 2 + v * 2.4 : kind == SizeKind::Highlighter ? 3 + v * 0.9 : kind == SizeKind::Eraser ? 4 + v * 0.22 : 4 + v * 0.35;
  return int(std::floor(std::min(18.0, std::max(3.0, d)) + 0.5));   // Math.round
}
