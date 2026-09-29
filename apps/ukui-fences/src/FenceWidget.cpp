#include "FenceWidget.h"
#include "DesktopIcon.h"
#include "DesktopCanvas.h"
#include "FileClipboard.h"
#include "MenuStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDateTime>
#include <QMimeData>
#include <QUrl>
#include <QDrag>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QColorDialog>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QPropertyAnimation>
#include <QWheelEvent>
#include <QUuid>
#include <QStringList>
#include <QKeyEvent>
#include <QProcess>
#include <QCoreApplication>
#include <QTimer>
#include <QCursor>
#include <QRadialGradient>
#include <algorithm>

#include <QFontDatabase>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QRegion>
#include <QtMath>

namespace {

constexpr const char *kInternalFileDragMime =
    "application/x-ukui-fences-file-drag";

QString normalizedStoredPath(const QString &path)
{
    if (path.startsWith(QLatin1String("computer://")) ||
        path.startsWith(QLatin1String("trash://")))
        return path;

    return QFileInfo(path).absoluteFilePath();
}

bool sameStoredPath(const QString &a, const QString &b)
{
    return normalizedStoredPath(a) == normalizedStoredPath(b);
}

void addExistingDirectory(QStringList &paths, const QString &path)
{
    if (path.trimmed().isEmpty())
        return;

    const QString clean = QDir::cleanPath(path);
    if (!clean.isEmpty() && QFileInfo(clean).isDir() && !paths.contains(clean))
        paths.append(clean);
}

QString expandedUserDirValue(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2);
    if (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
        value = value.mid(1, value.size() - 2);
    value.replace(QStringLiteral("$HOME"), QDir::homePath());
    value.replace(QStringLiteral("${HOME}"), QDir::homePath());
    return value;
}

QString primaryDesktopDirectory()
{
    QStringList paths;
    addExistingDirectory(paths,
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));

    QFile file(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
               + "/user-dirs.dirs");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1String("XDG_DESKTOP_DIR="))) {
                addExistingDirectory(paths,
                    expandedUserDirValue(line.section(QLatin1Char('='), 1)));
                break;
            }
        }
    }

    addExistingDirectory(paths, QDir::homePath() + "/桌面");
    addExistingDirectory(paths, QDir::homePath() + "/Desktop");
    return paths.isEmpty() ? QDir::homePath() : paths.first();
}

} // namespace

// ── 构造 ─────────────────────────────────────────────────

FenceWidget::FenceWidget(const QString &title,
                         const QRect   &geo,
                         QWidget       *parent)
    : QWidget(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
    , m_title(title)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(geo);
    m_iconViewport = new QWidget(this);
    m_iconViewport->setAttribute(Qt::WA_TranslucentBackground);
    m_iconViewport->setAutoFillBackground(false);
    m_iconViewport->setMouseTracking(true);
    m_iconViewport->installEventFilter(this);
    m_iconViewport->show();
    m_expandedH = qMax(TITLE_H + 60, geo.height());
    m_glassHoverTimer = new QTimer(this);
    m_glassHoverTimer->setInterval(33);
    connect(m_glassHoverTimer, &QTimer::timeout, this, [this] {
        const QPointF pos = mapFromGlobal(QCursor::pos());
        const bool inside = m_glassPointerPresent && isVisible() && m_liquidGlassEnabled && rect().contains(pos.toPoint())
            && (mask().isEmpty() || mask().contains(pos.toPoint()));
        const qreal target = inside ? 1.0 : 0.0;
        const qreal next = qAbs(target-m_glassHover) < 0.015 ? target : m_glassHover+(target-m_glassHover)*0.25;
        if (pos != m_glassPointer || next != m_glassHover) {
            m_glassPointer = pos;
            m_glassHover = next;
            update();
        }
        if (!inside && m_glassHover == 0) m_glassHoverTimer->stop();
    });
}

void FenceWidget::setLiquidGlassEnabled(bool enabled)
{
    m_liquidGlassEnabled = enabled;
    m_glassPointerPresent = enabled && underMouse();
    if (!enabled) {
        m_glassHoverTimer->stop();
        m_glassHover = 0;
    } else if (underMouse()) {
        m_glassHoverTimer->start();
    }
    updateShapeMask();
    invalidateGlassCache();
}

void FenceWidget::invalidateGlassCache()
{
    m_glassImage = {};
    m_glassGeometry = {};
    update();
}

void FenceWidget::enterEvent(QEvent *event)
{
    m_glassPointerPresent = true;
    if (m_liquidGlassEnabled) m_glassHoverTimer->start();
    QWidget::enterEvent(event);
}

void FenceWidget::leaveEvent(QEvent *event)
{
    m_glassPointerPresent = false;
    // Fade out; timeout also tracks movement over child icons without tooltips.
    if (m_liquidGlassEnabled) m_glassHoverTimer->start();
    QWidget::leaveEvent(event);
}

// ── 属性设置 ─────────────────────────────────────────────

void FenceWidget::setTitle(const QString &t)
{
    m_title = t;
    update();
    emit titleChanged(t);
}

void FenceWidget::setFenceColor(const QColor &c)
{
    m_color = c;
    update();
}

void FenceWidget::setLocked(bool locked)
{
    m_locked = locked;
    update();
}

void FenceWidget::setIconScale(qreal scale)
{
    m_iconScale = qBound<qreal>(0.75, scale, 1.75);
    for (auto *icon : m_icons)
        icon->setVisualScale(m_iconScale);
    layoutIcons();
}

void FenceWidget::setIconFontFamily(const QString &family)
{
    m_inheritedIconFontFamily = family;
    if (m_hasLocalFont) return; // 有独立配置时不被全局覆盖
    for (auto *icon : m_icons)
        icon->setFontFamily(family);
}

void FenceWidget::setIconFontSize(int size)
{
    m_inheritedIconFontSize = qBound(8, size, 24);
    if (m_hasLocalFont) return;
    for (auto *icon : m_icons)
        icon->setFontSize(m_inheritedIconFontSize);
}

void FenceWidget::setIconFontColor(const QColor &color)
{
    m_inheritedIconFontColor = color;
    if (m_hasLocalFont) return;
    for (auto *icon : m_icons)
        icon->setFontColor(color);
}

void FenceWidget::setIconFontBold(bool bold)
{
    m_inheritedIconFontBold = bold;
    if (m_hasLocalFont) return;
    for (auto *icon : m_icons)
        icon->setFontBold(bold);
}

void FenceWidget::setIconFontItalic(bool italic)
{
    m_inheritedIconFontItalic = italic;
    if (m_hasLocalFont) return;
    for (auto *icon : m_icons)
        icon->setFontItalic(italic);
}

void FenceWidget::setTitleIconPath(const QString &path)
{
    m_titleIconPath = path;
    if (!path.isEmpty() && QFile::exists(path))
        m_titleIcon = QIcon(path);
    else
        m_titleIcon = QIcon();
    update();
    emit geometryChanged();
}

void FenceWidget::setTitleTextColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_hasTitleFont = true;
    m_titleFontColor = color;
    update();
}

