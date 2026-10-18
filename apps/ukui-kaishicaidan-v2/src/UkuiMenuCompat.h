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
        connect(&m_nativeRequest, &QTimer::timeout, this,
                &UkuiMenuCompat::nativeRequestTimedOut);
    }
    void setKeyMonitorActive(bool active) { m_keyMonitorActive = active; }
    void observeSuperKey(bool down) {
        m_superDown = down;
        if (down && m_nativeToggle.isValid() && !m_nativeFallbackToggle)
            m_nativeToggle.invalidate();
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
        if (m_superDown
            || (m_lastRelease.isValid() && m_lastRelease.elapsed() < 250)
            || (m_nativeToggle.isValid()
                && m_nativeToggle.elapsed() < gestureDeduplicationMs))
            return;
        // XRecord and D-Bus are separate sockets; allow a queued recorded
        // gesture to cancel this notification before treating it as native-only.
        if (!m_nativeRequest.isActive()) m_nativeRequest.start();
    }
public:
    static constexpr int gestureDeduplicationMs = 500;
    // Called for the short-press gesture observed through XRecord. A native
    // notification can win the race and toggle first; consume the delayed
    // recorded gesture so that one physical press cannot toggle twice.
    void recordedWinKey() {
        if (m_nativeToggle.isValid()) {
            if (m_nativeToggle.elapsed() < gestureDeduplicationMs) {
                m_nativeToggle.invalidate();
                return;
            }
            m_nativeToggle.invalidate();
        }
        m_menu->toggle();
        m_nativeToggle.start();
        m_nativeFallbackToggle = false;
    }
public slots:
    void Activate(const QVariantMap &) { m_menu->toggle(); }
private:
    void nativeRequestTimedOut() {
        m_nativeToggle.start();
        m_nativeFallbackToggle = true;
        m_menu->toggle();
    }

    StartMenu *m_menu;
    bool m_keyMonitorActive = false;
    bool m_superDown = false;
    QElapsedTimer m_lastRelease;
    QElapsedTimer m_nativeToggle;
    bool m_nativeFallbackToggle = false;
    QTimer m_nativeRequest;
};
