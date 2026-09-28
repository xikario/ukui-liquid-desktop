#pragma once
#include "DeskletModels.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
class ActivityRecorder final : public QObject {
    Q_OBJECT
public:
    explicit ActivityRecorder(QObject *parent = nullptr);
    ~ActivityRecorder() override;
    const ActivityLedger &ledger() const { return m_ledger; }
    bool isRecording() const { return m_recording; }
    bool saveOk() const { return m_saveOk; }
    void setRecording(bool enabled);
    QString dataPath() const;
public slots:
    void sample();
    void flush();
    void setLocked(bool locked);
    void sessionLocked() { setLocked(true); }
    void sessionUnlocked() { setLocked(false); }
signals:
    void changed();
private:
    struct Native;
    std::unique_ptr<Native> m_native;
    ActivityLedger m_ledger;
    QElapsedTimer m_elapsed;
    QDateTime m_lastWall;
    QTimer m_sampleTimer, m_saveTimer;
    QString m_appId, m_appName;
    bool m_recording = true, m_locked = false, m_saveOk = true;
};
