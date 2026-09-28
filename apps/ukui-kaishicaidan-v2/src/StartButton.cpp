#include "StartButton.h"
#include "StartMenu.h"
#include "TaskbarDetector.h"
#include "StartMenuTheme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QIcon>
#include <QSocketNotifier>
#include <QScreen>
#include <initializer_list>
#include <X11/Xlib.h>
#include <X11/Xatom.h>



namespace {
    bool isX11Platform() {
        return QApplication::platformName().toLower().contains(QLatin1String("xcb"));
    }

    QIcon themedIcon(std::initializer_list<const char *> names) {
        for (const char *name : names) {
            QIcon icon = QIcon::fromTheme(QString::fromLatin1(name));
            if (!icon.isNull())
                return icon;
        }
        return QIcon();
    }
}

StartButton::StartButton(StartMenu *menu)
    : QWidget(nullptr)   // no parent — independent window
    , m_menu(menu)
{
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::X11BypassWindowManagerHint
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_X11NetWmWindowTypeNotification, true);
    setAutoFillBackground(false);

    setFixedSize(48, 48);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    setObjectName("startButtonOverlay");
    setWindowTitle("开始菜单按钮");
    watchTaskbar();
    positionOnTaskbar();
}

StartButton::~StartButton()
{
    delete m_panelNotifier;
    if(m_watchDisplay)XCloseDisplay(static_cast<Display *>(m_watchDisplay));
}

void StartButton::watchTaskbar()
{
    m_repositionTimer.setSingleShot(true);m_repositionTimer.setInterval(100);
    connect(&m_repositionTimer,&QTimer::timeout,this,&StartButton::positionOnTaskbar);
    for(auto *screen:QApplication::screens())
        connect(screen,&QScreen::geometryChanged,this,[this]{m_repositionTimer.start();});
    connect(qApp,&QGuiApplication::primaryScreenChanged,this,[this]{m_repositionTimer.start();});
    if(!isX11Platform())return;
    auto *display=XOpenDisplay(nullptr);if(!display)return;
    m_watchDisplay=display;m_clientListAtom=XInternAtom(display,"_NET_CLIENT_LIST",False);
    // A separate connection avoids changing Qt's event subscriptions. No polling.
    XSelectInput(display,DefaultRootWindow(display),PropertyChangeMask);XFlush(display);
    m_panelNotifier=new QSocketNotifier(ConnectionNumber(display),QSocketNotifier::Read,this);
    connect(m_panelNotifier,&QSocketNotifier::activated,this,[this](int){readTaskbarEvents();});
}

void StartButton::watchPanelWindow(unsigned long window)
{
    if(!m_watchDisplay || window==m_panelWindow)return;
    auto *display=static_cast<Display *>(m_watchDisplay);m_panelWindow=window;m_panelFrame=0;
    if(window){
        XSelectInput(display,window,StructureNotifyMask);
        Window root,parent,*children=nullptr;unsigned int count=0;
        if(XQueryTree(display,window,&root,&parent,&children,&count) && parent!=root){
            m_panelFrame=parent;XSelectInput(display,parent,StructureNotifyMask);
        }
        if(children)XFree(children);
    }
    XFlush(display);
    // XQueryTree can already have buffered events while waiting for its reply.
    QTimer::singleShot(0,this,&StartButton::readTaskbarEvents);
}

void StartButton::readTaskbarEvents()
{
    if(!m_watchDisplay)return;
    auto *display=static_cast<Display *>(m_watchDisplay);bool changed=false;
    while(XPending(display)){
        XEvent event;XNextEvent(display,&event);
        if(event.type==PropertyNotify && event.xproperty.atom==m_clientListAtom)changed=true;
        else if(event.xany.window==m_panelWindow || event.xany.window==m_panelFrame){
            if(event.type==ConfigureNotify || event.type==MapNotify || event.type==UnmapNotify || event.type==ReparentNotify)changed=true;
            if(event.type==DestroyNotify){m_panelWindow=m_panelFrame=0;hide();changed=true;}
        }
    }
    if(changed)m_repositionTimer.start();
}

void StartButton::positionOnTaskbar()
{
    TaskbarInfo info = TaskbarDetector::detect();
    watchPanelWindow(info.nativeWindow);
    if (!info.detected || (isX11Platform() && !info.nativeWindow)) {
        hide();return; // Never leave an invisible click trap at the fallback position.
    }

    const int panelThickness = (info.edge == 2 || info.edge == 3)
        ? info.geometry.height()
        : info.geometry.width();
    const int coverSize = qBound(44, panelThickness, 56);
    if (width() != coverSize || height() != coverSize)
        setFixedSize(coverSize, coverSize);

    QPoint center = info.startButtonCenter;
    int x = center.x() - width() / 2;
    int y = center.y() - height() / 2;

    const QRect bounds = info.geometry.adjusted(2, 2, -2, -2);
    x = qBound(bounds.left(), x, bounds.right() - width() + 1);
    y = qBound(bounds.top(), y, bounds.bottom() - height() + 1);

    move(x, y);
    if(!isVisible())show();
    qDebug() << "[StartButton] overlay geometry:" << geometry()
             << "taskbar:" << info.geometry
             << "startButtonCenter:" << info.startButtonCenter;
    applyX11Immunity();
}

