#include "AppRegistry.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>
#include <QApplication>
#include <QIcon>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QXmlStreamReader>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QRegularExpression>

namespace {

#if defined(UKUI_KAISHICAIDAN_V2)
constexpr const char *kConfigDir = "ukui-kaishicaidan-v2";
#else
constexpr const char *kConfigDir = "ukui-kaishicaidan";
#endif

// 当用户尚未配置任何固定应用时，使用的默认固定应用（按 .desktop 文件名）
// 这些是 UKUI/Kylin 上常见的核心应用
const QStringList kDefaultPinnedDesktops = {
    "kylin-browser.desktop",
    "firefox.desktop",
    "chrome.desktop",
    "google-chrome.desktop",
    "org.kde.konsole.desktop",
    "mate-terminal.desktop",
    "ukui-terminal.desktop",
    "peony.desktop",
    "kylin-software-center.desktop",
    "ukui-control-center.desktop",
    "kylin-assistant.desktop",
    "ukui-system-monitor.desktop",
    "org.kde.kcalc.desktop",
    "kalarm.desktop",
    "elisa.desktop",
    "vlc.desktop",
    "org.kde.okular.desktop",
    "wps-office-wps.desktop",
    "wps-office-et.desktop",
    "wps-office-wpp.desktop",
    "org.gnome.Calculator.desktop",
    "thunderbird.desktop",
    "org.kde.kmail2.desktop",
    "gimp.desktop",
};

QStringList defaultPinnedPaths()
{
    QStringList paths;
    const QStringList dirs = AppRegistry::applicationDirs();
    for (const QString &d : dirs) {
        for (const QString &name : kDefaultPinnedDesktops) {
            const QString path = d + "/" + name;
            if (QFile::exists(path) && !paths.contains(path))
                paths.append(path);
        }
    }
    return paths;
}

QString cleanupExec(QString exec)
{
    exec.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    exec.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    exec.remove('\'');
    exec.remove('"');
    exec = exec.trimmed();

    QStringList parts = exec.split(QRegularExpression(QStringLiteral("\\s+")),
                                   QString::SkipEmptyParts);
    while (!parts.isEmpty()) {
        const QString first = parts.first();
        if (first.contains('=') || first == QStringLiteral("env")
            || first == QStringLiteral("sh") || first == QStringLiteral("bash")
            || first == QStringLiteral("-c")) {
            parts.removeFirst();
            continue;
        }
        break;
    }
    if (parts.isEmpty())
        return QString();

    QString cmd = parts.first();
    if (cmd.startsWith(QStringLiteral("Exec=")))
        cmd = cmd.mid(5);
    return QFileInfo(cmd).fileName().toLower();
}

QString normalizedName(QString name)
{
    name = name.trimmed().toLower();
    name.remove(QRegularExpression(QStringLiteral("\\.(desktop|exe|app)$")));
    return name;
}

QString recentAppsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + "/" + QString::fromLatin1(kConfigDir) + "/recent-apps.conf";
}

QDateTime parseXbelTime(const QString &value)
{
    QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(value, Qt::ISODateWithMs);
    return dt;
}

void applyRecentMetadata(QList<AppEntry> &apps)
{
    QHash<QString, int> pathIndex;
    QMultiHash<QString, int> execIndex;
    QMultiHash<QString, int> nameIndex;

    for (int i = 0; i < apps.size(); ++i) {
        pathIndex.insert(apps[i].desktopPath, i);
        const QString exec = cleanupExec(apps[i].exec);
        if (!exec.isEmpty())
            execIndex.insert(exec, i);
        nameIndex.insert(normalizedName(apps[i].name), i);
    }

    const auto touch = [&apps](int idx, const QDateTime &when, int count) {
        if (idx < 0 || idx >= apps.size() || !when.isValid())
            return;
        if (!apps[idx].lastUsed.isValid() || when > apps[idx].lastUsed)
            apps[idx].lastUsed = when;
        apps[idx].launchCount += std::max(1, count);
    };

    QFile recentFile(recentAppsPath());
    if (recentFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&recentFile);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;

            QString path = line;
            QDateTime when = QFileInfo(recentAppsPath()).lastModified();
            const int tab = line.indexOf('\t');
            if (tab > 0) {
                when = QDateTime::fromString(line.left(tab), Qt::ISODate);
                path = line.mid(tab + 1).trimmed();
            }

            if (pathIndex.contains(path))
                touch(pathIndex.value(path), when, 1);
        }
    }

    QFile xbel(QDir::homePath() + "/.local/share/recently-used.xbel");
    if (!xbel.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QXmlStreamReader xml(&xbel);
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("application"))
            continue;

        const QXmlStreamAttributes attrs = xml.attributes();
        const QString exec = cleanupExec(attrs.value(QStringLiteral("exec")).toString());
        const QString name = normalizedName(attrs.value(QStringLiteral("name")).toString());
        const QDateTime when = parseXbelTime(attrs.value(QStringLiteral("modified")).toString());
        const int count = attrs.value(QStringLiteral("count")).toInt();

        QSet<int> matches;
        if (!exec.isEmpty()) {
            const auto indexes = execIndex.values(exec);
            for (int idx : indexes) matches.insert(idx);
        }
        if (!name.isEmpty()) {
            const auto indexes = nameIndex.values(name);
            for (int idx : indexes) matches.insert(idx);
        }

        for (int idx : matches)
            touch(idx, when, count);
    }
}

} // namespace

