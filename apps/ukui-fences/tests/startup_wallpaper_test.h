#pragma once

static int runStartupWallpaperTest(const QString &root)
{
    qputenv("GSETTINGS_BACKEND", "keyfile");
    QSettings settings;
    settings.setValue("systemMonitor/autoStart", false);
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("appearance/fenceLiquidGlass", true);
    for (const auto *key : {"clock", "activity", "music", "calendar"})
        settings.setValue(QString("desklets/%1/autoStart").arg(key), false);
    const QString path = root + "/startup-red.png";
    QImage image(1600, 1000, QImage::Format_RGB32);
    image.fill(QColor(210, 30, 30));
    image.save(path);
    QDir().mkpath(root + "/config/kyfences");
    QFile layout(root + "/config/kyfences/layout.json");
    layout.open(QIODevice::WriteOnly);
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath", path},
        {"wallpaperMode", 2}, {"fences", QJsonArray{QJsonObject{
            {"id", "startup-glass"}, {"title", "Startup"},
            {"x", 300}, {"y", 180}, {"w", 320}, {"h", 240}}}}}).toJson());
    layout.close();
    {
        DesktopCanvas canvas;
        check(!canvas.isVisible(), "constructor never maps an unprepared blue desktop");
        canvas.activateOnSessionStartup();
        canvas.showAndActivate();
        check(!canvas.isVisible(), "startup activation waits for the first wallpaper");
        bool ready = false;
        QObject::connect(&canvas, &DesktopCanvas::initialWallpaperReady, &canvas, [&] {
            ready = true;
            const QImage backdrop = canvas.wallpaperBackdrop(
                QRect(canvas.mapToGlobal(QPoint()), QSize(10, 10)), 1);
            check(!backdrop.isNull() && backdrop.pixelColor(5,5).red() > 180,
                  "first activation has the selected wallpaper prepared");
            auto *fence = canvas.findChild<FenceWidget *>();
            check(fence && fence->property("glassBuilds").toInt() > 0 && !canvas.isVisible(),
                  "actual fence material is cached before the desktop maps");
            if (fence) {
                const QImage panel = fence->grab().toImage();
                check(!panel.isNull() && panel.pixelColor(panel.width()/2, panel.height()/2).red()
                      > panel.pixelColor(panel.width()/2, panel.height()/2).blue() + 60,
                      "first fence panel has wallpaper glass rather than blue fallback");
            }
        });
        QElapsedTimer timer; timer.start();
        while (!ready && timer.elapsed()<5000) settle(20);
        check(ready && canvas.isVisible(), "desktop maps after background completion");
        auto *fence = canvas.findChild<FenceWidget *>();
        if (fence) {
            const int builds = fence->property("glassBuilds").toInt();
            fence->grab(); settle(300); fence->grab();
            check(fence->property("glassBuilds").toInt() == builds,
                  "first visible paint reuses the prepared material");
        }
        canvas.hideFences();
        settle(1000);
        check(!canvas.isVisible(), "login retries respect the user's hidden desktop");
    }
    {
        DesktopCanvas canvas;
        canvas.activateOnSessionStartup();
        canvas.hideFences();
        settle(500);
        check(!canvas.isVisible(), "wallpaper completion cannot override hide during startup");
    }
    {
        for (const auto *key : {"clock", "activity", "music", "calendar"})
            settings.setValue(QString("desklets/%1/autoStart").arg(key), true);
        settings.setValue("smartSpace/autoStart", true);
        settings.setValue("smartSpace/defaultHidden", true);
        settings.sync();
        DesktopCanvas canvas;
        bool widgetsReadyBeforeMap = false;
        QObject::connect(&canvas, &DesktopCanvas::initialWallpaperReady, &canvas, [&] {
            const auto widgets = canvas.findChildren<LiquidDesklet *>();
            widgetsReadyBeforeMap = !canvas.isVisible() && widgets.size() == 4
                && std::all_of(widgets.begin(), widgets.end(), [](LiquidDesklet *widget) { return !widget->isHidden(); });
        });
        canvas.activateOnSessionStartup();
        bool observed = false;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed()<5000 && !observed) {
            settle(10);
            if (canvas.smartSpaceVisible()) {
                check(canvas.clockWidgetVisible() && canvas.activityWidgetVisible()
                      && canvas.musicWidgetVisible() && canvas.calendarWidgetVisible(),
                      "all four auto-start widgets exist before Smart Space finishes startup");
                observed = true;
            }
        }
        check(observed, "session startup restores Smart Space after the small widgets");
        check(widgetsReadyBeforeMap, "all four light widgets are restored before the desktop maps");
        // Keep exercising the default-hidden entry as created during login.
        settle(500);
        canvas.revealSmartSpaceFromEdge(); settle(250);
        check(!canvas.smartSpaceEdgeHidden(), "first reveal works after automatic login collapse");
        canvas.hideSmartSpaceToEdge(); settle(250);
        check(canvas.smartSpaceEdgeHidden(), "first retract works after automatic login collapse");
    }
    // A missing wallpaper must release the same material gate using the gradient.
    {
        for (const auto *key : {"clock", "activity", "music", "calendar"})
            settings.setValue(QString("desklets/%1/autoStart").arg(key), false);
        settings.setValue("smartSpace/autoStart", false); settings.sync();
        QFile file(root + "/config/kyfences/layout.json");
        file.open(QIODevice::ReadOnly);
        QJsonObject broken = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        broken["wallpaperPath"] = root + "/missing.png";
        file.open(QIODevice::WriteOnly); file.write(QJsonDocument(broken).toJson()); file.close();
        DesktopCanvas canvas; canvas.showAndActivate();
        QElapsedTimer timer; timer.start();
        while (!canvas.isVisible() && timer.elapsed()<5000) settle(20);
        check(canvas.isVisible(), "failed wallpaper decoding releases startup with prepared gradient glass");
    }
    return failures ? 1 : 0;
}
