#include "LiquidDialog.h"
#include "FileClipboard.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QProcess>
#include <QUrl>
#include <QThread>
#include <QPointer>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QCryptographicHash>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <QSet>
#include <memory>
#include <atomic>
#include <QMessageBox>
#include "../../../shared/async-work/BackgroundTask.h"

namespace {

QString normalizedLocalPath(const QString &path)
{
    if (path.isEmpty()) return {};
    return QFileInfo(path).absoluteFilePath();
}

QString normalizedLocalPath(const QUrl &url)
{
    return normalizedLocalPath(url.toLocalFile());
}

QStringList clipboardFormats()
{
    return {
        QStringLiteral("x-special/gnome-copied-files"),
        QStringLiteral("x-special/mate-copied-files"),
        QStringLiteral("x-special/caja-copied-files"),
        QStringLiteral("x-special/peony-copied-files")
    };
}

bool pathExists(const QString &path)
{
    const QFileInfo info(path); return info.exists() || info.isSymLink();
}

QByteArray fileIdentity(const QString &path, bool includeChangeTime = true)
{
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) return {};
    QByteArray identity = QByteArray::number(quint64(st.st_dev)) + ':'
        + QByteArray::number(quint64(st.st_ino));
    if (includeChangeTime)
        identity += ':' + QByteArray::number(qint64(st.st_ctim.tv_sec)) + ':'
            + QByteArray::number(qint64(st.st_ctim.tv_nsec));
    return identity;
}

struct TrashLocation { QString root; QString relativeBase; };

QList<TrashLocation> trashLocations(const QString &original)
{
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QList<TrashLocation> locations{{data + "/Trash", data}};
    // Inspect the containing filesystem without following a trashed symlink.
    const QString top = QStorageInfo(QFileInfo(original).absolutePath()).rootPath();
    if (!top.isEmpty()) {
        const QString uid = QString::number(::getuid());
        const QString shared = QDir(top).filePath(".Trash");
        struct stat st;
        if (::lstat(QFile::encodeName(shared).constData(), &st) == 0
            && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX))
            locations.append({shared + '/' + uid, top});
        locations.append({QDir(top).filePath(".Trash-" + uid), top});
    }
    return locations;
}

QString trashOriginal(const QByteArray &metadata, const QString &relativeBase)
{
    const auto lines = metadata.split('\n');
    if (lines.isEmpty() || lines.first().trimmed() != "[Trash Info]") return {};
    for (auto line : lines) {
        if (!line.startsWith("Path=")) continue;
        if (line.endsWith('\r')) line.chop(1);
        QString path = QUrl::fromPercentEncoding(line.mid(5));
        if (path.isEmpty()) return {};
        if (QDir::isRelativePath(path)) {
            if (path.split('/').contains("..")) return {};
            path = QDir(relativeBase).absoluteFilePath(path);
        }
        return QDir::cleanPath(path);
    }
    return {};
}

QString resolvedParentPath(const QString &path)
{
    if (path.isEmpty()) return {};
    const QFileInfo info(path);
    const QString parent = info.dir().canonicalPath();
    return parent.isEmpty() ? QDir::cleanPath(info.absoluteFilePath())
                           : QDir(parent).filePath(info.fileName());
}

QStringList trashInfoPaths(const TrashLocation &location)
{
    struct stat st;
    if (::lstat(QFile::encodeName(location.root).constData(), &st) != 0
        || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) return {};
    const QDir info(location.root + "/info");
    QStringList paths;
    for (const QString &name : info.entryList({"*.trashinfo"}, QDir::Files | QDir::Hidden | QDir::NoSymLinks))
        paths << info.absoluteFilePath(name);
    return paths;
}

struct TrashCandidate { TrashLocation location; QString infoPath; QByteArray identity; };

