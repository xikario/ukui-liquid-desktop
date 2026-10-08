#include <QApplication>
#include <QWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QPointer>
#include <QLibrary>
#include <QDebug>
#include <QScreen>
#include <QMenu>
#include <QSet>
#include <QSettings>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <cstdlib>
class TaskViewButton : public QToolButton {
    Q_OBJECT
public:using QToolButton::QToolButton;};
class StatusNotifierButton : public QToolButton {
    Q_OBJECT
public:
    using QToolButton::QToolButton;
    enum ActionArea {Unknown=0,Storage=1,Show=2,Fixed=3};
    void setActionArea(ActionArea area){m_area=area;}
    __attribute__((noinline)) ActionArea getActionArea();
    int rightReleases=0,areaReads=0;
protected:void mouseReleaseEvent(QMouseEvent *event) override {
    if(event->button()==Qt::RightButton)++rightReleases;
    QToolButton::mouseReleaseEvent(event);
}
private:ActionArea m_area=Unknown;
};
StatusNotifierButton::ActionArea StatusNotifierButton::getActionArea(){++areaReads;return m_area;}
class StatusNotifierStorageArrow : public QToolButton {
    Q_OBJECT
public:using QToolButton::QToolButton;};
class UKUIStartBarWidget : public QWidget {
    Q_OBJECT
public:using QWidget::QWidget;};
class CustomToolTip : public QWidget {
    Q_OBJECT
public:CustomToolTip():QWidget(nullptr,Qt::ToolTip){resize(120,32);}};
class LayoutRequests : public QObject {
public:int count=0;
protected:bool eventFilter(QObject *,QEvent *event) override {
    if(event->type()==QEvent::LayoutRequest)++count;
    return false;
}};
static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}qInfo()<<"PASS:"<<message;}
static void wait(int ms=80){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
int main(int argc,char **argv) {
    QTemporaryDir temp;qputenv("XDG_CONFIG_HOME",temp.path().toUtf8());qputenv("GSETTINGS_BACKEND","memory");
    QApplication app(argc,argv);
    QLibrary gio("libgio-2.0.so.0");
    auto create=reinterpret_cast<void *(*)(const char *)>(gio.resolve("g_settings_new"));
    auto set=reinterpret_cast<int (*)(void *,const char *,int)>(gio.resolve("g_settings_set_boolean"));
    auto get=reinterpret_cast<int (*)(void *,const char *)>(gio.resolve("g_settings_get_boolean"));
    auto unref=reinterpret_cast<void (*)(void *)>(gio.resolve("g_object_unref"));
    check(create && set && get && unref,"GSettings fixture resolves public API");
    auto *settings=create("org.ukui.panel.settings");set(settings,"showtaskview",0);
    set(settings,"statusnotifierbutton",1);
    check(get(settings,"statusnotifierbutton"),"the startup fixture begins with native inline tray expansion enabled");
    QWidget panel;panel.resize(600,48);panel.move(300,700);
    QHBoxLayout row(&panel);
    UKUIStartBarWidget start;QHBoxLayout bar(&start);TaskViewButton task;task.setObjectName("TaskViewButton");
    QWidget divider;divider.setObjectName("Divider");divider.setFixedWidth(1);bar.addWidget(&divider);bar.addWidget(&task);row.addWidget(&start);
    QWidget home;home.setObjectName("StatusNotifier_statusNotifierWidget");QHBoxLayout tray(&home);
    StatusNotifierButton first,second,shown;first.setText("A");second.setText("B");shown.setText("C");
    first.setFixedSize(34,34);second.setFixedSize(34,34);shown.setFixedSize(34,34);
    first.setActionArea(StatusNotifierButton::Storage);second.setActionArea(StatusNotifierButton::Storage);
    shown.setActionArea(StatusNotifierButton::Show);
    first.setProperty("liquidTraySortId","fixture-a");second.setProperty("liquidTraySortId","fixture-b");
    first.setAcceptDrops(true);second.setAcceptDrops(true);
    StatusNotifierStorageArrow arrow;arrow.setFixedSize(34,34);
    tray.addWidget(&first);tray.addWidget(&second);tray.addWidget(&arrow);tray.addWidget(&shown);first.hide();second.hide();
    row.addWidget(&home);panel.show();wait(150);
    check(!get(settings,"statusnotifierbutton"),"creating the popup adapter folds native inline expansion at startup");
    check(task.isHidden() && divider.isHidden(),"disabled task view and divider are hidden at initial mapping");
    task.show();wait();check(task.isHidden(),"OEM showing the task button cannot override false preference");
    set(settings,"showtaskview",1);wait(120);
    check(task.isVisible() && divider.isVisible(),"external setting change immediately reveals task view");
    set(settings,"showtaskview",0);wait(120);
    check(task.isHidden() && divider.isHidden(),"external setting change immediately hides task view again");
    check(home.property("liquidTrayHiddenCount").toInt()==2 && first.parentWidget()!=&home,
          "real hidden buttons are separated from the inline OEM layout");
    QWidget *popup=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="liquidTrayOverflow")popup=w;
    check(popup && !popup->isVisible(),"overflow starts closed");
    const QPoint visiblePos=shown.mapToGlobal(QPoint());const QRect panelRect=panel.geometry();
    auto pressArrow=[&]{
        QPointF local=arrow.rect().center(),global=arrow.mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&arrow,&press);QApplication::sendEvent(&arrow,&release);
    };
    auto click=[&]{pressArrow();wait(280);};
    click();check(popup->isVisible() && first.isVisible() && second.isVisible(),"arrow opens the actual buttons in an independent popup");
    check(popup->geometry().bottom()<arrow.mapToGlobal(QPoint()).y(),"overflow opens above the panel");
    check(panel.geometry()==panelRect && shown.mapToGlobal(QPoint())==visiblePos,"expansion leaves visible icons and panel geometry unchanged");
    const QRect openGeometry=popup->geometry();
    for(int externalExpand=0;externalExpand<3;++externalExpand) {
        set(settings,"statusnotifierbutton",1);
        check(get(settings,"statusnotifierbutton"),"an external request actually enables native inline expansion");
        wait(120);
        check(!get(settings,"statusnotifierbutton") && popup->isVisible() && popup->geometry()==openGeometry
              && first.parentWidget()!=&home && second.parentWidget()!=&home,
              "native inline expansion is folded again without closing or emptying the popup");
        check(panel.geometry()==panelRect && shown.mapToGlobal(QPoint())==visiblePos,
              "folding an external inline expansion keeps the original taskbar geometry stable");
    }
    int activated=0;QObject::connect(&first,&QAbstractButton::clicked,[&]{++activated;});first.click();
    check(activated==1,"the original button and its activation connection are retained");
    // Native layout updates return existing buttons home. Re-adaptation must
    // remove them once and keep the popup connected to those same instances.
    tray.insertWidget(0,&first);tray.insertWidget(1,&second);first.hide();second.hide();wait(120);
    check(first.parentWidget()!=&home && first.isVisible() && home.property("liquidTrayHiddenCount").toInt()==2,
          "OEM layout reset reuses existing buttons without duplicates or losing the popup");
    tray.insertWidget(0,&second);tray.insertWidget(1,&first);second.hide();first.hide();wait(100);
    check(second.mapToGlobal(QPoint()).x()<first.mapToGlobal(QPoint()).x(),
          "OEM drag order is reflected in the popup without changing visible icons");
    check(first.areaReads>0 && second.areaReads>0 && shown.areaReads>0,
          "the plugin resolves and calls the exported native action-area getter");
    // The OEM can re-add its existing arrow before STORAGE buttons while a
    // reset/animation is underway. Their area, not that temporary index,
    // decides which host owns them. SHOW/FIXED may likewise precede the arrow.
    for(int reset=0;reset<3;++reset) {
        shown.setActionArea(reset%2?StatusNotifierButton::Fixed:StatusNotifierButton::Show);
        tray.addWidget(&second);tray.addWidget(&first);tray.insertWidget(0,&shown);
        check(tray.indexOf(&second)>tray.indexOf(&arrow) && tray.indexOf(&shown)<tray.indexOf(&arrow),
              "the fixture exposes native action areas opposite to temporary layout indices");
        first.hide();second.hide();wait(120);
        check(first.parentWidget()!=&home && second.parentWidget()!=&home
              && first.isVisible() && second.isVisible() && popup->isVisible()
              && home.property("liquidTrayHiddenCount").toInt()==2,
              "STORAGE icons after the arrow return to the popup without emptying it");
        check(shown.parentWidget()==&home && tray.indexOf(&shown)<tray.indexOf(&arrow),
              "SHOW and FIXED icons before the arrow remain in the native taskbar");
        tray.addWidget(&shown);wait(120);
        check(panel.geometry()==panelRect && shown.mapToGlobal(QPoint())==visiblePos,
              "restoring native order settles without exchanging icon hosts again");
    }
    shown.setActionArea(StatusNotifierButton::Show);
    // OEM resetIcon/exchangeHideAndShow call setVisible(false) on hidden
    // buttons; inside the open popup that used to leave an empty bubble.
    first.hide();second.hide();wait(80);
    check(popup->isVisible() && first.isVisible() && second.isVisible(),"OEM hide calls cannot empty the open popup");
    const QRect before=popup->geometry();
    QList<StatusNotifierButton *> extra;
    for(int count:{4,5,6}) {
        while(extra.size()+2<count) {
            auto *b=new StatusNotifierButton(&home);b->setFixedSize(34,34);b->setText("D");
            b->setProperty("liquidTraySortId",QString("fixture-extra-%1").arg(extra.size()));
            tray.insertWidget(0,b);b->hide();extra.append(b);
        }
        wait(120);
        QSet<int> columns,rows;QList<StatusNotifierButton *> buttons{&first,&second};buttons.append(extra);
        for(auto *b:buttons){const QPoint p=b->mapToGlobal(QPoint());columns.insert(p.x());rows.insert(p.y());}
        check(columns.size()==3 && rows.size()==2 && home.property("liquidTrayHiddenCount").toInt()==count,
              "four, five and six hidden icons use at most three columns and two rows");
        check(panel.geometry()==panelRect && shown.mapToGlobal(QPoint())==visiblePos,
              "changing popup icon counts leaves the original taskbar geometry unchanged");
    }
    const QRect grown=popup->geometry();
    check(grown.height()>before.height() && grown.width()>before.width(),"six hidden icons use three columns and a second row");
    check(qAbs(grown.bottom()-before.bottom())<=1,"a new row grows upward while the bubble stays on the arrow");
    for(auto *b:extra)delete b;home.layout()->invalidate();wait(120);
    check(popup->geometry()==before,"removing a row shrinks back onto the arrow instead of floating");
    LayoutRequests requests;home.installEventFilter(&requests);
    const int objects=app.allWidgets().size();wait(250);
    check(objects==app.allWidgets().size(),"settled tray has no repeated allocation or rebuilding");
    check(requests.count==0,"settled tray does not continuously request inline OEM layout updates");
    home.removeEventFilter(&requests);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(popup,&escape);wait(400);
    check(!popup->isVisible() && shown.mapToGlobal(QPoint())==visiblePos,"Escape closes the popup without moving visible icons");
    // The OEM tooltip needs a short compositor settling delay. Cancel each
    // pending open through real input/events before that timer can fire.
    CustomToolTip tip;
    tip.show();wait(10);pressArrow();
    check(tip.isHidden() && !popup->isVisible(),"a mapped OEM tooltip defers opening for one settling interval");
    QMouseEvent outside(QEvent::MouseButtonPress,QPointF(4,4),QPointF(panel.mapToGlobal(QPoint(4,4))),
                        Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&panel,&outside);wait(120);
    check(!popup->isVisible(),"an outside click cancels the delayed popup opening");
    tip.show();wait(10);pressArrow();check(tip.isHidden(),"panel-hide cancellation starts from an actual pending open");
    panel.hide();wait(120);
    check(!popup->isVisible(),"hiding the panel cancels the delayed popup opening");
    panel.show();wait(220);
    tip.show();wait(10);pressArrow();check(tip.isHidden(),"second-click cancellation starts from an actual pending open");
    pressArrow();wait(120);
    check(!popup->isVisible(),"a second arrow press cancels the delayed popup opening");
    tip.show();wait(10);pressArrow();check(tip.isHidden(),"mouse-leave cancellation starts from an actual pending open");
    QEvent leaveArrow(QEvent::Leave);QApplication::sendEvent(&arrow,&leaveArrow);wait(120);
    check(!popup->isVisible(),"leaving the arrow cancels a delayed mouse-triggered opening");
    tip.show();wait(10);QKeyEvent keyboardOpen(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier);
    QApplication::sendEvent(&arrow,&keyboardOpen);QApplication::sendEvent(&arrow,&leaveArrow);wait(280);
    check(popup->isVisible(),"keyboard opening survives an unrelated mouse leave");
    QApplication::sendEvent(popup,&escape);wait(400);
    // A root tray menu follows the clicked button, including async growth;
    // its nested menus retain their own native placement.
    click();check(popup->isVisible(),"popup reopens normally after cancelling pending requests");
    const QRect buttonAnchor(first.mapToGlobal(QPoint()),first.size());
    const QPointF buttonLocal=first.rect().center(),buttonGlobal=first.mapToGlobal(buttonLocal.toPoint());
    QMouseEvent rightPress(QEvent::MouseButtonPress,buttonLocal,buttonGlobal,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QMouseEvent rightRelease(QEvent::MouseButtonRelease,buttonLocal,buttonGlobal,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&first,&rightPress);QApplication::sendEvent(&first,&rightRelease);
    QMenu root;auto *pending=root.addAction("pending");auto *nested=root.addMenu("nested");auto *child=nested->addAction("child");
    root.popup(QPoint(25,25));wait(40);
    check(root.geometry().bottom()==buttonAnchor.top()-9,
          "a root context menu opens above the actual tray button");
    const QSize rootInitial=root.size();
    pending->setText("asynchronously loaded longer tray action");root.addAction("late item");wait(40);
    check(root.size()!=rootInitial && root.geometry().bottom()==buttonAnchor.top()-9,
          "async root menu resize remains attached above the clicked button");
    nested->popup(QPoint(40,80));wait(40);const QPoint childPosition=nested->pos();
    child->setText("asynchronously loaded nested action");wait(40);
    check(nested->isVisible() && nested->pos()==childPosition,
          "child menus keep their independent native placement after resizing");
    nested->hide();root.hide();wait(220);
    // A different click cancels a not-yet-matched async context-menu request.
    // The next unrelated menu must keep the position chosen by its own caller.
    if(!popup->isVisible())click();
    QApplication::sendEvent(&first,&rightPress);QApplication::sendEvent(&first,&rightRelease);
    QApplication::sendEvent(&panel,&outside);
    QMenu unrelated;unrelated.addAction("unrelated application menu");unrelated.popup(QPoint(25,25));wait(40);
    check(unrelated.pos()==QPoint(25,25),
          "a non-source press cancels unmatched menu anchoring for the next unrelated menu");
    unrelated.hide();wait(220);
    if(!popup->isVisible())click();
    // Exercise the real QDrag entry point, then deliver a drop from inside its
    // nested event loop. Fixture Ids provide stable persistence keys.
    bool dragObserved=false;
    const int beforeDragActivations=activated;
    const QPoint pressPoint=first.rect().center();
    QMouseEvent dragPress(QEvent::MouseButtonPress,QPointF(pressPoint),QPointF(first.mapToGlobal(pressPoint)),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&first,&dragPress);
    QTimer::singleShot(30,&first,[&]{
        auto *drag=first.findChild<QDrag *>();
        check(drag && drag->mimeData()->hasFormat("application/x-ukui-liquid-tray-button"),
              "hidden icon mouse movement enters QDrag with the dedicated tray format");
        const QPoint target=second.rect().center();
        QDragEnterEvent enter(target,Qt::MoveAction,drag->mimeData(),Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&second,&enter);
        check(enter.isAccepted(),"a hidden icon accepts the active tray drag");
        QDropEvent drop(QPointF(target),Qt::MoveAction,drag->mimeData(),Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&second,&drop);check(drop.isAccepted(),"the tray drag drop reorders hidden icons");
        dragObserved=true;QDrag::cancel();
    });
    const QPoint moved=pressPoint+QPoint(QApplication::startDragDistance()+8,0);
    QMouseEvent dragMove(QEvent::MouseMove,QPointF(moved),QPointF(first.mapToGlobal(moved)),
                         Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&first,&dragMove);wait(80);
    check(dragObserved,"the nested tray drag completes without activating the app");
    const int beforeRightRelease=first.rightReleases;
    const QPointF afterDragGlobal=first.mapToGlobal(buttonLocal.toPoint());
    QMouseEvent afterDragRightPress(QEvent::MouseButtonPress,buttonLocal,afterDragGlobal,
                                    Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QMouseEvent afterDragRightRelease(QEvent::MouseButtonRelease,buttonLocal,afterDragGlobal,
                                      Qt::RightButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&first,&afterDragRightPress);QApplication::sendEvent(&first,&afterDragRightRelease);
    check(first.rightReleases==beforeRightRelease+1,
          "the first right click after dragging reaches the original release handler");
    QMouseEvent dragRelease(QEvent::MouseButtonRelease,QPointF(moved),QPointF(first.mapToGlobal(moved)),
                            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&first,&dragRelease);wait(40);
    QSettings persisted(temp.path()+"/ukui/liquid-panel.ini",QSettings::IniFormat);
    check(persisted.value("tray/hiddenOrder").toStringList()==QStringList({"fixture-a","fixture-b"})
          && first.mapToGlobal(QPoint()).x()<second.mapToGlobal(QPoint()).x() && activated==beforeDragActivations && !first.isDown(),
          "drag order is persisted and releasing the drag does not activate the source app");
    QApplication::sendEvent(popup,&escape);wait(220);
    // Simulate an app being removed while the popup owns its live button.
    auto *gone=new StatusNotifierButton(&home);tray.insertWidget(0,gone);gone->hide();wait(80);
    check(home.property("liquidTrayHiddenCount").toInt()==3,"new hidden app enters the popup");
    delete gone;home.layout()->invalidate();wait(80);
    check(home.property("liquidTrayHiddenCount").toInt()==2,"removed app leaves no stale pointers");
    click();check(popup->isVisible(),"popup can reopen after an app exits");
    auto *liveGone=new StatusNotifierButton(&home);liveGone->setFixedSize(34,34);tray.insertWidget(0,liveGone);liveGone->hide();wait(80);
    delete liveGone;wait(120);const QRect settled=popup->geometry();
    wait(240);
    check(popup->isVisible() && popup->geometry()==settled && home.property("liquidTrayHiddenCount").toInt()==2,
          "unregistering an icon while open settles to stable popup geometry");
    panel.hide();wait(80);check(!popup->isVisible(),"hiding the panel closes the owned popup");
    // Stack-allocated fixtures must outlive their reparented QObject ownership.
    first.setParent(nullptr);second.setParent(nullptr);shown.setParent(nullptr);arrow.setParent(nullptr);
    task.setParent(nullptr);divider.setParent(nullptr);start.setParent(nullptr);home.setParent(nullptr);
    unref(settings);return 0;
}
#include "behavior_test.moc"
