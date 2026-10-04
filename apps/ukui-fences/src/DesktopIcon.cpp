#include "LiquidDialog.h"
#include "DesktopIcon.h"
#include "FenceWidget.h"
#include "DesktopCanvas.h"
#include "FileClipboard.h"
#include "MenuStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QPixmapCache>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QFontMetrics>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QHelpEvent>
#include <QToolTip>
#include "../../../shared/async-work/BackgroundTask.h"

namespace {

constexpr const char *kSystemIconMime = "application/x-kyfences-sysicon";
constexpr const char *kInternalFileDragMime =
    "application/x-ukui-fences-file-drag";

bool pathIsInside(const QString &path, const QString &directory)
{
    const QString cleanPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QString cleanDirectory = QDir::cleanPath(
        QFileInfo(directory).absoluteFilePath());
    if (!cleanDirectory.endsWith(QDir::separator()))
        cleanDirectory += QDir::separator();
    return cleanPath.startsWith(cleanDirectory);
}

QString findExecutable(const QStringList &names)
{
    for (const QString &name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) return path;
    }
    return {};
}

void showPropertiesDialog(const QString &path)
{
    const QString peony = findExecutable({ "peony" });
    if (!peony.isEmpty() &&
        QProcess::startDetached(peony,
            QStringList() << "--show-properties" << path))
        return;

    QProcess::startDetached("gio", QStringList() << "info" << path);
}

QString desktopFileForId(const QString &desktopId)
{
    if (QFile::exists(desktopId)) return desktopId;

    const QString located =
        QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
    if (!located.isEmpty()) return located;

    const QStringList dirs =
        QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dirs) {
        const QString path = QDir(dir).absoluteFilePath(desktopId);
        if (QFile::exists(path)) return path;
    }

    return {};
}

