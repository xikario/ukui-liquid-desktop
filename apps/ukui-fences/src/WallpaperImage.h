#pragma once
#include <QImageReader>
#include <QImageIOHandler>
#include <QPixmap>
#include <QGuiApplication>
#include <QScreen>
#include <QFileInfo>
#include <QDateTime>

inline QSize wallpaperDecodeSize()
{
    QSize target(1920,1080);
    for (const auto *screen:QGuiApplication::screens())
        target=target.expandedTo(screen->size()*screen->devicePixelRatio());
    return target;
}

// Pure QImage decoding is safe in background tasks. QPixmap cache stays GUI-only.
inline QImage readWallpaperImage(const QString &path, QString *error, const QSize &target)
{
    if (error) error->clear();
    QImageReader reader(path); reader.setDecideFormatFromContent(true); reader.setAutoTransform(true);
    const QSize source=reader.size();
    // Reject unreasonable headers before allocating. Scaling codecs still need
    // working buffers; this is not a promise of asynchronous decoding.
    if (source.isValid() && qint64(source.width())*source.height()>100000000) {
        if(error) *error=QStringLiteral("图片超过 1 亿像素，请先缩小后使用。"); return {};
    }
    if (source.isValid() && target.isValid()) {
        QSize decodeTarget=target;
        if (reader.transformation() & QImageIOHandler::TransformationRotate90) decodeTarget.transpose();
        QSize scaled=source.scaled(decodeTarget,Qt::KeepAspectRatioByExpanding);
        if (scaled.width()<source.width() && scaled.height()<source.height()) reader.setScaledSize(scaled);
    }
    QImage image=reader.read();
    if(image.isNull()) { if(error) *error=reader.errorString(); return {}; }
    return image;
}

// Content-aware decoding with one bounded cache entry shared by validation,
// preview and display. Native-size tile/centre modes can request full decoding.
inline QPixmap readWallpaperPixmap(const QString &path, QString *error=nullptr,
                                   const QSize &target=wallpaperDecodeSize())
{
    struct Cache { QString key; QPixmap pix; };
    static Cache cache;
    const QFileInfo info(path);
    const QString key=info.absoluteFilePath()+QString(":%1:%2:%3:%4:%5")
        .arg(info.lastModified().toMSecsSinceEpoch()).arg(info.metadataChangeTime().toMSecsSinceEpoch())
        .arg(info.size()).arg(target.width()).arg(target.height());
    if (error) error->clear();
    if (cache.key==key && !cache.pix.isNull()) return cache.pix;
    QImage image = readWallpaperImage(path, error, target);
    if (image.isNull()) return {};
    cache.key=key; cache.pix=QPixmap::fromImage(std::move(image)); return cache.pix;
}
