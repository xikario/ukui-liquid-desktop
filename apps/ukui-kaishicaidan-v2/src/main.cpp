#include "LiquidPopup.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include <QApplication>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QDebug>

#include "StartMenu.h"
#include "StartButton.h"
#include "KeyInterceptor.h"
#include "TaskbarDetector.h"
#include "StartMenuTheme.h"

namespace {

#if defined(UKUI_KAISHICAIDAN_V2)
constexpr const char *kOurService   = "org.ukui.kaishicaidan.v2";
constexpr const char *kOurPath      = "/ukuiKaishicaidanV2";
constexpr const char *kLegacyUkuiMenuPath = "/ukuiKaishicaidan";
constexpr const char *kOurInterface = "org.ukui.kaishicaidan.v2";
constexpr const char *kUkuiMenuAliasService = "org.ukui.menu";
#else
constexpr const char *kOurService   = "org.ukui.kaishicaidan";
constexpr const char *kOurPath      = "/ukuiKaishicaidan";
constexpr const char *kOurInterface = "org.ukui.kaishicaidan";
constexpr const char *kUkuiMenuAliasService = "org.ukui.menu";
#endif

bool callExistingInstance(const QString &method)
{
    QDBusInterface iface(kOurService, kOurPath, kOurInterface,
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;
    iface.call(method);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    BackgroundTask::ApplicationScope backgroundTasks;
    LiquidPopup::install(app);
    QApplication::setQuitOnLastWindowClosed(false);
#if defined(UKUI_KAISHICAIDAN_V2)
    app.setApplicationName("ukui-kaishicaidan-v2");
#else
    app.setApplicationName("ukui-kaishicaidan");
#endif
    app.setApplicationDisplayName(QString::fromUtf8("开始菜单"));
    app.setOrganizationName("kylin");
    app.setApplicationVersion("0.1.0");

    app.setStyleSheet(StartMenuTheme::tooltipStyleSheet());

    const QStringList args = app.arguments();
    QString existingMethod = QStringLiteral("toggle");
    if (args.contains("--quit")) existingMethod = QStringLiteral("quitApp");
    else if (args.contains("--hide")) existingMethod = QStringLiteral("hideMenu");
    else if (args.contains("--show")) existingMethod = QStringLiteral("showMenu");

    QDBusConnection bus = QDBusConnection::sessionBus();

    // Check if our own instance is already running
    if (!bus.registerService(kOurService)) {
        callExistingInstance(existingMethod);
        return 0;
    }

    const bool ownsUkuiMenuAlias = bus.registerService(kUkuiMenuAliasService);
    if (!ownsUkuiMenuAlias)
        qWarning() << "org.ukui.menu alias is already owned; V2 will keep its private D-Bus name";

    if (args.contains("--quit"))
        return 0;

    StartMenu menu;

    bus.registerObject(kOurPath, &menu,
                       QDBusConnection::ExportScriptableSlots);
#if defined(UKUI_KAISHICAIDAN_V2)
    // Keep the legacy UKUI object path available for panel/session clients
    // that still call org.ukui.menu/ukuiKaishicaidan.
    if (!bus.registerObject(kLegacyUkuiMenuPath, &menu,
                            QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "Failed to register legacy org.ukui.menu object path";
    }
#endif

    KeyInterceptor keyInterceptor(&menu);
    app.installNativeEventFilter(&keyInterceptor);
    keyInterceptor.grabWinKey();

    QObject::connect(&keyInterceptor, &KeyInterceptor::winKeyPressed,
                     &menu, &StartMenu::toggle);

    StartButton startButton(&menu);
    QTimer::singleShot(200, &startButton, &StartButton::positionOnTaskbar);
    QTimer::singleShot(400, &startButton, &StartButton::applyX11Immunity);
    // positionOnTaskbar shows the overlay only after a real panel is detected.

    if (args.contains("--show"))
        QTimer::singleShot(0, &menu, &StartMenu::showMenu);

    qDebug() << "ukui-kaishicaidan V2 started as the active start-menu overlay."
             << "org.ukui.menu alias:" << ownsUkuiMenuAlias;

    return app.exec();
}
