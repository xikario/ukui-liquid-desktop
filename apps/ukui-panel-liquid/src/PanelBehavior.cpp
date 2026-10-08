#include "PanelBehavior.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QAbstractButton>
#include <QToolButton>
#include <QLayout>
#include <QGridLayout>
#include <QBoxLayout>
#include <QScrollArea>
#include <QScreen>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLibrary>
#include <QDebug>
#include <QElapsedTimer>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QDrag>
#include <QMimeData>
#include <QDropEvent>
#include <algorithm>
#include <functional>
#include <dlfcn.h>

// GIO is already loaded by OEM QGSettings. Resolve its stable public C API so
// the style also loads on Qt hosts without GSettings development packages.
class PanelSettings {
public:
    PanelSettings(std::function<void()> changed):m_changed(std::move(changed)) {
        auto source=resolve<void *(*)()>("g_settings_schema_source_get_default");
        auto lookup=resolve<void *(*)(void *,const char *,int)>("g_settings_schema_source_lookup");
        auto has=resolve<int (*)(void *,const char *)>("g_settings_schema_has_key");
        auto release=resolve<void (*)(void *)>("g_settings_schema_unref");
        auto create=resolve<void *(*)(void *,void *,const char *)>("g_settings_new_full");
        m_get=resolve<int (*)(void *,const char *)>("g_settings_get_boolean");
        m_set=resolve<int (*)(void *,const char *,int)>("g_settings_set_boolean");
        m_unref=resolve<void (*)(void *)>("g_object_unref");
        auto connect=resolve<unsigned long (*)(void *,const char *,void (*)(),void *,void (*)(),int)>("g_signal_connect_data");
        if(!source || !lookup || !has || !release || !create || !m_get || !m_unref || !connect)return;
        void *schemas=source();if(!schemas)return;
        void *schema=lookup(schemas,"org.ukui.panel.settings",1);if(!schema)return;
        m_hasInline=has(schema,"statusnotifierbutton");
        if(has(schema,"showtaskview"))m_value=create(schema,nullptr,nullptr);
        release(schema);
        if(m_value) {
            connect(m_value,"changed::showtaskview",reinterpret_cast<void (*)()>(&PanelSettings::changed),this,nullptr,0);
            taskView(); // GSettings only notifies a key read after connection.
            if(m_hasInline) {
                connect(m_value,"changed::statusnotifierbutton",reinterpret_cast<void (*)()>(&PanelSettings::changed),this,nullptr,0);
                m_get(m_value,"statusnotifierbutton");
            }
        }
    }
    ~PanelSettings(){if(m_value)m_unref(m_value);}
    bool valid() const{return m_value;}
    bool taskView() const{return !m_value || m_get(m_value,"showtaskview");}
    void foldInlineTray(){if(m_value && m_hasInline && m_set && m_get(m_value,"statusnotifierbutton"))m_set(m_value,"statusnotifierbutton",0);}
private:
    template<class T>T resolve(const char *name){return reinterpret_cast<T>(m_gio.resolve(name));}
    static void changed(void *,char *,void *data){static_cast<PanelSettings *>(data)->m_changed();}
    QLibrary m_gio{QStringLiteral("libgio-2.0.so.0")};
    void *m_value=nullptr;
    int (*m_get)(void *,const char *)=nullptr;
    int (*m_set)(void *,const char *,int)=nullptr;
    bool m_hasInline=false;
    void (*m_unref)(void *)=nullptr;
    std::function<void()> m_changed;
};

