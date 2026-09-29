#pragma once

#include <QPoint>
#include <functional>
class QWidget;
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
// Returns whether a job was accepted; completion and clipboard updates run on GUI thread.
bool pasteFilesToDirectoryAsync(const QString &targetDir, QWidget *owner,
    std::function<void(const PasteResult &)> completed);

} // namespace FileClipboard