void FenceWidget::setEditMode(bool edit)
{
    m_editMode = edit;
    // 编辑模式下图标穿透鼠标事件，让 Fence 可整体拖动
    for (auto *ic : m_icons)
        ic->setAttribute(Qt::WA_TransparentForMouseEvents, edit);
    if (m_embeddedWidget)
        m_embeddedWidget->setAttribute(Qt::WA_TransparentForMouseEvents, edit);
    setCursor(edit ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

void FenceWidget::setEmbeddedWidget(QWidget *widget)
{
    if (m_embeddedWidget == widget) return;
    if (m_embeddedWidget)
        m_embeddedWidget->deleteLater();
    m_embeddedWidget = widget;
    if (!widget) {
        m_iconViewport->show();
        layoutIcons();
        return;
    }
    widget->setParent(this);
    m_iconViewport->hide();
    for (DesktopIcon *icon : m_icons) icon->hide();
    widget->setGeometry(MARGIN, TITLE_H + 4,
                        qMax(0, width() - MARGIN * 2),
                        qMax(0, height() - TITLE_H - 4 - MARGIN));
    widget->setVisible(!m_collapsed);
    widget->raise();
    update();
}

int FenceWidget::expandedHeight() const
{
    if (!m_collapsed && (!m_collapseAnimation ||
        m_collapseAnimation->state() != QAbstractAnimation::Running))
        return qMax(TITLE_H + 60, height());
    return qMax(TITLE_H + 60, m_expandedH);
}

void FenceWidget::setCollapsed(bool c)
{
    if (m_collapsed == c) return;
    const int restoredHeight = expandedHeight();
    if (!m_collapseAnimation) {
        m_collapseAnimation = new QPropertyAnimation(this, "geometry", this);
        m_collapseAnimation->setDuration(160);
        m_collapseAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_collapseAnimation, &QPropertyAnimation::finished, this, [this] {
            updateShapeMask();
            layoutIcons();
            emit geometryChanged();
        });
    }
    // A second click reverses the existing transition instead of leaving two
    // geometry animations (and stale completion callbacks) fighting each other.
    m_collapseAnimation->stop();
    m_expandedH = restoredHeight;
    m_collapsed = c;
    updateShapeMask();
    layoutIcons();
    m_collapseAnimation->setStartValue(geometry());
    m_collapseAnimation->setEndValue(
        QRect(x(), y(), width(), c ? TITLE_H : m_expandedH));
    m_collapseAnimation->start();
    update();
}

// ── 图标管理 ─────────────────────────────────────────────

void FenceWidget::applyInheritedIconFont(DesktopIcon *icon)
{
    if (!icon) return;
    icon->setFontFamily(m_inheritedIconFontFamily);
    icon->setFontSize(m_inheritedIconFontSize);
    icon->setFontColor(m_inheritedIconFontColor);
    icon->setFontBold(m_inheritedIconFontBold);
    icon->setFontItalic(m_inheritedIconFontItalic);
}

void FenceWidget::applyLocalIconFont(DesktopIcon *icon)
{
    if (!icon) return;
    icon->setFontFamily(m_localFontFamily);
    icon->setFontSize(m_localFontSize);
    icon->setFontColor(m_localFontColor);
    icon->setFontBold(m_localFontBold);
    icon->setFontItalic(m_localFontItalic);
}

void FenceWidget::applyEffectiveIconFont(DesktopIcon *icon)
{
    if (m_hasLocalFont)
        applyLocalIconFont(icon);
    else
        applyInheritedIconFont(icon);
}

void FenceWidget::setIconSelected(DesktopIcon *icon, bool selected)
{
    if (!icon) return;
    icon->setSelected(selected);
    if (selected)
        m_selectedIcons.insert(icon);
    else
        m_selectedIcons.remove(icon);
}

void FenceWidget::clearIconSelection()
{
    const auto icons = m_selectedIcons.values();
    for (auto *icon : icons)
        if (icon) icon->setSelected(false);
    m_selectedIcons.clear();
}

void FenceWidget::selectAllIcons()
{
    clearIconSelection();
    for (auto *icon : m_icons)
        setIconSelected(icon, true);
}

QStringList FenceWidget::selectedFilePaths() const
{
    QStringList paths;
    for (auto *icon : m_selectedIcons) {
        if (!icon || icon->item().isSystemIcon) continue;
        paths << icon->item().filePath;
    }
    return paths;
}

void FenceWidget::copySelectedIcons(bool move)
{
    const QStringList paths = selectedFilePaths();
    if (!paths.isEmpty())
        FileClipboard::writeFiles(paths, move);
}

void FenceWidget::pasteClipboardFiles()
{
    const QString desktopPath = primaryDesktopDirectory();
    FileClipboard::pasteFilesToDirectoryAsync(desktopPath,this,
        [this](const FileClipboard::PasteResult &result) {

    if (!result.hadFiles())
        return;

    if (!result.failedPaths.isEmpty()) {
        QMessageBox::warning(this, "粘贴失败",
            QString("有 %1 个项目无法粘贴到分区。").arg(result.failedPaths.size()));
    }

    for (const QString &path : result.placedPaths) {
        DesktopItem item = DesktopItem::fromPath(path);
        if (!item.isValid()) continue;
        addItem(item);
        emit fileDropped(item.filePath);
    }

    if (result.hasPlacedFiles())
        emit filesPasted(result.placedSourcePaths,
                         result.transferredPaths,
                         result.move);

    emit geometryChanged();
    });
}

void FenceWidget::finishNewItem(const QString &path)
{
    const DesktopItem item = DesktopItem::fromPath(path);
    if (!item.isValid())
        return;

    addItem(item);
    DesktopIcon *createdIcon = nullptr;
    for (DesktopIcon *icon : m_icons) {
        if (icon && sameStoredPath(icon->item().filePath, path)) {
            createdIcon = icon;
            break;
        }
    }
    if (!createdIcon)
        return;

    clearIconSelection();
    setIconSelected(createdIcon, true);
    layoutIcons();
    emit fileCreated(path);
    emit geometryChanged();

    QTimer::singleShot(0, createdIcon, [createdIcon] {
        createdIcon->startInlineRename();
    });
}

void FenceWidget::createNewFile(const QString &baseName,
                                const QString &suffix)
{
    QDir dir(primaryDesktopDirectory());
    const QString normalizedSuffix = suffix.startsWith('.')
        ? suffix : QStringLiteral(".") + suffix;
    QString name = baseName + normalizedSuffix;
    int n = 1;
    while (dir.exists(name)) {
        name = QStringLiteral("%1 (%2)%3")
                   .arg(baseName).arg(n++).arg(normalizedSuffix);
    }

    const QString path = dir.absoluteFilePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "新建失败",
                             QString("无法创建 %1。").arg(name));
        return;
    }
    file.close();
    finishNewItem(path);
}

void FenceWidget::createNewDirectory()
{
    QDir dir(primaryDesktopDirectory());
    QString name = QStringLiteral("新建文件夹");
    int n = 1;
    while (dir.exists(name))
        name = QStringLiteral("新建文件夹 (%1)").arg(n++);

    if (!dir.mkdir(name)) {
        QMessageBox::warning(this, "新建失败",
                             QString("无法创建 %1。").arg(name));
        return;
    }
    finishNewItem(dir.absoluteFilePath(name));
}

void FenceWidget::trashSelectedIcons()
{
    const QStringList paths = selectedFilePaths();
    if (paths.isEmpty()) return;

    if (QMessageBox::question(this, "移到回收站",
            QString("确定要将选中的 %1 个项目移到回收站吗？").arg(paths.size()))
        != QMessageBox::Yes)
        return;

    FileClipboard::trashFilesAsync(paths, this, [this](const FileClipboard::PasteResult &result) {
        for (const auto &path : result.placedPaths) removeItem(path);
        if (!result.failedPaths.isEmpty())
            QMessageBox::warning(this, "移到回收站未完成", QString("有 %1 个项目失败或已取消。").arg(result.failedPaths.size()));
        if (!result.placedPaths.isEmpty()) emit filesTrashed(result.placedPaths);
        emit geometryChanged();
    });
}

void FenceWidget::renameFirstSelectedIcon()
{
    if (m_selectedIcons.isEmpty()) return;

    DesktopIcon *icon = *m_selectedIcons.constBegin();
    if (!icon || icon->item().isSystemIcon) return;
    icon->startInlineRename();
}

void FenceWidget::openSelectedIcons()
{
    for (auto *icon : m_selectedIcons)
        if (icon) icon->item().open();
}

void FenceWidget::syncCutVisualState(const QSet<QString> &cutPaths)
{
    for (auto *icon : m_icons) {
        if (!icon) continue;
        const QString path = normalizedStoredPath(icon->item().filePath);
        icon->setCut(!icon->item().isSystemIcon && cutPaths.contains(path));
    }
}

void FenceWidget::addItem(const DesktopItem &item)
{
    insertItem(item, m_icons.size());
}

void FenceWidget::insertItem(const DesktopItem &item, int index)
{
    if (hasItem(item.filePath)) return;

    auto *icon = new DesktopIcon(item, m_iconViewport ? m_iconViewport : this);
    if (auto *canvas=qobject_cast<DesktopCanvas *>(parentWidget()))
        canvas->configureIconAppearance(icon,IconSurface::Fence);
    else
        icon->setIconAppearance(IconVisualStyle::Native,IconSurface::Fence,QColor("#7faee8"),.4);
    icon->setVisualScale(m_iconScale);
    connect(icon, &DesktopIcon::activated,
            [](const DesktopItem &it) { it.open(); });
    connect(icon, &DesktopIcon::undoRequested,
            this, [this] { emit undoRequested(); });
    connect(icon, &DesktopIcon::clicked,
            this, [this](DesktopIcon *self, Qt::KeyboardModifiers mods) {
        if (!self) return;
        setFocus(Qt::MouseFocusReason);
        emit interactionStarted(this);

        if (mods & Qt::ControlModifier) {
            setIconSelected(self, !self->isSelected());
        } else if (self->isSelected() && m_selectedIcons.size() > 1) {
            // 保留多选状态，允许直接拖动一组已选图标。
        } else {
            clearIconSelection();
            setIconSelected(self, true);
        }
    });
    connect(icon, &DesktopIcon::copyRequested,
            this, [this](DesktopIcon *self, bool move) {
        if (!self) return;
        setFocus(Qt::MouseFocusReason);
        emit interactionStarted(this);
        if (!self->isSelected()) {
            clearIconSelection();
            setIconSelected(self, true);
        }
        copySelectedIcons(move);
    });
    connect(icon, &DesktopIcon::dragStarted,
            this, [this](DesktopIcon *, QList<QUrl> *urls) {
        if (!urls || m_selectedIcons.size() <= 1) return;
        for (auto *selected : m_selectedIcons) {
            if (!selected || selected->item().isSystemIcon) continue;
            const QUrl url = QUrl::fromLocalFile(selected->item().filePath);
            if (!urls->contains(url))
                urls->append(url);
        }
    });
    connect(icon, &DesktopIcon::fileRemoved,
            this, [this](const QString &path) {
        removeItem(path);
        emit geometryChanged();
    });
    connect(icon, &DesktopIcon::fileRenamed,
            this, [this, icon](const QString &oldPath, const QString &newPath) {
        emit fileRenamedForUndo(oldPath, newPath);
        DesktopItem item = DesktopItem::fromPath(newPath);
        if (item.isValid())
            icon->setItem(item);
        emit geometryChanged();
    });
    connect(icon, &DesktopIcon::filesDroppedToTrash,
            this, [this](const QStringList &paths) {
        for (const QString &path : paths)
            removeItem(path);
        emit filesTrashed(paths);
        emit geometryChanged();
    });
    m_icons.insert(qBound(0, index, m_icons.size()), icon);
    applyEffectiveIconFont(icon);
    if (item.isSystemIcon && item.filePath == QLatin1String("trash:///"))
        icon->refreshTrashIcon();
    icon->setVisible(!m_collapsed);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents, m_editMode);
    layoutIcons();
    QSet<QString> cutPaths;
    for (const QString &path : FileClipboard::currentCutPaths())
        cutPaths.insert(QFileInfo(path).absoluteFilePath());
    syncCutVisualState(cutPaths);
}

