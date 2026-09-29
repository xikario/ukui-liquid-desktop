#pragma once
#include "IconAppearance.h"

#include <QWidget>
#include <QPixmap>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QSet>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QFont>
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <memory>
#include "DesktopItem.h"
#include "FileClipboard.h"

class FenceWidget;
class FenceGlassRenderer;
class DesktopIcon;
class SystemMonitor;
class SmartSpaceWidget;
class ClockDesklet;
class MusicDesklet;
class CalendarDesklet;
class ActivityDesklet;
class ActivityRecorder;
class QFileSystemWatcher;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMoveEvent;
class QResizeEvent;
class QWheelEvent;
class QScreen;
class QProcess;

class DesktopCanvas : public QWidget
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ukui.fences")
    friend class FenceWidget;
    friend class SystemMonitor;
public:
    explicit DesktopCanvas(QWidget *parent = nullptr);
    ~DesktopCanvas();

    FenceWidget *createFence(const QString &title, const QRect &geo);
    void removeFence(FenceWidget *fence);
    void setGlobalEditMode(bool edit);
    bool globalEditMode() const { return m_editMode; }

    void saveLayout();
    void loadLayout();
    void exportLayout();
    void importLayout();

    // 字体配置
    QString fontFamily()  const { return m_fontFamily; }
    int     fontSize()    const { return m_fontSize; }
    QColor  fontColor()   const { return m_fontColor; }
    bool    fontBold()    const { return m_fontBold; }
    bool    fontItalic()  const { return m_fontItalic; }
    void setFontFamily(const QString &family);
    void setFontSize(int size);
    void setFontColor(const QColor &color);
    void setFontBold(bool bold);
    void setFontItalic(bool italic);
    QColor wallpaperColorAt(const QRect &area) const;
    bool wallpaperMagnetEnabled() const { return m_wallpaperMagnetEnabled; }
    void configureIconAppearance(DesktopIcon *icon, IconSurface surface);
    void applyIconAppearanceToAll();
    void showIconAppearanceDialog();
    QImage wallpaperBackdrop(const QRect &globalArea, qreal dpr) const;

signals:
    void initialWallpaperReady();
    void wallpaperChanged();

public slots:
    Q_SCRIPTABLE void setFenceLiquidGlassEnabled(bool enabled);
    Q_SCRIPTABLE void setWallpaperMagnetEnabled(bool enabled);
    Q_SCRIPTABLE bool isWallpaperMagnetEnabled() const { return m_wallpaperMagnetEnabled; }
    Q_SCRIPTABLE bool fenceLiquidGlassEnabled() const { return m_fenceLiquidGlassEnabled; }
    Q_SCRIPTABLE void showAndActivate();
    Q_SCRIPTABLE void hideFences();
    Q_SCRIPTABLE void toggleEditMode();
    Q_SCRIPTABLE void toggleDesklet();
    Q_SCRIPTABLE void setSystemMonitorSkin(int skin);
    Q_SCRIPTABLE int systemMonitorSkin() const;
    Q_SCRIPTABLE void toggleSmartSpace();
    Q_SCRIPTABLE void showSystemMonitorWidget();
    Q_SCRIPTABLE void showSmartSpaceWidget();
    Q_SCRIPTABLE void setSmartSpaceAlwaysOnTop(bool enabled);
    Q_SCRIPTABLE bool smartSpaceAlwaysOnTop() const;
    Q_SCRIPTABLE void hideSmartSpaceToEdge();
    Q_SCRIPTABLE void revealSmartSpaceFromEdge();
    Q_SCRIPTABLE bool smartSpaceEdgeHidden() const;
    Q_SCRIPTABLE int smartSpaceX() const;
    Q_SCRIPTABLE int smartSpaceY() const;
    Q_SCRIPTABLE int smartSpaceWidth() const;
    Q_SCRIPTABLE int smartSpaceHeight() const;
    Q_SCRIPTABLE void moveSmartSpace(int x, int y);
    Q_SCRIPTABLE void resizeSmartSpace(int width, int height);
    Q_SCRIPTABLE void setSmartSpaceDensity(int density);
    Q_SCRIPTABLE void showSmartSpaceSettings();
    Q_SCRIPTABLE void startSmartSpaceFullIndex();
    Q_SCRIPTABLE bool smartSpaceIndexBusy() const;
    Q_SCRIPTABLE void hideSmartSpaceFolder(const QString &path);
    Q_SCRIPTABLE void excludeSmartSpaceFolder(const QString &path);
    Q_SCRIPTABLE bool smartSpaceOverlapsDesktopIcons() const;
    Q_SCRIPTABLE bool systemMonitorVisible() const { return m_monitor != nullptr; }
    Q_SCRIPTABLE bool smartSpaceVisible() const { return m_smartSpace != nullptr; }
    Q_SCRIPTABLE void setEditModeDBus(bool edit);
    Q_SCRIPTABLE void setClockWidgetVisible(bool visible);
    Q_SCRIPTABLE void setActivityWidgetVisible(bool visible);
    Q_SCRIPTABLE bool clockWidgetVisible() const;
    Q_SCRIPTABLE bool activityWidgetVisible() const;
    Q_SCRIPTABLE void setMusicWidgetVisible(bool visible);
    Q_SCRIPTABLE bool musicWidgetVisible() const;
    Q_SCRIPTABLE void setCalendarWidgetVisible(bool visible);
    Q_SCRIPTABLE bool calendarWidgetVisible() const;
    Q_SCRIPTABLE QString desktopWidgetsStatus() const;
    Q_SCRIPTABLE void refreshAll();
    Q_SCRIPTABLE void activateOnSessionStartup();
    Q_SCRIPTABLE void quitApp();

