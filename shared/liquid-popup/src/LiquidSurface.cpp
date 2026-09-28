#include "LiquidSurface.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
namespace LiquidPopup {
QImage renderSurface(QSize size, qreal dpr, const SurfaceStyle &s) {
    if (size.isEmpty()) return {};
    dpr=qBound(1.,dpr,3.);
    QImage image(QSize(qCeil(size.width()*dpr),qCeil(size.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
    const QRectF r=QRectF(QPointF(),size).adjusted(.75,.75,-.75,-.75);
    const qreal radius=qBound(0.,s.radius,qMin(r.width(),r.height())/2);
    const qreal alpha=qBound(.15,s.opacity,.95),glow=qBound(0.,s.highlight,1.);
    QPainterPath path;path.addRoundedRect(r,radius,radius);
    // A neutral transparent body leaves visible detail to the compositor.
    // The prior broad white-to-black bevel looked metallic on dark wallpaper.
    QLinearGradient tint(r.topLeft(),r.bottomLeft());
    const QColor body=s.light?QColor(227,236,247):QColor(24,29,39);
    QColor top=body,center=body,bottom=body;
    top.setAlpha(qRound(230*alpha));center.setAlpha(qRound(195*alpha));bottom.setAlpha(qRound(225*alpha));
    tint.setColorAt(0,top);tint.setColorAt(.5,center);tint.setColorAt(1,bottom);
    p.fillPath(path,tint);p.setClipPath(path);
    // Thin opposing reflections suggest lens thickness without a broad bevel
    // or any per-frame screen readback. This is not physical scene refraction.
    const qreal band=qMin(6.,r.height()/5.);
    QLinearGradient upper(r.topLeft(),QPointF(r.left(),r.top()+band));
    upper.setColorAt(0,QColor(247,253,255,qRound(83*glow)));
    upper.setColorAt(.25,QColor(215,237,255,qRound(25*glow)));upper.setColorAt(1,Qt::transparent);
    p.fillRect(QRectF(r.left(),r.top(),r.width(),band),upper);
    QLinearGradient lower(r.bottomLeft(),QPointF(r.left(),r.bottom()-band));
    lower.setColorAt(0,QColor(165,208,242,qRound(52*glow)));lower.setColorAt(1,Qt::transparent);
    p.fillRect(QRectF(r.left(),r.bottom()-band,r.width(),band),lower);
    p.setClipping(false);
    QLinearGradient rim(r.topLeft(),r.bottomRight());
    rim.setColorAt(0,QColor(255,255,255,qRound(185*glow)));
    rim.setColorAt(.35,QColor(213,237,255,qRound(45*glow)));
    rim.setColorAt(.68,QColor(203,225,255,qRound(28*glow)));
    rim.setColorAt(1,QColor(250,253,255,qRound(125*glow)));
    p.setBrush(Qt::NoBrush);p.setPen(QPen(QBrush(rim),.8));p.drawPath(path);
    return image;
}
}
