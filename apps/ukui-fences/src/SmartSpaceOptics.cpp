#include "SmartSpaceOptics.h"
#include <QtMath>
#include <cmath>

void SmartSpaceOptics::clear()
{
    QVector<Sample>().swap(m_samples);m_size={};m_dpr=0;
}

void SmartSpaceOptics::prepare(QSize size,qreal dpr)
{
    if(size==m_size && qFuzzyCompare(dpr,m_dpr))return;
    m_samples.clear();m_size=size;m_dpr=dpr;++m_builds;
    if(size.isEmpty())return;
    const float inset=5*dpr,radius=19*dpr,band=22*dpr;
    const float hw=size.width()*.5f-inset,hh=size.height()*.5f-inset;
    const float bx=qMax(0.f,hw-radius),by=qMax(0.f,hh-radius);
    // Allocate for a perimeter, never width*height. No full-size mask/LUT.
    const int reserve=qMin(qint64(size.width())*size.height(),
        qint64(qCeil(2*(size.width()+size.height())*(band+2*dpr))));
    m_samples.reserve(reserve);
    constexpr float eta=1/1.52f,pi=3.14159265358979323846f;
    for(int y=0;y<size.height();++y) {
        const float py=y+.5f-size.height()*.5f;
        for(int x=0;x<size.width();++x) {
            const float px=x+.5f-size.width()*.5f;
            if(std::abs(px)<hw-radius-band && std::abs(py)<hh-radius-band)continue;
            const float qx=std::abs(px)-bx,qy=std::abs(py)-by;
            const float mx=qMax(qx,0.f),my=qMax(qy,0.f);
            const float outside=std::sqrt(mx*mx+my*my);
            const float depth=radius-outside-qMin(qMax(qx,qy),0.f);
            if(depth<-.75f || depth>band)continue;
            float gx=0,gy=0;
            if(outside>1e-5f){gx=mx/outside*(px<0?-1:1);gy=my/outside*(py<0?-1:1);}
            else if(qx>qy)gx=px<0?-1:1;
            else gy=py<0?-1:1;
            const float t=qBound(0.f,depth/band,1.f),u=1-t;
            const float nz=std::sqrt(qMax(.03f,1-u*u));
            const float nx=gx*u,ny=gy*u;
            const float factor=eta*nz-std::sqrt(qMax(0.f,1-eta*eta*(1-nz*nz)));
            const float edge=u*u*(3-2*u);
            const float rimFade=qBound(0.f,(depth-float(dpr))/(2.5f*float(dpr)),1.f);
            const float lightDot=qMax(0.f,-.65f*nx-.65f*ny+.39f*nz);
            const float caustic=qMax(0.f,.65f*nx+.65f*ny);
            const int light=qRound(90*std::pow(lightDot,5)*edge
                +30*std::pow(caustic,3)*std::sin(t*pi));
            m_samples.append({y*size.width()+x,
                qint16(qRound(nx*factor*20*dpr*16)),qint16(qRound(ny*factor*20*dpr*16)),
                // Dispersion is in eighths of a device pixel. Subpixel taps
                // retain a quiet color fringe at every scale.
                qint8(qRound(gx*.65f*dpr*u*u*8)),qint8(qRound(gy*.65f*dpr*u*u*8)),
                quint8(qBound(0,qRound(edge*165*rimFade),165)),quint8(qBound(0,light,100))});
        }
    }
}

void SmartSpaceOptics::apply(QImage &material,const QImage &source)
{
    if(material.isNull() || source.isNull())return;
    prepare(material.size(),material.devicePixelRatioF());
    // Sharp input is local to this widget's sampled wallpaper rectangle;
    // physical ratios handle odd sizes, fractional DPR and negative screens.
    const QImage sharp=source.format()==QImage::Format_RGB32?source:source.convertToFormat(QImage::Format_RGB32);
    const int w=material.width(),h=material.height(),sw=sharp.width(),sh=sharp.height();
    auto sample=[&](qint64 x16,qint64 y16){
        const int sx16=int(qBound(qint64(0),x16*sw/w,qint64(sw-1)*16));
        const int sy16=int(qBound(qint64(0),y16*sh/h,qint64(sh-1)*16));
        const int sx=sx16/16,sy=sy16/16,tx=sx16%16,ty=sy16%16;
        const auto *top=reinterpret_cast<const QRgb *>(sharp.constScanLine(sy));
        const auto *bottom=reinterpret_cast<const QRgb *>(sharp.constScanLine(qMin(sy+1,sh-1)));
        const QRgb a=top[sx],b=top[qMin(sx+1,sw-1)],c=bottom[sx],d=bottom[qMin(sx+1,sw-1)];
        auto mix=[&](int av,int bv,int cv,int dv){return
            (av*(16-tx)*(16-ty)+bv*tx*(16-ty)+cv*(16-tx)*ty+dv*tx*ty+128)/256;};
        return qRgb(mix(qRed(a),qRed(b),qRed(c),qRed(d)),mix(qGreen(a),qGreen(b),qGreen(c),qGreen(d)),
                    mix(qBlue(a),qBlue(b),qBlue(c),qBlue(d)));
    };
    QRgb *row=nullptr;int rowY=-1;
    for(const Sample &s:m_samples) {
        const int y=s.pixel/w,x=s.pixel-y*w;
        if(y!=rowY){row=reinterpret_cast<QRgb *>(material.scanLine(y));rowY=y;}
        const QRgb old=row[x];const int a=qAlpha(old);
        if(!a)continue;
        const qint64 rx=qint64(x)*16+s.dx16,ry=qint64(y)*16+s.dy16;
        const QRgb red=sample(rx+s.dispersionX*2,ry+s.dispersionY*2);
        const QRgb green=sample(rx,ry);
        const QRgb blue=sample(rx-s.dispersionX*2,ry-s.dispersionY*2);
        auto channel=[&](int base,int clear,int tint){
            base=base*255/a;
            // A neutral veil keeps a readable dark rim under bright images.
            clear=(clear*180+tint*75)/255;
            const int result=qBound(0,(base*(255-s.mix)+clear*s.mix)/255+s.light,255);
            return (result*a+127)/255;
        };
        row[x]=qRgba(channel(qRed(old),qRed(red),8),channel(qGreen(old),qGreen(green),13),
                     channel(qBlue(old),qBlue(blue),21),a);
    }
}
