#include "LiquidPopup.h"
#include "DesktopCanvas.h"
#include "FenceWidget.h"
#include "FenceGlassRenderer.h"
#include "SystemMonitor.h"
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QMenu>
#include <QListWidget>
#include <QContextMenuEvent>
#include <cmath>

static int failures = 0;
static void check(bool ok, const char *what)
{
    if (!ok) { ++failures; qCritical() << "FAIL:" << what; }
    else qInfo() << "PASS:" << what;
}
static void settle(int ms=80)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static QImage wallpaper(QSize logical, qreal dpr)
{
    QImage image(QSize(qRound(logical.width()*dpr), qRound(logical.height()*dpr)), QImage::Format_RGB32);
    image.setDevicePixelRatio(dpr);
    QPainter p(&image);
    QLinearGradient gradient(0, 0, logical.width(), logical.height());
    gradient.setColorAt(0, QColor("#aa654e"));
    gradient.setColorAt(0.45, QColor("#487486"));
    gradient.setColorAt(1, QColor("#211e43"));
    p.fillRect(QRect(QPoint(), logical), gradient);
    p.setPen(QPen(QColor("#d6afa0"), 4));
    for (int x=-logical.height(); x<logical.width(); x+=42)
        p.drawLine(x, 0, x+logical.height(), logical.height());
    return image;
}

#include "selection_drag_test.h"
#include "fence_features_test.h"
#include "smart_wallpaper_test.h"
#include "desklet_test.h"
#include "music_test.h"
#include "calendar_test.h"
#include "appearance_test.h"
#include "settings_center_test.h"
#include "icon_appearance_test.h"
#include "clipboard_test.h"
#include "folder_drop_undo_test.h"
#include "icon_damage_test.h"
#include "smart_interaction_test.h"
#include "review_async_test.h"
#include "monitor_placement_test.h"
#include "monitor_diagnosis_test.h"
#include "startup_wallpaper_test.h"
#include "menu_shortcut_test.h"