int FenceWidget::dropInsertionIndex(const QPoint &pos) const
{
    const int count = m_icons.size();
    if (count == 0)
        return 0;

    const int iconW = qRound(ICON_W * m_iconScale);
    const int iconH = qRound(ICON_H * m_iconScale);
    const int stepX = iconW + ICON_GAP;
    const int stepY = iconH + ICON_GAP;
    const int leftInset = magneticContentInset();
    const int cols = iconColumnCount(iconW);

    const int contentY = pos.y() - contentTop() + m_scrollOffset;
    if (contentY < 0)
        return 0;

    const int row = qMax(0, contentY / stepY);
    const int contentX = pos.x() - leftInset;
    if (contentX < 0)
        return qBound(0, row * cols, count);

    const int col = qBound(0, contentX / stepX, cols - 1);
    const int withinX = contentX - col * stepX;
    int index = row * cols + col;
    if (withinX > iconW / 2)
        ++index;

    return qBound(0, index, count);
}

bool FenceWidget::moveItemsToIndex(const QStringList &paths, int targetIndex)
{
    QSet<QString> normalizedPaths;
    for (const QString &path : paths) {
        const QString normalized = normalizedStoredPath(path);
        if (!normalized.isEmpty())
            normalizedPaths.insert(normalized);
    }
    if (normalizedPaths.isEmpty())
        return false;

    QList<DesktopIcon *> movingIcons;
    int adjustedTarget = qBound(0, targetIndex, m_icons.size());
    for (int i = 0; i < m_icons.size(); ++i) {
        DesktopIcon *icon = m_icons[i];
        if (!icon)
            continue;

        if (normalizedPaths.contains(normalizedStoredPath(icon->item().filePath))) {
            movingIcons.append(icon);
            if (i < adjustedTarget)
                --adjustedTarget;
        }
    }
    if (movingIcons.isEmpty())
        return false;

    const QList<DesktopIcon *> before = m_icons;
    for (auto *icon : movingIcons)
        m_icons.removeAll(icon);

    adjustedTarget = qBound(0, adjustedTarget, m_icons.size());
    for (auto *icon : movingIcons)
        m_icons.insert(adjustedTarget++, icon);

    if (m_icons == before)
        return false;

    layoutIcons();
    emit geometryChanged();
    return true;
}

void FenceWidget::removeItem(const QString &filePath)
{
    for (int i = 0; i < m_icons.size(); ++i) {
        if (sameStoredPath(m_icons[i]->item().filePath, filePath)) {
            m_selectedIcons.remove(m_icons[i]);
            m_icons[i]->deleteLater();
            m_icons.removeAt(i);
            layoutIcons();
            return;
        }
    }
}

bool FenceWidget::hasItem(const QString &filePath) const
{
    for (auto *ic : m_icons)
        if (sameStoredPath(ic->item().filePath, filePath)) return true;
    return false;
}

QList<DesktopItem> FenceWidget::items() const
{
    QList<DesktopItem> list;
    for (auto *ic : m_icons) list << ic->item();
    return list;
}

// ── 图标布局 ─────────────────────────────────────────────

int FenceWidget::contentTop() const
{
    return TITLE_H + CONTENT_TOP_GAP;
}

int FenceWidget::contentViewportHeight() const
{
    return qMax(0, height() - contentTop() - CONTENT_BOTTOM_GAP);
}

int FenceWidget::contentRightInset() const
{
    return MARGIN + SCROLLBAR_RESERVED_W;
}

int FenceWidget::iconColumnCount(int iconW) const
{
    const int stepX = iconW + ICON_GAP;
    const int usableW = width() - magneticContentInset() - contentRightInset();
    return qMax(1, (usableW + ICON_GAP) / stepX);
}

bool FenceWidget::iconBelongsToThisFence(DesktopIcon *icon) const
{
    return icon && (icon->parentWidget() == this ||
                    icon->parentWidget() == m_iconViewport);
}

void FenceWidget::layoutIcons()
{
    if (m_embeddedWidget) {
        m_iconViewport->hide();
        m_embeddedWidget->setGeometry(
            MARGIN, TITLE_H + 4,
            qMax(0, width() - MARGIN * 2),
            qMax(0, height() - TITLE_H - 4 - MARGIN));
        m_embeddedWidget->setVisible(!m_collapsed);
        update();
        return;
    }
    const int iconW = qRound(ICON_W * m_iconScale);
    const int iconH = qRound(ICON_H * m_iconScale);
    const int leftInset = magneticContentInset();
    const int cols = iconColumnCount(iconW);
    const int rows = m_icons.isEmpty()
        ? 0
        : (m_icons.size() + cols - 1) / cols;
    m_contentHeight = rows > 0
        ? rows * iconH + (rows - 1) * ICON_GAP
        : 0;
    clampScrollOffset();

    const int top = contentTop();
    const int viewportH = contentViewportHeight();
    if (m_iconViewport) {
        m_iconViewport->setGeometry(0, top, width(), viewportH);
        m_iconViewport->setVisible(!m_collapsed && viewportH > 0);
    }

    const QRect visibleRect(0, 0, width(), viewportH);
    for (int i = 0; i < m_icons.size(); ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const QPoint pos(
            leftInset + col * (iconW + ICON_GAP),
            row * (iconH + ICON_GAP) - m_scrollOffset);
        m_icons[i]->move(pos);
        const QRect iconRect(pos, m_icons[i]->size());
        m_icons[i]->setVisible(!m_collapsed &&
                               visibleRect.intersects(iconRect));
    }
    update();
}

void FenceWidget::resizeEvent(QResizeEvent *)
{
    updateShapeMask();
    if (m_embeddedWidget) {
        m_embeddedWidget->setGeometry(
            MARGIN, TITLE_H + 4,
            qMax(0, width() - MARGIN * 2),
            qMax(0, height() - TITLE_H - 4 - MARGIN));
        return;
    }
    layoutIcons();
}

bool FenceWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_iconViewport || !m_iconViewport)
        return QWidget::eventFilter(watched, event);

    auto toFencePos = [this](const QPoint &pos) {
        return m_iconViewport->pos() + pos;
    };
    auto toFencePosF = [this](const QPointF &pos) {
        return QPointF(m_iconViewport->pos()) + pos;
    };

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QMouseEvent forwarded(
            mouse->type(),
            toFencePosF(mouse->localPos()),
            mouse->windowPos(),
            mouse->screenPos(),
            mouse->button(),
            mouse->buttons(),
            mouse->modifiers());
        QCoreApplication::sendEvent(this, &forwarded);
        event->setAccepted(forwarded.isAccepted());
        return forwarded.isAccepted();
    }
    case QEvent::ContextMenu: {
        auto *context = static_cast<QContextMenuEvent *>(event);
        QContextMenuEvent forwarded(
            context->reason(),
            toFencePos(context->pos()),
            context->globalPos(),
            context->modifiers());
        QCoreApplication::sendEvent(this, &forwarded);
        event->setAccepted(forwarded.isAccepted());
        return forwarded.isAccepted();
    }
    case QEvent::Wheel: {
        auto *wheel = static_cast<QWheelEvent *>(event);
        QWheelEvent forwarded(
            toFencePosF(wheel->posF()),
            wheel->globalPosF(),
            wheel->pixelDelta(),
            wheel->angleDelta(),
            wheel->buttons(),
            wheel->modifiers(),
            wheel->phase(),
            wheel->inverted(),
            wheel->source());
        QCoreApplication::sendEvent(this, &forwarded);
        event->setAccepted(forwarded.isAccepted());
        return forwarded.isAccepted();
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void FenceWidget::clampScrollOffset()
{
    const int viewportH = contentViewportHeight();
    const int maxOffset = qMax(0, m_contentHeight - viewportH);
    m_scrollOffset = qBound(0, m_scrollOffset, maxOffset);
}

void FenceWidget::wheelEvent(QWheelEvent *e)
{
    if (m_collapsed || m_contentHeight <= contentViewportHeight()) {
        QWidget::wheelEvent(e);
        return;
    }

    m_scrollOffset -= e->angleDelta().y() / 3;
    clampScrollOffset();
    layoutIcons();
    e->accept();
}

// ── 绘制 ─────────────────────────────────────────────────

void FenceWidget::drawHandles(QPainter &p)
{
    // 8个缩放控制点：4角 + 4边中点
    const int w = width(), h = height();
    const QList<QPoint> pts = {
        {0, 0}, {w/2, 0}, {w, 0},
        {0, h/2},          {w, h/2},
        {0, h}, {w/2, h},  {w, h}
    };
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(0, 120, 215), 1.5));
    for (const QPoint &pt : pts)
        p.drawEllipse(pt, HANDLE_R, HANDLE_R);
}

// ── 吸附对齐计算 ──────────────────────────────────────────

