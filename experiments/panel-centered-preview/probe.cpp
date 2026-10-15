// Experimental OEM panel layout probe, separate from the production plugin.
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QPointer>
#include <QProxyStyle>
#include <QScreen>
#include <QStyleFactory>
#include <QStylePlugin>
#include <QTimer>
#include <QWidget>

class CenterProbe : public QObject {
public:
    explicit CenterProbe(QObject *parent) : QObject(parent) {
        qApp->installEventFilter(this);
        QTimer::singleShot(0, this, [this] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (widget->inherits("UKUIPanel")) {
                    m_panel = widget;
                    apply();
                }
            }
        });
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || !widget->inherits("UKUIPanel")) return false;
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::LayoutRequest:
            m_panel = widget;
            if (!m_applying && !m_pending) {
                m_pending = true;
                QTimer::singleShot(0, this, [this] { apply(); });
            }
            break;
        default:
            break;
        }
        return false;
    }
private:
    void apply() {
        m_pending = false;
        if (!m_panel) return;
        auto *screen = QGuiApplication::screenAt(m_panel->mapToGlobal(m_panel->rect().center()));
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        const QRect bounds = screen->geometry();
        const int width = bounds.width() * 68 / 100;
        const QRect target(bounds.x() + (bounds.width() - width) / 2,
                           m_panel->y(), width, m_panel->height());
        if (m_panel->geometry() == target) return;
        m_applying = true;
        m_panel->setFixedWidth(width);
        m_panel->move(target.topLeft());
        m_applying = false;
        qInfo() << "[CenterPreview]" << m_panel->geometry()
                << "adjustments" << ++m_corrections;
    }
    QPointer<QWidget> m_panel;
    bool m_pending = false;
    bool m_applying = false;
    int m_corrections = 0;
};

class CenterProbeStyle : public QProxyStyle {
public:
    CenterProbeStyle() : QProxyStyle(QStyleFactory::create("ukuiliquidpanel")) {
        setObjectName("centerprobe");
        QTimer::singleShot(0, qApp, [] { new CenterProbe(qApp); });
    }
};

// Avoid Plugin: the OEM executable exports a class with that symbol.
class CenterProbeStylePlugin : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "probe.json")
public:
    QStyle *create(const QString &key) override {
        return key.compare("centerprobe", Qt::CaseInsensitive) == 0
            ? new CenterProbeStyle : nullptr;
    }
};
#include "probe.moc"
