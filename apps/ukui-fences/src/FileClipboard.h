#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

namespace FileClipboard {

struct ClipboardFiles
{
    QStringList paths;
    bool move = false;

    bool isEmpty() const { return paths.isEmpty(); }
};

struct PasteResult
{
    QStringList sourcePaths;
    QStringList placedPaths;
    QStringList placedSourcePaths;
    QStringList transferredPaths;
    QStringList failedPaths;
    bool move = false;

    bool hadFiles() const { return !sourcePaths.isEmpty(); }
    bool hasPlacedFiles() const { return !placedPaths.isEmpty(); }
};

ClipboardFiles readFiles();
void writeFiles(const QStringList &paths, bool move);
bool hasFiles();
QStringList currentCutPaths();

QString uniqueTargetPath(const QString &dirPath, const QString &fileName);
bool isInDirectory(const QString &path, const QString &dirPath);
bool transferPath(const QString &srcPath, const QString &targetPath, bool move);

PasteResult pasteFilesToDirectory(const QString &targetDir);

} // namespace FileClipboard
