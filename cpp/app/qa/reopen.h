// `xl_app --reopen N file.xopp...`: opens the files in turn N times in the real window (render the visible tiles, close by replacing the document) and
// prints the working set and private bytes after each round to stderr, so a leak shows as a line that keeps growing. Used by PARIDAD/qa.md.
#pragma once
#include <QStringList>
class MainWindow;
int runReopenCheck(MainWindow& win, const QStringList& files, int rounds);
