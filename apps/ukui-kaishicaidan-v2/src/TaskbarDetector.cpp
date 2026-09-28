#include "TaskbarDetector.h"

#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cstring>

namespace {
    bool isX11Platform() {
        return QApplication::platformName().toLower().contains(QLatin1String("xcb"));
    }
}

// ── Primary detection: find the real ukui-panel window ─────────
// This directly queries X11 for the panel window with WM_CLASS
// "ukui-panel" and _NET_WM_WINDOW_TYPE_DOCK, giving us its exact
// on-screen geometry regardless of whether the panel is centered.

TaskbarInfo TaskbarDetector::detect()
{
    if (!isX11Platform()) {
        TaskbarInfo info = fallback();
        queryPanelDbus(info);
        return info;
    }

    // Try real panel window first — most accurate
    TaskbarInfo info = detectPanelWindow();
    if (info.detected) {
        qDebug() << "[TaskbarDetector] Used real panel window:"
                 << info.geometry << "edge:" << info.edge
                 << "startButtonCenter:" << info.startButtonCenter;
        return info;
    }

    // Fall back to strut-based detection
    info = detectFromStrut();
    if (!info.detected)
        info = fallback();

    queryPanelDbus(info);

    qDebug() << "[TaskbarDetector] Fallback detection:"
             << info.geometry << "edge:" << info.edge
             << "startButtonCenter:" << info.startButtonCenter;
    return info;
}

// ── Find the real ukui-panel X window ───────────────────────────

TaskbarInfo TaskbarDetector::detectPanelWindow()
{
    TaskbarInfo info;

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return info;

    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) {
        XCloseDisplay(dpy);
        return info;
    }
    const QRect screenGeom = screen->geometry();

    // Get _NET_CLIENT_LIST from root
    const Window root = DefaultRootWindow(dpy);
    const Atom clientListAtom = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (clientListAtom == None) {
        XCloseDisplay(dpy);
        return info;
    }

    Atom type = None;
    int fmt = 0;
    unsigned long nItems = 0, after = 0;
    unsigned char *data = nullptr;

    if (XGetWindowProperty(dpy, root, clientListAtom, 0, 4096, False,
                           XA_WINDOW, &type, &fmt, &nItems, &after,
                           &data) != Success || !data) {
        XCloseDisplay(dpy);
        return info;
    }

    const Window *windows = reinterpret_cast<const Window *>(data);
    const Atom wmClassAtom = XInternAtom(dpy, "WM_CLASS", True);
    const Atom wmTypeAtom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", True);
    const Atom dockType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", True);
    const Atom strutPartialAtom = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", True);

    Window panelWin = 0;

    for (unsigned long i = 0; i < nItems; ++i) {
        // Check WM_CLASS
        Atom classType = None;
        int classFmt = 0;
        unsigned long classN = 0, classAfter = 0;
        unsigned char *classData = nullptr;

        if (XGetWindowProperty(dpy, windows[i], wmClassAtom, 0, 256, False,
                               XA_STRING, &classType, &classFmt, &classN,
                               &classAfter, &classData) == Success && classData) {
            // WM_CLASS is two null-terminated strings: instance\0class\0
            // We check if either contains "ukui-panel"
            bool match = false;
            if (classN > 0) {
                const char *str = reinterpret_cast<const char *>(classData);
                // instance name
                if (std::strstr(str, "ukui-panel"))
                    match = true;
                // class name (skip past first null)
                size_t len1 = std::strlen(str);
                if (!match && len1 + 1 < classN) {
                    const char *str2 = str + len1 + 1;
                    if (std::strstr(str2, "ukui-panel"))
                        match = true;
                }
            }
            XFree(classData);

            if (!match) continue;

            // Verify it's a DOCK window type
            Atom typeType2 = None;
            int typeFmt2 = 0;
            unsigned long typeN2 = 0, typeAfter2 = 0;
            unsigned char *typeData2 = nullptr;

            bool isDock = false;
            if (wmTypeAtom != None && dockType != None &&
                XGetWindowProperty(dpy, windows[i], wmTypeAtom, 0, 32, False,
                                   XA_ATOM, &typeType2, &typeFmt2, &typeN2,
                                   &typeAfter2, &typeData2) == Success && typeData2) {
                const Atom *types = reinterpret_cast<const Atom *>(typeData2);
                for (unsigned long t = 0; t < typeN2; ++t) {
                    if (types[t] == dockType) {
                        isDock = true;
                        break;
                    }
                }
                XFree(typeData2);
            }

            if (isDock) {
                panelWin = windows[i];
                break;
            }
        }
    }
    XFree(data);

    if (!panelWin) {
        XCloseDisplay(dpy);
        return info;
    }

    // Get the panel window's actual geometry
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(dpy, panelWin, &attrs)) {
        XCloseDisplay(dpy);
        return info;
    }

    // Translate coordinates to root
    Window child;
    int absX = 0, absY = 0;
    XTranslateCoordinates(dpy, panelWin, root, 0, 0, &absX, &absY, &child);

    // CRITICAL: X11 returns physical pixel coordinates, but Qt uses logical
    // pixels for move()/geometry(). On HiDPI (e.g. DPR=1.5, DPR=2.0) we must
    // convert, otherwise the menu position is scaled incorrectly.
    const qreal dpr = screen->devicePixelRatio();
    const int logX = static_cast<int>(absX / dpr);
    const int logY = static_cast<int>(absY / dpr);
    const int logW = static_cast<int>(attrs.width / dpr);
    const int logH = static_cast<int>(attrs.height / dpr);

    info.nativeWindow = panelWin;
    info.geometry = QRect(logX, logY, logW, logH);
    info.screenGeometry = screenGeom;
    info.detected = true;

    // Determine edge from position (compare logical coords with logical screen)
    if (logW > logH) {
        // Horizontal bar
        if (logY + logH / 2 > screenGeom.center().y()) {
            info.edge = 3;  // bottom
        } else {
            info.edge = 2;  // top
        }
    } else {
        // Vertical bar
        if (logX + logW / 2 > screenGeom.center().x()) {
            info.edge = 1;  // right
        } else {
            info.edge = 0;  // left
        }
    }

    // Check if panel is centered on screen
    if (info.edge == 3 || info.edge == 2) {
        const int panelMidX = logX + logW / 2;
        const int screenMidX = screenGeom.center().x();
        info.panelIsCentered = (qAbs(panelMidX - screenMidX) < 50);
    }
    info.panelCenter = QPoint(logX + logW / 2, logY + logH / 2);

    // Estimate start button center
    // The start button (ukui-menu) is at the leftmost position of the panel
    // on bottom/top bars, or topmost on side bars.
    // We use the panel's own left edge (not the strut's!) plus a small offset.
    const int btnOffset = logH / 2;  // roughly half the panel height

    if (info.edge == 3) {
        // Bottom taskbar
        info.startButtonCenter = QPoint(logX + btnOffset,
                                        logY + logH / 2);
    } else if (info.edge == 2) {
        // Top taskbar
        info.startButtonCenter = QPoint(logX + btnOffset,
                                        logY + logH / 2);
    } else if (info.edge == 0) {
        // Left taskbar
        info.startButtonCenter = QPoint(logX + logW / 2,
                                        logY + btnOffset);
    } else {
        // Right taskbar
        info.startButtonCenter = QPoint(logX + logW / 2,
                                        logY + btnOffset);
    }

    // Also try to read strut to get additional info but don't overwrite geometry
    if (strutPartialAtom != None) {
        Atom sType = None;
        int sFmt = 0;
        unsigned long sN = 0, sAfter = 0;
        unsigned char *sData = nullptr;
        if (XGetWindowProperty(dpy, panelWin, strutPartialAtom, 0, 12, False,
                               XA_CARDINAL, &sType, &sFmt, &sN, &sAfter,
                               &sData) == Success && sData) {
            // We already have the real geometry; strut info is just confirmation
            XFree(sData);
        }
    }

    XCloseDisplay(dpy);
    return info;
}

