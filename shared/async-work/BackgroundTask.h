#pragma once

#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QEventLoop>
#include <QSet>
#include <memory>
#include <type_traits>

// Work must capture values, never GUI objects. Callers coalesce requests before
// submitting. The thread owns its result until GUI delivery; destroying the
// recipient drops delivery. Shutdown stops submissions and drains workers while
// processing events, before application/global state can be destroyed.
namespace BackgroundTask {
namespace Detail {
class Registry final : public QObject {
public:
    explicit Registry(QCoreApplication *app) : QObject(app) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this] { drain(); });
    }
    ~Registry() override {
        // Fallback for hosts which never enter exec(). Production entry points
        // also use ApplicationScope, so this normally has nothing left to join.
        m_stopping = true;
        for (auto *thread : m_threads) {
            thread->requestInterruption();
            thread->wait();
            delete thread;
        }
    }
    bool stopping() const { return m_stopping; }
    void track(QThread *thread) {
        m_threads.insert(thread);
        connect(thread, &QThread::finished, this, [this, thread] {
            // finished precedes thread-local cleanup; join that too before the
            // application proceeds to destruct objects used by worker code.
            thread->wait();
            m_threads.remove(thread);
            thread->deleteLater();
            if (m_loop && m_threads.isEmpty()) m_loop->quit();
        });
    }
    void drain() {
        if (m_loop) return;
        m_stopping = true;
        for (auto *thread : m_threads) thread->requestInterruption();
        while (!m_threads.isEmpty()) {
            QEventLoop loop;
            m_loop = &loop;
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            m_loop = nullptr;
        }
    }
private:
    QSet<QThread *> m_threads;
    QEventLoop *m_loop = nullptr;
    bool m_stopping = false;
};
inline Registry *registry() {
    auto *app = QCoreApplication::instance();
    Q_ASSERT(app && QThread::currentThread() == app->thread());
    static QPointer<Registry> instance;
    if (!instance) instance = new Registry(app);
    return instance;
}
}

// Declare immediately after QApplication to keep application/global state alive
// across early-return paths as well as normal aboutToQuit. Work never owns GUI
// objects; their destruction independently disconnects completion delivery.
class ApplicationScope final {
public:
    ApplicationScope() : m_registry(Detail::registry()) {}
    ~ApplicationScope() { if (m_registry) m_registry->drain(); }
    ApplicationScope(const ApplicationScope &) = delete;
    ApplicationScope &operator=(const ApplicationScope &) = delete;
private:
    QPointer<Detail::Registry> m_registry;
};

template<class Work, class Done>
bool run(QObject *recipient, Work work, Done done)
{
    auto *registry = Detail::registry();
    if (!recipient || registry->stopping()) return false;
    using Result = typename std::decay<decltype(work())>::type;
    auto result = std::make_shared<Result>();
    auto *thread = QThread::create([work, result]() mutable { *result = work(); });
    registry->track(thread);
    const QPointer<Detail::Registry> guard(registry);
    QObject::connect(thread, &QThread::finished, recipient,
                     [result, done, guard] { if (guard && !guard->stopping()) done(*result); });
    thread->start();
    return true;
}
}
