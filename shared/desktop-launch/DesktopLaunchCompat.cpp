#include "DesktopLaunchCompat.h"

#include <QFile>
#include <QLibrary>
#include <QUrl>

namespace {

struct Error {
    unsigned int domain;
    int code;
    char *message;
};

using ChildSetup = void (*)(void *);
using PidCallback = void (*)(void *, int, void *);
using Launch = int (*)(void *, void *, void *, int, ChildSetup, void *,
                       PidCallback, void *, Error **);

void setError(QString *destination, const QString &message)
{
    if (destination)
        *destination = message;
}

} // namespace

bool launchDesktopFileCompat(const QString &path, QString *errorMessage,
                             const QStringList &files)
{
    if (errorMessage)
        errorMessage->clear();

    QLibrary gio(QStringLiteral("libgio-2.0.so.0"));
    gio.setLoadHints(QLibrary::PreventUnloadHint);
    const auto create = reinterpret_cast<void *(*)(const char *)>(
        gio.resolve("g_desktop_app_info_new_from_filename"));
    const auto launch = reinterpret_cast<Launch>(
        gio.resolve("g_desktop_app_info_launch_uris_as_manager"));
    const auto unref = reinterpret_cast<void (*)(void *)>(
        gio.resolve("g_object_unref"));
    const auto freeError = reinterpret_cast<void (*)(Error *)>(
        gio.resolve("g_error_free"));
    const auto append = reinterpret_cast<void *(*)(void *, void *)>(gio.resolve("g_list_append"));
    const auto freeList = reinterpret_cast<void (*)(void *)>(gio.resolve("g_list_free"));
    if (!create || !launch || !unref || !freeError || !append || !freeList) {
        setError(errorMessage, QStringLiteral("系统桌面应用启动接口不可用。"));
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(path);
    void *app = create(encodedPath.constData());
    if (!app) {
        setError(errorMessage, QStringLiteral("无法读取这个桌面快捷方式。"));
        return false;
    }

    Error *error = nullptr;
    QList<QByteArray> uris;
    for (const QString &file : files) {
        const QUrl url(file);
        uris.append((url.scheme().isEmpty() ? QUrl::fromLocalFile(file) : url).toEncoded());
    }
    void *list = nullptr;
    for (QByteArray &uri : uris)
        list = append(list, uri.data());
    // Keep the dispatched application's output detached from the short-lived
    // launcher helper. Otherwise a later write can hit a closed pipe.
    constexpr int searchPath = 1 << 2;       // G_SPAWN_SEARCH_PATH
    constexpr int stdoutToDevNull = 1 << 3; // G_SPAWN_STDOUT_TO_DEV_NULL
    constexpr int stderrToDevNull = 1 << 4; // G_SPAWN_STDERR_TO_DEV_NULL
    const bool launched = launch(app, list, nullptr,
        searchPath | stdoutToDevNull | stderrToDevNull,
        nullptr, nullptr, nullptr, nullptr, &error);
    unref(app);
    freeList(list);

    if (error) {
        setError(errorMessage, QString::fromUtf8(error->message));
        freeError(error);
    }
    return launched;
}