// One baseline per trash directory per batch. Subsequent refreshes stat only
// event names; lost/unavailable watches fall back to a complete reconciliation.
class TrashCatalog
{
public:
    explicit TrashCatalog(const TrashLocation &location) : m_location(location),
        m_fd(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {}
    ~TrashCatalog() { if (m_fd >= 0) ::close(m_fd); }
    QList<TrashCandidate> refresh()
    {
        struct stat root;
        if (::lstat(QFile::encodeName(m_location.root).constData(), &root) != 0
            || !S_ISDIR(root.st_mode) || root.st_uid != ::getuid()) {
            invalidate(); m_known.clear(); return {};
        }
        bool fullScan = m_watch < 0;
        QSet<QString> dirty;
        if (m_fd >= 0) {
            alignas(inotify_event) char buffer[16384];
            for (;;) {
                const ssize_t count = ::read(m_fd, buffer, sizeof(buffer));
                if (count < 0 && errno == EINTR) continue;
                if (count < 0 && errno == EAGAIN) break;
                if (count <= 0) { invalidate(); ::close(m_fd); m_fd = -1; fullScan = true; break; }
                for (ssize_t offset = 0; offset + ssize_t(sizeof(inotify_event)) <= count;) {
                    const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
                    if (offset + ssize_t(sizeof(inotify_event) + event->len) > count) { fullScan = true; break; }
                    if (event->mask & IN_Q_OVERFLOW) fullScan = true;
                    else if (event->wd == m_watch) {
                        if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT)) {
                            invalidate(); fullScan = true;
                        } else if (event->len) {
                            const QString name = QFile::decodeName(event->name);
                            if (name.endsWith(".trashinfo")) dirty.insert(m_location.root + "/info/" + name);
                        }
                    }
                    offset += sizeof(inotify_event) + event->len;
                }
            }
        }
        const QString infoDir = m_location.root + "/info";
        if (m_watch >= 0 && fileIdentity(infoDir, false) != m_watchIdentity) { invalidate(); fullScan = true; }
        if (m_watch < 0 && m_fd >= 0) {
            m_watch = ::inotify_add_watch(m_fd, QFile::encodeName(infoDir).constData(),
                IN_CREATE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE | IN_CLOSE_WRITE | IN_ATTRIB
                | IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW);
            m_watchIdentity = fileIdentity(infoDir, false);
            fullScan = true;
        }
        if (fullScan) {
            const auto paths = trashInfoPaths(m_location);
            dirty.clear();
            for (const QString &path : paths) dirty.insert(path);
            // Include removed records so the cache cannot retain stale names.
            for (auto it = m_known.cbegin(); it != m_known.cend(); ++it) dirty.insert(it.key());
        }
        QList<TrashCandidate> changed;
        for (const QString &path : dirty) {
            const QByteArray identity = QFileInfo(path).isSymLink() ? QByteArray() : fileIdentity(path);
            if (identity == m_known.value(path)) continue;
            if (identity.isEmpty()) m_known.remove(path);
            else { m_known.insert(path, identity); changed.append({m_location, path, identity}); }
        }
        return changed;
    }
private:
    Q_DISABLE_COPY(TrashCatalog)
    void invalidate() { if (m_fd >= 0 && m_watch >= 0) ::inotify_rm_watch(m_fd, m_watch); m_watch = -1; }
    TrashLocation m_location;
    int m_fd = -1, m_watch = -1;
    QByteArray m_watchIdentity;
    QMap<QString, QByteArray> m_known;
};

FileClipboard::TrashEntry findTrashEntry(const QString &original,
    const QByteArray &sourceIdentity, const QList<TrashCandidate> &candidates)
{
    FileClipboard::TrashEntry receipt;
    if (sourceIdentity.isEmpty()) return receipt;
    for (const auto &candidate : candidates) {
        const auto &location = candidate.location;
        const QString &infoPath = candidate.infoPath;
        const QByteArray infoId = fileIdentity(infoPath);
        if (infoId.isEmpty() || candidate.identity != infoId) continue;
        QFile info(infoPath);
        if (!info.open(QIODevice::ReadOnly)) continue;
        const QByteArray metadata = info.readAll();
        if (resolvedParentPath(trashOriginal(metadata, location.relativeBase))
            != resolvedParentPath(original)) continue;
        QString name = QFileInfo(infoPath).fileName();
        name.chop(QString(".trashinfo").size());
        const QString payload = location.root + "/files/" + name;
        if (fileIdentity(payload, false) != sourceIdentity) continue;
        // Never guess if a backend leaves more than one possible receipt.
        if (!receipt.infoPath.isEmpty()) return {};
        receipt = {original, infoPath, payload, infoId, fileIdentity(payload),
            QCryptographicHash::hash(metadata, QCryptographicHash::Sha256)};
    }
    return receipt;
}

