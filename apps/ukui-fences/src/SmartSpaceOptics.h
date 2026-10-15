#pragma once
#include <QColor>
#include <QImage>
#include <QVector>

// Wallpaper-independent optical samples for the narrow meniscus only.
// The caller owns the final image; stable paint/hover does not run this code.
class SmartSpaceOptics final {
public:
    // Both sources are local wallpaper crops. Only rebuilt on a backdrop change.
    static void prepareBackdrop(const QImage &wallpaper, qreal dpr,
                                QImage &frost, QImage &edge);
    // Recolor existing coverage without changing its alpha.
    static void tintSurface(QImage &material, const QImage &frost);
    void apply(QImage &material, const QImage &sharp);
    // Check the finished reading surface once; fade extra shade through the
    // existing optical band so the outer reflection can remain bright.
    qreal protectReadingArea(QImage &material, const QColor &text, const QColor &muted);
    void clear();
    qsizetype bytes() const { return qsizetype(m_samples.capacity()) * sizeof(Sample); }
    int geometryBuilds() const { return m_builds; }
private:
    struct Sample {
        int pixel;
        qint16 dx16,dy16;
        qint8 dispersionX,dispersionY;
        quint8 mix;
        qint8 light;
    };
    static_assert(sizeof(Sample)==12,"Optical sample memory must stay bounded");
    void prepare(QSize,qreal);
    QVector<Sample> m_samples;
    QSize m_size;
    qreal m_dpr=0;
    int m_builds=0;
};