QRect FenceWidget::computeSnappedGeometry(const QRect &proposed) const
{
    m_snapGuides.clear();

    auto *canvas = parentWidget();
    if (!canvas) return proposed;

    // 收集所有兄弟 Fence 的矩形
    QList<QRect> siblingRects;
    const auto children = canvas->findChildren<FenceWidget *>();
    for (auto *sibling : children) {
        if (sibling == this || !sibling->isVisible())
            continue;
        siblingRects.append(sibling->geometry());
    }

    if (siblingRects.isEmpty()) return proposed;

    // 屏幕边缘也作为吸附目标
    const QRect screenRect = canvas->rect();
    siblingRects.append(screenRect);

    QRect snapped = proposed;

    // 对齐候选：当前矩形的 left/right/top/bottom/centerX/centerY
    // 与目标矩形的 left/right/top/bottom/centerX/centerY 比较

    struct SnapCandidate {
        int delta;       // 需要偏移多少
        int guidePos;    // 辅助线的坐标值
        bool horizontal; // 辅助线是水平(true)还是垂直(false)
        int guideStart;  // 辅助线起点（另一轴）
        int guideEnd;    // 辅助线终点（另一轴）
    };

    int bestDx = SNAP_THRESHOLD + 1;
    int bestDy = SNAP_THRESHOLD + 1;
    QList<SnapCandidate> xCandidates, yCandidates;

    for (const QRect &sr : siblingRects) {
        // ── 水平方向吸附（调整 X）──
        const int myEdgesX[] = { proposed.left(), proposed.right(),
                                 proposed.center().x() };
        const int targetEdgesX[] = { sr.left(), sr.right(),
                                     sr.center().x() };

        for (int myX : myEdgesX) {
            for (int tX : targetEdgesX) {
                int dx = tX - myX;
                if (qAbs(dx) <= SNAP_THRESHOLD && qAbs(dx) <= qAbs(bestDx)) {
                    if (qAbs(dx) < qAbs(bestDx))
                        xCandidates.clear();
                    bestDx = dx;
                    // 垂直辅助线
                    int gStart = qMin(proposed.top(), sr.top());
                    int gEnd   = qMax(proposed.bottom(), sr.bottom());
                    xCandidates.append({dx, tX, false, gStart, gEnd});
                }
            }
        }

        // ── 垂直方向吸附（调整 Y）──
        const int myEdgesY[] = { proposed.top(), proposed.bottom(),
                                 proposed.center().y() };
        const int targetEdgesY[] = { sr.top(), sr.bottom(),
                                     sr.center().y() };

        for (int myY : myEdgesY) {
            for (int tY : targetEdgesY) {
                int dy = tY - myY;
                if (qAbs(dy) <= SNAP_THRESHOLD && qAbs(dy) <= qAbs(bestDy)) {
                    if (qAbs(dy) < qAbs(bestDy))
                        yCandidates.clear();
                    bestDy = dy;
                    // 水平辅助线
                    int gStart = qMin(proposed.left(), sr.left());
                    int gEnd   = qMax(proposed.right(), sr.right());
                    yCandidates.append({dy, tY, true, gStart, gEnd});
                }
            }
        }
    }

    // 应用吸附偏移
    if (qAbs(bestDx) <= SNAP_THRESHOLD) {
        if (m_dragging || (m_resizeEdge & EL)) {
            snapped.translate(bestDx, 0);
        } else if (m_resizeEdge & ER) {
            snapped.setRight(snapped.right() + bestDx);
        }
    }
    if (qAbs(bestDy) <= SNAP_THRESHOLD) {
        if (m_dragging || (m_resizeEdge & ET)) {
            snapped.translate(0, bestDy);
        } else if (m_resizeEdge & EB) {
            snapped.setBottom(snapped.bottom() + bestDy);
        }
    }

    // 生成辅助线（坐标需从父级坐标转到本地坐标）
    if (qAbs(bestDx) <= SNAP_THRESHOLD) {
        for (const auto &c : xCandidates) {
            // 垂直线：x = c.guidePos，转为本地坐标
            int localX = c.guidePos - snapped.left();
            int localY1 = c.guideStart - snapped.top();
            int localY2 = c.guideEnd   - snapped.top();
            m_snapGuides.append(QLine(localX, localY1, localX, localY2));
        }
    }
    if (qAbs(bestDy) <= SNAP_THRESHOLD) {
        for (const auto &c : yCandidates) {
            // 水平线：y = c.guidePos，转为本地坐标
            int localY = c.guidePos - snapped.top();
            int localX1 = c.guideStart - snapped.left();
            int localX2 = c.guideEnd   - snapped.left();
            m_snapGuides.append(QLine(localX1, localY, localX2, localY));
        }
    }

    return snapped;
}

QRect FenceWidget::applyWallpaperMagnet(const QRect &proposed)
{
    auto *canvas = qobject_cast<DesktopCanvas *>(parentWidget());
    if (m_collapsed)
        return proposed;
    if (canvas && !canvas->wallpaperMagnetEnabled()) {
        m_magneticEdge = MagneticEdge::None;
        m_magneticContour.clear();
        clearMask();
        return proposed;
    }
    if (!canvas || proposed.width() < 120 || proposed.height() < TITLE_H + 30) {
        m_magneticEdge = MagneticEdge::None;
        m_magneticContour.clear();
        clearMask();
        return proposed;
    }

    const QImage image = canvas->renderedWallpaperImage()
        .convertToFormat(QImage::Format_RGB32);
    if (image.isNull()) {
        m_magneticEdge = MagneticEdge::None;
        m_magneticContour.clear();
        clearMask();
        return proposed;
    }

    constexpr int searchRadius = 90;
    constexpr int sampleStep = 5;
    constexpr int gradientHalfWidth = 2;

    struct Candidate {
        MagneticEdge edge = MagneticEdge::None;
        QVector<QPoint> points;
        double score = 0.0;
    };

    auto luminance = [](QRgb pixel) {
        return (qRed(pixel) * 299 + qGreen(pixel) * 587
                + qBlue(pixel) * 114) / 1000;
    };
    auto contrast = [](QRgb a, QRgb b) {
        return (qAbs(qRed(a) - qRed(b))
              + qAbs(qGreen(a) - qGreen(b))
              + qAbs(qBlue(a) - qBlue(b))) / 3;
    };

    auto verticalCandidate = [&](MagneticEdge edge, int edgeX) {
        Candidate result;
        result.edge = edge;
        double total = 0.0;
        int expected = 0;

        for (int y = proposed.top(); y <= proposed.bottom(); y += sampleStep) {
            if (y < 0 || y >= image.height())
                continue;
            ++expected;
            int bestX = -1;
            int bestScore = 0;
            const int from = qMax(gradientHalfWidth,
                                  edgeX - searchRadius);
            const int to = qMin(image.width() - gradientHalfWidth - 1,
                                edgeX + searchRadius);
            for (int x = from; x <= to; ++x) {
                const QRgb a = image.pixel(x - gradientHalfWidth, y);
                const QRgb b = image.pixel(x + gradientHalfWidth, y);
                const int exterior = edge == MagneticEdge::Left
                    ? luminance(a) : luminance(b);
                const int interior = edge == MagneticEdge::Left
                    ? luminance(b) : luminance(a);
                const int diff = contrast(a, b);
                if (interior > 38 || exterior < 58 || diff < 34)
                    continue;
                const int score = diff + (38 - interior) * 2;
                if (score > bestScore) {
                    bestScore = score;
                    bestX = x;
                }
            }
            if (bestX >= 0) {
                result.points.append(QPoint(bestX, y));
                total += bestScore;
            }
        }

        if (expected == 0 ||
            result.points.size() < qRound(expected * 0.68)) {
            result.points.clear();
            return result;
        }
        result.score = total / result.points.size();
        return result;
    };

    auto horizontalCandidate = [&](MagneticEdge edge, int edgeY) {
        Candidate result;
        result.edge = edge;
        double total = 0.0;
        int expected = 0;

        for (int x = proposed.left(); x <= proposed.right(); x += sampleStep) {
            if (x < 0 || x >= image.width())
                continue;
            ++expected;
            int bestY = -1;
            int bestScore = 0;
            const int from = qMax(gradientHalfWidth,
                                  edgeY - searchRadius);
            const int to = qMin(image.height() - gradientHalfWidth - 1,
                                edgeY + searchRadius);
            for (int y = from; y <= to; ++y) {
                const QRgb a = image.pixel(x, y - gradientHalfWidth);
                const QRgb b = image.pixel(x, y + gradientHalfWidth);
                const int exterior = edge == MagneticEdge::Top
                    ? luminance(a) : luminance(b);
                const int interior = edge == MagneticEdge::Top
                    ? luminance(b) : luminance(a);
                const int diff = contrast(a, b);
                if (interior > 38 || exterior < 58 || diff < 34)
                    continue;
                const int score = diff + (38 - interior) * 2;
                if (score > bestScore) {
                    bestScore = score;
                    bestY = y;
                }
            }
            if (bestY >= 0) {
                result.points.append(QPoint(x, bestY));
                total += bestScore;
            }
        }

        if (expected == 0 ||
            result.points.size() < qRound(expected * 0.68)) {
            result.points.clear();
            return result;
        }
        result.score = total / result.points.size();
        return result;
    };

    QList<Candidate> candidates {
        verticalCandidate(MagneticEdge::Left, proposed.left()),
        verticalCandidate(MagneticEdge::Right, proposed.right()),
        horizontalCandidate(MagneticEdge::Top, proposed.top()),
        horizontalCandidate(MagneticEdge::Bottom, proposed.bottom())
    };

    Candidate best;
    for (const Candidate &candidate : candidates) {
        if (!candidate.points.isEmpty() && candidate.score > best.score)
            best = candidate;
    }

    // 本轮未检测到轮廓 —— 清除已有轮廓，恢复方块形状
    if (best.points.isEmpty() || best.score < 95.0) {
        if (m_magneticEdge != MagneticEdge::None) {
            m_magneticEdge = MagneticEdge::None;
            m_magneticContour.clear();
            clearMask();
        }
        return proposed;
    }

    QVector<QPoint> smoothed;
    smoothed.reserve(best.points.size());
    for (int i = 0; i < best.points.size(); ++i) {
        int sumX = 0;
        int sumY = 0;
        int count = 0;
        for (int j = qMax(0, i - 2);
             j <= qMin(best.points.size() - 1, i + 2); ++j) {
            sumX += best.points[j].x();
            sumY += best.points[j].y();
            ++count;
        }
        smoothed.append(QPoint(sumX / count, sumY / count));
    }

    int discontinuities = 0;
    for (int i = 1; i < smoothed.size(); ++i) {
        const int delta = (best.edge == MagneticEdge::Left ||
                           best.edge == MagneticEdge::Right)
            ? qAbs(smoothed[i].x() - smoothed[i - 1].x())
            : qAbs(smoothed[i].y() - smoothed[i - 1].y());
        if (delta > 14)
            ++discontinuities;
    }
    if (discontinuities > qMax(1, smoothed.size() / 12)) {
        // 新轮廓连续性不足 —— 保留已有轮廓不变
        return proposed;
    }

    if (best.edge == MagneticEdge::Left ||
        best.edge == MagneticEdge::Right) {
        smoothed.prepend(QPoint(smoothed.first().x(), proposed.top()));
        smoothed.append(QPoint(smoothed.last().x(), proposed.bottom()));
    } else {
        smoothed.prepend(QPoint(proposed.left(), smoothed.first().y()));
        smoothed.append(QPoint(proposed.right(), smoothed.last().y()));
    }

    QRect adjusted = proposed;
    if (best.edge == MagneticEdge::Left) {
        int minX = smoothed.first().x();
        for (const QPoint &point : smoothed) minX = qMin(minX, point.x());
        adjusted.setLeft(minX);
    } else if (best.edge == MagneticEdge::Right) {
        int maxX = smoothed.first().x();
        for (const QPoint &point : smoothed) maxX = qMax(maxX, point.x());
        adjusted.setRight(maxX);
    } else if (best.edge == MagneticEdge::Top) {
        int minY = smoothed.first().y();
        for (const QPoint &point : smoothed) minY = qMin(minY, point.y());
        adjusted.setTop(minY);
    } else if (best.edge == MagneticEdge::Bottom) {
        int maxY = smoothed.first().y();
        for (const QPoint &point : smoothed) maxY = qMax(maxY, point.y());
        adjusted.setBottom(maxY);
    }

    if (adjusted.width() < 120 || adjusted.height() < TITLE_H + 30)
        return proposed;

    m_magneticEdge = best.edge;
    m_magneticContour.clear();
    for (const QPoint &point : smoothed)
        m_magneticContour.append(point - adjusted.topLeft());
    updateShapeMask();
    return adjusted;
}

