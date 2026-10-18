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
#include <QSettings>
#include <QProcess>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QStringList>
#include <functional>
#include <memory>
#include "../../../shared/desktop-launch/DesktopLaunchCompat.h"

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
    using OpenCallback = std::function<void(bool, const QString &)>;
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

    // Interactive launchers must not wait on slow mounts in the UI thread.
    void openAsync(QObject *context, OpenCallback callback) const
    {
        if (!context) {
            if (callback)
                callback(false, QStringLiteral("打开请求已失效。"));
            return;
        }
        if (isSystemIcon ||
            filePath.startsWith(QLatin1String("computer://")) ||
            filePath.startsWith(QLatin1String("trash://"))) {
            const QString peony = QStandardPaths::findExecutable(
                QLatin1String("peony"));
            bool launched = !peony.isEmpty() &&
                QProcess::startDetached(peony, QStringList() << filePath);
            if (!launched)
                launched = QProcess::startDetached(QLatin1String("gio"),
                    QStringList() << QLatin1String("open") << filePath);
            if (!launched)
                launched = QProcess::startDetached(QLatin1String("xdg-open"),
                    QStringList() << filePath);
            if (callback)
                callback(launched, launched ? QString() :
                    QStringLiteral("无法启动文件管理器打开这个位置。"));
            return;
        }
        if (isDesktopEntry) {
            QString error;
            const bool launched = launchDesktopFileCompat(filePath, &error);
            if (callback)
                callback(launched, launched ? QString() :
                    (error.isEmpty() ? QStringLiteral("无法启动这个桌面快捷方式。") : error));
            return;
        }

        struct State {
            QProcess *process = nullptr;
            QTemporaryFile diagnostics;
            QString path;
            QString error;
            bool fallback = false;
            bool completed = false;
        };
        const auto state = std::make_shared<State>();
        state->path = filePath;
        state->process = new QProcess(context);
        // Opened applications can inherit the opener's descriptors. A pipe
        // disappears when this short-lived QProcess is deleted; a regular
        // temporary file remains safe for inherited writers after unlinking.
        state->process->setStandardOutputFile(QProcess::nullDevice());
        state->process->setStandardErrorFile(state->diagnostics.open()
            ? state->diagnostics.fileName() : QProcess::nullDevice());
        const auto readError = [state] {
            if (!state->diagnostics.isOpen() || !state->diagnostics.seek(0)) return QString();
            return QString::fromLocal8Bit(state->diagnostics.read(4096)).trimmed();
        };
        const auto startOpener = [state](const QStringList &command) {
            // xdg-open may exec the editor itself. Keep a separate monitor so
            // destroying the desktop only terminates this shell, never that
            // editor. Paths are positional arguments, never shell source.
            QStringList arguments{QStringLiteral("-c"),
                QStringLiteral("\"$@\"; result=$?; exit \"$result\""),
                QStringLiteral("ukui-file-opener")};
            arguments.append(command);
            state->process->start(QStringLiteral("/bin/sh"), arguments);
        };
        const auto finish = [state, callback](bool success, const QString &error) {
            if (state->completed)
                return;
            state->completed = true;
            // The callback may destroy its recipient (and the process).
            state->process->deleteLater();
            if (callback)
                callback(success, error);
        };
        const auto startFallback = [state, startOpener] {
            if (state->completed)
                return;
            state->fallback = true;
            startOpener({QStringLiteral("xdg-open"), state->path});
        };
        QObject::connect(state->process,
                         QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         context,
                         [state, finish, startFallback, readError](int code, QProcess::ExitStatus status) {
            if (!state->fallback) {
                if (status == QProcess::NormalExit && code == 0) {
                    finish(true, QString());
                    return;
                }
                state->error = readError();
                startFallback();
                return;
            }
            if (status == QProcess::NormalExit && code == 0) {
                finish(true, QString());
                return;
            }
            const QString detail = readError();
            finish(false, detail.isEmpty() ?
                (state->error.isEmpty() ? QStringLiteral("打开程序返回失败。") : state->error)
                : detail);
        });
        QObject::connect(state->process, &QProcess::errorOccurred, context,
                         [state, startFallback, finish](QProcess::ProcessError error) {
            if (state->completed || error != QProcess::FailedToStart)
                return;
            state->error = state->process->errorString();
            if (!state->fallback)
                startFallback();
            else
                finish(false, state->error.isEmpty() ?
                    QStringLiteral("没有找到可以打开此文件的应用程序。") : state->error);
        });
        startOpener({QStringLiteral("gio"), QStringLiteral("open"), filePath});
    }
};
