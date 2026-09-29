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

static PasteResult pasteFiles(const QString &targetDir, const ClipboardFiles &files)
{
    PasteResult result;
    result.sourcePaths = files.paths;
    result.move = files.move;

    if (files.paths.isEmpty())
        return result;

    const QString normalizedTargetDir = QDir(targetDir).absolutePath();
    QDir().mkpath(normalizedTargetDir);

    for (const QString &path : files.paths) {
        const QFileInfo src(path);
        if (!src.exists()) {
            result.failedPaths << path;
            continue;
        }

        const QString srcPath = src.absoluteFilePath();
        if (files.move && isInDirectory(srcPath, normalizedTargetDir)) {
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

bool pasteFilesToDirectoryAsync(const QString &targetDir, QWidget *owner,
    std::function<void(const PasteResult &)> completed)
{
    static QPointer<QThread> active;
    if(active) return false;
    const ClipboardFiles files=readFiles(); if(files.isEmpty())return false;
    const QPointer<QMimeData> original=const_cast<QMimeData *>(QApplication::clipboard()->mimeData());
    const QPointer<QWidget> context=owner;
    auto result=std::make_shared<PasteResult>();
    auto *progress=new QProgressDialog("正在粘贴文件，请稍候…",QString(),0,0,owner);
    progress->setWindowTitle("文件操作");progress->setCancelButton(nullptr);
    progress->setMinimumDuration(300);progress->setValue(0);
    const QPointer<QProgressDialog> progressGuard=progress;
    auto *worker=QThread::create([result,targetDir,files] { *result=pasteFiles(targetDir,files); });
    active=worker;
    const auto shutdown=QObject::connect(qApp,&QCoreApplication::aboutToQuit,worker,[worker]{worker->wait();});
    QObject::connect(worker,&QThread::finished,qApp,[=] {
        QObject::disconnect(shutdown);
        active.clear();
        if(progressGuard) {progressGuard->close();progressGuard->deleteLater();}
        // Do not overwrite a clipboard changed by the user while copying.
        if(original && QApplication::clipboard()->mimeData()==original) finishClipboard(*result);
        if(context) completed(*result);
        worker->deleteLater();
    });
    worker->start();return true;
}

} // namespace FileClipboard
