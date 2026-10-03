#include "LiquidButtons.h"
#include "LiquidPopup.h"
#include <QChildEvent>
#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QMenu>

namespace LiquidButtons {
namespace {
class ButtonPaint final : public QObject {
public:
    explicit ButtonPaint(QPushButton *button):QObject(button),button(button) {
        button->setProperty("liquidButtonInstalled",true);
        button->setAttribute(Qt::WA_Hover);
        button->setAutoFillBackground(false);
        button->setAttribute(Qt::WA_StyledBackground,false);
        button->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        switch(event->type()) {
        case QEvent::StyleChange:case QEvent::Polish:
            // A host can change its stylesheet while the same button lives.
            // Keep its metrics, but never add a second native background plate.
            button->setAttribute(Qt::WA_StyledBackground,false);
            break;
        case QEvent::Enter:case QEvent::Leave:case QEvent::FocusIn:case QEvent::FocusOut:
        case QEvent::EnabledChange:case QEvent::PaletteChange:
            button->update();break;
        case QEvent::Paint:
            if(!LiquidPopup::isEnabled())return false;
            paint();return true;
        default:break;
        }
        return false;
    }
private:
    void paint() {
        QPainter painter(button);
        painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
        QStyleOptionButton option;option.initFrom(button);
        option.text=button->text();option.icon=button->icon();option.iconSize=button->iconSize();
        if(button->isDefault())option.features|=QStyleOptionButton::DefaultButton;
        if(button->autoDefault())option.features|=QStyleOptionButton::AutoDefaultButton;
        if(button->isFlat())option.features|=QStyleOptionButton::Flat;
        if(button->menu())option.features|=QStyleOptionButton::HasMenu;
        if(button->isDown())option.state|=QStyle::State_Sunken;
        if(button->isChecked())option.state|=QStyle::State_On;
        const bool enabled=button->isEnabled();
        const bool hover=enabled&&button->underMouse();
        const bool active=enabled&&(button->isDown()||button->isChecked());
        const bool focus=enabled&&button->hasFocus();
        const bool flat=button->isFlat()||button->property("liquidButtonFlat").toBool();
        const QColor foreground=button->palette().color(QPalette::Active,QPalette::ButtonText);
        const bool light=foreground.lightness()<128;
        QColor rim=light?QColor(32,58,76,65):QColor(225,245,255,48);
        QColor fill=light?QColor(255,255,255,40):QColor(218,238,255,19);
        if(!enabled) {rim.setAlpha(18);fill.setAlpha(7);}
        else if(active) {fill=QColor(90,190,185,88);rim.setAlpha(105);}
        else if(hover) {fill=QColor(119,215,209,53);rim.setAlpha(90);}
        if(enabled&&button->isDefault())rim=light?QColor(31,125,126,100):QColor(99,207,198,120);
        if(focus)rim=light?QColor(31,125,126,200):QColor(99,207,198,210);
        // Leave a logical pixel for every AA sample. No rectangular clip/mask,
        // no native bevel and no opaque corners beneath this single silhouette.
        const QRectF bounds=QRectF(button->rect()).adjusted(1,1,-1,-1);
        const qreal radius=qMin(qreal(9),qMin(bounds.width(),bounds.height())/2);
        if(!flat||hover||active||focus) {
            QLinearGradient gradient(bounds.topLeft(),bounds.bottomLeft());
            QColor top=fill,bottom=fill;
            top.setAlpha(qMin(255,fill.alpha()+5));bottom.setAlpha(qMax(0,fill.alpha()-3));
            gradient.setColorAt(0,top);gradient.setColorAt(1,bottom);
            painter.setBrush(gradient);
            painter.setPen(flat&&!focus?QPen(Qt::NoPen):QPen(rim,1));
            painter.drawRoundedRect(bounds,radius,radius);
        }
        const QString glyph=button->property("liquidButtonGlyph").toString();
        if(glyph=="close"||glyph=="minimize") {
            QColor color=foreground;if(!enabled)color.setAlphaF(.38);
            painter.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            const QPointF center=bounds.center();
            if(glyph=="close") {
                painter.drawLine(center+QPointF(-4,-4),center+QPointF(4,4));
                painter.drawLine(center+QPointF(4,-4),center+QPointF(-4,4));
            }else painter.drawLine(center+QPointF(-5,2),center+QPointF(5,2));
        }else {
            option.rect=button->style()->subElementRect(QStyle::SE_PushButtonContents,&option,button);
            // The native label retains shortcuts, icon/text placement and menu
            // arrows. Only the bevel is replaced; default-button input stays Qt.
            button->style()->drawControl(QStyle::CE_PushButtonLabel,&option,&painter,button);
        }
    }
    QPushButton *button;
};

class Host final : public QObject {
public:
    explicit Host(QWidget *root):QObject(root),root(root) {
        root->setProperty("liquidButtonsHost",true);adopt(root);
    }
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()==QEvent::ChildPolished) {
            if(auto *widget=qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child()))adopt(widget);
        }else if(event->type()==QEvent::Show) {
            if(auto *widget=qobject_cast<QWidget *>(object))adopt(widget);
        }
        return false;
    }
private:
    void adopt(QWidget *widget) {
        if(widget!=root&&widget->isWindow())return;
        if(!widget->property("liquidButtonsWatched").toBool()) {
            widget->setProperty("liquidButtonsWatched",true);widget->installEventFilter(this);
        }
        if(auto *button=qobject_cast<QPushButton *>(widget))
            if(!button->property("liquidButtonInstalled").toBool()
                && !button->property("liquidButtonNative").toBool())new ButtonPaint(button);
        for(auto *child:widget->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))adopt(child);
    }
    QWidget *root;
};
}
void install(QWidget *root) {
    if(!root||root->property("liquidButtonsHost").toBool()
        ||qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0")return;
    new Host(root);
}
}
