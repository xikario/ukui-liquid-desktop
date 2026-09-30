#include "MusicDesklet.h"
#include "StrawberryPlayer.h"
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QMenu>
#include <QShowEvent>
#include <QHideEvent>
#include <QSignalBlocker>
#include <cmath>
namespace {
// Vector notes remain crisp at fractional scaling and need no music-symbol font.
void drawNote(QPainter &p, bool paired) {
    p.setPen(Qt::NoPen);
    p.save();p.translate(-3,5);p.rotate(-22);p.drawEllipse(QRectF(-3.5,-2.3,7,4.6));p.restore();
    p.drawRoundedRect(QRectF(-.8,-8,1.6,13),.6,.6);
    if (paired) {
        p.save();p.translate(5,3);p.rotate(-22);p.drawEllipse(QRectF(-3.5,-2.3,7,4.6));p.restore();
        p.drawRoundedRect(QRectF(7.2,-10,1.6,13),.6,.6);
        QPainterPath beam;beam.moveTo(-.8,-8);beam.lineTo(8.8,-10);beam.lineTo(8.8,-6.8);beam.lineTo(-.8,-4.8);beam.closeSubpath();p.drawPath(beam);
    } else {
        QPainterPath flag;flag.moveTo(.4,-8);flag.cubicTo(6,-6,7,-2,3,0);flag.cubicTo(5,-3,2,-4,.4,-4);flag.closeSubpath();p.drawPath(flag);
    }
}
QString timeLabel(qint64 us){const auto s=us/1000000;return QString("%1:%2").arg(s/60).arg(s%60,2,10,QChar('0'));}
QIcon transportIcon(int kind) {
    QPixmap pix(40,40);pix.fill(Qt::transparent);pix.setDevicePixelRatio(2);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor("#f4f7ff"));
    if(kind==1){p.drawRoundedRect(QRectF(5,4,3,12),.7,.7);p.drawRoundedRect(QRectF(12,4,3,12),.7,.7);}
    else {if(kind==2){p.translate(20,0);p.scale(-1,1);}QPainterPath triangle;triangle.moveTo(5,4);triangle.lineTo(15,10);triangle.lineTo(5,16);triangle.closeSubpath();p.drawPath(triangle);if(kind==2 || kind==3)p.drawRoundedRect(QRectF(15,4,2,12),.5,.5);}
    return QIcon(pix);
}
}
MusicDesklet::MusicDesklet(DesktopCanvas *canvas):LiquidDesklet(canvas,"music","Strawberry · 音乐",QSize(360,180)),m_player(new StrawberryPlayer(this)) {
    m_previous=button("","musicPrevious");m_play=button("","musicPlayPause");m_next=button("","musicNext");
    m_open=button("打开播放器","musicOpen");m_cover=button("","musicCover");
    m_cover->setStyleSheet("QPushButton{background:transparent;border:0;border-radius:10px;}QPushButton:hover{background:rgba(255,255,255,20);}");
    m_previous->setIcon(transportIcon(2));m_next->setIcon(transportIcon(3));
    m_previous->setToolTip("上一首");m_next->setToolTip("下一首");m_cover->setToolTip("打开 Strawberry");
    for(auto *b:{m_previous,m_play,m_next})b->setIconSize(QSize(20,20));
    m_seek=new QSlider(Qt::Horizontal,this);m_seek->setObjectName("musicSeek");m_seek->setRange(0,1000);m_seek->setAccessibleName("播放进度");
    m_volume=new QSlider(Qt::Horizontal,this);m_volume->setObjectName("musicVolume");m_volume->setRange(0,100);m_volume->setAccessibleName("音量");
    for(auto *slider:{m_seek,m_volume})slider->setStyleSheet("QSlider::groove:horizontal{height:4px;background:rgba(240,250,255,35);border-radius:2px;}QSlider::sub-page:horizontal{background:#9ae8db;border-radius:2px;}QSlider::handle:horizontal{background:#e6fff7;width:10px;margin:-3px 0;border-radius:5px;}QSlider:disabled{color:#718086;}");
    connect(m_previous,&QPushButton::clicked,m_player,&StrawberryPlayer::previous);
    connect(m_play,&QPushButton::clicked,m_player,&StrawberryPlayer::playPause);
    connect(m_next,&QPushButton::clicked,m_player,&StrawberryPlayer::next);
    connect(m_open,&QPushButton::clicked,m_player,&StrawberryPlayer::openPlayer);
    connect(m_cover,&QPushButton::clicked,m_player,&StrawberryPlayer::openPlayer);
    connect(m_seek,&QSlider::sliderPressed,this,[this]{m_seekTrack=m_player->trackId();});
    connect(m_seek,&QSlider::sliderReleased,this,[this]{if(m_seekTrack==m_player->trackId())m_player->seek(m_player->length()*m_seek->value()/1000);});
    connect(m_seek,&QSlider::valueChanged,this,[this](int v){if(!m_updating && !m_seek->isSliderDown())m_player->seek(m_player->length()*v/1000);});
    connect(m_volume,&QSlider::sliderReleased,this,[this]{m_player->setVolume(m_volume->value()/100.0);});
    connect(m_volume,&QSlider::valueChanged,this,[this](int v){if(!m_updating && !m_volume->isSliderDown())m_player->setVolume(v/100.0);});
    connect(m_player,&StrawberryPlayer::changed,this,&MusicDesklet::updateControls);
    m_notesTimer.setInterval(33);
    connect(&m_notesTimer,&QTimer::timeout,this,[this]{update(notesArea());});
    arrangeControls();updateControls();
}
QRect MusicDesklet::notesArea() const {return QRect(6,4,width()-12,qMax(1,height()-54));}
void MusicDesklet::syncNotesAnimation() {
    const bool animate=isVisible() && m_player->connected() && m_player->playing();
    if (animate && !m_notesTimer.isActive()) {
        m_notesClock.start();
        m_notesTimer.start();
        update(notesArea());
    } else if (!animate && m_notesTimer.isActive()) {
        m_notesTimer.stop();
        m_notesClock.invalidate();
        update(notesArea());
    }
}
void MusicDesklet::paintFloatingNotes(QPainter &p) {
    if (!m_notesTimer.isActive()) return;
    const qreal seconds=m_notesClock.elapsed()/1000.;
    const QColor colors[]={QColor("#a2f5df"),QColor("#fff0cc"),QColor("#e6c4ff")};
    p.save();p.setClipRect(notesArea(),Qt::IntersectClip);
    const QRect area=notesArea();
    for (int i=0;i<5;++i) {
        const qreal life=4.2+.23*i;
        const qreal phase=std::fmod(seconds/life+i/5.,1.);
        const qreal fade=qMin(qMin(phase/.15,(1.-phase)/.28),1.);
        const qreal x=area.left()+18+(area.width()-36)*(i+.5)/5.+std::sin(phase*6.283185+i*1.7)*9;
        const qreal y=area.bottom()-10-phase*(area.height()-20);
        p.save();p.translate(x,y);p.rotate(std::sin(phase*6.283185+i)*17);
        const qreal scale=.70+(i%3)*.12;p.scale(scale,scale);
        p.setOpacity(.72*fade*qMin(seconds/.25,1.));
        // Small dark outline keeps the notes legible over bright album art.
        p.setBrush(QColor(20,35,53,160));p.translate(.8,1);drawNote(p,i%2==0);
        p.translate(-.8,-1);p.setBrush(colors[i%3]);drawNote(p,i%2==0);
        p.restore();
    }
    p.restore();
}
void MusicDesklet::arrangeControls(){
    if(!m_player)return;
    m_cover->setGeometry(16,22,80,80);
    m_previous->setGeometry(112,68,32,30);m_play->setGeometry(150,68,36,30);m_next->setGeometry(192,68,32,30);
    m_open->setGeometry(236,68,width()-252,30);
    m_seek->setGeometry(16,height()-46,width()-150,18);m_volume->setGeometry(52,height()-24,width()-114,16);
}
void MusicDesklet::updateControls(){
    m_updating=true;
    m_play->setIcon(transportIcon(m_player->playing()?1:0));
    m_play->setToolTip(m_player->playing()?"暂停":"播放");m_play->setAccessibleName(m_play->toolTip());
    m_previous->setEnabled(m_player->capability("CanGoPrevious"));m_next->setEnabled(m_player->capability("CanGoNext"));
    m_play->setEnabled(m_player->capability(m_player->playing()?"CanPause":"CanPlay"));
    m_seek->setEnabled(m_player->capability("CanSeek") && m_player->length()>0);
    if(!m_seek->isSliderDown())m_seek->setValue(m_player->length()>0?m_player->position()*1000/m_player->length():0);
    m_volume->setEnabled(m_player->capability("CanControl"));
    if(!m_volume->isSliderDown())m_volume->setValue(qRound(m_player->volume()*100));
    m_volume->setToolTip(QString("音量 %1%").arg(m_volume->value()));
    m_open->setText(m_player->connected()?"打开播放器":"启动播放器");
    m_open->setToolTip(m_player->error());m_updating=false;
    syncNotesAnimation();
    if(isVisible())update(QRect(12,18,width()-24,height()-22));
}
void MusicDesklet::paintContent(QPainter &p){
    if(!m_player)return;
    const QRectF art(16,22,80,80);QPainterPath clip;clip.addRoundedRect(art,10,10);
    p.save();p.setClipPath(clip);p.fillRect(art,QColor(195,234,228,24));
    const QImage cover=m_player->cover();
    if(!cover.isNull()){
        const int side=qMin(cover.width(),cover.height());p.drawImage(art,cover,QRectF((cover.width()-side)/2,(cover.height()-side)/2,side,side));
    }else{text(p,art,"♫",36,QColor("#9ae8db"));}p.restore();
    paintFloatingNotes(p);
    const QString title=m_player->connected()?(m_player->title().isEmpty()?"尚未选择歌曲":m_player->title()):"Strawberry 未启动";
    QString subtitle=m_player->artist();if(subtitle.isEmpty())subtitle=m_player->connected()?"在播放器中选择音乐":"点击右侧按钮，开始听音乐";
    if(!m_player->error().isEmpty())subtitle=m_player->error();
    QFont f=font();f.setPixelSize(14);f.setBold(true);
    text(p,QRectF(112,21,width()-128,24),QFontMetrics(f).elidedText(title,Qt::ElideRight,width()-128),14,QColor("#f4f7ff"),true,Qt::AlignLeft|Qt::AlignVCenter);
    f.setPixelSize(11);f.setBold(false);
    text(p,QRectF(112,47,width()-128,18),QFontMetrics(f).elidedText(subtitle,Qt::ElideRight,width()-128),11,QColor("#b8c6d9"),false,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,QRectF(width()-128,height()-47,112,20),timeLabel(m_player->position())+" / "+timeLabel(m_player->length()),11,QColor("#c7d8e0"));
    text(p,QRectF(16,height()-25,32,18),"音量",10,QColor("#b8c6d9"));
    text(p,QRectF(width()-57,height()-25,38,18),QString::number(m_volume->value())+"%",10,QColor("#b8c6d9"));
}
void MusicDesklet::showEvent(QShowEvent *event){LiquidDesklet::showEvent(event);m_player->setVisible(true);syncNotesAnimation();}
void MusicDesklet::hideEvent(QHideEvent *event){m_player->setVisible(false);syncNotesAnimation();LiquidDesklet::hideEvent(event);}
void MusicDesklet::extendMenu(QMenu &menu){connect(menu.addAction("打开 Strawberry"),&QAction::triggered,m_player,&StrawberryPlayer::openPlayer);}
