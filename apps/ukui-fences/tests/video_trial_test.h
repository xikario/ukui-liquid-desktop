#pragma once
#include "VideoWallpaperCache.h"
#include <QCloseEvent>
#include <QScreen>
#include "VideoWallpaperPreview.h"
#include "MusicDesklet.h"
#include "SmartSpaceWidget.h"
#include "VideoWallpaperRegion.h"
static int runVideoTrialTest(const QString &root)
{
    QSettings settings;settings.setValue("systemMonitor/autoStart",false);settings.setValue("smartSpace/autoStart",false);
    for(const char *key:{"clock","activity","music","calendar"})LiquidDesklet::setAutoStart(key,false);
    QDir().mkpath(root+"/config/kyfences");QDir().mkpath(root+"/bin");
    QImage original(800,500,QImage::Format_RGB32);original.fill(QColor("#dc3030"));original.save(root+"/original.png");
    QImage poster(800,500,QImage::Format_RGB32);poster.fill(QColor("#3030dc"));poster.save(root+"/poster.png");
    QFile layout(root+"/config/kyfences/layout.json");layout.open(QIODevice::WriteOnly);
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",root+"/original.png"},{"wallpaperMode",2},{"fences",QJsonArray{}}}).toJson());layout.close();
    auto executable=[&](const QString &name,const QByteArray &body){QFile file(root+"/bin/"+name);file.open(QIODevice::WriteOnly);file.write(body);file.close();file.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);};
    executable("ffmpeg",("#!/usr/bin/python3\nimport sys,time\ntime.sleep(.1)\nsys.stdout.buffer.write(open('"+root+"/poster.png','rb').read())\n").toUtf8());
    executable("ffprobe","#!/usr/bin/python3\nimport time,sys\ntime.sleep(.4)\nsys.exit(1)\n");
    const auto oldPath=qgetenv("PATH");qputenv("PATH",(root+"/bin:").toUtf8()+oldPath);
    DesktopCanvas canvas;canvas.showAndActivate();QElapsedTimer timer;timer.start();
    while(!canvas.isVisible()&&timer.elapsed()<5000)settle(20);
    auto state=[&]{return QJsonDocument::fromJson(canvas.videoWallpaperTrialStatus().toUtf8()).object();};
    auto color=[&]{return canvas.wallpaperBackdrop(QRect(canvas.mapToGlobal(QPoint()),QSize(10,10)),1).pixelColor(5,5);};
    check(!canvas.startVideoWallpaperTrial(root+"/missing.mp4"),"missing trial media rejected without changing the wallpaper");
    QFile media(root+"/trial.mp4");media.open(QIODevice::WriteOnly);media.write("fixture");media.close();
    check(canvas.startVideoWallpaperTrial(media.fileName()),"valid local trial request accepted");
    canvas.stopVideoWallpaperTrial();settle(500);
    check(!state().value("active").toBool()&&color().red()>180,"cancel during first-frame extraction drops stale completion and restores original");
    bool sawPoster=false;
    QObject::connect(&canvas,&DesktopCanvas::wallpaperChanged,&canvas,[&]{sawPoster|=color().blue()>180;});
    check(canvas.startVideoWallpaperTrial(media.fileName()),"trial can restart after cancellation");
    timer.restart();
    while(state().value("state").toString()!="error"&&timer.elapsed()<6000){settle(10);sawPoster|=color().blue()>180;}
    check(sawPoster,"first frame is installed in the real wallpaper cache before playback");
    settle(100);
    check(state().value("state")=="error"&&!state().value("active").toBool()&&color().red()>180,
          "backend failure terminates trial and restores actual original wallpaper");
    QFile saved(root+"/config/kyfences/layout.json");saved.open(QIODevice::ReadOnly);
    const auto config=QJsonDocument::fromJson(saved.readAll()).object();
    check(config.value("wallpaperPath")==root+"/original.png"&&config.value("wallpaperMode")==2,
          "trial never persists media, first frame or changed fitting mode");
    executable("ukui-fences-video-trial", R"PY(#!/usr/bin/python3
import json,sys,time,os
log=open(os.path.join(os.path.dirname(sys.argv[0]),'geometry.jsonl'),'a',buffering=1)
first=sys.stdin.readline();log.write(first);json.loads(first)
print(json.dumps(dict(event='media', decoder='vaapi', sourceFps=60)), flush=True)
time.sleep(.3)
print(json.dumps(dict(event='stats', decoder='vaapi', sourceFps=60, width=3840, height=2160)), flush=True)
print(json.dumps(dict(event='state', state='playing')), flush=True)
for line in sys.stdin:
    log.write(line)
    message=json.loads(line)
    if message.get('command') == 'quit': break
)PY");
    bool deferredSave=false;
    QObject::connect(&canvas,&DesktopCanvas::videoWallpaperTrialChanged,&canvas,[&]{
        const auto s=state();
        if(s.value("saving").toBool() && s.value("sourceFps").toInt()==60 && !s.value("hardwareReady").toBool())
            deferredSave=QSettings().value("wallpaper/videoPath").toString().isEmpty();
    });
    check(canvas.setVideoWallpaper(media.fileName()),"persistent video request accepted");
    timer.restart();
    while(!state().value("hardwareReady").toBool() && timer.elapsed()<5000)settle(10);
    check(deferredSave,"media metadata alone does not save an unverified video");
    check(QSettings().value("wallpaper/videoPath")==media.fileName()
          && state().value("persistent").toBool(),"verified hardware playback saves the selected video");
    auto freeRegion=[&]{
        QFile log(root+"/bin/geometry.jsonl");log.open(QIODevice::ReadOnly);QJsonObject geometry;
        while(!log.atEnd()){
            const auto message=QJsonDocument::fromJson(log.readLine()).object();
            if(message.value("command")=="geometry")geometry=message;
        }
        QRegion region;
        for(const auto &value:geometry.value("rects").toArray()){
            const auto rect=value.toArray();
            region|=QRect(rect[0].toInt(),rect[1].toInt(),rect[2].toInt(),rect[3].toInt());
        }
        return region;
    };
    auto freeArea=[&]{
        qint64 area=0;for(const QRect &rect:freeRegion())area+=qint64(rect.width())*rect.height();
        return area;
    };
    const auto unobstructed=freeArea();
    auto *cutout=new QWidget(&canvas);cutout->setGeometry(600,400,100,80);
    cutout->setMask(QRegion(0,0,100,80));cutout->show();settle(100);
    check(freeArea()<unobstructed,"new child removes its real visible region from the video drawable");
    delete cutout;settle(100);
    check(freeArea()==unobstructed,"destroyed child restores the video region without stale mask pointers");
    cutout=new QWidget(&canvas);cutout->setGeometry(600,400,100,80);
    cutout->setMask(QRegion(0,0,40,40));cutout->show();settle(100);
    const auto smallerMask=freeArea();cutout->setMask(QRegion(0,0,100,80));cutout->update();settle(100);
    check(freeArea()<smallerMask,"replacement child and paint-time mask changes reach the player geometry");
    delete cutout;settle(100);
    // Follow the actual player geometry: transparent pixels must remain video,
    // while glass/text keep their cached poster. Empty QWidget masks used to
    // reserve both the entire music card and the star's rectangular hit area.
    auto videoAt=[&](const QPoint &logical){
        const qreal dpr=canvas.devicePixelRatioF();
        return freeRegion().contains(QPoint(qFloor(logical.x()*dpr),qFloor(logical.y()*dpr)));
    };
    canvas.setMusicWidgetVisible(true);
    auto *music=canvas.findChild<MusicDesklet *>();
    check(music!=nullptr,"video test creates the real music card");
    if(music){
        music->move(500,400);timer.restart();
        while(music->material().isNull()&&timer.elapsed()<3000)settle(20);
        settle(150);
        check(!music->internalWinId() && music->mask().isEmpty() && videoAt(music->pos()),
              "music transparent corner remains live video without altering input mask");
        check(!videoAt(music->geometry().center()),"music glass body remains protected from native video");
        const int builds=music->materialBuilds();
        for(int i=0;i<5;++i){music->grab();settle(30);}
        check(music->materialBuilds()==builds,"ordinary music repaints do not rebuild optics or video coverage");
        music->resize(music->size()+QSize(40,20));settle(300);
        check(videoAt(music->pos()+QPoint(music->width()-1,music->height()-1))
            && !videoAt(music->geometry().center()),"music resize updates the player's rounded cutout");
        canvas.setMusicWidgetVisible(false);settle(100);
    }
    QSettings().setValue("smartSpace/themeMode",3);
    QSettings().setValue("smartSpace/defaultHidden",false);
    QSettings().setValue("smartSpace/alwaysOnTop",false);
    canvas.showSmartSpaceWidget();canvas.moveSmartSpace(100,200);canvas.resizeSmartSpace(800,500);settle(300);
    auto *smart=canvas.findChild<SmartSpaceWidget *>();
    check(smart!=nullptr,"video test creates the real Smart Space surface");
    if(smart){
        check(videoAt(smart->pos()) && !videoAt(smart->geometry().center()),
              "expanded Smart Space leaves transparent margins on live video");
        smart->hideToNearestEdge();settle(300);
        check(smart->edgeHidden() && videoAt(smart->pos()+QPoint(1,1)),
              "collapsed entry no longer freezes its entire rectangular background");
        check(!videoAt(smart->geometry().center()),"star artwork remains above the video");
        check(!smart->internalWinId() && smart->mask().isEmpty(),"star keeps its mouse hit area without a native rectangle");
        smart->revealFromEdge();settle(300);
        check(videoAt(smart->pos()) && !videoAt(smart->geometry().center()),
              "reveal retires the animation cutout and restores rounded glass coverage");
        smart->hide();settle(100);
    }
    check(!canvas.setVideoWallpaper(root+"/missing.mp4")
          && QSettings().value("wallpaper/videoPath")==media.fileName(),"invalid replacement preserves saved selection");
    // A backend failure also preserves the last working choice for next launch.
    executable("ukui-fences-video-trial", "#!/usr/bin/python3\nimport sys,json\njson.loads(sys.stdin.readline())\nprint(json.dumps(dict(event='error',reason='fixture failure')),flush=True)\n");
    check(canvas.setVideoWallpaper(media.fileName()),"failure replacement accepted for asynchronous validation");
    timer.restart();while(state().value("state")!="error" && timer.elapsed()<5000)settle(10);
    check(state().value("state")=="error" && QSettings().value("wallpaper/videoPath")==media.fileName(),
          "backend failure restores image without overwriting the saved video");
    executable("ukui-fences-video-trial", R"PY(#!/usr/bin/python3
import json,sys
json.loads(sys.stdin.readline())
print(json.dumps(dict(event='stats',decoder='vaapi',sourceFps=60)),flush=True)
print(json.dumps(dict(event='state',state='playing')),flush=True)
for line in sys.stdin:
    if json.loads(line).get('command')=='quit': break
)PY");
    canvas.hideFences();
    {
        DesktopCanvas restored;
        bool cachedAtFirstMap=false;
        QObject::connect(&restored,&DesktopCanvas::initialWallpaperReady,&restored,[&]{
            const auto image=restored.wallpaperBackdrop(QRect(restored.mapToGlobal(QPoint()),QSize(10,10)),1);
            cachedAtFirstMap=!image.isNull() && image.pixelColor(5,5).blue()>180;
        });
        restored.showAndActivate();
        timer.restart();
        auto restoredState=[&]{return QJsonDocument::fromJson(restored.videoWallpaperTrialStatus().toUtf8()).object();};
        while(!restoredState().value("hardwareReady").toBool() && timer.elapsed()<5000)settle(10);
        check(restoredState().value("active").toBool() && restoredState().value("currentIsSaved").toBool(),
              "fresh desktop automatically restores saved video after static wallpaper readiness");
        check(cachedAtFirstMap,"saved video poster is prepared before first desktop mapping");
        restored.disableVideoWallpaper();settle(150);
        check(!restoredState().value("active").toBool() && !QSettings().contains("wallpaper/videoPath"),
              "restore image stops playback and clears future startup selection");
    }
    {
        DesktopCanvas disabled;disabled.showAndActivate();settle(900);
        check(!QJsonDocument::fromJson(disabled.videoWallpaperTrialStatus().toUtf8()).object().value("active").toBool(),
              "disabled video stays disabled on a fresh desktop");
    }
    // Exercise the real wallpaper page: choosing is a draft, applying validates,
    // low resolution is overridable, and a saved page closes without a warning.
    executable("ffprobe", R"PY(#!/usr/bin/python3
print('{"streams":[{"width":640,"height":360}],"format":{"duration":"10.0"}}')
)PY");
    canvas.showSettingsPage("wallpaper");settle(150);
    FencesSettingsWindow *window=nullptr;
    for(auto *w:QApplication::topLevelWidgets())
        if(auto *f=qobject_cast<FencesSettingsWindow *>(w))window=f;
    check(window,"wallpaper settings opens for draft integration");
    if(window) {
        auto *form=window->findChild<QWidget *>("wallpaperSettingsForm");
        auto *kind=window->findChild<QComboBox *>("wallpaperKind");
        auto *path=window->findChild<QLineEdit *>("videoWallpaperPath");
        auto *apply=window->findChild<QPushButton *>("wallpaperApply");
        check(form && kind && path && apply,"wallpaper has explicit type, video draft and shared apply");
        if(form && kind && path && apply) {
            kind->setCurrentIndex(1);path->setText(media.fileName());settle(100);
            check(!state().value("active").toBool() && !QSettings().contains("wallpaper/videoPath")
                && form->property("settingsDirty").toBool(),"choosing video edits only the draft");
            auto *frames=window->findChild<QWidget *>("videoWallpaperFrames");
            timer.restart();while(frames && frames->property("frameCount").toInt()!=3 && timer.elapsed()<5000)settle(20);
            check(frames && frames->property("frameCount").toInt()==3
                && frames->property("previewState")=="ready" && !state().value("active").toBool(),
                "video draft extracts three still thumbnails without starting wallpaper");
            if(frames) {
                for(int i=0;i<3;++i) {
                    auto *label=frames->findChild<QLabel *>(QString("videoPreviewFrame%1").arg(i));
                    check(label && label->pixmap() && !label->pixmap()->isNull(),"extracted thumbnail has actual image pixels");
                }
            }
            path->setText(root+"/missing-preview.mp4");path->setText(media.fileName());
            timer.restart();while(frames && frames->property("frameCount").toInt()!=3 && timer.elapsed()<5000)settle(20);
            check(frames && frames->property("frameCount").toInt()==3,"rapid file changes retain only the current video preview");
            bool prompted=false,mentionsDimensions=false;
            QTimer answer;answer.setInterval(20);
            int response=QMessageBox::No;
            QObject::connect(&answer,&QTimer::timeout,window,[&]{
                for(auto *w:QApplication::topLevelWidgets())
                    if(w->isVisible() && w->windowTitle()=="视频分辨率较低")
                        if(auto *dialog=qobject_cast<QDialog *>(w)) {
                            prompted=true;
                            for(auto *label:dialog->findChildren<QLabel *>())
                                mentionsDimensions|=label->text().contains("640×360") && label->text().contains("是否仍然应用");
                            dialog->done(response);
                        }
            });
            answer.start();apply->click();timer.restart();
            while(!apply->isEnabled() && timer.elapsed()<6000)settle(20);
            check(prompted && mentionsDimensions && !state().value("active").toBool()
                && form->property("settingsDirty").toBool(),"low-resolution cancellation preserves current wallpaper and draft");
            prompted=false;response=QMessageBox::Yes;apply->click();timer.restart();
            while(!apply->isEnabled() && timer.elapsed()<6000)settle(20);
            check(prompted && state().value("active").toBool() && state().value("persistent").toBool()
                && !form->property("settingsDirty").toBool(),"confirmed low-resolution video applies and clears unsaved state");
            answer.stop();
            bool videoClosePrompt=false;
            QTimer videoCloseGuard;videoCloseGuard.setInterval(20);
            QObject::connect(&videoCloseGuard,&QTimer::timeout,window,[&]{
                for(auto *w:QApplication::topLevelWidgets())
                    if(w->isVisible() && w!=window)
                        if(auto *dialog=qobject_cast<QDialog *>(w)){videoClosePrompt=true;dialog->reject();}
            });
            videoCloseGuard.start();QCloseEvent closeEvent;
            QApplication::sendEvent(window,&closeEvent);videoCloseGuard.stop();
            check(closeEvent.isAccepted() && !videoClosePrompt,
                "applied video closes without a false unsaved warning");
            kind->setCurrentIndex(0);settle(50);
            check(state().value("active").toBool() && form->property("settingsDirty").toBool(),
                "switching draft to static keeps playing until apply");
            apply->click();settle(150);
            check(!state().value("active").toBool() && !QSettings().contains("wallpaper/videoPath")
                && !form->property("settingsDirty").toBool(),"static apply stops video and clears saved active selection");
            // Revert draft changes to the baseline; no false dirty flag remains.
            kind->setCurrentIndex(1);kind->setCurrentIndex(0);
            check(!form->property("settingsDirty").toBool(),"reverted wallpaper draft is clean");
            bool closePrompt=false;
            QTimer closeGuard;closeGuard.setInterval(20);
            QObject::connect(&closeGuard,&QTimer::timeout,window,[&]{
                for(auto *w:QApplication::topLevelWidgets())
                    if(w->isVisible() && w!=window)
                        if(auto *dialog=qobject_cast<QDialog *>(w)){closePrompt=true;dialog->reject();}
            });
            QPointer<FencesSettingsWindow> closing=window;
            closeGuard.start();window->close();settle(60);closeGuard.stop();
            check(!closePrompt && (!closing || !closing->isVisible()),"applied wallpaper settings closes without unsaved confirmation");
        }
    }
    // Reproduce Peony raising/recreating its desktop during a RandR change.
    canvas.showAndActivate();settle(200);
    if (Display *display=XOpenDisplay(nullptr)) {
        const Window rootWindow=DefaultRootWindow(display);
        const Window peer=XCreateSimpleWindow(display,rootWindow,0,0,800,600,0,0,0);
        XClassHint hint;hint.res_name=const_cast<char *>("peony-qt-desktop");
        hint.res_class=const_cast<char *>("peony-qt-desktop");XSetClassHint(display,peer,&hint);
        const Atom desktop=XInternAtom(display,"_NET_WM_WINDOW_TYPE_DESKTOP",False);
        XChangeProperty(display,peer,XInternAtom(display,"_NET_WM_WINDOW_TYPE",False),XA_ATOM,32,
            PropModeReplace,reinterpret_cast<const unsigned char *>(&desktop),1);
        XMapRaised(display,peer);XSync(display,False);
        auto abovePeer=[&]{
            Window root,parent,*children=nullptr;unsigned int count=0;int ownIndex=-1,peerIndex=-1;
            XQueryTree(display,rootWindow,&root,&parent,&children,&count);
            for(unsigned int i=0;i<count;++i){if(children[i]==canvas.winId())ownIndex=i;if(children[i]==peer)peerIndex=i;}
            if(children)XFree(children);return ownIndex>peerIndex && peerIndex>=0;
        };
        check(!abovePeer(),"fixture Peony desktop initially covers Fences");
        auto *screen=QApplication::primaryScreen();
        QMetaObject::invokeMethod(screen,"geometryChanged",Qt::DirectConnection,Q_ARG(QRect,screen->geometry()));
        settle(450);
        check(abovePeer(),"screen geometry change restores Fences above the system desktop");
        canvas.hideFences();
        QMetaObject::invokeMethod(screen,"geometryChanged",Qt::DirectConnection,Q_ARG(QRect,screen->geometry()));
        settle(1500);
        check(!canvas.isVisible(),"screen change and deferred restacks respect explicit hide");
        XDestroyWindow(display,peer);XCloseDisplay(display);
    }
    qputenv("PATH",oldPath);
    QProcess encode;
    const QString realClip=root+"/preview-real.mp4";
    encode.start("ffmpeg",{"-hide_banner","-loglevel","error","-f","lavfi","-i",
        "testsrc2=size=160x90:rate=10","-t","2","-c:v","libx264","-threads","1",
        "-preset","ultrafast","-g","5","-pix_fmt","yuv420p","-y",realClip});
    const bool encoded=encode.waitForFinished(6000) && encode.exitCode()==0;
    if(!encoded){encode.kill();encode.waitForFinished(500);}
    check(encoded,"real thumbnail fixture encodes with bounded FFmpeg work");
    if(encoded) {
        VideoWallpaperPreview preview;
        preview.setFile(realClip);preview.show();timer.restart();
        while(preview.property("previewState")!="ready" && timer.elapsed()<8000)settle(20);
        check(preview.property("frameCount").toInt()==3,"real FFmpeg extracts three timestamped video frames");
        auto cleanPreview=[&](VideoWallpaperPreview &view){
            auto *status=view.findChild<QLabel *>("videoPreviewStatus");
            if(!status || status->isVisible() || !status->text().isEmpty())return false;
            for(auto *label:view.findChildren<QLabel *>())
                if(label->isVisible() && (label->text().contains("秒附近")
                    || label->text().contains(" / 3") || label->text().contains("三帧")))return false;
            return view.findChild<QLabel *>("videoAnimatedPreview")!=nullptr;
        };
        check(cleanPreview(preview),"successful preview displays image without numbering or frame descriptions");
        auto *first=preview.findChild<QLabel *>("videoPreviewFrame0");
        auto *last=preview.findChild<QLabel *>("videoPreviewFrame2");
        check(first && last && first->pixmap() && last->pixmap()
            && first->pixmap()->toImage()!=last->pixmap()->toImage(),"real preview frames show different points in the clip");
        QDir().mkpath("artifacts");preview.grab().save("artifacts/video-thumbnail-preview.png");
        VideoWallpaperPreview waiting;
        waiting.setFile(realClip); // No cache yet; its debounce races confirmation.
        preview.confirmFile(realClip);
        QFile manifest(VideoWallpaperCache::directory(realClip)+"/preview.json");
        manifest.open(QIODevice::ReadOnly);const QByteArray committed=manifest.readAll();manifest.close();
        timer.restart();
        while(!waiting.property("cacheHit").toBool() && timer.elapsed()<4000)settle(20);
        waiting.confirmFile(realClip);settle(300);
        manifest.open(QIODevice::ReadOnly);
        check(waiting.property("cacheHit").toBool() && manifest.readAll()==committed,
              "pending second preview adopts the confirmed cache without replacing its random frames");
        manifest.close();
        const auto metadata=QJsonDocument::fromJson(committed).object();
        QImage highDpi(VideoWallpaperCache::directory(realClip)+"/frame640-0.png");
        check(metadata.value("version")==2 && highDpi.size()==QSize(640,360),
              "new previews cache enough real pixels for a 2x display");
        {
            VideoWallpaperPreview reopened;
            reopened.setFile(realClip);reopened.show();
            check(reopened.property("cacheHit").toBool() && reopened.property("frameCount").toInt()==3,
                  "confirmed preview reloads persistent frames without FFmpeg");
            check(cleanPreview(reopened),"cached preview also keeps success and timestamp descriptions hidden");
            const int before=reopened.property("animationFrame").toInt();settle(1050);
            check(reopened.property("animationFrame").toInt()!=before,"visible preview animates cached frames");
            reopened.hide();const int hidden=reopened.property("animationFrame").toInt();settle(1050);
            check(reopened.property("animationFrame").toInt()==hidden,"hidden preview does not keep animating");
        }
        preview.setFile(root+"/missing.mp4");preview.setFile(QString());settle(400);
        check(preview.property("frameCount").toInt()==0 && preview.property("previewState")=="empty",
            "cancelling preview clears stale images and stops extraction");
        preview.setFile(root+"/missing.mp4");settle(400);
        auto *errorLabel=preview.findChild<QLabel *>("videoPreviewStatus");
        check(errorLabel && errorLabel->isVisible() && !errorLabel->text().isEmpty(),
              "preview errors remain readable after removing decorative descriptions");
        QFile prior(root+"/prior.mp4");prior.open(QIODevice::WriteOnly);prior.write("prior");prior.close();
        QFile stale(root+"/stale.mp4");stale.open(QIODevice::WriteOnly);stale.write("stale");stale.close();
        const auto priorDir=VideoWallpaperCache::directory(prior.fileName());
        const auto staleDir=VideoWallpaperCache::directory(stale.fileName());
        QDir().mkpath(priorDir);QDir().mkpath(staleDir);
        VideoWallpaperCache::prune(realClip,prior.fileName());
        check(QDir(VideoWallpaperCache::directory(realClip)).exists() && QDir(priorDir).exists() && !QDir(staleDir).exists(),
              "confirmation prunes only obsolete owned caches and retains the previous video");
    }
    return failures?1:0;
}
