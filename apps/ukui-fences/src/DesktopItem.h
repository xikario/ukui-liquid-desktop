#pragma once

#include <QString>
#include <QIcon>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QFileIconProvider>
#include <QMimeDatabase>
#include <QMimeType>
#include <QLocale>
#include <QDesktopServices>
#include <QSettings>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

inline QIcon resolveFileIcon(const QString &filePath, bool isDir)
{
    if (isDir) {
        QIcon icon = QIcon::fromTheme(QLatin1String("folder"));
        if (!icon.isNull()) return icon;
        return QIcon::fromTheme(QLatin1String("inode-directory"));
    }

    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(filePath);

    QIcon icon = QIcon::fromTheme(mime.iconName());
    if (!icon.isNull()) return icon;

    icon = QIcon::fromTheme(mime.genericIconName());
    if (!icon.isNull()) return icon;

    QFileIconProvider prov;
    icon = prov.icon(QFileInfo(filePath));
    if (!icon.isNull()) return icon;

    return QIcon::fromTheme(QLatin1String("text-x-generic"));
}

struct DesktopItem
{
    QString filePath;
    QString displayName;
    QIcon   icon;
    QString mimeType;
    bool    isDesktopEntry = false;
    bool    isDir          = false;
    bool    isSystemIcon   = false;

    bool isValid() const { return !filePath.isEmpty(); }

    // ── 从任意路径构造 ───────────────────────────────────────
    static DesktopItem fromPath(const QString &path)
    {
        QFileInfo fi(path);
        if (!fi.exists()) return {};

        if (!fi.isDir() &&
            path.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive))
            return fromDesktopFile(path);

        DesktopItem item;
        item.filePath    = fi.absoluteFilePath();
        item.isDir       = fi.isDir();
        item.displayName = fi.fileName();

        QMimeDatabase db;
        item.mimeType = db.mimeTypeForFile(fi).name();

        item.icon = resolveFileIcon(item.filePath, item.isDir);

        return item;
    }

    static DesktopItem computerItem()
    {
        const QStringList desktopIds = {
            "peony-computer.desktop",
            "org.ukui.peony.computer.desktop"
        };
        for (const QString &desktopId : desktopIds) {
            const QString path = QStandardPaths::locate(
                QStandardPaths::ApplicationsLocation, desktopId);
            if (!path.isEmpty()) {
                DesktopItem item = fromDesktopFile(path);
                item.filePath = QLatin1String("computer:///");
                item.isSystemIcon = true;
                item.isDir = true;
                return item;
            }
        }

        DesktopItem item;
        item.filePath = QLatin1String("computer:///");
        item.displayName = QStringLiteral("计算机");
        item.icon = QIcon::fromTheme(QLatin1String("computer"));
        item.mimeType = QLatin1String("inode/directory");
        item.isDir = true;
        item.isSystemIcon = true;
        return item;
    }

    static DesktopItem trashItem()
    {
        const QStringList desktopIds = {
            "peony-trash.desktop",
            "org.ukui.peony.trash.desktop"
        };
        for (const QString &desktopId : desktopIds) {
            const QString path = QStandardPaths::locate(
                QStandardPaths::ApplicationsLocation, desktopId);
            if (!path.isEmpty()) {
                DesktopItem item = fromDesktopFile(path);
                item.filePath = QLatin1String("trash:///");
                item.isSystemIcon = true;
                item.isDir = true;
                return item;
            }
        }

        DesktopItem item;
        item.filePath = QLatin1String("trash:///");
        item.displayName = QStringLiteral("回收站");
        item.icon = QIcon::fromTheme(QLatin1String("user-trash"));
        item.mimeType = QLatin1String("inode/directory");
        item.isDir = true;
        item.isSystemIcon = true;
        return item;
    }

    static DesktopItem fromStoredPath(const QString &path)
    {
        if (path.startsWith(QLatin1String("computer://")))
            return computerItem();
        if (path.startsWith(QLatin1String("trash://")))
            return trashItem();
        return fromPath(path);
    }

    // ── 从 .desktop 文件构造 ─────────────────────────────────
    static DesktopItem fromDesktopFile(const QString &path)
    {
        DesktopItem item;
        item.filePath       = path;
        item.isDesktopEntry = true;
        item.mimeType       = QLatin1String("application/x-desktop");

        QSettings s(path, QSettings::IniFormat);
        s.setIniCodec("UTF-8");
        s.beginGroup(QLatin1String("Desktop Entry"));

        // 名称：优先本地化
        const QString locale = QLocale::system().name();          // zh_CN
        const QString lang   = locale.left(locale.indexOf('_'));  // zh

        item.displayName = s.value(
            QString("Name[%1]").arg(locale)).toString();
        if (item.displayName.isEmpty())
            item.displayName = s.value(
                QString("Name[%1]").arg(lang)).toString();
        if (item.displayName.isEmpty())
            item.displayName = s.value(
                QLatin1String("Name")).toString();
        if (item.displayName.isEmpty())
            item.displayName = QFileInfo(path).completeBaseName();

        // 图标
        const QString iconStr = s.value(QLatin1String("Icon")).toString();
        if (!iconStr.isEmpty()) {
            if (QFile::exists(iconStr))
                item.icon = QIcon(iconStr);
            else
                item.icon = QIcon::fromTheme(iconStr);
        }
        if (item.icon.isNull())
            item.icon = QIcon::fromTheme(
                QLatin1String("application-x-executable"));

        return item;
    }

    // ── 打开 ─────────────────────────────────────────────────
    void open() const
    {
        if (isSystemIcon ||
            filePath.startsWith(QLatin1String("computer://")) ||
            filePath.startsWith(QLatin1String("trash://"))) {
            const QString peony = QStandardPaths::findExecutable(
                QLatin1String("peony"));
            if (!peony.isEmpty() &&
                QProcess::startDetached(peony, QStringList() << filePath))
                return;
            if (QProcess::startDetached(QLatin1String("gio"),
                    QStringList() << QLatin1String("open") << filePath))
                return;
            QProcess::startDetached(QLatin1String("xdg-open"),
                                    QStringList() << filePath);
            return;
        }

        if (isDesktopEntry) {
            QProcess::startDetached(QLatin1String("gio"),
                QStringList() << QLatin1String("launch") << filePath);
            return;
        }

        if (QProcess::startDetached(QLatin1String("gio"),
                QStringList() << QLatin1String("open") << filePath))
            return;

        if (QProcess::startDetached(QLatin1String("xdg-open"),
                QStringList() << filePath))
            return;

        if (QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)))
            return;

    }
};