QStringList AppRegistry::applicationDirs()
{
    QStringList dirs;
    const auto addDir = [&dirs](const QString &path) {
        if (path.isEmpty()) return;
        const QString clean = QDir::cleanPath(path);
        if (!dirs.contains(clean))
            dirs.append(clean);
    };

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString dataHome = env.value("XDG_DATA_HOME");
    if (dataHome.isEmpty())
        dataHome = QDir::homePath() + "/.local/share";
    addDir(dataHome + "/applications");

    for (const QString &path : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        addDir(path + "/applications");

    const QString dataDirs = env.value("XDG_DATA_DIRS",
                                       QStringLiteral("/usr/local/share:/usr/share"));
    for (const QString &base : dataDirs.split(':', QString::SkipEmptyParts))
        addDir(base + "/applications");

    addDir("/var/lib/flatpak/exports/share/applications");
    addDir(dataHome + "/flatpak/exports/share/applications");
    addDir("/var/lib/snapd/desktop/applications");
    addDir(QDir::homePath() + "/Desktop");
    addDir(QDir::homePath() + "/桌面");

    // A few locally unpacked apps place desktop files under ~/应用/*/usr/share/applications.
    const QStringList localRoots = {
        QDir::homePath() + "/应用",
        QDir::homePath() + "/Applications",
        QDir::homePath() + "/apps",
    };
    for (const QString &rootPath : localRoots) {
        QDir root(rootPath);
        if (!root.exists()) continue;
        const QFileInfoList children = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &child : children)
            addDir(child.absoluteFilePath() + "/usr/share/applications");
    }

    return dirs;
}

QString AppRegistry::pinnedConfigPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + "/" + QString::fromLatin1(kConfigDir) + "/pinned.conf";
}

QString AppRegistry::recentAppsConfigPath()
{
    return recentAppsPath();
}

QList<AppEntry> AppRegistry::installedApps()
{
    QList<AppEntry> apps;
    const QStringList searchPaths = applicationDirs();

    QStringList seenDesktopIds;
    for (const QString &dirPath : searchPaths) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        const QFileInfoList files = dir.entryInfoList(QStringList() << "*.desktop",
                                                       QDir::Files | QDir::Readable);
        for (const QFileInfo &fi : files) {
            AppEntry entry = parseDesktopFile(fi.absoluteFilePath());
            if (entry.name.isEmpty()) continue;
            if (entry.exec.isEmpty()) continue;

            const QString desktopId = fi.fileName().toLower();
            if (seenDesktopIds.contains(desktopId)) continue;
            seenDesktopIds.append(desktopId);

            apps.append(entry);
        }
    }

    std::sort(apps.begin(), apps.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.name.toLower() < b.name.toLower();
    });
    applyRecentMetadata(apps);

    return apps;
}

void AppRegistry::refreshUsageMetadata(QList<AppEntry> &apps)
{
    for (AppEntry &app : apps) {
        app.lastUsed = QDateTime();
        app.launchCount = 0;
    }
    applyRecentMetadata(apps);
}

AppEntry AppRegistry::parseDesktopFile(const QString &path)
{
    AppEntry entry;
    entry.desktopPath = path;

    QSettings desktop(path, QSettings::IniFormat);
    desktop.setIniCodec("UTF-8");
    desktop.beginGroup("Desktop Entry");

    const QString type = desktop.value("Type").toString();
    if (!type.isEmpty() && type != QStringLiteral("Application"))
        return AppEntry();

    entry.name = desktop.value("Name[zh_CN]").toString();
    if (entry.name.isEmpty())
        entry.name = desktop.value("Name").toString();

    if (desktop.value("NoDisplay", false).toBool())
        return AppEntry();
    if (desktop.value("Hidden", false).toBool())
        return AppEntry();

    entry.exec     = desktop.value("Exec").toString();
    entry.iconName = desktop.value("Icon").toString();
    entry.comment  = desktop.value("Comment[zh_CN]").toString();
    if (entry.comment.isEmpty())
        entry.comment = desktop.value("Comment").toString();

    QStringList cats = desktop.value("Categories").toString().split(";",
                         QString::SkipEmptyParts);
    entry.accent = accentForCategory(cats);
    entry.installTime = QFileInfo(path).lastModified();

    desktop.endGroup();
    return entry;
}

