#pragma once
#include <QScreen>

static int runMonitorPlacementTest()
{
    QSettings settings;
    settings.setValue("systemMonitor/autoStart", false);
    settings.setValue("smartSpace/autoStart", false);
    for (const auto *key : {"clock", "activity", "music", "calendar"})
        settings.setValue(QString("desklets/%1/autoStart").arg(key), false);
    qunsetenv("DEEPSEEK_API_KEY");
    DesktopCanvas canvas;
    canvas.showSystemMonitorWidget();
    auto *monitor = canvas.findChild<SystemMonitor *>("ukui-fences-monitor");
    check(monitor != nullptr, "monitor is created for placement regression");
    if (!monitor) return 1;
    const QPoint topRight(canvas.width() - monitor->width(), 0);
    monitor->move(topRight);
    // Changing skin uses the same position persistence path as ending a drag.
    monitor->setSkin(SystemMonitor::Skin::Dark);
    monitor->setSkin(SystemMonitor::Skin::Light);
    check(settings.value("systemMonitor/position").toPoint() == topRight,
          "top-right position is saved without an inset");
    canvas.toggleDesklet();
    settle();
    canvas.showSystemMonitorWidget();
    monitor = canvas.findChild<SystemMonitor *>("ukui-fences-monitor");
    check(monitor && monitor->pos() == topRight,
          "recreating monitor restores the exact top-right edge");
    if (!monitor) return 1;
    auto syncScreen = [] {
        auto *screen = QApplication::primaryScreen();
        // Login panel struts cause this notification without changing screen size.
        check(QMetaObject::invokeMethod(screen, "availableGeometryChanged",
                  Qt::DirectConnection, Q_ARG(QRect, screen->availableGeometry())),
              "screen work-area notification is delivered");
        settle(250);
    };
    syncScreen();
    syncScreen();
    check(monitor->pos() == topRight,
          "panel work-area updates preserve top-right placement");
    monitor->move(0, 0);
    syncScreen();
    check(monitor->pos() == QPoint(0, 0), "top-left edge has no forced margin");
    const QPoint bottomRight(canvas.width() - monitor->width(),
                             canvas.height() - monitor->height());
    monitor->move(bottomRight);
    monitor->setSkin(SystemMonitor::Skin::Dark);
    canvas.toggleDesklet();
    settle();
    canvas.showSystemMonitorWidget();
    monitor = canvas.findChild<SystemMonitor *>("ukui-fences-monitor");
    check(monitor && monitor->pos() == bottomRight,
          "restoring bottom-right placement has no one-pixel inset");
    if (!monitor) return 1;
    monitor->move(canvas.width() + 100, canvas.height() + 100);
    syncScreen();
    check(monitor->pos() == bottomRight,
          "offscreen positions are still clamped inside the desktop");
    canvas.show();canvas.setGlobalEditMode(true);settle(80);
    monitor->move(canvas.width()-monitor->width(),0);
    const QSize initial=monitor->size();
    auto *peer=canvas.createFence("resize neighbour",
        QRect(canvas.width()-initial.width()-40-200,0,200,initial.height()+80));
    const QPoint grip(monitor->width()-10,monitor->height()-10),global=monitor->mapToGlobal(grip);
    const int dx=70; // Averaging the two scale axes gives ~35 px wider.
    QMouseEvent press(QEvent::MouseButtonPress,grip,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(monitor,&press);
    QMouseEvent move(QEvent::MouseMove,grip+QPoint(dx,0),global+QPoint(dx,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(monitor,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,grip,global+QPoint(dx,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(monitor,&release);
    check(monitor->x()==peer->x()+peer->width()
          && monitor->x()+monitor->width()==canvas.width() && monitor->y()==0,
          "monitor real resize snaps to neighbour while preserving the top-right anchor");
    canvas.setGlobalEditMode(false);
    return failures ? 1 : 0;
}
