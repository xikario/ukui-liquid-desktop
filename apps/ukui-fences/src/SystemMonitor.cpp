#include "LiquidPopup.h"
#include "SystemMonitor.h"
#include "DesktopCanvas.h"
#include "MenuStyle.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QScreen>
#include <QSpinBox>
#include <QSlider>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

QStringList splitBySpace(const QString &text)
{
    QStringList result;
    QString current;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }
    if (!current.isEmpty())
        result.append(current);
    return result;
}

double clampPercent(double value)
{
    return std::max(0.0, std::min(100.0, value));
}

QString compactModelName(QString name)
{
    name.replace(QStringLiteral("(R)"), QString());
    name.replace(QStringLiteral("(TM)"), QString());
    name = name.simplified();
    if (name.size() > 24)
        name = name.left(23) + QStringLiteral("…");
    return name;
}

double averageOf(const QVector<double> &values)
{
    if (values.isEmpty())
        return 0.0;
    double total = 0.0;
    for (double value : values)
        total += value;
    return total / values.size();
}

double peakOf(const QVector<double> &values)
{
    return values.isEmpty()
        ? 0.0
        : *std::max_element(values.constBegin(), values.constEnd());
}

QString processGroupName(const ProcessInfo &process)
{
    QString executable = process.executablePath.section(QLatin1Char(' '), 0, 0);
    QString name = QFileInfo(executable).fileName().trimmed();
    if (name.isEmpty())
        name = process.name.trimmed();
    return name.isEmpty() ? QStringLiteral("未知进程") : name;
}

QStringList jsonStringList(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty())
            result << text;
    }
    return result;
}

} // namespace

SystemMonitor::SystemMonitor(QWidget *parent)
    : QWidget(parent)
{
    // 作为 DesktopCanvas 的子控件存在：不创建独立顶层窗口，因此绝不会
    // 覆盖普通应用窗口，也无需依赖不同窗口管理器对 desktop hint 的实现。
    // 避免 WA_TranslucentBackground：在当前 UKUI/X11 组合里它会让 native
    // child window 变成不可见的黑/透明窗口。
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("ukui-fences-monitor"));

    m_cpuModel = readCpuModel();
    m_prevCpu = readCpuTotals();
    m_lastProcTotal = m_prevCpu.total;
    m_coreCount = std::max(1, m_prevCpu.coreTotal.size());
    m_corePercents = QVector<double>(std::min(m_coreCount, 8), 0.0);
    m_cpuHistory = QVector<double>(HISTORY_POINTS, 0.0);
    m_memHistory = QVector<double>(HISTORY_POINTS, 0.0);
    m_diskHistory = QVector<double>(HISTORY_POINTS, 0.0);

    restoreSettings();
    const QString environmentKey =
        QString::fromUtf8(qgetenv("DEEPSEEK_API_KEY")).trimmed();
    if (!environmentKey.isEmpty())
        m_apiKey = environmentKey;
    const QString environmentUrl =
        QString::fromUtf8(qgetenv("DEEPSEEK_API_URL")).trimmed();
    if (!environmentUrl.isEmpty())
        m_apiUrl = environmentUrl;
    readDiskStats();
    refreshStats();

    m_timer.setTimerType(Qt::VeryCoarseTimer);
    m_timer.setInterval(m_statIntervalSec * 1000);
    connect(&m_timer, &QTimer::timeout,
            this, &SystemMonitor::refreshStats);
    m_diagnosisTimer.setTimerType(Qt::CoarseTimer);
    m_diagnosisTimer.setInterval(DIAGNOSIS_SAMPLE_INTERVAL_MS);
    connect(&m_diagnosisTimer, &QTimer::timeout,
            this, &SystemMonitor::collectDiagnosisSample);
    QTimer::singleShot(500, this, &SystemMonitor::refreshStats);
    QTimer::singleShot(5500, this, [this] {
        if (isVisible() && !m_compact) {
            m_tick = 0;
            refreshStats();
        }
    });
}

void SystemMonitor::setEditMode(bool edit)
{
    m_editMode = edit;
    m_dragging = false;
    setCursor(edit ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

SystemMonitor::CpuTotals SystemMonitor::readCpuTotals() const
{
    CpuTotals totals;
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return totals;

    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isEmpty())
            break;
        const QByteArray line = rawLine.simplified();
        if (!line.startsWith("cpu"))
            break;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 5)
            continue;

        const quint64 user = parts.value(1).toULongLong();
        const quint64 nice = parts.value(2).toULongLong();
        const quint64 system = parts.value(3).toULongLong();
        const quint64 idle = parts.value(4).toULongLong();
        const quint64 iowait = parts.value(5).toULongLong();
        const quint64 irq = parts.value(6).toULongLong();
        const quint64 softirq = parts.value(7).toULongLong();
        const quint64 steal = parts.value(8).toULongLong();
        const quint64 total =
            user + nice + system + idle + iowait + irq + softirq + steal;
        const quint64 idleAll = idle + iowait;

        if (parts.first() == "cpu") {
            totals.total = total;
            totals.idle = idleAll;
            totals.iowait = iowait;
        } else {
            totals.coreTotal.append(total);
            totals.coreIdle.append(idleAll);
        }
    }
    return totals;
}

SystemMonitor::MemStats SystemMonitor::readMemStats() const
{
    MemStats stats;
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return stats;

    while (true) {
        const QByteArray line = file.readLine();
        if (line.isEmpty())
            break;
        const int separator = line.indexOf(':');
        if (separator <= 0)
            continue;
        const QByteArray key = line.left(separator).trimmed();
        const QByteArray valuePart =
            line.mid(separator + 1).trimmed().split(' ').value(0);
        const quint64 value = valuePart.toULongLong();

        if (key == "MemTotal")
            stats.totalKiB = value;
        else if (key == "MemAvailable")
            stats.availableKiB = value;
        else if (key == "Cached" || key == "SReclaimable" ||
                 key == "Buffers")
            stats.cacheKiB += value;
        else if (key == "SwapTotal")
            stats.swapTotalKiB = value;
        else if (key == "SwapFree")
            stats.swapFreeKiB = value;
    }
    return stats;
}

QString SystemMonitor::readCpuModel() const
{
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("处理器");

    QString hardware;
    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isEmpty())
            break;
        const int separator = rawLine.indexOf(':');
        if (separator <= 0)
            continue;
        const QByteArray key = rawLine.left(separator).trimmed();
        const QString value =
            QString::fromUtf8(rawLine.mid(separator + 1).trimmed());
        if ((key == "model name" || key == "Processor") && !value.isEmpty())
            return compactModelName(value);
        if (key == "Hardware" && !value.isEmpty())
            hardware = value;
    }
    return hardware.isEmpty() ? QStringLiteral("处理器")
                              : compactModelName(hardware);
}

void SystemMonitor::readDiskStats()
{
    struct statvfs stats {};
    const QByteArray path = QDir::homePath().toLocal8Bit();
    if (statvfs(path.constData(), &stats) != 0)
        return;

    const double total =
        static_cast<double>(stats.f_blocks) * stats.f_frsize;
    const double available =
        static_cast<double>(stats.f_bavail) * stats.f_frsize;
    if (total <= 0.0)
        return;

    const double used = total - available;
    m_diskTotalGb = total / 1073741824.0;
    m_diskUsedGb = used / 1073741824.0;
    m_diskPercent = clampPercent(used * 100.0 / total);
}

QVector<ProcessInfo> SystemMonitor::readProcessSnapshot(
    int count, quint64 totalDelta, QHash<int, quint64> &previousTicks) const
{
    QVector<ProcessInfo> processes;
    QHash<int, quint64> currentTicks;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        bool pidOk = false;
        const int pid = entry.toInt(&pidOk);
        if (!pidOk || pid <= 0)
            continue;

        QFile file(QStringLiteral("/proc/") + entry + QStringLiteral("/stat"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        const QString stat = QString::fromUtf8(file.readAll());
        const int nameStart = stat.indexOf(QLatin1Char('('));
        const int nameEnd = stat.lastIndexOf(QLatin1Char(')'));
        if (nameStart < 0 || nameEnd <= nameStart)
            continue;

        const QStringList fields = splitBySpace(stat.mid(nameEnd + 2));
        if (fields.size() < 22)
            continue;

        bool ok = false;
        const quint64 userTicks = fields[11].toULongLong(&ok);
        if (!ok)
            continue;
        const quint64 systemTicks = fields[12].toULongLong(&ok);
        if (!ok)
            continue;
        const quint64 startTicks = fields[19].toULongLong(&ok);
        if (!ok)
            continue;
        const qint64 rssPages = fields[21].toLongLong(&ok);
        if (!ok)
            continue;

        const quint64 ticks = userTicks + systemTicks;
        currentTicks.insert(pid, ticks);

        ProcessInfo info;
        info.pid = pid;
        info.startTicks = startTicks;
        info.name = stat.mid(nameStart + 1, nameEnd - nameStart - 1);
        info.executablePath =
            QFileInfo(QStringLiteral("/proc/") + entry +
                      QStringLiteral("/exe")).symLinkTarget();
        if (info.executablePath.isEmpty()) {
            QFile commandFile(QStringLiteral("/proc/") + entry +
                              QStringLiteral("/cmdline"));
            if (commandFile.open(QIODevice::ReadOnly)) {
                QByteArray command = commandFile.read(512);
                command.replace('\0', ' ');
                info.executablePath =
                    QString::fromLocal8Bit(command).trimmed();
            }
        }
        info.rssMb = std::max<qint64>(0, rssPages) *
                     static_cast<double>(::getpagesize()) / 1048576.0;

        const quint64 previous = previousTicks.value(pid, ticks);
        if (totalDelta > 0 && ticks >= previous) {
            // /proc/stat 的总量包含全部核心，因此这里乘核心数得到常见的
            // “单进程可到 100%/核”的显示语义。
            info.cpuPercent = clampPercent(
                (ticks - previous) * 100.0 * m_coreCount / totalDelta);
        }
        processes.append(info);
    }

    previousTicks = currentTicks;
    std::sort(processes.begin(), processes.end(),
              [](const ProcessInfo &a, const ProcessInfo &b) {
        if (std::abs(a.cpuPercent - b.cpuPercent) > 0.05)
            return a.cpuPercent > b.cpuPercent;
        return a.rssMb > b.rssMb;
    });
    if (count > 0 && processes.size() > count)
        processes.resize(count);
    return processes;
}

SystemMonitor::ProcessDetails SystemMonitor::readProcessDetails(int pid) const
{
    ProcessDetails details;
    if (pid <= 0)
        return details;

    const QString procBase = QStringLiteral("/proc/%1/").arg(pid);
    QFile statFile(procBase + QStringLiteral("stat"));
    if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return details;
    const QString stat = QString::fromUtf8(statFile.readAll());
    const int nameStart = stat.indexOf(QLatin1Char('('));
    const int nameEnd = stat.lastIndexOf(QLatin1Char(')'));
    if (nameStart < 0 || nameEnd <= nameStart)
        return details;
    const QStringList statFields = splitBySpace(stat.mid(nameEnd + 2));
    if (statFields.size() < 22)
        return details;
    bool ok = false;
    details.startTicks = statFields[19].toULongLong(&ok);
    if (!ok)
        return details;
    details.name = stat.mid(nameStart + 1, nameEnd - nameStart - 1);

    QFile statusFile(procBase + QStringLiteral("status"));
    if (!statusFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return details;
    while (!statusFile.atEnd()) {
        const QString line = QString::fromUtf8(statusFile.readLine()).trimmed();
        const int separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0)
            continue;
        const QString key = line.left(separator);
        const QString value = line.mid(separator + 1).trimmed();
        const QString firstValue = value.section(QLatin1Char(' '), 0, 0);
        if (key == QLatin1String("Name"))
            details.name = value;
        else if (key == QLatin1String("State"))
            details.state = value;
        else if (key == QLatin1String("PPid"))
            details.parentPid = firstValue.toInt();
        else if (key == QLatin1String("Threads"))
            details.threadCount = firstValue.toInt();
        else if (key == QLatin1String("Uid"))
            details.uid = firstValue.toInt();
        else if (key == QLatin1String("VmRSS"))
            details.rssMb = firstValue.toDouble() / 1024.0;
        else if (key == QLatin1String("VmSize"))
            details.virtualMemoryMb = firstValue.toDouble() / 1024.0;
        else if (key == QLatin1String("voluntary_ctxt_switches"))
            details.voluntaryContextSwitches = firstValue.toULongLong();
        else if (key == QLatin1String("nonvoluntary_ctxt_switches"))
            details.involuntaryContextSwitches = firstValue.toULongLong();
    }

    const QString executable = QFileInfo(
        procBase + QStringLiteral("exe")).symLinkTarget();
    details.executableName = QFileInfo(executable).fileName();
    if (details.executableName.isEmpty())
        details.executableName = details.name;

    const QDir fdDirectory(procBase + QStringLiteral("fd"));
    if (fdDirectory.isReadable()) {
        details.fileDescriptorCount = fdDirectory.entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot).size();
    }

    QFile ioFile(procBase + QStringLiteral("io"));
    if (ioFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!ioFile.atEnd()) {
            const QString line = QString::fromUtf8(ioFile.readLine()).trimmed();
            const int separator = line.indexOf(QLatin1Char(':'));
            if (separator <= 0)
                continue;
            const QString key = line.left(separator);
            const quint64 value = line.mid(separator + 1).trimmed().toULongLong();
            if (key == QLatin1String("read_bytes")) {
                details.readBytes = value;
                details.hasIoCounters = true;
            } else if (key == QLatin1String("write_bytes")) {
                details.writeBytes = value;
                details.hasIoCounters = true;
            }
        }
    }
    details.valid = true;
    return details;
}

