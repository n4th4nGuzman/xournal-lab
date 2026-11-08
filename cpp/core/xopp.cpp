#include "xopp.h"
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <charconv>
#include <cmath>
#include <cstring>
#include <new>
#include <zlib.h>

static constexpr qsizetype kMaxXmlBytes = qsizetype(1536) << 20;
static constexpr int kMaxRawDepth = 200;   // nesting of an unknown element we are willing to keep (a deeper one would overflow the stack)

bool gunzip(const QByteArray& in, QByteArray* out, QString* error) {
  z_stream zs;
  std::memset(&zs, 0, sizeof zs);
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) { if (error) *error = "no se pudo iniciar la descompresión"; return false; }
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
  zs.avail_in = uInt(in.size());
  out->clear();
  out->reserve(in.size() * 6);
  char buf[1 << 16];
  int rc = Z_OK;
  while (rc != Z_STREAM_END) {
    zs.next_out = reinterpret_cast<Bytef*>(buf);
    zs.avail_out = sizeof buf;
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
      if (error) *error = rc == Z_BUF_ERROR ? QString("el archivo comprimido termina antes de tiempo") : QString("archivo comprimido dañado (zlib %1)").arg(rc);
      inflateEnd(&zs);
      return false;
    }
    out->append(buf, int(sizeof buf - zs.avail_out));
    if (out->size() > kMaxXmlBytes) {   // a few KB of gzip can expand to gigabytes: refuse before the memory is gone
      if (error) *error = "el documento descomprimido es demasiado grande (más de 1,5 GB)";
      inflateEnd(&zs);
      return false;
    }
    if (rc == Z_OK && zs.avail_in == 0 && zs.avail_out != 0) { if (error) *error = "el archivo comprimido termina antes de tiempo"; inflateEnd(&zs); return false; }
  }
  inflateEnd(&zs);
  return true;
}

bool gzip(const QByteArray& in, QByteArray* out) {
  z_stream zs;
  std::memset(&zs, 0, sizeof zs);
  if (deflateInit2(&zs, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
  zs.avail_in = uInt(in.size());
  out->clear();
  char buf[1 << 16];
  int rc = Z_OK;
  while (rc != Z_STREAM_END) {
    zs.next_out = reinterpret_cast<Bytef*>(buf);
    zs.avail_out = sizeof buf;
    rc = deflate(&zs, Z_FINISH);
    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) { deflateEnd(&zs); return false; }
    out->append(buf, int(sizeof buf - zs.avail_out));
  }
  deflateEnd(&zs);
  return true;
}

// ---- reading ------------------------------------------------------------------------------------------------------

/** Numbers separated by white space. Returns false on anything that is not a number, so a damaged stroke is reported and not read as zeros. */
static bool parseNumbers(const QString& s, std::vector<double>* out) {
  const QByteArray b = s.toLatin1();
  const char* p = b.constData();
  const char* e = p + b.size();
  while (p < e) {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) p++;
    if (p >= e) break;
    if (*p == '+' && p + 1 < e && p[1] != '+' && p[1] != '-') p++;   // from_chars refuses a leading plus that other writers produce
    double v = 0;
    const auto r = std::from_chars(p, e, v);
    if (r.ec != std::errc() || !std::isfinite(v)) return false;   // "nan" and "inf" parse as numbers but would poison every bounding box
    out->push_back(v);
    p = r.ptr;
  }
  return true;
}

static Attrs attrsOf(const QXmlStreamAttributes& a) {
  Attrs r;
  r.reserve(size_t(a.size()));
  for (const auto& x : a) r.emplace_back(x.qualifiedName().toString(), x.value().toString());   // qualified: xml:space, xmlns:... survive
  return r;
}

static void dropAttr(Attrs& a, const QString& name) {
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].first == name) { a.erase(a.begin() + long(i)); return; }
}

/** Qt's reader reports in English; the user reads Spanish. Known causes get a plain sentence, anything else a generic one (the line number is added by the caller). */
static QString spanishXmlError(const QXmlStreamReader& r) {
  switch (r.error()) {
    case QXmlStreamReader::PrematureEndOfDocumentError: return "el archivo termina antes de tiempo (le falta el final)";
    case QXmlStreamReader::UnexpectedElementError: return "hay un elemento donde solo debía haber texto (dentro de un trazo, un texto o una imagen)";
    default: break;
  }
  const QString e = r.errorString();
  if (e.contains("tag mismatch")) return "una etiqueta se abre y se cierra con nombres distintos";
  if (e.contains("Extra content")) return "hay contenido de más al final del documento";
  if (e.contains("Start tag expected")) return "el archivo no empieza con una etiqueta XML";
  if (e.contains("not declared")) return "usa una entidad que no está declarada";
  if (e.contains("encoded")) return "el texto tiene caracteres con codificación incorrecta";
  return "el contenido no es XML válido";
}

