#pragma once
#include <QImage>
#include <QRect>
#include <QPainterPath>
#include <memory>

// GUI-thread only. One prepared backdrop and one GL backend per renderer.
// Each caller caches its final surface; hover never re-runs the shader.
class LiquidOpticsRenderer final
{
public:
    LiquidOpticsRenderer();
    ~LiquidOpticsRenderer();
    void setOptics(qreal refraction, qreal shade, qreal highlight, qreal chroma = 1.1);
    void setMaterial(qreal clarity = 0, qreal liquidStrength = 1);
    void setWallpaper(const QImage &source);
    QImage renderPanel(const QRect &logicalRect, qreal radius, const QPainterPath &shape = {});
    QImage renderControl(const QRect &logicalRect, qreal radius, bool pressed=false);
    bool usedGpu() const { return m_usedGpu; }
    int preparationCount() const { return m_preparationCount; }
private:
    QImage renderSurface(const QRect &, qreal, const QPainterPath &, int control);
    class Backend;
    std::unique_ptr<Backend> m_backend;
    QImage m_source, m_body, m_clear;
    // Shape is local to the panel, so translating a fence must not rebuild it.
    QImage m_shapeField;
    QPainterPath m_cachedShape;
    QSize m_shapeSize;
    qreal m_shapeDpr = 0;
    int m_preparationCount = 0;
    bool m_usedGpu = false;
    qreal m_clarity=0, m_liquidStrength=1;
    qreal m_refraction=3.5, m_shade=.58, m_highlight=.55, m_chroma=1.1;
};
