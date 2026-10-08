#pragma once
#include <QObject>
#include <QImage>
#include <QProcess>
#include <QRegion>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QTimer>
#include <functional>
class QWidget;
// X11 player lifecycle. DesktopCanvas owns saved wallpaper selection.
class VideoWallpaperTrial final : public QObject {
public:
    VideoWallpaperTrial(QWidget *,std::function<void(const QImage &)>,std::function<void()>,std::function<QRegion()>);
    ~VideoWallpaperTrial() override;
    bool start(const QString &);
    void stop();
    void refreshGeometry();
    bool active() const {return enabled;}
    QString status() const;
    std::function<void()> changed;
protected:bool eventFilter(QObject *,QEvent *) override;
private:
    void scheduleGeometry();void sendGeometry();void receive();void watchWidget(QWidget *);
    QWidget *canvas;
    std::function<void(const QImage &)> poster;
    std::function<void()> restore;
    std::function<QRegion()> overlay;
    QProcess process;QTimer geometryTimer;QByteArray output,lastGeometry,errorOutput;
    QJsonObject playback;
    QHash<QObject *,QRegion> lastMasks;QRegion lastOverlay;
    QSet<QObject *> watchedWidgets;
    QString state="stopped",reason,source;
    quint64 revision=0;int loops=0;bool enabled=false;
};