QString desktopFileName(const QString &desktopFile)
{
    QSettings s(desktopFile, QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.beginGroup("Desktop Entry");

    const QString locale = QLocale::system().name();
    const QString lang = locale.left(locale.indexOf('_'));

    QString name = s.value(QString("Name[%1]").arg(locale)).toString();
    if (name.isEmpty()) name = s.value(QString("Name[%1]").arg(lang)).toString();
    if (name.isEmpty()) name = s.value("Name").toString();
    if (name.isEmpty()) name = QFileInfo(desktopFile).completeBaseName();
    return name;
}

QStringList splitCommandCompat(const QString &command)
{
    QStringList parts;
    QString current;
    QChar quote;
    bool escaped = false;

    for (const QChar ch : command) {
        if (escaped) {
            current += ch;
            escaped = false;
        } else if (ch == QLatin1Char('\\')) {
            escaped = true;
        } else if (!quote.isNull()) {
            if (ch == quote)
                quote = QChar();
            else
                current += ch;
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch.isSpace()) {
            if (!current.isEmpty()) {
                parts << current;
                current.clear();
            }
        } else {
            current += ch;
        }
    }

    if (escaped)
        current += QLatin1Char('\\');
    if (!current.isEmpty())
        parts << current;
    return parts;
}

bool launchDesktopFileWithPaths(const QString &desktopFile,
                                const QStringList &paths)
{
    QSettings s(desktopFile, QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.beginGroup("Desktop Entry");
    QString exec = s.value("Exec").toString().trimmed();
    if (exec.isEmpty()) return false;

    QStringList args = splitCommandCompat(exec);
    if (args.isEmpty()) return false;

    bool consumedPath = false;
    QStringList finalArgs;
    for (QString arg : args) {
        if (arg == "%f" || arg == "%u" || arg == "%F" || arg == "%U") {
            finalArgs << paths;
            consumedPath = true;
            continue;
        }

        if (arg == "%i" || arg == "%c" || arg == "%k" || arg == "%d" ||
            arg == "%D" || arg == "%n" || arg == "%N" || arg == "%v" ||
            arg == "%m")
            continue;

        if (arg.contains("%f") || arg.contains("%u")) {
            if (!paths.isEmpty()) {
                arg.replace("%f", paths.first());
                arg.replace("%u", paths.first());
                consumedPath = true;
            } else {
                arg.remove("%f");
                arg.remove("%u");
            }
        }
        arg.remove("%F");
        arg.remove("%U");
        arg.replace("%%", "%");

        if (!arg.isEmpty())
            finalArgs << arg;
    }

    if (!consumedPath)
        finalArgs << paths;
    if (finalArgs.isEmpty()) return false;

    const QString program = finalArgs.takeFirst();
    return QProcess::startDetached(program, finalArgs);
}

QStringList applicationsForMime(const QString &mimeType)
{
    if (mimeType.isEmpty()) return {};

    QProcess proc;
    proc.start("gio", QStringList() << "mime" << mimeType);
    if (!proc.waitForFinished(1200)) return {};

    QString output = QString::fromUtf8(proc.readAllStandardOutput());

    QSet<QString> seen;
    QStringList apps;
    const QRegularExpression desktopIdRe(
        QStringLiteral("([A-Za-z0-9._+\\-]+\\.desktop)"));
    auto matches = desktopIdRe.globalMatch(output);
    while (matches.hasNext()) {
        const QString token = matches.next().captured(1);
        if (seen.contains(token))
            continue;

        seen.insert(token);
        apps.append(token);
    }

    return apps;
}

void addOpenWithMenu(QMenu &menu, const DesktopItem &item)
{
    QMenu *openWith = menu.addMenu(QIcon::fromTheme("document-open"),
                                   "打开方式");
    MenuStyle::applyVenturaContextMenu(openWith);

    auto *loading = openWith->addAction("正在查找应用…"); loading->setEnabled(false);
    BackgroundTask::run(openWith, [mime = item.mimeType] {
        QList<QPair<QString, QString>> found;
        for (const auto &id : applicationsForMime(mime)) {
            const QString file = desktopFileForId(id);
            if (!file.isEmpty()) found.append(qMakePair(file, desktopFileName(file)));
        }
        return found;
    }, [openWith, path = item.filePath](const QList<QPair<QString, QString>> &found) {
        openWith->clear();
        for (const auto &entry : found) {
            auto *action = openWith->addAction(entry.second);
            QObject::connect(action, &QAction::triggered, openWith, [file = entry.first, path] {
                if (!QProcess::startDetached("gio", {"launch", file, path}))
                    launchDesktopFileWithPaths(file, {path});
            });
        }
        if (found.isEmpty()) openWith->addAction("无可用应用")->setEnabled(false);
    });
}

void compressItem(const QString &path)
{
    const QString archiver = findExecutable({ "engrampa", "file-roller" });
    if (archiver.isEmpty()) return;

    const QFileInfo fi(path);
    QProcess::startDetached(archiver,
        QStringList() << "--add"
                      << QString("--default-dir=%1").arg(fi.absolutePath())
                      << path);
}

QString formatBytes(qint64 bytes)
{
    static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double value = qMax<qint64>(0, bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    if (unit == 0)
        return QString("%1 B").arg(bytes);
    return QString("%1 %2").arg(QString::number(value, 'f', value >= 10 ? 1 : 2),
                                units[unit]);
}

QString formatModified(const QFileInfo &fi)
{
    if (!fi.exists()) return "未知";
    return fi.lastModified().toString("yyyy-MM-dd HH:mm");
}

int folderJobs = 0; // GUI thread only; at most two filesystem walks.
QString folderDetails(const QString &path)
{
    QElapsedTimer elapsed; elapsed.start();
    qint64 bytes = 0;
    int visited = 0, direct = 0;
    QStringList preview, pending{path};
    bool partial = false;
    while (!pending.isEmpty() && !partial) {
        const QString dir = pending.takeLast();
        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (it.hasNext()) {
            if (++visited > 1000 || elapsed.elapsed() > 250) { partial = true; break; }
            it.next();
            const QFileInfo info = it.fileInfo();
            if (dir == path) {
                ++direct;
                if (preview.size() < 2) preview << info.fileName();
            }
            if (info.isDir() && !info.isSymLink()) pending << info.absoluteFilePath();
            else bytes += info.size();
        }
    }
    return QString("<div><b>大小：</b>%1%2（%3%4 项）</div><div><b>项目预览：</b>%5</div>")
        .arg(partial ? "至少 " : "", formatBytes(bytes), partial ? "至少 " : "")
        .arg(direct).arg(preview.isEmpty() ? "无" : preview.join("、").toHtmlEscaped());
}

QString htmlLine(const QString &label, const QString &value)
{
    return QString("<div><b>%1</b>%2</div>")
        .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
}

} // namespace

DesktopIcon::DesktopIcon(const DesktopItem &item, QWidget *parent)
    : QWidget(parent), m_item(item)
{
    setVisualScale(1.0);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    updateToolTip();

    // 回收站和普通目录图标均接受文件拖放。
    if ((m_item.isSystemIcon &&
         m_item.filePath == QLatin1String("trash:///")) ||
        (m_item.isDir && !m_item.isSystemIcon))
        setAcceptDrops(true);

    m_folderHoverTimer.setSingleShot(true); m_folderHoverTimer.setInterval(300);
    connect(&m_folderHoverTimer, &QTimer::timeout, this, &DesktopIcon::requestFolderDetails);

    // 点击动画定时器
    m_clickAnimTimer.setInterval(16); // ~60fps
    connect(&m_clickAnimTimer, &QTimer::timeout, [this] {
        m_clickAnimProgress += 0.08;
        if (m_clickAnimProgress >= 1.0) {
            m_clickAnim = false;
            m_clickAnimProgress = 0.0;
            m_clickAnimTimer.stop();
        }
        update();
    });

    m_renameTimer.setSingleShot(true);
    connect(&m_renameTimer, &QTimer::timeout,
            this, [this] { startInlineRename(); });

    m_refreshFeedback.setDuration(180);
    m_refreshFeedback.setStartValue(0.0);
    m_refreshFeedback.setKeyValueAt(0.22, 1.0);
    m_refreshFeedback.setEndValue(0.0);
    connect(&m_refreshFeedback, &QVariantAnimation::valueChanged,
            this, [this] { update(); });

}

DesktopIcon::~DesktopIcon()
{
    // QProcess may emit finished from its destructor, after our m_item has
    // already been destroyed by the time QObject deletes children.
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this); process->kill();
    }
    finishInlineRename(false);
}

void DesktopIcon::setItem(const DesktopItem &item)
{
    m_item = item;
    ++m_folderRevision; m_folderDetails.clear(); m_folderDetailsTime = 0;
    updateToolTip();
    update();
}

