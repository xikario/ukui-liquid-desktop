#include "LiquidButtons.h"
#include "LiquidDialog.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QDebug>
#include <QtMath>
#include <cstdlib>

static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}}
static void settle(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
class Backdrop final : public QWidget {
public:using QWidget::QWidget;
protected:void paintEvent(QPaintEvent *)override {
    QPainter p(this);p.fillRect(rect(),QColor(16,43,49));
    for(int y=0;y<height();y+=4)p.fillRect(0,y,width(),2,QColor(33,67,59));
}
};
class Paints final : public QObject {
public:int count=0;
protected:bool eventFilter(QObject *,QEvent *e)override{if(e->type()==QEvent::Paint)++count;return false;}
};
static void mouse(QPushButton *b,QEvent::Type t) {
    const bool release=t==QEvent::MouseButtonRelease;
    QMouseEvent event(t,b->rect().center(),b->mapToGlobal(b->rect().center()),Qt::LeftButton,
        release?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(b,&event);
}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication app(argc,argv);
    check(app.setStyle(qEnvironmentVariable("UKUI_BUTTON_TEST_STYLE","Fusion"))!=nullptr,"requested style available");
    LiquidPopup::install(app);LiquidPopup::theme().reducedMotion=true;
    Backdrop host;host.resize(540,330);
    host.setStyleSheet("QPushButton {color:#edf5ff;background:rgba(218,238,255,22);border:1px solid #637b80;border-radius:8px;padding:6px 12px;} QPushButton:disabled {color:#758598;}");
    auto *layout=new QVBoxLayout(&host);
    auto *normal=new QPushButton("恢复默认",&host);normal->setFixedSize(116,38);layout->addWidget(normal);
    auto *pressed=new QPushButton("按下",&host);pressed->setDown(true);layout->addWidget(pressed);
    auto *checked=new QPushButton("已选择",&host);checked->setCheckable(true);checked->setChecked(true);layout->addWidget(checked);
    auto *disabled=new QPushButton("不可用",&host);disabled->setEnabled(false);layout->addWidget(disabled);
    auto *flat=new QPushButton("无框动作",&host);flat->setProperty("liquidButtonFlat",true);layout->addWidget(flat);
    auto *box=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close,&host);layout->addWidget(box);
    LiquidButtons::install(&host);host.show();settle(30);
    if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0") {
        check(!normal->property("liquidButtonInstalled").toBool(),"disabled integration preserves native buttons");return 0;
    }
    normal->clearFocus();host.setFocus();settle(15);
    check(normal->property("liquidButtonInstalled").toBool()&&box->button(QDialogButtonBox::Apply)->property("liquidButtonInstalled").toBool(),"standard and button-box children share the painter");
    QWidget unrelated;QPushButton native("native",&unrelated);
    check(!native.property("liquidButtonInstalled").toBool(),"unowned controls are not modified");
    auto *late=new QPushButton("动态动作",&host);layout->addWidget(late);late->show();settle(10);
    check(late->property("liquidButtonInstalled").toBool(),"lazy page children are adopted on polish/show");
    int clicked=0;QObject::connect(normal,&QPushButton::clicked,[&]{++clicked;});
    mouse(normal,QEvent::MouseButtonPress);mouse(normal,QEvent::MouseButtonRelease);
    check(clicked==1,"real mouse activation retains one native clicked signal");
    normal->setFocus();QKeyEvent down(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier),up(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
    QApplication::sendEvent(normal,&down);QApplication::sendEvent(normal,&up);
    check(clicked==2,"space-key activation remains native");
    mouse(checked,QEvent::MouseButtonPress);mouse(checked,QEvent::MouseButtonRelease);
    check(!checked->isChecked(),"checkable actions retain toggling");
    int forbidden=0;QObject::connect(disabled,&QPushButton::clicked,[&]{++forbidden;});
    mouse(disabled,QEvent::MouseButtonPress);mouse(disabled,QEvent::MouseButtonRelease);
    check(forbidden==0,"disabled controls cannot activate");
    normal->clearFocus();host.setFocus();settle(15);
    const QImage full=host.grab().toImage();const qreal dpr=full.devicePixelRatio();
    QDir().mkpath("artifacts");full.save(QString("artifacts/liquid-buttons-%1-%2.png").arg(dpr).arg(app.style()->objectName()));
    // All four extreme corner pixels are the parent's backdrop, not a button
    // rectangle. Intermediate edge pixels retain AA coverage over that image.
    const QRect rect=normal->geometry();
    auto pixel=[&](QPoint p){return full.pixelColor(qFloor(p.x()*dpr),qFloor(p.y()*dpr));};
    for(const QPoint p:{rect.topLeft(),rect.topRight(),rect.bottomLeft(),rect.bottomRight()}) {
        const QColor expected=pixel(QPoint(0,p.y()));
        check(pixel(p)==expected,"rounded extreme corner contains only the parent backdrop");
    }
    // Repainting narrow strips over a translucent button must match a clean
    // complete repaint, preventing dirty-region corner accumulation.
    for(int i=0;i<30;++i) {host.update(rect.adjusted(-1,-1,1,1));normal->update(QRect(0,0,8,normal->height()));settle(1);}
    const QImage partial=host.grab().toImage();host.update();settle(15);
    check(partial==host.grab().toImage(),"partial repaint does not accumulate rim pixels");
    Paints paints;normal->installEventFilter(&paints);settle(20);paints.count=0;settle(120);
    check(paints.count==0,"stationary buttons do not repaint or poll");
    QDir().mkpath("artifacts");host.grab().save(QString("artifacts/liquid-buttons-%1-%2.png").arg(dpr).arg(app.style()->objectName()));
    LiquidDialog::Dialog dialog;auto *form=new QVBoxLayout(&dialog);auto *apply=new QPushButton("应用",&dialog);form->addWidget(apply);apply->setDefault(true);
    int defaults=0;QObject::connect(apply,&QPushButton::clicked,[&]{++defaults;});dialog.show();settle(25);
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(&dialog,&enter);
    check(defaults==1,"dialog default Enter action is preserved");
    qInfo()<<"Antialiased glass buttons retain native actions and idle behavior";return 0;
}