void FenceWidget::refreshMagneticContour()
{
    const QRect adjusted = applyWallpaperMagnet(geometry());
    if (adjusted != geometry())
        setGeometry(adjusted);
    updateShapeMask();
    layoutIcons();
    update();
}

QPainterPath FenceWidget::fenceShapePath() const
{
    QPainterPath path;
    if (m_collapsed || m_magneticEdge == MagneticEdge::None ||
        m_magneticContour.size() < 2) {
        path.addRoundedRect(QRectF(rect()), 10, 10);
        return path;
    }

    if (m_magneticEdge == MagneticEdge::Left) {
        path.moveTo(m_magneticContour.first());
        path.lineTo(width(), 0);
        path.lineTo(width(), height());
        path.lineTo(m_magneticContour.last());
        for (int i = m_magneticContour.size() - 2; i >= 0; --i)
            path.lineTo(m_magneticContour[i]);
    } else if (m_magneticEdge == MagneticEdge::Right) {
        path.moveTo(0, 0);
        path.lineTo(m_magneticContour.first());
        for (int i = 1; i < m_magneticContour.size(); ++i)
            path.lineTo(m_magneticContour[i]);
        path.lineTo(0, height());
    } else if (m_magneticEdge == MagneticEdge::Top) {
        path.moveTo(m_magneticContour.first());
        for (int i = 1; i < m_magneticContour.size(); ++i)
            path.lineTo(m_magneticContour[i]);
        path.lineTo(width(), height());
        path.lineTo(0, height());
    } else {
        path.moveTo(0, 0);
        path.lineTo(width(), 0);
        path.lineTo(m_magneticContour.last());
        for (int i = m_magneticContour.size() - 2; i >= 0; --i)
            path.lineTo(m_magneticContour[i]);
    }
    path.closeSubpath();

    // 异形套索只负责替换被磁吸的一侧；其余边缘仍应遵循标准圆角。
    // 与圆角矩形求交可同时修复右侧/底部直角和窗口 mask 的锯齿外溢。
    QPainterPath roundedBounds;
    roundedBounds.addRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    return path.intersected(roundedBounds).simplified();
}

void FenceWidget::updateShapeMask()
{
    if (m_collapsed || m_magneticEdge == MagneticEdge::None ||
        m_magneticContour.size() < 2) {
        clearMask();
        return;
    }
    QRegion region(fenceShapePath().toFillPolygon().toPolygon());
    if (m_liquidGlassEnabled) {
        // Keep non-rectangular hit testing but let the smooth optical mask own
        // the last pixel of coverage instead of cutting it with integer QRegion.
        const QRegion original=region;
        region |= original.translated(1,0) | original.translated(-1,0)
                  | original.translated(0,1) | original.translated(0,-1);
    }
    setMask(region);
}

int FenceWidget::magneticContentInset() const
{
    if (m_collapsed || m_magneticEdge != MagneticEdge::Left ||
        m_magneticContour.isEmpty())
        return MARGIN;

    qreal maxX = 0;
    for (const QPointF &point : m_magneticContour)
        maxX = qMax(maxX, point.x());
    return qBound(MARGIN, qCeil(maxX) + MARGIN, width() / 2);
}

void FenceWidget::drawSnapGuides(QPainter &p)
{
    p.save();
    p.setClipping(false);  // 辅助线可能超出自身范围

    QPen pen(QColor(0, 200, 255, 200), 1.5, Qt::DashLine);
    pen.setDashPattern({6, 3});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    for (const QLine &line : m_snapGuides)
        p.drawLine(line);

    // 吸附点：在线段端点画小菱形标记
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 200, 255, 160));
    for (const QLine &line : m_snapGuides) {
        const QPoint pts[2] = { line.p1(), line.p2() };
        for (const QPoint &pt : pts) {
            const QPoint diamond[4] = {
                pt + QPoint(0, -3),
                pt + QPoint(3,  0),
                pt + QPoint(0,  3),
                pt + QPoint(-3, 0)
            };
            p.drawPolygon(diamond, 4);
        }
    }

    p.restore();
}

void FenceWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(rect());

    // 整体背景
    const QPainterPath bgPath = fenceShapePath();
    bool glass = m_liquidGlassEnabled;
    const bool shaped = m_magneticEdge != MagneticEdge::None && m_magneticContour.size() >= 2;
    if (glass) {
        if (m_glassImage.isNull() || m_glassGeometry != geometry() || m_glassShape != bgPath) {
            if (auto *canvas = qobject_cast<DesktopCanvas *>(parentWidget())) {
                m_glassImage = canvas->renderLiquidGlass(
                    geometry(), 10.0, shaped ? bgPath : QPainterPath());
                m_glassGeometry = geometry();
                m_glassShape = bgPath;
            }
        }
        glass = !m_glassImage.isNull();
    }
    if (glass) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(rect()), m_glassImage);
        if (m_glassHover > 0) {
            p.save();
            p.setClipPath(bgPath);
            QPainterPath rim;
            if (shaped) rim = bgPath;
            else rim.addRoundedRect(QRectF(rect()).adjusted(0.8, 0.8, -0.8, -0.8), 9.2, 9.2);
            QRadialGradient light(m_glassPointer, 110);
            light.setColorAt(0, QColor(230, 248, 255, qRound(210*m_glassHover)));
            light.setColorAt(0.42, QColor(196, 228, 255, qRound(70*m_glassHover)));
            light.setColorAt(1, Qt::transparent);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QBrush(light), 1.6));
            p.drawPath(rim);
            light.setColorAt(0, QColor(202, 231, 255, qRound(28*m_glassHover)));
            light.setColorAt(0.42, QColor(196, 228, 255, qRound(10*m_glassHover)));
            p.setPen(QPen(QBrush(light), 7));
            p.drawPath(rim);
            p.restore();
        }
    } else {
        p.fillPath(bgPath, m_color);
    }

    // 标题栏（略深，与主体平滑衔接）
    QColor titleBg = m_color;
    titleBg.setAlpha(qMin(255, m_color.alpha() + 60));
    QPainterPath titlePath;
    titlePath.addRoundedRect(QRectF(0, 0, width(), TITLE_H), 10, 10);
    QPainterPath cut;
    cut.addRect(QRectF(0, TITLE_H / 2.0, width(), TITLE_H / 2.0));
    titlePath = titlePath.united(cut).intersected(bgPath);
    if (!glass) p.fillPath(titlePath, titleBg);

    // 边框
    if (m_editMode)
        p.setPen(QPen(QColor(255, 200, 0, 230), 2));
    else {
        QColor border = m_hasTitleFont ? m_titleFontColor : QColor(Qt::white);
        border.setAlpha(55);
        p.setPen(QPen(border, 1));
    }
    if (!glass || m_editMode) p.drawPath(bgPath);

    // ── 标题图标 + 标题文字 ────────────────────────────────────
    int titleX = qMax(12, magneticContentInset());
    const int iconSz = TITLE_H - 12;
    if (!m_titleIcon.isNull()) {
        p.drawPixmap(QRect(titleX, (TITLE_H - iconSz) / 2, iconSz, iconSz),
                     m_titleIcon.pixmap(iconSz, iconSz));
        titleX += iconSz + 6;
    } else {
        // 默认白色小框
        p.setPen(QPen(QColor(255, 255, 255, 120), 1));
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawRoundedRect(titleX, (TITLE_H - iconSz) / 2, iconSz, iconSz, 3, 3);
        titleX += iconSz + 6;
    }
    QFont fTitle;
    if (m_hasTitleFont && !m_titleFontFamily.isEmpty())
        fTitle.setFamily(m_titleFontFamily);
    fTitle.setPixelSize(m_hasTitleFont ? m_titleFontSize : 13);
    fTitle.setBold(m_hasTitleFont ? m_titleFontBold : true);
    fTitle.setItalic(m_hasTitleFont ? m_titleFontItalic : false);
    p.setFont(fTitle);
    p.setPen(m_hasTitleFont ? m_titleFontColor : Qt::white);
    // 右侧留 50px：38 给数量文字 + 12 给折叠箭头区域
    p.drawText(QRect(titleX, 0, width() - titleX - 50, TITLE_H),
               Qt::AlignVCenter | Qt::AlignLeft, m_title);

    // ── 数量文字（右侧留 30px 给箭头）───────────────────────
    QFont fCount;
    fCount.setPixelSize(11);
    p.setFont(fCount);
    p.setPen(QColor(255, 255, 255, 170));
    if (!m_embeddedWidget) {
        p.drawText(QRect(0, 0, width() - 32, TITLE_H),
                   Qt::AlignVCenter | Qt::AlignRight,
                   QString("(%1)").arg(m_icons.size()));
    }

    // ── 折叠箭头（最右 30px 范围内）────────────────────────
    p.setPen(QPen(m_hasTitleFont ? m_titleFontColor : QColor(Qt::white), 1.8));
    const int ax = width() - 16;
    const int ay = TITLE_H / 2;
    if (m_collapsed) {
        // ▼
        p.drawLine(ax - 5, ay - 3, ax,     ay + 3);
        p.drawLine(ax,     ay + 3, ax + 5, ay - 3);
    } else {
        // ▲
        p.drawLine(ax - 5, ay + 3, ax,     ay - 3);
        p.drawLine(ax,     ay - 3, ax + 5, ay + 3);
    }

    const int viewportH = contentViewportHeight();
    if (!m_collapsed && m_contentHeight > viewportH && viewportH > 0) {
        const int trackH = viewportH;
        const int thumbH = qMax(24, trackH * viewportH / qMax(viewportH, m_contentHeight));
        const int maxOffset = qMax(1, m_contentHeight - viewportH);
        const int thumbY = contentTop()
            + (trackH - thumbH) * m_scrollOffset / maxOffset;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 80));
        p.drawRoundedRect(width() - 8, thumbY, 4, thumbH, 2, 2);
    }

    // ── 编辑模式：底部提示 + 8个控制手柄 ───────────────────
    if (m_editMode) {
        QFont fHint;
        fHint.setPixelSize(10);
        p.setFont(fHint);
        p.setPen(QColor(255, 200, 0, 220));
        p.drawText(rect().adjusted(6, 0, -6, -4),
                   Qt::AlignBottom | Qt::AlignLeft,
                   "✎ 编辑模式  拖动移位 / 边缘缩放");
        drawHandles(p);
    }

    // 吸附辅助线（画在最上层）
    if (!m_snapGuides.isEmpty())
        drawSnapGuides(p);
}

// ── 边缘检测 ─────────────────────────────────────────────

FenceWidget::Edge FenceWidget::hitEdge(const QPoint &pt) const
{
    if (!m_editMode) return None;
    const bool l = pt.x() < EDGE_W;
    const bool r = pt.x() > width()  - EDGE_W;
    if (m_collapsed) return l ? EL : r ? ER : None;
    const bool t = pt.y() < EDGE_W;
    const bool b = pt.y() > height() - EDGE_W;
    if (t && l) return ETL;
    if (t && r) return ETR;
    if (b && l) return EBL;
    if (b && r) return EBR;
    if (l) return EL;
    if (r) return ER;
    if (t) return ET;
    if (b) return EB;
    return None;
}

void FenceWidget::applyCursor(Edge e)
{
    switch (e) {
    case ETL: case EBR: setCursor(Qt::SizeFDiagCursor); break;
    case ETR: case EBL: setCursor(Qt::SizeBDiagCursor); break;
    case EL:  case ER:  setCursor(Qt::SizeHorCursor);   break;
    case ET:  case EB:  setCursor(Qt::SizeVerCursor);   break;
    default:
        setCursor(m_editMode
                  ? Qt::SizeAllCursor
                  : Qt::ArrowCursor);
    }
}

// ── 鼠标事件 ─────────────────────────────────────────────

void FenceWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }

    setFocus(Qt::MouseFocusReason);
    emit interactionStarted(this);

    // ── 单击标题栏右侧30px：折叠/展开（原来是双击，现改为单击）──
    if (e->pos().y() < TITLE_H && e->pos().x() > width() - 30 &&
        !(m_editMode && hitEdge(e->pos()) != None)) {
        setCollapsed(!m_collapsed);
        e->accept();
        return;
    }

    // ── 单击标题栏其他区域：折叠/展开 ──
    if (e->pos().y() < TITLE_H && !m_editMode) {
        setCollapsed(!m_collapsed);
        e->accept();
        return;
    }

    if (!m_editMode && !(e->modifiers() & Qt::ControlModifier))
        clearIconSelection();

    if (m_editMode && m_collapseAnimation &&
        m_collapseAnimation->state() == QAbstractAnimation::Running) {
        m_collapseAnimation->stop();
        resize(width(), m_collapsed ? TITLE_H : m_expandedH);
        layoutIcons();
    }
    m_mouseStart = e->globalPos();
    m_geoStart   = geometry();
    m_resizeEdge = hitEdge(e->pos());

    // 编辑模式下标题栏主体区域优先用于移动，只有左右边缘/角点才用于缩放。
    // 这样不会因为点到标题栏上沿而误触发纵向缩放，导致看起来“拖不动”。
    if (m_editMode && e->pos().y() < TITLE_H &&
        e->pos().x() >= EDGE_W && e->pos().x() <= width() - EDGE_W) {
        m_resizeEdge = None;
    }

    if (m_resizeEdge != None) {
        m_resizing = true;
        m_dragging = false;
    } else if (m_editMode) {
        m_dragging = true;
        m_resizing = false;
        grabMouse();
    }
    raise();
    e->accept();
}

void FenceWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging && !m_resizing) {
        applyCursor(hitEdge(e->pos()));
        return;
    }

    const QPoint delta = e->globalPos() - m_mouseStart;
    QRect geo = m_geoStart;

    if (m_dragging) {
        geo.moveTo(geo.topLeft() + delta);
        geo = computeSnappedGeometry(geo);
        geo = applyWallpaperMagnet(geo);
        move(geo.topLeft());
        if (size() != geo.size())
            resize(geo.size());
    } else {
        if (m_resizeEdge & EL) geo.setLeft(geo.left()     + delta.x());
        if (m_resizeEdge & ER) geo.setRight(geo.right()   + delta.x());
        if (m_resizeEdge & ET) geo.setTop(geo.top()       + delta.y());
        if (m_resizeEdge & EB) geo.setBottom(geo.bottom() + delta.y());
        geo.setWidth(qMax(geo.width(),   160));
        geo.setHeight(m_collapsed ? TITLE_H : qMax(geo.height(), TITLE_H + 60));
        geo = computeSnappedGeometry(geo);
        geo = applyWallpaperMagnet(geo);
        if (m_collapsed) geo.setHeight(TITLE_H);
        setGeometry(geo);
    }
    update();
    e->accept();
}

void FenceWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging || m_resizing) {
        if (!m_collapsed) m_expandedH = height();
        emit geometryChanged();
    }
    if (mouseGrabber() == this)
        releaseMouse();
    m_dragging   = false;
    m_resizing   = false;
    m_resizeEdge = None;
    m_snapGuides.clear();
    update();
    e->accept();
}

void FenceWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    // 双击标题栏仅用于重命名（编辑模式）
    if (e->pos().y() < TITLE_H) {
        if (m_editMode)
            showRenameDialog();
    }
    e->accept();
}

void FenceWidget::showRenameDialog()
{
    bool ok = false;
    const QString t = QInputDialog::getText(
        this, "重命名分区", "分区名称：",
        QLineEdit::Normal, m_title, &ok);
    if (ok && !t.isEmpty()) setTitle(t);
}

