#include "LiquidPopup.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include <QApplication>
#include <QDebug>
#include <QDBusError>
#include <QAction>
#include <QFile>
#include <QIcon>
#include <QMenu>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QStringList>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include "DesktopCanvas.h"
#include "MenuStyle.h"

namespace {

constexpr const char *kDbusService = "org.ukui.fences";
constexpr const char *kDbusPath = "/ukuiFences";
constexpr const char *kDbusInterface = "org.ukui.fences";

QIcon appIcon()
{
    QIcon icon = QIcon::fromTheme("ukui-fences");
    if (!icon.isNull())
        return icon;

    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/ukui-fences.svg",
        QCoreApplication::applicationDirPath() + "/../share/icons/hicolor/scalable/apps/ukui-fences.svg",
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path))
            return QIcon(path);
    }

    const QString installedIcon = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("icons/hicolor/scalable/apps/ukui-fences.svg"));
    if (!installedIcon.isEmpty())
        return QIcon(installedIcon);

    return QIcon::fromTheme("user-desktop");
}

bool callExistingInstance(const QString &method)
{
    QDBusInterface iface(kDbusService, kDbusPath, kDbusInterface,
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;

    const QDBusMessage reply = iface.call(method);
    return reply.type() != QDBusMessage::ErrorMessage;
}

void setupTrayIcon(DesktopCanvas &canvas, const QIcon &icon)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    auto *tray = new QSystemTrayIcon(icon, &canvas);
    tray->setToolTip("ukui-fences 桌面分区");

    auto *menu = new QMenu(&canvas);
    MenuStyle::applyVenturaContextMenu(menu);

    auto *actShow = menu->addAction(QIcon::fromTheme("view-restore"), "切回 Fences 桌面");
    QObject::connect(actShow, &QAction::triggered,
                     &canvas, &DesktopCanvas::showAndActivate);

    auto *actHide = menu->addAction(QIcon::fromTheme("user-desktop"), "切换到系统桌面");
    QObject::connect(actHide, &QAction::triggered,
                     &canvas, &DesktopCanvas::hideFences);

    auto *actEdit = menu->addAction(QIcon::fromTheme("document-edit"), "切换编辑模式");
    QObject::connect(actEdit, &QAction::triggered,
                     &canvas, &DesktopCanvas::toggleEditMode);

    auto *actRefresh = menu->addAction(
        QIcon::fromTheme("view-refresh"), "刷新桌面（强制同步）");
    QObject::connect(actRefresh, &QAction::triggered,
                     &canvas, &DesktopCanvas::refreshAll);

    auto *actMonitor = menu->addAction(
        QIcon::fromTheme("utilities-system-monitor"), "显示系统监控");
    QObject::connect(actMonitor, &QAction::triggered,
                     &canvas, &DesktopCanvas::showSystemMonitorWidget);

    auto *actSmartSpace = menu->addAction(
        QIcon::fromTheme("folder-saved-search"), "显示智能空间");
    QObject::connect(actSmartSpace, &QAction::triggered,
                     &canvas, &DesktopCanvas::showSmartSpaceWidget);

    menu->addSeparator();

    auto *actQuit = menu->addAction(QIcon::fromTheme("application-exit"), "退出 ukui-fences");
    QObject::connect(actQuit, &QAction::triggered,
                     &canvas, &DesktopCanvas::quitApp);

    tray->setContextMenu(menu);
    QObject::connect(tray, &QSystemTrayIcon::activated,
                     &canvas, [&canvas](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger ||
            reason == QSystemTrayIcon::DoubleClick) {
            canvas.showAndActivate();
        }
    });
    tray->show();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);

    QApplication app(argc, argv);
    BackgroundTask::ApplicationScope backgroundTasks;
    LiquidPopup::installMenuGlyphStyle(app);
    LiquidPopup::install(app);
    QApplication::setQuitOnLastWindowClosed(false);
    app.setApplicationName("ukui-fences");
    app.setApplicationDisplayName("ukui-fences");
    app.setOrganizationName("kylin");
    app.setApplicationVersion("0.5.0");
    app.setDesktopFileName("ukui-fences");

    app.setStyleSheet(MenuStyle::venturaContextMenuStyleSheet());

    const QIcon icon = appIcon();
    app.setWindowIcon(icon);

    const QStringList args = app.arguments();
    const QString existingMethod = args.contains("--quit") ? QStringLiteral("quitApp")
        : args.contains("--settings") ? QStringLiteral("showUnifiedSettings")
        : args.contains("--hide") ? QStringLiteral("hideFences")
        : args.contains("--edit") ? QStringLiteral("toggleEditMode")
        : args.contains("--system-monitor") ? QStringLiteral("showSystemMonitorWidget")
        : args.contains("--smart-space") ? QStringLiteral("showSmartSpaceWidget")
        : args.contains("--autostart") ? QStringLiteral("activateOnSessionStartup")
        : QStringLiteral("showAndActivate");

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(kDbusService)) {
        if (callExistingInstance(existingMethod)) return 0;
        qCritical() << "Cannot register or contact desktop service:" << bus.lastError().message();
        return 1;
    }

    if (args.contains("--quit"))
        return 0;

    DesktopCanvas canvas;
    if (!bus.registerObject(kDbusPath, &canvas, QDBusConnection::ExportScriptableSlots)) {
        qCritical() << "Cannot register desktop D-Bus object:" << bus.lastError().message();
        bus.unregisterService(kDbusService);
        return 1;
    }
    setupTrayIcon(canvas, icon);

    if (args.contains("--settings")) {
        canvas.showAndActivate();
        canvas.showUnifiedSettings();
    }
    else if (args.contains("--hide"))
        canvas.hideFences();
    else if (args.contains("--edit"))
        canvas.setEditModeDBus(true);
    else if (args.contains("--system-monitor")) {
        canvas.showAndActivate();
        canvas.showSystemMonitorWidget();
    }
    else if (args.contains("--smart-space")) {
        canvas.showAndActivate();
        canvas.showSmartSpaceWidget();
    }
    else if (args.contains("--autostart"))
        canvas.activateOnSessionStartup();
    else
        canvas.showAndActivate();

    return app.exec();
}
