#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QSocketNotifier>
#include <QSet>

// Forward declare X types we need
typedef unsigned long XID;
typedef XID Window;
typedef struct _XDisplay Display;

class StartMenu;

class KeyInterceptor : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit KeyInterceptor(StartMenu *menu, QObject *parent = nullptr);
    ~KeyInterceptor() override;

    bool grabWinKey();
    void ungrabWinKey();
    bool nativeEventFilter(const QByteArray &eventType,
                           void *message, long *result) override;

signals:
    void winKeyPressed();

private:
    bool setupXRecord();
    bool isSuperKeycode(unsigned int keycode) const;
    void processRecordedEvents();

    // XRecord data connection (receives recorded events)
    Display *m_dataDisplay = nullptr;
    // Control connection (used for XRecord setup and queries)
    Display *m_ctrlDisplay = nullptr;
    Window m_rootWindow = 0;
    StartMenu *m_menu = nullptr;
    QSocketNotifier *m_recordNotifier = nullptr;
    bool m_active = false;
    bool m_superDown = false;
    bool m_combined = false;
    qint64 m_pressTime = 0;

    // XRecord context handle
    unsigned long m_recordContext = 0;

    // Cached super keycodes
    QSet<unsigned int> m_superKeycodes;

    // --- Keep nativeEventFilter/m_grabbed for backward compat but unused ---
    bool m_grabbed = false;

    // Allow the XRecord C callback to access private members
    friend void xrecordCallback(char *, void *);
};
