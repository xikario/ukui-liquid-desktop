#pragma once

#include <QDateTime>
#include <QColor>
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QFileSystemWatcher;
class QContextMenuEvent;
class QEvent;
class QLineEdit;
class QMenu;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QButtonGroup;
class QFrame;
class QHBoxLayout;
class QToolButton;
class QMouseEvent;
class QMoveEvent;
class QProcess;
class QResizeEvent;
class QScrollArea;
class QSizeGrip;
class QLabel;
class QVBoxLayout;
class QSplitter;
class QPushButton;
class QProgressBar;
class QStackedWidget;
class QTextBrowser;

struct SmartSpaceEntry {
    QString path;
    QString root;
    QString parentPath;
    QString name;
    QString suffix;
    QString category;
    QString content;
    QString extractor;
    QString ocrStatus;
    QString ocrReason;
    QDateTime modified;
    qint64 size = 0;
    bool isDir = false;
};

class SmartSpaceWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SmartSpaceWidget(bool fenceEmbedded = false,
                              QWidget *parent = nullptr);
    ~SmartSpaceWidget() override;

    void setEditMode(bool edit);
    static bool autoStartEnabled();
    void setAutoStart(bool enabled);
    bool autoStart() const { return m_autoStart; }
    void setAlwaysOnTop(bool enabled);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setResultDensity(int density);
    void showSettings();
    void startFastFullIndex();
    bool indexBusy() const { return m_indexBusy; }
    void hideFolder(const QString &path);
    void excludeFolder(const QString &path);
    void recreateNativeSurface(bool translucent);
    // Refresh the wallpaper material behind the liquid-glass skin. Call this
    // before mapping a newly-created widget so the first frame is clean.
    void refreshGlassBackdrop();
    void hideToNearestEdge();
    void revealFromEdge();
    bool edgeHidden() const { return m_edgeHidden; }
    QColor fenceThemeColor() const { return m_surfaceColor; }
    QColor titleThemeColor() const { return m_textColor; }
    void refreshInheritedTheme();