void FenceWidget::sortItems(SortMode mode)
{
    std::sort(m_icons.begin(), m_icons.end(),
        [mode](DesktopIcon *a, DesktopIcon *b) {
            const DesktopItem &ia = a->item();
            const DesktopItem &ib = b->item();
            switch (mode) {
            case SortMode::Type:
                if (ia.mimeType != ib.mimeType)
                    return ia.mimeType < ib.mimeType;
                return ia.displayName.localeAwareCompare(ib.displayName) < 0;
            case SortMode::ModifiedTime:
                return QFileInfo(ia.filePath).lastModified()
                    > QFileInfo(ib.filePath).lastModified();
            case SortMode::Name:
            default:
                return ia.displayName.localeAwareCompare(ib.displayName) < 0;
            }
        });
    layoutIcons();
    emit geometryChanged();
}

void FenceWidget::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::SelectAll)) {
        emit interactionStarted(this);
        selectAllIcons();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Undo)) {
        emit undoRequested();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Copy)) {
        copySelectedIcons(false);
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Cut)) {
        copySelectedIcons(true);
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        pasteClipboardFiles();
        e->accept();
        return;
    }

    switch (e->key()) {
    case Qt::Key_Delete:
        trashSelectedIcons();
        e->accept();
        return;
    case Qt::Key_F2:
        renameFirstSelectedIcon();
        e->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        openSelectedIcons();
        e->accept();
        return;
    case Qt::Key_Escape:
        clearIconSelection();
        e->accept();
        return;
    default:
        QWidget::keyPressEvent(e);
    }
}

// ── 右键菜单 ─────────────────────────────────────────────

void FenceWidget::contextMenuEvent(QContextMenuEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    emit interactionStarted(this);

    if (e->pos().y() > TITLE_H) {
        QMenu menu(this);
        MenuStyle::applyVenturaContextMenu(&menu);

        auto *actUndo = menu.addAction(
            QIcon::fromTheme("edit-undo"), "撤回");
        actUndo->setShortcut(QKeySequence::Undo);
        connect(actUndo, &QAction::triggered,
                this, [this] { emit undoRequested(); });

        menu.addSeparator();

        auto *actPaste = menu.addAction(
            QIcon::fromTheme("edit-paste"), "粘贴");
        actPaste->setEnabled(!m_locked && FileClipboard::hasFiles());
        connect(actPaste, &QAction::triggered,
                [this] { pasteClipboardFiles(); });

        QMenu *newMenu = menu.addMenu(
            QIcon::fromTheme("document-new"), "新建");
        MenuStyle::applyVenturaContextMenu(newMenu);
        newMenu->setEnabled(!m_locked);

        auto *actNewDir = newMenu->addAction(
            QIcon::fromTheme("folder-new"), "文件夹");
        connect(actNewDir, &QAction::triggered,
                this, &FenceWidget::createNewDirectory);

        newMenu->addSeparator();
        auto addNewFile = [this, newMenu](const QString &label,
                                           const QString &baseName,
                                           const QString &suffix,
                                           const QString &iconName) {
            QAction *action = newMenu->addAction(
                QIcon::fromTheme(iconName), label);
            connect(action, &QAction::triggered,
                    [this, baseName, suffix] {
                createNewFile(baseName, suffix);
            });
        };
        addNewFile("文本文档 (.txt)", "新建文本文档", ".txt",
                   "text-x-generic");
        addNewFile("Word 文档 (.docx)", "新建 Word 文档", ".docx",
                   "x-office-document");
        addNewFile("Excel 工作表 (.xlsx)", "新建 Excel 工作表", ".xlsx",
                   "x-office-spreadsheet");
        addNewFile("PowerPoint 演示文稿 (.pptx)",
                   "新建 PowerPoint 演示文稿", ".pptx",
                   "x-office-presentation");
        addNewFile("Markdown 文档 (.md)", "新建 Markdown 文档", ".md",
                   "text-markdown");
        addNewFile("CSV 表格 (.csv)", "新建 CSV 表格", ".csv",
                   "text-csv");
        addNewFile("JSON 文件 (.json)", "新建 JSON 文件", ".json",
                   "application-json");

        menu.addSeparator();

        auto *actSelectAll = menu.addAction("全选");
        actSelectAll->setEnabled(!m_icons.isEmpty());
        connect(actSelectAll, &QAction::triggered,
                [this] { selectAllIcons(); });

        const bool hasSelection = !selectedFilePaths().isEmpty();
        auto *actCopy = menu.addAction(
            QIcon::fromTheme("edit-copy"), "复制");
        actCopy->setEnabled(hasSelection);
        connect(actCopy, &QAction::triggered,
                [this] { copySelectedIcons(false); });

        auto *actCut = menu.addAction(
            QIcon::fromTheme("edit-cut"), "剪切");
        actCut->setEnabled(hasSelection);
        connect(actCut, &QAction::triggered,
                [this] { copySelectedIcons(true); });

        menu.exec(e->globalPos());
        e->accept();
        return;
    }

    QMenu menu(this);
    MenuStyle::applyVenturaContextMenu(&menu);

    auto *actUndo = menu.addAction(
        QIcon::fromTheme("edit-undo"), "撤回");
    actUndo->setShortcut(QKeySequence::Undo);
    connect(actUndo, &QAction::triggered,
            this, [this] { emit undoRequested(); });

    menu.addSeparator();

    auto *actRename = menu.addAction("重命名");
    actRename->setEnabled(!m_locked);
    connect(actRename, &QAction::triggered,
            [this] { showRenameDialog(); });

    auto *actCollapse = menu.addAction(m_collapsed ? "展开" : "折叠");
    connect(actCollapse, &QAction::triggered,
            [this] { setCollapsed(!m_collapsed); });

    auto *actLock = menu.addAction(m_locked ? "解锁分区" : "锁定分区");
    connect(actLock, &QAction::triggered, [this] {
        setLocked(!m_locked);
        emit geometryChanged();
    });

    menu.addSeparator();

    QMenu *sortMenu = menu.addMenu("排序");
    MenuStyle::applyVenturaContextMenu(sortMenu);
    sortMenu->addAction("按名称", [this] { sortItems(SortMode::Name); });
    sortMenu->addAction("按类型", [this] { sortItems(SortMode::Type); });
    sortMenu->addAction("按修改时间", [this] { sortItems(SortMode::ModifiedTime); });

    QMenu *alphaMenu = menu.addMenu("透明度");
    MenuStyle::applyVenturaContextMenu(alphaMenu);
    const QList<QPair<QString, int>> alphaItems = {
        { "很淡", 45 },
        { "较淡", 75 },
        { "默认", 90 },
        { "较深", 130 },
        { "深色", 170 }
    };
    for (const auto &alpha : alphaItems) {
        alphaMenu->addAction(alpha.first, [this, value = alpha.second] {
            QColor c = m_color;
            c.setAlpha(value);
            setFenceColor(c);
            emit geometryChanged();
        });
    }

    // 颜色子菜单
    QMenu *cm = menu.addMenu("分区颜色");
    MenuStyle::applyVenturaContextMenu(cm);

    // 辅助：添加颜色项
    auto addColorAct = [&](const QString &name, int r, int g, int b, int a) {
        QColor c(r, g, b, a);
        QPixmap px(16, 16);
        px.fill(c);
        auto *act = cm->addAction(QIcon(px), name);
        connect(act, &QAction::triggered, [this, c] {
            setFenceColor(c);
            emit geometryChanged();
        });
    };

    addColorAct("蓝色",   0, 120, 215,  90);
    addColorAct("青色",   0, 180, 180,  90);
    addColorAct("绿色",   0, 180,  80,  90);
    addColorAct("红色", 215,  60,  60,  90);
    addColorAct("紫色", 140,  80, 200,  90);
    addColorAct("橙色", 215, 140,   0,  90);
    addColorAct("深灰",  80,  80,  80, 120);
    addColorAct("深黑",  20,  20,  20, 160);

    cm->addSeparator();
    auto *actCustom = cm->addAction("自定义…");
    connect(actCustom, &QAction::triggered, [this] {
        QColor c = QColorDialog::getColor(m_color, this, "选择分区颜色");
        if (c.isValid()) {
            c.setAlpha(90);
            setFenceColor(c);
            emit geometryChanged();
        }
    });

    menu.addSeparator();

    // 分区图标
    auto *actIcon = menu.addAction("分区图标…");
    connect(actIcon, &QAction::triggered, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "选择分区图标",
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            "图片文件 (*.png *.svg *.jpg *.ico)");
        if (!path.isEmpty())
            setTitleIconPath(path);
    });
    auto *actClearIcon = menu.addAction("清除分区图标");
    actClearIcon->setEnabled(!m_titleIconPath.isEmpty());
    connect(actClearIcon, &QAction::triggered, [this] {
        setTitleIconPath(QString());
    });

    // 分区标题字体设置
    auto *actTitleFont = menu.addAction("标题字体设置…");
    connect(actTitleFont, &QAction::triggered,
            [this] { showTitleFontSettingsDialog(); });

    // 分区内部图标字体设置
    auto *actFont = menu.addAction("内部图标字体设置…");
    connect(actFont, &QAction::triggered,
            [this] { showFontSettingsDialog(); });

    menu.addSeparator();

    auto *actDel = menu.addAction("删除此分区");
    actDel->setEnabled(!m_locked);
    connect(actDel, &QAction::triggered, [this] {
        if (QMessageBox::question(this, "确认",
                QString("删除分区 \"%1\"？\n"
                        "内部文件图标将回到桌面。").arg(m_title))
            == QMessageBox::Yes)
            emit deleteRequested(this);
    });

    menu.exec(e->globalPos());
    e->accept();
}

