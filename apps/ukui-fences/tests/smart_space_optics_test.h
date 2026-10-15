#pragma once
#include "SmartSpaceWidget.h"
#include "VideoWallpaperRegion.h"
#include <QLineEdit>
#include <QElapsedTimer>

class SmartSpaceOpticsTestAccess {
public:
    static int run(const QString &root) {
        QSettings settings;
        settings.setValue("smartSpace/themeMode",3);
        settings.setValue("smartSpace/glassOptics",false);
        settings.setValue("smartSpace/defaultHidden",false);
        settings.setValue("smartSpace/indexMode",0);settings.sync();
        QWidget host;host.resize(1200,760);host.show();
        SmartSpaceWidget widget(false,&host);widget.resize(920,520);widget.move(50,40);widget.show();settle(100);
        const qreal dpr=widget.devicePixelRatioF();
        const QImage source=wallpaper(widget.size(),dpr);
        auto setBackdrop=[&](QImage image) {
            widget.m_glassBackdrop=image.scaled(qMax(1,image.width()/8),qMax(1,image.height()/8),
                Qt::IgnoreAspectRatio,Qt::SmoothTransformation).scaled(image.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            widget.m_glassSharpBackdrop=image.scaled(qMax(1,image.width()/2),qMax(1,image.height()/2),
                Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
            widget.m_glassMaterial={};
        };
        widget.m_glassBackdropRefreshTimer.stop();setBackdrop(source);
        QElapsedTimer timer;timer.start();const QImage before=widget.glassMaterial();const auto oldMs=timer.elapsed();
        const QString artifacts=qEnvironmentVariable("UKUI_FENCES_OPTICS_ARTIFACTS",root+"/optics");QDir().mkpath(artifacts);
        widget.grab().save(artifacts+"/before.png");
        widget.setGlassOpticsEnabled(true);widget.m_glassBackdropRefreshTimer.stop();setBackdrop(source);
        timer.restart();const QImage after=widget.glassMaterial();const auto newMs=timer.elapsed();
        widget.grab().save(artifacts+"/after.png");before.save(artifacts+"/material-before.png");after.save(artifacts+"/material-after.png");
        check(after!=before,"preview actually changes the glass material");
        bool equalAlpha=true,premultiplied=true;int changed=0;
        for(int y=0;y<after.height();++y){
            const auto *a=reinterpret_cast<const QRgb *>(before.constScanLine(y));
            const auto *b=reinterpret_cast<const QRgb *>(after.constScanLine(y));
            for(int x=0;x<after.width();++x){
                equalAlpha&=qAlpha(a[x])==qAlpha(b[x]);
                premultiplied&=qRed(b[x])<=qAlpha(b[x]) && qGreen(b[x])<=qAlpha(b[x]) && qBlue(b[x])<=qAlpha(b[x]);
                changed+=a[x]!=b[x];
            }
        }
        check(equalAlpha,"optics preserves every painted alpha pixel, including fractional-DPR corners");
        check(premultiplied,"output channels remain valid premultiplied ARGB");
        check(after.pixel(after.width()/2,after.height()/2)==before.pixel(before.width()/2,before.height()/2),
              "central reading area retains the original glass color");
        check(changed>100 && changed<after.width()*after.height()/3,"refraction stays in the edge band");
        const QRegion exact=VideoWallpaperRegion::coverage(after,dpr);
        check(widget.m_glassCoverage==exact && widget.property(VideoWallpaperRegion::propertyName).value<QRegion>()==exact,
              "video wallpaper reserves the exact rendered edge, without a provisional halo");
        const auto builds=widget.property("smartGlassMaterialBuilds").toInt();
        const auto geometryBuilds=widget.m_glassOptics.geometryBuilds();
        const auto bytes=widget.m_glassOptics.bytes();
        check(bytes>0 && bytes<qsizetype(after.width())*after.height()*4,
              "sparse edge LUT uses less memory than one full-size 32-bit image");
        for(int i=0;i<12;++i){widget.update(QRect(100,70,40,30));widget.glassMaterial();widget.updateRoundedMask();}
        settle(80);
        check(widget.property("smartGlassMaterialBuilds").toInt()==builds && widget.m_glassOptics.geometryBuilds()==geometryBuilds,
              "stationary partial paints reuse the material and optical geometry");
        QImage changedSource=source.mirrored(true,false);setBackdrop(changedSource);
        const QImage changedWallpaper=widget.glassMaterial();
        check(changedWallpaper!=after && widget.m_glassOptics.geometryBuilds()==geometryBuilds,
              "wallpaper changes rebuild the material while reusing geometry");
        settings.sync();check(settings.value("smartSpace/glassOptics").toBool(),"preview selection persists");
        {
            SmartSpaceWidget restored(false,&host);
            check(restored.glassOpticsEnabled(),"new widget restores the selected glass style");
        }
        widget.setGlassOpticsEnabled(false);widget.m_glassBackdropRefreshTimer.stop();setBackdrop(source);
        check(widget.glassMaterial()==before && widget.m_glassOptics.bytes()==0,
              "original style restores exactly and releases the optical LUT");
        SmartSpaceOptics independent;QImage sample=before;
        independent.apply(sample,source);const int one=independent.geometryBuilds();
        sample.setDevicePixelRatio(dpr+.25);independent.apply(sample,source);
        check(independent.geometryBuilds()==one+1,"DPR is part of the optical geometry cache key");
        qInfo()<<"OPTICS"<<"dpr"<<dpr<<"old material ms"<<oldMs<<"preview cold ms"<<newMs
               <<"LUT bytes"<<bytes<<"changed pixels"<<changed;
        return failures?1:0;
    }
};
