#include "MusicClientIntegration.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QProcess>
#include <QProcessEnvironment>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

namespace {
QByteArray readBytes(const QString &path, int limit=65536) {
    QFile f(path); return f.open(QIODevice::ReadOnly)?f.read(limit):QByteArray();
}
quint64 startTime(const QString &directory) {
    const QByteArray stat=readBytes(directory+"/stat",16384);
    const int end=stat.lastIndexOf(')');
    return end<0?0:stat.mid(end+2).simplified().split(' ').value(19).toULongLong();
}
QString executablePath(const QString &path) {
    const QFileInfo f(path);
    return f.isFile() && f.isExecutable()?f.canonicalFilePath():QString();
}
QString desktopForProcess(const QString &service,const QString &exe,const QStringList &directories) {
    QString token=service.mid(QStringLiteral("org.mpris.MediaPlayer2.").size());
    token.remove(QRegularExpression("\\.(?:instance[A-Za-z0-9_-]*|pid[0-9]+)$"));
    QString best; int score=0; bool ambiguous=false;
    QSet<QString> seenIds;
    for(const QString &directory:directories) for(const QString &name:QDir(directory).entryList({"*.desktop"},QDir::Files)) {
        // User entries shadow system entries with the same desktop ID.
        if(seenIds.contains(name))continue; seenIds.insert(name);
        const QString file=directory+'/'+name;
        QSettings s(file,QSettings::IniFormat);s.setIniCodec("UTF-8");s.beginGroup("Desktop Entry");
        if(s.value("Type").toString()!="Application" || s.value("Hidden",false).toBool() || s.value("Exec").toString().isEmpty())continue;
        const QString id=name.left(name.size()-8);
        int match=0;
        if(id.compare(token,Qt::CaseInsensitive)==0 || id.compare("org."+token,Qt::CaseInsensitive)==0)match=3;
        const QString wm=s.value("StartupWMClass").toString();
        if(!wm.isEmpty() && (wm.compare(QFileInfo(exe).fileName(),Qt::CaseInsensitive)==0 || wm.compare(token,Qt::CaseInsensitive)==0))match=qMax(match,2);
        QString tryExec=s.value("TryExec").toString();
        if(!tryExec.isEmpty() && !QFileInfo(tryExec).isAbsolute())tryExec=QStandardPaths::findExecutable(tryExec);
        if(!exe.isEmpty() && executablePath(tryExec)==exe)match=4;
        if(match>score){score=match;best=file;ambiguous=false;}
        else if(match && match==score && file!=best)ambiguous=true;
    }
    return ambiguous?QString():best;
}
int ignoreWindowRace(Display *,XErrorEvent *) {return 0;}
QList<unsigned long> property(Display *d,Window w,Atom atom,Atom type) {
    Atom actual=0;int format=0;unsigned long count=0,rest=0;unsigned char *data=nullptr;
    QList<unsigned long> result;
    if(XGetWindowProperty(d,w,atom,0,4096,False,type,&actual,&format,&count,&rest,&data)==Success
       && actual==type && format==32 && data) {
        const auto *values=reinterpret_cast<unsigned long *>(data);
        for(unsigned long i=0;i<count;++i)result << values[i];
    }
    if(data)XFree(data);return result;
}
}
namespace MusicClientIntegration {
ProcessInfo inspectProcess(uint pid,const QString &service,const QString &procRoot,const QStringList &applicationDirs) {
    ProcessInfo info;
    const QString directory=procRoot+'/'+QString::number(pid);
    info.started=startTime(directory);if(!info.started)return info;
    info.launch.name=MprisPlayer::suggestedName(service);info.launch.service=service;
    const QString exe=executablePath(QFileInfo(directory+"/exe").symLinkTarget());
    QString image;
    for(const QByteArray &entry:readBytes(directory+"/environ").split('\0'))
        if(entry.startsWith("APPIMAGE=")){image=executablePath(QString::fromUtf8(entry.mid(9)));break;}
    const auto args=readBytes(directory+"/cmdline").split('\0');
    info.launch.program=image.isEmpty()?exe:image;
    // Temporary AppImage mounts cannot be relaunched once the client exits.
    if(image.isEmpty() && exe.startsWith("/tmp/.mount_"))info.launch.program.clear();
    if(!image.isEmpty())info.launch.arguments.clear();
    else {
        // Keep launch switches and interpreter scripts; do not reopen the last
        // media file or persist transient IPC / renderer arguments.
        const QString base=QFileInfo(exe).fileName().toLower();
        const bool interpreter=base.startsWith("python") || base=="node" || base=="java" || base=="bash" || base=="sh";
        if(interpreter) {
            const QString script=QString::fromUtf8(args.value(1));
            if(QFileInfo(script).isAbsolute() && QFileInfo(script).isFile())info.launch.arguments << script;
            else info.launch.program.clear();
        } else for(int i=1;i<args.size();++i) {
            const QString arg=QString::fromUtf8(args[i]);
            if(arg=="--no-sandbox" || arg=="--disable-gpu" || arg=="--disable-gpu-sandbox")info.launch.arguments << arg;
        }
    }
    info.launch.workingDirectory=QFileInfo(directory+"/cwd").symLinkTarget();
    if(!QFileInfo(info.launch.workingDirectory).isDir())info.launch.workingDirectory.clear();
    const auto dirs=applicationDirs.isEmpty()?QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation):applicationDirs;
    info.launch.desktopFile=desktopForProcess(service,exe,dirs);
    // Reject a PID recycled while reading executable/arguments.
    if(startTime(directory)!=info.started)return {};
    return info;
}
bool activateProcessWindow(uint pid,quint64 started) {
    if(!pid || !started || QGuiApplication::platformName()!="xcb"
       || startTime("/proc/"+QString::number(pid))!=started)return false;
    Display *d=XOpenDisplay(nullptr);if(!d)return false;
    XSync(d,False);auto old=XSetErrorHandler(ignoreWindowRace);
    const Window root=DefaultRootWindow(d);
    const Atom pidAtom=XInternAtom(d,"_NET_WM_PID",False),listAtom=XInternAtom(d,"_NET_CLIENT_LIST_STACKING",False);
    auto windows=property(d,root,listAtom,XA_WINDOW);
    if(windows.isEmpty())windows=property(d,root,XInternAtom(d,"_NET_CLIENT_LIST",False),XA_WINDOW);
    Window target=0;
    for(int i=windows.size()-1;i>=0;--i) {
        const auto ids=property(d,windows[i],pidAtom,XA_CARDINAL);
        if(ids.isEmpty() || ids.first()!=pid)continue;
        const auto types=property(d,windows[i],XInternAtom(d,"_NET_WM_WINDOW_TYPE",False),XA_ATOM);
        if(types.contains(XInternAtom(d,"_NET_WM_WINDOW_TYPE_DOCK",False))
           || types.contains(XInternAtom(d,"_NET_WM_WINDOW_TYPE_DESKTOP",False)))continue;
        XWindowAttributes attributes{};
        if(XGetWindowAttributes(d,windows[i],&attributes) && !attributes.override_redirect){target=windows[i];break;}
    }
    if(target) {
        // Restore minimized windows as well as asking the WM to focus them.
        XMapRaised(d,target);
        XEvent event{};event.xclient.type=ClientMessage;event.xclient.window=target;
        event.xclient.message_type=XInternAtom(d,"_NET_ACTIVE_WINDOW",False);event.xclient.format=32;
        event.xclient.data.l[0]=2;event.xclient.data.l[1]=CurrentTime;
        XSendEvent(d,root,False,SubstructureRedirectMask|SubstructureNotifyMask,&event);
    }
    XSync(d,False);XSetErrorHandler(old);XCloseDisplay(d);return target!=0;
}
bool launch(const MusicClientProfile &profile) {
    QProcess process;
    if(!profile.desktopFile.isEmpty()) {
        const QString launcher=QStandardPaths::findExecutable("gtk-launch");
        if(launcher.isEmpty() || !QFileInfo(profile.desktopFile).isFile())return false;
        // gtk-launch is available on this UKUI release; its older gio has no
        // launch subcommand. Detected files are installed application entries.
        process.setProgram(launcher);process.setArguments({QFileInfo(profile.desktopFile).fileName()});
    } else {
        if(profile.program.isEmpty())return false;
        process.setProgram(profile.program);process.setArguments(profile.arguments);
        process.setWorkingDirectory(profile.workingDirectory);
    }
    auto environment=QProcessEnvironment::systemEnvironment();environment.remove("ELECTRON_RUN_AS_NODE");
    process.setProcessEnvironment(environment);return process.startDetached();
}
}
