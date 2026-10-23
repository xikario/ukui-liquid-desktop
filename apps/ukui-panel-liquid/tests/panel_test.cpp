#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QScreen>
#include <QMenu>
#include <QTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <QDebug>
#include <QLabel>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QAbstractButton>
#include <QPushButton>
#include <QPointer>
#include <QSlider>
#include <QCheckBox>
#include <QTabWidget>
#include <QSpinBox>
#include <QSaveFile>
#include <QEventLoop>
#include <QProcess>
#include <QPainterPath>
#include <cstdlib>
#include <QLibrary>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef Bool
class UKUIPanel : public QWidget {
    Q_OBJECT
public:
    enum Alignment { AlignLeft=-1, AlignCenter=0, AlignRight=1 };
    using QWidget::QWidget;
    int layoutCalls=0;
    void paintEvent(QPaintEvent *) override {QPainter p(this);p.fillRect(rect(),QColor("#ad2144"));}
public slots:
    void setAlignment(UKUIPanel::Alignment alignment,bool save) {
        m_alignment=alignment;Q_UNUSED(save);
    }
    void setLength(int length,bool percent,bool save) {
        ++layoutCalls;
        const QRect area=QApplication::primaryScreen()->geometry();
        resize(percent?area.width()*length/100:length,height());
        move(area.left()+(area.width()-width())/2,area.bottom()-height()+1);
        if(save) {
            QSettings s(qEnvironmentVariable("XDG_CONFIG_HOME")+"/ukui/panel.conf",QSettings::IniFormat);
            s.setValue("panel1/alignment",int(m_alignment));
            s.setValue("panel1/width",length);s.setValue("panel1/width-percent",percent);s.sync();
        }
    }
private:
    Alignment m_alignment=AlignCenter;
};
class PointerPaintProbe : public QObject {
public:
    QWidget *panel=nullptr;
    int paints=0;
    bool eventFilter(QObject *object,QEvent *event) override {
        if(object==panel && event->type()==QEvent::Paint)++paints;
        return false;
    }
};
class NativeMenuShowLayout : public QObject {
public:
    QMenu *menu=nullptr;
    bool applied=false;
    bool eventFilter(QObject *object,QEvent *event) override {
        if(object==menu && event->type()==QEvent::Show && !applied) {
            applied=true;
            // Model a native/OEM layout adjustment while Show is being
            // delivered. Installing after polishing puts this filter before
            // the skin, so Resize reenters it before its Show initialization.
            menu->resize(menu->width()+30,menu->height()+10);
        }
        return false;
    }
};
static void check(bool ok,const char *s){if(!ok){qCritical()<<s;std::exit(1);}qInfo()<<"PASS:"<<s;}
int main(int argc,char **argv) {
    QTemporaryDir dir;qputenv("XDG_CONFIG_HOME",dir.path().toUtf8());
    qputenv("XDG_DATA_HOME",(dir.path()+"/data").toUtf8());
    const QString wallpaper=dir.path()+"/wallpaper.png";
    auto replaceWallpaper=[&](QColor color){
        QImage image(100,100,QImage::Format_RGB32);image.fill(color);
        QSaveFile file(wallpaper);check(file.open(QIODevice::WriteOnly),"temporary wallpaper opens");
        check(image.save(&file,"PNG") && file.commit(),"wallpaper atomically replaced");
    };
    replaceWallpaper(QColor("#bd6542"));qputenv("UKUI_LIQUID_WALLPAPER",wallpaper.toUtf8());
    auto settle=[] {QEventLoop loop;QTimer::singleShot(700,&loop,&QEventLoop::quit);loop.exec();};
    QApplication app(argc,argv);UKUIPanel panel;panel.setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);panel.setAttribute(Qt::WA_TranslucentBackground);panel.resize(780,60);
    panel.setObjectName("UKUIPanel panel1");
    {
        QSettings native(dir.path()+"/ukui/panel.conf",QSettings::IniFormat);
        native.setValue("panel1/width",68);native.setValue("panel1/width-percent",true);
        native.setValue("panel1/alignment",0);native.setValue("panel1/position","Bottom");
        native.setValue("taskbar/apps/1/desktop","preserve.desktop");
    }
    QLabel label("原有任务栏内容",&panel);label.move(60,20);panel.show();
    QTimer::singleShot(150,&app,[&]{
        check(panel.property("liquidPanelAttached").toBool(),"Qt style plugin attaches without panel source ABI");
        settle();
        {
            // Install after the style: this application filter observes paint
            // events before the plugin consumes them.
            PointerPaintProbe probe;probe.panel=&panel;app.installEventFilter(&probe);
            auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
            auto motion=[&](QPoint local){
                const QPoint global=panel.mapToGlobal(local);QCursor::setPos(global);
                QMouseEvent event(QEvent::MouseMove,label.mapFromGlobal(global),global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(&label,&event);
            };
            motion(QPoint(100,30));wait(100);probe.paints=0;
            for(int i=0;i<50;++i){motion(QPoint(100+i*9,30));wait(4);}
            wait(100);qInfo()<<"pointer frame paints"<<probe.paints;
            check(probe.paints>0 && probe.paints<=12,"child pointer motion merges old/new damage once per 32ms frame");
            if(QGuiApplication::platformName()=="xcb") {
                const QImage partial=QApplication::primaryScreen()->grabWindow(panel.winId()).toImage();
                panel.update();wait(80);
                const QImage full=QApplication::primaryScreen()->grabWindow(panel.winId()).toImage();
                int delta=0;
                for(int y=0;y<full.height();++y)for(int x=0;x<full.width();++x){
                    const QRgb a=partial.pixel(x,y),b=full.pixel(x,y);
                    delta=qMax(delta,qMax(qAbs(qRed(a)-qRed(b)),qMax(qAbs(qGreen(a)-qGreen(b)),qAbs(qBlue(a)-qBlue(b)))));
                }
                check(partial.size()==full.size() && delta<=2,"native panel partial/full pixels retain exact rim with fractional AA tolerance");
            }
            wait(100);probe.paints=0;wait(150);check(probe.paints==0,"stationary panel stops pointer repainting");
            motion(QPoint(-50,-50));wait(100);probe.paints=0;wait(150);
            check(probe.paints==0,"leaving panel clears highlight then stops repainting");
            app.removeEventFilter(&probe);
        }
        auto on=panel.grab().toImage();
        check(on.pixelColor(400,30)!=QColor("#ad2144"),"panel background replaced");
        check(label.isVisible(),"original children remain visible");
        {
            QImage reference=on;
            const qreal dpr=panel.devicePixelRatioF();
            QPainterPath curve;
            curve.addRoundedRect(QRectF(QPointF(),QSizeF(reference.size())),16*dpr,16*dpr);
            if(qEnvironmentVariableIsSet("UKUI_LIQUID_GLASS_NO_GL")) {
                reference=QImage(on.size(),QImage::Format_ARGB32_Premultiplied);
                reference.fill(Qt::transparent);
                QPainter painter(&reference);painter.setRenderHint(QPainter::Antialiasing);painter.fillPath(curve,Qt::white);
            }
            // grab() bypasses QWidget's on-screen mask; inspect the actual X11
            // backing pixels as well so an integer clip cannot hide in the test.
            Display *display=QGuiApplication::platformName()=="xcb"?XOpenDisplay(nullptr):nullptr;
            panel.update();settle();
            XImage *native=display?XGetImage(display,panel.winId(),0,0,on.width(),on.height(),AllPlanes,ZPixmap):nullptr;
            if(display)check(native!=nullptr,"actual X11 window pixels are readable");
            int clipped=0,partial=0;
            for(int y=0;y<on.height();++y)for(int x=0;x<on.width();++x) {
                if(x>=20*dpr && x<on.width()-20*dpr)continue;
                if(y>=20*dpr && y<on.height()-20*dpr)continue;
                const int expected=qAlpha(reference.pixel(x,y));
                if(expected>0 && expected<255)++partial;
                const int actual=native?int((XGetPixel(native,x,y)>>24)&255):qAlpha(on.pixel(x,y));
                // XGetImage outside ShapeBounding is undefined. Transparent
                // corners are checked separately through the native region.
                if(expected>0 && qAbs(actual-expected)>1)++clipped;

            }
            if(native)XDestroyImage(native);
            if(display)XCloseDisplay(display);
            qInfo()<<"corner coverage mismatches:"<<clipped<<"partial pixels:"<<partial;
            check(partial>0 && clipped==0,"all four corners retain exact antialiased coverage through QWidget clipping");
        }

        check(!panel.mask().contains(QPoint(0,0)) && panel.mask().contains(panel.rect().center()),"rounded native silhouette clips corners and retains body");
        check(panel.rect().contains(panel.mask().boundingRect()),"native outline stays inside panel bounds");
        const bool x11=QGuiApplication::platformName()=="xcb";
        const QString window=QString::number(panel.winId());
        const QString blur="_KDE_NET_WM_BLUR_BEHIND_REGION";
        auto blurProperty=[&]{QProcess p;p.start("xprop",{"-id",window,blur});p.waitForFinished();return p.readAllStandardOutput();};
        auto nativeRegion=[&] {
            using GetRectangles=XRectangle *(*)(Display *,Window,int,int *,int *);
            static QLibrary lib(QStringLiteral("libXext.so.6"));
            auto get=reinterpret_cast<GetRectangles>(lib.resolve("XShapeGetRectangles"));
            check(get!=nullptr,"X11 shape query available");
            auto *display=XOpenDisplay(nullptr);check(display!=nullptr,"X11 test connection available");
            int count=0,ordering=0;auto *rectangles=get(display,panel.winId(),0,&count,&ordering);
            QRegion region;for(int i=0;i<count;++i)region+=QRect(rectangles[i].x,rectangles[i].y,rectangles[i].width,rectangles[i].height);
            if(rectangles)XFree(rectangles);XCloseDisplay(display);return region;
        };
        auto validatePhysicalShape=[&] {
            if(!x11)return;
            const QRegion region=nativeRegion();const qreal dpr=panel.devicePixelRatioF();
            const QRect physical(0,0,qRound(panel.width()*dpr),qRound(panel.height()*dpr));
            check(physical.contains(region.boundingRect()),"X11 physical shape does not expand outside the window");
            check(region.contains(physical.center()) && !region.contains(QPoint(0,physical.height()-1))
                && !region.contains(QPoint(physical.width()-1,physical.height()-1)),"both lower physical corners are cut out");
            check(panel.property("liquidNativeOutlineKey").toLongLong()!=0,"X11 silhouette derives from rendered alpha coverage");
        };
        validatePhysicalShape();
        if(x11) {
            QProcess::execute("xprop",{"-id",window,"-f",blur,"32c","-set",blur,"0, 0, 780, 60"});
            panel.resize(790,60);settle();
            check(!blurProperty().contains(" = "),"geometry change removes OEM rectangular blur");
        }
        {
            // The native panel creates a fresh parented QMenu and execs it at
            // the pointer. A Show-time layout adjustment can deliver Resize
            // while visible but before the skin initializes its screen bounds.
            // Exercise that ordering through the installed plugin, rather
            // than LiquidPopup::execAt(), which pre-polishes its menu.
            const QRect screenArea=QApplication::primaryScreen()->availableGeometry();
            const QPoint originalPanelPosition=panel.pos();
            panel.move(screenArea.left()+qMax(0,(screenArea.width()-panel.width())/2),
                       screenArea.bottom()-panel.height()+1);
            app.processEvents();
            const QPoint click=panel.mapToGlobal(QPoint(panel.width()/2,panel.height()/2));
            QCursor::setPos(click);
            QMenu nativeContextMenu(&panel);
            nativeContextMenu.addAction("设置任务栏");
            nativeContextMenu.addAction("显示“任务视图”按钮")->setCheckable(true);
            nativeContextMenu.addAction("显示桌面");
            nativeContextMenu.addSeparator();
            nativeContextMenu.addAction("系统监视器");
            nativeContextMenu.addMenu("调整大小")->addAction("默认大小");
            nativeContextMenu.addMenu("调整位置")->addAction("底部");
            nativeContextMenu.addAction("隐藏任务栏");
            nativeContextMenu.addAction("锁定任务栏")->setCheckable(true);
            nativeContextMenu.addAction("关于麒麟");
            nativeContextMenu.ensurePolished();
            NativeMenuShowLayout showLayout;showLayout.menu=&nativeContextMenu;
            nativeContextMenu.installEventFilter(&showLayout);
            QRect openingGeometry;
            QTimer::singleShot(0,&nativeContextMenu,[&]{
                openingGeometry=nativeContextMenu.geometry();
                qInfo()<<"native panel first context-menu geometry"<<openingGeometry<<"click"<<click;
                check(showLayout.applied,"native menu exercises a visible layout resize before its skin handles Show");
                check(nativeContextMenu.isVisible() && screenArea.contains(openingGeometry),
                      "first native panel context menu stays inside the clicked screen");
                check(qAbs(openingGeometry.bottom()-click.y())<=panel.height()+12
                          && openingGeometry.top()<click.y()
                          && openingGeometry.left()<=click.x() && openingGeometry.right()>=click.x(),
                      "first native panel exec keeps its menu above and near the taskbar click instead of the screen origin");
                // Lazy dbusmenu/package content must still be constrained once
                // Show has completed; the initialization guard cannot disable
                // genuine post-show resizing.
                nativeContextMenu.addAction("异步新增的较长任务栏菜单项目，用来扩大实际弹窗宽度");
                nativeContextMenu.addAction("异步新增项目二");
                nativeContextMenu.adjustSize();
                QTimer::singleShot(0,&nativeContextMenu,[&]{
                    const QRect grownGeometry=nativeContextMenu.geometry();
                    qInfo()<<"native panel grown context-menu geometry"<<grownGeometry;
                    // The small offscreen display can cap/relayout height;
                    // the longer label must still grow the real popup width.
                    check(grownGeometry.width()>openingGeometry.width(),
                          "native context menu actually widens after its first Show");
                    check(screenArea.contains(grownGeometry)
                              && grownGeometry.left()<=click.x() && grownGeometry.right()>=click.x(),
                          "post-show native menu growth remains clamped on the clicked screen near its opening point");
                    nativeContextMenu.close();
                });
            });
            nativeContextMenu.exec(click);
            panel.move(originalPanelPosition);
            app.processEvents();
        }
        QMenu menu(&panel);menu.addAction("原菜单项目");menu.popup(QPoint(100,110));app.processEvents();
        auto *toggle=menu.findChild<QAction *>("liquidPanelEnabledAction");
        check(toggle && toggle->isChecked(),"native menu exposes enabled switch");
        toggle->trigger();app.processEvents();
        check(!panel.property("liquidPanelEnabled").toBool(),"disable applies live");
        check(panel.mask().isEmpty(),"disable restores original native mask");
        if(x11)check(nativeRegion().contains(QPoint(0,0)),"disable restores rectangular X11 shape");
        if(x11)check(blurProperty().contains("0, 0, 780, 60"),"disable restores OEM blur property");
        check(panel.grab().toImage().pixelColor(400,30)==QColor("#ad2144"),"disable restores original paintEvent");
        QSettings saved(dir.path()+"/ukui/liquid-panel.ini",QSettings::IniFormat);
        check(!saved.value("appearance/enabled",true).toBool(),"disabled preference persists");
        toggle->trigger();app.processEvents();
        check(panel.property("liquidPanelEnabled").toBool(),"reenable applies live");
        if(x11)check(!blurProperty().contains(" = "),"reenabling removes rectangular blur again");
        auto *settings=menu.findChild<QAction *>("liquidPanelSettingsAction");
        check(settings!=nullptr,"appearance settings action exists");settings->trigger();app.processEvents();
        auto dialogs=panel.findChildren<QDialog *>();
        check(!dialogs.isEmpty() && dialogs.first()->isVisible(),"appearance settings dialog opens");
        auto *appearanceDialog=dialogs.first();
        auto *settingsTabs=appearanceDialog->findChild<QTabWidget *>("liquidPanelSettingsTabs");
        check(settingsTabs && settingsTabs->count()==2 && settingsTabs->tabText(1)=="窗口动画"
                  && !appearanceDialog->findChild<QCheckBox *>("liquidWindowMagicLamp"),
              "appearance and desktop animations have separate tabs with lazy effects loading");
        settingsTabs->setCurrentIndex(1);app.processEvents();
        auto *effectDuration=appearanceDialog->findChild<QSpinBox *>("liquidWindowMagicDuration");
        check(effectDuration && effectDuration->property("liquidControlButtons").toBool(),
              "lazily created animation duration gets shared liquid spin arrows");
        settingsTabs->setCurrentIndex(0);app.processEvents();
        auto *centered=appearanceDialog->findChild<QCheckBox *>("liquidPanelCentered");
        auto *centeredLength=appearanceDialog->findChild<QSlider *>("liquidPanelCenteredLength");
        check(centered && centered->isEnabled() && centered->isChecked()
                  && centeredLength && centeredLength->value()==68,
              "layout controls read existing native centered configuration without changing it");
        check(panel.layoutCalls==0,"opening appearance settings does not write native layout");
        auto *trayColumns=appearanceDialog->findChild<QSpinBox *>("liquidTrayMaxColumns");
        check(trayColumns && trayColumns->minimum()==3 && trayColumns->maximum()==8 && trayColumns->value()==3,
              "hidden tray column control defaults to three with a three-to-eight range");
        trayColumns->setValue(8);app.processEvents();saved.sync();
        check(saved.value("tray/maxColumns").toInt()==8 && app.property("liquidTrayMaxColumns").toInt()==8,
              "hidden tray column setting saves immediately and notifies the live adapter");
        const QRect beforeLayout=panel.geometry();
        centered->click();
        QSettings native(dir.path()+"/ukui/panel.conf",QSettings::IniFormat);native.sync();
        check(!centeredLength->isEnabled() && native.value("panel1/width").toInt()==100
                  && panel.width()==QApplication::primaryScreen()->geometry().width(),
              "disabling centered layout uses native full edge length and persists it");
        centered->click();
        const int callsBeforeDrag=panel.layoutCalls;
        centeredLength->setSliderDown(true);centeredLength->setValue(55);centeredLength->setValue(72);
        check(panel.layoutCalls==callsBeforeDrag,"dragging centered length does not repeatedly relayout the dock");
        centeredLength->setSliderDown(false);native.sync();
        check(panel.layoutCalls==callsBeforeDrag+1 && native.value("panel1/width").toInt()==72
                  && native.value("panel1/alignment").toInt()==0
                  && native.value("panel1/width-percent").toBool()
                  && native.value("taskbar/apps/1/desktop").toString()=="preserve.desktop",
              "release applies and saves native centered length once without changing pinned apps");
        const QRect screenGeometry=QApplication::primaryScreen()->geometry();
        check(qAbs(panel.geometry().center().x()-screenGeometry.center().x())<=1,
              "native layout callback centers the actual window");
        centered->click();centered->click();native.sync();
        check(centeredLength->value()==72 && native.value("panel1/width").toInt()==72,
              "centering restores the chosen length after a full-width toggle");
        panel.setGeometry(beforeLayout);app.processEvents();
        auto *titlebar=appearanceDialog->findChild<QWidget *>("liquidDialogTitlebar");
        auto *titleClose=appearanceDialog->findChild<QAbstractButton *>("liquidDialogClose");
        auto *titleIcon=appearanceDialog->findChild<QLabel *>("liquidDialogIcon");
        check(!appearanceDialog->windowIcon().isNull() && titleIcon && titleIcon->pixmap()
                  && !titleIcon->pixmap()->isNull()
                  && appearanceDialog->windowRole()=="liquid-panel-settings",
              "panel settings expose an explicit icon and task-window role even when the OEM panel has no icon");
        check(appearanceDialog->windowFlags().testFlag(Qt::FramelessWindowHint)
                  && titlebar && titlebar->isVisible() && titleClose && titleClose->isVisible(),
              "real panel settings use the shared frameless titlebar and close control");
        check(appearanceDialog->windowType()==Qt::Window
                  && appearanceDialog->windowFlags().testFlag(Qt::WindowMinimizeButtonHint)
                  && !appearanceDialog->windowFlags().testFlag(Qt::WindowMaximizeButtonHint)
                  && appearanceDialog->findChild<QAbstractButton *>("liquidDialogMinimize")
                  && !appearanceDialog->findChild<QAbstractButton *>("liquidDialogMaximize"),
              "panel settings expose a task-managed window with minimize and close only");
        check(!appearanceDialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
              "panel settings stay in the ordinary application window layer");
        const QPoint settingsPosition=appearanceDialog->pos();
        const int settingsBuilds=appearanceDialog->property("liquidDialogMaterialBuilds").toInt();
        appearanceDialog->findChild<QAbstractButton *>("liquidDialogMinimize")->click();settle();
        check(appearanceDialog->isMinimized(),"real panel settings can be minimized to the task list");
        settings->trigger();settle();
        check(!appearanceDialog->isMinimized() && appearanceDialog->pos()==settingsPosition
                  && appearanceDialog->property("liquidDialogMaterialBuilds").toInt()==settingsBuilds,
              "settings action restores the existing dialog without capturing its mapped contents again");
        auto *form=qobject_cast<QFormLayout *>(appearanceDialog->layout());
        auto *introItem=form?form->itemAt(0,QFormLayout::SpanningRole):nullptr;
        auto *intro=introItem?qobject_cast<QLabel *>(introItem->widget()):nullptr;
        auto *footer=appearanceDialog->findChild<QDialogButtonBox *>();
        auto *footerClose=footer?footer->button(QDialogButtonBox::Close):nullptr;
        check(form && intro && footerClose,"panel settings retain their actual form and close footer");
        const QRect headerRect(titlebar->mapTo(appearanceDialog,QPoint()),titlebar->size());
        const QRect introRect(intro->mapTo(appearanceDialog,QPoint()),intro->size());
        const QRect footerRect(footerClose->mapTo(appearanceDialog,QPoint()),footerClose->size());
        check(headerRect.bottom()<introRect.top() && headerRect.bottom()<footerRect.top()
                  && appearanceDialog->rect().contains(introRect)
                  && appearanceDialog->rect().contains(footerRect),
              "panel form instructions and final close button remain below the custom header without clipping");
        auto *follow=dialogs.first()->findChild<QCheckBox *>("liquidPanelFollowWallpaper");
        check(follow && follow->isChecked(),"wallpaper adaptation defaults on");
        auto before=panel.grab().toImage();
        replaceWallpaper(QColor("#46b286"));settle();
        check(panel.grab().toImage()!=before,"atomic wallpaper change updates material automatically");
        follow->setChecked(false);settle();saved.sync();
        check(!saved.value("appearance/followWallpaper",true).toBool(),"adaptation switch persists");
        before=panel.grab().toImage();replaceWallpaper(QColor("#516dbb"));settle();
        check(panel.grab().toImage()==before,"disabled adaptation keeps cached wallpaper");
        follow->setChecked(true);settle();
        check(panel.property("liquidMaterialReady").toBool() && panel.grab().toImage()!=before,
              "reenabling adaptation asynchronously loads latest wallpaper");
        auto sliders=dialogs.first()->findChildren<QSlider *>();
        check(sliders.size()==8,"layout, material and see-through sliders exist");
        dialogs.first()->findChild<QSlider *>("liquidPanelShade")->setValue(72);settle();saved.sync();
        check(qAbs(saved.value("appearance/opacity").toDouble()-.72)<.001,"slider changes persist after the bounded debounce");
        dialogs.first()->findChild<QSlider *>("liquidPanelChroma")->setValue(35);settle();saved.sync();
        check(qAbs(saved.value("appearance/chroma").toDouble()-.35)<.001,"chroma setting persists immediately");
        auto *clarity=dialogs.first()->findChild<QSlider *>("liquidPanelClarity");
        auto *strength=dialogs.first()->findChild<QSlider *>("liquidPanelStrength");
        auto *through=dialogs.first()->findChild<QCheckBox *>("liquidPanelSeeThrough");
        auto *transparency=dialogs.first()->findChild<QSlider *>("liquidPanelTransparency");
        check(clarity && strength && through && transparency,"clarity, liquid strength and window see-through available");
        check(clarity->value()==0 && strength->value()==100 && !through->isChecked() && !transparency->isEnabled(),
              "new settings preserve existing defaults");
        clarity->setValue(60);strength->setValue(170);settle();saved.sync();
        check(qAbs(saved.value("appearance/clarity").toDouble()-.6)<.001 &&
              qAbs(saved.value("appearance/liquidStrength").toDouble()-1.7)<.001,"new material settings persist");
        const int writes=app.property("liquidPanelConfigWrites").toInt();
        const int batches=app.property("liquidPanelPreviewBatches").toInt();
        for(int v=20;v<=60;++v)clarity->setValue(v);
        check(app.property("liquidPanelConfigWrites").toInt()==writes,"slider burst does not synchronously write settings");
        settle();saved.sync();
        check(app.property("liquidPanelConfigWrites").toInt()==writes+1 && app.property("liquidPanelPreviewBatches").toInt()==batches+1,
              "41 slider values merge into one preview and one configuration write");
        check(qAbs(saved.value("appearance/clarity").toDouble()-.6)<.001,"merged burst saves its final value");
        const auto opaque=panel.grab().toImage();
        const auto buildsBeforeTransparency=panel.property("liquidMaterialBuilds").toInt();
        through->setChecked(true);for(int v=10;v<=50;++v)transparency->setValue(v);settle();saved.sync();
        check(panel.property("liquidMaterialBuilds").toInt()==buildsBeforeTransparency,"transparency preview reuses optical material");
        const auto transparent=panel.grab().toImage();
        const qreal dpr=panel.devicePixelRatioF();
        check(qAlpha(opaque.pixel(qRound(400*dpr),qRound(30*dpr)))==255 &&
              qAlpha(transparent.pixel(qRound(400*dpr),qRound(30*dpr)))>=125 &&
              qAlpha(transparent.pixel(qRound(400*dpr),qRound(30*dpr)))<=129,
              "see-through changes actual premultiplied surface alpha");
        check(label.isVisible() && qFuzzyCompare(label.windowOpacity(),1.),"see-through does not fade original child widgets");
        check(saved.value("appearance/seeThrough").toBool() && qAbs(saved.value("appearance/transparency").toDouble()-.5)<.001,
              "see-through enabled and amount persist");
        if(x11)check(!blurProperty().contains(" = "),"see-through never reintroduces native rectangular blur");
        through->setChecked(false);settle();
        check(panel.grab().toImage()==opaque,"disabling see-through restores exact material");
        const QPointer<QDialog> appearanceGuard(appearanceDialog);
        clarity->setValue(61);titleClose->click();settle();saved.sync();
        check(!appearanceGuard || !appearanceGuard->isVisible(),
              "shared titlebar close exits the real panel settings dialog");
        check(qAbs(saved.value("appearance/clarity").toDouble()-.61)<.001,
              "shared titlebar closing flushes the final pending settings value");
        settings->trigger();app.processEvents();
        auto *reopenedTrayColumns=panel.findChild<QSpinBox *>("liquidTrayMaxColumns");
        check(reopenedTrayColumns && reopenedTrayColumns->value()==8,"reopened settings remember the hidden tray column count");
        auto *reopenedCenter=panel.findChild<QCheckBox *>("liquidPanelCentered");
        auto *reopenedLength=panel.findChild<QSlider *>("liquidPanelCenteredLength");
        check(reopenedCenter && reopenedCenter->isChecked() && reopenedLength && reopenedLength->value()==72,
              "reopening layout settings reads the saved native length");
        panel.findChild<QAbstractButton *>("liquidDialogClose")->click();settle();
        panel.resize(860,60);settle();
        check(!panel.mask().contains(QPoint(859,0)) && panel.mask().contains(QPoint(858,30)),"resize retains rounded ends");
        validatePhysicalShape();
        panel.grab().save("panel-test.png");
        // Exercise real filesystem notifications, not only provider reload().
        qunsetenv("UKUI_LIQUID_WALLPAPER");
        QDir().mkpath(dir.path()+"/kyfences");QDir().mkpath(dir.path()+"/kylin");
        {
            QSaveFile layout(dir.path()+"/kyfences/layout.json");layout.open(QIODevice::WriteOnly);
            layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",wallpaper},{"wallpaperMode",0}}).toJson());
            check(layout.commit(),"Fences static layout committed");
        }
        auto makeVideo=[&](const QString &name){
            const QString path=dir.path()+name;QFile file(path);file.open(QIODevice::WriteOnly);file.write("video fixture");return path;
        };
        auto savePoster=[&](const QString &path,QColor color){
            const QFileInfo video(path);
            const QByteArray identity=video.canonicalFilePath().toUtf8()+'\n'+QByteArray::number(video.size())+'\n'
                +QByteArray::number(video.lastModified().toMSecsSinceEpoch());
            const auto folder=dir.path()+"/data/kylin/ukui-fences/video-previews/v2-"
                +QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex());
            QDir().mkpath(folder);QImage image(160,90,QImage::Format_RGB32);image.fill(color);
            QSaveFile file(folder+"/poster.png");check(file.open(QIODevice::WriteOnly),"poster file opened");
            check(image.save(&file,"PNG") && file.commit(),"video poster atomically committed");
        };
        QSettings fences(dir.path()+"/kylin/ukui-fences.ini",QSettings::IniFormat);
        const auto firstVideo=makeVideo("/first.mp4");
        fences.setValue("wallpaper/videoPath",firstVideo);fences.sync();settle();
        const QImage staticPixels=panel.grab().toImage();
        savePoster(firstVideo,QColor("#e37430"));settle();
        const QImage firstVideoPixels=panel.grab().toImage();
        check(panel.property("liquidMaterialReady").toBool() && firstVideoPixels!=staticPixels,
              "later creation of missing video cache automatically updates real panel pixels");
        const auto secondVideo=makeVideo("/second.mp4");savePoster(secondVideo,QColor("#32c482"));
        fences.setValue("wallpaper/videoPath",secondVideo);fences.sync();settle();
        check(panel.property("liquidMaterialReady").toBool() && panel.grab().toImage()!=firstVideoPixels,
              "video INI switch updates panel without any static layout change");
        fences.remove("wallpaper/videoPath");fences.sync();settle();
        check(panel.grab().toImage()==staticPixels,"disabling video automatically restores static panel material");
        app.quit();
    });
    return app.exec();
}
#include "panel_test.moc"
