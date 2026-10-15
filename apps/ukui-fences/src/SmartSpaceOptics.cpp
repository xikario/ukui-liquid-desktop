#include "SmartSpaceOptics.h"
#include "Palette.h"
#include <QPainter>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QtMath>
#include <cmath>

namespace {
// Three separable box passes approximate a Gaussian. The working image is
// downsampled; row accumulators keep the vertical pass contiguous in memory.
// No scanLine()/detach in a per-pixel loop and no unsigned channel subtraction.
void blur(QImage &image, int radius)
{
    if (image.isNull() || radius <= 0) return;
    const int w = image.width(), h = image.height();
    radius = qMin(radius, qMax(w, h));
    const int count = 2 * radius + 1;
    QImage scratch(image.size(), QImage::Format_RGB32);
    QVector<int> sums(w * 3);
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            const auto *src = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            auto *dst = reinterpret_cast<QRgb *>(scratch.scanLine(y));
            int r = 0, g = 0, b = 0;
            for (int i = -radius; i <= radius; ++i) {
                const QRgb p = src[qBound(0, i, w - 1)];
                r += qRed(p); g += qGreen(p); b += qBlue(p);
            }
            for (int x = 0; x < w; ++x) {
                dst[x] = qRgb((r + count / 2) / count, (g + count / 2) / count,
                              (b + count / 2) / count);
                const QRgb a = src[qMax(0, x - radius)];
                const QRgb z = src[qMin(w - 1, x + radius + 1)];
                r += qRed(z) - qRed(a); g += qGreen(z) - qGreen(a); b += qBlue(z) - qBlue(a);
            }
        }
        sums.fill(0);
        for (int i = -radius; i <= radius; ++i) {
            const auto *src = reinterpret_cast<const QRgb *>(scratch.constScanLine(qBound(0, i, h - 1)));
            for (int x = 0; x < w; ++x) {
                sums[x * 3] += qRed(src[x]); sums[x * 3 + 1] += qGreen(src[x]); sums[x * 3 + 2] += qBlue(src[x]);
            }
        }
        for (int y = 0; y < h; ++y) {
            auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
            const auto *a = reinterpret_cast<const QRgb *>(scratch.constScanLine(qMax(0, y - radius)));
            const auto *z = reinterpret_cast<const QRgb *>(scratch.constScanLine(qMin(h - 1, y + radius + 1)));
            for (int x = 0; x < w; ++x) {
                int *sum = sums.data() + x * 3;
                dst[x] = qRgb((sum[0] + count / 2) / count, (sum[1] + count / 2) / count,
                              (sum[2] + count / 2) / count);
                sum[0] += qRed(z[x]) - qRed(a[x]); sum[1] += qGreen(z[x]) - qGreen(a[x]);
                sum[2] += qBlue(z[x]) - qBlue(a[x]);
            }
        }
    }
}
}

