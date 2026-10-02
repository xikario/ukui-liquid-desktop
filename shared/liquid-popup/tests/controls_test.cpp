#include "LiquidPopup.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFontComboBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSpinBox>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QDebug>
#include <QDir>
#include <cstdlib>

static void check(bool ok,const char *message) {
    if(!ok){qCritical()<<message;std::exit(1);}
}
static void settle(int ms) {QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
static void mouse(QWidget *widget,QEvent::Type type,QPoint position) {
    const bool release=type==QEvent::MouseButtonRelease;
    QMouseEvent event(type,position,widget->mapToGlobal(position),Qt::LeftButton,
                      release?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(widget,&event);
}
static QRect spinButton(QAbstractSpinBox *spin,QStyle::SubControl part) {
    QStyleOptionSpinBox option;option.initFrom(spin);option.frame=spin->hasFrame();
    option.buttonSymbols=spin->buttonSymbols();
    option.stepEnabled=QAbstractSpinBox::StepUpEnabled|QAbstractSpinBox::StepDownEnabled;
    return spin->style()->subControlRect(QStyle::CC_SpinBox,&option,part,spin);
}
static QRect comboButton(QComboBox *combo) {
    QStyleOptionComboBox option;option.initFrom(combo);option.editable=combo->isEditable();option.frame=combo->hasFrame();
    return combo->style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxArrow,combo);
}
static int brightPixels(QWidget *control,QRect button) {
    const QImage image=control->grab().toImage();const qreal dpr=image.devicePixelRatio();
    const QRect pixels(QPoint(qRound(button.left()*dpr),qRound(button.top()*dpr)),
                       QSize(qRound(button.width()*dpr),qRound(button.height()*dpr)));
    int light=0;
    for(int y=pixels.top();y<=pixels.bottom()&&y<image.height();++y)
        for(int x=pixels.left();x<=pixels.right()&&x<image.width();++x) {
            QColor color=image.pixelColor(x,y);
            if(color.red()>200&&color.green()>210&&color.blue()>220)++light;
        }
    return light;
}
static void checkGlyph(QWidget *control,QRect button,int minimumGlyphs=6) {
    const qreal dpr=control->devicePixelRatioF();const int light=brightPixels(control,button);
    check(light>=minimumGlyphs*dpr*dpr,"button has a visible vector arrow");
    check(light<65*dpr*dpr,"button has no opaque native triangle or white rectangular frame");
}
class Paints final : public QObject {
public:int count=0;
protected:bool eventFilter(QObject *,QEvent *event)override{if(event->type()==QEvent::Paint)++count;return false;}
};
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication app(argc,argv);
    if(app.arguments().contains("--styles")){qInfo()<<QStyleFactory::keys();return 0;}
    if(!qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE"))app.setStyle("Fusion");
    qInfo()<<"Native control style"<<app.style()->objectName();
    LiquidPopup::install(app);LiquidPopup::theme().reducedMotion=true;
    QWidget form;auto *layout=new QVBoxLayout(&form);form.resize(390,280);
    form.setStyleSheet("QWidget {color:#edf5ff;background:#243749;} QComboBox,QAbstractSpinBox {"
                      "background:#172b40;border:1px solid #526577;border-radius:6px;padding:4px;}");
    auto *combo=new QComboBox;combo->addItems({"第一项","第二项","第三项"});layout->addWidget(combo);
    auto *fonts=new QFontComboBox;fonts->setEditable(false);layout->addWidget(fonts);
    auto *fontDelegate=fonts->view()->itemDelegate();
    auto *spin=new QSpinBox;spin->setRange(9,22);spin->setValue(14);spin->setSuffix(" pt");layout->addWidget(spin);
    auto *decimal=new QDoubleSpinBox;decimal->setRange(0,1);decimal->setSingleStep(.1);decimal->setValue(.5);layout->addWidget(decimal);
    LiquidPopup::installComboPopups(&form);form.show();settle(40);
    if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0") {
        check(!combo->property("liquidControlButtons").toBool()&&!spin->property("liquidControlButtons").toBool(),"disabled switch leaves native buttons intact");
        return 0;
    }
    check(fonts->view()->itemDelegate()==fontDelegate,"font preview delegate preserved exactly");
    check(combo->property("liquidControlButtons").toBool()&&spin->property("liquidControlButtons").toBool(),"combo and spin fields share button skin");
    check(combo->findChildren<QWidget *>("liquidControlGlyphs").size()==1,"one glyph layer installed");
    LiquidPopup::installComboPopups(&form);LiquidPopup::installControls(&form);
    check(combo->findChildren<QWidget *>("liquidControlGlyphs").size()==1,"repeated opening does not stack overlays");
    auto *glyph=combo->findChild<QWidget *>("liquidControlGlyphs");
    check(glyph->testAttribute(Qt::WA_TransparentForMouseEvents)&&glyph->focusPolicy()==Qt::NoFocus,"glyph layer cannot intercept input or focus");
    check(comboButton(combo).width()>=28&&spinButton(spin,QStyle::SC_SpinBoxUp).width()>=28,"arrows retain comfortable native hit areas");
    checkGlyph(combo,comboButton(combo));checkGlyph(fonts,comboButton(fonts));
    checkGlyph(spin,spinButton(spin,QStyle::SC_SpinBoxUp));checkGlyph(spin,spinButton(spin,QStyle::SC_SpinBoxDown));
    const QPoint arrow=comboButton(combo).center();mouse(combo,QEvent::MouseButtonPress,arrow);mouse(combo,QEvent::MouseButtonRelease,arrow);settle(30);
    check(combo->view()->window()->isVisible(),"clicking the skinned arrow opens the native list");
    QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier),enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(combo->view(),&down);QApplication::sendEvent(combo->view(),&enter);settle(20);
    check(combo->currentIndex()==1&&!combo->view()->window()->isVisible(),"native list keyboard activation remains correct");
    const QPoint up=spinButton(spin,QStyle::SC_SpinBoxUp).center(),lower=spinButton(spin,QStyle::SC_SpinBoxDown).center();
    mouse(spin,QEvent::MouseButtonPress,up);mouse(spin,QEvent::MouseButtonRelease,up);check(spin->value()==15,"upper chevron increments native value");
    mouse(spin,QEvent::MouseButtonPress,lower);mouse(spin,QEvent::MouseButtonRelease,lower);check(spin->value()==14,"lower chevron decrements native value");
    spin->setFocus();QKeyEvent keyUp(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(spin,&keyUp);
    check(spin->value()==15,"spin keyboard stepping preserved");
    QWheelEvent wheel(spin->rect().center(),spin->mapToGlobal(spin->rect().center()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(spin,&wheel);check(spin->value()==16,"spin wheel stepping preserved");
    mouse(spin,QEvent::MouseButtonPress,up);settle(850);mouse(spin,QEvent::MouseButtonRelease,up);
    check(spin->value()>17,"native held-button autorepeat preserved");
    spin->setValue(spin->maximum());mouse(spin,QEvent::MouseButtonPress,up);mouse(spin,QEvent::MouseButtonRelease,up);
    check(spin->value()==spin->maximum(),"disabled upper step cannot exceed maximum");
    spin->setValue(14);spin->setMaximum(14);settle(20);
    check(brightPixels(spin,spinButton(spin,QStyle::SC_SpinBoxUp))==0,"range change dims disabled arrow even when the value does not change");
    spin->setMaximum(22);spin->setReadOnly(true);settle(20);
    mouse(spin,QEvent::MouseButtonPress,up);mouse(spin,QEvent::MouseButtonRelease,up);
    check(spin->value()==14&&brightPixels(spin,spinButton(spin,QStyle::SC_SpinBoxUp))==0,"read-only fields keep native stepping disabled and glyph dimmed");
    spin->setReadOnly(false);spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);settle(20);
    check(brightPixels(spin,spinButton(spin,QStyle::SC_SpinBoxUp))>brightPixels(spin,spinButton(spin,QStyle::SC_SpinBoxDown))+3,"PlusMinus mode paints a plus and a minus rather than arrows");
    mouse(spin,QEvent::MouseButtonPress,up);mouse(spin,QEvent::MouseButtonRelease,up);
    check(spin->value()==15,"PlusMinus retains native increment behavior");
    const QPoint decimalUp=spinButton(decimal,QStyle::SC_SpinBoxUp).center();mouse(decimal,QEvent::MouseButtonPress,decimalUp);mouse(decimal,QEvent::MouseButtonRelease,decimalUp);
    check(qAbs(decimal->value()-.6)<.0001,"decimal spin buttons preserve native step size");
    const int buttonsEditWidth=spinButton(spin,QStyle::SC_SpinBoxEditField).width();
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);spin->update();settle(10);
    const int noButtonsValue=spin->value();mouse(spin,QEvent::MouseButtonPress,up);mouse(spin,QEvent::MouseButtonRelease,up);
    check(spin->value()==noButtonsValue,"NoButtons spin preserves its original input mode");
    check(spinButton(spin,QStyle::SC_SpinBoxEditField).width()>=buttonsEditWidth+20,"NoButtons releases the button area back to the text field");
    spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);spin->setValue(14);settle(10);
    const QSize oldSize=combo->size();combo->resize(oldSize+QSize(20,0));settle(10);
    check(glyph->geometry()==combo->rect(),"glyph follows resized field without layout ownership");
    combo->resize(oldSize);settle(10);
    Paints paints;glyph->installEventFilter(&paints);settle(30);paints.count=0;settle(100);
    check(paints.count==0,"stationary buttons have no timer or continuous paint");
    QDir().mkpath("artifacts");form.grab().save("artifacts/liquid-controls.png");
    qInfo()<<"Unified liquid control buttons preserve input and font delegate";return 0;
}
