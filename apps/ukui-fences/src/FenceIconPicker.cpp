#include "FenceIconPicker.h"
#include "FencesWindowIdentity.h"
#include "LiquidDialog.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include <atomic>
#include <QAbstractListModel>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const QStringList defaultNames = {
    "orbit", "tasks", "industry", "services", "presentation",
    "dock", "inbox", "archive", "ideas", "favorites"
};
const QStringList defaultLabels = {
    "轨道", "任务", "行业资料", "项目与服务", "演示素材",
    "临时停泊", "收件箱", "归档", "灵感", "收藏"
};

QStringList listValue(const QVariant &value)
{
    QStringList result;
    for (const auto &part : value.toStringList())
        for (const auto &name : part.split(',', QString::SkipEmptyParts))
            if (!name.trimmed().isEmpty()) result << name.trimmed();
    return result;
}

// Read only the selected theme, its declared parents and Qt's fallback.
// No images are decoded in this worker; closing the chooser cancels the scan.
QStringList collectIconNames(const QStringList &roots, QStringList themes,
    const QStringList &fallbackRoots, const std::shared_ptr<std::atomic_bool> &cancelled)
{
    auto interrupted = [&] {return cancelled->load() || QThread::currentThread()->isInterruptionRequested();};
    QSet<QString> names, visited;
    auto collect = [&](const QString &directory) {
        QDirIterator files(directory, {"*.png", "*.svg", "*.xpm"}, QDir::Files);
        while (files.hasNext() && !interrupted()) {
            files.next();
            names.insert(files.fileInfo().completeBaseName());
        }
    };
    // Appending parents after each theme gives a bounded traversal even
    // for broken themes with cycles. QIcon performs the actual lookup.
    for (int i = 0; i < themes.size() && !interrupted(); ++i) {
        const QString theme = themes.at(i);
        if (theme.isEmpty() || theme.contains('/') || theme == "." || theme == ".."
            || visited.contains(theme)) continue;
        visited.insert(theme);
        QStringList directories;
        for (const auto &root : roots) {
            const QString index = root + '/' + theme + "/index.theme";
            if (!QFileInfo::exists(index)) continue;
            QSettings settings(index, QSettings::IniFormat);
            settings.setIniCodec("UTF-8");
            settings.beginGroup("Icon Theme");
            directories = listValue(settings.value("Directories"));
            directories += listValue(settings.value("ScaledDirectories"));
            themes += listValue(settings.value("Inherits"));
            break; // First index governs all roots, as in the specification.
        }
        directories.removeDuplicates();
        for (const auto &root : roots) {
            for (const auto &subdir : directories) {
                if (interrupted()) break;
                const auto parts = subdir.split('/');
                if (QDir::isAbsolutePath(subdir) || parts.contains("..")) continue;
                collect(root + '/' + theme + '/' + subdir);
            }
        }
    }
    for (const auto &root : fallbackRoots) {
        if (interrupted()) break;
        collect(root);
    }
    if (interrupted()) return {};
    QStringList result = names.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

class IconListModel : public QAbstractListModel {
public:
    explicit IconListModel(bool system, QObject *parent)
        : QAbstractListModel(parent), themed(system) {}
    QStringList values, labels;
    bool themed;
    void setNames(const QStringList &names) {
        beginResetModel(); values = names; endResetModel();
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : values.size();
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= values.size()) return {};
        const QString value = values.at(index.row());
        if (role == Qt::DisplayRole)
            return labels.isEmpty() ? value : labels.at(index.row());
        if (role == Qt::ToolTipRole || role == Qt::UserRole) return value;
        if (role == Qt::DecorationRole)
            return themed ? QIcon::fromTheme(value) : QIcon(value);
        return {};
    }
};

class IconGridDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(136,84);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        p->save();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(selected ? QColor("#cce9e7") : QColor("#edf4f5"));
            p->drawRoundedRect(option.rect.adjusted(3,3,-3,-3),8,8);
        }
        const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(p,QRect(option.rect.center().x()-18,option.rect.top()+6,36,36),
                   Qt::AlignCenter,QIcon::Normal);
        const QRect text = option.rect.adjusted(6,46,-6,-6);
        p->setPen(QColor("#20343b"));p->setFont(option.font);
        p->drawText(text,Qt::AlignHCenter|Qt::AlignTop,
            option.fontMetrics.elidedText(index.data().toString(),Qt::ElideRight,text.width()));
        p->restore();
    }
};

