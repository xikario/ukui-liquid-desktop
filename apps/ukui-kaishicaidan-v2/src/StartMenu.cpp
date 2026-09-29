#include <QImageReader>
#include "../../../shared/async-work/BackgroundTask.h"
#include "LiquidPopup.h"
#include "../../ukui-fences/src/LiquidIconRenderer.h"
#include "StartMenu.h"
#include "StartButton.h"
#include "TaskbarDetector.h"
#include "StartMenuTheme.h"
#include "AppRegistry.h"
#include "RecentFiles.h"
#include "SettingsDialog.h"
#include "NextKdeGlassView.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QSocketNotifier>
#include <QDebug>
#include <QCursor>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QToolTip>
#include <QWidgetAction>
#include <QDesktopServices>
#include <QUrl>
#include <QScrollBar>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QSet>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <limits>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QRegularExpression>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>

namespace {
    bool isX11Platform() {
        return QApplication::platformName().toLower().contains(QLatin1String("xcb"));
    }

// helper: read current active window from root (_NET_ACTIVE_WINDOW)
unsigned long activeWindowId(Display *dpy)
{
    if (!isX11Platform()) return 0;
    if (!dpy) return 0;
    const Atom active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    const Atom root = XDefaultRootWindow(dpy);
    Atom type = None; int fmt = 0; unsigned long n = 0, after = 0;
    unsigned char *data = nullptr;
    unsigned long id = 0;
    if (active != None &&
        XGetWindowProperty(dpy, root, active, 0, 1, False, XA_WINDOW,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        id = *reinterpret_cast<unsigned long *>(data);
        XFree(data);
    }
    return id;
}

QStringList x11WindowClassNames(Display *dpy, unsigned long windowId)
{
    QStringList names;
    if (!isX11Platform() || windowId == 0) return names;

    if (!dpy) return names;

    const Atom wmClassAtom = XInternAtom(dpy, "WM_CLASS", True);
    Atom type = None; int fmt = 0; unsigned long n = 0, after = 0;
    unsigned char *data = nullptr;

    if (wmClassAtom != None &&
        XGetWindowProperty(dpy, static_cast<Window>(windowId), wmClassAtom,
                           0, 256, False, XA_STRING,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        const QByteArray raw(reinterpret_cast<const char *>(data), n);
        for (const QByteArray &part : raw.split('\0')) {
            const QString text = QString::fromUtf8(part).trimmed().toLower();
            if (!text.isEmpty() && !names.contains(text))
                names.append(text);
        }
        XFree(data);
    }

    return names;
}

unsigned long x11WindowPid(Display *dpy, unsigned long windowId)
{
    if (!isX11Platform() || windowId == 0) return 0;

    if (!dpy) return 0;

    const Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", True);
    const Atom cardinal = XInternAtom(dpy, "CARDINAL", True);
    Atom type = None; int fmt = 0; unsigned long n = 0, after = 0;
    unsigned char *data = nullptr;
    unsigned long pid = 0;

    if (pidAtom != None &&
        XGetWindowProperty(dpy, static_cast<Window>(windowId), pidAtom,
                           0, 1, False, cardinal,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        pid = *reinterpret_cast<unsigned long *>(data);
        XFree(data);
    }

    return pid;
}

QString normalizedAppToken(QString value)
{
    value = QFileInfo(value.trimmed()).completeBaseName().toLower();
    value.remove(QRegularExpression(QStringLiteral("^(org|com|net|io)\\.")));
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    value = value.trimmed();
    while (value.startsWith(QLatin1Char('-'))) value.remove(0, 1);
    while (value.endsWith(QLatin1Char('-'))) value.chop(1);
    return value;
}

QString executableBaseFromExec(QString exec)
{
    exec.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    exec.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    exec.remove('\'');
    exec.remove('"');
    exec = exec.trimmed();
    if (exec.isEmpty()) return {};

    QStringList parts = exec.split(QRegularExpression(QStringLiteral("\\s+")),
                                   QString::SkipEmptyParts);
    while (!parts.isEmpty()) {
        const QString first = parts.first();
        if (first == QStringLiteral("env") || first == QStringLiteral("sh") ||
            first == QStringLiteral("bash") || first == QStringLiteral("-c") ||
            first.contains(QLatin1Char('='))) {
            parts.removeFirst();
            continue;
        }
        break;
    }

    if (parts.isEmpty()) return {};
    return normalizedAppToken(parts.first());
}

QString processExecutableName(unsigned long pid)
{
    if (pid == 0) return {};
    const QString exe = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    return normalizedAppToken(exe);
}

bool tokensMatch(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) return false;
    if (left == right) return true;
    return (left.size() >= 4 && right.startsWith(left + QLatin1Char('-'))) ||
           (right.size() >= 4 && left.startsWith(right + QLatin1Char('-')));
}

QString desktopPathForWindow(Display *dpy, const QList<AppEntry> &apps, unsigned long windowId)
{
    QStringList activeTokens = x11WindowClassNames(dpy, windowId);
    const QString procToken = processExecutableName(x11WindowPid(dpy, windowId));
    if (!procToken.isEmpty() && !activeTokens.contains(procToken))
        activeTokens.append(procToken);

    if (activeTokens.isEmpty())
        return {};

    QString bestPath;
    int bestScore = 0;
    for (const AppEntry &app : apps) {
        QStringList appTokens;
        appTokens << normalizedAppToken(QFileInfo(app.desktopPath).fileName())
                  << executableBaseFromExec(app.exec)
                  << normalizedAppToken(app.iconName)
                  << normalizedAppToken(app.name);
        appTokens.removeAll(QString());

        int score = 0;
        for (const QString &active : activeTokens) {
            const QString token = normalizedAppToken(active);
            for (const QString &appToken : appTokens) {
                if (tokensMatch(token, appToken))
                    score = qMax(score, token == appToken ? 10 : 6);
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestPath = app.desktopPath;
        }
    }

    return bestScore >= 6 ? bestPath : QString();
}

QPixmap loadUserAvatarPixmap()
{
    const QString user = QString::fromUtf8(qgetenv("USER")).trimmed();
    QStringList candidates;

    if (!user.isEmpty()) {
        QDBusInterface accounts(QStringLiteral("org.freedesktop.Accounts"),
                                QStringLiteral("/org/freedesktop/Accounts"),
                                QStringLiteral("org.freedesktop.Accounts"),
                                QDBusConnection::systemBus());
        const QDBusReply<QDBusObjectPath> userPath =
            accounts.call(QStringLiteral("FindUserByName"), user);
        if (userPath.isValid() && !userPath.value().path().isEmpty()) {
            QDBusInterface props(QStringLiteral("org.freedesktop.Accounts"),
                                 userPath.value().path(),
                                 QStringLiteral("org.freedesktop.DBus.Properties"),
                                 QDBusConnection::systemBus());
            const QDBusReply<QVariant> iconReply =
                props.call(QStringLiteral("Get"),
                           QStringLiteral("org.freedesktop.Accounts.User"),
                           QStringLiteral("IconFile"));
            if (iconReply.isValid()) {
                QVariant iconValue = iconReply.value();
                if (iconValue.canConvert<QDBusVariant>())
                    iconValue = iconValue.value<QDBusVariant>().variant();
                const QString iconPath = iconValue.toString().trimmed();
                if (!iconPath.isEmpty())
                    candidates << iconPath;
            }
        }
    }

    candidates << QDir::homePath() + "/.face"
               << QDir::homePath() + "/.face.icon";
    if (!user.isEmpty())
        candidates << QStringLiteral("/var/lib/AccountsService/icons/%1").arg(user);

    QDir iconDir(QStringLiteral("/var/lib/AccountsService/icons"));
    const QFileInfoList accountIcons =
        iconDir.entryInfoList(QDir::Files | QDir::Readable,
                              QDir::Time);
    for (const QFileInfo &icon : accountIcons) {
        if (!candidates.contains(icon.absoluteFilePath()))
            candidates << icon.absoluteFilePath();
    }

    for (const QString &path : candidates) {
        if (!QFileInfo(path).isReadable()) continue;
        QPixmap pix(path);
        if (!pix.isNull())
            return pix;
    }

    return {};
}

QImage loadSystemWallpaperImage()
{
    struct SchemaKey { const char *schema; const char *key; };
    static const SchemaKey candidates[] = {
        { "org.mate.background", "picture-filename" },
        { "org.ukui.SettingsDaemon.plugins.background", "picture-filename" },
        { "org.gnome.desktop.background", "picture-uri" },
        { "org.gnome.desktop.background", "picture-uri-dark" },
        { nullptr, nullptr }
    };

    for (int i = 0; candidates[i].schema; ++i) {
        QProcess proc;
        proc.start("gsettings",
            QStringList() << "get"
                          << candidates[i].schema
                          << candidates[i].key);
        if (!proc.waitForFinished(1000))
            continue;

        QString path = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (path.startsWith('\'')) path = path.mid(1);
        if (path.endsWith('\''))   path.chop(1);
        if (path.startsWith("file://"))
            path = QUrl(path).toLocalFile();

        if (path.isEmpty() || !QFile::exists(path))
            continue;

        QImageReader reader(path); reader.setAutoTransform(true); reader.setScaledSize(QSize(128,128));
        const QImage pix = reader.read();
        if (!pix.isNull())
            return pix;
    }
    return {};
}

QColor accentColorFromWallpaper(const QImage &wallpaper)
{
    if (wallpaper.isNull())
        return {};

    const QImage image = wallpaper
        .scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB32);

    double r = 0;
    double g = 0;
    double b = 0;
    double weightSum = 0;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgb(image.pixel(x, y));
            int h = 0;
            int s = 0;
            int l = 0;
            color.getHsl(&h, &s, &l);

            if (s < 35 || l < 35 || l > 230)
                continue;

            const double lightnessWeight =
                1.0 - qMin(1.0, qAbs(l - 145) / 145.0);
            const double weight = (s / 255.0) * (0.35 + lightnessWeight);
            r += color.red() * weight;
            g += color.green() * weight;
            b += color.blue() * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 0)
        return {};

    QColor accent(qRound(r / weightSum),
                  qRound(g / weightSum),
                  qRound(b / weightSum));
    int h = 0;
    int s = 0;
    int l = 0;
    accent.getHsl(&h, &s, &l);
    if (h < 0)
        return {};

    accent = QColor::fromHsl(h, qBound(95, s + 35, 225),
                             qBound(80, l, 170), 90);
    return accent;
}

bool runCommandForOutput(const QString &program, const QStringList &arguments,
                         QString *standardOutput, QString *standardError,
                         int timeoutMs = 3000)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(1000)) {
        if (standardError)
            *standardError = process.errorString();
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        if (standardError)
            *standardError = QString::fromUtf8("操作超时");
        return false;
    }
    if (standardOutput)
        *standardOutput = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (standardError)
        *standardError = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool pathIsInside(const QString &path, const QString &directory)
{
    const QString cleanPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QString cleanDir = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    if (!cleanDir.endsWith(QLatin1Char('/')))
        cleanDir += QLatin1Char('/');
    return cleanPath.startsWith(cleanDir);
}

QStringList localAppRoots()
{
    return {
        QDir::homePath() + "/应用",
        QDir::homePath() + "/Applications",
        QDir::homePath() + "/apps",
    };
}

bool isUserDesktopShortcut(const QString &desktopPath)
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QStringList shortcutDirs = {
        dataHome + "/applications",
        QDir::homePath() + "/Desktop",
        QDir::homePath() + "/桌面",
    };
    for (const QString &directory : shortcutDirs) {
        if (pathIsInside(desktopPath, directory))
            return true;
    }
    return false;
}

QString folderBundleForDesktop(const QString &desktopPath)
{
    const QString desktopName = QFileInfo(desktopPath).fileName();
    if (desktopName.isEmpty())
        return {};

    const bool mayUseMatchingBundle = isUserDesktopShortcut(desktopPath);
    for (const QString &rootPath : localAppRoots()) {
        QDir root(rootPath);
        if (!root.exists())
            continue;

        const QFileInfoList children = root.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
        for (const QFileInfo &child : children) {
            const QString bundleRoot = child.absoluteFilePath();
            const QString expectedDesktop = bundleRoot
                + "/usr/share/applications/" + desktopName;
            if (QDir::cleanPath(desktopPath) == QDir::cleanPath(expectedDesktop)
                || (mayUseMatchingBundle && QFileInfo::exists(expectedDesktop))) {
                return bundleRoot;
            }
        }
    }
    return {};
}

bool isSafeFolderBundle(const QString &folderPath)
{
    const QFileInfo folder(folderPath);
    if (!folder.exists() || !folder.isDir() || folder.isSymLink())
        return false;
    const QString parent = QDir::cleanPath(folder.absolutePath());
    for (const QString &root : localAppRoots()) {
        if (parent == QDir::cleanPath(QFileInfo(root).absoluteFilePath()))
            return true;
    }
    return false;
}

QString executablePathFromDesktopExec(QString exec)
{
    exec.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    exec.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    exec.remove(QLatin1Char('\''));
    exec.remove(QLatin1Char('"'));
    QStringList parts = exec.split(QRegularExpression(QStringLiteral("\\s+")),
                                   QString::SkipEmptyParts);
    while (!parts.isEmpty()) {
        const QString first = parts.first();
        if (first == QStringLiteral("env") || first.contains(QLatin1Char('='))) {
            parts.removeFirst();
            continue;
        }
        if (first == QStringLiteral("sh") || first == QStringLiteral("bash"))
            return {};
        break;
    }
    if (parts.isEmpty())
        return {};

    QString executable = parts.first();
    executable.remove(QRegularExpression(QStringLiteral("%[fFuUdDnNickvm]")));
    if (QFileInfo(executable).isAbsolute())
        return QFileInfo(executable).absoluteFilePath();
    return QStandardPaths::findExecutable(executable);
}

QString debPackageForFile(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)
        || QStandardPaths::findExecutable(QStringLiteral("dpkg-query")).isEmpty()) {
        return {};
    }

    QString output;
    if (!runCommandForOutput(QStringLiteral("dpkg-query"),
                             {QStringLiteral("-S"), path}, &output, nullptr, 2500)) {
        return {};
    }

    const QRegularExpression validPackage(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9+.-]*(?::[A-Za-z0-9-]+)?$"));
    for (const QString &line : output.split(QLatin1Char('\n'), QString::SkipEmptyParts)) {
        const int separator = line.indexOf(QStringLiteral(": "));
        if (separator <= 0)
            continue;
        const QString package = line.left(separator).trimmed();
        if (!validPackage.match(package).hasMatch())
            continue;

        QString status;
        if (runCommandForOutput(QStringLiteral("dpkg-query"),
                                {QStringLiteral("-W"),
                                 QStringLiteral("-f=${db:Status-Abbrev}"), package},
                                &status, nullptr, 1500)
            && status.startsWith(QStringLiteral("ii"))) {
            return package;
        }
    }
    return {};
}

bool isProtectedPackageName(QString package)
{
    package = package.section(QLatin1Char(':'), 0, 0).toLower();
    static const QSet<QString> protectedPackages = {
        QStringLiteral("apt"),
        QStringLiteral("dpkg"),
        QStringLiteral("peony"),
        QStringLiteral("peony-extensions"),
        QStringLiteral("policykit-1"),
        QStringLiteral("sudo"),
        QStringLiteral("systemd"),
        QStringLiteral("ukui-control-center"),
        QStringLiteral("ukui-desktop-environment"),
        QStringLiteral("ukui-greeter"),
        QStringLiteral("ukui-panel"),
        QStringLiteral("ukui-menu"),
        QStringLiteral("ukui-polkit"),
        QStringLiteral("ukui-power-manager"),
        QStringLiteral("ukui-session-manager"),
        QStringLiteral("ukui-settings-daemon"),
        QStringLiteral("ukui-window-switch"),
        QStringLiteral("ukwm"),
        QStringLiteral("kylin-desktop"),
        QStringLiteral("kylin-desktop-environment"),
    };
    if (protectedPackages.contains(package))
        return true;
    return package.startsWith(QStringLiteral("linux-image"))
        || package.startsWith(QStringLiteral("linux-base"));
}

bool isProtectedDebPackage(const QString &package)
{
    if (isProtectedPackageName(package))
        return true;

    QString metadata;
    if (!runCommandForOutput(QStringLiteral("dpkg-query"),
                             {QStringLiteral("-W"),
                              QStringLiteral("-f=${Essential}"), package},
                             &metadata, nullptr, 1500)) {
        return false;
    }
    // Third-party packages sometimes misuse Priority=required. Only Essential=yes
    // is authoritative here; desktop-critical UKUI packages are listed explicitly.
    return metadata.trimmed() == QStringLiteral("yes");
}

