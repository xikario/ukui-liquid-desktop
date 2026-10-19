#include "AiSpaceClient.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QStandardPaths>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>

namespace {
const QString service=QStringLiteral("org.ukui.AiSpace");
QDBusMessage message(const QString &method,const QVariantList &arguments={}) {
    auto m=QDBusMessage::createMethodCall(service,"/aiSpace",service,method);m.setArguments(arguments);return m;
}
QString program() {
    if(qEnvironmentVariableIsSet("AI_SPACE_BINARY")) {
        const QString override=qEnvironmentVariable("AI_SPACE_BINARY");
        return QFileInfo(override).isFile() && QFileInfo(override).isExecutable()?override:QString();
    }
    const QStringList paths{QStandardPaths::findExecutable("ai-space"),
        QCoreApplication::applicationDirPath()+"/ai-space"};
    for(const auto &path:paths)if(!path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isExecutable())return path;
    return {};
}
}
bool AiSpaceClient::openStandalone(bool settings) {
    auto bus=QDBusConnection::sessionBus();
    if(bus.interface() && bus.interface()->isServiceRegistered(service).value())
        return bus.send(message(settings?"showSettings":"showWindow"));
    const QString binary=program();
    if(binary.isEmpty())return false;
    return QProcess::startDetached(binary,{settings?QStringLiteral("--settings"):QStringLiteral("--show")});
}
AiSpaceClient::AiSpaceClient(QObject *parent):QObject(parent) {
    auto bus=QDBusConnection::sessionBus();
    bus.connect(service,"/aiSpace",service,"stateChanged",this,SLOT(receiveState(QString)));
    auto *watcher=new QDBusServiceWatcher(service,bus,QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString &,const QString &,const QString &owner){
        ++m_serviceGeneration;
        if(owner.isEmpty()){m_state={};emit stateChanged();return;}
        drainPending();
    });
    // Do not activate an independent application just by starting Fences.
    auto owner=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus",
        "org.freedesktop.DBus","NameHasOwner");owner.setArguments({service});
    auto *probe=new QDBusPendingCallWatcher(bus.asyncCall(owner,750),this);
    connect(probe,&QDBusPendingCallWatcher::finished,this,[this,probe]{
        const QDBusPendingReply<bool> reply=*probe;
        if(!reply.isError() && reply.value())refresh();probe->deleteLater();
    });
}
void AiSpaceClient::receiveState(const QString &json) {
    const auto doc=QJsonDocument::fromJson(json.toUtf8());
    if(!doc.isObject())return;m_state=doc.object();emit stateChanged();
}
void AiSpaceClient::refresh() {
    const auto generation=m_serviceGeneration;
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message("status"),1500),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,generation]{
        const QDBusPendingReply<QString> reply=*watcher;
        if(!reply.isError() && generation==m_serviceGeneration)receiveState(reply.value());watcher->deleteLater();
    });
}
void AiSpaceClient::drainPending() {
    m_launching=false;++m_launchGeneration;
    const auto requests=m_pending;m_pending.clear();
    for(const auto &request:requests)call(request.first,request.second);
    refresh();
}
void AiSpaceClient::call(const QString &method,const QVariantList &arguments) {
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message(method,arguments),1500),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,method,arguments]{
        const QDBusPendingReply<> reply=*watcher;watcher->deleteLater();
        if(!reply.isError()){refresh();return;}
        const auto name=reply.error().name();
        if(name!="org.freedesktop.DBus.Error.ServiceUnknown" && name!="org.freedesktop.DBus.Error.NameHasNoOwner") {emit launchFailed();return;}
        if(m_pending.size()<32)m_pending.append(qMakePair(method,arguments));
        if(m_launching)return;
        m_launching=true;
        const quint64 generation=++m_launchGeneration;
        if(!openStandalone()){m_launching=false;m_pending.clear();emit launchFailed();return;}
        // The service can appear between the failed call and the owner-change
        // delivery. Probe once, so that pending requests cannot be stranded.
        auto probeMessage=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","NameHasOwner");
        probeMessage.setArguments({service});
        auto *probe=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(probeMessage,750),this);
        connect(probe,&QDBusPendingCallWatcher::finished,this,[this,probe]{
            const QDBusPendingReply<bool> reply=*probe;probe->deleteLater();
            if(!reply.isError() && reply.value() && m_launching)drainPending();
        });
        QTimer::singleShot(10000,this,[this,generation]{if(m_launching && generation==m_launchGeneration){m_launching=false;m_pending.clear();emit launchFailed();}});
    });
}
