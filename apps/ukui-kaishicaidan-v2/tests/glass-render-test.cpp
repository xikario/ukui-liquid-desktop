#include "NextKdeGlassView.h"
#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QDebug>
#include <QWidget>
#include <QTimer>
#include <QProcess>
#include <QKeyEvent>
#include <cmath>

static int failures=0;
static void check(bool ok, const char *name) {
    qInfo() << (ok ? "PASS" : "FAIL") << name;
    if (!ok) ++failures;
}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc,argv);
    if (app.arguments().contains("--backdrop-preview")) {
        // Temporary native test window, never changes wallpaper or settings.
        class Backdrop : public QWidget {
        public:
            void paintEvent(QPaintEvent *) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                if (QCoreApplication::arguments().contains("--bright")) {
                    p.fillRect(rect(),QColor("#faf8f2"));
                    p.scale(width()/1920.,height()/1200.);
                    p.setPen(QColor("#777777"));
                    p.setFont(QFont("sans-serif",12));
                    for(int y=30;y<1200;y+=30)
                        p.drawText(QPoint(300,y),"Bright background / documents / 0123456789 / readability test");
                    p.fillRect(QRect(870,0,180,1200),QColor("#d9e6ef"));
                    return;
                }
                QLinearGradient bg(0,0,width(),height());
                bg.setColorAt(0,QColor("#0c1423"));
                bg.setColorAt(0.5,QColor("#203b65"));
                bg.setColorAt(1,QColor("#0c1222"));
                p.fillRect(rect(),bg);
                p.scale(width()/1920.,height()/1200.);
                // High-contrast curved ribbons expose real lens displacement.
                for(int i=0;i<7;++i) {
                    QPainterPath ribbon;
                    ribbon.moveTo(-200,350+i*125);
                    ribbon.cubicTo(280,920+i*35,540,180+i*75,2100,420+i*100);
                    QLinearGradient color(100,300,1700,1000);
                    color.setColorAt(0,QColor("#38dec4"));
                    color.setColorAt(.42,QColor("#549eff"));
                    color.setColorAt(.72,QColor("#925de8"));
                    color.setColorAt(1,QColor("#efae89"));
                    p.setPen(QPen(QBrush(color),48-i*3));
                    p.drawPath(ribbon);
                }
                p.setPen(QColor(255,255,255,165));
                p.setFont(QFont("sans-serif",16));
                p.drawText(QPoint(60,60),"V2 optical test backdrop — temporary window / Esc closes");
                p.setPen(QColor(255,255,255,90));
                p.setFont(QFont("monospace",10));
                for(int y=150;y<1150;y+=28)
                    p.drawText(QPoint(330,y),"BACKGROUND TEXT 0123456789 / edge should bend, body should diffuse");
            }
            void keyPressEvent(QKeyEvent *e) override {
                if(e->key()==Qt::Key_Escape) close();
            }
        } backdrop;
        backdrop.setWindowTitle("V2 optical test backdrop");
        backdrop.showFullScreen();
        QTimer::singleShot(1200,[&]{
            QProcess::startDetached(QCoreApplication::applicationDirPath()+"/ukui-kaishicaidan-v2",{"--show"});
        });
        QTimer::singleShot(180000,&app,&QApplication::quit);
        return app.exec();
    }
    const QString out=argc>1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath();
    QDir().mkpath(out);
    NextKdeGlassView renderer;
    for (double dpr : {1.0,1.5,2.0}) {
        QImage src(QSize(qRound(400*dpr),qRound(300*dpr)),QImage::Format_RGB32);
        src.setDevicePixelRatio(dpr);
        src.fill(Qt::white);
        qunsetenv("KAISHICAIDAN_GLASS_NO_GL");
        renderer.setBackdrop(src);
        check(renderer.usedGpu(),"upstream shaders compile and render");
        check(renderer.image().size()==src.size() && renderer.image().devicePixelRatio()==dpr,"physical size / DPR preserved");
        check(qAlpha(renderer.image().pixel(0,0))<5,"rounded corner transparent");
        check(qRed(renderer.image().pixel(0,0))<5 &&
              qGreen(renderer.image().pixel(0,0))<5 &&
              qBlue(renderer.image().pixel(0,0))<5,"transparent corners have zero premultiplied RGB");
        QColor center=renderer.image().pixelColor(src.width()/2,src.height()/2);
        check(center.red()>135 && center.red()<155 && center.alpha()==255,"white body transmits light without returning to graphite");
        const QImage first=renderer.image();
        renderer.setBackdrop(src);
        check(renderer.image()==first,"cached program / FBO render is deterministic");
        check(renderer.gpuInitializationCount()==1,"one GL initialization across captures and DPR changes");
        check(renderer.luminanceAt(QRectF(100,100,60,20))>0.5,"bright label area is detected");
        check(renderer.luminanceAt(QRectF(-100,-100,10,10))==0,"out-of-bounds luminance sample safe");

        { QPainter p(&src);
          p.fillRect(QRect(0,0,400,150),QColor(255,50,20));
          p.fillRect(QRect(0,150,400,150),QColor(20,60,255));
          p.setPen(Qt::white);
          for(int y=0;y<300;y+=8) p.drawLine(0,y,400,y);
          p.setPen(Qt::black);
          p.drawText(QRect(10,20,380,260),Qt::TextWordWrap,
                     "Background text must disappear. Behind-glass typography should never compete with the menu.");
        }
        renderer.setBackdrop(src);
        QImage gpu=renderer.image();
        const QRectF buttonRect(70,65,78,82);
        const QImage button=renderer.controlImage(buttonRect,15);
        check(!button.isNull() && button.size()==QSize(qRound(78*dpr),qRound(82*dpr))
              && button.devicePixelRatio()==dpr,"button crop respects DPR and dimensions");
        check(qAlpha(button.pixel(0,0))<5 && qRed(button.pixel(0,0))<5,"button has premultiplied round corners");
        const int renderCount=renderer.controlRenderCount();
        check(renderer.controlImage(buttonRect,15)==button &&
              renderer.controlRenderCount()==renderCount,"hover frames reuse cached material");
        const QImage pressed=renderer.controlImage(buttonRect,15,true);
        check(pressed!=button && renderer.image()==gpu,"press has distinct optics without modifying panel");
        check(!renderer.controlImage(QRectF(80,80,150,26),13).isNull(),"shallow action capsule renders");
        check(renderer.controlImage(QRectF(-5,20,30,30),10).isNull(),"out-of-panel control does not stretch clipped source");
        button.save(out+QString("/button-%1.png").arg(dpr));
        pressed.save(out+QString("/button-pressed-%1.png").arg(dpr));
        check(gpu.pixelColor(qRound(200*dpr),qRound(70*dpr)).red() >
              gpu.pixelColor(qRound(200*dpr),qRound(70*dpr)).blue(),"top stays red: no UV flip");
        check(gpu.pixelColor(qRound(200*dpr),qRound(230*dpr)).blue() >
              gpu.pixelColor(qRound(200*dpr),qRound(230*dpr)).red(),"bottom stays blue");
        int delta=0;
        for(int y=60*dpr;y<100*dpr;++y)
            delta=qMax(delta,std::abs(qRed(gpu.pixel(200*dpr,y))-qRed(gpu.pixel(200*dpr,y+1))));
        check(delta<6,"high-frequency stripes/text suppressed in body");
        qputenv("KAISHICAIDAN_GLASS_NO_GL","1");
        renderer.setBackdrop(src);
        QImage cpu=renderer.image();
        check(!renderer.usedGpu() && !cpu.isNull(),"forced CPU fallback");
        check(renderer.controlRenderCount()==0,"new backdrop invalidates controls");
        check(!renderer.controlImage(buttonRect,15).isNull(),"buttons retain CPU fallback");
        double rimDiff=0; int n=0;
        for(int y=5*dpr;y<25*dpr;++y)
            for(int x=50*dpr;x<200*dpr;++x) {
                rimDiff+=std::abs(qRed(cpu.pixel(x,y))-qRed(gpu.pixel(x,y))); ++n;
            }
        check(rimDiff/n>0.1,"GPU optical edge differs from plain CPU blur");
        check(qAlpha(gpu.pixel(200*dpr,150*dpr))==255,"body has no accidental compositing transparency");
        gpu.save(out+QString("/glass-gpu-%1.png").arg(dpr));
        cpu.save(out+QString("/glass-cpu-%1.png").arg(dpr));
    }
    qunsetenv("KAISHICAIDAN_GLASS_NO_GL");
    QImage menu(QSize(1020,1080),QImage::Format_RGB32);
    menu.setDevicePixelRatio(1.5);
    menu.fill(QColor("#18212c"));
    qint64 total=0;
    for(int i=0;i<5;++i) {
        renderer.setBackdrop(menu);
        total+=renderer.lastRenderMs();
    }
    qInfo() << "Full-menu warm render mean:" << total/5.0 << "ms";
    check(renderer.usedGpu() && renderer.gpuInitializationCount()==1,"GPU resumes after CPU fallback without recompilation");
    check(renderer.luminanceAt(QRectF(100,100,100,20))<0.15,"dark backdrop avoids label scrim");
    NextKdeGlassView fast;
    for (double dpr : {1.0,1.5,2.0}) {
        QImage input(QSize(qRound(400*dpr),qRound(300*dpr)),QImage::Format_RGB32);
        input.setDevicePixelRatio(dpr);
        input.fill(Qt::white);
        const QByteArray environment=qgetenv("KAISHICAIDAN_GLASS_NO_GL");
        fast.setBackdropFast(input);
        check(!fast.usedGpu() && fast.gpuInitializationCount()==0,"fast material never initializes GL");
        check(qgetenv("KAISHICAIDAN_GLASS_NO_GL")==environment,"fast material leaves process environment untouched");
        check(fast.image().size()==input.size() && fast.image().devicePixelRatio()==dpr,"fast material preserves DPR");
        check(qAlpha(fast.image().pixel(0,0))==0 && qRed(fast.image().pixel(0,0))==0,"fast material has clean transparent corners");
        const QColor center=fast.image().pixelColor(input.width()/2,input.height()/2);
        check(center.red()>135 && center.red()<155 && center.alpha()==255,"fast white body remains readable");
        const QImage previous=fast.image();
        fast.setBackdropFast(input);
        check(fast.image()==previous,"fast material deterministic");
        const QImage control=fast.controlImage(QRectF(70,65,78,82),15);
        const int count=fast.controlRenderCount();
        check(!control.isNull() && fast.controlImage(QRectF(70,65,78,82),15)==control
              && fast.controlRenderCount()==count,"fast controls reuse cached material");
        input.fill(QColor(180,40,10));
        fast.setBackdropFast(input);
        check(fast.image()!=previous && fast.controlRenderCount()==0,"new fast backdrop invalidates material and controls");
        fast.image().save(out+QString("/glass-fast-%1.png").arg(dpr));
    }
    fast.setBackdropFast(menu);
    qInfo() << "Full-menu fast render:" << fast.lastRenderMs() << "ms";
    fast.setBackdropFast({});
    check(fast.image().isNull() && fast.controlImage(QRectF(0,0,60,60),12).isNull(),"fast clear removes cached material");
    renderer.setBackdrop({});
    check(renderer.image().isNull() && !renderer.usedGpu() &&
          renderer.luminanceAt(QRectF(0,0,1,1))==0,"clear removes stale visible material");
    check(renderer.controlImage(QRectF(0,0,60,60),12).isNull(),"clearing backdrop also clears button cache");
    return failures ? 1 : 0;
}
