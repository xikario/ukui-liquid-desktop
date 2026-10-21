#pragma once
#include <QScreen>

static int runMonitorIconAvoidanceTest(const QString &root)
{
    const QString desktop = root + "/desktop";
    QDir().mkpath(desktop);
    qputenv("XDG_DESKTOP_DIR", desktop.toUtf8());
    const QString path = desktop + "/monitor-overlap.txt";
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write("keep accessible\n") > 0,
          "create real desktop file for monitor overlap regression");
    file.close();
    QSettings settings;
    settings.setValue("systemMonitor/autoStart", false);
    for (const auto *key : {"clock", "activity", "music", "calendar"})
        settings.setValue(QString("desklets/%1/autoStart").arg(key), false);
    auto iconFor = [&](DesktopCanvas &canvas) -> DesktopIcon * {
        for (auto *icon : canvas.findChildren<DesktopIcon *>(QString(), Qt::FindDirectChildrenOnly))
            if (icon->item().filePath == path) return icon;
        return nullptr;
    };
    auto clearOfMonitor = [](DesktopCanvas &canvas, SystemMonitor *monitor) {
        for (auto *icon : canvas.findChildren<DesktopIcon *>(QString(), Qt::FindDirectChildrenOnly))
            if (icon->isVisible() && icon->geometry().intersects(monitor->geometry().adjusted(-8,-8,8,8)))
                return false;
        return true;
    };
    {
        DesktopCanvas canvas;
        canvas.showAndActivate(); settle(500);
        auto *icon = iconFor(canvas);
        check(icon != nullptr, "desktop file is present before monitor opens");
        if (!icon) return 1;
        settings.setValue("systemMonitor/position", icon->pos());
        canvas.showSystemMonitorWidget(); settle(180);
        auto *monitor = canvas.findChild<SystemMonitor *>("ukui-fences-monitor");
        check(monitor != nullptr, "monitor opens over an occupied desktop area");
        if (!monitor) return 1;
        check(clearOfMonitor(canvas, monitor), "opening monitor relocates all overlapping icons");
        monitor->move(icon->pos()); settle(180);
        check(clearOfMonitor(canvas, monitor), "moving monitor over icons schedules avoidance");

        // A native desktop-icon drop requests an occupied location. It must
        // remain a layout operation and leave the underlying file untouched.
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        const QPoint target = monitor->geometry().center();
        QDragEnterEvent enter(target, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &enter);
        QDropEvent drop(QPointF(target), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &drop); settle(180);
        check(drop.isAccepted() && QFileInfo::exists(path) && clearOfMonitor(canvas, monitor),
              "dropping a desktop file onto monitor chooses an accessible free cell");

        // Cover the icon by resizing without changing position, then give
        // the event coalescer time to relocate it.
        monitor->move(0,0); settle(150);
        monitor->resize(qMax(monitor->width(), icon->geometry().right()+12),
                        qMax(monitor->height(), icon->geometry().bottom()+12));
        settle(180);
        check(clearOfMonitor(canvas, monitor), "resizing monitor also updates occupied cells");
        canvas.refreshAll(); settle(250);
        check(clearOfMonitor(canvas, monitor) && iconFor(canvas) == icon,
              "refresh preserves the file control outside monitor bounds");
        // Hiding without destroying a widget must release its cells; showing
        // the same object must reserve them again, including parent hide/show.
        monitor->hide(); settle(100);
        const QPoint hiddenTarget = monitor->geometry().center();
        QDragEnterEvent hiddenEnter(hiddenTarget, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &hiddenEnter);
        QDropEvent hiddenDrop(QPointF(hiddenTarget), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &hiddenDrop); settle(100);
        check(hiddenDrop.isAccepted() && icon->geometry().intersects(monitor->geometry()),
              "hidden monitor releases its cells for an actual desktop drop");
        monitor->show(); settle(180);
        check(clearOfMonitor(canvas, monitor), "showing the existing monitor reserves its cells again");
        canvas.hide(); canvas.refreshAll(); settle(120);
        canvas.showAndActivate(); settle(220);
        check(clearOfMonitor(canvas, monitor) && iconFor(canvas) == icon,
              "desktop hide, hidden refresh and reveal restore icon avoidance");
        monitor->setSkin(SystemMonitor::Skin::Dark);
        settings.setValue("systemMonitor/autoStart", true);
        canvas.saveLayout();
    }
    settle(80);
    {
        DesktopCanvas restored;
        restored.showAndActivate(); settle(600);
        auto *monitor = restored.findChild<SystemMonitor *>("ukui-fences-monitor");
        check(monitor && iconFor(restored) && clearOfMonitor(restored, monitor),
              "restart restores monitor and file icons without overlap");
        restored.toggleDesklet(); settle(180);
        check(!restored.systemMonitorVisible() && iconFor(restored) && iconFor(restored)->isVisible(),
              "closing monitor leaves the desktop file accessible");
        restored.showSystemMonitorWidget(); settle(180);
        monitor = restored.findChild<SystemMonitor *>("ukui-fences-monitor");
        check(monitor && clearOfMonitor(restored, monitor),
              "reopening monitor reinstates reserved geometry");
    }
    check(file.open(QIODevice::ReadOnly) && file.readAll() == "keep accessible\n",
          "all layout operations preserve the real file path and contents");
    return failures ? 1 : 0;
}

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
