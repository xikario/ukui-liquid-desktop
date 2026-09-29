#include "DesktopWidgets.h"
#include "MusicDesklet.h"
#include "CalendarDesklet.h"
#include "ActivityRecorder.h"
#include "DesktopCanvas.h"
#include "FenceWidget.h"
#include "FenceGlassRenderer.h"
#include "DesktopIcon.h"
#include "FileClipboard.h"
#include "SystemMonitor.h"
#include "SmartSpaceWidget.h"
#include "MenuStyle.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QPainter>
#include <QLinearGradient>
#include <QScreen>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QClipboard>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QWindow>
#include <QRegion>
#include <QDrag>
#include <QFileSystemWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QSettings>
#include <QInputDialog>
#include <QLineEdit>
#include <QProcess>
#include <QPair>
#include <QUrl>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QMetaObject>
#include <QSignalBlocker>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <QFontDatabase>
#include <QFontDialog>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QEvent>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

namespace {

void releaseUnusedHeap()
{
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

constexpr const char *kSystemIconMime = "application/x-kyfences-sysicon";
constexpr bool kDesktopSyncDiagnostics = false;
constexpr int kHotCornerGuardSize = 10;

QRegion rubberBandDamage(const QRect &before, const QRect &after)
{
    if (before == after)
        return {};

    // The translucent fill is unchanged in the overlap. Repaint only the
    // entering/leaving strips plus both outlines (dash phase can change).
    // Include a logical-pixel margin for the pen and fractional display scales.
    auto outline = [](const QRect &rect) -> QRegion {
        if (!rect.isValid())
            return {};
        return QRegion(rect.adjusted(-2, -2, 2, 2))
            .subtracted(QRegion(rect.adjusted(2, 2, -2, -2)));
    };
    return QRegion(before).xored(QRegion(after))
        .united(outline(before)).united(outline(after));
}

enum HotCorner {
    HotCornerTopLeft = 0,
    HotCornerTopRight = 1,
    HotCornerBottomLeft = 2,
    HotCornerBottomRight = 3
};

QIcon fallbackMenuIcon(const QString &glyph,
                       const QColor &background = QColor("#334155"),
                       const QColor &foreground = Qt::white)
{
    constexpr int size = 24;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(2, 2, size - 4, size - 4), 5, 5);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(glyph.size() > 1 ? 10 : 14);
    painter.setFont(font);
    painter.setPen(foreground);
    painter.drawText(pix.rect(), Qt::AlignCenter, glyph);
    return QIcon(pix);
}

QIcon menuIcon(const QStringList &themeNames,
               const QString &fallbackGlyph,
               const QColor &fallbackBackground = QColor("#334155"))
{
    for (const QString &name : themeNames) {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return fallbackMenuIcon(fallbackGlyph, fallbackBackground);
}

QIcon colorSwatchMenuIcon(const QColor &color)
{
    constexpr int size = 24;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 255, 255, 120), 1));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(3, 3, size - 6, size - 6), 5, 5);
    return QIcon(pix);
}

bool transferDroppedPathToDesktop(const QString &srcPath,
                                  const QString &targetPath,
                                  bool move)
{
    return FileClipboard::transferPath(srcPath, targetPath, move);
}

QString layoutPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + "/kyfences/layout.json";
}

QString syncDebugLogPath()
{
    return QDir::homePath() + "/.log/ukui-fences-desktop-sync.log";
}

QString rectToString(const QRect &rect)
{
    return QStringLiteral("%1,%2 %3x%4")
        .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

QString pointToString(const QPoint &pt)
{
    return QStringLiteral("%1,%2").arg(pt.x()).arg(pt.y());
}

QString sortedPathList(const QSet<QString> &paths)
{
    QStringList list = paths.values();
    list.sort(Qt::CaseInsensitive);
    return list.join(QStringLiteral(" | "));
}

void writeSyncDebug(const QString &tag, const QString &details)
{
    if (!kDesktopSyncDiagnostics)
        return;

    const QString path = syncDebugLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        << " pid=" << QCoreApplication::applicationPid()
        << " [" << tag << "] " << details << '\n';
}

void addExistingDirectory(QStringList &paths, const QString &path)
{
    if (path.trimmed().isEmpty())
        return;

    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || !QFileInfo(clean).isDir() || paths.contains(clean))
        return;

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

QString xdgDesktopDirectoryFromConfig()
{
    QFile file(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
               + "/user-dirs.dirs");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('#')) ||
            !line.startsWith(QLatin1String("XDG_DESKTOP_DIR=")))
            continue;

        return expandedUserDirValue(line.section(QLatin1Char('='), 1));
    }

    return {};
}

QStringList desktopDirectoryCandidates()
{
    QStringList paths;
    addExistingDirectory(paths,
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    addExistingDirectory(paths, xdgDesktopDirectoryFromConfig());

    const QString xdgDesktop = QString::fromUtf8(qgetenv("XDG_DESKTOP_DIR"));
    addExistingDirectory(paths, expandedUserDirValue(xdgDesktop));

    QProcess proc;
    proc.start(QStringLiteral("xdg-user-dir"), QStringList() << QStringLiteral("DESKTOP"));
    if (proc.waitForFinished(500))
        addExistingDirectory(paths,
            QString::fromUtf8(proc.readAllStandardOutput()).trimmed());

    addExistingDirectory(paths, QDir::homePath() + "/桌面");
    addExistingDirectory(paths, QDir::homePath() + "/Desktop");

    if (paths.isEmpty())
        addExistingDirectory(paths, QDir::homePath());

    return paths;
}

int systemIconRank(const DesktopItem &item)
{
    if (item.filePath == "computer:///") return 0;
    if (item.filePath == "trash:///") return 1;
    return 2;
}

bool systemIconsFirst(DesktopIcon *a, DesktopIcon *b)
{
    if (a->item().isSystemIcon != b->item().isSystemIcon)
        return a->item().isSystemIcon;
    if (a->item().isSystemIcon && b->item().isSystemIcon)
        return systemIconRank(a->item()) < systemIconRank(b->item());
    return false;
}

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

bool storedItemExists(const DesktopItem &item)
{
    if (item.isSystemIcon ||
        item.filePath.startsWith(QLatin1String("computer://")) ||
        item.filePath.startsWith(QLatin1String("trash://")))
        return true;

    return QFileInfo::exists(item.filePath);
}

bool startControlCenter(const QStringList &args)
{
    const QString exe = QStandardPaths::findExecutable("ukui-control-center");
    if (exe.isEmpty()) return false;
    return QProcess::startDetached(exe, args);
}

void openWallpaperSettings()
{
    const QList<QStringList> candidates = {
        QStringList() << "-m" << "wallpaper",
        QStringList() << "-p" << "wallpaper",
        QStringList() << "-m" << "personalized",
        QStringList() << "-m" << "background",
        QStringList()
    };

    for (const QStringList &args : candidates)
        if (startControlCenter(args))
            return;
}

QPixmap loadSystemWallpaperPixmap()
{
    struct SchemaKey { const char *schema; const char *key; };
    static const SchemaKey candidates[] = {
        { "org.mate.background", "picture-filename" },
        { "org.ukui.SettingsDaemon.plugins.background", "picture-filename" },
        { "org.gnome.desktop.background", "picture-uri" },
        { "org.gnome.desktop.background", "picture-uri-dark" },
        { nullptr, nullptr }
    };

    for (int i = 0; candidates[i].schema; ++i) {
        QProcess proc;
        proc.start("gsettings",
            QStringList() << "get"
                          << candidates[i].schema
                          << candidates[i].key);
        if (!proc.waitForFinished(2000))
            continue;

        QString path = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (path.startsWith('\'')) path = path.mid(1);
        if (path.endsWith('\''))   path.chop(1);
        if (path.startsWith("file://"))
            path = QUrl(path).toLocalFile();

        if (path.isEmpty() || !QFile::exists(path))
            continue;

        QPixmap pix(path);
        if (!pix.isNull())
            return pix;
    }

    return {};
}

QColor pickJsonColor(const QJsonObject &root, const QStringList &names)
{
    for (const QString &name : names) {
        QColor color;
        color.setNamedColor(root[name].toString());
        if (color.isValid())
            return color;
    }
    return {};
}

bool readMatugenJsonColors(const QString &path, QColor *accent, QColor *text)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    if (accent)
        *accent = pickJsonColor(root, { "primary", "tertiary", "secondary" });
    if (text)
        *text = pickJsonColor(root, { "on_surface", "on_background", "on_primary" });

    return (accent && accent->isValid()) || (text && text->isValid());
}

QMap<QString, QColor> readCssDefinedColors(const QString &path)
{
    QMap<QString, QColor> colors;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return colors;

    const QString content = QString::fromUtf8(f.readAll());
    for (QString line : content.split('\n')) {
        line = line.trimmed();
        if (!line.startsWith(QLatin1String("@define-color ")))
            continue;

        line = line.mid(QStringLiteral("@define-color ").size()).trimmed();
        const int splitAt = line.indexOf(' ');
        if (splitAt <= 0)
            continue;

        const QString name = line.left(splitAt).trimmed();
        QString value = line.mid(splitAt + 1).trimmed();
        if (value.endsWith(';'))
            value.chop(1);

        QColor color;
        color.setNamedColor(value);
        if (color.isValid())
            colors.insert(name, color);
    }

    return colors;
}

bool readCssThemeColors(const QString &path, QColor *accent, QColor *text)
{
    const QMap<QString, QColor> colors = readCssDefinedColors(path);
    if (colors.isEmpty())
        return false;

    if (accent) {
        const QStringList names = { "primary", "tertiary", "secondary" };
        for (const QString &name : names) {
            if (colors.contains(name)) {
                *accent = colors.value(name);
                break;
            }
        }
    }

    if (text) {
        const QStringList names = { "on_surface", "on_background", "on_primary" };
        for (const QString &name : names) {
            if (colors.contains(name)) {
                *text = colors.value(name);
                break;
            }
        }
    }

    return (accent && accent->isValid()) || (text && text->isValid());
}

Window rootChildForWindow(Display *display, Window window)
{
    if (!display || !window)
        return 0;

    Window root = DefaultRootWindow(display);
    Window current = window;
    while (current) {
        Window queryRoot = 0;
        Window parent = 0;
        Window *children = nullptr;
        unsigned int childCount = 0;
        if (!XQueryTree(display, current, &queryRoot, &parent,
                        &children, &childCount)) {
            if (children)
                XFree(children);
            return window;
        }
        if (children)
            XFree(children);
        if (!parent || parent == root)
            return current;
        current = parent;
    }

    return window;
}

bool windowOrDescendantHasClass(Display *display, Window window,
                                const QStringList &classNames)
{
    if (!display || !window)
        return false;

    XClassHint hint;
    hint.res_name = nullptr;
    hint.res_class = nullptr;
    if (XGetClassHint(display, window, &hint)) {
        const QString className = QString::fromLatin1(
            hint.res_class ? hint.res_class : "");
        const QString appName = QString::fromLatin1(
            hint.res_name ? hint.res_name : "");
        if (hint.res_name)
            XFree(hint.res_name);
        if (hint.res_class)
            XFree(hint.res_class);

        if (classNames.contains(className) || classNames.contains(appName))
            return true;
    }

    Window root = 0;
    Window parent = 0;
    Window *children = nullptr;
    unsigned int childCount = 0;
    if (!XQueryTree(display, window, &root, &parent, &children, &childCount))
        return false;

    bool found = false;
    for (unsigned int i = 0; i < childCount && !found; ++i)
        found = windowOrDescendantHasClass(display, children[i], classNames);

    if (children)
        XFree(children);
    return found;
}

void lowerPeonyDesktopWindows(Display *display, Window ownWindow)
{
    if (!display)
        return;

    Window root = DefaultRootWindow(display);
    const Window ownFrame = rootChildForWindow(display, ownWindow);
    Window parent = 0;
    Window *children = nullptr;
    unsigned int childCount = 0;
    if (!XQueryTree(display, root, &root, &parent, &children, &childCount))
        return;

    const QStringList peonyClasses = {
        QStringLiteral("桌面"),
        QStringLiteral("peony-qt-desktop")
    };
    for (unsigned int i = 0; i < childCount; ++i) {
        const Window child = children[i];
        if (child == ownWindow || child == ownFrame)
            continue;

        if (windowOrDescendantHasClass(display, child, peonyClasses))
            XLowerWindow(display, child);
    }

    if (children)
        XFree(children);
}

QColor textColorForAccent(const QColor &accent)
{
    const int luminance =
        (accent.red() * 299 + accent.green() * 587 + accent.blue() * 114) / 1000;
    return luminance > 155 ? QColor("#202124") : QColor(Qt::white);
}

QColor accentColorFromWallpaper(const QPixmap &wallpaper)
{
    if (wallpaper.isNull())
        return {};

    const QImage image = wallpaper.toImage()
        .scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB32);

    double r = 0;
    double g = 0;
    double b = 0;
    double weightSum = 0;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgb(image.pixel(x, y));
            int h = 0;
            int s = 0;
            int l = 0;
            color.getHsl(&h, &s, &l);

            if (s < 35 || l < 35 || l > 230)
                continue;

            const double lightnessWeight =
                1.0 - qMin(1.0, qAbs(l - 145) / 145.0);
            const double weight = (s / 255.0) * (0.35 + lightnessWeight);
            r += color.red() * weight;
            g += color.green() * weight;
            b += color.blue() * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 0)
        return {};

    QColor accent(qRound(r / weightSum),
                  qRound(g / weightSum),
                  qRound(b / weightSum));
    int h = 0;
    int s = 0;
    int l = 0;
    accent.getHsl(&h, &s, &l);
    if (h < 0)
        return {};

    accent = QColor::fromHsl(h, qBound(95, s + 35, 225),
                             qBound(80, l, 170), 90);
    return accent;
}

QStringList defaultFenceIconPaths()
{
    const QStringList names = {
        "orbit.svg",
        "tasks.svg",
        "industry.svg",
        "services.svg",
        "presentation.svg",
        "dock.svg",
        "inbox.svg",
        "archive.svg",
        "ideas.svg",
        "favorites.svg"
    };
    const QStringList roots = {
        QCoreApplication::applicationDirPath()
            + "/../assets/fence-icons",
        QCoreApplication::applicationDirPath()
            + "/../share/ukui-fences/fence-icons",
        QStringLiteral("/usr/share/ukui-fences/fence-icons")
    };

    for (const QString &root : roots) {
        QStringList paths;
        bool complete = true;
        for (const QString &name : names) {
            const QString path = QDir(root).absoluteFilePath(name);
            if (!QFileInfo::exists(path)) {
                complete = false;
                break;
            }
            paths << path;
        }
        if (complete)
            return paths;
    }
    return {};
}

} // namespace

// ── 构造/析构 ─────────────────────────────────────────────

DesktopCanvas::DesktopCanvas(QWidget *parent)
    : QWidget(parent)
{
    const QSettings appearanceSettings;
    m_fenceLiquidGlassEnabled = appearanceSettings.value("appearance/fenceLiquidGlass", false).toBool();
    m_wallpaperMagnetEnabled = appearanceSettings.value("appearance/wallpaperMagnetEnabled", true).toBool();
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    // UKUI's style treats the top 48 logical pixels of a window as a drag
    // handle and sends an X11 ButtonRelease when moving it. This is a desktop
    // canvas: that synthetic release would cancel rubber-band selection.
    setProperty("useStyleWindowManager", false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // Let Qt initialise exposed backing-store regions before paintEvent.  The
    // former WA_NoSystemBackground optimisation left black areas on X11 while
    // an embedded Smart Space drag or filter invalidated child widgets.
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);

    lockToDesktopGeometry();
    updateHotCornerGuards();
    m_screenGeometryTimer = new QTimer(this);
    m_screenGeometryTimer->setSingleShot(true);
    m_screenGeometryTimer->setInterval(120);
    for (QScreen *screen : QGuiApplication::screens())
        connectScreenSignals(screen);
    connect(qApp, &QGuiApplication::screenAdded,
            this, [this](QScreen *screen) {
        connectScreenSignals(screen);
        scheduleScreenGeometrySync();
    });
    connect(qApp, &QGuiApplication::screenRemoved,
            this, [this](QScreen *) { scheduleScreenGeometrySync(); });
    connect(qApp, &QGuiApplication::primaryScreenChanged,
            this, [this](QScreen *screen) {
        connectScreenSignals(screen);
        scheduleScreenGeometrySync();
    });
    connect(m_screenGeometryTimer, &QTimer::timeout,
            this, &DesktopCanvas::syncScreenGeometry);

    // 在首次 map 前创建 native handle 并写入 DESKTOP 类型。这样 KWin
    // 从一开始就按桌面窗口处理，同时 QWidget 子控件仍保持正常父子层级。
    winId();
    applyX11DesktopHints();
    show();
    setupAsDesktop();
    lockToDesktopGeometry();
    updateHotCornerGuards();

    // 桌面目录：UKUI/Qt/xdg-user-dirs 在中文环境下偶尔给出不同路径。
    // 保留一个主目录用于新建/粘贴，同时扫描所有真实存在的候选桌面目录。
    refreshDesktopDirectories();
    loadExternalTheme();

    // 文件监控（跨线程安全：用 QueuedConnection 推回主线程）
    m_watcher  = new QFileSystemWatcher(this);
    ensureDesktopWatches();
    ensureTrashWatches();
    writeSyncDebug(QStringLiteral("START"),
        QStringLiteral("layout=%1 desktopPath=%2 desktopPaths=%3 watchedDirs=%4")
            .arg(layoutPath(), m_desktopPath,
                 m_desktopPaths.join(QStringLiteral(" | ")),
                 m_watcher->directories().join(QStringLiteral(" | "))));

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(180);

    m_desktopSyncTimer = new QTimer(this);
    m_desktopSyncTimer->setInterval(1500);

    m_cutRefreshTimer = new QTimer(this);
    m_cutRefreshTimer->setInterval(1000);

    m_smartSpaceRelayoutTimer = new QTimer(this);
    m_smartSpaceRelayoutTimer->setSingleShot(true);
    m_smartSpaceRelayoutTimer->setInterval(35);



    auto queueRefresh = [this](const QString &changedPath) {
        refreshDesktopDirectories();
        ensureDesktopWatches();
        ensureTrashWatches();
        writeSyncDebug(QStringLiteral("WATCH_EVENT"),
            QStringLiteral("changedPath=%1 desktopPaths=%2 watchedDirs=%3")
                .arg(changedPath,
                     m_desktopPaths.join(QStringLiteral(" | ")),
                     m_watcher ? m_watcher->directories().join(QStringLiteral(" | "))
                               : QString()));
        if (changedPath.contains(QLatin1String("/.local/share/Trash")) ||
            changedPath.endsWith(QLatin1String("/.local/share/Trash")) ||
            changedPath.startsWith(QLatin1String("trash://"))) {
            QTimer::singleShot(80, this, &DesktopCanvas::refreshTrashState);
            QTimer::singleShot(700, this, &DesktopCanvas::refreshTrashState);
        }

        QMetaObject::invokeMethod(m_debounce,
            qOverload<>(&QTimer::start), Qt::QueuedConnection);
    };

    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, queueRefresh);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, queueRefresh);
    connect(m_debounce, &QTimer::timeout,
            this, &DesktopCanvas::refreshDesktopIcons);
    connect(m_desktopSyncTimer, &QTimer::timeout,
            this, &DesktopCanvas::refreshDesktopIcons);
    connect(m_cutRefreshTimer, &QTimer::timeout,
            this, [this] {
        pruneMissingFileIcons();
        syncCutVisualState();
    });
    connect(m_smartSpaceRelayoutTimer, &QTimer::timeout, this, [this] {
        layoutLooseIcons();
        if (m_smartSpace && !m_smartSpace->isWindow())
            m_smartSpace->raise();
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            this, &DesktopCanvas::syncCutVisualState);

    loadLayout();
    loadWallpaper();
    // Event-driven: changing wallpaper must also invalidate dependent glass
    // surfaces, even when the settings application does not call refreshAll.
    auto *wallpaperDebounce = new QTimer(this);
    wallpaperDebounce->setSingleShot(true);
    wallpaperDebounce->setInterval(160);
    connect(wallpaperDebounce, &QTimer::timeout, this, [this] {
        if (m_wallpaperMode == WallpaperMode::System) {
            loadWallpaper();
            if (m_monitor)
                m_monitor->refreshWallpaperTheme();
        }
    });
    for (const QString &schema : {QStringLiteral("org.mate.background"),
            QStringLiteral("org.ukui.SettingsDaemon.plugins.background"),
            QStringLiteral("org.gnome.desktop.background")}) {
        auto *monitor = new QProcess(this);
        m_wallpaperMonitors.append(monitor);
        connect(monitor, &QProcess::readyReadStandardOutput, this,
                [monitor, wallpaperDebounce] {
            while (monitor->canReadLine()) {
                if (monitor->readLine().startsWith("picture-"))
                    wallpaperDebounce->start();
            }
        });
        monitor->start(QStringLiteral("gsettings"), {QStringLiteral("monitor"), schema});
    }
    addSystemIcons();
    refreshDesktopIcons();

    saveLayout();
    setFocus(Qt::OtherFocusReason);

    QTimer::singleShot(1400, this, [this] {
        if (LiquidDesklet::autoStartEnabled("clock")) setClockWidgetVisible(true);
        if (LiquidDesklet::autoStartEnabled("activity")) setActivityWidgetVisible(true);
        if (LiquidDesklet::autoStartEnabled("music")) setMusicWidgetVisible(true);
        if (LiquidDesklet::autoStartEnabled("calendar")) setCalendarWidgetVisible(true);
    });

    // 若用户启用了"随 Fences 自动启动系统监控"，则自动创建小组件
    if (SystemMonitor::autoStartEnabled()) {
        QTimer::singleShot(800, this, [this] {
            setSystemMonitorVisible(true);
        });
    }
    if (SmartSpaceWidget::autoStartEnabled()) {
        QTimer::singleShot(1100, this, [this] {
            setSmartSpaceVisible(true);
        });
    }
}

DesktopCanvas::~DesktopCanvas()
{
    // Persist these before QObject destroys children in construction order.
    delete m_calendarWidget; m_calendarWidget = nullptr;
    delete m_musicWidget; m_musicWidget = nullptr;
    delete m_activityWidget; m_activityWidget = nullptr;
    delete m_activityRecorder; m_activityRecorder = nullptr;
    delete m_clockWidget; m_clockWidget = nullptr;
    for (auto *monitor : m_wallpaperMonitors)
        monitor->terminate();
    for (auto *monitor : m_wallpaperMonitors) {
        if (monitor->state() == QProcess::NotRunning)
            continue;
        if (!monitor->waitForFinished(500)) {
            monitor->kill();
            monitor->waitForFinished(500);
        }
    }
    qDeleteAll(m_hotCornerGuards);
    m_hotCornerGuards.clear();
    saveLayout();
    hide();
}

