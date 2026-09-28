#pragma once
#include <QImage>
#include <QSize>
namespace LiquidPopup {
// Transparent surface for persistent panels. The compositor supplies backdrop
// blur; unlike popup snapshots this never captures the screen on repaint.
struct SurfaceStyle {
    qreal opacity = .58;
    qreal radius = 16;
    qreal highlight = .55;
    bool light = false;
};
QImage renderSurface(QSize logicalSize, qreal dpr, const SurfaceStyle &style);
}
