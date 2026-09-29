#include "LiquidPopup.h"
#include "SmartSpaceWidget.h"
#include "DesktopCanvas.h"

#include "DesktopItem.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDesktopServices>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHelpEvent>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QRegion>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QtEndian>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusConnection>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <signal.h>
#include <unistd.h>
#include <zlib.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace {

constexpr int kDefaultWidth = 920;
constexpr int kDefaultHeight = 520;
// Rich result cards use a few lightweight child widgets each. Build them in
// explicit pages so the complete index remains searchable without creating
// thousands of QWidget objects at once.
constexpr int kResultsPageSize = 100;
constexpr int kMaxWatchedDirectories = 512;
constexpr int kKnowledgeErrorLimit = 64 * 1024;
constexpr int kMinResultDensity = -5;
constexpr int kMaxResultDensity = 2;
constexpr int kResultDensityLevels = 8;
constexpr int kExactNameScore = 1200;
constexpr int kExactContentScore = 700;
constexpr int kExactPathScore = 350;
constexpr int kTermNameScore = 180;
constexpr int kTermContentScore = 70;
constexpr int kTermPathScore = 35;
constexpr int kYearTermBonus = 120;

bool signalOwnedProcessGroup(QProcess *process, int signalNumber)
{
    if (!process || process->state() == QProcess::NotRunning)
        return false;
    const qint64 rawPid = process->processId();
    if (rawPid <= 1 || rawPid > std::numeric_limits<pid_t>::max())
        return false;
    const pid_t pid = static_cast<pid_t>(rawPid);
    // A negative PID is only safe when this exact child is the process-group
    // leader created by setsid. Never signal an inherited desktop/session
    // group if the launcher or platform behavior changes.
    if (::getpgid(pid) != pid)
        return false;
    return ::kill(-pid, signalNumber) == 0;
}

void appendLimitedProcessOutput(QProcess *process, QByteArray &buffer,
                                const QByteArray &chunk, int limit)
{
    const int room = qMax(0, limit - buffer.size());
    if (room > 0)
        buffer.append(chunk.left(room));
    if (chunk.size() > room && process)
        process->setProperty("responseTruncated", true);
}

class AspectRatioSizeGrip final : public QWidget
{
public:
    AspectRatioSizeGrip(QWidget *target, QWidget *parent)
        : QWidget(parent), m_target(target)
    {
        setFixedSize(22, 22);
        setCursor(Qt::SizeFDiagCursor);
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_target)
            return QWidget::mousePressEvent(event);
        m_dragging = true;
        m_startGlobal = event->globalPos();
        m_startSize = m_target->size();
        m_ratio = qMax(0.1, m_startSize.width() /
                              static_cast<qreal>(qMax(1, m_startSize.height())));
        grabMouse();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging || !m_target)
            return QWidget::mouseMoveEvent(event);
        const QPoint delta = event->globalPos() - m_startGlobal;
        const qreal horizontal = qAbs(delta.x()) /
            static_cast<qreal>(qMax(1, m_startSize.width()));
        const qreal vertical = qAbs(delta.y()) /
            static_cast<qreal>(qMax(1, m_startSize.height()));
        int width = horizontal >= vertical
            ? m_startSize.width() + delta.x()
            : qRound((m_startSize.height() + delta.y()) * m_ratio);

        const QSize minimum = m_target->minimumSize();
        int minimumWidth = qMax(minimum.width(),
                                qCeil(minimum.height() * m_ratio));
        int maximumWidth = QWIDGETSIZE_MAX;
        if (QWidget *container = m_target->parentWidget()) {
            const int availableWidth = container->width() - m_target->x();
            const int availableHeight = container->height() - m_target->y();
            maximumWidth = qMin(availableWidth,
                                qFloor(availableHeight * m_ratio));
        }
        maximumWidth = qMax(minimumWidth, maximumWidth);
        width = qBound(minimumWidth, width, maximumWidth);
        m_target->resize(width, qRound(width / m_ratio));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            releaseMouse();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor line = palette().color(QPalette::Text);
        line.setAlpha(120);
        QPen pen(line, 1.5, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(width() - 5, height() - 12,
                         width() - 12, height() - 5);
        painter.drawLine(width() - 5, height() - 7,
                         width() - 7, height() - 5);
    }

private:
    QWidget *m_target = nullptr;
    QPoint m_startGlobal;
    QSize m_startSize;
    qreal m_ratio = 1.0;
    bool m_dragging = false;
};

QIcon smartMenuIcon(const QString &name)
{
    return QIcon(QStringLiteral(":/smart-space/menu-%1.png").arg(name));
}

struct FormatOption {
    const char *id;
    const char *label;
};

const QVector<FormatOption> &formatOptions()
{
    static const QVector<FormatOption> options{
        {"pdf", "PDF"},
        {"docx", "DOCX"}, {"doc", "DOC"}, {"wps", "WPS"}, {"odt", "ODT"},
        {"pptx", "PPTX"}, {"ppt", "PPT"}, {"dps", "DPS"}, {"odp", "ODP"},
        {"xlsx", "XLSX"}, {"xls", "XLS"}, {"et", "ET"}, {"ods", "ODS"},
        {"txt", "TXT"}, {"md", "MD"}, {"markdown", "MARKDOWN"},
        {"csv", "CSV"}, {"tsv", "TSV"}, {"rtf", "RTF"},
        {"json", "JSON"}, {"xml", "XML"}, {"yaml", "YAML"},
        {"yml", "YML"}, {"html", "HTML"}, {"htm", "HTM"},
        {"log", "LOG"}, {"ini", "INI"}, {"conf", "CONF"},
        {"jpg", "JPG"}, {"jpeg", "JPEG"}, {"png", "PNG"},
        {"webp", "WEBP"}, {"tif", "TIF"}, {"tiff", "TIFF"},
        {"bmp", "BMP"}, {"gif", "GIF"},
        {"css", "CSS"}, {"js", "JS"}, {"ts", "TS"},
        {"cpp", "CPP"}, {"c", "C"}, {"h", "H"}, {"hpp", "HPP"},
        {"py", "PY"}, {"sh", "SH"}, {"desktop", "DESKTOP"},
        {"__other__", "其他或无扩展名"}
    };
    return options;
}

const QSet<QString> &knownFormatIds()
{
    static const QSet<QString> ids = [] {
        QSet<QString> result;
        for (const FormatOption &option : formatOptions()) {
            const QString id = QString::fromLatin1(option.id);
            if (id != QLatin1String("__other__"))
                result.insert(id);
        }
        return result;
    }();
    return ids;
}

bool formatAllowed(const QString &suffix, const QStringList &selected)
{
    if (selected.isEmpty())
        return true;
    const QString extension = suffix.toLower().remove(QLatin1Char('.'));
    if (selected.contains(extension))
        return true;
    return selected.contains(QStringLiteral("__other__")) &&
           !knownFormatIds().contains(extension);
}

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QPixmap modernRailGlyph(const QString &key, const QColor &color)
{
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (key == QLatin1String("pin")) {
        QPainterPath pin;
        pin.moveTo(17, 10);
        pin.lineTo(31, 10);
        pin.lineTo(29, 18);
        pin.lineTo(34, 24);
        pin.lineTo(14, 24);
        pin.lineTo(19, 18);
        pin.closeSubpath();
        painter.fillPath(pin, color);
        painter.drawLine(QPointF(24, 24), QPointF(24, 39));
        painter.drawLine(QPointF(20, 39), QPointF(24, 34));
        painter.drawLine(QPointF(28, 39), QPointF(24, 34));
    } else if (key == QLatin1String("hide")) {
        painter.drawLine(QPointF(12, 12), QPointF(12, 36));
        QPainterPath arrow;
        arrow.moveTo(31, 13);
        arrow.lineTo(20, 24);
        arrow.lineTo(31, 35);
        painter.drawPath(arrow);
    } else if (key == QLatin1String("database")) {
        painter.drawEllipse(QRectF(12, 9, 24, 9));
        painter.drawArc(QRectF(12, 17, 24, 9), 180 * 16, 180 * 16);
        painter.drawArc(QRectF(12, 25, 24, 9), 180 * 16, 180 * 16);
        painter.drawArc(QRectF(12, 31, 24, 8), 180 * 16, 180 * 16);
        painter.drawLine(QPointF(12, 13.5), QPointF(12, 35));
        painter.drawLine(QPointF(36, 13.5), QPointF(36, 35));
    } else if (key == QLatin1String("refresh")) {
        painter.drawArc(QRectF(10, 10, 28, 28), 35 * 16, 275 * 16);
        QPainterPath arrow;
        arrow.moveTo(34, 8);
        arrow.lineTo(39, 17);
        arrow.lineTo(29, 16);
        arrow.closeSubpath();
        painter.fillPath(arrow, color);
    } else if (key == QLatin1String("ocr")) {
        QPainterPath document;
        document.moveTo(15, 8);
        document.lineTo(29, 8);
        document.lineTo(36, 15);
        document.lineTo(36, 39);
        document.lineTo(15, 39);
        document.closeSubpath();
        painter.drawPath(document);
        painter.drawLine(QPointF(29, 8), QPointF(29, 16));
        painter.drawLine(QPointF(29, 16), QPointF(36, 16));
        painter.drawLine(QPointF(20, 23), QPointF(31, 23));
        painter.drawLine(QPointF(20, 29), QPointF(31, 29));
        painter.drawLine(QPointF(20, 35), QPointF(28, 35));
    } else if (key == QLatin1String("pause")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(15, 11, 7, 27), 2.5, 2.5);
        painter.drawRoundedRect(QRectF(27, 11, 7, 27), 2.5, 2.5);
    } else if (key == QLatin1String("folder")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(7, 12, 34, 25), 4, 4);
        painter.drawRoundedRect(QRectF(10, 8, 15, 9), 3, 3);
    } else if (key == QLatin1String("list")) {
        painter.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoint(QPointF(10, 13));
        painter.drawPoint(QPointF(10, 24));
        painter.drawPoint(QPointF(10, 35));
        painter.drawLine(QPointF(18, 13), QPointF(38, 13));
        painter.drawLine(QPointF(18, 24), QPointF(38, 24));
        painter.drawLine(QPointF(18, 35), QPointF(38, 35));
    } else if (key == QLatin1String("sparkle")) {
        QPainterPath sparkle;
        sparkle.moveTo(24, 7);
        sparkle.cubicTo(25.5, 17, 31, 22.5, 41, 24);
        sparkle.cubicTo(31, 25.5, 25.5, 31, 24, 41);
        sparkle.cubicTo(22.5, 31, 17, 25.5, 7, 24);
        sparkle.cubicTo(17, 22.5, 22.5, 17, 24, 7);
        sparkle.closeSubpath();
        painter.fillPath(sparkle, color);
    } else if (key == QLatin1String("settings")) {
        painter.drawEllipse(QRectF(17, 17, 14, 14));
        painter.drawEllipse(QRectF(21, 21, 6, 6));
        for (int angle = 0; angle < 360; angle += 45) {
            const qreal radians = qDegreesToRadians(static_cast<qreal>(angle));
            painter.drawLine(
                QPointF(24 + std::cos(radians) * 10,
                        24 + std::sin(radians) * 10),
                QPointF(24 + std::cos(radians) * 16,
                        24 + std::sin(radians) * 16));
        }
    }
    return pixmap;
}

QIcon modernRailIcon(const QString &key, const QColor &normal,
                     const QColor &active, const QColor &selected)
{
    QIcon icon;
    icon.addPixmap(modernRailGlyph(key, normal), QIcon::Normal, QIcon::Off);
    icon.addPixmap(modernRailGlyph(key, active), QIcon::Active, QIcon::Off);
    QColor disabled = normal;
    disabled.setAlpha(85);
    icon.addPixmap(modernRailGlyph(key, disabled), QIcon::Disabled, QIcon::Off);
    icon.addPixmap(modernRailGlyph(key, selected), QIcon::Normal, QIcon::On);
    icon.addPixmap(modernRailGlyph(key, selected), QIcon::Active, QIcon::On);
    return icon;
}

void repairBackingStoreAfterDrag(QWidget *source)
{
    if (!source) return;
    auto repair = [source] {
        QWidget *top = source->window();
        QWidget *embedded = source;
        while (embedded->parentWidget() && embedded->parentWidget() != top)
            embedded = embedded->parentWidget();
        if (top) top->repaint();
        if (embedded && embedded != top) embedded->repaint();
    };
    QTimer::singleShot(0, source, repair);
    QTimer::singleShot(80, source, repair);
}

class SmartEntryList final : public QListWidget
{
public:
    SmartEntryList()
    {
        setDragEnabled(true);
        setAcceptDrops(false);
        setDragDropMode(QAbstractItemView::DragOnly);
        setDefaultDropAction(Qt::CopyAction);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
    }

    void setBlankClickAction(std::function<void()> action)
    {
        m_blankClickAction = std::move(action);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !itemAt(event->pos())) {
            clearSelection();
            if (m_blankClickAction)
                m_blankClickAction();
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }

    void startDrag(Qt::DropActions) override
    {
        QList<QUrl> urls;
        for (QListWidgetItem *item : selectedItems()) {
            const QString path = item->data(Qt::UserRole + 1).toString();
            if (!path.isEmpty() && QFileInfo::exists(path))
                urls << QUrl::fromLocalFile(path);
        }
        if (urls.isEmpty()) return;
        auto *mime = new QMimeData;
        mime->setUrls(urls);
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
        repairBackingStoreAfterDrag(this);
    }

private:
    std::function<void()> m_blankClickAction;
};

class SmartFileCard final : public QWidget
{
public:
    explicit SmartFileCard(std::function<void()> activate,
                           QWidget *parent = nullptr)
        : QWidget(parent), m_activate(std::move(activate))
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) &&
            m_activate) {
            m_activate();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    std::function<void()> m_activate;
};

QString normalizedPath(QString path)
{
    path = QDir::cleanPath(path);
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

bool pathIsInside(const QString &path, const QString &folder)
{
    const QString child = normalizedPath(path);
    const QString parent = normalizedPath(folder);
    return child == parent || child.startsWith(parent + QLatin1Char('/'));
}

bool copyDirectoryTree(const QString &sourcePath, const QString &targetPath,
                       QString *error)
{
    const QDir source(sourcePath);
    if (!source.exists()) {
        if (error)
            *error = QStringLiteral("源目录不存在");
        return false;
    }
    if (!QDir().mkpath(targetPath)) {
        if (error)
            *error = QStringLiteral("无法创建目标目录");
        return false;
    }
    const QFileInfoList entries = source.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString target = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryTree(entry.absoluteFilePath(), target, error))
                return false;
            continue;
        }
        QFile::remove(target);
        if (!QFile::copy(entry.absoluteFilePath(), target)) {
            if (error)
                *error = QStringLiteral("无法复制文件：%1")
                    .arg(entry.fileName());
            return false;
        }
    }
    return true;
}

QString categoryTitle(const QString &category)
{
    if (category == QLatin1String("image")) return QStringLiteral("图片");
    if (category == QLatin1String("pdf")) return QStringLiteral("PDF");
    if (category == QLatin1String("presentation")) return QStringLiteral("演示");
    if (category == QLatin1String("spreadsheet")) return QStringLiteral("表格");
    if (category == QLatin1String("document")) return QStringLiteral("文档");
    return QStringLiteral("其他");
}

QIcon entryIcon(const SmartSpaceEntry &entry)
{
    if (entry.isDir)
        return QIcon::fromTheme(QStringLiteral("folder"));
    if (entry.category == QLatin1String("image"))
        return QIcon::fromTheme(QStringLiteral("image-x-generic"));
    if (entry.category == QLatin1String("pdf"))
        return QIcon::fromTheme(QStringLiteral("application-pdf"));
    if (entry.category == QLatin1String("presentation"))
        return QIcon::fromTheme(QStringLiteral("x-office-presentation"));
    if (entry.category == QLatin1String("spreadsheet"))
        return QIcon::fromTheme(QStringLiteral("x-office-spreadsheet"));
    return QIcon::fromTheme(QStringLiteral("text-x-generic"));
}

QString formattedFileSize(qint64 size)
{
    if (size < 0)
        return QString();
    constexpr qreal kib = 1024.0;
    constexpr qreal mib = kib * 1024.0;
    constexpr qreal gib = mib * 1024.0;
    if (size >= gib)
        return QStringLiteral("%1 GB").arg(size / gib, 0, 'f', 1);
    if (size >= mib)
        return QStringLiteral("%1 MB").arg(size / mib, 0, 'f', 1);
    if (size >= kib)
        return QStringLiteral("%1 KB").arg(size / kib, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(size);
}

QString formattedModified(const QDateTime &modified)
{
    if (!modified.isValid())
        return QString();
    const QDateTime local = modified.toLocalTime();
    const QDate today = QDate::currentDate();
    if (local.date() == today)
        return QStringLiteral("今天 %1").arg(local.time().toString(QStringLiteral("HH:mm")));
    if (local.date() == today.addDays(-1))
        return QStringLiteral("昨天 %1").arg(local.time().toString(QStringLiteral("HH:mm")));
    return local.toString(QStringLiteral("MM-dd HH:mm"));
}

QStringList localQueryTerms(const QString &query)
{
    QStringList result;
    QSet<QString> seen;
    auto add = [&result, &seen](QString term) {
        term = term.trimmed().toCaseFolded();
        if (term.size() < 2 || seen.contains(term) || result.size() >= 28)
            return;
        seen.insert(term);
        result << term;
    };
    add(query);
    const QRegularExpression parts(
        QStringLiteral("[\\p{Han}]+|[A-Za-z]+|[0-9]+"));
    QRegularExpressionMatchIterator iterator = parts.globalMatch(query);
    while (iterator.hasNext()) {
        const QString part = iterator.next().captured();
        add(part);
        const bool han = QRegularExpression(
            QStringLiteral("^[\\p{Han}]+$")).match(part).hasMatch();
        if (!han || part.size() <= 2)
            continue;
        // Chinese searches do not always contain spaces. Bounded 2-4
        // character n-grams keep local matching useful for phrases such as
        // “宁波银行2025年报告”.
        for (int length = qMin(4, part.size()); length >= 2; --length) {
            for (int offset = 0; offset + length <= part.size(); ++offset)
                add(part.mid(offset, length));
        }
    }
    std::sort(result.begin(), result.end(), [](const QString &left,
                                                const QString &right) {
        return left.size() == right.size() ? left < right
                                           : left.size() > right.size();
    });
    return result;
}

int localEntryScore(const SmartSpaceEntry &entry, const QString &query,
                    QStringList *matchedTerms = nullptr)
{
    const QString foldedQuery = query.trimmed().toCaseFolded();
    const QString name = entry.name.toCaseFolded();
    const QString path = entry.path.toCaseFolded();
    const QString content = entry.content.toCaseFolded();
    int score = 0;
    if (!foldedQuery.isEmpty()) {
        if (name.contains(foldedQuery)) score += kExactNameScore;
        if (content.contains(foldedQuery)) score += kExactContentScore;
        if (path.contains(foldedQuery)) score += kExactPathScore;
    }
    for (const QString &term : localQueryTerms(query)) {
        const bool inName = name.contains(term);
        const bool inContent = content.contains(term);
        const bool inPath = path.contains(term);
        if (!inName && !inContent && !inPath)
            continue;
        if (matchedTerms && !matchedTerms->contains(term))
            matchedTerms->append(term);
        const int lengthBonus = qMin(10, term.size()) * 8;
        if (inName) score += kTermNameScore + lengthBonus;
        if (inContent) score += kTermContentScore + lengthBonus;
        if (inPath) score += kTermPathScore + lengthBonus;
        if (term.size() == 4 && term.at(0).isDigit())
            score += kYearTermBonus;
    }
    return score;
}

void clearButtonLayout(QHBoxLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

} // namespace

SmartSpaceWidget::SmartSpaceWidget(bool fenceEmbedded, QWidget *parent)
    : QWidget(parent)
    , m_fenceEmbedded(fenceEmbedded)
{
    // Embedded mode needs an opaque backing store.  The normal desktop-child
    // mode follows SystemMonitor: transparent corners, with every visible
    // surface painted explicitly and clipped by a rounded mask.
    setAttribute(Qt::WA_OpaquePaintEvent, m_fenceEmbedded);
    setAutoFillBackground(m_fenceEmbedded);
    setMinimumSize(620, 360);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("智能空间"));
    setObjectName(QStringLiteral("smartSpaceSurface"));

    buildUi();
    restoreSettings();
    m_glassBackdropRefreshTimer.setSingleShot(true);
    connect(&m_glassBackdropRefreshTimer, &QTimer::timeout,
            this, &SmartSpaceWidget::refreshGlassBackdrop);
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *canvas = qobject_cast<DesktopCanvas *>(ancestor)) {
            connect(canvas, &DesktopCanvas::wallpaperChanged, this, [this] {
                m_glassBackdrop = {};
                refreshGlassBackdrop();
                update();
            });
            break;
        }
    }
    m_glassPointerTimer.setInterval(33);
    connect(&m_glassPointerTimer, &QTimer::timeout, this, [this] {
        if (m_themeMode != 3 || m_fenceEmbedded || m_edgeHidden || !isVisible()) {
            if (m_glassPointerActive) {
                m_glassPointerActive = false;
                update();
            }
            if (m_themeMode != 3 || m_fenceEmbedded || m_edgeHidden)
                m_glassPointerTimer.stop();
            return;
        }

        const QPoint globalPosition = QCursor::pos();
        const QPoint localPosition = mapFromGlobal(globalPosition);
        // SmartSpace is normally a child of the desktop canvas. In that mode
        // widgetAt() resolves to the canvas window instead of this child, so
        // use the mapped geometry as the stable hit test for both modes.
        const bool active = rect().contains(localPosition);
        if (m_glassPointerActive != active ||
            (active && m_glassPointerPosition != localPosition)) {
            m_glassPointerActive = active;
            m_glassPointerPosition = localPosition;
            update();
        }
    });
    applyTheme();
    if (m_themeMode == 3)
        refreshGlassBackdrop();
    rebuildFileScopePill();
    rebuildCategoryPills();
    updateActionState();
    updateRoundedMask();

    m_indexer = new QProcess(this);
    connect(m_indexer, &QProcess::readyReadStandardOutput,
            this, &SmartSpaceWidget::handleIndexerOutput);
    connect(m_indexer, &QProcess::readyReadStandardError,
            this, &SmartSpaceWidget::handleIndexerOutput);
    connect(m_indexer,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                finishIndexing(code, static_cast<int>(status));
            });
    connect(m_indexer, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_indexBusy = false;
        m_idleFullIndex = false;
        m_ocrBackfill = false;
        m_idleUsesProcessGroup = false;
        if (m_idleIndexButton) {
            m_idleIndexButton->setText(QStringLiteral("快全量"));
            m_idleIndexButton->setToolTip(QStringLiteral(
                "快速全量：不启动 OCR，建立不限文件数的完整快照"));
            m_idleIndexButton->setEnabled(true);
        }
        if (m_ocrIndexButton)
            m_ocrIndexButton->setEnabled(true);
        if (m_refreshButton)
            m_refreshButton->setEnabled(true);
        if (m_pauseIndexButton)
            m_pauseIndexButton->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("索引器启动失败，请检查 Python3 和索引脚本"));
        if (m_indexProgress)
            m_indexProgress->hide();
    });

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this] { scheduleReindex(); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this] { scheduleReindex(); });

    m_reindexDebounce.setSingleShot(true);
    m_reindexDebounce.setInterval(4000);
    connect(&m_reindexDebounce, &QTimer::timeout,
            this, &SmartSpaceWidget::startIndexing);

    connect(&m_periodicRefresh, &QTimer::timeout,
            this, &SmartSpaceWidget::startIndexing);
    configureIndexScheduling();
    // Login brings up the desktop, file manager and session services in
    // several waves. QFileSystemWatcher can report those startup changes as
    // ordinary directory changes, so keep automatic policies disarmed while
    // the desktop session settles.
    QTimer::singleShot(30000, this, [this] {
        m_automaticIndexingReady = true;
    });

    if (QFileInfo::exists(indexPath())) {
        loadIndex();
    } else {
        rebuildWatches();
        m_statusLabel->setText(QStringLiteral(
            "尚未建立索引 · 点击更新按钮开始，打开组件不会自动扫描"));
    }
    // Startup auto-index is a test-fixture capability only. Requiring the
    // second guard prevents a leaked variable in a real desktop session from
    // silently starting the user's index.
    if (qEnvironmentVariableIntValue("UKUI_FENCES_SMARTSPACE_AUTO_INDEX") == 1 &&
        qEnvironmentVariableIntValue("UKUI_FENCES_TEST_AUTO_INDEX") == 1)
        QTimer::singleShot(250, this, &SmartSpaceWidget::startIndexing);

    // Default hidden: auto-hide to nearest edge after initialization
    if (m_defaultHidden && !m_edgeHidden) {
        QTimer::singleShot(200, this, [this]() {
            hideToNearestEdge();
        });
    }
}

SmartSpaceWidget::~SmartSpaceWidget()
{
    m_glassPointerTimer.stop();
    saveSettings();
    if (m_indexer && m_indexer->state() != QProcess::NotRunning) {
        if ((m_idleFullIndex || m_ocrBackfill) && m_idleUsesProcessGroup &&
            signalOwnedProcessGroup(m_indexer, SIGTERM)) {
            // The helper and any OCR descendants received SIGTERM together.
        }
        else
            m_indexer->terminate();
        if (!m_indexer->waitForFinished(500))
            m_indexer->kill();
    }
    if (m_knowledgeProcess &&
        m_knowledgeProcess->state() != QProcess::NotRunning) {
        QPointer<QProcess> process = m_knowledgeProcess;
        terminateKnowledgeProcess(false);
        if (process && !process->waitForFinished(500) && process)
            terminateKnowledgeProcess(true);
    }
    if (m_previewProcess && m_previewProcess->state() != QProcess::NotRunning) {
        m_previewProcess->kill();
        m_previewProcess->waitForFinished(200);
    }
}