bool safeTarget(const QString &source, const QString &target)
{
    const QFileInfo src(source), dst(target);
    if ((!src.exists() && !src.isSymLink()) || pathExists(target)) return false;
    if (!src.isDir() || src.isSymLink()) return true;
    const QString parent=QFileInfo(dst.absolutePath()).canonicalFilePath();
    const QString origin=src.canonicalFilePath();
    return !origin.isEmpty() && !parent.isEmpty() && parent!=origin && !parent.startsWith(origin+"/");
}

bool copyPath(const QString &srcPath, const QString &targetPath)
{
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()) || !safeTarget(srcPath,targetPath)) return false;
    // Stage in the destination filesystem so a failed copy never publishes a
    // partial destination. Preserve metadata and symlinks with cp -a.
    QTemporaryDir staging(QFileInfo(targetPath).absolutePath()+"/.ukui-fences-transfer-XXXXXX");
    if (!staging.isValid()) return false;
    const QString ready=staging.path()+"/item";
    if (QProcess::execute("cp", {"-aT", "--", srcPath, ready})!=0) return false;
    if (pathExists(targetPath)) return false;
    return QDir().rename(ready,targetPath);
}

bool movePath(const QString &srcPath, const QString &targetPath)
{
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()) || !safeTarget(srcPath,targetPath)) return false;
    if (QProcess::execute("gio", {"move", "-T", "--", srcPath, targetPath})==0) return true;
    // A failed backend may have created a destination; never overwrite it.
    if (pathExists(targetPath)) return false;
    if (QDir().rename(srcPath,targetPath)) return true;
    if (!copyPath(srcPath,targetPath)) return false;
    const QFileInfo src(srcPath);
    return src.isDir() && !src.isSymLink() ? QDir(srcPath).removeRecursively() : QFile::remove(srcPath);
}

void clearClipboard()
{
    QApplication::clipboard()->clear(QClipboard::Clipboard);
}

} // namespace

namespace FileClipboard {

ClipboardFiles readFiles()
{
    ClipboardFiles files;

    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime) return files;

    for (const QString &format : clipboardFormats()) {
        if (!mime->hasFormat(format)) continue;

        const QList<QByteArray> lines = mime->data(format).split('\n');
        if (lines.isEmpty()) continue;

        files.move =
            QString::fromUtf8(lines.first()).trimmed().compare(
                QStringLiteral("cut"), Qt::CaseInsensitive) == 0;

        for (int i = 1; i < lines.size(); ++i) {
            const QString line = QString::fromUtf8(lines[i]).trimmed();
            if (line.isEmpty()) continue;

            const QString path = line.startsWith(QStringLiteral("file://"))
                ? normalizedLocalPath(QUrl(line))
                : normalizedLocalPath(line);
            if (!path.isEmpty() && !files.paths.contains(path))
                files.paths.append(path);
        }

        if (!files.paths.isEmpty())
            return files;
    }

    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            const QString path = normalizedLocalPath(url);
            if (!path.isEmpty() && !files.paths.contains(path))
                files.paths.append(path);
        }
    }

    return files;
}

void writeFiles(const QStringList &paths, bool move)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;

    for (const QString &path : paths) {
        const QString normalized = normalizedLocalPath(path);
        if (normalized.isEmpty()) continue;
        urls << QUrl::fromLocalFile(normalized);
    }

    if (urls.isEmpty()) {
        delete mime;
        return;
    }

    mime->setUrls(urls);

    QStringList encodedUris;
    for (const QUrl &url : urls)
        encodedUris << QString::fromUtf8(url.toEncoded());
    const QString encodedText = encodedUris.join(QLatin1Char(' '));
    mime->setData(QStringLiteral("peony-qt/is-cut"),
                  QByteArray(move ? "true" : "false"));
    mime->setData(QStringLiteral("peony-qt/encoded-uris"),
                  encodedText.toUtf8());
    mime->setData(QStringLiteral("peony-qt/is-search"),
                  QByteArray("false"));
    mime->setText(encodedText);

    QByteArray data = move ? QByteArray("cut\n") : QByteArray("copy\n");
    for (const QUrl &url : urls)
        data += url.toEncoded() + '\n';

    for (const QString &format : clipboardFormats())
        mime->setData(format, data);

    QApplication::clipboard()->setMimeData(mime);
}

bool hasFiles()
{
    return !readFiles().isEmpty();
}

QStringList currentCutPaths()
{
    const ClipboardFiles files = readFiles();
    return files.move ? files.paths : QStringList();
}

