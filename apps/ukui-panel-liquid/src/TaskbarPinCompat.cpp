#include "TaskbarPinCompat.h"
#include "LiquidDialog.h"
#include "DesktopLaunchCompat.h"
#include <QAbstractButton>
#include <QApplication>
#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMenu>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace {
void *nativeSymbol(QWidget *widget,const char *name) {
    if(void *symbol=dlsym(RTLD_DEFAULT,name))return symbol;
    Dl_info info{};
    if(!widget || !dladdr(widget->metaObject(),&info) || !info.dli_fname)return nullptr;
    void *module=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);
    if(!module)return nullptr;
    void *symbol=dlsym(module,name);dlclose(module);return symbol;
}
template<class T>T native(QWidget *w,const char *name){return reinterpret_cast<T>(nativeSymbol(w,name));}
QWidget *ancestor(QWidget *w,const char *name) {
    for(;w;w=w->parentWidget())if(w->inherits(name))return w;
    return nullptr;
}
QString buttonDesktop(QWidget *button) {
    auto get=native<QString (*)(QWidget *)>(button,"_ZN14UKUITaskButton15desktopFileNameEv");
    return get?get(button):QString();
}
QString groupDesktop(QWidget *group) {
    auto get=native<QString (*)(QWidget *)>(group,"_ZN13UKUITaskGroup18getDesktopFileNameEv");
    return get?get(group):QString();
}
bool isPinned(QWidget *bar,QWidget *group,const QString &path) {
    // The OEM hasPinnedToTaskbar helper reads legacy config keys and can
    // report false even after pinToTaskbar saved a live pinned group. Query
    // actual group state, including another group with the same launcher.
    auto get=native<bool (*)(QWidget *)>(group?group:bar,"_ZN13UKUITaskGroup8isPinnedEv");
    if(!get)return false;
    if(group && get(group))return true;
    if(bar && !path.isEmpty())for(auto *candidate:bar->findChildren<QWidget *>())
        if(candidate->inherits("UKUITaskGroup") && groupDesktop(candidate)==path && get(candidate))return true;
    return false;
}
QString decode(const QString &value) {
    QString result;
    for(int i=0;i<value.size();++i) {
        if(value[i]=='\\' && i+1<value.size()) {
            const QChar c=value[++i];
            if(c=='n')result+='\n';else if(c=='r')result+='\r';
            else if(c=='t')result+='\t';else if(c=='s')result+=' ';
            else if(c=='\\')result+='\\';else {result+='\\';result+=c;}
        }else result+=value[i];
    }
    return result;
}
QString encode(QString value) {
    value.replace("\\","\\\\");value.replace("\n","\\n");value.replace("\r","\\r");value.replace("\t","\\t");
    return value;
}
QString execArgument(QString value) {
    value.replace("%","%%");value.replace("\\","\\\\");value.replace("\"","\\\"");
    value.replace("$","\\$");value.replace("`","\\`");
    return '"'+value+'"';
}
QString executablePath(const QString &value,const QString &cwd={}) {
    if(value.isEmpty())return {};
    const QString path=value.contains('/')?(QDir::isAbsolutePath(value)?value:QDir(cwd).absoluteFilePath(value))
        :QStandardPaths::findExecutable(value);
    const QFileInfo f(path);
    return f.isFile() && f.isExecutable()?f.absoluteFilePath():QString();
}
QString launcherWorkingDirectory(const TaskbarPin::WindowIdentity &identity) {
    return !identity.workingDirectory.isEmpty() && QFileInfo(identity.workingDirectory).isDir()
        ?QDir(identity.workingDirectory).absolutePath():QString();
}
QString launcherExec(const TaskbarPin::WindowIdentity &identity) {
    const QString program=executablePath(identity.executable,identity.workingDirectory);
    if(program.isEmpty())return {};
    QStringList arguments=identity.arguments;
    if(arguments.isEmpty())arguments.append(program);else arguments[0]=program;
    QStringList quoted;
    for(const QString &argument:arguments)quoted.append(execArgument(argument));
    return quoted.join(' ');
}
bool matchesGeneratedLauncher(const QMap<QString,QString> &entry,const TaskbarPin::WindowIdentity &identity) {
    const QString command=launcherExec(identity);
    return !command.isEmpty() && entry.value("Exec")==command
        && entry.value("Path")==launcherWorkingDirectory(identity);
}
QStringList applicationDirectories() {
    QStringList result;
    for(const QString &root:QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        result.append(QDir(root).filePath("applications"));
    result.removeDuplicates();return result;
}
bool nativePinAction(QAction *action) {
    const QString icon=action->icon().name();
    if(icon=="ukui-fixed-symbolic" || icon=="ukui-unfixed-symbolic")return true;
    QString text=action->text();text.remove('&');
    return text==QApplication::translate("UKUITaskButton","Pin to taskbar")
        || text==QApplication::translate("UKUITaskButton","Unpin from taskbar")
        || text=="Pin to taskbar" || text=="Unpin from taskbar"
        || text==QStringLiteral("固定到任务栏") || text==QStringLiteral("从任务栏取消固定")
        || text==QStringLiteral("取消固定到任务栏") || text==QStringLiteral("取消固定");
}
}

