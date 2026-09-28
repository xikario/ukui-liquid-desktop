#pragma once
#include <QImage>
#include <QRect>
#include <QPainterPath>
#include <memory>

// GUI-thread only. One wallpaper diffusion and one GL backend for all fences.
// FenceWidget owns its final panel cache; hover never re-runs the shader.
class FenceGlassRenderer final
{
public:
    FenceGlassRenderer();
    ~FenceGlassRenderer();
    void setWallpaper(const QImage &source);
    QImage renderPanel(const QRect &logicalRect, qreal radius, const QPainterPath &shape = {});
    bool usedGpu() const { return m_usedGpu; }
    int preparationCount() const { return m_preparationCount; }
private:
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
};
