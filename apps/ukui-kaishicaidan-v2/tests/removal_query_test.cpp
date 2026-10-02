#include "StartMenu.h"
#include "LiquidPopup.h"
#include "BackgroundTask.h"
#include "DesktopAppLaunch.h"
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDebug>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QKeyEvent>

class StartMenuRemovalQueryTest {
public:
    static int run(const QString &root) {
        int failures = 0;
        auto check = [&](bool ok, const char *message) {
            if (ok) qInfo() << "PASS:" << message;
            else { ++failures; qCritical() << "FAIL:" << message; }
        };
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        const QString bin = root + "/bin", applications = root + "/data/applications";
        QDir().mkpath(bin); QDir().mkpath(applications);
        const QByteArray script = R"(#!/bin/sh
if [ "$1" = "-S" ]; then
  printf '%s\n' "$2" >> "$UKUI_QUERY_CALLS"
  : > "$UKUI_QUERY_STARTED"
  while [ ! -f "$UKUI_QUERY_RELEASE" ]; do sleep 0.01; done
  case "$2" in
    *fixture-b.desktop) exit 1 ;;
    *fixture-a.desktop) printf 'fixture-a-package: %s\n' "$2" ;;
    *fixture-c.desktop) printf 'fixture-c-package: %s\n' "$2" ;;
    *) exit 1 ;;
  esac
  exit 0
