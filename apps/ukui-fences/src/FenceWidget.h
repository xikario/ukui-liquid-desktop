#pragma once
#include "FileClipboard.h"

#include <QWidget>
#include <QList>
#include <QSet>
#include <QColor>
#include <QIcon>
#include <QString>
#include <QRect>
#include <QLine>
#include <QPainterPath>
#include <QVector>
#include <QImage>
#include "DesktopItem.h"

class DesktopIcon;
class QKeyEvent;
class QWheelEvent;
class QTimer;
class QPropertyAnimation;

class FenceWidget : public QWidget
{
    Q_OBJECT
    friend class DesktopCanvas;
public:
    explicit FenceWidget(const QString &title,
                         const QRect   &geo,
                         QWidget       *parent = nullptr);

    // 属性读取
    QString fenceId()    const { return m_id; }
    QString title()      const { return m_title; }
    QColor  fenceColor() const { return m_color; }
    int     expandedHeight() const;
    bool    collapsed()  const { return m_collapsed; }
    bool    editMode()   const { return m_editMode; }
    bool    locked()     const { return m_locked; }
    QString titleIconPath() const { return m_titleIconPath; }
    const QList<DesktopIcon*> &icons() const { return m_icons; }

    // 属性设置
    void setTitle(const QString &t);
    void setFenceColor(const QColor &c);
    void setLiquidGlassEnabled(bool enabled);
    bool liquidGlassEnabled() const { return m_liquidGlassEnabled; }
    void invalidateGlassCache();
    void setEditMode(bool edit);
    void setCollapsed(bool c);
    void setLocked(bool locked);
    void setIconScale(qreal scale);
    void setIconFontFamily(const QString &family);
    void setIconFontSize(int size);
    void setIconFontColor(const QColor &color);
    void setIconFontBold(bool bold);
    void setIconFontItalic(bool italic);
    void setTitleIconPath(const QString &path);
    void setTitleTextColor(const QColor &color);
    void setEmbeddedWidget(QWidget *widget);
    bool hasEmbeddedWidget() const { return m_embeddedWidget != nullptr; }
    void refreshMagneticContour();
    QWidget *createFontSettingsPage(QWidget *parent);
    QWidget *createTitleFontSettingsPage(QWidget *parent);
    bool hasLocalFont() const { return m_hasLocalFont; }

    // 图标管理
    void addItem(const DesktopItem &item);
    void removeItem(const QString &filePath);
    bool hasItem(const QString &filePath) const;
    QList<DesktopItem> items() const;

signals:
    void geometryChanged();
    void titleChanged(const QString &newTitle);
    void fileDropped(const QString &filePath);
    void deleteRequested(FenceWidget *self);
    void interactionStarted(FenceWidget *self);
    void undoRequested();
    void filesTrashed(const FileClipboard::PasteResult &result);
    void fileRenamedForUndo(const QString &oldPath, const QString &newPath);
    void filesPasted(const QStringList &sourcePaths,
                     const QStringList &placedPaths,
                     bool move);
    void fileCreated(const QString &path);

protected:
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;
    void paintEvent(QPaintEvent *)            override;
    void mousePressEvent(QMouseEvent *)       override;
    void mouseMoveEvent(QMouseEvent *)        override;
    void mouseReleaseEvent(QMouseEvent *)     override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void dragEnterEvent(QDragEnterEvent *)    override;
    void dragMoveEvent(QDragMoveEvent *)      override;
    void dropEvent(QDropEvent *)              override;
    void keyPressEvent(QKeyEvent *)           override;
    void resizeEvent(QResizeEvent *)          override;
    void wheelEvent(QWheelEvent *)            override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void prepareGlassCache();
    enum class SortMode {
        Name,
        Type,
        ModifiedTime
    };

    // 8方向缩放边缘
    enum Edge {
        None = 0,
        EL = 1, ER = 2, ET = 4, EB = 8,
        ETL = ET | EL, ETR = ET | ER,
        EBL = EB | EL, EBR = EB | ER
    };

