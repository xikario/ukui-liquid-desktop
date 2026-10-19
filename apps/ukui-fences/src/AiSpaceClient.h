#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
class AiSpaceClient final : public QObject {
    Q_OBJECT
public:
    explicit AiSpaceClient(QObject *parent=nullptr);
    void call(const QString &method,const QVariantList &arguments={});
    QJsonObject state() const { return m_state; }
    static bool openStandalone(bool settings=false);
signals:
    void stateChanged();
    void launchFailed();
private slots:
    void receiveState(const QString &json);
private:
    void refresh();
    QJsonObject m_state;
    bool m_launching=false;
    quint64 m_launchGeneration=0;
    quint64 m_serviceGeneration=0;
    void drainPending();
    QList<QPair<QString,QVariantList>> m_pending;
};
