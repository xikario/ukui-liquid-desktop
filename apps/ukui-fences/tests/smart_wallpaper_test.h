#include "SmartSpaceWidget.h"
#include "MusicDesklet.h"
#include <QProcess>
#include <QToolButton>

static int runSmartWallpaperTest(const QString &root)
{
    // Keep all gsettings subprocesses on the isolated XDG_CONFIG_HOME. A
    // D-Bus-activated dconf writer inherits the bus launcher's environment.
    qputenv("GSETTINGS_BACKEND", "keyfile");
    QSettings settings;
    settings.setValue("smartSpace/themeMode", 3);
    settings.setValue("smartSpace/defaultHidden", false);
    settings.setValue("smartSpace/alwaysOnTop", false);
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    const QString redPath = root + "/red.png";
    const QString bluePath = root + "/blue.png";
    QImage solid(1440, 900, QImage::Format_RGB32);
    solid.fill(QColor(210, 30, 30)); solid.save(redPath);
    solid.fill(QColor(30, 30, 210)); solid.save(bluePath);
    auto systemWallpaper = [](const QString &path) {
        return QProcess::execute("gsettings", {"set", "org.mate.background",
                                               "picture-filename", path}) == 0;
    };
    auto layoutWallpaper = [&root](const QString &path, int mode) {
        QDir().mkpath(root + "/config/kyfences");
        QFile file(root + "/config/kyfences/layout.json");
        file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(QJsonObject{{"wallpaperMode", mode},
            {"wallpaperPath", path}, {"fences", QJsonArray{}}}).toJson());
    };
    auto sample = [](SmartSpaceWidget *smart) {
        const QImage image = smart->grab().toImage();
        const qreal dpr = image.devicePixelRatio();
        const QColor color = image.pixelColor(qRound(350 * dpr), qRound((smart->height() - 12) * dpr));
        qInfo() << "Smart Space material sample:" << color;
        return color;
    };
    auto isRed = [](QColor c) { return c.red() > c.blue() + 60; };
    auto isBlue = [](QColor c) { return c.blue() > c.red() + 60; };
    check(systemWallpaper(redPath), "isolated system wallpaper configured");
    layoutWallpaper({}, 5);
    {
        DesktopCanvas canvas;
        canvas.setMusicWidgetVisible(true);
        canvas.showSmartSpaceWidget();
        canvas.moveSmartSpace(100, 100);
        canvas.resizeSmartSpace(800, 500);
        settle(500);
        auto *smart = canvas.findChild<SmartSpaceWidget *>();
        check(smart != nullptr, "Smart Space created");
        if (!smart) return 1;
        auto *music = canvas.findChild<MusicDesklet *>();
        check(music != nullptr, "music card shares the wallpaper change fixture");
        auto musicSample = [&] {
            const QImage image = music ? music->material() : QImage();
            return image.isNull() ? QColor() : image.pixelColor(image.width()/2,image.height()/2);
        };
        check(isRed(sample(smart)), "initial material uses wallpaper A");
        check(isRed(musicSample()), "initial music material uses wallpaper A");
        check(systemWallpaper(bluePath), "switch system wallpaper to B");
        settle(700);
        check(isBlue(sample(smart)), "system wallpaper event refreshes visible material without skin toggle");
        check(isBlue(musicSample()), "system wallpaper event refreshes music without playback or manual refresh");

        auto *toggle = smart->findChild<QToolButton *>("smartThemeToggle");
        check(toggle != nullptr, "skin toggle available");
        if (!toggle) return 1;
        toggle->click(); settle(100);
        check(systemWallpaper(redPath), "change wallpaper during plain skin");
        settle(500);
        toggle->click(); toggle->click(); settle(200);
        check(isRed(sample(smart)), "returning to glass samples latest wallpaper");

        canvas.setSmartSpaceAlwaysOnTop(true); settle(200);
        check(systemWallpaper(bluePath), "change wallpaper in pinned mode");
        settle(600);
        check(isBlue(sample(smart)), "pinned material follows wallpaper without capturing its own window");
        smart->hideToNearestEdge();
        check(systemWallpaper(redPath), "change wallpaper while collapsed");
        settle(500);
        smart->revealFromEdge(); settle(200);
        check(isRed(sample(smart)), "reveal refreshes material after hidden wallpaper change");
        canvas.setSmartSpaceAlwaysOnTop(false); settle(200);

        layoutWallpaper(bluePath, 2);
        canvas.loadLayout(); canvas.refreshAll(); settle(300);
        check(isBlue(sample(smart)), "Fences custom wallpaper refresh reaches Smart Space");
        check(systemWallpaper(bluePath), "system change while custom wallpaper selected");
        settle(300);
        check(systemWallpaper(redPath), "system wallpaper differs from custom wallpaper");
        settle(500);
        check(isBlue(sample(smart)), "custom wallpaper remains authoritative");
        check(isBlue(musicSample()), "music keeps the authoritative custom wallpaper");
        smart->grab().save(root + "/smart-wallpaper-final.png");
    }
    return failures ? 1 : 0;
}
