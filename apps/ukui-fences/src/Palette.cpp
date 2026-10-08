#include "Palette.h"
#include <QHash>
#include <algorithm>
#include <cmath>

namespace Palette {
namespace {
qreal linear(qreal channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}
constexpr int MaxSide = 48;
}

qreal luminance(const QColor &color)
{
    const QColor c = color.toRgb();
    return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
}

qreal contrast(const QColor &a, const QColor &b)
{
    const qreal la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

QColor readableText(const QColor &background)
{
    return contrast(Qt::white, background) >= contrast(Qt::black, background)
        ? QColor(Qt::white) : QColor(Qt::black);
}

QColor mix(const QColor &base, const QColor &overlay, qreal amount)
{
    const qreal t = qBound<qreal>(0, amount, 1);
    const QColor a = base.toRgb(), b = overlay.toRgb();
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QColor ensureContrast(const QColor &foreground, const QColor &background, qreal ratio)
{
    return ensureContrast(foreground, QVector<QColor>{background}, ratio);
}

qreal minContrast(const QColor &foreground, const QVector<QColor> &backgrounds)
{
    qreal worst = 21;
    for (const QColor &background : backgrounds)
        worst = qMin(worst, contrast(foreground, background));
    return worst;
}

QColor ensureContrast(const QColor &foreground, const QVector<QColor> &backgrounds, qreal ratio)
{
    if (backgrounds.isEmpty() || minContrast(foreground, backgrounds) >= ratio)
        return foreground;
    const bool white = minContrast(Qt::white, backgrounds) >= minContrast(Qt::black, backgrounds);
    const QColor target = white ? QColor(255, 255, 255, foreground.alpha()) : QColor(0, 0, 0, foreground.alpha());
    if (minContrast(target, backgrounds) < ratio)
        return target;
    // `high` always satisfies the ratio, so the result does too even where the
    // worst-case curve is not monotonic.
    qreal low = 0, high = 1;
    for (int i = 0; i < 12; ++i) {
        const qreal mid = (low + high) / 2;
        (minContrast(mix(foreground, target, mid), backgrounds) >= ratio ? high : low) = mid;
    }
    return mix(foreground, target, high);
}

qreal toneLift(const QVector<QColor> &backgrounds, qreal ratio, const QColor &text)
{
    const QColor opposite = luminance(text) > .5 ? QColor(Qt::black) : QColor(Qt::white);
    auto lifted = [&](qreal amount) {
        QVector<QColor> moved;
        moved.reserve(backgrounds.size());
        for (const QColor &background : backgrounds)
            moved.append(mix(background, opposite, amount));
        return minContrast(text, moved);
    };
    if (backgrounds.isEmpty() || lifted(0) >= ratio)
        return 0;
    qreal low = 0, high = 1;
    for (int i = 0; i < 14; ++i) {
        const qreal mid = (low + high) / 2;
        (lifted(mid) >= ratio ? high : low) = mid;
    }
    return high;
}

bool usableSwatch(const QColor &color)
{
    const QColor c = color.toRgb();
    const qreal hi = qMax(c.redF(), qMax(c.greenF(), c.blueF()));
    const qreal lo = qMin(c.redF(), qMin(c.greenF(), c.blueF()));
    return hi > 0.16 && lo < 0.90 && hi - lo > 0.10;
}

qreal distance(const QColor &a, const QColor &b)
{
    const QColor x = a.toRgb(), y = b.toRgb();
    return qAbs(x.redF() - y.redF()) + qAbs(x.greenF() - y.greenF()) + qAbs(x.blueF() - y.blueF());
}

Colors extract(const QImage &image)
{
    if (image.isNull())
        return {};
    const QImage small = image.scaled(MaxSide, MaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32);
    struct Bin { qreal r = 0, g = 0, b = 0, weight = 0; };
    QHash<int, Bin> bins;
    for (int y = 0; y < small.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QRgb px = row[x];
            if (qAlpha(px) < 128 || !usableSwatch(QColor(px)))
                continue;
            Bin &bin = bins[(qRed(px) >> 4) << 8 | (qGreen(px) >> 4) << 4 | (qBlue(px) >> 4)];
            bin.r += qRed(px); bin.g += qGreen(px); bin.b += qBlue(px); bin.weight += 1;
        }
    }
    QVector<Bin> ranked;
    ranked.reserve(bins.size());
    for (const Bin &bin : qAsConst(bins))
        ranked.append(bin);
    std::sort(ranked.begin(), ranked.end(), [](const Bin &a, const Bin &b) { return a.weight > b.weight; });
    auto colorOf = [](const Bin &bin) {
        return QColor(qRound(bin.r / bin.weight), qRound(bin.g / bin.weight), qRound(bin.b / bin.weight));
    };
    Colors result;
    for (const Bin &bin : qAsConst(ranked)) {
        const QColor color = colorOf(bin);
        if (!result.primary.isValid()) {
            result.primary = color;
        } else if (distance(color, result.primary) > MinSecondaryDistance) {
            result.secondary = color;
            break;
        }
    }
    return result;
}
}
