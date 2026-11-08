// The glow of a pen stroke while it is being drawn: the very filter the page renderer applies to the finished stroke (render_stroke.cpp glowCascade), run
// incrementally. The live ink is kept in a bitmap; every update recomputes only the pixels that the newly drawn ink can change (its own area plus the reach
// of the three chained blurs), so the cost per update follows the size of the new piece and the glow, never the length of the stroke, and the result is,
// pixel for pixel, what the renderer produces for the same ink (same colour, alpha, radius, falloff, composite).
#pragma once
#include <QColor>
#include <QImage>
#include <QRect>

/** Glow of a stroke as the renderer reads it: size (page units, at most 40) and intensity (0.05..1). The one place that clamps the stored attributes. */
struct GlowSpec { double size = 0, intensity = 0; };
GlowSpec glowSpec(double size, double intensity);

/** Device pixels the glow filter reaches beyond the ink at scale `k`. */
int glowReach(const GlowSpec& spec, double k);

class LiveGlow {
 public:
  /** `size` pixels of the live layer, `k` device pixels per page unit (the scale the finished stroke is rendered at). */
  LiveGlow(QSize size, const QColor& colour, const GlowSpec& spec, double k);
  /** Pixels farther than this from new ink are not changed by it. */
  int reach() const { return reach_; }
  /** `ink` (premultiplied ARGB, same size as the layer) has new or changed pixels inside `dirty` (device pixels): recomputes the glow there and returns the area
   *  that changed. */
  QRect update(const QImage& ink, const QRect& dirty);
  /** The glow with the ink in it: the colour at the combined alpha, premultiplied. Draw it in place of the ink. */
  const QImage& image() const { return out_; }

 private:
  QColor colour_;
  GlowSpec spec_;
  double k_;
  int reach_;
  QImage out_;
};
