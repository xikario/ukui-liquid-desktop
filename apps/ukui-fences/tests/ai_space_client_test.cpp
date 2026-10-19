#include "AiSpaceClient.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

static bool waitFor(const std::function<bool()> &predicate, int timeout=15000)
{
    QElapsedTimer elapsed; elapsed.start();
    do {
        QCoreApplication::processEvents();
        if(predicate()) return true;
        QThread::msleep(10);
    } while(elapsed.elapsed()<timeout);
    return false;
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    if(argc!=3) return 2;
    QTemporaryDir home;
    qputenv("HOME",home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME",(home.path()+"/.config").toUtf8());
    qputenv("XDG_CACHE_HOME",(home.path()+"/.cache").toUtf8());
    qputenv("XDG_DATA_HOME",(home.path()+"/.local/share").toUtf8());
    qputenv("AI_SPACE_BINARY",argv[1]);
    qputenv("AI_SPACE_TEST_NO_BOOTSTRAP","1");
    qputenv("AI_SPACE_EXPLICIT_SHOW","1");
    qputenv("UKUI_FENCES_GLASS_NO_GL","1");
    qputenv("UKUI_LIQUID_GLASS_NO_GL","1");
    auto bus=QDBusConnection::sessionBus();
    const auto running=[&]{return bus.interface()->isServiceRegistered("org.ukui.AiSpace").value();};
    const auto quit=[&]{bus.call(QDBusMessage::createMethodCall("org.ukui.AiSpace","/aiSpace","org.ukui.AiSpace","quit"));};
    const auto check=[&](bool value,const char *label){
        std::fprintf(value?stdout:stderr,"%s: %s\n",value?"PASS":"FAIL",label);
        if(!value && running()) quit();
        return value;
    };
    auto *client=new AiSpaceClient;
    waitFor([]{return false;},200);
    if(!check(!running(),"constructing the bridge does not activate AI Space"))return 1;
    int failures=0;
    QObject::connect(client,&AiSpaceClient::launchFailed,&app,[&]{++failures;});
    client->call("showWindow");
    if(!check(waitFor([&]{auto s=client->state();return s.value("ready").toBool() && s.value("visible").toBool();}),"bridge launches the real standalone application"))return 1;
    const auto pid=client->state().value("pid");
    client->call("hideWindow");
    if(!check(waitFor([&]{return !client->state().value("visible").toBool();}),"hide is forwarded"))return 1;
    client->call("moveWindow",{120,130});
    client->call("showWindow");
    if(!check(waitFor([&]{return client->state().value("visible").toBool() && client->state().value("x").toInt()==120;}),"show and geometry are forwarded"))return 1;
    AiSpaceClient observer;
    if(!check(waitFor([&]{return observer.state().value("pid")==pid;}),"a new client discovers the running instance"))return 1;
    delete client;
    if(!check(running(),"destroying Fences bridge leaves AI Space alive"))return 1;
    QProcess cli;cli.start(QString::fromLocal8Bit(argv[2]),{"--smart-space"});
    if(!check(cli.waitForFinished(5000) && cli.exitCode()==0 && !bus.interface()->isServiceRegistered("org.ukui.fences").value(),"legacy CLI opens AI Space without starting a Fences desktop"))return 1;
    if(!check(failures==0,"forwarded calls report no connection errors"))return 1;
    quit();
    if(!check(waitFor([&]{return !running() && observer.state().isEmpty();}),"service exit clears cached state"))return 1;
    qputenv("AI_SPACE_BINARY","/does-not-exist/ai-space");
    QObject::connect(&observer,&AiSpaceClient::launchFailed,&app,[&]{++failures;});
    observer.call("showWindow");
    return check(waitFor([&]{return failures==1;}),"missing executable produces an actionable failure")?0:1;
}
