#include "LiquidPopup.h"
#include <QComboBox>
#include <QFontComboBox>
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
#include <QAbstractSpinBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>

namespace LiquidPopup {
namespace {
// The system style may paint opaque arrow buttons even when the field itself
// has a translucent stylesheet. Keep its hit testing and input handling; only
// replace those glyphs with a transparent, mouse-pass-through vector layer.
class ControlGlyphs final : public QWidget {
public:
    explicit ControlGlyphs(QWidget *control):QWidget(control),m_control(control) {
        setObjectName("liquidControlGlyphs");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet("background:transparent;border:0;");
        setFocusPolicy(Qt::NoFocus);
        control->installEventFilter(this);
        syncSpinMode();
        setGeometry(control->rect());show();raise();
        if(auto *spin=qobject_cast<QSpinBox *>(control))
            connect(spin,QOverload<int>::of(&QSpinBox::valueChanged),this,[this]{update();});
        else if(auto *spin=qobject_cast<QDoubleSpinBox *>(control))
            connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this]{update();});
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        switch(event->type()) {
        case QEvent::Paint:syncSpinMode();break;
        case QEvent::Resize:setGeometry(m_control->rect());break;
        case QEvent::Show:raise();break;
        case QEvent::EnabledChange:case QEvent::PaletteChange:
        case QEvent::StyleChange:case QEvent::FontChange:update();break;
        default:break;
        }
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        if(auto *combo=qobject_cast<QComboBox *>(m_control)) {
            QStyleOptionComboBox option;option.initFrom(combo);
            option.editable=combo->isEditable();option.frame=combo->hasFrame();
            const QRect button=combo->style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxArrow,combo);
            chevron(painter,button,false,combo->isEnabled());
        }else if(auto *spin=qobject_cast<QAbstractSpinBox *>(m_control)) {
            if(spin->buttonSymbols()==QAbstractSpinBox::NoButtons)return;
            QStyleOptionSpinBox option;option.initFrom(spin);
            option.buttonSymbols=spin->buttonSymbols();option.frame=spin->hasFrame();
            option.stepEnabled=QAbstractSpinBox::StepUpEnabled|QAbstractSpinBox::StepDownEnabled;
            bool up=spin->isEnabled()&&!spin->isReadOnly(),down=up;
            if(auto *integer=qobject_cast<QSpinBox *>(spin)) {
                up=up&&(integer->wrapping()||integer->value()<integer->maximum());
                down=down&&(integer->wrapping()||integer->value()>integer->minimum());
            }else if(auto *decimal=qobject_cast<QDoubleSpinBox *>(spin)) {
                up=up&&(decimal->wrapping()||decimal->value()<decimal->maximum());
                down=down&&(decimal->wrapping()||decimal->value()>decimal->minimum());
            }
            const bool plusMinus=spin->buttonSymbols()==QAbstractSpinBox::PlusMinus;
            chevron(painter,spin->style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxUp,spin),true,up,plusMinus);
            chevron(painter,spin->style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxDown,spin),false,down,plusMinus);
        }
    }
private:
    void syncSpinMode() {
        auto *spin=qobject_cast<QAbstractSpinBox *>(m_control);
        if(!spin || m_spinMode==int(spin->buttonSymbols()))return;
        m_spinMode=int(spin->buttonSymbols());
        spin->setProperty("liquidControlNoButtons",spin->buttonSymbols()==QAbstractSpinBox::NoButtons);
        // buttonSymbols has no changed signal. Its normal repaint reaches this
        // filter; refresh the CSS only when the mode changes, never on idle.
        spin->style()->unpolish(spin);spin->style()->polish(spin);spin->updateGeometry();
        update();
    }
    void chevron(QPainter &painter,const QRect &button,bool up,bool enabled,bool plusMinus=false) {
        if(button.isEmpty())return;
        QColor color=m_control->palette().color(QPalette::ButtonText);
        if(!enabled)color.setAlphaF(.36);
        painter.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const QPointF center=QRectF(button).center();
        if(plusMinus) {
            painter.drawLine(center+QPointF(-3.5,0),center+QPointF(3.5,0));
            if(up)painter.drawLine(center+QPointF(0,-3.5),center+QPointF(0,3.5));
            return;
        }
        const qreal halfWidth=4.,halfHeight=2.;
        const qreal direction=up?-1.:1.;
        QPainterPath path;path.moveTo(center+QPointF(-halfWidth,-direction*halfHeight));
        path.lineTo(center+QPointF(0,direction*halfHeight));
        path.lineTo(center+QPointF(halfWidth,-direction*halfHeight));
        painter.drawPath(path);
    }
    QWidget *m_control;
    int m_spinMode=-1;
};

