#pragma once
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QRect>
#include <QVariant>
class QWidget;
class QScreen;

// One adapter per native panel. CPU architecture is deliberately irrelevant.
class PanelLayoutCompat final : public QObject {
    Q_OBJECT
public:
    enum class Backend { Unsupported, Native, Widget };
    PanelLayoutCompat(QWidget *panel, const QString &preferences, QObject *parent = nullptr);
    ~PanelLayoutCompat() override;
    Backend backend() const;
    bool supported() const { return backend() != Backend::Unsupported; }
    bool centered() const;
    int length() const;
    QString explanation() const;
    Q_INVOKABLE bool setCentered(bool enabled, int percent);
    Q_INVOKABLE void schedule();
signals:
    void changed();
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    QString key(const QString &) const;
    QScreen *screen() const;
    bool nativeAvailable() const;
    bool applyNative(bool enabled, int percent);
    int minimumLength() const;
    void apply();
    void restore();
    void fitTaskbar();
    void captureStrutRange();
    void scheduleStrutRepair();
    void repairStrut();
    struct Constraint { QPointer<QWidget> widget; QSize minimum, maximum; };
    QList<Constraint> m_taskbars;
    using AnimationRunning = bool (*)(const QWidget *);
    AnimationRunning m_animationRunning = nullptr;
    QRect animationRect(const QRect &, const QRect &, qreal, const QRect &) const;
    static QVariant interpolateRect(const QRect &, const QRect &, qreal);
    QPointer<QWidget> m_panel;
    QString m_preferences, m_name;
    QSize m_originalMinimum, m_originalMaximum;
    bool m_enabled = false, m_pending = false, m_applying = false, m_constrained = false;
    bool m_strutRepairPending = false;
    bool m_strutRangeCaptured = false;
    unsigned long m_strutStart = 0, m_strutEnd = 0;
    bool m_firstLayout = true, m_immediateReady = false;
    int m_percent = 68;
    quint64 m_batches = 0, m_corrections = 0;
};
