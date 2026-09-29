#include "CalendarDesklet.h"
#include "DesktopCanvas.h"
#include "LiquidPopup.h"
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QCoreApplication>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QProcess>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QMenu>
#include <QWidgetAction>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QLocale>

namespace {
// A bounded five-row picker; it uses the shared menu material and placement.
class DateWheel final : public QWidget {
public:
    DateWheel(int current,int minimum,int maximum,const QString &suffix,QMenu *menu)
        :QWidget(menu),m_value(qBound(minimum,current,maximum)),m_min(minimum),m_max(maximum),m_suffix(suffix),m_menu(menu){
        setFixedSize(146,180);setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);
        setAccessibleName(suffix=="年"?"年份选择":"月份选择");
        setToolTip("滚轮或上下拖动选择，点击确认；Esc 取消");sync();
    }
    int value() const{return m_value;}
    bool cancelled=false;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);p.setBrush(QColor(154,232,219,40));p.drawRoundedRect(QRectF(8,73,width()-16,34),9,9);
        const QColor ink=m_menu->palette().color(QPalette::WindowText);
        for(int offset=-2;offset<=2;++offset){
            const int value=m_value+offset;if(value<m_min || value>m_max)continue;
            QFont f=font();f.setPixelSize(offset==0?19:qAbs(offset)==1?16:13);f.setBold(offset==0);p.setFont(f);
            QColor color=ink;color.setAlphaF(offset==0?1.:qAbs(offset)==1?.65:.32);p.setPen(color);
            p.drawText(QRectF(8,73+offset*34,width()-16,34),Qt::AlignCenter,QString::number(value)+" "+m_suffix);
        }
    }
    void wheelEvent(QWheelEvent *e) override {
        if(!e->pixelDelta().isNull()){
            m_pixels+=e->pixelDelta().y();const int steps=m_pixels/34;m_pixels-=steps*34;step(-steps);
        }else{
            m_angle+=e->angleDelta().y();const int steps=m_angle/120;m_angle-=steps*120;step(-steps);
        }
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if(e->button()!=Qt::LeftButton)return;
        m_startY=e->y();m_startValue=m_value;m_drag=false;e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if(!(e->buttons()&Qt::LeftButton))return;
        const int dy=e->y()-m_startY;if(qAbs(dy)>6)m_drag=true;
        if(m_drag){m_value=qBound(m_min,m_startValue-qRound(dy/34.),m_max);sync();}
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if(e->button()!=Qt::LeftButton)return;
        if(!m_drag){step(qBound(-2,(e->y()-5)/34-2,2));m_menu->close();}
        e->accept();
    }
    void keyPressEvent(QKeyEvent *e) override {
        switch(e->key()){
        case Qt::Key_Up:step(-1);break;
        case Qt::Key_Down:step(1);break;
        case Qt::Key_PageUp:step(-5);break;
        case Qt::Key_PageDown:step(5);break;
        case Qt::Key_Home:m_value=m_min;sync();break;
        case Qt::Key_End:m_value=m_max;sync();break;
        case Qt::Key_Return:case Qt::Key_Enter:m_menu->close();break;
        case Qt::Key_Escape:cancelled=true;m_menu->close();break;
        default:QWidget::keyPressEvent(e);return;
        }
        e->accept();
    }
