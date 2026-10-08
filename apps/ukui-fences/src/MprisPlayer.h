#pragma once
#include <QObject>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QTimer>
#include <QImage>
#include <QPointer>
#include <QDBusContext>
#include <QMap>
#include "Lyrics.h"
class QNetworkAccessManager;
class QNetworkReply;

struct MusicClientProfile {
    QString name, service, program;
    QStringList arguments;
    bool enabled = true;
    QString desktopFile, workingDirectory;
};

// Configured MPRIS clients, with one active playback connection at a time.
class MprisPlayer : public QObject, protected QDBusContext {
    Q_OBJECT
public:
    explicit MprisPlayer(QObject *parent=nullptr, bool playback=true);
    static QList<MusicClientProfile> loadProfiles();
    static bool saveProfiles(const QList<MusicClientProfile> &, QString *error=nullptr);
    static bool validateProfiles(const QList<MusicClientProfile> &, QString *error=nullptr);
    static QString suggestedName(const QString &service);
    void reloadConfiguration();
    QStringList availableServices() const { return m_instances.keys(); }
    MusicClientProfile detectedProfile(const QString &service) const;
    QString activeService() const { return m_service; }
    QString clientName() const;
    QString connectionId() const { return m_service + m_owner; }
    bool canRaise() const { return m_rootValues.value("CanRaise",false).toBool(); }
    bool canOpen() const;
    bool discovering() const { return m_initialDiscovery; }
    bool connected() const { return m_connected; }
    bool playing() const { return m_values.value("PlaybackStatus").toString()=="Playing"; }
    bool capability(const char *name) const;
    QString title() const;
    QString trackId() const;
    QString artist() const;
    QString error() const { return m_error; }
    QImage cover() const { return m_cover; }
    qint64 length() const;
    qint64 position() const;
    double volume() const { return m_values.value("Volume",1.0).toDouble(); }
    // Bumped once per distinct track/metadata; derived data keys off it.
    int metadataRevision() const { return m_metadataRevision; }
    const QVector<Lyrics::Line> &lyrics() const { return m_lyrics; }
    QString currentLyric() const;
    bool progressActive() const { return m_progress.isActive(); }
    void setVisible(bool visible);
    void playPause();
    void previous();
    void next();
    void seek(qint64 microseconds);
    void setVolume(double volume);
    void openPlayer();
signals:
    void changed();
    void clientsChanged();
    // Debounced 120 ms after the metadata revision changes.
    void metadataChanged();
private slots:
    void propertiesChanged(const QString &, const QVariantMap &, const QStringList &);
    void seeked(qlonglong position);
    void ownerChanged(const QString &, const QString &, const QString &);
private:
    void discover();
    void observe(const QString &name, const QString &owner, bool live);
    void selectActive();
    void setActive(const QString &name, const QString &owner);
    void finishDiscovery();
    int profileIndex(const QString &name) const;
    void refreshRoot();
    void refresh();
    void updateTimer();
    void invoke(const QString &method, const QList<QVariant> &args={}, bool root=false);
    void requestPosition();
    void updateCover();
    void decodeCover(const QByteArray &bytes);
    void probeStatus(const QString &name, const QString &owner);
    void noteMetadata();
    int statusRank(const QString &name, const QString &status) const;
    QVariantMap metadata() const;
    struct Instance { QString owner; quint64 started=0, order=0; bool ready=false; uint pid=0; MusicClientProfile launch; QString status; };
    QMap<QString,Instance> m_instances;
    QMap<QString,quint64> m_nameVersions;
    QList<MusicClientProfile> m_profiles;
    QString m_service, m_owner, m_error, m_artUrl, m_lastProfile;
    QVariantMap m_values, m_rootValues;
    QImage m_cover;
    QElapsedTimer m_elapsed;
    QTimer m_progress, m_metadataTimer;
    QString m_metadataKey;
    QVector<Lyrics::Line> m_lyrics;
    int m_metadataRevision=0;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_coverReply;
    qint64 m_position=0;
    int m_generation=0, m_revision=0, m_ticks=0;
    int m_ownerRequests=0, m_probes=0;
    quint64 m_order=0;
    bool m_initialDiscovery=true, m_listPending=true, m_playback=true;
    bool m_connected=false, m_visible=false, m_refreshing=false, m_refreshAgain=false, m_positionPending=false;
};
