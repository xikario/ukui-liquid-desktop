#pragma once
#include <QImage>
#include <QVector>

// Wallpaper-independent optical samples for the narrow meniscus only.
// The caller owns the final image; stable paint/hover does not run this code.
class SmartSpaceOptics final {
public:
    void apply(QImage &material, const QImage &sharp);
    void clear();
    qsizetype bytes() const { return qsizetype(m_samples.capacity()) * sizeof(Sample); }
    int geometryBuilds() const { return m_builds; }
private:
    struct Sample {
        int pixel;
        qint16 dx16,dy16;
        qint8 dispersionX,dispersionY;
        quint8 mix,light;
    };
    static_assert(sizeof(Sample)==12,"Optical sample memory must stay bounded");
    void prepare(QSize,qreal);
    QVector<Sample> m_samples;
    QSize m_size;
    qreal m_dpr=0;
    int m_builds=0;
};
