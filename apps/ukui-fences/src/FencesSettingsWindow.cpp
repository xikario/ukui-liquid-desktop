#include "LiquidDialog.h"
#include "FencesSettingsWindow.h"
#include "DesktopCanvas.h"
#include "DesktopWidgets.h"
#include "SmartSpaceWidget.h"
#include "SystemMonitor.h"
#include "CalendarDesklet.h"
#include "MusicDesklet.h"
#include "MprisPlayer.h"
#include "MusicClientsSettings.h"
#include "ActivityRecorder.h"
#include "FenceWidget.h"
#include "FenceIconPicker.h"
#include "LiquidOpticsRenderer.h"
#include "LiquidMaterialPreparation.h"
#include "LiquidPopup.h"
#include "SettingsComboPopup.h"
#include <QTimer>
#include <QTabWidget>
#include <QTabBar>
#include <QStyleFactory>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QProcess>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef None
#undef Status

namespace {
struct Page { const char *id; const char *name; };
const Page pages[] = {{"appearance","液态外观"},{"wallpaper","壁纸与配色"},
    {"icons","图标与文字"},{"layout","分区与布局"},{"widgets","桌面小组件"},
    {"smart","    智能空间"},{"monitor","    系统监视"},{"clock","    时钟与倒计时"},
    {"activity","    活动统计"},{"music","    音乐播放器"},{"calendar","    日历与待办"},
    {"sync","文件同步"},{"help","帮助与维护"}};
QPushButton *button(QVBoxLayout *layout, const QString &text, const std::function<void()> &fn,
                    const QString &id = {}) {
    auto *b=new QPushButton(text,layout->parentWidget());b->setObjectName(id);
    b->setCursor(Qt::PointingHandCursor); b->setMinimumHeight(34);
    layout->addWidget(b); QObject::connect(b,&QPushButton::clicked,b,fn);return b;
}
QLabel *hint(QVBoxLayout *layout,const QString &text) {
    auto *label=new QLabel(text,layout->parentWidget());label->setWordWrap(true);
    label->setProperty("hint",true);layout->addWidget(label);return label;
}
QVBoxLayout *card(QVBoxLayout *layout, const QString &title) {
    auto *frame=new QFrame(layout->parentWidget());frame->setObjectName("settingsCard");
    auto *inner=new QVBoxLayout(frame);inner->setContentsMargins(16,14,16,14);inner->setSpacing(10);
    if(!title.isEmpty()){auto *l=new QLabel(title,frame);l->setObjectName("cardHeading");inner->addWidget(l);}
    layout->addWidget(frame);return inner;
}
}
FencesSettingsWindow::FencesSettingsWindow(DesktopCanvas *canvas)
    : QWidget(nullptr),m_canvas(canvas),m_optics(new LiquidOpticsRenderer) {
    setObjectName("fencesSettingsWindow");
    LiquidDialog::installMotion(this);setWindowTitle("Fences 设置");
    setWindowRole("fences-settings");
    const QIcon source(":/settings/ukui-fences-settings.svg");
    QIcon icon;
    for(int side:{16,24,32,48,64,128,256})icon.addPixmap(source.pixmap(side,side));
    setWindowIcon(icon);
    setWindowFlags(Qt::Window|Qt::FramelessWindowHint|Qt::WindowMinimizeButtonHint
        |Qt::WindowCloseButtonHint);setAttribute(Qt::WA_TranslucentBackground);
    QPalette colors=palette();
    colors.setColor(QPalette::Window,QColor("#1c3044"));colors.setColor(QPalette::Base,QColor("#172b40"));
    colors.setColor(QPalette::Text,QColor("#edf5ff"));colors.setColor(QPalette::WindowText,QColor("#edf5ff"));
    colors.setColor(QPalette::ButtonText,QColor("#edf5ff"));colors.setColor(QPalette::PlaceholderText,QColor("#9bb0c6"));
    m_content=new QWidget(this);m_content->setObjectName("fencesSettingsSurface");m_content->setPalette(colors);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->addWidget(m_content);
    setAttribute(Qt::WA_DeleteOnClose);setMinimumSize(740,480);
    const QRect available=QGuiApplication::primaryScreen()->availableGeometry();
    resize(QSize(1020,730).boundedTo(available.size()-QSize(32,32)));
    move(available.center()-rect().center());
    auto *root=new QVBoxLayout(m_content);root->setContentsMargins(20,16,20,10);root->setSpacing(14);
    auto *titlebar=new QWidget(this);titlebar->setObjectName("settingsTitlebar");titlebar->installEventFilter(this);
    auto *titleRow=new QHBoxLayout(titlebar);titleRow->setContentsMargins(0,0,0,0);
    auto *title=new QLabel("Fences 设置",titlebar);title->setObjectName("settingsHeading");title->installEventFilter(this);
    titleRow->addWidget(title,1);
    auto *minimize=new QPushButton("−",titlebar);minimize->setAccessibleName("最小化");minimize->setFixedSize(34,30);
    minimize->setObjectName("settingsMinimize");
    minimize->setProperty("liquidButtonGlyph","minimize");
    connect(minimize,&QPushButton::clicked,this,&QWidget::showMinimized);titleRow->addWidget(minimize);
    auto *closeButton=new QPushButton("×",titlebar);closeButton->setAccessibleName("关闭设置");closeButton->setFixedSize(34,30);
    closeButton->setObjectName("settingsClose");
    closeButton->setProperty("liquidButtonGlyph","close");
    connect(closeButton,&QPushButton::clicked,this,&QWidget::close);titleRow->addWidget(closeButton);root->addWidget(titlebar);
    auto *body=new QHBoxLayout;body->setSpacing(16);root->addLayout(body,1);
    m_navigation=new QListWidget(this);m_navigation->setObjectName("settingsNavigation");m_navigation->setFixedWidth(186);
    m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for(const auto &p:pages){auto *item=new QListWidgetItem(QString::fromUtf8(p.name),m_navigation);item->setData(Qt::UserRole,p.id);}
    body->addWidget(m_navigation);
    m_stack=new QStackedWidget(this);m_stack->setObjectName("settingsPages");body->addWidget(m_stack,1);
    auto *bottom=new QHBoxLayout;auto *foot=new QLabel("外观开关即时生效 · 表单修改后请点击应用",this);foot->setProperty("hint",true);
    bottom->addWidget(foot,1);bottom->addWidget(new QSizeGrip(this));root->addLayout(bottom);
    m_content->setStyleSheet(R"(
        QWidget { color:#edf5ff; font-size:14px; }
        QWidget[settingsSurface=true], QStackedWidget { background:transparent; }
        QLabel#settingsHeading { font-size:24px; font-weight:600; }
        QLabel#pageHeading { font-size:21px; font-weight:600; }
        QLabel#cardHeading { font-size:15px; font-weight:600; }
        QLabel[hint=true] { color:#c2d4e6; font-size:12px; }
        QScrollArea,QWidget#settingsPage,QWidget#settingsViewport,QScrollArea#settingsPageScroll { background:transparent; border:0; }
        QListWidget#settingsNavigation { background:rgba(14,25,39,125); border:1px solid rgba(220,240,255,35); border-radius:14px; padding:7px; outline:0; }
        QListWidget#settingsNavigation::item { padding:8px 7px; border-radius:8px; }
        QListWidget#settingsNavigation::item:selected { background:rgba(102,207,198,95); }
        QListWidget#settingsNavigation::item:hover { background:rgba(222,239,255,23); }
        QFrame#settingsCard { background:rgba(14,24,40,172); border:1px solid rgba(220,240,255,35); border-radius:13px; }
        QPushButton { background:rgba(218,238,255,22); border:1px solid rgba(225,245,255,50); border-radius:8px; padding:6px 10px; }
        QPushButton:hover { background:rgba(119,215,209,65); }
        QPushButton:pressed { background:rgba(90,190,185,95); }
        QPushButton:disabled { color:#758598; }
        QLineEdit,QSpinBox,QComboBox,QListWidget,QTextEdit,QPlainTextEdit,QTextBrowser { background:rgba(12,22,38,180); color:#edf5ff; border:1px solid rgba(225,245,255,42); border-radius:7px; padding:5px; selection-background-color:#367f86; }
        QComboBox QAbstractItemView { background:#243749; color:#edf5ff; }
        QComboBox { padding-right:26px; }
        QComboBox::drop-down { width:24px; border:0; background:transparent; }
        QCheckBox { spacing:8px; }
        QScrollBar:vertical {background:rgba(18,31,45,80);width:9px;margin:0;}
        QScrollBar::handle:vertical {background:rgba(182,215,233,100);border-radius:4px;min-height:28px;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
        QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical {background:transparent;}
        QScrollBar:horizontal {background:rgba(18,31,45,80);height:9px;margin:0;}
        QScrollBar::handle:horizontal {background:rgba(182,215,233,100);border-radius:4px;min-width:28px;}
        QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {width:0;}
        QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal {background:transparent;}
        QTabWidget::pane { border:0; background:transparent; }
        QTabBar::tab { padding:8px; color:#d4e9fa; background:rgba(20,35,50,170); }
        QTabBar::tab:selected { background:rgba(70,139,144,150); }
    )");
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escape,&QShortcut::activated,this,[this]{
        if(auto *popup=QApplication::activePopupWidget()){popup->close();return;}
        close();
    });

    connect(canvas,&DesktopCanvas::desktopVisibilityChanged,this,&FencesSettingsWindow::refreshStates);
    // Give the panel a distinct application identity as well as an explicit
    // multi-resolution window icon; do not change the desktop canvas identity.
    if(QGuiApplication::platformName()=="xcb")if(Display *display=XOpenDisplay(nullptr)) {
        const Window window=static_cast<Window>(winId());
        QByteArray identity("ukui-fences-settings");
        XClassHint hint{identity.data(),identity.data()};XSetClassHint(display,window,&hint);
        XChangeProperty(display,window,XInternAtom(display,"_KDE_NET_WM_DESKTOP_FILE",False),
                        XInternAtom(display,"UTF8_STRING",False),8,PropModeReplace,
                        reinterpret_cast<const unsigned char *>(identity.constData()),identity.size());
        XSync(display,False);XCloseDisplay(display);
    }
    connect(m_navigation,&QListWidget::currentRowChanged,this,[this](int row){
        if(row>=0)openPage(m_navigation->item(row)->data(Qt::UserRole).toString());
    });
}
FencesSettingsWindow::~FencesSettingsWindow() = default;
void FencesSettingsWindow::openPage(const QString &requested) {
    QString id=requested.isEmpty()?QSettings().value("settingsWindow/page","appearance").toString():requested;
    if(id.startsWith("fence:")) {
        bool found=false;for(int i=0;i<m_navigation->count();++i)found|=m_navigation->item(i)->data(Qt::UserRole).toString()==id;
        if(!found) {auto *f=m_canvas->fenceById(id.mid(6));if(f){auto *item=new QListWidgetItem("    "+f->title(),m_navigation);item->setData(Qt::UserRole,id);}}
    }
    int row=0;for(int i=0;i<m_navigation->count();++i)if(m_navigation->item(i)->data(Qt::UserRole).toString()==id){row=i;break;}
    id=m_navigation->item(row)->data(Qt::UserRole).toString();
    {QSignalBlocker block(m_navigation);m_navigation->setCurrentRow(row);}
    if (m_pages.value(id)) {
        bool dirty=false;
        for(auto *w:m_pages[id]->findChildren<QWidget *>())dirty|=w->property("settingsDirty").toBool();
        if(!dirty && m_stack->currentWidget()!=m_pages[id]) {
            auto *old=m_pages.take(id).data();m_stack->removeWidget(old);delete old;
        }
    }
    if(!m_pages.value(id)) {
        m_pages[id]=buildPage(id);m_stack->addWidget(m_pages[id]);
    }
    m_stack->setCurrentWidget(m_pages[id]);refreshStates();
    QSettings().setValue("settingsWindow/page",id);

}
void FencesSettingsWindow::refreshMaterial() {
    if(!m_material.isNull() || m_materialPreparing || !m_canvas || !isVisible())return;
    const qreal dpr=devicePixelRatioF();
    QImage wall=m_canvas->wallpaperBackdrop(QRect(mapToGlobal(QPoint()),size()),dpr);
    if(wall.isNull()){wall=QImage(size()*dpr,QImage::Format_RGB32);wall.setDevicePixelRatio(dpr);wall.fill(QColor("#293c50"));}
    if(!m_preparation)m_preparation=new LiquidMaterial::Preparation(this);
    m_materialPreparing=true;
    const QSize requestedSize=size();
    m_preparation->request(wall,[this,requestedSize](const LiquidMaterial::Prepared &material){
        m_materialPreparing=false;
        const auto &theme=LiquidPopup::theme();m_optics->setOptics(theme.refraction,theme.tint,theme.highlight,1.0);
        m_optics->setPreparedWallpaper(material);m_material=m_optics->renderPanel(QRect(QPoint(),requestedSize),22);
        setProperty("materialBuilds",++m_materialBuilds);setProperty("opticalGpu",m_optics->usedGpu());
        if (!m_material.isNull()) m_optics.reset(); // Frozen for this window lifetime, including drag/resize/wallpaper changes.
        update();
    });
}
void FencesSettingsWindow::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if(m_material.isNull())QTimer::singleShot(0,this,&FencesSettingsWindow::refreshMaterial);
}
void FencesSettingsWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
    if(m_material.isNull()){p.setPen(Qt::NoPen);p.setBrush(QColor(28,43,59,240));p.drawRoundedRect(QRectF(rect()),22,22);}
    else p.drawImage(QRectF(rect()),m_material,QRectF(m_material.rect()));
}
bool FencesSettingsWindow::hasDrafts() const {
    for(auto *w:findChildren<QWidget *>())if(w->property("settingsDirty").toBool())return true;
    return false;
}
void FencesSettingsWindow::closeEvent(QCloseEvent *e) {
    if(hasDrafts()) {
        LiquidDialog::Dialog confirmation(this);confirmation.setObjectName("settingsDiscardDialog");
        confirmation.setWindowTitle("尚有未应用的修改");

        auto *layout=new QVBoxLayout(&confirmation);
        auto *label=new QLabel("关闭并放弃未应用的修改？",&confirmation);layout->addWidget(label);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Discard|QDialogButtonBox::Cancel,&confirmation);
        layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Discard)->setText("放弃修改");
        buttons->button(QDialogButtonBox::Cancel)->setText("继续编辑");
        buttons->button(QDialogButtonBox::Discard)->setAutoDefault(false);
        buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
        buttons->button(QDialogButtonBox::Cancel)->setFocus();
        connect(buttons->button(QDialogButtonBox::Discard),&QPushButton::clicked,&confirmation,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&confirmation,&QDialog::reject);
        if(confirmation.exec()!=QDialog::Accepted){e->ignore();return;}
    }
    e->accept();
}
void FencesSettingsWindow::changeEvent(QEvent *e) {
    QWidget::changeEvent(e);
    if(e->type()==QEvent::ActivationChange && isActiveWindow())refreshStates();
}
bool FencesSettingsWindow::eventFilter(QObject *o,QEvent *e) {
    if(e->type()==QEvent::MouseButtonPress){auto *m=static_cast<QMouseEvent *>(e);if(m->button()==Qt::LeftButton){m_dragging=true;m_dragOffset=m->globalPos()-pos();return true;}}
    if(e->type()==QEvent::MouseMove && m_dragging){auto *m=static_cast<QMouseEvent *>(e);move(m->globalPos()-m_dragOffset);return true;}
    if(e->type()==QEvent::MouseButtonRelease)m_dragging=false;
    return QWidget::eventFilter(o,e);
}
void FencesSettingsWindow::attachForm(QVBoxLayout *layout,QWidget *form) {
    layout->addWidget(form);form->setProperty("settingsForm",true);
    form->setPalette(m_content->palette());
    // QTabWidget sets autoFillBackground on ordinary page widgets in the UKUI
    // style. Clearing only scroll viewports leaves a white tab-sized rectangle.
    for(auto *tabs:form->findChildren<QTabWidget *>()){
        // UKUI's DefaultSlideAnimator creates opaque snapshot pages even when
        // the real pages are transparent. Use a local non-animated tab style.
        auto *tabStyle=QStyleFactory::create("Fusion");tabStyle->setParent(form);
        tabs->setStyle(tabStyle);tabs->tabBar()->setStyle(tabStyle);
        for(auto *stack:tabs->findChildren<QStackedWidget *>())stack->setStyle(tabStyle);
        for(int i=0;i<tabs->count();++i){
            auto *page=tabs->widget(i);page->setAutoFillBackground(false);
            page->setProperty("settingsSurface",true);
        }
    }
    for(auto *area:form->findChildren<QScrollArea *>()){
        area->viewport()->setObjectName("settingsViewport");area->viewport()->setAutoFillBackground(false);
        if(area->widget())area->widget()->setAutoFillBackground(false);
    }
    for(auto *box:form->findChildren<QDialogButtonBox *>()){
        if(auto *apply=box->button(QDialogButtonBox::Apply)){apply->setText("应用");apply->setIcon(QIcon());}
        if(auto *restore=box->button(QDialogButtonBox::RestoreDefaults)){restore->setText("恢复默认");restore->setIcon(QIcon());}
    }
    if(form->property("settingsManagesDraft").toBool())return;
    auto dirty=[form]{form->setProperty("settingsDirty",true);};
    for(auto *c:form->findChildren<QCheckBox *>())connect(c,&QCheckBox::toggled,form,dirty);
    for(auto *c:form->findChildren<QComboBox *>())connect(c,QOverload<int>::of(&QComboBox::currentIndexChanged),form,dirty);
    for(auto *c:form->findChildren<QSpinBox *>())connect(c,QOverload<int>::of(&QSpinBox::valueChanged),form,dirty);
    for(auto *c:form->findChildren<QSlider *>())connect(c,&QSlider::valueChanged,form,dirty);
    for(auto *c:form->findChildren<QLineEdit *>())connect(c,&QLineEdit::textEdited,form,dirty);
    for(auto *c:form->findChildren<QListWidget *>()){
        connect(c,&QListWidget::itemChanged,form,dirty);
        connect(c->model(),&QAbstractItemModel::rowsInserted,form,dirty);
        connect(c->model(),&QAbstractItemModel::rowsRemoved,form,dirty);
    }
    for(auto *b:form->findChildren<QPushButton *>())if(!qobject_cast<QDialogButtonBox *>(b->parentWidget()) && !b->property("settingsImmediate").toBool())
        connect(b,&QPushButton::clicked,form,dirty);
}
void FencesSettingsWindow::refreshStates() {
    if(!m_canvas)return;
    if(auto *toggle=findChild<QPushButton *>("desktopVisibilityButton"))
        toggle->setText(m_canvas->fencesDesktopVisible()?"切换到系统桌面":"切换回 Fences 桌面");
    if(auto *edit=findChild<QPushButton *>("layoutEditButton"))edit->setText(m_canvas->globalEditMode()?"退出布局编辑":"进入布局编辑");
    for(auto *check:findChildren<QCheckBox *>()){
        const QString id=check->property("visibilityKey").toString();
        const QString startup=check->property("startupKey").toString();
        QSignalBlocker block(check);
        if(!id.isEmpty()) {
            bool on=id=="smart"?m_canvas->smartSpaceVisible():id=="monitor"?m_canvas->systemMonitorVisible():
                id=="clock"?m_canvas->clockWidgetVisible():id=="activity"?m_canvas->activityWidgetVisible():
                id=="music"?m_canvas->musicWidgetVisible():m_canvas->calendarWidgetVisible();check->setChecked(on);
        } else if(!startup.isEmpty()) {
            const bool on=startup=="smart"?SmartSpaceWidget::autoStartEnabled():startup=="monitor"?SystemMonitor::autoStartEnabled():LiquidDesklet::autoStartEnabled(startup);
            check->setChecked(on);check->setText(on?"随 Fences 启动：已开启":"随 Fences 启动：未开启");
        } else if(check->objectName()=="fenceLiquidGlass")check->setChecked(m_canvas->fenceLiquidGlassEnabled());
        else if(check->objectName()=="reduceMotion")check->setChecked(m_canvas->reduceMotion());
        else if(check->objectName()=="wallpaperMagnet")check->setChecked(m_canvas->wallpaperMagnetEnabled());
        else if(check->objectName()=="fenceLocked") {
            if(auto *f=m_canvas->fenceById(check->property("fenceId").toString()))check->setChecked(f->locked());
        }
    }
}
QWidget *FencesSettingsWindow::buildPage(const QString &id) {
    auto *scroll=new QScrollArea(this);scroll->setObjectName("settingsScroll");scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);scroll->viewport()->setObjectName("settingsViewport");
    scroll->viewport()->setAutoFillBackground(false);
    auto *page=new QWidget;page->setObjectName("settingsPage");page->setAutoFillBackground(false);scroll->setWidget(page);
    auto *layout=new QVBoxLayout(page);layout->setContentsMargins(4,4,8,4);layout->setSpacing(12);
    QString name;for(const auto &p:pages)if(id==p.id)name=QString::fromUtf8(p.name).trimmed();
    auto *heading=new QLabel(name,page);heading->setObjectName("pageHeading");layout->addWidget(heading);
    if(id.startsWith("fence:")) {
        if(auto *f=m_canvas->fenceById(id.mid(6))) {
            heading->setText(f->title());
            QPointer<FenceWidget> fence=f;
            auto *c=card(layout,"分区外观");
            auto *titleEdit=new QLineEdit(f->title(),page);titleEdit->setObjectName("fenceTitleEdit");c->addWidget(titleEdit);
            connect(f,&FenceWidget::titleChanged,page,[this,id,heading,titleEdit](const QString &title){
                heading->setText(title);titleEdit->setText(title);
                for(int i=0;i<m_navigation->count();++i)if(m_navigation->item(i)->data(Qt::UserRole).toString()==id)
                    m_navigation->item(i)->setText("    "+title);
            });
            button(c,"修改分区名称",[fence,titleEdit]{if(fence && !titleEdit->text().trimmed().isEmpty())fence->setTitle(titleEdit->text().trimmed());});
            button(c,"分区颜色…",[this,fence]{if(!fence)return;QColor selected=LiquidDialog::getColor(fence->fenceColor(),this,"分区颜色",QColorDialog::ShowAlphaChannel);if(fence && selected.isValid()){fence->setFenceColor(selected);emit fence->geometryChanged();}});
            auto *alpha=new QSpinBox(page);alpha->setRange(0,100);alpha->setSuffix("%  不透明度");alpha->setValue(qRound(f->fenceColor().alphaF()*100));c->addWidget(alpha);
            connect(alpha,QOverload<int>::of(&QSpinBox::valueChanged),page,[fence](int n){if(fence){QColor color=fence->fenceColor();color.setAlphaF(n/100.);fence->setFenceColor(color);emit fence->geometryChanged();}});
            alpha->setObjectName("fenceOpacity");
            connect(f,&FenceWidget::fenceColorChanged,alpha,[alpha](const QColor &color){
                QSignalBlocker block(alpha);alpha->setValue(qRound(color.alphaF()*100));
            });
            auto *locked=new QCheckBox("锁定分区位置和内容",page);
            locked->setObjectName("fenceLocked");locked->setChecked(f->locked());c->addWidget(locked);
            locked->setProperty("fenceId",f->fenceId());
            connect(locked,&QCheckBox::toggled,page,[fence](bool on){if(fence){fence->setLocked(on);emit fence->geometryChanged();}});
            c=card(layout,"分区标题图标");
            auto *iconPreview=new QLabel(page);iconPreview->setObjectName("fenceTitleIconPreview");c->addWidget(iconPreview);
            auto *iconStatus=new QLabel(page);iconStatus->setObjectName("fenceTitleIconStatus");iconStatus->setWordWrap(true);iconStatus->setTextFormat(Qt::PlainText);c->addWidget(iconStatus);
            button(c,"系统与默认图标…",[this,fence]{
                if(!fence)return;
                const auto choice=FenceIconPicker::choose(this,fence->titleIconThemeName(),fence->titleIconPath());
                if(!fence || !choice.accepted)return;
                if(!choice.themeName.isEmpty())fence->setTitleIconThemeName(choice.themeName);
                else fence->setTitleIconPath(choice.path);
            },"fenceChooseLibraryIcon");
            button(c,"从图片文件选择…",[this,fence]{if(!fence)return;QString file=LiquidDialog::getOpenImageName(this,"选择分区图标",QString(),"图片 (*.png *.jpg *.jpeg *.svg *.ico);;所有文件 (*)");if(fence && !file.isEmpty())fence->setTitleIconPath(file);},"fenceChooseFileIcon");
            auto *clearIcon=button(c,"清除分区图标…",[this,fence]{if(fence)fence->confirmClearTitleIcon(this);},"fenceClearTitleIcon");
            const auto refreshIcon=[fence,iconPreview,iconStatus,clearIcon]{
                if(!fence)return;
                const auto name=fence->titleIconThemeName();
                const auto path=fence->titleIconPath();
                QIcon icon;
                if(!name.isEmpty()) {
                    icon=QIcon::fromTheme(name,QIcon(":/fence-icons/orbit.svg"));
                    iconStatus->setText("系统图标："+name+"（随系统图标主题变化）");
                } else if(!path.isEmpty()) {
                    icon=QIcon(path);
                    const auto label=FenceIconPicker::defaultIconLabel(path);
                    iconStatus->setText(!label.isEmpty()?"内置默认："+label:"图片文件："+QFileInfo(path).fileName());
                } else iconStatus->setText("未设置标题图标（显示默认小框）");
                iconPreview->setPixmap(icon.pixmap(32,32));
                clearIcon->setEnabled(fence->hasTitleIcon());
            };
            connect(f,&FenceWidget::titleIconChanged,page,refreshIcon);refreshIcon();
            hint(c,"系统图标库可搜索当前主题及其继承图标；内置默认图标无需安装额外主题。");
            attachForm(card(layout,"内部图标字体"),f->createFontSettingsPage(page));
            attachForm(card(layout,"分区标题字体"),f->createTitleFontSettingsPage(page));
            connect(f,&QObject::destroyed,scroll,[scroll]{scroll->deleteLater();});
        } else hint(layout,"此分区已移除。");
    } else if(id=="appearance") {
        auto *c=card(layout,"分区液态玻璃");
        auto *glass=new QCheckBox("启用分区液态玻璃",page);glass->setObjectName("fenceLiquidGlass");c->addWidget(glass);
        connect(glass,&QCheckBox::toggled,m_canvas,&DesktopCanvas::setFenceLiquidGlassEnabled);
        hint(c,"分区采用壁纸折射与边缘高光。此开关只控制分区，小组件保留各自皮肤。");
        auto *scrim=new QComboBox(page);scrim->setObjectName("glassScrim");
        scrim->addItems({"易读","均衡","通透","轻微"});scrim->setCurrentIndex(m_canvas->glassScrim());
        scrim->setProperty("settingsImmediate",true);c->addWidget(scrim);
        connect(scrim,QOverload<int>::of(&QComboBox::currentIndexChanged),m_canvas,&DesktopCanvas::setGlassScrim);
        hint(c,"玻璃按背后壁纸的局部亮度自动压暗，保证文字可读。档位越靠后越通透。");
        auto *motion=new QCheckBox("减少动态效果",page);motion->setObjectName("reduceMotion");
        motion->setChecked(m_canvas->reduceMotion());motion->setProperty("settingsImmediate",true);c->addWidget(motion);
        connect(motion,&QCheckBox::toggled,m_canvas,&DesktopCanvas::setReduceMotion);
        hint(c,"关闭弹出、折叠和排序动画，状态立即切换。");
        button(c,"刷新壁纸与液态材质",[this]{m_canvas->refreshAll();},"refreshMaterialButton");
    } else if(id=="wallpaper") {
        attachForm(card(layout,"Fences 壁纸"),m_canvas->createWallpaperSettingsPage(page));
        auto *c=card(layout,"主题配色");
        button(c,"从当前壁纸取色",[this]{if(!m_canvas->applyWallpaperThemeToFences())LiquidDialog::information(this,"壁纸取色","当前壁纸没有可提取的明显颜色。");},"wallpaperThemeButton");
        auto *preset=new QComboBox(page);preset->addItems({"海湾蓝","樱花粉","松石绿","暮色紫","石墨灰"});c->addWidget(preset);
        button(c,"应用所选配色",[this,preset]{const QColor colors[]={QColor("#2f80ed"),QColor("#ff7aa2"),QColor("#14b8a6"),QColor("#8b5cf6"),QColor("#202124")};QColor color=colors[preset->currentIndex()];color.setAlpha(90);m_canvas->applyThemeToFences(color,preset->currentIndex()==1?QColor("#202124"):QColor(Qt::white));});
        button(c,"应用外部主题文件",[this]{if(m_canvas->loadExternalTheme())m_canvas->applyExternalThemeToFences();else LiquidDialog::information(this,"外部主题","没有找到外部主题文件。可使用壁纸取色或内置配色。");});
        button(c,"打开系统壁纸设置",[this]{m_canvas->openSystemWallpaper();});
    } else if(id=="icons") {
        attachForm(card(layout,"图标底座"),m_canvas->createIconSettingsPage(page));
        attachForm(card(layout,"桌面字体"),m_canvas->createFontSettingsPage(page));
    } else if(id=="sync") {
        attachForm(card(layout,"新文件收纳"),m_canvas->createSyncSettingsPage(page));
    } else if(id=="layout") {
        auto *c=card(layout,"编辑与磁吸");
        button(c,"切换布局编辑状态",[this]{m_canvas->setGlobalEditMode(!m_canvas->globalEditMode());refreshStates();},"layoutEditButton");
        auto *magnet=new QCheckBox("壁纸磁性画线",page);magnet->setObjectName("wallpaperMagnet");c->addWidget(magnet);
        connect(magnet,&QCheckBox::toggled,m_canvas,&DesktopCanvas::setWallpaperMagnetEnabled);
        hint(c,"壁纸磁性画线沿明暗边界调整分区轮廓；组件之间的矩形边缘磁吸在布局编辑时自动工作。");
        attachForm(card(layout,"桌面网格"),m_canvas->createGridSettingsPage(page));
        c=card(layout,"分区");
        for(auto *f:m_canvas->m_fences){QPointer<FenceWidget> fence=f;auto *locked=new QCheckBox(f->title()+" · 锁定",page);locked->setChecked(f->locked());c->addWidget(locked);
            connect(locked,&QCheckBox::toggled,page,[fence](bool on){if(fence){fence->setLocked(on);emit fence->geometryChanged();}});
            button(c,f->title()+" · 分区设置",[this,fence]{if(fence)openPage("fence:"+fence->fenceId());});}
        c=card(layout,"布局备份");button(c,"导出布局…",[this]{m_canvas->exportLayout();});button(c,"导入布局…",[this]{m_canvas->importLayout();});
        button(c,"重置布局…",[this]{m_canvas->resetLayoutSettings();});
    } else if(id=="widgets") {
        hint(layout,"当前显示与随 Fences 启动分别设置。打开专项设置不会自动启动隐藏组件。");
        for(const QString key:{"smart","monitor","clock","activity","music","calendar"}) {
            QString title;for(const auto &p:pages)if(key==p.id)title=QString::fromUtf8(p.name).trimmed();
            auto *c=card(layout,title);auto *visible=new QCheckBox("当前显示",page);visible->setProperty("visibilityKey",key);c->addWidget(visible);
            connect(visible,&QCheckBox::toggled,page,[this,key](bool on){
                if(key=="smart")m_canvas->setSmartSpaceVisible(on);else if(key=="monitor")m_canvas->setSystemMonitorVisible(on);
                else if(key=="clock")m_canvas->setClockWidgetVisible(on);else if(key=="activity")m_canvas->setActivityWidgetVisible(on);
                else if(key=="music")m_canvas->setMusicWidgetVisible(on);else m_canvas->setCalendarWidgetVisible(on);
            });
            auto *startup=new QCheckBox(page);startup->setProperty("startupKey",key);c->addWidget(startup);
            connect(startup,&QCheckBox::toggled,page,[this,key](bool on){
                if(key=="smart" && m_canvas->m_smartSpace)m_canvas->m_smartSpace->setAutoStart(on);
                else if(key=="monitor" && m_canvas->m_monitor)m_canvas->m_monitor->setAutoStart(on);
                else if(key=="smart" || key=="monitor"){QSettings s;s.setValue((key=="smart"?"smartSpace/":"systemMonitor/")+QString("autoStart"),on);s.sync();}
                else LiquidDesklet::setAutoStart(key,on);refreshStates();
            });
            button(c,"专项设置",[this,key]{openPage(key);});
        }
    } else if(id=="smart" || id=="monitor") {
        auto *c=card(layout,"专项配置");QWidget *editor=nullptr;
        if(id=="smart" && m_canvas->m_smartSpace)editor=m_canvas->m_smartSpace->createSettingsPage(page);
        if(id=="monitor" && m_canvas->m_monitor)editor=m_canvas->m_monitor->createSettingsPage(page);
        if(editor) {
            editor->setStyleSheet(QString());
            attachForm(c,editor);
            // A component may be removed via the desktop shortcut while its
            // form is open. Remove that page together with its model.
            connect(editor,&QObject::destroyed,scroll,[scroll]{scroll->deleteLater();});
        } else {
            hint(c,"组件当前未启用。启用后可在此配置；仅查看此页不会启动采样或索引。");
            button(c,"启用组件并配置",[this,id,scroll]{if(id=="smart")m_canvas->setSmartSpaceVisible(true);else m_canvas->setSystemMonitorVisible(true);m_pages.remove(id);scroll->deleteLater();openPage(id);});
        }
    } else if(id=="calendar") {
        auto *c=card(layout,"待办与节假日");
        auto *collapsed=new QCheckBox("折叠待办列表",page);collapsed->setChecked(QSettings().value("desklets/calendar/agendaCollapsed",false).toBool());c->addWidget(collapsed);
        connect(collapsed,&QCheckBox::toggled,page,[this](bool on){if(m_canvas->m_calendarWidget)m_canvas->m_calendarWidget->setAgendaCollapsed(on,true);else QSettings().setValue("desklets/calendar/agendaCollapsed",on);});
        hint(c,"农历和已发布的中国节假日按本地数据展示，系统待办只读。同步需要启用日历组件。");
        auto *b=button(c,"同步最新农历和节假日",[this]{if(m_canvas->m_calendarWidget)m_canvas->m_calendarWidget->syncCalendarData();});b->setEnabled(m_canvas->calendarWidgetVisible());
    } else if(id=="activity") {
        auto *c=card(layout,"应用时长记录");auto *recording=new QCheckBox("记录前台应用时间",page);c->addWidget(recording);
        auto *recorder=m_canvas->findChild<ActivityRecorder *>();recording->setChecked(recorder?recorder->isRecording():QSettings().value("desklets/activity/recording",true).toBool());
        connect(recording,&QCheckBox::toggled,page,[this](bool on){if(auto *r=m_canvas->findChild<ActivityRecorder *>())r->setRecording(on);else QSettings().setValue("desklets/activity/recording",on);});
        hint(c,"只统计前台应用停留时间，锁屏、暂停及没有前台窗口时不累计应用时间。");
    } else if(id=="music") {
        auto *c=card(layout,"音乐客户端与自动接入");
        auto *editor=new MusicClientsSettings(page);
        connect(editor,&MusicClientsSettings::configurationApplied,page,[this]{
            if(auto *music=m_canvas->findChild<MusicDesklet *>())music->player()->reloadConfiguration();
        });
        attachForm(c,editor);
    } else if(id=="clock") {
        hint(card(layout,"时钟与倒计时"),"时钟跟随系统时间。倒计时的时长、开始、暂停和取消保留在组件本体中；显示与自启动在“桌面小组件”统一设置。");
    } else if(id=="help") {
        auto *c=card(layout,"使用说明与关于");auto *text=new QTextBrowser(page);text->setOpenExternalLinks(true);text->setMinimumHeight(380);
        text->setHtml(m_canvas->settingsHelpHtml()+m_canvas->settingsAboutHtml());c->addWidget(text);
        c=card(layout,"运行管理");button(c,"切换到系统桌面",[this]{
            if(m_canvas->fencesDesktopVisible())m_canvas->hideFences();else m_canvas->showAndActivate();
            refreshStates();
        },"desktopVisibilityButton");
        button(c,"退出 Fences",[this]{m_canvas->quitApp();});
    }
    SettingsComboPopup::install(page,m_canvas);
    layout->addStretch();return scroll;
}