protected:
    void paintEvent(QPaintEvent *)           override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mousePressEvent(QMouseEvent *)      override;
    void mouseMoveEvent(QMouseEvent *)       override;
    void mouseReleaseEvent(QMouseEvent *)    override;
    void moveEvent(QMoveEvent *)             override;
    void resizeEvent(QResizeEvent *)         override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void changeEvent(QEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshDesktopIcons();

private:
    enum class ArrangeMode {
        Manual,
        ByName,
        ByType,
        ByModifiedTime
    };

    enum class WallpaperMode {
        Fill,       // 等比缩放裁切铺满
        Fit,        // 等比缩放完整显示
        Stretch,    // 拉伸填满
        Tile,       // 原始尺寸平铺
        Center,     // 原始尺寸居中
        System      // 跟随系统桌面壁纸
    };

    struct UndoOperation {
        enum class Type {
            Create,
            Rename,
            Trash,
            Paste
        };

        Type type = Type::Create;
        QStringList sourcePaths;
        QStringList targetPaths;
        QStringList fenceIds;
        bool move = false;
    };

    void setupAsDesktop();
    void applyX11DesktopHints();
    void restackDesktopLayer();
    bool x11ShowingDesktop() const;
    void setX11ShowingDesktop(bool showing) const;
    void triggerShowDesktop();
    void updateHotCornerGuards();
    void connectScreenSignals(QScreen *screen);
    void scheduleScreenGeometrySync();
    void syncScreenGeometry();
    bool handleHotCornerPress(int corner, QMouseEvent *event);
    void lockToDesktopGeometry();
    void loadWallpaper();
    void clearWallpaperCache();
    void rebuildWallpaperCache();
    QImage renderLiquidGlass(const QRect &area, qreal radius = 10.0,
                             const QPainterPath &shape = {});
    bool loadExternalTheme();
    void applyExternalThemeToFences();
    bool applyWallpaperThemeToFences();
    void applyThemeToFences(const QColor &fenceColor,
                            const QColor &textColor = QColor());
    void layoutLooseIcons();
    QRect desktopIconArea() const;
    QRect smartSpaceReservedGeometry() const;
    QList<QRect> desktopWidgetReservedGeometries() const;
    void assignLooseIconsToFirstCells();
    void revealLooseIcons();
    void sortLooseIcons();
    void autoArrangeGrid();
    void disableAutoArrangeForManualPlacement();
    void addSystemIcons();
    void connectLooseIcon(DesktopIcon *icon);
    void clearLooseIcons();
    void removeLooseIcon(const QString &filePath);
    DesktopIcon *looseIconForPath(const QString &filePath) const;
    void finishNewDesktopItem(const QString &filePath,
                              const QPoint &clickPos);
    void createNewDesktopFile(const QString &baseName,
                              const QString &suffix,
                              const QPoint &clickPos);
    void createNewDesktopDirectory(const QPoint &clickPos);
    void showSettingsDialog();
    void showDesktopSyncSettingsDialog();
    void showGridDialog();
    void showWallpaperDialog();
    void showDesklet();
    void showSmartSpace();
    void setSmartSpaceVisible(bool visible);
    void setSystemMonitorVisible(bool visible);
    void applySmartSpaceWindowMode(bool alwaysOnTop);
    void applyDesktopGrid(int columns, int rows, qreal iconScale);
    void beginWallpaperFenceCapture();
    void finishWallpaperFenceCapture(const QRect &selection);
    QImage renderedWallpaperImage() const;
    QColor sampleFenceColorFromRect(const QRect &selection) const;
    void ensureTrashWatches();
    void applyFontToAll();
    void clearFenceSelections();
    void setIconSelected(DesktopIcon *icon, bool selected);
    void selectIconRange(DesktopIcon *anchor, DesktopIcon *target, bool keepExisting);
    void clearSelection();
    void selectAllLooseIcons();
    QStringList selectedFilePaths() const;
    bool pasteToDesktop(const QPoint &preferredPos = QPoint(-1, -1));
    void placeFilesOnDesktop(const QStringList &paths,
                             const QPoint &preferredPos = QPoint(-1, -1));
    void trashSelectedIcons();
    void renameFirstSelectedIcon();
    void openSelectedIcons();
    void copySelectedIcons(bool move);
    void applyIconScale(qreal scale);
    bool isInAnyFence(const QString &filePath) const;
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    void undoLastOperation();
    void pushUndo(const UndoOperation &op);
    void recordCreateUndo(const QString &path);
    void recordRenameUndo(const QString &oldPath, const QString &newPath,
                          FenceWidget *fence = nullptr);
    void recordTrashUndo(const QStringList &paths, FenceWidget *fence = nullptr);
    void recordPasteUndo(const FileClipboard::PasteResult &result,
                         FenceWidget *fence = nullptr);
    FenceWidget *fenceById(const QString &id) const;
    FenceWidget *fenceContainingPath(const QString &path) const;
    FenceWidget *desktopInboxFence() const;
    static QString restoreTrashedPath(const QString &originalPath);
    static bool deletePathForUndo(const QString &path);
    static bool movePathForUndo(const QString &srcPath, const QString &targetPath);
    void startMultiDrag();
    void refreshTrashState();
    void syncCutVisualState();
    void removePathsFromAllViews(const QStringList &paths);
    bool pruneMissingFileIcons();
    void updateCutPathWatches(const QStringList &cutPaths);
    void refreshDesktopDirectories();
    void ensureDesktopWatches();
    QSet<QString> collectDesktopFilePaths() const;
    void logDesktopSyncAudit(const QString &reason,
                             const QSet<QString> &currentPaths) const;
    void syncDesktopIcons(bool force);
    void forceSyncDesktopIcons();
    bool isInDesktopDirectory(const QString &path) const;
    void scheduleRefresh(int delayMs = 0);

    IconAppearance m_iconAppearance;
    QColor m_iconAccent = QColor("#7faee8");
    QPixmap             m_wallpaper;
    bool m_glassPreparing = false;
    bool m_initialWallpaperReady = false;
    bool m_wallpaperLoading = false;
    bool m_wallpaperReloadPending = false;
    QList<QProcess *>   m_wallpaperMonitors;
    bool                m_fenceLiquidGlassEnabled = false;
    bool                m_wallpaperMagnetEnabled = true;
    std::unique_ptr<FenceGlassRenderer> m_fenceGlassRenderer;
    qint64              m_fenceGlassWallpaperKey = -1;
    QPixmap             m_wallpaperCache;
    QSize               m_wallpaperCacheLogicalSize;
    qreal               m_wallpaperCacheDpr = 0.0;
    WallpaperMode       m_wallpaperMode = WallpaperMode::System;
    QString             m_wallpaperPath;
    bool                m_wallpaperUsingCustom = false;
    QList<FenceWidget*> m_fences;
    QList<DesktopIcon*> m_looseIcons;
    QSet<DesktopIcon*>  m_selectedIcons;
    DesktopIcon        *m_selectionAnchor = nullptr;
    QMap<QString, QPoint> m_looseIconPositions;
    QList<UndoOperation>  m_undoStack;
    QFileSystemWatcher *m_watcher   = nullptr;
    QTimer             *m_debounce  = nullptr;
    QTimer             *m_desktopSyncTimer = nullptr;
    QTimer             *m_cutRefreshTimer = nullptr;
    QTimer             *m_smartSpaceRelayoutTimer = nullptr;
    QTimer             *m_screenGeometryTimer = nullptr;
    QList<QWidget*>     m_hotCornerGuards;
    QSet<QString>       m_cutWatchPaths;
    QStringList         m_cutClipboardPaths;
    qint64              m_cutClipboardStartedMs = 0;
    int                 m_cutRefreshGraceTicks = 0;
    bool                m_editMode  = false;
    qreal               m_iconScale = 1.0;
    qreal               m_desktopIconScale = 1.0;
    int                 m_gridColumns = 18;
    int                 m_gridRows = 11;
    bool                m_autoArrange = false;
    ArrangeMode         m_arrangeMode = ArrangeMode::Manual;
    QString             m_desktopPath;
    QStringList         m_desktopPaths;
    QString             m_desktopInboxFenceId;
    bool                m_loadingLayout = false;
    bool                m_userHidden = false;
    QColor              m_defaultFenceColor { 0, 120, 215, 90 };

    // 字体配置
    QString             m_fontFamily = QString();
    int                 m_fontSize   = 11;
    QColor              m_fontColor  = Qt::white;
    bool                m_fontBold   = false;
    bool                m_fontItalic = false;

    // 框选 / 壁纸取样建分区
    bool                m_rubberBanding = false;
    bool                m_wallpaperFenceCapture = false;
    bool                m_lockingDesktopGeometry = false;
    int                 m_refreshAnimFrame = -1;  // -1=off, 0..8=active
    QTimer             *m_refreshAnimTimer = nullptr;
    QPoint              m_rubberOrigin;
    QRect               m_rubberRect;
    SystemMonitor      *m_monitor = nullptr;
    SmartSpaceWidget   *m_smartSpace = nullptr;
    MusicDesklet       *m_musicWidget = nullptr;
    CalendarDesklet    *m_calendarWidget = nullptr;
    ClockDesklet       *m_clockWidget = nullptr;
    ActivityDesklet    *m_activityWidget = nullptr;
    ActivityRecorder  *m_activityRecorder = nullptr;
};
