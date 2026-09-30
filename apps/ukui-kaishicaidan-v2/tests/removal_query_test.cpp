#include "StartMenu.h"
#include "LiquidPopup.h"
#include "BackgroundTask.h"
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDebug>

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
  sleep 0.4
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
        check(write(bin + "/dpkg-query", script), "delayed lookup backend created");
        QFile::setPermissions(bin + "/dpkg-query", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
        qputenv("UKUI_QUERY_CALLS", (root + "/calls").toUtf8());
        qputenv("UKUI_QUERY_STARTED", (root + "/started").toUtf8());
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
        QTimer::singleShot(20, [&] {
            if (auto *popup = current()) {
                discardedAction = popup->actions().last();
                check(discardedAction->text() == "正在检查安装来源…", "second menu queues its own lookup without blocked label");
                popup->close();
            }
        });
        menu.showAppContextMenu(b, QPoint(150, 150));
        check(!oldAction && !discardedAction, "closed menus destroy their callback recipients");
        // An actual uninstall impact simulation must not suppress read-only lookup.
        menu.m_removalPending = true;
        QRect initialGeometry; bool resolved = false; bool captured = false;
        QTimer poll; poll.setInterval(10); deadline.restart();
        QObject::connect(&poll, &QTimer::timeout, [&] {
            auto *popup = current(); if (!popup) return;
            if (!captured) { initialGeometry = popup->geometry(); captured = true; }
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
        QFile::remove(root + "/started");
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
        QTimer reopened; reopened.setInterval(10); deadline.restart(); resolved = false;
        QObject::connect(&reopened, &QTimer::timeout, [&] {
            if (auto *popup = current()) {
                auto *action = popup->actions().last();
                if (action->isEnabled() || deadline.elapsed() > 5000) {
                    resolved = action->isEnabled() && action->toolTip().contains("fixture-a-package");
                    popup->close(); reopened.stop();
                }
            }
        });
        reopened.start(); menu.showAppContextMenu(a, QPoint(150, 150));
        check(resolved && !menu.m_removalQueryPending, "same-app new menu completes its own query");
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
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
