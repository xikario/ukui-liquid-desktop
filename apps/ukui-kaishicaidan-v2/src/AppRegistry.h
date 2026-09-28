#pragma once

#include <QString>
#include <QList>
#include <QIcon>
#include <QColor>
#include <QDateTime>

struct AppEntry {
    QString name;
    QString desktopPath;
    QString iconName;
    QString exec;
    QString comment;
    QColor accent;
    QDateTime installTime;
    QDateTime lastUsed;
    int launchCount = 0;
    bool pinned = false;
};

class AppRegistry {
public:
    static QStringList applicationDirs();
    static QList<AppEntry> installedApps();
    static void refreshUsageMetadata(QList<AppEntry> &apps);
    static QList<AppEntry> pinnedApps();
    static QList<AppEntry> recentApps(const QList<AppEntry> &installedApps, int limit = 4);
    static void recordLaunchedApp(const QString &desktopPath);
    static void pinApp(const QString &desktopPath);
    static void unpinApp(const QString &desktopPath);
    static QStringList pinnedDesktopPaths();
    static void savePinned(const QStringList &paths);

private:
    static AppEntry parseDesktopFile(const QString &path);
    static QString pinnedConfigPath();
    static QString recentAppsConfigPath();
    static QColor accentForCategory(const QStringList &categories);
};
