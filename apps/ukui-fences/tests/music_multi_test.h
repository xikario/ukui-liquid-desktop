#pragma once
#include "MusicClientsSettings.h"
#include "FencesSettingsWindow.h"
#include <QComboBox>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QProcess>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include "MusicClientIntegration.h"
void runMusicWindowTest(const std::function<void(bool,const char *)> &verify);

static bool waitMusic(const std::function<bool()> &ready,int timeout=3500) {
    QElapsedTimer timer; timer.start();
    while(!ready() && timer.elapsed()<timeout)settle(15);
    return ready();
}
// Delayed replies exercise a real in-flight GetAll, rather than private state.
class DelayedMusicFixture : public QDBusVirtualObject {
public:
    bool requested=false;
    QString introspect(const QString &) const override{return {};}
    bool handleMessage(const QDBusMessage &message,const QDBusConnection &connection) override {
        if(message.interface()!="org.freedesktop.DBus.Properties" || message.member()!="GetAll")return false;
        if(message.arguments().first().toString()=="org.mpris.MediaPlayer2.Player") {
            requested=true;
            QTimer::singleShot(350,this,[message,connection] {
                QVariantMap values{{"PlaybackStatus","Paused"},{"CanControl",true},
                    {"Metadata",QVariantMap{{"xesam:title","旧客户端的延迟结果"}}}};
                connection.send(message.createReply(QVariantList{QVariant::fromValue(values)}));
            });
        } else connection.send(message.createReply(QVariantList{QVariant::fromValue(QVariantMap{{"CanRaise",false}})}));
        return true;
    }
};

