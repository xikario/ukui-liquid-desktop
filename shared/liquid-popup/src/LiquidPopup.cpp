#include <QElapsedTimer>
#include <QDebug>
#include "LiquidPopup.h"
#include <QHelpEvent>
#include <QAbstractItemView>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QProxyStyle>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOption>
#include <QToolTip>
#include <QTextDocument>
#include <QWidgetAction>
#include <QCache>
#include <QDataStream>
#include <QMouseEvent>
#include <memory>
#include <limits>
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
struct LensGeometry {
    QVector<qreal> qx,qy,rimX,rimY,cornerRim;
    QVector<int> sampleX,sampleY,cornerX,cornerY,cornerSample;
    int cornerWidth=0;
};
QCache<QByteArray, LensGeometry> lensCache(2*1024*1024);
quint64 lensBuilds=0, lensHits=0;
struct LensPoint { qreal rim; int sample; };
LensPoint lensPoint(int x,int y,QSize size,qreal dpr,QRectF bounds,qreal radius,qreal refraction) {
    const qreal px=(x+.5)/dpr,py=(y+.5)/dpr;
    const qreal sx=px-bounds.center().x(),sy=py-bounds.center().y();
    const qreal qx=std::abs(sx)-bounds.width()/2+radius;
    const qreal qy=std::abs(sy)-bounds.height()/2+radius;
    const qreal vx=qMax(qx,0.0),vy=qMax(qy,0.0),length=std::hypot(vx,vy);
    const qreal distance=length+qMin(qMax(qx,qy),0.0)-radius;
    const qreal rim=std::exp(-qMax(-distance,0.0)/6.0);
    const qreal nx=(sx<0?-1:1)*(length>0?vx/length:(qx>qy?1:0));
    const qreal ny=(sy<0?-1:1)*(length>0?vy/length:(qx>qy?0:1));
    return {rim,qBound(0,qRound(y-ny*refraction*rim*dpr),size.height()-1)*size.width()
        +qBound(0,qRound(x-nx*refraction*rim*dpr),size.width()-1)};
}
const LensGeometry *geometryFor(QSize size,qreal dpr,QRectF bounds,qreal radius,qreal refraction) {
    // Cache geometry only: every opening still uses its own current backdrop.
    QByteArray key;
    QDataStream stream(&key,QIODevice::WriteOnly);
    stream<<size<<dpr<<bounds<<radius<<refraction;
    if(auto *found=lensCache.object(key)){++lensHits;return found;}
    auto geometry=std::make_unique<LensGeometry>();
    QVector<int> cornerXs,cornerYs;
    auto axis=[&](int extent,qreal center,qreal half,QVector<qreal> &q,QVector<qreal> &rim,
                  QVector<int> &sample,QVector<int> &corner,QVector<int> &positions,int stride) {
        q.resize(extent);rim.resize(extent);sample.resize(extent);corner.fill(-1,extent);
        for(int i=0;i<extent;++i) {
            const qreal displacement=(i+.5)/dpr-center;
            q[i]=std::abs(displacement)-half+radius;
            rim[i]=std::exp(-qMax(radius-q[i],0.0)/6.0);
            sample[i]=qBound(0,qRound(i-(displacement<0?-1:1)*refraction*rim[i]*dpr),extent-1)*stride;
            if(q[i]>0){corner[i]=positions.size();positions.append(i);}
        }
    };
    axis(size.width(),bounds.center().x(),bounds.width()/2,geometry->qx,geometry->rimX,
        geometry->sampleX,geometry->cornerX,cornerXs,1);
    axis(size.height(),bounds.center().y(),bounds.height()/2,geometry->qy,geometry->rimY,
        geometry->sampleY,geometry->cornerY,cornerYs,size.width());
    const qint64 corners=qint64(cornerXs.size())*cornerYs.size();
    const qint64 cost=(qint64(size.width())+size.height())*(2*sizeof(qreal)+2*sizeof(int))
        +corners*(sizeof(qreal)+sizeof(int))+sizeof(LensGeometry)+256;
    if(cost>lensCache.maxCost() || corners>std::numeric_limits<int>::max())return nullptr;
    geometry->cornerWidth=cornerXs.size();
    geometry->cornerRim.resize(int(corners));geometry->cornerSample.resize(int(corners));
    for(int cy=0;cy<cornerYs.size();++cy)for(int cx=0;cx<cornerXs.size();++cx) {
        const auto point=lensPoint(cornerXs[cx],cornerYs[cy],size,dpr,bounds,radius,refraction);
        const int index=cy*cornerXs.size()+cx;
        geometry->cornerRim[index]=point.rim;geometry->cornerSample[index]=point.sample;
    }
    ++lensBuilds;
    auto *result=geometry.release();
    lensCache.insert(key,result,int(cost));
    return result;
}