QVector<ProcessInfo> SystemMonitor::readTopProcesses(int count,
                                                     quint64 totalDelta)
{
    return readProcessSnapshot(count, totalDelta, m_prevProcTicks);
}

SystemMonitor::DiskIoTotals SystemMonitor::readDiskIoTotals() const
{
    DiskIoTotals totals;
    QFile file(QStringLiteral("/proc/diskstats"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return totals;

    static const QRegularExpression devicePattern(
        QStringLiteral("^(sd[a-z]+|vd[a-z]+|xvd[a-z]+|nvme\\d+n\\d+|"
                       "mmcblk\\d+)$"));
    while (!file.atEnd()) {
        const QList<QByteArray> fields = file.readLine().simplified().split(' ');
        if (fields.size() < 14)
            continue;
        const QString device = QString::fromLatin1(fields.value(2));
        if (!devicePattern.match(device).hasMatch())
            continue;
        totals.readSectors += fields.value(5).toULongLong();
        totals.writeSectors += fields.value(9).toULongLong();
    }
    return totals;
}

QVector<double> SystemMonitor::readLoadAverages() const
{
    QFile file(QStringLiteral("/proc/loadavg"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QList<QByteArray> fields = file.readLine().simplified().split(' ');
    if (fields.size() < 3)
        return {};
    return { fields[0].toDouble(), fields[1].toDouble(),
             fields[2].toDouble() };
}

void SystemMonitor::appendHistory(QVector<double> &history, double value)
{
    if (history.size() >= HISTORY_POINTS)
        history.removeFirst();
    history.append(clampPercent(value));
}

void SystemMonitor::refreshStats()
{
    // 构造时允许完成一次首屏采样；之后不可见时完全跳过 I/O。
    if (m_tick > 0 && !isVisible())
        return;

    const CpuTotals current = readCpuTotals();
    const quint64 totalDelta =
        current.total > m_prevCpu.total ? current.total - m_prevCpu.total : 0;
    const quint64 idleDelta =
        current.idle > m_prevCpu.idle ? current.idle - m_prevCpu.idle : 0;

    const quint64 minimumUsefulDelta =
        static_cast<quint64>(std::max(1, m_coreCount)) * 5;
    if (totalDelta >= minimumUsefulDelta)
        m_cpuPercent =
            clampPercent((totalDelta - idleDelta) * 100.0 / totalDelta);

    const int coreCount = std::min(current.coreTotal.size(), 8);
    if (m_corePercents.size() != coreCount)
        m_corePercents = QVector<double>(coreCount, 0.0);
    for (int i = 0; i < coreCount; ++i) {
        const quint64 coreDelta =
            current.coreTotal[i] > m_prevCpu.coreTotal.value(i)
            ? current.coreTotal[i] - m_prevCpu.coreTotal.value(i) : 0;
        const quint64 coreIdleDelta =
            current.coreIdle.value(i) > m_prevCpu.coreIdle.value(i)
            ? current.coreIdle.value(i) - m_prevCpu.coreIdle.value(i) : 0;
        if (coreDelta >= 2) {
            m_corePercents[i] = clampPercent(
                (coreDelta - coreIdleDelta) * 100.0 / coreDelta);
        }
    }
    m_prevCpu = current;
    m_coreCount = current.coreTotal.isEmpty()
        ? std::max(1, QThread::idealThreadCount())
        : current.coreTotal.size();
    m_coreFrequenciesGhz.resize(std::min(8, m_coreCount));
    for (int i = 0; i < m_coreFrequenciesGhz.size(); ++i)
        m_coreFrequenciesGhz[i] = readCoreFrequencyGhz(i);

    const MemStats memory = readMemStats();
    if (memory.totalKiB > 0) {
        const quint64 usedKiB = memory.totalKiB - memory.availableKiB;
        m_memPercent =
            clampPercent(usedKiB * 100.0 / memory.totalKiB);
        m_memUsedGb = usedKiB / 1048576.0;
        m_memTotalGb = memory.totalKiB / 1048576.0;
        m_memCacheGb = memory.cacheKiB / 1048576.0;
    }

    if (m_tick == 0 || (m_tick % HEAVY_EVERY_TICKS) == 0) {
        if (!m_compact) {
            const quint64 processDelta =
                current.total > m_lastProcTotal
                ? current.total - m_lastProcTotal : 0;
            const QVector<ProcessInfo> top =
                readTopProcesses(5, processDelta);
            if (!top.isEmpty())
                m_topProcs = top;
        }
        m_lastProcTotal = current.total;
        readDiskStats();
    }
    ++m_tick;

    appendHistory(m_cpuHistory, m_cpuPercent);
    appendHistory(m_memHistory, m_memPercent);
    appendHistory(m_diskHistory, m_diskPercent);
    update();
}

double SystemMonitor::readCoreFrequencyGhz(int core) const
{
    const QString base = QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/")
                             .arg(core);
    const QStringList files = {
        base + QStringLiteral("scaling_cur_freq"),
        base + QStringLiteral("cpuinfo_cur_freq"),
        base + QStringLiteral("scaling_max_freq")
    };
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        bool ok = false;
        const double khz = file.readAll().trimmed().toDouble(&ok);
        if (ok && khz > 0.0)
            return khz / 1000000.0;
    }
    return 0.0;
}

SystemMonitor::Palette SystemMonitor::paletteForSkin() const
{
    Palette palette;
    switch (m_skin) {
    case Skin::Light:
        palette = { QColor(248, 250, 252, 242), QColor(255, 255, 255, 220),
                    QColor(100, 116, 139, 55), QColor(30, 41, 59),
                    QColor(100, 116, 139), QColor(79, 70, 229),
                    QColor(5, 150, 105), QColor(217, 119, 6),
                    QColor(100, 116, 139, 35) };
        break;
    case Skin::Cyber:
        palette = { QColor(10, 10, 22, 235), QColor(18, 18, 44, 225),
                    QColor(0, 240, 255, 95), QColor(235, 250, 255),
                    QColor(138, 167, 181), QColor(0, 240, 255),
                    QColor(255, 0, 127), QColor(250, 204, 21),
                    QColor(0, 240, 255, 28) };
        break;
    case Skin::Glass:
        palette = { QColor(22, 29, 43, 175), QColor(30, 41, 59, 150),
                    QColor(255, 255, 255, 62), QColor(241, 245, 249),
                    QColor(186, 200, 220), QColor(129, 140, 248),
                    QColor(52, 211, 153), QColor(251, 191, 36),
                    QColor(255, 255, 255, 28) };
        break;
    case Skin::Wallpaper: {
        QColor accent = m_wallpaperThemeColor.isValid()
            ? m_wallpaperThemeColor : QColor(79, 70, 229);
        const int luminance =
            (accent.red() * 299 + accent.green() * 587 +
             accent.blue() * 114) / 1000;
        QColor panel = accent.darker(luminance > 145 ? 210 : 155);
        QColor card = accent.darker(luminance > 145 ? 165 : 125);
        palette = { QColor(panel.red(), panel.green(), panel.blue(), 232),
                    QColor(card.red(), card.green(), card.blue(), 205),
                    QColor(accent.red(), accent.green(), accent.blue(), 105),
                    QColor(248, 250, 252), QColor(203, 213, 225),
                    accent.lighter(145), accent.lighter(120),
                    QColor(251, 191, 36),
                    QColor(accent.red(), accent.green(), accent.blue(), 38) };
        break;
    }
    case Skin::Liquid:
        // The Snell material is sampled once from the desktop wallpaper;
        // these light cards preserve contrast for live metrics and labels.
        palette = { QColor(14, 22, 34, 82), QColor(23, 31, 43, 68),
                    QColor(232, 244, 255, 46), QColor(250, 252, 255),
                    QColor(206, 219, 234), QColor(139, 190, 255),
                    QColor(101, 226, 176), QColor(255, 207, 120),
                    QColor(235, 246, 255, 24) };
        break;
    case Skin::Dark:
    default:
        palette = { QColor(15, 23, 42, 235), QColor(2, 6, 23, 118),
                    QColor(148, 163, 184, 45), QColor(241, 245, 249),
                    QColor(148, 163, 184), QColor(99, 102, 241),
                    QColor(16, 185, 129), QColor(245, 158, 11),
                    QColor(148, 163, 184, 28) };
        break;
    }

    const auto applyOpacity = [this](QColor color) {
        color.setAlpha(qRound(color.alpha() * m_windowOpacity / 100.0));
        return color;
    };
    palette.panel = applyOpacity(palette.panel);
    palette.card = applyOpacity(palette.card);
    palette.border = applyOpacity(palette.border);
    palette.track = applyOpacity(palette.track);
    return palette;
}

QColor SystemMonitor::wallpaperSampleColor() const
{
    const auto *canvas = qobject_cast<const DesktopCanvas *>(parentWidget());
    if (!canvas)
        return QColor();
    return canvas->wallpaperColorAt(geometry().adjusted(-40, -40, 40, 40));
}

void SystemMonitor::refreshWallpaperTheme()
{
    m_liquidGlassImage = {};
    m_liquidGlassGeometry = {};
    const QColor sampled = wallpaperSampleColor();
    if (sampled.isValid())
        m_wallpaperThemeColor = sampled;
    if (m_skin == Skin::Wallpaper)
        update();
    emit appearanceChanged();
}

void SystemMonitor::drawRing(QPainter &p, const QRectF &rect,
                             double percent, const QColor &accent,
                             const QColor &track)
{
    QPen pen(track, 5, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(rect, 0, 360 * 16);

    pen.setColor(accent);
    p.setPen(pen);
    p.drawArc(rect, 90 * 16,
              -qRound(360.0 * clampPercent(percent) / 100.0 * 16.0));
}

void SystemMonitor::drawSparkline(QPainter &p, const QRectF &rect,
                                  const QVector<double> &history,
                                  const QColor &accent)
{
    if (history.size() < 2)
        return;

    QPainterPath path;
    const qreal step = rect.width() / (history.size() - 1);
    for (int i = 0; i < history.size(); ++i) {
        const QPointF point(rect.left() + i * step,
            rect.bottom() - rect.height() * clampPercent(history[i]) / 100.0);
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    p.setPen(QPen(accent, 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void SystemMonitor::drawMetricCard(QPainter &p, const QRect &rect,
                                   const QString &title,
                                   const QString &detail,
                                   double percent,
                                   const QColor &accent,
                                   const QVector<double> &history)
{
    const Palette colors = paletteForSkin();
    if (m_premiumAesthetics) {
        QLinearGradient grad(rect.topLeft(), rect.bottomRight());
        QColor cStart = colors.card;
        cStart.setAlpha(qMin(255, cStart.alpha() + 25));
        QColor cEnd = colors.card;
        cEnd.setAlpha(qMax(0, cEnd.alpha() - 20));
        grad.setColorAt(0, cStart);
        grad.setColorAt(1, cEnd);
        p.setBrush(grad);
        p.setPen(QPen(colors.border, 1));
        p.drawRoundedRect(rect, 12, 12);
        
        // Glare highlight line
        p.setPen(QPen(QColor(255, 255, 255, 45), 1));
        p.drawLine(rect.left() + 10, rect.top() + 1, rect.right() - 10, rect.top() + 1);
    } else {
        p.setPen(QPen(colors.border, 1));
        p.setBrush(colors.card);
        p.drawRoundedRect(rect, 10, 10);
    }

    QFont titleFont = uiFont(2, true);
    p.setFont(titleFont);
    p.setPen(colors.muted);
    p.drawText(rect.adjusted(10, 8, -8, 0),
               Qt::AlignLeft | Qt::AlignTop, title);

    const QRect ringRect(rect.left() + 12, rect.top() + 38, 66, 66);
    drawRing(p, ringRect, percent, accent, colors.track);

    QFont valueFont = uiFont(7, true);
    p.setFont(valueFont);
    p.setPen(colors.text);
    p.drawText(ringRect, Qt::AlignCenter,
               QStringLiteral("%1%").arg(percent, 0, 'f', 0));

    QFont detailFont = uiFont(1);
    p.setFont(detailFont);
    p.setPen(colors.muted);
    const QRect detailRect(ringRect.right() + 12, ringRect.top(),
                           rect.right() - ringRect.right() - 22, 42);
    p.drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter |
               Qt::TextWordWrap, detail);

    drawSparkline(p, QRectF(ringRect.right() + 12, ringRect.bottom() - 9,
                            rect.right() - ringRect.right() - 22, 13),
                  history, accent);
}

void SystemMonitor::drawButton(QPainter &p, const QRect &rect,
                               const QString &text,
                               const QColor &foreground,
                               const QColor &background,
                               const QColor &border)
{
    p.setPen(QPen(border, 1));
    p.setBrush(background);
    p.drawRoundedRect(rect, 7, 7);
    QFont buttonFont = uiFont(0, true);
    p.setFont(buttonFont);
    p.setPen(foreground);
    p.drawText(rect, Qt::AlignCenter, text);
}

QSize SystemMonitor::baseSize() const
{
    return m_compact ? QSize(COMPACT_W, COMPACT_H)
                     : QSize(EXPANDED_W, EXPANDED_H + m_aiExtraHeight);
}

QFont SystemMonitor::uiFont(int sizeOffset, bool bold) const
{
    QFont result = font();
    if (!m_widgetFontFamily.isEmpty())
        result.setFamily(m_widgetFontFamily);
    result.setPixelSize(qBound(7, m_widgetFontSize + sizeOffset, 28));
    result.setBold(bold);
    return result;
}

int SystemMonitor::logicalWidth() const
{
    return baseSize().width();
}

int SystemMonitor::logicalHeight() const
{
    return baseSize().height();
}

QPoint SystemMonitor::logicalPosition(const QPoint &position) const
{
    return QPoint(qRound(position.x() / m_scale),
                  qRound(position.y() / m_scale));
}

void SystemMonitor::updateHitRects()
{
    const int y = 12;
    const int h = 30;
    m_settingsRect = QRect(logicalWidth() - 46, y, 30, h);
    m_skinRect = QRect(m_settingsRect.left() - 38, y, 30, h);
    m_toggleRect = QRect(m_skinRect.left() - 92, y, 84, h);
    m_optimizeRect = QRect(m_toggleRect.left() - 98, y, 90, h);

    if (!m_compact) {
        const int panelTop = 236;
        const int gap = 12;
        const int panelHeight = logicalHeight() - panelTop - 16;
        const int leftWidth =
            qRound((logicalWidth() - MARGIN * 2 - gap) * 0.40);
        const QRect aiRect(MARGIN + leftWidth + gap, panelTop,
                           logicalWidth() - MARGIN * 2 - leftWidth - gap,
                           panelHeight);
        m_aiRunRect = QRect(aiRect.left() + 12, aiRect.bottom() - 42,
                            aiRect.width() - 58, 30);
        m_aiClearRect = QRect(aiRect.right() - 38, aiRect.bottom() - 42,
                              26, 30);
    } else {
        m_aiRunRect = QRect();
        m_aiClearRect = QRect();
    }
    m_resizeHandleRect =
        QRect(logicalWidth() - 22, logicalHeight() - 22, 18, 18);
}

void SystemMonitor::drawHeader(QPainter &p, const Palette &colors)
{
    updateHitRects();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0,
        qRound((m_skin == Skin::Light ? 10 : 35) *
               m_windowOpacity / 100.0)));
    p.drawRoundedRect(QRect(1, 1, logicalWidth() - 2, HEADER_H), 15, 15);

    p.setBrush(QColor(16, 185, 129));
    p.drawEllipse(QPoint(MARGIN + 6, HEADER_H / 2), 5, 5);

    QFont titleFont = uiFont(3, true);
    p.setFont(titleFont);
    p.setPen(colors.text);
    p.drawText(QRect(MARGIN + 22, 0, m_optimizeRect.left() - MARGIN - 30,
                     HEADER_H),
               Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(titleFont).elidedText(
                   m_widgetTitle, Qt::ElideRight,
                   m_optimizeRect.left() - MARGIN - 34));

    drawButton(p, m_optimizeRect, QStringLiteral("⚡ 立即刷新"),
               colors.cpu, QColor(colors.cpu.red(), colors.cpu.green(),
                                  colors.cpu.blue(), 25),
               QColor(colors.cpu.red(), colors.cpu.green(),
                      colors.cpu.blue(), 70));
    drawButton(p, m_toggleRect,
               m_compact ? QStringLiteral("展开视图")
                         : QStringLiteral("紧凑模式"),
               colors.text, colors.card, colors.border);
    drawButton(p, m_skinRect, QStringLiteral("◈"),
               colors.text, colors.card, colors.border);
    drawButton(p, m_settingsRect, QStringLiteral("⚙"),
               colors.cpu, QColor(colors.cpu.red(), colors.cpu.green(),
                                  colors.cpu.blue(), 25),
               QColor(colors.cpu.red(), colors.cpu.green(),
                      colors.cpu.blue(), 70));

    if (m_editMode) {
        p.setPen(QPen(colors.cpu, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(
            QRect(2, 2, logicalWidth() - 4, logicalHeight() - 4),
            12, 12);
    }
}

void SystemMonitor::drawProcessPanel(QPainter &p, const QRect &rect,
                                     const Palette &colors)
{
    if (m_premiumAesthetics) {
        QLinearGradient grad(rect.topLeft(), rect.bottomRight());
        QColor cStart = colors.card;
        cStart.setAlpha(qMin(255, cStart.alpha() + 25));
        QColor cEnd = colors.card;
        cEnd.setAlpha(qMax(0, cEnd.alpha() - 20));
        grad.setColorAt(0, cStart);
        grad.setColorAt(1, cEnd);
        p.setBrush(grad);
        p.setPen(QPen(colors.border, 1));
        p.drawRoundedRect(rect, 12, 12);
        
        // Glare highlight line
        p.setPen(QPen(QColor(255, 255, 255, 45), 1));
        p.drawLine(rect.left() + 10, rect.top() + 1, rect.right() - 10, rect.top() + 1);
    } else {
        p.setPen(QPen(colors.border, 1));
        p.setBrush(colors.card);
        p.drawRoundedRect(rect, 10, 10);
    }

    QFont heading = uiFont(2, true);
    p.setFont(heading);
    p.setPen(colors.text);
    p.drawText(rect.adjusted(14, 12, -12, 0),
               Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("高负载进程  TOP 5"));

    QFont rowFont = uiFont();
    p.setFont(rowFont);

    p.setPen(colors.muted);
    p.drawText(QRect(rect.right() - 160, rect.top() + 9, 52, 18),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("PID"));
    p.drawText(QRect(rect.right() - 101, rect.top() + 9, 42, 18),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("CPU"));
    p.drawText(QRect(rect.right() - 50, rect.top() + 9, 38, 18),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("内存"));

    m_processRowRects.clear();
    int y = rect.top() + 38;
    for (int i = 0; i < m_topProcs.size() && i < 5; ++i) {
        const ProcessInfo &process = m_topProcs[i];
        if (i > 0) {
            p.setPen(QPen(colors.border, 1));
            p.drawLine(rect.left() + 12, y - 3, rect.right() - 12, y - 3);
        }

        p.setPen(colors.text);
        m_processRowRects.append(
            QRect(rect.left() + 8, y - 2, rect.width() - 16, 22));
        const QString name = QFontMetrics(rowFont).elidedText(
            process.name, Qt::ElideRight, rect.width() - 190);
        p.drawText(QRect(rect.left() + 12, y, rect.width() - 190, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, name);

        p.setPen(colors.muted);
        p.drawText(QRect(rect.right() - 172, y, 48, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(process.pid));
        p.setPen(colors.cpu);
        p.drawText(QRect(rect.right() - 116, y, 48, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1%").arg(process.cpuPercent, 0, 'f', 1));
        p.setPen(colors.memory);
        p.drawText(QRect(rect.right() - 61, y, 49, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   process.rssMb >= 1024.0
                       ? QStringLiteral("%1G").arg(process.rssMb / 1024.0, 0, 'f', 1)
                       : QStringLiteral("%1M").arg(process.rssMb, 0, 'f', 0));
        y += 24;
    }
}

void SystemMonitor::drawAiPanel(QPainter &p, const QRect &rect,
                                const Palette &colors)
{
    if (m_premiumAesthetics) {
        QLinearGradient grad(rect.topLeft(), rect.bottomRight());
        QColor cStart = colors.card;
        cStart.setAlpha(qMin(255, cStart.alpha() + 25));
        QColor cEnd = colors.card;
        cEnd.setAlpha(qMax(0, cEnd.alpha() - 20));
        grad.setColorAt(0, cStart);
        grad.setColorAt(1, cEnd);
        p.setBrush(grad);
        p.setPen(QPen(colors.border, 1));
        p.drawRoundedRect(rect, 12, 12);
        
        // Glare highlight line
        p.setPen(QPen(QColor(255, 255, 255, 45), 1));
        p.drawLine(rect.left() + 10, rect.top() + 1, rect.right() - 10, rect.top() + 1);
    } else {
        p.setPen(QPen(colors.border, 1));
        p.setBrush(colors.card);
        p.drawRoundedRect(rect, 10, 10);
    }

    QFont heading = uiFont(2, true);
    p.setFont(heading);
    p.setPen(colors.text);
    const QString diagnosisTitle = m_hasDiagnosisTarget
        ? QStringLiteral("◆ %1 · 进程诊断").arg(m_diagnosisTarget.name)
        : QStringLiteral("◆ DeepSeek 智能诊断");
    p.drawText(QRect(rect.left() + 14, rect.top() + 10,
                     rect.width() - 130, 20),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(heading).elidedText(
                   diagnosisTitle, Qt::ElideRight, rect.width() - 144));

    QFont modelFont = uiFont(-1);
    p.setFont(modelFont);
    p.setPen(colors.cpu);
    p.drawText(QRect(rect.right() - 124, rect.top() + 10, 110, 20),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("DeepSeek-V4"));

    const QRect content(rect.left() + 12, rect.top() + 40,
                         rect.width() - 24, rect.height() - 92);
    m_aiContentRect = content;
    p.setPen(QPen(colors.border, 1));
    p.setBrush(QColor(0, 0, 0,
        qRound((m_skin == Skin::Light ? 10 : 35) *
               m_windowOpacity / 100.0)));
    p.drawRoundedRect(content, 7, 7);

    QFont body = uiFont();
    p.setFont(body);
    const QString displayText = !m_aiError.isEmpty() ? m_aiError : m_aiText;
    if (!displayText.isEmpty()) {
        const QRect measured = QFontMetrics(body).boundingRect(
            QRect(0, 0, content.width() - 34, 10000),
            Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, displayText);
        m_aiContentHeight = measured.height() + 20;
    } else {
        m_aiContentHeight = 0;
        m_aiScrollOffset = 0;
    }
    const int viewportHeight = content.height() - 16;
    const int maxScroll = qMax(0, m_aiContentHeight - viewportHeight);
    m_aiScrollOffset = qBound(0, m_aiScrollOffset, maxScroll);
    const QRect textRect(content.left() + 12, content.top() + 10,
                         content.width() - 34,
                         qMax(viewportHeight, m_aiContentHeight));

    if (m_aiBusy) {
        p.setPen(colors.cpu);
        p.drawText(content.adjusted(12, 10, -12, -10),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   m_aiProgressText.isEmpty()
                       ? QStringLiteral("正在分析遥测数据…")
                       : m_aiProgressText);
    } else if (!m_aiError.isEmpty()) {
        p.save();
        p.setClipRect(content.adjusted(1, 1, -1, -1));
        p.translate(0, -m_aiScrollOffset);
        p.setPen(QColor(248, 113, 113));
        p.drawText(textRect,
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   m_aiError);
        p.restore();
    } else if (!m_aiText.isEmpty()) {
        p.save();
        p.setClipRect(content.adjusted(1, 1, -1, -1));
        p.translate(0, -m_aiScrollOffset);
        p.setPen(colors.text);
        p.drawText(textRect,
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   m_aiText);
        p.restore();
    } else {
        p.setPen(colors.muted);
        p.drawText(content.adjusted(16, 12, -16, -12),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   QStringLiteral("已就绪。点击下方按钮，发送遥测数据至 DeepSeek 智能诊断。"));
    }

    if (maxScroll > 0) {
        const QRect track(content.right() - 8, content.top() + 8,
                          3, content.height() - 16);
        const int thumbHeight =
            qMax(22, track.height() * viewportHeight / m_aiContentHeight);
        const int thumbY = track.top() +
            (track.height() - thumbHeight) * m_aiScrollOffset / maxScroll;
        p.setPen(Qt::NoPen);
        p.setBrush(colors.track);
        p.drawRoundedRect(track, 2, 2);
        p.setBrush(colors.cpu);
        p.drawRoundedRect(QRect(track.left(), thumbY,
                                track.width(), thumbHeight), 2, 2);
    }

    drawButton(p, m_aiRunRect,
               m_diagnosisSampling
                   ? QStringLiteral("%1采样 %2/%3")
                         .arg(m_hasDiagnosisTarget
                                  ? QStringLiteral("进程深度")
                                  : QStringLiteral("趋势"))
                         .arg(m_diagnosisSamplesTaken)
                         .arg(DIAGNOSIS_SAMPLE_COUNT)
                   : m_aiBusy ? QStringLiteral("DeepSeek 分析中…")
                        : QStringLiteral("执行深度诊断"),
               Qt::white, QColor(79, 70, 229),
               QColor(99, 102, 241));
    drawButton(p, m_aiClearRect, QStringLiteral("×"),
               colors.muted, colors.track, colors.border);
}

void SystemMonitor::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(m_scale, m_scale);
    const Palette colors = paletteForSkin();

    if (m_skin == Skin::Liquid) {
        if (m_liquidGlassImage.isNull() || m_liquidGlassGeometry != geometry()) {
            if (auto *canvas = qobject_cast<DesktopCanvas *>(parentWidget())) {
                m_liquidGlassImage = canvas->renderLiquidGlass(
                    geometry(), 14.0 * m_scale);
                m_liquidGlassGeometry = geometry();
            }
        }
        if (!m_liquidGlassImage.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(0, 0, logicalWidth(), logicalHeight()),
                              m_liquidGlassImage);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.panel);
            painter.drawRoundedRect(QRect(0, 0, logicalWidth(), logicalHeight()), 14, 14);
        }

        const QPointF pointer = logicalPosition(m_liquidGlassPointer.toPoint());
        if (m_hovered && pointer.x() >= 0 && pointer.y() >= 0 &&
            pointer.x() < logicalWidth() && pointer.y() < logicalHeight()) {
            QPainterPath rim;
            rim.addRoundedRect(QRectF(1, 1, logicalWidth() - 2,
                                     logicalHeight() - 2), 13, 13);
            QRadialGradient reflection(pointer, 112);
            reflection.setColorAt(0, QColor(233, 247, 255, 135));
            reflection.setColorAt(0.38, QColor(193, 225, 255, 48));
            reflection.setColorAt(1, Qt::transparent);
            painter.save();
            painter.setClipPath(rim);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QBrush(reflection), 1.6));
            painter.drawPath(rim);
            reflection.setColorAt(0, QColor(208, 234, 255, 22));
            reflection.setColorAt(0.45, QColor(193, 225, 255, 8));
            painter.setPen(QPen(QBrush(reflection), 8));
            painter.drawPath(rim);
            painter.restore();
        }
    } else if (m_premiumAesthetics) {
        // Draw main panel with double border & soft glow
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.panel);
        painter.drawRoundedRect(QRect(0, 0, logicalWidth(), logicalHeight()), 14, 14);

        // Soft outer glow using accent color (CPU color)
        QColor glowColor = colors.cpu;
        glowColor.setAlpha(25);
        painter.setPen(QPen(glowColor, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRect(1, 1, logicalWidth() - 2, logicalHeight() - 2), 13, 13);

        // Crisp inner border
        painter.setPen(QPen(colors.border, 1));
        painter.drawRoundedRect(QRect(0, 0, logicalWidth() - 1, logicalHeight() - 1), 14, 14);
    } else {
        painter.setPen(QPen(colors.border, 1));
        painter.setBrush(colors.panel);
        painter.drawRoundedRect(
            QRect(0, 0, logicalWidth() - 1, logicalHeight() - 1), 14, 14);
    }
    drawHeader(painter, colors);

    const int cardY = HEADER_H + 14;
    const int cardGap = 12;
    const int cardWidth =
        (logicalWidth() - MARGIN * 2 - cardGap * 2) / 3;
    const int cardHeight = 156;

    const QString cpuDetail =
        QStringLiteral("%1\n%2 核心").arg(m_cpuModel).arg(m_coreCount);
    const QString memDetail =
        QStringLiteral("%1 / %2 GB\n缓存 %3 GB")
            .arg(m_memUsedGb, 0, 'f', 1)
            .arg(m_memTotalGb, 0, 'f', 1)
            .arg(m_memCacheGb, 0, 'f', 1);
    const QString diskDetail =
        QStringLiteral("%1 / %2 GB\n主目录分区")
            .arg(m_diskUsedGb, 0, 'f', 0)
            .arg(m_diskTotalGb, 0, 'f', 0);

    const QRect cpuCard(MARGIN, cardY, cardWidth, cardHeight);
    const QRect memoryCard(MARGIN + cardWidth + cardGap, cardY,
                           cardWidth, cardHeight);
    const QRect diskCard(MARGIN + (cardWidth + cardGap) * 2, cardY,
                         cardWidth, cardHeight);
    m_cpuRingRect = QRect(cpuCard.left() + 12, cpuCard.top() + 38, 66, 66);
    m_memoryRingRect =
        QRect(memoryCard.left() + 12, memoryCard.top() + 38, 66, 66);
    m_diskRingRect =
        QRect(diskCard.left() + 12, diskCard.top() + 38, 66, 66);

    drawMetricCard(painter, cpuCard,
        QStringLiteral("CPU"), cpuDetail, m_cpuPercent,
        colors.cpu, m_cpuHistory);
    const int coreAreaX = cpuCard.left() + 92;
    const int coreAreaY = cpuCard.top() + 105;
    const int coreAreaW = cpuCard.right() - coreAreaX - 12;
    const int shownCores = std::min(8, std::max(1, m_coreCount));
    const int coreGap = 3;
    const int coreW =
        std::max(4, (coreAreaW - coreGap * (shownCores - 1)) / shownCores);
    m_coreRects.clear();
    for (int i = 0; i < shownCores; ++i) {
        const QRect track(coreAreaX + i * (coreW + coreGap),
                          coreAreaY, coreW, 36);
        m_coreRects.append(track);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.track);
        painter.drawRoundedRect(track, 2, 2);
        const double corePercent = m_corePercents.value(i, m_cpuPercent);
        const int fillH = qRound(track.height() * corePercent / 100.0);
        if (fillH > 0) {
            painter.setBrush(corePercent > 80.0
                                 ? QColor(244, 63, 94)
                                 : corePercent > 65.0
                                       ? QColor(245, 158, 11)
                                       : colors.cpu);
            painter.drawRoundedRect(
                QRect(track.left(), track.bottom() - fillH + 1,
                      track.width(), fillH), 2, 2);
        }
    }
    drawMetricCard(painter, memoryCard,
        QStringLiteral("内存"), memDetail, m_memPercent,
        colors.memory, m_memHistory);
    drawMetricCard(painter, diskCard,
        QStringLiteral("磁盘"), diskDetail, m_diskPercent,
        colors.disk, m_diskHistory);

    if (!m_compact) {
        const int panelTop = cardY + cardHeight + 12;
        const int panelGap = 12;
        const int panelHeight = logicalHeight() - panelTop - 16;
        // 进程面板缩窄至 34% 宽度（仅需容纳 PID/CPU/内存三列），
        // 剩余 66% 给智能诊断面板。
        const int leftWidth =
            qRound((logicalWidth() - MARGIN * 2 - panelGap) * 0.34);
        drawProcessPanel(painter,
            QRect(MARGIN, panelTop, leftWidth,
                  std::min(panelHeight, 188)), colors);
        drawAiPanel(painter,
            QRect(MARGIN + leftWidth + panelGap, panelTop,
                  logicalWidth() - MARGIN * 2 - leftWidth - panelGap,
                  panelHeight),
            colors);
    } else if (m_hovered && m_editMode) {
        QFont foot = uiFont(-1);
        painter.setFont(foot);
        painter.setPen(colors.muted);
        painter.drawText(QRect(MARGIN, logicalHeight() - 26,
                               logicalWidth() - MARGIN * 2, 14),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("编辑模式：拖动任意空白区域移动"));
    }

    if (m_editMode) {
        painter.setPen(QPen(colors.cpu, 2));
        painter.setBrush(QColor(colors.cpu.red(), colors.cpu.green(),
                                colors.cpu.blue(), 45));
        painter.drawRoundedRect(m_resizeHandleRect, 3, 3);
        painter.drawLine(m_resizeHandleRect.left() + 5,
                         m_resizeHandleRect.bottom() - 4,
                         m_resizeHandleRect.right() - 4,
                         m_resizeHandleRect.top() + 5);
    }
}

void SystemMonitor::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    updateAiLayoutHeight();
    resize(baseSize() * m_scale);
    if (!compact) {
        m_tick = 0;
        refreshStats();
        QTimer::singleShot(5500, this, [this] {
            if (isVisible() && !m_compact) {
                m_tick = 0;
                refreshStats();
            }
        });
    }
    move(boundedPosition(pos()));
    saveSettings();
    update();
}

void SystemMonitor::setSkin(Skin skin)
{
    if (m_skin == skin)
        return;
    m_skin = skin;
    if (skin == Skin::Wallpaper)
        refreshWallpaperTheme();
    saveSettings();
    update();
    emit appearanceChanged();
}

QPoint SystemMonitor::boundedPosition(const QPoint &position) const
{
    if (!parentWidget())
        return position;
    const QRect bounds = parentWidget()->rect();
    constexpr int snapDist = 32;  // 磁吸触发距离（像素）

    int x = qBound(bounds.left(), position.x(),
                   std::max(bounds.left(), bounds.right() - width() + 1));
    int y = qBound(bounds.top(), position.y(),
                   std::max(bounds.top(), bounds.bottom() - height() + 1));

    // 边缘磁吸：靠近屏幕边缘时自动吸附
    if (qAbs(x - bounds.left()) < snapDist)
        x = bounds.left();
    if (qAbs(x + width() - bounds.right()) < snapDist)
        x = bounds.right() - width();
    if (qAbs(y - bounds.top()) < snapDist)
        y = bounds.top();
    if (qAbs(y + height() - bounds.bottom()) < snapDist)
        y = bounds.bottom() - height();

    return QPoint(x, y);
}

void SystemMonitor::restoreSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("systemMonitor"));
    m_compact = settings.value(QStringLiteral("compact"), false).toBool();
    m_skin = static_cast<Skin>(
        qBound(0, settings.value(QStringLiteral("skin"),
                                 static_cast<int>(Skin::Liquid)).toInt(), 5));
    m_scale = qBound(0.65,
                     settings.value(QStringLiteral("scale"), 1.0).toDouble(),
                     1.60);
    m_statIntervalSec = qBound(
        1, settings.value(QStringLiteral("statInterval"),
                          DEFAULT_STAT_INTERVAL_SEC).toInt(), 60);
    m_autoStart = settings.value(QStringLiteral("autoStart"), true).toBool();
    m_premiumAesthetics = settings.value(QStringLiteral("premiumAesthetics"), true).toBool();
    m_apiUrl = settings.value(
        QStringLiteral("apiUrl"),
        QStringLiteral("https://api.deepseek.com/chat/completions")).toString();
    m_apiKey = settings.value(QStringLiteral("apiKey")).toString();
    const bool storedInKeyring = settings.value(QStringLiteral("credentialStore")).toString()
        == QStringLiteral("secret-service");
    // A legacy value is retained on disk until the keyring confirms the write.
    // Empty test profiles never connect to the user's keyring.
    if (!m_apiKey.isEmpty())
        saveCredential(m_apiKey);
    else if (storedInKeyring)
        runCredentialJob(QStringLiteral("read"));
    m_widgetTitle = settings.value(
        QStringLiteral("title"),
        QStringLiteral("飞腾桌面资源监控")).toString().trimmed();
    if (m_widgetTitle.isEmpty())
        m_widgetTitle = QStringLiteral("飞腾桌面资源监控");
    m_widgetFontFamily =
        settings.value(QStringLiteral("fontFamily")).toString();
    m_widgetFontSize = qBound(
        8, settings.value(QStringLiteral("fontSize"), 9).toInt(), 16);
    m_windowOpacity = qBound(
        25, settings.value(QStringLiteral("opacity"), 100).toInt(), 100);
    m_wallpaperThemeColor = QColor(
        settings.value(QStringLiteral("wallpaperThemeColor")).toString());
    if (!m_wallpaperThemeColor.isValid())
        m_wallpaperThemeColor = wallpaperSampleColor();
    resize(baseSize() * m_scale);

    QPoint position = settings.value(QStringLiteral("position"),
                                     QPoint(-1, -1)).toPoint();
    if (position.x() < 0 || position.y() < 0) {
        QSize area = parentWidget() ? parentWidget()->size() : QSize();
        if (area.isEmpty() && QApplication::primaryScreen())
            area = QApplication::primaryScreen()->size();
        position = QPoint(std::max(MARGIN, area.width() - width() - 24), 48);
    }
    move(boundedPosition(position));
    settings.endGroup();
    if (!m_apiKey.isEmpty()) {
        QFile::setPermissions(
            settings.fileName(),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void SystemMonitor::saveCredential(const QString &key)
{
    ++m_credentialRevision;
    m_pendingCredential = key;
    m_credentialWritePending = true;
    if (!m_credentialJob) {
        m_credentialWritePending = false;
        runCredentialJob(key.isEmpty() ? QStringLiteral("clear") : QStringLiteral("write"), key);
    }
}

void SystemMonitor::runCredentialJob(const QString &operation, const QString &key)
{
    const int revision = m_credentialRevision;
    auto *job = new QProcess(this);
    m_credentialJob = job;
    const QString configFile = QFileInfo(QSettings().fileName()).absoluteFilePath();
    const QString legacyKey = QSettings().value(QStringLiteral("systemMonitor/apiKey")).toString();
    auto finish = [this, job, operation, legacyKey, revision, configFile](bool success) {
        if (m_credentialJob != job)
            return;
        m_credentialJob = nullptr;
        if (success && operation == QLatin1String("read")) {
            const QJsonDocument data = QJsonDocument::fromJson(job->readAllStandardOutput());
            success = data.isObject() && !data.object().value("key").toString().isEmpty();
            if (success && revision == m_credentialRevision &&
                qgetenv("DEEPSEEK_API_KEY").trimmed().isEmpty())
                m_apiKey = data.object().value("key").toString();
        } else if (success) {
            QSettings settings(configFile, QSettings::IniFormat);
            settings.beginGroup(QStringLiteral("systemMonitor"));
            if (operation == QLatin1String("clear")) {
                settings.remove(QStringLiteral("credentialStore"));
                settings.remove(QStringLiteral("apiKey"));
            } else {
                settings.setValue(QStringLiteral("credentialStore"), QStringLiteral("secret-service"));
                if (settings.value(QStringLiteral("apiKey")).toString() == legacyKey)
                    settings.remove(QStringLiteral("apiKey"));
            }
            settings.sync();
            success = settings.status() == QSettings::NoError;
        }
        m_credentialFailed = !success;
        if (!success && !m_credentialWritePending)
            m_aiError = QStringLiteral("系统密钥环操作失败：旧凭据保持不变；新输入的密钥仅在本次运行有效，请解锁密钥环后重试。");
        job->deleteLater();
        update();
        if (m_credentialWritePending) {
            const QString pending = m_pendingCredential;
            m_credentialWritePending = false;
            runCredentialJob(pending.isEmpty() ? QStringLiteral("clear") : QStringLiteral("write"), pending);
        }
    };
    connect(job, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [finish](int code, QProcess::ExitStatus status) {
        finish(code == 0 && status == QProcess::NormalExit);
    });
    connect(job, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    auto *timeout = new QTimer(job);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, job, [job] { job->kill(); });
    timeout->start(30000);
    QString script = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../libexec/ukui-fences/credential_store.py");
    if (!QFileInfo::exists(script))
        script = QStringLiteral(UKUI_FENCES_CREDENTIAL_STORE_PATH);
    job->start(QStandardPaths::findExecutable(QStringLiteral("python3")),
               {script, operation, configFile});
    if (operation == QLatin1String("write"))
        job->write(QJsonDocument(QJsonObject{{"key", key}}).toJson(QJsonDocument::Compact));
    job->closeWriteChannel();
}

bool SystemMonitor::autoStartEnabled()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("systemMonitor"));
    const bool enabled = settings.value(QStringLiteral("autoStart"), true).toBool();
    settings.endGroup();
    return enabled;
}

void SystemMonitor::setAutoStart(bool enabled)
{
    if (m_autoStart == enabled)
        return;
    m_autoStart = enabled;
    saveSettings();
}

void SystemMonitor::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("systemMonitor"));
    settings.setValue(QStringLiteral("position"), pos());
    settings.setValue(QStringLiteral("compact"), m_compact);
    settings.setValue(QStringLiteral("skin"), static_cast<int>(m_skin));
    settings.setValue(QStringLiteral("scale"), m_scale);
    settings.setValue(QStringLiteral("statInterval"), m_statIntervalSec);
    settings.setValue(QStringLiteral("autoStart"), m_autoStart);
    settings.setValue(QStringLiteral("premiumAesthetics"), m_premiumAesthetics);
    settings.setValue(QStringLiteral("apiUrl"), m_apiUrl);
    // Credentials are persisted only by the asynchronous keyring job. General
    // layout saves must never copy an environment-provided key into this INI.
    settings.setValue(QStringLiteral("title"), m_widgetTitle);
    settings.setValue(QStringLiteral("fontFamily"), m_widgetFontFamily);
    settings.setValue(QStringLiteral("fontSize"), m_widgetFontSize);
    settings.setValue(QStringLiteral("opacity"), m_windowOpacity);
    settings.setValue(QStringLiteral("wallpaperThemeColor"),
                      m_wallpaperThemeColor.name(QColor::HexArgb));
    settings.sync();
    QFile::setPermissions(
        settings.fileName(),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    settings.endGroup();
}

QString SystemMonitor::diagnosticTelemetry() const
{
    QList<ProcessAggregate> processes = m_diagnosisProcesses.values();
    std::sort(processes.begin(), processes.end(),
              [](const ProcessAggregate &a, const ProcessAggregate &b) {
        if (std::abs(a.cpuSum - b.cpuSum) > 0.05)
            return a.cpuSum > b.cpuSum;
        return a.rssPeakMb > b.rssPeakMb;
    });

    QJsonArray processArray;
    for (int i = 0; i < processes.size() && i < 8; ++i) {
        const ProcessAggregate &process = processes[i];
        processArray.append(QJsonObject{
            { QStringLiteral("application"), process.name },
            { QStringLiteral("average_cpu_percent"),
              process.cpuSum / qMax(1, m_diagnosisSamplesTaken) },
            { QStringLiteral("peak_cpu_percent"), process.cpuPeak },
            { QStringLiteral("peak_rss_mb"), process.rssPeakMb }
        });
    }

    const QVector<double> loads = readLoadAverages();
    QJsonObject telemetry{
        { QStringLiteral("diagnosis_scope"),
          m_hasDiagnosisTarget ? QStringLiteral("single_process")
                               : QStringLiteral("whole_system") },
        { QStringLiteral("platform"),
          QStringLiteral("银河麒麟桌面操作系统 / UKUI / Linux") },
        { QStringLiteral("sample_window_seconds"),
          m_diagnosisSamplesTaken },
        { QStringLiteral("local_assessment"), localHealthAssessment() },
        { QStringLiteral("cpu"), QJsonObject{
            { QStringLiteral("model"), m_cpuModel },
            { QStringLiteral("logical_cores"), m_coreCount },
            { QStringLiteral("average_percent"),
              averageOf(m_diagnosisCpuSamples) },
            { QStringLiteral("peak_percent"),
              peakOf(m_diagnosisCpuSamples) },
            { QStringLiteral("average_iowait_percent"),
              averageOf(m_diagnosisIowaitSamples) },
            { QStringLiteral("load_1m_average_during_sampling"),
              averageOf(m_diagnosisLoadSamples) },
            { QStringLiteral("current_load_1m"), loads.value(0) },
            { QStringLiteral("current_load_5m"), loads.value(1) },
            { QStringLiteral("current_load_15m"), loads.value(2) }
        }},
        { QStringLiteral("memory"), QJsonObject{
            { QStringLiteral("total_gb"), m_memTotalGb },
            { QStringLiteral("used_gb"), m_memUsedGb },
            { QStringLiteral("average_used_percent"),
              averageOf(m_diagnosisMemorySamples) },
            { QStringLiteral("peak_used_percent"),
              peakOf(m_diagnosisMemorySamples) },
            { QStringLiteral("peak_swap_used_percent"),
              peakOf(m_diagnosisSwapSamples) },
            { QStringLiteral("cache_gb"), m_memCacheGb }
        }},
        { QStringLiteral("storage"), QJsonObject{
            { QStringLiteral("home_total_gb"), m_diskTotalGb },
            { QStringLiteral("home_used_gb"), m_diskUsedGb },
            { QStringLiteral("home_used_percent"), m_diskPercent },
            { QStringLiteral("average_read_mb_s"),
              averageOf(m_diagnosisDiskReadSamples) },
            { QStringLiteral("peak_read_mb_s"),
              peakOf(m_diagnosisDiskReadSamples) },
            { QStringLiteral("average_write_mb_s"),
              averageOf(m_diagnosisDiskWriteSamples) },
            { QStringLiteral("peak_write_mb_s"),
              peakOf(m_diagnosisDiskWriteSamples) }
        }},
        { QStringLiteral("applications"), processArray }
    };
    if (m_hasDiagnosisTarget) {
        const ProcessDetails &details = m_diagnosisTargetDetails;
        QJsonObject target{
            { QStringLiteral("process_name"), m_diagnosisTarget.name },
            { QStringLiteral("pid"), m_diagnosisTarget.pid },
            { QStringLiteral("executable_name"),
              details.executableName.isEmpty()
                  ? processGroupName(m_diagnosisTarget)
                  : details.executableName },
            { QStringLiteral("observation_status"),
              m_diagnosisTargetExited
                  ? QStringLiteral("exited_or_pid_changed_during_sampling")
                  : details.valid ? QStringLiteral("running")
                                  : QStringLiteral("unavailable") },
            { QStringLiteral("samples_observed"),
              m_diagnosisTargetSamplesSeen },
            { QStringLiteral("average_cpu_percent"),
              averageOf(m_diagnosisTargetCpuSamples) },
            { QStringLiteral("peak_cpu_percent"),
              peakOf(m_diagnosisTargetCpuSamples) },
            { QStringLiteral("average_rss_mb"),
              averageOf(m_diagnosisTargetRssSamples) },
            { QStringLiteral("peak_rss_mb"),
              peakOf(m_diagnosisTargetRssSamples) },
            { QStringLiteral("average_read_mb_s"),
              averageOf(m_diagnosisTargetReadSamples) },
            { QStringLiteral("peak_read_mb_s"),
              peakOf(m_diagnosisTargetReadSamples) },
            { QStringLiteral("average_write_mb_s"),
              averageOf(m_diagnosisTargetWriteSamples) },
            { QStringLiteral("peak_write_mb_s"),
              peakOf(m_diagnosisTargetWriteSamples) },
            { QStringLiteral("state"), details.state },
            { QStringLiteral("parent_pid"), details.parentPid },
            { QStringLiteral("thread_count"), details.threadCount },
            { QStringLiteral("file_descriptor_count"),
              details.fileDescriptorCount },
            { QStringLiteral("virtual_memory_mb"),
              details.virtualMemoryMb },
            { QStringLiteral("voluntary_context_switches"),
              static_cast<qint64>(details.voluntaryContextSwitches) },
            { QStringLiteral("involuntary_context_switches"),
              static_cast<qint64>(details.involuntaryContextSwitches) }
        };
        telemetry.insert(QStringLiteral("target_process"), target);
    }
    return QString::fromUtf8(
        QJsonDocument(telemetry).toJson(QJsonDocument::Compact));
}

QByteArray SystemMonitor::buildDiagnosisPayload() const
{
    const QString scopePrompt = m_hasDiagnosisTarget
        ? QStringLiteral(
              "本次任务是单进程深度诊断。必须把target_process作为分析主体，"
              "结合全局CPU、内存、iowait、负载和同类高负载应用判断该进程是"
              "正常繁忙、资源竞争、疑似卡顿还是已经退出。不得仅凭进程名猜测"
              "具体软件版本或业务用途；数据不足时明确说明。优先给出可验证、"
              "不丢失用户数据的检查建议，除非证据充分且已提示保存工作，"
              "不得建议直接强杀进程。")
        : QStringLiteral(
              "本次任务是整机健康诊断。请区分正常前台工作负载、瞬时尖峰和"
              "持续性资源瓶颈，不要因为单个进程占用较高就武断判定异常。");
    const QString systemPrompt = QStringLiteral(
        "你是针对国产飞腾处理器与银河麒麟/Linux 平台的智能运维专家。"
        "输入包含15秒趋势采样、本地规则评级、负载、iowait、Swap、磁盘IO，"
        "以及按应用聚合的进程数据。%1"
        "只返回合法JSON对象，不要Markdown，不要输出思考过程。JSON必须包含："
        "overall_status（健康/关注/异常）、summary（2到4句）、"
        "bottlenecks、evidence、immediate_actions、long_term_actions、"
        "risk_notes；后五项均为字符串数组。建议必须低风险、具体、可执行，"
        "每个数组最多3项，总体控制在1000个中文字符以内。"
        "不得建议清理缓存、强杀关键系统进程或盲目修改内核参数。")
        .arg(scopePrompt);

    QJsonArray messages;
    messages.append(QJsonObject{
        { QStringLiteral("role"), QStringLiteral("system") },
        { QStringLiteral("content"), systemPrompt }
    });
    messages.append(QJsonObject{
        { QStringLiteral("role"), QStringLiteral("user") },
        { QStringLiteral("content"), diagnosticTelemetry() }
    });

    QJsonObject payload{
        { QStringLiteral("model"), QStringLiteral("deepseek-v4-flash") },
        { QStringLiteral("messages"), messages },
        { QStringLiteral("thinking"),
          QJsonObject{{QStringLiteral("type"), QStringLiteral("enabled")}} },
        { QStringLiteral("reasoning_effort"), QStringLiteral("high") },
        { QStringLiteral("response_format"),
          QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}} },
        { QStringLiteral("max_tokens"), 4096 },
        { QStringLiteral("stream"), false }
    };
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

QString SystemMonitor::localHealthAssessment() const
{
    const double cpuAverage = averageOf(m_diagnosisCpuSamples);
    const double cpuPeak = peakOf(m_diagnosisCpuSamples);
    const double iowaitAverage = averageOf(m_diagnosisIowaitSamples);
    const double memoryPeak = peakOf(m_diagnosisMemorySamples);
    const double swapPeak = peakOf(m_diagnosisSwapSamples);
    const double loadPerCore =
        averageOf(m_diagnosisLoadSamples) / qMax(1, m_coreCount);

    QString level = QStringLiteral("健康");
    QStringList reasons;
    if (cpuAverage >= 90.0 || memoryPeak >= 95.0 || swapPeak >= 60.0 ||
        iowaitAverage >= 25.0 || m_diskPercent >= 95.0 ||
        loadPerCore >= 1.5) {
        level = QStringLiteral("异常");
    } else if (cpuAverage >= 75.0 || cpuPeak >= 95.0 ||
               memoryPeak >= 82.0 || swapPeak >= 10.0 ||
               iowaitAverage >= 10.0 || m_diskPercent >= 85.0 ||
               loadPerCore >= 1.0) {
        level = QStringLiteral("关注");
    }

    if (cpuAverage >= 75.0)
        reasons << QStringLiteral("CPU持续占用偏高");
    else if (cpuPeak >= 95.0)
        reasons << QStringLiteral("CPU出现短时峰值");
    if (memoryPeak >= 82.0)
        reasons << QStringLiteral("可用内存余量偏低");
    if (swapPeak >= 10.0)
        reasons << QStringLiteral("Swap存在明显使用");
    if (iowaitAverage >= 10.0)
        reasons << QStringLiteral("磁盘等待偏高");
    if (m_diskPercent >= 85.0)
        reasons << QStringLiteral("主目录磁盘空间偏紧");
    if (loadPerCore >= 1.0)
        reasons << QStringLiteral("系统运行队列偏高");
    if (reasons.isEmpty())
        reasons << QStringLiteral("CPU、内存、磁盘与系统负载均有充足余量");

    return QStringLiteral("%1：%2").arg(level, reasons.join(QStringLiteral("；")));
}

QString SystemMonitor::formatDiagnosisJson(const QJsonObject &result) const
{
    const QString status =
        result.value(QStringLiteral("overall_status")).toString(
            QStringLiteral("未知"));
    const QString summary =
        result.value(QStringLiteral("summary")).toString().trimmed();
    const auto section = [](const QString &title, const QStringList &items) {
        if (items.isEmpty())
            return QString();
        QStringList lines;
        lines << title;
        for (const QString &item : items)
            lines << QStringLiteral("• ") + item;
        return lines.join(QLatin1Char('\n'));
    };

    QStringList blocks;
    if (m_hasDiagnosisTarget) {
        blocks << QStringLiteral("进程深度诊断：%1（PID %2）")
                      .arg(m_diagnosisTarget.name)
                      .arg(m_diagnosisTarget.pid);
    }
    blocks << QStringLiteral("本地评级：%1").arg(localHealthAssessment());
    blocks << QStringLiteral("整体状态：%1").arg(status);
    if (!summary.isEmpty())
        blocks << summary;
    blocks << section(QStringLiteral("主要瓶颈"),
                      jsonStringList(result.value(QStringLiteral("bottlenecks"))));
    blocks << section(QStringLiteral("判断依据"),
                      jsonStringList(result.value(QStringLiteral("evidence"))));
    blocks << section(QStringLiteral("立即可做"),
                      jsonStringList(result.value(
                          QStringLiteral("immediate_actions"))));
    blocks << section(QStringLiteral("长期建议"),
                      jsonStringList(result.value(
                          QStringLiteral("long_term_actions"))));
    blocks << section(QStringLiteral("风险提示"),
                      jsonStringList(result.value(QStringLiteral("risk_notes"))));
    blocks.removeAll(QString());
    return compactAiText(blocks.join(QStringLiteral("\n\n")), 4000);
}

QString SystemMonitor::compactAiText(const QString &text, int limit) const
{
    QString result = text;
    result.replace(QStringLiteral("\r"), QString());
    result.replace(QStringLiteral("**"), QString());
    while (result.contains(QStringLiteral("\n\n\n")))
        result.replace(QStringLiteral("\n\n\n"), QStringLiteral("\n\n"));
    result = result.trimmed();
    if (limit > 0 && result.size() > limit)
        result = result.left(limit) + QStringLiteral("…");
    return result;
}

void SystemMonitor::updateAiLayoutHeight()
{
    m_aiExtraHeight = 0;
    m_aiScrollOffset = 0;
    resize(baseSize() * m_scale);
    move(boundedPosition(pos()));
}

QString SystemMonitor::tooltipAt(const QPoint &position) const
{
    if (m_cpuRingRect.contains(position)) {
        return QStringLiteral("CPU 总占用率：%1%\n处理器：%2\n逻辑核心：%3")
            .arg(m_cpuPercent, 0, 'f', 1)
            .arg(m_cpuModel)
            .arg(m_coreCount);
    }
    if (m_memoryRingRect.contains(position)) {
        return QStringLiteral("内存占用率：%1%\n已用：%2 GB\n总计：%3 GB\n"
                              "缓存：%4 GB")
            .arg(m_memPercent, 0, 'f', 1)
            .arg(m_memUsedGb, 0, 'f', 1)
            .arg(m_memTotalGb, 0, 'f', 1)
            .arg(m_memCacheGb, 0, 'f', 1);
    }
    if (m_diskRingRect.contains(position)) {
        return QStringLiteral("主目录磁盘占用率：%1%\n已用：%2 GB\n总计：%3 GB")
            .arg(m_diskPercent, 0, 'f', 1)
            .arg(m_diskUsedGb, 0, 'f', 0)
            .arg(m_diskTotalGb, 0, 'f', 0);
    }
    for (int i = 0; i < m_coreRects.size(); ++i) {
        if (!m_coreRects[i].contains(position))
            continue;
        const double frequency = m_coreFrequenciesGhz.value(i);
        return QStringLiteral("CPU 核心 %1\n当前频率：%2\n占用率：%3%")
            .arg(i + 1)
            .arg(frequency > 0.0
                     ? QStringLiteral("%1 GHz").arg(frequency, 0, 'f', 2)
                     : QStringLiteral("无法读取"))
            .arg(m_corePercents.value(i), 0, 'f', 1);
    }
    for (int i = 0; i < m_processRowRects.size() &&
                    i < m_topProcs.size(); ++i) {
        if (!m_processRowRects[i].contains(position))
            continue;
        const ProcessInfo &process = m_topProcs[i];
        return QStringLiteral("应用：%1\nPID：%2\nCPU：%3%\n内存：%4 MB\n"
                              "路径：%5\n右键可执行此进程的深度诊断")
            .arg(process.name)
            .arg(process.pid)
            .arg(process.cpuPercent, 0, 'f', 1)
            .arg(process.rssMb, 0, 'f', 0)
            .arg(process.executablePath.isEmpty()
                     ? QStringLiteral("无权限读取或内核线程")
                     : process.executablePath);
    }
    if (m_optimizeRect.contains(position))
        return QStringLiteral("立即刷新全部资源数据");
    if (m_toggleRect.contains(position))
        return m_compact ? QStringLiteral("展开进程与智能诊断")
                         : QStringLiteral("收起为紧凑视图");
    if (m_skinRect.contains(position))
        return QStringLiteral("切换整个小组件窗口皮肤");
    if (m_settingsRect.contains(position))
        return QStringLiteral("配置标题、字体、字号与自动启动");
    if (m_aiRunRect.contains(position))
        return QStringLiteral("发送当前实时遥测，生成简洁诊断建议");
    if (m_resizeHandleRect.contains(position) && m_editMode)
        return QStringLiteral("按住拖动，等比例放大或缩小组件");
    return QString();
}

void SystemMonitor::showSettingsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("小组件配置"));
    dialog.setMinimumWidth(500);
    auto *layout = new QFormLayout(&dialog);

    auto *titleEdit = new QLineEdit(m_widgetTitle, &dialog);
    titleEdit->setMaxLength(40);
    layout->addRow(QStringLiteral("小组件标题："), titleEdit);

    auto *fontCombo = new QComboBox(&dialog);
    fontCombo->addItem(QStringLiteral("系统默认"), QString());
    static const QStringList families = QFontDatabase().families();
    for (const QString &family : families)
        fontCombo->addItem(family, family);
    const int fontIndex = fontCombo->findData(m_widgetFontFamily);
    if (fontIndex >= 0)
        fontCombo->setCurrentIndex(fontIndex);
    layout->addRow(QStringLiteral("小组件字体："), fontCombo);

    auto *fontSize = new QSpinBox(&dialog);
    fontSize->setRange(8, 16);
    fontSize->setValue(m_widgetFontSize);
    fontSize->setSuffix(QStringLiteral(" px"));
    layout->addRow(QStringLiteral("字体大小："), fontSize);

    auto *opacitySlider = new QSlider(Qt::Horizontal, &dialog);
    opacitySlider->setRange(25, 100);
    opacitySlider->setValue(m_windowOpacity);
    auto *opacityLabel = new QLabel(
        QStringLiteral("%1%").arg(m_windowOpacity), &dialog);
    auto *opacityRow = new QWidget(&dialog);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(opacitySlider);
    opacityLayout->addWidget(opacityLabel);
    connect(opacitySlider, &QSlider::valueChanged,
            opacityLabel, [opacityLabel](int value) {
        opacityLabel->setText(QStringLiteral("%1%").arg(value));
    });
    layout->addRow(QStringLiteral("窗口透明度："), opacityRow);

    auto *preview = new QLabel(m_widgetTitle, &dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(42);
    auto updatePreview = [=] {
        QFont previewFont;
        const QString family = fontCombo->currentData().toString();
        if (!family.isEmpty())
            previewFont.setFamily(family);
        previewFont.setPixelSize(fontSize->value());
        previewFont.setBold(true);
        preview->setFont(previewFont);
        preview->setText(titleEdit->text().trimmed().isEmpty()
                             ? QStringLiteral("小组件标题预览")
                             : titleEdit->text().trimmed());
    };
    updatePreview();
    connect(titleEdit, &QLineEdit::textChanged,
            &dialog, [=](const QString &) { updatePreview(); });
    connect(fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [=](int) { updatePreview(); });
    connect(fontSize, QOverload<int>::of(&QSpinBox::valueChanged),
            &dialog, [=](int) { updatePreview(); });
    layout->addRow(QStringLiteral("预览："), preview);

    auto *keyEdit = new QLineEdit(&dialog);
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setText(m_apiKey);
    keyEdit->setPlaceholderText(
        QStringLiteral("请输入 DeepSeek API Key"));
    keyEdit->setEnabled(true);
    layout->addRow(QStringLiteral("API Key："), keyEdit);

    auto *urlEdit = new QLineEdit(m_apiUrl, &dialog);
    urlEdit->setEnabled(true);
    layout->addRow(QStringLiteral("Endpoint："), urlEdit);

    auto *intervalSpin = new QSpinBox(&dialog);
    intervalSpin->setRange(1, 60);
    intervalSpin->setValue(m_statIntervalSec);
    intervalSpin->setSuffix(QStringLiteral(" 秒"));
    layout->addRow(QStringLiteral("采样周期："), intervalSpin);

    auto *autoStartCheck = new QCheckBox(
        QStringLiteral("随 Fences 启动时自动打开系统监控"), &dialog);
    autoStartCheck->setChecked(m_autoStart);
    layout->addRow(QStringLiteral("自动启动："), autoStartCheck);

    auto *premiumCheck = new QCheckBox(
        QStringLiteral("启用高级仿毛玻璃与卡片发光"), &dialog);
    premiumCheck->setChecked(m_premiumAesthetics);
    layout->addRow(QStringLiteral("高级美化："), premiumCheck);

    auto *resourceHint = new QLabel(
        QStringLiteral("全部指标通过读取 /proc 内核文件系统本地采集；"
                       "智能诊断通过 API 调用 DeepSeek 云端大模型，"
                       "能够根据系统遥测状况生成深度运维与调优建议。"),
        &dialog);
    resourceHint->setWordWrap(true);
    layout->addRow(QStringLiteral("资源策略："), resourceHint);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted,
            &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        m_widgetTitle = titleEdit->text().trimmed();
        if (m_widgetTitle.isEmpty())
            m_widgetTitle = QStringLiteral("飞腾桌面资源监控");
        m_widgetFontFamily = fontCombo->currentData().toString();
        m_widgetFontSize = fontSize->value();
        m_windowOpacity = opacitySlider->value();
        m_statIntervalSec = intervalSpin->value();
        m_autoStart = autoStartCheck->isChecked();
        m_premiumAesthetics = premiumCheck->isChecked();
        const QString editedKey = keyEdit->text().trimmed();
        if (editedKey != m_apiKey || m_credentialFailed) {
            m_apiKey = editedKey;
            saveCredential(m_apiKey);
        }
        m_apiUrl = urlEdit->text().trimmed();
        m_aiError.clear();
        // 应用新的采样周期
        m_timer.setInterval(m_statIntervalSec * 1000);
        saveSettings();
        updateAiLayoutHeight();
        update();
        emit appearanceChanged();
    }
}