// OEM hides a button whose SNI status is Passive; that hide is the app's
// request and must survive. Every other OEM setVisible(false) on hidden-group
// buttons only reflects the inline fold state, which the popup replaces.
static void *nativeSymbol(QWidget *widget,const char *name) {
    if(void *symbol=dlsym(RTLD_DEFAULT,name))return symbol;
    // Qt loads panel plugins with local symbol visibility. Resolve against
    // the already-loaded module owning this class, without loading another.
    Dl_info info{};
    if(!widget || !dladdr(widget->metaObject(),&info) || !info.dli_fname)return nullptr;
    void *module=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);
    if(!module)return nullptr;
    void *symbol=dlsym(module,name);dlclose(module);return symbol;
}
static bool passive(QWidget *button) {
    using Status=int (*)(QWidget *);
    static const auto status=reinterpret_cast<Status>(nativeSymbol(button,"_ZN20StatusNotifierButton9getStatusEv"));
    return status && button->inherits("StatusNotifierButton") && status(button)==0;
}
static int actionArea(QWidget *button) {
    using Area=int (*)(QWidget *);
    static const auto area=reinterpret_cast<Area>(nativeSymbol(button,"_ZN20StatusNotifierButton13getActionAreaEv"));
    return area?area(button):0;
}

class TrayOverflow final : public QObject {
public:
    explicit TrayOverflow(QWidget *home):QObject(home),m_home(home) {
        m_popup=new LiquidPopup::Shell(home);m_popup->setObjectName("liquidTrayOverflow");
        m_popup->setWindowTitle("隐藏的托盘图标");
        m_scroll=new QScrollArea;m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setStyleSheet("QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;border:0;}");
        m_content=new QWidget;m_grid=new QGridLayout(m_content);
        m_grid->setContentsMargins(0,0,0,0);m_grid->setSpacing(6);m_scroll->setWidget(m_content);
        m_popup->setContent(m_scroll);m_popup->hide();
        m_popup->installEventFilter(this);
        m_resync.setSingleShot(true);m_resync.setInterval(0);
        connect(&m_resync,&QTimer::timeout,this,&TrayOverflow::sync);
        m_openPending.setSingleShot(true);m_openPending.setInterval(tipSettleMs);
        connect(&m_openPending,&QTimer::timeout,this,[this]{open();});
        QSettings settings(configFile(),QSettings::IniFormat);
        m_savedOrder=settings.value("tray/hiddenOrder").toStringList();
        qApp->installEventFilter(this);
    }
    ~TrayOverflow() override{if(qApp)qApp->removeEventFilter(this);}
    void sync() {
        if(m_syncing || !m_home || !m_home->layout())return;
        m_syncing=true;
        bool changed=false;
        auto *layout=m_home->layout();int arrowIndex=-1;
        for(int i=0;i<layout->count();++i) {
            auto *w=layout->itemAt(i)->widget();
            if(w && w->inherits("StatusNotifierStorageArrow")){m_arrow=qobject_cast<QToolButton *>(w);arrowIndex=i;break;}
        }
        if(!m_arrow){m_syncing=false;return;}
        // Native resetLayout and its animated moves can temporarily leave
        // STORAGE items after the already-existing arrow. Their action area
        // is authoritative; index order alone can bounce items between hosts.
        QList<QWidget *> hidden,shown;
        for(int i=0;i<layout->count();++i) {
            auto *w=layout->itemAt(i)->widget();
            if(w && w->inherits("StatusNotifierButton")) {
                const int area=actionArea(w);
                if(area==1 || (area==0 && i<arrowIndex))hidden.append(w);else shown.append(w);
            }
        }
        for(auto *w:shown) {
            for(int i=m_buttons.size()-1;i>=0;--i)if(m_buttons[i]==w){m_buttons.removeAt(i);changed=true;}
        }
        for(auto *w:hidden) {
            layout->removeWidget(w);
            if(!m_buttons.contains(w)) {
                m_buttons.append(w);
                connect(w,&QObject::destroyed,this,[this]{resync();});
            }
            w->setParent(m_content);
            sortId(w);
            changed=true;
        }
        if(!hidden.isEmpty()) {
            QList<QPointer<QWidget>> ordered;
            for(auto *w:hidden)ordered.append(w);
            for(const auto &w:m_buttons)if(w && !ordered.contains(w))ordered.append(w);
            m_buttons=ordered; // Follow OEM's persisted order after a drag/reset.
        }
        for(int i=m_buttons.size()-1;i>=0;--i)if(!m_buttons[i]){m_buttons.removeAt(i);changed=true;}
        if(!m_savedOrder.isEmpty())std::stable_sort(m_buttons.begin(),m_buttons.end(),[this](const auto &a,const auto &b){
            const int ai=m_savedOrder.indexOf(sortId(a)),bi=m_savedOrder.indexOf(sortId(b));
            return (ai<0?m_savedOrder.size():ai)<(bi<0?m_savedOrder.size():bi);
        });
        QList<QWidget *> active;
        for(const auto &w:m_buttons) {
            if(w->parentWidget()!=m_content)w->setParent(m_content);
            if(passive(w))w->hide();else active.append(w);
        }
        // Keep four/five items in a compact upward grid as well as larger sets.
        const int columns=qMin(3,qMax(1,active.size()));
        const int cell=qMax(32,qMax(m_arrow->width(),m_arrow->height()));
        if(changed || cell!=m_cell || active!=m_laidOut) {
            m_cell=cell;m_laidOut=active;
            while(auto *item=m_grid->takeAt(0))delete item;
            for(int i=0;i<active.size();++i)m_grid->addWidget(active[i],i/columns,i%columns);
            const int rows=qMax(1,(active.size()+columns-1)/columns);
            const int width=columns*cell+(columns-1)*6;
            const auto *screen=QGuiApplication::screenAt(m_arrow->mapToGlobal(m_arrow->rect().center()));
            const int maxHeight=screen?qMax(cell,screen->availableGeometry().height()/2):400;
            m_scroll->setFixedSize(width,qMin(maxHeight,rows*cell+(rows-1)*6));
            // The popup's rows are independent of the OEM tray geometry.
            // Invalidating home here feeds its realign/animation back into us.
        }
        // OEM resetLayout/exchangeHideAndShow call setVisible(false) on these
        // buttons; inside the popup that left an empty glass bubble.
        for(auto *w:active)if(w->isHidden())w->show();
        // Keep the open bubble attached above the arrow: a removed row shrinks
        // it toward the arrow instead of leaving it floating at the old top.
        m_popup->reanchor(arrowRect(),m_scroll->size()+QSize(40,48));
        const bool open=m_popup->isVisible() && !m_popup->isClosing();
        syncArrow(open);
        if(active.isEmpty())m_arrow->setToolTip("暂无隐藏图标");
        m_home->setProperty("liquidTrayHiddenCount",active.size());
        if(active.isEmpty() || !m_home->isVisible()){cancelOpen();m_popup->hide();}
        m_syncing=false;
    }
    void toggle() {
        if(m_openPending.isActive()){cancelOpen();return;}
        if(m_popup->isVisible() && !m_popup->isClosing()){m_popup->dismiss();syncArrow(false);return;}
        sync();if(m_laidOut.isEmpty())return;
        // OEM CustomToolTip is anchored in enterEvent, so cancel that hover
        // through its normal leave handler before it can cover the popup.
        QEvent leave(QEvent::Leave);QApplication::sendEvent(m_arrow,&leave);
        LiquidPopup::hideText();
        // Drop tips at once and let the compositor repaint before sampling.
        if(dropNativeTips()) {
            syncArrow(true);
            m_openPending.start();
            return;
        }
        open();
    }
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if(m_menuButton && !m_rootMenu && event->type()==QEvent::MouseButtonPress && object!=m_menuButton) {
            const auto *mouse=static_cast<QMouseEvent *>(event);
            // A right press ignored by the button propagates to its ancestors.
            // Only another input sequence invalidates the pending association.
            if(mouse->button()!=Qt::RightButton || mouse->globalPos()!=m_menuPressPosition || mouse->timestamp()!=m_menuPressTimestamp) {
                m_menuButton.clear();m_menuRequest.invalidate();
            }
        }
        if(m_openPending.isActive() && event->type()==QEvent::MouseButtonPress && object!=m_arrow)cancelOpen();
        if(m_openPending.isActive() && event->type()==QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape)cancelOpen();
        auto *button=qobject_cast<QWidget *>(object);
        if(button && button->inherits("StatusNotifierButton")) {
            if(event->type()==QEvent::MouseButtonPress) {
                auto *mouse=static_cast<QMouseEvent *>(event);
                m_pressed.clear();m_suppressRelease=false;
                if(mouse->button()==Qt::RightButton && (m_buttons.contains(button) || button->parentWidget()==m_home)) {
                    m_menuButton=button;m_menuAnchor=QRect(button->mapToGlobal(QPoint()),button->size());
                    m_menuPressPosition=mouse->globalPos();m_menuPressTimestamp=mouse->timestamp();
                    m_menuRequest.start();m_rootMenu.clear();
                }
                if(mouse->button()==Qt::LeftButton && m_buttons.contains(button)) {
                    m_pressed=button;m_pressPosition=mouse->pos();m_suppressRelease=false;
                }
            } else if(event->type()==QEvent::MouseMove && m_pressed==button) {
                auto *mouse=static_cast<QMouseEvent *>(event);
                if((mouse->buttons()&Qt::LeftButton) && (mouse->pos()-m_pressPosition).manhattanLength()>=QApplication::startDragDistance()) {
                    startDrag(button);return true;
                }
            } else if(event->type()==QEvent::MouseButtonRelease && m_pressed==button
                && static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton) {
                m_pressed.clear();if(m_suppressRelease){m_suppressRelease=false;return true;}
            }
            if(event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove || event->type()==QEvent::Drop) {
                auto *drop=static_cast<QDropEvent *>(event);
                auto *source=qobject_cast<QWidget *>(drop->source());
                if(!source)source=m_dragSource;
                if(source && source->inherits("StatusNotifierButton") && source!=button
                    && (m_buttons.contains(source) || m_buttons.contains(button))
                    && (m_buttons.contains(button) || button->parentWidget()==m_home)) {
                    drop->setDropAction(Qt::MoveAction);drop->accept();
                    if(event->type()==QEvent::Drop) {
                        if(m_buttons.contains(source) && m_buttons.contains(button))reorder(source,button);
                        else migrate(source,button);
                    }
                    return true;
                }
            }
        }
        if(auto *menu=qobject_cast<QMenu *>(object)) {
            if(event->type()==QEvent::Show && m_menuButton && m_menuRequest.isValid() && m_menuRequest.elapsed()<2000 && !m_rootMenu) {
                bool submenu=false;
                for(auto *w:QApplication::topLevelWidgets())if(auto *parent=qobject_cast<QMenu *>(w))
                    if(parent!=menu && parent->isVisible())for(auto *action:parent->actions())if(action->menu()==menu)submenu=true;
                if(!submenu)m_rootMenu=menu;
            }
            if(menu==m_rootMenu && (event->type()==QEvent::Show || event->type()==QEvent::Resize))anchorMenu(menu);
            if(menu==m_rootMenu && event->type()==QEvent::Hide){m_rootMenu.clear();m_menuButton.clear();m_menuRequest.invalidate();}
        }
        if(object==m_popup) {
            if(event->type()==QEvent::Hide && m_arrow){cancelOpen();m_lastHide.start();syncArrow(false);}
            // A click on the arrow while open collapses with motion. Other
            // outside clicks keep Qt's instant close and replay to the target.
            if(event->type()==QEvent::MouseButtonPress && m_arrow) {
                auto *mouse=static_cast<QMouseEvent *>(event);
                if(mouse->button()==Qt::LeftButton && arrowRect().contains(mouse->globalPos())){toggle();return true;}
            }
        }
        if(object==m_arrow) {
            if(event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove || event->type()==QEvent::Drop) {
                auto *drop=static_cast<QDropEvent *>(event);auto *source=qobject_cast<QWidget *>(drop->source());
                if(!source)source=m_dragSource;
                if(source && source->inherits("StatusNotifierButton")
                    && (m_buttons.contains(source) || source->parentWidget()==m_home)) {
                    drop->setDropAction(Qt::MoveAction);drop->accept();
                    if(event->type()==QEvent::Drop && !m_buttons.contains(source))migrate(source,nullptr);
                    return true;
                }
            }
            // Filters run before OEM enterEvent shows its tooltip, so this is
            // the last moment the area above the arrow is clean to sample.
            if(event->type()==QEvent::Enter && !m_popup->isVisible() && !m_laidOut.isEmpty())
                m_popup->prime(arrowRect(),LiquidPopup::Placement::Above);
            if(event->type()==QEvent::Leave && m_pendingFromMouse && m_openPending.isActive())cancelOpen();
            if(event->type()==QEvent::MouseButtonPress && static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton) {
                if(m_lastHide.isValid() && m_lastHide.elapsed()<180)return true;
                m_pendingFromMouse=true;
                toggle();return true;
            }
            if(event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton)return true;
            // OEM mouseMoveEvent always starts a modal QDrag on the arrow.
            if(event->type()==QEvent::MouseMove)return true;
            if(event->type()==QEvent::KeyPress) {
                const int key=static_cast<QKeyEvent *>(event)->key();
                if(key==Qt::Key_Space || key==Qt::Key_Return || key==Qt::Key_Up){m_pendingFromMouse=false;toggle();return true;}
            }
            if(event->type()==QEvent::Move)resync();
        }
        // Arrow hover would re-show the OEM tip over the open popup.
        if(event->type()==QEvent::Show && object->inherits("CustomToolTip")
            && m_popup->isVisible() && !m_popup->isClosing())
            QTimer::singleShot(0,this,[this]{dropNativeTips();});
        if(event->type()==QEvent::Hide && object->isWidgetType() && static_cast<QWidget *>(object)->isHidden()
            && static_cast<QWidget *>(object)->parentWidget()==m_content)resync();
        if(m_home && (object==m_home || object==m_home->window())) {
            if(event->type()==QEvent::Hide){cancelOpen();m_popup->hide();}
            // The tray shifts whenever neighbours resize; follow it instead
            // of closing the popup under the pointer.
            else if(event->type()==QEvent::Move || event->type()==QEvent::Resize)resync();
        }
        return false;
    }
