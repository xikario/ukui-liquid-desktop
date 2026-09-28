#include "music_fixture.h"
#include "MusicDesklet.h"
#include "StrawberryPlayer.h"
#include "DesktopIcon.h"
#include <QSlider>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
static int runMusicTest(const QString &root){
    QSettings settings;settings.setValue("smartSpace/autoStart",false);settings.setValue("systemMonitor/autoStart",false);
    settings.setValue("desklets/clock/autoStart",false);settings.setValue("desklets/activity/autoStart",false);settings.sync();
    const QString name="org.mpris.MediaPlayer2.strawberry",object="/org/mpris/MediaPlayer2";
    auto bus=QDBusConnection::sessionBus();
    MusicFixture fixture;
    QImage cover(300,300,QImage::Format_RGB32);cover.fill(QColor("#de5372"));cover.save(root+"/cover.png");fixture.art=QUrl::fromLocalFile(root+"/cover.png").toString();
    {
        DesktopCanvas canvas;canvas.show();canvas.setMusicWidgetVisible(true);settle(150);
        auto *card=canvas.findChild<MusicDesklet *>();check(card!=nullptr,"music widget can open while Strawberry is absent");if(!card)return 1;
        auto *player=card->player();auto *play=card->findChild<QPushButton *>("musicPlayPause");
        auto *seek=card->findChild<QSlider *>("musicSeek");auto *volume=card->findChild<QSlider *>("musicVolume");
        check(!player->connected() && !play->isEnabled() && !player->progressActive(),"offline card disables playback without running a progress timer");
        check(!card->notesAnimating(),"offline music card has no animation timer");
        check(bus.registerObject(object,&fixture,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllProperties),"mock MPRIS object registered");
        check(bus.registerService(name),"mock Strawberry owner registered");settle(300);
        check(player->connected() && player->title()=="测试歌曲" && player->artist()=="测试歌手","late player startup populates track metadata through real D-Bus");
        check(!player->cover().isNull() && player->length()==180000000 && seek->isEnabled(),"cover duration and seek capabilities load");
        settle(120);const int builds=card->materialBuilds();
        play->click();settle(180);check(player->playing() && fixture.playCount==1 && player->progressActive(),"play control starts real MPRIS playback and visible progress timer");
        check(card->notesAnimating(),"MPRIS playing starts floating notes");
        const QImage firstNotes=card->grab(QRect(6,4,104,120)).toImage();settle(180);
        check(firstNotes!=card->grab(QRect(6,4,104,120)).toImage(),"floating notes visibly move over the cover");
        const QString preview=qEnvironmentVariable("UKUI_FENCES_NOTES_PREVIEW");
        if(!preview.isEmpty()) {
            QDir().mkpath(preview);
            for(int i=0;i<36;++i){card->grab().save(preview+QString("/frame-%1.png").arg(i,3,10,QChar('0')));settle(100);}
        }
        const auto before=player->position();settle(1100);
        check(player->position()>before+900000 && card->materialBuilds()==builds,"playing progress advances without rebuilding the glass material");
        play->click();settle(150);const auto paused=player->position();settle(120);
        check(!player->playing() && !player->progressActive() && player->position()==paused,"pause freezes local progress and stops its timer");
        const QImage pausedNotes=card->grab(QRect(6,4,104,120)).toImage();settle(100);
        check(!card->notesAnimating() && pausedNotes==card->grab(QRect(6,4,104,120)).toImage(),"pause stops note repainting and clears animation");
        card->findChild<QPushButton *>("musicNext")->click();card->findChild<QPushButton *>("musicPrevious")->click();settle(120);
        check(fixture.nextCount==1 && fixture.previousCount==1,"previous and next reach Strawberry");
        seek->setValue(500);settle(150);
        check(fixture.seekCount==1 && fixture.pos==90000000 && fixture.seekTrack=="/track/one","seek sends microseconds with the current MPRIS object path");
        volume->setValue(42);settle(150);check(qAbs(fixture.vol-.42)<.001,"volume updates through D-Bus Properties.Set");
        const int seekCount=fixture.seekCount;
        seek->setSliderDown(true);seek->setValue(650);
        fixture.track="/track/two";fixture.notify({{"Metadata",fixture.metadata()}});settle(100);
        seek->setSliderDown(false);settle(100);
        check(fixture.seekCount==seekCount,"changing track while dragging does not seek the new song unexpectedly");
        QTcpServer server;check(server.listen(QHostAddress::LocalHost),"isolated cover HTTP fixture starts");
        int downloads=0;QFile coverFile(root+"/cover.png");coverFile.open(QIODevice::ReadOnly);const QByteArray image=coverFile.readAll();
        QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{
            auto *socket=server.nextPendingConnection();QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                if(socket->property("sent").toBool())return;socket->setProperty("sent",true);socket->readAll();++downloads;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(image.size())+"\r\nConnection: close\r\n\r\n"+image);socket->disconnectFromHost();
            });QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        fixture.art=QString("http://127.0.0.1:%1/cover.png").arg(server.serverPort());fixture.notify({{"Metadata",fixture.metadata()}});settle(200);
        check(downloads==1 && !player->cover().isNull(),"remote artwork loads asynchronously");
        fixture.notify({{"Metadata",fixture.metadata()}});settle(100);
        check(downloads==1,"unchanged artwork is reused across metadata updates");
        fixture.art=QUrl::fromLocalFile(root+"/cover.png").toString();fixture.notify({{"Metadata",fixture.metadata()}});settle(80);
        fixture.enabled=false;fixture.notify({{"CanControl",false}});settle(80);
        check(!play->isEnabled() && !seek->isEnabled() && !volume->isEnabled(),"player capability changes disable unsupported controls");
        fixture.enabled=true;fixture.notify({{"CanControl",true}});settle(80);
        play->click();settle(100);canvas.setMusicWidgetVisible(false);settle(100);
        check(fixture.state=="Playing" && !player->progressActive(),"hiding music card leaves playback running and stops progress updates");
        check(!card->notesAnimating(),"hidden card stops the floating-note timer");
        canvas.setMusicWidgetVisible(true);settle(150);check(player->progressActive(),"revealing card synchronizes and resumes visible progress");
        check(card->notesAnimating(),"revealing playing card resumes floating notes");
        fixture.state="Stopped";fixture.notify({{"PlaybackStatus",fixture.state}});settle(100);
        check(!card->notesAnimating(),"stopped playback clears floating notes");
        fixture.state="Playing";fixture.notify({{"PlaybackStatus",fixture.state}});settle(100);
        const QRect initial=card->geometry();
        auto drag=[&](QPoint target){const QPoint local(90,20),g=card->mapToGlobal(local),delta=target-card->pos();
            QMouseEvent press(QEvent::MouseButtonPress,local,g,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(card,&press);
            QMouseEvent move(QEvent::MouseMove,local+delta,g+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(card,&move);
            QMouseEvent release(QEvent::MouseButtonRelease,local,g+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(card,&release);};
        drag(QPoint(0,0));check(card->geometry()==initial,"music card is locked outside layout editing");
        canvas.setGlobalEditMode(true);drag(QPoint(15,15));settle(150);
        check(card->pos()==QPoint(0,0),"music card inherits global layout editing and flush edge snapping");
        bool avoided=true;for(auto *icon:canvas.findChildren<DesktopIcon *>(QString(),Qt::FindDirectChildrenOnly))
            if(icon->isVisible() && icon->geometry().intersects(card->geometry().adjusted(-8,-8,8,8)))avoided=false;
        check(avoided,"desktop icons avoid the music card");canvas.setGlobalEditMode(false);
        const QString output=QDir::currentPath()+"/../test-results/music-"+QString::number(card->devicePixelRatioF());QDir().mkpath(output);card->grab().save(output+"/playing.png");
        bus.unregisterService(name);settle(150);
        check(!player->connected() && player->cover().isNull() && !player->progressActive(),"player exit clears stale track and cover and stops timer");
        check(!card->notesAnimating(),"player exit stops note animation");
        card->grab().save(output+"/offline.png");
        bus.registerService(name);settle(180);check(player->connected(),"player restart reconnects automatically");
        LiquidDesklet::setAutoStart("music",true);card->savePlacement();
    }
    {
        DesktopCanvas canvas;canvas.show();settle(1800);
        auto *card=canvas.findChild<MusicDesklet *>();
        check(card && card->isVisible() && card->pos()==QPoint(0,0),"host restart restores music startup and exact placement");
    }
    bus.unregisterService(name);bus.unregisterObject(object);
    return failures?1:0;
}
