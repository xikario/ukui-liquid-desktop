#pragma once
#include <QObject>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QTimer>
#include <QImage>
#include <QPointer>
class QNetworkAccessManager;
class QNetworkReply;

// MPRIS client: asynchronous D-Bus only; the player continues independently.
class StrawberryPlayer : public QObject {
    Q_OBJECT
public:
    explicit StrawberryPlayer(QObject *parent=nullptr);
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
private slots:
    void propertiesChanged(const QString &, const QVariantMap &, const QStringList &);
    void seeked(qlonglong position);
    void ownerChanged(const QString &, const QString &, const QString &);
private:
    void discover();
    void refresh();
    void updateTimer();
    void invoke(const QString &method, const QList<QVariant> &args={}, bool root=false);
    void requestPosition();
    void updateCover();
    void decodeCover(const QByteArray &bytes);
    QVariantMap metadata() const;
    QString m_owner, m_error, m_artUrl;
    QVariantMap m_values;
    QImage m_cover;
    QElapsedTimer m_elapsed;
    QTimer m_progress;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_coverReply;
    qint64 m_position=0;
    int m_generation=0, m_revision=0, m_ticks=0;
    bool m_connected=false, m_visible=false, m_refreshing=false, m_refreshAgain=false, m_positionPending=false;
};