private:
    static constexpr int tipSettleMs=50;
    static QString configFile(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-panel.ini";}
    static const char *dragMime(){return "application/x-ukui-liquid-tray-button";}
    void cancelOpen(){m_openPending.stop();syncArrow(false);}
    void open(){
        if(!m_home || !m_home->isVisible() || !m_arrow || !m_arrow->isVisible() || m_laidOut.isEmpty()) {cancelOpen();return;}
        m_popup->openAt(arrowRect(),LiquidPopup::Placement::Above);syncArrow(true);
    }
    QString sortId(QWidget *button) {
        if(!button)return {};
        if(button->property("liquidTraySortIdRead").toBool())return button->property("liquidTraySortId").toString();
        // This OEM getter performs an asynchronous Id refresh. Call once after
        // the ready button enters its layout, never on every repaint or sync.
        using Id=QString (*)(QWidget *);
        static const auto id=reinterpret_cast<Id>(nativeSymbol(button,"_ZN20StatusNotifierButton28hideAbleStatusNotifierButtonEv"));
        QString value=button->property("liquidTraySortId").toString();
        if(value.isEmpty() && id)value=id(button);
        button->setProperty("liquidTraySortId",value);button->setProperty("liquidTraySortIdRead",true);
        if(value.isEmpty() && id && !button->property("liquidTraySortIdRetried").toBool()) {
            button->setProperty("liquidTraySortIdRetried",true);QPointer<QWidget> guard(button);
            QTimer::singleShot(250,this,[this,guard]{
                if(!guard)return;guard->setProperty("liquidTraySortIdRead",false);sortId(guard);resync();
            });
        }
        return value;
    }
    void reorder(QWidget *source,QWidget *target) {
        const int from=m_buttons.indexOf(source),to=m_buttons.indexOf(target);
        if(from<0 || to<0 || from==to)return;
        m_buttons.move(from,to);
        QStringList order;
        for(const auto &w:m_buttons){const QString id=sortId(w);if(!id.isEmpty() && !order.contains(id))order.append(id);}
        for(const auto &id:m_savedOrder)if(!order.contains(id))order.append(id);
        m_savedOrder=order;QSettings settings(configFile(),QSettings::IniFormat);settings.setValue("tray/hiddenOrder",order);
        sync();
    }
    void startDrag(QWidget *button) {
        if(m_dragSource)return;
        QPointer<TrayOverflow> guard(this);m_dragSource=button;m_suppressRelease=true;
        QPointer<QDrag> drag=new QDrag(button);auto *mime=new QMimeData;mime->setData(dragMime(),QByteArray("move"));drag->setMimeData(mime);
        if(auto *tool=qobject_cast<QAbstractButton *>(button)) {
            drag->setPixmap(tool->icon().pixmap(QSize(32,32)));tool->setDown(false);
        }
        drag->exec(Qt::MoveAction);if(drag)drag->deleteLater();
        if(guard)m_dragSource.clear();
    }
    void migrate(QWidget *source,QWidget *target) {
        QWidget *owner=m_home;
        while(owner && !owner->inherits("StatusNotiferScrollArea"))owner=owner->parentWidget();
        const QString from=sortId(source),to=sortId(target);
        using Save=void (*)(QWidget *,QString,QString);using Reset=void (*)(QWidget *);
        static const auto save=reinterpret_cast<Save>(nativeSymbol(owner,"_ZN23StatusNotiferScrollArea12saveSettingsE7QStringS0_"));
        static const auto reset=reinterpret_cast<Reset>(nativeSymbol(owner,"_ZN23StatusNotiferScrollArea11resetLayoutEv"));
        if(!owner || !save || from.isEmpty() || (target && to.isEmpty()) || (!target && !reset))return;
        // Let the native settings owner update its own maps; never feed -1
        // layout indexes to its animation/switchButtons path.
        save(owner,from,to);if(!target)reset(owner);resync();
    }
    void anchorMenu(QMenu *menu) {
        const auto *screen=QGuiApplication::screenAt(m_menuAnchor.center());
        if(!screen || !menu->width() || !menu->height())return;
        const QRect available=screen->availableGeometry();
        const int x=qBound(available.left(),m_menuAnchor.center().x()-menu->width()/2,qMax(available.left(),available.right()-menu->width()+1));
        const int y=qMax(available.top(),m_menuAnchor.top()-8-menu->height());
        if(menu->pos()!=QPoint(x,y))menu->move(x,y);
    }
    // Both OEM CustomToolTip and the liquid tooltip fade out over several
    // frames; any tip still mapped is captured into the glass as ghost text.
    bool dropNativeTips() const {
        bool dropped=false;
        for(auto *w:QApplication::topLevelWidgets()) {
            if(w==m_popup || !w->isVisible())continue;
            if(w->inherits("CustomToolTip") || w->windowType()==Qt::ToolTip){w->hide();dropped=true;}
        }
        return dropped;
    }
    QRect arrowRect() const{return m_arrow?QRect(m_arrow->mapToGlobal(QPoint()),m_arrow->size()):QRect();}
    void syncArrow(bool open) {
        if(!m_arrow)return;
        if(m_upIcon.isNull())m_upIcon=QIcon::fromTheme("ukui-up-symbolic");
        if(m_downIcon.isNull())m_downIcon=QIcon::fromTheme("ukui-down-symbolic");
        const QIcon &icon=open?m_downIcon:m_upIcon;
        // setIcon() invalidates the native layout even if visually identical.
        // Reuse the icon and avoid a LayoutRequest -> sync -> setIcon loop.
        if(m_arrow->icon().cacheKey()!=icon.cacheKey())m_arrow->setIcon(icon);
        const QString tooltip=open?"收起隐藏图标":"向上展开隐藏图标";
        if(m_arrow->toolTip()!=tooltip)m_arrow->setToolTip(tooltip);
    }
    // Coalesced: OEM emits several hides/moves per layout reset.
    void resync(){if(!m_resync.isActive())m_resync.start();}
private:
    QPointer<QWidget> m_home;
    QPointer<QToolButton> m_arrow;
    LiquidPopup::Shell *m_popup;
    QScrollArea *m_scroll;
    QWidget *m_content;
    QGridLayout *m_grid;
    QList<QPointer<QWidget>> m_buttons;
    bool m_syncing=false;
    int m_cell=-1;
    QList<QWidget *> m_laidOut;
    QElapsedTimer m_lastHide;
    QTimer m_resync;
    QTimer m_openPending;
    QStringList m_savedOrder;
    QPointer<QWidget> m_pressed,m_dragSource,m_menuButton;
    QPoint m_pressPosition;
    bool m_suppressRelease=false;
    bool m_pendingFromMouse=false;
    QPointer<QMenu> m_rootMenu;
    QRect m_menuAnchor;
    QPoint m_menuPressPosition;
    ulong m_menuPressTimestamp=0;
    QElapsedTimer m_menuRequest;
    QIcon m_upIcon,m_downIcon;
};

