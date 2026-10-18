#include "PanelLayoutCompat.h"
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLayout>
#include <QMetaMethod>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QDebug>
#include <QVariantAnimation>
#include <QThread>
#include <QAbstractScrollArea>
#include <array>
#include <dlfcn.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace {
QList<PanelLayoutCompat *> adapters;
using TaskbarRealign = void (*)(QWidget *);
TaskbarRealign taskbarRealign(QWidget *bar) {
    // Resolve the public method from the already loaded plugin that owns its
    // meta-object strings. No library path, CPU assumption or private offset.
    Dl_info info{};
    if (!dladdr(bar->metaObject()->className(), &info) || !info.dli_fname) return nullptr;
    void *handle = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) return nullptr;
    auto method = reinterpret_cast<TaskbarRealign>(dlsym(handle, "_ZN11UKUITaskBar7realignEv"));
    dlclose(handle);
    return method;
}
QString nativeConfig() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/ukui/panel.conf";
}
}
PanelLayoutCompat::PanelLayoutCompat(QWidget *panel, const QString &preferences, QObject *parent)
    : QObject(parent), m_panel(panel), m_preferences(preferences) {
    setObjectName("liquidPanelLayoutCompat");
    // Public exported getter, resolved by capability rather than CPU/OEM version.
    // Never inspect the host's private object layout or animation pointer.
    m_animationRunning = reinterpret_cast<AnimationRunning>(dlsym(RTLD_DEFAULT,
        "_ZNK9UKUIPanel16animationRunningEv"));
    adapters.append(this);
    if (adapters.size() == 1)
        qRegisterAnimationInterpolator<QRect>(&PanelLayoutCompat::interpolateRect);
    const QString prefix = QStringLiteral("UKUIPanel ");
    if (panel->objectName().startsWith(prefix)) m_name = panel->objectName().mid(prefix.size());
    if (m_name.contains('/') || m_name.contains(QChar(92))) m_name.clear();
    QSettings saved(m_preferences, QSettings::IniFormat);
    m_enabled = saved.value(key("centered"), false).toBool();
    m_percent = qBound(30, saved.value(key("centeredLength"), 68).toInt(), 95);
    m_originalMinimum = panel->minimumSize();
    m_originalMaximum = panel->maximumSize();
    qApp->installEventFilter(this);
    for (QScreen *s : QGuiApplication::screens())
        connect(s, &QScreen::geometryChanged, this, [this] {
            // A mode switch publishes a new native strut. Do not carry the
            // previous screen's right edge into the next repair.
            m_strutRangeCaptured = false;
            schedule(); emit changed();
        });
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *s) {
        connect(s, &QScreen::geometryChanged, this, [this] {
            m_strutRangeCaptured = false;
            schedule(); emit changed();
        });
        m_strutRangeCaptured = false;
        schedule(); emit changed();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        m_strutRangeCaptured = false;
        schedule(); emit changed();
    });
    if (panel->windowHandle())
        connect(panel->windowHandle(), &QWindow::screenChanged, this, [this] {
            m_strutRangeCaptured = false;
            schedule(); emit changed();
        });
    schedule();
}
PanelLayoutCompat::~PanelLayoutCompat() {
    adapters.removeAll(this);
    if (adapters.isEmpty()) qRegisterAnimationInterpolator<QRect>(nullptr);
}
QVariant PanelLayoutCompat::interpolateRect(const QRect &from, const QRect &to, qreal progress) {
    auto coordinate = [progress](int a, int b) { return int(a + (b - a) * progress); };
    QRect value;
    value.setCoords(coordinate(from.left(), to.left()), coordinate(from.top(), to.top()),
                    coordinate(from.right(), to.right()), coordinate(from.bottom(), to.bottom()));
    if (!qApp || QThread::currentThread() != qApp->thread()) return value;
    for (auto *adapter : adapters) {
        const QRect adjusted = adapter->animationRect(from, to, progress, value);
        if (adjusted != value) return adjusted;
    }
    return value;
}
QRect PanelLayoutCompat::animationRect(const QRect &from, const QRect &to, qreal, const QRect &value) const {
    if (!m_enabled || !m_panel || backend() != Backend::Widget || !m_animationRunning
        || !m_animationRunning(m_panel)) return value;
    const QRect bounds = screen()->geometry();
    const int width = qMin(bounds.width(), qMax(bounds.width() * m_percent / 100, minimumLength()));
    const int left = bounds.x() + (bounds.width() - width) / 2;
    const int height = m_panel->height();
    auto endpoint = [&](const QRect &r) {
        return r.height() == height && r.y() >= bounds.bottom() - height + 1
            && r.y() <= bounds.bottom() + 1
            && ((r.x() == bounds.x() && r.width() == bounds.width())
                || (r.x() == left && r.width() == width));
    };
    if (!endpoint(from) || !endpoint(to) || from.y() == to.y()) return value;
    return QRect(left, value.y(), width, value.height());
}
QString PanelLayoutCompat::key(const QString &suffix) const {
    return "layout/" + m_name + "/" + suffix;
}
QScreen *PanelLayoutCompat::screen() const {
    if (!m_panel) return nullptr;
    // A hidden panel's centre may be outside every screen. The QWindow retains
    // its assigned screen during the native hide animation.
    if (m_panel->windowHandle() && m_panel->windowHandle()->screen())
        return m_panel->windowHandle()->screen();
    return QGuiApplication::screenAt(m_panel->geometry().center());
}
bool PanelLayoutCompat::nativeAvailable() const {
    if (!m_panel) return false;
    const auto *meta = m_panel->metaObject();
    return meta->indexOfMethod("setAlignment(UKUIPanel::Alignment,bool)") >= 0
        && meta->indexOfMethod("setLength(int,bool,bool)") >= 0;
}
PanelLayoutCompat::Backend PanelLayoutCompat::backend() const {
    if (!m_panel || m_name.isEmpty()) return Backend::Unsupported;
    if (nativeAvailable()) return Backend::Native;
    QSettings native(nativeConfig(), QSettings::IniFormat);
    const QString position = native.value(m_name + "/position", "Bottom").toString();
    if (QGuiApplication::platformName() == "xcb" && QGuiApplication::screens().size() == 1
        && position == "Bottom" && screen()
        && m_panel->metaObject()->indexOfMethod("realign()") >= 0) return Backend::Widget;
    return Backend::Unsupported;
}
bool PanelLayoutCompat::centered() const {
    if (nativeAvailable()) {
        QSettings native(nativeConfig(), QSettings::IniFormat);
        return native.value(m_name + "/alignment", 0).toInt() == 0
            && length() < 100;
    }
    return m_enabled;
}
int PanelLayoutCompat::length() const {
    if (nativeAvailable()) {
        QSettings native(nativeConfig(), QSettings::IniFormat);
        const int value = native.value(m_name + "/width", 100).toInt();
        const bool percent = native.value(m_name + "/width-percent", true).toBool();
        const bool vertical = native.value(m_name + "/position").toString() == "Left"
            || native.value(m_name + "/position").toString() == "Right";
        QScreen *s = screen();
        const int extent = s ? (vertical ? s->geometry().height() : s->geometry().width()) : 1;
        return percent ? value : qRound(100. * value / qMax(1, extent));
    }
    return m_percent;
}
QString PanelLayoutCompat::explanation() const {
    if (backend() == Backend::Native)
        return QStringLiteral("由系统原生接口管理布局和预留区域。关闭居中恢复全宽。");
    if (backend() == Backend::Widget)
        return QStringLiteral("使用兼容布局（单屏底部任务栏）；保留系统自动隐藏及整屏底边预留。固定控件较多时可能扩大实际长度。关闭液态主题仍保留布局，关闭居中恢复全宽。");
    return QStringLiteral("当前布局不在兼容支持范围：缺少原生接口时仅支持 X11 单屏底部任务栏。");
}
bool PanelLayoutCompat::applyNative(bool enabled, int percent) {
    if (!m_panel) return false;
    const auto *meta = m_panel->metaObject();
    const int center = 0, value = enabled ? percent : 100;
    const auto align = meta->method(meta->indexOfMethod("setAlignment(UKUIPanel::Alignment,bool)"));
    const auto length = meta->method(meta->indexOfMethod("setLength(int,bool,bool)"));
    return align.invoke(m_panel, Qt::DirectConnection,
               QGenericArgument("UKUIPanel::Alignment", &center), Q_ARG(bool, false))
        && length.invoke(m_panel, Qt::DirectConnection,
               Q_ARG(int, value), Q_ARG(bool, true), Q_ARG(bool, true));
}
bool PanelLayoutCompat::setCentered(bool enabled, int percent) {
    if (!supported()) return false;
    percent = qBound(30, percent, 95);
    if (backend() == Backend::Native && !applyNative(enabled, percent)) return false;
    m_enabled = enabled; m_percent = percent;
    QSettings saved(m_preferences, QSettings::IniFormat);
    saved.setValue(key("centered"), enabled);
    saved.setValue(key("centeredLength"), percent); saved.sync();
    schedule(); emit changed();
    return saved.status() == QSettings::NoError;
}
void PanelLayoutCompat::schedule() {
    if (m_pending || m_applying || !m_panel) return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] { m_pending = false; apply(); });
}
int PanelLayoutCompat::minimumLength() const {
    if (!m_panel) return 0;
    auto *background = m_panel->findChild<QWidget *>("BackgroundWidget");
    if (!background) return 0;
    int width = 0;
    const auto plugins = background->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (auto *plugin : plugins) {
        if (!plugin->isVisible()) continue;
        bool taskbar = false;
        for (auto *child : plugin->findChildren<QAbstractScrollArea *>())
            if (child->inherits("UKUITaskBar")) taskbar = true;
        width += taskbar ? m_panel->height() + 32
            : qMax(plugin->minimumSizeHint().width(), plugin->sizeHint().width());
    }
    return qMax(0, width);
}
void PanelLayoutCompat::fitTaskbar() {
    for (auto *bar : m_panel->findChildren<QAbstractScrollArea *>()) {
        if (!bar->inherits("UKUITaskBar") || !bar->parentWidget()) continue;
        auto *container = bar->parentWidget();
        auto *pages = container->findChild<QWidget *>("TaskBar_KButtonBox");
        const auto realign = taskbarRealign(bar);
        if (!pages || !realign) continue;
        bool saved = false;
        for (const auto &c : m_taskbars) if (c.widget == bar) saved = true;
        if (!saved) m_taskbars.append({bar, bar->minimumSize(), bar->maximumSize()});
        if (container->layout()) container->layout()->activate();
        const int available = qMax(1, container->width() - (pages->isVisible() ? pages->width() : 0));
        if (bar->width() == available && bar->minimumWidth() == available) continue;
        bar->setFixedWidth(available);
        realign(bar);
        if (container->layout()) container->layout()->activate();
        const int finalWidth = qMax(1, container->width() - (pages->isVisible() ? pages->width() : 0));
        if (bar->width() != finalWidth) {
            bar->setFixedWidth(finalWidth);
            realign(bar);
        }
    }
}
void PanelLayoutCompat::scheduleStrutRepair() {
    if (m_strutRepairPending || !m_panel || !m_enabled || backend() != Backend::Widget) return;
    m_strutRepairPending = true;
    QTimer::singleShot(0, this, [this] {
        m_strutRepairPending = false;
        repairStrut();
    });
}
void PanelLayoutCompat::captureStrutRange() {
    if (m_strutRangeCaptured || !m_panel
        || QGuiApplication::platformName() != "xcb") return;

    // The OEM panel can rewrite _NET_WM_STRUT_PARTIAL after a mode change.
    // Reading the property here could therefore capture the old screen's
    // range, depending on which zero-timeout runs first. This backend only
    // supports one bottom panel on one screen, so the authoritative range is
    // the current screen geometry itself.
    QScreen *current = screen();
    if (!current || !current->geometry().isValid()) return;
    const QRect bounds = current->geometry();
    m_strutStart = static_cast<unsigned long>(qMax(0, bounds.left()));
    m_strutEnd = static_cast<unsigned long>(qMax(0, bounds.right()));
    m_strutRangeCaptured = true;
}
void PanelLayoutCompat::repairStrut() {
    if (!m_panel || !m_enabled || backend() != Backend::Widget
        || QGuiApplication::platformName() != "xcb") return;
    if (!m_panel->winId()) return;
    captureStrutRange();
    if (!m_strutRangeCaptured) return;
    Display *display = XOpenDisplay(nullptr);
    if (!display) return;
    const Window window = static_cast<Window>(m_panel->winId());
    const Atom property = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
    const Atom cardinal = XInternAtom(display, "CARDINAL", False);
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long count = 0, bytesAfter = 0;
    unsigned char *raw = nullptr;
    if (XGetWindowProperty(display, window, property, 0, 12, False, cardinal,
                           &actualType, &actualFormat, &count, &bytesAfter, &raw) == Success
        && actualType == cardinal && actualFormat == 32 && count >= 12) {
        std::array<unsigned long, 12> values{};
        for (size_t i = 0; i < values.size(); ++i)
            values[i] = reinterpret_cast<unsigned long *>(raw)[i];
        const unsigned long start = m_strutStart;
        const unsigned long end = m_strutEnd;
        if (values[10] != start || values[11] != end) {
            values[10] = start;
            values[11] = end;
            XChangeProperty(display, window, property, cardinal, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char *>(values.data()), 12);
            XFlush(display);
        }
    }
    if (raw) XFree(raw);
    XCloseDisplay(display);
}
void PanelLayoutCompat::restore() {
    if (!m_panel || !m_constrained) return;
    m_applying = true;
    m_panel->setMinimumWidth(m_originalMinimum.width());
    m_panel->setMaximumWidth(m_originalMaximum.width());
    m_constrained = false;
    for (const auto &c : m_taskbars) if (c.widget) {
        c.widget->setMinimumSize(c.minimum); c.widget->setMaximumSize(c.maximum);
    }
    m_taskbars.clear();
    QMetaObject::invokeMethod(m_panel, "realign", Qt::DirectConnection);
    m_applying = false;
}
void PanelLayoutCompat::apply() {
    if (!m_panel) return;
    ++m_batches;
    const auto selected = backend();
    m_panel->setProperty("liquidPanelLayoutBackend", selected == Backend::Native ? "native"
        : selected == Backend::Widget ? "widget" : "unsupported");
    m_panel->setProperty("liquidPanelLayoutBatches", QVariant::fromValue(m_batches));
    if (selected != Backend::Widget || !m_enabled) { restore(); return; }
    const QRect bounds = screen()->geometry();
    const int width = qMin(bounds.width(), qMax(bounds.width() * m_percent / 100, minimumLength()));
    int y = m_panel->y();
    // During startup the OEM panel can emit its first resize while still at
    // (0, 0). A visible bottom panel has a deterministic cross-axis target;
    // preserve the native y only for hide/reveal geometry and animations.
    if (m_firstLayout && m_panel->isVisible() && m_panel->height() > 4
        && (!m_animationRunning || !m_animationRunning(m_panel)))
        y = bounds.bottom() - m_panel->height() + 1;
    const QRect target(bounds.x() + (bounds.width() - width) / 2, y, width, m_panel->height());
    if (m_panel->geometry() == target) {
        m_firstLayout = false;
        m_applying = true; fitTaskbar(); m_applying = false;
        scheduleStrutRepair();
        return;
    }
    m_applying = true;
    m_constrained = true;
    m_panel->setFixedWidth(width);
    m_panel->move(target.topLeft());
    if (m_panel->layout()) m_panel->layout()->activate();
    fitTaskbar();
    m_applying = false;
    m_panel->setProperty("liquidPanelLayoutCorrections", QVariant::fromValue(++m_corrections));
    scheduleStrutRepair();
    m_firstLayout = false;
    if (qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_LAYOUT_TRACE"))
        qInfo() << "[PanelLayoutCompat]" << m_name << m_panel->geometry()
                << "batch" << m_batches << "correction" << m_corrections;
}
bool PanelLayoutCompat::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || !m_panel || m_applying) return false;
    if (widget != m_panel && !m_panel->isAncestorOf(widget)) return false;
    if (event->type() == QEvent::MouseButtonPress) m_immediateReady = true;
    switch (event->type()) {
    case QEvent::Move: case QEvent::Resize:
        // The OEM realign path briefly restores its configured full-width
        // rectangle before returning. Apply the compatibility rectangle while
        // that geometry event is being delivered so the transient full-width
        // frame never reaches the compositor. Other descendants still use the
        // coalesced path below, and hide/reveal animation geometry is left to
        // the registered QRect interpolator.
        if (widget == m_panel && m_enabled && backend() == Backend::Widget
            && m_panel->isVisible() && m_immediateReady && !m_firstLayout
            && (!m_animationRunning || !m_animationRunning(m_panel))) {
            const QRect bounds = screen() ? screen()->geometry() : QRect();
            const int width = bounds.isValid()
                ? qMin(bounds.width(), qMax(bounds.width() * m_percent / 100, minimumLength())) : 0;
            const int left = bounds.x() + (bounds.width() - width) / 2;
            const int visibleEdge = bounds.isValid()
                ? bounds.bottom() - m_panel->height() + 1 : 0;
            const bool atVisibleEdge = bounds.isValid() && m_panel->y() == visibleEdge;
            if (width > 0 && atVisibleEdge
                && (m_panel->width() != width || m_panel->x() != left)) {
                apply();
                scheduleStrutRepair();
                break;
            }
        }
        schedule(); break;
    case QEvent::Show:
    case QEvent::LayoutRequest: case QEvent::ScreenChangeInternal:
        schedule(); break;
    default: break;
    }
    return false;
}
