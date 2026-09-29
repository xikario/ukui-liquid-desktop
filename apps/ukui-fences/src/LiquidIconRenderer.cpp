#include "LiquidIconRenderer.h"
#include <QPixmapCache>
#include <QPainterPath>
#include <QtMath>

void LiquidIconRenderer::drawPlate(QPainter &p, const QRectF &rect,
    const QColor &accent, qreal strength, qreal dpr, bool hovered, bool selected)
{
    strength = qBound(0., strength, 1.);
    dpr = qMax(1., dpr);
    const qreal pad = 3.;
    const QSizeF size = rect.size() + QSizeF(pad*2, pad*2);
    const QString key = QStringLiteral("liquid-icon-v1:%1:%2:%3:%4:%5")
        .arg(rect.width(),0,'f',2).arg(rect.height(),0,'f',2)
        .arg(dpr,0,'f',3).arg(accent.rgba()).arg(qRound(strength*100));
    QPixmap material;
    if (!QPixmapCache::find(key, &material)) {
        material = QPixmap(qCeil(size.width()*dpr), qCeil(size.height()*dpr));
        material.setDevicePixelRatio(dpr);
        material.fill(Qt::transparent);
        QPainter g(&material);
        g.setRenderHint(QPainter::Antialiasing);
        const QRectF r(QPointF(pad,pad), rect.size());
        const qreal radius = r.width() * .235;
        g.setPen(Qt::NoPen);
        for (int i=3; i>=1; --i) {
            g.setBrush(QColor(0,0,0,qRound((4-i)*5*strength)));
            g.drawRoundedRect(r.adjusted(-i*.5, 1, i*.5, i*.65), radius+i*.5, radius+i*.5);
        }
        QLinearGradient glass(r.topLeft(),r.bottomRight());
        glass.setColorAt(0, QColor(255,255,255,qRound(110*strength)));
        glass.setColorAt(.45, QColor(255,255,255,qRound(36*strength)));
        glass.setColorAt(1, QColor(220,235,250,qRound(64*strength)));
        g.setBrush(glass); g.drawRoundedRect(r,radius,radius);
        QColor tint=accent; tint.setAlpha(qRound(46*strength));
        g.setBrush(tint); g.drawRoundedRect(r,radius,radius);
        QLinearGradient rim(r.topLeft(),r.bottomRight());
        rim.setColorAt(0,QColor(255,255,255,qRound(185*strength)));
        rim.setColorAt(.45,QColor(255,255,255,qRound(32*strength)));
        rim.setColorAt(1,QColor(255,255,255,qRound(100*strength)));
        g.setBrush(Qt::NoBrush); g.setPen(QPen(rim, .9));
        g.drawRoundedRect(r.adjusted(.5,.5,-.5,-.5),radius,radius);
        QPainterPath clip; clip.addRoundedRect(r.adjusted(1,1,-1,-1),radius-1,radius-1);
        g.setClipPath(clip);
        QRadialGradient shine(r.topLeft()+QPointF(r.width()*.22,0),r.width()*.8);
        shine.setColorAt(0,QColor(255,255,255,qRound(85*strength)));
        shine.setColorAt(1,Qt::transparent);
        g.fillRect(r,shine);
        g.end(); QPixmapCache::insert(key, material);
    }
    p.save();
    p.drawPixmap(rect.topLeft()-QPointF(pad,pad),material);
    if (hovered || selected) {
        QColor border = selected ? accent : QColor(Qt::white);
        border.setAlpha(selected ? 205 : 105);
        p.setPen(QPen(border, selected ? 1.5 : 1.));
        p.setBrush(QColor(255,255,255,hovered ? 18 : 8));
        p.drawRoundedRect(rect.adjusted(.75,.75,-.75,-.75),rect.width()*.235,rect.width()*.235);
    }
    p.restore();
}

void LiquidIconRenderer::drawIcon(QPainter &p, const QRectF &rect, const QIcon &icon, qreal dpr)
{
    const QSize size(qCeil(rect.width()*dpr),qCeil(rect.height()*dpr));
    const QPixmap pix=icon.pixmap(size,QIcon::Normal);
    const QSizeF fitted=QSizeF(pix.size()).scaled(rect.size(),Qt::KeepAspectRatio);
    const QRectF target(rect.center()-QPointF(fitted.width()/2,fitted.height()/2),fitted);
    p.drawPixmap(target,pix,QRectF(pix.rect()));
}
