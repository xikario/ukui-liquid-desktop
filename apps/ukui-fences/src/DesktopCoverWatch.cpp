#include "DesktopCoverWatch.h"
#include <QRegion>
#include <QSocketNotifier>
#include <QSet>
#include <QTimer>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

namespace DesktopCover {
bool covered(const QRect &desktop, const QVector<Client> &above) {
    if(desktop.isEmpty())return false;
    QRegion free(desktop);
    for(const auto &c:above) {
        if(!c.onDesktop || c.hidden || c.translucent)continue;
        if(c.dock || (c.normal && (c.maximized || c.fullscreen)))free-=c.frame;
    }
    qint64 left=0;
    for(const QRect &r:free)left+=qint64(r.width())*r.height();
    return left*100 < qint64(desktop.width())*desktop.height()*3;
}
QRect visibleFrame(const QRect &client, const QVector<unsigned long> &wm, const QVector<unsigned long> &shadow) {
    QRect r=client;
    if(wm.size()==4)r.adjust(-int(wm[0]),-int(wm[2]),int(wm[1]),int(wm[3]));
    if(shadow.size()==4)r.adjust(int(shadow[0]),int(shadow[2]),-int(shadow[1]),-int(shadow[3]));
    return r.isValid()?r:QRect();
}
}

namespace {
QVector<unsigned long> values(Display *display, Window window, Atom property) {
    Atom type;int format;unsigned long count,remaining;unsigned char *data=nullptr;
    QVector<unsigned long> result;
    if(XGetWindowProperty(display,window,property,0,1024,False,AnyPropertyType,
        &type,&format,&count,&remaining,&data)==Success && data && format==32) {
        // Format-32 items arrive as sign-extended longs; 0xffffffff (all desktops)
        // would otherwise never compare equal.
        const auto *items=reinterpret_cast<unsigned long *>(data);
        for(unsigned long i=0;i<count;++i)result.append(items[i]&0xffffffffUL);
    }
    if(data)XFree(data);return result;
}
int ignoreErrors(Display *, XErrorEvent *){return 0;}
}

struct DesktopCoverWatch::Private {
    Display *display=nullptr;
    Window window=0, root=0;
    std::function<void(bool)> changed;
    QTimer timer;
    QSet<Window> watched;
    QSet<Atom> relevant;
    bool covered=false;
    Atom atom(const char *name){return XInternAtom(display,name,False);}
    void check() {
        const auto stacking=values(display,root,atom("_NET_CLIENT_LIST_STACKING"));
        const int own=stacking.indexOf(window);
        bool now=false;
        if(own>=0 && values(display,root,atom("_NET_SHOWING_DESKTOP")).value(0)!=1) {
            // Client queries race with unmapping windows; never let one abort us.
            auto old=XSetErrorHandler(ignoreErrors);
            XWindowAttributes self{};XGetWindowAttributes(display,window,&self);
            int sx=0,sy=0;Window child;XTranslateCoordinates(display,window,root,0,0,&sx,&sy,&child);
            const auto desktop=values(display,root,atom("_NET_CURRENT_DESKTOP")).value(0,0);
            QVector<DesktopCover::Client> above;QSet<Window> live;
            for(int i=own+1;i<stacking.size();++i) {
                const Window w=stacking[i];live.insert(w);
                if(!watched.contains(w)){XSelectInput(display,w,PropertyChangeMask|StructureNotifyMask);watched.insert(w);}
                XWindowAttributes a{};
                if(!XGetWindowAttributes(display,w,&a) || a.map_state!=IsViewable)continue;
                DesktopCover::Client c;
                const auto types=values(display,w,atom("_NET_WM_WINDOW_TYPE"));
                c.dock=types.contains(atom("_NET_WM_WINDOW_TYPE_DOCK"));
                c.normal=types.isEmpty() || types.contains(atom("_NET_WM_WINDOW_TYPE_NORMAL"));
                const auto state=values(display,w,atom("_NET_WM_STATE"));
                c.hidden=state.contains(atom("_NET_WM_STATE_HIDDEN"));
                c.maximized=state.contains(atom("_NET_WM_STATE_MAXIMIZED_VERT")) && state.contains(atom("_NET_WM_STATE_MAXIMIZED_HORZ"));
                c.fullscreen=state.contains(atom("_NET_WM_STATE_FULLSCREEN"));
                const auto opacity=values(display,w,atom("_NET_WM_WINDOW_OPACITY"));
                c.translucent=!opacity.isEmpty() && opacity[0]<0xffffffffUL;
                const auto onDesk=values(display,w,atom("_NET_WM_DESKTOP"));
                c.onDesktop=onDesk.isEmpty() || onDesk[0]==desktop || onDesk[0]==0xffffffffUL;
                int x=0,y=0;XTranslateCoordinates(display,w,root,0,0,&x,&y,&child);
                c.frame=DesktopCover::visibleFrame(QRect(x,y,a.width,a.height),
                    values(display,w,atom("_NET_FRAME_EXTENTS")),values(display,w,atom("_GTK_FRAME_EXTENTS")));
                above.append(c);
            }
            XSync(display,False);XSetErrorHandler(old);
            watched.intersect(live);
            now=DesktopCover::covered(QRect(sx,sy,self.width,self.height),above);
        }
        if(now!=covered){covered=now;if(changed)changed(now);}
        // XSync above may have pulled events into Xlib's queue without the socket
        // becoming readable again; handle them now so none are lost.
        drain();
    }
    void drain() {
        bool hit=false;
        while(XPending(display)) {
            XEvent event;XNextEvent(display,&event);
            // Titles and _NET_WM_USER_TIME change on every keystroke; ignore them.
            hit|=event.type==PropertyNotify ? relevant.contains(event.xproperty.atom)
                : event.type==ConfigureNotify || event.type==MapNotify || event.type==UnmapNotify;
        }
        if(hit && !timer.isActive())timer.start();
    }
};

DesktopCoverWatch::DesktopCoverWatch(unsigned long window, std::function<void(bool)> changed, QObject *parent)
    :QObject(parent),d(new Private) {
    d->window=window;d->changed=std::move(changed);
    d->display=XOpenDisplay(nullptr);
    if(!d->display)return;
    d->root=DefaultRootWindow(d->display);
    XSelectInput(d->display,d->root,PropertyChangeMask);XFlush(d->display);
    for(const char *name:{"_NET_CLIENT_LIST_STACKING","_NET_CURRENT_DESKTOP","_NET_SHOWING_DESKTOP",
        "_NET_WM_STATE","_NET_WM_DESKTOP","_NET_WM_WINDOW_OPACITY","_NET_FRAME_EXTENTS","_GTK_FRAME_EXTENTS"})
        d->relevant.insert(d->atom(name));
    // Bursts of state changes (maximize animations, focus) resolve in one query.
    d->timer.setSingleShot(true);d->timer.setInterval(250);d->timer.setTimerType(Qt::CoarseTimer);
    connect(&d->timer,&QTimer::timeout,this,[this]{d->check();});
    auto *notifier=new QSocketNotifier(ConnectionNumber(d->display),QSocketNotifier::Read,this);
    connect(notifier,&QSocketNotifier::activated,this,[this](int){d->drain();});
    d->timer.start(0);
}
DesktopCoverWatch::~DesktopCoverWatch() {
    if(d->display)XCloseDisplay(d->display);
}
