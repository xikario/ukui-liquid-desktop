// Test-only driver for the real installed OEM taskbar. No private fields.
#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStylePlugin>
#include <QTimer>
#include <QVariant>
#include <dlfcn.h>
static void *symbol(QWidget *w,const char *name) {
    Dl_info info{};if(!dladdr(w->metaObject(),&info))return nullptr;
    void *module=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);if(!module)return nullptr;
    void *result=dlsym(module,name);dlclose(module);return result;
}
static QString desktop(QWidget *w){auto get=reinterpret_cast<QString (*)(QWidget *)>(symbol(w,"_ZN14UKUITaskButton15desktopFileNameEv"));return get?get(w):QString();}
static qulonglong window(QWidget *w){auto get=reinterpret_cast<QVariant (*)(QWidget *)>(symbol(w,"_ZNK14UKUITaskButton8windowIdEv"));return get?get(w).toULongLong():0;}
static QJsonObject snapshot() {
    QJsonArray buttons,menus;
    for(auto *w:QApplication::allWidgets()) {
        if(w->inherits("UKUITaskButton")) {
            const QPoint pos=w->mapToGlobal(QPoint());
            QWidget *group=w->parentWidget();while(group && !group->inherits("UKUITaskGroup"))group=group->parentWidget();
            buttons.append(QJsonObject{{"class",w->metaObject()->className()},{"group",QString::number(quintptr(group))},{"desktop",desktop(w)},
                {"window",QString::number(window(w))},{"routed",w->property("liquidPinnedLaunchRouted").toBool()},{"visible",w->isVisible()},{"x",pos.x()},{"y",pos.y()}});
        }
        if(auto *menu=qobject_cast<QMenu *>(w))if(menu->isVisible()) {
            QJsonArray actions;for(auto *a:menu->actions())actions.append(QJsonObject{{"name",a->objectName()},{"text",a->text()},{"enabled",a->isEnabled()}});
            menus.append(QJsonObject{{"actions",actions},{"x",menu->x()},{"y",menu->y()},{"w",menu->width()},{"h",menu->height()}});
        }
    }
    return {{"buttons",buttons},{"menus",menus}};
}
class PinProbe : public QObject {
    QLocalServer server;
    void command(const QJsonObject &request,QLocalSocket *socket) {
        const QString action=request.value("action").toString();bool ok=true;
        if(action=="menu" || action=="launch") {
            QWidget *target=nullptr;
            for(auto *w:QApplication::allWidgets())if(w->inherits("UKUITaskButton")) {
                if(action=="menu" && window(w)==request.value("window").toString().toULongLong())target=w;
                if(action=="launch" && window(w)==0 && desktop(w)==request.value("desktop").toString())target=w;
            }
            ok=target;
            if(target && action=="menu") {
                auto show=reinterpret_cast<void (*)(QWidget *,const QPoint &)>(symbol(target,"_ZN14UKUITaskButton15showContextMenuERK6QPoint"));
                ok=show;if(show)show(target,target->rect().center());
            }else if(target) {auto *button=qobject_cast<QAbstractButton *>(target);ok=button;if(button)button->click();}
        } else if(action=="toggle") {
            QAction *pin=nullptr;
            for(auto *w:QApplication::topLevelWidgets())if(auto *menu=qobject_cast<QMenu *>(w))if(menu->isVisible()) {
                if(auto *found=menu->findChild<QAction *>("liquidTaskbarPinAction"))pin=found;
            }
            ok=pin;if(pin)pin->trigger();
            for(auto *w:QApplication::topLevelWidgets())if(qobject_cast<QMenu *>(w))w->hide();
        } else if(action=="closeMenus") {
            for(auto *w:QApplication::topLevelWidgets())if(qobject_cast<QMenu *>(w))w->hide();
        }
        QPointer<QLocalSocket> guard(socket);
        QTimer::singleShot(request.value("wait").toInt(350),this,[=]{
            if(!guard)return;auto response=snapshot();response.insert("ok",ok);
            guard->write(QJsonDocument(response).toJson(QJsonDocument::Compact)+'\n');guard->flush();guard->disconnectFromServer();
        });
    }
public:
    explicit PinProbe(QObject *parent):QObject(parent) {
        connect(&server,&QLocalServer::newConnection,this,[this]{
            auto *socket=server.nextPendingConnection();
            auto read=[this,socket]{if(socket->canReadLine())command(QJsonDocument::fromJson(socket->readLine()).object(),socket);};
            connect(socket,&QLocalSocket::readyRead,this,read);read();
            connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
        });
        server.listen(qEnvironmentVariable("UKUI_PANEL_PIN_PROBE_SOCKET"));
    }
};
class PinProbeStyle : public QProxyStyle {
public:PinProbeStyle():QProxyStyle(QStyleFactory::create("ukuiliquidpanel")){QTimer::singleShot(0,qApp,[]{new PinProbe(qApp);});}
};
class PinProbeStylePlugin : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "pin_probe.json")
public:QStyle *create(const QString &key) override{return key=="pinprobe"?new PinProbeStyle:nullptr;}
};
#include "oem_pin_probe.moc"