QString uniqueTargetPath(const QString &dirPath, const QString &fileName)
{
    QFileInfo fi(fileName);
    const QString base = fi.completeBaseName().isEmpty()
        ? fi.fileName()
        : fi.completeBaseName();
    const QString suffix = fi.completeBaseName().isEmpty() || fi.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + fi.suffix();

    QString target = QDir(dirPath).absoluteFilePath(fileName);
    for (quint64 i = 1; pathExists(target); ++i) {
        target = QDir(dirPath).absoluteFilePath(
            QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix));
    }
    return target;
}

bool isInDirectory(const QString &path, const QString &dirPath)
{
    const QFileInfo fi(path);
    return QDir(fi.absolutePath()).absolutePath()
        == QDir(dirPath).absolutePath();
}

bool transferPath(const QString &srcPath, const QString &targetPath, bool move)
{
    return move ? movePath(srcPath, targetPath)
                : copyPath(srcPath, targetPath);
}

static PasteResult pasteFiles(const QString &targetDir, const ClipboardFiles &files,
    bool keepExisting = false, const std::shared_ptr<std::atomic_bool> &cancel = {})
{
    PasteResult result;
    result.sourcePaths = files.paths;
    result.move = files.move;

    if (files.paths.isEmpty())
        return result;

    const QString normalizedTargetDir = QDir(targetDir).absolutePath();
    QDir().mkpath(normalizedTargetDir);

    for (const QString &path : files.paths) {
        if ((cancel && cancel->load()) || QThread::currentThread()->isInterruptionRequested()) {
            result.cancelled = true;
            result.failedPaths << path;
            continue;
        }
        const QFileInfo src(path);
        if (!src.exists() && !src.isSymLink()) {
            result.failedPaths << path;
            continue;
        }

        const QString srcPath = src.absoluteFilePath();
        if ((files.move || keepExisting) && isInDirectory(srcPath, normalizedTargetDir)) {
            result.placedPaths << srcPath;
            continue;
        }

        const QString target =
            uniqueTargetPath(normalizedTargetDir, src.fileName());
        const bool ok = transferPath(srcPath, target, files.move);

        if (ok) {
            result.placedPaths << QFileInfo(target).absoluteFilePath();
            result.placedSourcePaths << srcPath;
            result.transferredPaths << QFileInfo(target).absoluteFilePath();
        } else {
            result.failedPaths << srcPath;
        }
    }

    return result;
}

static void finishClipboard(const PasteResult &result)
{
    if (result.move) {
        if (result.failedPaths.isEmpty()) clearClipboard();
        else writeFiles(result.failedPaths,true);
    }
}

PasteResult pasteFilesToDirectory(const QString &targetDir)
{
    const auto result=pasteFiles(targetDir,readFiles()); finishClipboard(result); return result;
}

namespace {
bool jobActive = false;
using Completion = std::function<void(const PasteResult &)>;
using Work = std::function<PasteResult(const std::shared_ptr<std::atomic_bool> &)>;
bool submit(QWidget *owner, Work work, Completion completed, bool cancellable = true)
{
    if (jobActive) {
        auto *message = LiquidDialog::createMessage(owner, "文件操作",
            "已有文件操作正在进行，请完成后重试。", QMessageBox::Information, QMessageBox::Ok);
        message->setAttribute(Qt::WA_DeleteOnClose); message->show();
        return false;
    }
    jobActive = true;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto *progress = new QProgressDialog("正在处理文件；取消将在当前项目完成后生效。", "取消后续项目", 0, 0, owner);
    if (!cancellable) { progress->setLabelText("正在处理文件，请稍候…"); progress->setCancelButton(nullptr); }
    LiquidDialog::install(progress);
    progress->setWindowTitle("文件操作");
    progress->setMinimumDuration(300);
    progress->setValue(0);
    const QPointer<QProgressDialog> progressGuard(progress);
    QObject::connect(progress, &QProgressDialog::canceled, [cancel] { cancel->store(true); });
    // Do not wait indefinitely for filesystem I/O when the desktop exits.
    // Copy is staged; the original is retained until the destination commits.
    const auto shutdown = QObject::connect(qApp, &QCoreApplication::aboutToQuit,
        qApp, [cancel] { cancel->store(true); });
    BackgroundTask::run(qApp, [work, cancel] { return work(cancel); },
        [=](const PasteResult &result) {
            QObject::disconnect(shutdown);
            jobActive = false;
            if (progressGuard) { progressGuard->close(); progressGuard->deleteLater(); }
            // Clipboard maintenance must survive deletion of the initiating icon.
            completed(result);
        });
    return true;
}
}

