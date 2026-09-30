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
        auto *message = new QMessageBox(QMessageBox::Information, "文件操作",
            "已有文件操作正在进行，请完成后重试。", QMessageBox::Ok, owner);
        message->setAttribute(Qt::WA_DeleteOnClose); message->show();
        return false;
    }
    jobActive = true;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto *progress = new QProgressDialog("正在处理文件；取消将在当前项目完成后生效。", "取消后续项目", 0, 0, owner);
    if (!cancellable) { progress->setLabelText("正在处理文件，请稍候…"); progress->setCancelButton(nullptr); }
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
        for (const auto &path : paths) {
            if (cancel->load() || QThread::currentThread()->isInterruptionRequested()) {
                result.cancelled = true; result.failedPaths << path; continue;
            }
            if (QProcess::execute("gio", {"trash", "--", path}) == 0)
                result.placedPaths << QFileInfo(path).absoluteFilePath();
            else result.failedPaths << path;
        }
        return result;
    }, [guard, completed](const PasteResult &result) {
        if (guard) completed(result);
    });
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
