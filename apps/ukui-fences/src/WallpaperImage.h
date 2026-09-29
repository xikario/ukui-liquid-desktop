#pragma once
#include <QImageReader>
#include <QPixmap>

// Wallpapers downloaded from browsers may have a suffix that does not match
// the encoded image. Use identical decoding for validation, preview and load.
inline QPixmap readWallpaperPixmap(const QString &path, QString *error = nullptr)
{
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (error)
        *error = image.isNull() ? reader.errorString() : QString();
    return QPixmap::fromImage(image);
}
