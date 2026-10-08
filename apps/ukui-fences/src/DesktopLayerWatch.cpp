#include "DesktopLayerWatch.h"
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QTimer>
#include <QVector>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

namespace {
QVector<unsigned long> values(Display *display, Window window, Atom property) {
    Atom type;int format;unsigned long count,remaining;unsigned char *data=nullptr;
    QVector<unsigned long> result;
    if(XGetWindowProperty(display,window,property,0,65536,False,AnyPropertyType,
        &type,&format,&count,&remaining,&data)==Success && data && format==32) {
        const auto *items=reinterpret_cast<unsigned long *>(data);
        for(unsigned long i=0;i<count;++i)result.append(items[i]);
    }
    if(data)XFree(data);return result;
}
void restack(Display *display, Window window, Window peer) {
    XEvent event{};
    event.xclient.type=ClientMessage;event.xclient.window=window;
    event.xclient.message_type=XInternAtom(display,"_NET_RESTACK_WINDOW",False);
    event.xclient.format=32;event.xclient.data.l[0]=2;
    event.xclient.data.l[1]=peer;event.xclient.data.l[2]=Above;
    XSendEvent(display,DefaultRootWindow(display),False,
        SubstructureRedirectMask|SubstructureNotifyMask,&event);
    XFlush(display);
}
}
struct DesktopLayerWatch::Private {
    Display *display=nullptr;
    Window window=0, root=0;
    Atom clients=0,type=0,desktop=0;
    std::function<bool()> visible;
    QTimer timer;
    QSocketNotifier *notifier=nullptr;
    QElapsedTimer lastRequest;
    int conflicts=0;
    Private(unsigned long id,std::function<bool()> fn):window(id),visible(std::move(fn)) {
        display=XOpenDisplay(nullptr);
        if(!display)return;
        root=DefaultRootWindow(display);
        clients=XInternAtom(display,"_NET_CLIENT_LIST_STACKING",False);
        type=XInternAtom(display,"_NET_WM_WINDOW_TYPE",False);
        desktop=XInternAtom(display,"_NET_WM_WINDOW_TYPE_DESKTOP",False);
        XSelectInput(display,root,PropertyChangeMask);XFlush(display);
        timer.setSingleShot(true);
    }
    ~Private() {
        if(notifier)notifier->setEnabled(false);
        if(display)XCloseDisplay(display);
    }
    void check() {
        if(!visible()){conflicts=0;return;}
        const auto list=values(display,root,clients);const int own=list.indexOf(window);
        if(own<0)return;
        Window peer=0;
        for(int i=own+1;i<list.size();++i)
            if(values(display,list[i],type).contains(desktop)
                && values(display,list[i],XInternAtom(display,"_UKUI_FENCES_VIDEO_TRANSITION",False)).isEmpty())peer=list[i];
        if(!peer){conflicts=0;return;}
        const int cooldown=qMin(8000,500*(1<<qMin(conflicts,4)));
        if(lastRequest.isValid() && lastRequest.elapsed()<cooldown) {
            timer.start(cooldown-lastRequest.elapsed());return;
        }
        restack(display,window,peer);
        lastRequest.start();++conflicts;
        timer.start(100);
    }
};
DesktopLayerWatch::DesktopLayerWatch(unsigned long window,std::function<bool()> visible,QObject *parent)
    :QObject(parent),d(new Private(window,std::move(visible))) {
    if(!d->display)return;
    connect(&d->timer,&QTimer::timeout,this,[this]{d->check();});
    d->notifier=new QSocketNotifier(ConnectionNumber(d->display),QSocketNotifier::Read,this);
    connect(d->notifier,&QSocketNotifier::activated,this,[this](int){
        bool changed=false;
        while(XPending(d->display)){
            XEvent event;XNextEvent(d->display,&event);
            changed|=event.type==PropertyNotify && event.xproperty.atom==d->clients;
        }
        if(changed && !d->timer.isActive())d->timer.start(100);
    });
    d->timer.start(0);
}
DesktopLayerWatch::~DesktopLayerWatch()=default;
void DesktopLayerWatch::requestPlacement(unsigned long window) {
    Display *display=XOpenDisplay(nullptr);if(!display)return;
    const auto clients=values(display,DefaultRootWindow(display),
        XInternAtom(display,"_NET_CLIENT_LIST_STACKING",False));
    const Atom type=XInternAtom(display,"_NET_WM_WINDOW_TYPE",False);
    const Atom desktop=XInternAtom(display,"_NET_WM_WINDOW_TYPE_DESKTOP",False);
    for(const auto peer:clients)
        if(peer!=window && values(display,peer,type).contains(desktop)
            && values(display,peer,XInternAtom(display,"_UKUI_FENCES_VIDEO_TRANSITION",False)).isEmpty())restack(display,window,peer);
    XCloseDisplay(display);
}