void StartButton::applyX11Immunity()
{
    if (!isX11Platform()) return;

    Display *display = XOpenDisplay(nullptr);
    if (!display) return;

    const Window window = static_cast<Window>(winId());

    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    if (stateAtom != None) {
        QVector<Atom> states;
        const char *stateNames[] = {
            "_NET_WM_STATE_ABOVE",
            "_NET_WM_STATE_SKIP_PAGER",
            "_NET_WM_STATE_SKIP_TASKBAR",
        };
        for (const char *name : stateNames) {
            const Atom atom = XInternAtom(display, name, False);
            if (atom != None)
                states.append(atom);
        }
        if (!states.isEmpty()) {
            XChangeProperty(display, window, stateAtom, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<unsigned char *>(states.data()),
                            states.size());
        }
    }

    XRaiseWindow(display, window);
    XSync(display, False);
    XCloseDisplay(display);

    // Limit re-runs to 3 passes to prevent infinite CPU wakeups
    static int runCount = 0;
    if (runCount < 3) {
        runCount++;
        int nextDelay = (runCount == 1) ? 400 : 1000;
        if (!m_immunityTimer) {
            m_immunityTimer = new QTimer(this);
            m_immunityTimer->setSingleShot(true);
            connect(m_immunityTimer, &QTimer::timeout, this, &StartButton::applyX11Immunity);
        }
        m_immunityTimer->start(nextDelay);
    }
}

void StartButton::paintEvent(QPaintEvent *)
{
    // Intentionally transparent: the original UKUI panel button remains visible.
}

void StartButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_menu->toggle();
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton) {
        showContextMenu(e->globalPos());
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void StartButton::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() != QEvent::WindowStateChange) return;

    if (isMinimized()) {
        QTimer::singleShot(0, this, [this] {
            setWindowState(windowState() & ~Qt::WindowMinimized);
            show();
            applyX11Immunity();
        });
    }
}

void StartButton::enterEvent(QEvent *)
{
}

void StartButton::leaveEvent(QEvent *)
{
}

void StartButton::contextMenuEvent(QContextMenuEvent *e)
{
    showContextMenu(e->globalPos());
    e->accept();
}

