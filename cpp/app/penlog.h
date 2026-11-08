#pragma once
// Opt-in diagnostic trace of the pen's raw events and of what the app decided with them: set XL_PENLOG=<file> before starting the app. Off (one cached bool) otherwise.
#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>

namespace penlog {
inline QFile* file() {
  static QFile* f = [] () -> QFile* {
    const QByteArray p = qgetenv("XL_PENLOG");
    if (p.isEmpty()) return nullptr;
    auto* out = new QFile(QString::fromLocal8Bit(p));
    return out->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) ? out : nullptr;
  }();
  return f;
}
inline bool on() { return file() != nullptr; }
inline void write(const QString& line) {
  if (QFile* f = file()) {
    f->write((QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + line + "\n").toUtf8());
    f->flush();
  }
}
}  // namespace penlog
