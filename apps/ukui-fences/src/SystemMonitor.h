#pragma once

#include <QColor>
#include <QByteArray>
#include <QHash>
#include <QPoint>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QContextMenuEvent;
class QHideEvent;
class QJsonObject;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QProcess;
class QShowEvent;
class QTemporaryFile;
class QWheelEvent;
class QMoveEvent;
class QResizeEvent;

struct ProcessInfo {
    QString name;
    QString executablePath;
    double cpuPercent = 0.0;
    double rssMb = 0.0;
    quint64 startTicks = 0;
    int pid = 0;
};

class SystemMonitor : public QWidget
{
    Q_OBJECT
public:
    enum class Skin {
        Dark,
        Light,
        Cyber,
        Glass,
        Wallpaper,
        Liquid
    };

    explicit SystemMonitor(QWidget *parent = nullptr);

    void setEditMode(bool edit);
    void refreshWallpaperTheme();
    void setSkin(Skin skin);
    Skin skin() const { return m_skin; }

    static bool autoStartEnabled();
    void setAutoStart(bool enabled);
    bool autoStart() const { return m_autoStart; }

signals:
    void closeRequested();
    void appearanceChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct CpuTotals {
        quint64 total = 0;
        quint64 idle = 0;
        quint64 iowait = 0;
        QVector<quint64> coreTotal;
        QVector<quint64> coreIdle;
    };

    struct MemStats {
        quint64 totalKiB = 0;
        quint64 availableKiB = 0;
        quint64 cacheKiB = 0;
        quint64 swapTotalKiB = 0;
        quint64 swapFreeKiB = 0;
    };

    struct DiskIoTotals {
        quint64 readSectors = 0;
        quint64 writeSectors = 0;
    };

    struct ProcessAggregate {
        QString name;
        double cpuSum = 0.0;
        double cpuPeak = 0.0;
        double rssPeakMb = 0.0;
        int samples = 0;
    };

    struct ProcessDetails {
        QString name;
        QString executableName;
        QString state;
        double rssMb = 0.0;
        double virtualMemoryMb = 0.0;
        quint64 startTicks = 0;
        quint64 readBytes = 0;
        quint64 writeBytes = 0;
        quint64 voluntaryContextSwitches = 0;
        quint64 involuntaryContextSwitches = 0;
        int parentPid = 0;
        int threadCount = 0;
        int uid = -1;
        int fileDescriptorCount = -1;
        bool hasIoCounters = false;
        bool valid = false;
    };

    struct Palette {
        QColor panel;
        QColor card;
        QColor border;
        QColor text;
        QColor muted;
        QColor cpu;
        QColor memory;
        QColor disk;
        QColor track;
    };

    void refreshStats();
    CpuTotals readCpuTotals() const;
    MemStats readMemStats() const;
    QVector<ProcessInfo> readTopProcesses(int count, quint64 totalDelta);
    QVector<ProcessInfo> readProcessSnapshot(
        int count, quint64 totalDelta, QHash<int, quint64> &previousTicks) const;
    ProcessDetails readProcessDetails(int pid) const;
    DiskIoTotals readDiskIoTotals() const;
    QVector<double> readLoadAverages() const;
    QString readCpuModel() const;
    void readDiskStats();

    void setCompact(bool compact);
    void restoreSettings();
    void saveSettings() const;
    void saveCredential(const QString &key);
    void runCredentialJob(const QString &operation, const QString &key = QString());
    QPoint boundedPosition(const QPoint &position) const;

    Palette paletteForSkin() const;
    void drawHeader(QPainter &p, const Palette &colors);
    void drawMetricCard(QPainter &p, const QRect &rect,
                        const QString &title, const QString &detail,
                        double percent, const QColor &accent,
                        const QVector<double> &history);
    void drawProcessPanel(QPainter &p, const QRect &rect,
                          const Palette &colors);
    void drawAiPanel(QPainter &p, const QRect &rect,
                     const Palette &colors);
    void drawButton(QPainter &p, const QRect &rect, const QString &text,
                    const QColor &foreground, const QColor &background,
                    const QColor &border);
    void drawRing(QPainter &p, const QRectF &rect, double percent,
                  const QColor &accent, const QColor &track);
    void drawSparkline(QPainter &p, const QRectF &rect,
                       const QVector<double> &history,
                       const QColor &accent);
    void appendHistory(QVector<double> &history, double value);
    void showSettingsDialog();
    void startDiagnosis();
    void startProcessDiagnosis(const ProcessInfo &process);
    void beginDiagnosis(const ProcessInfo *targetProcess);
    void collectDiagnosisSample();
    void sendDiagnosisRequest();
    bool prepareAiAuthHeader();
    void finishDiagnosis(int exitCode);
    QByteArray buildDiagnosisPayload() const;
    QString diagnosticTelemetry() const;
    QString localHealthAssessment() const;
    QString formatDiagnosisJson(const QJsonObject &result) const;
    QString compactAiText(const QString &text, int limit) const;
    QFont uiFont(int sizeOffset = 0, bool bold = false) const;
    void updateAiLayoutHeight();
    void updateHitRects();
    QSize baseSize() const;
    int logicalWidth() const;
    int logicalHeight() const;
    QPoint logicalPosition(const QPoint &position) const;
    QString tooltipAt(const QPoint &logicalPosition) const;
    double readCoreFrequencyGhz(int core) const;
    QColor wallpaperSampleColor() const;