TaskbarInfo TaskbarDetector::detectFromStrut()
{
    TaskbarInfo info;

    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return info;

    // Instead of reading strut from root (which may be wrong),
    // find the ukui-panel window and read ITS strut
    const Atom strutAtom = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", True);
    if (strutAtom == None) {
        XCloseDisplay(display);
        return info;
    }

    // Enumerate all top-level windows to find one with strut set
    const Window root = DefaultRootWindow(display);
    const Atom clientListAtom = XInternAtom(display, "_NET_CLIENT_LIST", True);
    if (clientListAtom == None) {
        XCloseDisplay(display);
        return info;
    }

    Atom type = None;
    int fmt = 0;
    unsigned long nItems = 0, after = 0;
    unsigned char *listData = nullptr;

    if (XGetWindowProperty(display, root, clientListAtom, 0, 4096, False,
                           XA_WINDOW, &type, &fmt, &nItems, &after,
                           &listData) != Success || !listData) {
        XCloseDisplay(display);
        return info;
    }

    const Window *windows = reinterpret_cast<const Window *>(listData);
    unsigned long strut[12] = {};
    bool foundStrut = false;

    for (unsigned long i = 0; i < nItems && !foundStrut; ++i) {
        Atom sType = None;
        int sFmt = 0;
        unsigned long sN = 0, sAfter = 0;
        unsigned char *sData = nullptr;

        if (XGetWindowProperty(display, windows[i], strutAtom, 0, 12, False,
                               XA_CARDINAL, &sType, &sFmt, &sN, &sAfter,
                               &sData) == Success && sData && sFmt == 32 && sN >= 12) {
            const unsigned long *s = reinterpret_cast<const unsigned long *>(sData);
            for (int k = 0; k < 12; ++k)
                strut[k] = s[k];
            foundStrut = true;
            XFree(sData);
        } else if (sData) {
            XFree(sData);
        }
    }
    XFree(listData);

    if (!foundStrut) {
        XCloseDisplay(display);
        return info;
    }

    const unsigned long left   = strut[0];
    const unsigned long right  = strut[1];
    const unsigned long top    = strut[2];
    const unsigned long bottom = strut[3];
    const unsigned long bottom_start_x = strut[10];
    const unsigned long bottom_end_x  = strut[11];
    const unsigned long top_start_x   = strut[8];
    const unsigned long top_end_x     = strut[9];
    const unsigned long left_start_y  = strut[4];
    const unsigned long left_end_y    = strut[5];
    const unsigned long right_start_y = strut[6];
    const unsigned long right_end_y   = strut[7];

    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) {
        XCloseDisplay(display);
        return info;
    }
    const QRect screenGeom = screen->geometry();

    if (bottom > 0) {
        info.edge = 3;
        const int panelHeight = static_cast<int>(bottom);
        const int panelX = static_cast<int>(bottom_start_x);
        const int panelW = (bottom_end_x > bottom_start_x)
                               ? static_cast<int>(bottom_end_x - bottom_start_x)
                               : screenGeom.width();
        info.geometry = QRect(panelX,
                              screenGeom.bottom() - panelHeight + 1,
                              panelW, panelHeight);
    } else if (top > 0) {
        info.edge = 2;
        const int panelX = static_cast<int>(top_start_x);
        const int panelW = (top_end_x > top_start_x)
                               ? static_cast<int>(top_end_x - top_start_x)
                               : screenGeom.width();
        info.geometry = QRect(panelX, 0, panelW, static_cast<int>(top));
    } else if (left > 0) {
        info.edge = 0;
        const int panelY = static_cast<int>(left_start_y);
        const int panelH = (left_end_y > left_start_y)
                               ? static_cast<int>(left_end_y - left_start_y)
                               : screenGeom.height();
        info.geometry = QRect(0, panelY, static_cast<int>(left), panelH);
    } else if (right > 0) {
        info.edge = 1;
        const int panelY = static_cast<int>(right_start_y);
        const int panelH = (right_end_y > right_start_y)
                               ? static_cast<int>(right_end_y - right_start_y)
                               : screenGeom.height();
        info.geometry = QRect(screenGeom.right() - static_cast<int>(right) + 1,
                              panelY, static_cast<int>(right), panelH);
    }

    info.detected = info.geometry.isValid();
    info.screenGeometry = screenGeom;

    if (info.detected) {
        if (info.edge == 3) {
            info.startButtonCenter = QPoint(info.geometry.left() + 24,
                                            info.geometry.top() + info.geometry.height() / 2);
        } else if (info.edge == 2) {
            info.startButtonCenter = QPoint(info.geometry.left() + 24,
                                            info.geometry.bottom() - info.geometry.height() / 2);
        } else if (info.edge == 0) {
            info.startButtonCenter = QPoint(info.geometry.left() + info.geometry.width() / 2,
                                            info.geometry.top() + 24);
        } else {
            info.startButtonCenter = QPoint(info.geometry.left() + info.geometry.width() / 2,
                                            info.geometry.top() + 24);
        }
    }

    XCloseDisplay(display);
    return info;
}

