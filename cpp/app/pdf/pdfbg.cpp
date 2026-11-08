#include "pdfbg.h"
#include "editor.h"
#include "mainwindow.h"
#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QTimer>
#include <algorithm>
#include <cmath>

Q_DECLARE_METATYPE(BackgroundRef)

// Rasterises on its own thread, with its own resolver (open PDFs are not shared between threads). One request at a time, so the queue order decided by the
// GUI thread (newest first) is what runs.
class PdfBgWorker : public QObject {
  Q_OBJECT
 public slots:
  void render(const BackgroundRef& ref, int widthPx, const QStringList& dirs, quint64 generation) {
    res_.setSearchDirs(dirs);
    QString err;
    const QImage img = res_.render(ref, widthPx, &err);
    emit done(ref, widthPx, img, img.isNull() && err.isEmpty() ? QStringLiteral("no se pudo dibujar el fondo") : err, generation);
  }
 signals:
  void done(const BackgroundRef& ref, int widthPx, const QImage& img, const QString& error, quint64 generation);

 private:
  BackgroundResolver res_;
};

namespace {
constexpr double kMaxPixels = 24e6;   // one page image at most ~96 MB; beyond that a very deep zoom shows the same picture enlarged
}

PdfBackgrounds::PdfBackgrounds(MainWindow& win) : QObject(&win), win_(win), ed_(win.editor()) {
  qRegisterMetaType<BackgroundRef>("BackgroundRef");
  qRegisterMetaType<QImage>("QImage");
  auto* w = new PdfBgWorker;
  worker_ = w;
  w->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, w, &QObject::deleteLater);
  connect(this, &PdfBackgrounds::work, w, &PdfBgWorker::render, Qt::QueuedConnection);
  connect(w, &PdfBgWorker::done, this, &PdfBackgrounds::onDone, Qt::QueuedConnection);
  thread_.start(QThread::LowPriority);   // the pen and the scrolling come first
  connect(&ed_, &Editor::documentReplaced, this, &PdfBackgrounds::documentChanged);
  connect(&ed_, &Editor::structureChanged, this, [this] {
    QString known;
    { QMutexLocker l(&mu_); known = docFile_; }
    if (normalizePdfReferences(ed_.doc(), known)) { QMutexLocker l(&mu_); docFile_ = documentPdfFile(ed_.doc()); }
    else if (const QString f = documentPdfFile(ed_.doc()); !f.isEmpty()) { QMutexLocker l(&mu_); docFile_ = f; }
  });
  documentChanged();
}

PdfBackgrounds::~PdfBackgrounds() {
  thread_.quit();
  thread_.wait();
}

QStringList PdfBackgrounds::searchDirs() const {
  QStringList d;
  const auto add = [&d](const QString& p) { if (!p.isEmpty() && !d.contains(p)) d << p; };
  if (!ed_.path().isEmpty()) add(QFileInfo(ed_.path()).absolutePath());
  int n = 0;
  for (const QString& f : win_.recentFiles()) { add(QFileInfo(f).absolutePath()); if (++n == 3) break; }
  add(QDir::currentPath());
  for (const QString& e : extraDirs_) add(e);
  return d;
}

void PdfBackgrounds::addSearchDir(const QString& dir) {
  if (dir.isEmpty()) return;
  {
    QMutexLocker l(&mu_);
    if (!extraDirs_.contains(dir)) extraDirs_ << dir;
    dirs_ = searchDirs();
    failed_.clear();
    reported_.clear();
    retried_.clear();
  }
  for (size_t i = 0; i < ed_.doc().pages.size(); i++) ed_.touch(int(i));   // every page that waited for the file asks again
}

void PdfBackgrounds::documentChanged() {
  const QString file = documentPdfFile(ed_.doc());
  const QStringList dirs = searchDirs();
  {
    QMutexLocker l(&mu_);
    docFile_ = file;
    dirs_ = dirs;
    generation_++;
    cache_.clear(); order_.clear(); widths_.clear(); bytes_ = 0;
    queue_.clear(); inflight_.clear(); failed_.clear(); busy_ = false;
    reported_.clear(); retried_.clear();
  }
  // openFile adds the file to the recent list right after this signal: look again once it has, so a PDF kept next to the .xopp is found
  QTimer::singleShot(0, this, [this] {
    const QStringList dirs = searchDirs();
    QMutexLocker l(&mu_);
    if (dirs != dirs_) { dirs_ = dirs; failed_.clear(); }
  });
}

int PdfBackgrounds::bucketFor(double wantPx, double widthPt, double heightPt) {
  static const int steps[] = {384, 512, 640, 800, 1024, 1280, 1600, 2048, 2560, 3200, 4096, 5120, 6400, 8192};
  const double maxW = std::sqrt(kMaxPixels * widthPt / std::max(1.0, heightPt));
  int best = steps[0];
  for (int s : steps) {
    if (s > maxW) break;
    best = s;
    if (s >= wantPx) break;
  }
  return best;
}

