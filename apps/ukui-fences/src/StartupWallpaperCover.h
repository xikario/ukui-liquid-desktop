#pragma once
#include <QWidget>
#include <QPixmap>

// Created only with a usable cached video poster. Released after the canvas paint.
class StartupWallpaperCover final : public QWidget {
public:
    explicit StartupWallpaperCover(const QImage &poster);
    bool showPrepared();
    void watchCanvas(QWidget *);
protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *,QEvent *) override;
private:
    QPixmap m_poster;
    bool m_painted=false,m_retiring=false;
    QWidget *m_canvas=nullptr;
};
