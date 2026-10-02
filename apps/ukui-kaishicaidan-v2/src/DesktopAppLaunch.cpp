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
    const auto launch=reinterpret_cast<int (*)(void *,void *,void *,Error **)>(gio.resolve("g_app_info_launch"));
    const auto unref=reinterpret_cast<void (*)(void *)>(gio.resolve("g_object_unref"));
    const auto freeError=reinterpret_cast<void (*)(Error *)>(gio.resolve("g_error_free"));
    QTextStream output(stderr);
    if(!create || !launch || !unref || !freeError) {
        output<<"系统桌面应用启动接口不可用。\n";return 1;
    }
    void *app=create(QFile::encodeName(path).constData());
    if(!app){output<<"无法读取或启动这个桌面快捷方式。\n";return 1;}
    Error *error=nullptr;
    const bool success=launch(app,nullptr,nullptr,&error);
    unref(app);
    if(error){output<<QString::fromUtf8(error->message)<<'\n';freeError(error);}
    return success?0:1;
}