void skinControl(QWidget *control) {
    if(control->property("liquidControlButtons").toBool())return;
    control->setProperty("liquidControlButtons",true);
    const QString comboRules=QStringLiteral(
        "\nQComboBox {padding-right:32px;min-height:24px;}"
        "QComboBox::drop-down {subcontrol-origin:border;subcontrol-position:top right;"
        "width:32px;border:0;background:transparent;border-top-right-radius:6px;border-bottom-right-radius:6px;}"
        "QComboBox::drop-down:hover {background:rgba(119,215,209,35);}"
        "QComboBox::drop-down:pressed {background:rgba(119,215,209,65);}"
        "QComboBox::down-arrow {image:none;width:0;height:0;}");
    const QString spinRules=QStringLiteral(
        "\nQAbstractSpinBox {padding-right:30px;min-height:24px;}"
        "QAbstractSpinBox[liquidControlNoButtons=true] {padding-right:4px;}"
        "QAbstractSpinBox::up-button {subcontrol-origin:border;subcontrol-position:top right;"
        "width:30px;border:0;background:transparent;border-top-right-radius:6px;}"
        "QAbstractSpinBox::down-button {subcontrol-origin:border;subcontrol-position:bottom right;"
        "width:30px;border:0;background:transparent;border-bottom-right-radius:6px;}"
        "QAbstractSpinBox::up-button:hover,QAbstractSpinBox::down-button:hover {background:rgba(119,215,209,35);}"
        "QAbstractSpinBox::up-button:pressed,QAbstractSpinBox::down-button:pressed {background:rgba(119,215,209,65);}"
        "QAbstractSpinBox::up-arrow,QAbstractSpinBox::down-arrow {image:none;width:0;height:0;}");
    control->setStyleSheet(control->styleSheet()+(qobject_cast<QComboBox *>(control)?comboRules:spinRules));
    new ControlGlyphs(control);
}

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
    Skin(QComboBox *combo,BackdropProvider provider):QObject(combo),m_provider(std::move(provider)),m_combo(combo),m_popup(combo->view()->window()) {
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
            // Screen removal can also leave primaryScreen() temporarily null.
            const QRect available=screen?screen->availableGeometry():m_combo->window()->geometry();
            m_popup->resize(m_popup->width(),qMin(available.height(),rowsHeight+insets+2));
            m_popup->move(m_popup->x(),qBound(available.top(),m_popup->y(),available.bottom()-m_popup->height()+1));
            if(m_popup->layout())m_popup->layout()->activate();

            const qreal dpr=m_popup->devicePixelRatioF();
            const QRect area(m_popup->mapToGlobal(QPoint()),m_popup->size());
            const QImage wall=m_provider?m_provider(area,dpr):captureBackdrop(area,dpr);
            m_material=renderMenuMaterial(wall,m_popup->size(),dpr,false);
            m_popup->setProperty("liquidComboMaterialBuilds",m_popup->property("liquidComboMaterialBuilds").toInt()+1);
            m_popup->setProperty("liquidComboMaterialBytes",m_material.sizeInBytes());
        }else if(e->type()==QEvent::Paint){
            QPainter p(m_popup);p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(m_popup->rect(),Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);p.drawImage(QPointF(),m_material);return true;
        }else if(e->type()==QEvent::Hide){m_material={};m_popup->setProperty("liquidComboMaterialBytes",0);}
        return false;
    }
private:
    BackdropProvider m_provider;QComboBox *m_combo;QWidget *m_popup;QImage m_material;
};
}
void installControls(QWidget *form){
    if(!form || !isEnabled() || qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0")return;
    if(qobject_cast<QComboBox *>(form)||qobject_cast<QAbstractSpinBox *>(form))skinControl(form);
    for(auto *control:form->findChildren<QComboBox *>())skinControl(control);
    for(auto *control:form->findChildren<QAbstractSpinBox *>())skinControl(control);
}
void installComboPopups(QWidget *form,BackdropProvider provider){
    if(!form || !isEnabled() || qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0")return;
    installControls(form);
    for(auto *combo:form->findChildren<QComboBox *>())if(!combo->property("settingsComboPopup").toBool()) {
        auto *style=new ListStyle;style->setParent(combo);combo->setStyle(style);
        // The menu delegate reads menu metrics, which clips the final row
        // once menu-style scrollers are disabled. Use the ordinary list delegate
        // so popup measurement and the styled row height agree.
        auto *view=new QListView(combo);
        // QFontComboBox owns a font-preview delegate: preserve its behavior.
        auto *fontDelegate=qobject_cast<QFontComboBox *>(combo)?combo->view()->itemDelegate():nullptr;
        if(fontDelegate)fontDelegate->setParent(view);
        combo->setView(view);
        view->setItemDelegate(fontDelegate?fontDelegate:new QStyledItemDelegate(view));
        new Skin(combo,provider);
    }
}
}