void DesktopCanvas::showAndActivate()
{
    m_userHidden = false;
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    lockToDesktopGeometry();
    setupAsDesktop();
    updateHotCornerGuards();
    raise();
    QTimer::singleShot(250, this, [this] {
        if (!m_userHidden && isVisible())
            applyX11DesktopHints();
    });
}

void DesktopCanvas::hideFences()
{
    m_userHidden = true;
    hide();
}

void DesktopCanvas::toggleEditMode()
{
    setGlobalEditMode(!m_editMode);
}

void DesktopCanvas::setEditModeDBus(bool edit)
{
    setGlobalEditMode(edit);
}

void DesktopCanvas::refreshAll()
{
    restackDesktopLayer();
    loadWallpaper();
    forceSyncDesktopIcons();
    refreshTrashState();
    applyFontToAll();
    if (m_monitor)
        m_monitor->refreshWallpaperTheme();

    // Windows 风格刷新：触发所有图标（包含 fences 里的）闪烁
    for (DesktopIcon *icon : m_looseIcons) {
        if (icon) icon->triggerRefreshBlink();
    }
    for (FenceWidget *fence : m_fences) {
        if (fence) {
            for (DesktopIcon *icon : fence->icons()) {
                if (icon) icon->triggerRefreshBlink();
            }
        }
    }

    // 同步刷新完成后用 repaint 确保 compositor 立即合成最终帧
    repaint();
}

void DesktopCanvas::activateOnSessionStartup()
{
    m_userHidden = false;

    // UKUI 登录时 Peony、KWin 与桌面插件并非同时完成初始化。它们可能在
    // Fences 首次映射之后重新建立桌面窗口层级，因此在会话稳定阶段分段
    // 重申 DESKTOP 类型与同层顺序。用户主动切换系统桌面后 m_userHidden
    // 会立即阻止剩余重试，不会抢回桌面。
    const int delays[] = { 0, 900, 2200, 4500, 8000, 13000 };
    for (const int delay : delays) {
        QTimer::singleShot(delay, this, [this] {
            if (m_userHidden)
                return;
            show();
            setWindowState(windowState() & ~Qt::WindowMinimized);
            lockToDesktopGeometry();
            setupAsDesktop();
            raise();
            applyX11DesktopHints();
        });
    }
}

void DesktopCanvas::quitApp()
{
    m_userHidden = true;
    saveLayout();
    hide();
    // 给 WM 一点时间处理，然后退出
    QApplication::processEvents();
    QApplication::quit();
}

void DesktopCanvas::ensureTrashWatches()
{
    if (!m_watcher)
        return;

    const QString trashRoot =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + "/.local/share/Trash";
    QDir().mkpath(trashRoot + "/files");
    QDir().mkpath(trashRoot + "/info");

    const QStringList paths = {
        trashRoot,
        trashRoot + "/files",
        trashRoot + "/info"
    };

    const QStringList watched = m_watcher->directories();
    for (const QString &path : paths) {
        if (QFileInfo::exists(path) && !watched.contains(path))
            m_watcher->addPath(path);
    }
}

void DesktopCanvas::refreshDesktopDirectories()
{
    const QStringList candidates = desktopDirectoryCandidates();
    if (candidates.isEmpty())
        return;

    m_desktopPaths = candidates;
    m_desktopPath = QDir::cleanPath(m_desktopPath);
    if (m_desktopPath.isEmpty() || !m_desktopPaths.contains(m_desktopPath))
        m_desktopPath = m_desktopPaths.first();
}

void DesktopCanvas::ensureDesktopWatches()
{
    if (!m_watcher)
        return;

    refreshDesktopDirectories();

    const QStringList watched = m_watcher->directories();
    for (const QString &path : m_desktopPaths) {
        if (!QFileInfo(path).isDir()) {
            writeSyncDebug(QStringLiteral("WATCH_SKIP"),
                QStringLiteral("desktop path is not a directory: %1").arg(path));
            continue;
        }
        if (!watched.contains(path)) {
            const bool ok = m_watcher->addPath(path);
            writeSyncDebug(ok ? QStringLiteral("WATCH_ADD")
                              : QStringLiteral("WATCH_ADD_FAILED"),
                QStringLiteral("path=%1 watchedDirs=%2")
                    .arg(path,
                         m_watcher->directories().join(QStringLiteral(" | "))));
        }
    }
}

QSet<QString> DesktopCanvas::collectDesktopFilePaths() const
{
    QSet<QString> currentPaths;
    for (const QString &desktopDir : m_desktopPaths) {
        QDir dir(desktopDir);
        const QStringList entries = dir.entryList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);
        for (const QString &name : entries) {
            const QString path =
                QFileInfo(dir.absoluteFilePath(name)).absoluteFilePath();
            if (!path.isEmpty())
                currentPaths.insert(path);
        }
    }
    return currentPaths;
}

bool DesktopCanvas::isInDesktopDirectory(const QString &path) const
{
    for (const QString &desktopDir : m_desktopPaths) {
        if (FileClipboard::isInDirectory(path, desktopDir))
            return true;
    }
    return FileClipboard::isInDirectory(path, m_desktopPath);
}

// ── 桌面层设置 ────────────────────────────────────────────

void DesktopCanvas::setupAsDesktop()
{
    setWindowFlag(Qt::FramelessWindowHint, true);

    const QString windowId =
        QStringLiteral("0x%1").arg(static_cast<qulonglong>(winId()), 0, 16);
    QProcess::execute(QStringLiteral("wmctrl"),
        QStringList() << QStringLiteral("-i")
                      << QStringLiteral("-r") << windowId
                      << QStringLiteral("-b")
                      << QStringLiteral("remove,below"));
    const QStringList states = {
        QStringLiteral("skip_taskbar"),
        QStringLiteral("skip_pager"),
        QStringLiteral("sticky")
    };
    for (const QString &state : states) {
        QProcess::execute(QStringLiteral("wmctrl"),
            QStringList() << QStringLiteral("-i")
                          << QStringLiteral("-r") << windowId
                          << QStringLiteral("-b")
                          << QStringLiteral("add,") + state);
    }

    applyX11DesktopHints();
    QTimer::singleShot(100, this, &DesktopCanvas::applyX11DesktopHints);
}

void DesktopCanvas::restackDesktopLayer()
{
    if (m_userHidden)
        m_userHidden = false;

    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    lockToDesktopGeometry();
    setupAsDesktop();
    raise();
    applyX11DesktopHints();

    const int delays[] = { 120, 500, 1200 };
    for (const int delay : delays) {
        QTimer::singleShot(delay, this, [this] {
            if (m_userHidden)
                return;
            show();
            lockToDesktopGeometry();
            updateHotCornerGuards();
            raise();
            applyX11DesktopHints();
        });
    }
}

void DesktopCanvas::applyX11DesktopHints()
{
    // 双桌面层：Peony 始终映射在最底层作保底，Fences 也是桌面类型，
    // 但创建得更晚并在桌面层内置顶。这样 Win+D 仍显示 Fences，
    // Fences 隐藏或崩溃时则立即露出 Peony。
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return;

    const Window window = static_cast<Window>(winId());

    const Atom typeAtom =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    const Atom desktopType =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    if (typeAtom != None && desktopType != None) {
        XChangeProperty(display, window, typeAtom, XA_ATOM, 32,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char *>(&desktopType),
                        1);
    }

    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    if (stateAtom != None) {
        QVector<Atom> states;
        const char *stateNames[] = {
            "_NET_WM_STATE_SKIP_TASKBAR",
            "_NET_WM_STATE_SKIP_PAGER",
            "_NET_WM_STATE_STICKY",
            "_KDE_NET_WM_STATE_SKIP_SWITCHER"
        };
        for (const char *name : stateNames) {
            const Atom atom = XInternAtom(display, name, False);
            if (atom != None)
                states.append(atom);
        }
        if (!states.isEmpty()) {
            XChangeProperty(display, window, stateAtom, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<unsigned char *>(states.data()),
                            states.size());
        }
    }

    lowerPeonyDesktopWindows(display, window);
    XRaiseWindow(display, window);
    const Window frame = rootChildForWindow(display, window);
    if (frame && frame != window)
        XRaiseWindow(display, frame);
    XSync(display, False);
    XCloseDisplay(display);
}

bool DesktopCanvas::x11ShowingDesktop() const
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;

    const Atom property =
        XInternAtom(display, "_NET_SHOWING_DESKTOP", True);
    if (property == None) {
        XCloseDisplay(display);
        return false;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(
        display, DefaultRootWindow(display), property,
        0, 1, False, XA_CARDINAL,
        &actualType, &actualFormat, &itemCount, &bytesAfter, &data);

    bool showing = false;
    if (status == Success && data && actualType == XA_CARDINAL &&
        actualFormat == 32 && itemCount == 1) {
        showing = *reinterpret_cast<unsigned long *>(data) != 0;
    }

    if (data)
        XFree(data);
    XCloseDisplay(display);
    return showing;
}

void DesktopCanvas::setX11ShowingDesktop(bool showing) const
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return;

    const Window root = DefaultRootWindow(display);
    const Atom atom = XInternAtom(display, "_NET_SHOWING_DESKTOP", False);
    if (atom != None) {
        XEvent event;
        memset(&event, 0, sizeof(event));
        event.xclient.type = ClientMessage;
        event.xclient.window = root;
        event.xclient.message_type = atom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = showing ? 1 : 0;
        event.xclient.data.l[1] = 0;
        XSendEvent(display, root, False,
                   SubstructureRedirectMask | SubstructureNotifyMask,
                   &event);
        XFlush(display);
    }

    XCloseDisplay(display);
}

void DesktopCanvas::triggerShowDesktop()
{
    setX11ShowingDesktop(!x11ShowingDesktop());
}

void DesktopCanvas::updateHotCornerGuards()
{
    // Do not install an invisible native hot-corner window. On UKUI/X11 it
    // can receive the last pixels of a projector output and turn a normal
    // edge click into Show Desktop, making Fences appear to exit.
    qDeleteAll(m_hotCornerGuards);
    m_hotCornerGuards.clear();
}

void DesktopCanvas::connectScreenSignals(QScreen *screen)
{
    if (!screen)
        return;

    connect(screen, &QScreen::geometryChanged,
            this, [this](const QRect &) { scheduleScreenGeometrySync(); },
            Qt::UniqueConnection);
    connect(screen, &QScreen::availableGeometryChanged,
            this, [this](const QRect &) { scheduleScreenGeometrySync(); },
            Qt::UniqueConnection);
    connect(screen, &QScreen::logicalDotsPerInchChanged,
            this, [this](qreal) { scheduleScreenGeometrySync(); },
            Qt::UniqueConnection);
}

void DesktopCanvas::scheduleScreenGeometrySync()
{
    if (m_screenGeometryTimer)
        m_screenGeometryTimer->start();
}

void DesktopCanvas::syncScreenGeometry()
{
    QScreen *screen = QApplication::primaryScreen();
    if (!screen || !screen->geometry().isValid()) {
        if (m_screenGeometryTimer)
            m_screenGeometryTimer->start(250);
        return;
    }

    lockToDesktopGeometry();
    updateHotCornerGuards();

    if (m_monitor) {
        const QPoint bounded(
            qBound(16, m_monitor->x(),
                   qMax(16, width() - m_monitor->width() - 16)),
            qBound(16, m_monitor->y(),
                   qMax(16, height() - m_monitor->height() - 16)));
        if (m_monitor->pos() != bounded)
            m_monitor->move(bounded);
    }
    if (m_smartSpace && !m_smartSpace->isWindow()) {
        const QPoint bounded(
            qBound(0, m_smartSpace->x(),
                   qMax(0, width() - m_smartSpace->width())),
            qBound(0, m_smartSpace->y(),
                   qMax(0, height() - m_smartSpace->height())));
        if (m_smartSpace->pos() != bounded)
            m_smartSpace->move(bounded);
    }
    update();
}

bool DesktopCanvas::handleHotCornerPress(int corner, QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton)
        return false;

    if (corner == HotCornerBottomRight)
        triggerShowDesktop();

    event->accept();
    return true;
}

void DesktopCanvas::lockToDesktopGeometry()
{
    QScreen *screen = QApplication::primaryScreen();
    if (!screen) return;

    // Qt's X11 platform maps a screen-sized QRect one pixel short on the
    // native desktop window. Keep the inclusive edge compensation so the
    // actual X window covers every pixel of a 1920x1080 projector too.
    const QRect target = screen->geometry().adjusted(0, 0, 1, 1);
    if (geometry() == target)
        return;

    m_lockingDesktopGeometry = true;
    setGeometry(target);
    m_lockingDesktopGeometry = false;
}

// ── 壁纸加载 ─────────────────────────────────────────────

void DesktopCanvas::loadWallpaper()
{
    if (m_wallpaperMode != WallpaperMode::System && !m_wallpaperPath.isEmpty()) {
        QPixmap pix(m_wallpaperPath);
        if (!pix.isNull()) {
            m_wallpaper = pix;
            m_wallpaperUsingCustom = true;
            clearWallpaperCache();
            rebuildWallpaperCache();
            update();
            return;
        }
    }

    m_wallpaper = loadSystemWallpaperPixmap();
    m_wallpaperUsingCustom = false;
    clearWallpaperCache();
    rebuildWallpaperCache();
    update();
}

void DesktopCanvas::clearWallpaperCache()
{
    m_fenceGlassRenderer.reset();
    m_fenceGlassWallpaperKey = -1;
    for (auto *fence : m_fences) fence->invalidateGlassCache();
    m_wallpaperCache = QPixmap();
    m_wallpaperCacheLogicalSize = QSize();
    m_wallpaperCacheDpr = 0.0;
}

void DesktopCanvas::rebuildWallpaperCache()
{
    if (m_wallpaper.isNull() || size().isEmpty()) {
        clearWallpaperCache();
        emit wallpaperChanged();
        return;
    }

    const qreal dpr = windowHandle()
        ? windowHandle()->devicePixelRatio()
        : devicePixelRatioF();
    const QSize pixelSize(qMax(1, qRound(width() * dpr)),
                          qMax(1, qRound(height() * dpr)));
    if (m_wallpaperCacheLogicalSize == size() &&
        qFuzzyCompare(m_wallpaperCacheDpr, dpr) &&
        !m_wallpaperCache.isNull()) {
        return;
    }

    const QImage source = m_wallpaper.toImage();
    QImage result(pixelSize, QImage::Format_RGB32);
    result.fill(Qt::black);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const WallpaperMode renderMode = m_wallpaperUsingCustom
        ? m_wallpaperMode
        : WallpaperMode::Fill;

    switch (renderMode) {
    case WallpaperMode::Fit: {
        const QImage scaled = source.scaled(pixelSize,
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
        const QPoint topLeft((pixelSize.width() - scaled.width()) / 2,
                             (pixelSize.height() - scaled.height()) / 2);
        painter.drawImage(topLeft, scaled);
        break;
    }
    case WallpaperMode::Stretch: {
        painter.drawImage(QRect(QPoint(0, 0), pixelSize),
                          source,
                          source.rect());
        break;
    }
    case WallpaperMode::Tile: {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.fillRect(QRect(QPoint(0, 0), pixelSize),
                         QBrush(QPixmap::fromImage(source)));
        break;
    }
    case WallpaperMode::Center: {
        const QPoint topLeft((pixelSize.width() - source.width()) / 2,
                             (pixelSize.height() - source.height()) / 2);
        painter.drawImage(topLeft, source);
        break;
    }
    case WallpaperMode::Fill:
    case WallpaperMode::System:
    default: {
        const QImage scaled = source.scaled(pixelSize,
                                           Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
        const int cropX = qMax(0, (scaled.width() - pixelSize.width()) / 2);
        const int cropY = qMax(0, (scaled.height() - pixelSize.height()) / 2);
        painter.drawImage(QPoint(0, 0),
                          scaled.copy(cropX, cropY,
                                      pixelSize.width(), pixelSize.height()));
        break;
    }
    }
    painter.end();

    m_wallpaperCache = QPixmap::fromImage(result);
    m_wallpaperCache.setDevicePixelRatio(dpr);
    m_wallpaperCacheLogicalSize = size();
    m_wallpaperCacheDpr = dpr;
    for (auto *fence : m_fences) fence->invalidateGlassCache();
    emit wallpaperChanged();
}

QImage DesktopCanvas::wallpaperBackdrop(const QRect &globalArea, qreal dpr) const
{
    if (m_wallpaperCache.isNull() || globalArea.isEmpty() || dpr <= 0)
        return {};
    QImage image(QSize(qMax(1, qRound(globalArea.width() * dpr)),
                      qMax(1, qRound(globalArea.height() * dpr))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(-mapFromGlobal(globalArea.topLeft()), m_wallpaperCache);
    return image;
}

void DesktopCanvas::setFenceLiquidGlassEnabled(bool enabled)
{
    if (m_fenceLiquidGlassEnabled == enabled) return;
    m_fenceLiquidGlassEnabled = enabled;
    QSettings settings;
    settings.setValue("appearance/fenceLiquidGlass", enabled);
    settings.sync();
    if (!enabled) {
        m_fenceGlassRenderer.reset();
        m_fenceGlassWallpaperKey = -1;
    }
    for (auto *fence : m_fences) fence->setLiquidGlassEnabled(enabled);
}

void DesktopCanvas::setWallpaperMagnetEnabled(bool enabled)
{
    if (m_wallpaperMagnetEnabled == enabled)
        return;

    m_wallpaperMagnetEnabled = enabled;
    QSettings settings;
    settings.setValue("appearance/wallpaperMagnetEnabled", enabled);
    settings.sync();

    for (auto *fence : m_fences) {
        if (!fence)
            continue;
        if (!enabled) {
            fence->m_magneticEdge = static_cast<FenceWidget::MagneticEdge>(0);
            fence->m_magneticContour.clear();
            fence->updateShapeMask();
            fence->invalidateGlassCache();
            fence->layoutIcons();
        }
    }
    saveLayout();
    update();
}

QImage DesktopCanvas::renderLiquidGlass(const QRect &area, qreal radius,
                                        const QPainterPath &shape)
{
    if (area.isEmpty()) return {};
    if (!m_fenceGlassRenderer) m_fenceGlassRenderer = std::make_unique<FenceGlassRenderer>();
    if (m_fenceGlassWallpaperKey != m_wallpaperCache.cacheKey()) {
        QImage source = m_wallpaperCache.toImage();
        if (source.isNull()) {
            // Match the desktop's no-wallpaper gradient exactly.
            const qreal dpr = devicePixelRatioF();
            source = QImage(QSize(qMax(1, qRound(width()*dpr)), qMax(1, qRound(height()*dpr))),
                            QImage::Format_RGB32);
            source.setDevicePixelRatio(dpr);
            QPainter p(&source);
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, QColor("#1a2a3a"));
            g.setColorAt(1, QColor("#2c5f8a"));
            p.fillRect(rect(), g);
        }
        m_fenceGlassRenderer->setWallpaper(source);
        m_fenceGlassWallpaperKey = m_wallpaperCache.cacheKey();
    }
    return m_fenceGlassRenderer->renderPanel(area, radius, shape);
}

bool DesktopCanvas::loadExternalTheme()
{
    QColor accent;
    QColor text;

    const QString home =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QStringList jsonCandidates = {
        home + "/.cache/quickshell_colors.json"
    };
    const QStringList cssCandidates = {
        home + "/.config/waybar/colors.css",
        home + "/.config/matugen/colors.css"
    };

    bool loaded = false;
    for (const QString &path : jsonCandidates) {
        loaded = readMatugenJsonColors(path, &accent, &text);
        if (loaded)
            break;
    }

    if (!loaded) {
        for (const QString &path : cssCandidates) {
            loaded = readCssThemeColors(path, &accent, &text);
            if (loaded)
                break;
        }
    }

    if (accent.isValid()) {
        accent.setAlpha(90);
        m_defaultFenceColor = accent;
    }
    if (text.isValid())
        m_fontColor = text;

    return loaded;
}

void DesktopCanvas::applyExternalThemeToFences()
{
    applyThemeToFences(m_defaultFenceColor, m_fontColor);
}

bool DesktopCanvas::applyWallpaperThemeToFences()
{
    const QColor accent = accentColorFromWallpaper(m_wallpaper);
    if (!accent.isValid())
        return false;

    applyThemeToFences(accent, textColorForAccent(accent));
    return true;
}

void DesktopCanvas::applyThemeToFences(const QColor &fenceColor,
                                       const QColor &textColor)
{
    if (!fenceColor.isValid())
        return;

    m_defaultFenceColor = fenceColor;
    for (auto *fence : m_fences) {
        if (fence)
            fence->setFenceColor(fenceColor);
    }
    if (textColor.isValid())
        m_fontColor = textColor;
    applyFontToAll();
    update();
    saveLayout();
}

void DesktopCanvas::connectLooseIcon(DesktopIcon *icon)
{
    icon->setVisualScale(m_desktopIconScale);
    icon->setFontFamily(m_fontFamily);
    icon->setFontSize(m_fontSize);
    icon->setFontColor(m_fontColor);
    icon->setFontBold(m_fontBold);
    icon->setFontItalic(m_fontItalic);
    connect(icon, &DesktopIcon::activated,
            [](const DesktopItem &it) { it.open(); });
    connect(icon, &DesktopIcon::undoRequested,
            this, &DesktopCanvas::undoLastOperation);
    connect(icon, &DesktopIcon::clicked,
            this, [this](DesktopIcon *self, Qt::KeyboardModifiers mods) {
        if (!self) return;
        setFocus(Qt::MouseFocusReason);
        clearFenceSelections();

        if (mods & Qt::ShiftModifier) {
            selectIconRange(m_selectionAnchor, self,
                            mods & Qt::ControlModifier);
            m_selectionAnchor = self;
        } else if (mods & Qt::ControlModifier) {
            setIconSelected(self, !self->isSelected());
            m_selectionAnchor = self;
        } else if (self->isSelected() && m_selectedIcons.size() > 1) {
            m_selectionAnchor = self;
        } else {
            clearSelection();
            setIconSelected(self, true);
            m_selectionAnchor = self;
        }
    });
    connect(icon, &DesktopIcon::copyRequested,
            this, [this](DesktopIcon *self, bool move) {
        if (!self) return;
        setFocus(Qt::MouseFocusReason);
        clearFenceSelections();
        if (!self->isSelected()) {
            clearSelection();
            setIconSelected(self, true);
            m_selectionAnchor = self;
        }
        copySelectedIcons(move);
    });
    connect(icon, &DesktopIcon::systemIconMoved,
            this, [this](DesktopIcon *self, const QPoint &newPos) {
        if (!self || !self->item().isSystemIcon) return;
        m_looseIconPositions[self->item().filePath] = newPos;
        layoutLooseIcons();
        saveLayout();
    });
    connect(icon, &DesktopIcon::fileRemoved,
            this, [this](const QString &path) {
        removeLooseIcon(path);
        saveLayout();
    });
    connect(icon, &DesktopIcon::fileRenamed,
            this, [this](const QString &oldPath, const QString &newPath) {
        recordRenameUndo(oldPath, newPath);
        for (auto *ic : m_looseIcons) {
            if (ic->item().filePath == newPath) {
                m_looseIconPositions[newPath] =
                    m_looseIconPositions.take(oldPath);
                break;
            }
        }
        saveLayout();
    });
    // 回收站拖入删除 —— 同时从散落区和所有分区中移除
    connect(icon, &DesktopIcon::filesDroppedToTrash,
            this, [this](const QStringList &paths) {
        recordTrashUndo(paths);
        removePathsFromAllViews(paths);
        scheduleRefresh(300);
        scheduleRefresh(1200);
        refreshTrashState();
    });
    // 多文件拖动：当一个图标开始拖动时，把其他选中文件的 URL 也加入
    connect(icon, &DesktopIcon::dragStarted,
            this, [this](DesktopIcon * /* self */, QList<QUrl> *urls) {
        if (!urls || m_selectedIcons.size() <= 1) return;
        for (auto *sel : m_selectedIcons) {
            if (!sel || sel->item().isSystemIcon) continue;
            const QUrl u = QUrl::fromLocalFile(sel->item().filePath);
            if (!urls->contains(u))
                urls->append(u);
        }
    });
}

void DesktopCanvas::clearLooseIcons()
{
    for (DesktopIcon *icon : m_looseIcons) {
        if (!icon)
            continue;
        icon->hide();
        icon->deleteLater();
    }
    m_looseIcons.clear();
    m_selectedIcons.clear();
    m_selectionAnchor = nullptr;
}

void DesktopCanvas::addSystemIcons()
{
    const QList<DesktopItem> items = {
        DesktopItem::computerItem(),
        DesktopItem::trashItem()
    };

    int insertAt = 0;
    for (const DesktopItem &item : items) {
        if (!item.isValid()) continue;
        if (isInAnyFence(item.filePath))
            continue;

        bool exists = false;
        for (auto *icon : m_looseIcons) {
            if (icon->item().filePath == item.filePath) {
                exists = true;
                break;
            }
        }
        if (exists) {
            ++insertAt;
            continue;
        }

        auto *icon = new DesktopIcon(item, this);
        connectLooseIcon(icon);
        m_looseIcons.insert(qMin(insertAt, m_looseIcons.size()), icon);
        ++insertAt;
        icon->show();
    }

    // 初始化回收站状态
    refreshTrashState();
}

// ── 字体配置 ─────────────────────────────────────────

void DesktopCanvas::setFontFamily(const QString &family)
{
    m_fontFamily = family;
    applyFontToAll();
    saveLayout();
}

void DesktopCanvas::setFontSize(int size)
{
    m_fontSize = qBound(8, size, 24);
    applyFontToAll();
    saveLayout();
}

void DesktopCanvas::setFontColor(const QColor &color)
{
    m_fontColor = color;
    applyFontToAll();
    saveLayout();
}

void DesktopCanvas::setFontBold(bool bold)
{
    m_fontBold = bold;
    applyFontToAll();
    saveLayout();
}

void DesktopCanvas::setFontItalic(bool italic)
{
    m_fontItalic = italic;
    applyFontToAll();
    saveLayout();
}

void DesktopCanvas::applyFontToAll()
{
    for (auto *icon : m_looseIcons) {
        icon->setFontFamily(m_fontFamily);
        icon->setFontSize(m_fontSize);
        icon->setFontColor(m_fontColor);
        icon->setFontBold(m_fontBold);
        icon->setFontItalic(m_fontItalic);
    }
    for (auto *fence : m_fences) {
        fence->setIconFontFamily(m_fontFamily);
        fence->setIconFontSize(m_fontSize);
        fence->setIconFontColor(m_fontColor);
        fence->setIconFontBold(m_fontBold);
        fence->setIconFontItalic(m_fontItalic);
    }
}

void DesktopCanvas::clearFenceSelections()
{
    for (auto *fence : m_fences) {
        if (fence)
            fence->clearIconSelection();
    }
}

void DesktopCanvas::showSettingsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("桌面字体设置");
    dlg.setMinimumWidth(360);

    auto *layout = new QFormLayout(&dlg);

    // 字体族
    auto *fontCombo = new QComboBox(&dlg);
    static const QStringList families = QFontDatabase().families();
    fontCombo->addItem("系统默认", QString());
    for (const QString &f : families)
        fontCombo->addItem(f, f);
    if (!m_fontFamily.isEmpty()) {
        int idx = fontCombo->findData(m_fontFamily);
        if (idx >= 0) fontCombo->setCurrentIndex(idx);
    }
    layout->addRow("字体：", fontCombo);

    // 字号
    auto *sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(8, 24);
    sizeSpin->setValue(m_fontSize);
    sizeSpin->setSuffix(" px");
    layout->addRow("字号：", sizeSpin);

    // 粗体 / 斜体
    auto *boldCheck = new QCheckBox("粗体", &dlg);
    boldCheck->setChecked(m_fontBold);
    layout->addRow("字重：", boldCheck);

    auto *italicCheck = new QCheckBox("斜体", &dlg);
    italicCheck->setChecked(m_fontItalic);
    layout->addRow("字形：", italicCheck);

    // 字体颜色
    QColor chosenColor = m_fontColor;
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
    layout->addRow("字体颜色：", colorBtn);

    // 预览
    auto *preview = new QLabel("预览文字 Preview", &dlg);
    preview->setAlignment(Qt::AlignCenter);
    auto updatePreview = [preview, fontCombo, sizeSpin, boldCheck, italicCheck, &chosenColor] {
        QFont f;
        const QString family = fontCombo->currentData().toString();
        if (!family.isEmpty()) f.setFamily(family);
        f.setPixelSize(sizeSpin->value());
        f.setBold(boldCheck->isChecked());
        f.setItalic(italicCheck->isChecked());
        preview->setFont(f);
        preview->setStyleSheet(
            QString("color: %1; background: #333; padding: 8px; border-radius: 4px;")
                .arg(chosenColor.name()));
    };
    updatePreview();
    connect(fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [updatePreview](int) { updatePreview(); });
    connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [updatePreview](int) { updatePreview(); });
    connect(boldCheck, &QCheckBox::toggled, [updatePreview](bool) { updatePreview(); });
    connect(italicCheck, &QCheckBox::toggled, [updatePreview](bool) { updatePreview(); });
    connect(colorBtn, &QPushButton::clicked, [updatePreview] { updatePreview(); });
    layout->addRow("预览：", preview);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        m_fontFamily = fontCombo->currentData().toString();
        m_fontSize = sizeSpin->value();
        m_fontColor = chosenColor;
        m_fontBold = boldCheck->isChecked();
        m_fontItalic = italicCheck->isChecked();
        applyFontToAll();
        saveLayout();
    }
}