bool simulateDebRemoval(const QString &package, QStringList *removedPackages,
                        QString *errorMessage)
{
    QString output;
    QString error;
    const bool ok = runCommandForOutput(
        QStringLiteral("apt-get"),
        {QStringLiteral("-s"), QStringLiteral("-o"),
         QStringLiteral("Debug::NoLocking=true"), QStringLiteral("remove"),
         QStringLiteral("--"), package},
        &output, &error, 8000);
    if (!ok) {
        if (errorMessage)
            *errorMessage = error.isEmpty() ? output : error;
        return false;
    }

    QStringList packages;
    const QRegularExpression removalLine(QStringLiteral("^Remv\\s+(\\S+)"));
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = removalLine.match(line.trimmed());
        if (match.hasMatch() && !packages.contains(match.captured(1)))
            packages.append(match.captured(1));
    }
    if (removedPackages)
        *removedPackages = packages;
    return true;
}

QString conciseProcessError(QString error)
{
    error = error.trimmed();
    if (error.size() > 500)
        error = error.left(500) + QStringLiteral("...");
    return error;
}
} // namespace

static QString rgbaCss(const QColor &c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

static bool startFirstAvailable(const QList<QPair<QString, QStringList>> &commands)
{
    for (const auto &cmd : commands) {
        if (QStandardPaths::findExecutable(cmd.first).isEmpty())
            continue;
        if (QProcess::startDetached(cmd.first, cmd.second))
            return true;
    }
    return false;
}

static QIcon themedIcon(std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        QIcon icon = QIcon::fromTheme(QString::fromLatin1(name));
        if (!icon.isNull())
            return icon;
    }
    return QIcon();
}

static void applyAlpha(QColor &color, int opacity)
{
    color.setAlpha(qBound(0, 255 * opacity / 100, 255));
}

static QIcon generatePremiumIcon(const QString &appName)
{
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    unsigned int hash = 0;
    for (int i = 0; i < appName.size(); ++i) {
        hash = hash * 31 + appName.at(i).unicode();
    }
    
    QColor c1, c2;
    int themeIndex = hash % 4;
    if (themeIndex == 0) {
        c1 = QColor(6, 182, 212);
        c2 = QColor(59, 130, 246);
    } else if (themeIndex == 1) {
        c1 = QColor(139, 92, 246);
        c2 = QColor(236, 72, 153);
    } else if (themeIndex == 2) {
        c1 = QColor(16, 185, 129);
        c2 = QColor(20, 184, 166);
    } else {
        c1 = QColor(245, 158, 11);
        c2 = QColor(239, 68, 68);
    }

    QLinearGradient bgGrad(0, 0, 128, 128);
    bgGrad.setColorAt(0.0, c1);
    bgGrad.setColorAt(1.0, c2);

    QRectF baseRect(8, 8, 112, 112);
    p.setPen(Qt::NoPen);
    p.setBrush(bgGrad);
    p.drawRoundedRect(baseRect, 28, 28);

    p.setBrush(QColor(255, 255, 255, 18));
    p.drawEllipse(QRectF(36, 36, 96, 96));

    QLinearGradient gloss(0, 8, 0, 60);
    gloss.setColorAt(0.0, QColor(255, 255, 255, 45));
    gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(gloss);
    p.drawRoundedRect(QRectF(8, 8, 112, 52), 28, 28);

    QString letter = appName.isEmpty() ? QStringLiteral("?") : appName.left(1).toUpper();
    
    QFont f = p.font();
    f.setPixelSize(56);
    f.setBold(true);
    f.setFamily(QStringLiteral("sans-serif"));
    p.setFont(f);

    p.setPen(QColor(0, 0, 0, 50));
    p.drawText(QRectF(8, 10, 112, 112), Qt::AlignCenter, letter);

    p.setPen(Qt::white);
    p.drawText(baseRect, Qt::AlignCenter, letter);

    p.end();
    return QIcon(pixmap);
}

static QIcon iconForDesktopIcon(const QString &appName, const QString &iconName, const QString &desktopPath = QString())
{
    if (iconName.isEmpty())
        return generatePremiumIcon(appName);

    QFileInfo iconInfo(iconName);
    if (iconInfo.isAbsolute() && iconInfo.exists())
        return QIcon(iconName);

    if (!desktopPath.isEmpty()) {
        const QString localIcon = QFileInfo(desktopPath).absoluteDir().absoluteFilePath(iconName);
        if (QFile::exists(localIcon))
            return QIcon(localIcon);
    }

    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull() && !iconInfo.completeBaseName().isEmpty())
        icon = QIcon::fromTheme(iconInfo.completeBaseName());
    if (icon.isNull())
        icon = QIcon::fromTheme(iconName.toLower());
    if (icon.isNull() && QFile::exists(iconName))
        icon = QIcon(iconName);
    if (icon.isNull()) {
        const QString base = iconInfo.completeBaseName().isEmpty()
            ? iconName : iconInfo.completeBaseName();
        const QStringList roots = {
            QDir::homePath() + "/.local/share/icons/hicolor",
            "/usr/share/icons/hicolor",
            "/usr/share/pixmaps",
        };
        const QStringList patterns = {
            base + ".png", base + ".svg", base + ".xpm",
            base.toLower() + ".png", base.toLower() + ".svg", base.toLower() + ".xpm",
        };
        for (const QString &root : roots) {
            QDir dir(root);
            if (!dir.exists()) continue;
            const QFileInfoList files = dir.entryInfoList(patterns, QDir::Files);
            if (!files.isEmpty())
                return QIcon(files.first().absoluteFilePath());

            const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &subdir : subdirs) {
                QDir appsDir(subdir.absoluteFilePath() + "/apps");
                const QFileInfoList appIcons = appsDir.entryInfoList(patterns, QDir::Files);
                if (!appIcons.isEmpty())
                    return QIcon(appIcons.first().absoluteFilePath());
            }
        }
    }
    // Scalable theme engines can legitimately omit a fixed-size inventory.
    if (icon.isNull()) {
        return generatePremiumIcon(appName);
    }
    return icon;
}

// One static, cached style for every application, including newly discovered
// launchers. Reuse Fences' material and DPR-aware rasterization; no captures,
// shaders or extra timers are needed for an icon plate.
static void drawLauncherIcon(QPainter &p, const QIcon &icon, const QRectF &rect,
                             qreal dpr, bool liquid)
{
    if (liquid) {
        const qreal inset=rect.width()*4.0/38.0;
        LiquidIconRenderer::drawPlate(p,rect.adjusted(-inset,-inset,inset,inset),
                                     QColor(166,193,212),.42,dpr,false,false);
    }
    LiquidIconRenderer::drawIcon(p,rect,icon,dpr);
}

static QString appUseLabel(const AppEntry &app)
{
    if (app.lastUsed.isValid())
        return QString::fromUtf8("最近使用");
    if (app.installTime.isValid())
        return QString::fromUtf8("新安装");
    return QString::fromUtf8("应用");
}

constexpr int kMaxRailApps = 4;

static int railAppY(int index)
{
    return 122 + index * 52;
}

static int railDocumentsY()
{
    return StartMenuTheme::kPanelHeight - 258;
}

static int railClipboardY()
{
    return StartMenuTheme::kPanelHeight - 206;
}

static int railThemeY()
{
    return StartMenuTheme::kPanelHeight - 154;
}

static int railSettingsY()
{
    return StartMenuTheme::kPanelHeight - 102;
}

static int railPowerY()
{
    return StartMenuTheme::kPanelHeight - 50;
}

// Each control samples only the clean material image held by StartMenu.
class GlassSearchEdit final : public QLineEdit {
public:
    explicit GlassSearchEdit(QWidget *parent) : QLineEdit(parent) { setMouseTracking(true); }
protected:
    void paintEvent(QPaintEvent *e) override {
        bool glass=false;
        {
            QPainter p(this);
            if(auto *menu=qobject_cast<StartMenu *>(window()))
                glass=menu->paintGlassControl(p,this,QRectF(rect()).adjusted(1,1,-1,-1),
                    21,hasFocus() ? 1.0 : (underMouse() ? 0.6 : 0.0));
        }
        QLineEdit::paintEvent(e);
        if(glass) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(220,232,247,190),1.2));
            const int y=height()/2-8;
            p.drawEllipse(QRectF(17,y,13,13));
            p.drawLine(QPointF(27,y+11),QPointF(34,y+18));
        }
    }
    void enterEvent(QEvent *e) override { QLineEdit::enterEvent(e); update(); }
    void leaveEvent(QEvent *e) override { QLineEdit::leaveEvent(e); update(); }
    void mouseMoveEvent(QMouseEvent *e) override { QLineEdit::mouseMoveEvent(e); update(); }
};

// ── Custom app button for the pinned grid ─────────────────

class AppIconButton : public QPushButton {
public:
    AppIconButton(const SkinPalette *pal, const AppEntry &app, int index, QWidget *parent,
                  bool isRecent = false, bool draggable = false, bool reorderable = false)
        : QPushButton(parent), m_palette(pal), m_app(app), m_index(index),
          m_isRecent(isRecent), m_draggable(draggable), m_reorderable(reorderable)
    {
        setFixedSize(84, 88); // 略微放大，留足呼吸感
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(app.name);

        m_icon = iconForDesktopIcon(app.name, app.iconName, app.desktopPath);

        // 初始化 Hover 动画
        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(160);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value){
            m_hoverFactor = value.toReal();
            update();
        });
    }

    int appIndex() const { return m_index; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Cached Fences plate under the original app icon; keep existing input
        // feedback without adding a separately animated material.
        auto *menu = qobject_cast<StartMenu *>(window());
        const bool liquid = menu && menu->isLiquidTheme();
        if (!liquid && m_hoverFactor > 0.01) {
            p.save();
            p.setOpacity(m_hoverFactor);
            QPainterPath btnBg;
            btnBg.addRoundedRect(rect().adjusted(2, 2, -2, -2), 10, 10);
            p.fillPath(btnBg, m_palette->hoverBg);
            QLinearGradient sheen(0, 0, width(), height());
            sheen.setColorAt(0, QColor(255, 255, 255, 26));
            sheen.setColorAt(1, QColor(255, 255, 255, 2));
            p.fillPath(btnBg, sheen);
            p.setPen(QPen(QColor(255, 255, 255, 38), 0.7));
            p.drawPath(btnBg);
            p.restore();
        }

        // 根据 Hover 状态产生微小的垂直位移 (0 ~ -3 像素)
        const double offset = -3.0 * m_hoverFactor;

        const int iconW = 38;
        const int iconH = 38;
        const int iconX = (width() - iconW) / 2;
        const double iconY = 12.0 + offset;

        QRectF iconRect(iconX, iconY, iconW, iconH);
        if (isDown()) iconRect=iconRect.adjusted(1.5,2.5,-1.5,-0.5);
        drawLauncherIcon(p,m_icon,iconRect,devicePixelRatioF(),liquid);

        p.setPen(m_palette->textSecondary);
        QFont labelFont = font();
        labelFont.setPixelSize(m_isRecent ? 10 : 11);
        p.setFont(labelFont);

        const auto readableText = [this,&p](QRect rect, int flags, const QString &text) {
            // Only brighten readability under the label, not the entire glass.
            const auto *menu=qobject_cast<StartMenu *>(window());
            const qreal lum=menu ? menu->glassLuminanceAt(
                QRectF(mapTo(window(),rect.topLeft()),rect.size())) : 0;
            const int alpha=qRound(qBound(0.0,(lum-0.30)/0.30,1.0)*125);
            if (alpha>0) {
                // Follow glyphs rather than drawing a clipped dark pill under
                // each label. The original text remains the foreground pass.
                QPainterPath glyph;
                const QFontMetrics metrics=p.fontMetrics();
                const qreal x=rect.left()+(rect.width()-metrics.horizontalAdvance(text))*0.5;
                glyph.addText(QPointF(x,rect.top()+metrics.ascent()),p.font(),text);
                p.save();
                p.setPen(QPen(QColor(0,0,0,alpha),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                p.setBrush(QColor(0,0,0,alpha));
                p.drawPath(glyph);
                p.restore();
            }
            const QPen foreground=p.pen();
            p.setPen(QColor(0,0,0,185));
            p.drawText(rect.translated(0,1),flags,text);
            p.setPen(foreground);
            p.drawText(rect,flags,text);
        };
        if (m_isRecent) {
            const QRect labelRect(2, iconY + iconH + 7, width() - 4, 12);
            const QString elided = p.fontMetrics().elidedText(m_app.name, Qt::ElideRight, width() - 8);
            readableText(labelRect, Qt::AlignHCenter | Qt::AlignTop, elided);

            p.setPen(m_palette->textMuted);
            QFont timeFont = font();
            timeFont.setPixelSize(8);
            p.setFont(timeFont);
            const QRect timeRect(2, labelRect.bottom() + 2, width() - 4, 11);
            QString timeStr = m_app.lastUsed.isValid() ? RecentFiles::timeAgoString(m_app.lastUsed) : QString::fromUtf8("刚刚");
            readableText(timeRect, Qt::AlignHCenter | Qt::AlignTop, timeStr);
        } else {
            const QRect labelRect(0, iconY + iconH + 6, width(), 16);
            const QString elided = p.fontMetrics().elidedText(m_app.name, Qt::ElideRight, width() - 8);
            readableText(labelRect, Qt::AlignHCenter | Qt::AlignTop, elided);
        }
    }

    void enterEvent(QEvent *) override {
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverFactor);
        m_hoverAnim->setEndValue(1.0);
        m_hoverAnim->start();
    }

    void leaveEvent(QEvent *) override {
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverFactor);
        m_hoverAnim->setEndValue(0.0);
        m_hoverAnim->start();
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton)
            m_dragStartPos = e->pos();
        QPushButton::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        update(); // refresh the local specular/hover state while moving
        if (!m_draggable || !(e->buttons() & Qt::LeftButton)
            || (e->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
            QPushButton::mouseMoveEvent(e);
            return;
        }

        auto *mimeData = new QMimeData;
        mimeData->setData(QStringLiteral("application/x-ukui-kaishicaidan-app-path"),
                          m_app.desktopPath.toUtf8());
        if (m_reorderable) {
            mimeData->setData(QStringLiteral("application/x-ukui-kaishicaidan-pinned-index"),
                              QByteArray::number(m_index));
        }

        auto *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        QPixmap preview = grab();
        drag->setPixmap(preview);
        drag->setHotSpot(e->pos());

        setDown(false);
        drag->exec(Qt::MoveAction);
    }

private:
    const SkinPalette *m_palette;
    AppEntry m_app;
    int m_index = -1;
    QIcon m_icon;
    bool m_isRecent = false;
    bool m_draggable = false;
    bool m_reorderable = false;
    QPoint m_dragStartPos;

    // 动画状态
    QVariantAnimation *m_hoverAnim = nullptr;
    double m_hoverFactor = 0.0;
};

class PinnedGridContainer : public QWidget {
public:
    explicit PinnedGridContainer(const SkinPalette *pal, QWidget *parent = nullptr)
        : QWidget(parent), m_palette(pal)
    {
        setAcceptDrops(true);
    }

    std::function<void(int, int)> onAppDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasFormat(QStringLiteral("application/x-ukui-kaishicaidan-pinned-index")))
            e->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *e) override {
        if (!e->mimeData()->hasFormat(QStringLiteral("application/x-ukui-kaishicaidan-pinned-index")))
            return;
        m_dropIndex = nearestAppIndex(e->pos());
        update();
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent *e) override {
        m_dropIndex = -1;
        update();
        e->accept();
    }

    void dropEvent(QDropEvent *e) override {
        bool ok = false;
        const int fromIndex = e->mimeData()
            ->data(QStringLiteral("application/x-ukui-kaishicaidan-pinned-index"))
            .toInt(&ok);
        const int toIndex = m_dropIndex;
        m_dropIndex = -1;
        update();

        if (ok && toIndex >= 0) {
            e->setDropAction(Qt::MoveAction);
            e->accept();
            if (onAppDropped)
                onAppDropped(fromIndex, toIndex);
        }
    }

    void paintEvent(QPaintEvent *e) override {
        QWidget::paintEvent(e);
        AppIconButton *target = buttonForIndex(m_dropIndex);
        if (!target)
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = m_palette->accent;
        fill.setAlpha(28);
        QColor outline = m_palette->accent;
        outline.setAlpha(190);
        const QRect marker = target->geometry().adjusted(2, 2, -2, -2);
        p.setBrush(fill);
        p.setPen(QPen(outline, 2));
        p.drawRoundedRect(marker, 8, 8);
    }

