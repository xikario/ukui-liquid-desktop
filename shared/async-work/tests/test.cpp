#include "../BackgroundTask.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <atomic>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::atomic_bool completed{false}, interrupted{false};
    int callbacks = 0, staleCallbacks = 0, heartbeats = 0;
    bool rejected = false;
    const QString mode = app.arguments().value(1);
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start(5);
    {
        BackgroundTask::ApplicationScope scope;
        auto *recipient = new QObject;
        BackgroundTask::run(recipient, [] { QThread::msleep(30); return 1; },
                            [&](int) { ++staleCallbacks; });
        delete recipient;
        BackgroundTask::run(&app, [&] {
            QThread::msleep(160);
            interrupted = QThread::currentThread()->isInterruptionRequested();
            completed = true;
            return 42;
        }, [&](int) { ++callbacks; });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
            rejected = !BackgroundTask::run(&app, [] { return 0; }, [](int) {});
        });
        if (mode != "scope") {
            QTimer::singleShot(10, &app, [&] {
                if (mode == "exit") QCoreApplication::exit(7);
                else QCoreApplication::quit();
            });
            const int code = app.exec();
            if (code != (mode == "exit" ? 7 : 0)) return 1;
            if (!completed) { qCritical() << "exec returned before workers joined"; return 2; }
        }
    }
    if (mode == "scope") rejected = !BackgroundTask::run(&app, [] { return 0; }, [](int) {});
    if (!completed || !interrupted || callbacks || staleCallbacks || !rejected || heartbeats < 5) {
        qCritical() << "shutdown did not drain responsively" << completed << interrupted
                    << callbacks << staleCallbacks << rejected << heartbeats;
        return 3;
    }
    qInfo() << "PASS: shutdown joins workers, drops callbacks and rejects new work; heartbeat" << heartbeats;
    return 0;
}
