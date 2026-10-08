#pragma once
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QImage>
#include <QSaveFile>
#include <QRegularExpression>

namespace VideoWallpaperCache {
inline void prune(const QString &current, const QString &previous);
inline QString directory(const QString &path) {
    const QFileInfo file(path);
    if (!file.isFile()) return {};
    const auto identity = file.canonicalFilePath().toUtf8() + '\n'
        + QByteArray::number(file.size()) + '\n'
        + QByteArray::number(file.lastModified().toMSecsSinceEpoch());
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/video-previews/v2-" + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
inline QImage poster(const QString &path) {
    const QString dir = directory(path);
    return dir.isEmpty() ? QImage() : QImage(dir + "/poster.png");
}
inline bool saveImage(const QString &dir, const QString &name, const QImage &image) {
    if (dir.isEmpty() || image.isNull() || !QDir().mkpath(dir)) return false;
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(dir + '/' + name);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) return false;
    return file.commit();
}
inline void prune(const QString &current, const QString &previous) {
    const QString keep=directory(current), older=directory(previous);
    if(keep.isEmpty())return;
    QDir root(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/video-previews");
    const QRegularExpression owned("^v2-[0-9a-f]{64}$");
    for(const auto &entry:root.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        if(entry.isSymLink() || !owned.match(entry.fileName()).hasMatch()
            || entry.absoluteFilePath()==keep || entry.absoluteFilePath()==older)continue;
        QDir(entry.absoluteFilePath()).removeRecursively();
    }
}
}