private:
    AppIconButton *buttonForIndex(int index) const {
        const auto buttons = findChildren<AppIconButton *>(QString(), Qt::FindDirectChildrenOnly);
        for (AppIconButton *button : buttons) {
            if (button->appIndex() == index)
                return button;
        }
        return nullptr;
    }

    int nearestAppIndex(const QPoint &pos) const {
        const auto buttons = findChildren<AppIconButton *>(QString(), Qt::FindDirectChildrenOnly);
        int nearest = -1;
        qint64 nearestDistance = std::numeric_limits<qint64>::max();
        for (AppIconButton *button : buttons) {
            const QPoint delta = button->geometry().center() - pos;
            const qint64 distance = qint64(delta.x()) * delta.x() + qint64(delta.y()) * delta.y();
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = button->appIndex();
            }
        }
        return nearest;
    }

    const SkinPalette *m_palette;
    int m_dropIndex = -1;
};



// ── Recent file item ────────────────────────────────────

class RecentFileButton : public QPushButton {
public:
    RecentFileButton(const SkinPalette *pal, const RecentFileEntry &file, QWidget *parent)
        : QPushButton(parent), m_palette(pal), m_file(file)
    {
        setFixedHeight(50);
        setCursor(Qt::PointingHandCursor);
        setToolTip(file.path);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (m_hovered || isDown()) {
            QPainterPath bgPath;
            bgPath.addRoundedRect(rect(), 8, 8);
            p.fillPath(bgPath, m_palette->hoverBg);
            p.setPen(QPen(QColor(255, 255, 255, 12), 1));
            p.drawPath(bgPath);
        }

        const int iconX = 8;
        const int iconY = (height() - 40) / 2;
        const QRect iconRect(iconX, iconY, 40, 40);

        QPainterPath iconBg;
        iconBg.addRoundedRect(iconRect, 8, 8);
        p.fillPath(iconBg, QColor(255, 255, 255, 13));

        static QIcon fileIcon = QIcon::fromTheme(
            QStringLiteral("text-x-generic"),
            QIcon::fromTheme(QStringLiteral("document-open-recent")));
        if (!fileIcon.isNull()) {
            fileIcon.paint(&p, iconRect.adjusted(8, 8, -8, -8), Qt::AlignCenter);
        } else {
            // 兜底：画一个简单文档矢量图标，不依赖字体 emoji
            p.setPen(QPen(m_palette->textSecondary, 1.4));
            p.setBrush(Qt::NoBrush);
            QRectF r(iconRect.center().x()-8, iconRect.center().y()-10, 16, 20);
            QPainterPath path;
            path.moveTo(r.left(), r.top());
            path.lineTo(r.right()-5, r.top());
            path.lineTo(r.right(), r.top()+5);
            path.lineTo(r.right(), r.bottom());
            path.lineTo(r.left(), r.bottom());
            path.closeSubpath();
            p.drawPath(path);
        }

        const int textX = iconRect.right() + 10;
        const QRect nameRect(textX, iconY, width() - textX - 8, 18);
        p.setPen(m_palette->textPrimary);
        QFont nameFont = font();
        nameFont.setPixelSize(12);
        p.setFont(nameFont);
        const QString elidedName = p.fontMetrics().elidedText(
            m_file.name, Qt::ElideRight, nameRect.width());
        p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

        const QRect metaRect(textX, nameRect.bottom() + 2, width() - textX - 8, 16);
        p.setPen(m_palette->textMuted);
        QFont metaFont = font();
        metaFont.setPixelSize(10);
        p.setFont(metaFont);
        p.drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                   m_file.timeAgo + " · " + m_file.mimeType.section('/', -1).toUpper());
    }

    void enterEvent(QEvent *) override { m_hovered = true; update(); }
    void leaveEvent(QEvent *) override { m_hovered = false; update(); }

private:
    const SkinPalette *m_palette;
    RecentFileEntry m_file;
    bool m_hovered = false;
};

class RecentAppButton : public QPushButton {
public:
    RecentAppButton(const SkinPalette *pal, const AppEntry &app, QWidget *parent)
        : QPushButton(parent), m_palette(pal), m_app(app)
    {
        setFixedHeight(50);
        setCursor(Qt::PointingHandCursor);
        setToolTip(app.name);
        m_icon = iconForDesktopIcon(app.name, app.iconName, app.desktopPath);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (m_hovered || isDown()) {
            QPainterPath bgPath;
            bgPath.addRoundedRect(rect(), 8, 8);
            p.fillPath(bgPath, m_palette->hoverBg);
            p.setPen(QPen(QColor(255, 255, 255, 12), 1));
            p.drawPath(bgPath);
        }

        const int iconX = 8;
        const int iconY = (height() - 40) / 2;
        const QRect iconRect(iconX, iconY, 40, 40);
        if (!m_icon.isNull()) {
            const auto *menu=qobject_cast<StartMenu *>(window());
            drawLauncherIcon(p,m_icon,iconRect.adjusted(7,7,-7,-7),devicePixelRatioF(),
                             menu && menu->isLiquidTheme());
        } else {
            p.setPen(m_app.accent);
            QFont f = font();
            f.setPixelSize(20);
            f.setBold(true);
            p.setFont(f);
            p.drawText(iconRect, Qt::AlignCenter, m_app.name.left(1));
        }

        const int textX = iconRect.right() + 10;
        p.setPen(m_palette->textPrimary);
        QFont nameFont = font();
        nameFont.setPixelSize(12);
        p.setFont(nameFont);
        p.drawText(QRect(textX, iconY, width() - textX - 8, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_app.name, Qt::ElideRight, width() - textX - 8));

        p.setPen(m_palette->textMuted);
        QFont metaFont = font();
        metaFont.setPixelSize(10);
        p.setFont(metaFont);
        const QString meta = appUseLabel(m_app);
        p.drawText(QRect(textX, iconY + 22, width() - textX - 8, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, meta);
    }

    void enterEvent(QEvent *) override { m_hovered = true; update(); }
    void leaveEvent(QEvent *) override { m_hovered = false; update(); }

private:
    const SkinPalette *m_palette;
    AppEntry m_app;
    QIcon m_icon;
    bool m_hovered = false;
};

// ── Section header label ─────────────────────────────────

class SectionHeader : public QWidget {
public:
    SectionHeader(const SkinPalette *pal, const QString &title,
                  const QString &actionText = QString(), QWidget *parent = nullptr)
        : QWidget(parent), m_palette(pal), m_title(title), m_action(actionText)
    {
        setFixedHeight(32);
        setAutoFillBackground(false);
        if (!actionText.isEmpty()) {
            setMouseTracking(true);
        }
    }

    void setTitle(const QString &title) { m_title = title; update(); }
    void setAction(const QString &action) { m_action = action; if (!action.isEmpty()) setMouseTracking(true); update(); }

    std::function<void()> onActionClicked;   // 排序（保留原名兼容）
    std::function<void()> onBackClicked;     // 新增：返回
    bool m_showBack = false;                  // 是否显示返回

private:
    QRect m_backRect;     // 缓存：返回点击区
    QRect m_sortRect;     // 缓存：排序点击区

    void layoutActionRects() {
        m_backRect = QRect();
        m_sortRect = QRect();
        if (m_action.isEmpty()) return;

        QFont af = font(); af.setPixelSize(13);
        const QFontMetrics fm(af);
        const int padLR = 16, gap = 10;
        int x = width();

        // 排序胶囊：右侧
        const int sortW = fm.horizontalAdvance(m_action) + padLR * 2;
        m_sortRect = QRect(x - sortW, 0, sortW, height());
        x -= sortW;

        // 返回按钮：排序左侧
        if (m_showBack) {
            x -= gap;
            const int backW = 32;
            m_backRect = QRect(x - backW, 0, backW, height());
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::transparent);

        p.setPen(m_palette->textPrimary);
        QFont f = font();
        f.setPixelSize(13);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, 160, height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);

        layoutActionRects();
        if (m_sortRect.isValid()) {
            QFont af = font(); af.setPixelSize(13); p.setFont(af);

            // 返回
            if (m_backRect.isValid()) {
                const bool h = m_hovered && m_backRect.contains(m_mousePos);
                if (h) {
                    QColor hoverBg = m_palette->accent;
                    hoverBg.setAlpha(m_palette->isLight ? 24 : 38);
                    p.setPen(Qt::NoPen);
                    p.setBrush(hoverBg);
                    p.drawRoundedRect(m_backRect.adjusted(2, 3, -2, -3), 6, 6);
                }

                if(auto *menu=qobject_cast<StartMenu *>(window()))
                    menu->paintGlassControl(p,this,m_backRect.adjusted(2,3,-2,-3),9,
                                            h ? 1.0 : 0.0,m_pressedPart==1);
                const QPointF center = m_backRect.center();
                QPainterPath arrow;
                arrow.moveTo(center.x() + 6, center.y());
                arrow.lineTo(center.x() - 5, center.y());
                arrow.moveTo(center.x() - 5, center.y());
                arrow.lineTo(center.x(), center.y() - 5);
                arrow.moveTo(center.x() - 5, center.y());
                arrow.lineTo(center.x(), center.y() + 5);
                QPen arrowPen(h ? m_palette->accent : m_palette->textSecondary, 1.8);
                arrowPen.setCapStyle(Qt::RoundCap);
                arrowPen.setJoinStyle(Qt::RoundJoin);
                p.setBrush(Qt::NoBrush);
                p.setPen(arrowPen);
                p.drawPath(arrow);
            }
            // 排序胶囊（带细描边，科技感）
            const bool hs = m_hovered && m_sortRect.contains(m_mousePos);
            QRect pill = m_sortRect.adjusted(2, 3, -2, -3);

            QColor pillBg;
            if (hs) {
                pillBg = QColor(m_palette->accent.red(), m_palette->accent.green(),
                                m_palette->accent.blue(), 45);
            } else {
                if (m_palette->isLight) {
                    pillBg = QColor(100, 116, 139, 20);
                } else {
                    pillBg = QColor(255, 255, 255, 12);
                }
            }

            auto *menu=qobject_cast<StartMenu *>(window());
            if (!menu || !menu->paintGlassControl(p,this,pill,pill.height()/2.0,
                                                   hs ? 1.0 : 0.0,m_pressedPart==2)) {
                p.setPen(QPen(hs ? m_palette->accent : m_palette->panelBorder, 1.2));
                p.setBrush(pillBg);
                p.drawRoundedRect(pill, pill.height()/2.0, pill.height()/2.0);
            }
            p.setPen(hs ? m_palette->accent : m_palette->textSecondary);
            p.drawText(m_sortRect, Qt::AlignCenter, m_action);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        layoutActionRects();
        if(e->button()==Qt::LeftButton) {
            m_pressedPart=m_backRect.contains(e->pos()) ? 1 : (m_sortRect.contains(e->pos()) ? 2 : 0);
            if(m_pressedPart) { update(); e->accept(); return; }
        }
        QWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if(e->button()==Qt::LeftButton && m_pressedPart) {
            const int part=m_pressedPart;
            m_pressedPart=0;
            update();
            e->accept();
            if(part==1 && m_backRect.contains(e->pos()) && onBackClicked) onBackClicked();
            else if(part==2 && m_sortRect.contains(e->pos()) && onActionClicked) onActionClicked();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        m_mousePos = e->pos();
        layoutActionRects();
        const bool on = (m_backRect.isValid() && m_backRect.contains(m_mousePos))
                     || (m_sortRect.isValid() && m_sortRect.contains(m_mousePos));
        setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
        QWidget::mouseMoveEvent(e);
    }
    void enterEvent(QEvent *) override { m_hovered = true; update(); }
    void leaveEvent(QEvent *) override { m_hovered = false; update(); }
private:
    const SkinPalette *m_palette;
    QString m_title;
    QString m_action;
    bool m_hovered = false;
    int m_pressedPart=0;
    QPoint m_mousePos;
};

// ── StartMenu ───────────────────────────────────────────

StartMenu::StartMenu(QWidget *parent)
    : QWidget(parent)
{
    // No WindowDoesNotAcceptFocus — the menu MUST be able to receive focus
    // so that we can detect when another window steals it (to auto-close).
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setAcceptDrops(true);

    setFixedSize(StartMenuTheme::kPanelWidth, StartMenuTheme::kPanelHeight);

    // 读取保存的皮肤/字体配置，立刻生效
    applyConfig();
    refreshPalette();

    QTimer::singleShot(0, this, &StartMenu::applyX11Immunity);

    setupUi();
    m_nextKdeGlassView = new NextKdeGlassView(this);
    m_nextKdeGlassView->setRadius(26);
    QTimer::singleShot(0, this, [this] {
        if (m_skin == Skin::EcoLiquid) m_nextKdeGlassView->prepareGpu();
    });
    m_glassLightTimer = new QTimer(this);
    m_glassLightTimer->setInterval(33);
    connect(m_glassLightTimer, &QTimer::timeout, this, [this] {
        if (m_skin != Skin::EcoLiquid) return;
        const QPointF target=mapFromGlobal(QCursor::pos());
        const QPointF delta=target-m_glassLightPos;
        if (qAbs(delta.x())+qAbs(delta.y()) < 0.5) return;
        m_glassLightPos += delta*0.3;
        // Reflection only touches the rim; don't repaint all text and icons.
        update(QRegion(rect()).subtracted(QRegion(rect().adjusted(28,28,-28,-28))));
    });
    setupClipboardHistory();
    applyFontToChildren();
    setupAppWatcher();
    rebuildAppList();
    refreshUserAvatar();
    setupActiveAppTracking();

    hide();

}

StartMenu::~StartMenu()
{
    delete m_activeAppNotifier;
    if (m_activeAppDisplay) XCloseDisplay(m_activeAppDisplay);
    if (m_pointerDisplay) {
        XCloseDisplay(m_pointerDisplay);
        m_pointerDisplay = nullptr;
    }
}

void StartMenu::applyConfig()
{
    m_skin       = StartMenuConfig::instance().skin();
    m_fontFamily = StartMenuConfig::instance().fontFamily();
    m_fontSize   = StartMenuConfig::instance().fontSize();
}

void StartMenu::refreshPalette()
{
    if (m_skin == Skin::Wallpaper && !m_palettePending && !m_paletteApplying) {
        m_palettePending = true;
        BackgroundTask::run(this, [] { return accentColorFromWallpaper(loadSystemWallpaperImage()); },
            [this](const QColor &accent) {
                m_palettePending = false; m_wallpaperAccent = accent;
                if (m_skin == Skin::Wallpaper) { m_paletteApplying=true; applySkin(m_skin); m_paletteApplying=false; update(); }
            });
    }
    m_palette = StartMenuTheme::paletteForSkin(m_skin, m_wallpaperAccent);
    const int opacity = StartMenuConfig::instance().panelOpacity();
    applyAlpha(m_palette.panelBg, opacity);
    applyAlpha(m_palette.railBg, opacity);
    applyAlpha(m_palette.panelBorder, opacity);
    applyAlpha(m_palette.railBorder, opacity);
    setWindowOpacity(1.0);
}

void StartMenu::applySkin(Skin skin)
{
    m_skin = skin;
    refreshPalette();
    applyX11BackdropEffect();
    applySearchStyle();
    // 刷新内容（按钮的颜色等都依赖 palette）
    rebuildAppList();
    if (m_pinnedHeader) m_pinnedHeader->update();
    if (m_recentHeader) m_recentHeader->update();
    update();
}

void StartMenu::applyFont(const QString &family, int size)
{
    m_fontFamily = family;
    m_fontSize = size;
    applyFontToChildren();
    if (m_pinnedHeader) m_pinnedHeader->update();
    if (m_recentHeader) m_recentHeader->update();
    update();
}

void StartMenu::applyFontToChildren()
{
    QFont f;
    if (!m_fontFamily.isEmpty())
        f.setFamily(m_fontFamily);
    f.setPointSize(m_fontSize > 0 ? m_fontSize : 14);
    f.setStyleStrategy(QFont::PreferAntialias);
    if (m_searchEdit)
        m_searchEdit->setFont(f);
    setFont(f);
}

void StartMenu::applySearchStyle()
{
    if (!m_searchEdit)
        return;

    if (m_skin == Skin::EcoLiquid) {
        const QString style = QStringLiteral(
            "QLineEdit { background: transparent; border: none; border-radius: 21px;"
            "padding: 10px 16px 10px 48px; color: rgba(248,250,252,248); font-size: 14px;"
            "selection-background-color: rgba(56,189,248,190); selection-color: #08101c; }");
        m_searchEdit->setStyleSheet(style);

        QPalette pal = m_searchEdit->palette();
        pal.setColor(QPalette::Text, m_palette.textPrimary);
        pal.setColor(QPalette::Base, Qt::transparent);
        pal.setColor(QPalette::PlaceholderText, m_palette.textMuted);
        m_searchEdit->setPalette(pal);
        return;
    }

    // 当输入框处于焦点时，让背景颜色呈现微弱高光
    QColor focusBg = m_palette.searchBg;
    focusBg.setAlpha(std::min(255, focusBg.alpha() + 15));

    const QString style = QStringLiteral(
        "QLineEdit {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 20px;"             // 略微降低圆角使结构稍显挺拔
        "  padding: 10px 16px 10px 48px;"
        "  color: %3;"
        "  font-size: 14px;"
        "  selection-background-color: %4;"
        "  selection-color: %5;"
        "}"
        "QLineEdit:focus {"
        "  border: 1.5px solid %6;"           // 焦点态高亮发光
        "  background: %7;"
        "}")
        .arg(rgbaCss(m_palette.searchBg),
             rgbaCss(m_palette.searchBorder),
             rgbaCss(m_palette.textPrimary),
             rgbaCss(m_palette.focusBorder),
             rgbaCss(m_palette.panelBg),
             rgbaCss(m_palette.accent),       // 焦点边框采用高对比度 accent
             rgbaCss(focusBg));
    m_searchEdit->setStyleSheet(style);

    QPalette pal = m_searchEdit->palette();
    pal.setColor(QPalette::Text, m_palette.textPrimary);
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::PlaceholderText, m_palette.textMuted);
    m_searchEdit->setPalette(pal);
}

void StartMenu::applyX11Immunity()
{
    if (!isX11Platform()) return;
    Display *display = XOpenDisplay(nullptr);
    if (!display) return;

    const Window window = static_cast<Window>(winId());

    // Do NOT override _NET_WM_WINDOW_TYPE — let Qt's Tool flag work normally.
    // Setting DOCK/POPUP_MENU types caused focus issues (menu couldn't receive
    // focus, leading to immediate auto-close).

    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    if (stateAtom != None) {
        QVector<Atom> states;
        const char *stateNames[] = {
            "_NET_WM_STATE_ABOVE",
            "_NET_WM_STATE_SKIP_PAGER",
            "_NET_WM_STATE_SKIP_TASKBAR",
        };
        for (const char *name : stateNames) {
            const Atom atom = XInternAtom(display, name, False);
            if (atom != None)
                states.append(atom);
        }
        if (!states.isEmpty()) {
            XChangeProperty(display, window, stateAtom, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<unsigned char *>(states.data()),
                            states.size());
        }
    }

    XRaiseWindow(display, window);
    XSync(display, False);
    XCloseDisplay(display);
    applyX11BackdropEffect();
}

void StartMenu::applyX11BackdropEffect()
{
    if (!isX11Platform()) return;
    Display *display = XOpenDisplay(nullptr);
    if (!display) return;

    const Window window = static_cast<Window>(winId());
    const Atom blurAtom = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
    // Snapshot material already contains its own blur. Asking KWin to blur
    // the rectangular window again produces halos outside the rounded mask.
    if (blurAtom != None)
        XDeleteProperty(display, window, blurAtom);
    XFlush(display);
    XCloseDisplay(display);
}

bool StartMenu::paintGlassControl(QPainter &p,const QWidget *owner,const QRectF &target,
                                  qreal radius,qreal hover,bool pressed)
{
    if(m_skin!=Skin::EcoLiquid || !m_nextKdeGlassView) return false;
    const QPoint origin=owner->mapTo(this,QPoint(0,0));
    const QImage glass=m_nextKdeGlassView->controlImage(target.translated(origin),radius,pressed);
    if(glass.isNull()) return false;
    const QRectF dest=pressed ? target.adjusted(1,1,-1,-1) : target;
    const qreal active=qBound(0.0,hover,1.0);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(dest,radius,radius);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,pressed ? 12 : 28));
    p.drawRoundedRect(dest.translated(0,1.3),radius,radius);
    p.setOpacity(0.58+active*0.42);
    p.drawImage(dest,glass);
    p.setOpacity(1);
    p.setClipPath(shape,Qt::IntersectClip);
    const QPointF cursor=owner->mapFromGlobal(QCursor::pos());
    QRadialGradient glint(cursor,qMax(40.0,target.width()*0.9));
    glint.setColorAt(0,QColor(255,255,255,qRound(80+active*100)));
    glint.setColorAt(0.42,QColor(207,236,255,qRound(20+active*30)));
    glint.setColorAt(1,QColor(255,255,255,0));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QBrush(glint),pressed ? 1.5 : 1.0));
    p.drawPath(shape);
    p.restore();
    return true;
}