void SystemMonitor::startDiagnosis()
{
    beginDiagnosis(nullptr);
}

void SystemMonitor::startProcessDiagnosis(const ProcessInfo &process)
{
    beginDiagnosis(&process);
}

void SystemMonitor::beginDiagnosis(const ProcessInfo *targetProcess)
{
    if (m_aiBusy)
        return;

    if (m_apiKey.trimmed().isEmpty()) {
        m_aiError = QStringLiteral("未配置 API Key。请先在设置中输入 DeepSeek API Key。");
        updateAiLayoutHeight();
        update();
        return;
    }

    m_hasDiagnosisTarget = targetProcess != nullptr;
    m_diagnosisTarget = targetProcess ? *targetProcess : ProcessInfo{};
    m_diagnosisTargetDetails = ProcessDetails{};
    m_diagnosisTargetExited = false;
    if (m_hasDiagnosisTarget) {
        m_diagnosisTargetDetails = readProcessDetails(m_diagnosisTarget.pid);
        if (!m_diagnosisTargetDetails.valid ||
            m_diagnosisTargetDetails.startTicks !=
                m_diagnosisTarget.startTicks) {
            m_hasDiagnosisTarget = false;
            m_aiError = QStringLiteral(
                "目标进程已退出或 PID 已变化，请刷新列表后重试。");
            updateAiLayoutHeight();
            update();
            return;
        }
    }

    m_aiBusy = true;
    m_diagnosisSampling = true;
    m_aiText.clear();
    m_aiReasoning.clear();
    m_aiError.clear();
    m_aiProgressText = m_hasDiagnosisTarget
        ? QStringLiteral("正在深度采样 %1（PID %2）… 0/%3")
              .arg(m_diagnosisTarget.name)
              .arg(m_diagnosisTarget.pid)
              .arg(DIAGNOSIS_SAMPLE_COUNT)
        : QStringLiteral("正在采样系统趋势… 0/%1")
              .arg(DIAGNOSIS_SAMPLE_COUNT);
    m_diagnosisCpuSamples.clear();
    m_diagnosisIowaitSamples.clear();
    m_diagnosisMemorySamples.clear();
    m_diagnosisSwapSamples.clear();
    m_diagnosisLoadSamples.clear();
    m_diagnosisDiskReadSamples.clear();
    m_diagnosisDiskWriteSamples.clear();
    m_diagnosisTargetCpuSamples.clear();
    m_diagnosisTargetRssSamples.clear();
    m_diagnosisTargetReadSamples.clear();
    m_diagnosisTargetWriteSamples.clear();
    m_diagnosisTargetSamplesSeen = 0;
    m_diagnosisTargetPreviousReadBytes =
        m_diagnosisTargetDetails.readBytes;
    m_diagnosisTargetPreviousWriteBytes =
        m_diagnosisTargetDetails.writeBytes;
    m_diagnosisProcesses.clear();
    m_diagnosisPrevProcTicks.clear();
    m_diagnosisSamplesTaken = 0;
    m_diagnosisPrevCpu = readCpuTotals();
    m_diagnosisPrevDiskIo = readDiskIoTotals();
    readProcessSnapshot(0, 0, m_diagnosisPrevProcTicks);
    updateAiLayoutHeight();
    update();
    m_diagnosisTimer.start();
}