/** The element the reader is on (a start element), whole, as a tree; leaves the reader on its end element. False when it nests too deeply. */
static bool readRaw(QXmlStreamReader& r, RawNode* out, int depth = 0) {
  if (depth > kMaxRawDepth) return false;
  out->name = r.qualifiedName().toString();
  out->attrs = attrsOf(r.attributes());
  while (!r.atEnd()) {
    const auto t = r.readNext();
    if (t == QXmlStreamReader::EndElement) return true;
    if (t == QXmlStreamReader::StartElement) {
      RawNode kid;
      if (!readRaw(r, &kid, depth + 1)) return false;
      out->kids.push_back(std::move(kid));
    } else if (t == QXmlStreamReader::Characters && !r.isWhitespace()) {
      RawNode txt;
      txt.isText = true;
      txt.text = r.text().toString();
      out->kids.push_back(std::move(txt));
    }
  }
  return true;   // premature end: the caller reports r.error()
}

LoadResult parseXml(const QByteArray& xml) {
  LoadResult res;
  QXmlStreamReader r(xml);
  r.setNamespaceProcessing(false);   // Xournal++ files have none; with it off an unknown prefixed element comes back exactly as written
  auto fail = [&](const QString& why) { res.ok = false; res.error = QString("%1 (línea %2)").arg(why).arg(r.lineNumber()); return res; };
  bool tooDeep = false;
  // An element we do not interpret is kept (not lost on save): anchored by how many known siblings came before it.
  auto keep = [&](const QString& where, std::vector<RawAt>& into, size_t before) {
    RawAt at;
    at.before = before;
    if (!readRaw(r, &at.node)) { tooDeep = true; r.skipCurrentElement(); return; }
    res.preserved << where + "/" + at.node.name;
    into.push_back(std::move(at));
  };

  if (!r.readNextStartElement() || r.name() != u"xournal") return fail("no es un documento de Xournal++ (falta <xournal>)");
  res.doc.rootAttrs = attrsOf(r.attributes());
  while (r.readNextStartElement()) {
    if (r.name() == u"title") res.doc.title = r.readElementText();
    else if (r.name() == u"preview") res.doc.preview = r.readElementText();
    else if (r.name() == u"page") {
      Page page;
      page.attrs = attrsOf(r.attributes());
      while (r.readNextStartElement()) {
        if (r.name() == u"background") { page.background = attrsOf(r.attributes()); r.skipCurrentElement(); }
        else if (r.name() == u"layer") {
          Layer layer;
          layer.attrs = attrsOf(r.attributes());
          while (r.readNextStartElement()) {
            if (r.name() == u"stroke") {
              Stroke s;
              s.attrs = attrsOf(r.attributes());
              if (const QString* w = attrFind(s.attrs, "width")) {
                if (!parseNumbers(*w, &s.widths)) return fail("el grosor de un trazo no es numérico");
                dropAttr(s.attrs, "width");
              }
              const QString body = r.readElementText();
              if (!parseNumbers(body, &s.pts)) return fail("las coordenadas de un trazo no son numéricas");
              if (s.pts.size() % 2 != 0) return fail("un trazo tiene un número impar de coordenadas");
              layer.elements.emplace_back(std::move(s));
            } else if (r.name() == u"text") {
              Text t;
              t.attrs = attrsOf(r.attributes());
              t.text = r.readElementText();
              layer.elements.emplace_back(std::move(t));
            } else if (r.name() == u"image" || r.name() == u"teximage") {
              Image im;
              im.tag = r.name().toString();
              im.attrs = attrsOf(r.attributes());
              im.data = r.readElementText();
              layer.elements.emplace_back(std::move(im));
            } else keep("layer", layer.extra, layer.elements.size());
          }
          page.layers.push_back(std::move(layer));
        } else keep("page", page.extra, page.layers.size());
      }
      res.doc.pages.push_back(std::move(page));
    } else keep("xournal", res.doc.extra, res.doc.pages.size());
  }
  if (r.hasError()) return fail("el archivo está dañado: " + spanishXmlError(r));
  if (tooDeep) return fail("un elemento desconocido está anidado a demasiada profundidad");
  res.ok = true;
  return res;
}

static LoadResult loadXoppUnguarded(const QString& path) {
  LoadResult res;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) { res.error = "no se pudo abrir: " + f.errorString(); return res; }
  const QByteArray raw = f.readAll();
  if (raw.isEmpty()) { res.error = "el archivo está vacío"; return res; }
  QByteArray xml;
  if (raw.size() >= 2 && uchar(raw[0]) == 0x1f && uchar(raw[1]) == 0x8b) {
    if (!gunzip(raw, &xml, &res.error)) return res;
  } else xml = raw;  // an uncompressed .xml is accepted too
  return parseXml(xml);
}

