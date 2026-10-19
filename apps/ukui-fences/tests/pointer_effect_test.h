#include "PointerEffect.h"
#include <QPaintEvent>
#include <QScreen>
#include <ctime>

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
    // Opt-in measurement, kept out of CTest: real production widgets and pointer
    // events under a private X server/session bus. Telemetry sampling is stopped
    // so its work does not get attributed to the monitor's edge reflection.
    static int benchmark(const QString &root, const QString &output) {
        QSettings settings;
        for (const char *key : {"smartSpace/autoStart", "systemMonitor/autoStart",
                 "desklets/clock/autoStart", "desklets/activity/autoStart",
                 "desklets/music/autoStart", "desklets/calendar/autoStart"})
            settings.setValue(key, false);
        settings.setValue("smartSpace/themeMode", 3);
        settings.setValue("smartSpace/glassOptics", true);
        settings.setValue("smartSpace/defaultHidden", false);
        settings.setValue("smartSpace/indexMode", 0);
        settings.setValue("smartSpace/alwaysOnTop", false);
        settings.sync();
        const QString path = root + "/pointer-wall.png";
        wallpaper(QSize(1440, 900), 1).save(path);
        QDir().mkpath(root + "/config/kyfences");
        QFile layout(root + "/config/kyfences/layout.json");
        if (!layout.open(QIODevice::WriteOnly)) return 2;
        layout.write(QJsonDocument(QJsonObject{{"wallpaperPath", path},
            {"wallpaperMode", 2}, {"fences", QJsonArray{}}}).toJson());
        layout.close();
        DesktopCanvas canvas;
        canvas.showAndActivate(); settle(600);
        canvas.setClockWidgetVisible(false); canvas.setActivityWidgetVisible(false);
        canvas.setMusicWidgetVisible(false); canvas.setCalendarWidgetVisible(false);
        canvas.showSystemMonitorWidget();
        auto *monitor = canvas.findChild<SystemMonitor *>();
        if (!monitor) return 2;
        monitor->setSkin(SystemMonitor::Skin::Liquid); monitor->setCompact(true);
        monitor->move(80, 80); settle(6000);
        monitor->m_timer.stop(); monitor->m_diagnosisTimer.stop();
        QJsonArray measurements;
        auto measure = [&](QWidget *widget, const QString &name, const QString &phase,
                           int duration, bool moving) {
            movePointer(widget, QPoint(15, 100)); settle(250);
            PointerPaintProbe probe;
            widget->installEventFilter(&probe);
            QTimer motion;
            motion.setTimerType(Qt::PreciseTimer); motion.setInterval(8);
            int moves = 0;
            QObject::connect(&motion, &QTimer::timeout, widget, [&] {
                const int span = qMin(widget->height() - 130, 100);
                const int offset = (moves++ * 3) % (span * 2);
                movePointer(widget, QPoint(15, 100 + (offset < span ? offset : span * 2 - offset)));
            });
            QElapsedTimer wall; wall.start();
            const std::clock_t begin = std::clock();
            if (moving) motion.start();
            settle(duration); motion.stop();
            const double cpu = double(std::clock() - begin) / CLOCKS_PER_SEC;
            const double seconds = wall.nsecsElapsed() / 1e9;
            widget->removeEventFilter(&probe);
            measurements.append(QJsonObject{{"widget", name}, {"phase", phase},
                {"wallSeconds", seconds}, {"cpuSeconds", cpu},
                {"cpuPercentOneCore", cpu / seconds * 100}, {"moves", moves},
                {"paints", probe.paints}, {"logicalPixels", double(probe.pixels)},
                {"width", widget->width()}, {"height", widget->height()},
                {"dpr", widget->devicePixelRatioF()}});
            settle(150);
            check(settled(widget), "benchmark pointer timer settles after motion");
            movePointer(widget, QPoint(15, 210)); settle(100);
            widget->grab().save(output + "-" + name + ".png");
        };
        measure(monitor, "monitor", "still", 2000, false);
        measure(monitor, "monitor", "edge-motion", 8000, true);
        monitor->hide();
        QFile result(output);
        if (!result.open(QIODevice::WriteOnly)) return 2;
        result.write(QJsonDocument(QJsonObject{{"measurements", measurements},
            {"scope", "Private Xvfb raster rendering; synthetic wallpaper; no indexing; monitor telemetry stopped; 125 Hz pointer input"}}).toJson());
        return failures ? 1 : 0;
    }
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
        settings.setValue("smartSpace/themeMode",3);settings.setValue("smartSpace/glassOptics",true);
        settings.setValue("smartSpace/defaultHidden",false);
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
        return failures ? 1 : 0;
    }
};