bool TaskbarDetector::queryPanelDbus(TaskbarInfo &info)
{
    QDBusInterface iface("org.ukui.panel", "/panel/position",
                         "org.ukui.panel",
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;

    QDBusReply<QString> posReply = iface.call("GetPanelPosition");
    if (posReply.isValid()) {
        const QString pos = posReply.value();
        if (pos == "bottom") info.edge = 3;
        else if (pos == "top") info.edge = 2;
        else if (pos == "left") info.edge = 0;
        else if (pos == "right") info.edge = 1;
    }

    QDBusReply<QVariantList> geomReply = iface.call("GetPrimaryScreenGeometry");
    if (geomReply.isValid()) {
        const QVariantList v = geomReply.value();
        if (v.size() >= 4) {
            info.screenGeometry = QRect(v[0].toInt(), v[1].toInt(),
                                        v[2].toInt(), v[3].toInt());
        }
    }

    if (info.geometry.isValid()) {
        info.panelCenter = QPoint(info.geometry.center().x(),
                                  info.geometry.center().y());
    }
    return true;
}

TaskbarInfo TaskbarDetector::fallback()
{
    TaskbarInfo info;
    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) return info;

    const QRect screenGeom = screen->geometry();
    const int panelHeight = 48;

    info.detected = true;
    info.edge = 3; // assume bottom
    info.geometry = QRect(screenGeom.left(),
                          screenGeom.bottom() - panelHeight + 1,
                          screenGeom.width(),
                          panelHeight);
    info.screenGeometry = screenGeom;
    info.startButtonCenter = QPoint(screenGeom.left() + 24,
                                    screenGeom.bottom() - panelHeight / 2);
    info.panelCenter = info.geometry.center();

    return info;
}