signals:
    void closeRequested();
    void themeChanged(const QColor &fenceColor, const QColor &titleColor);
    void alwaysOnTopChanged(bool enabled);
    void geometryChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum ItemRole {
        PathRole = Qt::UserRole + 1,
        FolderLevelRole
    };

    void buildUi();
    void applyTheme();
    void scheduleGlassBackdropRefresh();
    void finishEdgeTransition();
    void animateEdgeReveal();
    void animateEdgeFrame(const QPixmap &snapshot, const QRect &area, bool revealing);
    void applyMenuTheme(QMenu *menu) const;
    void setSimpleTheme(int mode, bool persist = true);
    void configureIndexScheduling();
    QWidget *createPanel(const QString &title, QWidget *content,
                         const QString &objectName);
    QListWidget *createEntryList();
    void restoreSettings();
    void saveSettings() const;
    QPoint boundedPosition(const QPoint &position) const;
    void updateRoundedMask();
    QString entryToolTip(const SmartSpaceEntry &entry) const;
    QString relevantSnippetHtml(const SmartSpaceEntry &entry,
                                int maxContexts, int before, int after) const;
    void updateActionState();
    void rebuildFileScopePill();
    void rebuildCategoryPills();
    void showHeaderMenu();
    void updateResponsiveLayout();
    QWidget *createFileCard(const SmartSpaceEntry &entry,
                            QListWidgetItem *item);
    const SmartSpaceEntry *entryForPath(const QString &path) const;
    void showPreviewForPath(const QString &path);
    void hidePreview();
    void showPreviewImage(const QString &imagePath);
    void startPdfPreview(const SmartSpaceEntry &entry);
    void prunePreviewCache();

    QString indexPath() const;
    QString uiStreamIndexPath() const;
    QString resumeIndexPath() const;
    QString ocrResumeIndexPath() const;
    QString indexerPath() const;
    QString knowledgeWorkerPath() const;
    QString knowledgeDbPath() const;
    QString skillDirectoryPath() const;
    QString providerConfigPath() const;
    QStringList defaultRoots() const;
    void startIndexing();
    void startIdleFullIndex();
    void startOcrIndex();
    void launchIndexer(bool fastFull, bool ocrBackfill = false);
    void stopIdleFullIndex();
    bool loadIndex();
    void handleIndexerOutput();
    void finishIndexing(int exitCode, int exitStatus);
    void rebuildWatches();
    void scheduleReindex();

    void updateResults(bool resetFolders = true);
    void executeSearch();
    bool commitSearchQuery(const QString &query, bool showError);
    bool entryMatches(const SmartSpaceEntry &entry) const;
    bool fileMatchesCurrentResults(const SmartSpaceEntry &entry,
                                   bool respectScope = true) const;
    bool categoryMatches(const SmartSpaceEntry &entry) const;
    bool pathInsideScope(const QString &path) const;
    bool pathExcluded(const QString &path) const;
    bool folderHidden(const QString &path) const;
    bool extensionVisible(const SmartSpaceEntry &entry) const;
    void hideFolderFromNavigation(const QString &path);
    void excludeFolderFromIndex(const QString &path);
    void updateFolderDensity();
    QVector<int> matchingFileIndexes() const;
    int relevancePercent(const SmartSpaceEntry &entry) const;
    void showResultSortMenu();

    void clearFolderLevels(int fromLevel = 0);
    void resetFolderScopeFromBlankClick();
    void appendRootLevel();
    void appendFolderLevel(const QString &parentPath, int level);
    void folderActivated(QListWidgetItem *item, int level);
    void openFileItem(QListWidgetItem *item);
    void openPath(const QString &path) const;
    void showEntryMenu(QListWidget *list, const QPoint &position);

    void showSettingsDialog();
    void startKnowledgeBuild();
    void startKnowledgeSearch(const QString &query);
    void startKnowledgeTask(const QString &task,
                            const QStringList &arguments);
    void terminateKnowledgeProcess(bool force);
    void handleKnowledgeOutput();
    void finishKnowledgeTask(int exitCode, int exitStatus);

    QVector<SmartSpaceEntry> m_entries;
    QStringList m_roots;
    QStringList m_excludedFolders;
    QStringList m_hiddenFolders;
    QStringList m_indexedExtensions;
    QStringList m_visibleExtensions;
    QStringList m_selectedCategories;
    QString m_providerConfig;
    QString m_scopePath;
    QString m_executedQuery;
    QStringList m_searchTerms;
    QRegularExpression m_searchExpression;
    bool m_searchUsesRegex = false;
    bool m_ocrImages = true;
    int m_maxItems = 25000;
    int m_maxContentPerFile = 12000;
    int m_maxTotalContentMb = 16;
    int m_indexMode = 0;
    int m_indexIntervalMinutes = 30;
    int m_themeMode = 0;
    bool m_autoStart = false;
    bool m_alwaysOnTop = false;
    bool m_idleFullIndex = false;
    bool m_ocrBackfill = false;
    bool m_idleIndexCancelled = false;
    bool m_idleUsesProcessGroup = false;
    bool m_automaticIndexingReady = false;
    bool m_defaultHidden = true;
    QString m_knowledgeDirectory;
    int m_knowledgeChunkSize = 1200;
    int m_knowledgeOverlap = 120;
    QHash<QString, int> m_knowledgeScores;
    QHash<QString, QString> m_knowledgeSnippets;
    QJsonObject m_knowledgeLastObject;
    QColor m_surfaceColor {15, 23, 42};
    QColor m_cardColor {2, 6, 23, 180};
    QColor m_borderColor {148, 163, 184, 45};
    QColor m_textColor {241, 245, 249};
    QColor m_mutedColor {148, 163, 184};
    QColor m_accentColor {99, 102, 241};
    // ---- Custom theme overrides (themeMode == 6) ----
    int    m_customBaseSkin = 0;
    int    m_customOpacity = 100;
    bool   m_customColorsEnabled = false;
    QColor m_customSurface;
    QColor m_customCard;
    QColor m_customBorder;
    QColor m_customText;
    QColor m_customMuted;
    QColor m_customAccent;
    // Extended per-widget color overrides (fall back to derived colors when unset)
    QColor m_customSearchBg;
    QColor m_customSearchText;
    QColor m_customButtonBg;
    QColor m_customButtonText;
    QColor m_customButtonHover;
    QColor m_customMenuBg;
    QColor m_customMenuText;
    QColor m_customMenuHover;
    QString m_customFontFamily;
    int     m_customFontSize = 13;
    bool    m_customFontBold = false;
    bool m_editMode = false;
    bool m_indexBusy = false;
    bool m_reindexPending = false;
    bool m_dragging = false;
    bool m_fenceEmbedded = false;
    bool m_edgeHidden = false;
    int m_edgeSide = 0;
    QPoint m_dragOffset;
    QPoint m_expandedPosition;
    QSize m_expandedSize {920, 520};
    QImage m_glassBackdrop;
    QRect m_glassBackdropGeometry;
    qreal m_glassBackdropDpr = 0;
    QPointer<QWidget> m_edgeTransition;
    QTimer m_geometrySaveTimer;
    QTimer m_glassBackdropRefreshTimer;
    QTimer m_glassPointerTimer;
    QPoint m_glassPointerPosition;
    bool m_glassPointerActive = false;

    QFrame *m_actionRail = nullptr;
    QWidget *m_contentContainer = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_folderTitleLabel = nullptr;
    QLabel *m_folderCountLabel = nullptr;
    QLabel *m_fileTitleLabel = nullptr;
    QLabel *m_fileCountLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QToolButton *m_searchButton = nullptr;
    QToolButton *m_themeToggleButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QToolButton *m_moreButton = nullptr;
    QToolButton *m_idleIndexButton = nullptr;
    QToolButton *m_pauseIndexButton = nullptr;
    QToolButton *m_ocrIndexButton = nullptr;

    QToolButton *m_refreshButton = nullptr;
    QToolButton *m_pinButton = nullptr;
    QToolButton *m_edgeRevealButton = nullptr;
    QToolButton *m_settingsButton = nullptr;
    QComboBox *m_categoryCombo = nullptr;
    QButtonGroup *m_categoryButtonGroup = nullptr;
    QWidget *m_scopePillContainer = nullptr;
    QWidget *m_categoryPillContainer = nullptr;
    QHBoxLayout *m_scopePillLayout = nullptr;
    QHBoxLayout *m_categoryPillLayout = nullptr;
    QListWidget *m_fileList = nullptr;
    QScrollArea *m_folderScroll = nullptr;
    QWidget *m_folderLevelsContainer = nullptr;
    QVBoxLayout *m_folderLevelsLayout = nullptr;
    QWidget *m_sizeGrip = nullptr;
    QVector<QWidget *> m_folderLevelWidgets;
    QSplitter *m_contentSplitter = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_folderPanel = nullptr;
    QWidget *m_filePanel = nullptr;
    QWidget *m_statusContainer = nullptr;
    QProgressBar *m_indexProgress = nullptr;
    QByteArray m_indexOutputBuffer;
    QByteArray m_knowledgeOutputBuffer;
    QByteArray m_knowledgeErrorBuffer;
    int m_indexProcessed = 0;
    int m_indexTotal = 0;

    QFrame *m_previewPanel = nullptr;
    QLabel *m_previewTitle = nullptr;
    QLabel *m_previewMeta = nullptr;
    QLabel *m_previewImage = nullptr;
    QTextBrowser *m_previewText = nullptr;
    QStackedWidget *m_previewStack = nullptr;
    QPushButton *m_previewOpenButton = nullptr;
    QToolButton *m_previewCloseButton = nullptr;
    QProcess *m_previewProcess = nullptr;
    QString m_previewPath;
    QString m_pendingPreviewImage;
    bool m_previewVisible = false;
    int m_resultDensity = 0;
    int m_resultSortMode = 0;
    int m_visibleResultLimit = 100;
    QString m_resultPageKey;
    QToolButton *m_sortButton = nullptr;
    QToolButton *m_densityDownButton = nullptr;
    QToolButton *m_densityUpButton = nullptr;

    QProcess *m_indexer = nullptr;
    QProcess *m_knowledgeProcess = nullptr;
    bool m_knowledgeUsesProcessGroup = false;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_reindexDebounce;
    QTimer m_periodicRefresh;
};