QMap<QString,QString> TaskbarPin::desktopEntry(const QString &path) {
    QFile file(path);QMap<QString,QString> result;
    if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024)return result;
    bool main=false;
    for(const QByteArray &bytes:file.readAll().split('\n')) {
        const QString line=QString::fromUtf8(bytes).trimmed();
        if(line.startsWith('#') || line.isEmpty())continue;
        if(line.startsWith('[')){main=line=="[Desktop Entry]";continue;}
        const int equal=line.indexOf('=');
        if(main && equal>0)result.insert(line.left(equal).trimmed(),decode(line.mid(equal+1)));
    }
    return result;
}
bool TaskbarPin::validDesktop(const QString &path) {
    const auto entry=desktopEntry(path);
    if(entry.value("Type")!="Application" || entry.value("Hidden").compare("true",Qt::CaseInsensitive)==0)return false;
    // GIO validates Exec, TryExec and D-Bus activation with the same runtime
    // parser used by the OEM taskbar's application launch path.
    QLibrary gio("libgio-2.0.so.0");
    auto create=reinterpret_cast<void *(*)(const char *)>(gio.resolve("g_desktop_app_info_new_from_filename"));
    auto unref=reinterpret_cast<void (*)(void *)>(gio.resolve("g_object_unref"));
    if(!create || !unref)return false;
    void *info=create(QFile::encodeName(path).constData());if(!info)return false;
    unref(info);return true;
}
QString TaskbarPin::findDesktop(const WindowIdentity &identity,const QStringList &directories) {
    QSet<QString> seen;QString best;int bestScore=0;bool ambiguous=false;
    for(const QString &directory:directories) {
        QDirIterator it(directory,{"*.desktop"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) {
            const QString path=it.next();
            QString id=QDir(directory).relativeFilePath(path);id.replace('/','-');
            if(seen.contains(id))continue;seen.insert(id);
            const auto entry=desktopEntry(path);const QString wmclass=entry.value("StartupWMClass");
            // Generated entries describe one command and working directory,
            // not every application sharing the same window class.
            if(entry.value("X-UKUI-Liquid-Pinned")=="true" && !matchesGeneratedLauncher(entry,identity))continue;
            int score=0;
            if(!wmclass.isEmpty() && ((!identity.windowClass.isEmpty() && wmclass.compare(identity.windowClass,Qt::CaseInsensitive)==0)
                || (!identity.instance.isEmpty() && wmclass.compare(identity.instance,Qt::CaseInsensitive)==0)))score=100;
            // Only use executable identity when GIO can resolve the desktop's
            // executable unambiguously. Generic interpreter matches alone are
            // not enough to identify a Python/Java/Electron application.
            const QString program=executablePath(entry.value("TryExec"));
            if(!program.isEmpty() && !identity.executable.isEmpty()
                && QFileInfo(program).canonicalFilePath()==QFileInfo(identity.executable).canonicalFilePath())score+=20;
            if(score<100 || !validDesktop(path))continue;
            if(entry.value("NoDisplay").compare("true",Qt::CaseInsensitive)!=0)++score;
            if(score>bestScore){bestScore=score;best=path;ambiguous=false;}
            else if(score==bestScore && path!=best)ambiguous=true;
        }
    }
    return ambiguous?QString():best;
}
QString TaskbarPin::writeLauncher(const WindowIdentity &identity,const QString &directory,QString *error) {
    if(error)error->clear();
    const QString program=executablePath(identity.executable,identity.workingDirectory);
    if(program.isEmpty()) {if(error)*error=QStringLiteral("无法确定这个窗口的启动程序。");return {};}
    const QString command=launcherExec(identity);
    const QString workingDirectory=launcherWorkingDirectory(identity);
    const QByteArray key=identity.windowClass.toUtf8()+'\0'+identity.instance.toUtf8()
        +'\0'+workingDirectory.toUtf8()+'\0'+command.toUtf8();
    const QString id=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex().left(32));
    const QString path=QDir(directory).filePath("ukui-liquid-pinned-"+id+".desktop");
    if(!QDir().mkpath(directory)) {if(error)*error=QStringLiteral("无法创建应用启动器目录。");return {};}
    // Never overwrite an unrelated entry or follow a user-supplied symlink.
    if(QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
        if(!QFileInfo(path).isSymLink() && desktopEntry(path).value("X-UKUI-Liquid-Pinned")=="true" && matchesGeneratedLauncher(desktopEntry(path),identity) && validDesktop(path))return path;
        if(error)*error=QStringLiteral("启动器路径已被其他文件占用。");return {};
    }
    const QString name=identity.windowClass.isEmpty()?QFileInfo(program).fileName():identity.windowClass;
    QString text="[Desktop Entry]\nType=Application\nVersion=1.0\nName="+encode(name)
        +"\nExec="+encode(command)+"\nIcon=application-x-executable\nTerminal=false\nNoDisplay=true\nX-UKUI-Liquid-Pinned=true\n";
    if(!identity.windowClass.isEmpty())text+="StartupWMClass="+encode(identity.windowClass)+"\n";
    if(!workingDirectory.isEmpty())text+="Path="+encode(workingDirectory)+"\n";
    QSaveFile file(path);
    const QByteArray bytes=text.toUtf8();
    if(!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
        || file.write(bytes)!=bytes.size() || !file.commit()) {
        if(error)*error=QStringLiteral("保存应用启动器失败：")+file.errorString();return {};
    }
    if(!validDesktop(path)){QFile::remove(path);if(error)*error=QStringLiteral("系统无法读取生成的应用启动器。");return {};}
    return path;
}
TaskbarPin::WindowIdentity TaskbarPin::windowIdentity(qulonglong window) {
    WindowIdentity result;
    if(!window || QGuiApplication::platformName()!="xcb")return result;
    Display *display=XOpenDisplay(nullptr);if(!display)return result;
    // A task can exit between the menu request and the property read. Xlib's
    // default BadWindow handler would otherwise terminate the entire panel.
    XSync(display,False);
    auto old=XSetErrorHandler([](Display *,XErrorEvent *){return 0;});
    XClassHint hint{};
    if(XGetClassHint(display,Window(window),&hint)) {
        if(hint.res_name){result.instance=QString::fromLocal8Bit(hint.res_name);XFree(hint.res_name);}
        if(hint.res_class){result.windowClass=QString::fromLocal8Bit(hint.res_class);XFree(hint.res_class);}
    }
    Atom type=None;int format=0;unsigned long count=0,remaining=0;unsigned char *data=nullptr;
    unsigned long pid=0;
    if(XGetWindowProperty(display,Window(window),XInternAtom(display,"_NET_WM_PID",False),0,1,False,AnyPropertyType,
        &type,&format,&count,&remaining,&data)==Success && format==32 && count==1)pid=*reinterpret_cast<unsigned long *>(data);
    if(data)XFree(data);
    XSync(display,False);XSetErrorHandler(old);XCloseDisplay(display);
    if(!pid)return result;
    const QString proc="/proc/"+QString::number(pid);
    struct stat info{};
    if(::stat(QFile::encodeName(proc).constData(),&info)!=0 || info.st_uid!=getuid())return result;
    QFile command(proc+"/cmdline");
    if(!command.open(QIODevice::ReadOnly))return result;
    const QByteArray bytes=command.read(1024*1024+1);
    if(bytes.size()>1024*1024)return result;
    QList<QByteArray> args=bytes.split('\0');if(!args.isEmpty() && args.last().isEmpty())args.removeLast();
    if(args.isEmpty())return result;
    for(const auto &arg:args)result.arguments.append(QString::fromLocal8Bit(arg));
    result.workingDirectory=QFileInfo(proc+"/cwd").symLinkTarget();
    // argv[0] can name a persistent launcher while /proc/exe points into a
    // temporary AppImage mount. Prefer the original executable when usable.
    result.executable=executablePath(result.arguments.first(),result.workingDirectory);
    if(result.executable.isEmpty())result.executable=QFileInfo(proc+"/exe").symLinkTarget();
    QFile environment(proc+"/environ");
    if(environment.open(QIODevice::ReadOnly))for(const QByteArray &variable:environment.read(1024*1024).split('\0')) {
        if(!variable.startsWith("APPIMAGE="))continue;
        const QString image=executablePath(QString::fromLocal8Bit(variable.mid(9)),result.workingDirectory);
        if(!image.isEmpty())result.executable=image;
    }
    return result;
}

