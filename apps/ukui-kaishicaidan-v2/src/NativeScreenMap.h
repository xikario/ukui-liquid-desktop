#pragma once
#include <QGuiApplication>
#include <QScreen>
#include <QLibrary>
#include <QRect>
#include <X11/Xlib.h>

namespace NativeScreenMap {
struct Screen { QRect native; QRect logical; qreal dpr=1; };
inline QRect toLogical(const QRect &rect, const Screen &screen)
{
    const QPoint offset=rect.topLeft()-screen.native.topLeft();
    return QRect(screen.logical.topLeft()+QPoint(qRound(offset.x()/screen.dpr),qRound(offset.y()/screen.dpr)),
        QSize(qMax(1,qRound(rect.width()/screen.dpr)),qMax(1,qRound(rect.height()/screen.dpr))));
}
inline QList<Screen> screens(Display *display)
{
    // Optional RandR 1.5 ABI (XRRMonitorInfo). Loading dynamically keeps older
    // OEM SDKs usable without the RandR development headers/linker symlink.
    struct Monitor { Atom name; Bool primary, automatic; int noutput; int x,y,width,height,mwidth,mheight; XID *outputs; };
    using Get=Monitor *(*)(Display *,Window,Bool,int *);
    using Free=void (*)(Monitor *);
    static QLibrary library(QStringLiteral("libXrandr.so.2"));
    static const auto get=reinterpret_cast<Get>(library.resolve("XRRGetMonitors"));
    static const auto freeMonitors=reinterpret_cast<Free>(library.resolve("XRRFreeMonitors"));
    using Version=Status (*)(Display *,int *,int *);
    static const auto version=reinterpret_cast<Version>(library.resolve("XRRQueryVersion"));
    int major=1,minor=5;
    const bool supported=version && version(display,&major,&minor) && (major>1 || (major==1 && minor>=5));
    QList<Screen> result;
    int count=0;
    Monitor *monitors=supported && get && freeMonitors ? get(display,DefaultRootWindow(display),True,&count) : nullptr;
    for (auto *screen : QGuiApplication::screens()) {
        QRect native;
        for (int i=0; i<count; ++i) {
            char *name=XGetAtomName(display,monitors[i].name);
            const bool match=name && screen->name()==QString::fromUtf8(name);
            if (name) XFree(name);
            if (match) { native=QRect(monitors[i].x,monitors[i].y,monitors[i].width,monitors[i].height); break; }
        }
        if (native.isEmpty() && QGuiApplication::screens().size()==1)
            native=QRect(0,0,DisplayWidth(display,DefaultScreen(display)),DisplayHeight(display,DefaultScreen(display)));
        // Unknown multi-screen origins must not be guessed by dividing root coordinates.
        if (!native.isEmpty()) result.append({native,screen->geometry(),screen->devicePixelRatio()});
    }
    if (monitors) freeMonitors(monitors);
    return result;
}
inline Screen best(const QRect &rect, const QList<Screen> &screens)
{
    Screen selected; qint64 bestArea=0;
    for (const auto &screen : screens) {
        const QRect intersection=rect.intersected(screen.native);
        const qint64 area=qint64(intersection.width())*intersection.height();
        if (!intersection.isEmpty() && area>bestArea) { bestArea=area; selected=screen; }
    }
    return selected;
}
}