void DesktopCanvas::showDesktopSyncSettingsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("桌面文件同步设置");
    dlg.setMinimumWidth(420);

    auto *layout = new QFormLayout(&dlg);

    auto *inboxCombo = new QComboBox(&dlg);
    inboxCombo->addItem("无（新增文件留在桌面）", QString());
    for (auto *fence : m_fences) {
        if (!fence)
            continue;

        const QString label = QString("%1  (%2)")
            .arg(fence->title())
            .arg(fence->icons().size());
        inboxCombo->addItem(label, fence->fenceId());
    }

    const int currentIndex = inboxCombo->findData(m_desktopInboxFenceId);
    if (currentIndex >= 0)
        inboxCombo->setCurrentIndex(currentIndex);

    layout->addRow("新增桌面文件放入：", inboxCombo);

    auto *hint = new QLabel(&dlg);
    hint->setWordWrap(true);
    hint->setText("选择分区后，外部程序保存到桌面的新文件会自动加入该分区；"
                  "选择“无”时，新文件按普通桌面图标保留在桌面上。");
    layout->addRow("说明：", hint);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        m_desktopInboxFenceId = inboxCombo->currentData().toString();
        saveLayout();
        forceSyncDesktopIcons();
    }
}

void DesktopCanvas::showWallpaperDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Fences 壁纸");
    dlg.setMinimumWidth(460);

    auto *root = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    root->addLayout(form);

    QString chosenPath = m_wallpaperPath;
    const QPixmap systemWallpaper = loadSystemWallpaperPixmap();

    auto *preview = new QLabel(&dlg);
    preview->setFixedSize(260, 160);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(
        "QLabel { background: #101827; border: 1px solid #39445a; "
        "border-radius: 10px; color: #cbd5e1; }");
    form->addRow("预览：", preview);

    auto *pathLabel = new QLabel(&dlg);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow("图片：", pathLabel);

    auto *pathButtons = new QWidget(&dlg);
    auto *pathLayout = new QHBoxLayout(pathButtons);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto *chooseBtn = new QPushButton("选择图片…", pathButtons);
    auto *clearBtn = new QPushButton("清空自定义", pathButtons);
    pathLayout->addWidget(chooseBtn);
    pathLayout->addWidget(clearBtn);
    pathLayout->addStretch();
    form->addRow("", pathButtons);

    auto *modeCombo = new QComboBox(&dlg);
    modeCombo->addItem("系统默认（跟随桌面）",
                       static_cast<int>(WallpaperMode::System));
    modeCombo->addItem("填满（等比裁切）",
                       static_cast<int>(WallpaperMode::Fill));
    modeCombo->addItem("适应（完整显示）",
                       static_cast<int>(WallpaperMode::Fit));
    modeCombo->addItem("拉伸（铺满屏幕）",
                       static_cast<int>(WallpaperMode::Stretch));
    modeCombo->addItem("平铺（原始尺寸重复）",
                       static_cast<int>(WallpaperMode::Tile));
    modeCombo->addItem("居中（原始尺寸）",
                       static_cast<int>(WallpaperMode::Center));

    const int currentMode = static_cast<int>(m_wallpaperMode);
    const int currentModeIndex = modeCombo->findData(currentMode);
    modeCombo->setCurrentIndex(currentModeIndex >= 0 ? currentModeIndex : 0);
    form->addRow("显示方式：", modeCombo);

    auto renderPreview = [](const QPixmap &pix,
                            WallpaperMode mode,
                            const QSize &targetSize) -> QPixmap {
        QImage result(targetSize, QImage::Format_RGB32);
        result.fill(Qt::black);

        if (pix.isNull())
            return QPixmap::fromImage(result);

        const QImage source = pix.toImage();
        QPainter painter(&result);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        switch (mode) {
        case WallpaperMode::Fit: {
            const QImage scaled = source.scaled(targetSize,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            painter.drawImage(QPoint((targetSize.width() - scaled.width()) / 2,
                                     (targetSize.height() - scaled.height()) / 2),
                              scaled);
            break;
        }
        case WallpaperMode::Stretch:
            painter.drawImage(QRect(QPoint(0, 0), targetSize),
                              source, source.rect());
            break;
        case WallpaperMode::Tile:
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.fillRect(QRect(QPoint(0, 0), targetSize),
                             QBrush(QPixmap::fromImage(source)));
            break;
        case WallpaperMode::Center:
            painter.drawImage(QPoint((targetSize.width() - source.width()) / 2,
                                     (targetSize.height() - source.height()) / 2),
                              source);
            break;
        case WallpaperMode::Fill:
        case WallpaperMode::System:
        default: {
            const QImage scaled = source.scaled(targetSize,
                                               Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation);
            const int cropX = qMax(0, (scaled.width() - targetSize.width()) / 2);
            const int cropY = qMax(0, (scaled.height() - targetSize.height()) / 2);
            painter.drawImage(QPoint(0, 0),
                              scaled.copy(cropX, cropY,
                                          targetSize.width(),
                                          targetSize.height()));
            break;
        }
        }

        return QPixmap::fromImage(result);
    };

    auto updatePreview = [&] {
        WallpaperMode mode = static_cast<WallpaperMode>(
            modeCombo->currentData().toInt());
        bool customMode = false;
        QPixmap source;
        if (mode != WallpaperMode::System && !chosenPath.isEmpty()) {
            source = QPixmap(chosenPath);
            customMode = !source.isNull();
        }
        if (!customMode)
            source = systemWallpaper;
        if (source.isNull())
            source = m_wallpaper;

        const WallpaperMode previewMode = customMode ? mode : WallpaperMode::Fill;
        preview->setPixmap(renderPreview(source, previewMode, preview->size()));

        if (chosenPath.isEmpty()) {
            pathLabel->setText("未选择自定义图片，当前使用系统桌面壁纸。");
        } else if (QFileInfo::exists(chosenPath)) {
            pathLabel->setText(chosenPath);
        } else {
            pathLabel->setText("图片不存在，将回退系统壁纸：\n" + chosenPath);
        }
    };

    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [&] { updatePreview(); });
    connect(chooseBtn, &QPushButton::clicked, [&] {
        const QString startDir = chosenPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            : QFileInfo(chosenPath).absolutePath();
        const QString path = QFileDialog::getOpenFileName(
            &dlg,
            "选择 Fences 壁纸",
            startDir,
            "图片文件 (*.jpg *.jpeg *.png *.bmp *.webp *.svg);;所有文件 (*)");
        if (path.isEmpty())
            return;

        QPixmap test(path);
        if (test.isNull()) {
            QMessageBox::warning(&dlg, "Fences 壁纸",
                                 "这张图片无法读取，请换一张图片。");
            return;
        }

        chosenPath = path;
        if (static_cast<WallpaperMode>(modeCombo->currentData().toInt()) ==
            WallpaperMode::System) {
            modeCombo->setCurrentIndex(
                modeCombo->findData(static_cast<int>(WallpaperMode::Fill)));
        }
        updatePreview();
    });
    connect(clearBtn, &QPushButton::clicked, [&] {
        chosenPath.clear();
        modeCombo->setCurrentIndex(
            modeCombo->findData(static_cast<int>(WallpaperMode::System)));
        updatePreview();
    });

    auto *hint = new QLabel(
        "提示：Fences 壁纸只影响 Fences 桌面层，不会修改系统桌面壁纸。"
        "图片只在保存、刷新或窗口尺寸变化时重新生成缓存，不常驻监听文件。",
        &dlg);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #64748b;");
    root->addWidget(hint);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(buttons);

    updatePreview();

    if (dlg.exec() != QDialog::Accepted)
        return;

    WallpaperMode selectedMode = static_cast<WallpaperMode>(
        modeCombo->currentData().toInt());
    if (chosenPath.isEmpty())
        selectedMode = WallpaperMode::System;

    m_wallpaperPath = chosenPath;
    m_wallpaperMode = selectedMode;
    loadWallpaper();
    if (m_monitor)
        m_monitor->refreshWallpaperTheme();
    saveLayout();
}

// ── 壁纸取样建分区 / 网格布局 ───────────────────────────

QImage DesktopCanvas::renderedWallpaperImage() const
{
    if (!m_wallpaperCache.isNull()) {
        return m_wallpaperCache.toImage().scaled(
            size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage image(size(), QImage::Format_RGB32);
    if (m_wallpaper.isNull()) {
        QPainter painter(&image);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor("#1a2a3a"));
        g.setColorAt(1, QColor("#2c5f8a"));
        painter.fillRect(rect(), g);
    }
    return image;
}

QColor DesktopCanvas::sampleFenceColorFromRect(const QRect &selection) const
{
    const QRect bounded = selection.normalized().intersected(rect());
    if (!bounded.isValid() || bounded.width() < 2 || bounded.height() < 2)
        return m_defaultFenceColor;

    const QImage image = renderedWallpaperImage();
    const QImage crop = image.copy(bounded).scaled(
        64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB32);

    double r = 0;
    double g = 0;
    double b = 0;
    double weightSum = 0;

    for (int y = 0; y < crop.height(); ++y) {
        for (int x = 0; x < crop.width(); ++x) {
            const QColor color = QColor::fromRgb(crop.pixel(x, y));
            int h = 0;
            int s = 0;
            int l = 0;
            color.getHsl(&h, &s, &l);
            if (s < 28 || l < 18 || l > 235)
                continue;

            const double weight = 0.35 + (s / 255.0);
            r += color.red() * weight;
            g += color.green() * weight;
            b += color.blue() * weight;
            weightSum += weight;
        }
    }

    QColor sampled = m_defaultFenceColor;
    if (weightSum > 0.01) {
        sampled = QColor(qRound(r / weightSum),
                         qRound(g / weightSum),
                         qRound(b / weightSum));
    }

    int h = 0;
    int s = 0;
    int l = 0;
    sampled.getHsl(&h, &s, &l);
    if (h >= 0) {
        sampled = QColor::fromHsl(h,
                                  qBound(70, s + 25, 220),
                                  qBound(45, l - 10, 145),
                                  110);
    } else {
        sampled.setAlpha(110);
    }
    return sampled;
}

QColor DesktopCanvas::wallpaperColorAt(const QRect &area) const
{
    return sampleFenceColorFromRect(area);
}

void DesktopCanvas::beginWallpaperFenceCapture()
{
    setGlobalEditMode(true);
    m_wallpaperFenceCapture = true;
    m_rubberBanding = false;
    m_rubberRect = QRect();
    setCursor(Qt::CrossCursor);
    update();
}

void DesktopCanvas::finishWallpaperFenceCapture(const QRect &selection)
{
    const QRect geo = selection.normalized().intersected(rect());
    m_wallpaperFenceCapture = false;
    unsetCursor();

    if (!geo.isValid() || geo.width() < 80 || geo.height() < 60) {
        update();
        return;
    }

    QColor color = sampleFenceColorFromRect(geo);
    const int minW = 160;
    const int minH = FenceWidget::TITLE_H + 60;
    QRect finalGeo = geo;
    if (finalGeo.width() < minW)
        finalGeo.setWidth(minW);
    if (finalGeo.height() < minH)
        finalGeo.setHeight(minH);
    finalGeo = finalGeo.intersected(rect());

    auto *fence = createFence("取样分区", finalGeo);
    fence->setFenceColor(color);
    saveLayout();
    update();
}

void DesktopCanvas::showGridDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("桌面图标网格");
    dlg.setMinimumWidth(390);

    auto *layout = new QFormLayout(&dlg);

    auto *preset = new QComboBox(&dlg);
    preset->addItem("中图标（推荐）", QVariantList { 18, 11, 1.0 });
    preset->addItem("小图标", QVariantList { 22, 13, 0.85 });
    preset->addItem("大图标", QVariantList { 15, 9, 1.15 });
    preset->addItem("特大图标", QVariantList { 12, 7, 1.35 });
    preset->addItem("自定义", QVariantList());
    int initialPreset = 4;
    for (int i = 0; i < 4; ++i) {
        const QVariantList values = preset->itemData(i).toList();
        if (values[0].toInt() == m_gridColumns &&
            values[1].toInt() == m_gridRows &&
            qAbs(values[2].toDouble() - m_desktopIconScale) < 0.01) {
            initialPreset = i;
            break;
        }
    }
    preset->setCurrentIndex(initialPreset);
    layout->addRow("默认方案：", preset);

    auto *colsSpin = new QSpinBox(&dlg);
    colsSpin->setRange(4, 40);
    colsSpin->setValue(m_gridColumns);
    layout->addRow("列数 X：", colsSpin);

    auto *rowsSpin = new QSpinBox(&dlg);
    rowsSpin->setRange(3, 24);
    rowsSpin->setValue(m_gridRows);
    layout->addRow("行数 Y：", rowsSpin);

    auto *scaleSpin = new QSpinBox(&dlg);
    scaleSpin->setRange(75, 175);
    scaleSpin->setSuffix("%");
    scaleSpin->setValue(qRound(m_desktopIconScale * 100));
    layout->addRow("图标大小：", scaleSpin);

    auto *hint = new QLabel(
        "仅约束 Fence 外的桌面图标。图标会吸附到 X×Y 网格并自动避开 Fence；"
        "Fence 内部图标布局保持不变。", &dlg);
    hint->setWordWrap(true);
    layout->addRow("说明：", hint);

    auto applyPreset = [=](int index) {
        const QVariantList values = preset->itemData(index).toList();
        if (values.size() != 3)
            return;
        const QSignalBlocker blockColumns(colsSpin);
        const QSignalBlocker blockRows(rowsSpin);
        const QSignalBlocker blockScale(scaleSpin);
        colsSpin->setValue(values[0].toInt());
        rowsSpin->setValue(values[1].toInt());
        scaleSpin->setValue(qRound(values[2].toDouble() * 100));
    };
    connect(preset, qOverload<int>(&QComboBox::currentIndexChanged),
            &dlg, applyPreset);
    connect(colsSpin, qOverload<int>(&QSpinBox::valueChanged),
            &dlg, [=] { preset->setCurrentIndex(4); });
    connect(rowsSpin, qOverload<int>(&QSpinBox::valueChanged),
            &dlg, [=] { preset->setCurrentIndex(4); });
    connect(scaleSpin, qOverload<int>(&QSpinBox::valueChanged),
            &dlg, [=] { preset->setCurrentIndex(4); });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted)
        applyDesktopGrid(colsSpin->value(), rowsSpin->value(),
                         scaleSpin->value() / 100.0);
}

void DesktopCanvas::applyDesktopGrid(int columns, int rows, qreal iconScale)
{
    m_gridColumns = qBound(4, columns, 40);
    m_gridRows = qBound(3, rows, 24);
    m_desktopIconScale = qBound<qreal>(0.75, iconScale, 1.75);
    for (auto *icon : m_looseIcons)
        icon->setVisualScale(m_desktopIconScale);
    // Fence 内部维持用户已有设计，不跟随桌面网格的图标档位。
    layoutLooseIcons();
    saveLayout();
}

