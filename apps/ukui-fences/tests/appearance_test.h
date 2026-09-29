#pragma once
#include "WallpaperImage.h"

static int runAppearanceTest(const QString &root)
{
    QSettings settings;
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    const QString imagePath = root + "/城市秋雨黄叶.png";
    QImage original(320, 240, QImage::Format_RGB32);
    original.fill(QColor(218, 173, 92));
    check(original.save(imagePath, "JPEG"), "create JPEG with PNG suffix and Chinese path");
    QString error;
    check(readWallpaperPixmap(imagePath, &error).size() == original.size() && error.isEmpty(),
          "wallpaper decoder detects contents instead of suffix");
    const QString invalidPath = root + "/invalid.png";
    QFile invalid(invalidPath);invalid.open(QIODevice::WriteOnly);invalid.write("not an image");invalid.close();
    check(readWallpaperPixmap(invalidPath, &error).isNull() && !error.isEmpty(),
          "invalid wallpaper returns an actionable reader error");
    QDir().mkpath(root + "/config/kyfences");
    QFile fixture(root + "/config/kyfences/layout.json");
    fixture.open(QIODevice::WriteOnly);
    fixture.write(QJsonDocument(QJsonObject{{"wallpaperMode",0},{"wallpaperPath",imagePath},
        {"fontColor","#ffffffff"},{"fences",QJsonArray{QJsonObject{{"id","appearance"},
        {"title","项目与服务"},{"x",80},{"y",100},{"w",500},{"h",300}}}}}).toJson());
    fixture.close();
    DesktopCanvas canvas;
    canvas.showAndActivate();settle(300);
    const auto background = canvas.wallpaperBackdrop(QRect(canvas.mapToGlobal(QPoint(40,40)),QSize(20,20)),1);
    check(!background.isNull() && background.pixelColor(10,10).red()>200,
          "custom wallpaper reload uses content-aware decoder too");
    bool inspected = false;
    QTimer::singleShot(100, &canvas, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        auto *themeAction = menu->findChild<QAction *>("wallpaperThemeAction");
        check(themeAction != nullptr, "wallpaper sampling action is present");
        if (themeAction) {
            const QColor before = canvas.fontColor();
            themeAction->trigger();
            check(canvas.fontColor() == before, "bright wallpaper tint cannot overwrite white labels");
            canvas.setFontColor(QColor("#273344"));
            themeAction->trigger();
            check(canvas.fontColor() == QColor("#273344"), "wallpaper tint preserves explicit dark label preference");
        }
        auto *widgets = menu->findChild<QMenu *>("desktopWidgetsMenu");
        check(widgets && widgets->actions().size()==6, "six widget visibility toggles available");
        if (widgets) {
            for (auto *action : widgets->actions())
                check(action->isCheckable() && action->icon().isNull(), "all six widgets reserve the check column");
            for (const char *name : {"monitorWidgetAction", "smartSpaceWidgetAction"}) {
                auto *action = widgets->findChild<QAction *>(name);
                check(action && !action->isChecked(), "disabled widget starts unchecked");
                if (action) {
                    action->trigger();
                    check(action->isChecked(), "widget enable marks the action checked");
                    action->trigger();
                    check(!action->isChecked(), "widget disable clears the check");
                }
            }
            // Save a rendered menu for reviewing glyphs on the real stylesheet.
            for (auto *action : widgets->actions()) action->setChecked(true);
            widgets->popup(QPoint(700,100));settle(100);
            QDir().mkpath("artifacts");widgets->grab().save("artifacts/widgets-menu.png");
            widgets->hide();
        }
        check(menu->findChild<QAction *>("desktopHelpAction") && menu->findChild<QAction *>("desktopAboutAction"),
              "updated help and attribution are reachable from context menu");
        inspected = true;
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse,QPoint(50,50),canvas.mapToGlobal(QPoint(50,50)));
    QApplication::sendEvent(&canvas,&event);
    check(inspected, "desktop appearance actions inspected");
    return failures ? 1 : 0;
}