QColor AppRegistry::accentForCategory(const QStringList &categories)
{
    for (const QString &cat : categories) {
        if (cat == "Network"    || cat == "WebBrowser")  return QColor(96, 165, 250);
        if (cat == "Development"|| cat == "TerminalEmulator") return QColor(74, 222, 128);
        if (cat == "System"     || cat == "Settings")    return QColor(156, 163, 175);
        if (cat == "AudioVideo" || cat == "Audio")       return QColor(244, 114, 182);
        if (cat == "Game")                               return QColor(250, 204, 21);
        if (cat == "Office")                             return QColor(251, 146, 60);
        if (cat == "Utility"    || cat == "Calculator")   return QColor(192, 132, 252);
        if (cat == "Graphics")                            return QColor(52, 211, 153);
    }
    return QColor(148, 163, 184);
}

QList<AppEntry> AppRegistry::pinnedApps()
{
    QList<AppEntry> result;
    QStringList paths = pinnedDesktopPaths();

    // 用户没有固定配置 → 使用默认固定列表
    if (paths.isEmpty())
        paths = defaultPinnedPaths();

    for (const QString &path : paths) {
        if (!QFile::exists(path)) continue;
        AppEntry entry = parseDesktopFile(path);
        if (entry.name.isEmpty()) continue;
        entry.pinned = true;
        result.append(entry);
    }

    return result;
}

QList<AppEntry> AppRegistry::recentApps(const QList<AppEntry> &installedApps, int limit)
{
    QList<AppEntry> result;
    QStringList usedPaths;

    const auto appendIfInstalled = [&](const QString &path) {
        if (path.isEmpty() || usedPaths.contains(path)) return;
        for (const AppEntry &app : installedApps) {
            if (app.desktopPath == path) {
                result.append(app);
                usedPaths.append(path);
                return;
            }
        }
    };

    QFile file(recentAppsConfigPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd() && result.size() < limit) {
            const QString line = in.readLine().trimmed();
            const int tab = line.indexOf('\t');
            appendIfInstalled(tab > 0 ? line.mid(tab + 1).trimmed() : line);
        }
    }

    QList<AppEntry> recent = installedApps;
    std::sort(recent.begin(), recent.end(), [](const AppEntry &a, const AppEntry &b) {
        if (a.lastUsed.isValid() != b.lastUsed.isValid())
            return a.lastUsed.isValid();
        if (a.lastUsed.isValid() && a.lastUsed != b.lastUsed)
            return a.lastUsed > b.lastUsed;
        if (a.launchCount != b.launchCount)
            return a.launchCount > b.launchCount;
        return a.installTime > b.installTime;
    });

    for (const AppEntry &app : recent) {
        if (result.size() >= limit) break;
        if (app.lastUsed.isValid())
            appendIfInstalled(app.desktopPath);
    }

    return result;
}

void AppRegistry::recordLaunchedApp(const QString &desktopPath)
{
    if (desktopPath.isEmpty())
        return;

    QStringList paths;
    QFile inFile(recentAppsConfigPath());
    if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&inFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            QString path = line;
            const int tab = line.indexOf('\t');
            if (tab > 0)
                path = line.mid(tab + 1).trimmed();
            if (!path.isEmpty() && QFile::exists(path) && path != desktopPath)
                paths.append(line);
        }
    }

    paths.prepend(QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + "\t" + desktopPath);
    while (paths.size() > 30)
        paths.removeLast();

    const QString configPath = recentAppsConfigPath();
    QDir().mkpath(QFileInfo(configPath).absolutePath());
    QFile outFile(configPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;

    QTextStream out(&outFile);
    for (const QString &path : paths)
        out << path << "\n";
}

void AppRegistry::pinApp(const QString &desktopPath)
{
    QStringList paths = pinnedDesktopPaths();
    if (paths.isEmpty())
        paths = defaultPinnedPaths();
    if (!paths.contains(desktopPath)) {
        paths.append(desktopPath);
        savePinned(paths);
    }
}

void AppRegistry::unpinApp(const QString &desktopPath)
{
    QStringList paths = pinnedDesktopPaths();
    if (paths.isEmpty())
        paths = defaultPinnedPaths();
    paths.removeAll(desktopPath);
    savePinned(paths);
}

QStringList AppRegistry::pinnedDesktopPaths()
{
    const QString configPath = pinnedConfigPath();
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringList();

    QStringList paths;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty() && !line.startsWith('#'))
            paths.append(line);
    }
    return paths;
}

void AppRegistry::savePinned(const QStringList &paths)
{
    const QString configPath = pinnedConfigPath();
    QDir().mkpath(QFileInfo(configPath).absolutePath());

    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;

    QTextStream out(&file);
    for (const QString &path : paths)
        out << path << "\n";
}
