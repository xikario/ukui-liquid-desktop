#ifdef KeyPress
#undef KeyPress
#endif
#include "CalendarDesklet.h"
#include <QListWidget>
#include <QProcess>
static int runCalendarTest(const QString &root){
    QSettings settings;settings.setValue("smartSpace/autoStart",false);settings.setValue("systemMonitor/autoStart",false);settings.sync();
    const QString database=root+"/config/.kylin_calendar_Schedule2.db";QDir().mkpath(root+"/config");
    auto sql=[&](const QString &statement){return QProcess::execute("/usr/bin/python3",{"-c","import sqlite3,sys; c=sqlite3.connect(sys.argv[1]);c.executescript(sys.argv[2]);c.commit();c.close()",database,statement})==0;};
    check(sql("CREATE TABLE Schedule(id,start_date,end_date,start_hour,start_minute,descript,repeat,isAllDay,isLunar,beginrepeat,endrepeat);"),"isolated native calendar database created");
    const QString today=QDate::currentDate().toString(Qt::ISODate);
    check(sql(QString("INSERT INTO Schedule VALUES('one','%1','%1','17','30','系统待办测试','不重复',0,0,'','');").arg(today)),"native schedule fixture inserted");
    {
        DesktopCanvas canvas;canvas.show();canvas.setCalendarWidgetVisible(true);settle(600);
        auto *calendar=canvas.findChild<CalendarDesklet *>();check(calendar && calendar->scheduleCount()==1,"calendar displays existing system schedules");if(!calendar)return 1;
        auto *list=calendar->findChild<QListWidget *>("calendarAgenda");check(list && list->count()==1 && list->item(0)->text().contains("系统待办测试"),"agenda shows native task title and time");
        canvas.setClockWidgetVisible(true);canvas.setActivityWidgetVisible(true);canvas.setMusicWidgetVisible(true);settle(100);
        check(canvas.findChildren<QPushButton *>("deskletClose").isEmpty(),"all new desklets have no close X button");
        bool foundSync=false;
        QTimer::singleShot(80,calendar,[&]{
            auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if(menu) {auto *action=menu->findChild<QAction *>("calendarSyncHolidays");
                foundSync=action && menu->actions().contains(action) && action->isEnabled();menu->close();}
        });
        QContextMenuEvent menuEvent(QContextMenuEvent::Mouse,QPoint(20,20),calendar->mapToGlobal(QPoint(20,20)));
        QApplication::sendEvent(calendar,&menuEvent);
        check(foundSync,"calendar context menu exposes enabled holiday synchronization action");
        const int builds=calendar->materialBuilds();
        check(sql(QString("INSERT INTO Schedule VALUES('two','%1','%1','18','0','新增待办','不重复',0,0,'','');").arg(today)),"external system calendar update written");settle(600);
        check(calendar->scheduleCount()==2 && calendar->materialBuilds()==builds,"database watcher updates tasks without rebuilding glass");
        QFile::copy(database,database+".replacement");
        check(sql("DELETE FROM Schedule WHERE id='two';"),"native deletion fixture applied");settle(450);
        check(calendar->scheduleCount()==1,"native calendar deletion is reflected");
        QFile::remove(database);QFile::rename(database+".replacement",database);settle(600);
        check(calendar->scheduleCount()==2,"atomic database replacement reattaches the file watch");
        calendar->selectDate(QDate::currentDate().addDays(1));check(!list->item(0)->data(Qt::UserRole).isValid(),"date selection filters native schedules");
        calendar->findChild<QPushButton *>("calendarAll")->click();check(list->count()==2,"all tasks view restores the native agenda");
        calendar->selectDate(QDate(2026,9,25));settle(500);
        // Five-row month: inspect lunar/holiday tooltip on Mid-Autumn.
        QMouseEvent hover(QEvent::MouseMove,QPointF(294,174),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(calendar,&hover);
        check(calendar->toolTip().contains("八月十五") && calendar->toolTip().contains("放假"),"lunar festival and official holiday reach the calendar UI");
        const QString output=QDir::currentPath()+"/../test-results/calendar-"+QString::number(calendar->devicePixelRatioF());QDir().mkpath(output);
        calendar->grab().save(output+"/lunar-september.png");
        calendar->selectDate(QDate(2026,8,1));settle(500);calendar->grab().save(output+"/lunar-six-weeks.png");
        calendar->selectDate(QDate::currentDate());settle(500);
        for(auto *widget:canvas.findChildren<LiquidDesklet *>())widget->grab().save(output+"/"+widget->objectName()+".png");
        auto *collapse=calendar->findChild<QPushButton *>("calendarCollapse");
        calendar->resize(calendar->width(),450);const QPoint expandedPos=calendar->pos();
        collapse->click();settle(150);
        check(calendar->height()==270 && !list->isVisible() && !calendar->findChild<QPushButton *>("calendarAll")->isVisible(),"collapse hides agenda and shrinks card");
        check(calendar->pos()==expandedPos && calendar->minimumHeight()==270,"collapse preserves position");
        calendar->grab().save(output+"/collapsed.png");
        collapse->click();settle(150);
        check(calendar->height()==450 && list->isVisible(),"expand restores custom expanded height and tasks");
        auto choose=[&](const QString &buttonName,const QString &menuName,int value){
            QTimer::singleShot(160,calendar,[&,menuName,value]{
                auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
                check(menu && menu->isVisible(),"date selector opens visible menu");
                if(!menu)return;
                check(menu->property("liquidPopupSkin").toBool(),"date selector uses shared liquid popup");
                menu->grab().save(output+"/"+menuName+".png");
                auto *wheel=menu->findChild<QWidget *>(menuName.left(menuName.size()-4)+"Wheel");
                check(wheel && wheel->height()==180 && menu->height()<=190,"picker footprint is bounded to five rows");
                if(!wheel){menu->close();return;}
                int current=wheel->property("selectedValue").toInt();
                while(current!=value){
                    QKeyEvent key(QEvent::KeyPress,current<value?Qt::Key_Down:Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(wheel,&key);
                    const int next=wheel->property("selectedValue").toInt();if(next==current)break;current=next;
                }
                check(current==value,"wheel scrolls to requested value");
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(wheel,&enter);
            });
            // Prevent a test failure from leaving a nested menu loop running.
            QTimer::singleShot(1000,calendar,[&,menuName]{if(auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget()))menu->close();});
            calendar->findChild<QPushButton *>(buttonName)->click();
        };
        calendar->selectDate(QDate(2028,2,29));settle(400);
        choose("calendarYear","calendarYearMenu",2027);
        check(calendar->selectedDate()==QDate(2027,2,28),"year menu clamps leap day to valid day");
        calendar->selectDate(QDate(2026,1,31));settle(400);
        choose("calendarMonth","calendarMonthMenu",2);
        check(calendar->selectedDate()==QDate(2026,2,28),"month menu clamps day 31 to February end");
        check(calendar->findChild<QPushButton *>("calendarYear")->text().contains("2026") && calendar->findChild<QPushButton *>("calendarMonth")->text().startsWith("2 "),"date header follows selection");
        const QDate beforeCancel=calendar->selectedDate();
        QTimer::singleShot(160,calendar,[&]{
            auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            auto *wheel=menu?menu->findChild<QWidget *>("calendarMonthWheel"):nullptr;
            check(wheel!=nullptr,"wheel supports direct scrolling and dragging");if(!wheel){if(menu)menu->close();return;}
            const QPointF center(wheel->rect().center());
            QWheelEvent scroll(center,wheel->mapToGlobal(center.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(wheel,&scroll);check(wheel->property("selectedValue").toInt()==3,"one wheel notch advances one month");
            QMouseEvent press(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(wheel,&press);
            QMouseEvent drag(QEvent::MouseMove,center-QPointF(0,34),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(wheel,&drag);
            QMouseEvent release(QEvent::MouseButtonRelease,center-QPointF(0,34),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(wheel,&release);
            check(wheel->property("selectedValue").toInt()==4 && menu->isVisible(),"drag advances and keeps wheel open");
            QKeyEvent end(QEvent::KeyPress,Qt::Key_End,Qt::NoModifier);QApplication::sendEvent(wheel,&end);QApplication::sendEvent(wheel,&scroll);
            check(wheel->property("selectedValue").toInt()==12,"wheel clamps at December");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(wheel,&escape);
        });
        calendar->findChild<QPushButton *>("calendarMonth")->click();
        check(calendar->selectedDate()==beforeCancel,"Escape cancels draft selection");
        calendar->selectDate(QDate::currentDate());settle(500);
        const QRect original=calendar->geometry();
        auto drag=[&]{QPoint l(90,20),g=calendar->mapToGlobal(l);QPoint delta=QPoint(10,10)-calendar->pos();
            QMouseEvent a(QEvent::MouseButtonPress,l,g,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(calendar,&a);
            QMouseEvent b(QEvent::MouseMove,l+delta,g+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(calendar,&b);
            QMouseEvent c(QEvent::MouseButtonRelease,l,g+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(calendar,&c);};
        drag();check(calendar->geometry()==original,"calendar remains locked outside editing");canvas.setGlobalEditMode(true);drag();settle(150);
        check(calendar->pos()==QPoint(0,0),"calendar supports edit mode and edge snapping");canvas.setGlobalEditMode(false);
        bool avoids=true;for(auto *icon:canvas.findChildren<DesktopIcon *>(QString(),Qt::FindDirectChildrenOnly))if(icon->isVisible() && icon->geometry().intersects(calendar->geometry().adjusted(-8,-8,8,8)))avoids=false;
        check(avoids,"desktop icons avoid calendar geometry");
        calendar->grab().save(output+"/calendar.png");
        canvas.setCalendarWidgetVisible(false);check(!calendar->isVisible(),"calendar can be hidden through desktop widget menu action");canvas.setCalendarWidgetVisible(true);
        collapse->click();LiquidDesklet::setAutoStart("calendar",true);calendar->savePlacement();
    }
    {DesktopCanvas canvas;canvas.show();settle(1900);auto *calendar=canvas.findChild<CalendarDesklet *>();check(calendar && calendar->isVisible() && calendar->pos()==QPoint(0,0) && calendar->scheduleCount()==2,"calendar restarts with its placement and system tasks");
        check(calendar && calendar->height()==270 && !calendar->findChild<QListWidget *>("calendarAgenda")->isVisible(),"collapsed state survives restart");
        if(calendar){calendar->findChild<QPushButton *>("calendarCollapse")->click();check(calendar->height()==450,"expanded custom height survives restart");}
    }
    return failures?1:0;
}