QListView *iconView(QWidget *parent, const QString &id)
{
    auto *view = new QListView(parent);
    view->setObjectName(id);
    // The glass shell stays translucent; only the icon canvas is opaque so
    // monochrome/transparent theme icons have reliable contrast.
    view->setStyleSheet("QListView {background:#ffffff;color:#20343b;"
                       "border:1px solid #d5dfe3;border-radius:6px;padding:4px;}");
    QPalette palette=view->palette();
    palette.setColor(QPalette::Base,Qt::white);
    palette.setColor(QPalette::Text,QColor("#20343b"));
    view->setPalette(palette);
    auto *style = QStyleFactory::create("Fusion");
    style->setParent(view);
    view->setStyle(style);
    view->setViewMode(QListView::IconMode);
    view->setResizeMode(QListView::Adjust);
    view->setMovement(QListView::Static);
    view->setLayoutMode(QListView::Batched);
    view->setBatchSize(100);
    view->setUniformItemSizes(true);
    view->setItemDelegate(new IconGridDelegate(view));
    view->setMouseTracking(true);
    view->setIconSize(QSize(36,36));
    view->setGridSize(QSize(136,84));
    view->setWordWrap(true);
    view->setTextElideMode(Qt::ElideRight);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return view;
}
}

QStringList FenceIconPicker::defaultIconPaths()
{
    QStringList paths;
    for (const auto &name : defaultNames) paths << ":/fence-icons/" + name + ".svg";
    return paths;
}

QString FenceIconPicker::defaultIconLabel(const QString &path)
{
    const int index = defaultIconPaths().indexOf(path);
    return index >= 0 ? defaultLabels.at(index) : QString();
}