void DesktopIcon::setSelected(bool s)
{
    if (m_selected == s) return;
    m_selected = s;
    update();
}

void DesktopIcon::setCut(bool cut)
{
    if (m_cut == cut) return;
    m_cut = cut;
    update();
}

void DesktopIcon::setVisualScale(qreal scale)
{
    m_scale = qBound<qreal>(0.75, scale, 1.75);
    m_iconSize = qRound(ICON_SIZE * m_scale);
    m_cellW = qRound(CELL_W * m_scale);
    m_cellH = qRound(CELL_H * m_scale);
    setFixedSize(m_cellW, m_cellH);
    update();
}

void DesktopIcon::setIconAppearance(IconVisualStyle style, IconSurface surface,
    const QColor &accent, qreal strength)
{
    strength=qBound(0.,strength,1.);
    if (m_visualStyle==style && m_iconSurface==surface && m_liquidAccent==accent
        && qFuzzyCompare(m_liquidStrength+1,strength+1)) return;
    m_visualStyle=style; m_iconSurface=surface;
    m_liquidAccent=accent; m_liquidStrength=strength; update();
}
QRectF DesktopIcon::plateRect() const
{
    const qreal size=m_iconSize+4*m_scale;
    return QRectF((width()-size)/2.,6,size,size);
}
QRectF DesktopIcon::contentIconRect() const
{
    if (m_visualStyle==IconVisualStyle::Native)
        return QRectF((width()-m_iconSize)/2.,6,m_iconSize,m_iconSize);
    const auto plate=plateRect(); const qreal size=m_iconSize*.82;
    return QRectF(plate.center()-QPointF(size/2,size/2),QSizeF(size,size));
}
QPixmap DesktopIcon::dragPixmap() const
{
    const qreal dpr=devicePixelRatioF();
    const bool liquid=m_visualStyle==IconVisualStyle::LiquidPlate;
    const QRectF bounds=liquid ? plateRect().adjusted(-3,-3,3,3) : contentIconRect();
    const QString key=QString("liquid-icon-unit:%1:%2:%3:%4:%5:%6:%7:%8")
        .arg(m_item.icon.cacheKey()).arg(m_iconSize).arg(bounds.width(),0,'f',2).arg(dpr,0,'f',3)
        .arg(m_liquidAccent.rgba()).arg(qRound(m_liquidStrength*100)).arg(liquid).arg(m_cut);
    QPixmap pix;
    if(QPixmapCache::find(key,&pix)) return pix;
    pix=QPixmap(qCeil(bounds.width()*dpr),qCeil(bounds.height()*dpr));
    pix.setDevicePixelRatio(dpr); pix.fill(Qt::transparent);
    QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform); p.translate(-bounds.topLeft());
    if(liquid) LiquidIconRenderer::drawPlate(p,plateRect(),m_liquidAccent,m_liquidStrength,dpr,false,false);
    LiquidIconRenderer::drawIcon(p,contentIconRect(),m_item.icon,dpr);
    if(m_cut) { p.setCompositionMode(QPainter::CompositionMode_DestinationIn); p.fillRect(bounds,QColor(0,0,0,107)); }
    p.end(); QPixmapCache::insert(key,pix);
    return pix;
}

void DesktopIcon::setFontFamily(const QString &family)
{
    m_fontFamily = family;
    update();
}

void DesktopIcon::setFontSize(int size)
{
    m_fontSize = qBound(8, size, 24);
    update();
}

void DesktopIcon::setFontColor(const QColor &color)
{
    m_fontColor = color;
    update();
}

void DesktopIcon::setFontBold(bool bold)
{
    m_fontBold = bold;
    update();
}

void DesktopIcon::setFontItalic(bool italic)
{
    m_fontItalic = italic;
    update();
}

void DesktopIcon::updateToolTip()
{
    if (m_item.isSystemIcon) {
        setToolTip(m_item.displayName.toHtmlEscaped());
        return;
    }

    const QFileInfo fi(m_item.filePath);
    QString tip = "<html><body style=\"white-space:nowrap;\">";
    tip += QString("<div><b>%1</b></div>")
        .arg(m_item.displayName.toHtmlEscaped());

    if (m_item.isDesktopEntry) {
        tip += htmlLine("文件名称：", fi.fileName());
        tip += htmlLine("路径：", m_item.filePath);
    } else if (m_item.isDir) {
        tip += htmlLine("修改日期：", formatModified(fi));
        tip += m_folderDetails.isEmpty() ? htmlLine("详情：", "悬停后按需统计") : m_folderDetails;
    } else {
        QMimeDatabase db;
        const QMimeType mime = db.mimeTypeForFile(m_item.filePath);
        QString type = mime.comment();
        if (type.isEmpty()) type = m_item.mimeType;
        if (type.isEmpty()) type = fi.suffix().isEmpty() ? "文件" : fi.suffix();

        tip += htmlLine("文件类型：", type);
        tip += htmlLine("大小：", formatBytes(fi.size()));
        tip += htmlLine("修改日期：", formatModified(fi));
    }

    tip += "</body></html>";
    setToolTip(tip);
}

bool DesktopIcon::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) requestFolderDetails();
    return QWidget::event(e);
}

