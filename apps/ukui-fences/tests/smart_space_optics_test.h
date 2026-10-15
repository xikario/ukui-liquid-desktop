#pragma once
#include "SmartSpaceWidget.h"
#include "VideoWallpaperRegion.h"
#include "Palette.h"
#include "GlassFinish.h"
#include <QLineEdit>
#include <QElapsedTimer>

class SmartSpaceOpticsTestAccess {
public:
    static double roughness(const QImage &image, qreal dpr) {
        double total=0;int count=0;const int step=qMax(1,qRound(3*dpr));
        for(int y=image.height()/4;y<image.height()*3/4;y+=step) {
            const auto *row=reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for(int x=image.width()/4;x<image.width()*3/4-step;x+=step) {
                total+=qAbs(qRed(row[x])-qRed(row[x+step]))+qAbs(qGreen(row[x])-qGreen(row[x+step]))
                    +qAbs(qBlue(row[x])-qBlue(row[x+step]));++count;
            }
        }
        return total/qMax(1,count);
    }
    static QVector<QColor> readingColors(const QImage &image, qreal dpr) {
        QVector<QColor> colors;
        const int inset=qCeil(40*dpr),step=qMax(1,qRound(9*dpr));
        for(int y=inset;y<image.height()-inset;y+=step) {
            const auto *row=reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for(int x=inset;x<image.width()-inset;x+=step)colors.append(QColor::fromRgb(row[x]));
        }
        return colors;
    }
    static int run(const QString &root) {
        QSettings settings;
        settings.setValue("smartSpace/themeMode",3);
        settings.setValue("smartSpace/glassOptics",false);
        settings.setValue("smartSpace/defaultHidden",false);
        settings.setValue("smartSpace/indexMode",0);settings.sync();
        QWidget host;host.resize(1200,760);host.show();
        SmartSpaceWidget widget(false,&host);widget.resize(920,520);widget.move(50,40);widget.show();settle(100);
        // Render real result rows using fictional in-memory entries only.
        const QString fixtureRoot=root+QStringLiteral("/示例资料");
        widget.m_roots=QStringList{fixtureRoot};
        const QStringList names={QStringLiteral("项目方案示例.docx"),QStringLiteral("季度汇报示例.pptx"),
            QStringLiteral("资料清单示例.xlsx"),QStringLiteral("工作纪要示例.pdf")};
        for(int i=0;i<names.size();++i) {
            SmartSpaceEntry entry;entry.root=fixtureRoot;entry.parentPath=fixtureRoot+QStringLiteral("/办公文档");
            entry.name=names[i];entry.path=entry.parentPath+QLatin1Char('/')+entry.name;
            entry.suffix=QFileInfo(entry.name).suffix();entry.category=QStringLiteral("文档");
            entry.content=QStringLiteral("这里是合成的预览文字，用于检查白色标题、辅助信息与液态底色的可读性。");
            entry.size=4096*(i+1);entry.modified=QDateTime(QDate(2026,10,15),QTime(9,30));
            widget.m_entries.append(entry);
        }
        widget.updateResults();widget.m_statusLabel->setText(QStringLiteral("样式预览 · 合成测试内容"));settle(60);
        const qreal dpr=widget.devicePixelRatioF();
        const QImage source=wallpaper(widget.size(),dpr);
        auto setBackdrop=[&](const QImage &image) { widget.cacheGlassBackdrop(image); };
        widget.m_glassBackdropRefreshTimer.stop();
        QElapsedTimer timer;timer.start();setBackdrop(source);
        const QImage before=widget.glassMaterial();const auto oldMs=timer.elapsed();
        const double oldRoughness=roughness(widget.m_glassBackdrop,dpr);
        const QString artifacts=qEnvironmentVariable("UKUI_FENCES_OPTICS_ARTIFACTS",root+"/optics");QDir().mkpath(artifacts);
        widget.grab().save(artifacts+"/before.png");
        widget.setGlassOpticsEnabled(true);widget.m_glassBackdropRefreshTimer.stop();
        timer.restart();setBackdrop(source);const QImage after=widget.glassMaterial();const auto newMs=timer.elapsed();
        check(widget.layout()->contentsMargins()==QMargins(16,16,16,16),"preview content clears the thicker optical edge");
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
        const double newRoughness=roughness(widget.m_glassBackdrop,dpr);
        check(newRoughness<oldRoughness*.25,"deep frost removes background lines in the central reading area");
        check(changed>after.width()*after.height()/2,"the new frost visibly changes the whole surface");
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
        const auto savedScrim=GlassFinish::scrim();
        for(const auto scrim:{GlassFinish::Scrim::Balanced,GlassFinish::Scrim::Transparent}) {
            GlassFinish::setScrim(scrim);
            for(int scene=0;scene<3;++scene) {
                QImage sceneSource(source.size(),QImage::Format_RGB32);sceneSource.setDevicePixelRatio(dpr);
                if(scene==0)sceneSource.fill(Qt::white);
                else if(scene==1) {
                    QPainter painter(&sceneSource);
                    QLinearGradient gradient(0,0,widget.width(),widget.height());
                    gradient.setColorAt(0,Qt::white);gradient.setColorAt(.5,QColor(255,240,160));
                    gradient.setColorAt(1,QColor(220,245,255));painter.fillRect(widget.rect(),gradient);
                } else sceneSource.fill(qRgb(18,45,33));
                setBackdrop(sceneSource);const QImage readable=widget.glassMaterial();
                const auto colors=readingColors(readable,dpr);
                const qreal primary=Palette::minContrast(widget.m_textColor,colors);
                const qreal secondary=Palette::minContrast(widget.m_mutedColor,colors);
                check(primary>=Palette::PrimaryTextContrast && secondary>=Palette::SecondaryTextContrast,
                      "bright/mixed/dark wallpapers retain readable primary and secondary text");
                qInfo()<<"OPTICS_READING"<<"dpr"<<dpr<<"scrim"<<int(scrim)<<"scene"<<scene
                       <<"primary"<<primary<<"secondary"<<secondary
                       <<"extra shade"<<widget.property("smartGlassReadingShade").toDouble();
                if(scene==0) {
                    check(widget.property("smartGlassReadingShade").toDouble()>0,"white wallpaper receives adaptive reading shade");
                    if(scrim==GlassFinish::Scrim::Balanced)widget.grab().save(artifacts+"/white-wallpaper.png");
                } else if(scene==2) {
                    check(widget.property("smartGlassReadingShade").toDouble()==0,"dark wallpaper avoids unnecessary extra shade");
                }
            }
        }
        GlassFinish::setScrim(savedScrim);
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
        check(widget.layout()->contentsMargins()==QMargins(12,12,12,12),"original content margins restore");
        check(widget.property("smartGlassReadingShade").toDouble()==0,"original style has no preview reading shade");
        QImage uniform(QSize(63,37),QImage::Format_RGB32);uniform.fill(qRgb(42,112,231));
        QImage frost,edge;SmartSpaceOptics::prepareBackdrop(uniform,dpr,frost,edge);
        bool constant=true;
        for(int y=0;y<frost.height();++y)for(int x=0;x<frost.width();++x)
            constant&=frost.pixel(x,y)==qRgb(42,112,231);
        check(constant,"signed blur arithmetic preserves flat colors without channel wrapping");
        QImage tiny(1,1,QImage::Format_RGB32);tiny.fill(qRgb(200,20,180));
        SmartSpaceOptics::prepareBackdrop(tiny,dpr,frost,edge);
        check(frost.pixel(0,0)==tiny.pixel(0,0),"clamped blur handles a one-pixel input");
        // Isolate the lens from the new frost: the lens still only changes the
        // border, and the signed lighting contains a dark trough then a cushion.
        QImage neutral(before.size(),QImage::Format_ARGB32_Premultiplied);
        neutral.setDevicePixelRatio(dpr);neutral.fill(qRgb(96,96,96));
        const QImage neutralSource=neutral;
        SmartSpaceOptics contour;contour.apply(neutral,neutralSource);
        check(neutral.pixel(neutral.width()/2,neutral.height()/2)==qRgb(96,96,96),"lens leaves the central plateau intact");
        check(qRed(neutral.pixel(qRound(22*dpr),neutral.height()/2))<80
              && qRed(neutral.pixel(qRound(12*dpr),neutral.height()/2))>105,
              "signed edge lighting creates the inner trough and raised cushion");
        SmartSpaceOptics independent;QImage sample=before;
        independent.apply(sample,source);const int one=independent.geometryBuilds();
        sample.setDevicePixelRatio(dpr+.25);independent.apply(sample,source);
        check(independent.geometryBuilds()==one+1,"DPR is part of the optical geometry cache key");
        qInfo()<<"OPTICS"<<"dpr"<<dpr<<"old backdrop+material ms"<<oldMs<<"preview backdrop+material ms"<<newMs
               <<"LUT bytes"<<bytes<<"changed pixels"<<changed<<"roughness"<<oldRoughness<<newRoughness;
        return failures?1:0;
    }
};
