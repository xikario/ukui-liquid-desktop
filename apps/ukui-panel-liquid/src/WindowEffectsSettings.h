#pragma once
#include <QWidget>
class QCheckBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QDBusMessage;

// A view onto UKUI KWin, not another animation engine.
class WindowEffectsSettings final : public QWidget {
public:
    explicit WindowEffectsSettings(QWidget *parent=nullptr);
    static void recoverPendingTrial();
private:
    void reloadBackend(int serial);
    void runBackendTrial(bool restore);
    void reload();
    void readFailure(const QDBusMessage &);
    void setBusy(bool);
    void toggleEffect(const QString &,bool);
    void saveDuration(int);
    QCheckBox *m_magic=nullptr,*m_maximize=nullptr;
    QSpinBox *m_duration=nullptr;
    QPushButton *m_reset=nullptr;
    QLabel *m_status=nullptr;
    QLabel *m_backend=nullptr;
    QPushButton *m_trial=nullptr,*m_restoreBackend=nullptr;
    bool m_magicSupported=false,m_maximizeSupported=false;
    bool m_busy=true,m_trialAvailable=false,m_restoreAvailable=false;
    bool m_helperRunning=false,m_trialPending=false;
    int m_reloadGeneration=0;
    QByteArray m_journalContents;
    QString m_operationError;
};
