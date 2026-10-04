#pragma once
#include <QWidget>
#include <QImage>
#include <QProcess>
#include <QTimer>
#include <QVector>
class QLabel;

// Bounded on-demand thumbnails. Never starts the wallpaper or a playback loop.
class VideoWallpaperPreview final : public QWidget {
public:
    explicit VideoWallpaperPreview(QWidget *parent = nullptr);
    ~VideoWallpaperPreview() override;
    void setFile(const QString &path);
    void confirmFile(const QString &path);
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    void begin();
    bool loadCache();
    void saveCache();
    void showFrame();
    void extract();
    void completed(int code, QProcess::ExitStatus status);
    void fail(const QString &message);
    QString m_path, m_identity;
    QProcess m_process;
    QTimer m_debounce, m_deadline, m_animation;
    QVector<QImage> m_frames;
    QVector<double> m_positions;
    QLabel *m_animatedImage, *m_animatedTime;
    QString m_cacheDirectory;
    bool m_confirmed = false;
    int m_displayFrame = 0;
    QVector<QLabel *> m_images, m_times;
    QLabel *m_hint;
    double m_duration = 0;
    int m_frame = 0;
    bool m_probing = false, m_cancelled = false, m_timedOut = false;
};
