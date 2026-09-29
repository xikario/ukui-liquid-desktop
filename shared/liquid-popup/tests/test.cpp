#include "LiquidPopup.h"
#include "LiquidSurface.h"
#include <QLabel>
#include <QListWidget>
#include <QHelpEvent>
#include <QScreen>
#include <QPainter>
#include <QTimer>
#include <QKeyEvent>
#include <QDebug>
#include <QDir>
#include <QStyleOption>
#include <cstdlib>
using namespace LiquidPopup;
void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}}
int main(int argc,char **argv){
 QApplication app(argc,argv);auto *originalAppStyle=app.style();
 installMenuGlyphStyle(app);install(app);install(app);
 check(app.style()==originalAppStyle,"menu integration preserves application style");
 if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0") {
   QMenu native;native.addAction("native");native.popup(QPoint(10,10));app.processEvents();
   check(!native.property("liquidPopupSkin").toBool(),"environment switch disables adaptation");
   check(!app.property("liquidMenuGlyphStyleInstalled").toBool(),"environment switch disables glyph style");
   return 0;
 }
 theme().reducedMotion=true;
 const QRect screen(-1280,0,1280,800);
 for(QPoint pt:{QPoint(-1270,8),QPoint(-5,795),QPoint(-600,350)})
   check(screen.contains(place(QSize(260,150),QRect(pt,QSize(1,1)),screen)),"popup must stay within negative-origin screen");
 QImage backdrop(600,300,QImage::Format_RGB32);backdrop.fill(QColor(90,140,190));backdrop.setDevicePixelRatio(2);
 const auto image=renderMaterial(backdrop,QSize(300,150),2);
 check(image.size()==QSize(600,300)&&image.devicePixelRatio()==2,"DPR retained");
 check(!renderMaterial({},QSize(260,100),1).isNull(),"missing capture has fallback");
 check(renderMaterial({},QSize(),1).isNull(),"empty size");
 auto shape=bubblePath(QRectF(8,12,260,100),16,130,true);
 check(!shape.contains(QPointF(8,12))&&shape.contains(QPointF(130,6)),"rounded corners and connector");
 auto sideShape=bubblePath(QRectF(8,12,260,100),16,60,Placement::Right);
 check(sideShape.contains(QPointF(2,60)) && !sideShape.contains(QPointF(274,60)),
       "right-placed bubble connector faces the anchor on its left");
 const auto leftShape=bubblePath(QRectF(8,12,260,100),16,60,Placement::Left);
 check(leftShape.contains(QPointF(274,60)) && !leftShape.contains(QPointF(2,60)),
       "left-placed bubble connector faces the anchor on its right");
 Shell shell;shell.setContent(new QLabel("test"));shell.openAt(QRect(300,100,20,20));
 check(shell.isVisible()&&shell.progress()==1,"reduced motion opens immediately");shell.dismiss();check(!shell.isVisible(),"dismiss closes");
 Shell sideTip(nullptr,true);sideTip.setContent(new QLabel("rail tooltip"));
 sideTip.openAt(QRect(80,300,40,40),Placement::Right);
 check(sideTip.isVisible() && sideTip.geometry().left()>119,
       "tooltip can place to the right of a rail anchor");
 const QRect available=QGuiApplication::primaryScreen()->availableGeometry();
 const QRect rightAnchor(available.right()-40,available.center().y(),32,32);
 sideTip.openAt(rightAnchor,Placement::Right);
 check(available.adjusted(8,8,-8,-8).contains(sideTip.geometry()) &&
       !sideTip.geometry().intersects(rightAnchor),
       "right-edge tooltip switches direction and stays on screen clear of anchor");
 const QRect leftAnchor(available.left()+8,available.center().y(),32,32);
 sideTip.openAt(leftAnchor,Placement::Left);
 check(available.adjusted(8,8,-8,-8).contains(sideTip.geometry()) &&
       !sideTip.geometry().intersects(leftAnchor),
       "left-edge tooltip switches direction and stays on screen clear of anchor");
 sideTip.dismiss();check(!sideTip.isVisible(),"side tooltip dismisses");
 QRect lastCapture;int captures=0;setBackdropProvider([&](const QRect &r,qreal d){lastCapture=r;++captures;QImage b(r.size(),QImage::Format_RGB32);b.fill(QColor(40,70,110));b.setDevicePixelRatio(d);return b;});
 QMenu menu;menu.setStyleSheet("QMenu { background:#202020;color:white;padding:8px; } QMenu::item {padding:7px 20px;} QMenu::item:selected{background:#406080;}");
 auto *action=menu.addAction("toggle");action->setCheckable(true);
 QPixmap actionPixmap(16,16);actionPixmap.fill(Qt::red);
 action->setIcon(QIcon(actionPixmap));
 auto *sub=menu.addMenu("submenu");sub->addAction("child");menu.addAction("disabled")->setEnabled(false);
 menu.popup(QPoint(50,50));app.processEvents();
 check(menu.property("liquidPopupSkin").toBool(),"menu is adapted");
 check(!action->isIconVisibleInMenu(),"toggle icon cannot obscure its checkmark");
 for(qreal d:{1.,1.5,2.}) {
   QImage glyph(QSize(qRound(18*d),qRound(18*d)),QImage::Format_ARGB32_Premultiplied);
   glyph.setDevicePixelRatio(d);glyph.fill(Qt::transparent);
   QStyleOption option;option.rect=QRect(2,2,14,14);
   option.state=QStyle::State_Enabled|QStyle::State_On;
   option.palette.setColor(QPalette::Text,Qt::white);
   {QPainter painter(&glyph);menu.style()->drawPrimitive(QStyle::PE_IndicatorMenuCheckMark,&option,&painter,&menu);}
   int partial=0,painted=0;
   for(int y=0;y<glyph.height();++y)for(int x=0;x<glyph.width();++x){
     const int alpha=glyph.pixelColor(x,y).alpha();
     if(alpha>0)++painted;
     if(alpha>0 && alpha<255)++partial;
   }
   check(painted>10 && partial>=5,"host style draws antialiased menu check at each DPR");
   QDir().mkpath("artifacts");glyph.save(QString("artifacts/check-%1.png").arg(d));
 }
 check(captures==1,"capture once per opening");
 check(lastCapture.size()==menu.size(),"capture is bounded to popup instead of full monitor");
 QImage painted(menu.size(),QImage::Format_ARGB32_Premultiplied);painted.fill(Qt::transparent);menu.render(&painted);
 check(qAlpha(painted.pixel(menu.width()/2,menu.height()/2))>0,"menu body paints");
 QDir().mkpath("artifacts");painted.save("artifacts/menu.png");
 bool triggered=false;QObject::connect(action,&QAction::triggered,[&]{triggered=true;});menu.setActiveAction(action);
 QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(&menu,&enter);
 check(triggered&&action->isChecked(),"native keyboard action/check semantics retained");
 check(action->isIconVisibleInMenu(),"closing a menu restores the action icon preference");
 QAction external("shared toggle",&app);external.setCheckable(true);external.setIcon(QIcon(actionPixmap));
 auto *temporary=new QMenu;temporary->addAction(&external);temporary->popup(QPoint(10,10));app.processEvents();
 check(!external.isIconVisibleInMenu(),"shared action adapted");delete temporary;
 check(external.isIconVisibleInMenu(),"destroying open menu restores external action");
 check(app.style()==originalAppStyle,"menu lifetime leaves application style unchanged");
 sub->popup(QPoint(300,50));app.processEvents();check(sub->property("liquidPopupSkin").toBool(),"native submenu adapted");sub->hide();
 // Exercise the actual nested QMenu::exec path, not just geometry math.
 const QRect anchor(250,80,200,35);
 QTimer::singleShot(0,&menu,[&]{
   check(std::abs(menu.geometry().center().x()-anchor.center().x())<=1,"button menu centered on anchor");
   check(menu.geometry().top()>anchor.bottom(),"menu clears trigger button");menu.close();
 });
 execAt(menu,anchor);
 const QRect edgeAnchor(250,QGuiApplication::primaryScreen()->availableGeometry().bottom()-35,200,25);
 QTimer::singleShot(0,&menu,[&]{
   check(menu.geometry().bottom()<edgeAnchor.top(),"bottom-edge menu flips above trigger");menu.close();
 });
 execAt(menu,edgeAnchor);
 QListWidget list;list.resize(280,120);auto *item=new QListWidgetItem("document",&list);
 item->setToolTip("model-role liquid tooltip");list.show();app.processEvents();
 const QPoint hover=list.visualItemRect(item).center();
 QHelpEvent help(QEvent::ToolTip,hover,list.viewport()->mapToGlobal(hover));
 QApplication::sendEvent(list.viewport(),&help);
 bool customTip=false;
 for(auto *w:QApplication::topLevelWidgets()) if(w->objectName()=="liquidPopupShell" && w->isVisible()) customTip=true;
 check(customTip,"item model tooltip is liquid");
 menu.popup(QPoint(50,50));app.processEvents();
 for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="liquidPopupShell")check(!w->isVisible(),"tooltip removed before menu capture");
 menu.hide();list.hide();
 const QString savedStyle=menu.styleSheet();const QRegion savedMask=menu.mask();
 setEnabled(false);check(!isEnabled(),"runtime disabled");
 const int beforeDisabled=captures;menu.popup(QPoint(50,50));app.processEvents();
 check(captures==beforeDisabled,"disabled menu does not capture screen");
 check(menu.styleSheet()==savedStyle && menu.mask()==savedMask,"disabled menu keeps native style and mask");
 menu.hide();setEnabled(true);menu.popup(QPoint(50,50));app.processEvents();
 check(captures>beforeDisabled,"reenabled menu regains material");menu.hide();
 for(qreal d:{1.,1.5,2.}) {
   auto rounded=renderMenuMaterial({},QSize(220,180),d);
   check(rounded.size()==QSize(qRound(220*d),qRound(180*d)) && rounded.devicePixelRatio()==d,"menu material retains physical resolution");
   for(int corner=0;corner<4;++corner) {
     int partial=0;
     for(int y=0;y<qRound(theme().radius*d);++y)for(int x=0;x<qRound(theme().radius*d);++x) {
       const int px=corner%2?rounded.width()-1-x:x;
       const int py=corner/2?rounded.height()-1-y:y;
       const int a=rounded.pixelColor(px,py).alpha();
       if(a>0 && a<255)++partial;
     }
     check(partial>=8,"each menu corner has fractional-alpha coverage");
   }
   check(rounded.pixelColor(0,0).alpha()==0 && rounded.pixelColor(rounded.width()-1,rounded.height()-1).alpha()==0,"menu corner exteriors remain transparent");
   rounded.save(QString("artifacts/menu-rounded-%1.png").arg(d));
   auto surface=renderSurface(QSize(400,48),d,{});
   check(surface.size()==QSize(qRound(400*d),qRound(48*d)),"surface retains physical resolution");
   check(surface.pixelColor(0,0).alpha()==0,"surface corners transparent");
   check(surface.pixelColor(qRound(200*d),qRound(24*d)).alpha()>0 && surface.pixelColor(qRound(200*d),qRound(24*d)).alpha()<255,"surface lets compositor backdrop through");
 }

 theme().reducedMotion=false;shell.openAt(QRect(300,100,20,20));
 QTimer::singleShot(35,&shell,[&]{shell.dismiss();});
 QTimer::singleShot(65,&shell,[&]{shell.openAt(QRect(300,100,20,20));});
 QTimer::singleShot(400,&shell,[&]{check(shell.isVisible()&&shell.progress()>.99,"interrupted motion reopens correctly");shell.hide();app.quit();});
 const int result=app.exec();qInfo()<<"PASS: geometry, DPR, fallback, menu capture, submenu, keyboard and interrupted motion";return result;
}
