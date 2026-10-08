#pragma once
#include <QKeyEvent>
#include <QPointer>

// The public menu and F5 entry points must preserve an unchanged desktop.
static int runDesktopRefreshTest(const QString &root)
{
    const QString desktop = root + "/desktop";
    QDir().mkpath(desktop);
    qputenv("XDG_DESKTOP_DIR", desktop.toUtf8());
    QSettings settings;
    for (const QString &name : {QString("smartSpace"), QString("systemMonitor"),
        QString("clockWidget"), QString("activityWidget"), QString("musicWidget"), QString("calendarWidget")})
        settings.setValue(name + "/autoStart", false);
    settings.sync();
    auto write = [](const QString &path, const QByteArray &data) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    auto entry = [&](const QString &path, const QByteArray &name) {
        check(write(path, "[Desktop Entry]\nType=Application\nName=" + name
            + "\nExec=/bin/true\nIcon=folder\n"), "write desktop entry fixture");
    };
    const QString loosePath = desktop + "/loose.desktop";
    const QString fencePath = desktop + "/fenced.desktop";
    entry(loosePath, "Loose before"); entry(fencePath, "Fence before");
    const QString wallPath = root + "/refresh-wall.png";
    QImage wall(320, 240, QImage::Format_RGB32); wall.fill(QColor("#b07a38"));
    check(wall.save(wallPath), "create refresh wallpaper fixture");
    QDir().mkpath(root + "/config/kyfences");
    write(root + "/config/kyfences/layout.json", QJsonDocument(QJsonObject{
        {"wallpaperPath", wallPath}, {"wallpaperMode", 2}, {"autoArrange", false},
        {"fences", QJsonArray{QJsonObject{{"id", "refresh-fence"}, {"title", "Refresh"},
            {"x", 600}, {"y", 150}, {"w", 320}, {"h", 260},
            {"files", QJsonArray{fencePath}}}}}}).toJson());
    DesktopCanvas canvas;
    canvas.showAndActivate(); settle(500);
    auto iconFor = [&](const QString &path) -> DesktopIcon * {
        for (auto *icon : canvas.findChildren<DesktopIcon *>())
            if (icon->item().filePath == path) return icon;
        return nullptr;
    };
    QPointer<DesktopIcon> loose = iconFor(loosePath);
    QPointer<DesktopIcon> fenced = iconFor(fencePath);
    check(loose && fenced && fenced->iconSurface() == IconSurface::Fence,
        "refresh fixture has loose and fenced icons");
    if (!loose || !fenced) return 1;
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(20, 20), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(loose, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(20, 20), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(loose, &release);
    settle(300);
    check(loose->isSelected(), "real click selects loose icon before refresh");
    const QPoint loosePos = loose->pos(), fencePos = fenced->pos();
    int wallpaperChanges = 0;
    QObject::connect(&canvas, &DesktopCanvas::wallpaperChanged, &canvas, [&] { ++wallpaperChanges; });
    bool menuUsed = false;
    QTimer::singleShot(60, &canvas, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (auto *action : menu->actions())
            if (action->text() == QStringLiteral("刷新桌面")) {
                menuUsed = true; action->trigger(); break;
            }
        menu->close();
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(500, 500), canvas.mapToGlobal(QPoint(500, 500)));
    QApplication::sendEvent(&canvas, &context);
    settle(100);
    check(menuUsed, "actual desktop context-menu refresh runs");
    check(loose && iconFor(loosePath) == loose && fenced && iconFor(fencePath) == fenced,
        "unchanged menu refresh retains loose and fence controls");
    check(loose && loose->isSelected() && loose->pos() == loosePos && fenced && fenced->pos() == fencePos,
        "unchanged menu refresh preserves selection and manual positions");
    check(wallpaperChanges == 0, "unchanged refresh retains wallpaper materials");
    if (!loose || !fenced) return 1;

    // A brief flash confirms refresh without replacing or hiding the icon.
    settle(180);
    const QImage stable = loose->grab().toImage();
    QKeyEvent f5(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &f5); settle(50);
    check(stable != loose->grab().toImage(), "F5 gives visible refresh feedback on the existing icon");
    settle(200);
    check(stable == loose->grab().toImage(), "refresh feedback ends without changing icon appearance or selection");
    canvas.setReduceMotion(true);
    QApplication::sendEvent(&canvas,&f5);settle(15);
    const QImage pulse=loose->grab().toImage();
    check(stable!=pulse,"reduced-motion refresh still has visible static feedback");
    settle(20);
    check(pulse==loose->grab().toImage(),"reduced-motion feedback does not animate between frames");
    settle(100);
    check(stable==loose->grab().toImage(),"the reduced-motion pulse ends and restores the existing icon");
    canvas.setReduceMotion(false);
    entry(loosePath, "Loose after"); entry(fencePath, "Fence after");
    QApplication::sendEvent(&canvas, &f5); settle(200);
    check(loose && loose->item().displayName == "Loose after" && loose->isSelected()
        && iconFor(loosePath) == loose && fenced && fenced->item().displayName == "Fence after"
        && iconFor(fencePath) == fenced, "F5 updates same-path metadata in both surfaces without recreating icons");
    QApplication::sendEvent(fenced, &press); QApplication::sendEvent(fenced, &release); settle(300);
    check(fenced->isSelected(), "real click selects fence icon before F5");
    QApplication::sendEvent(&canvas, &f5); settle(100);
    check(fenced && fenced->isSelected() && iconFor(fencePath) == fenced,
        "F5 preserves real fence selection");
    QApplication::sendEvent(loose, &press); QApplication::sendEvent(loose, &release); settle(300);
    const QString added = desktop + "/added.txt";
    write(added, "new file"); QFile::remove(fencePath);
    QApplication::sendEvent(&canvas, &f5); settle(200);
    // This harness runs before QApplication::exec(); flush deletion at the
    // outer-loop boundary that the real application reaches after handling F5.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(iconFor(added), "refresh displays newly added file");
    check(!iconFor(fencePath), "refresh removes deleted fence file control");
    check(loose && iconFor(loosePath) == loose && loose->isSelected(),
        "refresh preserves unchanged selection when other files are added or removed");

    // A watched directory can be replaced externally; refresh must rebind it.
    const QString oldDesktop = root + "/old-desktop";
    check(QDir().rename(desktop, oldDesktop) && QDir().mkpath(desktop), "replace desktop directory fixture");
    entry(loosePath, "Replacement");
    QApplication::sendEvent(&canvas, &f5); settle(200);
    const QString watched = desktop + "/watch-restored.txt";
    write(watched, "watch event");
    QElapsedTimer timer; timer.start();
    while (!iconFor(watched) && timer.elapsed() < 3000) settle(20);
    check(iconFor(watched), "manual refresh restores directory watching after replacement");

    wall.fill(QColor("#285acc")); wall.save(wallPath);
    QFile changed(wallPath); changed.open(QIODevice::ReadWrite);
    changed.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime); changed.close();
    QApplication::sendEvent(&canvas, &f5);
    timer.restart(); while (!wallpaperChanges && timer.elapsed() < 3000) settle(20);
    const QImage backdrop = canvas.wallpaperBackdrop(QRect(canvas.mapToGlobal(QPoint(10, 10)), QSize(20, 20)), 1);
    check(wallpaperChanges > 0 && !backdrop.isNull() && backdrop.pixelColor(10, 10).blue() > 150,
        "refresh applies actual same-path wallpaper changes");
    return failures ? 1 : 0;
}