fi
if [ "$1" = "-W" ]; then printf 'ii '; exit 0; fi
exit 1
)";
        check(write(bin + "/dpkg-query", script), "lookup backend with explicit release signal created");
        QFile::setPermissions(bin + "/dpkg-query", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
        qputenv("UKUI_QUERY_CALLS", (root + "/calls").toUtf8());
        qputenv("UKUI_QUERY_STARTED", (root + "/started").toUtf8());
        qputenv("UKUI_QUERY_RELEASE", (root + "/release").toUtf8());
        AppEntry a; a.name = "Fixture A"; a.desktopPath = applications + "/fixture-a.desktop";
        a.exec = "fixture-nonexistent-command";
        AppEntry b = a; b.name = "Fixture B"; b.desktopPath = applications + "/fixture-b.desktop";
        AppEntry c = a; c.name = "Fixture C"; c.desktopPath = applications + "/fixture-c.desktop";
        for (const auto &entry : {a, b, c})
            write(entry.desktopPath, "[Desktop Entry]\nType=Application\nName=Fixture\nExec=fixture-nonexistent-command\n");
        StartMenu menu;
        menu.show(); QApplication::processEvents();
        QPointer<QAction> oldAction, discardedAction;
        auto current = [] { return qobject_cast<QMenu *>(QApplication::activePopupWidget()); };
        QTimer first; first.setInterval(10);
        QElapsedTimer deadline; deadline.start();
        QObject::connect(&first, &QTimer::timeout, [&] {
            auto *popup = current();
            if (popup && (QFileInfo::exists(root + "/started") || deadline.elapsed() > 3000)) {
                oldAction = popup->actions().last();
                check(!oldAction->isEnabled(), "first menu remains usable while removal lookup is disabled");
                popup->close(); first.stop();
            }
        });
        first.start(); menu.showAppContextMenu(a, QPoint(150, 150));
        check(menu.m_removalQueryPending, "first query remains active after menu closes");
        QTimer second; second.setInterval(10); deadline.restart();
        QObject::connect(&second, &QTimer::timeout, [&] {
            if (auto *popup = current()) {
                discardedAction = popup->actions().last();
                check(discardedAction->text() == "正在检查安装来源…", "second menu queues its own lookup without blocked label");
                popup->close(); second.stop();
            }
        });
        second.start();
        menu.showAppContextMenu(b, QPoint(150, 150));
        check(!oldAction && !discardedAction, "closed menus destroy their callback recipients");
        // An actual uninstall impact simulation must not suppress read-only lookup.
        menu.m_removalPending = true;
        QRect initialGeometry; bool resolved = false; bool captured = false;
        QTimer poll; poll.setInterval(10); deadline.restart();
        QObject::connect(&poll, &QTimer::timeout, [&] {
            auto *popup = current(); if (!popup) return;
            if (!captured) { initialGeometry = popup->geometry(); captured = true;
                check(write(root + "/release", "release"), "release first worker after latest popup is shown"); }
            auto *action = popup->actions().last();
            if (action->isEnabled() || deadline.elapsed() > 5000) {
                resolved = action->isEnabled();
                check(action->text() == "卸载 deb 软件包", "latest menu receives its own removal label");
                check(action->toolTip().contains("fixture-c-package"), "latest menu receives its own package, never stale result");
                check(popup->geometry() == initialGeometry, "lookup completion preserves popup geometry");
                check(menu.m_removalPending, "read-only query preserves independent uninstall simulation guard");
                popup->close(); poll.stop();
            }
        });
        poll.start(); menu.showAppContextMenu(c, QPoint(150, 150));
        check(resolved && !menu.m_removalQueryPending, "latest menu completes without needing reopen");
        QFile calls(root + "/calls"); calls.open(QIODevice::ReadOnly); const QByteArray history = calls.readAll();
        check(history.count("fixture-a.desktop") == 1 && history.count("fixture-c.desktop") == 1
            && !history.contains("fixture-b.desktop"), "one active worker coalesces closed pending menus to latest request");
        menu.m_removalPending = false;
        QFile::remove(root + "/started"); QFile::remove(root + "/release");
        QTimer closeAgain; closeAgain.setInterval(10); deadline.restart();
        QObject::connect(&closeAgain, &QTimer::timeout, [&] {
            if (auto *popup = current()) {
                if (QFileInfo::exists(root + "/started") || deadline.elapsed() > 3000) {
                    oldAction = popup->actions().last(); popup->close(); closeAgain.stop();
                }
            }
        });
        closeAgain.start(); menu.showAppContextMenu(a, QPoint(150, 150));
        check(menu.m_removalQueryPending && !oldAction, "same-app reopen starts with older recipient destroyed");
        QTimer reopened; reopened.setInterval(10); deadline.restart(); resolved = false; bool released = false;
        QObject::connect(&reopened, &QTimer::timeout, [&] {
            if (auto *popup = current()) {
                if (!released) { write(root + "/release", "release"); released = true; }
                auto *action = popup->actions().last();
                if (action->isEnabled() || deadline.elapsed() > 5000) {
                    resolved = action->isEnabled() && action->toolTip().contains("fixture-a-package");
                    popup->close(); reopened.stop();
                }
            }
        });
        reopened.start(); menu.showAppContextMenu(a, QPoint(150, 150));
        check(resolved && !menu.m_removalQueryPending, "same-app new menu completes its own query");
        QFile::remove(root + "/started"); QFile::remove(root + "/release");
        deadline.restart(); first.start(); menu.showAppContextMenu(a, QPoint(150, 150));
        check(menu.m_removalQueryPending, "orphan fixture keeps first worker active");
        deadline.restart(); second.start(); menu.showAppContextMenu(b, QPoint(150, 150));
        check(!oldAction && !discardedAction, "both orphan fixture recipients destroyed");
        check(write(root + "/release", "release"), "release worker after queued menu is destroyed");
        deadline.restart();
        while (menu.m_removalQueryPending && deadline.elapsed() < 5000) {
            QEventLoop loop; QTimer::singleShot(10, &loop, &QEventLoop::quit); loop.exec();
        }
        QFile orphanCalls(root + "/calls"); orphanCalls.open(QIODevice::ReadOnly);
        const auto orphanHistory = orphanCalls.readAll();
        check(!menu.m_removalQueryPending && !menu.m_nextRemovalQuery
            && !orphanHistory.contains("fixture-b.desktop"), "closed latest request is discarded without starting orphan dpkg query");
        auto settle=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
        const QString runner=bin+"/播放器 working";
        write(runner,("#!/bin/sh\nprintf launched > '"+root+"/launched'\n").toUtf8());
        QFile::setPermissions(runner,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        auto entry=[&](const QString &path,const QString &exec,const QString &extra=QString()) {
            return write(path,("[Desktop Entry]\nType=Application\nName=Fixture\nExec="+exec+"\n"+extra).toUtf8());
        };
        entry(b.desktopPath,"\""+runner+"\" %U");
        check(StartMenu::brokenShortcutExecutable(b.desktopPath).isEmpty(),"quoted executable with spaces is recognized as working");
        const auto normal=StartMenu::detectAppRemovalTarget(b);
        check(normal.kind==StartMenu::AppRemovalTarget::Kind::Unsupported
            && !normal.actionText.contains("快捷方式"),"working user shortcut never receives a shortcut removal action");
        menu.launchAppEntry(b);deadline.restart();
        while(menu.m_lastRecordedActiveDesktop!=b.desktopPath && deadline.elapsed()<4000)settle(10);
        check(QFileInfo::exists(root+"/launched") && menu.m_lastRecordedActiveDesktop==b.desktopPath,
            "production launch dispatches quoted Exec through GLib DesktopAppInfo and records only success");
        auto launchAndAnswer=[&](const AppEntry &app,const QString &title,bool yes,std::function<void()> repair={}) {
            bool handled=false;QTimer answer;answer.setInterval(10);
            QObject::connect(&answer,&QTimer::timeout,[&] {
                auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;
                handled=true;answer.stop();
                check(dialog->windowTitle()==title,"launch error chooses the correct failure or broken-shortcut dialog");
                check(!menu.isVisible() && dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
                    "launch failure dialog is visible above the suspended launcher");
                if(title=="快捷方式已失效") {
                    QPushButton *no=nullptr,*confirm=nullptr;
                    for(auto *button:dialog->findChildren<QPushButton *>()) {
                        if(button->property("messageChoice").toInt()==QMessageBox::No)no=button;
                        if(button->property("messageChoice").toInt()==QMessageBox::Yes)confirm=button;
                    }
                    check(no && no->isDefault(),"broken shortcut suggestion defaults to keeping the entry");
                    if(repair)repair();
                    if(yes && confirm)confirm->click();else dialog->reject();
                } else dialog->reject();
            });
            answer.start();menu.show();menu.launchAppEntry(app);deadline.restart();
            while(!handled && deadline.elapsed()<4000)settle(10);
            answer.stop();check(handled,"launch helper outcome reaches a bounded confirmation");
            settle(30);
        };
        write(b.desktopPath,("[Desktop Entry]\nType=Link\nName=Fixture\nURL=file:///tmp/\nExec=\""+runner+"\"\n").toUtf8());
        launchAndAnswer(b,"无法打开应用",false);
        check(QFileInfo::exists(b.desktopPath),"unavailable desktop entry keeps a working executable shortcut");
        const QString missing=bin+"/missing player";
        entry(b.desktopPath,"\""+missing+"\" %U");
        check(StartMenu::brokenShortcutExecutable(b.desktopPath)==missing,"missing quoted executable is proven from current desktop entry");
        launchAndAnswer(b,"快捷方式已失效",false);
        check(QFileInfo::exists(b.desktopPath),"cancel preserves broken shortcut");
        launchAndAnswer(b,"快捷方式已失效",true,[&] {
            write(missing,"#!/bin/sh\nexit 0\n");
            QFile::setPermissions(missing,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        });
        check(QFileInfo::exists(b.desktopPath),"repair during confirmation prevents removing a now-working shortcut");
        QFile::remove(missing);
        launchAndAnswer(b,"快捷方式已失效",true);
        check(!QFileInfo::exists(b.desktopPath) && QFileInfo::exists(runner),"confirmed cleanup removes only the broken desktop entry");
        entry(b.desktopPath,"\""+missing+"\"","DBusActivatable=true\n");
        check(StartMenu::brokenShortcutExecutable(b.desktopPath).isEmpty(),"D-Bus activated app is not judged broken by unused Exec");
        entry(b.desktopPath,"env FIXTURE=1 fixture-missing-command");
        check(StartMenu::brokenShortcutExecutable(b.desktopPath).isEmpty(),"environment wrapper never triggers guessed cleanup");
        entry(root+"/system.desktop","fixture-missing-command");
        check(StartMenu::brokenShortcutExecutable(root+"/system.desktop").isEmpty(),"entries outside user shortcut folders cannot be removed");
        // Release an actual asynchronous simulation failure only after the
        // launcher has been reopened. No real package manager is executed.
        const QByteArray simulation = R"(#!/bin/sh
if [ "$1" != "-s" ]; then exit 99; fi
: > "$UKUI_REMOVE_STARTED"
while [ ! -f "$UKUI_REMOVE_RELEASE" ]; do sleep 0.01; done
printf 'fixture delayed simulation failure\n' >&2
exit 1
)";
        check(write(bin+"/apt-get",simulation),"isolated uninstall simulation failure backend created");
        QFile::setPermissions(bin+"/apt-get",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        qputenv("UKUI_REMOVE_STARTED",(root+"/remove-started").toUtf8());
        qputenv("UKUI_REMOVE_RELEASE",(root+"/remove-release").toUtf8());
        StartMenu::AppRemovalTarget removal;removal.kind=StartMenu::AppRemovalTarget::Kind::DebPackage;removal.target="fixture-package";
        menu.removeApp(a,removal);
        deadline.restart();
        while(!QFileInfo::exists(root+"/remove-started") && deadline.elapsed()<3000)settle(10);
        check(QFileInfo::exists(root+"/remove-started") && menu.m_removalPending,
            "real uninstall simulation waits in its isolated background worker");
        menu.show();
        check(menu.isVisible(),"launcher can be reopened while uninstall simulation is pending");
        bool inspectedDelayedWarning=false;
        QTimer delayedAnswer;delayedAnswer.setInterval(10);
        QObject::connect(&delayedAnswer,&QTimer::timeout,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if(!dialog)return;
            inspectedDelayedWarning=true;delayedAnswer.stop();
            check(dialog->windowTitle()=="无法卸载" && !menu.isVisible() && !menu.m_removalPending,
                "delayed uninstall failure suspends a reopened launcher before presenting its dialog");
            dialog->reject();
        });
        delayedAnswer.start();write(root+"/remove-release","release");deadline.restart();
        while(!inspectedDelayedWarning && deadline.elapsed()<4000)settle(10);
        delayedAnswer.stop();
        check(inspectedDelayedWarning,"delayed uninstall warning completes through the actual modal loop");
        bool inspectedConfirmation=false;
        QTimer::singleShot(30,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog && dialog->windowTitle()=="确认卸载" && !menu.isVisible(),
                "real uninstall confirmation hides the launcher before opening");
            if(dialog){
                inspectedConfirmation=true;
                menu.showMenu();menu.toggle();
                check(!menu.isVisible() && !menu.m_showPending
                          && QApplication::activeModalWidget()==dialog,
                    "show and Win-key requests preserve the real uninstall confirmation above the launcher");
                dialog->reject();
            }
        });
        menu.show();
        menu.confirmDebRemoval(a,removal,{"fixture-package"});
        check(inspectedConfirmation,"uninstall cancel exercises actual modal entry without running package commands");
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    if(argc==3 && QString::fromLatin1(argv[1])=="--launch-desktop") {
        QCoreApplication helper(argc,argv);
        return launchDesktopFile(helper.arguments().at(2));
    }
    QTemporaryDir isolated("/tmp/launcher-removal-query-XXXXXX");
    if (!isolated.isValid()) return 2;
    qputenv("HOME", isolated.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (isolated.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (isolated.path() + "/data").toUtf8());
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    BackgroundTask::ApplicationScope background;
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("kylin"); app.setApplicationName("launcher-query-test");
    LiquidPopup::installMenuGlyphStyle(app); LiquidPopup::install(app);
    return StartMenuRemovalQueryTest::run(isolated.path());
}
