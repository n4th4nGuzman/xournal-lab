// The loader against hostile and unusual files: truncated or corrupt gzip, empty input, unknown elements (kept, not lost), non-finite numbers, strokes of
// 0 and 1 points, and size (100k strokes, 500 pages, a 50 MB image). Nothing may crash or hang, and every failure must come with a Spanish message.
#include "xopp.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static QByteArray readFile(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }

static LoadResult loadBytes(const QByteArray& raw) {
  QTemporaryDir dir;
  const QString p = dir.filePath("t.xopp");
  QFile f(p);
  if (!f.open(QIODevice::WriteOnly)) return {};
  f.write(raw);
  f.close();
  return loadXopp(p);
}

static const char* kHead = "<?xml version=\"1.0\" standalone=\"no\"?><xournal creator=\"t\" fileversion=\"4\"><title>t</title>";
static QByteArray xmlWith(const QString& body) { return QByteArray(kHead) + body.toUtf8() + "</xournal>"; }
static QByteArray oneLayer(const QString& layerBody) {
  return xmlWith("<page width=\"100\" height=\"100\"><background type=\"solid\" color=\"#ffffffff\" style=\"plain\"/><layer>" + layerBody + "</layer></page>");
}

/** A Spanish message: not empty and none of the English words Qt's reader uses. */
static bool spanish(const QString& e) {
  static const QRegularExpression english(R"(\b(document|tag|entity|character|expected|invalid|content|encoded|sequence|attribute|declaration|error|premature|failed|cannot)\b)", QRegularExpression::CaseInsensitiveOption);
  return !e.isEmpty() && !e.contains(english);
}

static void unknownElements() {
  const QByteArray xml = xmlWith(
      "<custom a=\"1\" xml:lang=\"es\">texto<b x=\"&amp;\">dentro</b> cola</custom>"
      "<page width=\"200\" height=\"100\"><background type=\"solid\" color=\"#ffffffff\" style=\"plain\"/>"
      "<timeline ts=\"5\"/>"
      "<layer name=\"A\">"
      "<timestamp ts=\"1700000000\" fn=\"rec.mp3\"/>"
      "<stroke tool=\"pen\" color=\"#000000ff\" width=\"1\" fn=\"rec.mp3\" ts=\"123\">1 2 3 4</stroke>"
      "<audio fn=\"rec.mp3\"><chunk>1</chunk></audio>"
      "<text font=\"Sans\" size=\"12\" x=\"1\" y=\"2\" color=\"#000000ff\">hola</text>"
      "<x:foo xmlns:x=\"urn:x\" x:k=\"v\"><x:bar/></x:foo>"
      "</layer></page>"
      "<tail q=\"1\"/>");
  const LoadResult a = parseXml(xml);
  CHECK(a.ok, "parse: %s", qPrintable(a.error));
  if (!a.ok) return;
  CHECK(a.skipped.isEmpty(), "nothing is dropped any more");
  CHECK(a.preserved.size() == 6, "six unknown elements kept, got %d", int(a.preserved.size()));
  const QByteArray out = serializeXml(a.doc);
  const LoadResult b = parseXml(out);
  CHECK(b.ok && b.doc == a.doc, "unknown elements survive serialize + parse (%s)", qPrintable(b.error));
  const QString s = QString::fromUtf8(out);
  auto at = [&](const char* needle) { return s.indexOf(QString::fromUtf8(needle)); };
  CHECK(at("<timestamp ts=\"1700000000\" fn=\"rec.mp3\"") > 0 && at("<timestamp") < at("<stroke"), "the timestamp stays before the stroke it describes");
  CHECK(at("<audio") > at("<stroke") && at("<audio") < at("<text"), "the audio element stays between the stroke and the text");
  CHECK(at("<x:foo") > at("<text") && at("xmlns:x=\"urn:x\"") > 0, "prefixed element and its namespace declaration kept");
  CHECK(at("<custom") < at("<page") && at("<tail") > at("</page>"), "root-level unknown elements keep their side of the pages");
  CHECK(s.contains("<b x=\"&amp;\">dentro</b> cola"), "mixed content kept");
  CHECK(at("<timeline") > at("<background") && at("<timeline") < at("<layer"), "page-level unknown element between background and layer");
  // deleting a known element must not strand the unknown ones: they are written at the end of the layer
  Document d = a.doc;
  d.pages[0].layers[0].elements.clear();
  const LoadResult c = parseXml(serializeXml(d));
  CHECK(c.ok && c.doc.pages[0].layers[0].extra.size() == 3, "extras survive when their neighbours are gone");
  // too deep: reported, not a stack overflow
  QString deep;
  for (int i = 0; i < 5000; i++) deep += "<d>";
  for (int i = 0; i < 5000; i++) deep += "</d>";
  const LoadResult e = parseXml(oneLayer(deep));
  CHECK(!e.ok && spanish(e.error), "5000 nested unknown elements are refused politely: %s", qPrintable(e.error));
}

