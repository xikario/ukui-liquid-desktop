#include "StartupWallpaperCover.h"
#include "DesktopLayerWatch.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

StartupWallpaperCover::StartupWallpaperCover(const QImage &poster):QWidget(nullptr) {
    setWindowFlags(Qt::Window|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_X11NetWmWindowTypeDesktop);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setObjectName("fencesStartupCover");
    if(poster.isNull() || !QApplication::primaryScreen())return;
    const auto screen=QApplication::primaryScreen();
    setGeometry(screen->geometry().adjusted(0,0,1,1));
    const qreal dpr=screen->devicePixelRatio();
    m_poster=QPixmap::fromImage(poster).scaled(size()*dpr,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    m_poster.setDevicePixelRatio(dpr);
}
bool StartupWallpaperCover::showPrepared() {
    if(m_poster.isNull())return false;
    show();
    DesktopLayerWatch::requestPlacement(winId());
    QElapsedTimer deadline;deadline.start();
    while((!m_painted || !windowHandle()->isExposed()) && deadline.elapsed()<150)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,10);
    if(!m_painted || !windowHandle()->isExposed()){hide();return false;}
    DesktopLayerWatch::requestPlacement(winId());
    return true;
}
void StartupWallpaperCover::watchCanvas(QWidget *canvas) {
    m_canvas=canvas;canvas->installEventFilter(this);
    QTimer::singleShot(15000,this,&QObject::deleteLater);
}
void StartupWallpaperCover::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    const QSizeF logical=m_poster.size()/m_poster.devicePixelRatioF();
    painter.drawPixmap(QPointF((width()-logical.width())/2,(height()-logical.height())/2),m_poster);
    m_painted=true;
}
bool StartupWallpaperCover::eventFilter(QObject *object,QEvent *event) {
    if(object==m_canvas && !m_retiring && event->type()==QEvent::Paint && m_canvas->isVisible()) {
        m_retiring=true;QTimer::singleShot(34,this,&QObject::deleteLater);
    }
    return false;
}
