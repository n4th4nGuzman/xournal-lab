// The on-screen side of PDF/picture page backgrounds: answers renderBackground's provider hook (any rendering thread, never blocking), rasterises pages on
// one worker thread at the size the zoom needs, keeps a bounded cache and repaints the pages that were waiting when a picture arrives.
//
// Why a provider and not a bgImage argument: the tile code (and its workers) only has a copy of the page's <background> attributes, and the file name of a
// PDF page lives on the document's first PDF page; the provider gets the attributes and this object knows the document's PDF name.
#pragma once
#include "model.h"
#include "pdfbackground.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <list>
#include <unordered_map>
#include <memory>
#include <vector>

class Editor;
class MainWindow;

class PdfBackgrounds : public QObject {
  Q_OBJECT
 public:
  explicit PdfBackgrounds(MainWindow& win);
  ~PdfBackgrounds() override;

  /** The hook (thread-safe, non-blocking): the picture ready for this background at about this scale, any size, or null after queueing the work. */
  QImage provide(const Attrs& background, double widthPt, double heightPt, double scale);

  /** The directories where a PDF that moved is looked for by name: next to the document, where it was opened from, the current directory. */
  QStringList searchDirs() const;
  /** A folder the user pointed at ("Buscar el PDF…"): searched too, and everything that failed to find its file is asked again. */
  void addSearchDir(const QString& dir);

  struct Stats { int requested = 0, rendered = 0, failed = 0; qint64 cachedBytes = 0; int cachedImages = 0; int lastWidth = 0; };
  Stats stats() const;
  bool idle() const;                       // nothing queued or running
  void setBudgetBytes(qint64 b) { budget_ = b; }

 signals:
  void problem(const QString& message);    // a file is missing or unreadable: shown once per file
  void missing(const QString& file);       // the same, for a file that is nowhere (the application offers to locate it)
  void work(const BackgroundRef& ref, int widthPx, const QStringList& dirs, quint64 generation);   // to the worker

 private slots:
  void onDone(const BackgroundRef& ref, int widthPx, const QImage& img, const QString& error, quint64 generation);
  void pump();
  void documentChanged();

 private:
  struct Key {
    QString file; int pageNo; int width;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash { size_t operator()(const Key& k) const { return qHash(k.file) ^ (size_t(k.pageNo) * 2654435761u) ^ (size_t(k.width) << 7); } };
  struct Item { QImage img; std::list<Key>::iterator lru; };
  struct Wanted { BackgroundRef ref; int width; };

  static int bucketFor(double wantPx, double widthPt, double heightPt);
  void repaintPagesOf(const BackgroundRef& ref);
  void evict();   // mutex held

  MainWindow& win_;
  Editor& ed_;
  QThread thread_;
  QObject* worker_ = nullptr;   // PdfBgWorker (pdfbg.cpp), lives in thread_
  mutable QMutex mu_;
  QString docFile_;                         // the document's PDF as written (first PDF page that names it)
  QStringList dirs_;
  quint64 generation_ = 1;                  // bumped when the document is replaced: results of older requests are dropped
  std::unordered_map<Key, Item, KeyHash> cache_;
  std::list<Key> order_;                    // least recently used first
  qint64 bytes_ = 0, budget_ = 320ll << 20;
  std::vector<Wanted> queue_;               // newest last: the newest request is what the user is looking at
  std::unordered_map<Key, bool, KeyHash> inflight_, failed_;
  std::unordered_map<Key, std::vector<int>, KeyHash> widths_;   // (file, page, 0) -> widths cached for it, for the stale-then-sharp fallback
  QStringList retried_;
  QStringList extraDirs_;                   // folders chosen by the user while locating a PDF (this session)
  bool busy_ = false, pumpQueued_ = false;
  QStringList reported_;
  Stats stats_;
};
