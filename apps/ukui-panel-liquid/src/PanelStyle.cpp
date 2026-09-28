#include "PanelController.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QProxyStyle>
#include <QStylePlugin>
#include <QStyleFactory>
#include <QTimer>
#include <QDebug>
#include <QMenu>
#include <QStyleOptionMenuItem>
#include <QPainter>
class PanelStyle final : public QProxyStyle {
public:
    void drawPrimitive(PrimitiveElement element,const QStyleOption *option,QPainter *painter,const QWidget *widget=nullptr) const override {
        if(LiquidPopup::isEnabled() && qobject_cast<const QMenu *>(widget) &&
           widget->property("liquidPopupSkin").toBool() && LiquidPopup::drawMenuGlyph(element,option,painter))return;
        QProxyStyle::drawPrimitive(element,option,painter,widget);
    }
    void drawControl(ControlElement element,const QStyleOption *option,QPainter *painter,const QWidget *widget=nullptr) const override {
        painter->save();
        if(LiquidPopup::isEnabled() && qobject_cast<const QMenu *>(widget))
            painter->setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
        QProxyStyle::drawControl(element,option,painter,widget);
        painter->restore();
    }
    PanelStyle():QProxyStyle(QStyleFactory::create("ukui-default")) {
        setObjectName("ukuiliquidpanel");
        // Qt requests the style during QApplication construction. Install only
        // once its event loop is ready and all UKUI startup attributes exist.
        QTimer::singleShot(0,qApp,[]{
            if (!qApp->property("liquidPanelController").toBool()) {
                qApp->setProperty("liquidPanelController",true);
                new PanelController(qApp);
            }
        });
    }
};
class PanelStylePlugin final : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "style.json")
public:
    QStyle *create(const QString &key) override {
        if(key.compare("ukuiliquidpanel",Qt::CaseInsensitive)!=0) return nullptr;
        return new PanelStyle;
    }
};
#include "PanelStyle.moc"
