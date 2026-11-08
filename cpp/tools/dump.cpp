// xl_dump [--resave OUT] FILE : prints the document as canonical JSON (what the Electron parser must also see), optionally re-saving it first.
// Used by the interchange check (PARIDAD/qa.md): the same dump is produced from the Electron renderer's parseXopp and compared.
#include "xopp.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cstdio>

static QJsonArray attrsJson(const Attrs& a, bool dropWidth = false) {
  QJsonArray r;
  for (const auto& kv : a)
    if (!(dropWidth && kv.first == "width")) r.append(QJsonArray{kv.first, kv.second});
  return r;
}
static QJsonArray numsJson(const std::vector<double>& v) {
  QJsonArray r;
  for (double d : v) r.append(d);
  return r;
}
static QString sha(QString s) {   // FNV-1a over the whitespace-free text (the renderer has no crypto in a data: page)
  s.remove(QRegularExpression("\\s+"));
  const QByteArray b = s.toUtf8();
  quint32 h = 2166136261u;
  for (uchar c : b) { h ^= c; h *= 16777619u; }
  return QString::number(h, 16);
}

static QJsonObject elementJson(const Element& e) {
  QJsonObject o;
  if (const auto* s = std::get_if<Stroke>(&e)) {
    o["k"] = "stroke"; o["attrs"] = attrsJson(s->attrs, true); o["widths"] = numsJson(s->widths); o["pts"] = numsJson(s->pts);
  } else if (const auto* t = std::get_if<Text>(&e)) {
    o["k"] = "text"; o["attrs"] = attrsJson(t->attrs); o["text"] = t->text;
  } else {
    const auto& im = std::get<Image>(e);
    o["k"] = im.tag; o["attrs"] = attrsJson(im.attrs); o["sha"] = sha(im.data); o["len"] = im.data.size();
  }
  return o;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QString resave, path;
  for (int i = 1; i < argc; i++) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == "--resave" && i + 1 < argc) resave = QString::fromLocal8Bit(argv[++i]);
    else path = a;
  }
  LoadResult r = loadXopp(path);
  if (!r.ok) { std::fprintf(stderr, "FAIL %s\n", qPrintable(r.error)); return 1; }
  if (!resave.isEmpty()) {
    QString err;
    if (!saveXopp(resave, r.doc, &err)) { std::fprintf(stderr, "FAIL save %s\n", qPrintable(err)); return 1; }
  }
  QJsonObject d;
  d["root"] = attrsJson(r.doc.rootAttrs);
  d["title"] = r.doc.title;
  d["preview"] = sha(r.doc.preview);
  QJsonArray pages;
  for (const Page& p : r.doc.pages) {
    QJsonObject po;
    po["attrs"] = attrsJson(p.attrs);
    po["bg"] = attrsJson(p.background);
    QJsonArray layers;
    for (const Layer& l : p.layers) {
      QJsonObject lo;
      lo["attrs"] = attrsJson(l.attrs);
      QJsonArray els;
      for (const Element& e : l.elements) els.append(elementJson(e));
      lo["els"] = els;
      layers.append(lo);
    }
    po["layers"] = layers;
    pages.append(po);
  }
  d["pages"] = pages;
  d["skipped"] = QJsonArray::fromStringList(r.skipped);
  const QByteArray out = QJsonDocument(d).toJson(QJsonDocument::Compact);
  std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
  std::fputc('\n', stdout);
  return 0;
}
