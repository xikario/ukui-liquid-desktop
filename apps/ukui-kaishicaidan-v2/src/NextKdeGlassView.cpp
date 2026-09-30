#include "../../../shared/liquid-glass/src/LiquidMaterial.h"
#include "NextKdeGlassView.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
// Body diffusion (~7 logical pixels). Keep colour and large features intact.
// The optical rim gets a separate, nearly clear texture below.
using LiquidMaterial::diffuse;

} // namespace

NextKdeGlassView::NextKdeGlassView(QObject *parent) : QObject(parent) {}
NextKdeGlassView::~NextKdeGlassView() = default;

void NextKdeGlassView::prepareGpu()
{
    if (m_optics || qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_NO_GL")
            || qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_FAST")) return;
    QElapsedTimer timer;
    timer.start();
    m_optics=std::make_unique<LiquidOpticsRenderer>();
    ++m_gpuInitializationCount;
    QImage sample(64,64,QImage::Format_RGB32);
    sample.fill(QColor(40,60,80));
    m_optics->setWallpaper(sample);
    m_optics->renderPanel(sample.rect(),16);
    if (m_optics->usedGpu()) {
        m_controlOptics.setWallpaper(sample);
        m_controlOptics.renderControl(sample.rect(),16);
        m_controlOptics.setWallpaper({});
    }
    m_optics->setWallpaper({});
    qInfo() << "[NextKdeGlass] one-shot warmup" << timer.elapsed() << "ms";
}

qreal NextKdeGlassView::luminanceAt(const QRectF &logicalRect) const
{
    if (m_image.isNull()) return 0;
    const qreal dpr=m_image.devicePixelRatio();
    const QRectF bounds(0,0,m_image.width()/dpr,m_image.height()/dpr);
    const QRectF area=logicalRect.intersected(bounds);
    if (area.isEmpty()) return 0;
    qreal sum=0;
    for(int y=0;y<3;++y) for(int x=0;x<5;++x) {
        const int px=qBound(0,int((area.left()+area.width()*(x+0.5)/5)*dpr),m_image.width()-1);
        const int py=qBound(0,int((area.top()+area.height()*(y+0.5)/3)*dpr),m_image.height()-1);
        const QColor c=m_image.pixelColor(px,py);
        sum+=(0.299*c.redF()+0.587*c.greenF()+0.114*c.blueF())*c.alphaF();
    }
    return sum/15;
}

void NextKdeGlassView::setBackdrop(const QImage &source)
{
    ++m_backdropRevision;m_requestedBackdrop={};m_backdropReady={};
    m_controls.clear();
    m_controlRenderCount=0;
    m_image={};
    m_usedGpu=false;
    m_lastRenderMs=0;
    if (source.isNull()) return;
    QElapsedTimer timer;
    timer.start();
    const qreal dpr=source.devicePixelRatio();
    const QSize logical(qMax(1,qRound(source.width()/dpr)),
                        qMax(1,qRound(source.height()/dpr)));
    if (qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_NO_GL")) {
        setBackdropFast(source);
        return;
    }
    if(!m_optics) {
        m_optics=std::make_unique<LiquidOpticsRenderer>();
        ++m_gpuInitializationCount;
    }
    // Same full-resolution optical material as the desklets. Render only on
    // explicit opening; the resident renderer retains its program and FBO.
    m_optics->setOptics(3.5,.58,.70,1.1);
    m_optics->setMaterial(0,1.15);
    m_optics->setWallpaper(source);
    m_image=m_optics->renderPanel(QRect(QPoint(),logical),m_radius);
    m_usedGpu=m_optics->usedGpu();
    if(!m_usedGpu) {
        setBackdropFast(source);
        return;
    }
    m_controlOptics.setWallpaper(m_image);
    m_image.setDevicePixelRatio(dpr);
    m_lastRenderMs=timer.elapsed();
    qInfo() << "[NextKdeGlass]" << (m_usedGpu ? "upstream Snell/glints" : "CPU fallback")
            << source.size() << "DPR" << dpr << m_lastRenderMs << "ms";
}

namespace {
QImage fastBackdrop(const QImage &source, qreal radiusValue)
{
    if(source.isNull())return {};
    QElapsedTimer timer;
    timer.start();
    const qreal dpr=source.devicePixelRatio();
    const QSize logical(qMax(1,qRound(source.width()/dpr)),
                        qMax(1,qRound(source.height()/dpr)));
    QImage material=diffuse(source,logical);
    material.setDevicePixelRatio(1);
    const QImage clear=source.convertToFormat(QImage::Format_RGB32);
    const int w=material.width(), h=material.height();
    const qreal radius=qMin(qreal(radiusValue)*dpr, qMin(w,h)*0.5);
    const qreal rim=28*dpr;
    // The body needs only diffusion. Compute refraction on the narrow rim,
    // keeping icons/text outside this image and avoiding any GL resources.
    for (int y=0; y<h; ++y) {
        QRgb *out=reinterpret_cast<QRgb *>(material.scanLine(y));
        for (int x=0; x<w; ++x) {
            const qreal px=x+0.5-w*0.5, py=y+0.5-h*0.5;
            const qreal qx=qAbs(px)-(w*0.5-radius), qy=qAbs(py)-(h*0.5-radius);
            const qreal ox=qMax(qreal(0),qx), oy=qMax(qreal(0),qy);
            const qreal len=(ox>0 && oy>0) ? std::sqrt(ox*ox+oy*oy) : qMax(ox,oy);
            const qreal inside=radius-len-qMin(qMax(qx,qy),qreal(0));
            const QRgb base=out[x];
            qreal red=qRed(base), green=qGreen(base), blue=qBlue(base);
            qreal edge=0, nx=0, ny=0;
            if (inside<rim) {
                edge=qBound(qreal(0),1-inside/rim,qreal(1));
                if (len>0) { nx=ox/len; ny=oy/len; }
                else if (qx>qy) nx=1;
                else ny=1;
                if (px<0) nx=-nx;
                if (py<0) ny=-ny;
                const qreal bend=12*dpr*edge*edge;
                const int sx=qBound(0,qRound(x-nx*bend),w-1);
                const int sy=qBound(0,qRound(y-ny*bend),h-1);
                const QRgb refracted=reinterpret_cast<const QRgb *>(clear.constScanLine(sy))[sx];
                red+=(qRed(refracted)-red)*edge;
                green+=(qGreen(refracted)-green)*edge;
                blue+=(qBlue(refracted)-blue)*edge;
            }
            const qreal lum=(0.299*red+0.587*green+0.114*blue)/255;
            const qreal t=qBound(qreal(0),(lum-0.25)/0.70,qreal(1));
            const qreal scrim=(0.16+0.27*t*t*(3-2*t))*(1-edge)+0.12*edge;
            const qreal light=qMax(qreal(0),-0.65*nx-0.76*ny)*edge*edge*22;
            out[x]=qRgb(qBound(0,qRound(red*(1-scrim)+light),255),
                        qBound(0,qRound(green*(1-scrim)+light),255),
                        qBound(0,qRound(blue*(1-scrim)+light),255));
        }
    }
    QImage image(source.size(),QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(0,0,w,h),radius,radius);
        p.setClipPath(shape);
        p.drawImage(0,0,material);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255,255,255,65),dpr));
        p.drawPath(shape);
    }
    image.setDevicePixelRatio(dpr);
    qInfo() << "[NextKdeGlass] fast CPU refraction" << source.size()
            << "DPR" << dpr << timer.elapsed() << "ms";
    return image;
}

}
void NextKdeGlassView::setBackdropFast(const QImage &source)
{
    ++m_backdropRevision;m_requestedBackdrop={};m_backdropReady={};
    m_controls.clear();m_controlRenderCount=0;m_usedGpu=false;
    QElapsedTimer timer;timer.start();
    m_image=fastBackdrop(source,m_radius);m_lastRenderMs=timer.elapsed();
}

