#include "StartMenu.h"
#include "SettingsDialog.h"
#include <QFontComboBox>
#include <QGroupBox>
#include <QSettings>
#include <QStandardPaths>
#include "NextKdeGlassView.h"
#include "BackgroundTask.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QCursor>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <X11/Xlib.h>
#undef KeyPress
#undef KeyRelease

// Like the production XRecord backend, use the available libXtst runtime
// without requiring extension development headers on the target machine.
extern "C" int XTestFakeButtonEvent(Display *, unsigned int, Bool, unsigned long);

static void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

class PaintProbe : public StartMenu {
public:
    int paints = 0, largestDamage = 0;
    void reset() { paints = largestDamage = 0; }
protected:
    void paintEvent(QPaintEvent *event) override {
        int area = 0;
        for (const QRect &r : event->region()) area += r.width() * r.height();
        largestDamage = qMax(largestDamage, area);
        ++paints;
        StartMenu::paintEvent(event);
    }
};

class TilePaintProbe : public QObject {
public:
    int paints = 0;
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
    }
};

class StartMenuPointerTest {
public:
    static int run() {
        int failures = 0;
        auto check = [&](bool ok, const char *message) {
            if (ok) qInfo() << "PASS:" << message;
            else { ++failures; qCritical() << "FAIL:" << message; }
        };
        auto waitIdle = [](StartMenu &menu) {
            QElapsedTimer wait; wait.start();
            while (menu.m_glassLightTimer->isActive() && wait.elapsed() < 3000) settle(15);
        };
        auto move = [](StartMenu &menu, QWidget *receiver, QPoint local) {
            const QPoint global = receiver->mapToGlobal(local);
            QMouseEvent event(QEvent::MouseMove, local, global,
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(receiver, &event);
            return QPointF(menu.mapFromGlobal(global));
        };
        QCursor::setPos(QPoint(1800, 30));
        QWidget desktop;
        desktop.setStyleSheet("background:#267654");
        desktop.setGeometry(QGuiApplication::primaryScreen()->geometry());
        desktop.show();settle(30);
        PaintProbe menu;
        menu.applySkin(Skin::EcoLiquid);
        menu.showMenu();
        check(!menu.isVisible() && menu.m_showPending,
              "first opening stays unmapped while its actual desktop material is prepared");
        menu.toggle();
        check(!menu.m_showPending && !menu.isVisible(),"toggle cancels a pending first opening");
        settle(500);
        check(!menu.isVisible(),"completed canceled capture cannot reopen the launcher");
        menu.m_lastBackdrop={};
        menu.showMenu();
        QElapsedTimer opening;opening.start();
        while(menu.m_showPending && opening.elapsed()<5000)settle(10);
        check(menu.isVisible() && !menu.m_nextKdeGlassView->image().isNull()
            && !menu.m_lastBackdrop.isNull(),"production opening maps only with the current prepared material");
        check(menu.m_lastBackdrop.pixelColor(menu.m_lastBackdrop.rect().center())==QColor("#267654"),
              "first material samples the visible desktop window rather than the root wallpaper");
        settle(650);
        check(menu.m_outsideTimer && menu.m_outsideTimer->isActive()
                  && !menu.m_taskbarCacheTimer.isValid(),
              "stationary launcher never scans taskbar geometry without a mouse press");
        auto *pointerDisplay = XOpenDisplay(nullptr);
        check(pointerDisplay, "native pointer connection is available for outside-click regression");
        if (pointerDisplay) {
            XTestFakeButtonEvent(pointerDisplay, 1, True, CurrentTime);
            XSync(pointerDisplay, False);
            settle(200);
            const bool closed = !menu.isVisible();
            XTestFakeButtonEvent(pointerDisplay, 1, False, CurrentTime);
            XSync(pointerDisplay, False);
            XCloseDisplay(pointerDisplay);
            check(closed, "real desktop mouse press still closes the idle launcher");
        }
        menu.hideMenu();
        desktop.setStyleSheet("background:#b85438");settle(30);
        menu.showMenu();
        check(menu.m_showPending && !menu.isVisible(),"changed desktop never exposes a stale material while remapping");
        opening.restart();
        while(menu.m_showPending && opening.elapsed()<5000)settle(10);
        check(menu.m_lastBackdrop.pixelColor(menu.m_lastBackdrop.rect().center())==QColor("#b85438"),
              "subsequent opening refreshes actual desktop pixels");
        menu.hideMenu();desktop.hide();
        menu.show(); menu.move(80, 80);
        QElapsedTimer scan; scan.start();
        while (menu.m_appScanBusy && scan.elapsed() < 5000) settle(15);
        check(!menu.m_appScanBusy, "isolated application scan completes");
        for (int index = 0; index < 4; ++index) {
            int count = 0;
            bool usable = true;
            for (auto *button : menu.findChildren<QPushButton *>()) {
                if (button->toolTip() != QString("Fixture %1").arg(index)) continue;
                ++count;
                const QImage icon = button->icon().pixmap(38, 38).toImage();
                usable = usable && !icon.isNull();
                if (index == 3 && !icon.isNull())
                    usable = usable && icon.pixelColor(icon.rect().center()) == QColor("#48c9db");
            }
            check(count >= 2 && usable,
                  index == 3 ? "real pinned/recent tiles preserve a readable local application icon"
                             : "real pinned/recent tiles fall back to visible icons for empty, corrupt or directory icon paths");
        }
        QImage background(menu.size() * menu.devicePixelRatioF(), QImage::Format_RGB32);
        background.setDevicePixelRatio(menu.devicePixelRatioF());
        for (int y = 0; y < background.height(); ++y)
            for (int x = 0; x < background.width(); ++x)
                background.setPixelColor(x, y, QColor(30 + x % 140, 50 + y % 120, 90));
        menu.m_nextKdeGlassView->setBackdropFast(background);
        menu.update(); settle(500); waitIdle(menu);
        menu.reset(); settle(250);
        check(!menu.m_glassLightTimer->isActive() && menu.paints == 0,
              "visible stationary menu stops reflection timer and repainting");

        QWidget *tile = nullptr;
        for (auto *button : menu.findChildren<QPushButton *>())
            if (button->toolTip().startsWith("Fixture")) { tile = button; break; }
        check(tile, "real pinned application tile exists");
        if (tile) {
            QEvent enter(QEvent::Enter); QApplication::sendEvent(tile, &enter);
            settle(250);
            TilePaintProbe tileProbe; tile->installEventFilter(&tileProbe);
            menu.reset();
            QPointF latest;
            for (int i = 0; i < 200; ++i)
                latest = move(menu, tile, QPoint(25 + i % 25, 30));
            check(menu.m_glassLightTarget == latest,
                  "pointer events over a child update the reflection target");
            waitIdle(menu); settle(50);
            check(tileProbe.paints == 0,
                  "moving inside a settled app tile does not redraw its icon or label");
            check(menu.m_glassLightPos == latest && !menu.m_glassLightTimer->isActive(),
                  "reflection converges to latest event and then stops");
            check(menu.largestDamage < menu.width() * menu.height() / 3,
                  "moving reflection invalidates bounded rim areas");
            tile->removeEventFilter(&tileProbe);
            QEvent leave(QEvent::Leave); QApplication::sendEvent(tile, &leave);
            settle(250);
        }

        menu.reset();
        const int controlRenders = menu.m_nextKdeGlassView->controlRenderCount();
        QPointF latest;
        for (int i = 0; i < 250; ++i)
            latest = move(menu, menu.m_searchEdit, QPoint(40 + i % 250, 15));
        waitIdle(menu); settle(50);
        check(menu.m_glassLightPos == latest && menu.paints <= 30,
              "search control movement is coalesced with reflection frames");
        check(menu.m_nextKdeGlassView->controlRenderCount() == controlRenders,
              "pointer motion reuses cached glass control material");
        check(menu.largestDamage < menu.width() * menu.height() / 3,
              "control glint does not repaint the entire menu");

        // Native X11 pixels, not QWidget::grab() which forces a clean redraw.
        for (const QPoint &point : {QPoint(8, 8), QPoint(30, 350), QPoint(660, 690)}) {
            QCursor::setPos(menu.mapToGlobal(point)); settle(15);
            move(menu, &menu, point); waitIdle(menu); settle(100);
            const QImage partial = qApp->primaryScreen()->grabWindow(menu.winId()).toImage();
            menu.update(); settle(100);
            const QImage full = qApp->primaryScreen()->grabWindow(menu.winId()).toImage();
            int difference = 255;
            if (!partial.isNull() && partial.size() == full.size()) {
                difference = 0;
                for (int y = 0; y < partial.height(); ++y)
                    for (int x = 0; x < partial.width(); ++x) {
                        const QColor a = partial.pixelColor(x, y), b = full.pixelColor(x, y);
                        difference = qMax(difference, qAbs(a.red() - b.red()));
                        difference = qMax(difference, qAbs(a.green() - b.green()));
                        difference = qMax(difference, qAbs(a.blue() - b.blue()));
                        difference = qMax(difference, qAbs(a.alpha() - b.alpha()));
                    }
            }
            // Qt's clipped antialiasing can round a channel differently by
            // 1-2/255. Larger differences catch stale glints and hover trails.
            if (difference > 2) {
                QDir().mkpath("artifacts");
                const QString stem = QString("artifacts/pointer-%1-%2-%3")
                    .arg(menu.devicePixelRatioF()).arg(point.x()).arg(point.y());
                partial.save(stem + "-partial.png"); full.save(stem + "-full.png");
            }
            qInfo() << "native partial/full maximum channel difference:" << difference;
            check(difference <= 2,
                  "local rim/rail repaint matches a full native-window redraw without trails");
        }

        move(menu, menu.m_searchEdit, QPoint(200, 15));
        menu.m_searchEdit->setFocus();
        QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        QApplication::sendEvent(menu.m_searchEdit, &key);
        check(menu.m_searchEdit->text() == "a", "pointer observer preserves keyboard text input");
        menu.applySkin(Skin::Dark);
        move(menu, &menu, QPoint(10, 10));
        check(!menu.m_glassLightTimer->isActive(), "switching to non-liquid skin stops reflection work");
        menu.applySkin(Skin::EcoLiquid); settle(300);
        move(menu, &menu, QPoint(25, 300));
        check(menu.m_glassLightTimer->isActive(), "fresh pointer movement resumes liquid reflection");
        auto &config=StartMenuConfig::instance();
        config.setPanelOpacity(92);
        const QString configPath=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui-kaishicaidan-v2/settings.conf";
        auto readConfig=[&]{QFile file(configPath);file.open(QIODevice::ReadOnly);return file.readAll();};
        const auto beforeSettings=readConfig();
        {
            SettingsDialog settings(&menu);settings.show();settle(260);
            check(settings.property("liquidDialogInstalled").toBool(),"launcher settings use the common surface");
            check(config.panelOpacity()==92 && readConfig()==beforeSettings,"opening settings never persists the nearest opacity choice");
            QMetaObject::invokeMethod(&settings,"apply");
            const auto afterApply=readConfig();
            const auto captures=settings.property("liquidDialogMaterialBuilds").toInt();
            QMetaObject::invokeMethod(&settings,"apply");
            check(readConfig()==afterApply && settings.property("liquidDialogMaterialBuilds").toInt()==captures,"reapplying unchanged settings does not rebuild or rewrite");
            settings.close();settle(200);
        }
        const auto beforeProductionSettings=readConfig();
        bool inspectedSettings=false;
        // Exercise the production slot and its nested modal loop. A direct
        // SettingsDialog construction cannot catch host entry/close regressions.
        QTimer::singleShot(260,&menu,[&] {
            auto *settings=qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            check(settings && settings->objectName()=="launcherSettingsDialog",
                  "launcher settings entry opens the real owned modal window");
            if(!settings)return;
            inspectedSettings=true;
            check(menu.m_settingsOpen && !menu.isVisible(),
                  "opening settings suspends the launcher while the modal window is active");
            const QImage settingsBackdrop = menu.m_lastBackdrop;
            menu.showMenu();
            menu.toggle();
            check(!menu.isVisible() && !menu.m_showPending
                      && menu.m_lastBackdrop == settingsBackdrop
                      && QApplication::activeModalWidget() == settings,
                  "show and Win-key requests keep the existing settings dialog reachable without opening or capturing the launcher");
            check(settings->windowFlags().testFlag(Qt::FramelessWindowHint)
                      && settings->windowType()==Qt::Window
                      && settings->windowFlags().testFlag(Qt::WindowMinimizeButtonHint)
                      && !settings->windowFlags().testFlag(Qt::WindowMaximizeButtonHint),
                  "settings use a normal taskbar window with minimize and no maximize capability");
            auto *header=settings->findChild<QWidget *>("liquidDialogTitlebar");
            auto *close=settings->findChild<QAbstractButton *>("liquidDialogClose");
            auto *minimize=settings->findChild<QAbstractButton *>("liquidDialogMinimize");
            check(header && header->isVisible() && close && close->isVisible()
                      && minimize && minimize->isVisible()
                      && !settings->findChild<QAbstractButton *>("liquidDialogMaximize"),
                  "real launcher settings expose minimize and close without a maximize control");
            auto *skin=settings->findChild<QGroupBox *>("launcherSkinGroup");
            auto *font=settings->findChild<QGroupBox *>("launcherFontGroup");
            if(header && skin && font) {
                const QRect headerRect(header->mapTo(settings,QPoint()),header->size());
                const QRect skinRect(skin->mapTo(settings,QPoint()),skin->size());
                const QRect fontRect(font->mapTo(settings,QPoint()),font->size());
                check(headerRect.bottom()<skinRect.top() && skinRect.bottom()<fontRect.top()
                          && settings->rect().contains(skinRect) && settings->rect().contains(fontRect),
                      "custom header reserves space without overlapping or clipping settings groups");
            } else check(false,"settings content groups are identifiable for geometry checks");
            for(const auto *name:{"launcherTheme","launcherOpacity","launcherFont","launcherFontSize"}) {
                auto *control=settings->findChild<QWidget *>(name);
                check(control && control->property("liquidControlButtons").toBool()
                          && control->findChild<QWidget *>("liquidControlGlyphs"),
                      "real launcher selectors and spin buttons use shared liquid control glyphs");
            }
            check(readConfig()==beforeProductionSettings,"opening settings through the real entry does not alter saved appearance");
            if(minimize) {
                minimize->click();settle(30);
                check(settings->isMinimized() && menu.m_settingsOpen,
                      "minimizing settings keeps the live modal window and launcher settings state");
                menu.showMenu();settle(30);
                check(!settings->isMinimized() && settings->isVisible()
                          && !menu.isVisible() && !menu.m_showPending,
                      "explicit launcher opening restores the same minimized settings window for continued editing");
            }
            QDir().mkpath("artifacts");
            settings->grab().save(QString("artifacts/launcher-settings-chrome-%1.png").arg(settings->devicePixelRatioF()));
            if(close)close->click();else settings->reject();
        });
        // Fail closed if a broken header leaves the production exec() running.
        QTimer modalTimeout;
        modalTimeout.setSingleShot(true);
        QObject::connect(&modalTimeout,&QTimer::timeout,&menu,[&] {
            check(false,"launcher settings close completes within the bounded modal timeout");
            if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();
        });
        modalTimeout.start(5000);
        menu.openSettings();
        modalTimeout.stop();
        check(inspectedSettings && !menu.m_settingsOpen && !QApplication::activeModalWidget(),
              "shared titlebar close exits the real settings loop and releases launcher settings state");
        menu.hide(); menu.reset(); settle(250);
        check(!menu.m_glassLightTimer->isActive() && menu.paints == 0,
              "hiding during animation stops timer and discards repaint work");
        menu.show(); settle(300);
        check(!menu.m_glassLightTimer->isActive(), "reopening has no stale reflection animation");
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir root;
    for (const QString &name : {"home", "config", "data", "cache", "data/applications"})
        QDir().mkpath(root.path() + '/' + name);
    qputenv("HOME", (root.path() + "/home").toUtf8());
    qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
    qputenv("XDG_DATA_DIRS", (root.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (root.path() + "/cache").toUtf8());
    qputenv("QT_STYLE_OVERRIDE", "Fusion");
    qputenv("GSETTINGS_SCHEMA_DIR", "/usr/share/glib-2.0/schemas");
    QApplication app(argc, argv);
    QApplication::setCursorFlashTime(0);
    app.setOrganizationName("kylin"); app.setApplicationName("ukui-kaishicaidan-v2");
    BackgroundTask::ApplicationScope tasks;
    LiquidPopup::install(app); LiquidPopup::installMenuGlyphStyle(app);
    const QString applications = root.path() + "/data/applications";
    QFile emptyIcon(applications + "/empty-icon.png");
    emptyIcon.open(QIODevice::WriteOnly);emptyIcon.close();
    QFile corruptIcon(applications + "/corrupt-icon.png");
    corruptIcon.open(QIODevice::WriteOnly);corruptIcon.write("not an image");corruptIcon.close();
    QDir().mkpath(applications + "/icon-directory");
    QImage goodIcon(40, 40, QImage::Format_ARGB32_Premultiplied);
    goodIcon.fill(QColor("#48c9db"));goodIcon.save(applications + "/good-icon.png");
    QStringList pinned;
    for (int i = 0; i < 12; ++i) {
        const QString path = root.path() + QString("/data/applications/fixture-%1.desktop").arg(i);
        QFile file(path); file.open(QIODevice::WriteOnly);
        QString icon = "utilities-terminal";
        if (i == 0) icon = applications + "/empty-icon.png";
        else if (i == 1) icon = "corrupt-icon.png";
        else if (i == 2) icon = "icon-directory";
        else if (i == 3) icon = "good-icon.png";
        file.write(QString("[Desktop Entry]\nType=Application\nName=Fixture %1\nExec=/bin/true\nIcon=%2\n").arg(i).arg(icon).toUtf8());
        pinned << path;
        if (i < 4) AppRegistry::recordLaunchedApp(path);
    }
    AppRegistry::savePinned(pinned);
    return StartMenuPointerTest::run();
}