void DesktopIcon::requestFolderDetails()
{
    if (!m_item.isDir || m_item.isSystemIcon || m_folderPending || folderJobs >= 2) return;
    if (!m_folderDetails.isEmpty() && QDateTime::currentMSecsSinceEpoch() - m_folderDetailsTime < 30000) return;
    m_folderPending = true; ++folderJobs;
    const auto revision = m_folderRevision;
    const QString path = m_item.filePath;
    const QPointer<DesktopIcon> guard(this);
    BackgroundTask::run(qApp, [path] { return folderDetails(path); },
        [guard, revision, path](const QString &details) {
            --folderJobs;
            if (!guard) return;
            guard->m_folderPending = false;
            if (guard->m_folderRevision != revision || guard->m_item.filePath != path) return;
            guard->m_folderDetails = details;
            guard->m_folderDetailsTime = QDateTime::currentMSecsSinceEpoch();
            guard->updateToolTip();
            if (guard->underMouse() && QToolTip::isVisible())
                QToolTip::showText(QCursor::pos(), guard->toolTip(), guard);
        });
}

QRect DesktopIcon::labelRect() const
{
    QFont font;
    if (!m_fontFamily.isEmpty())
        font.setFamily(m_fontFamily);
    font.setPixelSize(m_fontSize);
    font.setBold(m_fontBold);
    font.setItalic(m_fontItalic);

    const QRect iconRect = (m_visualStyle==IconVisualStyle::LiquidPlate
        ? plateRect() : contentIconRect()).toAlignedRect();
    const QFontMetrics fm(font);
    return QRect(2, iconRect.bottom() + 3,
                 width() - 4,
                 qMin(height() - iconRect.bottom() - 4,
                      fm.lineSpacing() * 3));
}

void DesktopIcon::startInlineRename()
{
    m_renameTimer.stop();
    if (m_item.isSystemIcon || m_renameEdit)
        return;

    const QFileInfo fi(m_item.filePath);
    if (!fi.exists())
        return;

    QWidget *editorParent = window();
    m_renameEdit = new QLineEdit(editorParent);
    m_renameEdit->setText(fi.fileName());
    m_renameEdit->setAlignment(Qt::AlignCenter);
    const QFontMetrics metrics(m_renameEdit->font());
    const int editorWidth = qBound(240,
        metrics.horizontalAdvance(fi.fileName()) + 48, 420);
    const int editorHeight = qMax(32, m_renameEdit->sizeHint().height());
    const QRect label = labelRect();
    QPoint editorPos = mapTo(editorParent,
        QPoint(label.center().x() - editorWidth / 2, label.top()));
    const QRect bounds = editorParent->rect().adjusted(4, 4, -4, -4);
    editorPos.setX(qBound(bounds.left(), editorPos.x(),
                          qMax(bounds.left(), bounds.right() - editorWidth)));
    editorPos.setY(qBound(bounds.top(), editorPos.y(),
                          qMax(bounds.top(), bounds.bottom() - editorHeight)));
    m_renameEdit->setGeometry(QRect(editorPos,
                                    QSize(editorWidth, editorHeight)));
    m_renameEdit->setStyleSheet(
        "QLineEdit {"
        "background: rgba(255,255,255,235);"
        "color: #111;"
        "border: 1px solid #2b7cff;"
        "border-radius: 3px;"
        "padding: 1px 2px;"
        "}");
    m_renameEdit->installEventFilter(this);
    connect(m_renameEdit, &QLineEdit::editingFinished,
            this, [this] { finishInlineRename(true); });

    update();
    m_renameEdit->show();
    m_renameEdit->raise();
    m_renameEdit->setFocus(Qt::MouseFocusReason);

    if (!fi.isDir() && !fi.completeBaseName().isEmpty()) {
        m_renameEdit->setSelection(0, fi.completeBaseName().size());
    } else {
        m_renameEdit->selectAll();
    }
}

void DesktopIcon::finishInlineRename(bool commit)
{
    if (!m_renameEdit || m_finishingRename)
        return;

    m_finishingRename = true;
    auto *edit = m_renameEdit;
    m_renameEdit = nullptr;

    const QString oldPath = m_item.filePath;
    const QFileInfo fi(oldPath);
    const QString oldName = fi.fileName();
    const QString newName = edit->text().trimmed();

    edit->removeEventFilter(this);
    edit->hide();
    edit->deleteLater();
    update();

    if (commit && !newName.isEmpty() && newName != oldName) {
        if (newName.contains('/') || newName.contains('\\')) {
            LiquidDialog::warning(this, "重命名失败", "文件名不能包含路径分隔符。");
        } else {
            const QString newPath = fi.absolutePath() + "/" + newName;
            if (QFileInfo::exists(newPath)) {
                LiquidDialog::warning(this, "重命名失败", "同名项目已存在。");
            } else {
                const bool renamed = fi.isDir()
                    ? QDir().rename(oldPath, newPath)
                    : QFile::rename(oldPath, newPath);
                if (renamed) {
                    DesktopItem item = DesktopItem::fromPath(newPath);
                    if (item.isValid())
                        setItem(item);
                    emit fileRenamed(oldPath, newPath);
                } else {
                    LiquidDialog::warning(this, "重命名失败", "无法重命名该项目。");
                }
            }
        }
    }

    m_finishingRename = false;
}

// ── 绘制 ─────────────────────────────────────────────────

