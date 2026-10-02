#include "PointerEffect.h"
#include <QPaintEvent>
#include <QScreen>

class PointerPaintProbe : public QObject {
public:
    qint64 pixels=0;
    int paints=0;
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Paint) {
            ++paints;
            for(const QRect &r:static_cast<QPaintEvent *>(event)->region())pixels+=qint64(r.width())*r.height();
        }
        return false;
    }
    void reset(){pixels=0;paints=0;}
};
struct PointerEffectTestAccess {
    static bool settled(QWidget *widget) {
        // PointerEffect deliberately has no metaobject; locate its timer through QObject.
        auto *object=widget->findChild<QObject *>("pointerEffect");
        const auto timers=object?object->findChildren<QTimer *>():QList<QTimer *>();
        return timers.size()==1 && !timers.first()->isActive();
    }
    static void movePointer(QWidget *widget,QPoint position,QWidget *receiver=nullptr) {
        const QPoint global=widget->mapToGlobal(position);
        QCursor::setPos(global);
        if(!receiver)receiver=widget;
        QMouseEvent move(QEvent::MouseMove,receiver->mapFromGlobal(global),global,
                         Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(receiver,&move);
    }
    static QImage backing(QWidget *widget,DesktopCanvas &canvas) {
        const QImage desktop=QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage();
        const qreal dpr=canvas.devicePixelRatioF();
        const QPoint top=widget->mapTo(&canvas,QPoint());
        return desktop.copy(QRect(qRound(top.x()*dpr),qRound(top.y()*dpr),
                                  qRound(widget->width()*dpr),qRound(widget->height()*dpr)));
    }
    static void compare(QWidget *widget,DesktopCanvas &canvas,const QString &name) {
        settle(45);
        const QImage partial=backing(widget,canvas);
        widget->update();settle(45);
        const QImage full=backing(widget,canvas);
        int maxDelta=0;QRect difference;
        if(partial.size()==full.size())for(int y=0;y<full.height();++y)for(int x=0;x<full.width();++x) {
            const QRgb a=partial.pixel(x,y),b=full.pixel(x,y);
            const int delta=qMax(qAbs(qRed(a)-qRed(b)),qMax(qAbs(qGreen(a)-qGreen(b)),qAbs(qBlue(a)-qBlue(b))));
            maxDelta=qMax(maxDelta,delta);
            if(delta>2)difference|=QRect(x,y,1,1);
        }
        qInfo()<<name<<"partial/full max difference"<<maxDelta<<difference;
        if(maxDelta>2) {
            QDir().mkpath("artifacts");partial.save("artifacts/"+name+"-partial.png");full.save("artifacts/"+name+"-full.png");
        }
        check(!partial.isNull() && partial.size()==full.size() && maxDelta<=2,
              "partial backing equals full redraw without lost highlights or trails (AA tolerance 2/255)");
    }
    static int run(const QString &root) {
        QSettings settings;
        for(const char *key:{"smartSpace/autoStart","systemMonitor/autoStart","desklets/clock/autoStart",
                            "desklets/activity/autoStart","desklets/music/autoStart","desklets/calendar/autoStart"})settings.setValue(key,false);
        settings.setValue("smartSpace/themeMode",3);settings.setValue("smartSpace/defaultHidden",false);
        settings.setValue("smartSpace/indexMode",0);settings.setValue("smartSpace/alwaysOnTop",false);settings.sync();
        const QString path=root+"/pointer-wall.png";wallpaper(QSize(1440,900),1).save(path);
        QDir().mkpath(root+"/config/kyfences");QFile layout(root+"/config/kyfences/layout.json");layout.open(QIODevice::WriteOnly);
        layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",path},{"wallpaperMode",2},{"fences",QJsonArray{}}}).toJson());layout.close();
        DesktopCanvas canvas;canvas.showAndActivate();settle(600);
        canvas.setClockWidgetVisible(false);canvas.setActivityWidgetVisible(false);canvas.setMusicWidgetVisible(false);canvas.setCalendarWidgetVisible(false);
        canvas.showSystemMonitorWidget();auto *monitor=canvas.findChild<SystemMonitor *>();
        check(monitor!=nullptr,"production monitor available");if(!monitor)return 1;
        monitor->setSkin(SystemMonitor::Skin::Liquid);monitor->setCompact(true);monitor->move(80,80);
        settle(6000);monitor->m_timer.stop();
        PointerPaintProbe probe;monitor->installEventFilter(&probe);
        for(qreal scale:{1.0,0.8,1.2}) {
            monitor->m_scale=scale;monitor->resize(monitor->baseSize()*scale);settle(120);
            movePointer(monitor,QPoint(90*scale,15*scale));settle(60);
            probe.reset();
            for(int i=0;i<30;++i)movePointer(monitor,QPoint((110+i*2)*scale,15*scale));
            settle(80);
            check(probe.paints>0 && probe.paints<=3 && probe.pixels<qint64(monitor->width())*monitor->height()/2,
                  "monitor coalesces event burst and repaints rim footprints only");
            check(monitor->m_hovered && monitor->m_liquidGlassPointer==monitor->logicalPosition(
                      QPoint((110+29*2)*scale,15*scale)),
                  "monitor highlight follows latest pointer at every widget scale");
            check(settled(monitor),"monitor has no settled pointer timer");
            compare(monitor,canvas,"monitor-scale-"+QString::number(scale));
            movePointer(monitor,QPoint(monitor->width()/2,monitor->height()/2));settle(80);
            compare(monitor,canvas,"monitor-center-"+QString::number(scale));
        }
        monitor->setSkin(SystemMonitor::Skin::Dark);movePointer(monitor,QPoint(100,15));
        check(settled(monitor),"non-liquid monitor does not schedule an unused pointer effect");
        monitor->hide();settle(80);check(settled(monitor),"hidden monitor stops pointer frames");
        canvas.showSmartSpaceWidget();canvas.moveSmartSpace(80,80);canvas.resizeSmartSpace(760,450);settle(350);
        auto *smart=canvas.findChild<SmartSpaceWidget *>();check(smart!=nullptr,"production Smart Space available");if(!smart)return 1;
        smart->installEventFilter(&probe);movePointer(smart,smart->rect().center());settle(100);
        // Exclude child-control hover changes: measure an empty interior strip.
        movePointer(smart,QPoint(420,350));settle(200);probe.reset();
        for(int i=0;i<25;++i){movePointer(smart,QPoint(380+i*3,350));settle(3);}
        settle(80);check(probe.paints==0,"Smart Space interior motion does not redraw invisible edge effect");
        for(QPoint pos:{QPoint(15,160),QPoint(15,300),QPoint(20,20),QPoint(300,15),QPoint(745,210),QPoint(400,435)}) {
            movePointer(smart,pos);settle(70);
            check(smart->m_glassPointerActive && smart->m_glassPointerPosition==pos
                  && !smart->glassPointerDamage(pos).isEmpty(),"Smart Space retains active edge caustic at latest pointer");
            compare(smart,canvas,"smart-"+QString::number(pos.x())+"-"+QString::number(pos.y()));
        }
        check(settled(smart),"Smart Space pointer frames stop when still");
        smart->hide();settle(80);check(settled(smart),"hidden Smart Space stops pointer frames");smart->removeEventFilter(&probe);
        FenceWidget fence("动态边缘",QRect(100,100,600,350),&canvas);fence.setLiquidGlassEnabled(true);fence.show();settle(300);
        for(bool magnetic:{false,true}) {
            if(magnetic){fence.m_magneticEdge=FenceWidget::MagneticEdge::Left;
                fence.m_magneticContour={{12,40},{55,100},{18,160},{45,230},{12,310}};
                fence.updateShapeMask();fence.invalidateGlassCache();settle(100);}
            movePointer(&fence,QPoint(70,120),fence.m_iconViewport);settle(850);
            check(fence.m_glassHover==1 && settled(&fence),"fence reaches original hover brightness and stops its timer over child viewport");
            fence.installEventFilter(&probe);probe.reset();
            for(int i=0;i<25;++i)movePointer(&fence,QPoint(70,130+i*3),fence.m_iconViewport);
            settle(90);check(probe.paints>0 && probe.paints<=3 && probe.pixels<qint64(fence.width())*fence.height()/2,"fence merges child motion and bounds curved rim damage");
            compare(&fence,canvas,magnetic?"fence-magnetic":"fence-rounded");
            movePointer(&fence,QPoint(580,180));settle(90);compare(&fence,canvas,magnetic?"fence-magnetic-opposite":"fence-rounded-opposite");
            fence.removeEventFilter(&probe);movePointer(&fence,QPoint(-30,-30));settle(850);
            check(fence.m_glassHover==0 && settled(&fence),"fence fade finishes and stops after leave");
            compare(&fence,canvas,magnetic?"fence-magnetic-leave":"fence-rounded-leave");
        }
        fence.hide();settle(80);check(settled(&fence),"hidden fence has no pointer timer");
        MusicFixture fixture;MusicRootFixture musicRoot(&fixture);fixture.state="Playing";
        const QString cover=root+"/notes-cover.png";wallpaper(QSize(300,300),1).save(cover);
        fixture.art=QUrl::fromLocalFile(cover).toString();
        auto bus=QDBusConnection::sessionBus();check(bus.registerObject("/org/mpris/MediaPlayer2",&fixture,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllProperties|QDBusConnection::ExportAdaptors)
            && bus.registerService("org.mpris.MediaPlayer2.strawberry"),"real MPRIS fixture starts");
        canvas.setMusicWidgetVisible(true);auto *music=canvas.findChild<MusicDesklet *>();settle(450);
        check(music && music->notesAnimating(),"production music notes animate");if(!music)return 1;
        music->move(100,100);settle(150);
        // Freeze MPRIS progress while checking fixed note phases against a
        // full redraw; elapsed song time would otherwise alter static labels.
        fixture.state="Paused";fixture.notify({{"PlaybackStatus",fixture.state}});settle(150);
        QObject::disconnect(&music->m_notesTimer,nullptr,music,nullptr);
        music->m_notesTimer.start();
        for(auto *timer:music->player()->findChildren<QTimer *>())timer->stop();
        music->installEventFilter(&probe);
        const int builds=music->materialBuilds();
        for(qreal seconds:{0.0,0.033,0.5,1.5,4.19,4.23,5.0,8.8}) {
            probe.reset();const QRegion old=music->notesDamage(music->m_notesFrameSeconds);
            music->m_notesFrameSeconds=seconds;music->update(old|music->notesDamage(seconds));settle(45);
            check(probe.pixels<qint64(music->width())*music->height()/2,"music frame redraws old/new note footprints only");
            compare(music,canvas,"music-"+QString::number(seconds));
        }
        check(music->materialBuilds()==builds,"note animation reuses unchanged glass material");
        music->hide();settle(100);check(!music->notesAnimating(),"hiding music stops unchanged 33ms note animation");
        return failures?1:0;
    }
};
