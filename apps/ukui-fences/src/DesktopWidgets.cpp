#include "DesktopWidgets.h"
#include "DesktopCanvas.h"
#include "ActivityRecorder.h"
#include "LiquidOpticsRenderer.h"
#include "LiquidMaterialPreparation.h"
#include "VideoWallpaperRegion.h"
#include "GlassFinish.h"
#include "Palette.h"
#include <QDebug>
#include <QElapsedTimer>
#include "LiquidPopup.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include "LiquidButtons.h"
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
const QColor accent("#9ae8db");
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
    : QWidget(canvas), m_canvas(canvas), m_key(key), m_title(title), m_optics(new LiquidOpticsRenderer),
      m_preparation(new LiquidMaterial::Preparation(this)) {
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
    VideoWallpaperRegion::publish(this,VideoWallpaperRegion::rounded(QRectF(rect()),16,devicePixelRatioF()));
    m_materialTimer.setSingleShot(true); m_materialTimer.setInterval(100);
    connect(&m_materialTimer,&QTimer::timeout,this,[this]{if(isVisible())rebuildMaterial();});
    m_saveTimer.setSingleShot(true);m_saveTimer.setInterval(350);
    connect(&m_saveTimer,&QTimer::timeout,this,&LiquidDesklet::savePlacement);
    connect(canvas,&DesktopCanvas::wallpaperChanged,this,&LiquidDesklet::invalidateMaterial);
    connect(canvas,&DesktopCanvas::glassFinishChanged,this,&LiquidDesklet::invalidateMaterial);
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
    LiquidButtons::install(b);
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
void LiquidDesklet::invalidateMaterial(){
    m_preparation->invalidate();m_materialPending=false;m_materialDirty=true;
    if(isVisible())m_materialTimer.start();
}
void LiquidDesklet::rebuildMaterial() {
    m_materialTimer.stop();m_materialDirty=false;
    const qreal dpr=devicePixelRatioF();
    auto wallpaper=m_canvas->wallpaperBackdrop(QRect(mapToGlobal(QPoint()),size()),dpr);
    if(wallpaper.isNull()) {wallpaper=QImage(QSize(qRound(width()*dpr),qRound(height()*dpr)),QImage::Format_RGB32);wallpaper.setDevicePixelRatio(dpr);wallpaper.fill(QColor("#344257"));}
    const QRect requested(mapToGlobal(QPoint()),size());
    m_materialPending=true;
    m_preparation->request(wallpaper,[this,requested,dpr](const LiquidMaterial::Prepared &material){
        m_materialPending=false;
        if(requested!=QRect(mapToGlobal(QPoint()),size()) || !qFuzzyCompare(dpr,devicePixelRatioF()) || m_materialDirty){
            if(isVisible())rebuildMaterial();
            return;
        }
        const auto &theme=LiquidPopup::theme();
        m_optics->setOptics(theme.refraction,theme.tint,theme.highlight,1.0);
        m_optics->setPreparedWallpaper(material);
        QElapsedTimer timer;timer.start();
        const qreal radius=qMax(16.,theme.radius);
        m_material=GlassFinish::withEdge(GlassFinish::withGrain(GlassFinish::withScrim(m_optics->renderPanel(QRect(QPoint(),size()),radius),GlassFinish::scrim())),
            GlassFinish::squirclePath(QRectF(rect()),radius));
        qInfo().noquote()<<QStringLiteral("[glass] %1 %2x%3 rebuilt in %4 ms").arg(m_key).arg(width()).arg(height()).arg(timer.elapsed());
        if(!m_material.isNull())VideoWallpaperRegion::publish(this,VideoWallpaperRegion::coverage(m_material,devicePixelRatioF()));
        QVector<QColor> cells=materialCells();
        // Mid-grey glass (a scrimmed white wallpaper) cannot give 7:1 to any
        // text colour; nudge the baked material toward the readable side once.
        if(const qreal lift=Palette::toneLift(cells,Palette::PrimaryTextContrast+.05,nullptr);lift>0){
            QColor text;Palette::toneLift(cells,Palette::PrimaryTextContrast+.05,&text);
            QPainter tone(&m_material);tone.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            QColor veil=text==QColor(Qt::white)?QColor(Qt::black):QColor(Qt::white);veil.setAlphaF(qMin<qreal>(1,lift+.004));
            tone.fillRect(QRectF(rect()),veil);tone.end();
            cells=materialCells();
            qInfo().noquote()<<QStringLiteral("[glass] %1 tone lift %2 for 7:1 text").arg(m_key).arg(lift,0,'f',3);
        }
        m_ink=Palette::ensureContrast(QColor("#f4f7ff"),cells,Palette::PrimaryTextContrast);
        m_muted=Palette::ensureContrast(QColor("#b8c6d9"),cells,Palette::SecondaryTextContrast);
        ++m_materialBuilds;setProperty("liquidMaterialBuilds",m_materialBuilds);setProperty("liquidOpticalGpu",m_optics->usedGpu());
        update();
    });
}
QVector<QColor> LiquidDesklet::materialCells() const {
    // Once per material build: 12x8 averaged cells. The outer ring holds the
    // baked rim highlight and uncovered corners, never text, so skip it.
    QVector<QColor> cells;if(m_material.isNull())return cells;
    const QImage small=m_material.scaled(12,8,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    for(int y=1;y<small.height()-1;++y)for(int x=1;x<small.width()-1;++x){const QRgb px=small.pixel(x,y);if(qAlpha(px)>=200)cells.append(QColor(qRed(px),qGreen(px),qBlue(px)));}
    return cells;
}
void LiquidDesklet::paintEvent(QPaintEvent *event) {
    if(!m_materialPending && (m_material.isNull() || m_material.devicePixelRatio()!=devicePixelRatioF()))rebuildMaterial();
    QPainter p(this);p.setClipRegion(event->region());p.setRenderHint(QPainter::Antialiasing);
    if(m_material.isNull()){p.setPen(Qt::NoPen);p.setBrush(QColor(28,43,59,240));p.drawRoundedRect(QRectF(rect()),16,16);}
    else p.drawImage(QRectF(rect()),m_material,QRectF(m_material.rect()));
    paintContent(p);
    if(m_editMode) {
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(154,232,219,170),1,Qt::DashLine));
        p.drawRoundedRect(QRectF(rect()).adjusted(2,2,-2,-2),16,16);
        p.setPen(QPen(QColor(220,239,255,180),1.2,Qt::SolidLine,Qt::RoundCap));
        for(int i=0;i<3;++i)p.drawLine(QPointF(width()-12-i*4,height()-12),QPointF(width()-12,height()-12-i*4));
    }
}
void LiquidDesklet::resizeEvent(QResizeEvent *e){QWidget::resizeEvent(e);if(m_ready){
    VideoWallpaperRegion::publish(this,VideoWallpaperRegion::rounded(QRectF(rect()),qMax(16.,LiquidPopup::theme().radius),devicePixelRatioF()));
    arrangeControls();invalidateMaterial();m_saveTimer.start();}}
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
    else if(m_resize){
        const auto delta=e->globalPos()-m_pressGlobal;
        setGeometry(m_canvas->snappedWidgetResize(this, QRect(m_startPos,m_startSize),
            m_startSize+QSize(delta.x(),delta.y()), minimumSize(), maximumSize()));
    }
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
    connect(menu.addAction("组件设置…"),&QAction::triggered,this,[this]{m_canvas->showSettingsPage(m_key);});
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
    m_tick.setTimerType(Qt::CoarseTimer);connect(&m_tick,&QTimer::timeout,this,&ClockDesklet::tick);
    connect(canvas,&DesktopCanvas::desktopCoveredChanged,this,[this]{syncTick();if(isVisible())update();});
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
    syncTick();
}
void ClockDesklet::syncTick(){
    // The second hand only needs a timer while someone can see it. A hidden
    // running countdown keeps one coarse wake-up at its deadline for the alert.
    int interval=0;
    if(isVisible() && !m_canvas->desktopCovered())interval=1000;
    else if(m_countdown.state==CountdownState::Running)
        interval=int(qBound<qint64>(1000,m_countdown.remaining(QDateTime::currentMSecsSinceEpoch()),3600000));
    if(!interval){m_tick.stop();return;}
    if(!m_tick.isActive() || m_tick.interval()!=interval)m_tick.start(interval);
}
void ClockDesklet::showEvent(QShowEvent *e){LiquidDesklet::showEvent(e);syncTick();}
void ClockDesklet::hideEvent(QHideEvent *e){LiquidDesklet::hideEvent(e);syncTick();}
void ClockDesklet::updateControls(){
    m_clockTab->setChecked(!m_timerPage);m_timerTab->setChecked(m_timerPage);
    const bool active=m_countdown.state==CountdownState::Running || m_countdown.state==CountdownState::Paused;
    m_minutes->setVisible(m_timerPage);m_minutes->setEnabled(!active);
    for(auto *b:m_presets){b->setVisible(m_timerPage);b->setEnabled(!active);}
    m_start->setVisible(m_timerPage);m_cancel->setVisible(m_timerPage);m_cancel->setEnabled(active || m_countdown.state==CountdownState::Finished);
    m_start->setText(m_countdown.state==CountdownState::Running?"暂停":m_countdown.state==CountdownState::Paused?"继续":m_countdown.state==CountdownState::Finished?"再次开始":"开始");update();
    syncTick();
}
void ClockDesklet::arrangeControls(){
    if(!m_clockTab)return;
    const int half=(width()-40)/2;m_clockTab->setGeometry(16,18,half,24);m_timerTab->setGeometry(24+half,18,half,24);
    const int y=height()-75;m_minutes->setGeometry(16,y,90,25);
    const int bw=(width()-130)/3;for(int i=0;i<m_presets.size();++i)m_presets[i]->setGeometry(112+i*(bw+3),y,bw,25);
    m_start->setGeometry(16,height()-37,half,26);m_cancel->setGeometry(24+half,height()-37,half,26);
}
void ClockDesklet::paintGlassFace(QPainter &p,const QPointF &center,qreal radius) {
    const QRect face(qRound(center.x()-radius),qRound(center.y()-radius),
                     qRound(radius*2),qRound(radius*2));
    if(face.isEmpty())return;
    // Use the original desktop wallpaper under the face. Sampling the already
    // darkened outer card makes a second glass pass look like a flat blue disk.
    const qint64 sourceKey=material().cacheKey();
    if(m_faceMaterial.isNull() || m_faceSourceKey!=sourceKey || m_faceRect!=face) {
        const QRect globalFace(mapToGlobal(face.topLeft()),face.size());
        const QImage backdrop=m_canvas->wallpaperBackdrop(globalFace,devicePixelRatioF());
        if(backdrop.isNull())return;
        if(!m_faceOptics)m_faceOptics=std::make_unique<LiquidOpticsRenderer>();
        const auto &theme=LiquidPopup::theme();
        // Match desklet optics while keeping the face's own wallpaper crop.
        m_faceOptics->setOptics(theme.refraction,theme.tint,theme.highlight,1.0);
        m_faceOptics->setWallpaper(backdrop);
        m_faceMaterial=m_faceOptics->renderControl(QRect(QPoint(),face.size()),face.width()/2.);
        // A sparse wallpaper cannot produce a visible refracted contour on its
        // own. Cache a directional rim and a faint curved reflection so the
        // face reads as clear glass even over sky, without another opaque fill.
        {
            QPainter lens(&m_faceMaterial);lens.setRenderHint(QPainter::Antialiasing);
            const QRectF bounds(1,1,face.width()-2,face.height()-2);
            QPainterPath circle;circle.addEllipse(bounds);lens.setClipPath(circle);
            QRadialGradient reflection(QPointF(face.width()*.24,face.height()*.08),face.width()*.72);
            reflection.setColorAt(0,QColor(235,251,255,30));
            reflection.setColorAt(.50,QColor(225,248,255,8));
            reflection.setColorAt(1,Qt::transparent);
            lens.fillPath(circle,reflection);
            QLinearGradient rim(bounds.topLeft(),bounds.bottomRight());
            rim.setColorAt(0,QColor(235,253,255,210));
            rim.setColorAt(.28,QColor(217,246,255,85));
            rim.setColorAt(.5,QColor(217,246,255,8));
            rim.setColorAt(.76,QColor(217,246,255,30));
            rim.setColorAt(1,QColor(231,253,255,150));
            lens.setBrush(Qt::NoBrush);lens.setPen(QPen(rim,1.5));lens.drawEllipse(bounds);
            lens.setPen(QPen(QColor(225,250,255,38),1.));
            lens.drawArc(bounds.adjusted(3,3,-3,-3),30*16,105*16);
        }
        // Sample the rendered lens once, not on every clock tick. Clear glass
        // over snow/white walls needs dark hands; mixed backgrounds also get a
        // thin opposite-colour keyline around the dial content.
        const QImage sample=m_faceMaterial.scaled(12,12,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        qreal light=0;int count=0;
        for(int y=2;y<10;++y)for(int x=2;x<10;++x){const QColor c=sample.pixelColor(x,y);light+=.299*c.red()+.587*c.green()+.114*c.blue();++count;}
        m_faceLight=light/qMax(1,count)>155;setProperty("clockFaceDarkInk",m_faceLight);
        m_faceSourceKey=sourceKey;m_faceRect=face;++m_faceBuilds;
        setProperty("clockFaceOpticalGpu",m_faceOptics->usedGpu());
    }
    p.drawImage(QRectF(face),m_faceMaterial,QRectF(m_faceMaterial.rect()));
}
void ClockDesklet::paintContent(QPainter &p){
    const QDateTime now=QDateTime::currentDateTime();
    if(!m_timerPage){
        const qreal r=qMin(width()-54,height()-96)/2.;const QPointF c(width()/2.,50+r);
        paintGlassFace(p,c,r);
        const QColor faceInk=m_faceLight?QColor("#182b3b"):inkColor();
        const QColor faceAccent=m_faceLight?QColor("#146759"):accent;
        const QColor outline=m_faceLight?QColor(249,254,255,165):QColor(12,24,37,185);
        for(int i=0;i<60;++i){const qreal a=qDegreesToRadians(i*6.-90);const bool major=i%5==0;
            const QPointF from=c+QPointF(qCos(a)*(r-5),qSin(a)*(r-5)),to=c+QPointF(qCos(a)*(r-(major?12:8)),qSin(a)*(r-(major?12:8)));
            p.setPen(QPen(outline,major?3.:2.,Qt::SolidLine,Qt::RoundCap));p.drawLine(from,to);
            QColor mark=faceInk;mark.setAlpha(major?240:150);
            p.setPen(QPen(mark,major?1.7:1.,Qt::SolidLine,Qt::RoundCap));p.drawLine(from,to);
        }
        for(int i=1;i<=12;++i){
            const qreal a=qDegreesToRadians(i*30.-90);const QPointF pos=c+QPointF(qCos(a)*(r-20),qSin(a)*(r-20));
            QFont f=font();f.setPixelSize(11);f.setBold(i%3==0);QFontMetricsF fm(f);
            const QString number=QString::number(i);QPainterPath glyph;
            glyph.addText(pos-QPointF(fm.horizontalAdvance(number)/2.,-(fm.ascent()-fm.descent())/2.),f,number);
            p.strokePath(glyph,QPen(outline,2.2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.fillPath(glyph,faceInk);
        }
        auto hand=[&](qreal degrees,qreal length,qreal weight,QColor color){
            const qreal a=qDegreesToRadians(degrees-90);const QPointF from=c-QPointF(qCos(a)*5,qSin(a)*5),to=c+QPointF(qCos(a)*length,qSin(a)*length);
            p.setPen(QPen(outline,weight+1.8,Qt::SolidLine,Qt::RoundCap));p.drawLine(from,to);
            p.setPen(QPen(color,weight,Qt::SolidLine,Qt::RoundCap));p.drawLine(from,to);
        };
        const auto t=now.time();hand((t.hour()%12+t.minute()/60.)*30,r*.46,4.6,faceInk);hand((t.minute()+t.second()/60.)*6,r*.67,3.0,faceInk);hand(t.second()*6,r*.76,1.35,faceAccent);
        p.setPen(QPen(outline,1.));p.setBrush(faceAccent);p.drawEllipse(c,3.5,3.5);
        text(p,QRectF(20,height()-43,width()-40,22),now.toString("HH:mm:ss"),18,inkColor(),true);
        text(p,QRectF(20,height()-23,width()-40,18),QLocale(QLocale::Chinese).toString(now.date(),"M月d日 dddd"),11,mutedColor());
    }else{
        const qreal r=qMin(width()-90,height()-130)/2.;const QPointF c(width()/2.,50+r);
        paintGlassFace(p,c,r);
        const QColor faceInk=m_faceLight?QColor("#182b3b"):inkColor();
        const QColor faceAccent=m_faceLight?QColor("#146759"):accent;
        const QColor faceMuted=m_faceLight?QColor("#344c5e"):mutedColor();
        const QRectF ring(c-QPointF(r-3,r-3),QSizeF((r-3)*2,(r-3)*2));p.setBrush(Qt::NoBrush);p.setPen(QPen(m_faceLight?QColor(20,40,55,48):QColor(224,244,255,60),5));p.drawEllipse(ring);
        const qint64 left=m_countdown.remaining(now.toMSecsSinceEpoch());
        const qreal progress=qBound(0.,double(left)/m_countdown.durationMs,1.);
        p.setPen(QPen(faceAccent,5,Qt::SolidLine,Qt::RoundCap));p.drawArc(ring,90*16,-qRound(360*16*progress));
        text(p,QRectF(0,c.y()-22,width(),40),timerText(left),left>=3600000?21:27,faceInk,true);
        const QString status=m_countdown.state==CountdownState::Running?"专注这一刻":m_countdown.state==CountdownState::Paused?"已暂停":m_countdown.state==CountdownState::Finished?"时间到了":"准备开始";
        text(p,QRectF(0,c.y()+17,width(),18),status,11,faceMuted);
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
    text(p,QRectF(margin,19,leftWidth,21),"开机 "+shortDuration(current.uptimeMs),13,inkColor(),true,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,QRectF(margin,43,leftWidth,16),"最近 60 天",10,mutedColor(),false,Qt::AlignLeft|Qt::AlignVCenter);
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
    text(p,QRectF(rightX,19,rightWidth,21),"今日应用 · 前台停留",11,inkColor(),true,Qt::AlignLeft|Qt::AlignVCenter);
    QList<AppDuration> apps=current.apps.values();std::sort(apps.begin(),apps.end(),[](const AppDuration &a,const AppDuration &b){return a.ms>b.ms;});
    const int count=qMin(qMin(8,(height()-88)/27),apps.size());
    if(count==0)text(p,QRectF(rightX,59,rightWidth,56),m_recorder->isRecording()?"使用应用后\n显示统计":"应用记录已暂停",11,mutedColor());
    else for(int i=0;i<count;++i){
        const qreal y=49+i*27;const QString time=shortDuration(apps[i].ms);
        QFont small=font();small.setPixelSize(10);const qreal timeWidth=QFontMetrics(small).horizontalAdvance(time)+8;
        QFont label=font();label.setPixelSize(11);QFontMetrics fm(label);
        const qreal nameWidth=qMax(16.,rightWidth-timeWidth-16);
        p.setPen(Qt::NoPen);p.setBrush(QColor(151,222,215,25));p.drawRoundedRect(QRectF(rightX,y,rightWidth,23),5,5);
        p.setBrush(QColor(126,229,211,39));p.drawRoundedRect(QRectF(rightX,y,rightWidth*double(apps[i].ms)/qMax<qint64>(1,apps.first().ms),23),5,5);
        text(p,QRectF(rightX+6,y,nameWidth,23),fm.elidedText(apps[i].name,Qt::ElideRight,int(nameWidth)),11,inkColor(),false,Qt::AlignLeft|Qt::AlignVCenter);
        text(p,QRectF(width()-margin-timeWidth-5,y,timeWidth,23),time,10,mutedColor(),false,Qt::AlignRight|Qt::AlignVCenter);
    }
    const bool failed=!m_recorder->saveOk() || m_recorder->property("saveError").toBool();
    const QString footer=failed?"保存失败，请检查磁盘":m_recorder->isRecording()?"仅本机记录":"应用记录已暂停";
    text(p,QRectF(margin,height()-35,width()-128,24),footer,10,failed?QColor("#ffbba7"):mutedColor(),false,Qt::AlignLeft|Qt::AlignVCenter);
}
void ActivityDesklet::mouseMoveEvent(QMouseEvent *e){
    LiquidDesklet::mouseMoveEvent(e);QString tip;
    for(const auto &cell:m_cells)if(cell.first.contains(e->pos())){const auto ms=m_recorder->ledger().days.value(cell.second).uptimeMs;tip=cell.second.toString("yyyy-MM-dd")+(ms?"  开机 "+duration(ms):"  尚无记录")+"\n开机时长来自 /proc/uptime，包含锁屏和休眠。";break;}
    if(toolTip()!=tip)setToolTip(tip);
}
