// Optical reference from a884174, retained to detect effect regressions.
#pragma once
#include "LiquidPopup.h"
#include <QtMath>
#include <QPainter>
#include <cmath>
namespace MaterialReference {
using LiquidPopup::theme;
QImage diffuse(const QImage &in, QSize size) {
    // Bounded small convolution, once per popup; no animation-frame capture.
    QImage a = in.scaled(qMax(1,size.width()/4),qMax(1,size.height()/4),
        Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(),a.format());
    for (int pass=0;pass<4;++pass) {
        for(int y=0;y<a.height();++y) for(int x=0;x<a.width();++x) {
            int r=0,g=0,bl=0;
            for(int k=-2;k<=2;++k) {
                const QRgb c=a.pixel(qBound(0,x+(pass%2?0:k),a.width()-1),
                                    qBound(0,y+(pass%2?k:0),a.height()-1));
                r+=qRed(c);g+=qGreen(c);bl+=qBlue(c);
            }
            b.setPixel(x,y,qRgb(r/5,g/5,bl/5));
        }
        a.swap(b);
    }
    return a.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
QImage renderMaterial(const QImage &input,QSize logical,qreal dpr,bool light,QRectF bounds={}) {
    if(logical.isEmpty()) return {};
    dpr=qBound(1.0,dpr,3.0);
    if(bounds.isEmpty()) bounds=QRectF(QPointF(0,0),QSizeF(logical));
    const QSize size(qCeil(logical.width()*dpr),qCeil(logical.height()*dpr));
    QImage source=input;
    if(source.isNull()) {source=QImage(size,QImage::Format_RGB32);source.fill(light?QColor(222,227,234):QColor(40,47,57));}
    source=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage body=diffuse(source,size),out(size,QImage::Format_ARGB32_Premultiplied);
    // Approximate edge lens. Cached raster backend; not a compositor shader.
    for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) {
        const qreal px=(x+.5)/dpr,py=(y+.5)/dpr;
        const qreal radius=qMin(theme().radius,qMin(bounds.width(),bounds.height())/2);
        const qreal sx=px-bounds.center().x(),sy=py-bounds.center().y();
        const qreal qx=std::abs(sx)-bounds.width()/2+radius;
        const qreal qy=std::abs(sy)-bounds.height()/2+radius;
        const qreal vx=qMax(qx,0.0),vy=qMax(qy,0.0),length=std::hypot(vx,vy);
        const qreal distance=length+qMin(qMax(qx,qy),0.0)-radius;
        const qreal edge=qMax(-distance,0.0);
        const qreal rim=std::exp(-edge/6.0);
        const qreal nx=(sx<0?-1:1)*(length>0?vx/length:(qx>qy?1:0));
        const qreal ny=(sy<0?-1:1)*(length>0?vy/length:(qx>qy?0:1));
        const qreal dx=-nx*theme().refraction*rim*dpr;
        const qreal dy=-ny*theme().refraction*rim*dpr;
        const QRgb clear=source.pixel(qBound(0,qRound(x+dx),size.width()-1),qBound(0,qRound(y+dy),size.height()-1));
        const QRgb blur=body.pixel(x,y);
        const qreal luminance=(.2126*qRed(blur)+.7152*qGreen(blur)+.0722*qBlue(blur))/255.;
        const qreal contrastRisk=light ? qBound(0.0,(.55-luminance)/.55,1.0)
                                      : qBound(0.0,(luminance-.4)/.6,1.0);
        const qreal tint=qBound(0.0,(theme().tint+.23*contrastRisk)*(1-rim*.5),.9),target=light?244:15;
        const qreal glow=theme().highlight*26*std::exp(-py/18.0);
        auto c=[&](int a,int b){return qBound(0,qRound((a*(1-rim*.7)+b*rim*.7)*(1-tint)+target*tint+glow),255);};
        out.setPixel(x,y,qRgb(c(qRed(blur),qRed(clear)),c(qGreen(blur),qGreen(clear)),c(qBlue(blur),qBlue(clear))));
    }
    out.setDevicePixelRatio(dpr);return out;
}
QImage renderMenuMaterial(const QImage &input,QSize logical,qreal dpr,bool light) {
    QImage out=renderMaterial(input,logical,dpr,light);
    if(out.isNull())return out;
    const QRectF body=QRectF(QPointF(),QSizeF(logical)).adjusted(.5,.5,-.5,-.5);
    const qreal radius=qMin(theme().radius,qMin(body.width(),body.height())/2.);
    QPainterPath path;path.addRoundedRect(body,radius,radius);
    QImage mask(out.size(),QImage::Format_ARGB32_Premultiplied);
    mask.setDevicePixelRatio(out.devicePixelRatio());mask.fill(Qt::transparent);
    {QPainter p(&mask);p.setRenderHint(QPainter::Antialiasing);p.fillPath(path,Qt::white);}
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);p.drawImage(QPointF(),mask);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255,255,255,80),1.));p.drawPath(path);
    return out;
}
}
