#include "PanelNative.h"
#include <QWidget>
#include <QGuiApplication>
#include <QVariant>
#include <QImage>
#include <QLibrary>
#include <QVector>
#include <X11/Xlib.h>

void setPanelNativeBackdrop(QWidget *panel, bool liquid)
{
    if(QGuiApplication::platformName()!=QStringLiteral("xcb") || !panel->testAttribute(Qt::WA_WState_Created))return;
    Display *display=XOpenDisplay(nullptr);
    if(!display)return;
    const Window window=panel->winId();
    const Atom blur=XInternAtom(display,"_KDE_NET_WM_BLUR_BEHIND_REGION",False);
    if(liquid || !panel->property("liquidNativeSaved").toBool()) {
        Atom type=None;int format=0;unsigned long count=0,remaining=0;unsigned char *data=nullptr;
        if(XGetWindowProperty(display,window,blur,0,65536,False,AnyPropertyType,&type,&format,&count,&remaining,&data)==Success
           && (!panel->property("liquidNativeSaved").toBool() || type!=None)) {
            // OEM can publish its blur property after Polish/Show, or rewrite
            // it on geometry changes. Preserve that latest native value.
            panel->setProperty("liquidBlurType",qulonglong(type));
            panel->setProperty("liquidBlurFormat",format);
            panel->setProperty("liquidBlurCount",qulonglong(count));
            const int unit=format==32?sizeof(long):format/8;
            panel->setProperty("liquidBlurData",QByteArray(reinterpret_cast<char *>(data),int(count)*unit));
            panel->setProperty("liquidNativeSaved",true);
        }
        if(data)XFree(data);
    }
    if(liquid) {
        // The cached optical image already includes diffusion. A rectangular
        // compositor blur underneath it leaks into transparent rounded corners.
        XDeleteProperty(display,window,blur);
    } else {
        const Atom type=panel->property("liquidBlurType").toULongLong();
        if(type!=None) {
            const QByteArray data=panel->property("liquidBlurData").toByteArray();
            XChangeProperty(display,window,blur,type,panel->property("liquidBlurFormat").toInt(),PropModeReplace,
                reinterpret_cast<const unsigned char *>(data.constData()),panel->property("liquidBlurCount").toInt());
        } else XDeleteProperty(display,window,blur);
    }
    XFlush(display);XCloseDisplay(display);
}

// Retain every partially covered optical pixel; discard the transparent outer
// margin instead of exposing child fills/shadows through a widened QRegion.
void setPanelNativeOutline(QWidget *panel, const QImage &coverage)
{
    if(QGuiApplication::platformName()!=QStringLiteral("xcb") || coverage.isNull()
       || !panel->testAttribute(Qt::WA_WState_Created))return;
    using Combine = void (*)(Display *, Window, int, int, int, XRectangle *, int, int, int);
    static QLibrary library(QStringLiteral("libXext.so.6"));
    static auto combine=reinterpret_cast<Combine>(library.resolve("XShapeCombineRectangles"));
    if(!combine)return;
    QVector<XRectangle> spans;
    for(int y=0;y<coverage.height();++y) {
        const auto *pixels=reinterpret_cast<const QRgb *>(coverage.constScanLine(y));
        int first=0,last=coverage.width()-1;
        while(first<=last && qAlpha(pixels[first])==0)++first;
        while(last>=first && qAlpha(pixels[last])==0)--last;
        if(first>last)continue;
        if(!spans.isEmpty() && spans.last().x==first && spans.last().width==last-first+1
           && spans.last().y+spans.last().height==y)++spans.last().height;
        else spans.append(XRectangle{short(first),short(y),ushort(last-first+1),1});
    }
    Display *display=XOpenDisplay(nullptr);if(!display)return;
    // ShapeBounding=0, ShapeSet=0, YXBanded=3; input is bounded by this too.
    combine(display,panel->winId(),0,0,0,spans.data(),spans.size(),0,3);
    XFlush(display);XCloseDisplay(display);
    panel->setProperty("liquidNativeOutlineKey",coverage.cacheKey());
}
