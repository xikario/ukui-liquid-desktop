#include "GlassFinish.h"
#include <QtGlobal>
#include <QPainter>
#include <cmath>
#include <atomic>
#include <utility>

namespace GlassFinish {
namespace {
constexpr int CELL = 16;            // device pixels per luma cell (1/16 downsample)
constexpr qreal MAX_SCRIM = 0.38;   // extra darkening on top of the shader's own veil
constexpr qreal FLOOR = 0.06;       // fraction of the cap kept on dark backdrops
std::atomic<int> current{int(Scrim::Balanced)};
}

ScrimCurve curve(Scrim preset)
{
    switch (preset) {
    case Scrim::Readable: return {0.72, 1.0};
    case Scrim::Transparent: return {0.22, 0.5};
    case Scrim::Subtle: return {0.15, 0.45};
    case Scrim::Balanced: break;
    }
    return {0.47, 0.75};
}

Scrim scrimFromIndex(int index)
{
    return index >= 0 && index <= 3 ? Scrim(index) : Scrim::Balanced;
}

Scrim scrim() { return Scrim(current.load()); }
void setScrim(Scrim preset) { current.store(int(preset)); }

qreal scrimAlpha(qreal luma, ScrimCurve c)
{
    const qreal x = qBound(0.0, luma, 1.0);
    const qreal s = qBound(0.01, c.strength, 0.99);
    // Schlick bias: passes through (0,0), (0.5,s) and (1,1) without overshoot.
    const qreal biased = x / (x + (1.0 - x) * (1.0 - s) / s);
    return MAX_SCRIM * c.cap * qMax(FLOOR, biased);
}

QVector<qreal> cellLuma(const QImage &source, QSize *cells)
{
    const QImage image = source.format() == QImage::Format_ARGB32_Premultiplied
        ? source : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int cw = qMax(1, (image.width() + CELL - 1) / CELL);
    const int ch = qMax(1, (image.height() + CELL - 1) / CELL);
    QVector<qreal> luma(cw * ch, 0), weight(cw * ch, 0);
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        const int row = (y / CELL) * cw;
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            // Premultiplied channels sum to alpha-weighted luma directly.
            luma[row + x / CELL] += (0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p)) / 255.0;
            weight[row + x / CELL] += qAlpha(p) / 255.0;
        }
    }
    QVector<qreal> raw(cw * ch, 0);
    for (int i = 0; i < raw.size(); ++i) raw[i] = weight[i] > 0 ? luma[i] / weight[i] : 0;
    // 3x3 1:2:1 kernel, edge-clamped, skipping empty cells.
    QVector<qreal> smooth(cw * ch, 0);
    static constexpr int k[3] = {1, 2, 1};
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            qreal sum = 0, total = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int i = qBound(0, y + dy, ch - 1) * cw + qBound(0, x + dx, cw - 1);
                    if (weight[i] <= 0) continue;
                    const qreal w = k[dy + 1] * k[dx + 1] * weight[i];
                    sum += raw[i] * w; total += w;
                }
            smooth[y * cw + x] = total > 0 ? sum / total : 0;
        }
    if (cells) *cells = QSize(cw, ch);
    return smooth;
}

QImage withScrim(const QImage &source, Scrim preset)
{
    if (source.isNull()) return source;
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(source.devicePixelRatio());
    QSize cells;
    const QVector<qreal> luma = cellLuma(image, &cells);
    const ScrimCurve c = curve(preset);
    // Transmission (1-alpha) per cell in 1/256 steps, bilinear between centres.
    QVector<int> keep(luma.size());
    for (int i = 0; i < luma.size(); ++i) keep[i] = qRound(256 * (1.0 - scrimAlpha(luma[i], c)));
    const int cw = cells.width(), ch = cells.height();
    QVector<int> row(cw);
    for (int y = 0; y < image.height(); ++y) {
        const qreal fy = qBound(0.0, (y + 0.5) / CELL - 0.5, qreal(ch - 1));
        const int y0 = int(fy), y1 = qMin(y0 + 1, ch - 1);
        const int ty = qRound((fy - y0) * 256);
        for (int x = 0; x < cw; ++x)
            row[x] = (keep[y0 * cw + x] * (256 - ty) + keep[y1 * cw + x] * ty) >> 8;
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            if (!qAlpha(p)) continue;
            const qreal fx = qBound(0.0, (x + 0.5) / CELL - 0.5, qreal(cw - 1));
            const int x0 = int(fx), x1 = qMin(x0 + 1, cw - 1);
            const int tx = qRound((fx - x0) * 256);
            const int t = (row[x0] * (256 - tx) + row[x1] * tx) >> 8;
            // Darkening premultiplied RGB never exceeds alpha, so coverage is kept.
            line[x] = qRgba((qRed(p) * t) >> 8, (qGreen(p) * t) >> 8, (qBlue(p) * t) >> 8, qAlpha(p));
        }
    }
    return image;
}