void NextKdeGlassView::setBackdropAsync(const QImage &source, bool fast, std::function<void()> ready)
{
    ++m_backdropRevision;
    m_requestedBackdrop=source;
    m_requestedFast=fast || qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_NO_GL");
    m_backdropReady=std::move(ready);
    if(source.isNull()){m_image={};m_controls.clear();m_controlRenderCount=0;m_usedGpu=false;m_lastRenderMs=0;m_backdropReady={};return;}
    startBackdropPreparation();
}

void NextKdeGlassView::startBackdropPreparation()
{
    if(m_backdropPending || m_requestedBackdrop.isNull())return;
    m_backdropPending=true;
    const QImage source=m_requestedBackdrop;
    const bool fast=m_requestedFast;
    const qreal radius=m_radius;
    const auto revision=m_backdropRevision;
    struct Result { LiquidMaterial::Prepared material; QImage image; };
    BackgroundTask::run(this,[source,fast,radius]{
        Result result;
        if(fast)result.image=fastBackdrop(source,radius);
        else result.material=LiquidMaterial::prepare(source);
        return result;
    },[this,revision,fast,source,radius](const Result &result){
        m_backdropPending=false;
        if(revision!=m_backdropRevision){startBackdropPreparation();return;}
        QElapsedTimer timer;timer.start();
        m_usedGpu=false;
        if(fast)m_image=result.image;
        else {
            if(!m_optics){m_optics=std::make_unique<LiquidOpticsRenderer>();++m_gpuInitializationCount;}
            m_optics->setOptics(3.5,.58,.70,1.1);m_optics->setMaterial(0,1.15);
            m_optics->setPreparedWallpaper(result.material);
            const qreal dpr=source.devicePixelRatio();
            const QSize logical(qMax(1,qRound(source.width()/dpr)),qMax(1,qRound(source.height()/dpr)));
            m_image=m_optics->renderPanel(QRect(QPoint(),logical),radius);
            m_usedGpu=m_optics->usedGpu();
            if(!m_usedGpu){m_requestedFast=true;startBackdropPreparation();return;}
        }
        m_controls.clear();m_controlRenderCount=0;
        m_controlOptics.setWallpaper(m_image);
        m_lastRenderMs=timer.elapsed();
        m_requestedBackdrop={};
        auto ready=std::move(m_backdropReady);
        if(ready)ready();
    });
}

