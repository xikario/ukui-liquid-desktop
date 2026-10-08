#include "MprisPlayer.h"
#include "MusicClientIntegration.h"
#include <QGuiApplication>
#include <QDBusConnection>
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
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include "../../../shared/async-work/BackgroundTask.h"
#include <algorithm>
namespace {
const QString prefix="org.mpris.MediaPlayer2.", path="/org/mpris/MediaPlayer2";
const QString player="org.mpris.MediaPlayer2.Player", properties="org.freedesktop.DBus.Properties";
const QString rootInterface="org.mpris.MediaPlayer2";
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
QList<MusicClientProfile> MprisPlayer::loadProfiles() {
    QSettings settings;
    if (!settings.contains("desklets/music/clients")) {
        const QString portable=QDir::homePath()+"/应用/strawberry-1.2.26/Strawberry";
        return {{"Strawberry",prefix+"strawberry",QFileInfo(portable).isExecutable()?portable:QString("strawberry"),{},true}};
    }
    const auto doc=QJsonDocument::fromJson(settings.value("desklets/music/clients").toString().toUtf8());
    QList<MusicClientProfile> result;
    if (!doc.isObject() || doc.object().value("version").toInt()!=1) return result;
    for (const auto &value:doc.object().value("clients").toArray()) {
        const auto o=value.toObject(); MusicClientProfile p;
        p.name=o.value("name").toString(); p.service=o.value("service").toString();
        p.program=o.value("program").toString(); p.enabled=o.value("enabled").toBool();
        p.desktopFile=o.value("desktopFile").toString();p.workingDirectory=o.value("workingDirectory").toString();
        for (const auto &arg:o.value("arguments").toArray()) p.arguments << arg.toString();
        result << p;
    }
    return validateProfiles(result)?result:QList<MusicClientProfile>();
}
bool MprisPlayer::validateProfiles(const QList<MusicClientProfile> &profiles, QString *error) {
    static const QRegularExpression busName("^org\\.mpris\\.MediaPlayer2\\.[A-Za-z_][A-Za-z0-9_-]*(?:\\.[A-Za-z_][A-Za-z0-9_-]*)*$");
    QStringList services;
    const auto fail=[&](const QString &message) { if (error) *error=message; return false; };
    if (profiles.size()>32) return fail("最多配置 32 个客户端。");
    for (const auto &p:profiles) {
        if (p.name.trimmed().isEmpty() || p.name.size()>100) return fail("请填写客户端名称（最多 100 字）。");
        if (p.service.size()>255 || !busName.match(p.service).hasMatch()) return fail("MPRIS 名称应形如 org.mpris.MediaPlayer2.yesplaymusic。");
        if (services.contains(p.service)) return fail("同一个 MPRIS 名称不能重复配置。");
        if (p.program.contains(QChar::Null) || p.program.contains('\n') || p.program.size()>4096 || p.arguments.size()>32)
            return fail("启动程序或参数无效；参数每行一项，最多 32 项。");
        for (const auto &arg:p.arguments) if (arg.contains(QChar::Null) || arg.size()>4096) return fail("启动参数过长或含无效字符。");
        for(const auto &path:{p.desktopFile,p.workingDirectory})
            if(path.size()>4096 || path.contains(QChar::Null) || path.contains('\n') || (!path.isEmpty() && !QFileInfo(path).isAbsolute()))return fail("自动识别的启动路径无效，请重新识别。");
        services << p.service;
    }
    return true;
}
bool MprisPlayer::saveProfiles(const QList<MusicClientProfile> &profiles, QString *error) {
    if (!validateProfiles(profiles,error)) return false;
    QJsonArray clients;
    for (const auto &p:profiles) {
        QJsonArray arguments; for (const auto &arg:p.arguments) arguments.append(arg);
        clients.append(QJsonObject{{"name",p.name},{"service",p.service},{"program",p.program},{"arguments",arguments},{"enabled",p.enabled},{"desktopFile",p.desktopFile},{"workingDirectory",p.workingDirectory}});
    }
    QSettings settings;
    settings.setValue("desklets/music/clients",QString::fromUtf8(QJsonDocument(QJsonObject{{"version",1},{"clients",clients}}).toJson(QJsonDocument::Compact)));
    settings.sync();
    if (settings.status()!=QSettings::NoError) { if(error)*error="无法保存客户端配置。"; return false; }
    return true;
}
QString MprisPlayer::suggestedName(const QString &service) {
    const QString name=service.mid(prefix.size()).section('.',0,0);
    if (name.compare("yesplaymusic",Qt::CaseInsensitive)==0) return "YesPlayMusic";
    if (name=="strawberry") return "Strawberry";
    if (name=="vlc") return "VLC";
    return name;
}
MprisPlayer::MprisPlayer(QObject *parent,bool playback):QObject(parent),m_network(new QNetworkAccessManager(this)),m_playback(playback) {
    m_profiles=loadProfiles();
    m_lastProfile=QSettings().value("desklets/music/lastClient").toString();
    auto bus=QDBusConnection::sessionBus();
    bus.connect("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","NameOwnerChanged",this,SLOT(ownerChanged(QString,QString,QString)));
    m_progress.setInterval(1000);
    connect(&m_progress,&QTimer::timeout,this,[this]{emit changed();if(++m_ticks%10==0)requestPosition();});
    // Bursts of PropertiesChanged during a track switch collapse into one
    // lyrics parse and one artwork-palette pass.
    m_metadataTimer.setSingleShot(true);m_metadataTimer.setInterval(120);
    connect(&m_metadataTimer,&QTimer::timeout,this,[this]{
        m_lyrics=Lyrics::parse(unwrap(metadata().value("xesam:asText")).toString());
        emit metadataChanged();
    });
    discover();
}
void MprisPlayer::discover() {
    auto msg=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","ListNames");
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w]{
        QDBusPendingReply<QStringList> reply=*w; w->deleteLater();
        m_listPending=false;
        QStringList names;
        if (!reply.isError()) for (const auto &name:reply.value())
            if (name.startsWith(prefix) && !m_nameVersions.contains(name)) names << name;
        m_ownerRequests=names.size();
        for (const auto &name:names) {
            const quint64 version=m_nameVersions.value(name);
            auto request=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetNameOwner"); request<<name;
            auto *owner=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request,1500),this);
            connect(owner,&QDBusPendingCallWatcher::finished,this,[this,owner,name,version]{
                QDBusPendingReply<QString> r=*owner; owner->deleteLater(); --m_ownerRequests;
                if (!r.isError() && version==m_nameVersions.value(name)) observe(name,r.value(),false);
                finishDiscovery();
            });
        }
        finishDiscovery();
    });
}
void MprisPlayer::ownerChanged(const QString &name,const QString &,const QString &owner) {
    if (!name.startsWith(prefix)) return;
    ++m_nameVersions[name]; observe(name,owner,true);
}
void MprisPlayer::observe(const QString &name,const QString &owner,bool live) {
    if (owner.isEmpty()) { m_instances.remove(name); selectActive(); emit clientsChanged(); return; }
    if (m_instances.contains(name) && m_instances.value(name).owner==owner) return;
    m_instances[name]={owner,0,live?++m_order:0,false};
    // Drop the old connection immediately on owner replacement, even while the
    // new owner's process start time is being checked in the background.
    selectActive(); emit clientsChanged(); ++m_probes;
    auto msg=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionUnixProcessID"); msg<<owner;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,name,owner]{
        QDBusPendingReply<uint> r=*w; w->deleteLater();
        const uint pid=r.isError()?0:r.value();
        const auto done=[this,name,owner,pid](const MusicClientIntegration::ProcessInfo &info) {
            --m_probes;
            if (m_instances.contains(name) && m_instances.value(name).owner==owner) {
                auto &instance=m_instances[name]; instance.started=info.started; instance.pid=pid;instance.launch=info.launch;
                probeStatus(name,owner); return;
            }
            finishDiscovery(); selectActive(); emit clientsChanged();
        };
        if (r.isError()) { done({}); return; }
        if (!BackgroundTask::run(this,[pid,name]{return MusicClientIntegration::inspectProcess(pid,name);},done)) done({});
    });
}
void MprisPlayer::probeStatus(const QString &name,const QString &owner) {
    // One-shot status read so selection can prefer a playing client. Inactive
    // players are not tracked live: their later notifications never steal focus.
    ++m_probes;
    auto msg=QDBusMessage::createMethodCall(owner,path,properties,"Get"); msg<<player<<QStringLiteral("PlaybackStatus");
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,name,owner]{
        QDBusPendingReply<QDBusVariant> r=*w; w->deleteLater(); --m_probes;
        if (m_instances.contains(name) && m_instances.value(name).owner==owner) {
            auto &instance=m_instances[name]; instance.status=r.isError()?QString():r.value().variant().toString(); instance.ready=true;
        }
        finishDiscovery(); selectActive(); emit clientsChanged();
    });
}
int MprisPlayer::statusRank(const QString &,const QString &status) const {
    return status=="Playing" ? 2 : status=="Paused" ? 1 : 0;
}
void MprisPlayer::finishDiscovery() {
    if (!m_listPending && m_ownerRequests==0 && m_probes==0) { m_initialDiscovery=false; selectActive(); emit clientsChanged(); }
}
int MprisPlayer::profileIndex(const QString &name) const {
    int index=-1, length=-1;
    for (int i=0;i<m_profiles.size();++i) {
        const auto &p=m_profiles[i];
        if (p.enabled && (name==p.service || name.startsWith(p.service+'.')) && p.service.size()>length) { index=i; length=p.service.size(); }
    }
    return index;
}
void MprisPlayer::reloadConfiguration() { m_profiles=loadProfiles(); selectActive(); emit changed(); }
QString MprisPlayer::clientName() const {
    const int i=profileIndex(m_service);
    return i<0?QString():m_profiles[i].name;
}
MusicClientProfile MprisPlayer::detectedProfile(const QString &service) const {
    MusicClientProfile result; quint64 started=0,order=0;
    for(auto it=m_instances.cbegin();it!=m_instances.cend();++it) {
        const auto &instance=it.value();
        if(!instance.ready || (it.key()!=service && !it.key().startsWith(service+'.')))continue;
        if(result.service.isEmpty() || instance.started>started || (instance.started==started && instance.order>order)) {
            result=instance.launch;started=instance.started;order=instance.order;
        }
    }
    return result;
}
bool MprisPlayer::canOpen() const {
    return canRaise() || (QGuiApplication::platformName()=="xcb" && m_instances.value(m_service).pid!=0);
}
void MprisPlayer::selectActive() {
    if (!m_playback) return;
    // Initial ListNames order is unspecified: compare process start times only
    // after every initial lookup has completed, rather than callback order.
    if (m_initialDiscovery) {
        if (!m_service.isEmpty() && (!m_instances.contains(m_service) || m_instances.value(m_service).owner!=m_owner)) setActive({},{});
        return;
    }
    QString selected; Instance newest; int bestRank=-1;
    bool unavailableStart=false;
    for (auto it=m_instances.cbegin();it!=m_instances.cend();++it)
        if (it.value().ready && profileIndex(it.key())>=0 && !it.value().started) unavailableStart=true;
    for (auto it=m_instances.cbegin();it!=m_instances.cend();++it) {
        const auto &candidate=it.value();
        if (!candidate.ready || profileIndex(it.key())<0) continue;
        // Playing > paused > anything else; ties keep the newest client.
        const int rank=statusRank(it.key(),candidate.status);
        const bool later=selected.isEmpty() || rank>bestRank || (rank==bestRank && (
               (!unavailableStart && candidate.started>newest.started)
            || ((unavailableStart || candidate.started==newest.started) && candidate.order>newest.order)
            || ((unavailableStart || candidate.started==newest.started) && candidate.order==newest.order && it.key()>selected)));
        if (later) { selected=it.key(); newest=candidate; bestRank=rank; }
    }
    if (selected!=m_service || newest.owner!=m_owner) setActive(selected,newest.owner);
}
void MprisPlayer::setActive(const QString &name,const QString &owner) {
    auto bus=QDBusConnection::sessionBus();
    if (!m_owner.isEmpty()) {
        bus.disconnect(m_owner,path,properties,"PropertiesChanged",this,SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        bus.disconnect(m_owner,path,player,"Seeked",this,SLOT(seeked(qlonglong)));
    }
    ++m_generation;++m_revision; m_service=name; m_owner=owner;
    m_connected=false;m_values.clear();m_rootValues.clear();m_position=0;m_elapsed.invalidate();
    m_refreshing=m_refreshAgain=m_positionPending=false;m_error.clear();
    noteMetadata();updateCover();updateTimer();emit changed();
    if (!owner.isEmpty()) {
        bus.connect(owner,path,properties,"PropertiesChanged",this,SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        bus.connect(owner,path,player,"Seeked",this,SLOT(seeked(qlonglong)));
        const int i=profileIndex(name); m_lastProfile=m_profiles[i].service;
        QSettings().setValue("desklets/music/lastClient",m_lastProfile);
        refreshRoot(); refresh();
    }
}
void MprisPlayer::refreshRoot() {
    const int generation=m_generation;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,properties,"GetAll"); msg<<rootInterface;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation] {
        QDBusPendingReply<QVariantMap> r=*w; w->deleteLater();
        if (generation!=m_generation) return;
        if (!r.isError()) m_rootValues=r.value(); emit changed();
    });
}
void MprisPlayer::refresh() {
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
            if(m_instances.contains(m_service))m_instances[m_service].status=m_values.value("PlaybackStatus").toString();
            noteMetadata();updateCover();updateTimer();emit changed();
        } else if(reply.isError()){m_error="暂时无法读取播放状态";emit changed();}
        if(m_refreshAgain){m_refreshAgain=false;refresh();}
    });
}
void MprisPlayer::propertiesChanged(const QString &iface,const QVariantMap &updates,const QStringList &invalidated) {
    if (!calledFromDBus() || message().service()!=m_owner) return;
    if (iface==rootInterface) {
        for(auto it=updates.begin();it!=updates.end();++it)m_rootValues[it.key()]=unwrap(it.value());
        if (!invalidated.isEmpty()) refreshRoot(); emit changed(); return;
    }
    if(iface!=player || m_owner.isEmpty())return;
    m_position=position();m_elapsed.restart();++m_revision;
    for(auto it=updates.begin();it!=updates.end();++it)m_values[it.key()]=unwrap(it.value());
    for(const auto &name:invalidated)m_values.remove(name);
    if(updates.contains("Metadata")){m_position=0;noteMetadata();updateCover();requestPosition();}
    if(updates.contains("PlaybackStatus") && m_instances.contains(m_service))m_instances[m_service].status=m_values.value("PlaybackStatus").toString();
    if(updates.contains("Position"))m_position=unwrap(updates.value("Position")).toLongLong();
    updateTimer();emit changed();
    if(!invalidated.isEmpty() || m_refreshing || !m_connected)refresh();
}
void MprisPlayer::noteMetadata() {
    // Players resend identical Metadata with every status change; only a
    // different track/text bumps the revision and schedules derived work.
    const QVariantMap m=metadata();
    const QString key=trackId()+'\x1f'+title()+'\x1f'+artist()+'\x1f'+QString::number(length())+'\x1f'
        +unwrap(m.value("mpris:artUrl")).toString()+'\x1f'+unwrap(m.value("xesam:asText")).toString();
    if(key==m_metadataKey)return;
    m_metadataKey=key;++m_metadataRevision;m_metadataTimer.start();
}
QString MprisPlayer::currentLyric() const {
    const int i=Lyrics::lineAt(m_lyrics,position());
    return i>=0?m_lyrics.at(i).text:QString();
}
QVariantMap MprisPlayer::metadata() const {return mapValue(m_values.value("Metadata"));}
QString MprisPlayer::trackId() const {
    const QVariant id=unwrap(metadata().value("mpris:trackid"));
    return id.userType()==qMetaTypeId<QDBusObjectPath>()?id.value<QDBusObjectPath>().path():id.toString();
}
QString MprisPlayer::title() const {return unwrap(metadata().value("xesam:title")).toString();}
QString MprisPlayer::artist() const {return unwrap(metadata().value("xesam:artist")).toStringList().join(" / ");}
qint64 MprisPlayer::length() const {return qMax<qint64>(0,unwrap(metadata().value("mpris:length")).toLongLong());}
qint64 MprisPlayer::position() const {
    qint64 p=m_position;
    if(playing() && m_elapsed.isValid())p+=qint64(m_elapsed.elapsed()*1000.0*m_values.value("Rate",1.0).toDouble());
    return qBound<qint64>(0,p,length()>0?length():qMax<qint64>(0,p));
}
bool MprisPlayer::capability(const char *name) const {
    return m_connected && m_values.value("CanControl",false).toBool() && m_values.value(name,false).toBool();
}
void MprisPlayer::seeked(qlonglong p){if(!calledFromDBus() || message().service()!=m_owner)return;++m_revision;m_position=qMax<qint64>(0,p);m_elapsed.restart();emit changed();}
void MprisPlayer::setVisible(bool visible){m_visible=visible;updateTimer();if(visible)refresh();}
void MprisPlayer::updateTimer(){if(m_visible && m_connected && playing()){if(!m_progress.isActive())m_progress.start();}else m_progress.stop();}
void MprisPlayer::requestPosition(){
    if(m_owner.isEmpty() || !m_visible || m_positionPending)return;
    m_positionPending=true;const int generation=m_generation,revision=m_revision;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,properties,"Get");msg<<player<<QString("Position");
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation,revision]{
        QDBusPendingReply<QDBusVariant> r=*w;w->deleteLater();if(generation!=m_generation)return;m_positionPending=false;
        if(!r.isError() && revision==m_revision){m_position=r.value().variant().toLongLong();m_elapsed.restart();emit changed();}
    });
}
void MprisPlayer::invoke(const QString &method,const QList<QVariant> &args,bool root){
    if(m_owner.isEmpty())return;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,root?QString("org.mpris.MediaPlayer2"):player,method);
    msg.setArguments(args);const int generation=m_generation;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation]{
        QDBusPendingReply<> r=*w;w->deleteLater();if(generation!=m_generation)return;
        m_error=r.isError()?QStringLiteral("播放器暂未响应，请重试"):QString();emit changed();if(!r.isError())refresh();
    });
}
void MprisPlayer::playPause(){if(capability(playing()?"CanPause":"CanPlay"))invoke("PlayPause");}
void MprisPlayer::previous(){if(capability("CanGoPrevious"))invoke("Previous");}
void MprisPlayer::next(){if(capability("CanGoNext"))invoke("Next");}
void MprisPlayer::seek(qint64 p){
    if(!capability("CanSeek") || length()<=0)return;
    const QVariant track=unwrap(metadata().value("mpris:trackid"));
    const QString id=track.userType()==qMetaTypeId<QDBusObjectPath>()?track.value<QDBusObjectPath>().path():track.toString();
    if(!id.startsWith('/') || id.endsWith("/NoTrack"))return;
    invoke("SetPosition",{QVariant::fromValue(QDBusObjectPath(id)),QVariant::fromValue(qBound<qint64>(0,p,length()))});
}
void MprisPlayer::setVolume(double v){
    if(!capability("CanControl"))return;
    auto msg=QDBusMessage::createMethodCall(m_owner,path,properties,"Set");
    msg<<player<<QString("Volume")<<QVariant::fromValue(QDBusVariant(qBound(0.0,v,1.0)));
    const int generation=m_generation;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,generation]{QDBusPendingReply<> r=*w;w->deleteLater();if(generation!=m_generation)return;if(r.isError()){m_error="音量调整失败";emit changed();}else refresh();});
}
void MprisPlayer::openPlayer(){
    if(!m_owner.isEmpty()) {
        const auto instance=m_instances.value(m_service);
        if(MusicClientIntegration::activateProcessWindow(instance.pid,instance.started)){m_error.clear();emit changed();}
        else if (canRaise()) invoke("Raise",{},true);
        else { m_error="客户端不支持前置窗口，请从任务栏打开。"; emit changed(); }
        return;
    }
    int selected=-1;
    for(int i=0;i<m_profiles.size();++i) if(m_profiles[i].enabled && (!m_profiles[i].program.isEmpty() || !m_profiles[i].desktopFile.isEmpty())) {
        if(selected<0)selected=i;
        if(m_profiles[i].service==m_lastProfile){selected=i;break;}
    }
    if(selected<0){m_error="未配置可启动的客户端，请在音乐设置中添加，或手动启动。";emit changed();return;}
    const auto &profile=m_profiles[selected];
    // Program and arguments are passed separately; never run through a shell.
    if(!MusicClientIntegration::launch(profile)){m_error="无法启动 "+profile.name+"，请手动打开一次后在音乐设置中重新识别。";emit changed();}
}
void MprisPlayer::updateCover(){
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
        const int generation=m_generation;
        connect(reply,&QNetworkReply::finished,this,[this,reply,url,generation]{
            if(generation==m_generation && m_coverReply==reply && m_artUrl==url && reply->error()==QNetworkReply::NoError)decodeCover(reply->read(maxCoverBytes));
            if(m_coverReply==reply)m_coverReply=nullptr;reply->deleteLater();emit changed();
        });
    }
}
void MprisPlayer::decodeCover(const QByteArray &bytes){
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);reader.setAutoTransform(true);
    const QSize size=reader.size();if(!size.isValid() || size.width()>10000 || size.height()>10000 || qint64(size.width())*size.height()>16000000)return;
    reader.setScaledSize(size.scaled(256,256,Qt::KeepAspectRatio));m_cover=reader.read();
}