void SmartSpaceWidget::buildUi()
{
    auto *shell = new QHBoxLayout(this);
    shell->setContentsMargins(4, 4, 4, 4);
    shell->setSpacing(0);

    m_actionRail = new QFrame(this);
    m_actionRail->setObjectName(QStringLiteral("smartActionRail"));
    m_actionRail->setFixedWidth(62);
    auto *railLayout = new QVBoxLayout(m_actionRail);
    railLayout->setContentsMargins(3, 7, 3, 7);
    railLayout->setSpacing(4);

    auto createRailButton = [this, railLayout](const QString &iconKey,
                                                const QString &text,
                                                const QString &tip) {
        auto *button = new QToolButton(m_actionRail);
        button->setProperty("railIconKey", iconKey);
        button->setIcon(modernRailIcon(
            iconKey, QColor(QStringLiteral("#94a3b8")),
            QColor(QStringLiteral("#e2e8f0")), Qt::white));
        button->setIconSize(QSize(19, 19));
        button->setText(text);
        button->setToolTip(tip);
        button->setProperty("railAction", true);
        button->setAutoRaise(false);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setFixedSize(54, 48);
        railLayout->addWidget(button, 0, Qt::AlignHCenter);
        return button;
    };

    m_pinButton = createRailButton(
        QStringLiteral("pin"), QStringLiteral("置顶"),
        QStringLiteral("让智能空间悬浮在所有应用上方"));
    m_pinButton->setCheckable(true);
    m_closeButton = createRailButton(
        QStringLiteral("hide"), QStringLiteral("隐藏"),
        QStringLiteral("贴边隐藏；星标会留在这个按钮的同一纵向位置"));
    m_idleIndexButton = createRailButton(
        QStringLiteral("database"), QStringLiteral("快全量"),
        QStringLiteral("快速全量：不限文件数，不启动 OCR，待检测/OCR 文件单独标记"));
    m_refreshButton = createRailButton(
        QStringLiteral("refresh"), QStringLiteral("增量"),
        QStringLiteral("快速增量：复用现有索引，仅更新新增或变更文件，不启动 OCR"));
    m_ocrIndexButton = createRailButton(
        QStringLiteral("ocr"), QStringLiteral("OCR"),
        QStringLiteral("先检测 PDF 文本层，再仅识别图片和无文本层 PDF，支持断点"));
    m_pauseIndexButton = createRailButton(
        QStringLiteral("pause"), QStringLiteral("暂停"),
        QStringLiteral("安全中断当前任务；快速全量与 OCR 保留独立断点"));
    m_pauseIndexButton->setEnabled(false);
    railLayout->addStretch(1);
    m_moreButton = createRailButton(
        QStringLiteral("settings"), QStringLiteral("设置"),
        QStringLiteral("智能空间设置"));
    m_moreButton->setObjectName(QStringLiteral("smartMoreButton"));
    shell->addWidget(m_actionRail);

    m_contentContainer = new QWidget(this);
    auto *outer = new QVBoxLayout(m_contentContainer);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);
    shell->addWidget(m_contentContainer, 1);

    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("smartSpaceHeader"));
    m_header->installEventFilter(this);
    auto *headerLayout = new QVBoxLayout(m_header);
    headerLayout->setContentsMargins(10, 10, 10, 10);
    headerLayout->setSpacing(5);

    auto makeToolButton = [this](QHBoxLayout *row, const QString &icon,
                                 const QString &text, const QString &tip,
                                 const QString &role) {
        auto *button = new QToolButton(m_header);
        if (!icon.isEmpty())
            button->setIcon(QIcon::fromTheme(icon));
        button->setText(text);
        button->setToolTip(tip);
        button->setProperty("buttonRole", role);
        button->setAutoRaise(false);
        if (!text.isEmpty())
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        row->addWidget(button);
        return button;
    };

    auto *commandRow = new QHBoxLayout;
    commandRow->setContentsMargins(0, 0, 0, 0);
    commandRow->setSpacing(7);
    m_searchEdit = new QLineEdit(m_header);
    m_searchEdit->setObjectName(QStringLiteral("smartSpaceSearch"));
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索（空格=且；re:正则）"));
    m_searchEdit->setToolTip(QStringLiteral(
        "输入后点击“搜索”或按 Enter 才执行\n"
        "空格分隔多个必须同时命中的条件，例如：宁波 pdf\n"
        "正则表达式以 re: 开头，例如：re:^宁波.*\\.pdf$"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumHeight(42);
    m_searchEdit->setMinimumWidth(100);
    m_searchEdit->addAction(QIcon::fromTheme(QStringLiteral("edit-find")),
                            QLineEdit::LeadingPosition);
    QPixmap regexBadge(30, 20);
    regexBadge.fill(Qt::transparent);
    {
        QPainter painter(&regexBadge);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QColor(QStringLiteral("#94a3b8")));
        painter.setBrush(QColor(QStringLiteral("#f8fafc")));
        painter.drawRoundedRect(QRectF(0.5, 0.5, 29, 19), 5, 5);
        painter.drawText(regexBadge.rect(), Qt::AlignCenter,
                         QStringLiteral("re:"));
    }
    QAction *regexHint = m_searchEdit->addAction(
        QIcon(regexBadge), QLineEdit::TrailingPosition);
    regexHint->setEnabled(false);
    regexHint->setToolTip(QStringLiteral("以 re: 开头使用正则表达式"));
    commandRow->addWidget(m_searchEdit, 1);
    m_searchButton = makeToolButton(
        commandRow, QString(), QStringLiteral("搜索"),
        QStringLiteral("执行搜索（Enter）；空格表示“且”，re:表示正则"),
        QStringLiteral("accent"));
    m_searchButton->setObjectName(QStringLiteral("smartSearchButton"));
    m_searchButton->setFixedWidth(70);

    m_indexProgress = new QProgressBar(m_header);
    m_indexProgress->setObjectName(QStringLiteral("smartIndexProgress"));
    m_indexProgress->setRange(0, 0);
    m_indexProgress->setTextVisible(true);
    m_indexProgress->setFormat(QStringLiteral("正在统计待索引文件…"));
    m_indexProgress->setAlignment(Qt::AlignCenter);
    m_indexProgress->setFixedHeight(20);
    m_indexProgress->setMouseTracking(true);
    m_indexProgress->setAttribute(Qt::WA_Hover, true);
    m_indexProgress->installEventFilter(this);
    m_indexProgress->setToolTip(QStringLiteral("索引进度"));
    m_indexProgress->hide();

    m_categoryCombo = new QComboBox(m_header);
    m_categoryCombo->addItem(QStringLiteral("全部类型"), QString());
    m_categoryCombo->addItem(QStringLiteral("图片"), QStringLiteral("image"));
    m_categoryCombo->addItem(QStringLiteral("PDF"), QStringLiteral("pdf"));
    m_categoryCombo->addItem(QStringLiteral("文档"), QStringLiteral("document"));
    m_categoryCombo->addItem(QStringLiteral("演示"), QStringLiteral("presentation"));
    m_categoryCombo->addItem(QStringLiteral("表格"), QStringLiteral("spreadsheet"));
    m_categoryCombo->addItem(QStringLiteral("其他"), QStringLiteral("other"));
    // The type list is a native QComboBox popup.  On this X11/KWin graphics
    // stack its compositor shadow can be painted as a solid black frame when
    // the parent widget is not topmost.  Keep the native view and styling,
    // but disable only that extra popup shadow.
    if (QWidget *categoryPopup = m_categoryCombo->view())
        categoryPopup->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    m_categoryCombo->setMinimumHeight(42);
    m_categoryCombo->setFixedWidth(256);
    commandRow->addSpacing(8);
    auto *headerSeparator = new QFrame(m_header);
    headerSeparator->setObjectName(QStringLiteral("smartHeaderSeparator"));
    headerSeparator->setFrameShape(QFrame::VLine);
    headerSeparator->setFrameShadow(QFrame::Plain);
    headerSeparator->setFixedWidth(1);
    commandRow->addWidget(headerSeparator);
    commandRow->addSpacing(4);
    commandRow->addWidget(m_categoryCombo);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        m_selectedCategories.clear();
        if (m_categoryCombo && index > 0)
            m_selectedCategories << m_categoryCombo->itemData(index).toString();
        updateResults(true);
    });
    m_themeToggleButton = new QToolButton(m_header);
    m_themeToggleButton->setObjectName(QStringLiteral("smartThemeToggle"));
    m_themeToggleButton->setProperty("themeChoice", true);
    m_themeToggleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_themeToggleButton->setFixedSize(42, 42);
    commandRow->addWidget(m_themeToggleButton);
    headerLayout->addLayout(commandRow);
    headerLayout->addWidget(m_indexProgress);

    auto makeHiddenButton = [this](const QString &text) {
        auto *button = new QToolButton(m_header);
        button->setText(text);
        button->hide();
        return button;
    };
    m_settingsButton = makeHiddenButton(QStringLiteral("设置"));
    outer->addWidget(m_header);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("smartMainSplitter"));
    m_splitter->setChildrenCollapsible(false);

    m_folderLevelsContainer = new QWidget;
    m_folderLevelsContainer->installEventFilter(this);
    m_folderLevelsLayout = new QVBoxLayout(m_folderLevelsContainer);
    m_folderLevelsLayout->setContentsMargins(0, 0, 0, 0);
    m_folderLevelsLayout->setSpacing(8);
    m_folderLevelsLayout->addStretch(1);
    m_folderScroll = new QScrollArea;
    m_folderScroll->setWidgetResizable(true);
    m_folderScroll->setFrameShape(QFrame::NoFrame);
    m_folderScroll->setWidget(m_folderLevelsContainer);
    m_folderScroll->viewport()->installEventFilter(this);
    m_folderPanel = createPanel(
        QStringLiteral("文件夹拾取"), m_folderScroll,
        QStringLiteral("smartFolderPanel"));

    m_fileList = createEntryList();
    m_filePanel = createPanel(
        QStringLiteral("匹配结果"), m_fileList,
        QStringLiteral("smartFilePanel"));

    m_splitter->addWidget(m_folderPanel);
    m_splitter->addWidget(m_filePanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_folderPanel->setMinimumWidth(260);
    m_folderPanel->setMaximumWidth(360);
    m_splitter->setSizes({336, 1000});
    outer->addWidget(m_splitter, 1);

    m_previewPanel = new QFrame(this);
    m_previewPanel->setObjectName(QStringLiteral("smartPreviewPanel"));
    auto *previewLayout = new QVBoxLayout(m_previewPanel);
    previewLayout->setContentsMargins(12, 11, 12, 11);
    previewLayout->setSpacing(8);
    auto *previewHeader = new QHBoxLayout;
    m_previewTitle = new QLabel(QStringLiteral("相关内容"), m_previewPanel);
    m_previewTitle->setObjectName(QStringLiteral("smartPreviewTitle"));
    previewHeader->addWidget(m_previewTitle, 1);
    m_previewCloseButton = new QToolButton(m_previewPanel);
    m_previewCloseButton->setText(QStringLiteral("×"));
    m_previewCloseButton->setToolTip(QStringLiteral("关闭预览"));
    m_previewCloseButton->setProperty("buttonRole", QStringLiteral("quiet"));
    previewHeader->addWidget(m_previewCloseButton);
    previewLayout->addLayout(previewHeader);
    m_previewMeta = new QLabel(m_previewPanel);
    m_previewMeta->setObjectName(QStringLiteral("smartPreviewMeta"));
    m_previewMeta->setWordWrap(true);
    previewLayout->addWidget(m_previewMeta);
    m_previewStack = new QStackedWidget(m_previewPanel);
    m_previewText = new QTextBrowser(m_previewStack);
    m_previewText->setObjectName(QStringLiteral("smartPreviewText"));
    m_previewText->setFrameShape(QFrame::NoFrame);
    m_previewText->setOpenExternalLinks(false);
    m_previewStack->addWidget(m_previewText);
    auto *imageScroll = new QScrollArea(m_previewStack);
    imageScroll->setObjectName(QStringLiteral("smartPreviewImageScroll"));
    imageScroll->setWidgetResizable(true);
    imageScroll->setFrameShape(QFrame::NoFrame);
    m_previewImage = new QLabel(imageScroll);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setObjectName(QStringLiteral("smartPreviewImage"));
    imageScroll->setWidget(m_previewImage);
    m_previewStack->addWidget(imageScroll);
    previewLayout->addWidget(m_previewStack, 1);
    m_previewOpenButton = new QPushButton(QStringLiteral("打开文件"), m_previewPanel);
    m_previewOpenButton->setObjectName(QStringLiteral("smartPreviewOpen"));
    previewLayout->addWidget(m_previewOpenButton);
    m_previewPanel->hide();

    m_statusContainer = new QWidget(this);
    auto *statusRow = new QHBoxLayout(m_statusContainer);
    statusRow->setContentsMargins(2, 0, 0, 0);
    m_statusLabel = new QLabel(QStringLiteral("尚未建立索引"), m_statusContainer);
    m_statusLabel->setObjectName(QStringLiteral("smartSpaceStatus"));
    statusRow->addWidget(m_statusLabel, 1);
    m_sizeGrip = new AspectRatioSizeGrip(this, m_statusContainer);
    m_sizeGrip->setToolTip(QStringLiteral("拖动等比例缩放智能空间"));
    m_sizeGrip->setVisible(!m_fenceEmbedded);
    statusRow->addWidget(m_sizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);
    outer->addWidget(m_statusContainer);

    m_edgeRevealButton = new QToolButton(this);
    m_edgeRevealButton->setObjectName(QStringLiteral("smartEdgeReveal"));
    m_edgeRevealButton->setIcon(modernRailIcon(
        QStringLiteral("sparkle"), QColor(QStringLiteral("#2563eb")),
        QColor(QStringLiteral("#3b82f6")), QColor(QStringLiteral("#2563eb"))));
    m_edgeRevealButton->setIconSize(QSize(38, 38));
    m_edgeRevealButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_edgeRevealButton->setToolTip(QStringLiteral(
        "展开智能空间（位置与隐藏按钮对齐）"));
    m_edgeRevealButton->setAutoRaise(false);
    m_edgeRevealButton->setCursor(Qt::PointingHandCursor);
    m_edgeRevealButton->hide();

    connect(m_refreshButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::startIndexing);
    connect(m_themeToggleButton, &QToolButton::clicked, this, [this] {
        const int nextMode = m_themeMode == 1 ? 2
            : m_themeMode == 2 ? 3 : 1;
        setSimpleTheme(nextMode);
    });
    connect(m_settingsButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::showSettingsDialog);
    connect(m_idleIndexButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::startIdleFullIndex);
    connect(m_ocrIndexButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::startOcrIndex);
    connect(m_pauseIndexButton, &QToolButton::clicked, this, [this] {
        if (!m_indexer || m_indexer->state() == QProcess::NotRunning) {
            m_statusLabel->setText(QStringLiteral("当前没有运行中的索引任务"));
            return;
        }
        m_reindexPending = false;
        if (m_idleFullIndex || m_ocrBackfill) {
            stopIdleFullIndex();
            return;
        }
        m_idleIndexCancelled = true;
        m_statusLabel->setText(QStringLiteral("正在暂停增量索引…"));
        m_indexer->terminate();
        QTimer::singleShot(1500, this, [this] {
            if (m_indexer && m_indexer->state() != QProcess::NotRunning)
                m_indexer->kill();
        });
    });
    connect(m_searchButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::executeSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &SmartSpaceWidget::executeSearch);
    connect(m_pinButton, &QToolButton::clicked, this, [this] {
        setAlwaysOnTop(!m_alwaysOnTop);
    });
    connect(m_closeButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::hideToNearestEdge);
    connect(m_moreButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::showHeaderMenu);
    connect(m_edgeRevealButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::revealFromEdge);
    connect(m_previewCloseButton, &QToolButton::clicked,
            this, &SmartSpaceWidget::hidePreview);
    connect(m_previewOpenButton, &QPushButton::clicked, this, [this] {
        openPath(m_previewPath);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] {
        m_statusLabel->setText(QStringLiteral(
            "搜索条件已修改 · 点击“搜索”或按 Enter 后生效"));
    });
    connect(m_fileList, &QListWidget::itemClicked,
            this, &SmartSpaceWidget::openFileItem);
    connect(m_fileList, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) { showEntryMenu(m_fileList, pos); });
    auto *previewShortcut = new QShortcut(QKeySequence(Qt::Key_Space), m_fileList);
    previewShortcut->setContext(Qt::WidgetShortcut);
    connect(previewShortcut, &QShortcut::activated, this, [this] {
        if (QListWidgetItem *item = m_fileList->currentItem())
            showPreviewForPath(item->data(PathRole).toString());
    });
    auto *closePreviewShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    closePreviewShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closePreviewShortcut, &QShortcut::activated,
            this, &SmartSpaceWidget::hidePreview);

    m_previewProcess = new QProcess(this);
    m_previewProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_previewProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        if (!m_previewVisible || m_pendingPreviewImage.isEmpty())
            return;
        if (status == QProcess::NormalExit && code == 0 &&
            QFileInfo::exists(m_pendingPreviewImage)) {
            showPreviewImage(m_pendingPreviewImage);
            prunePreviewCache();
            return;
        }
        const SmartSpaceEntry *entry = entryForPath(m_previewPath);
        if (entry) {
            m_previewStack->setCurrentWidget(m_previewText);
            m_previewText->setHtml(relevantSnippetHtml(*entry, 3, 56, 120));
        }
    });

    rebuildCategoryPills();

    if (m_fenceEmbedded) {
        m_actionRail->hide();
        m_header->removeEventFilter(this);
        outer->setContentsMargins(6, 6, 6, 4);
    }
}

void SmartSpaceWidget::applyTheme()
{
    const bool glassMode = m_themeMode == 3;
    const bool lightMode = m_themeMode == 2;
    m_customOpacity = 100;
    m_customColorsEnabled = false;
    if (glassMode) {
        // Keep the material neutral and let the captured desktop supply the
        // color. A strong blue tint makes the surface read as a panel.
        m_surfaceColor = QColor(13, 18, 27, 92);
        m_cardColor = QColor(18, 24, 34, 76);
        m_borderColor = QColor(244, 249, 255, 42);
        m_textColor = QColor(247, 249, 252);
        m_mutedColor = QColor(202, 210, 220);
        m_accentColor = QColor(112, 205, 255);
    } else if (lightMode) {
        m_surfaceColor = QColor(QStringLiteral("#f8fafc"));
        m_cardColor = QColor(QStringLiteral("#ffffff"));
        m_borderColor = QColor(QStringLiteral("#e2e8f0"));
        m_textColor = QColor(QStringLiteral("#1e293b"));
        m_mutedColor = QColor(QStringLiteral("#64748b"));
        m_accentColor = QColor(QStringLiteral("#2563eb"));
    } else {
        m_surfaceColor = QColor(QStringLiteral("#0f172a"));
        m_cardColor = QColor(QStringLiteral("#1e293b"));
        m_borderColor = QColor(QStringLiteral("#334155"));
        m_textColor = QColor(QStringLiteral("#f1f5f9"));
        m_mutedColor = QColor(QStringLiteral("#94a3b8"));
        m_accentColor = QColor(QStringLiteral("#3b82f6"));
    }
    setFont(QFont());
    setAttribute(Qt::WA_OpaquePaintEvent, !glassMode);
    setAttribute(Qt::WA_NoSystemBackground, glassMode && isWindow());
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(!glassMode);
    if (!m_fenceEmbedded && layout()) {
        const int inset = glassMode ? 12 : 4;
        layout()->setContentsMargins(inset, inset, inset, inset);
    }
    if (glassMode && !m_fenceEmbedded && !m_edgeHidden)
        m_glassPointerTimer.start();
    else {
        m_glassPointerTimer.stop();
        m_glassPointerActive = false;
    }
    if (glassMode) {
        QPalette transparentPalette = palette();
        transparentPalette.setColor(QPalette::Window, Qt::transparent);
        transparentPalette.setColor(QPalette::Base, Qt::transparent);
        setPalette(transparentPalette);
    }
    if (m_themeToggleButton) {
        const QSignalBlocker blocker(m_themeToggleButton);
        m_themeToggleButton->setText(glassMode ? QStringLiteral("✦")
            : lightMode ? QStringLiteral("☀") : QStringLiteral("☾"));
        m_themeToggleButton->setToolTip(glassMode
            ? QStringLiteral("当前为液态玻璃，点击切换为黑夜模式")
            : lightMode
                ? QStringLiteral("当前为白天模式，点击切换为液态玻璃")
                : QStringLiteral("当前为黑夜模式，点击切换为白天模式"));
    }
    if (m_actionRail) {
        const QList<QToolButton *> railButtons =
            m_actionRail->findChildren<QToolButton *>();
        for (QToolButton *button : railButtons) {
            const QString key = button->property("railIconKey").toString();
            if (key.isEmpty())
                continue;
            button->setIcon(modernRailIcon(
                key, m_mutedColor, m_textColor, Qt::white));
        }
    }

    QPalette widgetPalette = palette();
    widgetPalette.setColor(QPalette::Window,
                          glassMode ? QColor(Qt::transparent) : m_surfaceColor);
    widgetPalette.setColor(QPalette::WindowText, m_textColor);
    widgetPalette.setColor(QPalette::Base, m_cardColor);
    widgetPalette.setColor(QPalette::Text, m_textColor);
    widgetPalette.setColor(QPalette::Button, m_cardColor);
    widgetPalette.setColor(QPalette::ButtonText, m_textColor);
    widgetPalette.setColor(QPalette::Highlight, m_accentColor);
    widgetPalette.setColor(QPalette::HighlightedText, Qt::white);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    widgetPalette.setColor(QPalette::PlaceholderText, m_mutedColor);
#endif
    setPalette(widgetPalette);
    if (m_searchEdit) {
        QPalette searchPalette = m_searchEdit->palette();
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
        searchPalette.setColor(QPalette::PlaceholderText, m_mutedColor);
#endif
        m_searchEdit->setPalette(searchPalette);
    }

    QColor hover = m_accentColor;
    hover.setAlpha(42);
    QColor selected = m_accentColor;
    selected.setAlpha(185);
    QColor field = m_cardColor;
    field.setAlpha(glassMode ? 82 : qMax(220, field.alpha()));

    QColor searchBg = field;
    QColor searchText = m_textColor;
    QColor buttonBg = selected;
    QColor buttonText(Qt::white);
    QColor buttonHover = hover;

    const QColor warningFill(245, 158, 11, 45);
    const QColor warningBorder(245, 158, 11, 115);
    QString style = QStringLiteral(R"(
        QWidget { color: %1; font-size: 13px; }
        QWidget#smartSpaceHeader {
            background: %3; border: 1px solid %4; border-radius: 12px;
        }
        #smartSpaceSearch { color: %10; background: %11; border: 1px solid %4;
                            border-radius: 11px; padding-left: 10px; padding-right: 10px;
                            font-size: 14px; }
        #smartSpaceSearch:focus { border: 1px solid %6; }
        #smartHeaderSeparator { background: %4; }
        #smartSpaceStatus { color: %2; background: %3; border-radius: 7px;
                            padding: 5px 9px; }
        QFrame#smartFolderPanel, QFrame#smartFilePanel, QFrame#smartPreviewPanel {
            background: %3; border: 1px solid %4; border-radius: 12px;
        }
        QFrame#smartPreviewPanel { background: %5; }
        QFrame#smartActionRail {
            background: %3; border: 1px solid %4; border-radius: 12px;
        }
        #smartPreviewTitle { color: %1; font-size: 14px; font-weight: 600; }
        #smartPreviewMeta { color: %2; font-size: 12px; }
        QTextBrowser#smartPreviewText { color: %1; background: transparent;
                                        border: none; padding: 4px; }
        QLabel#smartPreviewImage { background: transparent; }
        QPushButton#smartPreviewOpen {
            color: %12; background: %13; border: 1px solid %13;
            border-radius: 9px; padding: 8px 14px; font-weight: 600;
        }
        QPushButton#smartPreviewOpen:hover { background: %14; border-color: %14; }
        QLabel[panelTitle="true"] { color: %1; font-size: 13px; font-weight: 600;
                                     padding: 1px 2px 3px 2px; }
        QLabel#smartPanelIcon { background: transparent; padding: 0; }
        QLabel#smartPanelCount {
            color: %6; background: rgba(37,99,235,0.08);
            border: 1px solid rgba(37,99,235,0.12); border-radius: 6px;
            padding: 3px 8px; font-size: 12px; font-weight: 600;
        }
        QLineEdit, QComboBox, QSpinBox, QPlainTextEdit {
            color: %10; background: %11; border: 1px solid %4;
            border-radius: 8px; padding: 7px 10px; selection-background-color: %6;
        }
        QLineEdit:focus, QComboBox:focus { border: 1px solid %6; }
        QComboBox::drop-down { width: 26px; border: none; }
        QComboBox QAbstractItemView {
            color: %1; background: %5; border: 1px solid %4;
            selection-background-color: %6;
        }
        QListWidget { color: %1; background: transparent; border: none; outline: none; }
        QListWidget::item { min-height: 25px; padding: 5px 7px; border-radius: 7px; }
        QListWidget::item:hover { background: %7; }
        QListWidget::item:selected { background: %6; color: white; }
        QListWidget::item:disabled { color: %2; }
        QWidget#fileResultCard {
            background: %5; border: 1px solid %4; border-radius: 10px;
        }
        QWidget#fileResultCard:hover { background: %7; border-color: %6; }
        QLabel[cardName="true"] { color: %1; font-size: 13px; font-weight: 600; }
        QLabel[cardMeta="true"] { color: %2; font-size: 11px; }
        QLabel[cardSnippet="true"] { color: %2; font-size: 12px; }
        QLabel[matchScore="true"] {
            color: #10b981; background: transparent; font-size: 11px;
            font-weight: 600; padding: 2px 4px;
        }
        QLabel[matchLevel="medium"] { color: #f59e0b; }
        QToolButton { color: %1; background: transparent; padding: 6px 10px;
                      border: 1px solid transparent; border-radius: 8px; }
        QToolButton#smartSearchButton {
            padding-left: 5px; padding-right: 5px;
        }
        QToolButton:hover { background: %14; }
        QToolButton[buttonRole="quiet"] { background: %5; border-color: %4; }
        QToolButton[buttonRole="accent"] { color: %12; background: %13; }
        QToolButton[buttonRole="accent"]:hover { background: %14; }
        QToolButton[buttonRole="warning"] { background: %8; border-color: %9; }
        QToolButton[titleAction="true"] {
            color: %2; background: transparent; border: none;
            border-radius: 15px; padding: 5px 10px; min-height: 20px;
        }
        QToolButton[titleAction="true"]:hover { color: %1; background: %14; }
        QToolButton[titleAction="true"]:checked {
            color: %12; background: %13; font-weight: 600;
        }
        QToolButton[themeChoice="true"] {
            color: %2; background: %5; border: 1px solid %4;
            border-radius: 8px; padding: 6px 10px;
        }
        QToolButton[themeChoice="true"]:hover { color: %1; background: %14; }
        QToolButton[themeChoice="true"]:checked {
            color: %12; background: %13; border-color: %13;
            font-weight: 600;
        }
        QToolButton#smartThemeToggle {
            font-size: 20px; font-weight: 600; padding: 0;
        }
        QToolButton[railAction="true"] {
            color: %2; background: transparent; border: 1px solid transparent;
            border-radius: 9px; padding: 3px 0; font-size: 10px;
        }
        QToolButton[railAction="true"]:hover {
            color: %1; background: %14; border-color: %4;
        }
        QToolButton[railAction="true"]:checked {
            color: %12; background: %13; border-color: %13;
            font-weight: 600;
        }
        QToolButton[pill="true"] {
            color: %2; background: %5; border: 1px solid %4;
            border-radius: 15px; padding: 5px 11px; min-height: 20px;
        }
        QToolButton#smartManageButton {
            padding-left: 7px; padding-right: 7px;
        }
        QToolButton[pill="true"]:hover { color: %1; background: %14; }
        QToolButton[pill="true"]:checked {
            color: %12; background: %13; border-color: %13; font-weight: 600;
        }
        QToolButton[pill="true"]::menu-indicator {
            image: none; width: 0px;
        }
        QToolButton[previewAction="true"] {
            color: %1; background: transparent; border: 1px solid %4;
            border-radius: 8px; padding: 4px 7px;
        }
        QToolButton[previewAction="true"]:hover {
            color: %12; background: %13; border-color: %13;
        }
        QToolButton[loadMoreResults="true"] {
            color: %6; background: %5; border: 1px solid %4;
            border-radius: 9px; padding: 7px 12px; font-weight: 600;
        }
        QToolButton[loadMoreResults="true"]:hover {
            color: %12; background: %13; border-color: %13;
        }
        QToolButton[panelControl="true"] {
            color: %2; background: transparent; border: 1px solid %4;
            border-radius: 7px; padding: 0 12px; font-size: 13px;
        }
        QToolButton[panelControl="true"]:hover { color: %1; background: %14; }
        QToolButton#smartEdgeReveal {
            color: %13; background: transparent; border: none;
            border-radius: 0; padding: 0;
        }
        QToolButton#smartEdgeReveal:hover {
            color: %1; background: transparent;
        }
        QToolButton:disabled { color: %2; background: transparent; border-color: transparent; }
        QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
        QProgressBar#smartIndexProgress {
            color: %1; background: %5; border: 1px solid %4;
            border-radius: 7px; text-align: center; font-size: 11px;
            font-weight: 600; padding: 1px;
        }
        QProgressBar#smartIndexProgress::chunk {
            background: %13; border-radius: 5px;
        }
        QSplitter#smartMainSplitter::handle:horizontal {
            background: %4; width: 8px; margin: 9px 3px;
        }
        QSplitter#smartMainSplitter::handle:horizontal:hover {
            background: %13;
        }
        QSplitter#smartContentSplitter::handle:vertical {
            background: %4; height: 8px; margin: 3px 9px;
            border-radius: 3px;
        }
        QSplitter#smartContentSplitter::handle:vertical:hover {
            background: %13;
        }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 2px; }
        QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )")
        .arg(cssColor(m_textColor))
        .arg(cssColor(m_mutedColor))
        .arg(cssColor(m_cardColor))
        .arg(cssColor(m_borderColor))
        .arg(cssColor(field))
        .arg(cssColor(selected))
        .arg(cssColor(hover))
        .arg(cssColor(warningFill))
        .arg(cssColor(warningBorder))
        .arg(cssColor(searchText))
        .arg(cssColor(searchBg))
        .arg(cssColor(buttonText))
        .arg(cssColor(buttonBg))
        .arg(cssColor(buttonHover));
    if (glassMode) {
        style += QStringLiteral(R"(
            QWidget#smartSpaceHeader,
            QFrame#smartFolderPanel, QFrame#smartFilePanel,
            QFrame#smartPreviewPanel, QFrame#smartActionRail {
                background: rgba(14, 21, 31, 64);
                border: 1px solid rgba(244, 249, 255, 42);
            }
            #smartSpaceSearch, QLineEdit, QComboBox, QSpinBox,
            QPlainTextEdit {
                background: rgba(8, 13, 21, 74);
                border-color: rgba(244, 249, 255, 64);
            }
            QToolButton[railAction="true"], QToolButton[buttonRole="quiet"],
            QToolButton[themeChoice="true"], QToolButton[pill="true"],
            QToolButton[previewAction="true"], QToolButton[panelControl="true"] {
                background: rgba(255, 255, 255, 22);
                border-color: rgba(244, 249, 255, 38);
            }
            QToolButton[buttonRole="accent"],
            QToolButton[titleAction="true"]:checked,
            QToolButton[railAction="true"]:checked,
            QToolButton[pill="true"]:checked {
                background: rgba(95, 198, 255, 128);
                border-color: rgba(236, 250, 255, 120);
            }
            QToolButton:hover, QToolButton[railAction="true"]:hover,
            QToolButton[pill="true"]:hover {
                background: rgba(255, 255, 255, 44);
                border-color: rgba(244, 249, 255, 86);
            }
            QWidget#fileResultCard { background: rgba(12, 18, 27, 58); }
            QWidget#fileResultCard:hover { background: rgba(255, 255, 255, 32); }
            QListWidget::item:hover { background: rgba(255, 255, 255, 28); }
            /* Preview is a readability surface, not another transparent
               layer over the result list. Keep the glass treatment in its
               border while the content sits on a dense scrim. */
            QFrame#smartPreviewPanel {
                background: rgb(8, 13, 21);
                border: 1px solid rgba(244, 249, 255, 82);
            }
            QTextBrowser#smartPreviewText {
                color: rgba(247, 249, 252, 255);
                background: rgb(7, 11, 17);
                border: 1px solid rgba(244, 249, 255, 34);
                border-radius: 9px;
                padding: 10px;
            }
            QScrollArea#smartPreviewImageScroll,
            QScrollArea#smartPreviewImageScroll > QWidget > QWidget,
            QLabel#smartPreviewImage {
                background: rgb(7, 11, 17);
            }
            QProgressBar#smartIndexProgress { background: rgba(8, 12, 18, 72); }
            QProgressBar#smartIndexProgress::chunk { background: rgba(89, 200, 255, 165); }
            /* Transparent borders keep control geometry stable in all states. */
            #smartSpaceSurface QToolButton,
            #smartSpaceSurface QToolButton:hover,
            #smartSpaceSurface QToolButton:pressed,
            #smartSpaceSurface QToolButton:checked,
            #smartSpaceSurface QPushButton,
            #smartSpaceSurface QPushButton:hover {
                border: 1px solid transparent;
            }
            QComboBox QAbstractItemView {
                background: rgb(28, 33, 42); color: rgb(247, 249, 252);
                selection-background-color: rgb(48, 88, 118);
            }
        )");
    }
    setStyleSheet(style);
    updateRoundedMask();
    if (glassMode)
        scheduleGlassBackdropRefresh();
    update();
    emit themeChanged(m_surfaceColor, m_textColor);
}