qreal StartMenu::glassLuminanceAt(const QRectF &menuRect) const
{
    return m_skin == Skin::EcoLiquid && m_nextKdeGlassView
        ? m_nextKdeGlassView->luminanceAt(menuRect) : 0;
}

void StartMenu::captureNextKdeBackdrop()
{
    if (!m_nextKdeGlassView || isVisible())
        return;
    if (m_skin != Skin::EcoLiquid || !isX11Platform())
        return;

    QScreen *screen = QGuiApplication::screenAt(pos() + QPoint(width() / 2, height() / 2));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    // Capture while the menu is still hidden. Unlike the KWin6 effect, a Qt5
    // client cannot sample the compositor framebuffer, so this snapshot is the
    // portable V2 bridge for the backdrop texture used by the shader.
    const QPixmap snapshot = screen->grabWindow(0, pos().x(), pos().y(),
                                                 width(), height());
    if (snapshot.isNull()) {
        qWarning() << "[NextKdeGlass] screen backdrop capture failed";
        return;
    }

    const QImage backdrop = snapshot.toImage();
    // Compare actual pixels and DPR, not a timer: identical backgrounds reuse
    // the material; moved windows, wallpaper and screen changes refresh it.
    if (!m_nextKdeGlassView->image().isNull()
        && backdrop.devicePixelRatio() == m_lastBackdrop.devicePixelRatio()
        && backdrop == m_lastBackdrop)
        return;
    if (!qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_FAST"))
        m_nextKdeGlassView->setBackdrop(backdrop);
    else
        m_nextKdeGlassView->setBackdropFast(backdrop);
    m_lastBackdrop = backdrop;
    qDebug() << "[NextKdeGlass] captured backdrop" << snapshot.size()
             << "mode" << (qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_FAST") ? "fast" : "shared-optics")
             << "for menu" << size();
}

void StartMenu::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);

    // Auto-close when another window gets focus
    if (e->type() == QEvent::ActivationChange) {
        // Don't auto-close while the StartButton context menu is open
        if (property("contextMenuOpen").toBool()) return;
        // Don't auto-close on mere focus loss — only close on actual
        // clicks outside (handled by the outside watch timer).
    }

    if (e->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
            QTimer::singleShot(0, this, [this] {
                setWindowState(windowState() & ~Qt::WindowMinimized);
                if (m_visible) {
                    show();
                    applyX11Immunity();
                }
            });
        }
    }
}

void StartMenu::setupUi()
{
    m_searchEdit = new GlassSearchEdit(this);
    m_searchEdit->setGeometry(StartMenuTheme::kRailWidth + 32, 32,
                               StartMenuTheme::kPanelWidth - StartMenuTheme::kRailWidth - 64, 44);
    m_searchEdit->setPlaceholderText(QString::fromUtf8("\xE6\x90\x9C\xE7\xB4\xA2\xE5\xBA\x94\xE7\x94\xA8\xE3\x80\x81\xE6\x96\x87\xE4\xBB\xB6\xE6\x88\x96\xE8\xAE\xBE\xE7\xBD\xAE..."));
    applySearchStyle();
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(140);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this] {
        filterApps(m_pendingSearchText);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_pendingSearchText = text;
        m_searchDebounceTimer->start();
    });

    const int contentX = StartMenuTheme::kRailWidth + 32;
    const int contentW = StartMenuTheme::kPanelWidth - StartMenuTheme::kRailWidth - 64;
    const int topY = m_searchEdit->geometry().bottom() + 20;

    m_pinnedHeader = new SectionHeader(&m_palette,
        QString::fromUtf8("\xE5\xB7\xB2\xE5\x9B\xBA\xE5\xAE\x9A"),
        QString::fromUtf8("\xE6\x89\x80\xE6\x9C\x89\xE5\xBA\x94\xE7\x94\xA8 >"), this);
    m_pinnedHeader->onActionClicked = [this] {
        if (m_showingAllApps) {
            showAllAppsHeaderMenu();
        } else {
            showAllAppsView();
        }
    };
    m_pinnedHeader->setGeometry(contentX, topY, contentW, 32);

    m_pinnedScrollArea = new QScrollArea(this);
    m_pinnedScrollArea->setGeometry(contentX - 4, topY + 36, contentW + 8, 328);
    m_pinnedScrollArea->setFrameShape(QFrame::NoFrame);
    m_pinnedScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pinnedScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pinnedScrollArea->setWidgetResizable(false);
    m_pinnedScrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(100,116,139,90); border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ));

    auto *pinnedContainer = new PinnedGridContainer(&m_palette);
    pinnedContainer->onAppDropped = [this](int fromIndex, int toIndex) {
        QTimer::singleShot(0, this, [this, fromIndex, toIndex] {
            movePinnedApp(fromIndex, toIndex);
        });
    };
    m_pinnedContainer = pinnedContainer;
    m_pinnedContainer->setFixedWidth(contentW + 2);
    m_pinnedGrid = new QGridLayout(m_pinnedContainer);
    m_pinnedGrid->setContentsMargins(4, 0, 4, 0);
    m_pinnedGrid->setSpacing(2);
    m_pinnedGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_pinnedScrollArea->setWidget(m_pinnedContainer);
    connect(m_pinnedScrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this] {
        // Scroll blitting may otherwise move the old material with the icons.
        // Repaint from each control's new menu-space coordinates.
        if (m_skin==Skin::EcoLiquid) {
            m_pinnedScrollArea->viewport()->update();
            m_pinnedContainer->update();
        }
    });

    const int recentY = m_pinnedScrollArea->geometry().bottom() + 8;
    m_recentHeader = new SectionHeader(&m_palette,
        QString::fromUtf8("最近使用"), QString(), this);
    m_recentHeader->setGeometry(contentX, recentY, contentW, 32);

    m_recentContainer = new QWidget(this);
    m_recentContainer->setGeometry(contentX - 4, recentY + 32, contentW + 8, 180);
    auto *recentGrid = new QGridLayout(m_recentContainer);
    recentGrid->setContentsMargins(4, 0, 4, 0);
    recentGrid->setHorizontalSpacing(4);
    recentGrid->setVerticalSpacing(12);
    recentGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
}

void StartMenu::rebuildAppList()
{
    if (m_appRebuildTimer) m_appRebuildTimer->stop();
    if (m_appScanBusy) { m_appScanPending = true; return; }
    m_appScanBusy = true; m_appScanPending = false;
    BackgroundTask::run(this, [] {
        return qMakePair(AppRegistry::pinnedApps(), AppRegistry::installedApps());
    }, [this](const QPair<QList<AppEntry>, QList<AppEntry>> &lists) {
        m_appScanBusy = false;
        if (m_appScanPending) { rebuildAppList(); return; }
    m_pinnedApps = lists.first;
    m_allApps = lists.second;

    QStringList pinnedPaths;
    for (const AppEntry &app : m_pinnedApps)
        pinnedPaths.append(app.desktopPath);
    for (AppEntry &app : m_allApps)
        app.pinned = pinnedPaths.contains(app.desktopPath);
    sortAllApps();
    reloadRailApps();

    const bool keepAllApps = m_showingAllApps;
    const QString searchText = m_searchEdit ? m_searchEdit->text() : QString();
    if (!searchText.isEmpty()) {
        filterApps(searchText);
    } else if (keepAllApps) {
        showAllAppsView();
    } else {
        showPinnedView();
    }

    qDebug() << "[StartMenu] rebuildAppList: pinned=" << m_pinnedApps.size()
             << "all=" << m_allApps.size()
             << "recent will be fetched in setupRecentFiles";
    setupRecentFiles();
    refreshAppWatcher();
    });
}

void StartMenu::reloadRailApps()
{
    const QStringList savedPaths = StartMenuConfig::instance().railPinnedApps();
    QList<AppEntry> resolved;
    QStringList validPaths;

    for (const QString &path : savedPaths) {
        if (resolved.size() >= kMaxRailApps)
            break;
        for (const AppEntry &app : m_allApps) {
            if (app.desktopPath == path && !validPaths.contains(path)) {
                resolved.append(app);
                validPaths.append(path);
                break;
            }
        }
    }

    m_railApps = resolved;
    if (validPaths != savedPaths)
        StartMenuConfig::instance().setRailPinnedApps(validPaths);
    update();
}

void StartMenu::saveRailApps()
{
    QStringList paths;
    for (const AppEntry &app : m_railApps)
        paths.append(app.desktopPath);
    StartMenuConfig::instance().setRailPinnedApps(paths);
    update();
}

void StartMenu::removeRailApp(int index)
{
    if (index < 0 || index >= m_railApps.size())
        return;
    m_railApps.removeAt(index);
    saveRailApps();
}

void StartMenu::setupAppWatcher()
{
    if (m_appWatcher)
        return;

    m_appWatcher = new QFileSystemWatcher(this);
    m_appRebuildTimer = new QTimer(this);
    m_appRebuildTimer->setSingleShot(true); m_appRebuildTimer->setInterval(150);
    connect(m_appRebuildTimer, &QTimer::timeout, this, &StartMenu::rebuildAppList);
    auto schedule = [this] {
        if (m_appScanBusy) m_appScanPending = true;
        m_appRebuildTimer->start();
    };
    connect(m_appWatcher, &QFileSystemWatcher::directoryChanged, this, schedule);
    connect(m_appWatcher, &QFileSystemWatcher::fileChanged, this, schedule);
    refreshAppWatcher();
}

void StartMenu::refreshAppWatcher()
{
    if (!m_appWatcher)
        return;

    const QStringList oldDirs = m_appWatcher->directories();
    if (!oldDirs.isEmpty())
        m_appWatcher->removePaths(oldDirs);
    const QStringList oldFiles = m_appWatcher->files();
    if (!oldFiles.isEmpty()) m_appWatcher->removePaths(oldFiles);
    QStringList desktopFiles;
    for (const AppEntry &app : m_allApps)
        if (QFile::exists(app.desktopPath)) desktopFiles.append(app.desktopPath);
    if (!desktopFiles.isEmpty()) m_appWatcher->addPaths(desktopFiles);

    QStringList existingDirs;
    for (const QString &dir : AppRegistry::applicationDirs()) {
        if (QDir(dir).exists() && !existingDirs.contains(dir))
            existingDirs.append(dir);
    }

    const QStringList localRoots = {
        QDir::homePath() + "/应用",
        QDir::homePath() + "/Applications",
        QDir::homePath() + "/apps",
    };
    for (const QString &dir : localRoots) {
        if (QDir(dir).exists() && !existingDirs.contains(dir))
            existingDirs.append(dir);
    }

    if (!existingDirs.isEmpty())
        m_appWatcher->addPaths(existingDirs);
}

void StartMenu::setupActiveAppTracking()
{
    if (m_activeAppTimer || !isX11Platform())
        return;

    m_activeAppDisplay = XOpenDisplay(nullptr);
    if (!m_activeAppDisplay) return;
    const Atom activeAtom = XInternAtom(m_activeAppDisplay, "_NET_ACTIVE_WINDOW", False);
    XSelectInput(m_activeAppDisplay, DefaultRootWindow(m_activeAppDisplay), PropertyChangeMask);
    XFlush(m_activeAppDisplay);
    m_activeAppTimer = new QTimer(this);
    m_activeAppTimer->setSingleShot(true);
    m_activeAppTimer->setInterval(100);
    connect(m_activeAppTimer, &QTimer::timeout,
            this, &StartMenu::recordActiveWindowApp);
    m_activeAppNotifier = new QSocketNotifier(ConnectionNumber(m_activeAppDisplay),
                                              QSocketNotifier::Read, this);
    connect(m_activeAppNotifier, &QSocketNotifier::activated, this, [this, activeAtom](int) {
        while (XPending(m_activeAppDisplay)) {
            XEvent event;
            XNextEvent(m_activeAppDisplay, &event);
            if (event.type == PropertyNotify && event.xproperty.atom == activeAtom)
                m_activeAppTimer->start();
        }
    });
}

