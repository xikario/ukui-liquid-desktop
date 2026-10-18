// Test-only wrapper; never installed. Drives the production settings controls
// inside the real OEM executable without accessing private C++ fields.
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QProxyStyle>
#include <QScreen>
#include <QSlider>
#include <QStyleFactory>
#include <QStylePlugin>
#include <QTimer>
#include <QWidget>
#include <QAction>
#include <QMetaMethod>
#include <QScrollBar>
#include <QAbstractScrollArea>
#include <QProcess>
#include <cstdio>
class LayoutProbe : public QObject {
    QList<QWidget *> testWindows;
    QLocalServer server; QPointer<QWidget> panel; QJsonArray events;
    QJsonObject item(QWidget *w) {
        const QRect g(w->mapToGlobal(QPoint()),w->size());
        QJsonArray children;
        for(auto *c:w->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))children.append(item(c));
        QJsonArray methods;
        if(w->inherits("UKUITaskBar"))for(int i=w->metaObject()->methodOffset();i<w->metaObject()->methodCount();++i)
            methods.append(QString::fromLatin1(w->metaObject()->method(i).methodSignature()));
        QJsonObject extra;
        if(auto *scroll=qobject_cast<QAbstractScrollArea *>(w))extra={{"value",scroll->horizontalScrollBar()->value()},{"maximum",scroll->horizontalScrollBar()->maximum()},{"pageStep",scroll->horizontalScrollBar()->pageStep()}};
        return {{"methods",methods},{"scroll",extra},{"class",w->metaObject()->className()},{"name",w->objectName()},
            {"x",g.x()},{"y",g.y()},{"w",g.width()},{"h",g.height()},
            {"visible",w->isVisible()},{"minimum",w->minimumWidth()},
            {"layoutMinimum",w->layout()?w->layout()->minimumSize().width():-1},
            {"children",children}};
    }
    QWidget *find(const QString &name) {
        for(auto *w:QApplication::allWidgets())if(w->objectName()==name)return w;
        return nullptr;
    }
    void command(const QJsonObject &request,QLocalSocket *socket) {
        if(!panel)for(auto *w:QApplication::topLevelWidgets())if(w->inherits("UKUIPanel"))panel=w;
        const QString action=request.value("action").toString();bool ok=bool(panel);
        events={};
        if(panel && action=="settings") {
            QMenu menu(panel);menu.ensurePolished();menu.show();
            qApp->processEvents();
            if(auto *a=menu.findChild<QAction *>("liquidPanelSettingsAction"))a->trigger();else ok=false;
            menu.hide();
        } else if(panel && action=="layout") {
            auto *center=qobject_cast<QCheckBox *>(find("liquidPanelCentered"));
            auto *length=qobject_cast<QSlider *>(find("liquidPanelCenteredLength"));
            ok=center && center->isEnabled() && length;
            if(ok) {
                center->setChecked(request.value("enabled").toBool());
                length->setValue(request.value("percent").toInt(68));
            }
        } else if(panel && action=="closeSettings") {
            for(auto *w:panel->findChildren<QDialog *>())w->close();
        } else if(panel && action=="hidable") {
            ok=QMetaObject::invokeMethod(panel,"setHidable",Qt::DirectConnection,
                Q_ARG(bool,request.value("value").toBool()),Q_ARG(bool,false));
        } else if(panel && action=="hide") {
            ok=QMetaObject::invokeMethod(panel,"hidePanel",Qt::DirectConnection);
        } else if(panel && action=="show") {
            ok=QMetaObject::invokeMethod(panel,"showPanel",Qt::DirectConnection,Q_ARG(bool,true));
        } else if(panel && action=="realign") {
            ok=QMetaObject::invokeMethod(panel,"realign",Qt::DirectConnection);
        } else if(panel && action=="height") {
            ok=QMetaObject::invokeMethod(panel,"setPanelSize",Qt::DirectConnection,
                Q_ARG(int,request.value("value").toInt(48)),Q_ARG(bool,false));
        } else if(panel && action=="theme") {
            QMenu menu(panel);menu.ensurePolished();menu.show();qApp->processEvents();
            if(auto *a=menu.findChild<QAction *>("liquidPanelEnabledAction")) {
                if(a->isChecked()!=request.value("value").toBool())a->trigger();
            }else ok=false;menu.hide();
        } else if(action=="applications") {
            const int count=request.value("count").toInt();
            while(testWindows.size()>count) delete testWindows.takeLast();
            while(testWindows.size()<count) {
                auto *w=new QWidget(nullptr, Qt::Window);
                const QString title="LayoutCompatApp"+QString::number(testWindows.size());
                w->setWindowTitle(title); w->resize(240,120); w->show();
                const QString id=QString::number(w->winId());
                QProcess::startDetached("xprop",{"-id",id,"-f","WM_CLASS","8s","-set","WM_CLASS",title});
                testWindows.append(w);
            }
        } else if(action=="click") {
            auto *w=find(request.value("name").toString());ok=w;
            if(w) {
                const QPoint local=w->rect().center(),global=w->mapToGlobal(local);
                QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                const int repeat=qMax(1,request.value("repeat").toInt(1));
                for(int i=0;i<repeat;++i) {
                    QApplication::sendEvent(w,&press);QApplication::sendEvent(w,&release);
                }
            }
        }
        QPointer<QLocalSocket> guard(socket);
        QTimer::singleShot(request.value("wait").toInt(650),this,[=]{
            if(!guard)return;QJsonObject result{{"ok",ok}};
            if(panel) {
                result.insert("panel",item(panel));result.insert("winId",QString::number(panel->winId()));
                result.insert("backend",panel->property("liquidPanelLayoutBackend").toString());
                result.insert("batches",double(panel->property("liquidPanelLayoutBatches").toULongLong()));
                result.insert("corrections",double(panel->property("liquidPanelLayoutCorrections").toULongLong()));
            }
            QJsonArray windows;for(auto *w:QApplication::topLevelWidgets())if(w->isVisible() && w!=panel)windows.append(item(w));
            result.insert("windows",windows);result.insert("events",events);
            guard->write(QJsonDocument(result).toJson(QJsonDocument::Compact)+char(10));guard->flush();guard->disconnectFromServer();
        });
    }
    bool eventFilter(QObject *o,QEvent *e) override {
        if(o==panel && (e->type()==QEvent::Move || e->type()==QEvent::Resize)) {
            if(events.size()<500)events.append(QJsonObject{{"type",int(e->type())},{"x",panel->x()},{"y",panel->y()},{"w",panel->width()},{"h",panel->height()}});
        }return false;
    }
public:
    explicit LayoutProbe(QObject *parent):QObject(parent) {
        qApp->installEventFilter(this);
        connect(&server,&QLocalServer::newConnection,this,[this]{
            auto *socket=server.nextPendingConnection();
            auto consume=[this,socket]{
                if(socket->canReadLine())command(QJsonDocument::fromJson(socket->readLine()).object(),socket);
            };
            connect(socket,&QLocalSocket::readyRead,this,consume);
            consume();connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
        });
        server.listen(qEnvironmentVariable("UKUI_PANEL_LAYOUT_PROBE_SOCKET"));
    }
};
class LayoutProbeStyle : public QProxyStyle {
public:LayoutProbeStyle():QProxyStyle(QStyleFactory::create("ukuiliquidpanel")) {
    QTimer::singleShot(0,qApp,[]{new LayoutProbe(qApp);});
}};
class LayoutProbeStylePlugin : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "layout_probe.json")
public:QStyle *create(const QString &key) override {return key=="layoutprobe"?new LayoutProbeStyle:nullptr;}
};
#include "oem_layout_probe.moc"
