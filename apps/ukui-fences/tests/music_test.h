#include "music_fixture.h"
#include "MusicDesklet.h"
#include "MprisPlayer.h"
#include "DesktopIcon.h"
#include <QSlider>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include "music_multi_test.h"
#include "Lyrics.h"
static int runMusicTest(const QString &root){
    {
        const auto lines=Lyrics::parse("[ti:测试]\n[00:01.00][00:03.50]副歌\r\n[00:02]主歌\n[00:04.123]末行\n无时间戳");
        check(lines.size()==4 && lines[0].us==1000000 && lines[0].text=="副歌" && lines[1].text=="主歌" && lines[2].us==3500000 && lines[2].text=="副歌"
              && lines[3].us==4123000,"LRC lines with several timestamps expand into sorted entries");
        check(Lyrics::lineAt(lines,500000)==-1 && Lyrics::lineAt(lines,2000000)==1 && Lyrics::lineAt(lines,3600000)==2 && Lyrics::lineAt(lines,99000000)==3,
              "lyric lookup finds the line showing at a position");
        const auto shifted=Lyrics::parse("[offset:+500]\n[00:02.00]早半秒");
        check(shifted.size()==1 && shifted[0].us==1500000,"LRC offset tag shifts every line");
        const auto trailing=Lyrics::parse("[00:02.00]早半秒\n[offset:+500]");
        check(trailing.size()==1 && trailing[0].us==shifted[0].us,"trailing LRC offset has the same document-wide effect");
        const auto multiple=Lyrics::parse("[offset:+500]\n[00:01][00:02]副歌\n[offset:-250]");
        check(multiple.size()==2 && multiple[0].us==1250000 && multiple[1].us==2250000,
              "the final LRC offset declaration applies consistently to every timestamp");
        check(Lyrics::parse("纯文本歌词\n第二行").isEmpty(),"untimed text yields no timed lyrics");
    }
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
        canvas.setReduceMotion(true);
        check(!card->notesAnimating() && player->playing() && player->progressActive(),
              "reducing motion immediately stops decorative notes while music and progress continue");
        settle(90);
        check(!card->notesAnimating(),"playing updates cannot restart notes while motion is reduced");
        canvas.setReduceMotion(false);
        check(card->notesAnimating(),"restoring motion resumes visible playing notes");
        const QImage firstNotes=card->grab(QRect(6,4,104,120)).toImage();settle(180);
        check(firstNotes!=card->grab(QRect(6,4,104,120)).toImage(),"floating notes visibly move over the cover");
        const QRect expandedArea(112,4,card->width()-124,card->height()-56);
        const QImage rightNotes=card->grab(expandedArea).toImage();settle(210);
        check(rightNotes!=card->grab(expandedArea).toImage(),"notes animate across the right side above progress, beyond album art");
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
        bool holdCover=false;
        QList<QPointer<QTcpSocket>> heldCovers;
        const auto sendCover=[&image](QTcpSocket *socket){
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(image.size())+"\r\nConnection: close\r\n\r\n"+image);
            socket->disconnectFromHost();
        };
        QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{
            auto *socket=server.nextPendingConnection();QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                if(socket->property("sent").toBool())return;socket->setProperty("sent",true);socket->readAll();++downloads;
                if(holdCover)heldCovers.append(socket);else sendCover(socket);
            });QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        fixture.art=QString("http://127.0.0.1:%1/cover.png").arg(server.serverPort());fixture.notify({{"Metadata",fixture.metadata()}});settle(200);
        check(downloads==1 && !player->cover().isNull(),"remote artwork loads asynchronously");
        const int revision=player->metadataRevision();
        fixture.pos=45000000;fixture.notify({{"Position",fixture.pos}});settle(60);
        const int reads=fixture.positionReads;
        int updates=0;const auto repeatCounter=QObject::connect(player,&MprisPlayer::changed,[&]{++updates;});
        fixture.notify({{"Metadata",fixture.metadata()}});settle(100);
        check(downloads==1,"unchanged artwork is reused across metadata updates");
        check(player->metadataRevision()==revision,"identical metadata resends keep the metadata revision");
        check(player->position()==45000000 && fixture.positionReads==reads && updates==0,
              "identical metadata preserves paused progress without a Position query or UI update");
        QObject::disconnect(repeatCounter);
        fixture.song="只更新标题";fixture.notify({{"Metadata",fixture.metadata()}});settle(30);
        check(player->position()==45000000,"editing metadata for the same track never resets progress");
        settle(180);
        check(fixture.positionReads==reads && player->position()==45000000,
              "same-track metadata does not query playback position after the debounce");
        int rebuilds=0;auto counter=QObject::connect(player,&MprisPlayer::metadataChanged,[&]{++rebuilds;});
        for(const char *track:{"/track/burst1","/track/burst2","/track/burst3"}){
            fixture.track=track;fixture.art=QString("http://127.0.0.1:%1%2.png").arg(server.serverPort()).arg(track);
            fixture.notify({{"Metadata",fixture.metadata()}});settle(15);
        }
        fixture.lyrics="[00:01.00][00:03.50]副歌\n[00:02]主歌";fixture.notify({{"Metadata",fixture.metadata()}});
        fixture.vol=.21;fixture.notify({{"Volume",fixture.vol}});settle(20);
        check(volume->value()==21 && rebuilds==0,"volume controls update immediately while metadata work is debounced");
        check(waitMusic([&]{return rebuilds>0;}) && player->metadataRevision()>=revision+4,"each distinct metadata bumps the revision");
        settle(200);
        check(rebuilds==1 && player->lyrics().size()==3 && player->lyrics().at(1).text=="主歌","metadata bursts collapse into one debounced lyrics rebuild");
        check(downloads==2 && fixture.positionReads==reads+1,
              "distinct track/artwork bursts fetch only the final cover and request position once");
        QObject::disconnect(counter);fixture.lyrics.clear();
        fixture.track="/track/repeated_position";fixture.notify({{"Metadata",fixture.metadata()}});settle(20);
        check(player->trackId()==fixture.track,"the repeated-position fixture publishes the new track through D-Bus");
        fixture.notify({{"Position",fixture.pos}});settle(20);
        check(player->position()==fixture.pos,"a new-track Position event overrides reset even when it repeats the previous numeric value");
        settle(140);
        check(fixture.positionReads==reads+1,"an explicit Position event avoids the deferred track-position query");
        const int beforeSlow=downloads;holdCover=true;
        fixture.art=QString("http://127.0.0.1:%1/slow.png").arg(server.serverPort());
        fixture.notify({{"Metadata",fixture.metadata()}});
        check(waitMusic([&]{return downloads==beforeSlow+1;}),"a delayed real cover request is in flight");
        fixture.art=QString("http://127.0.0.1:%1/discarded.png").arg(server.serverPort());
        fixture.notify({{"Metadata",fixture.metadata()}});settle(15);
        fixture.art=QString("http://127.0.0.1:%1/slow.png").arg(server.serverPort());
        fixture.notify({{"Metadata",fixture.metadata()}});
        check(waitMusic([&]{return downloads==beforeSlow+2;}),
              "returning to a cancelled cover URL retries it instead of leaving a cached empty cover");
        holdCover=false;for(const auto &socket:heldCovers)if(socket)sendCover(socket);
        check(waitMusic([&]{return !player->cover().isNull();}),
              "only the current delayed cover becomes visible after a metadata burst");
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
    return runMusicMultiTest(root);
}
