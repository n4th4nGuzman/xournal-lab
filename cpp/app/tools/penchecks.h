// Self-test checks for the feel of the pen (live layer == finished stroke, width mapping, start of a stroke, file round trip) and for the tile pipeline
// of dense pages (index path == full scan, partial repaint, worker threads, stale results). Called from the self test; reports through the two callbacks.
#pragma once
#include <QString>
#include <functional>

class Editor;
class MainWindow;

struct CheckReporter {
  std::function<void(bool, const QString&)> check;   // one PASS/FAIL line
  std::function<void(const QString&)> out;           // a line of numbers
};

void penFeelChecks(MainWindow& win, Editor& ed, const CheckReporter& r);
void denseTileChecks(MainWindow& win, Editor& ed, const CheckReporter& r);
/** Audit of the pen: zoom, single page, fingers and palm, laser, hand, highlighter and glow while drawing, thumbnails of dense pages. */
void penAuditChecks(MainWindow& win, Editor& ed, const CheckReporter& r);
/** Before/after table for dense pages (0 / 8000 / 20000 extra strokes): cold tiles, undo, zoom and scroll steps, memory. "before" = every element tested per tile on the
 *  GUI thread with no piece culling (the earlier path), "after" = spatial index, piece culling, partial repaint, worker threads. */
void denseTileBench(MainWindow& win, Editor& ed, const CheckReporter& r);
