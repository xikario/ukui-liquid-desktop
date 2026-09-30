#include "KeyInterceptor.h"
#include "StartMenu.h"

#include <QDebug>
#include <QTimer>
#include <QSocketNotifier>
#include <QGuiApplication>
#include <QDateTime>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

// -----------------------------------------------------------------------
// Minimal XRecord declarations (from libXtst / X11/extensions/record.h)
// We declare these manually since libxtst-dev is not installed, but
// the runtime library libXtst.so.6 IS available and contains the symbols.
// -----------------------------------------------------------------------
extern "C" {

typedef unsigned long XRecordClientSpec;
#define XRecordAllClients       ((XRecordClientSpec)3)

typedef struct {
    unsigned char   first;
    unsigned char   last;
} XRecordRange8;

typedef struct {
    unsigned short  first;
    unsigned short  last;
} XRecordRange16;

typedef struct {
    XRecordRange8   ext_major;
    XRecordRange16  ext_minor;
} XRecordExtRange;

typedef struct {
    XRecordRange8     core_requests;
    XRecordRange8     core_replies;
    XRecordExtRange   ext_requests;
    XRecordExtRange   ext_replies;
    XRecordRange8     delivered_events;
    XRecordRange8     device_events;      // <-- KeyPress/KeyRelease range
    XRecordRange8     errors;
    Bool              client_started;
    Bool              client_died;
} XRecordRange;

typedef unsigned long XRecordContext;

typedef struct {
    XID                 id_base;
    Time                server_time;
    unsigned int        client_seq;
    int                 category;
    Bool                client_swapped;
    unsigned char       *data;
    unsigned long       data_len;
} XRecordInterceptData;

typedef void (*XRecordInterceptProc)(XPointer, XRecordInterceptData *);

// Functions from libXtst.so
extern int XRecordQueryVersion(Display *, int *, int *);
extern XRecordRange *XRecordAllocRange(void);
extern XRecordContext XRecordCreateContext(Display *, int,
    XRecordClientSpec *, int, XRecordRange **, int);
extern int XRecordEnableContextAsync(Display *, XRecordContext,
    XRecordInterceptProc, XPointer);
extern void XRecordProcessReplies(Display *);
extern int XRecordDisableContext(Display *, XRecordContext);
extern int XRecordFreeContext(Display *, XRecordContext);
extern void XRecordFreeData(XRecordInterceptData *);

} // extern "C"

// XRecord categories
#define XRecordFromServer       0
#define XRecordStartOfData      4

namespace {
    bool isX11Platform() {
        return QGuiApplication::platformName().toLower().contains(QLatin1String("xcb"));
    }

    bool keyDebugEnabled()
    {
        static const bool enabled =
            qEnvironmentVariableIsSet("UKUI_KAISHICAIDAN_DEBUG_KEYS");
        return enabled;
    }
}

// -----------------------------------------------------------------------
// Static callback for XRecord — forwards to the KeyInterceptor instance
// -----------------------------------------------------------------------
void xrecordCallback(char *closure, void *rawData)
{
    XRecordInterceptData *data = static_cast<XRecordInterceptData *>(rawData);
    if (data->category != XRecordFromServer) {
        XRecordFreeData(data);
        return;
    }

    // data->data is a wire event: first byte is event type
    if (data->data_len < 2) {
        XRecordFreeData(data);
        return;
    }

    // We don't process here; we let processRecordedEvents() pull data
    // via XRecordProcessReplies and the callback stores into a queue.
    // But for simplicity we handle it inline since processRecordedEvents
    // calls XRecordProcessReplies which invokes this callback synchronously.

    // Store the data pointer in the interceptor for processing
    // (We process inline in the callback invoked from processRecordedEvents)
    unsigned char type = data->data[0] & 0x7F;  // strip high bit
    unsigned char keycode = data->data[1];

    // Only handle KeyPress (type 2) and KeyRelease (type 3)
    if (type == 2 || type == 3) {
        KeyInterceptor *ki = reinterpret_cast<KeyInterceptor *>(closure);

        bool isSuper = ki->isSuperKeycode(keycode);

        if (type == 2) { // KeyPress
            if (isSuper) {
                if (keyDebugEnabled())
                    qDebug() << "[XRecord] Super KeyPress, keycode:" << keycode;
                if (!ki->m_superDown) {
                    ki->m_superDown = true;
                    ki->m_combined = false;
                    ki->m_pressTime = QDateTime::currentMSecsSinceEpoch();
                }
            } else {
                if (ki->m_superDown) {
                    if (keyDebugEnabled())
                        qDebug() << "[XRecord] Combined key detected, keycode:" << keycode;
                    ki->m_combined = true;
                }
            }
        } else { // KeyRelease (type 3)
            if (isSuper && ki->m_superDown) {
                ki->m_superDown = false;
                qint64 duration = QDateTime::currentMSecsSinceEpoch() - ki->m_pressTime;
                if (keyDebugEnabled())
                    qDebug() << "[XRecord] Super KeyRelease, duration:" << duration
                             << "combined:" << ki->m_combined;
                if (!ki->m_combined && duration < 400) {
                    if (keyDebugEnabled())
                        qDebug() << "[XRecord] >>> Emitting winKeyPressed!";
                    QTimer::singleShot(0, ki, [ki]() {
                        emit ki->winKeyPressed();
                    });
                }
            }
        }
    }

    XRecordFreeData(data);
}