void DesktopCanvas::setClockWidgetVisible(bool visible)
{
    if (visible && !m_clockWidget) {
        m_clockWidget = new ClockDesklet(this);
        m_clockWidget->setEditMode(m_editMode);
        m_clockWidget->installEventFilter(this);
    }
    if (!m_clockWidget) return;
    if (visible) m_clockWidget->reveal(); else m_clockWidget->hide();
}
void DesktopCanvas::setActivityWidgetVisible(bool visible)
{
    if (visible && !m_activityRecorder) m_activityRecorder = new ActivityRecorder(this);
    if (visible && !m_activityWidget) {
        m_activityWidget = new ActivityDesklet(this, m_activityRecorder);
        m_activityWidget->setEditMode(m_editMode);
        m_activityWidget->installEventFilter(this);
    }
    if (!m_activityWidget) return;
    if (visible) m_activityWidget->reveal(); else m_activityWidget->hide();
}
void DesktopCanvas::setMusicWidgetVisible(bool visible)
{
    if (visible && !m_musicWidget) {
        m_musicWidget = new MusicDesklet(this);
        m_musicWidget->setEditMode(m_editMode);
        m_musicWidget->installEventFilter(this);
    }
    if (!m_musicWidget) return;
    if (visible) m_musicWidget->reveal(); else m_musicWidget->hide();
}
bool DesktopCanvas::musicWidgetVisible() const { return m_musicWidget && m_musicWidget->isVisible(); }
void DesktopCanvas::setCalendarWidgetVisible(bool visible)
{
    if (visible && !m_calendarWidget) {
        m_calendarWidget = new CalendarDesklet(this);
        m_calendarWidget->setEditMode(m_editMode);
        m_calendarWidget->installEventFilter(this);
    }
    if (!m_calendarWidget) return;
    if (visible) m_calendarWidget->reveal(); else m_calendarWidget->hide();
}
bool DesktopCanvas::calendarWidgetVisible() const { return m_calendarWidget && m_calendarWidget->isVisible(); }
bool DesktopCanvas::clockWidgetVisible() const { return m_clockWidget && m_clockWidget->isVisible(); }
bool DesktopCanvas::activityWidgetVisible() const { return m_activityWidget && m_activityWidget->isVisible(); }
QString DesktopCanvas::desktopWidgetsStatus() const
{
    QJsonObject result;
    for (auto *w : {static_cast<LiquidDesklet *>(m_clockWidget), static_cast<LiquidDesklet *>(m_activityWidget), static_cast<LiquidDesklet *>(m_musicWidget), static_cast<LiquidDesklet *>(m_calendarWidget)}) {
        if (!w) continue;
        result.insert(w->objectName(), QJsonObject{{"visible",w->isVisible()}, {"x",w->x()}, {"y",w->y()},
            {"width",w->width()}, {"height",w->height()}, {"materialBuilds",w->materialBuilds()},
            {"gpu",w->property("liquidOpticalGpu").toBool()}});
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void DesktopCanvas::showDesklet()
{
    if (m_monitor) {
        m_monitor->deleteLater();
        m_monitor = nullptr;
        update();
        return;
    }

    // 小组件必须属于桌面画布，而不是独立的置顶窗口。作为子控件后，
    // 它天然跟随桌面显示/隐藏，也绝不会覆盖普通应用。
    m_monitor = new SystemMonitor(this);
    m_monitor->setEditMode(m_editMode);
    // 确保小组件在画布右上方可见区域展示，避免初始定位到屏幕外
    if (m_monitor->x() < 0 || m_monitor->y() < 0 ||
        m_monitor->x() > width() - 100 ||
        m_monitor->y() > height() - 100) {
        m_monitor->move(qMax(16, width() - m_monitor->width() - 24), 48);
    }
    m_monitor->show();
    m_monitor->raise();
    connect(m_monitor, &SystemMonitor::closeRequested,
            this, [this] { showDesklet(); });
    connect(m_monitor, &SystemMonitor::appearanceChanged, this, [this] {
        if (m_smartSpace)
            m_smartSpace->refreshInheritedTheme();
    });
    update();
}

void DesktopCanvas::toggleDesklet()
{
    showDesklet();
}

void DesktopCanvas::setSystemMonitorSkin(int skin)
{
    if (skin < static_cast<int>(SystemMonitor::Skin::Dark) ||
        skin > static_cast<int>(SystemMonitor::Skin::Liquid))
        return;
    setSystemMonitorVisible(true);
    if (m_monitor)
        m_monitor->setSkin(static_cast<SystemMonitor::Skin>(skin));
}

int DesktopCanvas::systemMonitorSkin() const
{
    if (m_monitor)
        return static_cast<int>(m_monitor->skin());
    QSettings settings;
    return settings.value(QStringLiteral("systemMonitor/skin"),
                         static_cast<int>(SystemMonitor::Skin::Liquid)).toInt();
}

void DesktopCanvas::setSystemMonitorVisible(bool visible)
{
    if (visible == (m_monitor != nullptr)) {
        if (visible) {
            m_monitor->show();
            m_monitor->raise();
        }
        return;
    }
    showDesklet();
}

void DesktopCanvas::showSystemMonitorWidget()
{
    showAndActivate();
    setSystemMonitorVisible(true);
}

void DesktopCanvas::showSmartSpace()
{
    if (m_smartSpace) {
        SmartSpaceWidget *closingWidget = m_smartSpace;
        m_smartSpace = nullptr;
        closingWidget->deleteLater();
        m_smartSpaceRelayoutTimer->start();
        QTimer::singleShot(200, this, [] { releaseUnusedHeap(); });
        update();
        return;
    }

    // Match SystemMonitor's proven desktop-child model by default.  The user
    // may explicitly promote this same widget to an owned tool window through
    // the pin control; no second copy or wrapper is created.
    m_smartSpace = new SmartSpaceWidget(false, this);
    m_smartSpace->setEditMode(m_editMode);
    connect(m_smartSpace, &SmartSpaceWidget::closeRequested,
            this, [this] { showSmartSpace(); });
    connect(m_smartSpace, &SmartSpaceWidget::alwaysOnTopChanged,
            this, &DesktopCanvas::applySmartSpaceWindowMode);
    connect(m_smartSpace, &SmartSpaceWidget::geometryChanged,
            m_smartSpaceRelayoutTimer,
            qOverload<>(&QTimer::start));
    SmartSpaceWidget *createdWidget = m_smartSpace;
    connect(createdWidget, &QObject::destroyed, this, [this, createdWidget] {
        if (m_smartSpace == createdWidget)
            m_smartSpace = nullptr;
    });
    applySmartSpaceWindowMode(m_smartSpace->alwaysOnTop());
    m_smartSpace->show();
    m_smartSpace->raise();
    m_smartSpaceRelayoutTimer->start();
    update();
}

void DesktopCanvas::applySmartSpaceWindowMode(bool alwaysOnTop)
{
    if (!m_smartSpace)
        return;

    const bool wasVisible = m_smartSpace->isVisible();
    const QSize widgetSize = m_smartSpace->size();
    const QPoint globalPosition = m_smartSpace->isWindow()
        ? m_smartSpace->pos()
        : m_smartSpace->mapToGlobal(QPoint(0, 0));

    if (alwaysOnTop) {
        // A binary QRegion mask has hard one-pixel steps on a native X11
        // window.  Recreating the pinned tool window with an ARGB surface lets
        // SmartSpaceWidget's antialiased QPainterPath provide smooth corners.
        m_smartSpace->setParent(
            this,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_smartSpace->recreateNativeSurface(true);
        m_smartSpace->setAttribute(Qt::WA_ShowWithoutActivating, true);
        m_smartSpace->resize(widgetSize);
        m_smartSpace->move(globalPosition);
    } else {
        m_smartSpace->setAttribute(Qt::WA_ShowWithoutActivating, false);
        m_smartSpace->setParent(this, Qt::Widget);
        // Desktop children deliberately use the stable binary-mask path: on
        // UKUI/X11 an ARGB native child can disappear after Show Desktop.
        m_smartSpace->recreateNativeSurface(false);
        m_smartSpace->resize(widgetSize);
        m_smartSpace->move(mapFromGlobal(globalPosition));
    }

    if (wasVisible) {
        m_smartSpace->show();
        m_smartSpace->raise();
    }
}

void DesktopCanvas::setSmartSpaceVisible(bool visible)
{
    if (visible == (m_smartSpace != nullptr)) {
        if (visible) {
            m_smartSpace->show();
            m_smartSpace->raise();
        }
        return;
    }
    showSmartSpace();
}

void DesktopCanvas::showSmartSpaceWidget()
{
    showAndActivate();
    setSmartSpaceVisible(true);
}

void DesktopCanvas::setSmartSpaceAlwaysOnTop(bool enabled)
{
    setSmartSpaceVisible(true);
    if (!m_smartSpace)
        return;
    if (m_smartSpace->alwaysOnTop() == enabled)
        applySmartSpaceWindowMode(enabled);
    else
        m_smartSpace->setAlwaysOnTop(enabled);
}

bool DesktopCanvas::smartSpaceAlwaysOnTop() const
{
    return m_smartSpace && m_smartSpace->alwaysOnTop();
}

void DesktopCanvas::hideSmartSpaceToEdge()
{
    setSmartSpaceVisible(true);
    if (m_smartSpace)
        m_smartSpace->hideToNearestEdge();
}

void DesktopCanvas::revealSmartSpaceFromEdge()
{
    if (m_smartSpace)
        m_smartSpace->revealFromEdge();
}

bool DesktopCanvas::smartSpaceEdgeHidden() const
{
    return m_smartSpace && m_smartSpace->edgeHidden();
}

int DesktopCanvas::smartSpaceX() const
{
    return m_smartSpace ? m_smartSpace->x() : -1;
}

int DesktopCanvas::smartSpaceY() const
{
    return m_smartSpace ? m_smartSpace->y() : -1;
}

int DesktopCanvas::smartSpaceWidth() const
{
    return m_smartSpace ? m_smartSpace->width() : 0;
}

int DesktopCanvas::smartSpaceHeight() const
{
    return m_smartSpace ? m_smartSpace->height() : 0;
}

void DesktopCanvas::moveSmartSpace(int x, int y)
{
    setSmartSpaceVisible(true);
    if (!m_smartSpace)
        return;
    const int maxX = qMax(0, width() - m_smartSpace->width());
    const int maxY = qMax(0, height() - m_smartSpace->height());
    m_smartSpace->move(qBound(0, x, maxX), qBound(0, y, maxY));
    m_smartSpace->raise();
}

void DesktopCanvas::resizeSmartSpace(int width, int height)
{
    setSmartSpaceVisible(true);
    if (!m_smartSpace)
        return;
    m_smartSpace->resize(qBound(620, width, 2400),
                         qBound(360, height, 1600));
    m_smartSpace->raise();
}

void DesktopCanvas::setSmartSpaceDensity(int density)
{
    setSmartSpaceVisible(true);
    if (m_smartSpace)
        m_smartSpace->setResultDensity(density);
}

void DesktopCanvas::showSmartSpaceSettings()
{
    setSmartSpaceVisible(true);
    if (m_smartSpace) {
        SmartSpaceWidget *widget = m_smartSpace;
        // A modal dialog must be entered after the D-Bus method has returned;
        // otherwise synchronous callers block until the dialog is closed.
        QTimer::singleShot(0, widget, [widget] { widget->showSettings(); });
    }
}

void DesktopCanvas::startSmartSpaceFullIndex()
{
    setSmartSpaceVisible(true);
    if (m_smartSpace) {
        SmartSpaceWidget *widget = m_smartSpace;
        QTimer::singleShot(0, widget, [widget] { widget->startFastFullIndex(); });
    }
}

bool DesktopCanvas::smartSpaceIndexBusy() const
{
    return m_smartSpace && m_smartSpace->indexBusy();
}

void DesktopCanvas::hideSmartSpaceFolder(const QString &path)
{
    setSmartSpaceVisible(true);
    if (m_smartSpace)
        m_smartSpace->hideFolder(path);
}

void DesktopCanvas::excludeSmartSpaceFolder(const QString &path)
{
    setSmartSpaceVisible(true);
    if (m_smartSpace) {
        SmartSpaceWidget *widget = m_smartSpace;
        QTimer::singleShot(0, widget, [widget, path] { widget->excludeFolder(path); });
    }
}

bool DesktopCanvas::smartSpaceOverlapsDesktopIcons() const
{
    const QRect reserved = smartSpaceReservedGeometry();
    if (!reserved.isValid())
        return false;
    for (DesktopIcon *icon : m_looseIcons) {
        if (icon && icon->isVisible() && icon->geometry().intersects(reserved))
            return true;
    }
    return false;
}

void DesktopCanvas::toggleSmartSpace()
{
    showSmartSpace();
}

// ── 桌面图标刷新 ─────────────────────────────────────────

void DesktopCanvas::refreshDesktopIcons()
{
    syncDesktopIcons(false);
}

void DesktopCanvas::forceSyncDesktopIcons()
{
    refreshDesktopDirectories();

    if (m_debounce)
        m_debounce->stop();

    // QFileSystemWatcher may silently drop a watched directory after the
    // desktop folder is replaced or temporarily unavailable. Rebinding the
    // desktop paths makes the manual refresh a real recovery action.
    if (m_watcher) {
        const QStringList watchedDirs = m_watcher->directories();
        for (const QString &path : watchedDirs) {
            if (m_desktopPaths.contains(path) || !QFileInfo(path).exists())
                m_watcher->removePath(path);
        }
    }
    ensureDesktopWatches();
    ensureTrashWatches();
    addSystemIcons();

    syncDesktopIcons(true);

    // Some applications save by writing a temp file and renaming it shortly
    // after the user-visible save action. Follow-up scans catch that final
    // name even when the first manual refresh lands in the middle of the save.
    scheduleRefresh(450);
    scheduleRefresh(1400);
}

void DesktopCanvas::logDesktopSyncAudit(const QString &reason,
                                        const QSet<QString> &currentPaths) const
{
    const QRect area = desktopIconArea();
    writeSyncDebug(QStringLiteral("SYNC_AUDIT"),
        QStringLiteral("reason=%1 desktopPath=%2 desktopPaths=%3 watchedDirs=%4 currentCount=%5 looseCount=%6 fenceCount=%7 positionCount=%8 area=%9 paths=%10")
            .arg(reason)
            .arg(m_desktopPath)
            .arg(m_desktopPaths.join(QStringLiteral(" | ")))
            .arg(m_watcher ? m_watcher->directories().join(QStringLiteral(" | "))
                            : QString())
            .arg(currentPaths.size())
            .arg(m_looseIcons.size())
            .arg(m_fences.size())
            .arg(m_looseIconPositions.size())
            .arg(rectToString(area))
            .arg(sortedPathList(currentPaths)));

    QStringList paths = currentPaths.values();
    paths.sort(Qt::CaseInsensitive);
    for (const QString &path : paths) {
        DesktopIcon *looseIcon = nullptr;
        int looseMatches = 0;
        for (DesktopIcon *icon : m_looseIcons) {
            if (!icon || icon->item().isSystemIcon)
                continue;
            if (sameStoredPath(icon->item().filePath, path)) {
                ++looseMatches;
                if (!looseIcon)
                    looseIcon = icon;
            }
        }

        FenceWidget *fenceMatch = nullptr;
        for (FenceWidget *fence : m_fences) {
            if (!fence)
                continue;
            if (fence->hasItem(path)) {
                fenceMatch = fence;
                break;
            }
        }

        const DesktopItem item = DesktopItem::fromPath(path);
        const bool validItem = item.isValid();
        const bool hasPosition = m_looseIconPositions.contains(path);
        const QPoint storedPos = m_looseIconPositions.value(path, QPoint(-1, -1));
        const QRect iconGeometry = looseIcon ? looseIcon->geometry() : QRect();
        const bool visible = looseIcon && looseIcon->isVisible();
        const bool inArea = !looseIcon || area.intersects(iconGeometry);
        bool intersectsFence = false;
        if (looseIcon) {
            for (FenceWidget *fence : m_fences) {
                if (fence && iconGeometry.intersects(fence->geometry())) {
                    intersectsFence = true;
                    break;
                }
            }
        }

        QString state = QStringLiteral("OK");
        QString where = QStringLiteral("loose");
        if (fenceMatch) {
            where = QStringLiteral("fence:%1").arg(fenceMatch->fenceId());
        } else if (!validItem) {
            state = QStringLiteral("PROBLEM invalid-desktop-item");
            where = QStringLiteral("none");
        } else if (looseMatches == 0) {
            state = QStringLiteral("PROBLEM missing-from-ui");
            where = QStringLiteral("none");
        } else if (looseMatches > 1) {
            state = QStringLiteral("PROBLEM duplicate-loose-icons");
        } else if (!visible) {
            state = QStringLiteral("PROBLEM hidden-icon");
        } else if (!hasPosition) {
            state = QStringLiteral("PROBLEM no-position");
        } else if (!inArea) {
            state = QStringLiteral("PROBLEM outside-icon-area");
        } else if (intersectsFence) {
            state = QStringLiteral("PROBLEM loose-icon-under-fence");
        }

        const bool shouldLogPath =
            reason != QStringLiteral("heartbeat") ||
            state.startsWith(QLatin1String("PROBLEM"));
        if (!shouldLogPath)
            continue;

        writeSyncDebug(QStringLiteral("SYNC_PATH"),
            QStringLiteral("state=%1 where=%2 path=%3 valid=%4 looseMatches=%5 visible=%6 hasPosition=%7 storedPos=%8 geometry=%9 mtime=%10")
                .arg(state)
                .arg(where)
                .arg(path)
                .arg(validItem ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(looseMatches)
                .arg(visible ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(hasPosition ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(pointToString(storedPos))
                .arg(rectToString(iconGeometry))
                .arg(QFileInfo(path).lastModified().toString(Qt::ISODateWithMs)));
    }
}

void DesktopCanvas::syncDesktopIcons(bool force)
{
    refreshDesktopDirectories();
    ensureDesktopWatches();

    // 1. 建立当前桌面路径集合
    const QSet<QString> currentPaths = collectDesktopFilePaths();
    static QSet<QString> lastAuditedPaths;
    static int syncAuditHeartbeat = 0;
    const bool pathSetChanged = currentPaths != lastAuditedPaths;

    bool changed = false;
    QSet<QString> knownLoosePaths;
    for (DesktopIcon *icon : m_looseIcons) {
        if (icon && !icon->item().isSystemIcon)
            knownLoosePaths.insert(icon->item().filePath);
    }
    for (auto it = m_looseIconPositions.constBegin();
         it != m_looseIconPositions.constEnd(); ++it) {
        if (!it.key().startsWith(QLatin1String("computer://")) &&
            !it.key().startsWith(QLatin1String("trash://")))
            knownLoosePaths.insert(it.key());
    }

    if (force) {
        for (int i = m_looseIcons.size() - 1; i >= 0; --i) {
            DesktopIcon *icon = m_looseIcons[i];
            if (!icon || icon->item().isSystemIcon)
                continue;
            m_selectedIcons.remove(icon);
            if (m_selectionAnchor == icon)
                m_selectionAnchor = nullptr;
            icon->hide();
            icon->deleteLater();
            m_looseIcons.removeAt(i);
        }
        changed = true;
    }

    // 清理已经不存在的散落图标坐标。旧版本只删除控件，没有清理那些
    // “启动前就已被移走”的路径，配置文件会长期残留幽灵坐标。
    for (auto it = m_looseIconPositions.begin();
         it != m_looseIconPositions.end();) {
        const QString path = it.key();
        const bool systemItem =
            path.startsWith(QLatin1String("computer://")) ||
            path.startsWith(QLatin1String("trash://"));
        if (!systemItem && (!currentPaths.contains(path) || isInAnyFence(path))) {
            it = m_looseIconPositions.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    // 2. 移除已消失的散落图标
    for (int i = m_looseIcons.size() - 1; i >= 0; --i) {
        if (m_looseIcons[i]->item().isSystemIcon)
            continue;
        if (!currentPaths.contains(m_looseIcons[i]->item().filePath)) {
            m_selectedIcons.remove(m_looseIcons[i]);
            if (m_selectionAnchor == m_looseIcons[i])
                m_selectionAnchor = nullptr;
            m_looseIconPositions.remove(m_looseIcons[i]->item().filePath);
            m_looseIcons[i]->deleteLater();
            m_looseIcons.removeAt(i);
            changed = true;
        }
    }

    // 2.5 ★ 移除分区中已消失的文件（拖到回收站或外部删除后清理残留图标）
    for (auto *fence : m_fences) {
        if (!fence) continue;
        const QList<DesktopItem> fenceItems = fence->items();
        for (const DesktopItem &item : fenceItems) {
            if (!storedItemExists(item)) {
                fence->removeItem(item.filePath);
                changed = true;
            }
        }
    }

    FenceWidget *inboxFence = desktopInboxFence();

    // 3. 收集已有散落图标路径，避免重复；强制同步时顺手清理重复控件。
    QSet<QString> existingLoose;
    for (int i = m_looseIcons.size() - 1; i >= 0; --i) {
        DesktopIcon *icon = m_looseIcons[i];
        if (!icon)
            continue;

        const QString path = icon->item().filePath;
        if (force && !icon->item().isSystemIcon &&
            existingLoose.contains(path)) {
            m_selectedIcons.remove(icon);
            if (m_selectionAnchor == icon)
                m_selectionAnchor = nullptr;
            icon->deleteLater();
            m_looseIcons.removeAt(i);
            changed = true;
            continue;
        }

        existingLoose.insert(path);

        if (force && !icon->item().isSystemIcon && currentPaths.contains(path)) {
            const DesktopItem refreshed = DesktopItem::fromPath(path);
            if (refreshed.isValid())
                icon->setItem(refreshed);
            icon->show();
            icon->raise();
            icon->update();
        }
    }

    // 4. 添加新出现的文件/目录
    bool addedNew = false;
    for (const QString &path : currentPaths) {
        if (isInAnyFence(path) || existingLoose.contains(path)) {
            if (pathSetChanged || force) {
                writeSyncDebug(QStringLiteral("SYNC_SKIP"),
                    QStringLiteral("path=%1 reason=%2")
                        .arg(path,
                             isInAnyFence(path) ? QStringLiteral("already-in-fence")
                                                : QStringLiteral("already-loose")));
            }
            continue;
        }

        DesktopItem item = DesktopItem::fromPath(path);
        if (!item.isValid()) {
            writeSyncDebug(QStringLiteral("SYNC_SKIP"),
                QStringLiteral("path=%1 reason=DesktopItem::fromPath-invalid exists=%2 isDir=%3 readable=%4")
                    .arg(path,
                         QFileInfo::exists(path) ? QStringLiteral("yes") : QStringLiteral("no"),
                         QFileInfo(path).isDir() ? QStringLiteral("yes") : QStringLiteral("no"),
                         QFileInfo(path).isReadable() ? QStringLiteral("yes") : QStringLiteral("no")));
            continue;
        }

        if (inboxFence && !item.isSystemIcon &&
            !knownLoosePaths.contains(path)) {
            inboxFence->addItem(item);
            writeSyncDebug(QStringLiteral("SYNC_ROUTE"),
                QStringLiteral("path=%1 targetFence=%2 title=%3 source=new")
                    .arg(item.filePath,
                         inboxFence->fenceId(),
                         inboxFence->title()));
            changed = true;
            addedNew = true;
            continue;
        }

        auto *icon = new DesktopIcon(item, this);
        connectLooseIcon(icon);
        m_looseIcons.append(icon);
        icon->show();
        icon->raise();
        icon->update();
        writeSyncDebug(QStringLiteral("SYNC_ADD"),
            QStringLiteral("path=%1 displayName=%2 mime=%3")
                .arg(item.filePath, item.displayName, item.mimeType));
        changed = true;
        addedNew = true;
    }

    // 新增了图标后，把 show/raise 事件排空，确保控件几何尺寸就绪后再布局。
    // 否则 layoutLooseIcons 可能在控件未完全初始化的状态下 move()，导致
    // KWin compositor 错过子控件区域，出现"数据已同步但不显示"的问题。
    if (addedNew)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // 这里不能只在 changed=true 时布局。真实桌面上可能出现"路径和坐标
    // 已在 layout 中，但对应子控件被隐藏/未 move/show"的半同步状态；
    // 这种情况下文件集合没有变化，手动刷新也必须重新压一次散落图标布局。
    if (changed || force)
        assignLooseIconsToFirstCells();
    layoutLooseIcons();
    if (changed || force || addedNew)
        revealLooseIcons();
    syncCutVisualState();
    if (changed || force)
        saveLayout();

    ++syncAuditHeartbeat;
    if (force || changed || addedNew || pathSetChanged ||
        syncAuditHeartbeat >= 20) {
        logDesktopSyncAudit(
            force ? QStringLiteral("force")
                  : (changed || addedNew || pathSetChanged)
                        ? QStringLiteral("changed")
                        : QStringLiteral("heartbeat"),
            currentPaths);
        lastAuditedPaths = currentPaths;
        syncAuditHeartbeat = 0;
    }
}

// ── 多文件拖动 ───────────────────────────────────────

void DesktopCanvas::startMultiDrag()
{
    QStringList paths = selectedFilePaths();
    if (paths.isEmpty()) return;

    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
    drag->setMimeData(mime);

    // 第一个图标作为拖动缩略图
    if (!m_selectedIcons.isEmpty()) {
        DesktopIcon *first = *m_selectedIcons.constBegin();
        if (first && !first->item().icon.isNull())
            drag->setPixmap(first->item().icon.pixmap(48, 48));
    }
    drag->exec(Qt::MoveAction | Qt::CopyAction);
}

// ── 回收站状态刷新 ───────────────────────────────────

void DesktopCanvas::refreshTrashState()
{
    for (auto *icon : m_looseIcons) {
        if (icon->item().isSystemIcon &&
            icon->item().filePath == QLatin1String("trash:///")) {
            icon->refreshTrashIcon();
        }
    }
    for (auto *fence : m_fences) {
        if (!fence) continue;
        for (auto *icon : fence->icons()) {
            if (icon && icon->item().isSystemIcon &&
                icon->item().filePath == QLatin1String("trash:///"))
                icon->refreshTrashIcon();
        }
    }
}

void DesktopCanvas::syncCutVisualState()
{
    pruneMissingFileIcons();

    const FileClipboard::ClipboardFiles files = FileClipboard::readFiles();
    QSet<QString> cutPaths;
    QStringList watchedCutPaths;

    if (files.move) {
        for (const QString &path : files.paths) {
            const QString normalized = normalizedStoredPath(path);
            if (normalized.isEmpty()) continue;

            watchedCutPaths << normalized;
            if (QFileInfo::exists(normalized))
                cutPaths.insert(normalized);
        }
    }

    watchedCutPaths.sort(Qt::CaseInsensitive);
    if (watchedCutPaths != m_cutClipboardPaths) {
        m_cutClipboardPaths = watchedCutPaths;
        m_cutClipboardStartedMs = watchedCutPaths.isEmpty()
            ? 0
            : QDateTime::currentMSecsSinceEpoch();
    }

    const bool staleCutVisual =
        !cutPaths.isEmpty() &&
        m_cutClipboardStartedMs > 0 &&
        QDateTime::currentMSecsSinceEpoch() - m_cutClipboardStartedMs > 3500;
    if (staleCutVisual)
        cutPaths.clear();

    updateCutPathWatches(watchedCutPaths);

    for (auto *icon : m_looseIcons) {
        if (!icon) continue;
        const QString path = normalizedStoredPath(icon->item().filePath);
        icon->setCut(!icon->item().isSystemIcon && cutPaths.contains(path));
    }

    for (auto *fence : m_fences) {
        if (fence)
            fence->syncCutVisualState(cutPaths);
    }

    if (m_cutRefreshTimer) {
        if (files.move && !files.paths.isEmpty() && !cutPaths.isEmpty()) {
            m_cutRefreshGraceTicks = 3;
            if (!m_cutRefreshTimer->isActive())
                m_cutRefreshTimer->start();
        } else if (files.move && !files.paths.isEmpty() &&
                   m_cutRefreshGraceTicks > 0) {
            --m_cutRefreshGraceTicks;
            if (!m_cutRefreshTimer->isActive())
                m_cutRefreshTimer->start();
        } else {
            m_cutRefreshGraceTicks = 0;
            m_cutRefreshTimer->stop();
        }
    }
}

void DesktopCanvas::sortLooseIcons()
{
    switch (m_arrangeMode) {
    case ArrangeMode::ByName:
        std::stable_sort(m_looseIcons.begin(), m_looseIcons.end(),
            [](DesktopIcon *a, DesktopIcon *b) {
                if (a->item().isSystemIcon || b->item().isSystemIcon)
                    return systemIconsFirst(a, b);
                return a->item().displayName.localeAwareCompare(
                    b->item().displayName) < 0;
            });
        break;
    case ArrangeMode::ByType:
        std::stable_sort(m_looseIcons.begin(), m_looseIcons.end(),
            [](DesktopIcon *a, DesktopIcon *b) {
                if (a->item().isSystemIcon || b->item().isSystemIcon)
                    return systemIconsFirst(a, b);
                if (a->item().isDir != b->item().isDir)
                    return a->item().isDir;
                if (a->item().mimeType != b->item().mimeType)
                    return a->item().mimeType < b->item().mimeType;
                return a->item().displayName.localeAwareCompare(
                    b->item().displayName) < 0;
            });
        break;
    case ArrangeMode::ByModifiedTime:
        std::stable_sort(m_looseIcons.begin(), m_looseIcons.end(),
            [](DesktopIcon *a, DesktopIcon *b) {
                if (a->item().isSystemIcon || b->item().isSystemIcon)
                    return systemIconsFirst(a, b);
                return QFileInfo(a->item().filePath).lastModified()
                    > QFileInfo(b->item().filePath).lastModified();
            });
        break;
    case ArrangeMode::Manual:
    default:
        break;
    }
}

void DesktopCanvas::autoArrangeGrid()
{
    const int iconW = qRound(80 * m_iconScale);
    const int iconH = qRound(104 * m_iconScale);
    constexpr int gap = 8;
    constexpr int marginX = 16, marginY = 16;
    const int screenH = QApplication::primaryScreen()->geometry().height();
    const int maxRows = qMax(1, (screenH - marginY * 2) / (iconH + gap));

    sortLooseIcons();
    m_looseIconPositions.clear();

    int col = 0, row = 0;
    for (auto *icon : m_looseIcons) {
        const QPoint pos(marginX + col * (iconW + gap),
                         marginY + row * (iconH + gap));
        m_looseIconPositions[icon->item().filePath] = pos;
        icon->move(pos);
        icon->show();
        if (++row >= maxRows) {
            row = 0;
            ++col;
        }
    }
    update();
}

void DesktopCanvas::disableAutoArrangeForManualPlacement()
{
    if (!m_autoArrange)
        return;

    m_autoArrange = false;
}

QRect DesktopCanvas::desktopIconArea() const
{
    QRect area = rect();
    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const QRect localAvailable = available.translated(-geometry().topLeft());
        const QRect clipped = localAvailable.intersected(rect());
        if (clipped.width() > 120 && clipped.height() > 120)
            area = clipped;
    }

    const int topInset = area.top() > 0 ? 12 : 48;
    area = area.adjusted(12, topInset, -12, -16);
    if (area.width() < 120 || area.height() < 120)
        area = rect().adjusted(12, 48, -12, -16);
    return area;
}

QRect DesktopCanvas::smartSpaceReservedGeometry() const
{
    if (!m_smartSpace || !m_smartSpace->isVisible())
        return QRect();

    const QPoint topLeft = m_smartSpace->isWindow()
        ? mapFromGlobal(m_smartSpace->frameGeometry().topLeft())
        : m_smartSpace->geometry().topLeft();
    return QRect(topLeft, m_smartSpace->size())
        .adjusted(-8, -8, 8, 8).intersected(rect());
}

QList<QRect> DesktopCanvas::desktopWidgetReservedGeometries() const
{
    QList<QRect> reserved;
    const QRect smart = smartSpaceReservedGeometry();
    if (smart.isValid()) reserved.append(smart);
    for (auto *widget : {static_cast<LiquidDesklet *>(m_clockWidget),
                         static_cast<LiquidDesklet *>(m_activityWidget), static_cast<LiquidDesklet *>(m_musicWidget), static_cast<LiquidDesklet *>(m_calendarWidget)}) {
        if (widget && widget->isVisible())
            reserved.append(widget->geometry().adjusted(-8,-8,8,8).intersected(rect()));
    }
    return reserved;
}

void DesktopCanvas::assignLooseIconsToFirstCells()
{
    const int columns = qMax(4, m_gridColumns);
    const int rows = qMax(3, m_gridRows);
    const QRect area = desktopIconArea();
    const qreal cellW = area.width() / qreal(columns);
    const qreal cellH = area.height() / qreal(rows);

    QList<QRect> fenceRects;
    for (auto *fence : m_fences)
        if (fence) fenceRects.append(fence->geometry());
    fenceRects.append(desktopWidgetReservedGeometries());

    std::stable_sort(m_looseIcons.begin(), m_looseIcons.end(),
        [](DesktopIcon *a, DesktopIcon *b) {
            if (!a || !b)
                return a != nullptr;
            if (a->item().isSystemIcon || b->item().isSystemIcon)
                return systemIconsFirst(a, b);

            const QDateTime am =
                QFileInfo(a->item().filePath).lastModified();
            const QDateTime bm =
                QFileInfo(b->item().filePath).lastModified();
            if (am != bm)
                return am > bm;

            return a->item().displayName.localeAwareCompare(
                b->item().displayName) < 0;
        });

    QSet<int> occupied;
    auto cellPosition = [&](int col, int row, DesktopIcon *icon) {
        const QPoint center(
            qRound(area.left() + (col + 0.5) * cellW),
            qRound(area.top() + (row + 0.5) * cellH));
        QPoint pos = center - QPoint(icon->width() / 2, icon->height() / 2);
        pos.setX(qBound(area.left(), pos.x(),
                        qMax(area.left(), area.right() - icon->width())));
        pos.setY(qBound(area.top(), pos.y(),
                        qMax(area.top(), area.bottom() - icon->height())));
        return pos;
    };
    auto cellAvailable = [&](int col, int row, DesktopIcon *icon) {
        const int key = row * columns + col;
        if (occupied.contains(key))
            return false;
        const QRect iconRect(cellPosition(col, row, icon), icon->size());
        for (const QRect &fenceRect : fenceRects)
            if (iconRect.intersects(fenceRect))
                return false;
        return true;
    };
    auto cellForPosition = [&](const QPoint &pos, DesktopIcon *icon) {
        const QPoint center = pos + QPoint(icon->width() / 2,
                                           icon->height() / 2);
        const int col = qBound(0,
            qRound((center.x() - area.left()) / cellW - 0.5),
            columns - 1);
        const int row = qBound(0,
            qRound((center.y() - area.top()) / cellH - 0.5),
            rows - 1);
        return QPoint(col, row);
    };

    QSet<QString> positioned;
    for (DesktopIcon *icon : m_looseIcons) {
        if (!icon)
            continue;

        const QString path = icon->item().filePath;
        if (!m_looseIconPositions.contains(path))
            continue;

        const QPoint pos = m_looseIconPositions.value(path);
        const QPoint cell = cellForPosition(pos, icon);
        const int key = cell.y() * columns + cell.x();
        const QRect iconRect(pos, icon->size());
        bool intersectsFence = false;
        for (const QRect &fenceRect : fenceRects) {
            if (iconRect.intersects(fenceRect)) {
                intersectsFence = true;
                break;
            }
        }

        if (!occupied.contains(key) && area.intersects(iconRect) &&
            !intersectsFence) {
            occupied.insert(key);
            positioned.insert(path);
        }
    }

    for (DesktopIcon *icon : m_looseIcons) {
        if (!icon)
            continue;
        if (positioned.contains(icon->item().filePath))
            continue;

        int chosenCol = -1;
        int chosenRow = -1;
        for (int row = 0; row < rows && chosenCol < 0; ++row) {
            for (int col = 0; col < columns; ++col) {
                if (!cellAvailable(col, row, icon))
                    continue;
                chosenCol = col;
                chosenRow = row;
                break;
            }
        }

        if (chosenCol < 0) {
            chosenCol = 0;
            chosenRow = 0;
        }

        occupied.insert(chosenRow * columns + chosenCol);
        m_looseIconPositions[icon->item().filePath] =
            cellPosition(chosenCol, chosenRow, icon);
    }
}

void DesktopCanvas::revealLooseIcons()
{
    for (DesktopIcon *icon : m_looseIcons) {
        if (!icon)
            continue;
        icon->show();
        icon->raise();
        icon->update();
    }
    repaint();
}

void DesktopCanvas::layoutLooseIcons()
{
    const int columns = qMax(4, m_gridColumns);
    const int rows = qMax(3, m_gridRows);
    const QRect area = desktopIconArea();
    const qreal cellW = area.width() / qreal(columns);
    const qreal cellH = area.height() / qreal(rows);

    if (m_autoArrange && m_arrangeMode != ArrangeMode::Manual)
        sortLooseIcons();

    QList<QRect> fenceRects;
    for (auto *fence : m_fences)
        if (fence) fenceRects.append(fence->geometry());
    fenceRects.append(desktopWidgetReservedGeometries());

    QSet<int> occupied;
    auto cellPosition = [&](int col, int row, DesktopIcon *icon) {
        const QPoint center(
            qRound(area.left() + (col + 0.5) * cellW),
            qRound(area.top() + (row + 0.5) * cellH));
        return center - QPoint(icon->width() / 2, icon->height() / 2);
    };
    auto validCell = [&](int col, int row, DesktopIcon *icon) {
        const int key = row * columns + col;
        if (occupied.contains(key))
            return false;
        const QRect iconRect(cellPosition(col, row, icon), icon->size());
        for (const QRect &fenceRect : fenceRects)
            if (iconRect.intersects(fenceRect))
                return false;
        return true;
    };

    for (int index = 0; index < m_looseIcons.size(); ++index) {
        DesktopIcon *icon = m_looseIcons[index];
        if (!icon) continue;

        const QString path = icon->item().filePath;
        QPoint preferred = m_looseIconPositions.value(path, area.topLeft());
        int preferredCol = qBound(0,
            qRound((preferred.x() + icon->width() / 2 - area.left()) / cellW - 0.5),
            columns - 1);
        int preferredRow = qBound(0,
            qRound((preferred.y() + icon->height() / 2 - area.top()) / cellH - 0.5),
            rows - 1);

        if (m_autoArrange) {
            preferredCol = index / rows;
            preferredRow = index % rows;
            preferredCol = qBound(0, preferredCol, columns - 1);
        }

        int chosenCol = -1;
        int chosenRow = -1;
        const int maxDistance = columns + rows;
        for (int distance = 0; distance <= maxDistance && chosenCol < 0;
             ++distance) {
            for (int row = 0; row < rows && chosenCol < 0; ++row) {
                for (int col = 0; col < columns; ++col) {
                    if (qAbs(col - preferredCol) +
                        qAbs(row - preferredRow) != distance)
                        continue;
                    if (validCell(col, row, icon)) {
                        chosenCol = col;
                        chosenRow = row;
                        break;
                    }
                }
            }
        }

        if (chosenCol < 0) {
            chosenCol = preferredCol;
            chosenRow = preferredRow;
        }

        occupied.insert(chosenRow * columns + chosenCol);
        QPoint snapped = cellPosition(chosenCol, chosenRow, icon);
        snapped.setX(qBound(area.left(), snapped.x(),
                            area.right() - icon->width()));
        snapped.setY(qBound(area.top(), snapped.y(),
                            area.bottom() - icon->height()));
        m_looseIconPositions[path] = snapped;
        icon->move(snapped);
        icon->show();
        icon->raise();
        icon->update();
    }
    for (auto *widget : {static_cast<LiquidDesklet *>(m_clockWidget),
                         static_cast<LiquidDesklet *>(m_activityWidget), static_cast<LiquidDesklet *>(m_musicWidget), static_cast<LiquidDesklet *>(m_calendarWidget)})
        if (widget && widget->isVisible()) widget->raise();
    // 用 repaint() 而不是 update()：桌面窗口是原生 X11 窗口，KWin
    // compositor 可能在异步 update 到达之前缓存父控件的中间帧（仅壁纸、
    // 不含子控件），导致新增图标只在重启后可见。
    repaint();
}

void DesktopCanvas::removeLooseIcon(const QString &filePath)
{
    for (int i = m_looseIcons.size() - 1; i >= 0; --i) {
        if (sameStoredPath(m_looseIcons[i]->item().filePath, filePath)) {
            m_selectedIcons.remove(m_looseIcons[i]);
            if (m_selectionAnchor == m_looseIcons[i])
                m_selectionAnchor = nullptr;
            m_looseIconPositions.remove(m_looseIcons[i]->item().filePath);
            m_looseIconPositions.remove(filePath);
            m_looseIcons[i]->deleteLater();
            m_looseIcons.removeAt(i);
            layoutLooseIcons();
            return;
        }
    }
}

DesktopIcon *DesktopCanvas::looseIconForPath(const QString &filePath) const
{
    for (DesktopIcon *icon : m_looseIcons) {
        if (icon && sameStoredPath(icon->item().filePath, filePath))
            return icon;
    }
    return nullptr;
}

void DesktopCanvas::finishNewDesktopItem(const QString &filePath,
                                         const QPoint &clickPos)
{
    DesktopItem item = DesktopItem::fromPath(filePath);
    if (!item.isValid())
        return;

    DesktopIcon *icon = looseIconForPath(filePath);
    if (!icon) {
        icon = new DesktopIcon(item, this);
        connectLooseIcon(icon);
        m_looseIcons.append(icon);
        icon->show();
    } else {
        icon->setItem(item);
    }

    for (FenceWidget *fence : m_fences) {
        if (fence)
            fence->removeItem(filePath);
    }

    if (!m_autoArrange) {
        QPoint pos = clickPos - QPoint(icon->width() / 2, 12);
        const QRect area = desktopIconArea();
        pos.setX(qBound(area.left(), pos.x(),
                        qMax(area.left(), area.right() - icon->width())));
        pos.setY(qBound(area.top(), pos.y(),
                        qMax(area.top(), area.bottom() - icon->height())));
        m_looseIconPositions[filePath] = pos;
    }

    layoutLooseIcons();
    clearSelection();
    clearFenceSelections();
    setIconSelected(icon, true);
    m_selectionAnchor = icon;
    icon->raise();
    saveLayout();

    QTimer::singleShot(0, icon, [icon] {
        icon->startInlineRename();
    });
}

void DesktopCanvas::createNewDesktopFile(const QString &baseName,
                                         const QString &suffix,
                                         const QPoint &clickPos)
{
    QDir dir(m_desktopPath);
    const QString normalizedSuffix = suffix.startsWith('.')
        ? suffix : QStringLiteral(".") + suffix;
    QString name = baseName + normalizedSuffix;
    int n = 1;
    while (dir.exists(name))
        name = QStringLiteral("%1 (%2)%3")
                   .arg(baseName).arg(n++).arg(normalizedSuffix);

    const QString path = dir.absoluteFilePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "新建失败",
                             QString("无法创建 %1。").arg(name));
        return;
    }
    file.close();
    recordCreateUndo(path);
    finishNewDesktopItem(path, clickPos);
}

void DesktopCanvas::createNewDesktopDirectory(const QPoint &clickPos)
{
    QDir dir(m_desktopPath);
    QString name = QStringLiteral("新建文件夹");
    int n = 1;
    while (dir.exists(name))
        name = QStringLiteral("新建文件夹 (%1)").arg(n++);

    if (!dir.mkdir(name)) {
        QMessageBox::warning(this, "新建失败",
                             QString("无法创建 %1。").arg(name));
        return;
    }

    const QString path = dir.absoluteFilePath(name);
    recordCreateUndo(path);
    finishNewDesktopItem(path, clickPos);
}

void DesktopCanvas::removePathsFromAllViews(const QStringList &paths)
{
    if (paths.isEmpty()) return;

    QSet<QString> normalizedPaths;
    for (const QString &path : paths) {
        const QString normalized = normalizedStoredPath(path);
        if (!normalized.isEmpty())
            normalizedPaths.insert(normalized);
    }
    if (normalizedPaths.isEmpty()) return;

    bool changed = false;

    for (const QString &path : normalizedPaths) {
        const int before = m_looseIcons.size();
        removeLooseIcon(path);
        changed = changed || before != m_looseIcons.size();
    }

    for (auto *fence : m_fences) {
        if (!fence) continue;
        const QList<DesktopItem> items = fence->items();
        for (const DesktopItem &item : items) {
            if (normalizedPaths.contains(normalizedStoredPath(item.filePath))) {
                fence->removeItem(item.filePath);
                changed = true;
            }
        }
    }

    if (changed) {
        saveLayout();
        syncCutVisualState();
    }
}

bool DesktopCanvas::pruneMissingFileIcons()
{
    bool changed = false;

    for (int i = m_looseIcons.size() - 1; i >= 0; --i) {
        DesktopIcon *icon = m_looseIcons[i];
        if (!icon || storedItemExists(icon->item()))
            continue;

        m_selectedIcons.remove(icon);
        if (m_selectionAnchor == icon)
            m_selectionAnchor = nullptr;
        m_looseIconPositions.remove(icon->item().filePath);
        icon->deleteLater();
        m_looseIcons.removeAt(i);
        changed = true;
    }

    for (auto *fence : m_fences) {
        if (!fence) continue;
        const QList<DesktopItem> items = fence->items();
        for (const DesktopItem &item : items) {
            if (storedItemExists(item))
                continue;
            fence->removeItem(item.filePath);
            changed = true;
        }
    }

    if (changed) {
        layoutLooseIcons();
        saveLayout();
    }

    return changed;
}

void DesktopCanvas::updateCutPathWatches(const QStringList &cutPaths)
{
    if (!m_watcher) return;

    QSet<QString> desired;
    const QString desktopPath = QDir(m_desktopPath).absolutePath();

    for (const QString &path : cutPaths) {
        const QString normalized = normalizedStoredPath(path);
        if (normalized.isEmpty() || !QFileInfo::exists(normalized))
            continue;

        const QFileInfo fi(normalized);
        const QString ownPath = fi.absoluteFilePath();
        const QString parentPath = QDir(fi.absolutePath()).absolutePath();

        if (ownPath != desktopPath)
            desired.insert(ownPath);
        if (!parentPath.isEmpty() && parentPath != desktopPath)
            desired.insert(parentPath);
    }

    const QStringList oldPaths = m_cutWatchPaths.values();
    for (const QString &path : oldPaths) {
        if (desired.contains(path))
            continue;
        m_watcher->removePath(path);
        m_cutWatchPaths.remove(path);
    }

    const QStringList watchedFiles = m_watcher->files();
    const QStringList watchedDirs = m_watcher->directories();
    for (const QString &path : desired) {
        if (watchedFiles.contains(path) || watchedDirs.contains(path)) {
            m_cutWatchPaths.insert(path);
            continue;
        }

        if (m_watcher->addPath(path))
            m_cutWatchPaths.insert(path);
    }
}

void DesktopCanvas::scheduleRefresh(int delayMs)
{
    if (delayMs <= 0) {
        QMetaObject::invokeMethod(this,
            &DesktopCanvas::refreshDesktopIcons,
            Qt::QueuedConnection);
        return;
    }

    QTimer::singleShot(delayMs, this, &DesktopCanvas::refreshDesktopIcons);
}

void DesktopCanvas::pushUndo(const UndoOperation &op)
{
    if (op.sourcePaths.isEmpty() && op.targetPaths.isEmpty())
        return;

    if (!m_undoStack.isEmpty()) {
        const UndoOperation &last = m_undoStack.constLast();
        if (last.type == op.type &&
            last.sourcePaths == op.sourcePaths &&
            last.targetPaths == op.targetPaths &&
            last.fenceIds == op.fenceIds)
            return;
    }

    m_undoStack.append(op);
    while (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
}

FenceWidget *DesktopCanvas::fenceById(const QString &id) const
{
    if (id.isEmpty()) return nullptr;
    for (auto *fence : m_fences) {
        if (fence && fence->fenceId() == id)
            return fence;
    }
    return nullptr;
}

FenceWidget *DesktopCanvas::fenceContainingPath(const QString &path) const
{
    for (auto *fence : m_fences) {
        if (fence && fence->hasItem(path))
            return fence;
    }
    return nullptr;
}

FenceWidget *DesktopCanvas::desktopInboxFence() const
{
    return fenceById(m_desktopInboxFenceId);
}

void DesktopCanvas::recordCreateUndo(const QString &path)
{
    UndoOperation op;
    op.type = UndoOperation::Type::Create;
    op.targetPaths << normalizedStoredPath(path);
    pushUndo(op);
}

void DesktopCanvas::recordRenameUndo(const QString &oldPath,
                                     const QString &newPath,
                                     FenceWidget *fence)
{
    UndoOperation op;
    op.type = UndoOperation::Type::Rename;
    op.sourcePaths << normalizedStoredPath(oldPath);
    op.targetPaths << normalizedStoredPath(newPath);
    op.fenceIds << (fence ? fence->fenceId() : QString());
    pushUndo(op);
}

void DesktopCanvas::recordTrashUndo(const QStringList &paths, FenceWidget *fence)
{
    UndoOperation op;
    op.type = UndoOperation::Type::Trash;

    for (const QString &path : paths) {
        const QString normalized = normalizedStoredPath(path);
        if (normalized.isEmpty()) continue;

        FenceWidget *owner = fence ? fence : fenceContainingPath(normalized);
        bool knownInLoose = false;
        for (auto *icon : m_looseIcons) {
            if (icon && sameStoredPath(icon->item().filePath, normalized)) {
                knownInLoose = true;
                break;
            }
        }

        if (!owner && !knownInLoose && !QFileInfo::exists(normalized))
            continue;

        op.sourcePaths << normalized;
        op.fenceIds << (owner ? owner->fenceId() : QString());
    }

    pushUndo(op);
}

void DesktopCanvas::recordPasteUndo(const FileClipboard::PasteResult &result,
                                    FenceWidget *fence)
{
    if (!result.hasPlacedFiles())
        return;

    UndoOperation op;
    op.type = UndoOperation::Type::Paste;
    op.move = result.move;
    op.targetPaths = result.transferredPaths;
    op.sourcePaths = result.placedSourcePaths;

    for (int i = 0; i < op.targetPaths.size(); ++i)
        op.fenceIds << (fence ? fence->fenceId() : QString());

    pushUndo(op);
}

bool DesktopCanvas::deletePathForUndo(const QString &path) const
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return true;
    return fi.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
}

bool DesktopCanvas::movePathForUndo(const QString &srcPath,
                                    const QString &targetPath) const
{
    if (!QFileInfo::exists(srcPath))
        return false;

    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (QProcess::execute("gio",
            QStringList() << "move" << "-T" << srcPath << targetPath) == 0)
        return true;

    const QFileInfo src(srcPath);
    return src.isDir()
        ? QDir().rename(srcPath, targetPath)
        : QFile::rename(srcPath, targetPath);
}

QString DesktopCanvas::restoreTrashedPath(const QString &originalPath) const
{
    const QString original = normalizedStoredPath(originalPath);
    const QString trashRoot =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + "/.local/share/Trash";
    QDir infoDir(trashRoot + "/info");
    QDir filesDir(trashRoot + "/files");
    if (!infoDir.exists() || !filesDir.exists())
        return {};

    QFileInfo bestInfo;
    const QFileInfoList infos =
        infoDir.entryInfoList(QStringList() << "*.trashinfo",
                              QDir::Files, QDir::Time);
    for (const QFileInfo &info : infos) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QString storedPath;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (!line.startsWith(QLatin1String("Path=")))
                continue;
            const QString encoded = line.mid(5);
            storedPath = QString::fromUtf8(
                QUrl::fromPercentEncoding(encoded.toUtf8()).toUtf8());
            break;
        }

        if (normalizedStoredPath(storedPath) == original) {
            bestInfo = info;
            break;
        }
    }

    if (!bestInfo.exists())
        return {};

    const QString trashName = bestInfo.completeBaseName();
    const QString trashedPath = filesDir.absoluteFilePath(trashName);
    if (!QFileInfo::exists(trashedPath))
        return {};

    QFileInfo originalInfo(original);
    QString restoreTarget = original;
    if (QFileInfo::exists(restoreTarget)) {
        restoreTarget = FileClipboard::uniqueTargetPath(
            originalInfo.absolutePath(), originalInfo.fileName());
    }

    if (!movePathForUndo(trashedPath, restoreTarget))
        return {};

    QFile::remove(bestInfo.absoluteFilePath());
    return QFileInfo(restoreTarget).absoluteFilePath();
}

void DesktopCanvas::undoLastOperation()
{
    if (m_undoStack.isEmpty()) {
        QMessageBox::information(this, "撤回", "没有可撤回的操作。");
        return;
    }

    const UndoOperation op = m_undoStack.takeLast();
    QStringList restoredPaths;
    QStringList failedPaths;

    switch (op.type) {
    case UndoOperation::Type::Create:
        for (const QString &path : op.targetPaths) {
            if (!deletePathForUndo(path))
                failedPaths << path;
        }
        break;

    case UndoOperation::Type::Rename:
        for (int i = 0; i < op.targetPaths.size(); ++i) {
            const QString oldPath = op.sourcePaths.value(i);
            const QString newPath = op.targetPaths.value(i);
            if (movePathForUndo(newPath, oldPath)) {
                restoredPaths << oldPath;
                if (auto *fence = fenceById(op.fenceIds.value(i))) {
                    fence->removeItem(newPath);
                    DesktopItem item = DesktopItem::fromPath(oldPath);
                    if (item.isValid()) fence->addItem(item);
                }
            } else {
                failedPaths << newPath;
            }
        }
        break;

    case UndoOperation::Type::Trash:
        for (int i = 0; i < op.sourcePaths.size(); ++i) {
            const QString restored = restoreTrashedPath(op.sourcePaths.value(i));
            if (restored.isEmpty()) {
                failedPaths << op.sourcePaths.value(i);
                continue;
            }

            restoredPaths << restored;
            if (auto *fence = fenceById(op.fenceIds.value(i))) {
                DesktopItem item = DesktopItem::fromPath(restored);
                if (item.isValid()) {
                    fence->addItem(item);
                    removeLooseIcon(restored);
                }
            }
        }
        break;

    case UndoOperation::Type::Paste:
        if (op.move) {
            for (int i = 0; i < op.targetPaths.size(); ++i) {
                const QString placed = op.targetPaths.value(i);
                const QString source = op.sourcePaths.value(i);
                if (source.isEmpty() || !movePathForUndo(placed, source)) {
                    failedPaths << placed;
                    continue;
                }
                restoredPaths << source;
            }
        } else {
            for (const QString &path : op.targetPaths) {
                if (!deletePathForUndo(path))
                    failedPaths << path;
            }
        }
        break;
    }

    refreshDesktopIcons();
    for (const QString &path : restoredPaths) {
        if (!path.isEmpty() && !isInAnyFence(path) &&
            isInDesktopDirectory(path)) {
            placeFilesOnDesktop(QStringList() << path);
        }
    }
    refreshTrashState();
    syncCutVisualState();
    saveLayout();

    if (!failedPaths.isEmpty()) {
        QMessageBox::warning(this, "撤回失败",
            QString("有 %1 个项目无法撤回。").arg(failedPaths.size()));
    }
}

bool DesktopCanvas::isInAnyFence(const QString &filePath) const
{
    for (auto *f : m_fences)
        if (f->hasItem(filePath)) return true;
    return false;
}

// ── Fence 管理 ────────────────────────────────────────────

FenceWidget *DesktopCanvas::createFence(const QString &title, const QRect &geo)
{
    auto *fence = new FenceWidget(title, geo, this);
    fence->setLiquidGlassEnabled(m_fenceLiquidGlassEnabled);
    fence->setFenceColor(m_defaultFenceColor);
    fence->setEditMode(m_editMode);
    fence->setIconScale(m_iconScale);
    const QStringList defaultIcons = defaultFenceIconPaths();
    if (!defaultIcons.isEmpty())
        fence->setTitleIconPath(
            defaultIcons.at(m_fences.size() % defaultIcons.size()));
    m_fences.append(fence);

    connect(fence, &FenceWidget::geometryChanged,
            this, [this] {
        layoutLooseIcons();
        saveLayout();
    });
    connect(fence, &FenceWidget::titleChanged,
            this,  &DesktopCanvas::saveLayout);
    connect(fence, &FenceWidget::undoRequested,
            this, &DesktopCanvas::undoLastOperation);
    connect(fence, &FenceWidget::interactionStarted,
            this, [this, fence](FenceWidget *) {
        clearSelection();
        for (auto *other : m_fences) {
            if (other && other != fence)
                other->clearIconSelection();
        }
    });

    // 文件拖入 Fence 后，从散落区移除
    connect(fence, &FenceWidget::fileDropped,
            [this, fence](const QString &path) {
        removeLooseIcon(path);
        for (auto *other : m_fences) {
            if (other != fence)
                other->removeItem(path);
        }
        saveLayout();
    });

    connect(fence, &FenceWidget::deleteRequested,
            this,  &DesktopCanvas::removeFence);
    connect(fence, &FenceWidget::fileRenamedForUndo,
            this, [this, fence](const QString &oldPath, const QString &newPath) {
        recordRenameUndo(oldPath, newPath, fence);
    });
    connect(fence, &FenceWidget::fileCreated,
            this, [this](const QString &path) {
        recordCreateUndo(path);
        saveLayout();
    });
    connect(fence, &FenceWidget::filesPasted,
            this, [this, fence](const QStringList &sourcePaths,
                                const QStringList &placedPaths,
                                bool move) {
        UndoOperation op;
        op.type = UndoOperation::Type::Paste;
        op.sourcePaths = sourcePaths;
        op.targetPaths = placedPaths;
        op.move = move;
        for (int i = 0; i < placedPaths.size(); ++i)
            op.fenceIds << (fence ? fence->fenceId() : QString());
        pushUndo(op);
    });
    connect(fence, &FenceWidget::filesTrashed,
            this, [this, fence](const QStringList &paths) {
        recordTrashUndo(paths, fence);
        removePathsFromAllViews(paths);
        scheduleRefresh(300);
        scheduleRefresh(1200);
        refreshTrashState();
    });

    fence->show();
    fence->raise();
    // 应用全局字体配置到新Fence
    fence->setIconFontFamily(m_fontFamily);
    fence->setIconFontSize(m_fontSize);
    fence->setIconFontColor(m_fontColor);
    fence->setIconFontBold(m_fontBold);
    fence->setIconFontItalic(m_fontItalic);

    if (!m_loadingLayout) {
        saveLayout();
        // ★ Fence创建后重新布局，让系统图标避开
        layoutLooseIcons();
    }

    return fence;
}

void DesktopCanvas::removeFence(FenceWidget *fence)
{
    if (fence && fence->fenceId() == m_desktopInboxFenceId)
        m_desktopInboxFenceId.clear();

    m_fences.removeAll(fence);

    for (const DesktopItem &item : fence->items()) {
        if (isInAnyFence(item.filePath)) continue;
        auto *icon = new DesktopIcon(item, this);
        connectLooseIcon(icon);
        m_looseIcons.append(icon);
        icon->show();
    }
    fence->deleteLater();
    layoutLooseIcons();
    saveLayout();
}

// ── 编辑模式 ─────────────────────────────────────────────

void DesktopCanvas::setGlobalEditMode(bool edit)
{
    m_editMode = edit;
    for (auto *f : m_fences) f->setEditMode(edit);
    if (m_monitor) m_monitor->setEditMode(edit);
    if (m_smartSpace) m_smartSpace->setEditMode(edit);
    if (m_clockWidget) m_clockWidget->setEditMode(edit);
    if (m_activityWidget) m_activityWidget->setEditMode(edit);
    if (m_musicWidget) m_musicWidget->setEditMode(edit);
    if (m_calendarWidget) m_calendarWidget->setEditMode(edit);
    update();
}

void DesktopCanvas::setIconSelected(DesktopIcon *icon, bool selected)
{
    if (!icon) return;
    icon->setSelected(selected);
    if (selected)
        m_selectedIcons.insert(icon);
    else
        m_selectedIcons.remove(icon);
}

void DesktopCanvas::selectIconRange(DesktopIcon *anchor,
                                    DesktopIcon *target,
                                    bool keepExisting)
{
    if (!target) return;
    if (!anchor || !m_looseIcons.contains(anchor)) {
        if (!keepExisting)
            clearSelection();
        setIconSelected(target, true);
        return;
    }

    const int a = m_looseIcons.indexOf(anchor);
    const int b = m_looseIcons.indexOf(target);
    if (a < 0 || b < 0) {
        if (!keepExisting)
            clearSelection();
        setIconSelected(target, true);
        return;
    }

    if (!keepExisting)
        clearSelection();

    const int from = qMin(a, b);
    const int to = qMax(a, b);
    for (int i = from; i <= to; ++i)
        setIconSelected(m_looseIcons[i], true);
}

void DesktopCanvas::clearSelection()
{
    const auto icons = m_selectedIcons.values();
    for (auto *icon : icons)
        if (icon) icon->setSelected(false);
    m_selectedIcons.clear();
    m_selectionAnchor = nullptr;
}

void DesktopCanvas::selectAllLooseIcons()
{
    clearFenceSelections();
    clearSelection();
    for (auto *icon : m_looseIcons)
        setIconSelected(icon, true);
}

QStringList DesktopCanvas::selectedFilePaths() const
{
    QStringList paths;
    for (auto *icon : m_selectedIcons) {
        if (!icon || icon->item().isSystemIcon) continue;
        paths << icon->item().filePath;
    }
    return paths;
}

void DesktopCanvas::copySelectedIcons(bool move)
{
    const QStringList paths = selectedFilePaths();
    if (!paths.isEmpty()) {
        FileClipboard::writeFiles(paths, move);
        syncCutVisualState();
    }
}

void DesktopCanvas::placeFilesOnDesktop(const QStringList &paths,
                                        const QPoint &preferredPos)
{
    if (paths.isEmpty()) return;

    QPoint pos = preferredPos;
    const bool hasPreferredPos = pos.x() >= 0 && pos.y() >= 0;
    if (hasPreferredPos)
        disableAutoArrangeForManualPlacement();

    for (const QString &path : paths) {
        DesktopItem item = DesktopItem::fromPath(path);
        if (!item.isValid() || item.isSystemIcon)
            continue;

        for (auto *fence : m_fences) {
            if (fence)
                fence->removeItem(item.filePath);
        }

        DesktopIcon *existing = nullptr;
        for (auto *icon : m_looseIcons) {
            if (icon && icon->item().filePath == item.filePath) {
                existing = icon;
                break;
            }
        }

        if (!existing) {
            existing = new DesktopIcon(item, this);
            connectLooseIcon(existing);
            m_looseIcons.append(existing);
            existing->show();
        } else {
            existing->setItem(item);
        }

        if (hasPreferredPos) {
            m_looseIconPositions[item.filePath] = pos;
            existing->move(pos);
            pos += QPoint(18, 18);
        }
    }

    layoutLooseIcons();
    saveLayout();
    syncCutVisualState();
}

bool DesktopCanvas::pasteToDesktop(const QPoint &preferredPos)
{
    const FileClipboard::PasteResult result =
        FileClipboard::pasteFilesToDirectory(m_desktopPath);
    if (!result.hadFiles())
        return false;

    if (!result.failedPaths.isEmpty()) {
        QMessageBox::warning(this, "粘贴失败",
            QString("有 %1 个项目无法粘贴。").arg(result.failedPaths.size()));
    }

    recordPasteUndo(result);
    placeFilesOnDesktop(result.placedPaths, preferredPos);
    refreshDesktopIcons();
    syncCutVisualState();
    return result.hasPlacedFiles();
}

void DesktopCanvas::trashSelectedIcons()
{
    const QStringList paths = selectedFilePaths();
    if (paths.isEmpty()) return;

    if (QMessageBox::question(this, "移到回收站",
            QString("确定要将选中的 %1 个项目移到回收站吗？").arg(paths.size()))
        != QMessageBox::Yes)
        return;

    QStringList trashedPaths;
    QStringList failedPaths;
    for (const QString &path : paths) {
        const int code = QProcess::execute("gio",
            QStringList() << "trash" << path);
        if (code == 0) {
            trashedPaths << normalizedStoredPath(path);
        } else {
            failedPaths << path;
        }
    }

    recordTrashUndo(trashedPaths);
    for (const QString &path : trashedPaths)
        removeLooseIcon(path);

    if (!failedPaths.isEmpty()) {
        QMessageBox::warning(this, "移到回收站失败",
            QString("有 %1 个项目无法移到回收站。")
                .arg(failedPaths.size()));
    }

    saveLayout();
    refreshTrashState();
    scheduleRefresh(300);
}

void DesktopCanvas::renameFirstSelectedIcon()
{
    if (m_selectedIcons.isEmpty()) return;

    DesktopIcon *icon = *m_selectedIcons.constBegin();
    if (!icon || icon->item().isSystemIcon) return;
    icon->startInlineRename();
}

void DesktopCanvas::openSelectedIcons()
{
    for (auto *icon : m_selectedIcons)
        if (icon) icon->item().open();
}

void DesktopCanvas::applyIconScale(qreal scale)
{
    m_iconScale = qBound<qreal>(0.75, scale, 1.75);
    m_desktopIconScale = m_iconScale;
    for (auto *icon : m_looseIcons)
        icon->setVisualScale(m_desktopIconScale);
    for (auto *fence : m_fences)
        fence->setIconScale(m_iconScale);
    layoutLooseIcons();
    saveLayout();
}

void DesktopCanvas::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() ||
        e->mimeData()->hasFormat(kSystemIconMime))
        e->acceptProposedAction();
}

void DesktopCanvas::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls() ||
        e->mimeData()->hasFormat(kSystemIconMime))
        e->acceptProposedAction();
}