QImage PdfBackgrounds::provide(const Attrs& background, double widthPt, double heightPt, double scale) {
  QMutexLocker l(&mu_);
  const BackgroundRef ref = backgroundRefOf(background, docFile_);
  if (ref.kind == BackgroundRef::None || widthPt <= 0 || heightPt <= 0) return QImage();
  const Key page{ref.file, ref.kind == BackgroundRef::Pdf ? ref.pageNo : 0, 0};
  if (failed_.count(page)) return QImage();
  const int want = bucketFor(widthPt * scale, widthPt, heightPt);
  const Key exact{page.file, page.pageNo, want};
  auto it = cache_.find(exact);
  if (it != cache_.end()) {
    order_.splice(order_.end(), order_, it->second.lru);
    return it->second.img;
  }
  // stale-then-sharp: whatever size we have, drawn stretched, until the right one arrives
  QImage fallback;
  int have = 0;
  if (auto w = widths_.find(page); w != widths_.end()) {
    for (int cand : w->second)
      if (!have || std::abs(std::log(double(cand) / want)) < std::abs(std::log(double(have) / want))) have = cand;
    if (have) {
      auto f = cache_.find(Key{page.file, page.pageNo, have});
      if (f != cache_.end()) { order_.splice(order_.end(), order_, f->second.lru); fallback = f->second.img; }
    }
  }
  const auto enqueue = [&](int width) {
    const Key k{page.file, page.pageNo, width};
    if (inflight_.count(k)) return;
    auto q = std::find_if(queue_.begin(), queue_.end(), [&](const Wanted& x) { return x.width == width && x.ref == ref; });
    if (q != queue_.end()) queue_.erase(q);   // asked again: it is current, so it goes to the front of the line (the end of the vector)
    queue_.push_back({ref, width});
    stats_.requested++;
  };
  enqueue(want);
  if (fallback.isNull() && want > 512) enqueue(512);   // a quick low-resolution page first (popped first), the sharp one right after
  while (queue_.size() > 24) queue_.erase(queue_.begin());
  if (!pumpQueued_) {
    pumpQueued_ = true;
    QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
  }
  return fallback;
}

void PdfBackgrounds::pump() {
  QMutexLocker l(&mu_);
  pumpQueued_ = false;
  while (!busy_ && !queue_.empty()) {
    const Wanted w = queue_.back();
    queue_.pop_back();
    const Key k{w.ref.file, w.ref.kind == BackgroundRef::Pdf ? w.ref.pageNo : 0, w.width};
    if (cache_.count(k) || inflight_.count(k)) continue;
    inflight_[k] = true;
    busy_ = true;
    emit work(w.ref, w.width, dirs_, generation_);
  }
}

void PdfBackgrounds::evict() {
  while (bytes_ > budget_ && order_.size() > 1) {
    const Key old = order_.front();
    order_.pop_front();
    auto it = cache_.find(old);
    if (it == cache_.end()) continue;
    bytes_ -= it->second.img.sizeInBytes();
    cache_.erase(it);
    auto& v = widths_[Key{old.file, old.pageNo, 0}];
    v.erase(std::remove(v.begin(), v.end(), old.width), v.end());
  }
}

void PdfBackgrounds::onDone(const BackgroundRef& ref, int widthPx, const QImage& img, const QString& error, quint64 generation) {
  bool repaint = false, retry = false;
  QString report;
  {
    QMutexLocker l(&mu_);
    busy_ = false;
    const Key page{ref.file, ref.kind == BackgroundRef::Pdf ? ref.pageNo : 0, 0};
    const Key k{page.file, page.pageNo, widthPx};
    inflight_.erase(k);
    if (generation == generation_) {
      if (img.isNull()) {
        stats_.failed++;
        failed_[page] = true;
        const QString name = QFileInfo(ref.file).fileName();
        if (error.startsWith("no se encuentra") && !retried_.contains(ref.file)) {
          retried_ << ref.file;
          retry = true;   // the recent list may have been updated since the worker looked: check again below, outside the lock
        } else if (!reported_.contains(ref.file)) {
          reported_ << ref.file;
          report = error.isEmpty() ? "no se pudo leer " + name : error;
        }
      } else {
        auto [it, fresh] = cache_.try_emplace(k);
        if (!fresh) { bytes_ -= it->second.img.sizeInBytes(); order_.erase(it->second.lru); }
        it->second.img = img;
        it->second.lru = order_.insert(order_.end(), k);
        bytes_ += img.sizeInBytes();
        auto& v = widths_[page];
        if (std::find(v.begin(), v.end(), widthPx) == v.end()) v.push_back(widthPx);
        stats_.rendered++;
        stats_.lastWidth = widthPx;
        evict();
        repaint = true;
      }
    }
  }
  if (retry) {
    const QStringList dirs = searchDirs();
    BackgroundResolver probe;
    probe.setSearchDirs(dirs);
    QMutexLocker l(&mu_);
    dirs_ = dirs;
    if (!probe.locate(ref.file).isEmpty()) {
      failed_.erase(Key{ref.file, ref.kind == BackgroundRef::Pdf ? ref.pageNo : 0, 0});
      repaint = true;   // asks again with the new directories
    } else if (!reported_.contains(ref.file)) {
      reported_ << ref.file;
      report = "no se encuentra el archivo " + QFileInfo(ref.file).fileName();
      emit missing(ref.file);
    }
  }
  if (!report.isEmpty()) emit problem("Fondo PDF: " + report);
  if (repaint) repaintPagesOf(ref);
  pump();
}

void PdfBackgrounds::repaintPagesOf(const BackgroundRef& ref) {
  QString docFile;
  { QMutexLocker l(&mu_); docFile = docFile_; }
  const auto& pages = ed_.doc().pages;
  for (size_t i = 0; i < pages.size(); i++) {
    const BackgroundRef r = backgroundRefOf(pages[i].background, docFile);
    if (r.kind == ref.kind && r.file == ref.file && (r.kind != BackgroundRef::Pdf || r.pageNo == ref.pageNo)) ed_.touch(int(i));
  }
}

PdfBackgrounds::Stats PdfBackgrounds::stats() const {
  QMutexLocker l(&mu_);
  Stats s = stats_;
  s.cachedBytes = bytes_;
  s.cachedImages = int(cache_.size());
  return s;
}

bool PdfBackgrounds::idle() const {
  QMutexLocker l(&mu_);
  return queue_.empty() && !busy_ && !pumpQueued_;
}

#include "pdfbg.moc"