void SmartSpaceWidget::setSimpleTheme(int mode, bool persist)
{
    const int normalized = mode == 3 ? 3 : mode == 2 ? 2 : 1;
    if (m_themeMode != normalized) {
        m_themeMode = normalized;
        applyTheme();
    } else if (m_themeToggleButton) {
        const bool lightMode = normalized == 2;
        const bool glassMode = normalized == 3;
        m_themeToggleButton->setText(glassMode ? QStringLiteral("✦")
            : lightMode ? QStringLiteral("☀") : QStringLiteral("☾"));
        m_themeToggleButton->setToolTip(glassMode
            ? QStringLiteral("当前为液态玻璃，点击切换为黑夜模式")
            : lightMode
                ? QStringLiteral("当前为白天模式，点击切换为液态玻璃")
                : QStringLiteral("当前为黑夜模式，点击切换为白天模式"));
    }
    if (persist)
        saveSettings();
}

void SmartSpaceWidget::refreshInheritedTheme()
{
    // The redesigned UI intentionally has two explicit, opaque palettes.
}

void SmartSpaceWidget::applyMenuTheme(QMenu *menu) const
{
    if (!menu)
        return;

    // A QMenu is a separate native window.  On its first show UKUI can expose
    // the opaque backing rectangle for one frame before the stylesheet's
    // rounded background is polished.  Prepare translucency and a matching
    // mask before mapping the window so the first and later openings match.
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setAutoFillBackground(false);
    if (!menu->property("smartRoundedMenuPrepared").toBool()) {
        menu->setProperty("smartRoundedMenuPrepared", true);
        connect(menu, &QMenu::aboutToShow, menu, [menu] {
            auto applyRoundedMask = [menu] {
                if (menu->width() <= 1 || menu->height() <= 1)
                    return;
                QPainterPath path;
                path.addRoundedRect(
                    QRectF(menu->rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                    12.0, 12.0);
                menu->setMask(QRegion(path.toFillPolygon().toPolygon()));
            };
            menu->ensurePolished();
            menu->adjustSize();
            applyRoundedMask();
            QTimer::singleShot(0, menu, applyRoundedMask);
        });
    }

    const bool extColors = (m_themeMode == 6 && m_customColorsEnabled);
    const qreal customOpacity = qBound(25, m_customOpacity, 100) / 100.0;
    auto customBackground = [customOpacity](QColor color) {
        color.setAlpha(qRound(color.alpha() * customOpacity));
        return color;
    };
    QColor menuBg = extColors && m_customMenuBg.isValid()
        ? customBackground(m_customMenuBg) : m_cardColor;
    if (m_themeMode == 3)
        menuBg = QColor(28, 33, 42, 255);
    QColor menuText = extColors && m_customMenuText.isValid() ? m_customMenuText : m_textColor;
    QColor menuHover = extColors && m_customMenuHover.isValid()
        ? customBackground(m_customMenuHover) : m_accentColor;
    menuHover.setAlpha(extColors && m_customMenuHover.isValid() ? menuHover.alpha() : 225);
    if (m_themeMode == 3)
        menuHover = QColor(48, 88, 118);

    QColor disabled = extColors && m_customMuted.isValid() ? m_customMuted : m_mutedColor;
    disabled.setAlpha(105);
    QColor separator = extColors && m_customBorder.isValid() ? m_customBorder : m_borderColor;
    separator.setAlpha(qMax(45, separator.alpha()));
    menu->setStyleSheet(QStringLiteral(R"(
        QMenu {
            background-color: %1; color: %2;
            border: 1px solid %3; border-radius: 12px; padding: 5px;
            icon-size: 20px;
        }
        QMenu::item {
            background-color: transparent; padding: 6px 24px 6px 12px;
            border-radius: 6px; min-height: 18px;
        }
        QMenu::item:selected { background-color: %4; color: %2; }
        QMenu::item:disabled { color: %5; background-color: transparent; }
        QMenu::separator {
            height: 1px; background-color: %6; margin: 4px 7px;
        }
        QMenu::indicator { width: 14px; height: 14px; }
    )").arg(cssColor(menuBg), cssColor(menuText),
             cssColor(separator), cssColor(menuHover), cssColor(disabled),
             cssColor(separator)));
}

void SmartSpaceWidget::configureIndexScheduling()
{
    m_periodicRefresh.stop();
    if (m_indexMode == 2) {
        m_periodicRefresh.setInterval(
            qBound(5, m_indexIntervalMinutes, 1440) * 60 * 1000);
        m_periodicRefresh.start();
    }
}

QWidget *SmartSpaceWidget::createPanel(const QString &title, QWidget *content,
                                       const QString &objectName)
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);
    auto *label = new QLabel(title, panel);
    label->setProperty("panelTitle", true);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(4);
    auto *panelIcon = new QLabel(panel);
    panelIcon->setObjectName(QStringLiteral("smartPanelIcon"));
    panelIcon->setPixmap(modernRailGlyph(
        objectName == QLatin1String("smartFolderPanel")
            ? QStringLiteral("folder") : QStringLiteral("list"),
        QColor(QStringLiteral("#2563eb"))).scaled(
            QSize(18, 18), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    titleRow->addWidget(panelIcon);
    titleRow->addWidget(label);
    titleRow->addStretch(1);
    if (objectName == QLatin1String("smartFolderPanel"))
        m_folderTitleLabel = label;
    else if (objectName == QLatin1String("smartFilePanel")) {
        m_fileTitleLabel = label;
        auto makeDensityButton = [panel, titleRow](const QString &text,
                                                   const QString &tip) {
            auto *button = new QToolButton(panel);
            button->setText(text);
            button->setToolTip(tip);
            button->setProperty("panelControl", true);
            button->setFixedSize(28, 26);
            titleRow->addWidget(button);
            return button;
        };
        m_sortButton = makeDensityButton(
            QStringLiteral("排序：按相关度⌄"),
            QStringLiteral("排序：名称命中优先，其次按内容相关度"));
        m_sortButton->setFixedWidth(190);
        connect(m_sortButton, &QToolButton::clicked,
                this, &SmartSpaceWidget::showResultSortMenu);
        m_densityDownButton = makeDensityButton(
            QStringLiteral("−"), QStringLiteral("缩小文件与文件夹，显示更多内容"));
        m_densityUpButton = makeDensityButton(
            QStringLiteral("＋"), QStringLiteral("放大文件与文件夹"));
        m_densityDownButton->hide();
        m_densityUpButton->hide();
        connect(m_densityDownButton, &QToolButton::clicked, this, [this] {
            setResultDensity(m_resultDensity - 1);
        });
        connect(m_densityUpButton, &QToolButton::clicked, this, [this] {
            setResultDensity(m_resultDensity + 1);
        });
    }
    auto *count = new QLabel(QStringLiteral("0 项"), panel);
    count->setObjectName(QStringLiteral("smartPanelCount"));
    count->setAlignment(Qt::AlignCenter);
    count->setMinimumWidth(42);
    if (objectName == QLatin1String("smartFolderPanel"))
        m_folderCountLabel = count;
    else if (objectName == QLatin1String("smartFilePanel"))
        m_fileCountLabel = count;
    titleRow->insertWidget(2, count);
    layout->addLayout(titleRow);
    layout->addWidget(content, 1);
    return panel;
}

QListWidget *SmartSpaceWidget::createEntryList()
{
    auto *list = new SmartEntryList;
    list->setIconSize(QSize(22, 22));
    list->setSpacing(6);
    list->setUniformItemSizes(false);
    list->setMouseTracking(true);
    list->setToolTipDuration(15000);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    return list;
}

void SmartSpaceWidget::rebuildFileScopePill()
{
    if (!m_scopePillLayout || !m_scopePillContainer)
        return;
    clearButtonLayout(m_scopePillLayout);
    auto *scope = new QToolButton(m_scopePillContainer);
    scope->setText(QStringLiteral("全部文件"));
    scope->setToolTip(QStringLiteral(
        "当前检索范围：全部索引文件\n"
        "空间切换和保存检索规则已取消"));
    scope->setProperty("pill", true);
    scope->setEnabled(false);
    scope->setFixedWidth(154);
    m_scopePillLayout->addWidget(scope);
    m_scopePillLayout->addStretch(1);
}

void SmartSpaceWidget::rebuildCategoryPills()
{
    if (!m_categoryPillLayout || !m_categoryPillContainer || !m_categoryCombo)
        return;
    clearButtonLayout(m_categoryPillLayout);
    if (m_categoryButtonGroup) {
        delete m_categoryButtonGroup;
        m_categoryButtonGroup = nullptr;
    }
    auto *picker = new QToolButton(m_categoryPillContainer);
    QString summary = QStringLiteral("全部类型");
    if (m_selectedCategories.size() == 1)
        summary = categoryTitle(m_selectedCategories.first());
    else if (m_selectedCategories.size() > 1)
        summary = QStringLiteral("已选 %1 种类型").arg(m_selectedCategories.size());
    QStringList selectedTitles;
    for (const QString &category : m_selectedCategories)
        selectedTitles << categoryTitle(category);
    picker->setText(summary + QStringLiteral("  ⌄"));
    picker->setProperty("pill", true);
    picker->setPopupMode(QToolButton::InstantPopup);
    picker->setToolTip(m_selectedCategories.isEmpty()
        ? QStringLiteral("当前搜索全部文件类型")
        : QStringLiteral("已选：%1").arg(selectedTitles.join(QStringLiteral("、"))));
    auto *menu = new QMenu(picker);
    applyMenuTheme(menu);
    QAction *all = menu->addAction(QStringLiteral("%1  全部类型")
        .arg(m_selectedCategories.isEmpty() ? QStringLiteral("✔")
                                            : QStringLiteral("○")));
    connect(all, &QAction::triggered, this, [this] {
        m_selectedCategories.clear();
        rebuildCategoryPills();
        updateResults(true);
    });
    menu->addSeparator();
    for (int index = 1; index < m_categoryCombo->count(); ++index) {
        const QString category = m_categoryCombo->itemData(index).toString();
        const bool selected = m_selectedCategories.contains(category);
        QAction *action = menu->addAction(QStringLiteral("%1  %2")
            .arg(selected ? QStringLiteral("✔") : QStringLiteral("○"),
                 m_categoryCombo->itemText(index)));
        connect(action, &QAction::triggered, this, [this, category] {
            if (m_selectedCategories.contains(category))
                m_selectedCategories.removeAll(category);
            else
                m_selectedCategories << category;
            rebuildCategoryPills();
            updateResults(true);
        });
    }
    picker->setMenu(menu);
    m_categoryPillLayout->addWidget(picker);
    m_categoryPillLayout->addStretch(1);
}

void SmartSpaceWidget::showHeaderMenu()
{
    QMenu menu(this);
    applyMenuTheme(&menu);
    QAction *refresh = menu.addAction(
        smartMenuIcon(QStringLiteral("refresh")),
        QStringLiteral("更新索引（增量复用）"));
    QAction *idle = menu.addAction(
        smartMenuIcon(QStringLiteral("fast-full")),
        m_idleFullIndex ? QStringLiteral("快速全量正在运行")
                        : QStringLiteral("快速全量…"));
    idle->setEnabled(!m_indexBusy);
    QAction *ocrIndex = menu.addAction(
        smartMenuIcon(QStringLiteral("ocr")),
        m_ocrBackfill ? QStringLiteral("OCR 检测/补全正在运行")
                      : QStringLiteral("OCR 检测与补全…"));
    ocrIndex->setEnabled(!m_indexBusy);
    menu.addSeparator();
    QAction *knowledgeBuild = menu.addAction(
        QIcon::fromTheme(QStringLiteral("system-search")),
        QStringLiteral("知识库快速建库"));
    QAction *knowledgeStats = menu.addAction(
        QIcon::fromTheme(QStringLiteral("view-statistics")),
        QStringLiteral("查看知识库状态"));
    QAction *knowledgeStop = menu.addAction(
        QIcon::fromTheme(QStringLiteral("media-playback-pause")),
        QStringLiteral("暂停知识库任务"));
    const bool knowledgeBusy = m_knowledgeProcess != nullptr;
    knowledgeBuild->setEnabled(!knowledgeBusy);
    knowledgeStats->setEnabled(!knowledgeBusy &&
                               QFileInfo::exists(knowledgeDbPath()));
    knowledgeStop->setEnabled(knowledgeBusy);
    menu.addSeparator();
    QAction *settings = menu.addAction(
        smartMenuIcon(QStringLiteral("settings")),
        QStringLiteral("智能空间设置"));
    QAction *close = menu.addAction(
        smartMenuIcon(QStringLiteral("close")),
        QStringLiteral("关闭智能空间"));

    connect(refresh, &QAction::triggered, this, &SmartSpaceWidget::startIndexing);
    connect(idle, &QAction::triggered, this, &SmartSpaceWidget::startIdleFullIndex);
    connect(ocrIndex, &QAction::triggered, this, &SmartSpaceWidget::startOcrIndex);
    connect(knowledgeBuild, &QAction::triggered, this,
            &SmartSpaceWidget::startKnowledgeBuild);
    connect(knowledgeStats, &QAction::triggered, this, [this] {
        startKnowledgeTask(QStringLiteral("stats"), {
            QStringLiteral("--db"), knowledgeDbPath()
        });
    });
    connect(knowledgeStop, &QAction::triggered, this, [this] {
        if (m_knowledgeProcess) {
            m_knowledgeProcess->setProperty("userPaused", true);
            terminateKnowledgeProcess(false);
            m_statusLabel->setText(QStringLiteral(
                "正在暂停知识库任务 · 已完成内容会保留"));
        }
    });
    connect(settings, &QAction::triggered, this,
            &SmartSpaceWidget::showSettingsDialog);
    connect(close, &QAction::triggered, this, &SmartSpaceWidget::closeRequested);
    LiquidPopup::execAt(menu,m_moreButton);
}

void SmartSpaceWidget::updateResponsiveLayout()
{
    if (!m_splitter || m_edgeHidden)
        return;
    const bool compact = width() < 760;
    const bool breakpointChanged =
        property("smartCompactLayout").toBool() != compact;
    setProperty("smartCompactLayout", compact);
    if (m_folderPanel)
        m_folderPanel->setVisible(!compact);
    if (breakpointChanged) {
        rebuildFileScopePill();
        rebuildCategoryPills();
    }
    if (!compact && m_splitter->sizes().size() == 2) {
        const int available = qMax(1, m_splitter->width());
        if (m_splitter->sizes().at(0) < 80)
            m_splitter->setSizes({available * 34 / 100,
                                  available * 66 / 100});
    }
    if (!m_previewVisible || !m_previewPanel)
        return;
    const QRect content(m_splitter->mapTo(this, QPoint(0, 0)),
                        m_splitter->size());
    int previewWidth = compact ? content.width() - 8
        : width() >= 1180 ? qBound(340, content.width() * 30 / 100, 460)
                          : qBound(320, content.width() * 42 / 100, 430);
    previewWidth = qMin(previewWidth, content.width());
    m_previewPanel->setGeometry(content.right() - previewWidth + 1,
                                content.top(), previewWidth,
                                content.height());
    m_previewPanel->show();
    m_previewPanel->raise();
}

void SmartSpaceWidget::restoreSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("smartSpace"));
    // Remove settings from the retired remote/semantic feature set.  This is
    // deliberately done during restore so old API keys do not remain in the
    // user's configuration after upgrading.
    const QStringList obsoleteKeys{
        QStringLiteral("aiEnabled"),
        QStringLiteral("aiUseMonitorConfig"),
        QStringLiteral("aiApiUrl"),
        QStringLiteral("aiApiKey"),
        QStringLiteral("aiModel"),
        QStringLiteral("aiSendContent"),
        QStringLiteral("knowledgeEnabled"),
        QStringLiteral("knowledgeSendContent"),
        QStringLiteral("currentSpace")};
    for (const QString &key : obsoleteKeys)
        settings.remove(key);
    m_roots = settings.value(QStringLiteral("roots"), defaultRoots()).toStringList();
    m_excludedFolders = settings.value(
        QStringLiteral("excludedFolders")).toStringList();
    m_hiddenFolders = settings.value(
        QStringLiteral("hiddenFolders")).toStringList();
    m_indexedExtensions = settings.value(
        QStringLiteral("indexedExtensions")).toStringList();
    m_visibleExtensions = settings.value(
        QStringLiteral("visibleExtensions")).toStringList();
    m_ocrImages = settings.value(QStringLiteral("ocrImages"), true).toBool();
    m_maxItems = qBound(1000, settings.value(
        QStringLiteral("maxItems"), 25000).toInt(), 100000);
    m_maxContentPerFile = qBound(2000, settings.value(
        QStringLiteral("maxContentPerFile"), 12000).toInt(), 100000);
    m_maxTotalContentMb = qBound(1, settings.value(
        QStringLiteral("maxTotalContentMb"), 16).toInt(), 64);
    m_indexMode = qBound(0, settings.value(
        QStringLiteral("indexMode"), 0).toInt(), 2);
    m_indexIntervalMinutes = qBound(5, settings.value(
        QStringLiteral("indexIntervalMinutes"), 30).toInt(), 1440);
    const int storedThemeMode = qBound(0, settings.value(
        QStringLiteral("themeMode"), 1).toInt(), 6);
    m_customBaseSkin = qBound(0, settings.value(
        QStringLiteral("customBaseSkin"), 0).toInt(), 4);
    if (storedThemeMode == 3)
        m_themeMode = 3;
    else if (storedThemeMode == 2 ||
             (storedThemeMode == 6 && m_customBaseSkin == 1))
        m_themeMode = 2;
    else
        m_themeMode = 1;
    m_customOpacity = qBound(25, settings.value(
        QStringLiteral("customOpacity"), 100).toInt(), 100);
    m_customColorsEnabled = settings.value(
        QStringLiteral("customColorsEnabled"), false).toBool();
    m_customSurface = QColor(settings.value(
        QStringLiteral("customSurface")).toString());
    m_customCard = QColor(settings.value(
        QStringLiteral("customCard")).toString());
    m_customBorder = QColor(settings.value(
        QStringLiteral("customBorder")).toString());
    m_customText = QColor(settings.value(
        QStringLiteral("customText")).toString());
    m_customMuted = QColor(settings.value(
        QStringLiteral("customMuted")).toString());
    m_customAccent = QColor(settings.value(
        QStringLiteral("customAccent")).toString());
    m_customSearchBg = QColor(settings.value(
        QStringLiteral("customSearchBg")).toString());
    m_customSearchText = QColor(settings.value(
        QStringLiteral("customSearchText")).toString());
    m_customButtonBg = QColor(settings.value(
        QStringLiteral("customButtonBg")).toString());
    m_customButtonText = QColor(settings.value(
        QStringLiteral("customButtonText")).toString());
    m_customButtonHover = QColor(settings.value(
        QStringLiteral("customButtonHover")).toString());
    m_customMenuBg = QColor(settings.value(
        QStringLiteral("customMenuBg")).toString());
    m_customMenuText = QColor(settings.value(
        QStringLiteral("customMenuText")).toString());
    m_customMenuHover = QColor(settings.value(
        QStringLiteral("customMenuHover")).toString());
    m_customFontFamily = settings.value(
        QStringLiteral("customFontFamily")).toString();
    m_customFontSize = qBound(10, settings.value(
        QStringLiteral("customFontSize"), 13).toInt(), 18);
    m_customFontBold = settings.value(
        QStringLiteral("customFontBold"), false).toBool();
    m_resultDensity = qBound(kMinResultDensity, settings.value(
        QStringLiteral("resultDensity"), 0).toInt(), kMaxResultDensity);
    m_resultSortMode = qBound(0, settings.value(
        QStringLiteral("resultSortMode"), 0).toInt(), 2);
    m_autoStart = settings.value(QStringLiteral("autoStart"), false).toBool();
    m_alwaysOnTop = settings.value(
        QStringLiteral("alwaysOnTop"), false).toBool();
    m_defaultHidden = settings.value(
        QStringLiteral("defaultHidden"), true).toBool();
    m_knowledgeDirectory = settings.value(
        QStringLiteral("knowledgeDirectory"),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/ukui-fences/knowledge")).toString();
    m_knowledgeChunkSize = qBound(600, settings.value(
        QStringLiteral("knowledgeChunkSize"), 1200).toInt(), 3000);
    m_knowledgeOverlap = qBound(0, settings.value(
        QStringLiteral("knowledgeOverlap"), 120).toInt(),
        m_knowledgeChunkSize / 3);
    m_providerConfig = settings.value(QStringLiteral("providerConfig"),
                                      providerConfigPath()).toString();
    const QSize storedSize = settings.value(
        QStringLiteral("size"), QSize(kDefaultWidth, kDefaultHeight)).toSize();
    m_expandedSize = storedSize.expandedTo(minimumSize());
    if (!m_fenceEmbedded)
        resize(m_expandedSize);
    QPoint position = settings.value(QStringLiteral("position"),
                                     QPoint(-1, -1)).toPoint();
    settings.endGroup();
    if (m_densityDownButton)
        m_densityDownButton->setEnabled(m_resultDensity > kMinResultDensity);
    if (m_densityUpButton)
        m_densityUpButton->setEnabled(m_resultDensity < kMaxResultDensity);
    if (m_densityDownButton)
        m_densityDownButton->setToolTip(QStringLiteral("缩小文件与文件夹 · 当前第 %1/%2 档")
            .arg(m_resultDensity - kMinResultDensity + 1)
            .arg(kResultDensityLevels));
    if (m_densityUpButton)
        m_densityUpButton->setToolTip(QStringLiteral("放大文件与文件夹 · 当前第 %1/%2 档")
            .arg(m_resultDensity - kMinResultDensity + 1)
            .arg(kResultDensityLevels));
    if (m_sortButton) {
        static const char *labels[] = {
            "排序：按相关度⌄", "排序：按修改时间⌄", "排序：按大小⌄"};
        m_sortButton->setText(QString::fromUtf8(labels[m_resultSortMode]));
    }

    const QString testRoots = QString::fromUtf8(qgetenv("UKUI_FENCES_SMARTSPACE_ROOTS"));
    if (!testRoots.isEmpty())
        m_roots = testRoots.split(QDir::listSeparator(), QString::SkipEmptyParts);
    if (qEnvironmentVariableIsSet("UKUI_FENCES_SMARTSPACE_OCR"))
        m_ocrImages = qEnvironmentVariableIntValue("UKUI_FENCES_SMARTSPACE_OCR") != 0;

    if (!m_fenceEmbedded && (position.x() < 0 || position.y() < 0)) {
        const QSize area = parentWidget() ? parentWidget()->size() : QSize(1920, 1080);
        position = QPoint(48,
                          std::max(64, area.height() - height() - 100));
    }
    if (!m_fenceEmbedded) {
        move(boundedPosition(position));
        m_expandedPosition = pos();
    }
}

void SmartSpaceWidget::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("smartSpace"));
    settings.setValue(QStringLiteral("roots"), m_roots);
    settings.setValue(QStringLiteral("excludedFolders"), m_excludedFolders);
    settings.setValue(QStringLiteral("hiddenFolders"), m_hiddenFolders);
    settings.setValue(QStringLiteral("indexedExtensions"), m_indexedExtensions);
    settings.setValue(QStringLiteral("visibleExtensions"), m_visibleExtensions);
    settings.setValue(QStringLiteral("ocrImages"), m_ocrImages);
    settings.setValue(QStringLiteral("maxItems"), m_maxItems);
    settings.setValue(QStringLiteral("maxContentPerFile"), m_maxContentPerFile);
    settings.setValue(QStringLiteral("maxTotalContentMb"), m_maxTotalContentMb);
    settings.setValue(QStringLiteral("indexMode"), m_indexMode);
    settings.setValue(QStringLiteral("indexIntervalMinutes"),
                      m_indexIntervalMinutes);
    settings.setValue(QStringLiteral("themeMode"), m_themeMode);
    settings.setValue(QStringLiteral("customBaseSkin"), m_customBaseSkin);
    settings.setValue(QStringLiteral("customOpacity"), m_customOpacity);
    settings.setValue(QStringLiteral("customColorsEnabled"), m_customColorsEnabled);
    settings.setValue(QStringLiteral("customSurface"),
                      m_customSurface.isValid() ? m_customSurface.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customCard"),
                      m_customCard.isValid() ? m_customCard.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customBorder"),
                      m_customBorder.isValid() ? m_customBorder.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customText"),
                      m_customText.isValid() ? m_customText.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customMuted"),
                      m_customMuted.isValid() ? m_customMuted.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customAccent"),
                      m_customAccent.isValid() ? m_customAccent.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customSearchBg"),
                      m_customSearchBg.isValid() ? m_customSearchBg.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customSearchText"),
                      m_customSearchText.isValid() ? m_customSearchText.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customButtonBg"),
                      m_customButtonBg.isValid() ? m_customButtonBg.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customButtonText"),
                      m_customButtonText.isValid() ? m_customButtonText.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customButtonHover"),
                      m_customButtonHover.isValid() ? m_customButtonHover.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customMenuBg"),
                      m_customMenuBg.isValid() ? m_customMenuBg.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customMenuText"),
                      m_customMenuText.isValid() ? m_customMenuText.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customMenuHover"),
                      m_customMenuHover.isValid() ? m_customMenuHover.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("customFontFamily"), m_customFontFamily);
    settings.setValue(QStringLiteral("customFontSize"), m_customFontSize);
    settings.setValue(QStringLiteral("customFontBold"), m_customFontBold);
    settings.setValue(QStringLiteral("resultDensity"), m_resultDensity);
    settings.setValue(QStringLiteral("resultSortMode"), m_resultSortMode);
    settings.setValue(QStringLiteral("autoStart"), m_autoStart);
    settings.setValue(QStringLiteral("alwaysOnTop"), m_alwaysOnTop);
    settings.setValue(QStringLiteral("defaultHidden"), m_defaultHidden);
    settings.setValue(QStringLiteral("knowledgeDirectory"),
                      m_knowledgeDirectory);
    settings.setValue(QStringLiteral("knowledgeChunkSize"),
                      m_knowledgeChunkSize);
    settings.setValue(QStringLiteral("knowledgeOverlap"),
                      m_knowledgeOverlap);
    settings.setValue(QStringLiteral("providerConfig"), m_providerConfig);
    if (!m_fenceEmbedded) {
        settings.setValue(QStringLiteral("position"),
                          m_edgeHidden ? m_expandedPosition : pos());
        settings.setValue(QStringLiteral("size"),
                          m_edgeHidden ? m_expandedSize : size());
    }
    settings.endGroup();
    settings.sync();
}

bool SmartSpaceWidget::autoStartEnabled()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("smartSpace"));
    const bool enabled = settings.value(QStringLiteral("autoStart"), false).toBool();
    settings.endGroup();
    return enabled;
}

void SmartSpaceWidget::setAutoStart(bool enabled)
{
    if (m_autoStart == enabled)
        return;
    m_autoStart = enabled;
    saveSettings();
}

void SmartSpaceWidget::setAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop == enabled) {
        updateActionState();
        return;
    }
    m_alwaysOnTop = enabled;
    updateActionState();
    saveSettings();
    emit alwaysOnTopChanged(enabled);
    // The signal is connected directly to DesktopCanvas, so the window has
    // already been reparented/recreated when control returns here.
    updateRoundedMask();
    update();
}

void SmartSpaceWidget::setResultDensity(int density)
{
    const int bounded = qBound(kMinResultDensity, density, kMaxResultDensity);
    if (m_resultDensity == bounded)
        return;
    m_resultDensity = bounded;
    if (m_densityDownButton) {
        m_densityDownButton->setEnabled(m_resultDensity > kMinResultDensity);
        m_densityDownButton->setToolTip(
            QStringLiteral("缩小文件与文件夹 · 当前第 %1/%2 档")
                .arg(m_resultDensity - kMinResultDensity + 1)
                .arg(kResultDensityLevels));
    }
    if (m_densityUpButton) {
        m_densityUpButton->setEnabled(m_resultDensity < kMaxResultDensity);
        m_densityUpButton->setToolTip(
            QStringLiteral("放大文件与文件夹 · 当前第 %1/%2 档")
                .arg(m_resultDensity - kMinResultDensity + 1)
                .arg(kResultDensityLevels));
    }
    updateFolderDensity();
    updateResults(false);
    saveSettings();
}

