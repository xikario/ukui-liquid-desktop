#pragma once
#include <QWidget>
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
private:
    void begin();
    void extract();
    void completed(int code, QProcess::ExitStatus status);
    void fail(const QString &message);
    QString m_path, m_identity;
    QProcess m_process;
    QTimer m_debounce, m_deadline;
    QVector<QLabel *> m_images, m_times;
    QLabel *m_hint;
    double m_duration = 0;
    int m_frame = 0;
    bool m_probing = false, m_cancelled = false, m_timedOut = false;
};
