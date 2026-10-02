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
#include <QSaveFile>
#include <QEventLoop>
#include <QProcess>
#include <QPainterPath>
#include <cstdlib>
#include <QLibrary>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
class UKUIPanel : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    void paintEvent(QPaintEvent *) override {QPainter p(this);p.fillRect(rect(),QColor("#ad2144"));}
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
static void check(bool ok,const char *s){if(!ok){qCritical()<<s;std::exit(1);}qInfo()<<"PASS:"<<s;}
int main(int argc,char **argv) {
    QTemporaryDir dir;qputenv("XDG_CONFIG_HOME",dir.path().toUtf8());
    const QString wallpaper=dir.path()+"/wallpaper.png";
    auto replaceWallpaper=[&](QColor color){
        QImage image(100,100,QImage::Format_RGB32);image.fill(color);
        QSaveFile file(wallpaper);check(file.open(QIODevice::WriteOnly),"temporary wallpaper opens");
        check(image.save(&file,"PNG") && file.commit(),"wallpaper atomically replaced");
    };
    replaceWallpaper(QColor("#bd6542"));qputenv("UKUI_LIQUID_WALLPAPER",wallpaper.toUtf8());
    auto settle=[] {QEventLoop loop;QTimer::singleShot(700,&loop,&QEventLoop::quit);loop.exec();};
    QApplication app(argc,argv);UKUIPanel panel;panel.setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);panel.setAttribute(Qt::WA_TranslucentBackground);panel.resize(780,60);
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
        check(sliders.size()==7,"material and see-through sliders exist");
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
        panel.resize(860,60);settle();
        check(!panel.mask().contains(QPoint(859,0)) && panel.mask().contains(QPoint(858,30)),"resize retains rounded ends");
        validatePhysicalShape();
        panel.grab().save("panel-test.png");app.quit();
    });
    return app.exec();
}
#include "panel_test.moc"