void SmartSpaceWidget::recreateNativeSurface(bool translucent)
{
    // WA_TranslucentBackground affects the X11 visual chosen while the native
    // window is created.  Reparenting an already-created 24-bit child window
    // is insufficient: Qt otherwise keeps the old visual and the alpha edge
    // is discarded.  Destroy only native resources; QObject/widget state and
    // the index remain intact and children are recreated lazily on show().
    const bool wasVisible = isVisible();
    if (wasVisible)
        hide();
    destroy(true, true);
    const bool glassSurface = m_themeMode == 3;
    // Only top-level windows get an ARGB visual. Desktop children share the
    // canvas backing store and must preserve its pixels outside the material.
    setAttribute(Qt::WA_TranslucentBackground, translucent);
    setAttribute(Qt::WA_NoSystemBackground, translucent);
    setAttribute(Qt::WA_OpaquePaintEvent, !translucent && !glassSurface);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(!(translucent || glassSurface));
    if (m_themeMode == 3)
        refreshGlassBackdrop();
    updateRoundedMask();
    if (wasVisible)
        show();
}

void SmartSpaceWidget::scheduleGlassBackdropRefresh()
{
    if (m_themeMode != 3 || m_edgeHidden)
        return;
    if (!m_glassBackdropRefreshTimer.isActive())
        m_glassBackdropRefreshTimer.start(80);
}

void SmartSpaceWidget::refreshGlassBackdrop()
{
    if (m_themeMode != 3 || m_edgeHidden || width() < 2 || height() < 2)
        return;

    // Sample the canvas' final wallpaper cache directly. Hiding and grabbing
    // the X11 root can capture the previous compositor frame (or this window).
    DesktopCanvas *canvas = nullptr;
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        canvas = qobject_cast<DesktopCanvas *>(ancestor);
        if (canvas)
            break;
    }
    const bool wasVisible = !canvas && isVisible();
    if (wasVisible)
        hide();

    QPixmap source;
    if (canvas) {
        source = QPixmap::fromImage(canvas->wallpaperBackdrop(
            QRect(mapToGlobal(QPoint()), size()), devicePixelRatioF()));
    } else if (isWindow()) {
        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect area = frameGeometry();
            source = screen->grabWindow(0, area.x(), area.y(),
                                        area.width(), area.height());
        }
    } else if (parentWidget()) {
        const QPoint local = mapTo(parentWidget(), QPoint(0, 0));
        source = parentWidget()->grab(
            QRect(local, QSize(width(), height())));
    }

    m_glassBackdrop = {};
    if (!source.isNull()) {
        const QImage image = source.toImage().convertToFormat(
            QImage::Format_ARGB32_Premultiplied);
        const QSize small(qMax(1, image.width() / 8),
                          qMax(1, image.height() / 8));
        m_glassBackdrop = image.scaled(
            small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .scaled(image.size(), Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
    }

    if (wasVisible) {
        show();
        raise();
    }
    update();
}

QPoint SmartSpaceWidget::boundedPosition(const QPoint &position) const
{
    QRect bounds;
    if (isWindow()) {
        QScreen *screen = QGuiApplication::screenAt(
            QRect(position, size()).center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen)
            bounds = screen->availableGeometry();
    } else if (parentWidget()) {
        bounds = parentWidget()->rect();
    }
    if (!bounds.isValid())
        return position;

    int x = qBound(bounds.left(), position.x(),
                   std::max(bounds.left(), bounds.right() - width() + 1));
    int y = qBound(bounds.top(), position.y(),
                   std::max(bounds.top(), bounds.bottom() - height() + 1));
    constexpr int snapDistance = 32;
    if (qAbs(x - bounds.left()) <= snapDistance)
        x = bounds.left();
    if (qAbs(x + width() - bounds.right() - 1) <= snapDistance)
        x = bounds.right() - width() + 1;
    if (qAbs(y - bounds.top()) <= snapDistance)
        y = bounds.top();
    if (qAbs(y + height() - bounds.bottom() - 1) <= snapDistance)
        y = bounds.bottom() - height() + 1;
    return QPoint(x, y);
}

void SmartSpaceWidget::hideToNearestEdge()
{
    if (m_fenceEmbedded || m_edgeHidden)
        return;
    const QPoint railAnchorGlobal = m_closeButton
        ? m_closeButton->mapToGlobal(m_closeButton->rect().center())
        : mapToGlobal(rect().center());
    m_expandedPosition = pos();
    m_expandedSize = size();
    hidePreview();

    QRect bounds;
    if (isWindow()) {
        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen)
            bounds = screen->availableGeometry();
    } else if (parentWidget()) {
        bounds = parentWidget()->rect();
    }
    if (!bounds.isValid())
        return;

    const QRect current(pos(), size());
    const int distances[] = {
        qAbs(current.left() - bounds.left()),
        qAbs(bounds.right() - current.right()),
        qAbs(current.top() - bounds.top()),
        qAbs(bounds.bottom() - current.bottom())
    };
    m_edgeSide = 0;
    for (int side = 1; side < 4; ++side) {
        if (distances[side] < distances[m_edgeSide])
            m_edgeSide = side;
    }

    const QPoint railAnchor = isWindow() || !parentWidget()
        ? railAnchorGlobal : parentWidget()->mapFromGlobal(railAnchorGlobal);

    m_edgeHidden = true;
    m_glassPointerActive = false;
    m_glassPointerTimer.stop();
    m_actionRail->hide();
    m_contentContainer->hide();
    m_edgeRevealButton->show();
    m_edgeRevealButton->raise();
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    clearMask();
    setMinimumSize(1, 1);

    resize(QSize(52, 52));
    QPoint target;
    if (m_edgeSide == 0) {
        target = QPoint(bounds.left(),
                        qBound(bounds.top(), railAnchor.y() - height() / 2,
                               bounds.bottom() - height() + 1));
    } else if (m_edgeSide == 1) {
        target = QPoint(bounds.right() - width() + 1,
                        qBound(bounds.top(), railAnchor.y() - height() / 2,
                               bounds.bottom() - height() + 1));
    } else if (m_edgeSide == 2) {
        target = QPoint(qBound(bounds.left(), railAnchor.x() - width() / 2,
                               bounds.right() - width() + 1), bounds.top());
    } else {
        target = QPoint(qBound(bounds.left(), railAnchor.x() - width() / 2,
                               bounds.right() - width() + 1),
                        bounds.bottom() - height() + 1);
    }
    move(target);
    m_edgeRevealButton->setGeometry(rect());
    updateRoundedMask();
    saveSettings();
    update();
}

void SmartSpaceWidget::revealFromEdge()
{
    if (!m_edgeHidden)
        return;
    // Keep the hidden-state guard active while restoring child visibility and
    // the minimum size.  Qt emits intermediate resize events here; allowing
    // those events to persist geometry would replace the saved expanded size
    // with the 620x360 minimum and move the widget to the star tab's edge.
    const QSize restoredSize = m_expandedSize.expandedTo(QSize(620, 360));
    const QPoint restoredPosition = m_expandedPosition;
    m_edgeRevealButton->hide();
    m_actionRail->show();
    m_contentContainer->show();
    const bool glassSurface = m_themeMode == 3;
    setAttribute(Qt::WA_NoSystemBackground, glassSurface && isWindow());
    setAttribute(Qt::WA_OpaquePaintEvent, !glassSurface);
    setAutoFillBackground(!glassSurface);
    setMinimumSize(620, 360);
    resize(restoredSize);
    move(boundedPosition(restoredPosition));
    m_edgeHidden = false;
    scheduleGlassBackdropRefresh();
    if (m_themeMode == 3 && !m_fenceEmbedded)
        m_glassPointerTimer.start();
    m_expandedSize = size();
    m_expandedPosition = pos();
    updateRoundedMask();
    saveSettings();
    update();
}

void SmartSpaceWidget::updateRoundedMask()
{
    if (m_edgeHidden || m_fenceEmbedded || isWindow() || m_themeMode == 3) {
        // Top-level pinned mode uses an ARGB backing surface.  Keeping the
        // integer QRegion here would discard antialiased edge alpha. Glass
        // mode instead keeps its content inside the rounded material inset.
        clearMask();
        return;
    }
    QPainterPath path;
    if (m_themeMode == 3)
        path.addRoundedRect(QRectF(rect()).adjusted(5, 5, -5, -5), 19, 19);
    else
        path.addRoundedRect(rect(), 16, 16);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

QString SmartSpaceWidget::indexPath() const
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cache + QStringLiteral("/smart-space/index.json");
}

QString SmartSpaceWidget::uiStreamIndexPath() const
{
    return indexPath() + QStringLiteral(".ui.bin.gz");
}

QString SmartSpaceWidget::resumeIndexPath() const
{
    return indexPath() + QStringLiteral(".fast.resume.jsonl");
}

QString SmartSpaceWidget::ocrResumeIndexPath() const
{
    return indexPath() + QStringLiteral(".ocr.resume.jsonl");
}

QString SmartSpaceWidget::providerConfigPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/kyfences/smart-space-providers.json");
}

QString SmartSpaceWidget::indexerPath() const
{
    QStringList candidates;
#ifdef UKUI_FENCES_SMART_INDEXER_PATH
    candidates << QString::fromUtf8(UKUI_FENCES_SMART_INDEXER_PATH);
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << appDir + QStringLiteral("/../libexec/ukui-fences/smart_space_indexer.py")
               << QStringLiteral("/usr/libexec/ukui-fences/smart_space_indexer.py")
               << QStringLiteral("/usr/lib/ukui-fences/smart_space_indexer.py");
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return QString();
}

QString SmartSpaceWidget::knowledgeWorkerPath() const
{
    QStringList candidates;
#ifdef UKUI_FENCES_KNOWLEDGE_WORKER_PATH
    candidates << QString::fromUtf8(UKUI_FENCES_KNOWLEDGE_WORKER_PATH);
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates
        << appDir + QStringLiteral(
               "/../libexec/ukui-fences/smart_space_knowledge.py")
        << QStringLiteral(
               "/usr/libexec/ukui-fences/smart_space_knowledge.py")
        << QStringLiteral("/usr/lib/ukui-fences/smart_space_knowledge.py");
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return QString();
}

QString SmartSpaceWidget::knowledgeDbPath() const
{
    QString directory = m_knowledgeDirectory.trimmed();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation)
            + QStringLiteral("/ukui-fences/knowledge");
    }
    return QDir(directory).filePath(
        QStringLiteral("smart-space-knowledge.sqlite"));
}

QString SmartSpaceWidget::skillDirectoryPath() const
{
    QStringList candidates;
#ifdef UKUI_FENCES_INDEX_SKILL_PATH
    candidates << QString::fromUtf8(UKUI_FENCES_INDEX_SKILL_PATH);
#endif
    const QString home = QDir::homePath();
    candidates
        << QDir(home).filePath(
               QStringLiteral("应用/.agents/skills/ukui-fences-index-query"))
        << QDir(home).filePath(
               QStringLiteral(".agents/skills/ukui-fences-index-query"))
        << QDir(QCoreApplication::applicationDirPath()).filePath(
               QStringLiteral("../share/ukui-fences/skills/ukui-fences-index-query"));
    for (const QString &candidate : candidates) {
        const QString clean = QDir::cleanPath(candidate);
        if (QFileInfo(clean).isDir() &&
            QFileInfo(QDir(clean).filePath(QStringLiteral("SKILL.md"))).isFile())
            return clean;
    }
    return QString();
}

void SmartSpaceWidget::startKnowledgeTask(
        const QString &task, const QStringList &arguments)
{
    if (m_knowledgeProcess) {
        m_statusLabel->setText(QStringLiteral(
            "知识库任务正在运行 · 可从管理菜单暂停"));
        return;
    }
    const QString worker = knowledgeWorkerPath();
    if (worker.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("找不到本地知识库后台程序"));
        return;
    }
    m_knowledgeOutputBuffer.clear();
    m_knowledgeErrorBuffer.clear();
    m_knowledgeLastObject = QJsonObject();
    m_knowledgeProcess = new QProcess(this);
    m_knowledgeProcess->setProperty("knowledgeTask", task);
    connect(m_knowledgeProcess, &QProcess::readyReadStandardOutput,
            this, &SmartSpaceWidget::handleKnowledgeOutput);
    connect(m_knowledgeProcess, &QProcess::readyReadStandardError,
            this, &SmartSpaceWidget::handleKnowledgeOutput);
    connect(m_knowledgeProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                finishKnowledgeTask(code, static_cast<int>(status));
            });
    QStringList command{worker, task};
    command.append(arguments);
    const QString python = QStandardPaths::findExecutable(
        QStringLiteral("python3"));
    const QString setsid = QStandardPaths::findExecutable(
        QStringLiteral("setsid"));
    QString program = python.isEmpty() ? QStringLiteral("python3") : python;
    QStringList processArguments = command;
    m_knowledgeUsesProcessGroup = false;
    if (!setsid.isEmpty()) {
        processArguments.prepend(program);
        program = setsid;
        m_knowledgeUsesProcessGroup = true;
    }
    m_knowledgeProcess->setProperty(
        "usesProcessGroup", m_knowledgeUsesProcessGroup);
    m_knowledgeProcess->start(program, processArguments);
    if (!m_knowledgeProcess->waitForStarted(1000)) {
        m_statusLabel->setText(QStringLiteral("知识库后台启动失败"));
        m_knowledgeProcess->deleteLater();
        m_knowledgeProcess = nullptr;
        m_knowledgeUsesProcessGroup = false;
        return;
    }
    if (task == QLatin1String("search") ||
        task == QLatin1String("stats")) {
        QPointer<QProcess> process = m_knowledgeProcess;
        QTimer::singleShot(30000, this, [this, process] {
            if (!process || m_knowledgeProcess != process ||
                process->state() == QProcess::NotRunning)
                return;
            process->setProperty("timedOut", true);
            terminateKnowledgeProcess(false);
            QTimer::singleShot(1200, this, [this, process] {
                if (!process || m_knowledgeProcess != process ||
                    process->state() == QProcess::NotRunning)
                    return;
                terminateKnowledgeProcess(true);
            });
        });
    }
}

void SmartSpaceWidget::terminateKnowledgeProcess(bool force)
{
    if (!m_knowledgeProcess ||
        m_knowledgeProcess->state() == QProcess::NotRunning)
        return;
    const bool usesProcessGroup = m_knowledgeProcess->property(
        "usesProcessGroup").toBool();
    const int signalNumber = force ? SIGKILL : SIGTERM;
    if (usesProcessGroup &&
        signalOwnedProcessGroup(m_knowledgeProcess, signalNumber))
        return;
    if (force)
        m_knowledgeProcess->kill();
    else
        m_knowledgeProcess->terminate();
}

void SmartSpaceWidget::startKnowledgeBuild()
{
    QDir().mkpath(QFileInfo(knowledgeDbPath()).absolutePath());
    startKnowledgeTask(QStringLiteral("build"), {
        QStringLiteral("--index-stream"), uiStreamIndexPath(),
        QStringLiteral("--index-json"), indexPath(),
        QStringLiteral("--db"), knowledgeDbPath(),
        QStringLiteral("--chunk-size"), QString::number(m_knowledgeChunkSize),
        QStringLiteral("--overlap"), QString::number(m_knowledgeOverlap)
    });
    if (m_knowledgeProcess)
        m_statusLabel->setText(QStringLiteral(
            "正在建立本地知识库 · 原始索引不会被修改"));
}

void SmartSpaceWidget::startKnowledgeSearch(const QString &query)
{
    m_knowledgeScores.clear();
    m_knowledgeSnippets.clear();
    if (query.trimmed().isEmpty() || !QFileInfo::exists(knowledgeDbPath()))
        return;
    startKnowledgeTask(QStringLiteral("search"), {
        QStringLiteral("--db"), knowledgeDbPath(),
        QStringLiteral("--query"), query,
        QStringLiteral("--limit"), QStringLiteral("120")
    });
}