PanelBehavior::PanelBehavior(QObject *parent):QObject(parent) {
    m_pending.setSingleShot(true);m_pending.setInterval(0);
    connect(&m_pending,&QTimer::timeout,this,&PanelBehavior::reconcile);
    m_settings=std::make_unique<PanelSettings>([this]{schedule();});
    qApp->installEventFilter(this);schedule();
}
PanelBehavior::~PanelBehavior(){if(qApp)qApp->removeEventFilter(this);}
void PanelBehavior::schedule(){if(!m_syncing && !m_pending.isActive())m_pending.start();}
void PanelBehavior::reconcile() {
    m_syncing=true;
    // This key is the OEM inline expansion state, not icon classification.
    // A stale true state (also set by native drag-enter) fights the independent
    // upward popup during icon/app removal and can resize the whole taskbar.
    for(auto *w:QApplication::allWidgets()) {
        if(w->inherits("TaskViewButton") && m_settings->valid()) {
            const bool show=m_settings->taskView();
            w->setVisible(show);
            if(auto *parent=w->parentWidget())
                if(auto *divider=parent->findChild<QWidget *>("Divider",Qt::FindDirectChildrenOnly))divider->setVisible(show);
        }
        if(w->objectName()=="StatusNotifier_statusNotifierWidget" && !w->property("liquidTrayAttached").toBool()) {
            w->setProperty("liquidTrayAttached",true);auto *tray=new TrayOverflow(w);m_trays.insert(tray);
            connect(tray,&QObject::destroyed,this,[this,tray]{m_trays.remove(tray);});
        }
    }
    if(!m_trays.isEmpty())m_settings->foldInlineTray();
    for(auto *tray:m_trays)tray->sync();
    m_syncing=false;
}
bool PanelBehavior::eventFilter(QObject *object,QEvent *event) {
    auto *w=qobject_cast<QWidget *>(object);if(!w)return false;
    switch(event->type()) {
    case QEvent::Show:
        if(w->inherits("TaskViewButton") && m_settings->valid() && !m_settings->taskView())w->hide();
        Q_FALLTHROUGH();
    case QEvent::Polish:case QEvent::ChildPolished:case QEvent::ParentChange:case QEvent::LayoutRequest:
        if(w->inherits("TaskViewButton") || w->inherits("StatusNotifierButton")
            || w->inherits("StatusNotifierStorageArrow") || w->objectName()=="StatusNotifier_statusNotifierWidget"
            || w->inherits("UKUIStartBarWidget"))schedule();
        break;
    default:break;
    }
    return false;
}
