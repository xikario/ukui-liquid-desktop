#pragma once
#include "DeskletModels.h"
#include <QWidget>
#include <QTimer>
#include <QImage>
#include <functional>
#include <memory>
class DesktopCanvas;
class LiquidOpticsRenderer;
class ActivityRecorder;
class QPushButton;
class QSpinBox;
class QPainter;
namespace LiquidMaterial { class Preparation; }

// Desktop desklets use this one cached material, placement, menu and persistence layer.
class LiquidDesklet : public QWidget {
    Q_OBJECT
public:
    LiquidDesklet(DesktopCanvas *canvas, const QString &key, const QString &title, QSize size);
    ~LiquidDesklet() override;
    static bool autoStartEnabled(const QString &key);
    static void setAutoStart(const QString &key, bool on);
    void reveal();
    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }
    void savePlacement();
    int materialBuilds() const { return m_materialBuilds; }
    QImage material() const { return m_material; }
    // Text colours resolved against the baked material: >= 7:1 for primary
    // text and >= 4.5:1 for secondary text over every covered cell.
    QColor inkColor() const { return m_ink; }
    QColor mutedColor() const { return m_muted; }
    QVector<QColor> materialCells() const;
public slots:
    void invalidateMaterial();
protected:
    bool eventFilter(QObject *, QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void showEvent(QShowEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    virtual void paintContent(QPainter &p) = 0;
    virtual void arrangeControls() = 0;
    virtual void extendMenu(class QMenu &) {}
    QPushButton *button(const QString &text, const QString &name);
    void text(QPainter &p, const QRectF &rect, const QString &value, int size,
              const QColor &color, bool bold = false, int align = Qt::AlignCenter) const;
    DesktopCanvas *m_canvas;
    QString m_key, m_title;
private:
    void rebuildMaterial();
    void constrainToCanvas();
    QPoint boundedPosition(const QPoint &position, bool snap) const;
    std::unique_ptr<LiquidOpticsRenderer> m_optics;
    LiquidMaterial::Preparation *m_preparation;
    QImage m_material;
    QTimer m_materialTimer, m_saveTimer;
    bool m_editMode = false;
    bool m_drag = false, m_resize = false, m_ready = false, m_materialDirty = true;
    bool m_materialPending = false;
    QPoint m_pressGlobal, m_startPos;
    QSize m_startSize;
    int m_materialBuilds = 0;
    QColor m_ink{"#f4f7ff"}, m_muted{"#b8c6d9"};
};
class ClockDesklet final : public LiquidDesklet {
    Q_OBJECT
public:
    explicit ClockDesklet(DesktopCanvas *canvas);
    ~ClockDesklet() override;
    const CountdownState &countdown() const { return m_countdown; }
    int faceMaterialBuilds() const { return m_faceBuilds; }
    int tickInterval() const { return m_tick.isActive() ? m_tick.interval() : 0; }
    QImage faceMaterial() const { return m_faceMaterial; }
public slots:
    void tick();
protected:
    void paintContent(QPainter &p) override;
    void arrangeControls() override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    void persist();
    void updateControls();
    void syncTick();
    void paintGlassFace(QPainter &p, const QPointF &center, qreal radius);
    std::unique_ptr<LiquidOpticsRenderer> m_faceOptics;
    QImage m_faceMaterial;
    QRect m_faceRect;
    qint64 m_faceSourceKey = 0;
    int m_faceBuilds = 0;
    bool m_timerPage = false;
    bool m_faceLight = false;
    CountdownState m_countdown;
    QTimer m_tick;
    QPushButton *m_clockTab = nullptr, *m_timerTab = nullptr;
    QPushButton *m_start = nullptr, *m_cancel = nullptr;
    QList<QPushButton *> m_presets;
    QSpinBox *m_minutes = nullptr;
};
class ActivityDesklet final : public LiquidDesklet {
    Q_OBJECT
public:
    ActivityDesklet(DesktopCanvas *canvas, ActivityRecorder *recorder);
protected:
    void paintContent(QPainter &p) override;
    void arrangeControls() override;
    void extendMenu(QMenu &menu) override;
    void mouseMoveEvent(QMouseEvent *) override;
private:
    ActivityRecorder *m_recorder;
    QPushButton *m_pause = nullptr;
    QList<QPair<QRectF, QDate>> m_cells;
};
