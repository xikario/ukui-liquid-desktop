#include "DesktopWidgets.h"
#include "DeskletModels.h"
#include "ActivityRecorder.h"
#include <QPushButton>
#include "DesktopIcon.h"
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QSpinBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

static int runDeskletTest(const QString &root)
{
    qputenv("GSETTINGS_BACKEND", "keyfile");
    CountdownState c;c.start(60000,1000);
    check(c.remaining(11000)==50000,"countdown uses deadline rather than tick count");
    c.toggle(11000);check(c.state==CountdownState::Paused && c.remaining(99000)==50000,"pause freezes remaining time");
    c=CountdownState::fromJson(c.toJson());c.toggle(101000);
    check(c.remaining(131000)==20000,"paused state survives restoration and resumes accurately");
    c=CountdownState::fromJson(c.toJson());
    check(c.expire(151001) && !c.expire(160000),"deadline expires exactly once after restart");
    c.cancel();check(c.state==CountdownState::Ready && c.remaining(0)==60000,"cancel resets duration");
    ActivityLedger ledger;
    const QDateTime end(QDate(2026,9,28),QTime(0,0,5));
    ledger.addInterval(end,10000,"editor","Editor");
    check(ledger.days.value(QDate(2026,9,27)).apps.value("editor").ms==5000 && ledger.days.value(end.date()).apps.value("editor").ms==5000,"foreground interval splits correctly at midnight");
    ledger.observeBoot("boot-a",60000,end);auto first=ledger.toJson();
    ledger=ActivityLedger::fromJson(first);ledger.observeBoot("boot-a",60000,end);
    check(ledger.toJson()==first,"same boot and uptime do not duplicate data after restart");
    ledger.observeBoot("boot-a",65000,end.addSecs(5));
    ledger.observeBoot("boot-b",3000,end.addSecs(8));
    qint64 total=0;for(const auto &d:ledger.days)total+=d.uptimeMs;
    check(total==68000,"new boot and same-boot increments are accounted once");
    ledger.days[QDate(2020,1,1)]={};ledger.prune(end.date());check(!ledger.days.contains(QDate(2020,1,1)),"history retains only the latest sixty dates");
    QSettings s;s.setValue("systemMonitor/autoStart",false);s.setValue("smartSpace/autoStart",false);s.sync();
    const QString redPath=root+"/desklet-red.png",bluePath=root+"/desklet-blue.png";
    QImage color(1440,900,QImage::Format_RGB32);color.fill(QColor(215,40,35));color.save(redPath);color.fill(QColor(30,40,215));color.save(bluePath);
    auto setWallpaper=[&](const QString &path){QDir().mkpath(root+"/config/kyfences");QFile f(root+"/config/kyfences/layout.json");f.open(QIODevice::WriteOnly);f.write(QJsonDocument(QJsonObject{{"wallpaperMode",2},{"wallpaperPath",path},{"fences",QJsonArray{}}}).toJson());};
    setWallpaper(redPath);
    const QString desktop=root+"/test-desktop";
    QDir().mkpath(desktop+"/fixture-folder");
    QFile fixture(desktop+"/fixture.txt");fixture.open(QIODevice::WriteOnly);fixture.write("layout fixture");fixture.close();
    qputenv("XDG_DESKTOP_DIR",desktop.toUtf8());
    {
        DesktopCanvas canvas;canvas.show();canvas.setClockWidgetVisible(true);canvas.setActivityWidgetVisible(true);settle(700);
        auto *clock=canvas.findChild<ClockDesklet *>();auto *activity=canvas.findChild<ActivityDesklet *>();auto *recorder=canvas.findChild<ActivityRecorder *>();
        check(clock && activity && recorder,"both desktop widgets share a live canvas and recorder");if(!clock || !activity || !recorder)return 1;
        check(clock->isVisible() && activity->isVisible(),"both display actions reveal their widgets");
        check(!clock->geometry().intersects(activity->geometry()),"first-use placement does not overlap the other desklet");
        check(!clock->material().isNull() && clock->material().pixelColor(0,0).alpha()==0,"shared material retains transparent antialiased corners");
        const int builds=clock->materialBuilds();for(int i=0;i<10;++i)clock->tick();settle(150);
        check(clock->materialBuilds()==builds,"clock tick reuses cached optics");
        auto mouse=[](QWidget *widget,QEvent::Type type,QPoint local,QPoint global){
            QMouseEvent event(type,local,global,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);
        };
        auto drag=[&](LiquidDesklet *widget,QPoint local,QPoint delta){
            const QPoint global=widget->mapToGlobal(local);
            mouse(widget,QEvent::MouseButtonPress,local,global);
            mouse(widget,QEvent::MouseMove,local+delta,global+delta);
            mouse(widget,QEvent::MouseButtonRelease,local,global+delta);
        };
        check(!clock->editMode() && !activity->editMode(),"widgets start with layout locked");
        for(auto *widget : {static_cast<LiquidDesklet *>(clock),static_cast<LiquidDesklet *>(activity)}){
            const QRect original=widget->geometry();
            drag(widget,QPoint(70,20),QPoint(32,36));
            drag(widget,QPoint(widget->width()-5,widget->height()-5),QPoint(32,36));
            check(widget->geometry()==original,"locked widget ignores header drag and resize grip");
            canvas.setGlobalEditMode(true);
            check(widget->editMode(),"global layout editing unlocks widget");
            drag(widget,QPoint(70,20),QPoint(32,36));
            check(widget->pos()!=original.topLeft() && canvas.rect().contains(widget->geometry()),"edit mode allows widget movement within the desktop (including snapping)");
            drag(widget,QPoint(widget->width()-5,widget->height()-5),QPoint(32,36));
            check(widget->size()==original.size()+QSize(32,36),"edit mode allows widget resize");
            const QPoint local(70,20),global=widget->mapToGlobal(local);
            mouse(widget,QEvent::MouseButtonPress,local,global);
            canvas.setGlobalEditMode(false);
            const QRect locked=widget->geometry();
            mouse(widget,QEvent::MouseMove,local+QPoint(40,40),global+QPoint(40,40));
            mouse(widget,QEvent::MouseButtonRelease,local,global+QPoint(40,40));
            check(widget->geometry()==locked,"exiting layout editing cancels an in-flight drag");
            widget->setGeometry(original);
        }
        // Real drag events exercise edge clamping and magnetic placement.
        const QRect clockBeforeSnap=clock->geometry(),activityBeforeSnap=activity->geometry();
        canvas.setGlobalEditMode(true);canvas.setActivityWidgetVisible(false);
        auto dragTo=[&](LiquidDesklet *widget,QPoint target){drag(widget,QPoint(70,20),target-widget->pos());};
        dragTo(clock,QPoint(19,23));
        check(clock->pos()==QPoint(0,0),"drag snaps flush to screen top and left without a forced margin");
        dragTo(clock,QPoint(canvas.width()-clock->width()-20,canvas.height()-clock->height()-20));
        check(clock->geometry().right()==canvas.rect().right() && clock->geometry().bottom()==canvas.rect().bottom(),
              "drag snaps flush to screen right and bottom without a one-pixel gap");
        dragTo(clock,QPoint(-100,-100));
        check(clock->pos()==QPoint(0,0),"dragging past screen bounds keeps the entire widget visible");
        canvas.setClockWidgetVisible(false);canvas.setClockWidgetVisible(true);
        check(clock->pos()==QPoint(0,0),"hide and reveal retain an edge-aligned placement");
        activity->move(600,200);canvas.setActivityWidgetVisible(true);
        dragTo(clock,QPoint(activity->x()-clock->width()+7,activity->y()+5));
        check(clock->x()+clock->width()==activity->x() && clock->y()==activity->y()
              && !clock->geometry().intersects(activity->geometry()),"neighboring widgets snap side by side with aligned tops and no overlap");
        dragTo(clock,QPoint(activity->x()+6,activity->y()+activity->height()+8));
        check(clock->x()==activity->x() && clock->y()==activity->y()+activity->height(),
              "neighboring widgets snap vertically with aligned left edges");
        dragTo(clock,QPoint(200,200));
        check(clock->pos()==QPoint(200,200),"widgets detach freely when dragged beyond the snap threshold");
        auto *snapFence=canvas.createFence("snap fixture",QRect(800,500,240,200));
        dragTo(clock,QPoint(800-clock->width()+6,505));
        check(clock->x()+clock->width()==snapFence->x() && clock->y()==snapFence->y(),
              "new widget can dock beside an existing fence");
        snapFence->hide();
        clock->setGeometry(clockBeforeSnap);activity->setGeometry(activityBeforeSnap);
        canvas.setGlobalEditMode(false);settle(100);
        auto fixtureIcon=[&](const QString &path)->DesktopIcon *{
            for(auto *icon:canvas.findChildren<DesktopIcon *>(QString(),Qt::FindDirectChildrenOnly))
                if(icon->item().filePath==path)return icon;
            return nullptr;
        };
        auto avoidsWidgets=[&](DesktopIcon *icon){
            return icon && !icon->geometry().intersects(clock->geometry().adjusted(-8,-8,8,8))
                && !icon->geometry().intersects(activity->geometry().adjusted(-8,-8,8,8));
        };
        settle(100);
        for(const QString &path:{desktop+"/fixture.txt",desktop+"/fixture-folder"}){
            check(avoidsWidgets(fixtureIcon(path)),"synced file and folder icons avoid both widget areas");
            QMimeData mime;mime.setUrls({QUrl::fromLocalFile(path)});
            for(auto *target:{static_cast<LiquidDesklet *>(clock),static_cast<LiquidDesklet *>(activity)}){
                const QPoint at=target->geometry().center();
                QDragEnterEvent enter(at,Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(&canvas,&enter);
                QDropEvent drop(at,Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(&canvas,&drop);settle(80);
                check(drop.isAccepted() && avoidsWidgets(fixtureIcon(path)),"file and folder dropped on a widget snap to an unoccupied cell");
                check(QFileInfo::exists(path),"same-desktop drop preserves the original fixture");
            }
        }
        if(auto *icon=fixtureIcon(desktop+"/fixture.txt")){
            const QRect original=activity->geometry();
            activity->move(icon->pos());settle(100);
            check(avoidsWidgets(icon),"moving a widget over an icon relocates the icon");
            activity->setGeometry(original);settle(100);
        }
        // Real X11 property changes on an isolated X server exercise the recorder.
        Display *display=XOpenDisplay(nullptr);check(display!=nullptr,"activity X11 connection opens");
        if(display){
            const Window rootWindow=DefaultRootWindow(display);
            const Atom activeAtom=XInternAtom(display,"_NET_ACTIVE_WINDOW",False);
            const Atom pidAtom=XInternAtom(display,"_NET_WM_PID",False);
            Window window=XCreateSimpleWindow(display,rootWindow,0,0,20,20,0,0,0);
            char instance[]="desklet-recorder-test", klass[]="DeskletTestEditor";
            XClassHint hint{instance,klass};XSetClassHint(display,window,&hint);
            unsigned long foreignPid=424242;
            XChangeProperty(display,window,pidAtom,XA_CARDINAL,32,PropModeReplace,reinterpret_cast<unsigned char *>(&foreignPid),1);
            XChangeProperty(display,rootWindow,activeAtom,XA_WINDOW,32,PropModeReplace,reinterpret_cast<unsigned char *>(&window),1);XSync(display,False);
            settle(100);recorder->sample();settle(120);recorder->sample();
            auto appTime=[&]{return recorder->ledger().days.value(QDate::currentDate()).apps.value("desklettesteditor").ms;};
            check(appTime()>=100,"X11 foreground class accrues actual elapsed time");
            recorder->setLocked(true);const auto locked=appTime();settle(120);recorder->sample();
            check(appTime()==locked,"lock screen excludes foreground time");recorder->setLocked(false);
            auto bus=QDBusConnection::sessionBus();check(bus.registerService("org.ukui.ScreenSaver"),"isolated UKUI lock service registered");
            bus.send(QDBusMessage::createSignal("/","org.ukui.ScreenSaver","lock"));settle(70);
            const auto ukuiLocked=appTime();settle(120);recorder->sample();check(appTime()==ukuiLocked,"native UKUI lock signal pauses accounting");
            bus.send(QDBusMessage::createSignal("/","org.ukui.ScreenSaver","unlock"));settle(70);
            settle(120);recorder->sample();check(appTime()>ukuiLocked,"native UKUI unlock signal resumes accounting");
            bus.unregisterService("org.ukui.ScreenSaver");
            recorder->setRecording(false);const auto paused=appTime();settle(120);recorder->sample();
            check(appTime()==paused,"paused recording excludes foreground time");recorder->setRecording(true);
            XDeleteProperty(display,rootWindow,activeAtom);XDestroyWindow(display,window);XSync(display,False);settle(50);recorder->sample();
            const auto hidden=appTime();settle(100);recorder->sample();check(appTime()==hidden,"missing foreground window accrues no application time");
            XCloseDisplay(display);
        }
        const int activityBuilds=activity->materialBuilds();recorder->sample();settle(100);
        check(activity->materialBuilds()==activityBuilds,"activity sampling does not regenerate material");
        clock->findChild<QPushButton *>("timerTab")->click();
        auto *minutes=clock->findChild<QSpinBox *>("countdownMinutes");minutes->setValue(5);
        auto *start=clock->findChild<QPushButton *>("countdownStart");start->click();
        check(clock->countdown().state==CountdownState::Running && !minutes->isEnabled(),"countdown begins and protects its active duration");
        start->click();check(clock->countdown().state==CountdownState::Paused,"UI pauses countdown");
        start->click();check(clock->countdown().state==CountdownState::Running,"UI resumes countdown");
        clock->findChild<QPushButton *>("countdownCancel")->click();check(clock->countdown().state==CountdownState::Ready,"UI cancels countdown");
        auto *pause=activity->findChild<QPushButton *>("activityRecording");pause->click();check(!recorder->isRecording(),"recording can be paused explicitly");pause->click();
        const auto before=clock->material();setWallpaper(bluePath);canvas.loadLayout();canvas.refreshAll();settle(700);
        check(before!=clock->material(),"wallpaper refresh reaches new clock material");
        canvas.setActivityWidgetVisible(false);setWallpaper(redPath);canvas.loadLayout();canvas.refreshAll();settle(300);canvas.setActivityWidgetVisible(true);settle(200);
        const QImage material=activity->material();const QColor sample=material.pixelColor(material.width()/2,material.height()/2);
        check(sample.red()>sample.blue()+40,"hidden activity widget refreshes wallpaper on reveal");
        clock->move(123,145);clock->savePlacement();LiquidDesklet::setAutoStart("clock",true);LiquidDesklet::setAutoStart("activity",true);
        check(LiquidDesklet::autoStartEnabled("clock") && LiquidDesklet::autoStartEnabled("activity"),"independent startup flags persist");
        const QImage timerPreview=clock->grab().toImage();timerPreview.save(root+"/clock.png");clock->findChild<QPushButton *>("clockTab")->click();clock->grab().save(root+"/clock-analogue.png");activity->grab().save(root+"/activity.png");
        const QString output=QDir::currentPath()+"/../test-results/desklets-"+QString::number(clock->devicePixelRatioF());QDir().mkpath(output);timerPreview.save(output+"/countdown.png");clock->grab().save(output+"/clock.png");activity->grab().save(output+"/activity.png");
        recorder->flush();QFile f(recorder->dataPath());check(f.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(f.readAll()).isObject(),"activity data saves atomically as valid local JSON");
    }
    {
        DesktopCanvas canvas;canvas.showAndActivate();
        QElapsedTimer startup; startup.start();
        while ((!canvas.clockWidgetVisible() || !canvas.activityWidgetVisible())
               && startup.elapsed()<5000) settle(20);
        check(canvas.clockWidgetVisible() && canvas.activityWidgetVisible(),"saved startup choices restore both widgets");
        auto *clock=canvas.findChild<ClockDesklet *>();check(clock && clock->pos()==QPoint(123,145),"widget position survives host restart");
    }
    {
        LiquidDesklet::setAutoStart("clock",false);LiquidDesklet::setAutoStart("activity",false);
        DesktopCanvas canvas;canvas.show();canvas.setGlobalEditMode(true);
        canvas.setClockWidgetVisible(true);canvas.setActivityWidgetVisible(true);
        check(canvas.findChild<ClockDesklet *>()->editMode() && canvas.findChild<ActivityDesklet *>()->editMode(),
              "widgets created during layout editing inherit the global mode");
        auto *clock=canvas.findChild<ClockDesklet *>();clock->move(0,0);clock->savePlacement();
    }
    {
        DesktopCanvas canvas;canvas.show();canvas.setClockWidgetVisible(true);
        check(canvas.findChild<ClockDesklet *>()->pos()==QPoint(0,0),"restart restores a flush top-left position without adding a margin");
    }
    return failures ? 1 : 0;
}