LoadResult loadXopp(const QString& path) {
  try {
    return loadXoppUnguarded(path);
  } catch (const std::bad_alloc&) {   // a hostile or enormous file must not take the application down
    LoadResult res;
    res.error = "no hay memoria suficiente para abrir este archivo";
    return res;
  }
}

// ---- writing ------------------------------------------------------------------------------------------------------

static void appendNumbers(const std::vector<double>& v, std::string* out) {
  char buf[40];
  for (size_t i = 0; i < v.size(); i++) {
    if (i) out->push_back(' ');
    const auto r = std::to_chars(buf, buf + sizeof buf, v[i]);  // the shortest text that reads back as the same number
    out->append(buf, size_t(r.ptr - buf));
  }
}

static void writeAttrs(QXmlStreamWriter& w, const Attrs& a) {
  for (const auto& kv : a) w.writeAttribute(kv.first, kv.second);
}

static void writeRaw(QXmlStreamWriter& w, const RawNode& n) {
  if (n.isText) { w.writeCharacters(n.text); return; }
  w.writeStartElement(n.name);
  writeAttrs(w, n.attrs);
  for (const RawNode& k : n.kids) writeRaw(w, k);
  w.writeEndElement();
}
/** The unknown children anchored at `before` == index, starting at *next; with index = SIZE_MAX, whatever is left. */
static void writeExtras(QXmlStreamWriter& w, const std::vector<RawAt>& extra, size_t* next, size_t index) {
  while (*next < extra.size() && extra[*next].before <= index) writeRaw(w, extra[(*next)++].node);
}

QByteArray serializeXml(const Document& d) {
  QByteArray out;
  out.reserve(1 << 20);
  QXmlStreamWriter w(&out);
  w.setAutoFormatting(false);
  w.writeStartDocument("1.0", false);
  w.writeStartElement("xournal");
  writeAttrs(w, d.rootAttrs);
  w.writeTextElement("title", d.title);
  if (!d.preview.isEmpty()) w.writeTextElement("preview", d.preview);
  std::string num;
  size_t docNext = 0, pageIndex = 0;
  for (const Page& p : d.pages) {
    writeExtras(w, d.extra, &docNext, pageIndex++);
    w.writeStartElement("page");
    writeAttrs(w, p.attrs);
    w.writeStartElement("background");
    writeAttrs(w, p.background);
    w.writeEndElement();
    size_t pageNext = 0, layerIndex = 0;
    for (const Layer& l : p.layers) {
      writeExtras(w, p.extra, &pageNext, layerIndex++);
      w.writeStartElement("layer");
      writeAttrs(w, l.attrs);
      size_t layerNext = 0, elementIndex = 0;
      for (const Element& e : l.elements) {
        writeExtras(w, l.extra, &layerNext, elementIndex++);
        if (const auto* s = std::get_if<Stroke>(&e)) {
          w.writeStartElement("stroke");
          // tool and color first, then the width list, then the rest: the order Xournal++ itself writes
          bool wrote = false;
          auto emitWidth = [&] {
            if (wrote || s->widths.empty()) return;
            num.clear();
            appendNumbers(s->widths, &num);
            w.writeAttribute("width", QString::fromLatin1(num.data(), qsizetype(num.size())));
            wrote = true;
          };
          for (const auto& kv : s->attrs) {
            if (kv.first != "tool" && kv.first != "color") emitWidth();
            w.writeAttribute(kv.first, kv.second);
          }
          emitWidth();
          num.clear();
          appendNumbers(s->pts, &num);
          w.writeCharacters(QString::fromLatin1(num.data(), qsizetype(num.size())));
          w.writeEndElement();
        } else if (const auto* t = std::get_if<Text>(&e)) {
          w.writeStartElement("text");
          writeAttrs(w, t->attrs);
          w.writeCharacters(t->text);
          w.writeEndElement();
        } else if (const auto* im = std::get_if<Image>(&e)) {
          w.writeStartElement(im->tag);
          writeAttrs(w, im->attrs);
          w.writeCharacters(im->data);
          w.writeEndElement();
        }
      }
      writeExtras(w, l.extra, &layerNext, SIZE_MAX);
      w.writeEndElement();
    }
    writeExtras(w, p.extra, &pageNext, SIZE_MAX);
    w.writeEndElement();
  }
  writeExtras(w, d.extra, &docNext, SIZE_MAX);
  w.writeEndElement();
  w.writeEndDocument();
  return out;
}

bool saveXopp(const QString& path, const Document& doc, QString* error) {
  QByteArray gz;
  if (!gzip(serializeXml(doc), &gz)) { if (error) *error = "no se pudo comprimir"; return false; }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) { if (error) *error = "no se pudo escribir: " + f.errorString(); return false; }
  if (f.write(gz) != gz.size() || !f.commit()) { if (error) *error = "no se pudo guardar: " + f.errorString(); return false; }
  return true;
}
