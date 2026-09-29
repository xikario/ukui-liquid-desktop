#pragma once
#include "DesktopIcon.h"
#include "../../../shared/async-work/BackgroundTask.h"

static int runReviewAsyncTest(const QString &root)
{
    const QString folder=root+"/lazy-folder";QDir().mkpath(folder);
    for(int i=0;i<40;++i)QDir().mkpath(folder+QString("/empty-%1/deeper").arg(i));
    QFile f(folder+"/sample.txt");f.open(QIODevice::WriteOnly);f.write("example");f.close();
    DesktopIcon icon(DesktopItem::fromPath(folder));
    check(icon.toolTip().contains("悬停后按需统计"),"constructing directory icon does not recursively scan it");
    QEvent enter(QEvent::Enter);QApplication::sendEvent(&icon,&enter);
    int heartbeat=0;QTimer timer;QObject::connect(&timer,&QTimer::timeout,[&]{++heartbeat;});timer.start(10);
    QElapsedTimer elapsed;elapsed.start();while(!icon.toolTip().contains("项目预览") && elapsed.elapsed()<3000)settle(20);
    check(icon.toolTip().contains("项目预览") && heartbeat>10,"hover asynchronously fills directory detail while event loop runs");
    const QString another=root+"/other-folder";QDir().mkpath(another);
    icon.setItem(DesktopItem::fromPath(another));
    check(icon.toolTip().contains("悬停后按需统计") && !icon.toolTip().contains("sample.txt"),"changed item invalidates old folder detail");
    bool stale=false,completed=false;
    auto *recipient=new QObject;
    BackgroundTask::run(recipient,[]{QThread::msleep(150);return 42;},[&](int){stale=true;});
    delete recipient;
    BackgroundTask::run(qApp,[]{QThread::msleep(200);return 42;},[&](int result){completed=result==42;});
    elapsed.restart();while(!completed && elapsed.elapsed()<2000)settle(20);
    check(completed && !stale,"destroyed recipient never receives worker completion");
    QSettings().setValue("systemMonitor/autoStart",false);
    QSettings().setValue("smartSpace/autoStart",false);
    {
        DesktopCanvas canvas;canvas.show();bool finished=false;
        FileClipboard::runOperationAsync(&canvas,[]{QThread::msleep(250);return FileClipboard::PasteResult();},[&](const auto &){finished=true;});
        canvas.quitApp();
        check(canvas.isVisible(),"normal quit keeps desktop alive until file operation completes");
        elapsed.restart();while(!finished && elapsed.elapsed()<3000)settle(20);
        check(finished,"pending file operation completes without blocking the quit notice");
    }
    QSettings().setValue("systemMonitor/credentialStore",QString());
    QSettings().setValue("systemMonitor/apiKey",QString());
    qunsetenv("DEEPSEEK_API_KEY");
    auto *monitor=new SystemMonitor;monitor->show();settle(700);delete monitor;settle(300);
    check(heartbeat>50,"monitor sampling and destruction keep event loop responsive");
    return failures?1:0;
}
