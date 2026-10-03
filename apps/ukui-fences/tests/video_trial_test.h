#pragma once
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
        DesktopCanvas restored;restored.showAndActivate();
        timer.restart();
        auto restoredState=[&]{return QJsonDocument::fromJson(restored.videoWallpaperTrialStatus().toUtf8()).object();};
        while(!restoredState().value("hardwareReady").toBool() && timer.elapsed()<5000)settle(10);
        check(restoredState().value("active").toBool() && restoredState().value("currentIsSaved").toBool(),
              "fresh desktop automatically restores saved video after static wallpaper readiness");
        restored.disableVideoWallpaper();settle(150);
        check(!restoredState().value("active").toBool() && !QSettings().contains("wallpaper/videoPath"),
              "restore image stops playback and clears future startup selection");
    }
    {
        DesktopCanvas disabled;disabled.showAndActivate();settle(900);
        check(!QJsonDocument::fromJson(disabled.videoWallpaperTrialStatus().toUtf8()).object().value("active").toBool(),
              "disabled video stays disabled on a fresh desktop");
    }
    qputenv("PATH",oldPath);return failures?1:0;
}
