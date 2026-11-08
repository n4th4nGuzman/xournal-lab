// The file lifecycle with the real documents of samples/: open each one, save it somewhere else, open that copy and compare it with the original through
// the model's own equality (and through the serialised XML, which must also match what a second round trip produces). Plus the exact-span find
// boxes (core/find.h) that the find bar draws.
#include "find.h"
#include "xopp.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while (0)

static Text textBox(const QString& text, bool markdown) {
  Text t;
  t.text = text;
  attrSet(t.attrs, "font", "Sans");
  attrSet(t.attrs, "size", "20");
  attrSet(t.attrs, "x", "100");
  attrSet(t.attrs, "y", "50");
  attrSet(t.attrs, "color", "#000000ff");
  if (markdown) attrSet(t.attrs, "xlMd", "1");
  return t;
}

static void findSpans() {
  Document d;
  d.pages.resize(1);
  d.pages[0].layers.resize(1);
  auto& els = d.pages[0].layers[0].elements;
  els.emplace_back(textBox("hola mundo, hola", false));
  els.emplace_back(textBox("**hola** mundo", true));
  els.emplace_back(textBox("line one\nsecond hola line", false));
  const auto hits = findText(d, "hola");
  CHECK(hits.size() == 3);
  if (hits.size() != 3) return;
  const auto plain = findSpanBoxes(d, hits[0]);
  CHECK(plain.size() == 2);   // both matches of the box, each a part of the box (not the whole of it)
  if (plain.size() == 2) {
    const auto whole = *findHitBox(d, hits[0]);
    CHECK(plain[0][0] >= whole[0] - 0.01 && plain[0][0] < plain[0][2] && plain[1][0] > plain[0][2]);
    CHECK(plain[1][2] <= whole[2] + 1.0);
    CHECK(plain[0][2] - plain[0][0] < (whole[2] - whole[0]) * 0.5);   // "hola" is under half of "hola mundo, hola"
    CHECK(plain[0][0] <= whole[0] + 0.01);                              // starts at the box's left edge
  }
  const auto md = findSpanBoxes(d, hits[1]);   // the markup characters are not drawn: the match is where "hola" is drawn
  CHECK(md.size() == 1 && md[0][2] > md[0][0] && md[0][0] <= 100.5);
  const auto second = findSpanBoxes(d, hits[2]);   // second line of the box
  CHECK(second.size() == 1 && second[0][1] > 50 + 20 * 1.2 - 0.01 && second[0][0] > 100);
  // a formula has no glyphs to point at: the caller marks the whole box
  Image f;
  f.tag = "teximage";
  attrSet(f.attrs, "text", "x = hola");
  for (const char* k : {"left", "top", "right", "bottom"}) attrSet(f.attrs, k, "10");
  els.emplace_back(std::move(f));
  const auto fh = findText(d, "hola");
  CHECK(fh.size() == 4 && findSpanBoxes(d, fh[3]).empty() && findHitBox(d, fh[3]).has_value());
  // a case-insensitive match keeps the case it has in the text
  const auto up = findText(d, "HOLA");
  CHECK(up.size() == 4 && findSpanBoxes(d, up[0]).size() == 2);
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);   // text metrics need the font database
  findSpans();

  QTemporaryDir tmp;
  CHECK(tmp.isValid());
  const QDir samples(QStringLiteral("G:/xournal-lab/samples"));
  const QStringList names = samples.entryList({"*.xopp"}, QDir::Files, QDir::Name);
  CHECK(names.size() == 6);
  for (const QString& n : names) {
    const QString src = samples.filePath(n);
    LoadResult a = loadXopp(src);
    CHECK(a.ok);
    if (!a.ok) { std::printf("  %s: %s\n", qPrintable(n), qPrintable(a.error)); continue; }
    const QString dst = tmp.filePath("copia-" + n);
    QString err;
    CHECK(saveXopp(dst, a.doc, &err));
    LoadResult b = loadXopp(dst);
    CHECK(b.ok);
    if (!b.ok) continue;
    const bool same = a.doc == b.doc;
    CHECK(same);
    // the XML of the copy is what the original's model serialises to, and a second generation changes nothing
    CHECK(serializeXml(a.doc) == serializeXml(b.doc));
    QString err2;
    const QString dst2 = tmp.filePath("copia2-" + n);
    CHECK(saveXopp(dst2, b.doc, &err2));
    LoadResult c = loadXopp(dst2);
    CHECK(c.ok && c.doc == a.doc);
    CHECK(a.skipped.isEmpty() == b.skipped.isEmpty());
    // the temporary file of the atomic write is gone
    CHECK(!QFile::exists(dst + ".tmp") && QDir(tmp.path()).entryList({"*.tmp"}).isEmpty());
    std::printf("  %-34s pages %d  skipped %lld  %s\n", qPrintable(n), int(a.doc.pages.size()), static_cast<long long>(a.skipped.size()), same ? "same" : "DIFFERENT");
  }
  // overwriting an existing file replaces it completely (no leftovers of a longer old file)
  if (!names.isEmpty()) {
    LoadResult big = loadXopp(samples.filePath(names.first()));
    Document tiny;
    tiny.pages.resize(1);
    tiny.pages[0].layers.resize(1);
    const QString f = tmp.filePath("sobrescrito.xopp");
    CHECK(saveXopp(f, big.doc));
    CHECK(saveXopp(f, tiny));
    LoadResult r = loadXopp(f);
    CHECK(r.ok && r.doc == tiny);
  }
  if (fails) { std::printf("%d check(s) failed\n", fails); return 1; }
  std::printf("lifecycle: all checks passed\n");
  return 0;
}
