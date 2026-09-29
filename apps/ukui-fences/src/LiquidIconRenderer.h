#pragma once
#include <QColor>
#include <QIcon>
#include <QPainter>

enum class IconVisualStyle { Native, LiquidPlate };
enum class IconSurface { Desktop, Fence };

// Only the material is cached. App icons retain their theme/source and colour.
class LiquidIconRenderer {
public:
    static void drawPlate(QPainter &p, const QRectF &rect, const QColor &accent,
                          qreal strength, qreal dpr, bool hovered, bool selected);
    static void drawIcon(QPainter &p, const QRectF &rect, const QIcon &icon, qreal dpr);
};
