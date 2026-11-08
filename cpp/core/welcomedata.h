// What the welcome page shows that is not drawing: the papers offered as starting points and the list of recent files. Port of the data of
// app/src/renderer/welcome.ts and the recent-files handling of main.ts (recent.json: a JSON array of paths, at most 10).
#pragma once
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

struct StartPaper { QString value, label; };   // value: a built-in style ("graph") or a special paper ("x:log")
/** The built-in papers, then `special` (id, label) of the special papers (millimetre, polar, logarithmic...) as "x:<id>". */
std::vector<StartPaper> startPapers(const std::vector<std::pair<QString, QString>>& special = {});

struct RecentFile { QString path, name, dir; };
constexpr int kRecentMax = 10;
/** The list as stored (strings only, at most kRecentMax). Missing or bad file: empty. */
QStringList loadRecent(const QString& path);
bool saveRecent(const QString& path, const QStringList& recent, QString* error = nullptr);
/** `file` first, without repeats, cut at kRecentMax. */
QStringList addRecent(const QStringList& recent, const QString& file);
/** The files that still exist, with their name and folder, for the welcome page. */
std::vector<RecentFile> existingRecent(const QStringList& recent);