bool busy() { return jobActive; }
bool runOperationAsync(QWidget *owner, std::function<PasteResult()> work, Completion completed)
{
    const QPointer<QWidget> guard(owner);
    return submit(owner, [work](const auto &) { return work(); }, [guard, completed](const PasteResult &result) {
        if (guard) completed(result);
    }, false);
}

bool transferFilesAsync(const QStringList &paths, const QString &targetDir,
    bool move, bool keepExisting, QWidget *owner, Completion completed)
{
    if (paths.isEmpty()) return false;
    const QPointer<QWidget> guard(owner);
    const ClipboardFiles files{paths, move};
    return submit(owner, [=](const auto &cancel) {
        return pasteFiles(targetDir, files, keepExisting, cancel);
    }, [guard, completed](const PasteResult &result) {
        if (guard) completed(result);
    });
}

bool trashFilesAsync(const QStringList &paths, QWidget *owner, Completion completed)
{
    if (paths.isEmpty()) return false;
    const QPointer<QWidget> guard(owner);
    return submit(owner, [paths](const auto &cancel) {
        PasteResult result; result.sourcePaths = paths; result.move = true;
        QMap<QString, std::shared_ptr<TrashCatalog>> catalogs;
        QMap<QString, QList<TrashLocation>> parentLocations;
        for (const auto &path : paths) {
            if (cancel->load() || QThread::currentThread()->isInterruptionRequested()) {
                result.cancelled = true; result.failedPaths << path; continue;
            }
            const QString original = normalizedLocalPath(path);
            const QString parent = QFileInfo(original).absolutePath();
            if (!parentLocations.contains(parent)) parentLocations.insert(parent, trashLocations(original));
            const auto locations = parentLocations.value(parent);
            for (const auto &location : locations) {
                auto &catalog = catalogs[location.root];
                if (!catalog) catalog = std::make_shared<TrashCatalog>(location);
                catalog->refresh(); // Commit external changes before this deletion.
            }
            const QByteArray sourceIdentity = fileIdentity(original, false);
            if (QProcess::execute("gio", {"trash", "--", original}) == 0) {
                result.placedPaths << original;
                QList<TrashCandidate> candidates;
                for (const auto &location : locations) candidates.append(catalogs.value(location.root)->refresh());
                const auto entry = findTrashEntry(original, sourceIdentity, candidates);
                if (!entry.infoPath.isEmpty()) result.trashEntries.insert(original, entry);
                else result.undoUnavailablePaths << original;
            } else result.failedPaths << path;
        }
        return result;
    }, [guard, completed](const PasteResult &result) {
        if (guard) completed(result);
    });
}

QString restoreTrashedEntry(const TrashEntry &entry)
{
    if (entry.originalPath.isEmpty() || entry.infoIdentity.isEmpty() || entry.fileIdentity.isEmpty()
        || fileIdentity(entry.infoPath) != entry.infoIdentity
        || fileIdentity(entry.trashedPath) != entry.fileIdentity) return {};
    QFile info(entry.infoPath);
    if (!info.open(QIODevice::ReadOnly)
        || QCryptographicHash::hash(info.readAll(), QCryptographicHash::Sha256) != entry.infoDigest) return {};
    info.close();
    const QFileInfo original(entry.originalPath);
    const QString target = pathExists(entry.originalPath)
        ? uniqueTargetPath(original.absolutePath(), original.fileName()) : entry.originalPath;
    if (!transferPath(entry.trashedPath, target, true)) return {};
    QFile::remove(entry.infoPath);
    return target;
}

bool pasteFilesToDirectoryAsync(const QString &targetDir, QWidget *owner,
    Completion completed)
{
    const ClipboardFiles files = readFiles();
    if (files.isEmpty()) return false;
    const QPointer<QMimeData> original = const_cast<QMimeData *>(QApplication::clipboard()->mimeData());
    const QPointer<QWidget> guard(owner);
    return submit(owner, [=](const auto &cancel) {
        return pasteFiles(targetDir, files, false, cancel);
    }, [=](const PasteResult &result) {
        if (original && QApplication::clipboard()->mimeData() == original) finishClipboard(result);
        if (guard) completed(result);
    });
}

} // namespace FileClipboard
