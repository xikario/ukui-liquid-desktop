#pragma once
#include "VideoWallpaperCache.h"
#include <QApplication>
#include <QLabel>
#include <QScreen>
#include <QSettings>
#include <QTimer>

// A process-owned desktop surface: never unmap/stop Peony. On exit or crash
// the X server removes this surface, exposing the untouched system desktop.
class StartupWallpaperCover final : public QLabel {
public:
    StartupWallpaperCover() : QLabel(nullptr) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_X11NetWmWindowTypeDesktop);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setObjectName("fencesStartupCover");
        setStyleSheet("background:#101827;");
        setAlignment(Qt::AlignCenter);
        const auto screen=QApplication::primaryScreen();
        if(!screen)return;
        setGeometry(screen->geometry().adjusted(0,0,1,1));
        const QImage poster=VideoWallpaperCache::poster(QSettings().value("wallpaper/videoPath").toString());
        if(!poster.isNull()){
            const qreal dpr=screen->devicePixelRatio();
            QPixmap pix=QPixmap::fromImage(poster).scaled(size()*dpr,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
            pix.setDevicePixelRatio(dpr);setPixmap(pix);
        }
    }
};