static int runMusicMultiTest(const QString &root) {
    const QString prefix="org.mpris.MediaPlayer2.", path="/org/mpris/MediaPlayer2";
    const QString aName=prefix+"fixture_a", bName=prefix+"fixture_b", ignored=prefix+"fixture_aevil";
    const auto original=MprisPlayer::loadProfiles();
    QList<MusicClientProfile> profiles{{"客户端 A",aName,{}, {},true},{"客户端 B",bName,{}, {},false}};
    check(MprisPlayer::saveProfiles(profiles),"configured-client list saves without starting any player");
    auto busA=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"music-fixture-a");
    auto busB=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"music-fixture-b");
    MusicFixture a,b; a.connectionName="music-fixture-a"; b.connectionName="music-fixture-b"; a.song="A 的歌曲"; b.song="B 的歌曲";
    MusicRootFixture rootA(&a),rootB(&b);
    const auto exports=QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllProperties|QDBusConnection::ExportAdaptors;
    check(busA.registerObject(path,&a,exports) && busB.registerObject(path,&b,exports),"two independent MPRIS objects export playback and root interfaces");
    {
        DesktopCanvas canvas; canvas.show(); canvas.setMusicWidgetVisible(true);
        auto *card=canvas.findChild<MusicDesklet *>(); if(!card)return 1;
        auto *player=card->player();
        busA.registerService(aName);
        check(waitMusic([&]{return player->connected() && player->activeService()==aName;}),"first enabled player automatically connects");
        busB.registerService(bName); busB.registerService(ignored);
        check(waitMusic([&]{return player->availableServices().contains(bName) && player->availableServices().contains(ignored);})
              && player->activeService()==aName,"disabled clients and similar-name unconfigured clients do not take over");
        profiles[1].enabled=true; MprisPlayer::saveProfiles(profiles); player->reloadConfiguration();
        check(waitMusic([&]{return player->connected() && player->activeService()==bName && player->canRaise();}),"enabling the last-started client switches the sole control target");
        check(player->title()==b.song && card->windowTitle().contains("客户端 B"),"song metadata and visible client identity follow the selected player");
        a.state="Playing"; a.notify({{"PlaybackStatus",a.state},{"Metadata",a.metadata()}}); settle(80);
        check(player->activeService()==bName && player->title()==b.song && !player->playing(),"old playback notifications never steal selection or overwrite the new player");
        card->findChild<QPushButton *>("musicNext")->click(); player->openPlayer();
        check(waitMusic([&]{return b.nextCount==1 && b.raiseCount==1;}) && a.nextCount==0 && a.raiseCount==0,"playback and Raise are routed exclusively to the active owner");
        const QString instance=bName+".instance77"; busB.registerService(instance);
        check(waitMusic([&]{return player->connected() && player->activeService()==instance;}),"configured service prefixes include additional MPRIS instances");
        auto *seek=card->findChild<QSlider *>("musicSeek"), *volume=card->findChild<QSlider *>("musicVolume");
        seek->setSliderDown(true); seek->setValue(650); volume->setSliderDown(true); volume->setValue(15);
        const int oldSeek=a.seekCount; const double oldVolume=a.vol;
        busB.unregisterService(instance); busB.unregisterService(bName);
        check(waitMusic([&]{return player->connected() && player->activeService()==aName;}),"selected player exit falls back to the remaining configured player");
        seek->setSliderDown(false); volume->setSliderDown(false); settle(80);
        check(a.seekCount==oldSeek && a.vol==oldVolume,"client changes during dragging cannot seek or alter the fallback player, even with the same track ID");
        busB.registerService(bName);
        check(waitMusic([&]{return player->connected() && player->activeService()==bName;}),"restarted MPRIS registration reconnects automatically");

        canvas.showSettingsPage("music"); settle(80);
        auto *window=canvas.findChild<FencesSettingsWindow *>();
        if(!window)for(auto *top:QApplication::topLevelWidgets())if((window=qobject_cast<FencesSettingsWindow *>(top)))break;
        auto *form=window?window->findChild<MusicClientsSettings *>():nullptr;
        check(form!=nullptr,"unified settings exposes the real music-client editor");
        if(form) {
            auto *clients=form->findChild<QListWidget *>("musicClients");
            check(clients->item(0)->checkState()==Qt::Checked && clients->item(1)->checkState()==Qt::Checked,
                  "opening the settings editor preserves enabled profile flags");
            auto *available=form->findChild<QComboBox *>("musicDetectedClients");
            check(waitMusic([&]{return available->findData(ignored)>=0;}),"settings discovers running clients through the session bus");
            check(waitMusic([&]{return !form->findChild<QLineEdit *>("musicClientProgram")->text().isEmpty();})
                  && form->property("settingsDirty").toBool(),"running clients with empty launch settings automatically receive an identifiable launch draft");
            auto *apply=form->findChild<QDialogButtonBox *>("musicClientsButtons")->button(QDialogButtonBox::Apply);apply->click();
            check(!form->property("settingsDirty").toBool(),"Apply saves the detected launch information");
            available->setCurrentIndex(available->findData(ignored));
            check(!form->property("settingsDirty").toBool(),"selecting a discovery result alone is not a configuration change");
            form->findChild<QPushButton *>("musicAddDetected")->click();
            check(!form->findChild<QLineEdit *>("musicClientProgram")->text().isEmpty(),"adding a running client identifies its executable without a file chooser");
            auto *identify=form->findChild<QPushButton *>("musicIdentifyProgram");identify->click();settle(20);
            bool chooser=false;for(auto *top:QApplication::topLevelWidgets())chooser|=QString(top->metaObject()->className()).contains("FileDialog");
            check(!chooser && !form->findChild<QLabel *>("musicClientsStatus")->text().isEmpty(),"process identification returns without a modal file dialog");
            check(clients->item(0)->checkState()==Qt::Checked && clients->item(1)->checkState()==Qt::Checked
                  && clients->item(2)->checkState()==Qt::Checked,"rebuilding the list to add a client preserves every enable flag");
            check(form->property("settingsDirty").toBool() && MprisPlayer::loadProfiles().size()==2,"adding a client stays in the draft until Apply");
            const auto edit=[form](const char *id,const QString &value){auto *field=form->findChild<QLineEdit *>(id);field->setText(value);QMetaObject::invokeMethod(field,"textEdited",Qt::DirectConnection,Q_ARG(QString,value));};
            edit("musicClientService","org.invalid.player");
            apply->click();
            check(MprisPlayer::loadProfiles().size()==2 && !form->findChild<QLabel *>("musicClientsStatus")->text().isEmpty(),"invalid services cannot replace the saved configuration");
            edit("musicClientService",ignored); edit("musicClientProgram",root+"/player with spaces");
            form->findChild<QListWidget *>("musicClients")->item(1)->setCheckState(Qt::Unchecked);
            form->findChild<QPlainTextEdit *>("musicClientArguments")->setPlainText("argument with spaces\n$(touch must-not-run)"); apply->click();
            const auto restored=MprisPlayer::loadProfiles();
            check(!form->property("settingsDirty").toBool() && restored.size()==3 && restored[0].enabled && !restored[1].enabled && restored[2].enabled
                  && restored.last().program.endsWith("player with spaces") && restored.last().arguments.last()=="$(touch must-not-run)"
                  && restored.last().desktopFile.isEmpty() && restored.last().workingDirectory.isEmpty(),"manual launch edits replace auto detection and preserve literal arguments without shell evaluation");
            check(waitMusic([&]{return player->connected() && player->activeService()==ignored;}),"Apply updates the live desklet connection immediately");
            QDir().mkpath("artifacts"); window->grab().save(QString("artifacts/music-clients-settings-%1.png").arg(window->devicePixelRatioF()));
            form->findChild<QPushButton *>("musicAddCustom")->click();
            check(form->findChild<QLineEdit *>("musicClientService")->isEnabled(),"custom configuration supports manual MPRIS input");
            form->findChild<QPushButton *>("musicRemoveClient")->click(); apply->click();
            window->close(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        busB.unregisterService(ignored); busB.unregisterService(bName); busA.unregisterService(aName);
        check(waitMusic([&]{return player->activeService().isEmpty() && !player->connected();}),"all clients exiting clears the last playback connection");
    }
    {
        const QString fixtureRoot=root+"/music-process",proc=fixtureRoot+"/proc/42",apps=QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        QDir().mkpath(proc);QDir().mkpath(apps);
        const auto write=[](const QString &path,const QByteArray &data){QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(data)==data.size();};
        write(proc+"/stat",QByteArray("42 (music process with spaces) S ")+QByteArray("0 ").repeated(18)+"98765 0\n");
        QFile::link(QCoreApplication::applicationFilePath(),proc+"/exe");QFile::link(fixtureRoot,proc+"/cwd");
        write(proc+"/cmdline",QCoreApplication::applicationFilePath().toUtf8()+'\0'+"--no-sandbox"+'\0'+"file:///private-song.mp3"+'\0');
        write(proc+"/environ","IGNORED_SECRET=must-not-be-saved\0");
        const QString service=prefix+"fixture_detected",desktop=apps+"/fixture_detected.desktop",script=fixtureRoot+"/launcher with spaces",marker=fixtureRoot+"/launched";
        write(script,("#!/bin/sh\nprintf '%s' \"${ELECTRON_RUN_AS_NODE-unset}\" > '"+marker+"'\n").toUtf8());
        QFile::setPermissions(script,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
        write(desktop,("[Desktop Entry]\nType=Application\nName=Fixture\nExec=\""+script+"\" %U\n").toUtf8());
        const auto info=MusicClientIntegration::inspectProcess(42,service,fixtureRoot+"/proc",{apps});
        check(info.started==98765 && info.launch.program==QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()
              && info.launch.arguments==QStringList{"--no-sandbox"} && info.launch.desktopFile==desktop
              && info.launch.workingDirectory==fixtureRoot,"process inspection identifies start time, desktop launcher, executable and working directory without storing media paths or environment");
        const QByteArray oldEnv=qgetenv("ELECTRON_RUN_AS_NODE");const bool existed=qEnvironmentVariableIsSet("ELECTRON_RUN_AS_NODE");qputenv("ELECTRON_RUN_AS_NODE","1");
        check(MusicClientIntegration::launch(info.launch) && waitMusic([&]{QFile f(marker);return f.open(QIODevice::ReadOnly) && f.readAll()=="unset";}),"detected desktop entry launches its wrapper with a spaced path and clears inherited Electron node mode");
        if(existed)qputenv("ELECTRON_RUN_AS_NODE",oldEnv);else qunsetenv("ELECTRON_RUN_AS_NODE");
        auto imageProfile=info.launch;write(proc+"/environ",("APPIMAGE="+script).toUtf8()+'\0');
        imageProfile=MusicClientIntegration::inspectProcess(42,service,fixtureRoot+"/proc",{apps}).launch;
        check(imageProfile.program==script && imageProfile.arguments.isEmpty(),"AppImage discovery retains the original executable rather than a temporary mount");
        write(apps+"/other.desktop",("[Desktop Entry]\nType=Application\nName=Other\nExec=\""+script+"\"\nTryExec="+QCoreApplication::applicationFilePath()+"\n").toUtf8());
        write(desktop,("[Desktop Entry]\nType=Application\nName=Fixture\nExec=\""+script+"\"\nTryExec="+QCoreApplication::applicationFilePath()+"\n").toUtf8());
        check(MusicClientIntegration::inspectProcess(42,service,fixtureRoot+"/proc",{apps}).launch.desktopFile.isEmpty(),"ambiguous desktop entries fall back to the exact process executable");
        check(!MusicClientIntegration::inspectProcess(99,service,fixtureRoot+"/proc",{apps}).started,"exited processes cannot yield a stale launch identity");
    }
    runMusicWindowTest(check);
    busA.unregisterObject(path); busB.unregisterObject(path);
    {
        const QString delayedName=prefix+"fixture_delayed", fastName=prefix+"fixture_fast";
        MprisPlayer::saveProfiles({{"慢客户端",delayedName,{}, {},true},{"快客户端",fastName,{}, {},true}});
        DelayedMusicFixture delayed; busA.registerVirtualObject(path,&delayed);
        busB.registerObject(path,&b,exports);
        MprisPlayer player; busA.registerService(delayedName);
        check(waitMusic([&]{return delayed.requested;}),"slow player's actual GetAll request is in flight");
        busB.registerService(fastName);
        check(waitMusic([&]{return player.connected() && player.activeService()==fastName;}),"later client connects while the old reply is still pending");
        settle(400);
        check(player.activeService()==fastName && player.title()==b.song,"old asynchronous GetAll reply cannot overwrite the new player");
        busA.unregisterService(delayedName); busB.unregisterService(fastName); busA.unregisterObject(path); busB.unregisterObject(path);
    }
    {
        const QString oldName=prefix+"cold_zeta", newName=prefix+"cold_alpha";
        MprisPlayer::saveProfiles({{"先启动",oldName,{}, {},true},{"后启动",newName,{}, {},true}});
        QProcess older,newer;
        const auto start=[](QProcess &process,const QString &name){
            process.start(QCoreApplication::applicationFilePath(),{"--music-server-fixture",name});
            QByteArray output;
            return waitMusic([&]{output+=process.readAllStandardOutput();return output.contains("READY");},5000);
        };
        check(start(older,oldName),"older independent fixture process starts"); settle(80);
        check(start(newer,newName),"newer independent fixture process starts");
        MprisPlayer lateWidget;
        check(waitMusic([&]{return lateWidget.connected() && lateWidget.activeService()==newName;}),"cold discovery chooses actual process start time, independent of bus-name order");
        newer.terminate(); newer.waitForFinished(1500);
        check(waitMusic([&]{return lateWidget.connected() && lateWidget.activeService()==oldName;}),"newest process exit falls back to the older independent client");
        older.terminate(); older.waitForFinished(1500);
    }
    MprisPlayer::saveProfiles(original);
    QDBusConnection::disconnectFromBus("music-fixture-a"); QDBusConnection::disconnectFromBus("music-fixture-b");
    return failures?1:0;
}