void DesktopIcon::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    // A translucent child must not carry a copied patch of its old wallpaper.
    // Repaint only the previous and new footprints in the parent's coordinates.
    if (isVisible() && parentWidget() && event->oldPos()!=event->pos()) {
        parentWidget()->update(QRegion(QRect(event->oldPos(),size()).adjusted(-2,-2,2,2))
                               | QRegion(geometry().adjusted(-2,-2,2,2)));
        update();
    }
}

void DesktopIcon::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const int w = width(), h = height();

    // ── 悬浮缩放特效（参考特效文件 group-hover:scale-105）──
    qreal drawScale = 1.0;
    if (m_clickAnim) {
        // 点击时先缩小再恢复（scale-90 效果）
        const qreal t = m_clickAnimProgress;
        drawScale = (t < 0.4) ? (1.0 - 0.1 * (t / 0.4))
                               : (0.9 + 0.1 * ((t - 0.4) / 0.6));
    } else if (m_hovered) {
        drawScale = 1.05;
    }

    if (qAbs(drawScale - 1.0) > 0.001) {
        const QPointF pivot = m_visualStyle==IconVisualStyle::LiquidPlate ? plateRect().center() : QPointF(w/2.,h/2.);
        p.translate(pivot);
        p.scale(drawScale, drawScale);
        p.translate(-pivot);
    }

    // 选中 / 悬浮背景（灰色边框）
    if (m_selected || m_hovered) {
        QColor bg = m_selected
            ? QColor(160, 160, 160, 100)
            : QColor(255, 255, 255, 30);
        if (m_visualStyle==IconVisualStyle::LiquidPlate) { bg=m_liquidAccent; bg.setAlpha(m_selected?32:10); }
        p.setBrush(bg);
        p.setPen(m_visualStyle==IconVisualStyle::LiquidPlate ? QPen(Qt::NoPen) : m_selected
            ? QPen(QColor(180, 180, 180, 200), 1)
            : QPen(QColor(255, 255, 255, 40), 1));
        p.drawRoundedRect(1, 1, w - 2, h - 2, 8, 8);
    }

    const bool liquid=m_visualStyle==IconVisualStyle::LiquidPlate;
    const QRect iconRect=(liquid ? plateRect() : contentIconRect()).toAlignedRect();
    if (m_cut) {
        const QRectF bounds=liquid ? plateRect().adjusted(-3,-3,3,3) : contentIconRect();
        p.drawPixmap(bounds.topLeft(),dragPixmap());
    } else {
        if(liquid) LiquidIconRenderer::drawPlate(p,plateRect(),m_liquidAccent,m_liquidStrength,
                                               devicePixelRatioF(),m_hovered,m_selected);
        LiquidIconRenderer::drawIcon(p,contentIconRect(),m_item.icon,devicePixelRatioF());
    }

    // ── 点击散出特效（参考特效文件 ping 动画）──
    if (m_clickAnim) {
        const qreal t = m_clickAnimProgress;
        const qreal expand = 1.0 + t * 0.5;
        const int alpha = qRound(100 * (1.0 - t));
        const int cx = iconRect.center().x();
        const int cy = iconRect.center().y();
        const int rw = qRound(iconRect.width() * expand / 2);
        const int rh = qRound(iconRect.height() * expand / 2);
        p.setPen(Qt::NoPen);
        QColor ripple=liquid ? m_liquidAccent : QColor(Qt::white); ripple.setAlpha(alpha);
        p.setBrush(ripple);
        p.drawRoundedRect(cx - rw, cy - rh, rw * 2, rh * 2, 8, 8);
    }

    // 文件名最多三行，长词也允许换行。
    QFont font;
    if (!m_fontFamily.isEmpty())
        font.setFamily(m_fontFamily);
    font.setPixelSize(m_fontSize);
    font.setBold(m_fontBold);
    font.setItalic(m_fontItalic);
    p.setFont(font);

    const QFontMetrics fm(font);
    const QRect textRect(2, iconRect.bottom() + 3,
                         w - 4, qMin(h - iconRect.bottom() - 4,
                                     fm.lineSpacing() * 3));
    const int flags = Qt::AlignTop | Qt::AlignHCenter |
                      Qt::TextWordWrap | Qt::TextWrapAnywhere;

    if (m_renameEdit)
        return;

    const QColor textColor = m_cut
        ? QColor(210, 210, 210, 155)
        : m_fontColor;

    // Use a contrasting halo: black shadows make dark text look doubled.
    const int luminance = (textColor.red()*299 + textColor.green()*587 + textColor.blue()*114)/1000;
    p.setPen(m_cut ? QColor(0, 0, 0, 80)
                  : luminance < 128 ? QColor(255, 255, 255, 150) : QColor(0, 0, 0, 160));
    p.drawText(textRect.adjusted(1, 1, 1, 1), flags, m_item.displayName);
    // 正文 — 使用配置颜色
    p.setPen(textColor);
    p.drawText(textRect, flags, m_item.displayName);

    if (m_refreshFeedback.state() == QAbstractAnimation::Running) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, qRound(90 * m_refreshFeedback.currentValue().toReal())));
        p.drawRoundedRect(rect(), 8, 8);
    }

}

// ── 鼠标事件 ─────────────────────────────────────────────

void DesktopIcon::triggerRefreshFeedback()
{
    m_refreshFeedback.stop();
    m_refreshFeedback.start();
}

