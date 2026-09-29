#pragma once
#include <QMoveEvent>
static int runIconDamageTest(const QString &root)
{
    QSettings settings;settings.setValue("smartSpace/autoStart",false);settings.setValue("systemMonitor/autoStart",false);settings.sync();
    const QString path=root+"/damage-wall.png";wallpaper(QSize(1440,900),1).save(path);
    QDir().mkpath(root+"/config/kyfences");QFile layout(root+"/config/kyfences/layout.json");layout.open(QIODevice::WriteOnly);
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",path},{"wallpaperMode",2},{"fences",QJsonArray{}}}).toJson());layout.close();
    SelectionPaintProbe canvas;canvas.showAndActivate();
    canvas.setClockWidgetVisible(false);canvas.setActivityWidgetVisible(false);canvas.setMusicWidgetVisible(false);canvas.setCalendarWidgetVisible(false);
    // Let the theme plugin's deferred startup polish finish before measuring.
    settle(6000);
    // Production currently leaves this fallback timer stopped. Exercise two
    // explicit reconciliation ticks so the test detects accidental full repaint.
    for(auto *timer:canvas.findChildren<QTimer *>())
        if(timer->interval()==1500) timer->start();
    canvas.paintedPixels=0;canvas.paints=0;settle(3300);
    qInfo()<<"Idle reconciliation paints/pixels:"<<canvas.paints<<canvas.paintedPixels;
    check(canvas.paintedPixels<qint64(canvas.width())*canvas.height()/2,"unchanged file reconciliation does not repaint full desktop");
    DesktopItem item;item.isSystemIcon=true;item.filePath="computer:///";item.displayName="移动图标";item.icon=QIcon::fromTheme("folder");
    DesktopIcon icon(item,&canvas);canvas.configureIconAppearance(&icon,IconSurface::Desktop);icon.move(400,250);icon.show();settle(100);
    auto capture=[&]{return QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage();};
    for(QPoint pos:{QPoint(500,400),QPoint(650,200),QPoint(410,450),QPoint(400,250)}) {
        canvas.paintedPixels=0;icon.move(pos);settle(60);const QImage incremental=capture();
        check(canvas.paintedPixels<qint64(canvas.width())*canvas.height()/2,"moving icon damages bounded footprints only");
        canvas.update();settle(60);check(!incremental.isNull() && incremental==capture(),"moved icon backing store equals clean repaint without old wallpaper patches");
    }
    return failures?1:0;
}
