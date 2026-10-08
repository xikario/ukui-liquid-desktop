#pragma once
#include <QImage>
#include <QPainterPath>
#include <QRegion>
#include <QTransform>
#include <QVariant>
#include <QWidget>
#include <QtMath>

// Native video sits above the canvas backing store. Reserve only pixels a
// child really paints, while keeping its normal rectangular mouse hit area.
// Regions are in widget-local device pixels: logical regions at fractional
// scale factors step in 1.5px stairs that expose the static poster.
namespace VideoWallpaperRegion {
constexpr const char *propertyName = "videoWallpaperPaintRegion";
// An edge pixel blends the widget over the poster. Below roughly a third of
// coverage it reads closer to the live video than to the widget.
constexpr int coverageAlpha = 80;

inline QRegion coverage(const QImage &source, qreal dpr)
{
    if (source.isNull()) return {};
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal scale = dpr / image.devicePixelRatioF();
    if (!qFuzzyCompare(scale, 1.0))
        image = image.scaled(qRound(image.width()*scale), qRound(image.height()*scale),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QRegion region;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        int start = -1;
        for (int x = 0; x <= image.width(); ++x) {
            const bool painted = x < image.width() && qAlpha(row[x]) >= coverageAlpha;
            if (painted && start < 0) start = x;
            if (!painted && start >= 0) {
                region |= QRect(start, y, x-start, 1);
                start = -1;
            }
        }
    }
    return region;
}

inline QRegion shape(const QPainterPath &logical, qreal dpr)
{
    return QRegion(QTransform::fromScale(dpr, dpr).map(logical).toFillPolygon().toPolygon());
}

inline QRegion rounded(const QRectF &rect, qreal radius, qreal dpr)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return shape(path, dpr);
}

inline QRegion toDevice(const QRegion &logical, qreal dpr)
{
    QRegion region;
    for (const QRect &rect : logical) {
        const int left = qFloor(rect.left()*dpr), top = qFloor(rect.top()*dpr);
        region |= QRect(left, top, qCeil((rect.right()+1)*dpr)-left, qCeil((rect.bottom()+1)*dpr)-top);
    }
    return region;
}

inline void publish(QWidget *widget, const QRegion &region)
{
    const QVariant old = widget->property(propertyName);
    if (!old.isValid() || old.value<QRegion>() != region)
        widget->setProperty(propertyName, QVariant::fromValue(region));
}

inline QRegion painted(const QWidget *widget)
{
    const qreal dpr = widget->devicePixelRatioF();
    const QVariant custom = widget->property(propertyName);
    QRegion region = custom.isValid() ? custom.value<QRegion>() : toDevice(QRegion(widget->rect()), dpr);
    region &= toDevice(QRegion(widget->rect()), dpr);
    if (!widget->mask().isEmpty()) region &= toDevice(widget->mask(), dpr);
    return region;
}
}
