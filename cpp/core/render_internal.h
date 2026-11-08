// Private to the page renderer (pagerender.cpp and render_stroke.cpp): not part of the contract other code calls.
#pragma once
#include "pagerender.h"

/** One stroke (pen, highlighter) with its glow when the options ask for it. The painter's transform maps page units to pixels. */
void renderStroke(QPainter& p, const Stroke& s, const RenderOptions& o);

/** Page-unit rectangle a stroke can touch: its points padded by half the widest width and the reach of its glow. Empty for a stroke without points. */
QRectF strokeBounds(const Stroke& s);

/** Forget every cached glow bitmap (a document was closed). */
void clearGlowCache();