int main(int argc, char **argv)
{
    // Must run under a separate X server and session bus (CTest does this).
    QTemporaryDir isolated("/tmp/fences-global-glass-test-XXXXXX");
    if (!isolated.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", (isolated.path()+"/config").toUtf8());
    qputenv("XDG_DATA_HOME", (isolated.path()+"/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (isolated.path()+"/cache").toUtf8());
    qputenv("UKUI_FENCES_SMARTSPACE_ROOTS", isolated.path().toUtf8());
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
    QApplication app(argc, argv);
    LiquidPopup::installMenuGlyphStyle(app);
    LiquidPopup::install(app);
    app.setOrganizationName("kylin");
    app.setApplicationName("ukui-fences");
    if (app.arguments().contains("--menu-shortcut-only"))
        return runMenuShortcutTest();
    if (app.arguments().contains("--startup-wallpaper-only"))
        return runStartupWallpaperTest(isolated.path());
    if (app.arguments().contains("--monitor-placement-only"))
        return runMonitorPlacementTest();
    if (app.arguments().contains("--monitor-diagnosis-only"))
        return SystemMonitorTestAccess::run();
    if (app.arguments().contains("--review-async-only"))
        return runReviewAsyncTest(isolated.path());
    if (app.arguments().contains("--smart-interaction-only"))
        return runSmartInteractionTest(isolated.path());
    if (app.arguments().contains("--icon-damage-only"))
        return runIconDamageTest(isolated.path());
    if (app.arguments().contains("--folder-drop-undo-only"))
        return runFolderDropUndoTest(isolated.path());
    if (app.arguments().contains("--clipboard-only"))
        return runClipboardTest(isolated.path());
    if (app.arguments().contains("--icon-appearance-only"))
        return runIconAppearanceTest(isolated.path());
    if (app.arguments().contains("--settings-center-only"))
        return runSettingsCenterTest(isolated.path());
    if (app.arguments().contains("--appearance-only"))
        return runAppearanceTest(isolated.path());
    if (app.arguments().contains("--read-wallpaper")) {
        const int index = app.arguments().indexOf("--read-wallpaper") + 1;
        if (index >= app.arguments().size()) return 2;
        const QPixmap decoded = readWallpaperPixmap(app.arguments()[index]);
        check(!decoded.isNull(), "provided wallpaper decodes successfully");
        qInfo() << "Decoded size:" << decoded.size();
        return failures ? 1 : 0;
    }
    if (app.arguments().contains("--calendar-only"))
        return runCalendarTest(isolated.path());
    if (app.arguments().contains("--music-only"))
        return runMusicTest(isolated.path());
    if (app.arguments().contains("--desklets-only"))
        return runDeskletTest(isolated.path());
    if (app.arguments().contains("--wallpaper-only"))
        return runSmartWallpaperTest(isolated.path());
    if (app.arguments().contains("--selection-only"))
        return runSelectionDragTest(isolated.path());
    if (app.arguments().contains("--features-only"))
        return runFenceFeaturesTest(isolated.path());
    const bool cpu = qEnvironmentVariableIsSet("UKUI_FENCES_GLASS_NO_GL");
    const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : isolated.path();
    QDir().mkpath(output);

    FenceGlassRenderer renderer;
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        const QImage source = wallpaper(QSize(700, 460), dpr);
        renderer.setWallpaper(source);
        const int oldCount = renderer.preparationCount();
        const QImage panel = renderer.renderPanel(QRect(45, 60, 320, 260), 10);
        if (!cpu && !renderer.usedGpu()) {
            qWarning() << "SKIP: this display has no working OpenGL context; run --render-only on a GPU display";
            return 77;
        }
        check(renderer.usedGpu() != cpu, "requested GPU / CPU backend actually used");
        check(panel.size() == QSize(qRound(320*dpr), qRound(260*dpr)) && panel.devicePixelRatio()==dpr,
              "physical pixels and fractional DPR preserved");
        if (panel.isNull()) return 2;
        check(qAlpha(panel.pixel(0, 0)) < 5 && qRed(panel.pixel(0, 0)) < 5,
              "transparent corners contain no leaking RGB");
        check(qAlpha(panel.pixel(panel.width()/2, panel.height()/2)) == 255,
              "material body opaque after wallpaper sampling");
        check(renderer.preparationCount()==oldCount+1, "wallpaper diffused once");
        renderer.setWallpaper(source);
        check(renderer.renderPanel(QRect(45, 60, 320, 260), 10)==panel, "repeat render deterministic");
        const QImage moved = renderer.renderPanel(QRect(145, 80, 320, 260), 10);
        check(moved!=panel && renderer.preparationCount()==oldCount+1, "different fences share diffusion, sample their own position");
        check(!renderer.renderPanel(QRect(20, 20, 200, 34), 10).isNull(), "collapsed fence has safe optical radius");
        check(!renderer.renderPanel(QRect(-10, -10, 100, 80), 10).isNull(), "partially offscreen fence renders safely");
        int partial = 0;
        bool premultiplied = true;
        for (int y=0; y<qRound(12*dpr); ++y)
            for (int x=0; x<qRound(12*dpr); ++x) {
                const QRgb pixel=panel.pixel(x,y);
                if (qAlpha(pixel)>0 && qAlpha(pixel)<255) ++partial;
                premultiplied &= qRed(pixel)<=qAlpha(pixel) && qGreen(pixel)<=qAlpha(pixel) && qBlue(pixel)<=qAlpha(pixel);
            }
        check(premultiplied, "edge pixels are premultiplied");
        check(partial>2, "curved edge has antialiased coverage");
        QPainterPath contour;
        contour.moveTo(25,0); contour.lineTo(200,0); contour.lineTo(200,140);
        contour.lineTo(30,140); contour.lineTo(5,100); contour.lineTo(35,70);
        contour.lineTo(8,30); contour.closeSubpath();
        QElapsedTimer opticalTimer;
        opticalTimer.start();
        const QImage shaped=renderer.renderPanel(QRect(30,60,200,140),10,contour);
        qInfo() << "Irregular render DPR" << dpr << opticalTimer.elapsed() << "ms";
        check(renderer.usedGpu()!=cpu, "irregular shape uses requested rendering backend");
        check(qAlpha(shaped.pixel(qRound(2*dpr),qRound(70*dpr)))==0
              && qAlpha(shaped.pixel(qRound(100*dpr),qRound(70*dpr)))==255,
              "irregular optical silhouette follows contour, not rectangle");
        shaped.save(output+QString("/irregular-%1.png").arg(dpr));
        panel.save(output+QString("/material-%1.png").arg(dpr));
    }
    renderer.setWallpaper({});
    check(renderer.renderPanel(QRect(0,0,200,100),10).isNull(), "cleared wallpaper never reuses stale content");
    renderer.setWallpaper(wallpaper(QSize(800,600),1.5));
    QPainterPath desktopContour;
    desktopContour.moveTo(24,0); desktopContour.lineTo(420,0); desktopContour.lineTo(420,240);
    for (int y=240; y>=0; y-=5) desktopContour.lineTo(15+10*std::sin(y/24.0),y);
    desktopContour.closeSubpath();
    QElapsedTimer desktopTimer; desktopTimer.start();
    renderer.renderPanel(QRect(20,20,420,240),10,desktopContour);
    qInfo() << "50-point desktop contour cold render (includes shared blur)" << desktopTimer.elapsed() << "ms";
    desktopTimer.restart();
    renderer.renderPanel(QRect(30,20,420,240),10,desktopContour);
    qInfo() << "50-point desktop contour warm move" << desktopTimer.elapsed() << "ms";
    if (app.arguments().contains("--render-only")) return failures ? 1 : 0;

    QSettings settings;
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    const QString wallPath=isolated.path()+"/wallpaper.png";
    wallpaper(QSize(1440,900),1).save(wallPath);
    QDir().mkpath(isolated.path()+"/config/kyfences");
    QFile layout(isolated.path()+"/config/kyfences/layout.json");
    layout.open(QIODevice::WriteOnly);
    const QJsonObject irregular{{"id","irregular-fixture"}, {"title","磁吸轮廓"},
        {"x",240}, {"y",580}, {"w",320}, {"h",160}, {"color","#5a144664"},
        {"magneticEdge",1}, {"magneticContour",QJsonArray{
            QJsonArray{24,0},QJsonArray{4,35},QJsonArray{30,75},QJsonArray{6,120},QJsonArray{24,160}}}};
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath", wallPath}, {"wallpaperMode", 2}, {"fences", QJsonArray{irregular}}}).toJson());
    layout.close();
    const QString fixture=isolated.path()+"/清晰文字测试.txt";
    { QFile f(fixture); f.open(QIODevice::WriteOnly); f.write("test"); }
    {
        DesktopCanvas canvas;
        auto *magnetic=canvas.findChild<FenceWidget *>();
        check(magnetic!=nullptr, "magnetic contour loaded from fixture");
        if (!magnetic) return 2;
        const QRegion originalMask=magnetic->mask();
        check(!canvas.fenceLiquidGlassEnabled(), "new install keeps original style by default");
        auto *first=canvas.createFence("项目与服务 · 液态玻璃", QRect(240,180,350,340));
        auto *second=canvas.createFence("演示与素材", QRect(640,180,320,340));
        first->setFenceColor(QColor(20,70,100,90));
        first->addItem(DesktopItem::fromPath(fixture));
        const QColor original=first->fenceColor();
        const QRect geo=first->geometry();
        QCursor::setPos(canvas.mapToGlobal(QPoint(1200,750)));
        settle();
        const QImage before=canvas.grab(QRect(225,165,755,375)).toImage();
        const QImage magneticBefore=canvas.grab(QRect(225,565,350,190)).toImage();
        before.save(output+"/fences-original.png");
        canvas.setFenceLiquidGlassEnabled(true);
        check(first->liquidGlassEnabled() && second->liquidGlassEnabled(), "global toggle reaches existing fences");
        check(magnetic->liquidGlassEnabled() && !magnetic->mask().isEmpty(), "irregular fence enabled without losing shaped hit area");
        auto *third=canvas.createFence("新分区", QRect(1000,180,230,280));
        check(third->liquidGlassEnabled(), "new fences inherit enabled setting");
        check(QSettings().value("appearance/fenceLiquidGlass").toBool(), "global setting persisted");
        QElapsedTimer preparationWait; preparationWait.start();
        QImage glass;
        do { settle(30); glass=canvas.grab(QRect(225,165,755,375)).toImage(); }
        while (glass==before && preparationWait.elapsed()<3000);
        glass.save(output+"/fences-liquid-glass.png");
        const QImage magneticGlass=canvas.grab(QRect(225,565,350,190)).toImage();
        magneticGlass.save(output+"/fences-magnetic.png");
        check(magneticGlass!=magneticBefore, "global setting changes magnetic fence material too");
        check(glass!=before, "enabling changes rendered material");
        QCursor::setPos(first->mapToGlobal(QPoint(3,120)));
        QEvent enter(QEvent::Enter); QApplication::sendEvent(first, &enter);
        settle(350);
        const QImage hover=canvas.grab(QRect(225,165,755,375)).toImage();
        hover.save(output+"/fences-hover.png");
        check(hover!=glass, "pointer near edge adds local liquid reflection");
        QCursor::setPos(canvas.mapToGlobal(QPoint(1200,750)));
        settle(800);
        check(canvas.grab(QRect(225,165,755,375)).toImage()==glass, "leaving clears reflection without changing cached material");
        first->move(260,210);
        settle();
        check(!first->grab().isNull(), "move invalidates position-dependent panel cache");
        first->setGeometry(geo);
        first->setCollapsed(true);
        settle(240);
        check(!first->grab().isNull(), "collapsed widget renders");
        first->setCollapsed(false);
        settle(240);
        first->setGeometry(geo);
        canvas.setFenceLiquidGlassEnabled(false);
        settle();
        check(!first->liquidGlassEnabled() && !second->liquidGlassEnabled() && !third->liquidGlassEnabled(), "off applies globally");
        check(first->fenceColor()==original, "original colour preserved");
        check(first->geometry()==geo, "original layout preserved");
        check(first->hasItem(fixture), "files preserved");
        check(canvas.grab(QRect(225,165,755,375)).toImage()==before, "off restores original pixels");
        check(magnetic->mask()==originalMask, "off restores original magnetic hit mask");
        check(canvas.grab(QRect(225,565,350,190)).toImage()==magneticBefore, "off restores magnetic fence pixels");

        canvas.showSystemMonitorWidget();
        auto *monitor=canvas.findChild<SystemMonitor *>("ukui-fences-monitor");
        check(monitor!=nullptr, "system monitor remains integrated as a desktop widget");
        if (!monitor) return 2;
        monitor->setSkin(SystemMonitor::Skin::Dark);
        settle();
        const QImage monitorOriginal=monitor->grab().toImage();
        monitor->setSkin(SystemMonitor::Skin::Liquid);
        settle();
        const QImage monitorLiquid=monitor->grab().toImage();
        monitorLiquid.save(output+"/system-monitor-liquid.png");
        check(monitorLiquid!=monitorOriginal, "system monitor liquid skin renders wallpaper refraction");
        check(QSettings().value("systemMonitor/skin").toInt()==5, "system monitor skin persists globally");
        monitor->setSkin(SystemMonitor::Skin::Dark);
        settle();
        const QImage restoredMonitor=monitor->grab().toImage();
        check(monitor->skin()==SystemMonitor::Skin::Dark &&
              restoredMonitor.pixelColor(restoredMonitor.width()/2,8)==monitorOriginal.pixelColor(monitorOriginal.width()/2,8),
              "system monitor restores original skin and background despite changing live statistics");
        canvas.toggleDesklet();

        // Open the actual desktop context menu; ensure the visible setting is wired.
        bool menuFound=false;
        QTimer::singleShot(100, [&] {
            if (auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                auto labels = [](QMenu *target) {
                    QStringList result;
                    if (target) for (auto *a : target->actions())
                        if (!a->isSeparator()) result << a->text();
                    return result;
                };
                check(labels(menu) == QStringList({"新建", "粘贴", "撤销上一步", "刷新桌面", "打开文件管理器", "编辑分区布局",
                    "排列与布局", "桌面小组件", "Fences 设置…"}),
                    "desktop menu has nine ordered entries");
                auto *edit=menu->findChild<QAction *>("layoutEditAction");
                check(edit && !edit->isChecked(),"layout action starts unlocked for activation");
                if(edit){
                    edit->trigger();
                    check(canvas.globalEditMode() && edit->isChecked() && edit->text()==QStringLiteral("退出布局编辑"),
                          "editing menu changes label to exit layout editing");
                    edit->trigger();
                    check(!canvas.globalEditMode() && !edit->isChecked() && edit->text()==QStringLiteral("编辑分区布局"),
                          "leaving layout editing restores its action label");
                }
                check(!menu->findChild<QAction *>("liquidPopupPreviewAction"),
                      "unused liquid preview is absent from desktop menu");
                auto *widgets=menu->findChild<QMenu *>("desktopWidgetsMenu");
                check(widgets && widgets->actions().size()==6 && !menu->findChild<QMenu *>("desktopStartupMenu"),
                      "visibility stays in menu while startup moves to unified settings");
                auto *layout=menu->findChild<QMenu *>("desktopLayoutMenu");
                check(labels(layout)==QStringList({"手动排列", "按名称排列", "按类型排列", "按修改时间排列"}),
                    "sorting and layout controls are grouped");
                if (layout && layout->actions().size()>=4) {
                    auto *manual=layout->actions()[0];
                    auto *byName=layout->actions()[1];
                    manual->trigger(); byName->trigger();
                    check(byName->isChecked() && !manual->isChecked(), "sorting choices are exclusive");
                    manual->trigger();
                    check(manual->isChecked() && !byName->isChecked(), "manual sorting can be restored");
                }
                menu->grab().save(output+"/desktop-menu.png");
                auto *action=menu->findChild<QAction *>("fencesSettingsAction");
                if (action) { menuFound=true; action->trigger(); }
                menu->close();
            }
        });
        QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(100,100), canvas.mapToGlobal(QPoint(100,100)));
        QApplication::sendEvent(&canvas, &context);
        FencesSettingsWindow *window=nullptr;
        for(auto *w:QApplication::topLevelWidgets())if(auto *f=qobject_cast<FencesSettingsWindow *>(w))window=f;
        check(menuFound && window,"desktop menu opens unified settings");
        if(window){
            window->openPage("layout");
            auto *magnet=window->findChild<QCheckBox *>("wallpaperMagnet");
            check(magnet && magnet->isChecked(),"unified layout contains magnetic contour toggle");
            if(magnet){magnet->click();check(!canvas.wallpaperMagnetEnabled() && magnetic->mask().isEmpty(),"settings toggle removes magnetic mask");magnet->click();}
            window->openPage("appearance");
            auto *glass=window->findChild<QCheckBox *>("fenceLiquidGlass");
            check(glass && !glass->isChecked(),"glass toggle reflects disabled state");
            if(glass)glass->click();
            check(canvas.fenceLiquidGlassEnabled(),"unified settings enables and persists glass");
            window->close();settle();
        }
        canvas.saveLayout();
    }
    {
        DesktopCanvas reloaded;
        check(reloaded.fenceLiquidGlassEnabled(), "restart restores enabled preference");
        const auto fences=reloaded.findChildren<FenceWidget *>();
        check(fences.size()==4, "restart preserves all fences");
        for (auto *f : fences) check(f->liquidGlassEnabled(), "restored fences inherit global setting");
        reloaded.setFenceLiquidGlassEnabled(false);
    }
    check(!QSettings().value("appearance/fenceLiquidGlass").toBool(), "off preference persists too");
    qInfo() << "Fence glass failures:" << failures << "output:" << output;
    return failures ? 1 : 0;
}
