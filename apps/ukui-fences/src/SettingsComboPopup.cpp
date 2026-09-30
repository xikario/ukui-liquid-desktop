#include "SettingsComboPopup.h"
#include "DesktopCanvas.h"
#include "LiquidPopup.h"
#include <QComboBox>
#include <QAbstractItemView>
#include <QPainter>
#include <QPointer>
#include <QEvent>
#include <QKeyEvent>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QListView>
#include <QStyledItemDelegate>
#include <QLayout>
#include <QGuiApplication>
#include <QScreen>

namespace SettingsComboPopup {
namespace {
// Use an ordinary scrolling list, not Qt's menu-style combo scrollers. UKUI
// paints those private top/bottom widgets as opaque white horizontal bands.
class ListStyle final : public QProxyStyle {
public:
    ListStyle():QProxyStyle(QStyleFactory::create("Fusion")) {}
    int styleHint(StyleHint hint, const QStyleOption *option=nullptr,
                  const QWidget *widget=nullptr, QStyleHintReturn *data=nullptr) const override {
        if(hint==SH_ComboBox_Popup) return 0;
        return QProxyStyle::styleHint(hint,option,widget,data);
    }
};
// Keep Qt's list, scrolling, model, activation and keyboard handling intact.
// Only the popup container's background is replaced, once per opening.
class Skin final : public QObject {
public:
    Skin(QComboBox *combo,DesktopCanvas *canvas):QObject(combo),m_canvas(canvas),m_combo(combo),m_popup(combo->view()->window()) {
        combo->setProperty("settingsComboPopup",true);combo->setMaxVisibleItems(10);
        auto *view=combo->view();
        view->setStyleSheet("QAbstractItemView {background:transparent;color:#edf5ff;border:0;padding:0;outline:0;} QAbstractItemView::item {min-height:28px;padding:3px 10px;border-radius:6px;} QAbstractItemView::item:selected {background:rgba(100,202,194,90);color:white;}");
        view->installEventFilter(this);view->viewport()->installEventFilter(this);
        view->setAutoFillBackground(false);view->viewport()->setAutoFillBackground(false);
        m_popup->setWindowFlag(Qt::NoDropShadowWindowHint,true);
        m_popup->setAttribute(Qt::WA_TranslucentBackground);
        m_popup->setAutoFillBackground(false);m_popup->installEventFilter(this);
        m_popup->setProperty("settingsComboPopupSurface",true);
    }
protected:
    bool eventFilter(QObject *watched,QEvent *e) override {
        if ((e->type()==QEvent::ShortcutOverride || e->type()==QEvent::KeyPress) &&
            static_cast<QKeyEvent *>(e)->key()==Qt::Key_Escape) {
            e->accept();
            if(e->type()==QEvent::KeyPress)m_combo->hidePopup();
            return true;
        }
        if(watched!=m_popup)return false;
        if(e->type()==QEvent::Show){
            auto *view=m_combo->view();
            view->ensurePolished();
            if(m_popup->layout())m_popup->layout()->activate();
            view->doItemsLayout();
            // QComboBox measures before all inherited QSS row metrics settle.
            // Account for the final viewport insets and styled row sizes now.
            int rowsHeight=0;
            for(int row=0;row<qMin(m_combo->count(),m_combo->maxVisibleItems());++row)
                rowsHeight+=qMax(1,view->sizeHintForRow(row));
            const int insets=qMax(0,m_popup->height()-view->viewport()->height());
            QScreen *screen=QGuiApplication::screenAt(m_popup->pos());
            if(!screen)screen=QGuiApplication::primaryScreen();
            const QRect available=screen->availableGeometry();
            m_popup->resize(m_popup->width(),qMin(available.height(),rowsHeight+insets+2));
            m_popup->move(m_popup->x(),qBound(available.top(),m_popup->y(),available.bottom()-m_popup->height()+1));
            if(m_popup->layout())m_popup->layout()->activate();

            const qreal dpr=m_popup->devicePixelRatioF();
            const QImage wall=m_canvas?m_canvas->wallpaperBackdrop(QRect(m_popup->mapToGlobal(QPoint()),m_popup->size()),dpr):QImage();
            m_material=LiquidPopup::renderMenuMaterial(wall,m_popup->size(),dpr,false);
        }else if(e->type()==QEvent::Paint){
            QPainter p(m_popup);p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(m_popup->rect(),Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);p.drawImage(QPointF(),m_material);return true;
        }else if(e->type()==QEvent::Hide)m_material={};
        return false;
    }
private:
    QPointer<DesktopCanvas> m_canvas;QComboBox *m_combo;QWidget *m_popup;QImage m_material;
};
}
void install(QWidget *form,DesktopCanvas *canvas){
    for(auto *combo:form->findChildren<QComboBox *>())if(!combo->property("settingsComboPopup").toBool()) {
        auto *style=new ListStyle;style->setParent(combo);combo->setStyle(style);
        // The menu delegate reads menu metrics, which clips the final row
        // once menu-style scrollers are disabled. Use the ordinary list delegate
        // so popup measurement and the styled row height agree.
        auto *view=new QListView(combo);combo->setView(view);
        view->setItemDelegate(new QStyledItemDelegate(view));
        new Skin(combo,canvas);
    }
}
}