void StartMenu::recordActiveWindowApp()
{
    if (!isX11Platform())
        return;

    const unsigned long windowId = activeWindowId(m_activeAppDisplay);
    if (windowId == 0 || windowId == static_cast<unsigned long>(winId()))
        return;
    if (windowId == m_lastActiveWindow)
        return;

    m_lastActiveWindow = windowId;

    if (m_allApps.isEmpty()) { rebuildAppList(); return; }

    const QString desktopPath = desktopPathForWindow(m_activeAppDisplay, m_allApps, windowId);
    if (desktopPath.isEmpty() || desktopPath == m_lastRecordedActiveDesktop)
        return;

    AppRegistry::recordLaunchedApp(desktopPath);
    m_lastRecordedActiveDesktop = desktopPath;
}

void StartMenu::setupPinnedGrid()
{
    QLayoutItem *item;
    while ((item = m_pinnedGrid->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    const QList<AppEntry> apps = displayedApps();
    const int maxApps = apps.size();
    const int columns = 6;
    const int rows = std::max(1, (maxApps + columns - 1) / columns);
    m_pinnedContainer->setMinimumHeight(rows * 86);
    m_pinnedContainer->resize(m_pinnedContainer->width(), rows * 86);

    for (int i = 0; i < apps.size() && i < maxApps; ++i) {
        const bool allowReorder = !m_showingAllApps
                               && m_searchEdit
                               && m_searchEdit->text().isEmpty();
        auto *btn = new AppIconButton(&m_palette, apps[i], i, m_pinnedContainer,
                                      false, true, allowReorder);
        connect(btn, &QPushButton::clicked, this, [this, i] {
            launchApp(i);
        });
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        const AppEntry app = apps[i];
        connect(btn, &QWidget::customContextMenuRequested, this, [this, btn, app](const QPoint &pos) {
            showAppContextMenu(app, btn->mapToGlobal(pos));
        });
        const int row = i / columns;
        const int col = i % columns;
        m_pinnedGrid->addWidget(btn, row, col);
        btn->show();
    }
    if (m_pinnedScrollArea)
        m_pinnedScrollArea->verticalScrollBar()->setValue(0);
}

void StartMenu::movePinnedApp(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_pinnedApps.size()
        || toIndex < 0 || toIndex >= m_pinnedApps.size()
        || fromIndex == toIndex) {
        return;
    }

    const int scrollPosition = m_pinnedScrollArea
        ? m_pinnedScrollArea->verticalScrollBar()->value()
        : 0;
    m_pinnedApps.move(fromIndex, toIndex);

    QStringList paths;
    paths.reserve(m_pinnedApps.size());
    for (const AppEntry &app : m_pinnedApps)
        paths.append(app.desktopPath);
    AppRegistry::savePinned(paths);

    m_filteredApps = m_pinnedApps;
    setupPinnedGrid();
    if (m_pinnedScrollArea)
        m_pinnedScrollArea->verticalScrollBar()->setValue(scrollPosition);
}

void StartMenu::setupRecentFiles()
{
    QLayout *layout = m_recentContainer->layout();
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    QGridLayout *grid = qobject_cast<QGridLayout *>(layout);
    if (!grid) return;

    const QList<AppEntry> recentApps = AppRegistry::recentApps(m_allApps, 12);
    for (int i = 0; i < recentApps.size(); ++i) {
        const AppEntry &app = recentApps[i];
        auto *btn = new AppIconButton(&m_palette, app, i, m_recentContainer,
                                      true, true, false);
        connect(btn, &QPushButton::clicked, this, [this, app] {
            launchAppEntry(app);
        });
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this, [this, btn, app](const QPoint &pos) {
            showAppContextMenu(app, btn->mapToGlobal(pos));
        });
        int row = i / 6;
        int col = i % 6;
        grid->addWidget(btn, row, col);
        btn->show();
    }
}

void StartMenu::filterApps(const QString &text)
{
    if (text.isEmpty()) {
        if (m_showingAllApps)
            showAllAppsView();
        else
            showPinnedView();
        return;
    } else {
        m_filteredApps.clear();
        for (const auto &app : m_allApps) {
            if (app.name.contains(text, Qt::CaseInsensitive)
                || app.comment.contains(text, Qt::CaseInsensitive)
                || app.exec.contains(text, Qt::CaseInsensitive))
            {
                m_filteredApps.append(app);
            }
        }
        if (m_pinnedHeader) {
            m_pinnedHeader->setTitle(QString::fromUtf8("搜索结果"));
            m_pinnedHeader->setAction(QString::fromUtf8("排序: ") + sortModeName());
        }
    }
    setupPinnedGrid();
}

void StartMenu::launchApp(int index)
{
    const QList<AppEntry> apps = displayedApps();
    if (index < 0 || index >= apps.size()) return;

    launchAppEntry(apps[index]);
}

void StartMenu::launchAppEntry(const AppEntry &app)
{
    bool started = false;

    if (!app.desktopPath.isEmpty()) {
        const QString desktopFileName = QFileInfo(app.desktopPath).fileName();
        started = QProcess::startDetached("gtk-launch", QStringList() << desktopFileName);
    }

    if (!started && !app.exec.isEmpty()) {
        QString execStr = app.exec;
        QStringList parts = execStr.split(' ', QString::SkipEmptyParts);
        if (!parts.isEmpty()) {
            const QString cmd = parts.takeFirst();
            parts.erase(std::remove_if(parts.begin(), parts.end(),
                [](const QString &s) { return s.startsWith('%'); }),
                parts.end());
            started = QProcess::startDetached(cmd, parts);
        }
    }

    if (started) {
        AppRegistry::recordLaunchedApp(app.desktopPath);
        m_lastRecordedActiveDesktop = app.desktopPath;
    }

    hideMenu();
}

void StartMenu::launchRecent(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    hideMenu();
}

void StartMenu::positionAboveStartButton()
{
    const TaskbarInfo info = TaskbarDetector::detect();
    if (!info.detected) return;

    const int gap = 4;
    const int menuW = StartMenuTheme::kPanelWidth;
    const int menuH = StartMenuTheme::kPanelHeight;

    int menuX, menuY;

    if (info.edge == 3) {
        // Bottom taskbar: menu left edge aligns with start button left edge
        // startButtonCenter is now accurate (from real panel window detection)
        menuX = info.startButtonCenter.x() - 20;
        menuY = info.geometry.top() - gap - menuH;
    } else if (info.edge == 2) {
        // Top taskbar
        menuX = info.startButtonCenter.x() - 20;
        menuY = info.geometry.bottom() + gap;
    } else if (info.edge == 0) {
        // Left taskbar
        menuX = info.geometry.right() + gap;
        menuY = info.startButtonCenter.y() - menuH / 2;
    } else {
        // Right taskbar
        menuX = info.geometry.left() - gap - menuW;
        menuY = info.startButtonCenter.y() - menuH / 2;
    }

    const QRect screen = info.screenGeometry;
    if (menuX + menuW > screen.right() - 8)
        menuX = screen.right() - menuW - 8;
    if (menuX < screen.left() + 8)
        menuX = screen.left() + 8;
    if (menuY + menuH > screen.bottom() - 8)
        menuY = screen.bottom() - menuH - 8;
    if (menuY < screen.top() + 8)
        menuY = screen.top() + 8;

    move(menuX, menuY);
}

void StartMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRect r = rect();
    const QRectF panelRect = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);

    {
        QPainterPath panelPath;
        panelPath.addRoundedRect(panelRect, StartMenuTheme::kCornerRadius,
                                 StartMenuTheme::kCornerRadius);
        if (m_skin == Skin::EcoLiquid) {
            if (m_nextKdeGlassView && !m_nextKdeGlassView->image().isNull()) {
                p.drawImage(QPoint(0, 0), m_nextKdeGlassView->image());
                // Responsive reflected light, not repeated framebuffer capture.
                // Child widgets do not receive or lose any pointer events.
                p.save();
                QPainterPath silhouette;
                silhouette.addRoundedRect(panelRect,26,26);
                p.setClipPath(silhouette);
                QRadialGradient reflection(m_glassLightPos,180);
                reflection.setColorAt(0,QColor(255,255,255,150));
                reflection.setColorAt(0.4,QColor(230,245,255,45));
                reflection.setColorAt(1,QColor(255,255,255,0));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QBrush(reflection),1.6));
                p.drawPath(silhouette);
                p.restore();
            } else {
                // Readable fallback when capture is unavailable (e.g. Wayland).
                p.fillPath(panelPath, QColor(28, 37, 53, 248));
                p.setPen(QPen(QColor(255, 255, 255, 70), 1));
                p.drawPath(panelPath);
            }
        } else {
            p.fillPath(panelPath, m_palette.panelBg);
        }
        if (m_skin == Skin::Cyber) {
            QColor neon = m_palette.accent;
            p.setPen(QPen(QColor(neon.red(), neon.green(), neon.blue(), 60), 2));
            p.drawPath(panelPath);
            p.setPen(QPen(QColor(neon.red(), neon.green(), neon.blue(), 160), 1));
            QPainterPath innerPath;
            innerPath.addRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5),
                                     StartMenuTheme::kCornerRadius - 1,
                                     StartMenuTheme::kCornerRadius - 1);
            p.drawPath(innerPath);
        } else if (m_skin != Skin::EcoLiquid) {
            p.setPen(QPen(m_palette.panelBorder, 1));
            p.drawPath(panelPath);
        }
    }

    drawLeftRail(p);

    if (m_skin != Skin::EcoLiquid) {
        const QRect searchRect = m_searchEdit->geometry();
        const int iconX = searchRect.left() + 16;
        const int iconY = searchRect.center().y() - 9;
        p.setPen(m_palette.textMuted);
        QFont iconFont = p.font();
        iconFont.setPixelSize(16);
        p.setFont(iconFont);
        p.drawEllipse(iconX, iconY, 14, 14);
        p.drawLine(iconX + 11, iconY + 11, iconX + 18, iconY + 18);
    }

}

void StartMenu::drawLeftRail(QPainter &p)
{
    const int railW = StartMenuTheme::kRailWidth;
    const int cornerRadius = m_skin == Skin::EcoLiquid ? 26 : StartMenuTheme::kCornerRadius;
    const QRect r = rect();
    const qreal innerRadius = 28.0;

    QPainterPath railClip;
    railClip.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5),
                            cornerRadius, cornerRadius);

    QPainterPath railPath;
    railPath.moveTo(0.5, cornerRadius);
    railPath.quadTo(0.5, 0.5, cornerRadius, 0.5);
    railPath.lineTo(railW, 0.5);
    railPath.lineTo(railW, r.height() - innerRadius - 0.5);
    railPath.quadTo(railW, r.height() - 0.5,
                    railW - innerRadius, r.height() - 0.5);
    railPath.lineTo(cornerRadius, r.height() - 0.5);
    railPath.quadTo(0.5, r.height() - 0.5,
                    0.5, r.height() - cornerRadius);
    railPath.closeSubpath();

    p.save();
    p.setClipPath(railClip);

    if (m_skin == Skin::EcoLiquid) {
        // A subtle shade on the same material, no separately opaque capsule.
        p.fillRect(QRectF(0.5, 0.5, railW, height()-1), QColor(0, 0, 0, 16));
        p.setPen(QPen(QColor(255, 255, 255, 18), 1));
        p.drawLine(QPointF(railW, 28), QPointF(railW, height()-28));
    } else {
        p.fillPath(railPath, m_palette.railBg);
    }

    p.restore();

    p.save();
    p.setClipPath(railClip);
    drawAvatarButton(p);
    drawRailApps(p);
    drawRailButtons(p);
    p.restore();
}

void StartMenu::refreshUserAvatar()
{
    m_userAvatar = loadUserAvatarPixmap();
    update();
}

bool StartMenu::openAccountSettings()
{
    const QList<QPair<QString, QStringList>> commands = {
        {"ukui-control-center", QStringList() << "-m" << "userinfo"},
        {"ukui-control-center", QStringList() << "-p" << "account" << "-m" << "userinfo"},
        {"ukui-control-center", QStringList() << "-m" << "account"},
        {"ukui-control-center", QStringList() << "-m" << "accounts"},
        {"ukui-control-center", QStringList() << "-m" << "user"},
        {"ukui-control-center", QStringList() << "-p" << "account"},
        {"ukui-control-center", QStringList()},
        {"gnome-control-center", QStringList() << "user-accounts"},
        {"mate-control-center", QStringList()},
    };
    return startFirstAvailable(commands);
}

void StartMenu::drawAvatarButton(QPainter &p)
{
    const int cx = StartMenuTheme::kRailWidth / 2;
    const int cy = 50;
    const int r = 16;

    if (m_hoveredRailBtn == RailButton::Avatar) {
        QRect avatarRect(cx - 20, cy - 20, 40, 40);
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        p.drawRoundedRect(avatarRect, 10, 10);
    }

    // 1. 头像边缘精致的科技感仪表环（虚线）
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_palette.accent, 1, Qt::DashLine));
    p.drawArc(cx - r - 4, cy - r - 4, (r + 4) * 2, (r + 4) * 2, 45 * 16, 270 * 16);
    p.restore();

    QPainterPath circle;
    circle.addEllipse(QPointF(cx, cy), r, r);

    if (!m_userAvatar.isNull()) {
        const QPixmap scaledAvatar =
            m_userAvatar.scaled(r * 2, r * 2,
                                Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
        const QRect crop((scaledAvatar.width() - r * 2) / 2,
                         (scaledAvatar.height() - r * 2) / 2,
                         r * 2, r * 2);
        p.save();
        p.setClipPath(circle);
        p.drawPixmap(QRect(cx - r, cy - r, r * 2, r * 2),
                     scaledAvatar.copy(crop));
        p.restore();
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(circle);
    } else {
        QLinearGradient g(cx - r, cy - r, cx + r, cy + r);
        g.setColorAt(0, m_palette.avatarGrad1);
        g.setColorAt(1, m_palette.avatarGrad2);
        p.fillPath(circle, g);

        p.setPen(QPen(QColor(255, 255, 255, 220), 1.2));
        p.drawEllipse(QPointF(cx, cy - 4), 5, 5);
        p.drawEllipse(QPointF(cx, cy + 9), 9, 5);
    }

    // 4. 头像右下角：高亮在线状态指示灯
    p.setPen(QPen(m_palette.panelBg, 1.5));
    p.setBrush(QColor(34, 197, 94)); // 现代莹光绿 (Green-500)
    p.drawEllipse(QPointF(cx + 10, cy + 10), 4.5, 4.5);
}

