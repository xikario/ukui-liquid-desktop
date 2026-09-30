#pragma once
#include "LiquidMaterial.h"
#include "../../async-work/BackgroundTask.h"
#include <QObject>
#include <functional>

namespace LiquidMaterial {
// GUI-side request owner. Workers capture only a QImage; GL stays on the GUI
// thread. A busy owner coalesces changes and only delivers the latest request.
class Preparation final : public QObject {
public:
    using Done = std::function<void(const Prepared &)>;
    explicit Preparation(QObject *parent) : QObject(parent) {}
    void request(const QImage &source, Done done) {
        ++m_revision;
        m_source = source;
        m_done = std::move(done);
        start();
    }
    void invalidate() {
        ++m_revision;
        m_source = {};
        m_done = {};
    }
    bool busy() const { return m_busy; }
private:
    void start() {
        if (m_busy || !m_done) return;
        m_busy = true;
        const auto revision = m_revision;
        const QImage source = m_source;
        if (!BackgroundTask::run(this, [source] { return prepare(source); },
            [this, revision](const Prepared &material) {
                m_busy = false;
                if (revision != m_revision) { start(); return; }
                auto done = std::move(m_done);
                m_source = {};
                if (done) done(material);
            })) m_busy = false;
    }
    QImage m_source;
    Done m_done;
    quint64 m_revision = 0;
    bool m_busy = false;
};
}
