#pragma once
#include <QImage>
#include <QSize>

namespace LiquidMaterial {
// GUI-independent, bounded-resolution six-pass diffusion. Shared by all
// optical adapters; integer rounding and edge extension match the old output.
inline QImage diffuse(const QImage &source, QSize logical, int radius=2, int divisor=3)
{
    if (source.isNull()) return {};
    QImage a = source.scaled(qMax(1, logical.width()/divisor), qMax(1, logical.height()/divisor),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(), a.format());
    for (int pass = 0; pass < 6; ++pass) {
        const bool horizontal = pass % 2 == 0;
        const int lines = horizontal ? a.height() : a.width();
        const int length = horizontal ? a.width() : a.height();
        for (int line = 0; line < lines; ++line) {
            auto pixel = [&](int position) {
                position = qBound(0, position, length-1);
                return reinterpret_cast<const QRgb *>(a.constScanLine(horizontal ? line : position))[horizontal ? position : line];
            };
            int r=0, g=0, blue=0;
            for (int k=-radius; k<=radius; ++k) { const QRgb c=pixel(k); r+=qRed(c); g+=qGreen(c); blue+=qBlue(c); }
            for (int position=0; position<length; ++position) {
                reinterpret_cast<QRgb *>(b.scanLine(horizontal ? line : position))[horizontal ? position : line]
                    = qRgb(r/(radius*2+1), g/(radius*2+1), blue/(radius*2+1));
                const QRgb old=pixel(position-radius), next=pixel(position+radius+1);
                r+=qRed(next)-qRed(old); g+=qGreen(next)-qGreen(old); blue+=qBlue(next)-qBlue(old);
            }
        }
        a.swap(b);
    }
    return a.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
struct Prepared {
    QImage source, body, clear;
};
inline Prepared prepare(const QImage &source)
{
    Prepared material; material.source=source;
    if (source.isNull()) return material;
    const qreal dpr=source.devicePixelRatio();
    const QSize logical(qMax(1,qRound(source.width()/dpr)),qMax(1,qRound(source.height()/dpr)));
    material.body=diffuse(source,logical); material.clear=diffuse(source,logical,1,2);
    material.body.setDevicePixelRatio(1); material.clear.setDevicePixelRatio(1);
    return material;
}
}