void StartMenu::drawRailApps(QPainter &p)
{
    const int cx = StartMenuTheme::kRailWidth / 2;

    for (int i = 0; i < m_railApps.size() && i < kMaxRailApps; ++i) {
        const int cy = railAppY(i);
        const QRect slotRect(cx - 18, cy - 18, 36, 36);
        if (m_hoveredRailApp == i) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_palette.hoverBg);
            if (m_skin != Skin::EcoLiquid)
                p.drawRoundedRect(slotRect, 10, 10);
            p.setBrush(m_palette.accent);
            p.drawRoundedRect(QRectF(3, cy - 8, 3, 16), 1.5, 1.5);
        }

        const QIcon icon = iconForDesktopIcon(m_railApps[i].name,
                                              m_railApps[i].iconName,
                                              m_railApps[i].desktopPath);
        drawLauncherIcon(p,icon,QRect(cx-13,cy-13,26,26),devicePixelRatioF(),
                         m_skin==Skin::EcoLiquid);
    }

    if (m_railDropIndex >= 0 && m_railDropIndex < kMaxRailApps) {
        const int cy = railAppY(m_railDropIndex);
        QColor fill = m_palette.accent;
        fill.setAlpha(42);
        QColor outline = m_palette.accent;
        outline.setAlpha(210);
        p.setBrush(fill);
        p.setPen(QPen(outline, 1.5));
        p.drawRoundedRect(QRect(cx - 18, cy - 18, 36, 36), 10, 10);

        p.setPen(QPen(outline, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(cx - 6, cy, cx + 6, cy);
        p.drawLine(cx, cy - 6, cx, cy + 6);
    }
}

void StartMenu::drawRailButtons(QPainter &p)
{
    const int cx = StartMenuTheme::kRailWidth / 2;

    // Documents 按钮绘制
    const int docY = railDocumentsY();
    QRect docRect(cx - 18, docY - 18, 36, 36);
    paintGlassControl(p,this,docRect,11,m_hoveredRailBtn==RailButton::Documents ? 1.0 : 0.0);
    if (m_hoveredRailBtn == RailButton::Documents) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        if (m_skin!=Skin::EcoLiquid) p.drawRoundedRect(docRect, 8, 8);

        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, docY - 8, 3, 16), 1.5, 1.5);
    }
    p.setPen(m_palette.textSecondary);
    p.drawRect(docRect.adjusted(7, 5, -7, -11));
    p.drawLine(docRect.left() + 11, docRect.bottom() - 8, docRect.right() - 11, docRect.bottom() - 8);

    // Clipboard history
    const int clipY = railClipboardY();
    QRect clipRect(cx - 18, clipY - 18, 36, 36);
    paintGlassControl(p,this,clipRect,11,m_hoveredRailBtn==RailButton::Clipboard ? 1.0 : 0.0);
    if (m_hoveredRailBtn == RailButton::Clipboard) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        if (m_skin!=Skin::EcoLiquid) p.drawRoundedRect(clipRect, 8, 8);
        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, clipY - 8, 3, 16), 1.5, 1.5);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m_palette.textSecondary, 1.5, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawRoundedRect(QRectF(cx - 8, clipY - 8, 16, 18), 3, 3);
    p.drawRoundedRect(QRectF(cx - 4, clipY - 11, 8, 5), 2, 2);
    p.drawLine(cx - 4, clipY - 1, cx + 4, clipY - 1);
    p.drawLine(cx - 4, clipY + 4, cx + 3, clipY + 4);

    // Quick theme switch: a small liquid droplet
    const int themeY = railThemeY();
    QRect themeRect(cx - 18, themeY - 18, 36, 36);
    paintGlassControl(p,this,themeRect,11,m_hoveredRailBtn==RailButton::Theme ? 1.0 : 0.0);
    if (m_hoveredRailBtn == RailButton::Theme) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        if (m_skin!=Skin::EcoLiquid) p.drawRoundedRect(themeRect, 8, 8);
        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, themeY - 8, 3, 16), 1.5, 1.5);
    }
    QPainterPath drop;
    drop.moveTo(cx, themeY - 11);
    drop.cubicTo(cx - 3, themeY - 5, cx - 9, themeY,
                 cx - 9, themeY + 5);
    drop.cubicTo(cx - 9, themeY + 11, cx - 4, themeY + 14,
                 cx, themeY + 14);
    drop.cubicTo(cx + 5, themeY + 14, cx + 9, themeY + 10,
                 cx + 9, themeY + 5);
    drop.cubicTo(cx + 9, themeY, cx + 3, themeY - 5,
                 cx, themeY - 11);
    QLinearGradient dropGradient(cx - 8, themeY - 8, cx + 8, themeY + 12);
    dropGradient.setColorAt(0.0, m_palette.accent.lighter(135));
    dropGradient.setColorAt(1.0, QColor(99, 102, 241));
    p.setPen(QPen(QColor(255, 255, 255, 90), 0.8));
    p.setBrush(dropGradient);
    p.drawPath(drop);
    p.setPen(QPen(QColor(255, 255, 255, 145), 1.1, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(cx - 5, themeY - 3, 9, 9), 35 * 16, 105 * 16);

    // Settings 按钮绘制
    const int setY = railSettingsY();
    QRect setRect(cx - 18, setY - 18, 36, 36);
    paintGlassControl(p,this,setRect,11,m_hoveredRailBtn==RailButton::Settings ? 1.0 : 0.0);
    if (m_hoveredRailBtn == RailButton::Settings) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        if (m_skin!=Skin::EcoLiquid) p.drawRoundedRect(setRect, 8, 8);

        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, setY - 8, 3, 16), 1.5, 1.5);
    }
    p.setPen(m_palette.textSecondary);
    p.drawEllipse(setRect.adjusted(6, 6, -6, -6));
    p.drawEllipse(setRect.center().x() - 3, setRect.center().y() - 3, 6, 6);

    // Power 按钮绘制（使用圆角笔触描绘精确的开关弧线）
    const int powY = railPowerY();
    QRect powRect(cx - 18, powY - 18, 36, 36);
    paintGlassControl(p,this,powRect,11,m_hoveredRailBtn==RailButton::Power ? 1.0 : 0.0);
    if (m_hoveredRailBtn == RailButton::Power) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        if (m_skin!=Skin::EcoLiquid) p.drawRoundedRect(powRect, 8, 8);

        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, powY - 8, 3, 16), 1.5, 1.5);
    }
    
    // 开关机图标：圆角断开的环 + 竖线
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_palette.powerNormal, 1.8, Qt::SolidLine, Qt::RoundCap));
    
    p.drawArc(powRect.adjusted(9, 9, -9, -9), 120 * 16, 300 * 16);
    p.drawLine(cx, powY - 8, cx, powY + 1);
    p.restore();
}

void StartMenu::handleRailClick(const QPoint &pos)
{
    const int cx = StartMenuTheme::kRailWidth / 2;
    const auto hit = [pos](int cxp, int y) {
        const QRect rc(cxp - 18, y - 18, 36, 36);
        return rc.contains(pos);
    };

    // 头像
    if (hit(cx, 50)) {
        openAccountSettings();
        hideMenu();
        return;
    }

    const int railAppIndex = railAppIndexAt(pos);
    if (railAppIndex >= 0) {
        launchAppEntry(m_railApps[railAppIndex]);
        return;
    }

    // 文档按钮
    if (hit(cx, railDocumentsY())) {
        openDocuments();
        return;
    }
    if (hit(cx, railClipboardY())) {
        showClipboardHistoryMenu();
        return;
    }
    if (hit(cx, railThemeY())) {
        cycleQuickTheme();
        return;
    }
    // 设置按钮
    if (hit(cx, railSettingsY())) {
        QMenu menu(this);
        StartMenuTheme::applySharedMenuStyle(&menu);
        auto *actSystem = menu.addAction(themedIcon({"preferences-system", "ukui-control-center", "settings"}),
                                         QString::fromUtf8("系统设置"));
        auto *actAbout = menu.addAction(themedIcon({"help-about", "ukui-control-center", "computer"}),
                                        QString::fromUtf8("关于麒麟"));
        auto *actStart = menu.addAction(themedIcon({"configure", "preferences-desktop", "preferences-system"}),
                                        QString::fromUtf8("开始菜单设置"));
        QAction *chosen = menu.exec(mapToGlobal(QPoint(StartMenuTheme::kRailWidth + 6,
                                                       railSettingsY())));
        if (chosen == actSystem)
            openSystemSettings();
        else if (chosen == actAbout)
            openAboutKylin();
        else if (chosen == actStart)
            openSettings();
        return;
    }
    // 电源按钮
    if (hit(cx, railPowerY())) {
        const QPoint globalPos = mapToGlobal(QPoint(StartMenuTheme::kRailWidth + 6, railPowerY()));
        showPowerMenu(globalPos);
        return;
    }
}

int StartMenu::railAppIndexAt(const QPoint &pos) const
{
    if (pos.x() < 0 || pos.x() >= StartMenuTheme::kRailWidth)
        return -1;
    const int cx = StartMenuTheme::kRailWidth / 2;
    for (int i = 0; i < m_railApps.size() && i < kMaxRailApps; ++i) {
        if (QRect(cx - 18, railAppY(i) - 18, 36, 36).contains(pos))
            return i;
    }
    return -1;
}

int StartMenu::railDropIndexAt(const QPoint &pos) const
{
    // Give the user a forgiving target: the visible rail plus a small inset
    // into the content area. The whole blank rail area above Documents accepts
    // a drop and resolves it to the nearest of the four shortcut slots.
    if (pos.x() < 0 || pos.x() >= StartMenuTheme::kRailWidth + 30)
        return -1;
    if (pos.y() < railAppY(0) - 40 || pos.y() > railDocumentsY() - 24)
        return -1;

    int nearest = 0;
    int nearestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < kMaxRailApps; ++i) {
        const int distance = qAbs(pos.y() - railAppY(i));
        if (distance < nearestDistance) {
            nearest = i;
            nearestDistance = distance;
        }
    }
    return nearest;
}

void StartMenu::handleRailContextMenu(const QPoint &pos)
{
    const int appIndex = railAppIndexAt(pos);
    if (appIndex >= 0) {
        QMenu menu(this);
        StartMenuTheme::applySharedMenuStyle(&menu);
        QAction *openAction = menu.addAction(
            themedIcon({"document-open", "system-run", "go-next"}),
            QString::fromUtf8("打开"));
        QAction *removeAction = menu.addAction(
            themedIcon({"list-remove", "edit-delete", "user-trash"}),
            QString::fromUtf8("从侧栏移除"));
        QAction *chosen = menu.exec(mapToGlobal(pos));
        if (chosen == openAction)
            launchAppEntry(m_railApps[appIndex]);
        else if (chosen == removeAction)
            removeRailApp(appIndex);
        return;
    }

    const int cx = StartMenuTheme::kRailWidth / 2;
    if (QRect(cx - 18, railThemeY() - 18, 36, 36).contains(pos))
        showThemeMenu();
}

void StartMenu::openDocuments()
{
    const QString docs = QDir::homePath() + "/Documents";
    const QString dir = QDir(docs).exists() ? docs : QDir::homePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    hideMenu();
}

void StartMenu::setupClipboardHistory()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard)
        return;

    const auto captureClipboard = [this, clipboard] {
        const QMimeData *mime = clipboard->mimeData(QClipboard::Clipboard);
        if (!mime)
            return;

        ClipboardHistoryItem item;
        if (mime->hasImage()) {
            const QVariant imageData = mime->imageData();
            if (imageData.canConvert<QImage>())
                item.image = qvariant_cast<QImage>(imageData);
            else if (imageData.canConvert<QPixmap>())
                item.image = qvariant_cast<QPixmap>(imageData).toImage();
            if (item.image.isNull())
                return;
        } else {
            item.text = mime->text();
            if (item.text.trimmed().isEmpty())
                return;
            if (item.text.size() > 20000)
                item.text.truncate(20000);
        }

        for (int i = m_clipboardHistory.size() - 1; i >= 0; --i) {
            const ClipboardHistoryItem &old = m_clipboardHistory[i];
            const bool duplicate = item.isImage()
                ? old.isImage() && old.image == item.image
                : !old.isImage() && old.text == item.text;
            if (duplicate)
                m_clipboardHistory.removeAt(i);
        }
        m_clipboardHistory.prepend(item);
        while (m_clipboardHistory.size() > 20)
            m_clipboardHistory.removeLast();
    };

    connect(clipboard, &QClipboard::dataChanged, this, captureClipboard);
    captureClipboard();
}

