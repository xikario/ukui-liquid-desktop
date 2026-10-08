#pragma once

#include <QApplication>
#include <QMouseEvent>
#include <QCursor>
#include <QElapsedTimer>
#include <QPainterPathStroker>
#include <QPointer>
#include <QRegion>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <cmath>

// Track native mouse events, including events delivered to child controls.
// A frame consumes the latest position; returning true keeps a finite fade
// running. There is no cursor polling while the pointer/effect is settled.
class PointerEffect final : public QObject {
public:
    using Frame = std::function<bool(const QPoint &, bool)>;
    PointerEffect(QWidget *host, Frame frame, int frameInterval = 33)
        : QObject(host), m_host(host), m_frame(std::move(frame)) {
        setObjectName(QStringLiteral("pointerEffect"));
        m_timer.setObjectName(QStringLiteral("pointerEffectFrame"));
        m_timer.setParent(this);
        m_timer.setSingleShot(true);
        m_timer.setInterval(frameInterval);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (m_host && m_frame(m_position, inside()) && m_host->isVisible())
                m_timer.start();
        });
        qApp->installEventFilter(this);
        trackChildren();
    }
    void setEnabled(bool enabled) {
        m_enabled = enabled;
        refresh();
    }
    void stop() {
        m_enabled = false;
        m_timer.stop();
    }
    void refresh(bool hitTest = true) {
        if (!m_host) return;
        if (hitTest) m_present = m_host->underMouse();
        m_position = m_host->mapFromGlobal(QCursor::pos());
        if (!m_enabled || !m_host->isVisible()) {
            m_timer.stop();
            m_frame(m_position, false);
        } else if (!m_timer.isActive()) {
            m_timer.start();
        }
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *widget = qobject_cast<QWidget *>(object);
        if (!m_host || !widget) return false;
        const bool descendant = widget == m_host || m_host->isAncestorOf(widget);
        if (descendant && event->type() == QEvent::ChildAdded && !m_trackingPending) {
            m_trackingPending = true;
            QTimer::singleShot(0, this, [this] {
                m_trackingPending = false;
                trackChildren();
            });
        }
        if (widget == m_host && (event->type() == QEvent::Show
                                || event->type() == QEvent::Hide)) {
            refresh();
        } else if (event->type() == QEvent::MouseMove
                   && m_enabled && (descendant || inside())) {
            const QPoint position = m_host->mapFromGlobal(
                static_cast<QMouseEvent *>(event)->globalPos());
            // Ignored mouse moves propagate to the canvas after delivery to a
            // child. That second delivery is still over this host, not a leave.
            const bool propagated = m_present && widget->isAncestorOf(m_host)
                && widget->window() == m_host->window()
                && m_host->rect().contains(position)
                && (m_host->mask().isEmpty() || m_host->mask().contains(position));
            const bool present = propagated
                || (descendant && widget->window() == m_host->window());
            if (position != m_position || present != m_present) {
                m_position = position;
                m_present = present;
                if (m_host->isVisible() && !m_timer.isActive()) m_timer.start();
            }
        } else if (m_enabled && widget == m_host && (event->type() == QEvent::Enter
                                       || event->type() == QEvent::Leave)) {
            m_present = event->type() == QEvent::Enter;
            refresh(false);
        }
        return false;
    }
private:
    bool inside() const {
        return m_enabled && m_present && m_host && m_host->isVisible() && m_host->rect().contains(m_position)
            && (m_host->mask().isEmpty() || m_host->mask().contains(m_position));
    }
    void trackChildren() {
        if (!m_host) return;
        m_host->setMouseTracking(true);
        for (auto *child : m_host->findChildren<QWidget *>())
            child->setMouseTracking(true);
    }
    QPointer<QWidget> m_host;
    Frame m_frame;
    QTimer m_timer;
    QPoint m_position {-1000, -1000};
    bool m_trackingPending = false;
    bool m_enabled = true;
    bool m_present = false;
};

// Frame-rate independent exponential approach: the same wall-clock fade at
// 30, 60 or 120 Hz and after a dropped frame. Snaps once visually settled.
inline qreal smoothToward(qreal current, qreal target, qreal seconds, qreal tau = 0.042) {
    const qreal next = target - (target - current) * std::exp(-qMax<qreal>(0, seconds) / tau);
    return qAbs(target - next) <= 1e-4 ? target : next;
}
// Wall-clock step for a pointer frame; a fade starting from rest counts as one
// nominal frame so the first step is never a jump.
inline qreal frameSeconds(QElapsedTimer &clock, int nominalMs = 33) {
    const qint64 elapsed = clock.isValid() ? clock.restart() : (clock.start(), nominalMs);
    return (elapsed > 4 * nominalMs ? nominalMs : elapsed) / 1000.0;
}

// Conservative coverage of a gradient stroked along a curved rim. The wider
// support includes antialiasing at fractional DPI and magnetic fence contours.
inline QRegion pointerRimDamage(const QPainterPath &rim, const QPointF &pointer,
                                qreal radius, qreal strokeSupport = 14) {
    QPainterPathStroker stroker;
    stroker.setWidth(strokeSupport);
    const QRegion band(stroker.createStroke(rim).toFillPolygon().toPolygon(),
                       Qt::WindingFill);
    const QRect footprint = QRectF(pointer.x() - radius - 2,
                                   pointer.y() - radius - 2,
                                   2 * radius + 4, 2 * radius + 4).toAlignedRect();
    return band.intersected(footprint);
}
