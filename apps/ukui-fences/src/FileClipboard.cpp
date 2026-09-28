#include "FileClipboard.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QProcess>
#include <QUrl>

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

bool copyPath(const QString &srcPath, const QString &targetPath)
{
    const QFileInfo src(srcPath);
    if (!src.exists()) return false;

    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    if (src.isDir()) {
        return QProcess::execute(QStringLiteral("cp"),
            QStringList() << QStringLiteral("-aT") << srcPath << targetPath) == 0;
    }

    QFile::remove(targetPath);
    return QFile::copy(srcPath, targetPath);
}

bool movePath(const QString &srcPath, const QString &targetPath)
{
    const QFileInfo src(srcPath);
    if (!src.exists()) return false;

    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    if (QProcess::execute(QStringLiteral("gio"),
            QStringList() << QStringLiteral("move")
                          << QStringLiteral("-T")
                          << srcPath
                          << targetPath) == 0)
        return true;

    return src.isDir()
        ? QDir().rename(srcPath, targetPath)
        : QFile::rename(srcPath, targetPath);
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
    const QString suffix = fi.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + fi.suffix();

    QString target = QDir(dirPath).absoluteFilePath(fileName);
    for (int i = 1; QFileInfo::exists(target); ++i) {
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

PasteResult pasteFilesToDirectory(const QString &targetDir)
{
    PasteResult result;
    const ClipboardFiles files = readFiles();
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

    if (files.move) {
        if (result.failedPaths.isEmpty())
            clearClipboard();
        else
            writeFiles(result.failedPaths, true);
    }

    return result;
}

} // namespace FileClipboard
