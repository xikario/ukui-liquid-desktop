#pragma once

#include "StartMenu.h"
#include <QDBusAbstractAdaptor>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QTimer>

// UKUI 3.26 calls this path/interface, not the older /ukuiKaishicaidan.
class UkuiMenuCompat : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ukui.menu")
public:
    explicit UkuiMenuCompat(StartMenu *menu)
        : QDBusAbstractAdaptor(menu), m_menu(menu) {
        m_nativeRequest.setSingleShot(true);
        m_nativeRequest.setInterval(80);
        connect(&m_nativeRequest, &QTimer::timeout, menu, &StartMenu::toggle);
    }
    void setKeyMonitorActive(bool active) { m_keyMonitorActive = active; }
    void observeSuperKey(bool down) {
        m_superDown = down;
        m_nativeRequest.stop();
        if (!down) m_lastRelease.start();
    }
public slots:
    void WinKeyResponse() {
        if (!m_keyMonitorActive) {
            m_menu->toggle();
            return;
        }
        // Suppress only a gesture we actually observed, including combinations
        // and long presses. Cold D-Bus activation precedes XRecord monitoring.
        if (m_superDown || (m_lastRelease.isValid() && m_lastRelease.elapsed() < 250))
            return;
        // XRecord and D-Bus are separate sockets; allow a queued recorded
        // gesture to cancel this notification before treating it as native-only.
        if (!m_nativeRequest.isActive()) m_nativeRequest.start();
    }
    void Activate(const QVariantMap &) { m_menu->toggle(); }
private:
    StartMenu *m_menu;
    bool m_keyMonitorActive = false;
    bool m_superDown = false;
    QElapsedTimer m_lastRelease;
    QTimer m_nativeRequest;
};
