#include "StrawberryPlayer.h"
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QImageReader>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
namespace {
const QString service="org.mpris.MediaPlayer2.strawberry", path="/org/mpris/MediaPlayer2";
const QString player="org.mpris.MediaPlayer2.Player", properties="org.freedesktop.DBus.Properties";
constexpr qint64 maxCoverBytes=8*1024*1024;
QVariant unwrap(QVariant value) {
    if(value.userType()==qMetaTypeId<QDBusVariant>())return value.value<QDBusVariant>().variant();
    return value;
}
QVariantMap mapValue(QVariant value) {
    value=unwrap(value);
    if(value.userType()==qMetaTypeId<QDBusArgument>())return qdbus_cast<QVariantMap>(value);
    return value.toMap();
}
}
StrawberryPlayer::StrawberryPlayer(QObject *parent):QObject(parent),m_network(new QNetworkAccessManager(this)) {
    auto bus=QDBusConnection::sessionBus();
    auto *watch=new QDBusServiceWatcher(service,bus,QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(watch,&QDBusServiceWatcher::serviceOwnerChanged,this,&StrawberryPlayer::ownerChanged);
    bus.connect(service,path,properties,"PropertiesChanged",this,SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    bus.connect(service,path,player,"Seeked",this,SLOT(seeked(qlonglong)));
    m_progress.setInterval(1000);
    connect(&m_progress,&QTimer::timeout,this,[this]{emit changed();if(++m_ticks%10==0)requestPosition();});
    discover();
}
void StrawberryPlayer::discover() {
    const int generation=m_generation;
    auto msg=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetNameOwner");msg<<service;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation]{
        QDBusPendingReply<QString> reply=*w;w->deleteLater();
        if(generation!=m_generation)return;
        ownerChanged(service,{},reply.isError()?QString():reply.value());
    });
}
void StrawberryPlayer::ownerChanged(const QString &,const QString &,const QString &owner) {
    ++m_generation;++m_revision;m_owner=owner;m_connected=false;m_values.clear();m_position=0;m_elapsed.invalidate();
    m_refreshing=m_refreshAgain=m_positionPending=false;m_error.clear();
    updateCover();updateTimer();emit changed();
    if(!owner.isEmpty())refresh();
}
void StrawberryPlayer::refresh() {
    if(m_owner.isEmpty())return;
    if(m_refreshing){m_refreshAgain=true;return;}
    m_refreshing=true;const int generation=m_generation,revision=m_revision;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,properties,"GetAll");msg<<player;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation,revision]{
        QDBusPendingReply<QVariantMap> reply=*w;w->deleteLater();if(generation!=m_generation)return;
        m_refreshing=false;
        if(!reply.isError() && revision==m_revision){
            m_values=reply.value();m_connected=true;m_error.clear();
            m_position=unwrap(m_values.value("Position")).toLongLong();m_elapsed.restart();
            updateCover();updateTimer();emit changed();
        } else if(reply.isError()){m_error="暂时无法读取播放状态";emit changed();}
        if(m_refreshAgain){m_refreshAgain=false;refresh();}
    });
}
void StrawberryPlayer::propertiesChanged(const QString &iface,const QVariantMap &updates,const QStringList &invalidated) {
    if(iface!=player || m_owner.isEmpty())return;
    m_position=position();m_elapsed.restart();++m_revision;
    for(auto it=updates.begin();it!=updates.end();++it)m_values[it.key()]=unwrap(it.value());
    for(const auto &name:invalidated)m_values.remove(name);
    if(updates.contains("Metadata")){m_position=0;updateCover();requestPosition();}
    if(updates.contains("Position"))m_position=unwrap(updates.value("Position")).toLongLong();
    updateTimer();emit changed();
    if(!invalidated.isEmpty() || m_refreshing || !m_connected)refresh();
}
QVariantMap StrawberryPlayer::metadata() const {return mapValue(m_values.value("Metadata"));}
QString StrawberryPlayer::trackId() const {
    const QVariant id=unwrap(metadata().value("mpris:trackid"));
    return id.userType()==qMetaTypeId<QDBusObjectPath>()?id.value<QDBusObjectPath>().path():id.toString();
}
QString StrawberryPlayer::title() const {return unwrap(metadata().value("xesam:title")).toString();}
QString StrawberryPlayer::artist() const {return unwrap(metadata().value("xesam:artist")).toStringList().join(" / ");}
qint64 StrawberryPlayer::length() const {return qMax<qint64>(0,unwrap(metadata().value("mpris:length")).toLongLong());}
qint64 StrawberryPlayer::position() const {
    qint64 p=m_position;
    if(playing() && m_elapsed.isValid())p+=qint64(m_elapsed.elapsed()*1000.0*m_values.value("Rate",1.0).toDouble());
    return qBound<qint64>(0,p,length()>0?length():qMax<qint64>(0,p));
}
bool StrawberryPlayer::capability(const char *name) const {
    return m_connected && m_values.value("CanControl",false).toBool() && m_values.value(name,false).toBool();
}
void StrawberryPlayer::seeked(qlonglong p){++m_revision;m_position=qMax<qint64>(0,p);m_elapsed.restart();emit changed();}
void StrawberryPlayer::setVisible(bool visible){m_visible=visible;updateTimer();if(visible)refresh();}
void StrawberryPlayer::updateTimer(){if(m_visible && m_connected && playing()){if(!m_progress.isActive())m_progress.start();}else m_progress.stop();}
void StrawberryPlayer::requestPosition(){
    if(m_owner.isEmpty() || !m_visible || m_positionPending)return;
    m_positionPending=true;const int generation=m_generation,revision=m_revision;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,properties,"Get");msg<<player<<QString("Position");
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation,revision]{
        QDBusPendingReply<QDBusVariant> r=*w;w->deleteLater();if(generation!=m_generation)return;m_positionPending=false;
        if(!r.isError() && revision==m_revision){m_position=r.value().variant().toLongLong();m_elapsed.restart();emit changed();}
    });
}
void StrawberryPlayer::invoke(const QString &method,const QList<QVariant> &args,bool root){
    if(m_owner.isEmpty())return;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,root?QString("org.mpris.MediaPlayer2"):player,method);
    msg.setArguments(args);const int generation=m_generation;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation]{
        QDBusPendingReply<> r=*w;w->deleteLater();if(generation!=m_generation)return;
        m_error=r.isError()?QStringLiteral("播放器暂未响应，请重试"):QString();emit changed();if(!r.isError())refresh();
    });
}
void StrawberryPlayer::playPause(){if(capability(playing()?"CanPause":"CanPlay"))invoke("PlayPause");}
void StrawberryPlayer::previous(){if(capability("CanGoPrevious"))invoke("Previous");}
void StrawberryPlayer::next(){if(capability("CanGoNext"))invoke("Next");}
void StrawberryPlayer::seek(qint64 p){
    if(!capability("CanSeek") || length()<=0)return;
    const QVariant track=unwrap(metadata().value("mpris:trackid"));
    const QString id=track.userType()==qMetaTypeId<QDBusObjectPath>()?track.value<QDBusObjectPath>().path():track.toString();
    if(!id.startsWith('/') || id.endsWith("/NoTrack"))return;
    invoke("SetPosition",{QVariant::fromValue(QDBusObjectPath(id)),QVariant::fromValue(qBound<qint64>(0,p,length()))});
}
void StrawberryPlayer::setVolume(double v){
    if(!capability("CanControl"))return;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,properties,"Set");
    msg<<player<<QString("Volume")<<QVariant::fromValue(QDBusVariant(qBound(0.0,v,1.0)));
    const int generation=m_generation;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation]{QDBusPendingReply<> r=*w;w->deleteLater();if(generation!=m_generation)return;if(r.isError()){m_error="音量调整失败";emit changed();}else refresh();});
}
void StrawberryPlayer::openPlayer(){
    if(!m_owner.isEmpty()){invoke("Raise",{},true);return;}
    // Prefer the user's portable installation, then a system installation.
    const QString portable=QDir::homePath()+"/应用/strawberry-1.2.26/Strawberry";
    const QString executable=QFileInfo(portable).isExecutable()?portable:QStandardPaths::findExecutable("strawberry");
    if(executable.isEmpty() || !QProcess::startDetached(executable,{})){m_error="未找到 Strawberry，请先安装或手动启动";emit changed();}
}
void StrawberryPlayer::updateCover(){
    const QString url=unwrap(metadata().value("mpris:artUrl")).toString();if(url==m_artUrl)return;
    m_artUrl=url;m_cover=QImage();
    if(m_coverReply){m_coverReply->abort();m_coverReply=nullptr;}
    const QUrl source(url);
    if(source.isLocalFile()){
        QFile f(source.toLocalFile());if(f.size()<=maxCoverBytes && f.open(QIODevice::ReadOnly))decodeCover(f.read(maxCoverBytes));
    }else if(source.scheme()=="https" || source.scheme()=="http"){
        QNetworkRequest request(source);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply=m_network->get(request);m_coverReply=reply;reply->setReadBufferSize(maxCoverBytes+1);
        connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 n,qint64 total){if(n>maxCoverBytes || total>maxCoverBytes)reply->abort();});
        connect(reply,&QIODevice::readyRead,this,[reply]{if(reply->bytesAvailable()>maxCoverBytes)reply->abort();});
        QTimer::singleShot(8000,reply,[reply]{if(!reply->isFinished())reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,url]{
            if(m_artUrl==url && reply->error()==QNetworkReply::NoError)decodeCover(reply->read(maxCoverBytes));
            if(m_coverReply==reply)m_coverReply=nullptr;reply->deleteLater();emit changed();
        });
    }
}
void StrawberryPlayer::decodeCover(const QByteArray &bytes){
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);reader.setAutoTransform(true);
    const QSize size=reader.size();if(!size.isValid() || size.width()>10000 || size.height()>10000 || qint64(size.width())*size.height()>16000000)return;
    reader.setScaledSize(size.scaled(256,256,Qt::KeepAspectRatio));m_cover=reader.read();
}
