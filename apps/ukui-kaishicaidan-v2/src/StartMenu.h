#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFileSystemWatcher>
#include <QImage>
#include <QPixmap>
#include "StartMenuTheme.h"
#include "AppRegistry.h"
#include "RecentFiles.h"
#include "TaskbarDetector.h"

class StartButton;
class SettingsDialog;
class SectionHeader;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
typedef struct _XDisplay Display;
class NextKdeGlassView;
class QSocketNotifier;

class StartMenu : public QWidget {
    Q_OBJECT
#if defined(UKUI_KAISHICAIDAN_V2)
    Q_CLASSINFO("D-Bus Interface", "org.ukui.kaishicaidan.v2")
#else
    Q_CLASSINFO("D-Bus Interface", "org.ukui.kaishicaidan")
#endif
public:
    explicit StartMenu(QWidget *parent = nullptr);
    ~StartMenu() override;

    void positionAboveStartButton();
    void applyX11Immunity();
    bool isMenuVisible() const { return m_visible; }
    bool isLiquidTheme() const { return m_skin == Skin::EcoLiquid; }
    qreal glassLuminanceAt(const QRectF &menuRect) const;
    bool paintGlassControl(QPainter &p,const QWidget *owner,const QRectF &rect,
                           qreal radius,qreal hover,bool pressed=false);

    void applySkin(Skin skin);
    void applyFont(const QString &family, int size);

public slots:
    Q_SCRIPTABLE void toggle();
    Q_SCRIPTABLE void showMenu();
    Q_SCRIPTABLE void hideMenu();
    Q_SCRIPTABLE void quitApp();
    Q_SCRIPTABLE void openSettings();
    Q_SCRIPTABLE void openSystemSettings();
    Q_SCRIPTABLE void openAboutKylin();
    Q_SCRIPTABLE void lockScreen();
    Q_SCRIPTABLE void suspendSystem();
    Q_SCRIPTABLE void hibernateSystem();
    Q_SCRIPTABLE void hybridSleepSystem();
    Q_SCRIPTABLE void logoutSession();
    Q_SCRIPTABLE void rebootSystem();
    Q_SCRIPTABLE void poweroffSystem();

protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void enterEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    void setupUi();
    void setupPinnedGrid();
    void setupRecentFiles();
    void rebuildAppList();
    void setupAppWatcher();
    void refreshAppWatcher();
    void setupActiveAppTracking();
    void recordActiveWindowApp();
    void refreshPalette();
    void applyConfig();
    void applyFontToChildren();
    void applySearchStyle();
    void applyX11BackdropEffect();
    void filterApps(const QString &text);
    void launchApp(int index);
    void launchAppEntry(const AppEntry &app);
    void launchRecent(const QString &path);
    void drawLeftRail(QPainter &p);
    void drawAvatarButton(QPainter &p);
    void drawRailButtons(QPainter &p);
    void drawRailApps(QPainter &p);
    void handleRailClick(const QPoint &pos);
    void handleRailContextMenu(const QPoint &pos);
    void reloadRailApps();
    void saveRailApps();
    void removeRailApp(int index);
    int railAppIndexAt(const QPoint &pos) const;
    int railDropIndexAt(const QPoint &pos) const;
    void setupClipboardHistory();
    void showClipboardHistoryMenu();
    void updateRailTooltip();
    void cycleQuickTheme();
    void showThemeMenu();
    void refreshUserAvatar();
    bool openAccountSettings();
    void openDocuments();
    void showPowerMenu(const QPoint &globalPos);
    void showMoreRecent();
    struct AppRemovalTarget {
        enum class Kind {
            DebPackage,
            FolderBundle,
            DesktopShortcut,
            ProtectedPackage,
            Unsupported
        };

        Kind kind = Kind::Unsupported;
        QString target;
        QString actionText;
        QString detail;
    };
    static AppRemovalTarget detectAppRemovalTarget(const AppEntry &app);
    void removeApp(const AppEntry &app, const AppRemovalTarget &target);
    void confirmDebRemoval(const AppEntry &app, const AppRemovalTarget &target, const QStringList &removedPackages);
    bool m_removalPending = false;
    bool m_palettePending = false;
    bool m_paletteApplying = false;
    QColor m_wallpaperAccent;
    void showAppContextMenu(const AppEntry &app, const QPoint &globalPos);
    void movePinnedApp(int fromIndex, int toIndex);
    void showPinnedView();
    void showAllAppsView();
    void showAllAppsHeaderMenu();
    void sortAllApps();
    QString sortModeName() const;
    QList<AppEntry> displayedApps() const;

    // Click-outside detection via polling active X window
    void startOutsideWatch();
    void stopOutsideWatch();
    void captureNextKdeBackdrop();

    // Active palette (set by applySkin)
    SkinPalette m_palette;
    Skin m_skin = Skin::Dark;
    QString m_fontFamily;
    int m_fontSize = 14;

    bool m_visible = false;
    bool m_settingsOpen = false;
    bool m_showingAllApps = false;
    enum class AppSortMode {
        Alphabetical,
        InstallTime,
        LastUsed
    };
    AppSortMode m_appSortMode = AppSortMode::Alphabetical;
    QTimer *m_outsideTimer = nullptr;
    QTimer *m_activeAppTimer = nullptr;
    Display *m_activeAppDisplay = nullptr;
    QSocketNotifier *m_activeAppNotifier = nullptr;
    QImage m_lastBackdrop;
    QTimer *m_searchDebounceTimer = nullptr;
    QElapsedTimer m_showTime;           // tracks when menu was last shown
    TaskbarInfo m_cachedTaskbarInfo;
    QElapsedTimer m_taskbarCacheTimer;
    Display *m_pointerDisplay = nullptr;
    unsigned long m_lastActiveWindow = 0;
    QString m_lastRecordedActiveDesktop;
    QString m_pendingSearchText;
    bool m_pointerHasEnteredMenu = false;

    enum class RailButton {
        NoButton,
        Avatar,
        Documents,
        Clipboard,
        Theme,
        Settings,
        Power
    };
    RailButton m_hoveredRailBtn = RailButton::NoButton;
    int m_hoveredRailApp = -1;
    int m_railDropIndex = -1;
    int m_pressedRailApp = -1;
    QPoint m_railDragStartPos;

    // Child widgets
    SectionHeader *m_pinnedHeader = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    SectionHeader *m_recentHeader = nullptr;
    QScrollArea *m_pinnedScrollArea = nullptr;
    QWidget *m_pinnedContainer = nullptr;
    QGridLayout *m_pinnedGrid = nullptr;
    QWidget *m_recentContainer = nullptr;
    QFileSystemWatcher *m_appWatcher = nullptr;
    QTimer *m_appRebuildTimer = nullptr;
    bool m_appScanBusy = false;
    bool m_appScanPending = false;

    // Data (use global types from AppRegistry / RecentFiles)
    QList<AppEntry> m_pinnedApps;
    QList<AppEntry> m_allApps;
    QList<AppEntry> m_filteredApps;
    QList<AppEntry> m_railApps;
    QList<RecentFileEntry> m_recentFiles;
    struct ClipboardHistoryItem {
        QString text;
        QImage image;

        bool isImage() const { return !image.isNull(); }
    };
    QList<ClipboardHistoryItem> m_clipboardHistory;
    QPixmap m_userAvatar;
    NextKdeGlassView *m_nextKdeGlassView = nullptr;
    QTimer *m_glassLightTimer = nullptr;
    QPointF m_glassLightPos = QPointF(-500, -500);

    friend class StartButton;
    friend class SettingsDialog;
};