void SmartSpaceWidget::handleKnowledgeOutput()
{
    if (!m_knowledgeProcess)
        return;
    m_knowledgeOutputBuffer += m_knowledgeProcess->readAllStandardOutput();
    const QByteArray errors = m_knowledgeProcess->readAllStandardError();
    if (!errors.isEmpty()) {
        const int room = qMax(
            0, kKnowledgeErrorLimit - m_knowledgeErrorBuffer.size());
        if (room > 0)
            m_knowledgeErrorBuffer += errors.left(room);
    }
    if (m_knowledgeOutputBuffer.size() > 4 * 1024 * 1024)
        m_knowledgeOutputBuffer =
            m_knowledgeOutputBuffer.right(2 * 1024 * 1024);
    int newline = -1;
    while ((newline = m_knowledgeOutputBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_knowledgeOutputBuffer.left(newline).trimmed();
        m_knowledgeOutputBuffer.remove(0, newline + 1);
        QJsonParseError error;
        const QJsonObject object =
            QJsonDocument::fromJson(line, &error).object();
        if (error.error != QJsonParseError::NoError || object.isEmpty())
            continue;
        m_knowledgeLastObject = object;
        const QString task = object.value(QStringLiteral("task")).toString();
        if (task == QLatin1String("build") && !object.value(
                QStringLiteral("done")).toBool()) {
            m_statusLabel->setText(QStringLiteral(
                "知识库建库 %1/%2 · 文档 %3 · 片段 %4")
                .arg(object.value(QStringLiteral("processed")).toInt())
                .arg(object.value(QStringLiteral("total")).toInt())
                .arg(object.value(QStringLiteral("documents")).toInt())
                .arg(object.value(QStringLiteral("chunks")).toInt()));
        }
    }
}

void SmartSpaceWidget::finishKnowledgeTask(int exitCode, int exitStatus)
{
    Q_UNUSED(exitStatus)
    if (!m_knowledgeProcess)
        return;
    handleKnowledgeOutput();
    const QString task =
        m_knowledgeProcess->property("knowledgeTask").toString();
    const bool userPaused =
        m_knowledgeProcess->property("userPaused").toBool();
    const bool timedOut =
        m_knowledgeProcess->property("timedOut").toBool();
    m_knowledgeProcess->deleteLater();
    m_knowledgeProcess = nullptr;
    m_knowledgeUsesProcessGroup = false;
    const QJsonObject result = m_knowledgeLastObject;
    const QString error = result.value(QStringLiteral("error")).toString();
    const QString processError = QString::fromUtf8(
        m_knowledgeErrorBuffer).trimmed().left(500);
    if (userPaused) {
        m_statusLabel->setText(QStringLiteral(
            "知识库任务已暂停 · 已完成内容已保存，下次自动续跑"));
        return;
    }
    if (timedOut) {
        m_statusLabel->setText(QStringLiteral(
            "知识库任务超时 · 已安全结束，可稍后重试"));
        return;
    }
    if (exitCode != 0 || exitStatus != QProcess::NormalExit ||
        !error.isEmpty()) {
        const QString detail = !error.isEmpty()
            ? error
            : !processError.isEmpty()
                ? processError
                : QStringLiteral("退出码 %1").arg(exitCode);
        m_statusLabel->setText(QStringLiteral("知识库任务失败 · %1")
            .arg(detail));
        return;
    }
    if (task == QLatin1String("search")) {
        const QJsonArray rows = result.value(QStringLiteral("results")).toArray();
        for (const QJsonValue &value : rows) {
            const QJsonObject row = value.toObject();
            const QString path = row.value(QStringLiteral("path")).toString();
            if (path.isEmpty())
                continue;
            m_knowledgeScores.insert(
                path, row.value(QStringLiteral("score")).toInt());
            m_knowledgeSnippets.insert(
                path, row.value(QStringLiteral("snippet")).toString());
        }
        updateResults(true);
        m_statusLabel->setText(QStringLiteral(
            "混合检索完成 · 本地知识库召回 %1 个候选")
            .arg(m_knowledgeScores.size()));
        return;
    }
    if (task == QLatin1String("build")) {
        m_statusLabel->setText(QStringLiteral(
            "知识库建库完成 · %1 个文档 · %2 个片段 · %3 MB")
            .arg(result.value(QStringLiteral("documents")).toInt())
            .arg(result.value(QStringLiteral("chunks")).toInt())
            .arg(result.value(QStringLiteral("dbBytes")).toDouble()
                 / 1024.0 / 1024.0, 0, 'f', 1));
    }
    else if (task == QLatin1String("stats")) {
        m_statusLabel->setText(QStringLiteral(
            "知识库：%1 文档 / %2 片段 / %3 MB")
            .arg(result.value(QStringLiteral("documents")).toInt())
            .arg(result.value(QStringLiteral("chunks")).toInt())
            .arg(result.value(QStringLiteral("dbBytes")).toDouble()
                 / 1024.0 / 1024.0, 0, 'f', 1));
    }
}

QStringList SmartSpaceWidget::defaultRoots() const
{
    QStringList roots;
    // The user's Documents tree can contain hundreds of thousands of files.
    // Start with the small desktop tree and let the user opt into precise
    // document folders from Settings.
    const QString path = QStandardPaths::writableLocation(
        QStandardPaths::DesktopLocation);
    if (!path.isEmpty() && QDir(path).exists())
        roots << path;
    return roots;
}

void SmartSpaceWidget::startIndexing()
{
    launchIndexer(false);
}

void SmartSpaceWidget::showSettings()
{
    showSettingsDialog();
}

void SmartSpaceWidget::startFastFullIndex()
{
    startIdleFullIndex();
}

void SmartSpaceWidget::hideFolder(const QString &path)
{
    hideFolderFromNavigation(path);
}

void SmartSpaceWidget::excludeFolder(const QString &path)
{
    excludeFolderFromIndex(path);
}

void SmartSpaceWidget::startIdleFullIndex()
{
    if (m_idleFullIndex && m_indexer &&
        m_indexer->state() != QProcess::NotRunning) {
        m_statusLabel->setText(QStringLiteral(
            "快速全量正在运行 · 如需中断请点击左侧“暂停”"));
        return;
    }
    if (m_indexer && m_indexer->state() != QProcess::NotRunning) {
        QMessageBox::information(this, QStringLiteral("索引任务正在运行"),
            QStringLiteral("请等待当前索引完成后再启动快速全量。"));
        return;
    }
    const bool canResume = QFileInfo::exists(resumeIndexPath());
    const QString operation = canResume
        ? QStringLiteral("将从上次快速全量断点继续。")
        : QStringLiteral("将建立所有已配置目录的快速全量索引。");
    if (qEnvironmentVariableIntValue("UKUI_FENCES_TEST_CONFIRM_IDLE") != 1 &&
        QMessageBox::question(this, QStringLiteral("快速全量索引"),
            operation + QStringLiteral(
                           "\n\n本次不限文件数，但不启动 Tesseract OCR。"
                           "可直接读取的 PDF/Office/WPS 正文会正常提取；"
                           "PDF 和图片会标记为“待检测/OCR”。"
                           "\n\n完成后可用左侧 OCR 按钮低优先级补全。是否开始？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    launchIndexer(true, false);
}

void SmartSpaceWidget::startOcrIndex()
{
    if (m_indexer && m_indexer->state() != QProcess::NotRunning) {
        QMessageBox::information(this, QStringLiteral("索引任务正在运行"),
            QStringLiteral("请等待当前任务完成，或先点击“暂停”。"));
        return;
    }
    if (!QFileInfo::exists(indexPath())) {
        m_statusLabel->setText(QStringLiteral("请先运行“快全量”建立待检测/OCR 队列"));
        return;
    }
    if (!m_ocrImages) {
        m_statusLabel->setText(QStringLiteral(
            "OCR 补全已在设置中关闭 · 请先启用 Tesseract OCR"));
        return;
    }
    const bool canResume = QFileInfo::exists(ocrResumeIndexPath());
    const QString detail = canResume
        ? QStringLiteral("将从上次 OCR 断点继续。")
        : QStringLiteral("先检测所有候选 PDF 的文本层，仅将图片和无文本层 PDF 交给 OCR。");
    if (qEnvironmentVariableIntValue("UKUI_FENCES_TEST_CONFIRM_OCR") != 1 &&
        QMessageBox::question(this, QStringLiteral("OCR 检测与补全"),
            detail + QStringLiteral(
                "\n\nPDF 会逐页识别，任务使用最低 CPU/I/O 优先级，"
                "可随时暂停，已完成文件会保留在断点中。是否开始？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    launchIndexer(false, true);
}

void SmartSpaceWidget::stopIdleFullIndex()
{
    if (!m_indexer || m_indexer->state() == QProcess::NotRunning)
        return;
    m_idleIndexCancelled = true;
    m_statusLabel->setText(QStringLiteral("正在中断索引并保留断点…"));
    if (m_indexProgress)
        m_indexProgress->setToolTip(QStringLiteral("正在安全中断；已完成的文件会用于下次续扫"));
    if (!m_idleUsesProcessGroup ||
        !signalOwnedProcessGroup(m_indexer, SIGTERM))
        m_indexer->terminate();
    const qint64 pid = m_indexer->processId();
    const bool processGroup = m_idleUsesProcessGroup;
    QTimer::singleShot(1500, this, [this, pid, processGroup] {
        if (!m_indexer || m_indexer->state() == QProcess::NotRunning)
            return;
        // QProcess is reused for later index jobs. An old pause timer must
        // never terminate a new job that happened to start meanwhile.
        if (m_indexer->processId() != pid)
            return;
        if (!processGroup ||
            !signalOwnedProcessGroup(m_indexer, SIGKILL))
            m_indexer->kill();
    });
}

void SmartSpaceWidget::launchIndexer(bool fastFull, bool ocrBackfill)
{
    if (!m_indexer)
        return;
    if (m_indexer->state() != QProcess::NotRunning) {
        if (!m_idleFullIndex && !m_ocrBackfill)
            m_reindexPending = true;
        return;
    }
    m_reindexPending = false;
    const QString script = indexerPath();
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (script.isEmpty() || python.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("索引器不可用：未找到 Python3 或索引脚本"));
        return;
    }

    QJsonArray rootArray;
    for (const QString &root : m_roots) {
        if (QDir(root).exists())
            rootArray.append(root);
    }
    if (rootArray.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请先配置至少一个索引目录"));
        return;
    }

    QDir().mkpath(QFileInfo(indexPath()).absolutePath());
    QStringList arguments;
    arguments << script
              << QStringLiteral("--roots-json")
              << QString::fromUtf8(QJsonDocument(rootArray).toJson(QJsonDocument::Compact))
              << QStringLiteral("--exclude-roots-json")
              << QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(
                     m_excludedFolders)).toJson(QJsonDocument::Compact))
              << QStringLiteral("--include-extensions-json")
              << QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(
                     m_indexedExtensions)).toJson(QJsonDocument::Compact))
              << QStringLiteral("--output") << indexPath();
    if (fastFull) {
        arguments << QStringLiteral("--fast-full")
                  << QStringLiteral("--resume-state") << resumeIndexPath();
    } else if (ocrBackfill) {
        arguments << QStringLiteral("--ocr-pending")
                  << QStringLiteral("--ocr-images")
                  << QStringLiteral("--ocr-pdf-pages") << QStringLiteral("0")
                  << QStringLiteral("--resume-state") << ocrResumeIndexPath();
    }
    arguments << QStringLiteral("--no-tags");
    arguments << QStringLiteral("--max-items")
              // All modes must preserve the complete snapshot.  A bounded
              // incremental pass previously replaced a 93k full index with
              // its first 25k entries and made completed OCR appear lost.
              << QStringLiteral("0")
              << QStringLiteral("--max-content")
              << QString::number(ocrBackfill ? qMin(m_maxContentPerFile, 4000)
                              : fastFull ? qMax(m_maxContentPerFile, 12000)
                                         : m_maxContentPerFile)
              << QStringLiteral("--max-total-content")
              << QString::number((ocrBackfill ? qMax(m_maxTotalContentMb, 256)
                              : fastFull ? qMax(m_maxTotalContentMb, 32)
                                         : qMax(m_maxTotalContentMb, 64)) *
                                 1024 * 1024);
    if (!m_providerConfig.trimmed().isEmpty() && QFileInfo::exists(m_providerConfig))
        arguments << QStringLiteral("--provider-config") << m_providerConfig;

    m_indexBusy = true;
    m_idleFullIndex = fastFull;
    m_ocrBackfill = ocrBackfill;
    m_idleIndexCancelled = false;
    m_idleUsesProcessGroup = false;
    m_indexOutputBuffer.clear();
    m_indexProcessed = 0;
    m_indexTotal = 0;
    if (m_idleIndexButton) {
        m_idleIndexButton->setText(QStringLiteral("快全量"));
        m_idleIndexButton->setEnabled(false);
    }
    if (m_refreshButton)
        m_refreshButton->setEnabled(false);
    if (m_ocrIndexButton)
        m_ocrIndexButton->setEnabled(false);
    if (m_pauseIndexButton)
        m_pauseIndexButton->setEnabled(true);
    m_statusLabel->setText(fastFull
        ? QStringLiteral("快速全量已启动 · 文件数不限 · 不启动 OCR")
        : ocrBackfill
            ? QStringLiteral("正在低优先级检测 PDF 文本层…")
            : QStringLiteral("正在快速增量索引…"));
    if (m_indexProgress)
    {
        m_indexProgress->setRange(0, 0);
        m_indexProgress->setValue(0);
        m_indexProgress->setFormat(QStringLiteral("正在统计待索引文件…"));
        m_indexProgress->setToolTip(QStringLiteral("正在统计待索引文件…"));
        m_indexProgress->show();
    }

    QString program = python;
    QStringList processArguments = arguments;
    if (fastFull || ocrBackfill) {
        QStringList wrapped;
        const QString ionice = QStandardPaths::findExecutable(QStringLiteral("ionice"));
        if (!ionice.isEmpty())
            wrapped << ionice << QStringLiteral("-c") << QStringLiteral("3");
        wrapped << python;
        wrapped.append(arguments);
        const QString setsid = QStandardPaths::findExecutable(QStringLiteral("setsid"));
        if (!setsid.isEmpty()) {
            program = setsid;
            processArguments = wrapped;
            m_idleUsesProcessGroup = true;
        } else if (!ionice.isEmpty()) {
            program = ionice;
            processArguments.clear();
            processArguments << QStringLiteral("-c") << QStringLiteral("3") << python;
            processArguments.append(arguments);
        }
    }
    m_indexer->start(program, processArguments);
}

void SmartSpaceWidget::handleIndexerOutput()
{
    m_indexOutputBuffer.append(m_indexer->readAllStandardOutput());
    m_indexer->readAllStandardError();
    int newline = -1;
    while ((newline = m_indexOutputBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_indexOutputBuffer.left(newline);
        m_indexOutputBuffer.remove(0, newline + 1);
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.value(QStringLiteral("phase")).toString() ==
            QLatin1String("counting")) {
            if (m_indexProgress) {
                m_indexProgress->setRange(0, 0);
                m_indexProgress->setFormat(QStringLiteral("正在统计待索引文件…"));
                m_indexProgress->setToolTip(QStringLiteral("正在统计待索引文件…"));
            }
            continue;
        }
        if (object.contains(QStringLiteral("resuming"))) {
            const int saved = object.value(QStringLiteral("resuming")).toInt();
            const int preflight = object.value(
                QStringLiteral("resumePreflight")).toInt();
            const int ocrCompleted = object.value(
                QStringLiteral("resumeOcrCompleted")).toInt();
            m_statusLabel->setText(m_ocrBackfill
                ? QStringLiteral("OCR 断点续扫 · 已完成 %1 项 · 已检测 %2 项 · 复用 %3 条记录")
                    .arg(ocrCompleted).arg(preflight).arg(saved)
                : QStringLiteral("发现索引断点 · 将复用 %1 个已完成项后继续")
                    .arg(saved));
            continue;
        }
        if (object.contains(QStringLiteral("progress"))) {
            const QString phase = object.value(QStringLiteral("phase")).toString();
            const int page = object.value(QStringLiteral("page")).toInt();
            const int pages = object.value(QStringLiteral("pages")).toInt();
            m_indexProcessed = object.value(QStringLiteral("progress")).toInt();
            if (object.contains(QStringLiteral("total")))
                m_indexTotal = object.value(QStringLiteral("total")).toInt();
            if (m_indexProgress && m_indexTotal > 0) {
                m_indexProgress->setRange(0, m_indexTotal);
                m_indexProgress->setValue(qMin(m_indexProcessed, m_indexTotal));
                if (phase == QLatin1String("ocr-preflight"))
                    m_indexProgress->setFormat(QStringLiteral("检测文本层   %p%   %v / %m"));
                else if (phase == QLatin1String("ocr-pages") && pages > 0)
                    m_indexProgress->setFormat(QStringLiteral(
                        "OCR %1 / %2 项   PDF %3 / %4 页")
                        .arg(m_indexProcessed).arg(m_indexTotal)
                        .arg(page).arg(pages));
                else
                    m_indexProgress->setFormat(m_ocrBackfill
                        ? QStringLiteral("OCR 总进度   %p%   %v / %m 项")
                        : QStringLiteral("%p%   %v / %m 项"));
                const int percent = qBound(0,
                    qRound(m_indexProcessed * 100.0 / m_indexTotal), 100);
                const QString currentPath = object.value(
                    QStringLiteral("path")).toString();
                const QString operation = phase == QLatin1String("ocr-preflight")
                    ? QStringLiteral("检测 PDF 文本层")
                    : phase == QLatin1String("ocr-pages")
                        ? QStringLiteral("OCR 页进度 %1 / %2").arg(page).arg(pages)
                        : QStringLiteral("索引进度 %1%").arg(percent);
                m_indexProgress->setToolTip(QStringLiteral(
                    "%1 · %2 / %3%4")
                    .arg(operation).arg(m_indexProcessed).arg(m_indexTotal)
                    .arg(currentPath.isEmpty() ? QString()
                        : QStringLiteral("\n当前文件：%1").arg(currentPath)));
                if (m_indexProgress->underMouse()) {
                    LiquidPopup::showText(
                        m_indexProgress->mapToGlobal(
                            QPoint(m_indexProgress->width() / 2,
                                   m_indexProgress->height() + 4)),
                        m_indexProgress->toolTip(), m_indexProgress);
                }
            }
            if (m_indexProcessed == 0 && phase != QLatin1String("ocr-running") &&
                phase != QLatin1String("ocr-pages"))
                continue;
            m_statusLabel->setText(m_ocrBackfill
                ? (phase == QLatin1String("ocr-preflight")
                    ? QStringLiteral("正在检测 PDF 文本层 · %1 / %2 · 此阶段不运行 OCR")
                        .arg(m_indexProcessed).arg(m_indexTotal)
                    : phase == QLatin1String("ocr-pages")
                        ? QStringLiteral("OCR 图片型 PDF · 第 %1 / %2 页 · 文件 %3 / %4")
                            .arg(page).arg(pages)
                            .arg(m_indexProcessed + 1).arg(m_indexTotal)
                        : QStringLiteral("OCR 图片/扫描 PDF · %1 / %2 项")
                            .arg(m_indexProcessed).arg(m_indexTotal))
                : m_idleFullIndex
                    ? QStringLiteral("快速全量 · %1 / %2 项 · 未启动 OCR")
                        .arg(m_indexProcessed).arg(m_indexTotal)
                    : QStringLiteral("快速增量…%1 / %2 项")
                        .arg(m_indexProcessed).arg(m_indexTotal));
        }
    }
    // A valid progress record is one bounded JSON line.  If a broken helper
    // writes an unterminated stream, never let that partial line grow for the
    // entire lifetime of the desktop process.
    constexpr int kMaxPartialIndexerLine = 1024 * 1024;
    if (m_indexOutputBuffer.size() > kMaxPartialIndexerLine) {
        m_indexOutputBuffer.clear();
        m_statusLabel->setText(QStringLiteral(
            "索引器返回了异常长的状态记录 · 已丢弃该条，索引任务继续"));
    }
}

void SmartSpaceWidget::finishIndexing(int exitCode, int exitStatus)
{
    const bool wasIdleFull = m_idleFullIndex;
    const bool wasOcrBackfill = m_ocrBackfill;
    const bool wasCancelled = m_idleIndexCancelled;
    m_indexBusy = false;
    m_idleFullIndex = false;
    m_ocrBackfill = false;
    m_idleUsesProcessGroup = false;
    if (m_indexProgress)
        m_indexProgress->hide();
    if (m_idleIndexButton) {
        m_idleIndexButton->setText(QStringLiteral("快全量"));
        m_idleIndexButton->setToolTip(QStringLiteral(
            "快速全量：文件数不限，不启动 OCR，待检测/OCR 文件单独标记"));
        m_idleIndexButton->setEnabled(true);
    }
    if (m_refreshButton)
        m_refreshButton->setEnabled(true);
    if (m_ocrIndexButton)
        m_ocrIndexButton->setEnabled(true);
    if (m_pauseIndexButton)
        m_pauseIndexButton->setEnabled(false);
    if (exitStatus == static_cast<int>(QProcess::NormalExit) &&
        exitCode == 0 && QFileInfo::exists(indexPath())) {
        const bool loaded = loadIndex();
        if (!loaded) {
            m_idleIndexCancelled = false;
            return;
        }
        const int pendingOcr = std::count_if(
            m_entries.cbegin(), m_entries.cend(),
            [](const SmartSpaceEntry &entry) {
                return entry.ocrStatus == QLatin1String("pending");
            });
        if (wasOcrBackfill)
            m_statusLabel->setText(QStringLiteral(
                "OCR 补全完成 · 索引共 %1 项 · 剩余待 OCR %2 项")
                .arg(m_entries.size()).arg(pendingOcr));
        else if (wasIdleFull)
            m_statusLabel->setText(QStringLiteral(
                "快速全量完成 · 已索引 %1 项 · 待检测/OCR %2 项")
                .arg(m_entries.size()).arg(pendingOcr));
    } else if ((wasIdleFull || wasOcrBackfill) && wasCancelled) {
        m_statusLabel->setText(QStringLiteral(
            "%1已暂停 · 正式索引保持不变 · 下次自动续扫")
            .arg(wasOcrBackfill ? QStringLiteral("OCR")
                                : QStringLiteral("快速全量")));
    } else if (!wasIdleFull && !wasOcrBackfill && wasCancelled) {
        m_statusLabel->setText(QStringLiteral(
            "增量索引已暂停 · 现有索引保持不变"));
    } else {
        m_statusLabel->setText(QStringLiteral("索引失败，请检查目录权限或索引器日志"));
    }
    m_idleIndexCancelled = false;
    if (!wasIdleFull && !wasOcrBackfill && m_reindexPending && m_indexMode != 0)
        QTimer::singleShot(100, this, &SmartSpaceWidget::startIndexing);
}

bool SmartSpaceWidget::loadIndex()
{
    QJsonObject root;
    QVector<SmartSpaceEntry> loaded;
    auto appendEntry = [this, &loaded](const QJsonObject &object) {
        SmartSpaceEntry entry;
        entry.path = normalizedPath(object.value(QStringLiteral("path")).toString());
        entry.root = normalizedPath(object.value(QStringLiteral("root")).toString());
        entry.parentPath = normalizedPath(object.value(QStringLiteral("parentPath")).toString());
        if (entry.parentPath.isEmpty())
            entry.parentPath = normalizedPath(QFileInfo(entry.path).absolutePath());
        entry.name = object.value(QStringLiteral("name")).toString();
        if (entry.name.isEmpty())
            entry.name = QFileInfo(entry.path).fileName();
        entry.suffix = object.value(QStringLiteral("suffix")).toString();
        if (entry.suffix.isEmpty() && !object.value(QStringLiteral("isDir")).toBool())
            entry.suffix = QFileInfo(entry.path).suffix().toLower();
        entry.category = object.value(QStringLiteral("category")).toString();
        entry.content = object.value(QStringLiteral("content")).toString();
        entry.extractor = object.value(QStringLiteral("extractor")).toString();
        entry.ocrStatus = object.value(QStringLiteral("ocrStatus")).toString();
        entry.ocrReason = object.value(QStringLiteral("ocrReason")).toString();
        entry.isDir = object.value(QStringLiteral("isDir")).toBool();
        entry.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        entry.modified = QDateTime::fromString(
            object.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
        if (!entry.path.isEmpty() && !pathExcluded(entry.path))
            loaded.append(entry);
    };

    bool loadedFromStream = false;
    QString streamFailure;
    const QByteArray streamPath = QFile::encodeName(uiStreamIndexPath());
    const QFileInfo streamInfo(uiStreamIndexPath());
    const QFileInfo canonicalInfo(indexPath());
    const bool streamIsCurrent = streamInfo.exists() &&
        (!canonicalInfo.exists() ||
         streamInfo.lastModified() >= canonicalInfo.lastModified());
    if (streamInfo.exists() && !streamIsCurrent)
        streamFailure = QStringLiteral("流式索引旧于主索引");
    if (streamIsCurrent) {
        gzFile archive = gzopen(streamPath.constData(), "rb");
        if (!archive) {
            streamFailure = QStringLiteral("无法打开流式索引");
        } else {
            QByteArray decompressedBuffer(256 * 1024, '\0');
            int bufferOffset = 0;
            int bufferSize = 0;
            auto readExact = [archive, &decompressedBuffer, &bufferOffset,
                              &bufferSize](void *destination, int bytes) {
                char *cursor = static_cast<char *>(destination);
                int remaining = bytes;
                while (remaining > 0) {
                    if (bufferOffset >= bufferSize) {
                        bufferSize = gzread(archive, decompressedBuffer.data(),
                                            decompressedBuffer.size());
                        bufferOffset = 0;
                        if (bufferSize <= 0)
                            return false;
                    }
                    const int count = qMin(remaining,
                                           bufferSize - bufferOffset);
                    std::memcpy(cursor,
                                decompressedBuffer.constData() + bufferOffset,
                                static_cast<size_t>(count));
                    cursor += count;
                    bufferOffset += count;
                    remaining -= count;
                }
                return true;
            };
            auto readU32 = [&readExact](quint32 &value) {
                quint32 encoded = 0;
                if (!readExact(&encoded, sizeof(encoded)))
                    return false;
                value = qFromLittleEndian(encoded);
                return true;
            };
            auto readI64 = [&readExact](qint64 &value) {
                qint64 encoded = 0;
                if (!readExact(&encoded, sizeof(encoded)))
                    return false;
                value = qFromLittleEndian(encoded);
                return true;
            };
            auto readString = [&readExact, &readU32](QString &value) {
                quint32 length = 0;
                if (!readU32(length) || length > 64U * 1024U * 1024U)
                    return false;
                QByteArray encoded;
                encoded.resize(static_cast<int>(length));
                if (length > 0 && !readExact(encoded.data(), encoded.size()))
                    return false;
                value = QString::fromUtf8(encoded);
                return true;
            };

            char magic[8] = {};
            bool streamOk = readExact(magic, sizeof(magic)) &&
                std::memcmp(magic, "UKFIDX1\n", sizeof(magic)) == 0;
            quint32 streamVersion = 0;
            quint32 metadataLength = 0;
            streamOk = streamOk && readU32(streamVersion) &&
                streamVersion == 1 && readU32(metadataLength) &&
                metadataLength <= 4U * 1024U * 1024U;
            QByteArray metadata;
            if (streamOk) {
                metadata.resize(static_cast<int>(metadataLength));
                streamOk = metadataLength == 0 ||
                    readExact(metadata.data(), metadata.size());
            }
            int expectedItems = -1;
            int parsedItems = 0;
            if (streamOk) {
                QJsonParseError metaError;
                const QJsonDocument metaDocument = QJsonDocument::fromJson(
                    metadata, &metaError);
                root = metaDocument.object();
                quint32 encodedItems = 0;
                streamOk = readU32(encodedItems);
                expectedItems = static_cast<int>(encodedItems);
                streamOk = metaError.error == QJsonParseError::NoError &&
                    metaDocument.isObject() && !root.isEmpty() && streamOk &&
                    expectedItems >= 0 &&
                    root.value(QStringLiteral("uiStreamItems")).toInt(-1) ==
                        expectedItems;
                if (!streamOk)
                    streamFailure = QStringLiteral("流式索引元数据无效");
            }
            if (streamOk)
                loaded.reserve(expectedItems);
            while (streamOk && parsedItems < expectedItems) {
                quint8 flags = 0;
                qint64 size = 0;
                qint64 modifiedMs = -1;
                SmartSpaceEntry entry;
                streamOk = readExact(&flags, sizeof(flags)) &&
                    readI64(size) && readI64(modifiedMs) &&
                    readString(entry.path) && readString(entry.root) &&
                    readString(entry.name) && readString(entry.category) &&
                    readString(entry.content) && readString(entry.extractor) &&
                    readString(entry.ocrStatus) && readString(entry.ocrReason);
                if (!streamOk) {
                    streamOk = false;
                    streamFailure = QStringLiteral("流式索引条目无效");
                    break;
                }
                ++parsedItems;
                entry.path = normalizedPath(entry.path);
                entry.root = normalizedPath(entry.root);
                entry.parentPath = normalizedPath(
                    QFileInfo(entry.path).absolutePath());
                if (entry.name.isEmpty())
                    entry.name = QFileInfo(entry.path).fileName();
                entry.isDir = (flags & 1U) != 0;
                if (!entry.isDir)
                    entry.suffix = QFileInfo(entry.path).suffix().toLower();
                entry.size = size;
                if (modifiedMs >= 0)
                    entry.modified = QDateTime::fromMSecsSinceEpoch(
                        modifiedMs, Qt::UTC);
                if (!entry.path.isEmpty() && !pathExcluded(entry.path))
                    loaded.append(entry);
            }
            int zlibError = Z_OK;
            gzerror(archive, &zlibError);
            if (zlibError != Z_OK && zlibError != Z_STREAM_END) {
                streamOk = false;
                streamFailure = QStringLiteral("流式索引压缩数据损坏");
            }
            if (parsedItems != expectedItems) {
                streamOk = false;
                streamFailure = QStringLiteral("流式索引条目数不完整");
            }
            if (gzclose(archive) != Z_OK) {
                streamOk = false;
                streamFailure = QStringLiteral("流式索引解压失败");
            }
            loadedFromStream = streamOk;
            if (!streamOk) {
                root = QJsonObject();
                loaded.clear();
            }
        }
    }

    if (!loadedFromStream) {
        QFile file(indexPath());
        if (!file.open(QIODevice::ReadOnly)) {
            m_statusLabel->setText(QStringLiteral("无法读取索引文件，请检查文件权限"));
            return false;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            const QString detail = streamFailure.isEmpty()
                ? error.errorString()
                : QStringLiteral("%1；%2").arg(error.errorString(), streamFailure);
            m_statusLabel->setText(QStringLiteral(
                "索引加载失败（%1）· 索引文件保持不变")
                .arg(detail));
            return false;
        }
        root = document.object();
        const QJsonArray items = root.value(QStringLiteral("items")).toArray();
        loaded.reserve(items.size());
        for (const QJsonValue &value : items)
            appendEntry(value.toObject());
    }

    m_entries.clear();
    m_entries.squeeze();
    m_entries = std::move(loaded);

    updateResults(true);
    rebuildWatches();

    const QString generated = root.value(QStringLiteral("generatedAt")).toString();
    const int errorCount = root.value(QStringLiteral("errors")).toArray().size();
    const QJsonObject stats = root.value(QStringLiteral("stats")).toObject();
    const QJsonObject capabilities = root.value(
        QStringLiteral("capabilities")).toObject();
    const int reused = stats.value(QStringLiteral("reused")).toInt();
    int pendingOcr = stats.value(QStringLiteral("ocrPending")).toInt(-1);
    // The published fast-full snapshot keeps the original OCR candidate count.
    // While an OCR backfill is paused, the append-only resume journal contains
    // the newer per-file states, so use its latest state for the status bar.
    QFile ocrResume(ocrResumeIndexPath());
    if (ocrResume.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QHash<QString, bool> pendingByPath;
        bool resumeMatchesSnapshot = false;
        while (!ocrResume.atEnd()) {
            const QByteArray line = ocrResume.readLine().trimmed();
            if (line.isEmpty())
                continue;
            const QJsonDocument checkpoint = QJsonDocument::fromJson(line);
            if (!checkpoint.isObject())
                continue;
            const QJsonObject record = checkpoint.object();
            if (record.value(QStringLiteral("kind")).toString() ==
                QLatin1String("header")) {
                const QJsonObject signature = record.value(
                    QStringLiteral("signature")).toObject();
                resumeMatchesSnapshot = signature.value(
                    QStringLiteral("snapshotGeneratedAt")).toString() ==
                    root.value(QStringLiteral("generatedAt")).toString();
                if (!resumeMatchesSnapshot)
                    break;
                continue;
            }
            if (!resumeMatchesSnapshot)
                continue;
            const QJsonObject item = record.value(QStringLiteral("item")).toObject();
            const QString path = item.value(QStringLiteral("path")).toString();
            if (path.isEmpty())
                continue;
            pendingByPath.insert(
                path, item.value(QStringLiteral("ocrStatus")).toString() ==
                          QLatin1String("pending"));
        }
        if (resumeMatchesSnapshot && !pendingByPath.isEmpty())
            pendingOcr = std::count(pendingByPath.cbegin(),
                                    pendingByPath.cend(), true);
    }
    if (pendingOcr < 0) {
        pendingOcr = 0;
        for (const SmartSpaceEntry &entry : m_entries) {
            if (entry.ocrStatus == QLatin1String("pending"))
                ++pendingOcr;
        }
    }
    const bool truncated = stats.value(QStringLiteral("truncated")).toInt() != 0;
    const bool fullRebuild = root.value(QStringLiteral("fullRebuild")).toBool();
    const QString storedIndexMode = root.value(
        QStringLiteral("indexMode")).toString();
    const int snapshotLimit = capabilities.value(
        QStringLiteral("maxItems")).toInt();
    const bool fullResumePending = !fullRebuild && QFileInfo::exists(
        resumeIndexPath());
    const QDateTime generatedTime = QDateTime::fromString(generated, Qt::ISODateWithMs);
    const QString displayTime = generatedTime.isValid()
        ? generatedTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : generated.left(19).replace(QLatin1Char('T'), QLatin1Char(' '));
    const QString snapshotKind = fullRebuild
        ? (storedIndexMode == QLatin1String("fast-full")
            ? QStringLiteral("快速全量已同步")
            : QStringLiteral("全量索引已同步"))
        : (fullResumePending
            ? QStringLiteral("全量待续扫 · 当前展示上次日常索引")
            : QStringLiteral("日常索引已同步"));
    const QString limitState = fullRebuild
        ? QStringLiteral(" · 全量无文件数上限")
        : (truncated
            ? QStringLiteral(" · 已达日常上限 %1").arg(
                  snapshotLimit > 0 ? snapshotLimit : m_maxItems)
            : QString());
    const QString ocrState = pendingOcr > 0
        ? QStringLiteral(" · 待检测/OCR %1 项").arg(pendingOcr)
        : QString();
    m_statusLabel->setText(QStringLiteral("%1 · %2 项%3%4%5%6 · %7")
        .arg(snapshotKind)
        .arg(m_entries.size())
        .arg(errorCount ? QStringLiteral(" · %1 项跳过").arg(errorCount) : QString())
        .arg(reused ? QStringLiteral(" · 复用 %1 项").arg(reused) : QString())
        .arg(limitState)
        .arg(ocrState)
        .arg(displayTime));
#if defined(__GLIBC__)
    // Both the old and new 100k-entry vectors coexist briefly during an
    // atomic refresh.  Return the retired arenas after temporary JSON/stream
    // objects leave this stack frame instead of retaining hundreds of MB.
    QTimer::singleShot(0, [] { malloc_trim(0); });
#endif
    return true;
}

void SmartSpaceWidget::rebuildWatches()
{
    if (!m_watcher)
        return;
    const QStringList oldDirs = m_watcher->directories();
    if (!oldDirs.isEmpty())
        m_watcher->removePaths(oldDirs);

    QStringList paths;
    QSet<QString> seenPaths;
    for (const QString &root : m_roots) {
        if (QDir(root).exists() && !pathExcluded(root) &&
            !seenPaths.contains(root)) {
            paths << root;
            seenPaths.insert(root);
        }
    }
    for (const SmartSpaceEntry &entry : m_entries) {
        if (!entry.isDir || pathExcluded(entry.path))
            continue;
        if (paths.size() >= kMaxWatchedDirectories)
            break;
        if (QDir(entry.path).exists() && !seenPaths.contains(entry.path)) {
            paths << entry.path;
            seenPaths.insert(entry.path);
        }
    }
    m_watcher->addPaths(paths);
}

void SmartSpaceWidget::scheduleReindex()
{
    if (m_indexMode != 1 || !m_automaticIndexingReady)
        return;
    if (m_indexBusy) {
        m_reindexPending = true;
        return;
    }
    m_reindexDebounce.start();
}

void SmartSpaceWidget::updateActionState()
{
    if (!m_pinButton)
        return;
    const QSignalBlocker blocker(m_pinButton);
    m_pinButton->setChecked(m_alwaysOnTop);
    m_pinButton->setText(m_alwaysOnTop
        ? QStringLiteral("已置顶") : QStringLiteral("置顶"));
    m_pinButton->setToolTip(m_alwaysOnTop
        ? QStringLiteral("取消置顶，返回桌面小组件层")
        : QStringLiteral("让智能空间悬浮在所有应用上方"));
}

bool SmartSpaceWidget::entryMatches(const SmartSpaceEntry &entry) const
{
    if (pathExcluded(entry.path) || (!entry.isDir && !extensionVisible(entry)))
        return false;
    if (!m_executedQuery.isEmpty()) {
        if (m_searchUsesRegex) {
            const bool matches = m_searchExpression.match(entry.name).hasMatch()
                || m_searchExpression.match(entry.path).hasMatch()
                || m_searchExpression.match(entry.content).hasMatch();
            if (!matches)
                return false;
        } else {
            for (const QString &term : m_searchTerms) {
                const bool matches = entry.name.contains(term, Qt::CaseInsensitive)
                    || entry.path.contains(term, Qt::CaseInsensitive)
                    || entry.content.contains(term, Qt::CaseInsensitive);
                if (!matches)
                    return false;
            }
        }
    }

    if (!categoryMatches(entry))
        return false;
    return true;
}

bool SmartSpaceWidget::fileMatchesCurrentResults(
    const SmartSpaceEntry &entry, bool respectScope) const
{
    if (entry.isDir)
        return false;
    const bool knowledgeMatch =
        !m_executedQuery.isEmpty() &&
        m_knowledgeScores.contains(entry.path);
    const bool visibleKnowledgeMatch = knowledgeMatch &&
        !pathExcluded(entry.path) && extensionVisible(entry) &&
        categoryMatches(entry);
    if (!entryMatches(entry) && !visibleKnowledgeMatch)
        return false;
    if (respectScope && !pathInsideScope(entry.path))
        return false;
    return true;
}

bool SmartSpaceWidget::commitSearchQuery(const QString &query, bool showError)
{
    const QString normalized = query.trimmed();
    const bool regexMode = normalized.startsWith(
        QStringLiteral("re:"), Qt::CaseInsensitive);
    QRegularExpression expression;
    QStringList terms;
    if (regexMode) {
        const QString pattern = normalized.mid(3).trimmed();
        expression = QRegularExpression(
            pattern, QRegularExpression::CaseInsensitiveOption |
                     QRegularExpression::UseUnicodePropertiesOption);
        if (pattern.isEmpty() || !expression.isValid()) {
            if (showError && m_statusLabel) {
                m_statusLabel->setText(pattern.isEmpty()
                    ? QStringLiteral("正则表达式为空，请在 re: 后输入规则")
                    : QStringLiteral("正则表达式无效：%1")
                        .arg(expression.errorString()));
            }
            return false;
        }
    } else if (!normalized.isEmpty()) {
        terms = normalized.split(QRegularExpression(QStringLiteral("\\s+")),
                                 QString::SkipEmptyParts);
    }
    m_executedQuery = normalized;
    m_searchUsesRegex = regexMode;
    m_searchExpression = expression;
    m_searchTerms = terms;
    return true;
}

void SmartSpaceWidget::executeSearch()
{
    if (!m_searchEdit || !commitSearchQuery(m_searchEdit->text(), true))
        return;
    m_knowledgeScores.clear();
    m_knowledgeSnippets.clear();
    updateResults(true);
    if (!m_searchUsesRegex && !m_executedQuery.isEmpty())
        startKnowledgeSearch(m_executedQuery);
}

bool SmartSpaceWidget::categoryMatches(const SmartSpaceEntry &entry) const
{
    return m_selectedCategories.isEmpty() ||
        m_selectedCategories.contains(entry.category);
}

QString SmartSpaceWidget::entryToolTip(const SmartSpaceEntry &entry) const
{
    const QString body = relevantSnippetHtml(entry, 3, 42, 86);
    const QString ocr = entry.ocrStatus == QLatin1String("pending")
        ? QStringLiteral("<br><span style='color:#d97706'><b>待检测/OCR</b> · "
                         "会先检测 PDF 文本层，必要时才运行 OCR</span>")
        : QString();
    return QStringLiteral(
        "<qt><div style='min-width:320px;max-width:460px'>"
        "<b>%1</b>%2<br><br>%3</div></qt>")
        .arg(entry.name.toHtmlEscaped(), ocr, body);
}

QString SmartSpaceWidget::relevantSnippetHtml(const SmartSpaceEntry &entry,
                                               int maxContexts,
                                               int before, int after) const
{
    const QString content = entry.content.simplified();
    QString snippetQuery = m_executedQuery;
    if (m_searchUsesRegex && snippetQuery.startsWith(
            QStringLiteral("re:"), Qt::CaseInsensitive))
        snippetQuery = snippetQuery.mid(3);
    const QStringList terms = localQueryTerms(snippetQuery);

    struct Hit { int start; int length; };
    QVector<Hit> hits;
    for (const QString &term : terms) {
        if (term.size() < 2)
            continue;
        int from = 0;
        while (from < content.size() && hits.size() < 96) {
            const int at = content.indexOf(term, from, Qt::CaseInsensitive);
            if (at < 0)
                break;
            hits.append({at, term.size()});
            from = at + qMax(1, term.size());
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &left, const Hit &right) {
        return left.start == right.start
            ? left.length > right.length : left.start < right.start;
    });

    auto highlighted = [this, &terms](const QString &value) {
        QString html;
        int offset = 0;
        while (offset < value.size()) {
            int next = -1;
            int length = 0;
            for (const QString &term : terms) {
                const int at = value.indexOf(term, offset, Qt::CaseInsensitive);
                if (at >= 0 && (next < 0 || at < next ||
                                (at == next && term.size() > length))) {
                    next = at;
                    length = term.size();
                }
            }
            if (next < 0) {
                html += value.mid(offset).toHtmlEscaped();
                break;
            }
            html += value.mid(offset, next - offset).toHtmlEscaped();
            html += QStringLiteral("<b style='color:%1'>%2</b>")
                .arg(m_accentColor.name(),
                     value.mid(next, length).toHtmlEscaped());
            offset = next + length;
        }
        return html;
    };

    QStringList snippets;
    int previousEnd = -1;
    for (const Hit &hit : hits) {
        const int start = qMax(0, hit.start - qMax(0, before));
        const int end = qMin(content.size(), hit.start + hit.length + qMax(0, after));
        if (start <= previousEnd)
            continue;
        QString excerpt = highlighted(content.mid(start, end - start));
        if (start > 0)
            excerpt.prepend(QStringLiteral("…"));
        if (end < content.size())
            excerpt.append(QStringLiteral("…"));
        snippets << excerpt;
        previousEnd = end;
        if (snippets.size() == qMax(1, maxContexts))
            break;
    }
    if (snippets.isEmpty() && !content.isEmpty()) {
        const int fallbackLength = qMax(80, before + after);
        QString excerpt = content.left(fallbackLength).toHtmlEscaped();
        if (content.size() > fallbackLength)
            excerpt.append(QStringLiteral("…"));
        snippets << excerpt;
    }

    return snippets.isEmpty()
        ? (entry.ocrStatus == QLatin1String("pending")
            ? QStringLiteral("待检测/OCR：已收录文件信息，PDF 将先检测文本层。")
            : QStringLiteral("当前索引没有可展示的正文片段。"))
        : snippets.join(QStringLiteral("<br><br>"));
}

bool SmartSpaceWidget::pathInsideScope(const QString &path) const
{
    return m_scopePath.isEmpty() || pathIsInside(path, m_scopePath);
}

bool SmartSpaceWidget::pathExcluded(const QString &path) const
{
    for (const QString &excluded : m_excludedFolders) {
        if (pathIsInside(path, excluded))
            return true;
    }
    return false;
}

bool SmartSpaceWidget::folderHidden(const QString &path) const
{
    const QString clean = normalizedPath(path);
    for (const QString &hidden : m_hiddenFolders) {
        if (clean == normalizedPath(hidden))
            return true;
    }
    return false;
}

bool SmartSpaceWidget::extensionVisible(const SmartSpaceEntry &entry) const
{
    return entry.isDir || formatAllowed(entry.suffix, m_visibleExtensions);
}

void SmartSpaceWidget::hideFolderFromNavigation(const QString &path)
{
    const QString clean = normalizedPath(path);
    if (clean.isEmpty() || m_hiddenFolders.contains(clean))
        return;
    m_hiddenFolders << clean;
    if (!m_scopePath.isEmpty() && pathIsInside(m_scopePath, clean))
        m_scopePath.clear();
    saveSettings();
    updateResults(true);
    QString displayName = QFileInfo(clean).fileName();
    if (displayName.isEmpty())
        displayName = clean;
    m_statusLabel->setText(QStringLiteral(
        "已从左侧隐藏“%1” · 文件仍可搜索 · 可在设置中恢复")
        .arg(displayName));
}

void SmartSpaceWidget::excludeFolderFromIndex(const QString &path)
{
    const QString clean = normalizedPath(path);
    if (clean.isEmpty() || pathExcluded(clean))
        return;
    if (qEnvironmentVariableIntValue("UKUI_FENCES_TEST_CONFIRM_EXCLUDE") != 1 &&
        QMessageBox::question(this, QStringLiteral("取消索引文件夹"),
            QStringLiteral("将从当前结果移除“%1”及其全部子项，后续增量、快速全量和 OCR 都会跳过。\n\n"
                           "文件本身不会删除，可在设置 → 索引范围中恢复。是否继续？")
                .arg(clean),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    m_excludedFolders << clean;
    m_hiddenFolders.removeAll(clean);
    if (m_indexer && m_indexer->state() != QProcess::NotRunning) {
        if (m_idleFullIndex || m_ocrBackfill)
            stopIdleFullIndex();
        else
            m_indexer->terminate();
    }
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
        [this](const SmartSpaceEntry &entry) { return pathExcluded(entry.path); }),
        m_entries.end());
    if (!m_scopePath.isEmpty() && pathIsInside(m_scopePath, clean))
        m_scopePath.clear();
    saveSettings();
    rebuildWatches();
    updateResults(true);
    m_statusLabel->setText(QStringLiteral(
        "已排除索引“%1” · 点击更新后写入正式快照 · 设置中可恢复")
        .arg(QFileInfo(clean).fileName()));
}

void SmartSpaceWidget::showResultSortMenu()
{
    if (!m_sortButton)
        return;
    QMenu menu(this);
    applyMenuTheme(&menu);
    struct SortOption { int mode; const char *label; const char *tip; };
    const SortOption options[] = {
        {0, "相关度", "文件名命中优先，再按正文和路径的匹配度排序"},
        {1, "修改时间", "最近修改的文件排在前面"},
        {2, "文件大小", "较大的文件排在前面"}
    };
    for (const SortOption &option : options) {
        QAction *action = menu.addAction(QStringLiteral("%1  %2")
            .arg(option.mode == m_resultSortMode
                    ? QStringLiteral("✔") : QStringLiteral("○"),
                 QString::fromUtf8(option.label)));
        action->setToolTip(QString::fromUtf8(option.tip));
        connect(action, &QAction::triggered, this, [this, option] {
            m_resultSortMode = option.mode;
            static const char *labels[] = {
                "排序：按相关度⌄", "排序：按修改时间⌄", "排序：按大小⌄"};
            m_sortButton->setText(QString::fromUtf8(labels[m_resultSortMode]));
            m_sortButton->setToolTip(QString::fromUtf8(option.tip));
            saveSettings();
            updateResults(false);
        });
    }
    LiquidPopup::execAt(menu,m_sortButton);
}

QVector<int> SmartSpaceWidget::matchingFileIndexes() const
{
    QVector<int> result;
    for (int i = 0; i < m_entries.size(); ++i) {
        const SmartSpaceEntry &entry = m_entries.at(i);
        if (fileMatchesCurrentResults(entry))
            result.append(i);
    }
    // An empty search has no meaningful relevance order.  Keeping snapshot
    // order avoids scoring and sorting tens of thousands of files at startup.
    if (m_resultSortMode == 0 && m_executedQuery.trimmed().isEmpty() &&
true)
        return result;

    const QString foldedQuery = m_executedQuery.trimmed().toCaseFolded();
    QStringList foldedTerms;
    foldedTerms.reserve(m_searchTerms.size());
    for (const QString &term : m_searchTerms)
        foldedTerms.append(term.toCaseFolded());
    auto nameMatchRank = [this, &foldedQuery,
                          &foldedTerms](const SmartSpaceEntry &entry) {
        if (m_searchUsesRegex)
            return m_searchExpression.match(entry.name).hasMatch() ? 3 : 0;
        const QString foldedName = entry.name.toCaseFolded();
        if (!foldedQuery.isEmpty() && foldedName.contains(foldedQuery))
            return 3;
        int hits = 0;
        for (const QString &term : foldedTerms)
            hits += foldedName.contains(term) ? 1 : 0;
        if (!foldedTerms.isEmpty() && hits == foldedTerms.size())
            return 2;
        return hits > 0 ? 1 : 0;
    };

    struct RelevanceMetric {
        int index = -1;
        int percent = -1;
        int nameRank = 0;
        int score = 0;
        int knowledgeScore = 0;
    };
    if (m_resultSortMode == 0) {
        QVector<RelevanceMetric> ranked;
        ranked.reserve(result.size());
        for (const int index : result) {
            const SmartSpaceEntry &entry = m_entries.at(index);
            RelevanceMetric metric;
            metric.index = index;
            metric.percent = relevancePercent(entry);
            metric.nameRank = nameMatchRank(entry);
            metric.score = localEntryScore(entry, m_executedQuery);
            metric.knowledgeScore = m_knowledgeScores.value(entry.path);
            ranked.append(metric);
        }
        std::stable_sort(ranked.begin(), ranked.end(),
            [this](const RelevanceMetric &left,
                   const RelevanceMetric &right) {
                // “相关度”与文件卡片显示的“匹配 xx%”必须是同一个
                // 概念。百分比较低的结果不得因隐藏的子分数排到前面；
                // 文件名和知识库分数只用于百分比相同的稳定排序。
                if (left.percent != right.percent)
                    return left.percent > right.percent;
                if (left.nameRank != right.nameRank)
                    return left.nameRank > right.nameRank;
                if (left.knowledgeScore != right.knowledgeScore)
                    return left.knowledgeScore > right.knowledgeScore;
                if (left.score != right.score)
                    return left.score > right.score;
                return QString::localeAwareCompare(
                    m_entries.at(left.index).name,
                    m_entries.at(right.index).name) < 0;
            });
        result.clear();
        result.reserve(ranked.size());
        for (const RelevanceMetric &metric : ranked)
            result.append(metric.index);
        return result;
    }

    std::stable_sort(result.begin(), result.end(), [this, &nameMatchRank](int a, int b) {
        const SmartSpaceEntry &left = m_entries.at(a);
        const SmartSpaceEntry &right = m_entries.at(b);
        if (m_resultSortMode == 1 && left.modified != right.modified)
            return left.modified > right.modified;
        if (m_resultSortMode == 2 && left.size != right.size)
            return left.size > right.size;
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    return result;
}

int SmartSpaceWidget::relevancePercent(const SmartSpaceEntry &entry) const
{
    const int knowledgePercent = m_knowledgeScores.value(entry.path, -1);
    const QString query = m_executedQuery.trimmed();
    if (query.isEmpty())
        return -1;
    if (m_searchUsesRegex)
        return m_searchExpression.match(entry.name).hasMatch() ? 96 : 72;
    const QString folded = query.toCaseFolded();
    if (entry.name.toCaseFolded().contains(folded))
        return 98;
    const int score = localEntryScore(entry, query);
    if (score <= 0)
        return knowledgePercent;
    int percent = 30 + (score * 69 / (score + 350));
    if (entry.content.toCaseFolded().contains(folded))
        percent = qMax(percent, 90);
    return qMax(knowledgePercent, qBound(35, percent, 97));
}

QWidget *SmartSpaceWidget::createFileCard(const SmartSpaceEntry &entry,
                                          QListWidgetItem *item)
{
    auto *card = new SmartFileCard([this, path = entry.path, item] {
        if (item)
            m_fileList->setCurrentItem(item);
        openPath(path);
    }, m_fileList);
    card->setObjectName(QStringLiteral("fileResultCard"));
    card->setAttribute(Qt::WA_Hover, true);
    const QString toolTip = entryToolTip(entry);
    auto *row = new QHBoxLayout(card);
    static const int iconExtents[] = {14, 16, 18, 20, 25, 30, 34, 38};
    static const int cardHeights[] = {24, 28, 33, 38, 50, 64, 78, 94};
    static const int previewExtents[] = {20, 22, 24, 26, 28, 32, 32, 32};
    const int densityIndex = qBound(
        0, m_resultDensity - kMinResultDensity, kResultDensityLevels - 1);
    const bool compact = m_resultDensity < 0;
    const bool ultraCompact = m_resultDensity <= -2;
    const int iconExtent = iconExtents[densityIndex];
    const int cardHeight = cardHeights[densityIndex];
    row->setContentsMargins(m_resultDensity <= -3 ? 4 : ultraCompact ? 6 : 9,
                            m_resultDensity <= -3 ? 0 : compact ? 2 : 5,
                            m_resultDensity <= -3 ? 4 : 8,
                            m_resultDensity <= -3 ? 0 : compact ? 2 : 5);
    row->setSpacing(m_resultDensity <= -3 ? 4 : compact ? 6 : 8);

    auto *icon = new QLabel(card);
    icon->setFixedSize(iconExtent + 4, iconExtent + 4);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(entryIcon(entry).pixmap(iconExtent, iconExtent));
    row->addWidget(icon, 0, Qt::AlignVCenter);

    auto *textContainer = new QWidget(card);
    textContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *texts = new QVBoxLayout(textContainer);
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    auto *name = new QLabel(entry.name, card);
    name->setProperty("cardName", true);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name->setTextInteractionFlags(Qt::NoTextInteraction);
    name->setMouseTracking(true);
    name->installEventFilter(this);
    name->setToolTip(toolTip);
    texts->addWidget(name);

    QStringList metadata;
    const QString size = formattedFileSize(entry.size);
    const QString modified = formattedModified(entry.modified);
    if (!size.isEmpty())
        metadata << size;
    if (!modified.isEmpty())
        metadata << modified;
    if (entry.ocrStatus == QLatin1String("pending"))
        metadata << QStringLiteral("待检测/OCR");
    auto *meta = new QLabel(metadata.join(QStringLiteral(" · ")), card);
    meta->setProperty("cardMeta", true);
    meta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    meta->setVisible(m_resultDensity >= 0);
    texts->addWidget(meta);

    auto *snippet = new QLabel(card);
    snippet->setProperty("cardSnippet", true);
    snippet->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    snippet->setTextFormat(Qt::RichText);
    snippet->setText(relevantSnippetHtml(entry, 1, 24, 70));
    snippet->setTextInteractionFlags(Qt::NoTextInteraction);
    snippet->setVisible(!ultraCompact);
    texts->addWidget(snippet);
    row->addWidget(textContainer, 1);

    const int matchPercent = relevancePercent(entry);
    if (matchPercent >= 0) {
        auto *match = new QLabel(
            ultraCompact
                ? QStringLiteral("%1%").arg(matchPercent)
                : QStringLiteral("匹配 %1%").arg(matchPercent),
            card);
        match->setProperty("matchScore", true);
        match->setProperty("matchLevel",
                           matchPercent >= 85 ? QStringLiteral("high")
                                              : QStringLiteral("medium"));
        match->setToolTip(QStringLiteral(
            "本地相关度 %1%：由文件名、路径和正文命中综合换算")
            .arg(matchPercent));
        row->addWidget(match, 0, Qt::AlignVCenter);
    }

    auto *preview = new QToolButton(card);
    preview->setText(compact
        ? QStringLiteral("◉")
        : QStringLiteral("◉ 预览"));
    preview->setToolTip(QStringLiteral("预览相关内容"));
    preview->setProperty("previewAction", true);
    preview->setToolButtonStyle(Qt::ToolButtonTextOnly);
    preview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const int previewExtent = previewExtents[densityIndex];
    if (compact)
        preview->setFixedSize(previewExtent, previewExtent);
    else
        preview->setMinimumHeight(previewExtent);
    connect(preview, &QToolButton::clicked, this,
            [this, path = entry.path] { showPreviewForPath(path); });
    row->addWidget(preview, 0, Qt::AlignVCenter);

    if (item)
        item->setSizeHint(QSize(0, cardHeight));
    return card;
}

const SmartSpaceEntry *SmartSpaceWidget::entryForPath(const QString &path) const
{
    for (const SmartSpaceEntry &entry : m_entries) {
        if (entry.path == path)
            return &entry;
    }
    return nullptr;
}

void SmartSpaceWidget::showPreviewForPath(const QString &path)
{
    const SmartSpaceEntry *entry = entryForPath(path);
    if (!entry || entry->isDir || !m_previewPanel)
        return;
    LiquidPopup::hideText();
    if (m_previewProcess && m_previewProcess->state() != QProcess::NotRunning) {
        m_previewProcess->kill();
        m_previewProcess->waitForFinished(120);
    }
    m_pendingPreviewImage.clear();
    m_previewPath = path;
    m_previewVisible = true;
    updateResponsiveLayout();
    m_previewTitle->setText(entry->name);
    QStringList metadata;
    metadata << categoryTitle(entry->category);
    const QString size = formattedFileSize(entry->size);
    const QString modified = formattedModified(entry->modified);
    if (!size.isEmpty())
        metadata << size;
    if (!modified.isEmpty())
        metadata << modified;
    m_previewMeta->setText(metadata.join(QStringLiteral(" · ")));
    m_previewOpenButton->setEnabled(QFileInfo::exists(entry->path));
    m_previewImage->clear();

    if (entry->category == QLatin1String("image") &&
        QFileInfo::exists(entry->path)) {
        showPreviewImage(entry->path);
    } else if (entry->category == QLatin1String("pdf") &&
               QFileInfo::exists(entry->path)) {
        startPdfPreview(*entry);
    } else {
        m_previewStack->setCurrentWidget(m_previewText);
        const QString query = m_executedQuery;
        if (!query.isEmpty()) {
            m_previewText->setHtml(QStringLiteral(
                "<h3>%1</h3><p>%2</p>")
                .arg(QStringLiteral("最相关的索引片段"),
                     relevantSnippetHtml(*entry, 3, 56, 120)));
        } else if (!entry->content.trimmed().isEmpty()) {
            QString body = entry->content.left(20000).toHtmlEscaped();
            body.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            if (entry->content.size() > 20000)
                body.append(QStringLiteral("<br>…"));
            m_previewText->setHtml(body);
        } else {
            m_previewText->setHtml(QStringLiteral(
                "<p>当前索引没有可预览的正文。</p>"
                "<p>可直接打开原文件。</p>"));
        }
    }
}

void SmartSpaceWidget::hidePreview()
{
    if (m_previewProcess && m_previewProcess->state() != QProcess::NotRunning) {
        m_previewProcess->kill();
        m_previewProcess->waitForFinished(120);
    }
    m_previewVisible = false;
    m_previewPath.clear();
    m_pendingPreviewImage.clear();
    if (m_previewImage)
        m_previewImage->clear();
    if (m_previewImage)
        m_previewImage->setMinimumSize(QSize(0, 0));
    if (m_previewPanel)
        m_previewPanel->hide();
}

void SmartSpaceWidget::showPreviewImage(const QString &imagePath)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid()) {
        const int previewWidth = m_previewPanel
            ? qMax(240, m_previewPanel->width() - 40) : 800;
        const QSize bounded = original.scaled(QSize(previewWidth, 1600),
                                              Qt::KeepAspectRatio);
        if (bounded.width() < original.width() || bounded.height() < original.height())
            reader.setScaledSize(bounded);
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        const SmartSpaceEntry *entry = entryForPath(m_previewPath);
        m_previewStack->setCurrentWidget(m_previewText);
        m_previewText->setHtml(entry
            ? relevantSnippetHtml(*entry, 3, 56, 120)
            : QStringLiteral("预览图片加载失败。"));
        return;
    }
    m_previewImage->setPixmap(QPixmap::fromImage(image));
    m_previewImage->setMinimumSize(image.size());
    m_previewStack->setCurrentIndex(1);
}

void SmartSpaceWidget::startPdfPreview(const SmartSpaceEntry &entry)
{
    const QString pdftoppm = QStandardPaths::findExecutable(
        QStringLiteral("pdftoppm"));
    if (pdftoppm.isEmpty() || !m_previewProcess) {
        m_previewStack->setCurrentWidget(m_previewText);
        m_previewText->setHtml(relevantSnippetHtml(entry, 3, 56, 120));
        return;
    }

    const QByteArray identity = QStringLiteral("%1|%2|%3")
        .arg(entry.path).arg(entry.size)
        .arg(entry.modified.toMSecsSinceEpoch()).toUtf8();
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
    const QString cacheDir = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation) + QStringLiteral("/smart-space/previews");
    QDir().mkpath(cacheDir);
    m_pendingPreviewImage = cacheDir + QLatin1Char('/') + key + QStringLiteral(".png");
    if (QFileInfo::exists(m_pendingPreviewImage)) {
        showPreviewImage(m_pendingPreviewImage);
        prunePreviewCache();
        return;
    }

    m_previewStack->setCurrentWidget(m_previewText);
    m_previewText->setHtml(QStringLiteral(
        "<p>正在低优先级生成 PDF 首页预览…</p>"));
    QString prefix = m_pendingPreviewImage;
    prefix.chop(4);
    QStringList renderArguments{
        QStringLiteral("-f"), QStringLiteral("1"),
        QStringLiteral("-l"), QStringLiteral("1"),
        QStringLiteral("-singlefile"), QStringLiteral("-r"),
        QStringLiteral("120"), QStringLiteral("-png"),
        entry.path, prefix
    };
    const QString nice = QStandardPaths::findExecutable(QStringLiteral("nice"));
    const QString ionice = QStandardPaths::findExecutable(QStringLiteral("ionice"));
    if (!ionice.isEmpty()) {
        QStringList arguments{QStringLiteral("-c"), QStringLiteral("3")};
        if (!nice.isEmpty())
            arguments << nice << QStringLiteral("-n") << QStringLiteral("15");
        arguments << pdftoppm;
        arguments.append(renderArguments);
        m_previewProcess->start(ionice, arguments);
    } else if (!nice.isEmpty()) {
        QStringList arguments{QStringLiteral("-n"), QStringLiteral("15"), pdftoppm};
        arguments.append(renderArguments);
        m_previewProcess->start(nice, arguments);
    } else {
        m_previewProcess->start(pdftoppm, renderArguments);
    }
}

void SmartSpaceWidget::prunePreviewCache()
{
    const QString cacheDir = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation) + QStringLiteral("/smart-space/previews");
    QDir dir(cacheDir);
    const QFileInfoList previews = dir.entryInfoList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Time);
    for (int index = 3; index < previews.size(); ++index)
        QFile::remove(previews.at(index).absoluteFilePath());
}