static void numbers() {
  struct Case { const char* body; bool ok; };
  const Case cases[] = {
      {"<stroke tool=\"pen\" width=\"1\">1 2 3 4</stroke>", true},
      {"<stroke tool=\"pen\" width=\"1\">+1 +2 3 4</stroke>", true},
      {"<stroke tool=\"pen\" width=\"1\">1e2 -2.5e-3 3 4</stroke>", true},
      {"<stroke tool=\"pen\" width=\"1\">nan 2 3 4</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">1 inf 3 4</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">1 -inf 3 4</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">1 2 1e999 4</stroke>", false},
      {"<stroke tool=\"pen\" width=\"nan\">1 2 3 4</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">1,5 2</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">1 2 3</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">0x10 2</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\">abc</stroke>", false},
      {"<stroke tool=\"pen\" width=\"1\"></stroke>", true},          // 0 points
      {"<stroke tool=\"pen\" width=\"1\"/>", true},
      {"<stroke tool=\"pen\" width=\"1\">5 5</stroke>", true},       // 1 point
      {"<stroke tool=\"pen\" width=\"\">1 2 3 4</stroke>", true},    // no widths at all
      {"<stroke tool=\"pen\" width=\"1\"><b/></stroke>", false},     // child element inside a stroke
      {"<text>sin atributos</text>", true},
      {"<image left=\"nan\" top=\"x\">AAAA</image>", true},          // attributes stay strings; the renderer must cope
  };
  for (const Case& c : cases) {
    const LoadResult r = parseXml(oneLayer(c.body));
    CHECK(r.ok == c.ok, "%s -> ok=%d (%s)", c.body, int(r.ok), qPrintable(r.error));
    if (!r.ok) CHECK(spanish(r.error), "message in Spanish for %s: '%s'", c.body, qPrintable(r.error));
  }
  const LoadResult plus = parseXml(oneLayer("<stroke tool=\"pen\" width=\"1\">+1 +2 3 4</stroke>"));
  CHECK(plus.ok && std::get<Stroke>(plus.doc.pages[0].layers[0].elements[0]).pts == std::vector<double>({1, 2, 3, 4}), "leading plus reads as the number");
}

static void corrupt() {
  const QByteArray empty;
  LoadResult r = loadBytes(empty);
  CHECK(!r.ok && spanish(r.error), "empty file: '%s'", qPrintable(r.error));
  r = loadBytes("not xml at all");
  CHECK(!r.ok && spanish(r.error), "garbage: '%s'", qPrintable(r.error));
  r = loadBytes("<xournal><page");
  CHECK(!r.ok && spanish(r.error), "truncated xml: '%s'", qPrintable(r.error));
  r = loadBytes("<html></html>");
  CHECK(!r.ok && spanish(r.error), "wrong root: '%s'", qPrintable(r.error));
  r = loadBytes(QByteArray("\x1f\x8b", 2));
  CHECK(!r.ok && spanish(r.error), "gzip magic only: '%s'", qPrintable(r.error));
  r = loadBytes(QByteArray("\x1f\x8b\x08\x00garbage-after-the-header", 30));
  CHECK(!r.ok && spanish(r.error), "gzip header then garbage: '%s'", qPrintable(r.error));
  CHECK(!loadXopp("Z:/no/such/dir/file.xopp").ok && spanish(loadXopp("Z:/no/such/dir/file.xopp").error), "missing file");
  CHECK(!loadXopp(QDir::tempPath()).ok, "a directory");

  // every sample: truncated at many points and with flipped bytes. Either it loads or says why; it never crashes or hangs.
  QDir samples(QCoreApplication::applicationDirPath() + "/../../samples");
  if (!samples.exists()) samples.setPath("G:/xournal-lab/samples");
  const QStringList files = samples.entryList({"*.xopp"}, QDir::Files);
  CHECK(!files.isEmpty(), "samples folder not found");
  QRandomGenerator rng(12345);
  int tried = 0, loaded = 0;
  for (const QString& name : files) {
    const QByteArray gz = readFile(samples.filePath(name));
    QByteArray xml;
    QString err;
    CHECK(gunzip(gz, &xml, &err), "gunzip %s", qPrintable(name));
    const int reps = std::clamp(int(60e6 / double(std::max<qsizetype>(1, xml.size()))), 3, 40);   // big samples get fewer rounds: each one parses the whole file
    for (int i = 0; i < reps; i++) {   // gzip cut anywhere
      const qsizetype n = qsizetype(rng.bounded(quint32(gz.size())));
      const LoadResult t = loadBytes(gz.left(n));
      tried++;
      if (t.ok) loaded++; else CHECK(spanish(t.error), "truncated gzip at %lld: '%s'", (long long)n, qPrintable(t.error));
    }
    for (int i = 0; i < reps; i++) {   // XML cut anywhere
      const qsizetype n = qsizetype(rng.bounded(quint32(xml.size())));
      const LoadResult t = parseXml(xml.left(n));
      tried++;
      if (t.ok) loaded++; else CHECK(spanish(t.error), "truncated xml at %lld: '%s'", (long long)n, qPrintable(t.error));
    }
    for (int i = 0; i < reps; i++) {   // flipped bytes in the XML (some land inside numbers, some inside tags)
      QByteArray m = xml;
      for (int k = 0; k < 1 + i % 5; k++) m[qsizetype(rng.bounded(quint32(m.size())))] = char(rng.bounded(256u));
      const LoadResult t = parseXml(m);
      tried++;
      if (t.ok) { loaded++; QByteArray again = serializeXml(t.doc); CHECK(parseXml(again).ok, "a document that loaded must save to something that loads"); }
      else CHECK(spanish(t.error), "flipped bytes: '%s'", qPrintable(t.error));
    }
    for (int i = 0; i < std::max(2, reps / 2); i++) {   // flipped bytes in the gzip (CRC or deflate errors)
      QByteArray m = gz;
      m[qsizetype(rng.bounded(quint32(m.size())))] = char(rng.bounded(256u));
      const LoadResult t = loadBytes(m);
      tried++;
      if (t.ok) loaded++; else CHECK(spanish(t.error), "flipped gzip byte: '%s'", qPrintable(t.error));
    }
  }
  std::printf("corrupt: %d inputs, %d still loaded, the rest were refused with a message\n", tried, loaded);
}

static void scale() {
  QElapsedTimer t;
  {  // 100 000 strokes of 8 points
    Document d;
    d.rootAttrs = {{"creator", "t"}, {"fileversion", "4"}};
    Page p;
    p.attrs = {{"width", "600"}, {"height", "800"}};
    p.background = {{"type", "solid"}, {"color", "#ffffffff"}, {"style", "plain"}};
    Layer l;
    QRandomGenerator rng(7);
    for (int i = 0; i < 100000; i++) {
      Stroke s;
      s.attrs = {{"tool", "pen"}, {"color", "#000000ff"}, {"capStyle", "round"}};
      s.widths = {1.41};
      const double x = rng.bounded(600.0), y = rng.bounded(800.0);
      for (int k = 0; k < 8; k++) { s.pts.push_back(x + k * 2.5); s.pts.push_back(y + (k % 3)); }
      l.elements.emplace_back(std::move(s));
    }
    p.layers.push_back(std::move(l));
    d.pages.push_back(std::move(p));
    QTemporaryDir dir;
    const QString f = dir.filePath("big.xopp");
    t.start();
    QString err;
    CHECK(saveXopp(f, d, &err), "save 100k: %s", qPrintable(err));
    const double saveMs = double(t.restart());
    const LoadResult r = loadXopp(f);
    const double loadMs = double(t.elapsed());
    CHECK(r.ok && countOf(r.doc).strokes == 100000, "100k strokes load back (%s)", qPrintable(r.error));
    CHECK(r.ok && r.doc == d, "100k strokes are equal after the round trip");
    CHECK(saveMs < 20000 && loadMs < 20000, "100k strokes: save %.0f ms, load %.0f ms", saveMs, loadMs);
    std::printf("100k strokes: save %.0f ms, load %.0f ms, file %lld KB\n", saveMs, loadMs, (long long)(QFileInfo(f).size() / 1024));
  }
  {  // 500 pages
    Document d;
    d.rootAttrs = {{"creator", "t"}, {"fileversion", "4"}};
    for (int i = 0; i < 500; i++) {
      Page p;
      p.attrs = {{"width", "595.27"}, {"height", "841.89"}};
      p.background = {{"type", "solid"}, {"color", "#ffffffff"}, {"style", "lined"}};
      Layer l;
      Stroke s;
      s.attrs = {{"tool", "pen"}, {"color", "#ff0000ff"}};
      s.widths = {2};
      s.pts = {10, 10, 100.5, 100.25 + i};
      l.elements.emplace_back(s);
      Text tx;
      tx.attrs = {{"font", "Sans"}, {"size", "12"}, {"x", "50"}, {"y", "50"}, {"color", "#000000ff"}};
      tx.text = QString("página %1").arg(i + 1);
      l.elements.emplace_back(tx);
      p.layers.push_back(std::move(l));
      d.pages.push_back(std::move(p));
    }
    QTemporaryDir dir;
    const QString f = dir.filePath("pages.xopp");
    t.start();
    QString err;
    CHECK(saveXopp(f, d, &err), "save 500 pages");
    const LoadResult r = loadXopp(f);
    CHECK(r.ok && r.doc == d && r.doc.pages.size() == 500, "500 pages round trip");
    std::printf("500 pages: save + load %lld ms\n", (long long)t.elapsed());
  }
  {  // a 50 MB image (base64 text; the bytes are random so it is not a picture: the loader must not care)
    QByteArray raw(37 * 1024 * 1024, 0);
    QRandomGenerator rng(9);
    for (qsizetype i = 0; i + 4 <= raw.size(); i += 4) { const quint32 v = rng.generate(); memcpy(raw.data() + i, &v, 4); }
    Document d;
    d.rootAttrs = {{"creator", "t"}, {"fileversion", "4"}};
    Page p;
    p.attrs = {{"width", "600"}, {"height", "800"}};
    p.background = {{"type", "solid"}, {"color", "#ffffffff"}, {"style", "plain"}};
    Layer l;
    Image im;
    im.tag = "image";
    im.attrs = {{"left", "10"}, {"top", "10"}, {"right", "300"}, {"bottom", "300"}};
    im.data = QString::fromLatin1(raw.toBase64());
    l.elements.emplace_back(std::move(im));
    p.layers.push_back(std::move(l));
    d.pages.push_back(std::move(p));
    QTemporaryDir dir;
    const QString f = dir.filePath("img.xopp");
    t.start();
    QString err;
    CHECK(saveXopp(f, d, &err), "save 50 MB image: %s", qPrintable(err));
    const double saveMs = double(t.restart());
    const LoadResult r = loadXopp(f);
    CHECK(r.ok && r.doc == d, "50 MB image round trip (%s)", qPrintable(r.error));
    std::printf("50 MB image: save %.0f ms, load %lld ms\n", saveMs, (long long)t.elapsed());
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  unknownElements();
  numbers();
  corrupt();
  scale();
  if (failures) std::printf("%d check(s) failed\n", failures);
  else std::printf("robust_load: all checks passed\n");
  return failures ? 1 : 0;
}