void DesktopIcon::enterEvent(QEvent *) { m_hovered = true; m_folderHoverTimer.start(); update(); }
void DesktopIcon::leaveEvent(QEvent *) { m_hovered = false; m_folderHoverTimer.stop(); update(); }

void DesktopIcon::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const bool shouldRename =
            !m_item.isSystemIcon && m_selected && labelRect().contains(e->pos());

        if (parentWidget())
            parentWidget()->setFocus(Qt::MouseFocusReason);

        m_dragStart = e->pos();
        m_dragGlobalStart = e->globalPos();
        m_widgetStart = pos();
        m_systemDragging = false;
        emit clicked(this, e->modifiers());

        if (shouldRename) {
            m_renameTimer.start(QApplication::doubleClickInterval() + 80);
        } else {
            m_renameTimer.stop();
        }

        // 触发点击动画（参考特效文件的 ping 效果）
        m_clickAnim = true;
        m_clickAnimProgress = 0.0;
        m_clickAnimTimer.start();

        e->accept();
    }
}

void DesktopIcon::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton)) return;
    if ((e->pos() - m_dragStart).manhattanLength()
            < QApplication::startDragDistance()) return;
    m_renameTimer.stop();
    m_clickAnimTimer.stop();m_clickAnim=false;m_clickAnimProgress=0;
    m_hovered=false;update();
    if (m_item.isSystemIcon) {
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setData(kSystemIconMime, m_item.filePath.toUtf8());
        drag->setMimeData(mime);
        const QPixmap preview=dragPixmap();
        drag->setPixmap(preview);
        drag->setHotSpot(QPoint(qRound(preview.width()/preview.devicePixelRatioF()/2),
                               qRound(preview.height()/preview.devicePixelRatioF()/2)));
        drag->exec(Qt::MoveAction);
        emit dragFinished();
        e->accept();
        return;
    }

    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    urls << QUrl::fromLocalFile(m_item.filePath);
    // 父组件可通过 dragStarted 信号添加更多选中的 URL
    emit dragStarted(this, &urls);
    mime->setUrls(urls);
    mime->setData(kInternalFileDragMime, QByteArrayLiteral("1"));
    drag->setMimeData(mime);
    const QPixmap preview=dragPixmap();
    drag->setPixmap(preview);
    drag->setHotSpot(QPoint(qRound(preview.width()/preview.devicePixelRatioF()/2),
                               qRound(preview.height()/preview.devicePixelRatioF()/2)));
    const Qt::DropAction action =
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    emit dragFinished();
    if (action == Qt::MoveAction) {
        QStringList draggedPaths;
        for (const QUrl &url : urls) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty() && !draggedPaths.contains(path))
                draggedPaths << path;
        }

        auto notifyRemoved = [this, draggedPaths] {
            for (const QString &path : draggedPaths) {
                if (!QFileInfo::exists(path))
                    emit fileRemoved(path);
            }
        };
        QTimer::singleShot(350, this, notifyRemoved);
        QTimer::singleShot(1400, this, notifyRemoved);
    }
    e->accept();
}

void DesktopIcon::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_item.isSystemIcon && m_systemDragging) {
        emit systemIconMoved(this, pos());
        m_systemDragging = false;
    }
    e->accept();
}

void DesktopIcon::mouseDoubleClickEvent(QMouseEvent *e)
{
    m_renameTimer.stop();
    if (e->button() == Qt::LeftButton)
        emit activated(m_item);
    e->accept();
}

