// Parity with the Electron app's paper code. tests/data/paper_parity holds what the Electron app itself produced (tools/paperparity: its own
// paper catalogue as JSON, and for each case the .xopp its writer emits plus the PNG its renderer draws at 1x). Here the catalogue must match
// exactly (ids, labels, look-alike style, every option and its order, so the defaults), the files must load with the same xlPaper text and
// round trip, and the C++ renderer must draw the same page: only anti-aliasing and glyph differences are tolerated.
// Logarithmic paper is not compared pixel by pixel on purpose: Electron's drawLog has a dangling `else` (it binds to the inner `if`), so its
// log-log minor lines and its semilog vertical minor lines are wrong; the C++ port draws what the README describes.
#include "paper.h"
#include "pagerender.h"
#include "xopp.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static QJsonArray readArray(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).array();
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  const QString dir = QStringLiteral("tests/data/paper_parity/");

  // ---- catalogue ------------------------------------------------------------------------------------------------------------------------------
  const QJsonArray kinds = readArray(dir + "kinds.json");
  CHECK(kinds.size() == 7 && size_t(kinds.size()) == paperKinds().size(), "kinds: %d in Electron, %zu here", int(kinds.size()), paperKinds().size());
  for (int i = 0; i < kinds.size() && size_t(i) < paperKinds().size(); i++) {
    const QJsonObject e = kinds[i].toObject();
    const PaperKind& k = paperKinds()[size_t(i)];
    CHECK(k.id == e["id"].toString() && k.label == e["label"].toString() && k.fallback == e["fallback"].toString(), "kind %d: id/label/fallback differ (%s)", i, qPrintable(k.id));
    const QJsonArray ps = e["params"].toArray();
    CHECK(size_t(ps.size()) == k.params.size(), "kind %s: %d params in Electron, %zu here", qPrintable(k.id), int(ps.size()), k.params.size());
    for (int j = 0; j < ps.size() && size_t(j) < k.params.size(); j++) {
      const QJsonObject p = ps[j].toObject();
      const PaperParam& q = k.params[size_t(j)];
      CHECK(q.key == p["key"].toString() && q.label == p["label"].toString(), "kind %s param %d: key/label differ", qPrintable(k.id), j);
      const QJsonArray os = p["options"].toArray();
      bool same = size_t(os.size()) == q.options.size();
      for (int o = 0; same && o < os.size(); o++) same = q.options[size_t(o)].first == os[o].toArray()[0].toString() && q.options[size_t(o)].second == os[o].toArray()[1].toString();
      CHECK(same, "kind %s param %s: options (values, labels, order) differ", qPrintable(k.id), qPrintable(q.key));
    }
  }

  // ---- files and pixels -----------------------------------------------------------------------------------------------------------------------
  const QJsonArray cases = readArray(dir + "cases.json");
  CHECK(cases.size() >= 25, "cases: %d", int(cases.size()));
  for (const QJsonValue& cv : cases) {
    const QString name = cv.toObject()["name"].toString();
    const QString value = cv.toObject()["value"].toString();
    const LoadResult r = loadXopp(dir + name + ".electron.xopp");
    CHECK(r.ok && r.doc.pages.size() == 1, "%s: Electron's file does not load: %s", qPrintable(name), qPrintable(r.error));
    if (!r.ok || r.doc.pages.empty()) continue;
    const Page& page = r.doc.pages[0];
    const auto spec = paperOfBackground(page.background);
    if (value.startsWith("x:")) {
      const QString raw = *attrFind(page.background, "xlPaper");
      CHECK(spec && formatSpec(*spec) == raw, "%s: xlPaper \"%s\" does not round trip through parse/format", qPrintable(name), qPrintable(raw));
      const PaperKind* k = spec ? paperKind(spec->kind) : nullptr;
      CHECK(k && attrOr(page.background, "style") == k->fallback, "%s: look-alike style differs", qPrintable(name));
    } else {
      CHECK(!spec && attrOr(page.background, "style") == value, "%s: built-in style", qPrintable(name));
    }
    QImage want(dir + name + ".electron.png");
    want = want.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage got(want.size(), QImage::Format_ARGB32_Premultiplied);
    got.fill(Qt::transparent);
    CHECK(want.width() == int(std::ceil(page.width())) && want.height() == int(std::ceil(page.height())), "%s: size %dx%d", qPrintable(name), want.width(), want.height());
    {
      QPainter p(&got);
      RenderOptions o;
      o.scale = 1;
      renderPage(p, page, o);
    }
    long far = 0, near = 0;
    for (int y = 0; y < want.height(); y++)
      for (int x = 0; x < want.width(); x++) {
        const QRgb a = want.pixel(x, y), b = got.pixel(x, y);
        const int d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
        if (d > 80) far++;
        else if (d > 24) near++;
      }
    const double total = double(want.width()) * want.height();
    std::printf("  %-42s far %6.3f%%  near %6.3f%%\n", qPrintable(name), 100.0 * far / total, 100.0 * near / total);
    CHECK(far / total < 0.004, "%s: %.3f%% of the pixels differ a lot from the Electron renderer", qPrintable(name), 100.0 * far / total);
    CHECK(near / total < 0.01, "%s: %.3f%% of the pixels differ noticeably from the Electron renderer", qPrintable(name), 100.0 * near / total);
  }
  std::printf(failures ? "paper_parity: %d FAILURES\n" : "paper_parity: all OK\n", failures);
  return failures ? 1 : 0;
}