void DesktopCanvas::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat(kSystemIconMime)) {
        const QString path =
            QString::fromUtf8(e->mimeData()->data(kSystemIconMime)).trimmed();
        for (auto *icon : m_looseIcons) {
            if (!icon->item().isSystemIcon || icon->item().filePath != path)
                continue;

            const QPoint pos = e->pos()
                - QPoint(icon->width() / 2, icon->height() / 2);
            disableAutoArrangeForManualPlacement();
            m_looseIconPositions[path] = pos;
            icon->move(pos);
            icon->show();
            layoutLooseIcons();
            saveLayout();
            e->acceptProposedAction();
            return;
        }

        DesktopItem item = DesktopItem::fromStoredPath(path);
        if (!item.isValid()) {
            e->ignore();
            return;
        }

        for (auto *fence : m_fences)
            fence->removeItem(item.filePath);

        auto *icon = new DesktopIcon(item, this);
        connectLooseIcon(icon);
        m_looseIcons.append(icon);

        const QPoint pos = e->pos()
            - QPoint(icon->width() / 2, icon->height() / 2);
        disableAutoArrangeForManualPlacement();
        m_looseIconPositions[item.filePath] = pos;
        icon->move(pos);
        icon->show();
        layoutLooseIcons();
        saveLayout();
        e->acceptProposedAction();
        return;
    }

    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }

    QPoint pos = e->pos();
    disableAutoArrangeForManualPlacement();
    for (const QUrl &url : e->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.isEmpty()) continue;

        const QFileInfo src(path);
        if (!src.exists()) continue;

        if (!isInDesktopDirectory(path)) {
            const QString target =
                FileClipboard::uniqueTargetPath(m_desktopPath, src.fileName());
            const bool move =
                e->proposedAction() == Qt::MoveAction ||
                e->dropAction() == Qt::MoveAction;

            if (!transferDroppedPathToDesktop(path, target, move)) {
                QMessageBox::warning(this, "拖放失败",
                    QString("无法将 \"%1\" 放到桌面。").arg(src.fileName()));
                continue;
            }
            path = target;
        }

        for (auto *fence : m_fences)
            fence->removeItem(path);

        DesktopIcon *existing = nullptr;
        for (auto *icon : m_looseIcons) {
            if (icon->item().filePath == path) {
                existing = icon;
                break;
            }
        }

        if (!existing) {
            DesktopItem item = DesktopItem::fromStoredPath(path);
            if (!item.isValid()) continue;

            existing = new DesktopIcon(item, this);
            connectLooseIcon(existing);
            m_looseIcons.append(existing);
            existing->show();
        }

        m_looseIconPositions[path] = pos;
        existing->move(pos);
        pos += QPoint(18, 18);
    }

    layoutLooseIcons();
    saveLayout();
    e->acceptProposedAction();
}