void SmartSpaceWidget::updateResults(bool resetFolders)
{
    if (resetFolders) {
        m_scopePath.clear();
        clearFolderLevels();
        appendRootLevel();
    }

    QStringList categories = m_selectedCategories;
    QStringList visibleExtensions = m_visibleExtensions;
    QStringList excludedFolders = m_excludedFolders;
    categories.sort(Qt::CaseInsensitive);
    visibleExtensions.sort(Qt::CaseInsensitive);
    excludedFolders.sort(Qt::CaseInsensitive);
    const QString pageKey = QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6\n%7\n%8")
        .arg(m_executedQuery,
             categories.join(QLatin1Char(',')),
             visibleExtensions.join(QLatin1Char(',')),
             excludedFolders.join(QLatin1Char('\n')),
             m_scopePath,
             QString::number(m_resultSortMode),
 QString(),
             QString::number(m_entries.size()));
    if (pageKey != m_resultPageKey) {
        m_resultPageKey = pageKey;
        m_visibleResultLimit = kResultsPageSize;
    }

    m_fileList->setSpacing(m_resultDensity <= -3 ? 0
                           : m_resultDensity <= -2 ? 1
                           : m_resultDensity < 0 ? 2 : 4);
    m_fileList->clear();
    const QVector<int> matches = matchingFileIndexes();
    const int visible = std::min(matches.size(), m_visibleResultLimit);
    for (int i = 0; i < visible; ++i) {
        const SmartSpaceEntry &entry = m_entries.at(matches.at(i));
        auto *item = new QListWidgetItem(m_fileList);
        item->setData(PathRole, entry.path);
        m_fileList->setItemWidget(item, createFileCard(entry, item));
    }
    if (visible < matches.size()) {
        const int remaining = matches.size() - visible;
        const int nextBatch = std::min(kResultsPageSize, remaining);
        auto *moreItem = new QListWidgetItem(m_fileList);
        moreItem->setFlags(Qt::NoItemFlags);
        moreItem->setSizeHint(QSize(0, 44));
        auto *loadMore = new QToolButton(m_fileList);
        loadMore->setProperty("loadMoreResults", true);
        loadMore->setToolButtonStyle(Qt::ToolButtonTextOnly);
        loadMore->setCursor(Qt::PointingHandCursor);
        loadMore->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        loadMore->setText(QStringLiteral(
            "加载更多 %1 个 · 剩余 %2")
            .arg(nextBatch).arg(remaining));
        loadMore->setToolTip(QStringLiteral(
            "每次只创建 100 个文件卡片，避免大量结果一次占用过多资源"));
        m_fileList->setItemWidget(moreItem, loadMore);
        connect(loadMore, &QToolButton::clicked, this, [this, visible] {
            m_visibleResultLimit += kResultsPageSize;
            updateResults(false);
            if (visible < m_fileList->count()) {
                if (QListWidgetItem *firstNew = m_fileList->item(visible))
                    m_fileList->scrollToItem(
                        firstNew, QAbstractItemView::PositionAtCenter);
            }
        });
    }
    if (visible == 0) {
        auto *empty = new QListWidgetItem(
            QIcon::fromTheme(QStringLiteral("edit-find")),
            m_entries.isEmpty() ? QStringLiteral("尚未建立索引")
                                : QStringLiteral("没有匹配文件"),
            m_fileList);
        empty->setFlags(Qt::NoItemFlags);
    }

    QString scope = m_scopePath.isEmpty()
        ? QStringLiteral("全部目录")
        : QFileInfo(m_scopePath).fileName();
    if (scope.isEmpty())
        scope = m_scopePath;
    if (m_folderTitleLabel)
        m_folderTitleLabel->setText(QStringLiteral("文件夹拾取"));
    if (m_fileTitleLabel)
        m_fileTitleLabel->setText(QStringLiteral("匹配结果"));
    if (m_fileCountLabel)
        m_fileCountLabel->setText(QStringLiteral("%1 项").arg(
            QLocale::system().toString(matches.size())));
    if (m_folderCountLabel) {
        int folderCount = 0;
        if (!m_folderLevelWidgets.isEmpty()) {
            const QList<QListWidget *> lists =
                m_folderLevelWidgets.constLast()->findChildren<QListWidget *>();
            if (!lists.isEmpty()) {
                for (int row = 0; row < lists.first()->count(); ++row) {
                    if (lists.first()->item(row)->flags() != Qt::NoItemFlags)
                        ++folderCount;
                }
            }
        }
        m_folderCountLabel->setText(QStringLiteral("%1 项").arg(
            QLocale::system().toString(folderCount)));
    }
    m_statusLabel->setText(QStringLiteral("%1%2 · 匹配 %3 个文件%4")
        .arg(scope)
        .arg(QString())
        .arg(matches.size())
        .arg(matches.size() > visible
            ? QStringLiteral(" · 显示前 %1 个").arg(visible) : QString()));
    update();
    if (parentWidget())
        parentWidget()->update(geometry().adjusted(-4, -4, 4, 4));
}

void SmartSpaceWidget::clearFolderLevels(int fromLevel)
{
    while (m_folderLevelWidgets.size() > fromLevel) {
        QWidget *widget = m_folderLevelWidgets.takeLast();
        m_folderLevelsLayout->removeWidget(widget);
        delete widget;
    }
}

void SmartSpaceWidget::resetFolderScopeFromBlankClick()
{
    if (m_scopePath.isEmpty())
        return;
    // The clicked list is part of the hierarchy that updateResults destroys.
    // Defer rebuilding until the mouse event has returned to Qt.
    QTimer::singleShot(0, this, [this] {
        if (m_scopePath.isEmpty())
            return;
        updateResults(true);
    });
}

void SmartSpaceWidget::updateFolderDensity()
{
    static const int rowHeights[] = {22, 25, 28, 30, 36, 44, 52, 62};
    static const int iconSizes[] = {13, 15, 16, 18, 21, 24, 28, 32};
    const int densityIndex = qBound(
        0, m_resultDensity - kMinResultDensity, kResultDensityLevels - 1);
    const int rowHeight = rowHeights[densityIndex];
    for (int level = 0; level < m_folderLevelWidgets.size(); ++level) {
        const QList<QListWidget *> lists =
            m_folderLevelWidgets.at(level)->findChildren<QListWidget *>();
        for (QListWidget *list : lists) {
            list->setIconSize(QSize(iconSizes[densityIndex],
                                    iconSizes[densityIndex]));
            list->setSpacing(m_resultDensity <= -3 ? 0
                             : m_resultDensity <= -1 ? 1
                             : m_resultDensity >= 1 ? 4 : 2);
            for (int row = 0; row < list->count(); ++row)
                list->item(row)->setSizeHint(QSize(0, rowHeight));
            if (level > 0) {
                const int visibleRows = qBound(2, list->count(), 5);
                list->setMaximumHeight(qMin(320,
                    visibleRows * rowHeight + 12));
            }
        }
    }
}

void SmartSpaceWidget::appendRootLevel()
{
    clearFolderLevels();
    auto *frame = new QFrame(m_folderLevelsContainer);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    QListWidget *list = createEntryList();
    static_cast<SmartEntryList *>(list)->setBlankClickAction(
        [this] { resetFolderScopeFromBlankClick(); });
    list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(list);
    struct FolderResult {
        QString path;
        QString name;
        int count = 0;
    };
    QVector<FolderResult> folders;
    QHash<QString, int> folderIndexes;
    // Build navigation groups from the same result set shown on the right.
    // Do not depend on separate directory records: knowledge-only matches and
    // snapshots without directory entries must still produce folder groups.
    for (const SmartSpaceEntry &entry : m_entries) {
        if (!fileMatchesCurrentResults(entry, false))
            continue;
        QStringList candidateRoots;
        if (!entry.root.isEmpty())
            candidateRoots << entry.root;
        for (const QString &configuredRoot : m_roots) {
            if (!candidateRoots.contains(configuredRoot))
                candidateRoots << configuredRoot;
        }
        for (const QString &root : candidateRoots) {
            const QString cleanRoot = normalizedPath(root);
            if (!pathIsInside(entry.path, cleanRoot))
                continue;
            const QString relative = QDir(cleanRoot).relativeFilePath(entry.path);
            const int separator = relative.indexOf(QLatin1Char('/'));
            if (separator <= 0)
                break;
            const QString key = normalizedPath(
                QDir(cleanRoot).filePath(relative.left(separator)));
            if (folderHidden(key))
                break;
            const auto found = folderIndexes.constFind(key);
            if (found == folderIndexes.constEnd()) {
                folderIndexes.insert(key, folders.size());
                folders.append({key, QFileInfo(key).fileName(), 1});
            } else {
                ++folders[found.value()].count;
            }
            break;
        }
    }
    std::sort(folders.begin(), folders.end(), [](const FolderResult &a,
                                                  const FolderResult &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    for (const FolderResult &folder : folders) {
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(QStringLiteral("folder")),
            QStringLiteral("%1  (%2)").arg(folder.name).arg(folder.count), list);
        item->setData(PathRole, normalizedPath(folder.path));
        item->setToolTip(folder.path);
    }
    if (folders.isEmpty()) {
        auto *empty = new QListWidgetItem(
            QIcon::fromTheme(QStringLiteral("folder")),
            m_entries.isEmpty() ? QStringLiteral("请先点击“更新”建立索引")
                                : QStringLiteral("当前条件下没有子文件夹"), list);
        empty->setFlags(Qt::NoItemFlags);
    }
    connect(list, &QListWidget::itemClicked,
            this, [this](QListWidgetItem *item) { folderActivated(item, 0); });
    connect(list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
                openPath(item->data(PathRole).toString());
            });
    connect(list, &QListWidget::customContextMenuRequested,
            this, [this, list](const QPoint &pos) { showEntryMenu(list, pos); });
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_folderLevelsLayout->insertWidget(
        m_folderLevelsLayout->count() - 1, frame, 1);
    m_folderLevelWidgets.append(frame);
    updateFolderDensity();
}

