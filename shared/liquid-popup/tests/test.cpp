#include <QEventLoop>
#include "LiquidPopup.h"
#include "LiquidSurface.h"
#include "MenuTextRecorder.h"
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
class GeometryChanges final : public QObject {
public:
 int count=0;
protected:
 bool eventFilter(QObject *,QEvent *event) override {
   if(event->type()==QEvent::Resize || event->type()==QEvent::Move) ++count;
   return false;
 }
};
void checkTextColumns(QMenu &menu, QAction &action, const QString &label, const QString &shortcut) {
 const QRect row=menu.actionGeometry(&action);
 MenuTextRecorder recorder(menu);
 const QRectF text=recorder.boundsFor(label,row), keys=recorder.boundsFor(shortcut,row);
 qInfo()<<"painted menu columns"<<menu.layoutDirection()<<label<<text<<keys;
 check(!text.isEmpty() && !keys.isEmpty(),"both label and shortcut actually paint");
 check(row.contains(text.toAlignedRect()) && row.contains(keys.toAlignedRect()),"painted text fits the action row");
 const qreal gap=menu.layoutDirection()==Qt::LeftToRight?keys.left()-text.right():text.left()-keys.right();
 check(gap>=8,"painted shortcut and label keep a readable gap");
}
void testMenuText(QApplication &app) {
 theme().reducedMotion=true;
 setBackdropProvider([](const QRect &rect,qreal dpr){
   QImage image(rect.size()*dpr,QImage::Format_RGB32);image.setDevicePixelRatio(dpr);
   image.fill(QColor(40,55,70));return image;
 });
 QPixmap icon(16,16);icon.fill(Qt::red);
 QDir().mkpath("artifacts");
 for(int points:{10,18}) for(bool hidden:{true,false}) {
   QMenu menu;
   QFont font=app.font();font.setPointSize(points);menu.setFont(font);
   menu.setStyleSheet("QMenu {color:white;padding:4px;} QMenu::item {padding:4px 24px 4px 12px;}");
   auto *undo=menu.addAction(QIcon(icon),QString::fromUtf8("撤回"));
   undo->setIconVisibleInMenu(!hidden);undo->setShortcut(QKeySequence::Undo);
   menu.addAction(QString::fromUtf8("粘贴"))->setEnabled(false);
   menu.addMenu(QString::fromUtf8("新建"))->addAction("child");
   auto *explicitTab=menu.addAction(QString::fromUtf8("复制\tCtrl+C"));
   explicitTab->setEnabled(false);
   menu.popup(QPoint(30,30));app.processEvents();
   checkTextColumns(menu,*undo,QString::fromUtf8("撤回"),undo->shortcut().toString(QKeySequence::NativeText));
   checkTextColumns(menu,*explicitTab,QString::fromUtf8("复制"),"Ctrl+C");
   undo->setText(QString::fromUtf8("撤回文件操作"));undo->setShortcut(QKeySequence("Ctrl+Shift+Z"));app.processEvents();
   checkTextColumns(menu,*undo,QString::fromUtf8("撤回文件操作"),undo->shortcut().toString(QKeySequence::NativeText));
   font.setPointSize(points+2);menu.setFont(font);app.processEvents();
   checkTextColumns(menu,*undo,QString::fromUtf8("撤回文件操作"),undo->shortcut().toString(QKeySequence::NativeText));
   // The widest label has no icon; it still needs the shared icon column.
   explicitTab->setText(QString::fromUtf8("复制选中的文件\tCtrl+C"));app.processEvents();
   checkTextColumns(menu,*explicitTab,QString::fromUtf8("复制选中的文件"),"Ctrl+C");
   auto *extra=menu.addAction(QIcon(icon),"icon");extra->setIconVisibleInMenu(true);app.processEvents();
   checkTextColumns(menu,*explicitTab,QString::fromUtf8("复制选中的文件"),"Ctrl+C");
   menu.removeAction(extra);delete extra;app.processEvents();
   checkTextColumns(menu,*undo,QString::fromUtf8("撤回文件操作"),undo->shortcut().toString(QKeySequence::NativeText));
   menu.grab().save(QString("artifacts/menu-text-%1-%2-%3.png")
     .arg(points).arg(hidden).arg(menu.devicePixelRatioF()));
   menu.hide();
   check(undo->isIconVisibleInMenu()==!hidden,"shortcut layout preserves action icon preference");
 }
}
int main(int argc,char **argv){
 QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
 QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
 QApplication app(argc,argv);auto *originalAppStyle=app.style();
 installMenuGlyphStyle(app);install(app);install(app);
 check(app.style()==originalAppStyle,"menu integration preserves application style");
 if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0") {
   QMenu native;native.addAction("native");native.popup(QPoint(10,10));app.processEvents();
   check(!native.property("liquidPopupSkin").toBool(),"environment switch disables adaptation");
   check(!app.property("liquidMenuGlyphStyleInstalled").toBool(),"environment switch disables glyph style");
   return 0;
 }
 if(app.arguments().contains("--menu-text-only")){testMenuText(app);return 0;}
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
 // Reversing the same bubble must preserve progress and backdrop.
 theme().reducedMotion=false;
 Shell reversal;reversal.setContent(new QLabel("reversal"));const QRect reverseAnchor(200,200,40,30);
 reversal.openAt(reverseAnchor);
 QEventLoop animationWait;QTimer::singleShot(90,&animationWait,&QEventLoop::quit);animationWait.exec();
 reversal.dismiss();const qreal beforeReverse=reversal.progress();const int beforeCaptures=captures;
 reversal.openAt(reverseAnchor);
 check(beforeReverse>0 && reversal.progress()>=beforeReverse && captures==beforeCaptures,"same bubble reverses without progress reset or self-capture");
 reversal.hide();theme().reducedMotion=true;captures=0;
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
 // Launcher installation-source lookup changes this label after native Show.
 // Exercise both shrinking and growing without mapping a second popup.
 QMenu changing;
 changing.setStyleSheet("QMenu {color:white;padding:6px;} QMenu::item {min-height:18px;}");
 auto *changingAction=changing.addAction(QString::fromUtf8("正在检查安装来源…"));
 changingAction->setEnabled(false);
 changing.popup(QPoint(80,80));app.processEvents();
 const QSize openingSize=changing.size();const int openingCaptures=captures;
 changingAction->setText(QString::fromUtf8("卸载 deb 软件包"));
 changingAction->setEnabled(true);app.processEvents();
 qInfo()<<"dynamic menu opening/shrunk size"<<openingSize<<changing.size();
 check(changing.mask().contains(QPoint(changing.width()-2,changing.height()/2)),
       "shrunk popup mask follows the current right edge");
 const int narrowWidth=changing.width();
 changingAction->setText(QString::fromUtf8("正在检查其他应用，请稍后重开菜单"));
 app.processEvents();
 qInfo()<<"dynamic menu expanded size"<<changing.size();
 check(changing.width()>narrowWidth,"async action text grows the native menu");
 for(int x:{1,changing.width()/2,changing.width()-2})
   check(changing.mask().contains(QPoint(x,changing.height()/2)),
         "resized popup silhouette covers both current side edges");
 QImage expanded(changing.size()*changing.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);
 expanded.setDevicePixelRatio(changing.devicePixelRatioF());
 expanded.fill(Qt::transparent);changing.render(&expanded,QPoint(),QRegion(),QWidget::DrawChildren);
 for(int x:{1,expanded.width()/2,expanded.width()-2})
   check(expanded.pixelColor(x,expanded.height()/2).alpha()>0,
         "resized popup material paints across its full width");
 check(captures==openingCaptures,"visible resize never captures the popup itself");
 expanded.save("artifacts/menu-resized.png");
 // A late short result, followed by another long result, must not retain
 // pixels or masks from either earlier width. Repeat with a fresh opening.
 changing.hide();
 for(int opening=0;opening<3;++opening) {
   changing.popup(QPoint(80,80));app.processEvents();
   const int capturesAfterOpen=captures;
   for(const auto &text:{QString::fromUtf8("移除快捷方式"),
                        QString::fromUtf8("系统组件，无法卸载"),
                        QString::fromUtf8("正在检查其他应用，请稍后重开菜单")}) {
     changingAction->setText(text);app.processEvents();
     QImage frame(changing.size()*changing.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);
     frame.setDevicePixelRatio(changing.devicePixelRatioF());frame.fill(Qt::transparent);
     changing.render(&frame,QPoint(),QRegion(),QWidget::DrawChildren);
     const int middle=frame.height()/2;
     check(frame.pixelColor(1,middle).alpha()>0 &&
           frame.pixelColor(frame.width()-2,middle).alpha()>0,
           "repeated grow/shrink repaints both material edges");
     check(frame.pixelColor(0,0).alpha()==0 &&
           frame.pixelColor(frame.width()-1,0).alpha()==0 &&
           frame.pixelColor(0,frame.height()-1).alpha()==0 &&
           frame.pixelColor(frame.width()-1,frame.height()-1).alpha()==0,
           "repeated grow/shrink clears all exterior corners");
     check(captures==capturesAfterOpen,"repeated action updates reuse only the opening capture");
   }
   changing.hide();
   check(changing.mask().isEmpty(),"closing resized popup restores its native mask");
 }
 changingAction->setText(QString::fromUtf8("打开"));
 changing.popup(available.bottomRight()-QPoint(8,8));app.processEvents();
 changingAction->setText(QString::fromUtf8("正在检查其他应用，请稍后重开菜单"));app.processEvents();
 qInfo()<<"resized edge menu"<<changing.geometry()<<"available"<<available;
 check(available.contains(changing.geometry()),"async resized popup remains inside the screen");
 changing.hide();
 // Unlike general dynamic menus above, an async status menu reserves all
 // result labels while hidden: neither its first nor any later frame resizes.
 QMenu stable;
 stable.setStyleSheet("QMenu {color:white;padding:6px;} QMenu::item {min-height:18px;}");
 stable.addAction(QString::fromUtf8("从已固定取消"));
 stable.addAction(QString::fromUtf8("打开"));
 stable.addAction(QString::fromUtf8("打开目录"));
 stable.addAction(QString::fromUtf8("修改图标"));
 stable.addSeparator();
 const QString pendingText=QString::fromUtf8("正在检查安装来源…");
 auto *status=stable.addAction(pendingText);status->setEnabled(false);
 const QStringList results={QString::fromUtf8("移到回收站（文件夹版）"),
   QString::fromUtf8("系统组件，无法卸载"),QString::fromUtf8("卸载 deb 软件包"),
   QString::fromUtf8("移除快捷方式"),QString::fromUtf8("请在软件商店卸载（Flatpak）"),
   QString::fromUtf8("请在软件商店卸载（Snap）"),QString::fromUtf8("无法判断安装来源")};
 bool statusTriggered=false;
 QObject::connect(status,&QAction::triggered,[&]{statusTriggered=true;});
 reserveActionTextWidth(stable,*status,results);
 check(!stable.isVisible() && status->text()==pendingText && !status->isEnabled() && !statusTriggered,
       "reserving status width preserves pending text, disabled state and actions");
 GeometryChanges geometryChanges;
 for(int opening=0;opening<3;++opening) {
   status->setText(pendingText);status->setEnabled(false);
   const int beforeCapture=captures;
   stable.popup(opening==2?available.bottomRight()-QPoint(8,8):QPoint(80,80));app.processEvents();
   const QRect initialGeometry=stable.geometry();
   check(available.contains(initialGeometry),"reserved popup initially fits the screen including at its edge");
   geometryChanges.count=0;stable.installEventFilter(&geometryChanges);
   QEventLoop lookupWait;
   for(int i=0;i<results.size();++i)
     QTimer::singleShot(10+i*15,&stable,[&,i]{
       status->setText(results[i]);status->setEnabled(i%2==0);
       check(stable.geometry()==initialGeometry,"async result keeps initial popup position and size");
     });
   QTimer::singleShot(130,&lookupWait,&QEventLoop::quit);lookupWait.exec();
   check(geometryChanges.count==0,"no resize or move event occurs between pending and final labels");
   check(captures==beforeCapture+1,"stable status menu samples only its initial opening");
   stable.removeEventFilter(&geometryChanges);
   QImage frame(stable.size()*stable.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);
   frame.setDevicePixelRatio(stable.devicePixelRatioF());frame.fill(Qt::transparent);
   stable.render(&frame,QPoint(),QRegion(),QWidget::DrawChildren);
   frame.save(QString("artifacts/menu-stable-%1.png").arg(opening));
   qInfo()<<"stable async menu geometry"<<initialGeometry<<"layout events"<<geometryChanges.count;
   stable.hide();
 }
 check(!statusTriggered,"lookup updates never trigger a menu command");
 stable.popup(QPoint(80,80));app.processEvents();status->setEnabled(true);stable.setActiveAction(status);
 QKeyEvent confirmStatus(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
 QApplication::sendEvent(&stable,&confirmStatus);
 check(statusTriggered && !stable.isVisible(),"reserved menu retains native keyboard activation");
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