FenceIconPicker::Choice FenceIconPicker::choose(QWidget *parent,
    const QString &themeName, const QString &path)
{
    LiquidDialog::Dialog dialog(parent);
    dialog.setObjectName("fenceIconPicker");
    dialog.setWindowTitle("选择分区图标");
    QIcon icon;
    const QIcon source(":/settings/ukui-fences-settings.svg");
    for(int side:{16,24,32,48,64,128,256})icon.addPixmap(source.pixmap(side,side));
    dialog.setWindowIcon(icon);
    dialog.setWindowRole("fences-icon-picker");
    applyFencesWindowIdentity(&dialog,"ukui-fences-settings");
    dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(
        "\nQWidget#fenceIconPicker QTabWidget::pane {background:transparent;border:0;}"
        "QWidget#fenceIconPicker QTabBar::tab {background:rgba(16,28,41,175);color:#d4e9fa;"
        "padding:9px 14px;border:0;border-top-left-radius:7px;border-top-right-radius:7px;}"
        "QWidget#fenceIconPicker QTabBar::tab:selected {background:rgba(70,139,144,170);color:#ffffff;}"
        "QWidget#fenceIconSystemPage {background:transparent;}"));
    dialog.resize(660,520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    // Use the same local tab style as the settings center. UKUI's slide
    // animator paints opaque snapshots over otherwise transparent pages.
    auto *tabStyle = QStyleFactory::create("Fusion");
    tabStyle->setParent(&dialog);
    tabs->setStyle(tabStyle);
    tabs->tabBar()->setStyle(tabStyle);
    for (auto *stack : tabs->findChildren<QStackedWidget *>()) stack->setStyle(tabStyle);
    tabs->setObjectName("fenceIconTabs");
    layout->addWidget(tabs,1);
    auto *systemPage = new QWidget(tabs);
    systemPage->setObjectName("fenceIconSystemPage");
    systemPage->setAutoFillBackground(false);
    auto *systemLayout = new QVBoxLayout(systemPage);
    auto *search = new QLineEdit(systemPage);
    search->setObjectName("fenceIconSearch");
    search->setPlaceholderText("搜索系统图标名称，例如 folder、music、office");
    search->setClearButtonEnabled(true);
    search->setPalette(dialog.palette());
    systemLayout->addWidget(search);
    auto *status = new QLabel("正在读取系统图标库…",systemPage);
    status->setObjectName("fenceIconCatalogStatus");
    systemLayout->addWidget(status);
    auto *systemView = iconView(systemPage,"fenceSystemIconList");
    auto *systemModel = new IconListModel(true,&dialog);
    auto *proxy = new QSortFilterProxyModel(&dialog);
    proxy->setSourceModel(systemModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    systemView->setModel(proxy);
    systemLayout->addWidget(systemView,1);
    tabs->addTab(systemPage,"系统图标库");
    auto *defaults = iconView(tabs,"fenceDefaultIconList");
    auto *defaultModel = new IconListModel(false,&dialog);
    defaultModel->labels = defaultLabels;
    defaultModel->setNames(defaultIconPaths());
    defaults->setModel(defaultModel);
    tabs->addTab(defaults,"内置默认图标");
    auto *selection = new QLabel(&dialog);
    selection->setWordWrap(true);
    selection->setText("选择一个图标后点击确定；取消不会改变当前图标。");
    layout->addWidget(selection);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->setObjectName("fenceIconButtons");
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText("确定");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    ok->setEnabled(false);
    layout->addWidget(buttons);
    Choice chosen;
    auto update = [&] {
        auto *view = tabs->currentIndex() == 0 ? systemView : defaults;
        const auto index = view->currentIndex();
        chosen = {};
        if (index.isValid()) {
            const auto value = index.data(Qt::UserRole).toString();
            if (view == systemView) chosen.themeName = value;
            else chosen.path = value;
            const bool available = view != systemView || QIcon::hasThemeIcon(value);
            ok->setEnabled(available);
            selection->setText(available ? "已选：" + index.data().toString()
                : "当前系统主题无法读取此图标，请选择其他图标。");
        } else {
            ok->setEnabled(false);
            selection->setText("选择一个图标后点击确定；取消不会改变当前图标。");
        }
    };
    for (auto *view : {systemView,defaults})
        QObject::connect(view->selectionModel(),&QItemSelectionModel::currentChanged,
            &dialog,update);
    QObject::connect(tabs,&QTabWidget::currentChanged,&dialog,update);
    QTimer filter;
    filter.setSingleShot(true); filter.setInterval(100);
    QObject::connect(search,&QLineEdit::textChanged,&filter,[&] {filter.start();});
    QObject::connect(&filter,&QTimer::timeout,&dialog,[&] {
        proxy->setFilterFixedString(search->text().trimmed()); update();
    });
    const int defaultIndex = defaultModel->values.indexOf(path);
    if (defaultIndex >= 0) {
        defaults->setCurrentIndex(defaultModel->index(defaultIndex,0));
        tabs->setCurrentIndex(1);
    }
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    QObject::connect(&dialog,&QObject::destroyed,[cancelled] {cancelled->store(true);});
    Choice acceptedChoice;
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        if (!ok->isEnabled() || (chosen.path.isEmpty() && chosen.themeName.isEmpty())) return;
        // Pin the visible choice before the closing animation. A pending
        // filter or catalog delivery must never turn acceptance into a clear.
        acceptedChoice = chosen;acceptedChoice.accepted = true;
        filter.stop();cancelled->store(true);dialog.accept();
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,[&] {
        filter.stop();cancelled->store(true);dialog.reject();
    });
    bool catalogStarted = false;
    auto loadCatalog = [&] {
        if (tabs->currentIndex() != 0 || catalogStarted) return;
        catalogStarted = true;
        const auto roots = QIcon::themeSearchPaths();
        const auto themes = QStringList{QIcon::themeName(), QIcon::fallbackThemeName(), "hicolor"};
        const auto fallbackRoots = QIcon::fallbackSearchPaths();
        BackgroundTask::run(&dialog,
            [roots,themes,fallbackRoots,cancelled] {return collectIconNames(roots,themes,fallbackRoots,cancelled);},
            [=](const QStringList &names) {
                if (cancelled->load()) return;
                systemModel->setNames(names);
                status->setText(QString("当前主题：%1 · %2 个图标")
                    .arg(QIcon::themeName().isEmpty() ? QString("系统默认") : QIcon::themeName())
                    .arg(names.size()));
                const int index = names.indexOf(themeName);
                if (index >= 0)
                    systemView->setCurrentIndex(proxy->mapFromSource(systemModel->index(index,0)));
            });
    };
    QObject::connect(tabs,&QTabWidget::currentChanged,&dialog,loadCatalog);
    loadCatalog();
    const int result = dialog.exec();
    cancelled->store(true);
    return result == QDialog::Accepted ? acceptedChoice : Choice();
}