    QTimer m_timer;
    QTimer m_diagnosisTimer;
    CpuTotals m_prevCpu;
    quint64 m_lastProcTotal = 0;
    QHash<int, quint64> m_prevProcTicks;

    QString m_cpuModel;
    int m_coreCount = 1;
    double m_cpuPercent = 0.0;
    double m_memPercent = 0.0;
    double m_memUsedGb = 0.0;
    double m_memTotalGb = 0.0;
    double m_memCacheGb = 0.0;
    double m_diskPercent = 0.0;
    double m_diskUsedGb = 0.0;
    double m_diskTotalGb = 0.0;
    QVector<double> m_corePercents;
    QVector<double> m_coreFrequenciesGhz;
    QVector<double> m_cpuHistory;
    QVector<double> m_memHistory;
    QVector<double> m_diskHistory;
    QVector<ProcessInfo> m_topProcs;
    QProcess *m_curl = nullptr;
    QTemporaryFile *m_aiAuthFile = nullptr;
    QByteArray m_curlOutput;
    QByteArray m_curlError;
    QString m_apiKey;
    QProcess *m_credentialJob = nullptr;
    QString m_pendingCredential;
    bool m_credentialWritePending = false;
    bool m_credentialFailed = false;
    int m_credentialRevision = 0;
    QString m_apiUrl = QStringLiteral("https://api.deepseek.com/chat/completions");
    QString m_widgetTitle = QStringLiteral("飞腾桌面资源监控");
    QString m_widgetFontFamily;
    int m_widgetFontSize = 9;
    int m_windowOpacity = 100;
    QColor m_wallpaperThemeColor;
    QImage m_liquidGlassImage;
    QRect m_liquidGlassGeometry;
    QPointF m_liquidGlassPointer {-1000, -1000};
    QString m_aiText;
    QString m_aiReasoning;
    QString m_aiError;
    QString m_aiProgressText;
    bool m_aiBusy = false;
    bool m_diagnosisSampling = false;

    CpuTotals m_diagnosisPrevCpu;
    DiskIoTotals m_diagnosisPrevDiskIo;
    QHash<int, quint64> m_diagnosisPrevProcTicks;
    QHash<QString, ProcessAggregate> m_diagnosisProcesses;
    QVector<double> m_diagnosisCpuSamples;
    QVector<double> m_diagnosisIowaitSamples;
    QVector<double> m_diagnosisMemorySamples;
    QVector<double> m_diagnosisSwapSamples;
    QVector<double> m_diagnosisLoadSamples;
    QVector<double> m_diagnosisDiskReadSamples;
    QVector<double> m_diagnosisDiskWriteSamples;
    ProcessInfo m_diagnosisTarget;
    ProcessDetails m_diagnosisTargetDetails;
    QVector<double> m_diagnosisTargetCpuSamples;
    QVector<double> m_diagnosisTargetRssSamples;
    QVector<double> m_diagnosisTargetReadSamples;
    QVector<double> m_diagnosisTargetWriteSamples;
    quint64 m_diagnosisTargetPreviousReadBytes = 0;
    quint64 m_diagnosisTargetPreviousWriteBytes = 0;
    int m_diagnosisTargetSamplesSeen = 0;
    bool m_hasDiagnosisTarget = false;
    bool m_diagnosisTargetExited = false;
    int m_diagnosisSamplesTaken = 0;

    bool m_compact = false;
    bool m_hovered = false;
    bool m_editMode = false;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_mouseGrabbed = false;
    bool m_autoStart = false;
    bool m_premiumAesthetics = true;
    QPoint m_dragOffset;
    QPoint m_resizeStartGlobal;
    QSize m_resizeStartSize;
    double m_scale = 1.0;
    int m_statIntervalSec = 5;
    int m_aiExtraHeight = 0;
    int m_aiScrollOffset = 0;
    int m_aiContentHeight = 0;
    Skin m_skin = Skin::Dark;
    int m_tick = 0;
    QRect m_optimizeRect;
    QRect m_toggleRect;
    QRect m_skinRect;
    QRect m_settingsRect;
    QRect m_aiRunRect;
    QRect m_aiClearRect;
    QRect m_aiContentRect;
    QRect m_cpuRingRect;
    QRect m_memoryRingRect;
    QRect m_diskRingRect;
    QRect m_resizeHandleRect;
    QVector<QRect> m_coreRects;
    QVector<QRect> m_processRowRects;

    static constexpr int COMPACT_W = 720;
    static constexpr int COMPACT_H = 248;
    static constexpr int EXPANDED_W = 900;
    static constexpr int EXPANDED_H = 440;
    static constexpr int HEADER_H = 54;
    static constexpr int MARGIN = 16;
    static constexpr int DEFAULT_STAT_INTERVAL_SEC = 5;
    static constexpr int HEAVY_EVERY_TICKS = 6;
    static constexpr int HISTORY_POINTS = 20;
    static constexpr int DIAGNOSIS_SAMPLE_COUNT = 15;
    static constexpr int DIAGNOSIS_SAMPLE_INTERVAL_MS = 1000;
};