void SystemMonitor::collectDiagnosisSample()
{
    if (!m_aiBusy || !m_diagnosisSampling) {
        m_diagnosisTimer.stop();
        return;
    }

    const CpuTotals current = readCpuTotals();
    const quint64 totalDelta = current.total > m_diagnosisPrevCpu.total
        ? current.total - m_diagnosisPrevCpu.total : 0;
    const quint64 idleDelta = current.idle > m_diagnosisPrevCpu.idle
        ? current.idle - m_diagnosisPrevCpu.idle : 0;
    const quint64 iowaitDelta = current.iowait > m_diagnosisPrevCpu.iowait
        ? current.iowait - m_diagnosisPrevCpu.iowait : 0;
    if (totalDelta > 0) {
        m_diagnosisCpuSamples << clampPercent(
            (totalDelta - idleDelta) * 100.0 / totalDelta);
        m_diagnosisIowaitSamples << clampPercent(
            iowaitDelta * 100.0 / totalDelta);
    }
    m_diagnosisPrevCpu = current;

    const MemStats memory = readMemStats();
    if (memory.totalKiB > 0) {
        const quint64 usedKiB = memory.totalKiB - memory.availableKiB;
        m_diagnosisMemorySamples << clampPercent(
            usedKiB * 100.0 / memory.totalKiB);
        m_memUsedGb = usedKiB / 1048576.0;
        m_memTotalGb = memory.totalKiB / 1048576.0;
        m_memCacheGb = memory.cacheKiB / 1048576.0;
        m_memPercent = m_diagnosisMemorySamples.constLast();
    }
    if (memory.swapTotalKiB > 0) {
        m_diagnosisSwapSamples << clampPercent(
            (memory.swapTotalKiB - memory.swapFreeKiB) * 100.0 /
            memory.swapTotalKiB);
    } else {
        m_diagnosisSwapSamples << 0.0;
    }

    const QVector<double> loads = readLoadAverages();
    if (!loads.isEmpty())
        m_diagnosisLoadSamples << loads.first();

    const DiskIoTotals diskIo = readDiskIoTotals();
    constexpr double sectorToMb = 512.0 / 1048576.0;
    if (diskIo.readSectors >= m_diagnosisPrevDiskIo.readSectors) {
        m_diagnosisDiskReadSamples << (
            diskIo.readSectors - m_diagnosisPrevDiskIo.readSectors) *
            sectorToMb;
    }
    if (diskIo.writeSectors >= m_diagnosisPrevDiskIo.writeSectors) {
        m_diagnosisDiskWriteSamples << (
            diskIo.writeSectors - m_diagnosisPrevDiskIo.writeSectors) *
            sectorToMb;
    }
    m_diagnosisPrevDiskIo = diskIo;

    QHash<QString, double> applicationCpu;
    QHash<QString, double> applicationRss;
    const QVector<ProcessInfo> processes = readProcessSnapshot(
        0, totalDelta, m_diagnosisPrevProcTicks);
    const int applicationLimit = qMin(24, processes.size());
    for (int i = 0; i < applicationLimit; ++i) {
        const ProcessInfo &process = processes[i];
        const QString name = processGroupName(process);
        applicationCpu[name] += process.cpuPercent;
        applicationRss[name] += process.rssMb;
    }
    for (auto it = applicationCpu.constBegin();
         it != applicationCpu.constEnd(); ++it) {
        ProcessAggregate aggregate = m_diagnosisProcesses.value(it.key());
        aggregate.name = it.key();
        aggregate.cpuSum += it.value();
        aggregate.cpuPeak = qMax(aggregate.cpuPeak, it.value());
        aggregate.rssPeakMb =
            qMax(aggregate.rssPeakMb, applicationRss.value(it.key()));
        ++aggregate.samples;
        m_diagnosisProcesses.insert(it.key(), aggregate);
    }

    if (m_hasDiagnosisTarget && !m_diagnosisTargetExited) {
        auto targetIt = std::find_if(
            processes.constBegin(), processes.constEnd(),
            [this](const ProcessInfo &process) {
                return process.pid == m_diagnosisTarget.pid &&
                       process.startTicks == m_diagnosisTarget.startTicks;
            });
        const ProcessDetails details =
            readProcessDetails(m_diagnosisTarget.pid);
        if (targetIt == processes.constEnd() || !details.valid ||
            details.startTicks != m_diagnosisTarget.startTicks) {
            m_diagnosisTargetExited = true;
        } else {
            m_diagnosisTargetCpuSamples << targetIt->cpuPercent;
            m_diagnosisTargetRssSamples << targetIt->rssMb;
            ++m_diagnosisTargetSamplesSeen;
            if (details.hasIoCounters &&
                m_diagnosisTargetDetails.hasIoCounters) {
                constexpr double bytesToMb = 1.0 / 1048576.0;
                if (details.readBytes >= m_diagnosisTargetPreviousReadBytes) {
                    m_diagnosisTargetReadSamples <<
                        (details.readBytes -
                         m_diagnosisTargetPreviousReadBytes) * bytesToMb;
                }
                if (details.writeBytes >= m_diagnosisTargetPreviousWriteBytes) {
                    m_diagnosisTargetWriteSamples <<
                        (details.writeBytes -
                         m_diagnosisTargetPreviousWriteBytes) * bytesToMb;
                }
            }
            m_diagnosisTargetPreviousReadBytes = details.readBytes;
            m_diagnosisTargetPreviousWriteBytes = details.writeBytes;
            m_diagnosisTargetDetails = details;
        }
    }

    ++m_diagnosisSamplesTaken;
    if (m_hasDiagnosisTarget) {
        m_aiProgressText = m_diagnosisTargetExited
            ? QStringLiteral(
                  "%1（PID %2）已退出，继续整理已采样数据… %3/%4")
                  .arg(m_diagnosisTarget.name)
                  .arg(m_diagnosisTarget.pid)
                  .arg(m_diagnosisSamplesTaken)
                  .arg(DIAGNOSIS_SAMPLE_COUNT)
            : QStringLiteral(
                  "正在深度采样 %1（PID %2）… %3/%4\n"
                  "进程 CPU %5%，内存 %6 MB；整机 CPU %7%")
                  .arg(m_diagnosisTarget.name)
                  .arg(m_diagnosisTarget.pid)
                  .arg(m_diagnosisSamplesTaken)
                  .arg(DIAGNOSIS_SAMPLE_COUNT)
                  .arg(m_diagnosisTargetCpuSamples.value(
                           m_diagnosisTargetCpuSamples.size() - 1), 0, 'f', 1)
                  .arg(m_diagnosisTargetRssSamples.value(
                           m_diagnosisTargetRssSamples.size() - 1), 0, 'f', 0)
                  .arg(m_diagnosisCpuSamples.value(
                           m_diagnosisCpuSamples.size() - 1), 0, 'f', 1);
    } else {
        m_aiProgressText = QStringLiteral(
            "正在采样系统趋势… %1/%2\n"
            "当前 CPU %3%，内存 %4%，IO 等待 %5%")
            .arg(m_diagnosisSamplesTaken)
            .arg(DIAGNOSIS_SAMPLE_COUNT)
            .arg(m_diagnosisCpuSamples.value(
                     m_diagnosisCpuSamples.size() - 1), 0, 'f', 1)
            .arg(m_diagnosisMemorySamples.value(
                     m_diagnosisMemorySamples.size() - 1), 0, 'f', 1)
            .arg(m_diagnosisIowaitSamples.value(
                     m_diagnosisIowaitSamples.size() - 1), 0, 'f', 1);
    }
    update();

    if (m_diagnosisSamplesTaken >= DIAGNOSIS_SAMPLE_COUNT) {
        m_diagnosisTimer.stop();
        m_diagnosisSampling = false;
        readDiskStats();
        sendDiagnosisRequest();
    }
}