void StartMenu::showClipboardHistoryMenu()
{
    QMenu menu(this);
    StartMenuTheme::applySharedMenuStyle(&menu);

    QAction *heading = menu.addAction(QString::fromUtf8("剪贴板历史（仅当前会话）"));
    heading->setEnabled(false);
    menu.addSeparator();

    const int shownCount = qMin(10, m_clipboardHistory.size());
    for (int i = 0; i < shownCount; ++i) {
        const ClipboardHistoryItem item = m_clipboardHistory[i];
        auto *widgetAction = new QWidgetAction(&menu);
        auto *button = new QToolButton(&menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedWidth(420);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { color: rgb(241,245,249); background: transparent;"
            " border: 0; border-radius: 7px; padding: 6px 10px; text-align: left; }"
            "QToolButton:hover { background: rgb(55,105,190); color: white; }"));

        if (item.isImage()) {
            const QPixmap preview = QPixmap::fromImage(item.image).scaled(
                QSize(112, 70), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            button->setIcon(QIcon(preview));
            button->setIconSize(QSize(112, 70));
            button->setMinimumHeight(82);
            button->setText(QString::fromUtf8("图片  %1 × %2")
                                .arg(item.image.width()).arg(item.image.height()));
        } else {
            QString label = item.text;
            label.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
            label = label.trimmed();
            if (label.size() > 46)
                label = label.left(45) + QString::fromUtf8("…");
            button->setIcon(themedIcon({"edit-paste", "edit-copy"}));
            button->setIconSize(QSize(20, 20));
            button->setMinimumHeight(36);
            button->setText(label);
        }

        connect(button, &QToolButton::clicked, &menu, [this, item, &menu] {
            QClipboard *clipboard = QApplication::clipboard();
            if (item.isImage())
                clipboard->setImage(item.image, QClipboard::Clipboard);
            else
                clipboard->setText(item.text, QClipboard::Clipboard);
            menu.close();
        });
        widgetAction->setDefaultWidget(button);
        menu.addAction(widgetAction);
    }

    QAction *emptyAction = nullptr;
    if (m_clipboardHistory.isEmpty()) {
        emptyAction = menu.addAction(QString::fromUtf8("暂无文本或图片记录"));
        emptyAction->setEnabled(false);
    }

    menu.addSeparator();
    QAction *clearAction = menu.addAction(
        themedIcon({"edit-clear-history", "edit-clear", "user-trash"}),
        QString::fromUtf8("清空历史"));
    clearAction->setEnabled(!m_clipboardHistory.isEmpty());

    QAction *chosen = menu.exec(mapToGlobal(
        QPoint(StartMenuTheme::kRailWidth + 6, railClipboardY())));
    if (chosen == clearAction)
        m_clipboardHistory.clear();
}

void StartMenu::updateRailTooltip()
{
    QString text;
    int yCenter = 0;
    if (m_hoveredRailApp >= 0 && m_hoveredRailApp < m_railApps.size()) {
        text = m_railApps[m_hoveredRailApp].name;
        yCenter = railAppY(m_hoveredRailApp);
    } else {
        switch (m_hoveredRailBtn) {
        case RailButton::Avatar:
            text = QString::fromUtf8("个人中心");
            yCenter = 50;
            break;
        case RailButton::Documents:
            text = QString::fromUtf8("文档");
            yCenter = railDocumentsY();
            break;
        case RailButton::Clipboard:
            text = QString::fromUtf8("剪贴板历史");
            yCenter = railClipboardY();
            break;
        case RailButton::Theme:
            text = QString::fromUtf8("快速切换主题");
            yCenter = railThemeY();
            break;
        case RailButton::Settings:
            text = QString::fromUtf8("设置");
            yCenter = railSettingsY();
            break;
        case RailButton::Power:
            text = QString::fromUtf8("电源");
            yCenter = railPowerY();
            break;
        default:
            break;
        }
    }

    if (text.isEmpty()) {
        LiquidPopup::hideText();
        return;
    }

    const QPoint globalPos = mapToGlobal(
        QPoint(StartMenuTheme::kRailWidth + 10, yCenter + 12));
    LiquidPopup::showText(globalPos, text, this,
                       QRect(0, yCenter - 20, StartMenuTheme::kRailWidth, 40),
                       10000);
}

void StartMenu::cycleQuickTheme()
{
    const QList<Skin> quickThemes = { Skin::EcoLiquid, Skin::Dark, Skin::Light };
    int current = quickThemes.indexOf(m_skin);
    const Skin next = current < 0
        ? Skin::EcoLiquid
        : quickThemes[(current + 1) % quickThemes.size()];
    StartMenuConfig::instance().setSkin(next);
    applySkin(next);
}

void StartMenu::showThemeMenu()
{
    QMenu menu(this);
    StartMenuTheme::applySharedMenuStyle(&menu);

    const QList<QPair<Skin, QString>> themes = {
        { Skin::EcoLiquid, QString::fromUtf8("极简液态") },
        { Skin::Dark, QString::fromUtf8("深色") },
        { Skin::Light, QString::fromUtf8("浅色") },
        { Skin::Cyber, QString::fromUtf8("赛博") },
        { Skin::Glass, QString::fromUtf8("玻璃") },
        { Skin::Wallpaper, QString::fromUtf8("壁纸色") },
    };

    QList<QPair<QAction *, Skin>> actions;
    for (const auto &theme : themes) {
        QAction *action = menu.addAction(themedIcon({"preferences-desktop-theme",
                                                      "preferences-desktop-color"}),
                                          theme.second);
        action->setCheckable(true);
        action->setChecked(theme.first == m_skin);
        actions.append(qMakePair(action, theme.first));
    }

    QAction *chosen = menu.exec(mapToGlobal(
        QPoint(StartMenuTheme::kRailWidth + 6, railThemeY())));
    for (const auto &entry : actions) {
        if (chosen == entry.first) {
            StartMenuConfig::instance().setSkin(entry.second);
            applySkin(entry.second);
            break;
        }
    }
}

void StartMenu::showMoreRecent()
{
    m_appSortMode = AppSortMode::LastUsed;
    if (m_searchEdit)
        m_searchEdit->clear();
    showAllAppsView();
}

void StartMenu::mousePressEvent(QMouseEvent *e)
{
    if (e->pos().x() < StartMenuTheme::kRailWidth) {
        if (e->button() == Qt::LeftButton) {
            const int appIndex = railAppIndexAt(e->pos());
            if (appIndex >= 0) {
                m_pressedRailApp = appIndex;
                m_railDragStartPos = e->pos();
                e->accept();
                return;
            }
            handleRailClick(e->pos());
            e->accept();
            return;
        }
        if (e->button() == Qt::RightButton) {
            handleRailContextMenu(e->pos());
            e->accept();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void StartMenu::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_pressedRailApp >= 0) {
        const int appIndex = m_pressedRailApp;
        m_pressedRailApp = -1;
        if (appIndex < m_railApps.size() && railAppIndexAt(e->pos()) == appIndex)
            launchAppEntry(m_railApps[appIndex]);
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void StartMenu::mouseMoveEvent(QMouseEvent *e)
{
    if (m_pressedRailApp >= 0
        && (e->buttons() & Qt::LeftButton)
        && (e->pos() - m_railDragStartPos).manhattanLength()
             >= QApplication::startDragDistance()) {
        const int appIndex = m_pressedRailApp;
        m_pressedRailApp = -1;
        if (appIndex >= 0 && appIndex < m_railApps.size()) {
            auto *mimeData = new QMimeData;
            mimeData->setData(QStringLiteral("application/x-ukui-kaishicaidan-app-path"),
                              m_railApps[appIndex].desktopPath.toUtf8());

            auto *drag = new QDrag(this);
            drag->setMimeData(mimeData);
            const QIcon icon = iconForDesktopIcon(m_railApps[appIndex].name,
                                                  m_railApps[appIndex].iconName,
                                                  m_railApps[appIndex].desktopPath);
            drag->setPixmap(icon.pixmap(32, 32));
            drag->setHotSpot(QPoint(16, 16));
            drag->exec(Qt::MoveAction);
        }
        e->accept();
        return;
    }

    QWidget::mouseMoveEvent(e);
    m_pointerHasEnteredMenu = true;

    const int cx = StartMenuTheme::kRailWidth / 2;
    const auto hit = [e](int cxp, int y) {
        const QRect rc(cxp - 18, y - 18, 36, 36);
        return rc.contains(e->pos());
    };

    const int newRailAppHover = railAppIndexAt(e->pos());
    RailButton newHover = RailButton::NoButton;
    if (newRailAppHover < 0) {
        if (hit(cx, 50)) newHover = RailButton::Avatar;
        else if (hit(cx, railDocumentsY())) newHover = RailButton::Documents;
        else if (hit(cx, railClipboardY())) newHover = RailButton::Clipboard;
        else if (hit(cx, railThemeY())) newHover = RailButton::Theme;
        else if (hit(cx, railSettingsY())) newHover = RailButton::Settings;
        else if (hit(cx, railPowerY())) newHover = RailButton::Power;
    }

    if (newHover != m_hoveredRailBtn || newRailAppHover != m_hoveredRailApp) {
        m_hoveredRailBtn = newHover;
        m_hoveredRailApp = newRailAppHover;
        updateRailTooltip();
        update();
    }
}

void StartMenu::dragEnterEvent(QDragEnterEvent *e)
{
    if (!e->mimeData()->hasFormat(QStringLiteral("application/x-ukui-kaishicaidan-app-path"))) {
        e->ignore();
        return;
    }

    // Accept the drag as soon as it starts over any child of the menu. If the
    // initial enter is rejected while still over the app grid, Qt will not
    // reliably promote the same drag to the parent when it reaches the rail.
    e->setDropAction(Qt::MoveAction);
    e->accept();
}

void StartMenu::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(QStringLiteral("application/x-ukui-kaishicaidan-app-path"))) {
        e->ignore();
        return;
    }

    const QString path = QString::fromUtf8(
        e->mimeData()->data(QStringLiteral("application/x-ukui-kaishicaidan-app-path")));
    const bool alreadyPinned = std::any_of(m_railApps.cbegin(), m_railApps.cend(),
        [&path](const AppEntry &app) { return app.desktopPath == path; });
    const int target = railDropIndexAt(e->pos());
    const int newDropIndex = (target >= 0
                              && (alreadyPinned || m_railApps.size() < kMaxRailApps))
        ? target : -1;
    if (newDropIndex != m_railDropIndex) {
        m_railDropIndex = newDropIndex;
        update();
    }

    if (m_railDropIndex >= 0) {
        e->setDropAction(Qt::MoveAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void StartMenu::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_railDropIndex = -1;
    update();
    e->accept();
}

void StartMenu::dropEvent(QDropEvent *e)
{
    const QString mimeType = QStringLiteral("application/x-ukui-kaishicaidan-app-path");
    if (!e->mimeData()->hasFormat(mimeType)) {
        e->ignore();
        return;
    }

    const QString path = QString::fromUtf8(e->mimeData()->data(mimeType));
    const int targetSlot = railDropIndexAt(e->pos());
    m_railDropIndex = -1;

    if (targetSlot < 0) {
        update();
        e->ignore();
        return;
    }

    int existingIndex = -1;
    for (int i = 0; i < m_railApps.size(); ++i) {
        if (m_railApps[i].desktopPath == path) {
            existingIndex = i;
            break;
        }
    }

    if (existingIndex >= 0) {
        const int destination = qBound(0, targetSlot, m_railApps.size() - 1);
        if (existingIndex != destination)
            m_railApps.move(existingIndex, destination);
    } else {
        if (m_railApps.size() >= kMaxRailApps) {
            update();
            e->ignore();
            return;
        }

        const auto it = std::find_if(m_allApps.cbegin(), m_allApps.cend(),
            [&path](const AppEntry &app) { return app.desktopPath == path; });
        if (it == m_allApps.cend()) {
            update();
            e->ignore();
            return;
        }
        m_railApps.insert(qMin(targetSlot, m_railApps.size()), *it);
    }

    saveRailApps();
    e->setDropAction(Qt::MoveAction);
    e->accept();
}

void StartMenu::enterEvent(QEvent *e)
{
    QWidget::enterEvent(e);
    m_pointerHasEnteredMenu = true;
}

void StartMenu::leaveEvent(QEvent *e)
{
    QWidget::leaveEvent(e);
    if (m_hoveredRailBtn != RailButton::NoButton || m_hoveredRailApp >= 0) {
        m_hoveredRailBtn = RailButton::NoButton;
        m_hoveredRailApp = -1;
        LiquidPopup::hideText();
        update();
    }
}

void StartMenu::toggle()
{
    if (m_visible) {
        hideMenu();
    } else {
        showMenu();
    }
}

void StartMenu::showMenu()
{
    // Repeated --show must never recapture the menu into its own backdrop.
    if (isVisible()) {
        raise();
        activateWindow();
        return;
    }
    // Desktop file/directory watchers own the installed-app refresh. Opening
    // only reloads the small usage record, not every application on disk.
    AppRegistry::refreshUsageMetadata(m_allApps);
    sortAllApps();
    if (m_showingAllApps && m_appSortMode == AppSortMode::LastUsed)
        setupPinnedGrid();
    setupRecentFiles();
    refreshUserAvatar();
    positionAboveStartButton();
    // Only capture on explicit opening, while still hidden. Unchanged pixels
    // reuse the material; never keep the first desktop snapshot indefinitely.
    if (m_skin == Skin::EcoLiquid && m_nextKdeGlassView) {
        captureNextKdeBackdrop();
    }
    applyX11Immunity();

    const bool instantLiquid = m_skin == Skin::EcoLiquid;
    setWindowOpacity(instantLiquid ? 1.0 : 0.0);
    show();
    raise();
    activateWindow();

    // Re-position after show to override window manager map positioning policies
    positionAboveStartButton();

    const QPoint endPos = pos();
    const TaskbarInfo info = TaskbarDetector::detect();
    QPoint startOffset(0, 0);
    if (info.detected) {
        if (info.edge == 3) startOffset = QPoint(0, 12);
        else if (info.edge == 2) startOffset = QPoint(0, -12);
        else if (info.edge == 0) startOffset = QPoint(-12, 0);
        else if (info.edge == 1) startOffset = QPoint(12, 0);
    } else {
        startOffset = QPoint(0, 12);
    }

    // A frozen backdrop must remain registered to its capture coordinates.
    if (m_skin == Skin::EcoLiquid) startOffset = QPoint();
    move(endPos + startOffset);

    if (!instantLiquid) {
        auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(160);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);

        auto *slide = new QPropertyAnimation(this, "pos", this);
        slide->setDuration(180);
        slide->setStartValue(endPos + startOffset);
        slide->setEndValue(endPos);
        slide->setEasingCurve(QEasingCurve::OutCubic);

        fade->start(QAbstractAnimation::DeleteWhenStopped);
        slide->start(QAbstractAnimation::DeleteWhenStopped);
    }

    m_visible = true;
    m_showTime.start();
    m_pointerHasEnteredMenu = geometry().adjusted(-4, -4, 4, 4).contains(QCursor::pos());
    m_searchEdit->clear();
    m_searchEdit->setFocus();

    // Start mouse-position fallback watcher (belt-and-suspenders;
    // the primary close mechanism is ActivationChange in changeEvent).
    QTimer::singleShot(350, this, [this] {
        if (m_visible)
            startOutsideWatch();
    });
}

void StartMenu::hideMenu()
{
    stopOutsideWatch();
    m_visible = false;
    m_pointerHasEnteredMenu = false;
    m_showTime.invalidate();
    hide();
}

void StartMenu::quitApp()
{
    QApplication::quit();
}

void StartMenu::openSettings()
{
    if (m_settingsOpen) return;
    m_settingsOpen = true;
    hideMenu();
    SettingsDialog dlg(this);
    dlg.exec();
    m_settingsOpen = false;
}

void StartMenu::openSystemSettings()
{
    startFirstAvailable({
        {"ukui-control-center", QStringList()},
        {"mate-control-center", QStringList()},
        {"gnome-control-center", QStringList()},
        {"systemsettings5", QStringList()},
        {"systemsettings", QStringList()},
    });
    hideMenu();
}

void StartMenu::openAboutKylin()
{
    startFirstAvailable({
        {"ukui-control-center", QStringList() << "-m" << "about"},
        {"ukui-control-center", QStringList() << "-p" << "about"},
        {"ukui-control-center", QStringList()},
        {"gnome-control-center", QStringList() << "about"},
        {"mate-about", QStringList()},
    });
    hideMenu();
}

void StartMenu::lockScreen()
{
    startFirstAvailable({
        {"ukui-screensaver-command", QStringList() << "-l"},
        {"loginctl", QStringList() << "lock-session"},
    });
    hideMenu();
}

void StartMenu::suspendSystem()
{
    QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << "suspend");
    hideMenu();
}

void StartMenu::hibernateSystem()
{
    QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << "hibernate");
    hideMenu();
}

void StartMenu::hybridSleepSystem()
{
    QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << "hybrid-sleep");
    hideMenu();
}

void StartMenu::logoutSession()
{
    startFirstAvailable({
        {"ukui-session", QStringList() << "--logout"},
        {"mate-session-save", QStringList() << "--logout-dialog"},
    });
    hideMenu();
}

void StartMenu::rebootSystem()
{
    QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << "reboot");
    hideMenu();
}

void StartMenu::poweroffSystem()
{
    QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << "poweroff");
    hideMenu();
}

void StartMenu::showPowerMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    StartMenuTheme::applySharedMenuStyle(&menu);

    auto *actLock = menu.addAction(themedIcon({"system-lock-screen", "changes-prevent", "object-locked"}),
                                   QString::fromUtf8("锁屏"));
    auto *actSuspend = menu.addAction(themedIcon({"system-suspend", "media-playback-pause", "appointment-soon"}),
                                      QString::fromUtf8("睡眠"));
    auto *actHibernate = menu.addAction(themedIcon({"system-suspend-hibernate", "weather-clear-night", "system-suspend"}),
                                        QString::fromUtf8("休眠"));
    auto *actHybridSleep = menu.addAction(themedIcon({"system-suspend-hybrid", "weather-few-clouds-night", "system-suspend"}),
                                          QString::fromUtf8("混合睡眠"));
    auto *actLogout = menu.addAction(themedIcon({"system-log-out", "application-exit", "go-previous"}),
                                     QString::fromUtf8("注销"));
    menu.addSeparator();
    auto *actReboot = menu.addAction(themedIcon({"system-reboot", "view-refresh", "reload"}),
                                     QString::fromUtf8("重启"));
    auto *actPower = menu.addAction(themedIcon({"system-shutdown", "system-shutdown-panel", "gtk-quit"}),
                                    QString::fromUtf8("关机"));

    QAction *chosen = menu.exec(globalPos);
    if (chosen == actLock)
        lockScreen();
    else if (chosen == actSuspend)
        suspendSystem();
    else if (chosen == actHibernate)
        hibernateSystem();
    else if (chosen == actHybridSleep)
        hybridSleepSystem();
    else if (chosen == actLogout)
        logoutSession();
    else if (chosen == actReboot)
        rebootSystem();
    else if (chosen == actPower)
        poweroffSystem();
}

StartMenu::AppRemovalTarget StartMenu::detectAppRemovalTarget(const AppEntry &app)
{
    AppRemovalTarget result;
    const QString desktopPath = QFileInfo(app.desktopPath).absoluteFilePath();
    const QString desktopName = QFileInfo(desktopPath).fileName();

    const QString bundleRoot = folderBundleForDesktop(desktopPath);
    if (!bundleRoot.isEmpty() && isSafeFolderBundle(bundleRoot)) {
        result.kind = AppRemovalTarget::Kind::FolderBundle;
        result.target = bundleRoot;
        result.actionText = QString::fromUtf8("移到回收站（文件夹版）");
        result.detail = bundleRoot;
        return result;
    }

    QStringList packageCandidates = {desktopPath};
    if (!desktopName.isEmpty()) {
        packageCandidates << QStringLiteral("/usr/share/applications/") + desktopName
                          << QStringLiteral("/usr/local/share/applications/") + desktopName;
    }
    const QString executablePath = executablePathFromDesktopExec(app.exec);
    if (!executablePath.isEmpty())
        packageCandidates << executablePath;

    QString package;
    for (const QString &candidate : packageCandidates) {
        package = debPackageForFile(candidate);
        if (!package.isEmpty())
            break;
    }
    if (!package.isEmpty()) {
        result.target = package;
        result.detail = QString::fromUtf8("deb 软件包：%1").arg(package);
        if (isProtectedDebPackage(package)) {
            result.kind = AppRemovalTarget::Kind::ProtectedPackage;
            result.actionText = QString::fromUtf8("系统组件，无法卸载");
        } else {
            result.kind = AppRemovalTarget::Kind::DebPackage;
            result.actionText = QString::fromUtf8("卸载 deb 软件包");
        }
        return result;
    }

    if (isUserDesktopShortcut(desktopPath)) {
        result.kind = AppRemovalTarget::Kind::DesktopShortcut;
        result.target = desktopPath;
        result.actionText = QString::fromUtf8("移除快捷方式");
        result.detail = QString::fromUtf8("只移除菜单入口，不删除程序文件");
        return result;
    }

    result.kind = AppRemovalTarget::Kind::Unsupported;
    if (desktopPath.contains(QStringLiteral("/flatpak/"))) {
        result.actionText = QString::fromUtf8("请在软件商店卸载（Flatpak）");
        result.detail = QString::fromUtf8("第一版暂不自动卸载 Flatpak 应用");
    } else if (desktopPath.contains(QStringLiteral("/snapd/"))) {
        result.actionText = QString::fromUtf8("请在软件商店卸载（Snap）");
        result.detail = QString::fromUtf8("第一版暂不自动卸载 Snap 应用");
    } else {
        result.actionText = QString::fromUtf8("无法判断安装来源");
        result.detail = desktopPath;
    }
    return result;
}