// -----------------------------------------------------------------------
// KeyInterceptor implementation
// -----------------------------------------------------------------------

KeyInterceptor::KeyInterceptor(StartMenu *menu, QObject *parent)
    : QObject(parent)
    , m_menu(menu)
{
}

KeyInterceptor::~KeyInterceptor()
{
    ungrabWinKey();
}

bool KeyInterceptor::grabWinKey()
{
    return setupXRecord();
}

bool KeyInterceptor::setupXRecord()
{
    if (!isX11Platform()) {
        qWarning() << "[KeyInterceptor] Non-X11 platform, global Win-key detection disabled";
        return false;
    }

    // XRecord requires TWO display connections:
    // - m_ctrlDisplay: for setup/control calls
    // - m_dataDisplay: for receiving recorded event data
    m_ctrlDisplay = XOpenDisplay(nullptr);
    m_dataDisplay = XOpenDisplay(nullptr);
    if (!m_ctrlDisplay || !m_dataDisplay) {
        qWarning() << "[KeyInterceptor] Failed to open X display connections";
        if (m_ctrlDisplay) { XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr; }
        if (m_dataDisplay) { XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr; }
        return false;
    }

    m_rootWindow = DefaultRootWindow(m_ctrlDisplay);

    // Check XRecord extension on BOTH connections
    // (libXtst caches extension info per-display; the data connection
    //  must also have the extension initialized before EnableContextAsync)
    int major = 0, minor = 0;
    if (!XRecordQueryVersion(m_ctrlDisplay, &major, &minor)) {
        qWarning() << "[KeyInterceptor] XRecord extension not available (ctrl)";
        XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr;
        XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr;
        return false;
    }
    int dmaj = 0, dmin = 0;
    if (!XRecordQueryVersion(m_dataDisplay, &dmaj, &dmin)) {
        qWarning() << "[KeyInterceptor] XRecord extension not available (data)";
        XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr;
        XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr;
        return false;
    }
    if (keyDebugEnabled())
        qDebug() << "[KeyInterceptor] XRecord version:" << major << "." << minor;

    // Resolve Super keycodes
    KeyCode superL = XKeysymToKeycode(m_ctrlDisplay, XK_Super_L);
    KeyCode superR = XKeysymToKeycode(m_ctrlDisplay, XK_Super_R);

    if (superL == 0 && superR == 0) {
        qWarning() << "[KeyInterceptor] No Super_L or Super_R found in keymap";
        XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr;
        XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr;
        return false;
    }

    if (superL) m_superKeycodes.insert(superL);
    if (superR) m_superKeycodes.insert(superR);

    // Set up XRecord to monitor all device events (KeyPress + KeyRelease)
    XRecordRange *range = XRecordAllocRange();
    if (!range) {
        qWarning() << "[KeyInterceptor] Failed to allocate XRecord range";
        XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr;
        XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr;
        return false;
    }

    // Monitor KeyPress (type 2) and KeyRelease (type 3)
    range->device_events.first = KeyPress;   // 2
    range->device_events.last  = KeyRelease; // 3

    XRecordClientSpec clients = XRecordAllClients;
    m_recordContext = XRecordCreateContext(m_ctrlDisplay, 0,
                                          &clients, 1,
                                          &range, 1);
    XFree(range);

    if (!m_recordContext) {
        qWarning() << "[KeyInterceptor] Failed to create XRecord context";
        XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr;
        XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr;
        return false;
    }

    // CRITICAL: Sync the control connection so the X server has processed
    // the CreateContext request before the data connection tries to use it.
    XSync(m_ctrlDisplay, False);

    if (keyDebugEnabled())
        qDebug() << "[KeyInterceptor] XRecord context created:" << m_recordContext;

    // Enable context asynchronously on the data display
    int enableResult = XRecordEnableContextAsync(m_dataDisplay, m_recordContext,
                                   reinterpret_cast<XRecordInterceptProc>(xrecordCallback),
                                   reinterpret_cast<XPointer>(this));
    if (keyDebugEnabled())
        qDebug() << "[KeyInterceptor] XRecordEnableContextAsync returned:" << enableResult;
    if (!enableResult) {
        qWarning() << "[KeyInterceptor] Failed to enable XRecord context";
        XRecordFreeContext(m_ctrlDisplay, m_recordContext);
        m_recordContext = 0;
        XCloseDisplay(m_ctrlDisplay); m_ctrlDisplay = nullptr;
        XCloseDisplay(m_dataDisplay); m_dataDisplay = nullptr;
        return false;
    }

    XFlush(m_dataDisplay);

    m_active = true;

    if (!m_recordNotifier) {
        m_recordNotifier = new QSocketNotifier(ConnectionNumber(m_dataDisplay),
                                               QSocketNotifier::Read, this);
        connect(m_recordNotifier, &QSocketNotifier::activated,
                this, [this] {
            if (!m_recordNotifier)
                return;
            m_recordNotifier->setEnabled(false);
            processRecordedEvents();
            if (m_recordNotifier && m_active)
                m_recordNotifier->setEnabled(true);
        });
    }
    m_recordNotifier->setEnabled(true);

    if (keyDebugEnabled())
        qDebug() << "[KeyInterceptor] XRecord Super key monitoring active,"
                 << m_superKeycodes.size() << "keycodes tracked";
    return true;
}

