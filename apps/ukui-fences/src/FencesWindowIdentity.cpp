#include "FencesWindowIdentity.h"
#include <QGuiApplication>
#include <QWidget>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef None
#undef Status

void applyFencesWindowIdentity(QWidget *widget,const QByteArray &identity)
{
    if(QGuiApplication::platformName()!="xcb")return;
    Display *display=XOpenDisplay(nullptr);
    if(!display)return;
    const Window window=static_cast<Window>(widget->winId());
    QByteArray name=identity;
    XClassHint hint{name.data(),name.data()};
    XSetClassHint(display,window,&hint);
    XChangeProperty(display,window,XInternAtom(display,"_KDE_NET_WM_DESKTOP_FILE",False),
        XInternAtom(display,"UTF8_STRING",False),8,PropModeReplace,
        reinterpret_cast<const unsigned char *>(name.constData()),name.size());
    XSync(display,False);XCloseDisplay(display);
}