QImage NextKdeGlassView::controlImage(const QRectF &rect, qreal radius, bool pressed)
{
    if(m_image.isNull() || rect.width()<2 || rect.height()<2) return {};
    const qreal dpr=m_image.devicePixelRatio();
    const QRect pixels(qRound(rect.x()*dpr),qRound(rect.y()*dpr),
                       qRound(rect.width()*dpr),qRound(rect.height()*dpr));
    // Never stretch an intersected crop at a clipped/scrolling boundary.
    if(!m_image.rect().contains(pixels)) return {};
    const QString key=QString("%1,%2,%3,%4/%5/%6")
        .arg(pixels.x()).arg(pixels.y()).arg(pixels.width()).arg(pixels.height())
        .arg(radius).arg(pressed);
    if(auto *cached=m_controls.object(key)) return *cached;
    QImage crop=m_image.copy(pixels);
    crop.setDevicePixelRatio(1);
    QImage result;
    if(m_usedGpu && !qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_NO_GL"))
        result=m_controlOptics.renderControl(rect.toRect(),radius,pressed);
    if(result.isNull()) {
        result=QImage(pixels.size(),QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter p(&result);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(QPointF(),QSizeF(pixels.size())),radius*dpr,radius*dpr);
        p.setClipPath(shape);
        p.drawImage(0,0,crop);
        p.fillPath(shape,pressed ? QColor(0,0,0,20) : QColor(255,255,255,20));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255,255,255,95),dpr));
        p.drawPath(shape);
    }
    result.setDevicePixelRatio(dpr);
    ++m_controlRenderCount;
    m_controls.insert(key,new QImage(result),qMax(1,int(result.sizeInBytes()/1024)));
    return result;
}