void StartButton::showContextMenu(const QPoint &pos)
{
    // Close any existing menu
    if (m_contextMenu) {
        qApp->removeEventFilter(this);
        m_contextMenu->close();
        m_contextMenu->deleteLater();
        m_contextMenu = nullptr;
    }

    m_contextMenu = new QMenu();
    m_contextMenu->setAttribute(Qt::WA_DeleteOnClose);
    StartMenuTheme::applySharedMenuStyle(m_contextMenu);

    auto *actOpen = m_contextMenu->addAction(
        themedIcon({"kylin-startmenu", "start-here", "ukui-menu", "view-grid"}),
        QString::fromUtf8("打开开始菜单"));
    connect(actOpen, &QAction::triggered, m_menu, &StartMenu::toggle);

    m_contextMenu->addSeparator();

    // (管理员) 打开终端
    auto *actTermAdmin = m_contextMenu->addAction(
        themedIcon({"utilities-terminal", "terminal", "dialog-password"}),
        QString::fromUtf8("(管理员) 打开终端"));
    connect(actTermAdmin, &QAction::triggered, this, [this] {
        openTerminalAsAdmin();
    });

    // 普通终端
    auto *actTerm = m_contextMenu->addAction(
        themedIcon({"utilities-terminal", "terminal", "application-x-shellscript"}),
        QString::fromUtf8("打开终端"));
    connect(actTerm, &QAction::triggered, this, [this] {
        const QStringList candidates = {"ukui-terminal", "mate-terminal",
                                        "gnome-terminal", "xterm"};
        for (const QString &cmd : candidates) {
            if (QStandardPaths::findExecutable(cmd).isEmpty())
                continue;
            QProcess::startDetached(cmd, QStringList());
            break;
        }
    });

    m_contextMenu->addSeparator();

    // 设置
    auto *actSettings = m_contextMenu->addAction(
        themedIcon({"preferences-system", "ukui-control-center", "settings"}),
        QString::fromUtf8("系统设置"));
    connect(actSettings, &QAction::triggered, m_menu, &StartMenu::openSystemSettings);

    auto *actAbout = m_contextMenu->addAction(
        themedIcon({"help-about", "ukui-control-center", "computer"}),
        QString::fromUtf8("关于麒麟"));
    connect(actAbout, &QAction::triggered, m_menu, &StartMenu::openAboutKylin);

    auto *actStartSettings = m_contextMenu->addAction(
        themedIcon({"configure", "preferences-desktop", "preferences-system"}),
        QString::fromUtf8("开始菜单设置"));
    connect(actStartSettings, &QAction::triggered, m_menu, &StartMenu::openSettings);

    m_contextMenu->addSeparator();

    auto *powerMenu = m_contextMenu->addMenu(
        themedIcon({"system-shutdown", "system-shutdown-panel", "gtk-quit"}),
        QString::fromUtf8("电源"));
    StartMenuTheme::applySharedMenuStyle(powerMenu);

    auto *actLock = powerMenu->addAction(
        themedIcon({"system-lock-screen", "changes-prevent", "object-locked"}),
        QString::fromUtf8("锁屏"));
    connect(actLock, &QAction::triggered, m_menu, &StartMenu::lockScreen);

    auto *actSuspend = powerMenu->addAction(
        themedIcon({"system-suspend", "media-playback-pause", "appointment-soon"}),
        QString::fromUtf8("睡眠"));
    connect(actSuspend, &QAction::triggered, m_menu, &StartMenu::suspendSystem);

    auto *actHibernate = powerMenu->addAction(
        themedIcon({"system-suspend-hibernate", "weather-clear-night", "system-suspend"}),
        QString::fromUtf8("休眠"));
    connect(actHibernate, &QAction::triggered, m_menu, &StartMenu::hibernateSystem);

    auto *actHybridSleep = powerMenu->addAction(
        themedIcon({"system-suspend-hybrid", "weather-few-clouds-night", "system-suspend"}),
        QString::fromUtf8("混合睡眠"));
    connect(actHybridSleep, &QAction::triggered, m_menu, &StartMenu::hybridSleepSystem);

    auto *actLogout = powerMenu->addAction(
        themedIcon({"system-log-out", "application-exit", "go-previous"}),
        QString::fromUtf8("注销"));
    connect(actLogout, &QAction::triggered, m_menu, &StartMenu::logoutSession);

    powerMenu->addSeparator();

    auto *actReboot = powerMenu->addAction(
        themedIcon({"system-reboot", "view-refresh", "reload"}),
        QString::fromUtf8("重启"));
    connect(actReboot, &QAction::triggered, m_menu, &StartMenu::rebootSystem);

    auto *actPower = powerMenu->addAction(
        themedIcon({"system-shutdown", "system-shutdown-panel", "gtk-quit"}),
        QString::fromUtf8("关机"));
    connect(actPower, &QAction::triggered, m_menu, &StartMenu::poweroffSystem);

    // Cleanup when menu hides
    connect(m_contextMenu, &QMenu::aboutToHide, this, [this] {
        qApp->removeEventFilter(this);
        m_contextMenu = nullptr;
        // Tell StartMenu it can resume auto-closing
        if (m_menu) m_menu->setProperty("contextMenuOpen", false);
    });

    // Tell StartMenu to suppress auto-close while context menu is open
    if (m_menu) m_menu->setProperty("contextMenuOpen", true);

    // Install event filter to catch clicks outside the menu
    qApp->installEventFilter(this);

    // Use popup() instead of exec() — exec()'s X11 pointer grab
    // fails because StartButton has X11BypassWindowManagerHint.
    m_contextMenu->popup(pos);
}

bool StartButton::isPointInMenuOrSubmenus(const QPoint &globalPos) const
{
    if (!m_contextMenu)
        return false;

    if (m_contextMenu->geometry().contains(globalPos))
        return true;

    // Check submenus
    for (QObject *child : m_contextMenu->children()) {
        auto *sub = qobject_cast<QMenu *>(child);
        if (sub && sub->isVisible() && sub->geometry().contains(globalPos))
            return true;
    }
    return false;
}

bool StartButton::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_contextMenu)
        return false;

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!isPointInMenuOrSubmenus(me->globalPos())) {
            m_contextMenu->close();
            // Don't consume the event — let it pass through to whatever
            // window is underneath, so the desktop/panel gets a proper
            // repaint trigger.
        }
    } else if (event->type() == static_cast<QEvent::Type>(6)) { // QEvent::KeyPress (avoid X11 KeyPress macro clash)
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            m_contextMenu->close();
            return true;
        }
    }

    return false;  // don't consume
}

void StartButton::openTerminalAsAdmin()
{
    // 在终端内进入 root shell，比 pkexec 直接拉起图形终端更兼容。
    const QList<QPair<QString, QStringList>> commands = {
        {"ukui-terminal", QStringList() << "--" << "bash" << "-lc" << "sudo -i"},
        {"mate-terminal", QStringList() << "--" << "bash" << "-lc" << "sudo -i"},
        {"gnome-terminal", QStringList() << "--" << "bash" << "-lc" << "sudo -i"},
        {"xterm", QStringList() << "-e" << "sudo" << "-i"},
    };
    for (const auto &cmd : commands) {
        if (QStandardPaths::findExecutable(cmd.first).isEmpty())
            continue;
        if (QProcess::startDetached(cmd.first, cmd.second))
            return;
    }

    if (QStandardPaths::findExecutable("pkexec").isEmpty()) {
        qWarning() << "[StartButton] No terminal found";
        return;
    }

    QProcess::startDetached("pkexec", QStringList() << "bash" << "-lc" << "exec bash");
}
