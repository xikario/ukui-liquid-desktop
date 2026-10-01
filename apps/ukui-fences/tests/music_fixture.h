#pragma once
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QVariantMap>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusMessage>
class MusicFixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(bool CanControl READ control)
    Q_PROPERTY(bool CanGoNext READ control)
    Q_PROPERTY(bool CanGoPrevious READ control)
    Q_PROPERTY(bool CanPlay READ control)
    Q_PROPERTY(bool CanPause READ control)
    Q_PROPERTY(bool CanSeek READ control)
public:
    QString state="Paused",art,track="/track/one",song="测试歌曲",connectionName;
    qlonglong pos=4000000;
    double vol=.65;
    bool enabled=true;
    int nextCount=0,previousCount=0,playCount=0,seekCount=0,raiseCount=0;
    QString seekTrack;
    QString status() const{return state;}
    QVariantMap metadata() const{return {{"xesam:title",song},{"xesam:artist",QStringList{"测试歌手"}},
        {"mpris:length",QVariant::fromValue<qlonglong>(180000000)},
        {"mpris:trackid",QVariant::fromValue(QDBusObjectPath(track))},{"mpris:artUrl",art}};}
    qlonglong position() const{return pos;}
    double volume() const{return vol;}
    double rate() const{return 1.0;}
    bool control() const{return enabled;}
    QDBusConnection bus() const{return connectionName.isEmpty()?QDBusConnection::sessionBus():QDBusConnection(connectionName);}
    void notify(const QVariantMap &values){auto m=QDBusMessage::createSignal("/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","PropertiesChanged");m<<QString("org.mpris.MediaPlayer2.Player")<<values<<QStringList{};bus().send(m);}
    void setVolume(double v){vol=v;notify({{"Volume",v}});}
public slots:
    void PlayPause(){++playCount;state=state=="Playing"?"Paused":"Playing";notify({{"PlaybackStatus",state}});}
    void Next(){++nextCount;}
    void Previous(){++previousCount;}
    void SetPosition(const QDBusObjectPath &track,qlonglong position){++seekCount;seekTrack=track.path();pos=position;auto m=QDBusMessage::createSignal("/org/mpris/MediaPlayer2","org.mpris.MediaPlayer2.Player","Seeked");m<<position;bus().send(m);}
};
class MusicRootFixture : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(QString Identity READ identity)
public:
    explicit MusicRootFixture(MusicFixture *fixture):QDBusAbstractAdaptor(fixture),m_fixture(fixture){}
    bool raises=true;
    bool canRaise() const{return raises;}
    QString identity() const{return "Fixture player";}
public slots:
    void Raise(){++m_fixture->raiseCount;}
private:
    MusicFixture *m_fixture;
};
