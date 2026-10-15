#pragma once

#include "StartMenu.h"
#include <QDBusAbstractAdaptor>
#include <QVariantMap>

// UKUI 3.26 calls this path/interface, not the older /ukuiKaishicaidan.
class UkuiMenuCompat : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ukui.menu")
public:
    explicit UkuiMenuCompat(StartMenu *menu)
        : QDBusAbstractAdaptor(menu), m_menu(menu) {}
    void setKeyMonitorActive(bool active) { m_keyMonitorActive = active; }
public slots:
    void WinKeyResponse() {
        // XRecord already handles a standalone Super release and preserves
        // combinations. The native shortcut notification is the SAME press.
        if (!m_keyMonitorActive) m_menu->toggle();
    }
    void Activate(const QVariantMap &) { m_menu->toggle(); }
private:
    StartMenu *m_menu;
    bool m_keyMonitorActive = false;
};