void DesktopCanvas::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::SelectAll)) {
        selectAllLooseIcons();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Undo)) {
        undoLastOperation();
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
        pasteToDesktop();
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
    case Qt::Key_F5:
        refreshAll();
        e->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        openSelectedIcons();
        e->accept();
        return;
    case Qt::Key_Escape:
        clearSelection();
        e->accept();
        return;
    default:
        QWidget::keyPressEvent(e);
    }
}

void DesktopCanvas::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const qreal step = e->angleDelta().y() > 0 ? 0.1 : -0.1;
        applyIconScale(m_iconScale + step);
        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

// ── 绘制 ─────────────────────────────────────────────────

void DesktopCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    if (!m_wallpaperCache.isNull()) {
        p.drawPixmap(0, 0, m_wallpaperCache);
    } else {
        // 渐变兜底
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor("#1a2a3a"));
        g.setColorAt(1, QColor("#2c5f8a"));
        p.fillRect(rect(), g);
    }

    // 编辑模式：顶部提示条
    if (m_editMode) {
        p.fillRect(0, 0, width(), 30, QColor(0, 0, 0, 80));
        QFont f;
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 200, 0));
        p.drawText(QRect(0, 0, width(), 30),
                   Qt::AlignVCenter | Qt::AlignHCenter,
                   m_wallpaperFenceCapture
                   ? "✎ 壁纸取样建分区  —  拖拽圈定壁纸区域，松开后自动生成同色系 Fence"
                   : "✎ 编辑模式  —  拖动分区移位，拖拽边缘缩放，右键桌面退出");
    }

    // ── 框选矩形 ──
    if (m_rubberBanding && m_rubberRect.isValid()) {
        p.setPen(QPen(QColor(180, 180, 180, 200), 1, Qt::DashLine));
        p.setBrush(QColor(180, 180, 180, 40));
        p.drawRect(m_rubberRect);
    }

}

