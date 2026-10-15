#include "WindowEffectsSettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QDBusConnection>
#include <QDBusContext>
#include <QElapsedTimer>
#include <QThread>
#include <QSet>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <cstdio>
#include <functional>

class Effects : public QObject,protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.kwin.Effects")
public:
    QSet<QString> loaded{"kwin4_effect_squash","kwin4_effect_maximize"};
    bool failLoad=false,failRefresh=false,failUnload=false,unsupported=false,delaySupport=false;
    int mutations=0;
    QStringList refreshes;
public slots:
    bool isEffectSupported(const QString &name) {
        if(delaySupport){setDelayedReply(true);return false;}
        return !unsupported || name!="magiclamp";
    }
    bool isEffectLoaded(const QString &name) {return loaded.contains(name);}
    bool loadEffect(const QString &name) {++mutations;if(failLoad)return false;loaded.insert(name);return true;}
    void unloadEffect(const QString &name) {++mutations;if(failUnload && name=="kwin4_effect_squash")return;loaded.remove(name);}
    void reconfigureEffect(const QString &name) {
        ++mutations;refreshes.append(name);
        if(failRefresh)sendErrorReply("org.ukui.Test.ReconfigureFailed","fixture failure");
    }
};
static void check(bool condition,const char *message){if(!condition){qCritical()<<message;std::exit(1);}qInfo()<<"PASS:"<<message;}
static bool wait(const std::function<bool()> &predicate) {
    QElapsedTimer timer;timer.start();
    while(!predicate() && timer.elapsed()<6000){qApp->processEvents();QThread::msleep(2);}
    return predicate();
}
int main(int argc,char **argv) {
    QTemporaryDir temp;qputenv("XDG_CONFIG_HOME",temp.path().toUtf8());
    QApplication app(argc,argv);Effects service;
    auto bus=QDBusConnection::sessionBus();
    if(app.arguments().contains("--stall")) {
        service.delaySupport=true;
        if(!bus.registerService("org.ukui.KWin") || !bus.registerObject("/Effects",&service,QDBusConnection::ExportAllSlots))return 2;
        std::puts("READY");std::fflush(stdout);return app.exec();
    }
    check(bus.registerService("org.ukui.KWin"),"mock owns isolated compositor service");
    check(bus.registerObject("/Effects",&service,QDBusConnection::ExportAllSlots),"mock exports real effects protocol");
    const auto config=temp.path()+"/ukui-kwinrc";
    {QSettings s(config,QSettings::IniFormat);s.setValue("Plugins/blurEnabled",true);s.setValue("Compositing/AnimationSpeed",2);s.setValue("Plugins/magiclampEnabled",false);s.setValue("Plugins/kwin4_effect_squashEnabled",true);}
    auto read=[&](const QString &key){QSettings s(config,QSettings::IniFormat);return s.value(key);};
    auto *view=new WindowEffectsSettings;view->show();
    auto *magic=view->findChild<QCheckBox *>("liquidWindowMagicLamp");
    auto *maximize=view->findChild<QCheckBox *>("liquidWindowMaximize");
    auto *duration=view->findChild<QSpinBox *>("liquidWindowMagicDuration");
    auto *reset=view->findChild<QPushButton *>("liquidWindowMagicReset");
    check(wait([&]{return magic->isEnabled();}),"asynchronous initial state completes");
    check(!magic->isChecked() && maximize->isChecked() && duration->value()==0 && service.mutations==0,"opening reads runtime status and default duration without changing system");
    magic->click();check(wait([&]{return magic->isEnabled();}),"magic enable finishes");
    check(service.loaded.contains("magiclamp") && !service.loaded.contains("kwin4_effect_squash")
        && read("Plugins/magiclampEnabled").toBool() && read("Plugins/kwin4_effect_magiclampEnabled").toBool()
        && !read("Plugins/kwin4_effect_squashEnabled").toBool(),"magic loads, competing minimize effect unloads and preferences persist");
    duration->setValue(550);check(wait([&]{return duration->isEnabled();}),"duration refresh completes");
    check(read("Effect-MagicLamp/AnimationDuration").toInt()==550 && service.refreshes.last()=="magiclamp","duration saves to exact native group and reconfigures only magic lamp");
    reset->click();check(wait([&]{return reset->isEnabled();}),"duration reset completes");
    check(duration->value()==0 && read("Effect-MagicLamp/AnimationDuration").toInt()==0,"reset returns duration to native zero/default sentinel");
    maximize->click();check(wait([&]{return maximize->isEnabled();}),"maximize switch completes");
    check(!service.loaded.contains("kwin4_effect_maximize") && !read("Plugins/kwin4_effect_maximizeEnabled").toBool()
        && service.loaded.contains("magiclamp"),"maximize toggle is independent of minimize animation");
    check(read("Plugins/blurEnabled").toBool() && read("Compositing/AnimationSpeed").toInt()==2,"unrelated compositor effects and global speed preserved");
    magic->click();check(wait([&]{return magic->isEnabled();}),"magic disable completes");
    check(!read("Plugins/magiclampEnabled").toBool() && !read("Plugins/kwin4_effect_magiclampEnabled").toBool(),"both OEM magic preference names disable together");
    service.failLoad=true;magic->click();
    auto *status=view->findChild<QLabel *>("liquidWindowEffectsStatus");
    check(wait([&]{return status->text().contains("未能切换");}),"load rejection is reported without success feedback");
    check(!read("Plugins/magiclampEnabled").toBool() && !service.loaded.contains("magiclamp"),"failed load does not persist desired state");
    delete view;service.failLoad=false;
    service.loaded.insert("kwin4_effect_squash");service.failUnload=true;
    view=new WindowEffectsSettings;magic=view->findChild<QCheckBox *>("liquidWindowMagicLamp");status=view->findChild<QLabel *>("liquidWindowEffectsStatus");
    check(wait([&]{return magic->isEnabled();}),"second view reads current state");magic->click();
    check(wait([&]{return status->text().contains("未能确认");}),"failed conflict removal is detected");
    check(!service.loaded.contains("magiclamp") && service.loaded.contains("kwin4_effect_squash") && !read("Plugins/magiclampEnabled").toBool(),"partial activation rolls runtime back without saving");
    delete view;service.failUnload=false;
    view=new WindowEffectsSettings;magic=view->findChild<QCheckBox *>("liquidWindowMagicLamp");
    check(wait([&]{return magic->isEnabled();}),"third view reads current state");magic->click();delete view;
    check(wait([&]{return read("Plugins/magiclampEnabled").toBool();}),"closing view during change still completes persistence");
    view=new WindowEffectsSettings;duration=view->findChild<QSpinBox *>("liquidWindowMagicDuration");status=view->findChild<QLabel *>("liquidWindowEffectsStatus");
    check(wait([&]{return duration->isEnabled();}),"reopened view ready");
    service.failRefresh=true;duration->setValue(900);
    check(wait([&]{return status->text().contains("无法连接或更新");}),"reconfiguration error reported");
    check(read("Effect-MagicLamp/AnimationDuration").toInt()==0,"failed reconfiguration restores old duration");
    delete view;service.failRefresh=false;service.unsupported=true;
    view=new WindowEffectsSettings;magic=view->findChild<QCheckBox *>("liquidWindowMagicLamp");maximize=view->findChild<QCheckBox *>("liquidWindowMaximize");
    check(wait([&]{return maximize->isEnabled();}) && !magic->isEnabled(),"unsupported magic disables only its controls");delete view;
    bus.unregisterService("org.ukui.KWin");
    QProcess stalled;stalled.setProcessChannelMode(QProcess::MergedChannels);
    stalled.start(QCoreApplication::applicationFilePath(),{"--stall"});
    QByteArray ready;
    check(wait([&]{ready+=stalled.readAll();return ready.contains("READY");}),"separate compositor fixture accepts calls without replying");
    int ticks=0;QTimer responsive;responsive.setInterval(10);
    QObject::connect(&responsive,&QTimer::timeout,[&]{++ticks;});responsive.start();
    view=new WindowEffectsSettings;status=view->findChild<QLabel *>("liquidWindowEffectsStatus");
    check(wait([&]{return status->text().contains("无法连接或更新");}) && ticks>30,
        "unresponsive compositor times out while the UI event loop continues running");
    responsive.stop();delete view;stalled.terminate();stalled.waitForFinished(2000);
    bus.unregisterService("org.ukui.KWin");
    view=new WindowEffectsSettings;status=view->findChild<QLabel *>("liquidWindowEffectsStatus");magic=view->findChild<QCheckBox *>("liquidWindowMagicLamp");
    check(wait([&]{return status->text().contains("无法连接或更新");}) && !magic->isEnabled(),"absent compositor is explained and cannot edit settings");delete view;
    return 0;
}
#include "window_effects_test.moc"