void SmartSpaceOptics::prepareBackdrop(const QImage &wallpaper, qreal dpr,
                                      QImage &frost, QImage &edge)
{
    frost = {}; edge = {};
    if (wallpaper.isNull()) return;
    const QImage opaque = wallpaper.convertToFormat(QImage::Format_RGB32);
    edge = opaque.scaled(qMax(1, opaque.width() / 2), qMax(1, opaque.height() / 2),
                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    blur(edge, qMax(1, qRound(2 * dpr)));
    QImage small = edge.scaled(qMax(1, opaque.width() / 4), qMax(1, opaque.height() / 4),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Radius follows DPR, so the frost has the same logical depth at 1–2x.
    blur(small, qMax(1, qRound(11 * dpr)));
    // Match the original backdrop's raster format. Qt's RGB32 clip fast path
    // otherwise rounds antialiased corner alpha differently by one level.
    frost = small.scaled(wallpaper.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void SmartSpaceOptics::clear()
{
    QVector<Sample>().swap(m_samples);m_size={};m_dpr=0;
}

void SmartSpaceOptics::tintSurface(QImage &material, const QImage &frost)
{
    if (material.isNull() || frost.isNull()) return;
    const qreal dpr = material.devicePixelRatioF();
    const QRectF bounds(0, 0, material.width() / dpr, material.height() / dpr);
    {
        QPainter painter(&material);
        // The original material owns the exact antialiased outline. SourceAtop
        // changes color inside that coverage, without adding a second mask.
        painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        painter.drawImage(bounds, frost);
        const qreal diagonal = (bounds.width() + bounds.height()) * .5;
        QLinearGradient veil(0, 0, diagonal, diagonal);
        veil.setColorAt(0, QColor(10, 15, 20, 72));
        veil.setColorAt(1, QColor(10, 15, 20, 98));
        painter.fillRect(bounds, veil);
        QRadialGradient glow(QPointF(bounds.width() * .22, bounds.height() * .04),
                             qMax(bounds.width(), bounds.height()) * .78);
        glow.setColorAt(0, QColor(255, 255, 255, 18));
        glow.setColorAt(.25, QColor(255, 255, 255, 10));
        glow.setColorAt(.5, QColor(255, 255, 255, 5));
        glow.setColorAt(.75, QColor(255, 255, 255, 1));
        glow.setColorAt(1, QColor(255, 255, 255, 0));
        painter.fillRect(bounds, glow);
    }
}

qreal SmartSpaceOptics::protectReadingArea(QImage &material, const QColor &text, const QColor &muted)
{
    if (material.isNull()) return 0;
    // Check the final scrim/grain result rather than darkening a second time
    // before those passes. The frost is smooth; small headroom covers points
    // between this 24-logical-pixel sampling grid.
    const qreal dpr = material.devicePixelRatioF();
    const int inset = qCeil(36 * dpr), step = qMax(1, qRound(24 * dpr));
    QVector<QColor> backgrounds;
    for (int y = inset; y < material.height() - inset; y += step) {
        const auto *row = reinterpret_cast<const QRgb *>(material.constScanLine(y));
        for (int x = inset; x < material.width() - inset; x += step) {
            const QRgb p = row[x];
            if (qAlpha(p) != 255) continue;
            backgrounds.append(QColor(qMin(255, qRed(p) + 2), qMin(255, qGreen(p) + 2),
                                      qMin(255, qBlue(p) + 2)));
        }
    }
    const qreal shade = qMax(Palette::toneLift(backgrounds, Palette::PrimaryTextContrast + .2, text),
                             Palette::toneLift(backgrounds, Palette::SecondaryTextContrast + .1, muted));
    if (shade > 0) {
        prepare(material.size(), dpr);
        const int opacity = qCeil(shade * 255);
        int sampleIndex = 0;
        for (int y = 0; y < material.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(material.scanLine(y));
            for (int x = 0; x < material.width(); ++x) {
                const int pixel = y * material.width() + x;
                int keep = 255 - opacity;
                if (sampleIndex < m_samples.size() && m_samples[sampleIndex].pixel == pixel) {
                    keep = 255 - opacity * (255 - m_samples[sampleIndex].mix) / 255;
                    ++sampleIndex;
                }
                const QRgb p = row[x];
                row[x] = qRgba(qRed(p) * keep / 255, qGreen(p) * keep / 255,
                                qBlue(p) * keep / 255, qAlpha(p));
            }
        }
    }
    return shade;
}

void SmartSpaceOptics::prepare(QSize size,qreal dpr)
{
    if(size==m_size && qFuzzyCompare(dpr,m_dpr))return;
    m_samples.clear();m_size=size;m_dpr=dpr;++m_builds;
    if(size.isEmpty())return;
    const float inset=5*dpr,radius=19*dpr,band=21*dpr,innerRadius=12*dpr;
    const float hw=size.width()*.5f-inset,hh=size.height()*.5f-inset;
    const float bx=qMax(0.f,hw-radius),by=qMax(0.f,hh-radius);
    const float ix=qMax(0.f,hw-band-innerRadius),iy=qMax(0.f,hh-band-innerRadius);
    // Allocate for a perimeter, never width*height. No full-size mask/LUT.
    const int reserve=qMin(qint64(size.width())*size.height(),
        qint64(qCeil(2*(size.width()+size.height())*(band+2*dpr)+4*innerRadius*innerRadius)));
    m_samples.reserve(reserve);
    constexpr float pi=3.14159265358979323846f;
    for(int y=0;y<size.height();++y) {
        const float py=y+.5f-size.height()*.5f;
        for(int x=0;x<size.width();++x) {
            const float px=x+.5f-size.width()*.5f;
            if(std::abs(px)<ix && std::abs(py)<iy)continue;
            const float qx=std::abs(px)-bx,qy=std::abs(py)-by;
            const float mx=qMax(qx,0.f),my=qMax(qy,0.f);
            const float outside=std::sqrt(mx*mx+my*my);
            const float outer=outside+qMin(qMax(qx,qy),0.f)-radius;
            if(outer>.75f)continue;
            const float ax=std::abs(px)-ix,ay=std::abs(py)-iy;
            const float imx=qMax(ax,0.f),imy=qMax(ay,0.f);
            const float distance=std::sqrt(imx*imx+imy*imy);
            const float inner=distance+qMin(qMax(ax,ay),0.f)-innerRadius;
            if(inner<=0)continue;
            // Two rounded envelopes keep the inner plateau curved even when
            // the optical band is wider than the outer corner radius.
            const float u=qBound(0.f,inner/qMax(1.f,inner-outer),1.f);
            const float gx=imx/qMax(1e-4f,distance)*(px<0?-1:1);
            const float gy=imy/qMax(1e-4f,distance)*(py<0?-1:1);
            const float corner=1+.35f*2*imx*imy/qMax(1e-4f,distance*distance);
            const float pull=(.52f*std::pow(u,1.15f)+.48f*u*u*u)*48*dpr*corner;
            const float tangent=(1-u)*u*2.5f*dpr*(px*py>0?1:-1);
            const float direction=-.68f*gx-.68f*gy;
            const float lit=qMax(0.f,direction),opposite=qMax(0.f,-direction);
            const float trough=u<.36f?-26*std::sin(u/.36f*pi):0;
            const float cushion=u>=.32f && u<.92f
                ?std::sin((u-.32f)/.60f*pi)*(18+32*lit+22*opposite)*corner:0;
            const float rimT=qBound(0.f,1+outer/(2.2f*float(dpr)),1.f);
            const int light=qRound(trough+cushion+rimT*rimT*(78+56*lit+24*opposite));
            m_samples.append({y*size.width()+x,
                qint16(qRound((-gx*pull-gy*tangent)*16)),qint16(qRound((-gy*pull+gx*tangent)*16)),
                // Dispersion is in eighths of a device pixel. Subpixel taps
                // retain a quiet color fringe at every scale.
                qint8(qBound(-127,qRound(gx*2.6f*dpr*u*u*8),127)),
                qint8(qBound(-127,qRound(gy*2.6f*dpr*u*u*8),127)),
                quint8(qRound(qMin(1.f,std::pow(u,.62f)*1.12f)*242)),qint8(qBound(-128,light,127))});
        }
    }
}

void SmartSpaceOptics::apply(QImage &material,const QImage &source)
{
    if(material.isNull() || source.isNull())return;
    prepare(material.size(),material.devicePixelRatioF());
    // Sharp input is local to this widget's sampled wallpaper rectangle;
    // physical ratios handle odd sizes, fractional DPR and negative screens.
    const QImage sharp=source.format()==QImage::Format_RGB32?source:source.convertToFormat(QImage::Format_RGB32);
    const int w=material.width(),h=material.height(),sw=sharp.width(),sh=sharp.height();
    auto sample=[&](qint64 x16,qint64 y16){
        const int sx16=int(qBound(qint64(0),x16*sw/w,qint64(sw-1)*16));
        const int sy16=int(qBound(qint64(0),y16*sh/h,qint64(sh-1)*16));
        const int sx=sx16/16,sy=sy16/16,tx=sx16%16,ty=sy16%16;
        const auto *top=reinterpret_cast<const QRgb *>(sharp.constScanLine(sy));
        const auto *bottom=reinterpret_cast<const QRgb *>(sharp.constScanLine(qMin(sy+1,sh-1)));
        const QRgb a=top[sx],b=top[qMin(sx+1,sw-1)],c=bottom[sx],d=bottom[qMin(sx+1,sw-1)];
        auto mix=[&](int av,int bv,int cv,int dv){return
            (av*(16-tx)*(16-ty)+bv*tx*(16-ty)+cv*(16-tx)*ty+dv*tx*ty+128)/256;};
        return qRgb(mix(qRed(a),qRed(b),qRed(c),qRed(d)),mix(qGreen(a),qGreen(b),qGreen(c),qGreen(d)),
                    mix(qBlue(a),qBlue(b),qBlue(c),qBlue(d)));
    };
    QRgb *row=nullptr;int rowY=-1;
    for(const Sample &s:m_samples) {
        const int y=s.pixel/w,x=s.pixel-y*w;
        if(y!=rowY){row=reinterpret_cast<QRgb *>(material.scanLine(y));rowY=y;}
        const QRgb old=row[x];const int a=qAlpha(old);
        if(!a)continue;
        const qint64 rx=qint64(x)*16+s.dx16,ry=qint64(y)*16+s.dy16;
        const QRgb red=sample(rx+s.dispersionX*2,ry+s.dispersionY*2);
        const QRgb green=sample(rx,ry);
        const QRgb blue=sample(rx-s.dispersionX*2,ry-s.dispersionY*2);
        const int luma=(qRed(red)*77+qGreen(green)*150+qBlue(blue)*29)/256;
        auto saturated=[&](int value){return qBound(0,luma+(value-luma)*310/256,255);};
        auto channel=[&](int base,int clear,int tint){
            base=base*255/a;
            // A neutral veil keeps a readable dark rim under bright images.
            clear=(clear*220+tint*35)/255;
            const int result=qBound(0,(base*(255-s.mix)+clear*s.mix)/255+s.light,255);
            return (result*a+127)/255;
        };
        row[x]=qRgba(channel(qRed(old),saturated(qRed(red)),8),channel(qGreen(old),saturated(qGreen(green)),13),
                     channel(qBlue(old),saturated(qBlue(blue)),21),a);
    }
}
