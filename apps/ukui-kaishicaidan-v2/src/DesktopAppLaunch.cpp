#include "DesktopAppLaunch.h"
#include "../../../shared/desktop-launch/DesktopLaunchCompat.h"
#include <QTextStream>

int launchDesktopFile(const QString &path)
{
    QTextStream output(stderr);
    QString error;
    if (launchDesktopFileCompat(path, &error))
        return 0;
    output << (error.isEmpty() ? QStringLiteral("应用启动失败。") : error)
           << '\n';
    return 1;
}
