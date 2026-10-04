#pragma once
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QImage>
#include <QSaveFile>

namespace VideoWallpaperCache {
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
}