    Edge hitEdge(const QPoint &p) const;
    void applyCursor(Edge e);
    void insertItem(const DesktopItem &item, int index);
    int dropInsertionIndex(const QPoint &pos) const;
    bool moveItemsToIndex(const QStringList &paths, int targetIndex);
    void layoutIcons();
    void showRenameDialog();
    void drawHandles(QPainter &p);   // 编辑模式角点手柄
    void drawSnapGuides(QPainter &p); // 吸附对齐辅助线
    void sortItems(SortMode mode);
    void clampScrollOffset();
    int contentTop() const;
    int contentViewportHeight() const;
    int contentRightInset() const;
    int iconColumnCount(int iconW) const;
    bool iconBelongsToThisFence(DesktopIcon *icon) const;
    QRect computeSnappedGeometry(const QRect &proposed) const;
    QRect applyWallpaperMagnet(const QRect &proposed);
    QPainterPath fenceShapePath() const;
    void updateShapeMask();
    int magneticContentInset() const;
    void showFontSettingsDialog();
    void showTitleFontSettingsDialog();
    void applyInheritedIconFont(DesktopIcon *icon);
    void applyLocalIconFont(DesktopIcon *icon);
    void applyEffectiveIconFont(DesktopIcon *icon);
    void setIconSelected(DesktopIcon *icon, bool selected);
    void clearIconSelection();
    void selectAllIcons();
    QStringList selectedFilePaths() const;
    void copySelectedIcons(bool move);
    void pasteClipboardFiles();
    void createNewFile(const QString &baseName, const QString &suffix);
    void createNewDirectory();
    void finishNewItem(const QString &path);
    void trashSelectedIcons();
    void renameFirstSelectedIcon();
    void openSelectedIcons();
    void syncCutVisualState(const QSet<QString> &cutPaths);

    QString  m_id;
    QString  m_title;
    QColor   m_color    { 0, 120, 215, 90 };
    bool     m_editMode  = false;
    bool     m_collapsed = false;
    bool     m_locked    = false;
    int      m_expandedH = 240;
    QPropertyAnimation *m_collapseAnimation = nullptr;
    QPixmap m_collapseSnapshot;
    qreal    m_iconScale = 1.0;
    int      m_scrollOffset = 0;
    int      m_contentHeight = 0;
    QString  m_titleIconPath;
    QIcon    m_titleIcon;
    bool     m_liquidGlassEnabled = false;
    QImage   m_glassImage;
    QRect    m_glassGeometry;
    QPainterPath m_glassShape;
    QTimer  *m_glassHoverTimer = nullptr;
    QPointF  m_glassPointer {-1000, -1000};
    qreal    m_glassHover = 0.0;
    bool     m_glassPointerPresent = false;
    QWidget *m_iconViewport = nullptr;
    QWidget *m_embeddedWidget = nullptr;

    // 分区标题字体配置
    bool     m_hasTitleFont = false;
    QString  m_titleFontFamily;
    int      m_titleFontSize = 13;
    QColor   m_titleFontColor = Qt::white;
    bool     m_titleFontBold = true;
    bool     m_titleFontItalic = false;

    // 父级传入的内部图标字体，供后续拖入/重置使用
    QString  m_inheritedIconFontFamily;
    int      m_inheritedIconFontSize = 11;
    QColor   m_inheritedIconFontColor = Qt::white;
    bool     m_inheritedIconFontBold = false;
    bool     m_inheritedIconFontItalic = false;

    // 分区独立字体配置
    bool     m_hasLocalFont = false;
    QString  m_localFontFamily;
    int      m_localFontSize = 11;
    QColor   m_localFontColor = Qt::white;
    bool     m_localFontBold = false;
    bool     m_localFontItalic = false;

    bool   m_dragging   = false;
    bool   m_resizing   = false;
    Edge   m_resizeEdge = None;
    QPoint m_mouseStart;
    QRect  m_geoStart;

    enum class MagneticEdge {
        None,
        Left,
        Right,
        Top,
        Bottom
    };
    MagneticEdge m_magneticEdge = MagneticEdge::None;
    QVector<QPointF> m_magneticContour;

    QList<DesktopIcon *> m_icons;
    QSet<DesktopIcon *>  m_selectedIcons;

    static constexpr int TITLE_H  = 34;
    static constexpr int EDGE_W   = 12;   // 扩大到12，更易命中
    static constexpr int MARGIN   = 6;
    static constexpr int CONTENT_TOP_GAP = 18;
    static constexpr int CONTENT_BOTTOM_GAP = 12;
    static constexpr int SCROLLBAR_RESERVED_W = 14;
    static constexpr int ICON_W   = 80;
    static constexpr int ICON_H   = 104;
    static constexpr int ICON_GAP = 4;
    static constexpr int HANDLE_R = 5;    // 角点手柄半径
    static constexpr int SNAP_THRESHOLD = 10; // 吸附阈值（像素）

    // 吸附辅助线（拖动/缩放时显示）
    mutable QList<QLine> m_snapGuides;
};