void SmartSpaceWidget::appendFolderLevel(const QString &parentPath, int level)
{
    clearFolderLevels(level);
    struct FolderResult {
        QString path;
        QString name;
        int count = 0;
    };
    QVector<FolderResult> folders;
    QHash<QString, int> folderIndexes;
    const QString cleanParent = normalizedPath(parentPath);
    for (const SmartSpaceEntry &entry : m_entries) {
        if (!fileMatchesCurrentResults(entry, false) ||
            !pathIsInside(entry.path, cleanParent))
            continue;
        const QString relative = QDir(cleanParent).relativeFilePath(entry.path);
        const int separator = relative.indexOf(QLatin1Char('/'));
        if (separator <= 0)
            continue;
        const QString key = normalizedPath(
            QDir(cleanParent).filePath(relative.left(separator)));
        if (folderHidden(key))
            continue;
        const auto found = folderIndexes.constFind(key);
        if (found == folderIndexes.constEnd()) {
            folderIndexes.insert(key, folders.size());
            folders.append({key, QFileInfo(key).fileName(), 1});
        } else {
            ++folders[found.value()].count;
        }
    }
    std::sort(folders.begin(), folders.end(), [](const FolderResult &a,
                                                  const FolderResult &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    if (folders.isEmpty())
        return;

    auto *frame = new QFrame(m_folderLevelsContainer);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 7, 0, 0);
    layout->setSpacing(4);
    QColor branch = m_accentColor;
    branch.setAlpha(140);
    frame->setStyleSheet(QStringLiteral(
        "QFrame { border-left: 2px solid %1; }").arg(cssColor(branch)));
    QString title = QFileInfo(parentPath).fileName();
    if (title.isEmpty()) title = parentPath;
    auto *label = new QLabel(QStringLiteral("↳ %1").arg(title), frame);
    label->setProperty("panelTitle", true);
    layout->addWidget(label);
    QListWidget *list = createEntryList();
    static_cast<SmartEntryList *>(list)->setBlankClickAction(
        [this] { resetFolderScopeFromBlankClick(); });
    list->setMaximumHeight(190);
    layout->addWidget(list);
    for (const FolderResult &folder : folders) {
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(QStringLiteral("folder")),
            QStringLiteral("%1  (%2)").arg(folder.name).arg(folder.count), list);
        item->setData(PathRole, folder.path);
        item->setToolTip(folder.path);
    }
    connect(list, &QListWidget::itemClicked,
            this, [this, level](QListWidgetItem *item) {
                folderActivated(item, level);
            });
    connect(list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
                openPath(item->data(PathRole).toString());
            });
    connect(list, &QListWidget::customContextMenuRequested,
            this, [this, list](const QPoint &pos) { showEntryMenu(list, pos); });
    m_folderLevelsLayout->insertWidget(m_folderLevelsLayout->count() - 1, frame);
    m_folderLevelWidgets.append(frame);
    updateFolderDensity();
}

void SmartSpaceWidget::folderActivated(QListWidgetItem *item, int level)
{
    if (!item)
        return;
    m_scopePath = normalizedPath(item->data(PathRole).toString());
    appendFolderLevel(m_scopePath, level + 1);
    updateResults(false);
    m_folderScroll->ensureVisible(0, m_folderLevelsContainer->height(), 0, 20);
}

void SmartSpaceWidget::openFileItem(QListWidgetItem *item)
{
    if (item)
        openPath(item->data(PathRole).toString());
}

void SmartSpaceWidget::openPath(const QString &path) const
{
    const DesktopItem entry = DesktopItem::fromPath(path);
    if (entry.isValid())
        entry.open();
}

void SmartSpaceWidget::showEntryMenu(QListWidget *list, const QPoint &position)
{
    QListWidgetItem *item = list ? list->itemAt(position) : nullptr;
    if (!item)
        return;
    list->setCurrentItem(item);
    const QString path = item->data(PathRole).toString();
    if (path.isEmpty())
        return;

    QMenu menu(this);
    applyMenuTheme(&menu);
    QAction *open = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                                   QFileInfo(path).isDir()
                                       ? QStringLiteral("用文件管理器打开")
                                       : QStringLiteral("打开"));
    connect(open, &QAction::triggered, this, [this, path] { openPath(path); });
    if (QFileInfo(path).isDir()) {
        menu.addSeparator();
        QAction *hideFolder = menu.addAction(
            QIcon::fromTheme(QStringLiteral("view-hidden")),
            QStringLiteral("取消显示（仅隐藏左栏）"));
        hideFolder->setToolTip(QStringLiteral("不影响索引和右侧搜索结果"));
        QAction *excludeFolder = menu.addAction(
            QIcon::fromTheme(QStringLiteral("edit-delete")),
            QStringLiteral("取消索引此文件夹…"));
        excludeFolder->setToolTip(QStringLiteral(
            "从索引中排除此文件夹及全部子项，不删除本地文件"));
        connect(hideFolder, &QAction::triggered, this,
                [this, path] { hideFolderFromNavigation(path); });
        connect(excludeFolder, &QAction::triggered, this,
                [this, path] { excludeFolderFromIndex(path); });
    } else {
        QAction *containing = menu.addAction(
            QIcon::fromTheme(QStringLiteral("folder-open")),
            QStringLiteral("打开所在文件夹"));
        connect(containing, &QAction::triggered, this, [this, path] {
            openPath(QFileInfo(path).absolutePath());
        });
    }
    menu.exec(list->viewport()->mapToGlobal(position));
}

void SmartSpaceWidget::showSettingsDialog()
{
    struct ThemeSnapshot {
        int mode;
        int baseSkin;
        int opacity;
        bool colorsEnabled;
        QColor surface, card, border, text, muted, accent;
        QColor searchBg, searchText, buttonBg, buttonText, buttonHover;
        QColor menuBg, menuText, menuHover;
        QString fontFamily;
        int fontSize;
        bool fontBold;
    };
    const ThemeSnapshot originalTheme{
        m_themeMode, m_customBaseSkin, m_customOpacity, m_customColorsEnabled,
        m_customSurface, m_customCard, m_customBorder, m_customText,
        m_customMuted, m_customAccent, m_customSearchBg, m_customSearchText,
        m_customButtonBg, m_customButtonText, m_customButtonHover,
        m_customMenuBg, m_customMenuText, m_customMenuHover,
        m_customFontFamily, m_customFontSize, m_customFontBold
    };
    auto restoreTheme = [this, originalTheme] {
        m_themeMode = originalTheme.mode;
        m_customBaseSkin = originalTheme.baseSkin;
        m_customOpacity = originalTheme.opacity;
        m_customColorsEnabled = originalTheme.colorsEnabled;
        m_customSurface = originalTheme.surface;
        m_customCard = originalTheme.card;
        m_customBorder = originalTheme.border;
        m_customText = originalTheme.text;
        m_customMuted = originalTheme.muted;
        m_customAccent = originalTheme.accent;
        m_customSearchBg = originalTheme.searchBg;
        m_customSearchText = originalTheme.searchText;
        m_customButtonBg = originalTheme.buttonBg;
        m_customButtonText = originalTheme.buttonText;
        m_customButtonHover = originalTheme.buttonHover;
        m_customMenuBg = originalTheme.menuBg;
        m_customMenuText = originalTheme.menuText;
        m_customMenuHover = originalTheme.menuHover;
        m_customFontFamily = originalTheme.fontFamily;
        m_customFontSize = originalTheme.fontSize;
        m_customFontBold = originalTheme.fontBold;
        applyTheme();
    };
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("智能空间设置"));
    dialog.resize(760, 700);
    dialog.setMinimumSize(700, 620);
    dialog.setPalette(palette());
    QColor buttonHover = m_accentColor;
    buttonHover.setAlpha(55);
    dialog.setStyleSheet(styleSheet() + QStringLiteral(R"(
        QDialog { background: %1; color: %2; }
        QLabel[settingsSubtitle="true"] { color: %4; font-size: 12px; }
        QLabel[settingsHint="true"] { color: %4; background: %6;
                                      border: 1px solid %3; border-radius: 8px;
                                      padding: 9px 11px; }
        QTabWidget::pane { background: %6; border: 1px solid %3;
                           border-radius: 10px; top: -1px; }
        QTabBar::tab { color: %4; padding: 9px 15px; margin-right: 2px;
                       border-radius: 7px; }
        QTabBar::tab:hover { color: %2; background: %6; }
        QTabBar::tab:selected { color: %2; background: %5; font-weight: 600; }
        QScrollArea#settingsPageScroll, QWidget#settingsPage {
            background: transparent; border: none;
        }
        QLabel[settingsSectionTitle="true"] { color: %2; font-weight: 600; }
        QListWidget { background: %6; border: 1px solid %3; border-radius: 8px; }
        QPushButton { color: %2; background: %6; border: 1px solid %3;
                      border-radius: 8px; padding: 8px 15px; }
        QPushButton[settingsGroupAction="true"] {
            padding: 5px 12px; min-height: 22px;
        }
        QPushButton:hover { background: %5; }
        QPushButton#primarySettingsButton { color: white; background: %7;
                                            border-color: %7; font-weight: 600; }
        QCheckBox, QLabel { color: %2; }
    )").arg(cssColor(m_themeMode == 3 ? QColor(28, 33, 42) : m_surfaceColor), cssColor(m_textColor),
             cssColor(m_borderColor), cssColor(m_mutedColor),
             cssColor(buttonHover), cssColor(m_cardColor),
             cssColor(m_accentColor)));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 15, 18, 15);
    layout->setSpacing(11);
    auto *settingsSubtitle = new QLabel(QStringLiteral(
        "索引范围、更新策略、外观和本地 Skill 配置均保存在本机"), &dialog);
    settingsSubtitle->setProperty("settingsSubtitle", true);
    layout->addWidget(settingsSubtitle);
    auto *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs, 1);

    auto *indexTab = new QScrollArea(tabs);
    indexTab->setObjectName(QStringLiteral("settingsPageScroll"));
    indexTab->setWidgetResizable(true);
    indexTab->setFrameShape(QFrame::NoFrame);
    indexTab->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *indexPage = new QWidget(indexTab);
    indexPage->setObjectName(QStringLiteral("settingsPage"));
    indexPage->setMinimumHeight(570);
    auto *indexLayout = new QVBoxLayout(indexPage);
    indexLayout->setContentsMargins(12, 12, 12, 12);
    indexLayout->setSpacing(8);
    auto *addRoot = new QPushButton(QStringLiteral("添加…"), indexPage);
    auto *removeRoot = new QPushButton(QStringLiteral("移除"), indexPage);
    addRoot->setProperty("settingsGroupAction", true);
    removeRoot->setProperty("settingsGroupAction", true);
    auto *rootHeadingRow = new QHBoxLayout;
    rootHeadingRow->setContentsMargins(0, 0, 0, 0);
    rootHeadingRow->setSpacing(6);
    auto *rootHeading = new QLabel(QStringLiteral("索引目录"), indexPage);
    rootHeading->setProperty("settingsSectionTitle", true);
    rootHeadingRow->addWidget(rootHeading);
    rootHeadingRow->addStretch(1);
    rootHeadingRow->addWidget(addRoot);
    rootHeadingRow->addWidget(removeRoot);
    indexLayout->addLayout(rootHeadingRow);
    auto *rootDescription = new QLabel(
        QStringLiteral("只选择需要搜索的目录；范围越精确，索引越快、资源越低。"), indexPage);
    rootDescription->setProperty("settingsSubtitle", true);
    indexLayout->addWidget(rootDescription);
    auto *rootList = new QListWidget(indexPage);
    rootList->addItems(m_roots);
    rootList->setFixedHeight(82);
    indexLayout->addWidget(rootList);

    auto *addExclude = new QPushButton(QStringLiteral("添加排除…"), indexPage);
    auto *restoreExclude = new QPushButton(QStringLiteral("恢复索引"), indexPage);
    addExclude->setProperty("settingsGroupAction", true);
    restoreExclude->setProperty("settingsGroupAction", true);
    auto *excludeHeadingRow = new QHBoxLayout;
    excludeHeadingRow->setContentsMargins(0, 0, 0, 0);
    excludeHeadingRow->setSpacing(6);
    auto *excludeHeading = new QLabel(QStringLiteral("排除文件夹（不进入索引）"), indexPage);
    excludeHeading->setProperty("settingsSectionTitle", true);
    excludeHeadingRow->addWidget(excludeHeading);
    excludeHeadingRow->addStretch(1);
    excludeHeadingRow->addWidget(addExclude);
    excludeHeadingRow->addWidget(restoreExclude);
    indexLayout->addLayout(excludeHeadingRow);
    auto *excludeList = new QListWidget(indexPage);
    excludeList->addItems(m_excludedFolders);
    excludeList->setFixedHeight(66);
    excludeList->setToolTip(QStringLiteral("增量、快速全量和 OCR 补全都会跳过这些文件夹及子项"));
    indexLayout->addWidget(excludeList);
    auto *restoreHidden = new QPushButton(QStringLiteral("恢复显示"), indexPage);
    restoreHidden->setProperty("settingsGroupAction", true);
    auto *hiddenHeadingRow = new QHBoxLayout;
    hiddenHeadingRow->setContentsMargins(0, 0, 0, 0);
    hiddenHeadingRow->setSpacing(6);
    auto *hiddenHeading = new QLabel(QStringLiteral("左栏隐藏文件夹（仍可搜索）"), indexPage);
    hiddenHeading->setProperty("settingsSectionTitle", true);
    hiddenHeadingRow->addWidget(hiddenHeading);
    hiddenHeadingRow->addStretch(1);
    hiddenHeadingRow->addWidget(restoreHidden);
    indexLayout->addLayout(hiddenHeadingRow);
    auto *hiddenList = new QListWidget(indexPage);
    hiddenList->addItems(m_hiddenFolders);
    hiddenList->setFixedHeight(54);
    indexLayout->addWidget(hiddenList);
    auto *ocr = new QCheckBox(
        QStringLiteral("启用独立 OCR 补全（Tesseract）"), indexPage);
    ocr->setChecked(m_ocrImages);
    indexLayout->addWidget(ocr);
    auto *resourceRow = new QHBoxLayout;
    auto *contentBudget = new QSpinBox(indexPage);
    contentBudget->setRange(32, 64);
    contentBudget->setValue(qBound(32, m_maxTotalContentMb, 64));
    contentBudget->setSuffix(QStringLiteral(" MB"));
    resourceRow->addWidget(new QLabel(
        QStringLiteral("快全量正文预算："), indexPage));
    resourceRow->addWidget(contentBudget);
    resourceRow->addStretch(1);
    indexLayout->addLayout(resourceRow);
    auto *resourceHint = new QLabel(QStringLiteral(
        "增量与快全量均保留完整快照，不再设置文件数上限。"
        "增量固定使用 64 MB 新正文保护预算；上方预算仅用于快全量。"
        "扫描 PDF 和图片会先标记，再交给独立 OCR 按钮。"), indexPage);
    resourceHint->setWordWrap(true);
    resourceHint->setProperty("settingsHint", true);
    indexLayout->addWidget(resourceHint);
    indexLayout->addStretch(1);
    indexTab->setWidget(indexPage);
    tabs->addTab(indexTab, QStringLiteral("索引范围"));

    auto *formatTab = new QWidget(tabs);
    auto *formatLayout = new QVBoxLayout(formatTab);
    formatLayout->setContentsMargins(14, 14, 14, 14);
    formatLayout->setSpacing(8);
    auto formatItemText = [](QListWidgetItem *item) {
        const QString label = item->data(Qt::UserRole + 1).toString();
        return QStringLiteral("%1  %2")
            .arg(item->checkState() == Qt::Checked
                    ? QStringLiteral("✔") : QStringLiteral("○"),
                 label);
    };
    auto createFormatList = [&formatTab, formatItemText](const QStringList &selected) {
        auto *list = new QListWidget(formatTab);
        list->setViewMode(QListView::IconMode);
        list->setResizeMode(QListView::Adjust);
        list->setMovement(QListView::Static);
        list->setWrapping(true);
        list->setGridSize(QSize(138, 31));
        list->setSpacing(2);
        const bool all = selected.isEmpty();
        for (const FormatOption &option : formatOptions()) {
            const QString id = QString::fromLatin1(option.id);
            const QString label =
                id == QLatin1String("__other__")
                    ? QString::fromUtf8(option.label)
                    : QStringLiteral("%1  .%2")
                        .arg(QString::fromLatin1(option.label), id);
            auto *item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, id);
            item->setData(Qt::UserRole + 1, label);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setCheckState(all || selected.contains(id)
                ? Qt::Checked : Qt::Unchecked);
            item->setText(formatItemText(item));
        }
        QObject::connect(list, &QListWidget::itemChanged, list,
                         [formatItemText](QListWidgetItem *item) {
            const QString text = formatItemText(item);
            if (item->text() != text)
                item->setText(text);
        });
        QObject::connect(list, &QListWidget::itemPressed, list,
                         [](QListWidgetItem *item) {
            if (!item)
                return;
            item->setCheckState(item->checkState() == Qt::Checked
                ? Qt::Unchecked : Qt::Checked);
        });
        return list;
    };
    auto *indexFormatHeading = new QLabel(QStringLiteral("索引格式白名单"), formatTab);
    indexFormatHeading->setStyleSheet(QStringLiteral("font-weight: 600;"));
    formatLayout->addWidget(indexFormatHeading);
    auto *indexFormatHint = new QLabel(QStringLiteral(
        "只有勾选的文件才会进入索引；文件夹本身始终保留以支持向下钻取。"), formatTab);
    indexFormatHint->setProperty("settingsSubtitle", true);
    indexFormatHint->setWordWrap(true);
    formatLayout->addWidget(indexFormatHint);
    auto *indexFormatList = createFormatList(m_indexedExtensions);
    indexFormatList->setMinimumHeight(125);
    indexFormatList->setMaximumHeight(145);
    formatLayout->addWidget(indexFormatList, 1);
    auto *indexFormatButtons = new QHBoxLayout;
    auto *indexSelectAll = new QPushButton(QStringLiteral("全选"), formatTab);
    auto *indexSelectCommon = new QPushButton(QStringLiteral("仅常用办公格式"), formatTab);
    auto *indexClear = new QPushButton(QStringLiteral("清空"), formatTab);
    indexFormatButtons->addWidget(indexSelectAll);
    indexFormatButtons->addWidget(indexSelectCommon);
    indexFormatButtons->addWidget(indexClear);
    indexFormatButtons->addStretch(1);
    formatLayout->addLayout(indexFormatButtons);

    auto *visibleFormatHeading = new QLabel(QStringLiteral("匹配文件显示格式"), formatTab);
    visibleFormatHeading->setStyleSheet(QStringLiteral("font-weight: 600;"));
    formatLayout->addWidget(visibleFormatHeading);
    auto *visibleFormatHint = new QLabel(QStringLiteral(
        "仅控制右侧匹配文件的显示范围，不会删除已有索引。"), formatTab);
    visibleFormatHint->setProperty("settingsSubtitle", true);
    visibleFormatHint->setWordWrap(true);
    formatLayout->addWidget(visibleFormatHint);
    auto *visibleFormatList = createFormatList(m_visibleExtensions);
    visibleFormatList->setMinimumHeight(115);
    visibleFormatList->setMaximumHeight(135);
    formatLayout->addWidget(visibleFormatList, 1);
    auto *visibleFormatButtons = new QHBoxLayout;
    auto *visibleSelectAll = new QPushButton(QStringLiteral("全选"), formatTab);
    auto *visibleSelectIndexed = new QPushButton(QStringLiteral("与索引格式同步"), formatTab);
    auto *visibleClear = new QPushButton(QStringLiteral("清空"), formatTab);
    visibleFormatButtons->addWidget(visibleSelectAll);
    visibleFormatButtons->addWidget(visibleSelectIndexed);
    visibleFormatButtons->addWidget(visibleClear);
    visibleFormatButtons->addStretch(1);
    formatLayout->addLayout(visibleFormatButtons);
    tabs->addTab(formatTab, QStringLiteral("文件格式"));

    auto *updateTab = new QWidget(tabs);
    auto *updateLayout = new QVBoxLayout(updateTab);
    updateLayout->setContentsMargins(14, 14, 14, 14);
    updateLayout->setSpacing(12);
    auto *modeRow = new QHBoxLayout;
    auto *indexMode = new QComboBox(updateTab);
    indexMode->addItem(QStringLiteral("仅手动（推荐，打开时不扫描）"), 0);
    indexMode->addItem(QStringLiteral("文件变化后增量更新"), 1);
    indexMode->addItem(QStringLiteral("定时增量更新"), 2);
    indexMode->setCurrentIndex(indexMode->findData(m_indexMode));
    auto *interval = new QSpinBox(updateTab);
    interval->setRange(5, 1440);
    interval->setValue(m_indexIntervalMinutes);
    interval->setSuffix(QStringLiteral(" 分钟"));
    interval->setEnabled(m_indexMode == 2);
    modeRow->addWidget(new QLabel(QStringLiteral("更新方式："), updateTab));
    modeRow->addWidget(indexMode, 1);
    modeRow->addWidget(interval);
    updateLayout->addLayout(modeRow);
    connect(indexMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            interval, [indexMode, interval](int) {
        interval->setEnabled(indexMode->currentData().toInt() == 2);
    });

    auto *autoStart = new QCheckBox(
        QStringLiteral("随 Fences 启动时显示组件（不代表自动索引）"), updateTab);
    autoStart->setChecked(m_autoStart);
    updateLayout->addWidget(autoStart);
    auto *defaultHidden = new QCheckBox(
        QStringLiteral("启动或启用组件后默认贴边隐藏"), updateTab);
    defaultHidden->setChecked(m_defaultHidden);
    defaultHidden->setToolTip(QStringLiteral(
        "打开组件后点击边缘图标即可展开；关闭后恢复上次展开状态"));
    updateLayout->addWidget(defaultHidden);
    auto *syncKylin = new QCheckBox(
        QStringLiteral("同步目录到麒麟文件索引服务（如果已安装）"), updateTab);
    syncKylin->setChecked(true);
    updateLayout->addWidget(syncKylin);
    auto *indexNow = new QCheckBox(
        QStringLiteral("保存设置后立即更新一次索引"), updateTab);
    indexNow->setChecked(false);
    updateLayout->addWidget(indexNow);
    QString snapshotText = QStringLiteral("当前没有索引快照。");
    QFile snapshot(indexPath());
    if (snapshot.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(snapshot.readAll()).object();
        snapshotText = QStringLiteral("已保存快照：%1 项 · %2")
            .arg(object.value(QStringLiteral("items")).toArray().size())
            .arg(object.value(QStringLiteral("generatedAt")).toString()
                .left(19).replace(QLatin1Char('T'), QLatin1Char(' ')));
    }
    auto *snapshotLabel = new QLabel(snapshotText, updateTab);
    snapshotLabel->setWordWrap(true);
    updateLayout->addWidget(snapshotLabel);
    auto *saveHint = new QLabel(QStringLiteral(
        "索引器每次生成新快照后再原子替换旧文件；"
        "智能空间规则和设置单独持久化。关闭组件不会丢失已有索引。"), updateTab);
    saveHint->setWordWrap(true);
    saveHint->setProperty("settingsHint", true);
    updateLayout->addWidget(saveHint);
    auto *idleHint = new QLabel(QStringLiteral(
        "“快全量”和“增量”都不启动 OCR。"
        "“OCR”只补全已标记的扫描 PDF/图片，低优先级逐文件处理并支持暂停续扫。"), updateTab);
    idleHint->setWordWrap(true);
    idleHint->setProperty("settingsHint", true);
    updateLayout->addWidget(idleHint);
    updateLayout->addStretch(1);
    tabs->addTab(updateTab, QStringLiteral("更新策略"));

    auto *appearanceTab = new QWidget(tabs);
    auto *appearanceLayout = new QVBoxLayout(appearanceTab);
    appearanceLayout->setContentsMargins(18, 18, 18, 18);
    appearanceLayout->setSpacing(12);
    auto *appearanceTitle = new QLabel(QStringLiteral("智能空间皮肤"),
                                       appearanceTab);
    appearanceTitle->setProperty("settingsSectionTitle", true);
    appearanceLayout->addWidget(appearanceTitle);
    auto *appearanceHint = new QLabel(QStringLiteral(
        "选择智能空间的整体视觉风格。液态玻璃会抓取桌面内容作为柔化背景，"
        "并应用半透明面板、边缘高光和玻璃按钮。"), appearanceTab);
    appearanceHint->setProperty("settingsHint", true);
    appearanceHint->setWordWrap(true);
    appearanceLayout->addWidget(appearanceHint);
    auto *themeMode = new QComboBox(&dialog);
    themeMode->addItem(QStringLiteral("黑夜模式"), 1);
    themeMode->addItem(QStringLiteral("白天模式"), 2);
    themeMode->addItem(QStringLiteral("液态玻璃"), 3);
    themeMode->setCurrentIndex(themeMode->findData(m_themeMode));
    themeMode->setMinimumWidth(220);
    appearanceLayout->addWidget(themeMode);
    auto *appearanceDetail = new QLabel(QStringLiteral(
        "黑夜模式和白天模式使用纯色界面；液态玻璃适合叠加在桌面壁纸上。"
        "设置保存后，下次打开智能空间会继续使用当前皮肤。"), appearanceTab);
    appearanceDetail->setProperty("settingsSubtitle", true);
    appearanceDetail->setWordWrap(true);
    appearanceLayout->addWidget(appearanceDetail);
    appearanceLayout->addStretch(1);
    tabs->addTab(appearanceTab, QStringLiteral("外观"));
    connect(themeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [this, themeMode](int) {
        const int mode = themeMode->currentData().toInt();
        if (mode < 1 || mode > 3)
            return;
        m_themeMode = mode;
        applyTheme();
    });

    auto *knowledgeTab = new QWidget(tabs);
    auto *knowledgeLayout = new QVBoxLayout(knowledgeTab);
    knowledgeLayout->setContentsMargins(14, 14, 14, 14);
    knowledgeLayout->setSpacing(10);
    auto *knowledgePathRow = new QHBoxLayout;
    auto *knowledgePathEdit = new QLineEdit(
        m_knowledgeDirectory, knowledgeTab);
    auto *browseKnowledge = new QPushButton(
        QStringLiteral("选择位置…"), knowledgeTab);
    knowledgePathRow->addWidget(
        new QLabel(QStringLiteral("保存位置："), knowledgeTab));
    knowledgePathRow->addWidget(knowledgePathEdit, 1);
    knowledgePathRow->addWidget(browseKnowledge);
    knowledgeLayout->addLayout(knowledgePathRow);
    connect(browseKnowledge, &QPushButton::clicked, &dialog,
            [&dialog, knowledgePathEdit] {
        const QString path = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("选择知识库保存位置"),
            knowledgePathEdit->text());
        if (!path.isEmpty())
            knowledgePathEdit->setText(path);
    });
    auto *chunkRow = new QHBoxLayout;
    auto *knowledgeChunkSize = new QSpinBox(knowledgeTab);
    knowledgeChunkSize->setRange(600, 3000);
    knowledgeChunkSize->setSingleStep(100);
    knowledgeChunkSize->setValue(m_knowledgeChunkSize);
    knowledgeChunkSize->setSuffix(QStringLiteral(" 字"));
    auto *knowledgeOverlap = new QSpinBox(knowledgeTab);
    knowledgeOverlap->setRange(0, 600);
    knowledgeOverlap->setSingleStep(20);
    knowledgeOverlap->setValue(m_knowledgeOverlap);
    knowledgeOverlap->setSuffix(QStringLiteral(" 字"));
    chunkRow->addWidget(new QLabel(QStringLiteral("片段长度："), knowledgeTab));
    chunkRow->addWidget(knowledgeChunkSize);
    chunkRow->addSpacing(18);
    chunkRow->addWidget(new QLabel(QStringLiteral("上下文重叠："), knowledgeTab));
    chunkRow->addWidget(knowledgeOverlap);
    chunkRow->addStretch(1);
    knowledgeLayout->addLayout(chunkRow);
    connect(knowledgeChunkSize, QOverload<int>::of(&QSpinBox::valueChanged),
            knowledgeOverlap, [knowledgeOverlap](int value) {
        knowledgeOverlap->setMaximum(value / 3);
    });
    auto *buildKnowledgeNow = new QCheckBox(
        QStringLiteral("保存后立即快速建库（仅本地处理，可断点重跑）"),
        knowledgeTab);
    knowledgeLayout->addWidget(buildKnowledgeNow);
    QString knowledgeStatus = QStringLiteral("尚未建立知识库");
    const QString existingKnowledgeDb = knowledgeDbPath();
    if (QFileInfo::exists(existingKnowledgeDb)) {
        knowledgeStatus = QStringLiteral("现有知识库：%1 MB · %2")
            .arg(QFileInfo(existingKnowledgeDb).size()
                 / 1024.0 / 1024.0, 0, 'f', 1)
            .arg(existingKnowledgeDb);
    }
    auto *knowledgeHint = new QLabel(
        knowledgeStatus + QStringLiteral(
            "\n快速建库不会调用网络，也不会修改 OCR/全文索引；"
            "仅保存本地正文片段和检索元数据，暂停后会从未完成处继续。"),
        knowledgeTab);
    knowledgeHint->setWordWrap(true);
    knowledgeHint->setProperty("settingsHint", true);
    knowledgeLayout->addWidget(knowledgeHint);
    knowledgeLayout->addStretch(1);
    tabs->addTab(knowledgeTab, QStringLiteral("知识库"));

    auto *skillTab = new QWidget(tabs);
    auto *skillLayout = new QVBoxLayout(skillTab);
    skillLayout->setContentsMargins(14, 14, 14, 14);
    skillLayout->setSpacing(10);
    auto *skillTitle = new QLabel(QStringLiteral(
        "将本地索引能力提供给 Codex、Antigravity 等 Agent"), skillTab);
    skillTitle->setProperty("settingsSectionTitle", true);
    skillLayout->addWidget(skillTitle);
    auto *skillHint = new QLabel(QStringLiteral(
        "Skill 只负责调用本地索引和组织研究流程，模型由外部 Agent 自行决定。"
        "复制路径可直接配置其他 Agent；导出会把完整 Skill 目录复制到指定位置。"),
        skillTab);
    skillHint->setWordWrap(true);
    skillHint->setProperty("settingsHint", true);
    skillLayout->addWidget(skillHint);
    auto *skillPath = new QLineEdit(skillDirectoryPath(), skillTab);
    skillPath->setReadOnly(true);
    skillPath->setToolTip(skillPath->text());
    auto *copySkillPath = new QPushButton(QStringLiteral("复制 Skill 路径"), skillTab);
    auto *exportSkill = new QPushButton(QStringLiteral("导出 Skill"), skillTab);
    auto *skillActionRow = new QHBoxLayout;
    skillActionRow->addWidget(skillPath, 1);
    skillActionRow->addWidget(copySkillPath);
    skillActionRow->addWidget(exportSkill);
    skillLayout->addLayout(skillActionRow);
    auto *skillStatus = new QLabel(skillTab);
    skillStatus->setWordWrap(true);
    skillStatus->setProperty("settingsSubtitle", true);
    skillLayout->addWidget(skillStatus);
    skillLayout->addStretch(1);
    connect(copySkillPath, &QPushButton::clicked, &dialog,
            [skillPath, skillStatus] {
        const QString path = skillPath->text().trimmed();
        if (path.isEmpty() || !QDir(path).exists()) {
            skillStatus->setText(QStringLiteral("Skill 目录不存在，请先完成索引 Skill 安装。"));
            return;
        }
        QApplication::clipboard()->setText(path);
        skillStatus->setText(QStringLiteral("Skill 路径已复制到剪贴板。"));
    });
    connect(exportSkill, &QPushButton::clicked, &dialog,
            [this, &dialog, skillPath, skillStatus] {
        const QString source = skillPath->text().trimmed();
        if (source.isEmpty() || !QDir(source).exists()) {
            skillStatus->setText(QStringLiteral("Skill 目录不存在，无法导出。"));
            return;
        }
        const QString destination = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("选择 Skill 导出目录"));
        if (destination.isEmpty())
            return;
        const QString target = QDir(destination).filePath(
            QStringLiteral("ukui-fences-index-query"));
        if (QDir::cleanPath(source) == QDir::cleanPath(target)) {
            skillStatus->setText(QStringLiteral("目标目录就是当前 Skill，无需导出。"));
            return;
        }
        if (QDir(target).exists()) {
            const auto answer = QMessageBox::question(
                &dialog, QStringLiteral("确认覆盖"),
                QStringLiteral("目标目录已存在，是否覆盖其中同名文件？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        QString error;
        if (!copyDirectoryTree(source, target, &error)) {
            skillStatus->setText(QStringLiteral("Skill 导出失败：%1").arg(error));
            return;
        }
        skillStatus->setText(QStringLiteral("Skill 已导出到：%1").arg(target));
    });
    tabs->addTab(skillTab, QStringLiteral("Skill"));

    auto *providerTab = new QWidget(tabs);
    auto *providerLayout = new QVBoxLayout(providerTab);
    providerLayout->setContentsMargins(14, 14, 14, 14);
    providerLayout->setSpacing(10);
    auto *providerRow = new QHBoxLayout;
    auto *providerEdit = new QLineEdit(m_providerConfig, providerTab);
    providerEdit->setPlaceholderText(QStringLiteral("可选：Command / HTTP / D-Bus Provider JSON"));
    auto *browseProvider = new QPushButton(QStringLiteral("选择…"), providerTab);
    auto *editProvider = new QPushButton(QStringLiteral("编辑 JSON…"), providerTab);
    providerRow->addWidget(new QLabel(QStringLiteral("外部数据源："), providerTab));
    providerRow->addWidget(providerEdit, 1);
    providerRow->addWidget(browseProvider);
    providerRow->addWidget(editProvider);
    providerLayout->addLayout(providerRow);
    auto *hint = new QLabel(
        QStringLiteral("支持：PDF、DOCX、PPTX、XLSX、ODF、文本、图片 OCR；"
                       "ET/XLS 优先只读解析，DOC/PPT/WPS/DPS 使用兼容文本提取。"),
        providerTab);
    hint->setWordWrap(true);
    hint->setProperty("settingsHint", true);
    providerLayout->addWidget(hint);
    providerLayout->addStretch(1);
    tabs->addTab(providerTab, QStringLiteral("数据源"));

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (auto *ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->setText(QStringLiteral("保存设置"));
        ok->setIcon(QIcon());
        ok->setObjectName(QStringLiteral("primarySettingsButton"));
        ok->setMinimumWidth(104);
        ok->setStyleSheet(QStringLiteral(
            "QPushButton { color: white; background: %1; border: 1px solid %1; "
            "border-radius: 8px; padding: 8px 16px; font-weight: 600; }")
            .arg(cssColor(m_accentColor)));
    }
    if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setText(QStringLiteral("取消"));
        cancel->setIcon(QIcon());
    }
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(addRoot, &QPushButton::clicked, &dialog, [&dialog, rootList] {
        const QString path = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("选择索引目录"));
        if (!path.isEmpty()) {
            const QList<QListWidgetItem *> found = rootList->findItems(path, Qt::MatchExactly);
            if (found.isEmpty())
                rootList->addItem(path);
        }
    });
    connect(removeRoot, &QPushButton::clicked, &dialog, [rootList] {
        qDeleteAll(rootList->selectedItems());
    });
    connect(addExclude, &QPushButton::clicked, &dialog,
            [&dialog, rootList, excludeList] {
        const QString path = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("选择要排除的文件夹"));
        if (path.isEmpty())
            return;
        const QString clean = normalizedPath(path);
        bool insideRoot = false;
        bool equalsRoot = false;
        for (int row = 0; row < rootList->count(); ++row) {
            const QString root = normalizedPath(rootList->item(row)->text());
            insideRoot = insideRoot || pathIsInside(clean, root);
            equalsRoot = equalsRoot || clean == root;
        }
        if (!insideRoot || equalsRoot) {
            QMessageBox::information(&dialog, QStringLiteral("不能添加"),
                equalsRoot
                    ? QStringLiteral("如需取消整个根目录，请从“索引目录”中移除。")
                    : QStringLiteral("排除文件夹必须位于已配置的索引目录内。"));
            return;
        }
        if (excludeList->findItems(clean, Qt::MatchExactly).isEmpty())
            excludeList->addItem(clean);
    });
    connect(restoreExclude, &QPushButton::clicked, &dialog, [excludeList] {
        qDeleteAll(excludeList->selectedItems());
    });
    connect(restoreHidden, &QPushButton::clicked, &dialog, [hiddenList] {
        qDeleteAll(hiddenList->selectedItems());
    });
    auto setFormatChecks = [](QListWidget *list, Qt::CheckState state) {
        for (int row = 0; row < list->count(); ++row)
            list->item(row)->setCheckState(state);
    };
    connect(indexSelectAll, &QPushButton::clicked, &dialog,
            [indexFormatList, setFormatChecks] {
        setFormatChecks(indexFormatList, Qt::Checked);
    });
    connect(indexClear, &QPushButton::clicked, &dialog,
            [indexFormatList, setFormatChecks] {
        setFormatChecks(indexFormatList, Qt::Unchecked);
    });
    connect(indexSelectCommon, &QPushButton::clicked, &dialog,
            [indexFormatList] {
        const QSet<QString> common{
            QStringLiteral("pdf"), QStringLiteral("docx"), QStringLiteral("doc"),
            QStringLiteral("wps"), QStringLiteral("pptx"), QStringLiteral("ppt"),
            QStringLiteral("dps"), QStringLiteral("xlsx"), QStringLiteral("xls"),
            QStringLiteral("et"), QStringLiteral("txt"), QStringLiteral("md"),
            QStringLiteral("csv"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("png")
        };
        for (int row = 0; row < indexFormatList->count(); ++row) {
            QListWidgetItem *item = indexFormatList->item(row);
            item->setCheckState(common.contains(item->data(Qt::UserRole).toString())
                ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(visibleSelectAll, &QPushButton::clicked, &dialog,
            [visibleFormatList, setFormatChecks] {
        setFormatChecks(visibleFormatList, Qt::Checked);
    });
    connect(visibleClear, &QPushButton::clicked, &dialog,
            [visibleFormatList, setFormatChecks] {
        setFormatChecks(visibleFormatList, Qt::Unchecked);
    });
    connect(visibleSelectIndexed, &QPushButton::clicked, &dialog,
            [indexFormatList, visibleFormatList] {
        for (int row = 0; row < visibleFormatList->count(); ++row)
            visibleFormatList->item(row)->setCheckState(
                indexFormatList->item(row)->checkState());
    });
    connect(browseProvider, &QPushButton::clicked, &dialog, [&dialog, providerEdit] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("选择 Provider 配置"),
            QFileInfo(providerEdit->text()).absolutePath(), QStringLiteral("JSON (*.json)"));
        if (!path.isEmpty()) providerEdit->setText(path);
    });
    connect(editProvider, &QPushButton::clicked, &dialog, [this, &dialog, providerEdit] {
        QString path = providerEdit->text().trimmed();
        if (path.isEmpty()) path = providerConfigPath();
        QFile source(path);
        QByteArray value;
        if (source.open(QIODevice::ReadOnly)) value = source.readAll();
        if (value.trimmed().isEmpty()) {
            value = QByteArray(R"({
  "providers": [
    {
      "name": "本地命令示例",
      "enabled": false,
      "type": "command",
      "program": "/path/to/provider",
      "arguments": [],
      "timeout": 30,
      "limit": 5000
    },
    {
      "name": "HTTP API 示例",
      "enabled": false,
      "type": "http",
      "url": "http://127.0.0.1:8080/search",
      "tokenEnv": "UKUI_FENCES_PROVIDER_TOKEN"
    },
    {
      "name": "D-Bus 示例",
      "enabled": false,
      "type": "dbus",
      "service": "org.example.Search",
      "objectPath": "/org/example/Search",
      "method": "org.example.Search.queryJson"
    }
  ]
})");
        }
        QDialog editor(&dialog);
        editor.setWindowTitle(QStringLiteral("Provider JSON 配置"));
        editor.resize(720, 540);
        auto *editorLayout = new QVBoxLayout(&editor);
        auto *textEdit = new QPlainTextEdit(QString::fromUtf8(value), &editor);
        textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
        editorLayout->addWidget(textEdit, 1);
        auto *editorButtons = new QDialogButtonBox(
            QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
        editorLayout->addWidget(editorButtons);
        connect(editorButtons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        connect(editorButtons, &QDialogButtonBox::accepted, &editor,
                [&editor, textEdit, path, providerEdit] {
            QJsonParseError parseError;
            const QByteArray bytes = textEdit->toPlainText().toUtf8();
            const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                QMessageBox::warning(&editor, QStringLiteral("JSON 无效"),
                                     parseError.errorString());
                return;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly) ||
                output.write(document.toJson(QJsonDocument::Indented)) < 0 ||
                !output.commit()) {
                QMessageBox::warning(&editor, QStringLiteral("保存失败"), path);
                return;
            }
            providerEdit->setText(path);
            editor.accept();
        });
        editor.exec();
    });

    if (dialog.exec() != QDialog::Accepted) {
        restoreTheme();
        return;
    }
    QStringList roots;
    for (int i = 0; i < rootList->count(); ++i) {
        const QString path = rootList->item(i)->text();
        if (QDir(path).exists() && !roots.contains(path))
            roots << path;
    }
    if (roots.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("智能空间"),
                                 QStringLiteral("至少需要保留一个可访问的索引目录。"));
        restoreTheme();
        return;
    }
    QStringList excludedFolders;
    for (int row = 0; row < excludeList->count(); ++row) {
        const QString path = normalizedPath(excludeList->item(row)->text());
        bool valid = false;
        for (const QString &root : roots) {
            if (path != normalizedPath(root) && pathIsInside(path, root)) {
                valid = true;
                break;
            }
        }
        if (valid && !excludedFolders.contains(path))
            excludedFolders << path;
    }
    std::sort(excludedFolders.begin(), excludedFolders.end(),
              [](const QString &left, const QString &right) {
        return left.size() < right.size();
    });
    QStringList compactExcluded;
    for (const QString &path : excludedFolders) {
        bool alreadyCovered = false;
        for (const QString &parent : compactExcluded)
            alreadyCovered = alreadyCovered || pathIsInside(path, parent);
        if (!alreadyCovered)
            compactExcluded << path;
    }
    excludedFolders = compactExcluded;
    QStringList hiddenFolders;
    for (int row = 0; row < hiddenList->count(); ++row) {
        const QString path = normalizedPath(hiddenList->item(row)->text());
        if (!path.isEmpty() && !hiddenFolders.contains(path))
            hiddenFolders << path;
    }
    auto checkedFormats = [](QListWidget *list, bool *noneSelected) {
        QStringList result;
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem *item = list->item(row);
            if (item->checkState() == Qt::Checked)
                result << item->data(Qt::UserRole).toString();
        }
        *noneSelected = result.isEmpty();
        if (result.size() == list->count())
            result.clear(); // Empty is the backward-compatible "all formats" value.
        return result;
    };
    bool noIndexFormats = false;
    bool noVisibleFormats = false;
    const QStringList indexedExtensions = checkedFormats(
        indexFormatList, &noIndexFormats);
    const QStringList visibleExtensions = checkedFormats(
        visibleFormatList, &noVisibleFormats);
    if (noIndexFormats || noVisibleFormats) {
        QMessageBox::information(this, QStringLiteral("智能空间"),
            noIndexFormats
                ? QStringLiteral("索引格式至少需要保留一项。")
                : QStringLiteral("匹配文件显示格式至少需要保留一项。"));
        restoreTheme();
        return;
    }
    const bool excludeChanged = excludedFolders != m_excludedFolders;
    const bool hiddenChanged = hiddenFolders != m_hiddenFolders;
    const bool visibleFormatsChanged = visibleExtensions != m_visibleExtensions;
    const bool previousDefaultHidden = m_defaultHidden;
    const bool indexInputsChanged = roots != m_roots ||
        excludeChanged || indexedExtensions != m_indexedExtensions ||
        ocr->isChecked() != m_ocrImages ||
        contentBudget->value() != m_maxTotalContentMb ||
        providerEdit->text().trimmed() != m_providerConfig;
    m_roots = roots;
    m_excludedFolders = excludedFolders;
    m_hiddenFolders = hiddenFolders;
    m_indexedExtensions = indexedExtensions;
    m_visibleExtensions = visibleExtensions;
    m_ocrImages = ocr->isChecked();
    m_maxTotalContentMb = contentBudget->value();
    m_indexMode = indexMode->currentData().toInt();
    m_indexIntervalMinutes = interval->value();
    m_themeMode = themeMode->currentData().toInt();
    m_customOpacity = 100;
    m_customColorsEnabled = false;
    m_autoStart = autoStart->isChecked();
    m_defaultHidden = defaultHidden->isChecked();
    m_knowledgeDirectory = QDir::cleanPath(
        knowledgePathEdit->text().trimmed());
    if (m_knowledgeDirectory.isEmpty()) {
        m_knowledgeDirectory = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation)
            + QStringLiteral("/ukui-fences/knowledge");
    }
    m_knowledgeChunkSize = knowledgeChunkSize->value();
    m_knowledgeOverlap = qMin(
        knowledgeOverlap->value(), m_knowledgeChunkSize / 3);
    m_providerConfig = providerEdit->text().trimmed();
    if (indexInputsChanged && m_indexer &&
        m_indexer->state() != QProcess::NotRunning) {
        if (m_idleFullIndex || m_ocrBackfill)
            stopIdleFullIndex();
        else
            m_indexer->terminate();
    }
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
        [this](const SmartSpaceEntry &entry) { return pathExcluded(entry.path); }),
        m_entries.end());
    configureIndexScheduling();
    applyTheme();
    saveSettings();
    if (m_defaultHidden && !previousDefaultHidden && !m_edgeHidden &&
        !m_fenceEmbedded) {
        QTimer::singleShot(0, this, &SmartSpaceWidget::hideToNearestEdge);
    } else if (!m_defaultHidden && previousDefaultHidden && m_edgeHidden) {
        revealFromEdge();
    }
    if (syncKylin->isChecked()) {
        QDBusInterface indexService(
            QStringLiteral("com.ukui.search.fileindex.service"),
            QStringLiteral("/org/ukui/search/fileindex"),
            QStringLiteral("org.ukui.search.fileindex"),
            QDBusConnection::sessionBus());
        if (indexService.isValid()) {
            for (const QString &root : m_roots)
                indexService.call(QStringLiteral("appendSearchDir"), root);
        }
    }
    rebuildWatches();
    if (excludeChanged || visibleFormatsChanged || hiddenChanged)
        updateResults(true);
    if (indexNow->isChecked()) {
        startIndexing();
    } else if (indexInputsChanged) {
        m_statusLabel->setText(QStringLiteral(
            "索引设置已保存 · 未自动扫描，点击更新按钮后生效"));
    }
    if (buildKnowledgeNow->isChecked())
        QTimer::singleShot(0, this, &SmartSpaceWidget::startKnowledgeBuild);
}

