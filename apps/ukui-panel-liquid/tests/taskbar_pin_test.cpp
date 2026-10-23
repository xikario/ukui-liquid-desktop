#include "TaskbarPinCompat.h"
#include "DesktopLaunchCompat.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <cstdio>
#include <cstdlib>

static void check(bool ok,const char *message){if(!ok){qCritical("FAIL: %s",message);std::exit(1);}qInfo("PASS: %s",message);}
static void wait(int ms=40){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
static void write(const QString &path,const QByteArray &text){QFile f(path);check(f.open(QIODevice::WriteOnly),"fixture file writable");f.write(text);}
class UKUITaskGroup;
class UKUITaskBar : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    QStringList pinned;
    QString config;
    int operations=0;
    bool reject=false;
    __attribute__((noinline)) bool hasPinnedToTaskbar(QString path);
public slots:
    void pinToTaskbar(QString path);
    void unpinFromTaskbar(QString path);
};
class UKUITaskGroup : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    QString desktop;
    __attribute__((noinline)) QString getDesktopFileName();
    __attribute__((noinline)) void setDesktopFileName(QString);
    __attribute__((noinline)) bool isPinned();
};
class UKUITaskButton : public QToolButton {
    Q_OBJECT
public:
    using QToolButton::QToolButton;
    QString desktop;qulonglong window=0;
    __attribute__((noinline)) QString desktopFileName();
    __attribute__((noinline)) void setDesktopFileName(QString);
    __attribute__((noinline)) QVariant windowId() const;
};
QString UKUITaskButton::desktopFileName(){return desktop;}
void UKUITaskButton::setDesktopFileName(QString p){desktop=p;}
QVariant UKUITaskButton::windowId() const{return QVariant::fromValue(window);}
QString UKUITaskGroup::getDesktopFileName(){return desktop;}
void UKUITaskGroup::setDesktopFileName(QString p){desktop=p;}
bool UKUITaskGroup::isPinned(){return static_cast<UKUITaskBar *>(parentWidget())->pinned.contains(desktop);}
bool UKUITaskBar::hasPinnedToTaskbar(QString){return false;} // OEM legacy helper is unreliable.
void UKUITaskBar::pinToTaskbar(QString p){++operations;if(reject)return;if(!pinned.contains(p))pinned.append(p);QSettings(config,QSettings::IniFormat).setValue("pinned",pinned);}
void UKUITaskBar::unpinFromTaskbar(QString p){++operations;pinned.removeAll(p);QSettings(config,QSettings::IniFormat).setValue("pinned",pinned);}
static QAction *openMenu(QMenu &menu) {
    menu.ensurePolished();menu.popup(QPoint(20,20));wait();
    auto actions=menu.findChildren<QAction *>("liquidTaskbarPinAction");
    check(actions.size()==1 && actions.first()->isEnabled(),"each application menu has exactly one enabled pin operation");
    return actions.first();
}
int main(int argc,char **argv) {
    // Relaunch target used to verify real GIO Exec parsing, without a shell.
    if(argc>2 && QString::fromLocal8Bit(argv[1])=="--capture-arguments") {
        QJsonArray args;for(int i=3;i<argc;++i)args.append(QString::fromLocal8Bit(argv[i]));
        QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly))return 2;
        file.write(QJsonDocument(QJsonObject{{"args",args},{"cwd",QDir::currentPath()}}).toJson());return 0;
    }
    QTemporaryDir temp;check(temp.isValid(),"temporary config available");
    qputenv("XDG_DATA_HOME",(temp.path()+"/data").toUtf8());
    qputenv("XDG_CONFIG_HOME",(temp.path()+"/config").toUtf8());
    QApplication app(argc,argv);TaskbarPinCompat compat(&app);
    const QString apps=temp.path()+"/apps",lower=temp.path()+"/lower";
    QDir().mkpath(apps);QDir().mkpath(lower);
    const QString visible=apps+"/visible.desktop",hidden=apps+"/hidden.desktop";
    write(visible,"[Desktop Entry]\nType=Application\nName=Visible\nExec=/bin/true\nStartupWMClass=Example\n");
    write(hidden,"[Desktop Entry]\nType=Application\nName=Hidden\nExec=/bin/true\nStartupWMClass=Example\nNoDisplay=true\n");
    check(TaskbarPin::validDesktop(hidden),"NoDisplay launcher remains a valid launch target");
    TaskbarPin::WindowIdentity identity;identity.windowClass="Example";
    check(TaskbarPin::findDesktop(identity,{apps})==visible,"class discovery prefers the visible equivalent");
    write(apps+"/second.desktop","[Desktop Entry]\nType=Application\nName=Second\nExec=/bin/false\nStartupWMClass=Example\n");
    check(TaskbarPin::findDesktop(identity,{apps}).isEmpty(),"ambiguous class matches are not silently assigned to another app");
    QFile::remove(apps+"/second.desktop");
    write(lower+"/visible.desktop","[Desktop Entry]\nType=Application\nName=Lower\nExec=/bin/false\nStartupWMClass=Example\n");
    write(visible,"[Desktop Entry]\nType=Application\nName=Removed\nHidden=true\nExec=/bin/true\n");
    check(TaskbarPin::findDesktop(identity,{apps,lower})==hidden,"user-hidden override blocks an identically named lower-priority entry");
    check(!TaskbarPin::validDesktop(visible),"explicitly deleted entries are not treated as launchers");
    write(visible,"[Desktop Entry]\nType=Application\nName=Visible\nExec=/bin/true\nStartupWMClass=Example\n");
    QString error;const QString cwd=temp.path()+"/cwd with spaces";QDir().mkpath(cwd);
    identity.executable=QCoreApplication::applicationFilePath();identity.workingDirectory=cwd;
    const QString capture=temp.path()+"/capture.json";
    const QStringList special={"space value","quote\"value","dollar$HOME","back`tick","percent%U%","slash\\value","line\nbreak","",QStringLiteral("中文参数")};
    identity.arguments=QStringList{identity.executable,"--capture-arguments",capture};identity.arguments.append(special);
    const QString generated=TaskbarPin::writeLauncher(identity,apps,&error);
    check(!generated.isEmpty() && error.isEmpty(),"missing desktop can create a persistent hidden launcher");
    check(TaskbarPin::desktopEntry(generated).value("NoDisplay")=="true","generated launcher does not clutter application lists");
    check(TaskbarPin::writeLauncher(identity,apps,&error)==generated,"repeated pin discovery reuses the stable launcher");
    check(launchDesktopFileCompat(generated,&error),"generated launcher starts through production GIO launch path");
    for(int i=0;i<80 && !QFileInfo::exists(capture);++i)wait(25);
    QFile captured(capture);check(captured.open(QIODevice::ReadOnly),"relaunch target recorded its arguments");
    const auto capturedObject=QJsonDocument::fromJson(captured.readAll()).object();
    check(capturedObject.value("args").toArray()==QJsonArray::fromStringList(special),"spaces, quotes, percent fields, shell metacharacters, Unicode and empty args survive exactly");
    check(capturedObject.value("cwd").toString()==cwd,"working directory survives relaunch");
    // Same command from another workspace must not silently reuse its old Path.
    TaskbarPin::WindowIdentity otherCwd=identity;
    otherCwd.workingDirectory=temp.path()+"/other workspace";QDir().mkpath(otherCwd.workingDirectory);
    const QString generatedOnly=temp.path()+"/generated-only";
    const QString firstWorkspace=TaskbarPin::writeLauncher(identity,generatedOnly,&error);
    check(TaskbarPin::findDesktop(otherCwd,{generatedOnly}).isEmpty(),"class lookup rejects a generated launcher from another working directory");
    const QString secondWorkspace=TaskbarPin::writeLauncher(otherCwd,generatedOnly,&error);
    check(!secondWorkspace.isEmpty() && secondWorkspace!=firstWorkspace,"working directories have distinct persistent launch identities");
    check(TaskbarPin::findDesktop(otherCwd,{generatedOnly})==secondWorkspace,"class lookup selects the exact generated command and working directory");
    QFile::remove(capture);
    check(launchDesktopFileCompat(secondWorkspace,&error),"second workspace starts through GIO");
    for(int i=0;i<80 && !QFileInfo::exists(capture);++i)wait(25);
    QFile otherCapture(capture);check(otherCapture.open(QIODevice::ReadOnly),"second workspace recorded actual execution");
    check(QJsonDocument::fromJson(otherCapture.readAll()).object().value("cwd").toString()==otherCwd.workingDirectory,"second workspace actually runs in its own directory");
    TaskbarPin::WindowIdentity otherCommand=otherCwd;otherCommand.arguments.append("different");
    check(TaskbarPin::findDesktop(otherCommand,{generatedOnly}).isEmpty(),"class lookup rejects another generated command");
    // Preserve invocation paths: scripts and multi-call binaries use argv[0].
    const QString target=temp.path()+"/actual-tool",alias=temp.path()+"/alias-tool",record=temp.path()+"/alias-record";
    write(target,("#!/bin/sh\nprintf '%s' \"$0\" > '"+record+"'\n").toUtf8());
    QFile::setPermissions(target,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    check(QFile::link(target,alias),"symbolic executable entry available");
    TaskbarPin::WindowIdentity symlink;symlink.windowClass="AliasFixture";symlink.executable=alias;symlink.arguments=QStringList{alias};
    const QString aliasDesktop=TaskbarPin::writeLauncher(symlink,generatedOnly,&error);
    check(launchDesktopFileCompat(aliasDesktop,&error),"symbolic entry starts through GIO");
    for(int i=0;i<80 && !QFileInfo::exists(record);++i)wait(25);
    QFile aliasOutput(record);check(aliasOutput.open(QIODevice::ReadOnly) && aliasOutput.readAll()==alias.toUtf8(),"actual execution preserves symbolic invocation name");
    TaskbarPin::WindowIdentity invalid;invalid.executable="/no/such/program";
    check(TaskbarPin::writeLauncher(invalid,apps,&error).isEmpty() && !error.isEmpty(),"unlaunchable process gets an explicit error rather than a dead pin");

    UKUITaskBar bar;bar.config=temp.path()+"/native-panel.ini";bar.resize(500,48);
    UKUITaskGroup group(&bar);UKUITaskButton button(&group),second(&group);bar.show();group.show();button.show();
    for(const QString &desktop:QStringList{visible,hidden}) {
        group.desktop=desktop;button.desktop=desktop;second.desktop=desktop;
        QMenu menu(&button);auto *native=menu.addAction(QIcon::fromTheme("ukui-fixed-symbolic"),"Pin to taskbar");Q_UNUSED(native);
        menu.addAction("Close");QMenu *sub=menu.addMenu("More");sub->addAction("Other");
        auto *action=openMenu(menu);check(menu.actions().size()==3,"native pin is replaced without disturbing other actions");
        action->trigger();menu.hide();wait();
        check(bar.pinned.contains(desktop),"visible and NoDisplay applications both pin through native settings");
        check(QSettings(bar.config,QSettings::IniFormat).value("pinned").toStringList().contains(desktop),"native configuration persists the pin");
        action=openMenu(menu);check(action->text()==QStringLiteral("从任务栏取消固定"),"reopening reflects native pinned state");
        menu.hide();sub->popup(QPoint(30,30));wait();
        check(!sub->findChild<QAction *>("liquidTaskbarPinAction"),"application submenus are not polluted with duplicate pin controls");sub->hide();
        action->trigger();wait();check(!bar.pinned.contains(desktop),"hidden applications can also unpin");
    }
    QWidget foreignWindow;foreignWindow.setWindowTitle("Unregistered application");foreignWindow.show();wait();
    // Simulate an OEM app database assigning a stale generated entry by class.
    group.desktop=generated;button.desktop=group.desktop;second.desktop=group.desktop;
    button.window=foreignWindow.winId();
    QMenu unknown(&button);unknown.addAction("Close");
    QAction *action=openMenu(unknown);action->trigger();unknown.hide();wait(160);
    check(bar.pinned.size()==1 && bar.pinned.first()!=generated && TaskbarPin::validDesktop(bar.pinned.first()),"a stale generated association is replaced using the live process identity");
    check(group.desktop==bar.pinned.first() && second.desktop==group.desktop,"all existing task buttons share the persistent launch identity");
    const QString saved=bar.pinned.first();
    UKUITaskBar restored;restored.pinned=QSettings(bar.config,QSettings::IniFormat).value("pinned").toStringList();
    check(restored.pinned==QStringList{saved} && TaskbarPin::validDesktop(saved),"a fresh taskbar can recover the pinned launcher from native settings");
    action=openMenu(unknown);action->trigger();unknown.hide();wait();check(bar.pinned.isEmpty(),"generated launcher unpins cleanly");
    check(TaskbarPin::validDesktop(saved),"unpin does not delete a launcher still referenced by an existing window");
    // Reject a pin in the native backend: every changed task identity rolls back.
    bar.reject=true;group.desktop="/missing/rejected.desktop";button.desktop=group.desktop;second.desktop="/different/original.desktop";
    QTimer closeError;closeError.setInterval(20);
    QObject::connect(&closeError,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QDialog *>(w))dialog->reject();});
    closeError.start();action=openMenu(unknown);action->trigger();unknown.hide();wait(180);closeError.stop();
    check(bar.pinned.isEmpty() && group.desktop=="/missing/rejected.desktop" && button.desktop==group.desktop && second.desktop=="/different/original.desktop","native failure restores all previous button and group identities");bar.reject=false;
    int nativeLaunches=0,otherClicks=0;
    UKUITaskButton launchButton(&group);launchButton.desktop=generated;launchButton.window=0;
    QObject::connect(&launchButton,&QAbstractButton::clicked,&group,[&]{++nativeLaunches;});
    QObject::connect(&launchButton,&QAbstractButton::clicked,&bar,[&]{++otherClicks;});
    launchButton.show();wait();QFile::remove(capture);launchButton.click();
    for(int i=0;i<80 && !QFileInfo::exists(capture);++i)wait(25);
    check(nativeLaunches==0 && otherClicks==1 && QFileInfo::exists(capture),"pinned launch replaces only the expired OEM callback and keeps other observers");
        // Even windows with no PID/launcher keep an enabled operation. A picker
    // lets the user supply the missing launch information instead of a dead pin.
    group.desktop="/missing/no-identity.desktop";button.desktop=group.desktop;button.window=0;
    QTimer cancelPicker;cancelPicker.setInterval(20);
    QObject::connect(&cancelPicker,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())if(auto *picker=qobject_cast<QFileDialog *>(w))picker->reject();});
    cancelPicker.start();action=openMenu(unknown);action->trigger();unknown.hide();wait(180);cancelPicker.stop();
    check(bar.pinned.isEmpty() && group.desktop=="/missing/no-identity.desktop","canceling missing-information recovery leaves native settings untouched");
    bool selected=false;QTimer choosePicker;choosePicker.setInterval(20);
    QObject::connect(&choosePicker,&QTimer::timeout,[&]{
        if(selected)return;
        for(auto *w:QApplication::topLevelWidgets())if(auto *picker=qobject_cast<QFileDialog *>(w)) {
            selected=true;picker->selectFile(visible);QMetaObject::invokeMethod(picker,"accept",Qt::QueuedConnection);break;
        }
    });
    choosePicker.start();action=openMenu(unknown);action->trigger();unknown.hide();wait(180);choosePicker.stop();
    check(selected && bar.pinned.contains(visible),"windows without process metadata can pin a user-selected launcher");
    bar.unpinFromTaskbar(visible);
    // BadWindow used to terminate callers of the default Xlib error handler.
    foreignWindow.close();wait();
    check(TaskbarPin::windowIdentity(0x7ffffffe).executable.isEmpty(),"a disappearing window does not terminate the panel");
    QMenu ordinary;ordinary.addAction("Unrelated");ordinary.popup(QPoint(20,20));wait();
    check(!ordinary.findChild<QAction *>("liquidTaskbarPinAction"),"non-task menus remain untouched");ordinary.hide();
    return 0;
}
#include "taskbar_pin_test.moc"
