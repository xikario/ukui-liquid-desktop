#include "LiquidWheelPicker.h"
#include "LiquidPopup.h"
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QMenu>
#include <QWidgetAction>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QToolButton>
#include <QTimer>
#include <QPointer>
#include <QAbstractItemModel>
#include <QFontMetrics>
#include <limits>

namespace LiquidWheelPicker {
namespace {
class Wheel final : public QWidget {
public:
    Wheel(QMenu *menu,int current,int minimum,int maximum,std::function<QString(int)> label,
          std::function<bool(int)> enabled,int step)
        :QWidget(menu),m_menu(menu),m_value(current),m_min(minimum),m_max(maximum),
         m_step(qMax(1,step)),m_label(std::move(label)),m_enabled(std::move(enabled)) {
        setFont(menu->font());setFixedWidth(260);updateMetrics();setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);
        setToolTip("滚轮或拖动选择，点击或 Enter 确认，Esc 取消；输入文字可快速定位");
        setProperty("visibleRows",5);sync();
    }
    bool accepted=false;
    int value() const{return m_value;}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);p.setBrush(QColor(154,232,219,42));
        p.drawRoundedRect(QRectF(8,5+2*m_rowHeight,width()-16,m_rowHeight),9,9);
        for(int offset=-2;offset<=2;++offset){
            const qint64 value=qint64(m_value)+qint64(offset)*m_step;if(value<m_min || value>m_max)continue;
            QFont f=rowFont(offset);p.setFont(f);
            QColor ink=m_menu->palette().color(QPalette::WindowText);
            ink.setAlphaF(!allowed(int(value))?.2:offset==0?1.:qAbs(offset)==1?.66:.34);p.setPen(ink);
            p.drawText(QRectF(12,5+(offset+2)*m_rowHeight,width()-24,m_rowHeight),Qt::AlignCenter,
                       p.fontMetrics().elidedText(m_label(int(value)),Qt::ElideRight,width()-24));
        }
    }
    void wheelEvent(QWheelEvent *e) override {
        if(!e->pixelDelta().isNull()){m_pixels+=e->pixelDelta().y();const int n=m_pixels/m_rowHeight;m_pixels-=n*m_rowHeight;step(-n);}
        else{m_angle+=e->angleDelta().y();const int n=m_angle/120;m_angle-=n*120;step(-n);}e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if(e->button()!=Qt::LeftButton)return;
        m_startY=e->y();m_startValue=m_value;m_drag=false;e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if(!(e->buttons()&Qt::LeftButton))return;
        const int dy=e->y()-m_startY;if(qAbs(dy)>6)m_drag=true;
        if(m_drag)setValue(qint64(m_startValue)-qint64(qRound(qreal(dy)/m_rowHeight))*m_step);e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if(e->button()!=Qt::LeftButton)return;
        if(!m_drag){step(qBound(-2,(e->y()-5)/m_rowHeight-2,2));commit();}e->accept();
    }
    bool event(QEvent *e) override {
        if(e->type()==QEvent::FontChange){updateMetrics();update();}
        // Keep Escape local instead of invoking the settings-window shortcut.
        if(e->type()==QEvent::ShortcutOverride){auto *k=static_cast<QKeyEvent *>(e);if(k->key()==Qt::Key_Escape){e->accept();return true;}}
        return QWidget::event(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        switch(e->key()){
        case Qt::Key_Up:step(-1);break;
        case Qt::Key_Down:step(1);break;
        case Qt::Key_PageUp:step(-5);break;
        case Qt::Key_PageDown:step(5);break;
        case Qt::Key_Home:setValue(m_min);break;
        case Qt::Key_End:setValue(m_max);break;
        case Qt::Key_Return:case Qt::Key_Enter:case Qt::Key_Space:commit();break;
        case Qt::Key_Escape:m_menu->close();break;
        default:
            if(e->text().isEmpty()){QWidget::keyPressEvent(e);return;}
            if(!m_searchClock.isValid() || m_searchClock.elapsed()>1200)m_search.clear();
            m_searchClock.restart();m_search+=e->text();
            // Font/model lists are bounded, numeric ranges need no allocated list.
            for(qint64 i=m_min;i<=m_max && i<qint64(m_min)+10000;++i)
                if(m_label(int(i)).startsWith(m_search,Qt::CaseInsensitive) && allowed(int(i))){setValue(i);break;}
        }
        e->accept();
    }
private:
    QFont rowFont(int offset)const {
        QFont f=font();const qreal scale=offset==0?1.:qAbs(offset)==1?.88:.76;
        if(f.pixelSize()>0)f.setPixelSize(qRound(qMax(17,f.pixelSize())*scale));
        else f.setPointSizeF(qMax(12.75,f.pointSizeF())*scale);
        f.setBold(offset==0);return f;
    }
    void updateMetrics(){
        m_rowHeight=qMax(34,QFontMetrics(rowFont(0)).height()+12);
        setFixedHeight(5*m_rowHeight+10);setProperty("rowHeight",m_rowHeight);
    }
    bool allowed(int n)const{return !m_enabled || m_enabled(n);}
    void commit(){if(allowed(m_value)){accepted=true;m_menu->close();}}
    void setValue(qint64 n){m_value=int(qBound(qint64(m_min),n,qint64(m_max)));sync();}
    void step(int n){setValue(qint64(m_value)+qint64(n)*m_step);}
    void sync(){setProperty("selectedValue",m_value);setAccessibleDescription(m_label(m_value));update();}
    QMenu *m_menu;int m_value,m_min,m_max,m_step,m_pixels=0,m_angle=0,m_startY=0,m_startValue=0;
    int m_rowHeight=34;
    bool m_drag=false;std::function<QString(int)> m_label;std::function<bool(int)> m_enabled;
    QString m_search;QElapsedTimer m_searchClock;
};

}
int pick(QWidget *anchor,const QString &name,int current,int minimum,int maximum,
         const std::function<QString(int)> &label,const std::function<bool(int)> &enabled,int step) {
    if(maximum<minimum)return current;
    // No QObject parent: destruction of an originating page must not delete a
    // stack menu while exec is running. QPointer and destroyed handle that case.
    QMenu menu;menu.setObjectName(name+"Menu");menu.setPalette(anchor->palette());menu.setFont(anchor->font());
    menu.setStyleSheet("QMenu{padding:0px;margin:0px;}");
    QObject::connect(anchor,&QObject::destroyed,&menu,&QMenu::close);
    auto *wheel=new Wheel(&menu,qBound(minimum,current,maximum),minimum,maximum,label,enabled,step);
    wheel->setObjectName(name+"Wheel");wheel->setAccessibleName(anchor->accessibleName().isEmpty()?name:anchor->accessibleName());
    wheel->setFixedWidth(qBound(146,anchor->width(),360));
    auto *action=new QWidgetAction(&menu);action->setDefaultWidget(wheel);menu.addAction(action);
    QObject::connect(&menu,&QMenu::aboutToShow,wheel,[wheel]{QTimer::singleShot(0,wheel,[wheel]{wheel->setFocus(Qt::PopupFocusReason);});});
    LiquidPopup::execAt(menu,anchor);
    return wheel->accepted?wheel->value():current;
}
}
