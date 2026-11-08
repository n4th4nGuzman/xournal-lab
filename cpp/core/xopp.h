// Reading and writing .xopp (gzip-compressed XML), the Xournal++ document format.
#pragma once
#include "model.h"
#include <QByteArray>
#include <QStringList>

struct LoadResult {
  bool ok = false;
  QString error;             // why it failed, with the position when the XML is malformed
  Document doc;
  QStringList skipped;       // elements that could not be kept (none now: unknown elements are preserved, see Document::extra)
  QStringList preserved;     // unknown elements kept verbatim ("layer/timestamp"), written back on save
};

LoadResult parseXml(const QByteArray& xml);
LoadResult loadXopp(const QString& path);

QByteArray serializeXml(const Document& doc);
/** Writes through a temporary file and renames it over the target, so a failed save never leaves a half file. */
bool saveXopp(const QString& path, const Document& doc, QString* error = nullptr);

bool gunzip(const QByteArray& in, QByteArray* out, QString* error);
bool gzip(const QByteArray& in, QByteArray* out);