void SystemMonitor::sendDiagnosisRequest()
{
    m_aiProgressText = m_hasDiagnosisTarget
        ? QStringLiteral(
              "%1（PID %2）采样完成。\n正在整理进程上下文并请求后台 AI…")
              .arg(m_diagnosisTarget.name)
              .arg(m_diagnosisTarget.pid)
        : QStringLiteral(
              "趋势采样完成。\n本地评级：%1\n正在请求 DeepSeek…")
              .arg(localHealthAssessment());
    update();

    if (!prepareAiAuthHeader()) {
        m_aiError = QStringLiteral("无法创建受保护的临时认证头。");
        m_aiBusy = false;
        m_aiProgressText.clear();
        updateAiLayoutHeight();
        update();
        return;
    }
    m_curl = new QProcess(this);
    m_curlOutput.clear();
    m_curlError.clear();
    connect(m_curl, &QProcess::readyReadStandardOutput, this, [this] {
        constexpr int limit = 4 * 1024 * 1024;
        const QByteArray chunk = m_curl->readAllStandardOutput();
        const int room = qMax(0, limit - m_curlOutput.size());
        if (room > 0)
            m_curlOutput.append(chunk.left(room));
    });
    connect(m_curl, &QProcess::readyReadStandardError, this, [this] {
        constexpr int limit = 64 * 1024;
        const QByteArray chunk = m_curl->readAllStandardError();
        const int room = qMax(0, limit - m_curlError.size());
        if (room > 0)
            m_curlError.append(chunk.left(room));
    });
    QStringList arguments;
    arguments << QStringLiteral("-s")
              << QStringLiteral("-X") << QStringLiteral("POST")
              << QStringLiteral("-H") << QStringLiteral("Content-Type: application/json")
              << QStringLiteral("-H") << QStringLiteral("@") + m_aiAuthFile->fileName()
              << QStringLiteral("-d") << QStringLiteral("@-")
              << m_apiUrl.trimmed();

    connect(m_curl, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                Q_UNUSED(exitStatus);
                this->finishDiagnosis(exitCode);
            });

    m_curl->start(QStringLiteral("curl"), arguments);
    if (!m_curl->waitForStarted()) {
        m_aiError = QStringLiteral("无法启动 curl 进程，请确保系统已安装 curl。");
        m_aiBusy = false;
        m_aiProgressText.clear();
        m_curl->deleteLater();
        m_curl = nullptr;
        delete m_aiAuthFile;
        m_aiAuthFile = nullptr;
        updateAiLayoutHeight();
        update();
        return;
    }

    QByteArray payload = buildDiagnosisPayload();
    m_curl->write(payload);
    m_curl->closeWriteChannel();

    // 30秒超时控制
    const QPointer<QProcess> request = m_curl;
    QTimer::singleShot(30000, this, [request]() {
        if (request && request->state() == QProcess::Running) {
            request->kill();
        }
    });
}

