#pragma once
#include <QApplication>
#include <QImage>
#include <QElapsedTimer>
#include <QMenu>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QStyle>
#include <functional>

namespace LiquidPopup {
enum class Placement { Auto, Above, Below, Left, Right };
// All distances are logical pixels; supplied images retain their DPR.
struct Theme {
    qreal radius = 16;
    qreal tint = .42;
    qreal refraction = 3.5;
    qreal highlight = .35;
    int openMs = 150;
    int closeMs = 140;
    bool reducedMotion = false;
};
Theme &theme();
// Shared motion tokens. Durations scale with the distance still to travel so
// interrupted transitions never restart at full length.
namespace Motion {
constexpr int Fast = 135;    // small state changes: reorder, press feedback
constexpr int Normal = 200;  // panels, overlays, refresh feedback
constexpr int Slow = 360;    // large geometry such as expanding a fence
// 0 when reduced motion is on; otherwise full*remaining, at least floorMs.
int duration(int fullMs, qreal remaining = 1, int floorMs = 1);
}
using BackdropProvider = std::function<QImage(const QRect &, qreal)>;
void setBackdropProvider(BackdropProvider provider);
QImage captureBackdrop(const QRect &globalArea, qreal dpr);
// Skin the native combo/spin buttons while retaining their input handling.
void installControls(QWidget *form);
void installComboPopups(QWidget *form, BackdropProvider provider = {});
struct MaterialCacheStats { int bytes; quint64 builds; quint64 hits; };
MaterialCacheStats materialCacheStats();
void clearMaterialCache();
QImage renderMaterial(const QImage &backdrop, QSize logicalSize, qreal dpr,
                      bool light = false, QRectF body = QRectF());
// Cached alpha silhouette and rim; does not rely on aliased painter clips.
QImage renderMenuMaterial(const QImage &backdrop, QSize logicalSize, qreal dpr,
                          bool light = false);
QRect place(QSize size, const QRect &anchor, const QRect &available);
QPainterPath bubblePath(QRectF body, qreal radius, qreal connectorX = -1,
                        bool connectorAtTop = false);
QPainterPath bubblePath(QRectF body, qreal radius, qreal connector,
                        Placement side);

// Install once per application: native QMenu behaviour remains owned by Qt.
// Context menus receive material + fade, tooltips receive the bubble shell.
void install(QApplication &app);
// Standalone Qt hosts opt into the same vector menu glyphs as the panel style.
void installMenuGlyphStyle(QApplication &app);
// Per-process runtime switch. Restores native menu styling when disabled.
void setEnabled(bool enabled);
bool isEnabled();
// Used by host styles for crisp menu symbols at fractional display scales.
bool drawMenuGlyph(QStyle::PrimitiveElement element, const QStyleOption *option,
                   QPainter *painter);
// Reserve an async action's possible labels before opening, using the final
// menu style. Text updates then keep the native popup geometry unchanged.
void reserveActionTextWidth(QMenu &menu, QAction &action, const QStringList &texts);
// Center button menus on the trigger and flip upward when space is short.
QAction *execAt(QMenu &menu, const QRect &globalAnchor);
QAction *execAt(QMenu &menu, QWidget *anchor);
void showText(const QPoint &globalPos, const QString &text, QWidget *owner = nullptr,
              const QRect &ownerRect = QRect(), int duration = -1);
void hideText();

class Shell : public QWidget {
public:
    explicit Shell(QWidget *parent = nullptr, bool tooltip = false);
    void setContent(QWidget *content);
    void openAt(const QRect &globalAnchor, Placement placement = Placement::Auto);
    // Follow a moved anchor or new bubble size while open, keeping the
    // requested side attached; no capture or motion restart.
    void reanchor(const QRect &globalAnchor, QSize bubbleSize = QSize());
    // Sample the screen for a later openAt while nothing transient (fading
    // tooltips) covers it. openAt reuses a fresh sample for the same area.
    void prime(const QRect &globalAnchor, Placement placement = Placement::Auto);
    void dismiss();
    qreal progress() const { return m_progress; }
    bool isClosing() const { return m_closing; }
protected:
    void paintEvent(QPaintEvent *) override;
    void hideEvent(QHideEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QRect placeFor(QSize size, const QRect &anchor, Placement placement, Placement *chosen) const;
    void applyPlacement(const QRect &area, const QRect &anchor, Placement chosen);
    void renderBubble(const QRect &area);
    void updateContent();
    QWidget *m_content = nullptr;
    QVariantAnimation m_motion;
    QImage m_material;
    qreal m_progress = 0;
    qreal m_connectorX = -1;
    qreal m_connectorY = -1;
    bool m_top = false;
    Placement m_placement = Placement::Auto;
    Placement m_requested = Placement::Auto;
    QRect m_anchor;
    QImage m_backdrop;
    QRect m_backdropArea;
    QImage m_primed;
    QRect m_primedArea;
    QElapsedTimer m_primedAge;
    bool m_tooltip = false;
    bool m_closing = false;
};
}
