#pragma once

#include <QPoint>
#include <functional>
class QWidget;
#include <QString>
#include <QStringList>
#include <QMap>
#include <QByteArray>

namespace FileClipboard {

struct ClipboardFiles
{
    QStringList paths;
    bool move = false;

    bool isEmpty() const { return paths.isEmpty(); }
};

// A receipt identifies this deletion, not just a reusable original pathname.
struct TrashEntry
{
    QString originalPath;
    QString infoPath;
    QString trashedPath;
    QByteArray infoIdentity;
    QByteArray fileIdentity;
    QByteArray infoDigest;

    bool operator==(const TrashEntry &other) const
    {
        return originalPath == other.originalPath && infoPath == other.infoPath
            && trashedPath == other.trashedPath && infoIdentity == other.infoIdentity
            && fileIdentity == other.fileIdentity && infoDigest == other.infoDigest;
    }
};

struct PasteResult
{
    QStringList sourcePaths;
    QStringList placedPaths;
    QStringList placedSourcePaths;
    QStringList transferredPaths;
    QStringList failedPaths;
    QMap<QString, TrashEntry> trashEntries;
    QStringList undoUnavailablePaths;
    bool move = false;
    bool cancelled = false;

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
QString restoreTrashedEntry(const TrashEntry &entry);

// All GUI entry points share one bounded job slot. Cancellation takes effect
// between files so an in-flight move is never interrupted halfway through.
bool transferFilesAsync(const QStringList &paths, const QString &targetDir,
    bool move, bool keepExisting, QWidget *owner,
    std::function<void(const PasteResult &)> completed);
bool trashFilesAsync(const QStringList &paths, QWidget *owner,
    std::function<void(const PasteResult &)> completed);
bool busy();
bool runOperationAsync(QWidget *owner, std::function<PasteResult()> work,
    std::function<void(const PasteResult &)> completed);
PasteResult pasteFilesToDirectory(const QString &targetDir);
// Returns whether a job was accepted; completion and clipboard updates run on GUI thread.
bool pasteFilesToDirectoryAsync(const QString &targetDir, QWidget *owner,
    std::function<void(const PasteResult &)> completed);

} // namespace FileClipboard
