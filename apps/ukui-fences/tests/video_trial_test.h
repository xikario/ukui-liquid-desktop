#pragma once
#include <QCloseEvent>
#include <QScreen>
#include "VideoWallpaperPreview.h"
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
import json,sys,time
json.loads(sys.stdin.readline())
print(json.dumps(dict(event='media', decoder='vaapi', sourceFps=60)), flush=True)
time.sleep(.3)
print(json.dumps(dict(event='stats', decoder='vaapi', sourceFps=60, width=3840, height=2160)), flush=True)
print(json.dumps(dict(event='state', state='playing')), flush=True)
for line in sys.stdin:
    if json.loads(line).get('command') == 'quit': break
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
        auto *first=preview.findChild<QLabel *>("videoPreviewFrame0");
        auto *last=preview.findChild<QLabel *>("videoPreviewFrame2");
        check(first && last && first->pixmap() && last->pixmap()
            && first->pixmap()->toImage()!=last->pixmap()->toImage(),"real preview frames show different points in the clip");
        QDir().mkpath("artifacts");preview.grab().save("artifacts/video-thumbnail-preview.png");
        preview.confirmFile(realClip);
        {
            VideoWallpaperPreview reopened;
            reopened.setFile(realClip);reopened.show();
            check(reopened.property("cacheHit").toBool() && reopened.property("frameCount").toInt()==3,
                  "confirmed preview reloads persistent frames without FFmpeg");
            const int before=reopened.property("animationFrame").toInt();settle(1050);
            check(reopened.property("animationFrame").toInt()!=before,"visible preview animates cached frames");
            reopened.hide();const int hidden=reopened.property("animationFrame").toInt();settle(1050);
            check(reopened.property("animationFrame").toInt()==hidden,"hidden preview does not keep animating");
        }
        preview.setFile(root+"/missing.mp4");preview.setFile(QString());settle(400);
        check(preview.property("frameCount").toInt()==0 && preview.property("previewState")=="empty",
            "cancelling preview clears stale images and stops extraction");
    }
    return failures?1:0;
}
