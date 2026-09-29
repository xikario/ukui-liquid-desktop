#pragma once
#include <QObject>
#include <QImage>
#include <QRectF>
#include <memory>
#include <QCache>
#include "LiquidOpticsRenderer.h"

// Cached full-panel renderer, painted below BOTH the rail and content.
class NextKdeGlassView final : public QObject
{
public:
    explicit NextKdeGlassView(QObject *parent = nullptr);
    ~NextKdeGlassView() override;
    // One-shot synthetic warmup: no desktop capture and no visible material.
    void prepareGpu();
    void setBackdrop(const QImage &image);
    // Explicit low-cost fallback, without shader/FBO resources.
    void setBackdropFast(const QImage &image);
    void setRadius(float radius) { m_radius = radius; }
    const QImage &image() const { return m_image; }
    bool usedGpu() const { return m_usedGpu; }
    qreal luminanceAt(const QRectF &logicalRect) const;
    int gpuInitializationCount() const { return m_gpuInitializationCount; }
    qint64 lastRenderMs() const { return m_lastRenderMs; }
    QImage controlImage(const QRectF &logicalRect, qreal radius, bool pressed=false);
    int controlRenderCount() const { return m_controlRenderCount; }
private:
    std::unique_ptr<LiquidOpticsRenderer> m_optics;
    LiquidOpticsRenderer m_controlOptics;
    int m_gpuInitializationCount = 0;
    qint64 m_lastRenderMs = 0;
    QImage m_image;
    float m_radius = 16.0f;
    bool m_usedGpu = false;
    QCache<QString,QImage> m_controls{8192}; // bounded to 8 MiB, reset per backdrop
    int m_controlRenderCount = 0;
};
