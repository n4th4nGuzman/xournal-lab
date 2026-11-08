#include "welcomedata.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

std::vector<StartPaper> startPapers(const std::vector<std::pair<QString, QString>>& special) {
  std::vector<StartPaper> out = {{"plain", "Liso"}, {"graph", "Cuadriculado"}, {"lined", "Rayado"}, {"dotted", "Puntos"}};
  for (const auto& s : special) out.push_back({"x:" + s.first, s.second});
  return out;
}

QStringList loadRecent(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
  QStringList out;
  if (!d.isArray()) return out;
  for (const QJsonValue& v : d.array())
    if (v.isString()) out << v.toString();
  return out.mid(0, kRecentMax);
}

bool saveRecent(const QString& path, const QStringList& recent, QString* error) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
  f.write(QJsonDocument(QJsonArray::fromStringList(recent)).toJson(QJsonDocument::Compact));
  if (!f.commit()) { if (error) *error = f.errorString(); return false; }
  return true;
}

QStringList addRecent(const QStringList& recent, const QString& file) {
  QStringList out{file};
  for (const QString& p : recent)
    if (p != file) out << p;
  return out.mid(0, kRecentMax);
}

std::vector<RecentFile> existingRecent(const QStringList& recent) {
  std::vector<RecentFile> out;
  for (const QString& p : recent) {
    const QFileInfo fi(p);
    if (fi.exists()) out.push_back({p, fi.fileName(), fi.absolutePath()});
  }
  return out;
}