// ── 拖放 ─────────────────────────────────────────────────

void FenceWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() ||
        e->mimeData()->hasFormat("application/x-kyfences-sysicon")) {
        if (e->mimeData()->hasFormat(kInternalFileDragMime))
            e->setDropAction(Qt::MoveAction);
        e->accept();
    }
}

void FenceWidget::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls() ||
        e->mimeData()->hasFormat("application/x-kyfences-sysicon")) {
        if (e->mimeData()->hasFormat(kInternalFileDragMime))
            e->setDropAction(Qt::MoveAction);
        e->accept();
    }
}

void FenceWidget::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat("application/x-kyfences-sysicon")) {
        const QString path = QString::fromUtf8(
            e->mimeData()->data("application/x-kyfences-sysicon")).trimmed();
        DesktopItem item = DesktopItem::fromStoredPath(path);
        if (item.isValid()) {
            if (auto *sourceIcon = qobject_cast<DesktopIcon *>(e->source())) {
                if (iconBelongsToThisFence(sourceIcon)) {
                    moveItemsToIndex(QStringList() << path,
                                     dropInsertionIndex(e->pos()));
                    e->setDropAction(Qt::MoveAction);
                    e->accept();
                    return;
                }
            }

            insertItem(item, dropInsertionIndex(e->pos()));
            emit fileDropped(item.filePath);
            e->acceptProposedAction();
            return;
        }
        e->ignore();
        return;
    }

    QStringList paths;
    for (const QUrl &url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && !paths.contains(path))
            paths << path;
    }

    if (paths.isEmpty()) {
        e->ignore();
        return;
    }

    if (auto *sourceIcon = qobject_cast<DesktopIcon *>(e->source())) {
        if (iconBelongsToThisFence(sourceIcon)) {
            moveItemsToIndex(paths, dropInsertionIndex(e->pos()));
            e->setDropAction(Qt::MoveAction);
            e->accept();
            return;
        }
    }

    const bool internal =
        e->mimeData()->hasFormat(kInternalFileDragMime);
    Qt::DropAction action = e->dropAction();
    if (action == Qt::IgnoreAction)
        action = e->proposedAction();
    if (action == Qt::IgnoreAction)
        action = Qt::CopyAction;
    const int insertAt = dropInsertionIndex(e->pos());
    auto place = [this, insertAt](const FileClipboard::PasteResult &result) {
        int at = insertAt;
        for (const auto &path : result.placedPaths) {
            const DesktopItem item = DesktopItem::fromStoredPath(path);
            if (!item.isValid()) continue;
            insertItem(item, at++); emit fileDropped(item.filePath);
        }
        if (!result.transferredPaths.isEmpty())
            emit filesPasted(result.placedSourcePaths, result.transferredPaths, result.move);
        if (!result.failedPaths.isEmpty())
            QMessageBox::warning(this, "拖放未完成", QString("有 %1 个项目失败或已取消。").arg(result.failedPaths.size()));
    };
    bool accepted = true;
    if (internal) {
        FileClipboard::PasteResult result; result.placedPaths = paths;
        place(result);
    } else {
        accepted = FileClipboard::transferFilesAsync(paths, primaryDesktopDirectory(),
            action == Qt::MoveAction, true, this, place);
    }
    e->setDropAction(internal ? Qt::MoveAction : action);
    if (accepted) e->accept(); else e->ignore();
}

// ── 分区字体设置对话框 ─────────────────────────────────

void FenceWidget::showFontSettingsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QString("分区「%1」内部图标字体设置").arg(m_title));
    dlg.setMinimumWidth(340);

    auto *layout = new QFormLayout(&dlg);

    // 字体族
    auto *fontCombo = new QComboBox(&dlg);
    static const QStringList families = QFontDatabase().families();
    fontCombo->addItem("系统默认", QString());
    for (const QString &f : families)
        fontCombo->addItem(f, f);
    if (m_hasLocalFont && !m_localFontFamily.isEmpty()) {
        int idx = fontCombo->findData(m_localFontFamily);
        if (idx >= 0) fontCombo->setCurrentIndex(idx);
    }
    layout->addRow("内部字体：", fontCombo);

    // 字号
    auto *sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(8, 24);
    sizeSpin->setValue(m_hasLocalFont ? m_localFontSize : 11);
    sizeSpin->setSuffix(" px");
    layout->addRow("内部字号：", sizeSpin);

    // 粗体
    auto *boldCheck = new QCheckBox("粗体", &dlg);
    boldCheck->setChecked(m_hasLocalFont ? m_localFontBold : false);
    layout->addRow("", boldCheck);

    // 斜体
    auto *italicCheck = new QCheckBox("斜体", &dlg);
    italicCheck->setChecked(m_hasLocalFont ? m_localFontItalic : false);
    layout->addRow("", italicCheck);

    // 字体颜色
    QColor chosenColor = m_hasLocalFont ? m_localFontColor : Qt::white;
    auto *colorBtn = new QPushButton(&dlg);
    auto updateColorBtn = [colorBtn, &chosenColor] {
        QPixmap px(48, 16);
        px.fill(chosenColor);
        colorBtn->setIcon(QIcon(px));
        colorBtn->setText(chosenColor.name());
    };
    updateColorBtn();
    connect(colorBtn, &QPushButton::clicked, [&] {
        QColor c = QColorDialog::getColor(chosenColor, &dlg, "选择字体颜色");
        if (c.isValid()) {
            chosenColor = c;
            updateColorBtn();
        }
    });
    layout->addRow("内部字体颜色：", colorBtn);

    // 重置按钮（恢复为全局设置）
    auto *resetBtn = new QPushButton("内部图标恢复为全局设置", &dlg);
    layout->addRow("", resetBtn);

    bool resetClicked = false;
    connect(resetBtn, &QPushButton::clicked, [&] {
        resetClicked = true;
        dlg.accept();
    });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        if (resetClicked) {
            // 清除本地字体设置
            m_hasLocalFont = false;
            for (auto *icon : m_icons)
                applyInheritedIconFont(icon);
            update();
            emit geometryChanged();
            return;
        }

        m_hasLocalFont = true;
        m_localFontFamily = fontCombo->currentData().toString();
        m_localFontSize = sizeSpin->value();
        m_localFontColor = chosenColor;
        m_localFontBold = boldCheck->isChecked();
        m_localFontItalic = italicCheck->isChecked();

        // 应用到所有图标
        for (auto *icon : m_icons)
            applyLocalIconFont(icon);
        emit geometryChanged();
    }
}

void FenceWidget::showTitleFontSettingsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QString("分区「%1」标题字体设置").arg(m_title));
    dlg.setMinimumWidth(340);

    auto *layout = new QFormLayout(&dlg);

    auto *fontCombo = new QComboBox(&dlg);
    static const QStringList families = QFontDatabase().families();
    fontCombo->addItem("系统默认", QString());
    for (const QString &f : families)
        fontCombo->addItem(f, f);
    if (m_hasTitleFont && !m_titleFontFamily.isEmpty()) {
        const int idx = fontCombo->findData(m_titleFontFamily);
        if (idx >= 0) fontCombo->setCurrentIndex(idx);
    }
    layout->addRow("标题字体：", fontCombo);

    auto *sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(9, 28);
    sizeSpin->setValue(m_hasTitleFont ? m_titleFontSize : 13);
    sizeSpin->setSuffix(" px");
    layout->addRow("标题字号：", sizeSpin);

    auto *boldCheck = new QCheckBox("粗体", &dlg);
    boldCheck->setChecked(m_hasTitleFont ? m_titleFontBold : true);
    layout->addRow("", boldCheck);

    auto *italicCheck = new QCheckBox("斜体", &dlg);
    italicCheck->setChecked(m_hasTitleFont ? m_titleFontItalic : false);
    layout->addRow("", italicCheck);

    QColor chosenColor = m_hasTitleFont ? m_titleFontColor : Qt::white;
    auto *colorBtn = new QPushButton(&dlg);
    auto updateColorBtn = [colorBtn, &chosenColor] {
        QPixmap px(48, 16);
        px.fill(chosenColor);
        colorBtn->setIcon(QIcon(px));
        colorBtn->setText(chosenColor.name());
    };
    updateColorBtn();
    connect(colorBtn, &QPushButton::clicked, [&] {
        QColor c = QColorDialog::getColor(chosenColor, &dlg, "选择标题字体颜色");
        if (c.isValid()) {
            chosenColor = c;
            updateColorBtn();
        }
    });
    layout->addRow("标题字体颜色：", colorBtn);

    auto *resetBtn = new QPushButton("标题恢复为默认设置", &dlg);
    layout->addRow("", resetBtn);

    bool resetClicked = false;
    connect(resetBtn, &QPushButton::clicked, [&] {
        resetClicked = true;
        dlg.accept();
    });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        if (resetClicked) {
            m_hasTitleFont = false;
            update();
            emit geometryChanged();
            return;
        }

        m_hasTitleFont = true;
        m_titleFontFamily = fontCombo->currentData().toString();
        m_titleFontSize = sizeSpin->value();
        m_titleFontColor = chosenColor;
        m_titleFontBold = boldCheck->isChecked();
        m_titleFontItalic = italicCheck->isChecked();
        update();
        emit geometryChanged();
    }
}
