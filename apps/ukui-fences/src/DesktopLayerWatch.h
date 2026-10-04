#pragma once
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <functional>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

// React to WM restacking instead of relying only on login-time retries.
// Peony remains mapped and available immediately when Fences hides or exits.
class DesktopLayerWatch final : public QObject {
public:
    DesktopLayerWatch(unsigned long canvas, std::function<bool()> visible,
                      std::function<void()> restore, QObject *parent=nullptr)
        : QObject(parent), m_window(canvas), m_visible(visible), m_restore(restore) {
        m_display=XOpenDisplay(nullptr);
        if(!m_display)return;
        m_root=DefaultRootWindow(m_display);
        m_clients=XInternAtom(m_display,"_NET_CLIENT_LIST_STACKING",False);
        m_type=XInternAtom(m_display,"_NET_WM_WINDOW_TYPE",False);
        m_desktop=XInternAtom(m_display,"_NET_WM_WINDOW_TYPE_DESKTOP",False);
        XSelectInput(m_display,m_root,PropertyChangeMask|SubstructureNotifyMask);XFlush(m_display);
        m_timer.setSingleShot(true);m_timer.setInterval(100);
        connect(&m_timer,&QTimer::timeout,this,[this]{check();});
        auto *notifier=new QSocketNotifier(ConnectionNumber(m_display),QSocketNotifier::Read,this);
        connect(notifier,&QSocketNotifier::activated,this,[this](int){
            bool changed=false;
            while(XPending(m_display)){
                XEvent event;XNextEvent(m_display,&event);
                changed|=(event.type==PropertyNotify && event.xproperty.atom==m_clients)
                    || event.type==MapNotify;
            }
            if(changed && !m_timer.isActive())m_timer.start();
        });
    }
    ~DesktopLayerWatch() override {
        for(auto *notifier:findChildren<QSocketNotifier *>())notifier->setEnabled(false);
        if(m_display)XCloseDisplay(m_display);
    }
private:
    QVector<unsigned long> values(Window window,Atom property) {
        Atom type;int format;unsigned long count,remaining;unsigned char *data=nullptr;
        QVector<unsigned long> result;
        if(XGetWindowProperty(m_display,window,property,0,65536,False,AnyPropertyType,
            &type,&format,&count,&remaining,&data)==Success && data && format==32) {
            auto *items=reinterpret_cast<unsigned long *>(data);
            for(unsigned long i=0;i<count;++i)result.append(items[i]);
        }
        if(data)XFree(data);return result;
    }
    void check() {
        if(!m_visible())return;
        const auto clients=values(m_root,m_clients);const int own=clients.indexOf(m_window);
        if(own<0)return;
        for(int i=own+1;i<clients.size();++i) {
            if(!values(clients[i],m_type).contains(m_desktop))continue;
            // Any system DESKTOP above us is a layer conflict; normal windows,
            // docks and menus are deliberately left alone.
            m_restore();return;
        }
    }
    Display *m_display=nullptr;Window m_root=0,m_window;Atom m_clients,m_type,m_desktop;
    QTimer m_timer;std::function<bool()> m_visible;std::function<void()> m_restore;
};
#undef None
#undef Status
