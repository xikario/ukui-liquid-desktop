#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <memory>
class QWidget;
class TrayOverflow;
class PanelSettings;

// Adapt public Qt widgets; no OEM private field offsets or replacement panel.
class PanelBehavior final : public QObject {
public:
    explicit PanelBehavior(QObject *parent);
    ~PanelBehavior() override;
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    void schedule();
    void reconcile();
    QTimer m_pending;
    std::unique_ptr<PanelSettings> m_settings;
    QSet<TrayOverflow *> m_trays;
    bool m_syncing=false;
};
