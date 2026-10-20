#include <QApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QTemporaryDir>
#include <QProcess>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <QSet>
#include <QDebug>
#include <QTimer>
#include <QDBusMessage>
#include <QFile>
#include <QDir>
#include <csignal>
#include <functional>
class Compositor:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.kwin.Compositing")
    Q_PROPERTY(QString compositingType READ type WRITE setType)
public:
    QString value="xrender";bool refuse=false,refuseRestore=false;
    QString type()const{return value;}
    void setType(const QString &s){if((refuse && s=="OpenGL") || (refuseRestore && s=="XRender"))return;value=s=="OpenGL"?"gl2":"xrender";}
};
class Effects:public QObject,protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.kwin.Effects")
public:Compositor *compositor;QSet<QString> loaded{"kwin4_effect_squash"};bool refuse=false,failStateQuery=false,stallSupport=false;
public slots:
    bool isEffectSupported(const QString &){
        if(stallSupport){setDelayedReply(true);return false;}
        return compositor->value=="gl2";
    }
    bool isEffectLoaded(const QString &s){
        if(failStateQuery && s=="kwin4_effect_squash" && !loaded.contains(s)) {
            sendErrorReply("org.ukui.Test.StateUnavailable","state query failed");return false;
        }
        return loaded.contains(s);
    }
    bool loadEffect(const QString &s){if(loaded.contains(s) || (refuse && s=="magiclamp"))return false;loaded.insert(s);return true;}
    void unloadEffect(const QString &s){loaded.remove(s);}
};
void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}qInfo()<<"PASS"<<message;}
int main(int argc,char **argv){
    QTemporaryDir temp;qputenv("XDG_CONFIG_HOME",(temp.path()+"/config").toUtf8());qputenv("XDG_DATA_HOME",(temp.path()+"/data").toUtf8());
    QApplication app(argc,argv);Compositor compositor;Effects effects;effects.compositor=&compositor;
    auto bus=QDBusConnection::sessionBus();check(bus.registerService("org.ukui.KWin"),"isolated service");
    check(bus.registerObject("/Compositor",&compositor,QDBusConnection::ExportAllProperties),"backend API");
    check(bus.registerObject("/Effects",&effects,QDBusConnection::ExportAllSlots),"effects API");
    const QString config=temp.path()+"/config/ukui-kwinrc",journal=temp.path()+"/config/ukui/liquid-effects-trial.ini";
    {QSettings s(config,QSettings::IniFormat);s.setValue("Compositing/Backend","XRender");s.setValue("Compositing/AutoBackend","XRender");s.setValue("Plugins/blurEnabled",true);s.setValue("Plugins/kwin4_effect_squashEnabled",true);s.sync();}
    auto run=[&](QStringList args){
        QProcess p;p.setProcessChannelMode(QProcess::MergedChannels);p.start(app.arguments().at(1),args);
        QElapsedTimer t;t.start();while(p.state()!=QProcess::NotRunning && t.elapsed()<27000){app.processEvents();QThread::msleep(5);}
        check(p.state()==QProcess::NotRunning && p.exitStatus()==QProcess::NormalExit,"helper exits normally within bound");qInfo().noquote()<<p.readAll();return p.exitCode();
    };
    auto wait=[&](const std::function<bool()> &predicate,int timeout=6000){
        QElapsedTimer timer;timer.start();
        while(!predicate() && timer.elapsed()<timeout){app.processEvents();QThread::msleep(5);}
        return predicate();
    };
    if(app.arguments().contains("--deadline-only")) {
        QProcess helper;helper.setProcessChannelMode(QProcess::MergedChannels);
        helper.start(app.arguments().at(1),QStringList{});QByteArray output;
        check(wait([&]{output+=helper.readAll();return output.contains("READY");}),"real trial is ready before timer suspension");
        check(::kill(helper.processId(),SIGSTOP)==0,"pause only the isolated trial helper");
        QElapsedTimer suspended;suspended.start();
        check(wait([&]{return suspended.elapsed()>=20500;},22000),"wall time passes while event delivery is suspended");
        check(::kill(helper.processId(),SIGCONT)==0,"resume isolated helper");
        check(wait([&]{return helper.state()==QProcess::NotRunning;},3500),"overdue deadline restores immediately instead of counting missed ticks");
        check(helper.exitCode()==2 && compositor.value=="xrender","expired trial restores rather than commits");
        return 0;
    }
    if(app.arguments().contains("--interrupted-only")) {
        QProcess helper;helper.setProcessChannelMode(QProcess::MergedChannels);
        helper.start(app.arguments().at(1),QStringList{});QByteArray output;
        check(wait([&]{output+=helper.readAll();return output.contains("READY");}),"real helper writes a complete recovery journal");
        check(run({"--restore"})==3 && compositor.value=="gl2","operation lock prevents a second process interfering with a live trial");
        helper.kill();check(helper.waitForFinished(2000),"simulate interrupted helper process");
        {QSettings j(journal,QSettings::IniFormat);check(j.value("pending").toBool(),"interruption preserves actual pending journal");}
        compositor.refuseRestore=true;
        check(run({"--recover"})==1,"refused recovery is not reported as successful");
        {QSettings j(journal,QSettings::IniFormat);check(j.value("pending").toBool(),"failed recovery retains pending journal");}
        compositor.refuseRestore=false;
        check(run({"--recover"})==0 && compositor.value=="xrender","interrupted transaction recovers using the complete baseline");
        check(effects.loaded==QSet<QString>{"kwin4_effect_squash"},"interrupted trial restores actual effect loading state");
        {QSettings j(journal,QSettings::IniFormat);j.setValue("pending",true);j.sync();}
        const auto previous=compositor.value;const auto previousEffects=effects.loaded;
        check(run({"--recover"})==1 && compositor.value==previous && effects.loaded==previousEffects,
              "incomplete journal is rejected without guessing runtime state or deleting preferences");
        return 0;
    }
    if(app.arguments().contains("--fault-only")) {
        effects.failStateQuery=true;
        check(run({"--enable"})==1,"failed effect query cannot be mistaken for an unloaded effect");
        return 0;
    }
    check(run({"--verify-only"})==2,"verified trial automatically rolls back");
    check(compositor.value=="xrender" && effects.loaded==QSet<QString>{"kwin4_effect_squash"},"runtime backend and effects restored");
    {QSettings s(config,QSettings::IniFormat),j(journal,QSettings::IniFormat);check(s.value("Compositing/Backend")=="XRender" && !s.contains("Plugins/magiclampEnabled") && !j.value("pending").toBool(),"preferences and missing keys restored");}
    check(run({})==2 && compositor.value=="xrender","unconfirmed real countdown restores backend");
    check(run({"--enable"})==0,"successful confirmation commits");
    {QSettings s(config,QSettings::IniFormat),j(journal,QSettings::IniFormat);check(compositor.value=="gl2" && effects.loaded.contains("magiclamp") && !effects.loaded.contains("kwin4_effect_squash") && s.value("Plugins/magiclampEnabled").toBool() && j.value("saved").toBool(),"runtime and persistent magic agree");
        check(s.value("Plugins/blurEnabled").toBool() && s.value("Compositing/AutoBackend")=="XRender","unrelated effect and OEM automatic policy preserved");}
    check(run({"--recover"})==0 && compositor.value=="gl2","startup recovery does not undo a confirmed trial");
    check(run({"--restore"})==0 && compositor.value=="xrender","explicit restore returns to baseline");
    compositor.refuse=true;check(run({"--enable"})==1,"driver/backend refusal does not commit");compositor.refuse=false;
    effects.refuse=true;check(run({"--enable"})==1,"effect load rejection rolls back");effects.refuse=false;
    effects.failStateQuery=true;check(run({"--enable"})==1,"state query failure cannot confirm a competing effect is unloaded");
    effects.failStateQuery=false;check(run({"--recover"})==0,"query failure recovery can be retried");
    effects.stallSupport=true;check(run({"--enable"})==1 && compositor.value=="xrender","unresponsive capability query times out and rolls back");
    effects.stallSupport=false;
    check(run({"--recover"})==0,"recovery with no pending journal is harmless");
    check(QFile::rename(config,config+".baseline") && QDir().mkdir(config),"simulate a non-writable configuration target");
    check(run({"--enable"})==1 && compositor.value=="xrender","unwritable configuration prevents trial mutation");
    check(QDir().rmdir(config) && QFile::rename(config+".baseline",config),"restore isolated configuration fixture");
}
#include "backend_trial_test.moc"
