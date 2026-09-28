#include "LiquidPopup.h"
#include <QHelpEvent>
#include <QAbstractItemView>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QStyleOption>
#include <QToolTip>
#include <QTextDocument>
#include <QtMath>
#include <algorithm>

namespace LiquidPopup {
namespace {
Theme settings;
bool enabled = true;
BackdropProvider provider;
QPointer<Shell> tip;
QPointer<QWidget> tipOwner;
QRect tipOwnerRect;
QString tipText;
QTimer *tipTimeout = nullptr;

QImage capture(const QRect &area, qreal dpr) {
    if (provider) return provider(area, dpr);
    // X11 snapshot before mapping. Wayland has no silent global capture;
    // return an empty image and use a readable tonal fallback there.
    if (QGuiApplication::platformName() != QStringLiteral("xcb")) return {};
    QScreen *s = QGuiApplication::screenAt(area.center());
    if (!s) return {};
    const QRect local = area.translated(-s->geometry().topLeft());
    return s->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
}
QRect screenRect(QPoint p) {
    QScreen *s = QGuiApplication::screenAt(p);
    if (!s) s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry() : QRect(0,0,1024,768);
}
QImage diffuse(const QImage &in, QSize size) {
    // Bounded small convolution, once per popup; no animation-frame capture.
    QImage a = in.scaled(qMax(1,size.width()/4),qMax(1,size.height()/4),
        Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(),a.format());
    for (int pass=0;pass<4;++pass) {
        for(int y=0;y<a.height();++y) for(int x=0;x<a.width();++x) {
            int r=0,g=0,bl=0;
            for(int k=-2;k<=2;++k) {
                const QRgb c=a.pixel(qBound(0,x+(pass%2?0:k),a.width()-1),
                                    qBound(0,y+(pass%2?k:0),a.height()-1));
                r+=qRed(c);g+=qGreen(c);bl+=qBlue(c);
            }
            b.setPixel(x,y,qRgb(r/5,g/5,bl/5));
        }
        a.swap(b);
    }
    return a.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
class MenuSkin final : public QObject {
public:
    explicit MenuSkin(QMenu *m):QObject(m),menu(m) {
        menu->installEventFilter(this);
        connect(menu,&QMenu::aboutToShow,this,[this] {
            if (!enabled) return;
            if(tip) tip->hide();
            QToolTip::hideText();
            prepare();
        });
        connect(&fade,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
            menu->setWindowOpacity(v.toReal());
        });
    }
    void prepare() {
            if(!enabled || styled)return;
            originalStyle = menu->styleSheet();
            originalMask = menu->mask();
            originalTranslucent = menu->testAttribute(Qt::WA_TranslucentBackground);
            originalAutoFill = menu->autoFillBackground();
            styled = true;
            menu->setAttribute(Qt::WA_TranslucentBackground);
            menu->setAutoFillBackground(false);
            // Explicit item padding reserves the indicator/arrow columns when
            // stylesheet selection takes over native menu layout.
            const QString override=QStringLiteral(
                "\nQMenu { background: transparent; border: 1px solid transparent; }\n"
                "QMenu::indicator { width: 14px; height: 14px; }\n"
                "QMenu::item { padding: 6px 28px; margin: 2px 6px; }\n"
                "QMenu::item:selected:enabled { background-color: palette(highlight); color: palette(highlighted-text); border-radius: 6px; }\n"
                "QMenu::separator { height: 1px; background-color: rgba(128,128,128,70); margin: 4px 10px; }\n");
            if(!menu->styleSheet().endsWith(override)) menu->setStyleSheet(menu->styleSheet()+override);
            menu->ensurePolished();
    }
protected:
    bool eventFilter(QObject *,QEvent *e) override {
        if (!enabled) {
            restore();
            return false;
        }
        if(e->type()==QEvent::Show) {
            prepare();
            // Show follows final placement and precedes native mapping.
            // Capture only the popup footprint, not an entire monitor.
            if(tip) tip->hide();
            QToolTip::hideText();
            const QImage crop=capture(QRect(menu->mapToGlobal(QPoint()),menu->size()),
                                      menu->devicePixelRatioF());
            const bool light=menu->palette().color(QPalette::WindowText).lightness()<128;
            material=renderMenuMaterial(crop,menu->size(),menu->devicePixelRatioF(),light);
            QPainterPath path;
            // Native regions are binary and use logical integer coordinates.
            // Keep their stair steps outside the antialiased material edge.
            const qreal radius=qMin(theme().radius,qMin(menu->width(),menu->height())/2.);
            path.addRoundedRect(QRectF(menu->rect()).adjusted(-2,-2,2,2),radius+2,radius+2);
            menu->setMask(QRegion(path.toFillPolygon().toPolygon()));
            if(!theme().reducedMotion && QGuiApplication::platformName()=="xcb") {
                fade.stop();fade.setStartValue(.15);fade.setEndValue(1.0);
                fade.setDuration(theme().openMs);fade.setEasingCurve(QEasingCurve::OutCubic);fade.start();
            }
        } else if(e->type()==QEvent::Paint) {
            QPainter p(menu);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(menu->rect(),Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.drawImage(QPointF(),material);
            // Qt now paints normal items, separators, checks and scroll arrows.
        } else if(e->type()==QEvent::Hide) {
            restore();
        }
        return false;
    }
    void restore() {
        if (!styled) return;
        styled = false;
        fade.stop();menu->setWindowOpacity(1);material={};
        menu->setStyleSheet(originalStyle);
        menu->setMask(originalMask);
        menu->setAutoFillBackground(originalAutoFill);
        menu->setAttribute(Qt::WA_TranslucentBackground, originalTranslucent);
    }
private:
    QString originalStyle;
    QRegion originalMask;
    bool styled = false, originalTranslucent = false, originalAutoFill = false;
    QMenu *menu;
    QImage material;
    QVariantAnimation fade;
};
class Filter final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *o,QEvent *e) override {
        auto *w=qobject_cast<QWidget *>(o);
        if(!w) return false;
        if(e->type()==QEvent::Polish) {
            auto *menu=qobject_cast<QMenu *>(w);
            if(menu && !menu->property("liquidPopupSkin").toBool()) {
                menu->setProperty("liquidPopupSkin",true);new MenuSkin(menu);
            }
        }
        if (!enabled) return false;
        if(e->type()==QEvent::ToolTip) {
            auto *h=static_cast<QHelpEvent *>(e);
            QString text=w->toolTip();
            QRect ownerArea=w->rect();
            // Qt item views store help in model roles, not QWidget::toolTip.
            auto *view=qobject_cast<QAbstractItemView *>(w->parentWidget());
            if(view && w==view->viewport()) {
                const QModelIndex index=view->indexAt(h->pos());
                text=index.data(Qt::ToolTipRole).toString();
                ownerArea=view->visualRect(index);
            }
            if(auto *menu=qobject_cast<QMenu *>(w)) {
                if(menu->toolTipsVisible()) {
                    if(auto *a=menu->actionAt(h->pos())) {
                        text=a->toolTip();ownerArea=menu->actionGeometry(a);
                    }
                }
            }
            if(!text.isEmpty()) {
                showText(h->globalPos()+QPoint(10,16),text,w,ownerArea,w->toolTipDuration());
                e->accept();return true;
            }
        }
        if(tip && tip->isVisible()) {
            if(e->type()==QEvent::MouseButtonPress || e->type()==QEvent::KeyPress) {
                // Remove immediately before a menu captures the background.
                tip->hide();
            } else if(w==tipOwner && (e->type()==QEvent::Leave || e->type()==QEvent::Hide || e->type()==QEvent::Close)) hideText();
            if(e->type()==QEvent::MouseMove && w==tipOwner && !tipOwnerRect.isEmpty() &&
               !tipOwnerRect.contains(w->mapFromGlobal(QCursor::pos()))) hideText();
        }
        return false;
    }
};
}
QAction *execAt(QMenu &menu,const QRect &anchor) {
    if(tip) tip->hide();
    QToolTip::hideText();
    menu.ensurePolished();
    // Apply layout-affecting menu metrics before measuring anchored placement.
    for(QObject *child:menu.children())if(auto *skin=dynamic_cast<MenuSkin *>(child))skin->prepare();
    const QRect target=place(menu.sizeHint(),anchor,screenRect(anchor.center()));
    return menu.exec(target.topLeft());
}
QAction *execAt(QMenu &menu,QWidget *anchor) {
    if(!anchor) return menu.exec(QCursor::pos());
    return execAt(menu,QRect(anchor->mapToGlobal(QPoint()),anchor->size()));
}
Theme &theme(){return settings;}
bool isEnabled(){return enabled;}
bool drawMenuGlyph(QStyle::PrimitiveElement element,const QStyleOption *option,QPainter *p) {
    const bool check=element==QStyle::PE_IndicatorMenuCheckMark;
    const bool arrow=element==QStyle::PE_IndicatorArrowLeft || element==QStyle::PE_IndicatorArrowRight ||
        element==QStyle::PE_IndicatorArrowUp || element==QStyle::PE_IndicatorArrowDown;
    if(!check && !arrow)return false;
    const QRectF rect(option->rect);
    const qreal extent=qMax(0.,qMin(check?11.:8.,qMin(rect.width(),rect.height())-2.));
    const bool active=option->state & QStyle::State_Enabled;
    const bool selected=option->state & (QStyle::State_Selected|QStyle::State_MouseOver|QStyle::State_Sunken);
    const QColor color=option->palette.color(active?QPalette::Active:QPalette::Disabled,
        active && selected?QPalette::HighlightedText:QPalette::Text);
    p->save();p->setRenderHint(QPainter::Antialiasing);p->translate(rect.center());
    QPainterPath path;
    if(check) {
        path.moveTo(-extent*.42,0);path.lineTo(-extent*.12,extent*.29);path.lineTo(extent*.43,-extent*.32);
    } else {
        if(element==QStyle::PE_IndicatorArrowLeft)p->rotate(180);
        else if(element==QStyle::PE_IndicatorArrowUp)p->rotate(-90);
        else if(element==QStyle::PE_IndicatorArrowDown)p->rotate(90);
        path.moveTo(-extent*.22,-extent*.42);path.lineTo(extent*.22,0);path.lineTo(-extent*.22,extent*.42);
    }
    p->setBrush(Qt::NoBrush);p->setPen(QPen(color,check?1.6:1.25,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p->drawPath(path);p->restore();return true;
}
void setEnabled(bool value) {
    if (enabled == value) return;
    // Closing an open menu restores its original stylesheet before switching.
    if (qApp) for (QWidget *w : QApplication::topLevelWidgets())
        if (qobject_cast<QMenu *>(w) && w->isVisible()) w->hide();
    if (tip) tip->hide();
    enabled = value;
}
void setBackdropProvider(BackdropProvider p){provider=std::move(p);}
QRect place(QSize size,const QRect &anchor,const QRect &available) {
    const QRect safe=available.adjusted(8,8,-8,-8);
    size=size.boundedTo(safe.size());
    int x=anchor.center().x()-size.width()/2;
    int y=anchor.bottom()+6;
    if(y+size.height()>safe.bottom()+1) y=anchor.top()-size.height()-6;
    return QRect(QPoint(qBound(safe.left(),x,safe.right()-size.width()+1),
                        qBound(safe.top(),y,safe.bottom()-size.height()+1)),size);
}
namespace {
QRect placeTooltip(QSize size, const QRect &anchor, const QRect &available,
                   Placement preferred, Placement *chosen) {
    const QRect safe=available.adjusted(8,8,-8,-8);
    size=size.boundedTo(safe.size());
    const int gap=8;
    const auto candidate=[&](Placement side) {
        switch(side) {
        case Placement::Right:
            return QRect(anchor.right()+1+gap,
                         anchor.center().y()-size.height()/2,size.width(),size.height());
        case Placement::Left:
            return QRect(anchor.left()-gap-size.width(),
                         anchor.center().y()-size.height()/2,size.width(),size.height());
        case Placement::Above:
            return QRect(anchor.center().x()-size.width()/2,
                         anchor.top()-gap-size.height(),size.width(),size.height());
        case Placement::Below:
        case Placement::Auto:
            return QRect(anchor.center().x()-size.width()/2,
                         anchor.bottom()+1+gap,size.width(),size.height());
        }
        return QRect();
    };
    const auto fits=[&](const QRect &r) { return safe.contains(r.topLeft()) && safe.contains(r.bottomRight()); };
    QList<Placement> order;
    const auto add=[&order](Placement side) { if (!order.contains(side)) order << side; };
    if (preferred != Placement::Auto) add(preferred);
    // Normal tooltips retain the familiar below/above preference. A caller
    // can request a side placement for controls in a narrow rail.
    add(Placement::Below); add(Placement::Above); add(Placement::Right); add(Placement::Left);
    for (const Placement side : order) {
        const QRect r=candidate(side);
        if (fits(r)) { if(chosen) *chosen=side; return r; }
    }
    Placement side=preferred==Placement::Auto ? Placement::Below : preferred;
    QRect r=candidate(side);
    r.moveLeft(qBound(safe.left(),r.left(),safe.right()-r.width()+1));
    r.moveTop(qBound(safe.top(),r.top(),safe.bottom()-r.height()+1));
    if(chosen) *chosen=side;
    return r;
}
}

QPainterPath bubblePath(QRectF b,qreal radius,qreal cx,bool top) {
    return bubblePath(b,radius,cx,top ? Placement::Below : Placement::Above);
}

QPainterPath bubblePath(QRectF b,qreal radius,qreal connector,Placement side) {
    QPainterPath p;p.addRoundedRect(b,radius,radius);
    const bool vertical=side==Placement::Above || side==Placement::Below;
    const bool top=side==Placement::Below;
    if(connector>=0 && ((vertical && b.width()>radius*2+32) || (!vertical && b.height()>radius*2+32))) {
        if (vertical) {
            connector=qBound(b.left()+radius+16,connector,b.right()-radius-16);
            const qreal y=top?b.top():b.bottom(),dir=top?-1:1;
            QPainterPath neck;neck.moveTo(connector-15,y-dir*2);
            neck.cubicTo(connector-7,y,connector-7,y+dir*8,connector,y+dir*8);
            neck.cubicTo(connector+7,y+dir*8,connector+7,y,connector+15,y-dir*2);
            neck.closeSubpath();p=p.united(neck);
        } else {
            connector=qBound(b.top()+radius+16,connector,b.bottom()-radius-16);
            // Placement describes the bubble relative to its anchor, so the
            // connector belongs on the opposite edge, facing the anchor.
            const qreal x=side==Placement::Right?b.left():b.right(),dir=side==Placement::Right?-1:1;
            QPainterPath neck;neck.moveTo(x-dir*2,connector-15);
            neck.cubicTo(x,connector-7,x+dir*8,connector-7,x+dir*8,connector);
            neck.cubicTo(x+dir*8,connector+7,x,connector+7,x-dir*2,connector+15);
            neck.closeSubpath();p=p.united(neck);
        }
    }
    return p;
}
QImage renderMaterial(const QImage &input,QSize logical,qreal dpr,bool light,QRectF bounds) {
    if(logical.isEmpty()) return {};
    dpr=qBound(1.0,dpr,3.0);
    if(bounds.isEmpty()) bounds=QRectF(QPointF(0,0),QSizeF(logical));
    const QSize size(qCeil(logical.width()*dpr),qCeil(logical.height()*dpr));
    QImage source=input;
    if(source.isNull()) {source=QImage(size,QImage::Format_RGB32);source.fill(light?QColor(222,227,234):QColor(40,47,57));}
    source=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage body=diffuse(source,size),out(size,QImage::Format_ARGB32_Premultiplied);
    // Approximate edge lens. Cached raster backend; not a compositor shader.
    for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) {
        const qreal px=(x+.5)/dpr,py=(y+.5)/dpr;
        const qreal radius=qMin(theme().radius,qMin(bounds.width(),bounds.height())/2);
        const qreal sx=px-bounds.center().x(),sy=py-bounds.center().y();
        const qreal qx=std::abs(sx)-bounds.width()/2+radius;
        const qreal qy=std::abs(sy)-bounds.height()/2+radius;
        const qreal vx=qMax(qx,0.0),vy=qMax(qy,0.0),length=std::hypot(vx,vy);
        const qreal distance=length+qMin(qMax(qx,qy),0.0)-radius;
        const qreal edge=qMax(-distance,0.0);
        const qreal rim=std::exp(-edge/6.0);
        const qreal nx=(sx<0?-1:1)*(length>0?vx/length:(qx>qy?1:0));
        const qreal ny=(sy<0?-1:1)*(length>0?vy/length:(qx>qy?0:1));
        const qreal dx=-nx*theme().refraction*rim*dpr;
        const qreal dy=-ny*theme().refraction*rim*dpr;
        const QRgb clear=source.pixel(qBound(0,qRound(x+dx),size.width()-1),qBound(0,qRound(y+dy),size.height()-1));
        const QRgb blur=body.pixel(x,y);
        const qreal luminance=(.2126*qRed(blur)+.7152*qGreen(blur)+.0722*qBlue(blur))/255.;
        const qreal contrastRisk=light ? qBound(0.0,(.55-luminance)/.55,1.0)
                                      : qBound(0.0,(luminance-.4)/.6,1.0);
        const qreal tint=qBound(0.0,(theme().tint+.23*contrastRisk)*(1-rim*.5),.9),target=light?244:15;
        const qreal glow=theme().highlight*26*std::exp(-py/18.0);
        auto c=[&](int a,int b){return qBound(0,qRound((a*(1-rim*.7)+b*rim*.7)*(1-tint)+target*tint+glow),255);};
        out.setPixel(x,y,qRgb(c(qRed(blur),qRed(clear)),c(qGreen(blur),qGreen(clear)),c(qBlue(blur),qBlue(clear))));
    }
    out.setDevicePixelRatio(dpr);return out;
}
QImage renderMenuMaterial(const QImage &input,QSize logical,qreal dpr,bool light) {
    QImage out=renderMaterial(input,logical,dpr,light);
    if(out.isNull())return out;
    const QRectF body=QRectF(QPointF(),QSizeF(logical)).adjusted(.5,.5,-.5,-.5);
    const qreal radius=qMin(theme().radius,qMin(body.width(),body.height())/2.);
    QPainterPath path;path.addRoundedRect(body,radius,radius);
    QImage mask(out.size(),QImage::Format_ARGB32_Premultiplied);
    mask.setDevicePixelRatio(out.devicePixelRatio());mask.fill(Qt::transparent);
    {QPainter p(&mask);p.setRenderHint(QPainter::Antialiasing);p.fillPath(path,Qt::white);}
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);p.drawImage(QPointF(),mask);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255,255,255,80),1.));p.drawPath(path);
    return out;
}
Shell::Shell(QWidget *parent,bool tooltip):QWidget(parent,tooltip?Qt::ToolTip:Qt::Popup),m_tooltip(tooltip) {
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating,tooltip);
    setAttribute(Qt::WA_TransparentForMouseEvents,tooltip);
    setObjectName("liquidPopupShell");
    connect(&m_motion,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
        m_progress=v.toReal();
        if(m_content) {
            m_content->setVisible(m_progress>.55 && !m_closing);
            if(auto *effect=qobject_cast<QGraphicsOpacityEffect *>(m_content->graphicsEffect()))
                effect->setOpacity(qBound(0.0,(m_progress-.55)/.4,1.0));
        }
        update();
    });
    connect(&m_motion,&QVariantAnimation::finished,this,[this]{if(m_closing) hide();});
}
void Shell::setContent(QWidget *content) {
    if(m_content && m_content!=content) delete m_content;
    m_content=content;if(content) {content->setGraphicsEffect(new QGraphicsOpacityEffect(content));content->setParent(this);content->adjustSize();resize(content->size()+QSize(40,48));}
}
void Shell::openAt(const QRect &anchor, Placement placement) {
    m_motion.stop();m_closing=false;
    Placement chosen=placement;
    const QRect area=m_tooltip
        ? placeTooltip(size(),anchor,screenRect(anchor.center()),placement,&chosen)
        : place(size(),anchor,screenRect(anchor.center()));
    setGeometry(area);
    if (!m_tooltip && chosen==Placement::Auto)
        chosen=area.top()>=anchor.bottom()?Placement::Below:Placement::Above;
    m_placement=chosen;
    m_top=chosen==Placement::Below;
    m_connectorX=anchor.center().x()-area.left();
    m_connectorY=anchor.center().y()-area.top();
    m_material=renderMaterial(capture(area,devicePixelRatioF()),size(),devicePixelRatioF(),false,
        QRectF(rect()).adjusted(8,12,-8,-12));
    if(m_content) {m_content->setGeometry(20,24,width()-40,height()-48);m_content->hide();}
    m_progress=theme().reducedMotion?1:0;
    show();
    // Some X11 window managers apply their own tooltip placement during map.
    // Re-assert our adaptive rectangle after mapping so side bubbles remain
    // attached to the requested rail edge instead of jumping below it.
    if (m_tooltip) setGeometry(area);
    if(m_content && theme().reducedMotion) {static_cast<QGraphicsOpacityEffect *>(m_content->graphicsEffect())->setOpacity(1);m_content->show();}
    if(!theme().reducedMotion) {
        m_motion.setStartValue(0.0);m_motion.setEndValue(1.0);m_motion.setDuration(theme().openMs);
        m_motion.setEasingCurve(QEasingCurve::OutCubic);m_motion.start();
    }
}
void Shell::dismiss() {
    if(!isVisible() || m_closing)return;
    m_closing=true;if(m_content)m_content->hide();m_motion.stop();
    if(theme().reducedMotion){hide();return;}
    m_motion.setStartValue(m_progress);m_motion.setEndValue(0.0);
    m_motion.setDuration(qMax(1,qRound(theme().closeMs*m_progress)));
    m_motion.setEasingCurve(QEasingCurve::InCubic);m_motion.start();
}
void Shell::hideEvent(QHideEvent *e){m_motion.stop();m_progress=0;m_material={};QWidget::hideEvent(e);}
void Shell::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape)dismiss();else QWidget::keyPressEvent(e);}
void Shell::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const QRectF body=QRectF(rect()).adjusted(8,12,-8,-12);
    QPointF origin;
    qreal connector = m_connectorX;
    if (m_placement==Placement::Right || m_placement==Placement::Left) {
        connector=m_connectorY;
        origin=QPointF(m_placement==Placement::Right?body.left():body.right(),connector);
    } else {
        origin=QPointF(m_connectorX,m_top?body.top():body.bottom());
    }
    p.translate(origin);p.scale(.93+.07*m_progress,.82+.18*m_progress);p.translate(-origin);
    const auto path=bubblePath(body,theme().radius,connector,m_placement);
    p.setOpacity(m_progress);
    p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,25));
    for(int i=5;i>0;--i){p.save();p.translate(0,i);p.drawPath(path);p.restore();}
    p.save();p.setClipPath(path);p.drawImage(rect(),m_material);p.restore();
    QLinearGradient edge(body.topLeft(),body.bottomRight());
    edge.setColorAt(0,QColor(255,255,255,155));edge.setColorAt(.45,QColor(255,255,255,20));
    edge.setColorAt(1,QColor(160,195,220,65));
    p.setBrush(Qt::NoBrush);p.setPen(QPen(edge,1));p.drawPath(path);
}
void showText(const QPoint &pos,const QString &text,QWidget *owner,const QRect &ownerRect,int duration) {
    if (!enabled) { QToolTip::showText(pos,text,owner,ownerRect,duration); return; }
    if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0") {QToolTip::showText(pos,text,owner,ownerRect,duration);return;}
    if(text.isEmpty()){hideText();return;}
    if(tip && tip->isVisible() && tipText==text && tipOwner==owner && !tip->isClosing()) return;
    if(!tip) {
        tip=new Shell(nullptr,true);tipTimeout=new QTimer(tip);tipTimeout->setSingleShot(true);
        QObject::connect(tipTimeout,&QTimer::timeout,tip,[]{hideText();});
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,tip,[]{delete tip.data();tipTimeout=nullptr;});
    }
    tip->hide();tipText=text;tipOwner=owner;tipOwnerRect=ownerRect;
    auto *label=new QLabel;label->setTextFormat(Qt::AutoText);label->setText(text);
    label->setWordWrap(true);label->setMaximumWidth(qMin(380,screenRect(pos).width()-64));
    label->setStyleSheet("QLabel { color: #f5f7fc; background: transparent; font-size: 13px; }");
    label->ensurePolished();
    QTextDocument measure;measure.setDocumentMargin(0);measure.setDefaultFont(label->font());
    if(Qt::mightBeRichText(text)) measure.setHtml(text);else measure.setPlainText(text);
    const int textWidth=qMin(label->maximumWidth(),qMax(48,qCeil(measure.idealWidth())+6));
    label->setFixedSize(textWidth,qMax(label->fontMetrics().height(),label->heightForWidth(textWidth)));
    Placement preferred=Placement::Auto;
    QRect anchor(pos,QSize(1,1));
    // Controls in a narrow left/right rail should open toward the content
    // area. The old point-only placement opened below the control, causing a
    // bottom rail tooltip to cover application tiles and look non-adaptive.
    if (owner && !ownerRect.isEmpty() && owner->width()>0) {
        const QRect ownerGlobal(owner->mapToGlobal(ownerRect.topLeft()),ownerRect.size());
        const bool narrowOwner=owner->width()>=ownerRect.width()*3;
        if (narrowOwner && ownerRect.left()<=8) {
            preferred=Placement::Right; anchor=ownerGlobal;
        } else if (narrowOwner && ownerRect.right()>=owner->width()-8) {
            preferred=Placement::Left; anchor=ownerGlobal;
        }
    }
    tip->setContent(label);tip->openAt(anchor,preferred);
    tipTimeout->start(duration>0?duration:7000);
}
void hideText(){if(tip)tip->dismiss();QToolTip::hideText();}
void install(QApplication &app) {
    if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0" || app.property("liquidPopupsInstalled").toBool())return;
    app.setProperty("liquidPopupsInstalled",true);
    app.installEventFilter(new Filter(&app));
    qInfo()<<"[LiquidPopup] shared popup v0.2 enabled: menus, widget and item-view tooltips";
}
}