// ── 鼠标事件（框选 + 点击空白取消选中）─────────────────

void DesktopCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);

        if (m_wallpaperFenceCapture) {
            clearSelection();
            clearFenceSelections();
            m_rubberOrigin = e->pos();
            m_rubberBanding = true;
            m_rubberRect = QRect();
            e->accept();
            return;
        }

        // 点击空白区域清除选中
        clearSelection();
        clearFenceSelections();
        m_rubberOrigin = e->pos();
        m_rubberBanding = true;
        m_rubberRect = QRect();
        e->accept();
    } else {
        QWidget::mousePressEvent(e);
    }
}

void DesktopCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_rubberBanding && (e->buttons() & Qt::LeftButton)) {
        const QRect next = QRect(m_rubberOrigin, e->pos()).normalized();
        if (next == m_rubberRect) {
            e->accept();
            return;
        }
        const QRegion damage = rubberBandDamage(m_rubberRect, next);
        m_rubberRect = next;

        if (!m_wallpaperFenceCapture) {
            // 框选逻辑：选中框内的图标
            for (auto *icon : m_looseIcons) {
                const QRect iconGeo(icon->pos(), icon->size());
                setIconSelected(icon, m_rubberRect.intersects(iconGeo));
            }
        }
        update(damage);
        e->accept();
    } else {
        QWidget::mouseMoveEvent(e);
    }
}

void DesktopCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_rubberBanding) {
        const QRect selection = m_rubberRect;
        m_rubberBanding = false;
        m_rubberRect = QRect();

        if (m_wallpaperFenceCapture) {
            finishWallpaperFenceCapture(selection);
            e->accept();
            return;
        }

        // 如果有选中图标且拖动了一定距离，不启动拖动（在release时不做）
        // 多文件拖动通过 DesktopIcon 的 dragStarted 信号触发
        update(rubberBandDamage(selection, QRect()));
        e->accept();
    } else {
        QWidget::mouseReleaseEvent(e);
    }
}

void DesktopCanvas::moveEvent(QMoveEvent *e)
{
    QWidget::moveEvent(e);
}

void DesktopCanvas::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    rebuildWallpaperCache();
    updateHotCornerGuards();
}

bool DesktopCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_clockWidget || watched == m_activityWidget || watched == m_musicWidget || watched == m_calendarWidget) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            // Use the existing layout debounce, also covering the widget's ×.
            m_smartSpaceRelayoutTimer->start();
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DesktopCanvas::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() != QEvent::WindowStateChange)
        return;

    // 某些 KWin 版本仍会发送普通的最小化事件。处于 Win+D 状态时
    // 交给 KWin 管理，只有普通误最小化才主动恢复。
    if (isMinimized()) {
        QTimer::singleShot(0, this, [this] {
            if (x11ShowingDesktop() || m_userHidden)
                return;
            setWindowState(windowState() & ~Qt::WindowMinimized);
            show();
            lockToDesktopGeometry();
            applyX11DesktopHints();
        });
    }
}

// ── 右键菜单 ─────────────────────────────────────────────

void DesktopCanvas::contextMenuEvent(QContextMenuEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    clearFenceSelections();
    QMenu menu(this);
    MenuStyle::applyVenturaContextMenu(&menu);
    menu.setObjectName("fencesDesktopMenu");
    menu.setToolTipsVisible(true);
    auto makeMenu = [&menu](const QString &title, const char *id) {
        auto *child = new QMenu(title, &menu);
        child->setObjectName(QString::fromLatin1(id));
        child->setToolTipsVisible(true);
        MenuStyle::applyVenturaContextMenu(child);
        return child;
    };
    auto rootAction = [&menu](const QIcon &icon, const QString &title) {
        return new QAction(icon, title, &menu);
    };
    auto *newMenu = makeMenu("新建", "desktopNewMenu");
    auto *sortModeMenu = makeMenu("排列与布局", "desktopLayoutMenu");
    auto *widgetsMenu = makeMenu("桌面小组件", "desktopWidgetsMenu");
    auto *desktopSettingsMenu = makeMenu("外观与特效", "desktopAppearanceMenu");
    auto *fencesMenu = makeMenu("设置与帮助", "desktopSettingsHelpMenu");
    auto *widgetStartupMenu = makeMenu("启动设置", "desktopStartupMenu");
    auto *themeMenu = makeMenu("主题配色", "desktopThemeMenu");
    // The same layout submenu owns sorting, grid and layout persistence.
    auto *layoutMenu = sortModeMenu;

    auto *actUndo = rootAction(
        menuIcon(QStringList() << "edit-undo", "↶", QColor("#475569")),
        "撤销上一步");
    actUndo->setShortcut(QKeySequence::Undo);
    actUndo->setEnabled(canUndo());
    connect(actUndo, &QAction::triggered,
            this, &DesktopCanvas::undoLastOperation);

    // 编辑模式切换
    auto *actEdit = rootAction(
        menuIcon(QStringList() << "document-edit" << "edit-select",
                 "✎", QColor("#2563eb")),
        m_editMode ? "退出布局编辑" : "编辑分区布局");
    actEdit->setObjectName("layoutEditAction");
    actEdit->setToolTip("开启后可移动、缩放分区和桌面小组件；退出后锁定布局。");
    actEdit->setCheckable(true);
    actEdit->setChecked(m_editMode);
    connect(actEdit, &QAction::triggered,
            [this, actEdit](bool enabled) {
        setGlobalEditMode(enabled);
        actEdit->setText(enabled ? "退出布局编辑" : "编辑分区布局");
    });

    auto *actRefresh = rootAction(
        menuIcon(QStringList() << "view-refresh" << "reload",
                 "↻", QColor("#0ea5e9")),
        "刷新桌面");
    actRefresh->setToolTip("刷新壁纸、桌面文件与图标，同步最新变化。");
    connect(actRefresh, &QAction::triggered,
            this, &DesktopCanvas::refreshAll);

    // 新建分区（在点击位置创建）
    const QPoint clickPos = e->pos();
    auto *actNewFence = newMenu->addAction(
        menuIcon(QStringList() << "folder-new" << "list-add",
                 "+", QColor("#16a34a")),
        "普通分区…");
    connect(actNewFence, &QAction::triggered, [this, clickPos] {
        bool ok = false;
        const QString title = QInputDialog::getText(
            this, "新建分区", "分区名称：",
            QLineEdit::Normal, "新分区", &ok);
        if (ok && !title.isEmpty())
            createFence(title, QRect(clickPos, QSize(380, 300)));
    });

    auto *actWallpaperCapture = newMenu->addAction(
        menuIcon(QStringList() << "color-picker" << "applications-graphics",
                 "◎", QColor("#7c3aed")),
        "从壁纸取样创建分区…");
    actWallpaperCapture->setToolTip("拖拽选择壁纸区域，按取样轮廓创建分区。");
    connect(actWallpaperCapture, &QAction::triggered,
            [this] { beginWallpaperFenceCapture(); });

    auto *actDesktopGrid = sortModeMenu->addAction(
        menuIcon(QStringList() << "view-grid" << "view-grid-symbolic",
                 "▦", QColor("#0891b2")),
        "图标网格…");
    connect(actDesktopGrid, &QAction::triggered,
            [this] { showGridDialog(); });

    auto *actDesklet = widgetsMenu->addAction(
        menuIcon(QStringList() << "utilities-system-monitor" << "system-run",
                 "CPU", QColor("#dc2626")),
        "显示系统监视");
    actDesklet->setCheckable(true);
    actDesklet->setChecked(m_monitor != nullptr);
    connect(actDesklet, &QAction::triggered,
            [this](bool visible) { setSystemMonitorVisible(visible); });

    auto *actSmartSpace = widgetsMenu->addAction(
        menuIcon(QStringList() << "folder-saved-search" << "system-search",
                 "✦", QColor("#6366f1")),
        "显示智能空间");
    actSmartSpace->setCheckable(true);
    actSmartSpace->setChecked(m_smartSpace != nullptr);
    connect(actSmartSpace, &QAction::triggered,
            [this](bool visible) { setSmartSpaceVisible(visible); });

    auto startupLabel = [](const QString &name, bool enabled) {
        return name + (enabled ? QStringLiteral("（已开启自启动）")
                               : QStringLiteral("（未开启自启动）"));
    };
    auto addDesktopWidget = [this, widgetsMenu, widgetStartupMenu, startupLabel](
            const QString &key, const QString &title, bool visible, auto show) {
        auto *action = widgetsMenu->addAction("显示" + title);
        action->setObjectName(key + "WidgetAction"); action->setCheckable(true); action->setChecked(visible);
        connect(action, &QAction::triggered, this, show);
        auto *startup = widgetStartupMenu->addAction(startupLabel(title, LiquidDesklet::autoStartEnabled(key)));
        startup->setObjectName(key + "StartupAction"); startup->setCheckable(true);
        startup->setChecked(LiquidDesklet::autoStartEnabled(key));
        connect(startup, &QAction::triggered, this, [startup, startupLabel, key, title](bool enabled) {
            LiquidDesklet::setAutoStart(key, enabled); startup->setText(startupLabel(title, enabled));
        });
    };
    addDesktopWidget("clock", "时钟与倒计时", clockWidgetVisible(), &DesktopCanvas::setClockWidgetVisible);
    addDesktopWidget("activity", "活动统计", activityWidgetVisible(), &DesktopCanvas::setActivityWidgetVisible);
    addDesktopWidget("music", "Strawberry 音乐", musicWidgetVisible(), &DesktopCanvas::setMusicWidgetVisible);
    addDesktopWidget("calendar", "日历与系统待办", calendarWidgetVisible(), &DesktopCanvas::setCalendarWidgetVisible);
    // --- 系统监控 ---
    const bool monitorOn = SystemMonitor::autoStartEnabled();
    auto *monitorStartup = widgetStartupMenu->addAction(startupLabel(QStringLiteral("系统监视"), monitorOn));
    monitorStartup->setObjectName("monitorStartupAction");
    monitorStartup->setCheckable(true);
    monitorStartup->setChecked(monitorOn);
    connect(monitorStartup, &QAction::triggered, this,
            [this, monitorStartup, startupLabel](bool enabled) {
        monitorStartup->setText(startupLabel(QStringLiteral("系统监视"), enabled));
        if (m_monitor) {
            m_monitor->setAutoStart(enabled);
            return;
        }
        QSettings settings;
        settings.beginGroup(QStringLiteral("systemMonitor"));
        settings.setValue(QStringLiteral("autoStart"), enabled);
        settings.endGroup();
        settings.sync();
    });
    // --- 智能空间 ---
    const bool smartOn = SmartSpaceWidget::autoStartEnabled();
    auto *smartStartup = widgetStartupMenu->addAction(startupLabel(QStringLiteral("智能空间"), smartOn));
    smartStartup->setObjectName("smartStartupAction");
    smartStartup->setCheckable(true);
    smartStartup->setChecked(smartOn);
    connect(smartStartup, &QAction::triggered, this,
            [this, smartStartup, startupLabel](bool enabled) {
        smartStartup->setText(startupLabel(QStringLiteral("智能空间"), enabled));
        if (m_smartSpace) {
            m_smartSpace->setAutoStart(enabled);
            return;
        }
        QSettings settings;
        settings.beginGroup(QStringLiteral("smartSpace"));
        settings.setValue(QStringLiteral("autoStart"), enabled);
        settings.endGroup();
        settings.sync();
    });


    auto *actWallpaperTheme = themeMenu->addAction(
        menuIcon(QStringList() << "color-picker" << "preferences-color",
                 "◎", QColor("#7c3aed")),
        "从当前壁纸取色");
    connect(actWallpaperTheme, &QAction::triggered, [this] {
        if (!applyWallpaperThemeToFences()) {
            QMessageBox::warning(this, "主题颜色",
                "当前壁纸没有可提取的明显颜色。");
        }
    });

    themeMenu->addSeparator();

    auto addPreset = [this, themeMenu](const QString &name,
                                       const QColor &accent,
                                       const QColor &text = QColor(Qt::white)) {
        auto *act = themeMenu->addAction(colorSwatchMenuIcon(accent), name);
        connect(act, &QAction::triggered, [this, accent, text] {
            QColor c = accent;
            c.setAlpha(90);
            applyThemeToFences(c, text);
        });
    };
    addPreset("海湾蓝", QColor("#2f80ed"));
    addPreset("樱花粉", QColor("#ff7aa2"), QColor("#202124"));
    addPreset("松石绿", QColor("#14b8a6"));
    addPreset("暮色紫", QColor("#8b5cf6"));
    addPreset("石墨灰", QColor("#202124"));

    auto *actApplyExternalTheme = fencesMenu->addAction(
        menuIcon(QStringList() << "document-open" << "folder-open",
                 "CSS", QColor("#475569")),
        "应用外部主题文件");
    connect(actApplyExternalTheme, &QAction::triggered, [this] {
        if (!loadExternalTheme()) {
            QMessageBox::warning(this, "主题颜色",
                "没有找到外部主题文件。\n\n"
                "这是给 Matugen/Quickshell/Waybar 用户用的高级入口。\n"
                "普通使用可以直接点“从当前壁纸取色”或选择内置配色。");
            return;
        }

        applyExternalThemeToFences();
    });

    auto *arrangeGroup = new QActionGroup(&menu);
    arrangeGroup->setExclusive(true);
    const ArrangeMode activeMode = m_autoArrange ? m_arrangeMode : ArrangeMode::Manual;
    auto addArrangeMode = [&](const QString &name,
                              ArrangeMode mode,
                              const QIcon &icon) {
        auto *act = sortModeMenu->addAction(
            icon, name);
        act->setCheckable(true);
        act->setChecked(activeMode == mode);
        arrangeGroup->addAction(act);
        connect(act, &QAction::triggered, [this, mode] {
            m_arrangeMode = mode;
            if (mode == ArrangeMode::Manual)
                m_autoArrange = false;
            else
                m_autoArrange = true;
            layoutLooseIcons();
            saveLayout();
        });
    };
    addArrangeMode("手动排列", ArrangeMode::Manual,
                   menuIcon(QStringList() << "input-mouse", "✋", QColor("#475569")));
    addArrangeMode("按名称排列", ArrangeMode::ByName,
                   menuIcon(QStringList() << "view-sort-ascending", "A↓", QColor("#2563eb")));
    addArrangeMode("按类型排列", ArrangeMode::ByType,
                   menuIcon(QStringList() << "folder-documents", "T", QColor("#16a34a")));
    addArrangeMode("按修改时间排列", ArrangeMode::ByModifiedTime,
                   menuIcon(QStringList() << "view-calendar" << "appointment-new",
                            "⌚", QColor("#ea580c")));


    newMenu->addSeparator();
    auto *actNewDir = newMenu->addAction(
        menuIcon(QStringList() << "folder-new", "+", QColor("#16a34a")),
        "文件夹");
    connect(actNewDir, &QAction::triggered, [this, clickPos] {
        createNewDesktopDirectory(clickPos);
    });

    newMenu->addSeparator();
    auto addNewFileAction = [this, newMenu, clickPos](
                                const QString &label,
                                const QString &baseName,
                                const QString &suffix,
                                const QStringList &iconNames,
                                const QString &glyph,
                                const QColor &color) {
        QAction *action = newMenu->addAction(
            menuIcon(iconNames, glyph, color), label);
        connect(action, &QAction::triggered,
                [this, baseName, suffix, clickPos] {
            createNewDesktopFile(baseName, suffix, clickPos);
        });
    };

    addNewFileAction("文本文档 (.txt)", "新建文本文档", ".txt",
                     QStringList() << "text-x-generic", "TXT", QColor("#2563eb"));
    addNewFileAction("Word 文档 (.docx)", "新建 Word 文档", ".docx",
                     QStringList() << "application-vnd.openxmlformats-officedocument.wordprocessingml.document"
                                   << "x-office-document",
                     "DOC", QColor("#2563eb"));
    addNewFileAction("Excel 工作表 (.xlsx)", "新建 Excel 工作表", ".xlsx",
                     QStringList() << "application-vnd.openxmlformats-officedocument.spreadsheetml.sheet"
                                   << "x-office-spreadsheet",
                     "XLS", QColor("#16a34a"));
    addNewFileAction("PowerPoint 演示文稿 (.pptx)", "新建 PowerPoint 演示文稿", ".pptx",
                     QStringList() << "application-vnd.openxmlformats-officedocument.presentationml.presentation"
                                   << "x-office-presentation",
                     "PPT", QColor("#ea580c"));
    addNewFileAction("Markdown 文档 (.md)", "新建 Markdown 文档", ".md",
                     QStringList() << "text-markdown" << "text-x-generic",
                     "MD", QColor("#7c3aed"));
    addNewFileAction("CSV 表格 (.csv)", "新建 CSV 表格", ".csv",
                     QStringList() << "text-csv" << "x-office-spreadsheet",
                     "CSV", QColor("#0891b2"));
    addNewFileAction("JSON 文件 (.json)", "新建 JSON 文件", ".json",
                     QStringList() << "application-json" << "text-x-script",
                     "{}", QColor("#475569"));

    auto *actPaste = rootAction(
        menuIcon(QStringList() << "edit-paste", "▣", QColor("#64748b")),
        "粘贴");
    actPaste->setEnabled(FileClipboard::hasFiles());
    connect(actPaste, &QAction::triggered, [this, clickPos] {
        pasteToDesktop(clickPos);
    });

    auto *actPeony = fencesMenu->addAction(
        menuIcon(QStringList() << "system-file-manager" << "folder",
                 "📁", QColor("#ea580c")),
        "打开文件管理器");
    connect(actPeony, &QAction::triggered, [] {
        const QString peony = QStandardPaths::findExecutable(
            QStringLiteral("peony"));
        if (!peony.isEmpty() &&
            QProcess::startDetached(peony, QStringList()))
            return;
        const QString home = QStandardPaths::writableLocation(
            QStandardPaths::HomeLocation);
        if (QProcess::startDetached(
                QStringLiteral("gio"),
                QStringList() << QStringLiteral("open") << home))
            return;
        QProcess::startDetached(
            QStringLiteral("xdg-open"), QStringList() << home);
    });

    auto *glassAction = desktopSettingsMenu->addAction("分区液态玻璃");
    glassAction->setObjectName("fenceLiquidGlassAction");
    glassAction->setCheckable(true);
    glassAction->setChecked(m_fenceLiquidGlassEnabled);
    glassAction->setToolTip("所有分区的壁纸折射与鼠标边缘高光，保留磁吸轮廓。关闭即可恢复原配色。");
    connect(glassAction, &QAction::toggled, this, &DesktopCanvas::setFenceLiquidGlassEnabled);
    auto *magnetAction = desktopSettingsMenu->addAction("壁纸磁性画线");
    magnetAction->setObjectName("wallpaperMagnetAction");
    magnetAction->setCheckable(true);
    magnetAction->setChecked(m_wallpaperMagnetEnabled);
    magnetAction->setToolTip("拖动或调整分区时，自动沿壁纸明暗边界吸附轮廓。");
    connect(magnetAction, &QAction::toggled,
            this, &DesktopCanvas::setWallpaperMagnetEnabled);
    desktopSettingsMenu->addSeparator();

    auto *actSettings = desktopSettingsMenu->addAction(
        menuIcon(QStringList() << "preferences-desktop-font" << "preferences-other",
                 "Aa", QColor("#2563eb")),
        "桌面字体…");
    connect(actSettings, &QAction::triggered,
            [this] { showSettingsDialog(); });

    auto *actDesktopSync = fencesMenu->addAction(
        menuIcon(QStringList() << "folder-sync" << "folder-saved-search",
                 "↔", QColor("#0ea5e9")),
        "桌面文件同步设置…");
    connect(actDesktopSync, &QAction::triggered,
            [this] { showDesktopSyncSettingsDialog(); });

    auto *actWallpaper = desktopSettingsMenu->addAction(
        menuIcon(QStringList() << "preferences-desktop-wallpaper" << "image-x-generic",
                 "🖼", QColor("#16a34a")),
        "系统壁纸…");
    connect(actWallpaper, &QAction::triggered, [] {
        openWallpaperSettings();
    });

    auto *actFencesWallpaper = desktopSettingsMenu->addAction(
        menuIcon(QStringList() << "preferences-desktop-wallpaper" << "image-x-generic",
                 "▧", QColor("#0891b2")),
        "Fences 壁纸…");
    connect(actFencesWallpaper, &QAction::triggered,
            [this] { showWallpaperDialog(); });


    auto *actHelp = fencesMenu->addAction(
        menuIcon(QStringList() << "help-contents" << "help-browser",
                 "?", QColor("#475569")),
        "使用说明");
    connect(actHelp, &QAction::triggered, [this] {
        QMessageBox helpBox(this);
        helpBox.setWindowTitle("ukui-fences 使用说明");
        helpBox.setIcon(QMessageBox::Information);
        helpBox.setText(
            "<h3>ukui-fences 桌面分区工具</h3>"
            "<p>将桌面文件分组整理到半透明分区中，让桌面井然有序。</p>");
        helpBox.setInformativeText(
            "<b>基本操作</b><br>"
            "• <b>新建分区</b>：右键桌面空白处 → 新建 → 普通分区<br>"
            "• <b>拖入文件</b>：直接将桌面文件拖入分区<br>"
            "• <b>拖出文件</b>：从分区拖到桌面空白处<br>"
            "• <b>折叠/展开</b>：单击分区标题栏<br>"
            "• <b>删除文件</b>：选中后按 Delete 键，或拖到回收站图标<br><br>"
            "<b>复制与剪切</b><br>"
            "• 剪切后的文件会暂时置灰，表示正在等待移动<br>"
            "• 在文件管理器等位置粘贴完成后，图标会自动消失或恢复<br><br>"
            "<b>编辑模式</b>（右键桌面 → 编辑分区布局）<br>"
            "• 拖动分区标题栏可 <b>移动位置</b><br>"
            "• 拖动分区边缘可 <b>调整大小</b><br>"
            "• 靠近其他分区时会自动 <b>吸附对齐</b><br>"
            "• 双击标题栏可 <b>重命名</b> 分区<br><br>"
            "<b>快捷键</b><br>"
            "• Ctrl+A 全选 &nbsp;| Ctrl+C 复制 &nbsp;| Ctrl+X 剪切<br>"
            "• Ctrl+V 粘贴 &nbsp;| Delete 删除 &nbsp;| F2 重命名<br>"
            "• Enter 打开 &nbsp;| Ctrl+滚轮 缩放图标<br><br>"
            "<b>分区菜单</b>（右键分区标题栏）<br>"
            "• 重命名、折叠/展开、锁定分区<br>"
            "• 排序、透明度、颜色、图标自定义<br>"
            "• 标题字体 / 内部图标字体独立设置<br><br>"
            "<b>其他功能</b><br>"
            "• 右键桌面可新建文件夹/文本文件<br>"
            "• 布局备份：可导出/导入分区配置<br>"
            "• 系统图标（计算机、回收站）可自由拖放"
        );
        helpBox.exec();
    });

    auto *actAbout = fencesMenu->addAction(
        menuIcon(QStringList() << "help-about" << "dialog-information",
                 "i", QColor("#0ea5e9")),
        "关于 ukui-fences");
    connect(actAbout, &QAction::triggered, [this] {
        const QString version = QCoreApplication::applicationVersion();
        QMessageBox::about(this, "关于 ukui-fences",
            QStringLiteral(
                "ukui-fences\n\n"
                "一个面向 UKUI 桌面的 Fences 风格桌面分区工具。\n"
                "版本：%1\n\n"
                "简要使用：右键桌面 → 新建 → 普通分区，将桌面文件拖入分区整理；"
                "右键桌面开启编辑分区布局可移动或缩放分区；"
                "拖到回收站或按 Delete 可移到回收站；"
                "剪切后置灰，移动完成后会自动恢复或移除。")
                .arg(version));
    });

    fencesMenu->addSeparator();

    auto *actSystemDesktop = fencesMenu->addAction(
        menuIcon(QStringList() << "user-desktop" << "computer",
                 "▣", QColor("#16a34a")),
        "切换到系统桌面");
    connect(actSystemDesktop, &QAction::triggered,
            this, &DesktopCanvas::hideFences);

    auto *actQuit = fencesMenu->addAction(
        menuIcon(QStringList() << "application-exit" << "system-log-out",
                 "⏻", QColor("#dc2626")),
        "退出 ukui-fences");
    connect(actQuit, &QAction::triggered, [] {
        QApplication::quit();
    });

    auto *actExport = layoutMenu->addAction(
        menuIcon(QStringList() << "document-save-as" << "document-export",
                 "↑", QColor("#2563eb")),
        "导出布局…");
    connect(actExport, &QAction::triggered,
            [this] { exportLayout(); });
    auto *actImport = layoutMenu->addAction(
        menuIcon(QStringList() << "document-open" << "document-import",
                 "↓", QColor("#16a34a")),
        "导入布局…");
    connect(actImport, &QAction::triggered,
            [this] { importLayout(); });
    layoutMenu->addSeparator();
    auto *actReset = layoutMenu->addAction(
        menuIcon(QStringList() << "edit-delete" << "view-refresh",
                 "!", QColor("#dc2626")),
        "重置布局…");
    connect(actReset, &QAction::triggered, [this] {
        if (QMessageBox::question(this, "重置布局…",
                "确定清空所有分区和图标位置吗？") != QMessageBox::Yes)
            return;

        for (auto *fence : m_fences) {
            fence->hide();
            fence->deleteLater();
        }
        m_fences.clear();
        clearLooseIcons();
        m_looseIconPositions.clear();
        clearSelection();
        m_iconScale = 1.0;
        m_desktopIconScale = 1.0;
        m_gridColumns = 18;
        m_gridRows = 11;
        m_autoArrange = false;
        m_arrangeMode = ArrangeMode::Manual;
        addSystemIcons();
        refreshDesktopIcons();
        saveLayout();
    });

    auto *previewAction = new QAction("液态气泡预览…", &menu);
    previewAction->setObjectName("liquidPopupPreviewAction");
    previewAction->setToolTip("预览四个界面的统一气泡效果，不修改应用设置。");
    connect(previewAction, &QAction::triggered, this, [this] {
        QStringList candidates;
        candidates << QDir(QCoreApplication::applicationDirPath()).filePath("liquid-popup-preview")
                   << QStandardPaths::findExecutable("liquid-popup-preview");
#ifdef UKUI_LIQUID_POPUP_PREVIEW_PATH
        candidates << QString::fromUtf8(UKUI_LIQUID_POPUP_PREVIEW_PATH);
#endif
        for (const QString &candidate : candidates) {
            if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()
                && QProcess::startDetached(candidate, QStringList()))
                return;
        }
        QMessageBox::warning(this, "液态气泡预览", "无法打开预览程序，请重新安装液态气泡预览组件。");
    });

    // Compose menus last: preserve each action's behaviour, make the hierarchy
    // and separators explicit, and keep only nine entries at the desktop root.
    auto resetMenu = [](QMenu *target) {
        for (QAction *action : target->actions()) target->removeAction(action);
    };
    for (QAction *action : {smartStartup, monitorStartup,
            widgetStartupMenu->findChild<QAction *>("clockStartupAction"),
            widgetStartupMenu->findChild<QAction *>("activityStartupAction"),
            widgetStartupMenu->findChild<QAction *>("musicStartupAction"),
            widgetStartupMenu->findChild<QAction *>("calendarStartupAction")}) {
        widgetStartupMenu->removeAction(action); widgetStartupMenu->addAction(action);
    }
    for (QAction *action : {actSmartSpace, actDesklet,
            widgetsMenu->findChild<QAction *>("clockWidgetAction"),
            widgetsMenu->findChild<QAction *>("activityWidgetAction"),
            widgetsMenu->findChild<QAction *>("musicWidgetAction"),
            widgetsMenu->findChild<QAction *>("calendarWidgetAction")}) {
        widgetsMenu->removeAction(action); widgetsMenu->addAction(action);
    }
    sortModeMenu->removeAction(actDesktopGrid);
    sortModeMenu->insertSeparator(actExport);
    sortModeMenu->insertAction(actExport, actDesktopGrid);
    sortModeMenu->insertSeparator(actExport);
    resetMenu(desktopSettingsMenu);
    desktopSettingsMenu->addAction(glassAction);
    desktopSettingsMenu->addAction(magnetAction);
    desktopSettingsMenu->addAction(previewAction);
    desktopSettingsMenu->addSeparator();
    desktopSettingsMenu->addMenu(themeMenu);
    desktopSettingsMenu->addAction(actSettings);
    desktopSettingsMenu->addSeparator();
    desktopSettingsMenu->addAction(actWallpaper);
    desktopSettingsMenu->addAction(actFencesWallpaper);
    resetMenu(fencesMenu);
    fencesMenu->addAction(actDesktopSync);
    fencesMenu->addMenu(widgetStartupMenu);
    fencesMenu->addAction(actApplyExternalTheme);
    fencesMenu->addSeparator();
    fencesMenu->addAction(actPeony);
    fencesMenu->addSeparator();
    fencesMenu->addAction(actHelp);
    fencesMenu->addAction(actAbout);
    fencesMenu->addSeparator();
    fencesMenu->addAction(actSystemDesktop);
    fencesMenu->addAction(actQuit);
    menu.addMenu(newMenu);
    menu.addAction(actPaste);
    menu.addAction(actUndo);
    menu.addAction(actRefresh);
    menu.addSeparator();
    menu.addAction(actEdit);
    menu.addMenu(sortModeMenu);
    menu.addSeparator();
    menu.addMenu(widgetsMenu);
    menu.addMenu(desktopSettingsMenu);
    menu.addSeparator();
    menu.addMenu(fencesMenu);

    menu.exec(e->globalPos());
    e->accept();
}

