#pragma once

static int runStartupWallpaperTest(const QString &root)
{
    qputenv("GSETTINGS_BACKEND", "keyfile");
    QSettings settings;
    settings.setValue("systemMonitor/autoStart", false);
    settings.setValue("smartSpace/autoStart", false);
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
        {"wallpaperMode", 2}, {"fences", QJsonArray{}}}).toJson());
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
        });
        QElapsedTimer timer; timer.start();
        while (!ready && timer.elapsed()<5000) settle(20);
        check(ready && canvas.isVisible(), "desktop maps after background completion");
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
    return failures ? 1 : 0;
}
