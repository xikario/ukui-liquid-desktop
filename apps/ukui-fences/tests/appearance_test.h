#pragma once
#include "FencesSettingsWindow.h"
#include <QPushButton>
#include "WallpaperImage.h"
#include "DesktopIcon.h"

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
    const auto cached=readWallpaperPixmap(imagePath);
    check(cached.cacheKey()==readWallpaperPixmap(imagePath).cacheKey(),"wallpaper decode cache reused");
    const QString largePath=root+"/large.jpg";
    QImage large(3200,1600,QImage::Format_RGB32);large.fill(Qt::blue);large.save(largePath,"JPEG");
    check(readWallpaperPixmap(largePath,nullptr,QSize(800,400)).size()==QSize(800,400),"large JPEG decoded to target size");
    check(readWallpaperPixmap(largePath,nullptr,QSize()).size()==large.size(),"native-size wallpaper modes preserve pixels");
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
    DesktopIcon probe(DesktopItem::fromPath(imagePath),&canvas);
    canvas.configureIconAppearance(&probe,IconSurface::Desktop);
    const QColor previousAccent=probe.liquidAccent();
    original.fill(QColor("#285acc"));original.save(imagePath,"JPEG");
    { QFile changed(imagePath);changed.open(QIODevice::ReadWrite);
      changed.setFileTime(QDateTime::currentDateTime().addSecs(2),QFileDevice::FileModificationTime); }
    canvas.refreshAll();
    QElapsedTimer refreshed; refreshed.start();
    while (probe.liquidAccent() == previousAccent && refreshed.elapsed() < 3000) settle(20);
    check(probe.liquidAccent()!=previousAccent,"wallpaper refresh updates existing icon tint without recreating icon");
    check(readWallpaperPixmap(imagePath).toImage().pixelColor(10,10).blue()>150,"same-path wallpaper replacement invalidates decoded cache");
    bool inspected = false;
    QTimer::singleShot(100, &canvas, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for(auto *action:menu->findChildren<QAction *>())
            check(!action->isIconVisibleInMenu(),"context menus hide decorative icons without losing toggle state");
        auto *settingsAction=menu->findChild<QAction *>("fencesSettingsAction");
        check(settingsAction && menu->actions().contains(settingsAction),"single settings entry is present at root");
        check(!menu->findChild<QMenu *>("desktopAppearanceMenu") && !menu->findChild<QMenu *>("desktopSettingsHelpMenu"),
              "old appearance and help submenus are merged");
        auto *fileManager=menu->findChild<QAction *>("desktopFileManagerAction");
        const int managerIndex=menu->actions().indexOf(fileManager);
        check(managerIndex>0 && menu->actions().at(managerIndex-1)->text()=="刷新桌面",
              "file manager sits directly below refresh desktop");
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
        inspected = true;
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse,QPoint(50,50),canvas.mapToGlobal(QPoint(50,50)));
    QApplication::sendEvent(&canvas,&event);
    check(inspected, "desktop appearance actions inspected");
    canvas.showSettingsPage("wallpaper");settle(100);
    FencesSettingsWindow *window=nullptr;
    for(auto *w:QApplication::topLevelWidgets())if(auto *candidate=qobject_cast<FencesSettingsWindow *>(w))window=candidate;
    auto *sample=window?window->findChild<QPushButton *>("wallpaperThemeButton"):nullptr;
    check(sample!=nullptr,"wallpaper sampling is reachable inside settings");
    if(sample){const QColor before=canvas.fontColor();sample->click();check(canvas.fontColor()==before,"wallpaper tint preserves label color");
        canvas.setFontColor(QColor("#273344"));sample->click();check(canvas.fontColor()==QColor("#273344"),"wallpaper tint preserves explicit dark labels");}
    return failures ? 1 : 0;
}