QPainterPath squirclePath(const QRectF &rect, qreal radius)
{
    QPainterPath path;
    if (rect.isEmpty()) return path;
    // An exponent-3 quadrant reaches 0.206*s along the diagonal against 0.293*r
    // for a circle; s = 1.45*r keeps it inside the circular corner.
    const qreal s = qMin(radius * 1.45, qMin(rect.width(), rect.height()) / 2);
    constexpr int STEPS = 16;
    const QPointF centres[4] = {{rect.right() - s, rect.top() + s}, {rect.right() - s, rect.bottom() - s},
                                {rect.left() + s, rect.bottom() - s}, {rect.left() + s, rect.top() + s}};
    const int sx[4] = {1, 1, -1, -1}, sy[4] = {-1, 1, 1, -1};
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= STEPS; ++i) {
            // Walk the corners clockwise, each from its incoming edge to the next.
            const qreal t = M_PI_2 * (corner % 2 ? i : STEPS - i) / STEPS;
            const qreal cx = std::cbrt(std::cos(t) * std::cos(t)), cy = std::cbrt(std::sin(t) * std::sin(t));
            const QPointF p = centres[corner] + QPointF(sx[corner] * s * cx, sy[corner] * s * cy);
            if (corner == 0 && i == 0) path.moveTo(p); else path.lineTo(p);
        }
    path.closeSubpath();
    return path;
}

QImage withEdge(const QImage &source, const QPainterPath &outline, bool clip)
{
    if (source.isNull() || outline.isEmpty()) return source;
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(source.devicePixelRatio());
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    if (clip) {
        QImage mask(image.size(), QImage::Format_ARGB32_Premultiplied);
        mask.setDevicePixelRatio(image.devicePixelRatio());
        mask.fill(Qt::transparent);
        { QPainter m(&mask); m.setRenderHint(QPainter::Antialiasing); m.fillPath(outline, Qt::white); }
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, mask);
    }
    // Only add light where glass already exists, and only inside the outline.
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    p.setClipPath(outline);
    const QRectF bounds = outline.boundingRect();
    QLinearGradient light(bounds.topLeft(), bounds.bottomRight());
    light.setColorAt(0, QColor(255, 255, 255, 92));
    light.setColorAt(0.45, QColor(255, 255, 255, 26));
    light.setColorAt(1, QColor(255, 255, 255, 14));
    p.setPen(QPen(QBrush(light), 2.0));  // half of the stroke lies inside the clip
    p.setBrush(Qt::NoBrush);
    p.drawPath(outline);
    return image;
}

const QVector<qint8> &grainTile()
{
    static const QVector<qint8> tile = [] {
        QVector<qint8> values(GRAIN_TILE * GRAIN_TILE);
        quint32 state = 0x9e3779b9u;
        for (int i = 0; i < values.size(); i += 2) {
            state = state * 1664525u + 1013904223u;
            const qint8 v = qint8(int(state >> 24) % (GRAIN_LEVELS + 1));
            // Antithetic pairs make the tile exactly zero-mean before RGB
            // clamping; black/white limits can still slightly bias the output.
            values[i] = v;
            values[i + 1] = qint8(-v);
        }
        // Shuffle pairs apart so +/- neighbours do not form a visible pattern.
        for (int i = values.size() - 1; i > 0; --i) {
            state = state * 1664525u + 1013904223u;
            std::swap(values[i], values[int((state >> 8) % quint32(i + 1))]);
        }
        return values;
    }();
    return tile;
}

QImage withGrain(const QImage &source)
{
    if (source.isNull()) return source;
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(source.devicePixelRatio());
    const QVector<qint8> &tile = grainTile();
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const qint8 *noise = tile.constData() + (y % GRAIN_TILE) * GRAIN_TILE;
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = row[x];
            const int a = qAlpha(px);
            if (!a) continue;
            const int d = noise[x % GRAIN_TILE] * a / 255;
            if (!d) continue;
            row[x] = qRgba(qBound(0, qRed(px) + d, a), qBound(0, qGreen(px) + d, a), qBound(0, qBlue(px) + d, a), a);
        }
    }
    return image;
}
}
