#pragma once
#include "SmartSpaceWidget.h"
#include "FenceGlassRenderer.h"
#include "GlassFinish.h"
#include "VideoWallpaperRegion.h"
#include <QLineEdit>
#include <QScrollBar>
#include <functional>

class SmartSpaceOpticsTestAccess {
    static QImage finish(const QImage &panel, QSize size) {
        const QImage finished=GlassFinish::withEdge(GlassFinish::withGrain(GlassFinish::withScrim(panel,GlassFinish::scrim())),
                                      GlassFinish::squirclePath(QRectF(QPointF(),QSizeF(size-QSize(4,4))),10));
        QImage image(QSize(qRound(size.width()*panel.devicePixelRatioF()),qRound(size.height()*panel.devicePixelRatioF())),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(panel.devicePixelRatioF());image.fill(Qt::transparent);
        QPainter painter(&image);painter.drawImage(QPointF(2,2),finished);return image;
    }
    static void entries(SmartSpaceWidget &widget,const QString &root) {
        widget.m_roots=QStringList{root};widget.m_entries.clear();
        for(const QString &name:{QStringLiteral("项目方案示例.docx"),QStringLiteral("季度汇报示例.pptx"),
                QStringLiteral("资料清单示例.xlsx"),QStringLiteral("工作纪要示例.pdf")}) {
            SmartSpaceEntry entry;entry.root=root;entry.parentPath=root+QStringLiteral("/办公文档");
            entry.name=name;entry.path=entry.parentPath+QLatin1Char('/')+name;
            entry.suffix=QFileInfo(name).suffix();entry.category=QStringLiteral("文档");entry.size=4096;
            entry.content=QStringLiteral("合成测试内容：检查分区同款玻璃、文字和边缘，不读取用户文档。");
            widget.m_entries.append(entry);
        }
        widget.updateResults();widget.m_statusLabel->setText(QStringLiteral("分区同款玻璃 · 合成测试内容"));
    }
    static bool waitFor(const std::function<bool()> &ready,int timeout=6000) {
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<timeout) { if(ready())return true;settle(40); }
        return ready();
    }
public:
    static int run(const QString &root) {
        qputenv("GSETTINGS_BACKEND","keyfile");QSettings settings;
        for(const char *key:{"smartSpace/autoStart","systemMonitor/autoStart","desklets/clock/autoStart",
                "desklets/activity/autoStart","desklets/music/autoStart","desklets/calendar/autoStart"})settings.setValue(key,false);
        settings.setValue("smartSpace/themeMode",3);settings.setValue("smartSpace/glassOptics",false);
        settings.setValue("smartSpace/defaultHidden",false);settings.setValue("smartSpace/indexMode",0);
        settings.setValue("smartSpace/alwaysOnTop",false);settings.sync();
        const bool quiet=qEnvironmentVariableIsSet("UKUI_FENCES_TEST_NO_WINDOWS");
        const bool requireGl=qEnvironmentVariableIsSet("UKUI_FENCES_SMART_REQUIRE_GL");
        const QString artifacts=qEnvironmentVariable("UKUI_FENCES_OPTICS_ARTIFACTS",root+"/shared-glass");QDir().mkpath(artifacts);
        QWidget host;host.setAttribute(Qt::WA_DontShowOnScreen,quiet);host.resize(1200,760);host.show();
        SmartSpaceWidget widget(false,&host);widget.resize(920,520);widget.move(50,40);widget.show();settle(100);
        entries(widget,root+QStringLiteral("/示例资料"));settle(60);
        const qreal dpr=widget.devicePixelRatioF();const QImage source=wallpaper(widget.size(),dpr);
        widget.m_glassBackdropRefreshTimer.stop();widget.cacheGlassBackdrop(source);
        const QImage before=widget.glassMaterial();widget.grab().save(artifacts+"/before.png");
        widget.setGlassOpticsEnabled(true);widget.m_glassBackdropRefreshTimer.stop();widget.cacheGlassBackdrop(source);
        QElapsedTimer timer;timer.start();const QImage after=widget.glassMaterial();const auto localMs=timer.elapsed();
        FenceGlassRenderer reference;reference.setWallpaper(source);
        const QImage expected=finish(reference.renderPanel(QRect(QPoint(2,2),widget.size()-QSize(4,4)),10),widget.size());
        if(requireGl && !reference.usedGpu()) { qWarning()<<"SKIP: no usable OpenGL context";return 77; }
        check(after==expected,"standalone Smart Space exactly matches the fence renderer and finish pipeline");
        check(after!=before,"shared fence material replaces the custom dark frame");
        check(widget.m_standaloneGlassRenderer!=nullptr,"standalone host uses the fence renderer fallback");
        bool premultiplied=true;
        for(int y=0;y<after.height();++y)for(int x=0;x<after.width();++x) {
            const QRgb p=after.pixel(x,y);premultiplied&=qRed(p)<=qAlpha(p) && qGreen(p)<=qAlpha(p) && qBlue(p)<=qAlpha(p);
        }
        check(premultiplied && qAlpha(after.pixel(0,0))==0,"shared glass preserves premultiplied transparent corners");
        check(widget.m_glassCoverage==VideoWallpaperRegion::coverage(after,dpr),"video cutout follows the rendered fence material exactly");
        widget.grab().save(artifacts+"/after.png");after.save(artifacts+"/material-after.png");
        widget.setGlassOpticsEnabled(false);widget.m_glassBackdropRefreshTimer.stop();widget.cacheGlassBackdrop(source);
        check(widget.glassMaterial()==before && !widget.m_standaloneGlassRenderer,"original style restores exactly and releases standalone renderer");
        const QString wallpaperPath=root+"/shared-wallpaper.png";wallpaper(QSize(1600,1000),dpr).save(wallpaperPath);
        QDir().mkpath(root+"/config/kyfences");QFile layout(root+"/config/kyfences/layout.json");
        layout.open(QIODevice::WriteOnly);layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",wallpaperPath},
            {"wallpaperMode",2},{"fences",QJsonArray{}}}).toJson());layout.close();
        settings.setValue("smartSpace/glassOptics",true);settings.sync();
        DesktopCanvas canvas;canvas.setAttribute(Qt::WA_DontShowOnScreen,quiet);canvas.show();
        int deliveries=0;QObject::connect(&canvas,&DesktopCanvas::liquidGlassReady,&canvas,[&]{++deliveries;});
        canvas.setSmartSpaceVisible(true);canvas.moveSmartSpace(80,60);canvas.resizeSmartSpace(920,520);
        auto *smart=canvas.findChild<SmartSpaceWidget *>();check(smart!=nullptr,"production canvas creates Smart Space");if(!smart)return 1;
        const bool ready=waitFor([&]{return !smart->glassMaterial().isNull() && canvas.m_fenceGlassRenderer
            && canvas.m_fenceGlassWallpaperKey==canvas.m_wallpaperCache.cacheKey() && !canvas.m_wallpaperLoading;});
        check(ready,"asynchronous shared wallpaper preparation reaches the material");if(!ready)return 1;
        settle(180);entries(*smart,root+QStringLiteral("/示例资料"));settle(60);
        auto referenceFor=[&] { return finish(canvas.renderLiquidGlass(
            QRect(canvas.mapFromGlobal(smart->mapToGlobal(QPoint(2,2))),smart->size()-QSize(4,4)),10),smart->size()); };
        check(smart->glassMaterial()==referenceFor(),"production Smart Space uses the exact same canvas renderer as fences");
        check(!smart->m_standaloneGlassRenderer && smart->m_glassBackdrop.isNull(),"production retains no second renderer, raw crop or optical LUT");
        check(smart->property("smartGlassMaterialBackend").toString()=="fence-shared","shared renderer is the actual production path");
        const int preparations=canvas.m_fenceGlassRenderer->preparationCount();
        for(int i=0;i<24;++i) {
            SmartSpaceEntry entry=smart->m_entries.at(i%4);
            entry.name=QStringLiteral("滚动测试资料%1.docx").arg(i+1);
            entry.path=entry.parentPath+QLatin1Char('/')+entry.name;smart->m_entries.append(entry);
        }
        smart->updateResults(false);settle(60);
        const int builds=smart->property("smartGlassMaterialBuilds").toInt();
        for(int i=0;i<12;++i) { smart->update(QRect(100,100,30,30));smart->glassMaterial(); }
        smart->m_searchEdit->setText(QStringLiteral("示例"));
        auto *scroll=smart->m_fileList->verticalScrollBar();scroll->setValue(scroll->maximum());settle(100);
        check(scroll->maximum()>0 && scroll->value()>0,"list fixture performs a real scroll before checking cache reuse");
        check(smart->property("smartGlassMaterialBuilds").toInt()==builds,"partial paints, typing and list interaction reuse the material");
        entries(*smart,root+QStringLiteral("/示例资料"));settle(40);
        for(int i=0;i<8;++i) { smart->move(90+i*3,70+i*2);smart->resize(930+i,525+i);smart->glassMaterial(); }
        check(smart->property("smartGlassMaterialBuilds").toInt()==builds,"geometry bursts stretch the previous cache until movement settles");
        settle(180);smart->glassMaterial();
        check(smart->property("smartGlassMaterialBuilds").toInt()==builds+1 && smart->glassMaterial()==referenceFor(),
              "movement and resize rebuild once at the final wallpaper coordinates");
        check(canvas.m_fenceGlassRenderer->preparationCount()==preparations,"geometry changes reuse canvas wallpaper diffusion");
        canvas.setSmartSpaceAlwaysOnTop(true);settle(180);smart->glassMaterial();
        check(smart->glassMaterial()==referenceFor() && !smart->m_standaloneGlassRenderer,"pinned window retains sharing and correct canvas coordinates");
        canvas.setSmartSpaceAlwaysOnTop(false);settle(180);
        const QImage prior=smart->glassMaterial();const int beforeReady=deliveries;
        canvas.m_wallpaper=QPixmap::fromImage(wallpaper(QSize(1600,1000),dpr).mirrored(true,false));
        canvas.clearWallpaperCache();canvas.rebuildWallpaperCache();
        check(waitFor([&]{return !smart->glassMaterial().isNull() && deliveries>beforeReady;}),"shared readiness wakes Smart Space without a frozen substitute");
        check(smart->glassMaterial()!=prior && smart->glassMaterial()==referenceFor(),"wallpaper changes replace shared material without stale colors");
        smart->grab().save(artifacts+"/shared-canvas.png");
        check(smart->m_glassCoverage==VideoWallpaperRegion::coverage(smart->glassMaterial(),smart->devicePixelRatioF()),"shared material publishes exact video coverage");
        qInfo()<<"SHARED_GLASS"<<"dpr"<<dpr<<"GPU"<<reference.usedGpu()<<"local cold ms"<<localMs
               <<"shared preparations"<<preparations<<"ready notifications"<<deliveries;
        return failures?1:0;
    }
};