private:
    void step(int delta){if(!delta)return;m_value=qBound(m_min,m_value+delta,m_max);sync();}
    void sync(){setProperty("selectedValue",m_value);setAccessibleDescription(QString::number(m_value)+m_suffix);update();}
    int m_value,m_min,m_max,m_angle=0,m_pixels=0,m_startY=0,m_startValue=0;
    bool m_drag=false;
    QString m_suffix;QMenu *m_menu;
};
int pickDateValue(QWidget *owner,QPushButton *anchor,const QString &name,int current,int minimum,int maximum,const QString &suffix){
    QMenu menu(owner);menu.setObjectName(name+"Menu");menu.setStyleSheet("QMenu{padding:0px;margin:0px;}");
    auto *wheel=new DateWheel(current,minimum,maximum,suffix,&menu);wheel->setObjectName(name+"Wheel");
    auto *action=new QWidgetAction(&menu);action->setDefaultWidget(wheel);menu.addAction(action);
    QObject::connect(&menu,&QMenu::aboutToShow,wheel,[wheel]{QTimer::singleShot(0,wheel,[wheel]{wheel->setFocus(Qt::PopupFocusReason);});});
    LiquidPopup::execAt(menu,anchor);
    return wheel->cancelled?current:wheel->value();
}
class AgendaDelegate final : public QStyledItemDelegate {
public:
    explicit AgendaDelegate(QObject *parent):QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override{return QSize(100,48);}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        p->save();p->setRenderHint(QPainter::Antialiasing);p->setClipRect(option.rect);
        if(option.state & (QStyle::State_Selected|QStyle::State_MouseOver)){
            p->setPen(Qt::NoPen);p->setBrush(QColor(154,232,219,35));p->drawRoundedRect(option.rect.adjusted(2,1,-2,-1),7,7);
        }
        const QStringList lines=index.data().toString().split('\n');
        QFont f=option.font;f.setPixelSize(10);p->setFont(f);p->setPen(QColor("#b8cbd6"));
        const QRect line=option.rect.adjusted(7,3,-7,-25);
        p->drawText(line,Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(f).elidedText(lines.value(0),Qt::ElideRight,line.width()));
        f.setPixelSize(12);p->setFont(f);p->setPen(QColor("#f4f7ff"));
        const QRect title=option.rect.adjusted(7,22,-7,-3);
        p->drawText(title,Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(f).elidedText(lines.mid(1).join(" "),Qt::ElideRight,title.width()));p->restore();
    }
};
}