bool SystemMonitor::prepareAiAuthHeader()
{
    if (m_aiAuthFile) {
        delete m_aiAuthFile;
        m_aiAuthFile = nullptr;
    }
    m_aiAuthFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/ukui-fences-monitor-ai-XXXXXX"), this);
    m_aiAuthFile->setAutoRemove(true);
    if (!m_aiAuthFile->open()) {
        delete m_aiAuthFile;
        m_aiAuthFile = nullptr;
        return false;
    }
    m_aiAuthFile->setPermissions(
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray header = QByteArray("Authorization: Bearer ")
        + m_apiKey.trimmed().toUtf8() + '\n';
    if (m_aiAuthFile->write(header) != header.size() ||
        !m_aiAuthFile->flush()) {
        delete m_aiAuthFile;
        m_aiAuthFile = nullptr;
        return false;
    }
    return true;
}

void SystemMonitor::finishDiagnosis(int exitCode)
{
    if (!m_curl) {
        m_aiBusy = false;
        m_diagnosisSampling = false;
        m_aiProgressText.clear();
        return;
    }

    const QByteArray finalOutput = m_curl->readAllStandardOutput();
    const QByteArray finalError = m_curl->readAllStandardError();
    const int outputRoom = qMax(0, 4 * 1024 * 1024 - m_curlOutput.size());
    const int errorRoom = qMax(0, 64 * 1024 - m_curlError.size());
    if (outputRoom > 0)
        m_curlOutput.append(finalOutput.left(outputRoom));
    if (errorRoom > 0)
        m_curlError.append(finalError.left(errorRoom));
    const QByteArray output = m_curlOutput;
    const QByteArray errorOutput = m_curlError;
    m_curlOutput.clear();
    m_curlError.clear();
    m_curl->deleteLater();
    m_curl = nullptr;
    delete m_aiAuthFile;
    m_aiAuthFile = nullptr;
    m_aiBusy = false;
    m_diagnosisSampling = false;
    m_aiProgressText.clear();

    if (exitCode != 0) {
        m_aiError = QStringLiteral("请求失败，退出码: %1。%2")
                        .arg(exitCode)
                        .arg(QString::fromUtf8(errorOutput).trimmed());
        if (m_aiError.isEmpty()) {
            m_aiError = QStringLiteral("网络错误或 API 服务无法访问，请检查 Endpoint 与 API Key。");
        }
        updateAiLayoutHeight();
        update();
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(output, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_aiError = QStringLiteral("解析 DeepSeek 响应失败：%1。").arg(err.errorString());
        updateAiLayoutHeight();
        update();
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error"))) {
        QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
        m_aiError = errObj.value(QStringLiteral("message")).toString();
        updateAiLayoutHeight();
        update();
        return;
    }

    QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        m_aiError = QStringLiteral("响应数据不完整，未包含 choices 字段。");
        updateAiLayoutHeight();
        update();
        return;
    }

    QJsonObject choice = choices.first().toObject();
    QJsonObject message = choice.value(QStringLiteral("message")).toObject();

    QString reasoning = message.value(QStringLiteral("reasoning_content")).toString().trimmed();
    QString content = message.value(QStringLiteral("content")).toString().trimmed();

    if (content.isEmpty()) {
        m_aiError = QStringLiteral("DeepSeek 未返回有效诊断内容。");
    } else {
        if (content.startsWith(QStringLiteral("```"))) {
            content.remove(QRegularExpression(
                QStringLiteral("^```(?:json)?\\s*")));
            content.remove(QRegularExpression(QStringLiteral("\\s*```$")));
        }
        QJsonParseError contentError;
        const QJsonDocument contentDocument =
            QJsonDocument::fromJson(content.toUtf8(), &contentError);
        if (contentError.error == QJsonParseError::NoError &&
            contentDocument.isObject()) {
            m_aiText = formatDiagnosisJson(contentDocument.object());
        } else {
            m_aiText = QStringLiteral("%1\n\n%2")
                .arg(localHealthAssessment(), compactAiText(content, 3500));
        }
        m_aiReasoning = reasoning;
    }

    updateAiLayoutHeight();
    update();
}

void SystemMonitor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint logicalPos = logicalPosition(event->pos());
        if (m_editMode && m_resizeHandleRect.contains(logicalPos)) {
            m_resizing = true;
            m_resizeStartGlobal = event->globalPos();
            m_resizeStartSize = size();
            if (!m_mouseGrabbed) {
                grabMouse();
                m_mouseGrabbed = true;
            }
            setCursor(Qt::SizeFDiagCursor);
            event->accept();
            return;
        }
        if (m_optimizeRect.contains(logicalPos)) {
            refreshStats();
            event->accept();
            return;
        }
        if (m_toggleRect.contains(logicalPos)) {
            setCompact(!m_compact);
            event->accept();
            return;
        }
        if (m_skinRect.contains(logicalPos)) {
            QMenu menu(this);
            MenuStyle::applyVenturaContextMenu(&menu);
            QAction *dark = menu.addAction(QStringLiteral("极客碳黑"));
            QAction *light = menu.addAction(QStringLiteral("优雅亮白"));
            QAction *cyber = menu.addAction(QStringLiteral("赛博霓虹"));
            QAction *glass = menu.addAction(QStringLiteral("晶莹玻璃"));
            QAction *wallpaper =
                menu.addAction(QStringLiteral("壁纸自动取色"));
            QAction *liquid = menu.addAction(QStringLiteral("NextKDE 液态玻璃"));
            const QRect anchor(mapToGlobal(QPoint(qRound(m_skinRect.left()*m_scale),
                                                  qRound(m_skinRect.top()*m_scale))),
                               QSize(qRound(m_skinRect.width()*m_scale),qRound(m_skinRect.height()*m_scale)));
            QAction *chosen = LiquidPopup::execAt(menu,anchor);
            if (chosen == dark) setSkin(Skin::Dark);
            else if (chosen == light) setSkin(Skin::Light);
            else if (chosen == cyber) setSkin(Skin::Cyber);
            else if (chosen == glass) setSkin(Skin::Glass);
            else if (chosen == liquid) setSkin(Skin::Liquid);
            else if (chosen == wallpaper) {
                refreshWallpaperTheme();
                setSkin(Skin::Wallpaper);
            }
            event->accept();
            return;
        }
        if (m_settingsRect.contains(logicalPos)) {
            showSettingsDialog();
            event->accept();
            return;
        }
        if (m_aiRunRect.contains(logicalPos)) {
            startDiagnosis();
            event->accept();
            return;
        }
        if (m_aiClearRect.contains(logicalPos)) {
            m_aiText.clear();
            m_aiReasoning.clear();
            m_aiError.clear();
            updateAiLayoutHeight();
            update();
            event->accept();
            return;
        }

        // 非编辑模式完全固定；只有全局编辑模式打开后才能移动。
        if (m_editMode) {
            m_dragging = true;
            m_dragOffset = event->pos();
            if (!m_mouseGrabbed) {
                grabMouse();
                m_mouseGrabbed = true;
            }
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SystemMonitor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_skin == Skin::Liquid) {
        m_liquidGlassPointer = logicalPosition(event->pos());
        if (!m_dragging && !m_resizing)
            update();
    }
    if (m_resizing && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = event->globalPos() - m_resizeStartGlobal;
        const QSize logical = baseSize();

        // 判断当前磁吸状态：左右边缘是否吸附在屏幕边缘
        if (!parentWidget())
            return;
        const QRect bounds = parentWidget()->rect();
        constexpr int snapFreeze = 8;  // 吸附后固定阈值
        const bool snappedLeft =
            qAbs(pos().x() - bounds.left()) <= snapFreeze;
        const bool snappedRight =
            qAbs(pos().x() + width() - bounds.right()) <= snapFreeze;
        const bool snappedTop =
            qAbs(pos().y() - bounds.top()) <= snapFreeze;
        const bool snappedBottom =
            qAbs(pos().y() + height() - bounds.bottom()) <= snapFreeze;

        const double horizontal =
            (m_resizeStartSize.width() + delta.x()) /
            static_cast<double>(logical.width());
        const double vertical =
            (m_resizeStartSize.height() + delta.y()) /
            static_cast<double>(logical.height());
        m_scale = qBound(0.65, (horizontal + vertical) / 2.0, 1.60);
        const QSize newSize = logical * m_scale;
        resize(newSize);

        // 磁吸边缘不动：只向非吸附方向扩展/收缩
        QPoint newPos = pos();
        if (snappedLeft)
            newPos.setX(bounds.left());
        else if (snappedRight)
            newPos.setX(bounds.right() - newSize.width());
        if (snappedTop)
            newPos.setY(bounds.top());
        else if (snappedBottom)
            newPos.setY(bounds.bottom() - newSize.height());

        // 如果左右都吸附，以左侧为准固定
        if (snappedLeft && snappedRight)
            newPos.setX(bounds.left());
        // 如果上下都吸附，以上侧为准固定
        if (snappedTop && snappedBottom)
            newPos.setY(bounds.top());

        move(boundedPosition(newPos));
        update();
        event->accept();
        return;
    }
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint parentPoint =
            parentWidget() ? parentWidget()->mapFromGlobal(event->globalPos())
                           : event->globalPos();
        move(boundedPosition(parentPoint - m_dragOffset));
        event->accept();
        return;
    }

    const QPoint logicalPos = logicalPosition(event->pos());
    if (m_editMode && m_resizeHandleRect.contains(logicalPos))
        setCursor(Qt::SizeFDiagCursor);
    else
        setCursor(m_editMode ? Qt::SizeAllCursor : Qt::ArrowCursor);

    const QString tooltip = tooltipAt(logicalPos);
    if (tooltip.isEmpty())
        LiquidPopup::hideText();
    else
        LiquidPopup::showText(event->globalPos() + QPoint(12, 18),
                           tooltip, this);
    QWidget::mouseMoveEvent(event);
}

