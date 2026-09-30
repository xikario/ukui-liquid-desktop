#pragma once

#include <QList>
#include <QRect>
#include <QtMath>

namespace WidgetResizeSnap {
// All coordinates are logical desktop pixels. Right/bottom are exclusive:
// touching widgets must neither overlap nor leave a one-pixel seam.
inline QRect geometry(const QRect &start, QSize requested, QSize minimum,
                      QSize maximum, const QRect &bounds,
                      const QList<QRect> &peers, qreal ratio = 0,
                      Qt::Edges fixed = Qt::TopEdge | Qt::LeftEdge)
{
    const bool right = fixed.testFlag(Qt::RightEdge);
    const bool bottom = fixed.testFlag(Qt::BottomEdge);
    const int anchorX = right ? start.x()+start.width() : start.x();
    const int anchorY = bottom ? start.y()+start.height() : start.y();
    maximum = maximum.boundedTo(QSize(
        right ? anchorX-bounds.x() : bounds.x()+bounds.width()-anchorX,
        bottom ? anchorY-bounds.y() : bounds.y()+bounds.height()-anchorY));
    // A pre-existing off-screen/minimum-size conflict must not produce a
    // negative size or silently move the user's fixed corner.
    maximum = maximum.expandedTo(minimum);
    auto rectFor = [&](QSize size) {
        return QRect(QPoint(right ? anchorX-size.width() : anchorX,
                            bottom ? anchorY-size.height() : anchorY), size);
    };
    int minWidth = minimum.width(), maxWidth = maximum.width();
    if (ratio > 0) {
        minWidth = qMax(minWidth, qCeil(minimum.height()*ratio));
        maxWidth = qMax(minWidth, qMin(maxWidth, qFloor(maximum.height()*ratio)));
        requested.setWidth(qBound(minWidth, requested.width(), maxWidth));
        requested.setHeight(qRound(requested.width()/ratio));
    } else {
        requested = requested.expandedTo(minimum).boundedTo(maximum);
    }
    const QRect proposed = rectFor(requested);
    const int movingX = right ? proposed.x() : proposed.x()+proposed.width();
    const int movingY = bottom ? proposed.y() : proposed.y()+proposed.height();
    int bestX = 33, bestY = 33;
    QSize xSize = requested, ySize = requested;
    auto candidate = [&](int edge, bool horizontal, int threshold) {
        const int distance = qAbs(edge-(horizontal ? movingX : movingY));
        int &best = horizontal ? bestX : bestY;
        if (distance > threshold || distance >= best) return;
        QSize size = requested;
        if (horizontal) {
            size.setWidth(right ? anchorX-edge : edge-anchorX);
            if (ratio > 0) size.setHeight(qRound(size.width()/ratio));
        } else {
            size.setHeight(bottom ? anchorY-edge : edge-anchorY);
            if (ratio > 0) size.setWidth(qRound(size.height()*ratio));
        }
        if (size.width()<minimum.width() || size.height()<minimum.height()
            || size.width()>maximum.width() || size.height()>maximum.height()) return;
        best = distance;
        (horizontal ? xSize : ySize) = size;
    };
    candidate(right ? bounds.x() : bounds.x()+bounds.width(), true, 32);
    candidate(bottom ? bounds.y() : bounds.y()+bounds.height(), false, 32);
    const bool screenX = bestX<=32, screenY = bestY<=32;
    for (const QRect &peer : peers) {
        const bool nearY = proposed.y()<=peer.y()+peer.height()+12
            && proposed.y()+proposed.height()>=peer.y()-12;
        const bool nearX = proposed.x()<=peer.x()+peer.width()+12
            && proposed.x()+proposed.width()>=peer.x()-12;
        if (!screenX && nearY) {
            candidate(peer.x(), true, 12);
            candidate(peer.x()+peer.width(), true, 12);
        }
        if (!screenY && nearX) {
            candidate(peer.y(), false, 12);
            candidate(peer.y()+peer.height(), false, 12);
        }
    }
    if (ratio > 0) {
        // One degree of freedom: choose one edge, never distort the widget
        // attempting to satisfy two incompatible neighbours.
        if (screenX != screenY) requested = screenX ? xSize : ySize;
        else if (bestX<=32 || bestY<=32) requested = bestX<=bestY ? xSize : ySize;
    } else {
        requested = QSize(xSize.width(), ySize.height());
    }
    return rectFor(requested);
}
}
