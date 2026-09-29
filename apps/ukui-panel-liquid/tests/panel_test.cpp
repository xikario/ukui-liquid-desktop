#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QMenu>
#include <QTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <QDebug>
#include <QLabel>
#include <QDir>
#include <QDialog>
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
    QApplication app(argc,argv);UKUIPanel panel;panel.setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);panel.setAttribute(Qt::WA_TranslucentBackground);panel.resize(780,60);
    QLabel label("原有任务栏内容",&panel);label.move(60,20);panel.show();
    QTimer::singleShot(150,&app,[&]{
        check(panel.property("liquidPanelAttached").toBool(),"Qt style plugin attaches without panel source ABI");
        settle();
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
        auto *follow=dialogs.first()->findChild<QCheckBox *>("liquidPanelFollowWallpaper");
        check(follow && follow->isChecked(),"wallpaper adaptation defaults on");
        auto before=panel.grab().toImage();
        replaceWallpaper(QColor("#46b286"));settle();
        check(panel.grab().toImage()!=before,"atomic wallpaper change updates material automatically");
        follow->setChecked(false);app.processEvents();saved.sync();
        check(!saved.value("appearance/followWallpaper",true).toBool(),"adaptation switch persists");
        before=panel.grab().toImage();replaceWallpaper(QColor("#516dbb"));settle();
        check(panel.grab().toImage()==before,"disabled adaptation keeps cached wallpaper");
        follow->setChecked(true);app.processEvents();
        check(panel.grab().toImage()!=before,"reenabling adaptation immediately loads latest wallpaper");
        auto sliders=dialogs.first()->findChildren<QSlider *>();
        check(sliders.size()==5,"shade radius highlight refraction and chroma controls exist");
        sliders.first()->setValue(72);app.processEvents();saved.sync();
        check(qAbs(saved.value("appearance/opacity").toDouble()-.72)<.001,"slider changes persist immediately");
        sliders.last()->setValue(35);app.processEvents();saved.sync();
        check(qAbs(saved.value("appearance/chroma").toDouble()-.35)<.001,"chroma setting persists immediately");
        dialogs.first()->close();
        panel.resize(860,60);settle();
        check(!panel.mask().contains(QPoint(859,0)) && panel.mask().contains(QPoint(858,30)),"resize retains rounded ends");
        validatePhysicalShape();
        panel.grab().save("panel-test.png");app.quit();
    });
    return app.exec();
}
#include "panel_test.moc"