void SystemMonitor::mouseReleaseEvent(QMouseEvent *event)
{
    if ((m_dragging || m_resizing) &&
        event->button() == Qt::LeftButton) {
        m_dragging = false;
        m_resizing = false;
        if (m_mouseGrabbed) {
            releaseMouse();
            m_mouseGrabbed = false;
        }
        setCursor(m_editMode ? Qt::SizeAllCursor : Qt::ArrowCursor);
        saveSettings();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SystemMonitor::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_editMode && event->button() == Qt::LeftButton &&
        logicalPosition(event->pos()).y() <= HEADER_H) {
        setCompact(!m_compact);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SystemMonitor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    MenuStyle::applyVenturaContextMenu(&menu);
    ProcessInfo selectedProcess;
    bool hasSelectedProcess = false;
    const QPoint logicalPos = logicalPosition(event->pos());
    if (!m_compact) {
        for (int i = 0; i < m_processRowRects.size() &&
                        i < m_topProcs.size(); ++i) {
            if (!m_processRowRects[i].contains(logicalPos))
                continue;
            selectedProcess = m_topProcs[i];
            hasSelectedProcess = true;
            break;
        }
    }
    QAction *processDiagnose = nullptr;
    if (hasSelectedProcess) {
        processDiagnose = menu.addAction(
            QStringLiteral("执行深度诊断 · %1（PID %2）")
                .arg(selectedProcess.name)
                .arg(selectedProcess.pid));
        processDiagnose->setEnabled(!m_aiBusy);
        menu.addSeparator();
    }
    QAction *toggle = menu.addAction(
        m_compact ? QStringLiteral("展开详细信息")
                  : QStringLiteral("切换紧凑模式"));
    QAction *diagnose = nullptr;
    if (!m_compact)
        diagnose = menu.addAction(QStringLiteral("DeepSeek 趋势诊断"));
    QAction *settings =
        menu.addAction(QStringLiteral("小组件设置"));
    QMenu *skins = menu.addMenu(QStringLiteral("窗口皮肤"));
    MenuStyle::applyVenturaContextMenu(skins);
    QAction *dark = skins->addAction(QStringLiteral("极客碳黑"));
    QAction *light = skins->addAction(QStringLiteral("优雅亮白"));
    QAction *cyber = skins->addAction(QStringLiteral("赛博霓虹"));
    QAction *glass = skins->addAction(QStringLiteral("晶莹玻璃"));
    QAction *wallpaper = skins->addAction(QStringLiteral("壁纸自动取色"));
    QAction *liquid = skins->addAction(QStringLiteral("NextKDE 液态玻璃"));
    menu.addSeparator();
    QAction *close = menu.addAction(QStringLiteral("隐藏小组件"));

    QAction *chosen = menu.exec(event->globalPos());
    if (processDiagnose && chosen == processDiagnose)
        startProcessDiagnosis(selectedProcess);
    else if (chosen == toggle)
        setCompact(!m_compact);
    else if (diagnose && chosen == diagnose)
        startDiagnosis();
    else if (chosen == settings)
        showSettingsDialog();
    else if (chosen == dark)
        setSkin(Skin::Dark);
    else if (chosen == light)
        setSkin(Skin::Light);
    else if (chosen == cyber)
        setSkin(Skin::Cyber);
    else if (chosen == glass)
        setSkin(Skin::Glass);
    else if (chosen == liquid)
        setSkin(Skin::Liquid);
    else if (chosen == wallpaper) {
        refreshWallpaperTheme();
        setSkin(Skin::Wallpaper);
    }
    else if (chosen == close)
        emit closeRequested();
}

void SystemMonitor::enterEvent(QEvent *)
{
    m_hovered = true;
    m_liquidGlassPointer = logicalPosition(mapFromGlobal(QCursor::pos()));
    update();
}

void SystemMonitor::leaveEvent(QEvent *)
{
    m_hovered = false;
    LiquidPopup::hideText();
    update();
}

void SystemMonitor::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    m_liquidGlassImage = {};
    m_liquidGlassGeometry = {};
    if (m_skin == Skin::Liquid) update();
}

void SystemMonitor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_liquidGlassImage = {};
    m_liquidGlassGeometry = {};
}

void SystemMonitor::wheelEvent(QWheelEvent *event)
{
    const QPoint logicalPos = logicalPosition(event->pos());
    const int viewportHeight = qMax(0, m_aiContentRect.height() - 16);
    if (!m_compact && m_aiContentRect.contains(logicalPos) &&
        m_aiContentHeight > viewportHeight) {
        const int maxScroll =
            qMax(0, m_aiContentHeight - viewportHeight);
        const int step = qMax(20, uiFont().pixelSize() * 3);
        m_aiScrollOffset = qBound(
            0, m_aiScrollOffset -
               (event->angleDelta().y() > 0 ? step : -step),
            maxScroll);
        update();
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier) {
        setCompact(event->angleDelta().y() < 0);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void SystemMonitor::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshStats();
    if (!m_timer.isActive())
        m_timer.start(m_statIntervalSec * 1000);
}

void SystemMonitor::hideEvent(QHideEvent *event)
{
    m_timer.stop();
    if (m_mouseGrabbed) {
        releaseMouse();
        m_mouseGrabbed = false;
    }
    m_dragging = false;
    m_resizing = false;
    QWidget::hideEvent(event);
}