CalendarDesklet::CalendarDesklet(DesktopCanvas *canvas):LiquidDesklet(canvas,"calendar","日历 · 系统待办",QSize(400,270)),
    m_selected(QDate::currentDate()),m_month(m_selected.year(),m_selected.month(),1),m_today(m_selected),
    m_reader(new QProcess(this)),m_watcher(new QFileSystemWatcher(this)) {
    m_database=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/.kylin_calendar_Schedule2.db";
    m_previous=button("‹","calendarPrevious");m_next=button("›","calendarNext");m_todayButton=button("今天","calendarToday");
    m_all=button("全部","calendarAll");m_open=button("系统日历","calendarOpen");
    m_yearButton=button("","calendarYear");m_monthButton=button("","calendarMonth");
    m_collapse=button("−","calendarCollapse");
    m_yearButton->setToolTip("选择年份");m_monthButton->setToolTip("选择月份");
    connect(m_yearButton,&QPushButton::clicked,this,&CalendarDesklet::showYearMenu);
    connect(m_monthButton,&QPushButton::clicked,this,&CalendarDesklet::showMonthMenu);
    connect(m_collapse,&QPushButton::clicked,this,[this]{setAgendaCollapsed(!m_agendaCollapsed);});
    m_previous->setToolTip("上个月");m_next->setToolTip("下个月");m_all->setToolTip("显示本月及今天附近的系统待办");
    m_list=new QListWidget(this);m_list->setObjectName("calendarAgenda");m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new AgendaDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet("QListWidget{background:transparent;color:#edf5fa;border:0;font-size:12px;}QListWidget::item{padding:6px;border-radius:7px;}QListWidget::item:selected{background:rgba(154,232,219,35);}QListWidget::item:hover{background:rgba(154,232,219,20);}QScrollBar:vertical{width:5px;background:transparent;}QScrollBar::handle:vertical{background:rgba(210,235,241,80);border-radius:2px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    connect(m_previous,&QPushButton::clicked,this,[this]{selectDate(m_month.addMonths(-1));});
    connect(m_next,&QPushButton::clicked,this,[this]{selectDate(m_month.addMonths(1));});
    connect(m_todayButton,&QPushButton::clicked,this,[this]{selectDate(QDate::currentDate());});
    connect(m_all,&QPushButton::clicked,this,[this]{m_allDates=true;updateList();update();});
    connect(m_open,&QPushButton::clicked,this,[this]{openSystemCalendar(m_selected);});
    connect(m_list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){const QDate d=item->data(Qt::UserRole).toDate();if(d.isValid())openSystemCalendar(d);});
    m_debounce.setSingleShot(true);m_debounce.setInterval(180);connect(&m_debounce,&QTimer::timeout,this,&CalendarDesklet::reload);
    auto changed=[this](const QString &){rewatch();m_debounce.start();};
    connect(m_watcher,&QFileSystemWatcher::fileChanged,this,changed);
    // Only react to database creation/replacement, not unrelated config writes.
    connect(m_watcher,&QFileSystemWatcher::directoryChanged,this,[this](const QString &){
        const QStringList files=m_watcher->files();
        for(const QString &p:{m_database,m_database+"-wal"})if(QFileInfo::exists(p)!=files.contains(p)){rewatch();m_debounce.start();break;}
    });
    m_timeout.setSingleShot(true);m_timeout.setInterval(3000);connect(&m_timeout,&QTimer::timeout,m_reader,&QProcess::kill);
    connect(m_reader,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int exit,QProcess::ExitStatus status){
        m_timeout.stop();QJsonParseError error;const auto document=QJsonDocument::fromJson(m_reader->readAllStandardOutput(),&error);
        if(exit==0 && status==QProcess::NormalExit && error.error==QJsonParseError::NoError && document.isObject()){
            m_items=document.object()["items"].toArray();m_warning=document.object()["warning"].toString();
            m_dates=document.object()["dates"].toObject();m_calendarWarning=document.object()["calendarWarning"].toString();
        }else m_warning="系统待办读取失败，保留上次内容";
        rewatch();updateList();if(isVisible())update();
        if(m_again){m_again=false;m_debounce.start();}
    });
    connect(m_reader,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){m_timeout.stop();m_warning="无法启动日历读取程序";updateList();update();}});
    m_dayTimer.setInterval(60000);connect(&m_dayTimer,&QTimer::timeout,this,[this]{
        const QDate now=QDate::currentDate();if(now!=m_today){bool followed=m_selected==m_today;m_today=now;if(followed){m_selected=now;m_month=QDate(now.year(),now.month(),1);}reload();updateDateButtons();updateList();update();}
    });
    QSettings settings;
    const bool collapsed=settings.value("desklets/calendar/agendaCollapsed",false).toBool();
    m_expandedHeight=qBound(390,(collapsed?settings.value("desklets/calendar/expandedHeight",390).toInt():height()),700);
    m_agendaCollapsed=collapsed;setAgendaCollapsed(collapsed);
    updateDateButtons();rewatch();arrangeControls();reload();
}
CalendarDesklet::~CalendarDesklet(){if(m_holidaySync){disconnect(m_holidaySync,nullptr,this,nullptr);m_holidaySync->kill();m_holidaySync->waitForFinished(500);}if(!m_agendaCollapsed)QSettings().setValue("desklets/calendar/expandedHeight",height());if(m_reader->state()!=QProcess::NotRunning){m_reader->kill();m_reader->waitForFinished(500);}}
void CalendarDesklet::rewatch(){
    QStringList wanted;for(const QString &p:{QFileInfo(m_database).absolutePath(),m_database,m_database+"-wal"})if(QFileInfo::exists(p))wanted<<p;
    const auto existing=m_watcher->files()+m_watcher->directories();for(const QString &p:wanted)if(!existing.contains(p))m_watcher->addPath(p);
}
void CalendarDesklet::reload(){
    if(m_reader->state()!=QProcess::NotRunning){m_again=true;return;}
    const QDate begin=qMin(m_month,QDate::currentDate().addDays(-30));
    const QDate end=qMax(m_month.addMonths(1).addDays(-1),QDate::currentDate().addDays(90));
    // Limit expansion for distant navigation; today's tasks are available via Today.
    const bool distant=begin.daysTo(end)>400;
    QString script=QCoreApplication::applicationDirPath()+"/calendar_snapshot.py";
    if(!QFileInfo::exists(script))script=QString::fromUtf8(UKUI_FENCES_CALENDAR_READER_PATH);
    m_reader->start("/usr/bin/python3",{script,"--database",m_database,"--begin",(distant?m_month:begin).toString(Qt::ISODate),"--end",(distant?m_month.addMonths(1).addDays(-1):end).toString(Qt::ISODate)});m_timeout.start();
}
void CalendarDesklet::selectDate(QDate d){if(!d.isValid())return;const bool month=d.month()!=m_month.month() || d.year()!=m_month.year();m_selected=d;m_month=QDate(d.year(),d.month(),1);m_allDates=false;if(month)reload();updateDateButtons();updateList();update();}
QRect CalendarDesklet::gridRect() const{return QRect(width()*.34,60,width()-int(width()*.34)-16,164);}
QRect CalendarDesklet::dateCell(int day) const{
    const QRect grid=gridRect();const int offset=m_month.dayOfWeek()-1,index=offset+day-1,weeks=(offset+m_month.daysInMonth()+6)/7;
    const int left=grid.x()+index%7*grid.width()/7,right=grid.x()+(index%7+1)*grid.width()/7;
    const int top=grid.y()+index/7*grid.height()/weeks,bottom=grid.y()+(index/7+1)*grid.height()/weeks;
    return QRect(left,top,right-left,bottom-top);
}
void CalendarDesklet::arrangeControls(){
    if(!m_list)return;
    m_previous->setGeometry(16,16,28,25);m_next->setGeometry(width()-78,16,28,25);
    m_yearButton->setGeometry(width()/2-92,16,96,25);m_monthButton->setGeometry(width()/2+10,16,66,25);
    m_collapse->setGeometry(width()-44,16,28,25);
    m_todayButton->setGeometry(32,196,66,26);
    m_all->setGeometry(width()-160,236,48,24);m_open->setGeometry(width()-105,236,89,24);
    m_list->setGeometry(12,266,width()-24,qMax(0,height()-294));
}
void CalendarDesklet::updateDateButtons(){
    m_yearButton->setText(QString::number(m_month.year())+" 年 ▾");
    m_monthButton->setText(QString::number(m_month.month())+" 月 ▾");
}
void CalendarDesklet::setAgendaCollapsed(bool collapsed){
    if(collapsed && !m_agendaCollapsed)m_expandedHeight=height();
    m_agendaCollapsed=collapsed;
    // Keep a separate expanded height so repeated toggles and restarts restore it.
    setMinimumHeight(collapsed?270:390);setMaximumHeight(collapsed?270:700);
    resize(width(),collapsed?270:m_expandedHeight);
    move(x(),qBound(0,y(),qMax(0,m_canvas->height()-height())));
    for(QWidget *w:{static_cast<QWidget *>(m_list),static_cast<QWidget *>(m_all),static_cast<QWidget *>(m_open)})w->setVisible(!collapsed);
    m_collapse->setText(collapsed?"＋":"−");
    m_collapse->setToolTip(collapsed?"展开系统待办":"收起系统待办");m_collapse->setAccessibleName(m_collapse->toolTip());
    QSettings settings;settings.setValue("desklets/calendar/agendaCollapsed",collapsed);
    settings.setValue("desklets/calendar/expandedHeight",m_expandedHeight);settings.sync();
    arrangeControls();savePlacement();update();
}
void CalendarDesklet::showMonthMenu(){
    const int month=pickDateValue(this,m_monthButton,"calendarMonth",m_month.month(),1,12,"月");
    if(month==m_month.month())return;
    const QDate first(m_month.year(),month,1);
    selectDate(QDate(first.year(),first.month(),qMin(m_selected.day(),first.daysInMonth())));
}
void CalendarDesklet::showYearMenu(){
    const int year=pickDateValue(this,m_yearButton,"calendarYear",m_month.year(),1900,2100,"年");
    if(year==m_month.year())return;
    const QDate month(year,m_month.month(),1);
    selectDate(QDate(month.year(),month.month(),qMin(m_selected.day(),month.daysInMonth())));
}
void CalendarDesklet::updateList(){
    if(!m_list)return;const int scroll=m_list->verticalScrollBar()->value();m_list->clear();
    for(const auto &value:m_items){const auto o=value.toObject();const QDate date=QDate::fromString(o["date"].toString(),Qt::ISODate),end=QDate::fromString(o["endDate"].toString(),Qt::ISODate);
        if(!m_allDates && (date>m_selected || end<m_selected))continue;
        QString prefix=date.toString("MM-dd")+"  "+o["time"].toString();if(end<QDate::currentDate())prefix+=" · 已过日期";
        auto *item=new QListWidgetItem(prefix+"\n"+o["title"].toString(),m_list);item->setData(Qt::UserRole,m_allDates?date:m_selected);item->setToolTip(o["title"].toString()+"\n点击在系统日历查看/编辑");
    }
    if(m_list->count()==0){auto *item=new QListWidgetItem(m_warning.isEmpty()?"这一天没有系统待办":m_warning,m_list);item->setFlags(Qt::NoItemFlags);}
    m_list->verticalScrollBar()->setValue(scroll);
}
void CalendarDesklet::paintContent(QPainter &p){
    if(!m_list)return;const QColor ink("#f4f7ff"),muted("#bacbd8"),accent("#9ae8db");
    text(p,QRectF(12,66,width()*.29,55),QString::number(m_selected.day()),42,ink,true);
    text(p,QRectF(12,120,width()*.29,22),QLocale(QLocale::Chinese).dayName(m_selected.dayOfWeek(),QLocale::LongFormat),12,muted);
    const auto selected=m_dates.value(m_selected.toString(Qt::ISODate)).toObject();
    text(p,QRectF(12,144,width()*.29,20),selected["lunar"].toString(),12,accent);
    QString detail=selected["festival"].toString();
    if(selected.contains("off"))detail+=(detail.isEmpty()?selected["holiday"].toString():QString())+(selected["off"].toBool()?" · 休":" · 班");
    if(detail.isEmpty())detail=selected["yearName"].toString();
    text(p,QRectF(12,168,width()*.29,20),detail,10,muted);
    const QRect grid=gridRect();const QStringList labels={"一","二","三","四","五","六","日"};
    for(int col=0;col<7;++col)text(p,QRectF(grid.x()+col*grid.width()/7,42,grid.width()/7,18),labels[col],10,col>=5?accent:muted);
    for(int d=1;d<=m_month.daysInMonth();++d){const QDate date(m_month.year(),m_month.month(),d);const QRect cell=dateCell(d);const QPoint center(cell.center().x(),cell.top()+8);
        const auto info=m_dates.value(date.toString(Qt::ISODate)).toObject();
        if(date==m_selected){p.setPen(Qt::NoPen);p.setBrush(QColor(154,232,219,55));p.drawRoundedRect(QRectF(cell).adjusted(2,1,-2,-1),7,7);}
        if(date==m_today){p.setPen(Qt::NoPen);p.setBrush(accent);p.drawEllipse(QPointF(center),8,8);}
        text(p,QRectF(cell.x(),cell.y(),cell.width(),16),QString::number(d),11,date==m_today?QColor("#203c3a"):ink,date==m_today);
        text(p,QRectF(cell.x(),cell.y()+16,cell.width(),11),info["label"].toString(),9,info["festival"].toString().isEmpty()?muted:accent);
        if(info.contains("off")){
            const bool off=info["off"].toBool();
            text(p,QRectF(cell.right()-9,cell.top(),10,11),off?"休":"班",8,off?QColor("#ffb6a9"):QColor("#f3d39a"));
        }
        bool marked=false;for(const auto &v:m_items){auto o=v.toObject();if(date>=QDate::fromString(o["date"].toString(),Qt::ISODate) && date<=QDate::fromString(o["endDate"].toString(),Qt::ISODate)){marked=true;break;}}
        if(marked){p.setPen(Qt::NoPen);p.setBrush(accent);p.drawEllipse(QPointF(cell.left()+3,cell.top()+8),1.4,1.4);}
    }
    if(!m_agendaCollapsed){
    p.setPen(QColor(230,245,250,35));p.drawLine(16,230,width()-16,230);
    text(p,QRectF(18,236,width()-184,24),m_allDates?QString("系统待办 · %1 项").arg(m_items.size()):m_selected.toString("M月d日 · 待办"),12,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    }
    QString footer=m_agendaCollapsed?"休：放假  班：调休上班":"休：放假  班：调休上班 · 点击事项查看";
    if(!selected.isEmpty() && !selected["scheduleKnown"].toBool())footer=QString::number(m_selected.year())+" 年放假调休数据尚未收录";
    if(!m_calendarWarning.isEmpty())footer=m_calendarWarning;
    if(!m_warning.isEmpty())footer=m_warning;
    text(p,QRectF(16,height()-25,width()-32,18),footer,10,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
}
void CalendarDesklet::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton){for(int d=1;d<=m_month.daysInMonth();++d)if(dateCell(d).contains(e->pos())){selectDate(QDate(m_month.year(),m_month.month(),d));e->accept();return;}}LiquidDesklet::mousePressEvent(e);}
void CalendarDesklet::mouseMoveEvent(QMouseEvent *e){
    LiquidDesklet::mouseMoveEvent(e);QString tip;
    for(int day=1;day<=m_month.daysInMonth();++day)if(dateCell(day).contains(e->pos())){
        const QDate date(m_month.year(),m_month.month(),day);const auto info=m_dates.value(date.toString(Qt::ISODate)).toObject();
        tip=date.toString("yyyy-MM-dd")+"  "+info["yearName"].toString()+info["lunar"].toString();
        if(!info["festival"].toString().isEmpty())tip+="\n"+info["festival"].toString();
        if(info.contains("off"))tip+="\n"+info["holiday"].toString()+(info["off"].toBool()?" 放假":" 调休上班");
        else if(!info.isEmpty() && !info["scheduleKnown"].toBool())tip+="\n本年放假调休数据尚未收录";
        break;
    }
    if(toolTip()!=tip)setToolTip(tip);
}
void CalendarDesklet::showEvent(QShowEvent *e){LiquidDesklet::showEvent(e);m_dayTimer.start();reload();}
void CalendarDesklet::hideEvent(QHideEvent *e){m_dayTimer.stop();LiquidDesklet::hideEvent(e);}
void CalendarDesklet::openSystemCalendar(QDate d){
    auto msg=QDBusMessage::createMethodCall("org.ukui.calendar.Notifications","/org/ukui/calendar/Notifications","org.ukui.calendar.Notifications","OpenSchedule");msg<<d.day()<<d.month()<<d.year();
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w]{QDBusPendingReply<> r=*w;w->deleteLater();if(r.isError()){m_warning="系统日历暂未响应，请从任务栏打开";update();}});
}
void CalendarDesklet::syncCalendarData()
{
    if(m_holidaySync) return;
    QString script=QCoreApplication::applicationDirPath()+"/holiday_sync.py";
    if(!QFileInfo::exists(script)) script=QFileInfo(QString::fromUtf8(UKUI_FENCES_CALENDAR_READER_PATH)).dir().filePath("holiday_sync.py");
    auto *process=new QProcess(this);m_holidaySync=process;
    m_calendarWarning="正在同步农历和节假日…";update();
    auto *timeout=new QTimer(process);timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,process,&QProcess::kill);
    auto finish=[this,process,timeout](bool started) {
        if(m_holidaySync!=process) return;
        timeout->stop();m_holidaySync=nullptr;
        const auto result=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        const QString message=result["message"].toString();
        m_calendarWarning=message.isEmpty()?"同步失败，保留原农历与节假日数据":message;
        reload();update();
        auto *notice=new QMessageBox(QMessageBox::Information,"农历和节假日同步",
            message.isEmpty() ? (started?"网络或数据源暂不可用，原数据已保留。":"无法启动同步程序，原数据已保留。")
                              : "农历按系统 ICU 重新计算。\n"+message,
            QMessageBox::Ok,this);
        notice->setAttribute(Qt::WA_DeleteOnClose);notice->open();process->deleteLater();
    };
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int,QProcess::ExitStatus){finish(true);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(false);});
    const int current=QDate::currentDate().year();
    process->start("/usr/bin/python3",{script,"--year",QString::number(m_selected.year()),"--year",QString::number(current),"--year",QString::number(current+1)});
    timeout->start(30000);
}
void CalendarDesklet::extendMenu(QMenu &menu)
{
    connect(menu.addAction("打开系统日历"),&QAction::triggered,this,[this]{openSystemCalendar(m_selected);});
    connect(menu.addAction("刷新系统待办"),&QAction::triggered,this,&CalendarDesklet::reload);
    auto *sync=menu.addAction(m_holidaySync?"正在同步农历和节假日…":"同步最新农历和节假日");
    sync->setObjectName("calendarSyncHolidays");sync->setEnabled(!m_holidaySync);
    sync->setToolTip("重新计算农历，从 holiday-cn 同步当前、所选及下一年的已发布调休数据；失败保留原数据。");
    connect(sync,&QAction::triggered,this,&CalendarDesklet::syncCalendarData);
}