// ── 布局持久化 ────────────────────────────────────────────

void DesktopCanvas::saveLayout()
{
    if (m_loadingLayout)
        return;

    QJsonArray fenceArr;
    for (auto *fence : m_fences) {
        QJsonObject obj;
        obj["id"]        = fence->fenceId();
        obj["title"]     = fence->title();
        obj["x"]         = fence->x();
        obj["y"]         = fence->y();
        obj["w"]         = fence->width();
        obj["h"]         = fence->collapsed() ? FenceWidget::TITLE_H : fence->expandedHeight();
        obj["expandedHeight"] = fence->expandedHeight();
        obj["color"]     = fence->fenceColor().name(QColor::HexArgb);
        obj["collapsed"] = fence->collapsed();
        obj["locked"]    = fence->locked();
        obj["magneticEdge"] =
            static_cast<int>(fence->m_magneticEdge);
        if (!fence->m_magneticContour.isEmpty()) {
            QJsonArray contourArr;
            for (const QPointF &pt : fence->m_magneticContour) {
                QJsonArray ptArr;
                ptArr.append(pt.x());
                ptArr.append(pt.y());
                contourArr.append(ptArr);
            }
            obj["magneticContour"] = contourArr;
        }
        if (!fence->titleIconPath().isEmpty())
            obj["titleIcon"] = fence->titleIconPath();

        if (fence->m_hasTitleFont) {
            QJsonObject fontObj;
            fontObj["family"] = fence->m_titleFontFamily;
            fontObj["size"]   = fence->m_titleFontSize;
            fontObj["color"]  = fence->m_titleFontColor.name(QColor::HexArgb);
            fontObj["bold"]   = fence->m_titleFontBold;
            fontObj["italic"] = fence->m_titleFontItalic;
            obj["titleFont"]  = fontObj;
        }

        // 分区独立字体
        if (fence->hasLocalFont()) {
            QJsonObject fontObj;
            fontObj["family"] = fence->m_localFontFamily;
            fontObj["size"]   = fence->m_localFontSize;
            fontObj["color"]  = fence->m_localFontColor.name(QColor::HexArgb);
            fontObj["bold"]   = fence->m_localFontBold;
            fontObj["italic"] = fence->m_localFontItalic;
            obj["localFont"]  = fontObj;
        }

        QJsonArray files;
        for (const DesktopItem &item : fence->items())
            files.append(item.filePath);
        obj["files"] = files;
        fenceArr.append(obj);
    }

    QJsonArray loosePosArr;
    for (auto it = m_looseIconPositions.constBegin();
         it != m_looseIconPositions.constEnd(); ++it) {
        QJsonObject obj;
        obj["path"] = it.key();
        obj["x"] = it.value().x();
        obj["y"] = it.value().y();
        loosePosArr.append(obj);
    }

    QJsonObject root;
    root["version"] = 4;
    root["iconScale"] = m_iconScale;
    root["desktopIconScale"] = m_desktopIconScale;
    root["gridColumns"] = m_gridColumns;
    root["gridRows"] = m_gridRows;
    root["autoArrange"] = m_autoArrange;
    root["arrangeMode"] = static_cast<int>(m_arrangeMode);
    root["desktopInboxFenceId"] = m_desktopInboxFenceId;
    root["wallpaperPath"] = m_wallpaperPath;
    root["wallpaperMode"] = static_cast<int>(m_wallpaperMode);
    root["fences"] = fenceArr;
    root["looseIconPositions"] = loosePosArr;

    // 字体配置
    if (!m_fontFamily.isEmpty())
        root["fontFamily"] = m_fontFamily;
    root["fontSize"] = m_fontSize;
    root["fontColor"] = m_fontColor.name(QColor::HexArgb);
    root["fontBold"] = m_fontBold;
    root["fontItalic"] = m_fontItalic;

    const QString path = layoutPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString tmpPath = path + ".tmp";
    QFile f(tmpPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson());
        f.flush();
        f.close();
        QFile::remove(path);
        QFile::rename(tmpPath, path);
    }
}

void DesktopCanvas::loadLayout()
{
    QFile f(layoutPath());
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    const QJsonObject root = doc.object();
    const bool wasLoadingLayout = m_loadingLayout;
    m_loadingLayout = true;

    m_iconScale = qBound<qreal>(0.75, root["iconScale"].toDouble(1.0), 1.75);
    m_desktopIconScale = qBound<qreal>(
        0.75, root["desktopIconScale"].toDouble(m_iconScale), 1.75);
    m_gridColumns = qBound(4, root["gridColumns"].toInt(18), 40);
    m_gridRows = qBound(3, root["gridRows"].toInt(11), 24);
    m_autoArrange = root["autoArrange"].toBool(false);
    const bool hasInboxSetting = root.contains("desktopInboxFenceId");
    m_desktopInboxFenceId = root["desktopInboxFenceId"].toString().trimmed();
    const int mode = root["arrangeMode"].toInt(0);
    if (mode >= static_cast<int>(ArrangeMode::Manual) &&
        mode <= static_cast<int>(ArrangeMode::ByModifiedTime))
        m_arrangeMode = static_cast<ArrangeMode>(mode);
    else
        m_arrangeMode = ArrangeMode::Manual;

    m_wallpaperPath = root["wallpaperPath"].toString();
    const int wallpaperMode =
        root["wallpaperMode"].toInt(static_cast<int>(WallpaperMode::System));
    if (wallpaperMode >= static_cast<int>(WallpaperMode::Fill) &&
        wallpaperMode <= static_cast<int>(WallpaperMode::System)) {
        m_wallpaperMode = static_cast<WallpaperMode>(wallpaperMode);
    } else {
        m_wallpaperMode = WallpaperMode::System;
    }
    if (m_wallpaperPath.isEmpty())
        m_wallpaperMode = WallpaperMode::System;

    // 字体配置
    m_fontFamily = root["fontFamily"].toString();
    const int fs = root["fontSize"].toInt(11);
    m_fontSize = qBound(8, fs, 24);
    QColor fc;
    fc.setNamedColor(root["fontColor"].toString("#ffffffff"));
    if (fc.isValid()) m_fontColor = fc;
    m_fontBold = root["fontBold"].toBool(false);
    m_fontItalic = root["fontItalic"].toBool(false);

    for (const QJsonValue &v : root["looseIconPositions"].toArray()) {
        const QJsonObject obj = v.toObject();
        const QString path = obj["path"].toString();
        if (!path.isEmpty())
            m_looseIconPositions[path] =
                QPoint(obj["x"].toInt(), obj["y"].toInt());
    }

    for (const QJsonValue &v : root["systemIconPositions"].toArray()) {
        const QJsonObject obj = v.toObject();
        const QString path = obj["path"].toString();
        if (!path.isEmpty() && !m_looseIconPositions.contains(path))
            m_looseIconPositions[path] =
                QPoint(obj["x"].toInt(), obj["y"].toInt());
    }

    for (const QJsonValue &v : root["fences"].toArray()) {
        const QJsonObject obj = v.toObject();
        // Old layouts saved only the collapsed title height, sometimes even
        // with collapsed=false. Recover a usable body instead of reopening to
        // another title-only rectangle after restart.
        const int storedHeight = obj["h"].toInt(280);
        const int expandedHeight = qMax(FenceWidget::TITLE_H + 60,
            obj["expandedHeight"].toInt(storedHeight > FenceWidget::TITLE_H
                                       ? storedHeight : 240));
        const QRect geo(obj["x"].toInt(100), obj["y"].toInt(100),
                        qMax(160, obj["w"].toInt(380)), expandedHeight);

        QString title = obj["title"].toString("分区");
        const QStringList oldEmojiPrefixes = {
            QStringLiteral("🪐 "), QStringLiteral("🚀 "),
            QStringLiteral("📚 "), QStringLiteral("🛰 "),
            QStringLiteral("✨ "), QStringLiteral("🌙 ")
        };
        for (const QString &prefix : oldEmojiPrefixes) {
            if (title.startsWith(prefix)) {
                title.remove(0, prefix.size());
                break;
            }
        }
        auto *fence = createFence(title, geo);
        const QString storedId = obj["id"].toString().trimmed();
        if (!storedId.isEmpty())
            fence->m_id = storedId;

        QColor color;
        color.setNamedColor(obj["color"].toString("#5a0078d7"));
        if (color.isValid()) fence->setFenceColor(color);

        for (const QJsonValue &fv : obj["files"].toArray()) {
            const QString fp = fv.toString();
            DesktopItem item = DesktopItem::fromStoredPath(fp);
            if (item.isValid())
                fence->addItem(item);
        }

        if (obj["collapsed"].toBool()) {
            fence->m_collapsed = true;
            fence->resize(fence->width(), FenceWidget::TITLE_H);
            fence->updateShapeMask();
            fence->layoutIcons();
        }

        if (obj["locked"].toBool())
            fence->setLocked(true);

        const int magneticEdge = obj["magneticEdge"].toInt(0);
        if (m_wallpaperMagnetEnabled && magneticEdge >= static_cast<int>(
                FenceWidget::MagneticEdge::Left) &&
            magneticEdge <= static_cast<int>(
                FenceWidget::MagneticEdge::Bottom)) {
            fence->m_magneticEdge =
                static_cast<FenceWidget::MagneticEdge>(magneticEdge);
            // 优先恢复保存的轮廓点，否则重新检测
            const QJsonArray contourArr =
                obj["magneticContour"].toArray();
            if (!contourArr.isEmpty()) {
                fence->m_magneticContour.clear();
                fence->m_magneticContour.reserve(contourArr.size());
                for (const QJsonValue &ptVal : contourArr) {
                    const QJsonArray ptArr = ptVal.toArray();
                    if (ptArr.size() >= 2) {
                        fence->m_magneticContour.append(
                            QPointF(ptArr[0].toDouble(),
                                    ptArr[1].toDouble()));
                    }
                }
                fence->updateShapeMask();
                fence->layoutIcons();
            } else {
                QTimer::singleShot(
                    0, fence, &FenceWidget::refreshMagneticContour);
            }
        }

        const QString iconPath = obj["titleIcon"].toString();
        if (!iconPath.isEmpty() && QFileInfo::exists(iconPath))
            fence->setTitleIconPath(iconPath);

        if (obj.contains("titleFont")) {
            const QJsonObject fontObj = obj["titleFont"].toObject();
            fence->m_hasTitleFont = true;
            fence->m_titleFontFamily = fontObj["family"].toString();
            fence->m_titleFontSize = qBound(9, fontObj["size"].toInt(13), 28);
            QColor tc;
            tc.setNamedColor(fontObj["color"].toString("#ffffffff"));
            if (tc.isValid()) fence->m_titleFontColor = tc;
            fence->m_titleFontBold = fontObj["bold"].toBool(true);
            fence->m_titleFontItalic = fontObj["italic"].toBool();
            fence->update();
        }

        // 加载分区独立字体
        if (obj.contains("localFont")) {
            const QJsonObject fontObj = obj["localFont"].toObject();
            fence->m_hasLocalFont = true;
            fence->m_localFontFamily = fontObj["family"].toString();
            fence->m_localFontSize = qBound(8, fontObj["size"].toInt(11), 24);
            QColor fc;
            fc.setNamedColor(fontObj["color"].toString("#ffffffff"));
            if (fc.isValid()) fence->m_localFontColor = fc;
            fence->m_localFontBold = fontObj["bold"].toBool();
            fence->m_localFontItalic = fontObj["italic"].toBool();
            // 应用到已加载的图标
            for (auto *icon : fence->icons())
                fence->applyLocalIconFont(icon);
        }
    }

    if (hasInboxSetting && !m_desktopInboxFenceId.isEmpty() &&
        !fenceById(m_desktopInboxFenceId)) {
        m_desktopInboxFenceId.clear();
    } else if (!hasInboxSetting) {
        for (auto *fence : m_fences) {
            if (fence && fence->title() == QStringLiteral("临时停泊")) {
                m_desktopInboxFenceId = fence->fenceId();
                break;
            }
        }
    }

    m_loadingLayout = wasLoadingLayout;
}

void DesktopCanvas::exportLayout()
{
    saveLayout();

    const QString defaultPath =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + "/kyfences_backup_"
        + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")
        + ".json";
    const QString target = QFileDialog::getSaveFileName(
        this, "导出布局", defaultPath, "JSON 文件 (*.json)");
    if (target.isEmpty()) return;

    QFile::remove(target);
    if (QFile::copy(layoutPath(), target))
        QMessageBox::information(this, "导出布局", "布局已导出。");
    else
        QMessageBox::warning(this, "导出布局", "布局导出失败。");
}

void DesktopCanvas::importLayout()
{
    const QString source = QFileDialog::getOpenFileName(
        this, "导入布局",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        "JSON 文件 (*.json)");
    if (source.isEmpty()) return;

    QFile f(source);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "导入布局", "无法读取布局文件。");
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject() || !doc.object().contains("fences")) {
        QMessageBox::warning(this, "导入布局", "布局文件格式不正确。");
        return;
    }

    const QString target = layoutPath();
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    if (!QFile::copy(source, target)) {
        QMessageBox::warning(this, "导入布局", "写入布局失败。");
        return;
    }

    for (auto *fence : m_fences) {
        fence->hide();
        fence->deleteLater();
    }
    m_fences.clear();
    clearLooseIcons();
    m_looseIconPositions.clear();
    clearSelection();

    loadLayout();
    loadWallpaper();
    addSystemIcons();
    for (auto *fence : m_fences)
        fence->setIconScale(m_iconScale);
    for (auto *icon : m_looseIcons)
        icon->setVisualScale(m_desktopIconScale);
    refreshDesktopIcons();
    QMessageBox::information(this, "导入布局", "布局已导入。");
}
