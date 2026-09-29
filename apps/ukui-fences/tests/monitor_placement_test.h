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
    return failures ? 1 : 0;
}