bool KeyInterceptor::isSuperKeycode(unsigned int keycode) const
{
    return m_superKeycodes.contains(keycode);
}

void KeyInterceptor::ungrabWinKey()
{
    if (m_recordNotifier) {
        m_recordNotifier->setEnabled(false);
        m_recordNotifier->deleteLater();
        m_recordNotifier = nullptr;
    }

    if (m_active && m_ctrlDisplay && m_recordContext) {
        XRecordDisableContext(m_ctrlDisplay, m_recordContext);
        XRecordFreeContext(m_ctrlDisplay, m_recordContext);
        // Deliver the control requests before XCloseDisplay synchronizes the
        // recording connection; otherwise its still-enabled stream can block
        // shutdown while the disable request remains in the control buffer.
        XSync(m_ctrlDisplay, False);
        m_recordContext = 0;
    }

    if (m_dataDisplay) {
        XCloseDisplay(m_dataDisplay);
        m_dataDisplay = nullptr;
    }
    if (m_ctrlDisplay) {
        XCloseDisplay(m_ctrlDisplay);
        m_ctrlDisplay = nullptr;
    }

    m_active = false;
    m_superDown = false;
}

void KeyInterceptor::processRecordedEvents()
{
    if (!m_dataDisplay || !m_active)
        return;

    // XRecordProcessReplies reads any pending data from the data display
    // and invokes xrecordCallback synchronously for each recorded event.
    XRecordProcessReplies(m_dataDisplay);
}

bool KeyInterceptor::nativeEventFilter(const QByteArray &eventType,
                                       void *message, long *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    // XRecord-based approach does not need the native event filter.
    // We keep the method for ABI/interface compatibility.
    return false;
}