bool DesktopIcon::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_renameEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            finishInlineRename(false);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return ||
            keyEvent->key() == Qt::Key_Enter) {
            finishInlineRename(true);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

// ── 右键菜单 ─────────────────────────────────────────────

void DesktopIcon::contextMenuEvent(QContextMenuEvent *e)
{
    if (parentWidget())
        parentWidget()->setFocus(Qt::MouseFocusReason);

    QMenu menu;
    MenuStyle::applyVenturaContextMenu(&menu);

    auto *actUndo = menu.addAction(
        QIcon::fromTheme("edit-undo"), "撤回");
    actUndo->setShortcut(QKeySequence::Undo);
    connect(actUndo, &QAction::triggered,
            [this] { emit undoRequested(); });

    menu.addSeparator();

    auto *actOpen = menu.addAction(
        QIcon::fromTheme("document-open"), "打开");
    connect(actOpen, &QAction::triggered,
            [this] { emit activated(m_item); });

    if (!m_item.isSystemIcon)
        addOpenWithMenu(menu, m_item);

    // 打开所在文件夹（非 .desktop）
    if (!m_item.isSystemIcon && !m_item.isDesktopEntry && !m_item.isDir) {
        auto *actFolder = menu.addAction(
            QIcon::fromTheme("folder-open"), "打开所在文件夹");
        connect(actFolder, &QAction::triggered, [this] {
            const QString peony = findExecutable({ "peony" });
            if (!peony.isEmpty()) {
                QProcess::startDetached(peony,
                    QStringList() << "--show-items" << m_item.filePath);
                return;
            }

            QProcess::startDetached("gio",
                QStringList() << "open"
                              << QFileInfo(m_item.filePath).absolutePath());
        });
    }

    if (m_item.isSystemIcon) {
        menu.addSeparator();
        if (m_item.filePath == QLatin1String("trash:///")) {
            auto *actEmptyTrash = menu.addAction(
                QIcon::fromTheme("user-trash"), "清空回收站");
            connect(actEmptyTrash, &QAction::triggered, [this] {
                if (LiquidDialog::question(this, "清空回收站",
                        "确定要永久删除回收站中的所有项目吗？\n此操作无法撤销。")
                    != QMessageBox::Yes)
                    return;

                auto *proc = new QProcess(this);
                connect(proc,
                    QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, proc](int, QProcess::ExitStatus) {
                    refreshTrashIcon();
                    proc->deleteLater();
                });
                proc->start("gio", QStringList() << "trash" << "--empty");
            });
            menu.addSeparator();
        }
        auto *actProp = menu.addAction(
            QIcon::fromTheme("document-properties"), "属性");
        connect(actProp, &QAction::triggered,
                [this] { showPropertiesDialog(m_item.filePath); });
        menu.exec(e->globalPos());
        e->accept();
        return;
    }

    menu.addSeparator();
    auto *actCopy = menu.addAction(
        QIcon::fromTheme("edit-copy"), "复制");
    connect(actCopy, &QAction::triggered,
            [this] { emit copyRequested(this, false); });

    auto *actCut = menu.addAction(
        QIcon::fromTheme("edit-cut"), "剪切");
    connect(actCut, &QAction::triggered,
            [this] { emit copyRequested(this, true); });

    auto *actCopyPath = menu.addAction("复制路径");
    connect(actCopyPath, &QAction::triggered,
            [this] { QApplication::clipboard()->setText(m_item.filePath); });

    // 重命名
    auto *actRename = menu.addAction(
        QIcon::fromTheme("edit-rename"), "重命名");
    connect(actRename, &QAction::triggered,
            [this] { startInlineRename(); });

    menu.addSeparator();

    auto *actCompress = menu.addAction(
        QIcon::fromTheme("package-x-generic"), "压缩…");
    actCompress->setEnabled(!findExecutable({ "engrampa", "file-roller" }).isEmpty());
    connect(actCompress, &QAction::triggered,
            [this] { compressItem(m_item.filePath); });

    menu.addSeparator();

    auto *actDel = menu.addAction(
        QIcon::fromTheme("user-trash"), "移到回收站");
    connect(actDel, &QAction::triggered, [this] {
        if (LiquidDialog::question(this, "确认删除",
                QString("确定要将 \"%1\" 移到回收站吗？").arg(m_item.displayName))
            == QMessageBox::Yes) {
            FileClipboard::trashFilesAsync({m_item.filePath}, this,
                [this](const FileClipboard::PasteResult &result) {
                    if (!result.failedPaths.isEmpty())
                        LiquidDialog::warning(this, "移到回收站失败", "项目未能移到回收站。");
                    if (!result.placedPaths.isEmpty()) emit filesDroppedToTrash(result);
                });
        }
    });

    auto *actPermanentDel = menu.addAction(
        QIcon::fromTheme("edit-delete"), "永久删除");
    connect(actPermanentDel, &QAction::triggered, [this] {
        if (LiquidDialog::warning(this, "永久删除",
                QString("确定永久删除 \"%1\"？\n此操作无法撤销。")
                    .arg(m_item.displayName),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes)
            return;

        const QString oldPath = m_item.filePath;
        FileClipboard::runOperationAsync(this, [oldPath] {
            FileClipboard::PasteResult result;
            const QFileInfo info(oldPath);
            const bool ok=info.isDir() && !info.isSymLink() ? QDir(oldPath).removeRecursively() : QFile::remove(oldPath);
            if (ok) result.placedPaths << oldPath; else result.failedPaths << oldPath;
            return result;
        }, [this, oldPath](const FileClipboard::PasteResult &result) {
            if (!result.failedPaths.isEmpty()) LiquidDialog::warning(this,"删除失败","项目未能删除。");
            else emit fileRemoved(oldPath);
        });
    });

    menu.addSeparator();

    // 属性
    auto *actProp = menu.addAction(
        QIcon::fromTheme("document-properties"), "属性");
    connect(actProp, &QAction::triggered, [this] {
        showPropertiesDialog(m_item.filePath);
    });

    menu.exec(e->globalPos());
    e->accept();
}

// ── 回收站拖放支持 ─────────────────────────────────────

namespace {
FenceWidget *reorderFence(DesktopIcon *icon, const QMimeData *mime, QObject *source)
{
    for(QWidget *parent=icon->parentWidget();parent;parent=parent->parentWidget())
        if(auto *fence=qobject_cast<FenceWidget *>(parent))
            return fence->acceptsIconReorder(mime,source) ? fence : nullptr;
    return nullptr;
}
bool dropsOntoSelf(DesktopIcon *icon,const QMimeData *mime)
{
    if(!mime->hasFormat(kInternalFileDragMime))return false;
    const QFileInfo target(icon->item().filePath);
    for(const auto &url:mime->urls()) {
        const QFileInfo source(url.toLocalFile());
        if(url.isLocalFile() && (source.absoluteFilePath()==target.absoluteFilePath()
            || (!source.canonicalFilePath().isEmpty() && source.canonicalFilePath()==target.canonicalFilePath())))return true;
    }
    return false;
}
}

void DesktopIcon::dragEnterEvent(QDragEnterEvent *e)
{
    if(auto *fence=reorderFence(this,e->mimeData(),e->source())) {
        fence->previewIconReorder(e->mimeData(),fence->mapFromGlobal(mapToGlobal(e->pos())));
        e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    if(dropsOntoSelf(this,e->mimeData())) {
        e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    if (m_item.isSystemIcon && m_item.filePath == QLatin1String("trash:///")) {
        if (e->mimeData()->hasUrls() &&
            !e->mimeData()->hasFormat("application/x-kyfences-sysicon")) {
            e->setDropAction(Qt::MoveAction);
            e->accept();
            m_hovered = true;
            update();
            return;
        }
    }
    if (m_item.isDir && !m_item.isSystemIcon && e->mimeData()->hasUrls()) {
        const Qt::DropAction action =
            e->mimeData()->hasFormat(kInternalFileDragMime)
                ? Qt::MoveAction : e->proposedAction();
        e->setDropAction(action == Qt::IgnoreAction
            ? Qt::MoveAction : action);
        e->accept();
        m_hovered = true;
        update();
        return;
    }
    e->ignore();
}

void DesktopIcon::dragMoveEvent(QDragMoveEvent *e)
{
    if(auto *fence=reorderFence(this,e->mimeData(),e->source())) {
        fence->previewIconReorder(e->mimeData(),fence->mapFromGlobal(mapToGlobal(e->pos())));
        e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    if(dropsOntoSelf(this,e->mimeData())) {
        e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    if (m_item.isSystemIcon && m_item.filePath == QLatin1String("trash:///")) {
        if (e->mimeData()->hasUrls() &&
            !e->mimeData()->hasFormat("application/x-kyfences-sysicon")) {
            e->setDropAction(Qt::MoveAction);
            e->accept();
            return;
        }
    }
    if (m_item.isDir && !m_item.isSystemIcon && e->mimeData()->hasUrls()) {
        const Qt::DropAction action =
            e->mimeData()->hasFormat(kInternalFileDragMime)
                ? Qt::MoveAction : e->proposedAction();
        e->setDropAction(action == Qt::IgnoreAction
            ? Qt::MoveAction : action);
        e->accept();
        return;
    }
    e->ignore();
}

void DesktopIcon::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_hovered=false;update();
    for(QWidget *parent=parentWidget();parent;parent=parent->parentWidget()) {
        if(auto *fence=qobject_cast<FenceWidget *>(parent)) {
            fence->leaveIconReorder();
            break;
        }
    }
    e->accept();
}

void DesktopIcon::dropEvent(QDropEvent *e)
{
    if(auto *fence=reorderFence(this,e->mimeData(),e->source())) {
        m_hovered=false;update();
        // Qt can retain a child drop target while it slides away under the
        // pointer. The last child-local drag position is then stale.
        const QPoint global=e->source()?QCursor::pos():mapToGlobal(e->pos());
        fence->commitIconReorder(e->mimeData(),fence->mapFromGlobal(global));
        e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    if(dropsOntoSelf(this,e->mimeData())) {
        m_hovered=false;update();e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    m_hovered = false;
    QStringList paths;
    for (const auto &url : e->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    bool accepted = false;
    if (m_item.isSystemIcon && m_item.filePath == QLatin1String("trash:///")) {
        accepted = FileClipboard::trashFilesAsync(paths, this,
            [this](const FileClipboard::PasteResult &result) {
                if (!result.failedPaths.isEmpty())
                    LiquidDialog::warning(this, "移到回收站未完成", QString("有 %1 个项目失败或已取消。").arg(result.failedPaths.size()));
                refreshTrashIcon();
                if (!result.placedPaths.isEmpty()) emit filesDroppedToTrash(result);
            });
        e->setDropAction(Qt::MoveAction);
    } else if (m_item.isDir && !m_item.isSystemIcon) {
        Qt::DropAction action = e->mimeData()->hasFormat(kInternalFileDragMime)
            ? Qt::MoveAction : e->proposedAction();
        if (action == Qt::IgnoreAction) action = Qt::MoveAction;
        // The canvas outlives icons rebuilt by file watchers during a transfer.
        for (QWidget *owner = parentWidget(); owner; owner = owner->parentWidget()) {
            if (auto *canvas = qobject_cast<DesktopCanvas *>(owner)) {
                const QPointer<DesktopIcon> guard(this);
                accepted = canvas->transferFilesToFolder(paths, m_item.filePath,
                    action == Qt::MoveAction, [guard] {
                        if (guard) { guard->m_folderDetails.clear(); guard->updateToolTip(); }
                    });
                break;
            }
        }
        e->setDropAction(action);
    }
    if (accepted) e->accept(); else e->ignore();
    update();
}

// ── 回收站图标状态 ─────────────────────────────────────

void DesktopIcon::refreshTrashIcon()
{
    if (!m_item.isSystemIcon || m_item.filePath != QLatin1String("trash:///"))
        return;

    // 异步检查回收站是否为空（避免阻塞 UI）
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int, QProcess::ExitStatus) {
        const QString output = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        if (output.isEmpty()) {
            m_item.icon = QIcon::fromTheme(QLatin1String("user-trash"));
        } else {
            m_item.icon = QIcon::fromTheme(QLatin1String("user-trash-full"));
        }
        update();
        proc->deleteLater();
    });
    proc->start("gio", QStringList() << "list" << "trash:///");
}
