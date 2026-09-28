#include "DesktopWidgets.h"
#include "DesktopCanvas.h"
#include "ActivityRecorder.h"
#include "LiquidOpticsRenderer.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QLocale>
#include <QtMath>
#include <algorithm>
#include <climits>

namespace {
const QColor ink("#f4f7ff"), muted("#b8c6d9"), accent("#9ae8db");
QString duration(qint64 ms) {
    const qint64 minutes = ms/60000;
    if(minutes < 1) return QStringLiteral("不足 1 分钟");
    if(minutes < 60) return QString::number(minutes)+QStringLiteral(" 分钟");
    return QStringLiteral("%1 小时 %2 分").arg(minutes/60).arg(minutes%60);
}
QString timerText(qint64 ms) {
    const auto seconds = (ms+999)/1000;
    if(seconds>=3600) return QStringLiteral("%1:%2:%3").arg(seconds/3600).arg(seconds/60%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
    return QStringLiteral("%1:%2").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
}
}
LiquidDesklet::LiquidDesklet(DesktopCanvas *canvas, const QString &key, const QString &title, QSize initial)
    : QWidget(canvas), m_canvas(canvas), m_key(key), m_title(title), m_optics(new LiquidOpticsRenderer) {
    setObjectName(key+"Desklet"); setWindowTitle(title); setAccessibleName(title);
    setAttribute(Qt::WA_TranslucentBackground); setAutoFillBackground(false); setMouseTracking(true);
    setMinimumSize(initial); setMaximumSize(800,700);
    QSettings settings; settings.beginGroup("desklets/"+key);
    const QRect saved=settings.value("geometry").toRect();
    const QSize oldDefault = key=="clock" ? QSize(310,358) : QSize(410,358);
    resize(saved.isValid() && ((key!="clock" && key!="activity") || saved.size()!=oldDefault) ? saved.size() : initial);
    if(saved.isValid()) move(saved.topLeft());
    else {
        const QPoint preferred(80+(key=="activity"?244:0),qMax(72,canvas->height()/6));
        const QRect area=canvas->rect().adjusted(16,48,-16,-80);
        QList<QRect> occupied;
        for(auto *child:canvas->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly)) {
            if(child==this || !child->isVisible() || child->isWindow())continue;
            if(child->inherits("SmartSpaceWidget") && canvas->smartSpaceEdgeHidden())continue;
            if(child->inherits("FenceWidget") || child->inherits("SystemMonitor")
                || child->inherits("SmartSpaceWidget") || child->inherits("LiquidDesklet")
                || child->inherits("DesktopIcon"))occupied.append(child->geometry().adjusted(-12,-12,12,12));
        }
        auto free=[&](QPoint pos){const QRect candidate(pos,size());if(!area.contains(candidate))return false;
            for(const QRect &rect:occupied)if(rect.intersects(candidate))return false;return true;};
        QPoint selected=preferred;qint64 best=LLONG_MAX;
        if(!free(preferred))for(int y=area.top();y+height()<=area.bottom()+1;y+=24)
            for(int x=area.left();x+width()<=area.right()+1;x+=24) {
                const QPoint candidate(x,y);qint64 score=(candidate-preferred).manhattanLength();
                for(const QRect &rect:occupied){const QRect overlap=rect.intersected(QRect(candidate,size()));
                    score+=qint64(overlap.width())*overlap.height()*100000;}
                if(score<best){selected=candidate;best=score;}
            }
        move(selected);
    }
    constrainToCanvas();
    m_materialTimer.setSingleShot(true); m_materialTimer.setInterval(100);
    connect(&m_materialTimer,&QTimer::timeout,this,[this]{if(isVisible())rebuildMaterial();});
    m_saveTimer.setSingleShot(true);m_saveTimer.setInterval(350);
    connect(&m_saveTimer,&QTimer::timeout,this,&LiquidDesklet::savePlacement);
    connect(canvas,&DesktopCanvas::wallpaperChanged,this,&LiquidDesklet::invalidateMaterial);
    connect(qApp,&QCoreApplication::aboutToQuit,this,&LiquidDesklet::savePlacement);
    m_ready=true;canvas->installEventFilter(this);
}
LiquidDesklet::~LiquidDesklet() { savePlacement(); }
bool LiquidDesklet::autoStartEnabled(const QString &key) {return QSettings().value("desklets/"+key+"/autoStart",false).toBool();}
void LiquidDesklet::setAutoStart(const QString &key,bool on) {QSettings s;s.setValue("desklets/"+key+"/autoStart",on);s.sync();}
QPushButton *LiquidDesklet::button(const QString &label,const QString &name) {
    auto *b=new QPushButton(label,this);QFont buttonFont=font();buttonFont.setPixelSize(12);b->setFont(buttonFont);b->setObjectName(name);b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet("QPushButton {color:#eaf3ff;background:rgba(222,239,255,19);border:1px solid rgba(235,245,255,28);border-radius:9px;padding:3px;}"
                     "QPushButton:hover {background:rgba(190,238,241,48);border-color:rgba(224,255,253,95);}"
                     "QPushButton:pressed,QPushButton:checked {background:rgba(123,213,208,74);}"
                     "QPushButton:disabled {color:rgba(219,231,247,85);background:rgba(225,242,255,9);}");
    return b;
}
void LiquidDesklet::text(QPainter &p,const QRectF &r,const QString &s,int size,const QColor &c,bool bold,int align) const {
    QFont f=font();f.setPixelSize(size);f.setBold(bold);p.setFont(f);p.setPen(c);p.drawText(r,align,s);
}
void LiquidDesklet::setEditMode(bool enabled) {
    m_editMode=enabled;
    if(!enabled){m_drag=m_resize=false;if(QWidget::mouseGrabber()==this)releaseMouse();}
    setCursor(Qt::ArrowCursor);
    update();
}
void LiquidDesklet::savePlacement(){if(m_ready){QSettings s;s.setValue("desklets/"+m_key+"/geometry",geometry());s.sync();}}
QPoint LiquidDesklet::boundedPosition(const QPoint &position, bool snap) const {
    // Child widgets use the same full desktop bounds as the older desklets.
    const QRect bounds=m_canvas->rect();
    const int maxX=qMax(bounds.left(),bounds.right()-width()+1);
    const int maxY=qMax(bounds.top(),bounds.bottom()-height()+1);
    QPoint result(qBound(bounds.left(),position.x(),maxX),
                  qBound(bounds.top(),position.y(),maxY));
    if(!snap)return result;

    constexpr int screenDistance=32, peerDistance=12;
    const auto nearestEdge=[](int value,int first,int last){
        return qAbs(value-first)<=qAbs(value-last)?first:last;
    };
    const int edgeX=nearestEdge(result.x(),bounds.left(),maxX);
    const int edgeY=nearestEdge(result.y(),bounds.top(),maxY);
    const bool screenX=qAbs(result.x()-edgeX)<=screenDistance;
    const bool screenY=qAbs(result.y()-edgeY)<=screenDistance;
    if(screenX)result.setX(edgeX);
    if(screenY)result.setY(edgeY);

    // Use exclusive right/bottom edges so touching cards never overlap by one pixel.
    const QRect proposed(result,size());
    int bestX=peerDistance+1,bestY=peerDistance+1;
    auto candidate=[](int delta,int value,int min,int max,int &best){
        if(value+delta>=min && value+delta<=max && qAbs(delta)<qAbs(best))best=delta;
    };
    for(auto *peer:m_canvas->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly)) {
        if(peer==this || !peer->isVisible())continue;
        if(!(peer->inherits("LiquidDesklet") || peer->inherits("FenceWidget")
             || peer->inherits("SystemMonitor") || peer->inherits("SmartSpaceWidget")))continue;
        if(peer->inherits("SmartSpaceWidget") && m_canvas->smartSpaceEdgeHidden())continue;
        const QRect target(peer->isWindow()?m_canvas->mapFromGlobal(peer->mapToGlobal(QPoint())):peer->pos(),peer->size());
        const int left=target.x(),right=target.x()+target.width();
        const int top=target.y(),bottom=target.y()+target.height();
        const bool nearY=proposed.y()<=bottom+peerDistance && proposed.y()+height()>=top-peerDistance;
        const bool nearX=proposed.x()<=right+peerDistance && proposed.x()+width()>=left-peerDistance;
        if(!screenX && nearY)for(int x:{left-width(),right,left,right-width()})
            candidate(x-result.x(),result.x(),bounds.left(),maxX,bestX);
        if(!screenY && nearX)for(int y:{top-height(),bottom,top,bottom-height()})
            candidate(y-result.y(),result.y(),bounds.top(),maxY,bestY);
    }
    if(qAbs(bestX)<=peerDistance)result.rx()+=bestX;
    if(qAbs(bestY)<=peerDistance)result.ry()+=bestY;
    return result;
}
void LiquidDesklet::constrainToCanvas() {
    // Restoring or showing a widget must not re-snap a saved custom position.
    move(boundedPosition(pos(),false));
}
void LiquidDesklet::reveal(){constrainToCanvas();show();raise();invalidateMaterial();}
void LiquidDesklet::invalidateMaterial(){m_materialDirty=true;if(isVisible())m_materialTimer.start();}
void LiquidDesklet::rebuildMaterial() {
    m_materialTimer.stop();m_materialDirty=false;
    const qreal dpr=devicePixelRatioF();
    auto wallpaper=m_canvas->wallpaperBackdrop(QRect(mapToGlobal(QPoint()),size()),dpr);
    if(wallpaper.isNull()) {wallpaper=QImage(QSize(qRound(width()*dpr),qRound(height()*dpr)),QImage::Format_RGB32);wallpaper.setDevicePixelRatio(dpr);wallpaper.fill(QColor("#344257"));}
    const auto &theme=LiquidPopup::theme();
    m_optics->setOptics(theme.refraction,theme.tint,theme.highlight,1.0);
    m_optics->setWallpaper(wallpaper);
    m_material=m_optics->renderPanel(QRect(QPoint(),size()),qMax(16.,theme.radius));
    ++m_materialBuilds;setProperty("liquidMaterialBuilds",m_materialBuilds);setProperty("liquidOpticalGpu",m_optics->usedGpu());
    update();
}
void LiquidDesklet::paintEvent(QPaintEvent *) {
    if(m_material.isNull() || m_material.devicePixelRatio()!=devicePixelRatioF())rebuildMaterial();
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.drawImage(QRectF(rect()),m_material,QRectF(m_material.rect()));
    paintContent(p);
    if(m_editMode) {
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(154,232,219,170),1,Qt::DashLine));
        p.drawRoundedRect(QRectF(rect()).adjusted(2,2,-2,-2),16,16);
        p.setPen(QPen(QColor(220,239,255,180),1.2,Qt::SolidLine,Qt::RoundCap));
        for(int i=0;i<3;++i)p.drawLine(QPointF(width()-12-i*4,height()-12),QPointF(width()-12,height()-12-i*4));
    }
}
void LiquidDesklet::resizeEvent(QResizeEvent *e){QWidget::resizeEvent(e);if(m_ready){arrangeControls();invalidateMaterial();m_saveTimer.start();}}
void LiquidDesklet::moveEvent(QMoveEvent *e){QWidget::moveEvent(e);if(m_ready){invalidateMaterial();m_saveTimer.start();}}
void LiquidDesklet::showEvent(QShowEvent *e){QWidget::showEvent(e);arrangeControls();if(m_materialDirty)rebuildMaterial();}
void LiquidDesklet::mousePressEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton){QWidget::mousePressEvent(e);return;}
    e->accept();
    if(!m_editMode)return;
    m_resize=e->x()>width()-24 && e->y()>height()-24;m_drag=!m_resize && e->y()<44;
    if(m_drag || m_resize){m_pressGlobal=e->globalPos();m_startPos=pos();m_startSize=size();raise();e->accept();}
}
void LiquidDesklet::mouseMoveEvent(QMouseEvent *e) {
    if(!m_editMode){setCursor(Qt::ArrowCursor);return;}
    if(m_drag){move(boundedPosition(m_startPos+e->globalPos()-m_pressGlobal,true));}
    else if(m_resize){const auto delta=e->globalPos()-m_pressGlobal;resize(m_startSize+QSize(delta.x(),delta.y()));constrainToCanvas();}
    else setCursor((e->x()>width()-24 && e->y()>height()-24)?Qt::SizeFDiagCursor:(e->y()<44?Qt::SizeAllCursor:Qt::ArrowCursor));
}
void LiquidDesklet::mouseReleaseEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton){const bool changed=m_drag || m_resize;m_drag=m_resize=false;if(changed)savePlacement();e->accept();}else QWidget::mouseReleaseEvent(e);}
bool LiquidDesklet::eventFilter(QObject *object,QEvent *event){
    if(object==m_canvas && event->type()==QEvent::Resize && m_ready){constrainToCanvas();invalidateMaterial();}
    return QWidget::eventFilter(object,event);
}
void LiquidDesklet::contextMenuEvent(QContextMenuEvent *e){
    QMenu menu(this);auto *startup=menu.addAction(autoStartEnabled(m_key)?"随 Fences 启动（已开启）":"随 Fences 启动（未开启）");
    startup->setCheckable(true);startup->setChecked(autoStartEnabled(m_key));
    connect(startup,&QAction::triggered,this,[this](bool on){setAutoStart(m_key,on);});
    extendMenu(menu);menu.addSeparator();
    connect(menu.addAction("刷新液态材质"),&QAction::triggered,this,&LiquidDesklet::invalidateMaterial);
    connect(menu.addAction("隐藏组件"),&QAction::triggered,this,&QWidget::hide);
    menu.exec(e->globalPos());
}
ClockDesklet::ClockDesklet(DesktopCanvas *canvas):LiquidDesklet(canvas,"clock","时钟 · 倒计时",QSize(220,250)) {
    QSettings s;m_timerPage=s.value("desklets/clock/timerPage",false).toBool();
    m_countdown=CountdownState::fromJson(QJsonDocument::fromJson(s.value("desklets/clock/countdown").toByteArray()).object());
    m_clockTab=button("时钟","clockTab");m_timerTab=button("倒计时","timerTab");
    m_clockTab->setCheckable(true);m_timerTab->setCheckable(true);
    connect(m_clockTab,&QPushButton::clicked,this,[this]{m_timerPage=false;updateControls();persist();});
    connect(m_timerTab,&QPushButton::clicked,this,[this]{m_timerPage=true;updateControls();persist();});
    m_minutes=new QSpinBox(this);m_minutes->setObjectName("countdownMinutes");m_minutes->setRange(1,1440);m_minutes->setSuffix(" 分钟");m_minutes->setValue(qMax<qint64>(1,m_countdown.durationMs/60000));
    QFont inputFont=font();inputFont.setPixelSize(12);m_minutes->setFont(inputFont);
    m_minutes->setStyleSheet("QSpinBox {color:#eef7ff;background:rgba(210,235,255,20);border:1px solid rgba(235,250,255,45);border-radius:8px;padding:4px;}");
    connect(m_minutes,qOverload<int>(&QSpinBox::valueChanged),this,[this](int v){if(m_countdown.state==CountdownState::Ready || m_countdown.state==CountdownState::Finished){m_countdown.cancel();m_countdown.durationMs=qint64(v)*60000;persist();update();}});
    for(int minutes:{5,15,25}) {auto *b=button(QString::number(minutes)+"′","preset"+QString::number(minutes));m_presets.append(b);connect(b,&QPushButton::clicked,this,[this,minutes]{m_minutes->setValue(minutes);});}
    m_start=button("开始","countdownStart");m_cancel=button("取消","countdownCancel");
    connect(m_start,&QPushButton::clicked,this,[this]{
        if(m_countdown.state==CountdownState::Running && m_countdown.remaining(QDateTime::currentMSecsSinceEpoch())==0){tick();return;}
        m_countdown.toggle(QDateTime::currentMSecsSinceEpoch());
        persist();updateControls();
    });
    connect(m_cancel,&QPushButton::clicked,this,[this]{m_countdown.cancel();persist();updateControls();});
    m_tick.setInterval(1000);m_tick.setTimerType(Qt::CoarseTimer);connect(&m_tick,&QTimer::timeout,this,&ClockDesklet::tick);m_tick.start();
    arrangeControls();updateControls();QTimer::singleShot(0,this,&ClockDesklet::tick);
}
ClockDesklet::~ClockDesklet(){persist();}
void ClockDesklet::persist(){QSettings s;s.setValue("desklets/clock/timerPage",m_timerPage);s.setValue("desklets/clock/countdown",QJsonDocument(m_countdown.toJson()).toJson(QJsonDocument::Compact));s.sync();}
void ClockDesklet::tick(){
    if(m_countdown.expire(QDateTime::currentMSecsSinceEpoch())) {
        persist();updateControls();
        auto message=QDBusMessage::createMethodCall("org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","Notify");
        message << QString("UKUI 时钟") << quint32(0) << QString("alarm-clock") << QString("倒计时结束") << QString("设定的时间已到。") << QStringList() << QVariantMap() << 10000;
        QDBusConnection::sessionBus().asyncCall(message);QApplication::beep();
    }
    if(isVisible())update();
}
void ClockDesklet::updateControls(){
    m_clockTab->setChecked(!m_timerPage);m_timerTab->setChecked(m_timerPage);
    const bool active=m_countdown.state==CountdownState::Running || m_countdown.state==CountdownState::Paused;
    m_minutes->setVisible(m_timerPage);m_minutes->setEnabled(!active);
    for(auto *b:m_presets){b->setVisible(m_timerPage);b->setEnabled(!active);}
    m_start->setVisible(m_timerPage);m_cancel->setVisible(m_timerPage);m_cancel->setEnabled(active || m_countdown.state==CountdownState::Finished);
    m_start->setText(m_countdown.state==CountdownState::Running?"暂停":m_countdown.state==CountdownState::Paused?"继续":m_countdown.state==CountdownState::Finished?"再次开始":"开始");update();
}
void ClockDesklet::arrangeControls(){
    if(!m_clockTab)return;
    const int half=(width()-40)/2;m_clockTab->setGeometry(16,18,half,24);m_timerTab->setGeometry(24+half,18,half,24);
    const int y=height()-75;m_minutes->setGeometry(16,y,90,25);
    const int bw=(width()-130)/3;for(int i=0;i<m_presets.size();++i)m_presets[i]->setGeometry(112+i*(bw+3),y,bw,25);
    m_start->setGeometry(16,height()-37,half,26);m_cancel->setGeometry(24+half,height()-37,half,26);
}
void ClockDesklet::paintContent(QPainter &p){
    const QDateTime now=QDateTime::currentDateTime();
    if(!m_timerPage){
        const qreal r=qMin(width()-54,height()-96)/2.;const QPointF c(width()/2.,50+r);
        p.setPen(QPen(QColor(219,244,255,34),1));p.setBrush(QColor(10,22,41,24));p.drawEllipse(c,r,r);
        for(int i=0;i<60;++i){const qreal a=qDegreesToRadians(i*6.-90);const bool major=i%5==0;
            p.setPen(QPen(major?QColor(235,250,255,150):QColor(210,237,252,48),major?1.7:1.,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(c+QPointF(qCos(a)*(r-5),qSin(a)*(r-5)),c+QPointF(qCos(a)*(r-(major?12:8)),qSin(a)*(r-(major?12:8))));
        }
        for(int i=1;i<=12;++i){const qreal a=qDegreesToRadians(i*30.-90);QPointF pos=c+QPointF(qCos(a)*(r-20),qSin(a)*(r-20));text(p,QRectF(pos-QPointF(13,12),QSizeF(26,24)),QString::number(i),11,ink,i%3==0);}
        auto hand=[&](qreal degrees,qreal length,qreal weight,QColor color){const qreal a=qDegreesToRadians(degrees-90);p.setPen(QPen(color,weight,Qt::SolidLine,Qt::RoundCap));p.drawLine(c-QPointF(qCos(a)*5,qSin(a)*5),c+QPointF(qCos(a)*length,qSin(a)*length));};
        const auto t=now.time();hand((t.hour()%12+t.minute()/60.)*30,r*.46,4.6,ink);hand((t.minute()+t.second()/60.)*6,r*.67,3.0,ink);hand(t.second()*6,r*.76,1.35,accent);
        p.setPen(Qt::NoPen);p.setBrush(accent);p.drawEllipse(c,3.5,3.5);
        text(p,QRectF(20,height()-43,width()-40,22),now.toString("HH:mm:ss"),18,ink,true);
        text(p,QRectF(20,height()-23,width()-40,18),QLocale(QLocale::Chinese).toString(now.date(),"M月d日 dddd"),11,muted);
    }else{
        const qreal r=qMin(width()-90,height()-130)/2.;const QPointF c(width()/2.,50+r);
        const QRectF ring(c-QPointF(r,r),QSizeF(r*2,r*2));p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(224,244,255,27),5));p.drawEllipse(ring);
        const qint64 left=m_countdown.remaining(now.toMSecsSinceEpoch());
        const qreal progress=qBound(0.,double(left)/m_countdown.durationMs,1.);
        p.setPen(QPen(accent,5,Qt::SolidLine,Qt::RoundCap));p.drawArc(ring,90*16,-qRound(360*16*progress));
        text(p,QRectF(0,c.y()-22,width(),40),timerText(left),left>=3600000?21:27,ink,true);
        const QString status=m_countdown.state==CountdownState::Running?"专注这一刻":m_countdown.state==CountdownState::Paused?"已暂停":m_countdown.state==CountdownState::Finished?"时间到了":"准备开始";
        text(p,QRectF(0,c.y()+17,width(),18),status,11,muted);
    }
}
ActivityDesklet::ActivityDesklet(DesktopCanvas *canvas,ActivityRecorder *recorder):LiquidDesklet(canvas,"activity","活动统计",QSize(360,220)),m_recorder(recorder){
    m_pause=button("","activityRecording");m_pause->setToolTip("仅在本机记录应用名称和前台停留时长；不读取窗口标题或输入内容。隐藏组件不停止统计，可在这里暂停。");
    connect(m_pause,&QPushButton::clicked,this,[this]{m_recorder->setRecording(!m_recorder->isRecording());});
    connect(recorder,&ActivityRecorder::changed,this,[this]{m_pause->setText(m_recorder->isRecording()?"暂停记录":"继续记录");if(isVisible())update();});
    m_pause->setText(recorder->isRecording()?"暂停记录":"继续记录");arrangeControls();
}
void ActivityDesklet::arrangeControls(){if(m_pause)m_pause->setGeometry(width()-104,height()-35,88,24);}
void ActivityDesklet::extendMenu(QMenu &menu){connect(menu.addAction(m_recorder->isRecording()?"暂停应用时长记录":"继续应用时长记录"),&QAction::triggered,this,[this]{m_recorder->setRecording(!m_recorder->isRecording());});}
void ActivityDesklet::paintContent(QPainter &p){
    const auto today=QDate::currentDate();const auto &ledger=m_recorder->ledger();const auto current=ledger.days.value(today);
    auto shortDuration=[](qint64 ms){
        const qint64 minutes=ms/60000;
        return minutes<1 ? QStringLiteral("<1分") : minutes<60 ? QString::number(minutes)+"分"
            : QStringLiteral("%1小时%2分").arg(minutes/60).arg(minutes%60);
    };
    // NextKde's medium activity card pairs uptime and apps horizontally.
    const qreal margin=16,gap=16,leftWidth=(width()-2*margin-gap)*.46;
    const qreal rightX=margin+leftWidth+gap,rightWidth=width()-margin-rightX;
    text(p,QRectF(margin,19,leftWidth,21),"开机 "+shortDuration(current.uptimeMs),13,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,QRectF(margin,43,leftWidth,16),"最近 60 天",10,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    const qreal cellGap=3, cw=(leftWidth-9*cellGap)/10.;
    const qreal ch=qMin(cw,(height()-107-5*cellGap)/6.);
    m_cells.clear();
    for(int i=0;i<60;++i){
        const auto date=today.addDays(i-59);const auto ms=ledger.days.value(date).uptimeMs;
        const QRectF cell(margin+(i%10)*(cw+cellGap),65+(i/10)*(ch+cellGap),cw,ch);m_cells.append(qMakePair(cell,date));
        const qreal level=qMin(1.,ms/double(10*3600000));
        const QColor color=ms?QColor::fromRgbF(.31+.22*level,.60+.31*level,.63+.23*level,.42+.53*level):QColor(220,235,255,20);
        p.setPen(date==today?QPen(QColor(232,255,252,195),1):QPen(Qt::NoPen));p.setBrush(color);p.drawRoundedRect(cell,2,2);
    }
    text(p,QRectF(rightX,19,rightWidth,21),"今日应用 · 前台停留",11,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    QList<AppDuration> apps=current.apps.values();std::sort(apps.begin(),apps.end(),[](const AppDuration &a,const AppDuration &b){return a.ms>b.ms;});
    const int count=qMin(qMin(8,(height()-88)/27),apps.size());
    if(count==0)text(p,QRectF(rightX,59,rightWidth,56),m_recorder->isRecording()?"使用应用后\n显示统计":"应用记录已暂停",11,muted);
    else for(int i=0;i<count;++i){
        const qreal y=49+i*27;const QString time=shortDuration(apps[i].ms);
        QFont small=font();small.setPixelSize(10);const qreal timeWidth=QFontMetrics(small).horizontalAdvance(time)+8;
        QFont label=font();label.setPixelSize(11);QFontMetrics fm(label);
        const qreal nameWidth=qMax(16.,rightWidth-timeWidth-16);
        p.setPen(Qt::NoPen);p.setBrush(QColor(151,222,215,25));p.drawRoundedRect(QRectF(rightX,y,rightWidth,23),5,5);
        p.setBrush(QColor(126,229,211,39));p.drawRoundedRect(QRectF(rightX,y,rightWidth*double(apps[i].ms)/qMax<qint64>(1,apps.first().ms),23),5,5);
        text(p,QRectF(rightX+6,y,nameWidth,23),fm.elidedText(apps[i].name,Qt::ElideRight,int(nameWidth)),11,ink,false,Qt::AlignLeft|Qt::AlignVCenter);
        text(p,QRectF(width()-margin-timeWidth-5,y,timeWidth,23),time,10,muted,false,Qt::AlignRight|Qt::AlignVCenter);
    }
    const bool failed=!m_recorder->saveOk() || m_recorder->property("saveError").toBool();
    const QString footer=failed?"保存失败，请检查磁盘":m_recorder->isRecording()?"仅本机记录":"应用记录已暂停";
    text(p,QRectF(margin,height()-35,width()-128,24),footer,10,failed?QColor("#ffbba7"):muted,false,Qt::AlignLeft|Qt::AlignVCenter);
}
void ActivityDesklet::mouseMoveEvent(QMouseEvent *e){
    LiquidDesklet::mouseMoveEvent(e);QString tip;
    for(const auto &cell:m_cells)if(cell.first.contains(e->pos())){const auto ms=m_recorder->ledger().days.value(cell.second).uptimeMs;tip=cell.second.toString("yyyy-MM-dd")+(ms?"  开机 "+duration(ms):"  尚无记录")+"\n开机时长来自 /proc/uptime，包含锁屏和休眠。";break;}
    if(toolTip()!=tip)setToolTip(tip);
}
