#include "MusicClientIntegration.h"
#include "music_fixture.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QThread>
#include <functional>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

void runMusicWindowTest(const std::function<void(bool,const char *)> &verify) {
    Display *d=XOpenDisplay(nullptr);verify(d!=nullptr,"window activation fixture connects to the isolated X server");if(!d)return;
    const Window root=DefaultRootWindow(d),target=XCreateSimpleWindow(d,root,10,10,120,80,0,0,0),foreign=XCreateSimpleWindow(d,root,20,20,120,80,0,0,0);
    const Atom pidAtom=XInternAtom(d,"_NET_WM_PID",False),list=XInternAtom(d,"_NET_CLIENT_LIST_STACKING",False),active=XInternAtom(d,"_NET_ACTIVE_WINDOW",False);
    const unsigned long ourPid=QCoreApplication::applicationPid(),foreignPid=1;
    XChangeProperty(d,target,pidAtom,XA_CARDINAL,32,PropModeReplace,reinterpret_cast<const unsigned char *>(&ourPid),1);
    XChangeProperty(d,foreign,pidAtom,XA_CARDINAL,32,PropModeReplace,reinterpret_cast<const unsigned char *>(&foreignPid),1);
    const unsigned long windows[]={target,foreign};
    XChangeProperty(d,root,list,XA_WINDOW,32,PropModeReplace,reinterpret_cast<const unsigned char *>(windows),2);
    XSelectInput(d,root,SubstructureNotifyMask);XSync(d,False);
    const QString service="org.mpris.MediaPlayer2.fixture_window";
    const auto profiles=MprisPlayer::loadProfiles();MprisPlayer::saveProfiles({{"窗口恢复",service,{}, {},true}});
    auto bus=QDBusConnection::sessionBus();MusicFixture fixture;MusicRootFixture adaptor(&fixture);
    bus.registerObject("/org/mpris/MediaPlayer2",&fixture,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllProperties|QDBusConnection::ExportAdaptors);bus.registerService(service);
    {
        MprisPlayer player;QElapsedTimer timer;timer.start();
        while(!player.connected() && timer.elapsed()<3000){QCoreApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(5);}
        verify(player.connected() && player.canOpen(),"active process can open its window even when the MPRIS Raise handler has no UI effect");
        player.openPlayer();XSync(d,False);
        XWindowAttributes a{},b{};XGetWindowAttributes(d,target,&a);XGetWindowAttributes(d,foreign,&b);
        bool requested=false;while(XPending(d)){XEvent event;XNextEvent(d,&event);if(event.type==ClientMessage && event.xclient.message_type==active && event.xclient.window==target)requested=true;}
        verify(a.map_state==IsViewable && b.map_state==IsUnmapped && requested && fixture.raiseCount==0,"Open restores and activates only the selected process window, bypassing a no-op Raise");
        XUnmapWindow(d,target);XSync(d,False);
        const auto process=MusicClientIntegration::inspectProcess(ourPid,service);
        verify(!MusicClientIntegration::activateProcessWindow(ourPid,process.started+1),"a recycled process identity cannot activate an unrelated window");
        XGetWindowAttributes(d,target,&a);verify(a.map_state==IsUnmapped,"rejected process identity leaves the target window untouched");
    }
    bus.unregisterService(service);bus.unregisterObject("/org/mpris/MediaPlayer2");MprisPlayer::saveProfiles(profiles);
    XDeleteProperty(d,root,list);XDestroyWindow(d,target);XDestroyWindow(d,foreign);XCloseDisplay(d);
}
