// Prototype of the page canvas: smooth pressure strokes, the stroke in progress on its own layer, and the finished page cached.
#pragma once
#include <QElapsedTimer>
#include <QPainter>
#include <QPixmap>
#include <QWidget>
#include <vector>

struct Pt { float x, y, p; };
struct Stroke { std::vector<Pt> pts; QColor color; float width; };

/** Draws a stroke piece by piece: quadratic curves between midpoints, width following pressure. The same sequence is used live and when the page is rebuilt, so both look identical. */
class StrokeRenderer {
 public:
  StrokeRenderer(QPainter& qp, const QColor& c, float width) : qp_(qp), color_(c), width_(width) {}
  QRectF add(const Pt& p);   // returns the area touched
  QRectF end();
 private:
  QRectF seg(const QPointF& a, const QPointF& c, const QPointF& b, float wa, float wb);
  float widthOf(float pressure) const;
  QPainter& qp_;
  QColor color_;
  float width_;
  Pt prev_{}, mid_{};
  int n_ = 0;
  float wmid_ = 0;
};

class Canvas : public QWidget {
  Q_OBJECT
 public:
  explicit Canvas(QWidget* parent = nullptr);
  void addRandomStrokes(int n);
  void rebuildPage();
  // measurement entry points (also used by --bench)
  void liveBegin(const Pt& p);
  void liveAdd(const Pt& p);
  void liveEnd();
  size_t strokeCount() const { return strokes_.size(); }
  void undo();
  void clearAll();
  QColor color = QColor(20, 20, 30);
  float penWidth = 2.2f;
 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void tabletEvent(QTabletEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
 private:
  void ensureLayers();
  QPixmap newLayer(bool opaque) const;
  std::vector<Stroke> strokes_;
  QPixmap page_, live_;
  Stroke cur_;
  bool drawing_ = false;
  bool tabletDown_ = false;
  QPainter livePainter_;
  StrokeRenderer* liveRender_ = nullptr;
  QElapsedTimer inputClock_;
  qint64 lastInputNs_ = 0;
  double lastLatencyMs_ = 0, worstLatencyMs_ = 0;
  int frames_ = 0;
  QElapsedTimer fpsClock_;
  double fps_ = 0;
  int eventsSince_ = 0, evPerSec_ = 0;
  QElapsedTimer evClock_;
};