void SmartSpaceWidget::setEditMode(bool edit)
{
    m_editMode = edit;
    if (m_sizeGrip)
        m_sizeGrip->setVisible(!m_fenceEmbedded);
    update();
}

void SmartSpaceWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_edgeHidden) {
        if (isWindow()) {
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(rect(), Qt::transparent);
        }
        return;
    }
    if (m_themeMode == 3 && !m_fenceEmbedded) {
        if (isWindow()) {
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(rect(), Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }

        const QRectF materialRect = QRectF(rect()).adjusted(5.0, 5.0,
                                                            -5.0, -5.0);
        QPainterPath path;
        path.addRoundedRect(materialRect, 19, 19);
        painter.save();
        painter.setClipPath(path);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!m_glassBackdrop.isNull())
            painter.drawImage(rect(), m_glassBackdrop);

        QLinearGradient tint(materialRect.topLeft(), materialRect.bottomRight());
        tint.setColorAt(0.0, QColor(255, 255, 255, 24));
        tint.setColorAt(0.28, QColor(255, 255, 255, 6));
        tint.setColorAt(0.68, QColor(5, 9, 15, 34));
        tint.setColorAt(1.0, QColor(3, 6, 11, 68));
        painter.fillPath(path, tint);

        QRadialGradient glow(QPointF(width() * 0.22, height() * 0.04),
                             qMax(width(), height()) * 0.78);
        glow.setColorAt(0.0, QColor(255, 255, 255, 30));
        glow.setColorAt(0.34, QColor(214, 239, 255, 10));
        glow.setColorAt(1.0, QColor(214, 239, 255, 0));
        painter.fillPath(path, glow);
        painter.restore();

        QPainterPath rim;
        rim.setFillRule(Qt::OddEvenFill);
        rim.addRoundedRect(materialRect, 19, 19);
        rim.addRoundedRect(materialRect.adjusted(1.1, 1.1, -1.1, -1.1),
                           17.9, 17.9);
        painter.fillPath(rim, QColor(245, 250, 255, 50));

        if (m_glassPointerActive) {
            const qreal radius = 19.0;
            const QPointF pointer(m_glassPointerPosition);
            const qreal left = materialRect.left();
            const qreal right = materialRect.right();
            const qreal top = materialRect.top();
            const qreal bottom = materialRect.bottom();
            const QPointF candidates[] = {
                QPointF(qBound(left + radius, pointer.x(), right - radius), top),
                QPointF(qBound(left + radius, pointer.x(), right - radius), bottom),
                QPointF(left, qBound(top + radius, pointer.y(), bottom - radius)),
                QPointF(right, qBound(top + radius, pointer.y(), bottom - radius))
            };
            int edge = 0;
            qreal distanceSquared = std::numeric_limits<qreal>::max();
            for (int i = 0; i < 4; ++i) {
                const QPointF delta = pointer - candidates[i];
                const qreal candidateDistance = QPointF::dotProduct(delta, delta);
                if (candidateDistance < distanceSquared) {
                    edge = i;
                    distanceSquared = candidateDistance;
                }
            }

            if (distanceSquared < 52.0 * 52.0) {
                const QPointF edgePoint = candidates[edge];
                QPainterPath causticBand;
                causticBand.setFillRule(Qt::OddEvenFill);
                causticBand.addRoundedRect(materialRect, 19, 19);
                causticBand.addRoundedRect(
                    materialRect.adjusted(13, 13, -13, -13), 6, 6);

                QRadialGradient edgeGlow(edgePoint, 54);
                edgeGlow.setColorAt(0.0, QColor(255, 255, 255, 88));
                edgeGlow.setColorAt(0.32, QColor(192, 232, 255, 48));
                edgeGlow.setColorAt(1.0, QColor(210, 239, 255, 0));
                painter.save();
                painter.setClipPath(causticBand);
                painter.fillPath(causticBand, edgeGlow);

                QPointF start;
                QPointF end;
                QPainterPath caustic;
                if (edge < 2) {
                    const qreal y = edge == 0 ? top + 8 : bottom - 8;
                    const qreal controlY = edge == 0 ? top + 2 : bottom - 2;
                    start = QPointF(edgePoint.x() - 74, y);
                    end = QPointF(edgePoint.x() + 74, y);
                    caustic.moveTo(start);
                    caustic.cubicTo(edgePoint.x() - 38, controlY,
                                    edgePoint.x() + 38, controlY, end.x(), end.y());
                } else {
                    const qreal x = edge == 2 ? left + 8 : right - 8;
                    const qreal controlX = edge == 2 ? left + 2 : right - 2;
                    start = QPointF(x, edgePoint.y() - 74);
                    end = QPointF(x, edgePoint.y() + 74);
                    caustic.moveTo(start);
                    caustic.cubicTo(controlX, edgePoint.y() - 38,
                                    controlX, edgePoint.y() + 38, end.x(), end.y());
                }
                QLinearGradient causticColor(start, end);
                causticColor.setColorAt(0.0, QColor(255, 255, 255, 0));
                causticColor.setColorAt(0.30, QColor(220, 242, 255, 76));
                causticColor.setColorAt(0.50, QColor(255, 255, 255, 178));
                causticColor.setColorAt(0.70, QColor(220, 242, 255, 76));
                causticColor.setColorAt(1.0, QColor(255, 255, 255, 0));
                painter.setPen(QPen(QBrush(causticColor), 2.0,
                                    Qt::SolidLine, Qt::RoundCap));
                painter.drawPath(caustic);
                painter.restore();
            }
        }

        QPainterPath topHighlight;
        topHighlight.moveTo(30, 5.5);
        topHighlight.cubicTo(width() * 0.30, 3.7,
                             width() * 0.68, 3.7, width() - 30, 5.5);
        painter.setPen(QPen(QColor(255, 255, 255, 48), 0.9,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(topHighlight);
        return;
    }
    if (m_fenceEmbedded) {
        painter.fillRect(rect(), m_surfaceColor);
        return;
    }
    if (isWindow()) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    QPainterPath path;
    const int radius = m_edgeHidden ? 13 : 16;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), radius, radius);
    QLinearGradient glass(rect().topLeft(), rect().bottomRight());
    QColor highlight = m_surfaceColor.lighter(112);
    highlight.setAlpha(m_surfaceColor.alpha());
    glass.setColorAt(0.0, highlight);
    glass.setColorAt(0.42, m_surfaceColor);
    glass.setColorAt(1.0, m_surfaceColor.darker(108));
    painter.fillPath(path, glass);
    QColor outline = m_accentColor;
    outline.setAlpha(m_editMode ? 210 : 100);
    painter.setPen(QPen(outline,
                        m_editMode ? 2 : 1));
    painter.drawPath(path);
}

void SmartSpaceWidget::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (!m_edgeHidden)
        m_expandedPosition = pos();
    scheduleGlassBackdropRefresh();
    emit geometryChanged();
}

void SmartSpaceWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateRoundedMask();
    if (m_edgeHidden) {
        if (m_edgeRevealButton)
            m_edgeRevealButton->setGeometry(rect());
        emit geometryChanged();
        return;
    }
    updateResponsiveLayout();
    move(boundedPosition(pos()));
    m_expandedPosition = pos();
    m_expandedSize = size();
    scheduleGlassBackdropRefresh();
    saveSettings();
    emit geometryChanged();
}

void SmartSpaceWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    applyMenuTheme(&menu);
    QAction *pin = menu.addAction(
        m_alwaysOnTop ? QStringLiteral("取消置顶") : QStringLiteral("置顶"));
    QAction *hide = menu.addAction(QStringLiteral("贴边隐藏"));
    menu.addSeparator();
    QAction *refresh = menu.addAction(smartMenuIcon(QStringLiteral("refresh")),
                                      QStringLiteral("更新索引（增量复用）"));
    QAction *settings = menu.addAction(smartMenuIcon(QStringLiteral("settings")),
                                       QStringLiteral("智能空间设置"));
    QAction *idleIndex = menu.addAction(smartMenuIcon(QStringLiteral("fast-full")),
        m_idleFullIndex ? QStringLiteral("快速全量正在运行")
                        : QStringLiteral("快速全量…"));
    QAction *ocrIndex = menu.addAction(smartMenuIcon(QStringLiteral("ocr")),
        m_ocrBackfill ? QStringLiteral("OCR 检测/补全正在运行")
                      : QStringLiteral("OCR 检测与补全…"));
    idleIndex->setEnabled(!m_indexBusy);
    ocrIndex->setEnabled(!m_indexBusy);

    QAction *selected = menu.exec(event->globalPos());
    if (selected == pin)
        setAlwaysOnTop(!m_alwaysOnTop);
    else if (selected == hide)
        hideToNearestEdge();
    else if (selected == refresh)
        startIndexing();
    else if (selected == settings)
        showSettingsDialog();
    else if (selected == idleIndex)
        startIdleFullIndex();
    else if (selected == ocrIndex)
        startOcrIndex();
}

bool SmartSpaceWidget::eventFilter(QObject *watched, QEvent *event)
{
    auto *title = qobject_cast<QLabel *>(watched);
    if (title && title->property("cardName").toBool()) {
        if (event->type() == QEvent::Leave) {
            LiquidPopup::hideText();
        } else if (event->type() == QEvent::ToolTip ||
                   event->type() == QEvent::MouseMove) {
            const QPoint pos = event->type() == QEvent::ToolTip
                ? static_cast<QHelpEvent *>(event)->pos()
                : static_cast<QMouseEvent *>(event)->pos();
            const int textEnd = title->contentsRect().left() +
                title->fontMetrics().horizontalAdvance(title->text());
            if (pos.x() >= textEnd) {
                LiquidPopup::hideText();
                if (event->type() == QEvent::ToolTip)
                    return true;
            }
        }
    }
    if ((watched == m_folderLevelsContainer ||
         (m_folderScroll && watched == m_folderScroll->viewport())) &&
        event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && !m_scopePath.isEmpty()) {
            resetFolderScopeFromBlankClick();
            return true;
        }
    }
    if (watched == m_indexProgress) {
        if ((event->type() == QEvent::Enter || event->type() == QEvent::ToolTip)
            && m_indexProgress && m_indexProgress->isVisible()) {
            LiquidPopup::showText(
                m_indexProgress->mapToGlobal(
                    QPoint(m_indexProgress->width() / 2,
                           m_indexProgress->height() + 4)),
                m_indexProgress->toolTip(), m_indexProgress);
            return event->type() == QEvent::ToolTip;
        }
        if (event->type() == QEvent::Leave)
            LiquidPopup::hideText();
    }
    if (!m_fenceEmbedded && watched == m_header) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOffset = mouse->globalPos() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            move(boundedPosition(mouse->globalPos() - m_dragOffset));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging) {
            m_dragging = false;
            saveSettings();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
