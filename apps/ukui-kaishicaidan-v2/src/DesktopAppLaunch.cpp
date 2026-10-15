#include "DesktopAppLaunch.h"
#include <QFile>
#include <QLibrary>
#include <QTextStream>

int launchDesktopFile(const QString &path)
{
    // Older Kylin GIO has DesktopAppInfo but no launch CLI subcommand.
    // Resolve its stable runtime API without another development SDK.
    QLibrary gio("libgio-2.0.so.0");
    gio.setLoadHints(QLibrary::PreventUnloadHint);
    struct Error { unsigned int domain;int code;char *message; };
    const auto create=reinterpret_cast<void *(*)(const char *)>(gio.resolve("g_desktop_app_info_new_from_filename"));
    using ChildSetup = void (*)(void *);
    using PidCallback = void (*)(void *, int, void *);
    const auto launch=reinterpret_cast<int (*)(void *,void *,void *,int,
        ChildSetup,void *,PidCallback,void *,Error **)>(
            gio.resolve("g_desktop_app_info_launch_uris_as_manager"));
    const auto unref=reinterpret_cast<void (*)(void *)>(gio.resolve("g_object_unref"));
    const auto freeError=reinterpret_cast<void (*)(Error *)>(gio.resolve("g_error_free"));
    QTextStream output(stderr);
    if(!create || !launch || !unref || !freeError) {
        output<<"系统桌面应用启动接口不可用。\n";return 1;
    }
    void *app=create(QFile::encodeName(path).constData());
    if(!app){output<<"无法读取或启动这个桌面快捷方式。\n";return 1;}
    Error *error=nullptr;
    // The UI owns pipes to this short-lived helper. Applications must not
    // inherit them: once QProcess is deleted, later log writes can SIGPIPE
    // the application. Keep helper errors on stderr, but dispatch app output
    // independently using the stable GLib 2.28 manager API.
    constexpr int searchPath = 1 << 2;       // G_SPAWN_SEARCH_PATH
    constexpr int stdoutToNull = 1 << 3;     // G_SPAWN_STDOUT_TO_DEV_NULL
    constexpr int stderrToNull = 1 << 4;     // G_SPAWN_STDERR_TO_DEV_NULL
    const bool success=launch(app,nullptr,nullptr,
        searchPath | stdoutToNull | stderrToNull,
        nullptr,nullptr,nullptr,nullptr,&error);
    unref(app);
    if(error){output<<QString::fromUtf8(error->message)<<'\n';freeError(error);}
    return success?0:1;
}