TaskbarPinCompat::TaskbarPinCompat(QObject *parent):QObject(parent) {
    qApp->installEventFilter(this);
    // The style controller can attach after startup pins were already shown.
    QTimer::singleShot(0,this,[this]{
        for(auto *w:QApplication::allWidgets())if(w->inherits("UKUITaskButton"))routePinnedLaunch(w);
    });
}
TaskbarPinCompat::~TaskbarPinCompat(){if(qApp)qApp->removeEventFilter(this);}
bool TaskbarPinCompat::eventFilter(QObject *object,QEvent *event) {
    auto *widget=qobject_cast<QWidget *>(object);
    if(widget && widget->inherits("UKUITaskButton") && (event->type()==QEvent::Show || event->type()==QEvent::Polish)) {
        QPointer<QWidget> guard(widget);
        QTimer::singleShot(0,this,[this,guard]{if(guard)routePinnedLaunch(guard);});
    }
    auto *menu=qobject_cast<QMenu *>(object);
    if(menu && menu->parentWidget() && menu->parentWidget()->inherits("UKUITaskButton")) {
        if(event->type()==QEvent::Polish && !menu->property("liquidTaskbarPinConnected").toBool()) {
            menu->setProperty("liquidTaskbarPinConnected",true);
            connect(menu,&QMenu::aboutToShow,this,[this,menu]{prepare(menu);});
        }
        if(event->type()==QEvent::Show)prepare(menu);
    }
    return false;
}
void TaskbarPinCompat::routePinnedLaunch(QWidget *widget) {
    if(widget->property("liquidPinnedLaunchRouted").toBool())return;
    auto window=native<QVariant (*)(QWidget *)>(widget,"_ZNK14UKUITaskButton8windowIdEv");
    auto *button=qobject_cast<QAbstractButton *>(widget);
    QWidget *group=ancestor(widget->parentWidget(),"UKUITaskGroup");
    bool numeric=false;
    const qulonglong id=window?window(widget).toULongLong(&numeric):0;
    if(!button || !group || !numeric || id!=0 || buttonDesktop(widget).isEmpty())return;
    // This OEM's quick-launch clicked lambda dereferences a constructor-local
    // shared_ptr after it has expired (confirmed crash in execAction after a
    // panel restart). Replace only that button -> group clicked connection;
    // unrelated observers and all real-window activation paths stay intact.
    if(!disconnect(button,SIGNAL(clicked(bool)),group,nullptr))return;
    widget->setProperty("liquidPinnedLaunchRouted",true);
    QPointer<QWidget> guard(widget),parent(group);
    connect(button,&QAbstractButton::clicked,this,[guard,parent]{
        if(!guard)return;
        QString error;
        if(!launchDesktopFileCompat(buttonDesktop(guard),&error))
            LiquidDialog::warning(parent,QStringLiteral("启动应用失败"),error);
    });
}
void TaskbarPinCompat::prepare(QMenu *menu) {
    QWidget *button=ancestor(menu->parentWidget(),"UKUITaskButton");
    if(!button)return;
    QWidget *group=ancestor(button->parentWidget(),"UKUITaskGroup");
    QWidget *bar=ancestor(button->parentWidget(),"UKUITaskBar");
    QString path=group?groupDesktop(group):QString();if(path.isEmpty())path=buttonDesktop(button);
    const bool pinned=isPinned(bar,group,path);
    QAction *ours=menu->findChild<QAction *>("liquidTaskbarPinAction",Qt::FindDirectChildrenOnly);
    for(auto *action:menu->actions())if(action!=ours && nativePinAction(action))menu->removeAction(action);
    if(!ours) {
        ours=new QAction(menu);ours->setObjectName("liquidTaskbarPinAction");
        menu->insertAction(menu->actions().isEmpty()?nullptr:menu->actions().first(),ours);
        QPointer<QWidget> guard(button);
        connect(ours,&QAction::triggered,this,[this,guard]{
            // Let the native popup release its grab before a fallback picker
            // or failure dialog is shown. The application can exit meanwhile.
            QTimer::singleShot(0,this,[this,guard]{if(guard)toggle(guard);});
        });
    }
    ours->setText(pinned?QStringLiteral("从任务栏取消固定"):QStringLiteral("固定到任务栏"));
    ours->setIcon(QIcon::fromTheme(pinned?"ukui-unfixed-symbolic":"ukui-fixed-symbolic"));
    ours->setEnabled(true);
}
void TaskbarPinCompat::toggle(QWidget *button) {
    QPointer<QWidget> guard(button),group=ancestor(button->parentWidget(),"UKUITaskGroup");
    QPointer<QWidget> bar=ancestor(button->parentWidget(),"UKUITaskBar");
    auto fail=[bar](const QString &text){LiquidDialog::warning(bar,QStringLiteral("固定到任务栏"),text);};
    if(!group || !bar){fail(QStringLiteral("当前任务栏未提供应用固定接口。"));return;}
    QString path=groupDesktop(group);if(path.isEmpty())path=buttonDesktop(button);
    const bool pinned=isPinned(bar,group,path);
    QString error;
    TaskbarPin::WindowIdentity identity;
    const bool generated=TaskbarPin::desktopEntry(path).value("X-UKUI-Liquid-Pinned")=="true";
    const bool valid=TaskbarPin::validDesktop(path);
    if(!pinned && (generated || !valid)) {
        auto window=native<QVariant (*)(QWidget *)>(button,"_ZNK14UKUITaskButton8windowIdEv");
        identity=TaskbarPin::windowIdentity(window?window(button).toULongLong():0);
    }
    if(!pinned && (!valid || (generated && !matchesGeneratedLauncher(TaskbarPin::desktopEntry(path),identity)))) {
        path=TaskbarPin::findDesktop(identity,applicationDirectories());
        if(path.isEmpty() && !identity.executable.isEmpty())
            path=TaskbarPin::writeLauncher(identity,applicationDirectories().first(),&error);
        if(path.isEmpty() && identity.executable.isEmpty()) {
            const QString selected=LiquidDialog::getOpenFileName(bar,QStringLiteral("选择这个应用的启动程序或快捷方式"),QDir::homePath());
            if(selected.isEmpty())return;
            if(TaskbarPin::validDesktop(selected))path=selected;
            else {
                identity.executable=selected;identity.arguments=QStringList{selected};
                path=TaskbarPin::writeLauncher(identity,applicationDirectories().first(),&error);
            }
        }
        if(path.isEmpty()){fail(error.isEmpty()?QStringLiteral("无法确定这个应用的启动方式。"):error);return;}
    }
    if(!guard || !group || !bar)return;
    auto setGroup=native<void (*)(QWidget *,QString)>(group,"_ZN13UKUITaskGroup18setDesktopFileNameE7QString");
    auto setButton=native<void (*)(QWidget *,QString)>(guard,"_ZN14UKUITaskButton18setDesktopFileNameE7QString");
    if(!setGroup || !setButton){fail(QStringLiteral("当前任务栏缺少应用启动器关联接口。"));return;}
    const QString oldGroup=groupDesktop(group);
    QList<QPair<QPointer<QWidget>,QString>> previous;
    if(!pinned) {
        setGroup(group,path);
        for(auto *item:group->findChildren<QWidget *>()) {
            if(!item->inherits("UKUITaskButton"))continue;
            previous.append(qMakePair(QPointer<QWidget>(item),buttonDesktop(item)));
            setButton(item,path);
        }
    }
    const bool invoked=QMetaObject::invokeMethod(bar,pinned?"unpinFromTaskbar":"pinToTaskbar",Qt::DirectConnection,Q_ARG(QString,path));
    if(!invoked || !bar || isPinned(bar,group,path)==pinned) {
        if(group)setGroup(group,oldGroup);
        for(const auto &item:previous)if(item.first)setButton(item.first,item.second);
        fail(pinned?QStringLiteral("取消固定未成功，请重试。"):QStringLiteral("任务栏未能保存固定状态，请重试。"));
    }
}