class MenuGlyphStyle final : public QProxyStyle {
public:
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override {
        if (isEnabled() && qobject_cast<const QMenu *>(widget) &&
            widget->property("liquidPopupSkin").toBool() &&
            drawMenuGlyph(element, option, painter))
            return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

struct ProfileScope {
    const char *name; bool enabled=qEnvironmentVariableIsSet("UKUI_LIQUID_POPUP_PROFILE"); QElapsedTimer timer;
    explicit ProfileScope(const char *stage):name(stage){if(enabled)timer.start();}
    ~ProfileScope(){if(enabled)qInfo()<<"[LiquidPopupProfile]"<<name<<"us"<<timer.nsecsElapsed()/1000;}
};
QImage capture(const QRect &area, qreal dpr) {
    ProfileScope profile("capture");
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
        hover.setSingleShot(true);
        connect(&hover,&QTimer::timeout,this,[this]{deliverHover();});
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
    ~MenuSkin() override { restoreIcons(); }
    void prepare() {
            if(!enabled || styled)return;
            originalStyle = menu->styleSheet();
            originalMask = menu->mask();
            originalTranslucent = menu->testAttribute(Qt::WA_TranslucentBackground);
            originalAutoFill = menu->autoFillBackground();
            styled = true;
            originalWidgetStyle = menu->testAttribute(Qt::WA_SetStyle) ? menu->style() : nullptr;
            if (qApp->property("liquidMenuGlyphStyleInstalled").toBool()) {
                if (!glyphStyle) { glyphStyle = new MenuGlyphStyle; glyphStyle->setParent(this); }
                menu->setStyle(glyphStyle);
            }
            // Qt shares one column between an action icon and its check mark.
            // State must remain visible for toggles even when they have icons.
            for (QAction *action : menu->actions()) {
                if (action->isCheckable() && action->isIconVisibleInMenu() && !action->icon().isNull()) {
                    indicatorIcons.append(action);
                    action->setIconVisibleInMenu(false);
                }
            }
            menu->setAttribute(Qt::WA_TranslucentBackground);
            menu->setAutoFillBackground(false);
            originalMaximumHeight=menu->maximumHeight();
            capHeight();
            updateItemMetrics();
            menu->ensurePolished();
    }
    void updateItemMetrics() {
            if (!styled || updatingMetrics) return;
            QByteArray key;
            QDataStream stream(&key,QIODevice::WriteOnly);
            stream<<menu->font()<<menu->layoutDirection();
            for(QAction *action:menu->actions()) {
                stream<<quintptr(action)<<action->isVisible()<<action->isSeparator()
                    <<action->text()<<action->font()<<action->shortcut().toString()
                    <<action->icon().cacheKey()<<bool(qobject_cast<QWidgetAction *>(action));
            }
            if(key==metricsKey)return;
            metricsKey=key;
            updatingMetrics = true;
            int labelWidth = 0, iconWidth = 0, shortcutGap = 0;
            for (QAction *action : menu->actions()) {
                if (!action->isVisible() || action->isSeparator() || qobject_cast<QWidgetAction *>(action)) continue;
                const QFontMetrics metrics(action->font().resolve(menu->font()));
                const QString label = action->text().section(QLatin1Char('\t'), 0, 0);
                labelWidth = qMax(labelWidth, metrics.boundingRect(QRect(),
                    Qt::TextSingleLine | Qt::TextShowMnemonic, label).width());
                // Qt 5 QMenu allocates this column even for hidden icons. Its
                // stylesheet sizeFromContents only counts the current item's
                // visible icon, but painting always subtracts the whole column.
                if (!action->icon().isNull())
                    iconWidth = menu->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, menu) + 4;
                if (!action->shortcut().isEmpty() || action->text().contains(QLatin1Char('\t')))
                    shortcutGap = qMax(shortcutGap, qMax(12, metrics.horizontalAdvance(QStringLiteral("  "))));
            }
            // QMenu adds the shortcut width separately. Reserve only the label,
            // shared icon column and gap here; padding keeps checks/arrows clear.
            const QString override=QStringLiteral(
                "\nQMenu { background: transparent; border: 1px solid transparent; menu-scrollable: 1; }\n"
                "QMenu::indicator { width: 14px; height: 14px; }\n"
                "QMenu::item { padding: 6px 28px; margin: 2px 6px; min-width: %1px; }\n"
                "QMenu::item:selected:enabled { background-color: palette(highlight); color: palette(highlighted-text); border-radius: 6px; }\n"
                "QMenu::separator { height: 1px; background-color: rgba(128,128,128,70); margin: 4px 10px; }\n")
                .arg(labelWidth + iconWidth + shortcutGap);
            const QString style = originalStyle + override;
            if (menu->styleSheet() != style) menu->setStyleSheet(style);
            updatingMetrics = false;
    }
protected:
    bool eventFilter(QObject *,QEvent *e) override {
        if (!enabled) {
            hover.stop();pendingHover.reset();
            restore();
            return false;
        }
        if(e->type()==QEvent::MouseMove && styled && menu->isVisible() && !deliveringHover) {
            auto *mouse=static_cast<QMouseEvent *>(e);
            if(mouse->buttons()==Qt::NoButton) {
                pendingHover=std::make_unique<QMouseEvent>(*mouse);
                QScreen *screen=QGuiApplication::screenAt(mouse->globalPos());
                const qreal rate=screen?screen->refreshRate():60.;
                const int interval=qBound(4,qRound(1000./(rate>0?rate:60.)),33);
                if(!hoverClock.isValid() || hoverClock.elapsed()>=interval)deliverHover();
                else if(!hover.isActive())hover.start(qMax(1,interval-int(hoverClock.elapsed())));
                return true;
            }
        } else if(e->type()==QEvent::MouseButtonPress && styled && openedClock.isValid()
                  && openedClock.elapsed()<pressGuardMs
                  && !menu->rect().contains(static_cast<QMouseEvent *>(e)->pos())) {
            // The press that opened a context menu can be replayed as an
            // outside click; drop it and its release instead of closing.
            swallowRelease=true;
            return true;
        } else if(e->type()==QEvent::MouseButtonRelease && swallowRelease) {
            swallowRelease=false;
            return true;
        } else if(e->type()==QEvent::MouseButtonPress || e->type()==QEvent::MouseButtonRelease ||
                  e->type()==QEvent::KeyPress || e->type()==QEvent::Leave) {
            // Input results remain immediate, including a click between frames.
            const QPointer<MenuSkin> guard(this);
            deliverHover();
            if(!guard)return true;
        }
        if (styled && (e->type()==QEvent::ActionAdded || e->type()==QEvent::ActionRemoved ||
                       e->type()==QEvent::ActionChanged || e->type()==QEvent::FontChange)) {
            updateItemMetrics();
        }
        if(e->type()==QEvent::Show) {
            prepare();
            openedClock.start();swallowRelease=false;
            // Show follows final placement and precedes native mapping.
            // Capture only the popup footprint, not an entire monitor.
            if(tip) tip->hide();
            QToolTip::hideText();
            backdropArea=QRect(menu->mapToGlobal(QPoint()),menu->size());
            availableArea=screenRect(backdropArea.center());
            backdrop=capture(backdropArea,menu->devicePixelRatioF());
            rebuildMaterial();
            if(!theme().reducedMotion && QGuiApplication::platformName()=="xcb") {
                fade.stop();fade.setStartValue(.15);fade.setEndValue(1.0);
                fade.setDuration(theme().openMs);fade.setEasingCurve(QEasingCurve::OutCubic);fade.start();
            }
        } else if(e->type()==QEvent::Resize && styled && menu->isVisible()) {
            // QAction text can change after Show (e.g. async package lookup).
            // Keep the alpha material and native silhouette at the same size.
            // Reuse the opening snapshot: capturing a mapped menu would feed
            // its own text/rim back into the glass and produce ghost images.
            // Qt constrains initial popup placement, but not a later resize.
            // dbusmenu submenus are filled after Show: an empty menu maps at
            // zero width, so its material was null and its mask a few pixels.
            // That mask hid every lazily filled third-level menu.
            const QPoint bounded(
                qBound(availableArea.left(),menu->x(),
                       qMax(availableArea.left(),availableArea.right()-menu->width()+1)),
                qBound(availableArea.top(),menu->y(),
                       qMax(availableArea.top(),availableArea.bottom()-menu->height()+1)));
            if(menu->pos()!=bounded) menu->move(bounded);
            if(backdropArea.width()<minimumBackdrop || backdropArea.height()<minimumBackdrop) {
                // Nothing meaningful was captured while empty and masked away,
                // so this snapshot cannot contain the menu's own text.
                backdropArea=QRect(menu->mapToGlobal(QPoint()),menu->size());
                backdrop=capture(backdropArea,menu->devicePixelRatioF());
            }
            rebuildMaterial();
            menu->update();
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
    QImage alignedBackdrop() const {
        const QRect area(menu->mapToGlobal(QPoint()),menu->size());
        if(backdrop.isNull() || area==backdropArea) return backdrop;
        const qreal dpr=menu->devicePixelRatioF();
        const QImage source=backdrop.scaled(
            QSize(qMax(1,qRound(backdropArea.width()*dpr)),qMax(1,qRound(backdropArea.height()*dpr))),
            Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
        QImage aligned(QSize(qMax(1,qRound(area.width()*dpr)),qMax(1,qRound(area.height()*dpr))),
                       QImage::Format_RGB32);
        const QPoint offset(qRound((area.x()-backdropArea.x())*dpr),
                            qRound((area.y()-backdropArea.y())*dpr));
        // Keep overlapping background pixels in place instead of stretching
        // the scene when a label changes. Extend only uncaptured edge pixels.
        for(int y=0;y<aligned.height();++y) {
            const auto *row=reinterpret_cast<const QRgb *>(source.constScanLine(qBound(0,y+offset.y(),source.height()-1)));
            auto *output=reinterpret_cast<QRgb *>(aligned.scanLine(y));
            for(int x=0;x<aligned.width();++x)
                output[x]=row[qBound(0,x+offset.x(),source.width()-1)];
        }
        aligned.setDevicePixelRatio(dpr);
        return aligned;
    }
    void rebuildMaterial() {
        const bool light=menu->palette().color(QPalette::WindowText).lightness()<128;
        material=renderMenuMaterial(alignedBackdrop(),menu->size(),menu->devicePixelRatioF(),light);
        QPainterPath path;
        // Native regions are binary and use logical integer coordinates.
        // Keep their stair steps outside the antialiased material edge.
        const qreal radius=qMin(theme().radius,qMin(menu->width(),menu->height())/2.);
        path.addRoundedRect(QRectF(menu->rect()).adjusted(-2,-2,2,2),radius+2,radius+2);
        menu->setMask(QRegion(path.toFillPolygon().toPolygon()));
    }
    void restore() {
        if (!styled) return;
        hover.stop();pendingHover.reset();hoverClock.invalidate();metricsKey.clear();
        styled = false;
        fade.stop();menu->setWindowOpacity(1);material={};backdrop={};
        menu->setStyleSheet(originalStyle);
        if (glyphStyle) menu->setStyle(originalWidgetStyle);
        menu->setMask(originalMask);
        menu->setAutoFillBackground(originalAutoFill);
        menu->setAttribute(Qt::WA_TranslucentBackground, originalTranslucent);
        menu->setMaximumHeight(originalMaximumHeight);
        openedClock.invalidate();swallowRelease=false;
        restoreIcons();
    }
    // Long menus scroll inside 80% of the screen instead of splitting into
    // columns or touching the screen edges.
    void capHeight() {
        const QRect screen=screenRect(menu->isVisible()?menu->geometry().center():QCursor::pos());
        if(screen.isEmpty())return;
        menu->setMaximumHeight(qMin(originalMaximumHeight,qRound(screen.height()*.8)));
    }
    void restoreIcons() {
        // During QObject child destruction QMenu is no longer a QWidget.
        // Only restore surviving externally-owned actions here.
        for (const auto &action : indicatorIcons)
            if (action) action->setIconVisibleInMenu(true);
        indicatorIcons.clear();
    }
private:
    void deliverHover() {
        hover.stop();
        auto event=std::move(pendingHover);
        if(!event || !menu->isVisible())return;
        deliveringHover=true;
        hoverClock.start();
        const QPointer<MenuSkin> guard(this);
        QCoreApplication::sendEvent(menu,event.get());
        if(guard)deliveringHover=false;
    }
    QPointer<QStyle> glyphStyle, originalWidgetStyle;
    QString originalStyle;
    QRegion originalMask;
    bool styled = false, updatingMetrics = false, originalTranslucent = false, originalAutoFill = false;
    QMenu *menu;
    QImage material, backdrop;
    QRect backdropArea, availableArea;
    QList<QPointer<QAction>> indicatorIcons;
    QVariantAnimation fade;
    QTimer hover;
    QElapsedTimer hoverClock;
    QElapsedTimer openedClock;
    bool swallowRelease=false;
    int originalMaximumHeight=QWIDGETSIZE_MAX;
    static constexpr int pressGuardMs=250;
    std::unique_ptr<QMouseEvent> pendingHover;
    QByteArray metricsKey;
    bool deliveringHover=false;
    static constexpr int minimumBackdrop=24;
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
void reserveActionTextWidth(QMenu &menu,QAction &action,const QStringList &texts) {
    // This must run while hidden; measuring must never flash intermediate text.
    if(menu.isVisible() || !menu.actions().contains(&action)) return;
    menu.ensurePolished();
    for(QObject *child:menu.children())if(auto *skin=dynamic_cast<MenuSkin *>(child))skin->prepare();
    const QString original=action.text();
    int width=menu.sizeHint().width();
    for(const QString &text:texts) {
        action.setText(text);
        width=qMax(width,menu.sizeHint().width());
    }
    action.setText(original);
    menu.setMinimumWidth(qMax(menu.minimumWidth(),width));
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
int Motion::duration(int fullMs, qreal remaining, int floorMs) {
    if(settings.reducedMotion) return 0;
    return qMax(qMax(1,floorMs),qRound(fullMs*qBound<qreal>(0,remaining,1)));
}
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
QImage captureBackdrop(const QRect &area,qreal dpr){return capture(area,dpr);}
MaterialCacheStats materialCacheStats(){return {lensCache.totalCost(),lensBuilds,lensHits};}
void clearMaterialCache(){lensCache.clear();lensBuilds=lensHits=0;}
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
    ProfileScope profile("renderMaterial");
    if(logical.isEmpty()) return {};
    dpr=qBound(1.0,dpr,3.0);
    if(bounds.isEmpty()) bounds=QRectF(QPointF(0,0),QSizeF(logical));
    const QSize size(qCeil(logical.width()*dpr),qCeil(logical.height()*dpr));
    QImage source=input;
    if(source.isNull()) {source=QImage(size,QImage::Format_RGB32);source.fill(light?QColor(222,227,234):QColor(40,47,57));}
    source=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage body=diffuse(source,size),out(size,QImage::Format_ARGB32_Premultiplied);
    if(source.isNull() || body.isNull() || out.isNull())return {};
    const qreal radius=qMin(theme().radius,qMin(bounds.width(),bounds.height())/2);
    const auto *geometry=geometryFor(size,dpr,bounds,radius,theme().refraction);
    const auto *clearPixels=reinterpret_cast<const QRgb *>(source.constBits());
    // The optics and double precision are unchanged. Reuse geometry, move
    // row-invariant glow outside the pixel loop, and write contiguous scanlines.
    for(int y=0;y<size.height();++y) {
      const qreal py=(y+.5)/dpr;
      const qreal glow=theme().highlight*26*std::exp(-py/18.0);
      const auto *blurPixels=reinterpret_cast<const QRgb *>(body.constScanLine(y));
      auto *output=reinterpret_cast<QRgb *>(out.scanLine(y));
      for(int x=0;x<size.width();++x) {
        LensPoint point;
        if(!geometry)point=lensPoint(x,y,size,dpr,bounds,radius,theme().refraction);
        else if(geometry->cornerX[x]>=0 && geometry->cornerY[y]>=0) {
            const int index=geometry->cornerY[y]*geometry->cornerWidth+geometry->cornerX[x];
            point={geometry->cornerRim[index],geometry->cornerSample[index]};
        } else if(geometry->qx[x]>geometry->qy[y])point={geometry->rimX[x],y*size.width()+geometry->sampleX[x]};
        else point={geometry->rimY[y],geometry->sampleY[y]+x};
        const qreal rim=point.rim;
        const QRgb clear=clearPixels[point.sample],blur=blurPixels[x];
        const qreal luminance=(.2126*qRed(blur)+.7152*qGreen(blur)+.0722*qBlue(blur))/255.;
        const qreal contrastRisk=light ? qBound(0.0,(.55-luminance)/.55,1.0)
                                      : qBound(0.0,(luminance-.4)/.6,1.0);
        const qreal tint=qBound(0.0,(theme().tint+.23*contrastRisk)*(1-rim*.5),.9),target=light?244:15;
        auto c=[&](int a,int b){return qBound(0,qRound((a*(1-rim*.7)+b*rim*.7)*(1-tint)+target*tint+glow),255);};
        output[x]=qRgb(c(qRed(blur),qRed(clear)),c(qGreen(blur),qGreen(clear)),c(qBlue(blur),qBlue(clear)));
      }
    }
    out.setDevicePixelRatio(dpr);return out;
}
QImage renderMenuMaterial(const QImage &input,QSize logical,qreal dpr,bool light) {
    ProfileScope profile("renderMenuMaterial");
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
        m_progress=v.toReal();updateContent();update();
    });
    connect(&m_motion,&QVariantAnimation::finished,this,[this]{if(m_closing) hide();});
}
void Shell::setContent(QWidget *content) {
    if(m_content && m_content!=content) delete m_content;
    m_material={};
    m_content=content;if(content) {content->setGraphicsEffect(new QGraphicsOpacityEffect(content));content->setParent(this);content->adjustSize();resize(content->size()+QSize(40,48));}
}
QRect Shell::placeFor(QSize size, const QRect &anchor, Placement placement, Placement *chosen) const {
    *chosen=placement;
    return (m_tooltip || placement!=Placement::Auto)
        ? placeTooltip(size,anchor,screenRect(anchor.center()),placement,chosen)
        : place(size,anchor,screenRect(anchor.center()));
}
void Shell::applyPlacement(const QRect &area, const QRect &anchor, Placement chosen) {
    setGeometry(area);
    if (!m_tooltip && chosen==Placement::Auto)
        chosen=area.top()>=anchor.bottom()?Placement::Below:Placement::Above;
    m_placement=chosen;
    m_top=chosen==Placement::Below;
    m_anchor=anchor;
    m_connectorX=anchor.center().x()-area.left();
    m_connectorY=anchor.center().y()-area.top();
    updateContent();
}
void Shell::renderBubble(const QRect &area) {
    const qreal dpr=devicePixelRatioF();
    QImage input=m_backdrop;
    if(!input.isNull() && area!=m_backdropArea) {
        const QRect source(QPoint(qRound((area.x()-m_backdropArea.x())*dpr),qRound((area.y()-m_backdropArea.y())*dpr)),
                           QSize(qRound(area.width()*dpr),qRound(area.height()*dpr)));
        input=m_backdrop.copy(source);input.setDevicePixelRatio(dpr);
    }
    m_material=renderMaterial(input,area.size(),dpr,false,QRectF(QPointF(),QSizeF(area.size())).adjusted(8,12,-8,-12));
}
void Shell::updateContent() {
    if(!m_content) return;
    // Content trails the bubble: it fades in once the glass has mostly
    // formed and settles toward its final position from the anchor side.
    const qreal start=m_closing?.6:.45;
    const qreal shown=theme().reducedMotion?1.:qBound(0.0,(m_progress-start)/(1-start),1.0);
    const qreal lift=6*(1-shown);
    QPoint offset;
    switch(m_placement) {
    case Placement::Above: offset=QPoint(0,qRound(lift));break;
    case Placement::Below: offset=QPoint(0,-qRound(lift));break;
    case Placement::Right: offset=QPoint(-qRound(lift),0);break;
    case Placement::Left: offset=QPoint(qRound(lift),0);break;
    case Placement::Auto: break;
    }
    m_content->setGeometry(QRect(QPoint(20,24)+offset,QSize(width()-40,height()-48)));
    if(auto *effect=qobject_cast<QGraphicsOpacityEffect *>(m_content->graphicsEffect())) effect->setOpacity(shown);
    m_content->setVisible(shown>0);
}
void Shell::openAt(const QRect &anchor, Placement placement) {
    const bool reversing=m_closing && m_motion.state()==QAbstractAnimation::Running;
    m_motion.stop();m_closing=false;m_requested=placement;
    Placement chosen;
    const QRect area=placeFor(size(),anchor,placement,&chosen);
    const bool reverse = reversing && isVisible() && geometry() == area && !m_material.isNull()
        && m_material.devicePixelRatio() == devicePixelRatioF()
        && m_connectorX == anchor.center().x()-area.left()
        && m_connectorY == anchor.center().y()-area.top();
    if (!reverse) {
        constexpr qint64 primeLifeMs=8000;
        const bool primed=!m_primed.isNull() && m_primedArea==area && m_primedAge.isValid()
            && m_primedAge.elapsed()<primeLifeMs && qFuzzyCompare(m_primed.devicePixelRatio(),devicePixelRatioF());
        m_backdropArea=area;m_backdrop=primed?m_primed:capture(area,devicePixelRatioF());
    }
    m_primed={};
    m_progress=theme().reducedMotion?1:(reverse?m_progress:0);
    applyPlacement(area,anchor,chosen);
    if (!reverse) renderBubble(area);
    show();
    // Some X11 window managers apply their own tooltip placement during map.
    // Re-assert our adaptive rectangle after mapping so side bubbles remain
    // attached to the requested rail edge instead of jumping below it.
    if (m_tooltip) setGeometry(area);
    if(!theme().reducedMotion) {
        const qreal start=qBound<qreal>(0,m_progress,1);
        const int duration=Motion::duration(theme().openMs,1-start);
        // Decelerate only: a short, overshoot-free settle reads as responsive.
        const QEasingCurve curve(QEasingCurve::OutCubic);
        {
            // A stopped animation retains its previous currentTime. Changing
            // its range synchronously emits valueChanged at that old time;
            // do not let those intermediate values rewrite the start/duration.
            const QSignalBlocker block(&m_motion);
            m_motion.setDuration(duration);m_motion.setEasingCurve(curve);
            m_motion.setStartValue(start);m_motion.setEndValue(1.0);m_motion.setCurrentTime(0);
        }
        m_progress=start;updateContent();m_motion.start();
    }
}
void Shell::prime(const QRect &anchor, Placement placement) {
    if(isVisible())return;
    Placement chosen;
    m_primedArea=placeFor(size(),anchor,placement,&chosen);
    m_primed=capture(m_primedArea,devicePixelRatioF());
    m_primedAge.start();
}
void Shell::reanchor(const QRect &anchor, QSize bubble) {
    if(bubble.isEmpty()) bubble=size();
    if(!isVisible()) {resize(bubble);return;}
    Placement chosen;
    const QRect area=placeFor(bubble,anchor,m_requested,&chosen);
    if(area==geometry() && anchor==m_anchor) return;
    const qreal dpr=devicePixelRatioF();
    if(!m_backdropArea.contains(area)) {
        // Only newly uncovered screen is captured. The mapped popup may be
        // in that snapshot, so restore the old clean pixels where they overlap.
        QImage merged=capture(area,dpr);
        if(!merged.isNull() && !m_backdrop.isNull()) {
            merged=merged.convertToFormat(QImage::Format_RGB32);merged.setDevicePixelRatio(dpr);
            QPainter p(&merged);
            p.drawImage(QPointF(m_backdropArea.topLeft()-area.topLeft()),m_backdrop);
        }
        m_backdrop=merged;m_backdropArea=area;
    }
    applyPlacement(area,anchor,chosen);
    renderBubble(area);
    update();
}
void Shell::dismiss() {
    if(!isVisible() || m_closing)return;
    m_closing=true;m_motion.stop();
    if(theme().reducedMotion){hide();return;}
    const qreal start=qBound<qreal>(0,m_progress,1);
    const int duration=Motion::duration(theme().closeMs,start);
    {
        const QSignalBlocker block(&m_motion);
        m_motion.setDuration(duration);m_motion.setEasingCurve(QEasingCurve::InCubic);
        m_motion.setStartValue(start);m_motion.setEndValue(0.0);m_motion.setCurrentTime(0);
    }
    m_progress=start;updateContent();m_motion.start();
}
void Shell::hideEvent(QHideEvent *e){m_motion.stop();m_progress=0;m_closing=false;m_material={};m_backdrop={};QWidget::hideEvent(e);}
void Shell::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape)dismiss();else QWidget::keyPressEvent(e);}
void Shell::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const QRectF body=QRectF(rect()).adjusted(8,12,-8,-12);
    QPointF origin;
    qreal connector = m_connectorX;
    const bool side=m_placement==Placement::Right || m_placement==Placement::Left;
    if (side) {
        connector=m_connectorY;
        origin=QPointF(m_placement==Placement::Right?body.left():body.right(),connector);
    } else {
        origin=QPointF(m_connectorX,m_top?body.top():body.bottom());
    }
    // Grow out of the connector from 94%, slightly more along the opening axis.
    const qreal along=.94+.06*m_progress,across=.97+.03*m_progress;
    p.translate(origin);
    if(side) p.scale(along,across);else p.scale(across,along);
    p.translate(-origin);
    const auto path=bubblePath(body,theme().radius,connector,m_placement);
    p.setOpacity(qBound<qreal>(0,m_progress,1));
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
void installMenuGlyphStyle(QApplication &app) {
    if (qEnvironmentVariable("UKUI_LIQUID_POPUP") == "0") return;
    if (app.property("liquidMenuGlyphStyleInstalled").toBool()) return;
    app.setProperty("liquidMenuGlyphStyleInstalled", true);
    // Each liquid menu owns a proxy; the application style stays untouched.
}
}