void StartMenu::removeApp(const AppEntry &app, const AppRemovalTarget &target)
{
    if (target.kind == AppRemovalTarget::Kind::DesktopShortcut) {
        if (!isUserDesktopShortcut(target.target))
            return;
        const auto reply = QMessageBox::question(
            this, QString::fromUtf8("移除快捷方式"),
            QString::fromUtf8("只从开始菜单移除 %1 的快捷方式，不会删除程序文件。\n\n%2")
                .arg(app.name, target.target),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;

        if (!QFile::remove(target.target)) {
            QMessageBox::warning(this, QString::fromUtf8("移除失败"),
                                 QString::fromUtf8("无法删除快捷方式：\n%1").arg(target.target));
            return;
        }
        AppRegistry::unpinApp(app.desktopPath);
        rebuildAppList();
        return;
    }

    if (target.kind == AppRemovalTarget::Kind::FolderBundle) {
        if (!isSafeFolderBundle(target.target)) {
            QMessageBox::warning(this, QString::fromUtf8("无法删除"),
                                 QString::fromUtf8("程序目录未通过安全检查。"));
            return;
        }
        if (QStandardPaths::findExecutable(QStringLiteral("gio")).isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("无法删除"),
                                 QString::fromUtf8("系统缺少回收站工具 gio，未删除任何文件。"));
            return;
        }

        const auto reply = QMessageBox::question(
            this, QString::fromUtf8("移到回收站"),
            QString::fromUtf8("将 %1 的整个程序文件夹移到回收站？\n\n%2")
                .arg(app.name, target.target),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;

        const QString desktopPath = app.desktopPath;
        const QString appName = app.name;
        auto *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process, desktopPath, appName](int exitCode, QProcess::ExitStatus status) {
            const QString output = conciseProcessError(
                QString::fromUtf8(process->readAllStandardOutput()));
            process->deleteLater();
            if (status == QProcess::NormalExit && exitCode == 0) {
                if (isUserDesktopShortcut(desktopPath))
                    QFile::remove(desktopPath);
                AppRegistry::unpinApp(desktopPath);
                rebuildAppList();
                QMessageBox::information(this, QString::fromUtf8("已移到回收站"),
                                         QString::fromUtf8("%1 已移到回收站。").arg(appName));
            } else {
                QMessageBox::warning(this, QString::fromUtf8("操作失败"),
                    output.isEmpty() ? QString::fromUtf8("无法将程序文件夹移到回收站。") : output);
            }
        });
        connect(process, &QProcess::errorOccurred, this,
                [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                QMessageBox::warning(this, QString::fromUtf8("操作失败"), process->errorString());
                process->deleteLater();
            }
        });
        hideMenu();
        process->start(QStringLiteral("gio"), {QStringLiteral("trash"), target.target});
        return;
    }

    if (target.kind != AppRemovalTarget::Kind::DebPackage)
        return;

    if (m_removalPending) return;
    m_removalPending = true;
    BackgroundTask::run(this, [target] {
        QStringList removedPackages; QString error;
        if (!simulateDebRemoval(target.target, &removedPackages, &error))
            return qMakePair(QStringList(), QString("无法检查卸载影响：")+conciseProcessError(error));
        for (const auto &package:removedPackages)
            if (isProtectedDebPackage(package)) return qMakePair(QStringList(),QString("卸载涉及系统关键组件，已阻止：")+package);
        return qMakePair(removedPackages,QString());
    }, [this, app, target](const QPair<QStringList,QString> &result) {
        m_removalPending = false;
        if (!result.second.isEmpty()) { QMessageBox::warning(this,"无法卸载",result.second); return; }
        confirmDebRemoval(app,target,result.first);
    });
}

void StartMenu::confirmDebRemoval(const AppEntry &app, const AppRemovalTarget &target, const QStringList &removedPackages)
{
    QString impactText;
    QStringList additionalPackages = removedPackages;
    additionalPackages.removeAll(target.target);
    additionalPackages.removeAll(target.target.section(QLatin1Char(':'), 0, 0));
    if (!additionalPackages.isEmpty()) {
        const int shownCount = qMin(8, additionalPackages.size());
        impactText = QString::fromUtf8("\n\n还会移除 %1 个相关软件包：\n%2")
            .arg(additionalPackages.size())
            .arg(additionalPackages.mid(0, shownCount).join(QLatin1Char('\n')));
        if (additionalPackages.size() > shownCount)
            impactText += QString::fromUtf8("\n……");
    }

    const auto reply = QMessageBox::question(
        this, QString::fromUtf8("确认卸载"),
        QString::fromUtf8("卸载 %1？\n\n软件包：%2\n应用配置将保留。%3")
            .arg(app.name, target.target, impactText),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    if (QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("无法卸载"),
                             QString::fromUtf8("系统缺少管理员授权工具 pkexec。"));
        return;
    }

    const QString desktopPath = app.desktopPath;
    const QString appName = app.name;
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, desktopPath, appName](int exitCode, QProcess::ExitStatus status) {
        const QString output = conciseProcessError(
            QString::fromUtf8(process->readAllStandardOutput()));
        process->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            if (isUserDesktopShortcut(desktopPath))
                QFile::remove(desktopPath);
            AppRegistry::unpinApp(desktopPath);
            rebuildAppList();
            QMessageBox::information(this, QString::fromUtf8("卸载完成"),
                                     QString::fromUtf8("%1 已卸载。").arg(appName));
        } else {
            QMessageBox::warning(
                this, QString::fromUtf8("卸载未完成"),
                output.isEmpty() ? QString::fromUtf8("授权被取消或卸载命令执行失败。") : output);
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QMessageBox::warning(this, QString::fromUtf8("卸载失败"), process->errorString());
            process->deleteLater();
        }
    });
    hideMenu();
    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral("apt-get"), QStringLiteral("remove"),
                    QStringLiteral("-y"), QStringLiteral("--"), target.target});
}

void StartMenu::showAppContextMenu(const AppEntry &app, const QPoint &globalPos)
{
    if (app.desktopPath.isEmpty())
        return;

    QMenu menu(this);
    StartMenuTheme::applySharedMenuStyle(&menu);

    QAction *actPin = menu.addAction(
        themedIcon({app.pinned ? "list-remove" : "list-add", app.pinned ? "edit-delete" : "emblem-favorite", "emblem-symbolic-link"}),
        app.pinned
        ? QString::fromUtf8("从已固定取消")
        : QString::fromUtf8("固定到已固定"));
    QAction *actOpen = menu.addAction(themedIcon({"document-open", "system-run", "go-next"}),
                                      QString::fromUtf8("打开"));
    QAction *actOpenDir = menu.addAction(themedIcon({"folder-open", "folder", "inode-directory"}),
                                         QString::fromUtf8("打开目录"));
    QAction *actModifyIcon = menu.addAction(themedIcon({"preferences-desktop-icons", "insert-image", "applications-graphics"}),
                                            QString::fromUtf8("修改图标"));
    menu.addSeparator();
    auto removalTarget = std::make_shared<AppRemovalTarget>();
    QAction *actUninstall = menu.addAction(themedIcon({"edit-delete"}), "正在检查安装来源…");
    actUninstall->setEnabled(false);
    // Keep the rest of the menu usable while dpkg is consulted.
    const QPointer<QAction> actionGuard(actUninstall);
    if (!m_removalPending) {
        m_removalPending = true;
        BackgroundTask::run(this, [app] { return detectAppRemovalTarget(app); },
            [this, actionGuard, removalTarget](const AppRemovalTarget &target) {
                m_removalPending = false; *removalTarget = target;
                if (!actionGuard) return;
                actionGuard->setText(target.actionText); actionGuard->setToolTip(target.detail);
                actionGuard->setEnabled(target.kind==AppRemovalTarget::Kind::DebPackage || target.kind==AppRemovalTarget::Kind::FolderBundle || target.kind==AppRemovalTarget::Kind::DesktopShortcut);
            });
    } else actUninstall->setText("正在检查其他应用，请稍后重开菜单");

    QAction *chosen = menu.exec(globalPos);
    if (chosen == actPin) {
        if (app.pinned)
            AppRegistry::unpinApp(app.desktopPath);
        else
            AppRegistry::pinApp(app.desktopPath);
        rebuildAppList();
    } else if (chosen == actOpen) {
        launchAppEntry(app);
    } else if (chosen == actOpenDir) {
        QString dir = QFileInfo(app.desktopPath).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    } else if (chosen == actModifyIcon) {
        QString newIconPath = QFileDialog::getOpenFileName(this,
            QString::fromUtf8("选择新图标"),
            QDir::homePath(),
            QString::fromUtf8("Images (*.png *.jpg *.jpeg *.svg *.xpm);;All Files (*)"));
        if (!newIconPath.isEmpty()) {
            QString targetPath = app.desktopPath;
            if (targetPath.startsWith(QLatin1String("/usr/share/applications/"))) {
                QString localDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
                QDir().mkpath(localDir);
                QString fileName = QFileInfo(targetPath).fileName();
                QString localPath = localDir + "/" + fileName;
                if (!QFile::exists(localPath)) {
                    QFile::copy(targetPath, localPath);
                }
                targetPath = localPath;
            }
            QSettings settings(targetPath, QSettings::IniFormat);
            settings.setIniCodec("UTF-8");
            settings.setValue(QStringLiteral("Desktop Entry/Icon"), newIconPath);
            settings.sync();
            rebuildAppList();
        }
    } else if (chosen == actUninstall) {
        removeApp(app, *removalTarget);
    }
}

void StartMenu::showPinnedView()
{
    m_filteredApps = m_pinnedApps;
    m_showingAllApps = false;
    if (m_pinnedHeader) {
        m_pinnedHeader->setTitle(QString::fromUtf8("已固定"));
        m_pinnedHeader->m_showBack = false;
        m_pinnedHeader->setAction(QString::fromUtf8("所有应用 >"));
        m_pinnedHeader->onActionClicked = [this] { showAllAppsView(); };
    }
    setupPinnedGrid();
}

void StartMenu::showAllAppsView()
{
    sortAllApps();
    m_filteredApps = m_allApps;
    m_showingAllApps = true;
    if (m_pinnedHeader) {
        m_pinnedHeader->setTitle(QString::fromUtf8("所有应用"));
        m_pinnedHeader->m_showBack = true;
        m_pinnedHeader->setAction(sortModeName());
        m_pinnedHeader->onBackClicked = [this] { showPinnedView(); };
        m_pinnedHeader->onActionClicked = [this] { showAllAppsHeaderMenu(); };
    }
    setupPinnedGrid();
}

void StartMenu::showAllAppsHeaderMenu()
{
    QMenu menu(this);
    StartMenuTheme::applySharedMenuStyle(&menu);

    auto *actBack = menu.addAction(themedIcon({"go-previous", "go-home", "view-grid"}),
                                   QString::fromUtf8("返回已固定"));
    menu.addSeparator();
    auto *actAlpha = menu.addAction(themedIcon({"view-sort-ascending", "format-text-direction-ltr", "font-x-generic"}),
                                    QString::fromUtf8("按字母排序"));
    auto *actInstall = menu.addAction(themedIcon({"document-new", "software-install", "package-x-generic"}),
                                      QString::fromUtf8("按安装时间排序"));
    auto *actLastUsed = menu.addAction(themedIcon({"document-open-recent", "appointment-soon", "office-calendar"}),
                                       QString::fromUtf8("按最后使用时间排序"));
    actAlpha->setCheckable(true);
    actInstall->setCheckable(true);
    actLastUsed->setCheckable(true);
    actAlpha->setChecked(m_appSortMode == AppSortMode::Alphabetical);
    actInstall->setChecked(m_appSortMode == AppSortMode::InstallTime);
    actLastUsed->setChecked(m_appSortMode == AppSortMode::LastUsed);

    QAction *chosen = menu.exec(m_pinnedHeader->mapToGlobal(QPoint(m_pinnedHeader->width() - 120,
                                                                   m_pinnedHeader->height())));
    if (chosen == actBack) {
        showPinnedView();
    } else if (chosen == actAlpha) {
        m_appSortMode = AppSortMode::Alphabetical;
        showAllAppsView();
    } else if (chosen == actInstall) {
        m_appSortMode = AppSortMode::InstallTime;
        showAllAppsView();
    } else if (chosen == actLastUsed) {
        m_appSortMode = AppSortMode::LastUsed;
        showAllAppsView();
    }
}

void StartMenu::sortAllApps()
{
    std::sort(m_allApps.begin(), m_allApps.end(), [this](const AppEntry &a, const AppEntry &b) {
        switch (m_appSortMode) {
        case AppSortMode::InstallTime:
            if (a.installTime != b.installTime)
                return a.installTime > b.installTime;
            break;
        case AppSortMode::LastUsed:
            if (a.lastUsed.isValid() != b.lastUsed.isValid())
                return a.lastUsed.isValid();
            if (a.lastUsed.isValid() && a.lastUsed != b.lastUsed)
                return a.lastUsed > b.lastUsed;
            if (a.launchCount != b.launchCount)
                return a.launchCount > b.launchCount;
            break;
        case AppSortMode::Alphabetical:
            break;
        }
        return a.name.toLower() < b.name.toLower();
    });
}

QString StartMenu::sortModeName() const
{
    switch (m_appSortMode) {
    case AppSortMode::InstallTime:
        return QString::fromUtf8("安装");
    case AppSortMode::LastUsed:
        return QString::fromUtf8("最近");
    case AppSortMode::Alphabetical:
    default:
        return QString::fromUtf8("字母");
    }
}

QList<AppEntry> StartMenu::displayedApps() const
{
    if (!m_searchEdit || !m_searchEdit->text().isEmpty())
        return m_filteredApps;
    if (m_showingAllApps)
        return m_allApps;
    return m_pinnedApps;
}

void StartMenu::startOutsideWatch()
{
    // This is a fallback safety net. The primary close mechanism is
    // ActivationChange in changeEvent(). This timer catches edge cases
    // where the WM doesn't send deactivation (e.g. clicking on DOCK
    // windows like the taskbar, or the desktop background).
    if (!m_outsideTimer) {
        m_outsideTimer = new QTimer(this);
        m_outsideTimer->setInterval(160);
        connect(m_outsideTimer, &QTimer::timeout, this, [this] {
            if (!m_visible || m_settingsOpen) return;
            if (QApplication::activePopupWidget()) return;
            if (m_showTime.isValid() && m_showTime.elapsed() < 450)
                return;

            bool mousePressed = false;
            if (isX11Platform()) {
                if (!m_pointerDisplay)
                    m_pointerDisplay = XOpenDisplay(nullptr);
                if (m_pointerDisplay) {
                    const Window root = DefaultRootWindow(m_pointerDisplay);
                    Window root_ret, child_ret;
                    int root_x, root_y, win_x, win_y;
                    unsigned int mask;
                    if (XQueryPointer(m_pointerDisplay, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
                        if (mask & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)) {
                            mousePressed = true;
                        }
                    } else {
                        XCloseDisplay(m_pointerDisplay);
                        m_pointerDisplay = nullptr;
                    }
                }
            } else {
                mousePressed = (QGuiApplication::mouseButtons() != Qt::NoButton);
            }

            // Cache taskbar geometry for 2 seconds to avoid frequent scanning
            if (!m_taskbarCacheTimer.isValid() || m_taskbarCacheTimer.elapsed() > 1500) {
                m_cachedTaskbarInfo = TaskbarDetector::detect();
                m_taskbarCacheTimer.restart();
            }

            const QPoint cursor = QCursor::pos();
            const QRect menuGeom = geometry();

            // Keep a small grace zone so the menu does not flicker at edges.
            const QRect menuKeepAlive = menuGeom.adjusted(-12, -12, 12, 12);
            if (menuKeepAlive.contains(cursor)) {
                m_pointerHasEnteredMenu = true;
                return;
            }

            // Don't close if cursor is near the start button
            if (m_cachedTaskbarInfo.detected) {
                QRect btnArea(m_cachedTaskbarInfo.startButtonCenter.x() - 34,
                              m_cachedTaskbarInfo.startButtonCenter.y() - 34, 68, 68);
                if (btnArea.contains(cursor))
                    return;
            }

            if (mousePressed)
                hideMenu();
        });
    }
    m_taskbarCacheTimer.invalidate(); // Force refresh on first tick
    m_outsideTimer->start();
}

void StartMenu::stopOutsideWatch()
{
    if (m_outsideTimer)
        m_outsideTimer->stop();
}

void StartMenu::showEvent(QShowEvent *e)
{
    if (m_glassLightTimer) {
        m_glassLightPos=mapFromGlobal(QCursor::pos());
        m_glassLightTimer->start();
    }
    QWidget::showEvent(e);
    positionAboveStartButton();
    applyX11Immunity();
}

void StartMenu::hideEvent(QHideEvent *e)
{
    if (m_glassLightTimer) m_glassLightTimer->stop();
    QWidget::hideEvent(e);
    m_visible = false;
    m_pointerHasEnteredMenu = false;
    m_showTime.invalidate();
    LiquidPopup::hideText();
    stopOutsideWatch();

}

void StartMenu::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        hideMenu();
        e->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_searchDebounceTimer && m_searchDebounceTimer->isActive()) {
            m_searchDebounceTimer->stop();
            filterApps(m_pendingSearchText);
        }
        if (!m_filteredApps.isEmpty() && m_searchEdit->hasFocus()) {
            launchApp(0);
        }
        e->accept();
        return;
    default:
        QWidget::keyPressEvent(e);
    }
}
