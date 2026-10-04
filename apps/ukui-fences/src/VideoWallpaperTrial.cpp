#include "VideoWallpaperTrial.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include <QApplication>
#include <QChildEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QWidget>
#include <QtMath>

VideoWallpaperTrial::VideoWallpaperTrial(QWidget *canvas,std::function<void(const QImage &)> poster,
    std::function<void()> restore,std::function<QRegion()> overlay)
    :QObject(canvas),canvas(canvas),poster(poster),restore(restore),overlay(overlay) {
    geometryTimer.setSingleShot(true);geometryTimer.setInterval(16);
    connect(&geometryTimer,&QTimer::timeout,this,[this]{sendGeometry();});
    canvas->installEventFilter(this);
    for(auto *child:canvas->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))child->installEventFilter(this);
    connect(&process,&QProcess::started,this,[this]{sendGeometry();});
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{receive();});
    connect(&process,&QProcess::readyReadStandardError,this,[this]{process.readAllStandardError();});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        if(enabled){reason=QString("player exited (%1)").arg(code);stop();state="error";if(changed)changed();}
    });
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(enabled && error==QProcess::FailedToStart){reason="cannot start player";stop();state="error";if(changed)changed();}
    });
}
VideoWallpaperTrial::~VideoWallpaperTrial(){restore={};changed={};stop();}
bool VideoWallpaperTrial::start(const QString &path) {
    const auto file=QFileInfo(path);
    if(QGuiApplication::platformName()!="xcb" || !file.isFile() || file.suffix().toLower()!="mp4")return false;
    const QString python=QStandardPaths::findExecutable("python3"),ffmpeg=QStandardPaths::findExecutable("ffmpeg");
    QString helper=QStandardPaths::findExecutable("ukui-fences-video-trial");
    if(helper.isEmpty())helper=QStringLiteral(UKUI_FENCES_VIDEO_TRIAL_PATH);
    if(python.isEmpty() || ffmpeg.isEmpty() || !QFileInfo::exists(helper))return false;
    stop();enabled=true;state="preparing";reason.clear();loops=0;playback={};source=file.canonicalFilePath();
    if(changed)changed();
    const auto stamp=++revision;const auto video=source;
    BackgroundTask::run(this,[ffmpeg,video]{
        QProcess decode;decode.start(ffmpeg,{"-hide_banner","-loglevel","error","-nostdin","-threads","2",
            "-i",video,"-an","-frames:v","1","-vf","scale=1920:-2","-f","image2pipe","-vcodec","png","pipe:1"});
        if(!decode.waitForFinished(15000)){decode.kill();decode.waitForFinished(500);return QImage();}
        if(decode.exitCode()!=0)return QImage();
        return QImage::fromData(decode.readAllStandardOutput(),"PNG");
    },[this,stamp,python,helper](const QImage &image){
        if(!enabled || stamp!=revision)return;
        if(image.isNull()){reason="cannot decode first frame";stop();state="error";if(changed)changed();return;}
        poster(image);state="starting";
        if(changed)changed();
        auto env=QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONUNBUFFERED","1");
        process.setProcessEnvironment(env);
        process.start(python,{helper,"--parent",QString::number(canvas->winId()),"--file",source});
    });
    return true;
}
void VideoWallpaperTrial::stop() {
    const bool wasEnabled=enabled;enabled=false;++revision;geometryTimer.stop();
    if(process.state()!=QProcess::NotRunning){
        process.write("{\"command\":\"quit\"}\n");process.closeWriteChannel();
        if(!process.waitForFinished(1200)){process.terminate();if(!process.waitForFinished(1200)){process.kill();process.waitForFinished(500);}}
    }
    output.clear();lastGeometry.clear();state="stopped";
    if(wasEnabled && restore)restore();
    if(wasEnabled && changed)changed();
}
QString VideoWallpaperTrial::status() const {
    auto result=QJsonObject{{"active",enabled},{"state",state},
        {"reason",reason},{"loops",loops},{"pid",double(process.processId())},
        {"posterOnlyGlass",true},{"source",source}};
    for(auto it=playback.constBegin();it!=playback.constEnd();++it)result.insert(it.key(),it.value());
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}
void VideoWallpaperTrial::receive() {
    output+=process.readAllStandardOutput();if(output.size()>65536){output.clear();return;}
    int newline;
    while((newline=output.indexOf('\n'))>=0){
        const auto line=output.left(newline);output.remove(0,newline+1);
        const auto message=QJsonDocument::fromJson(line).object();if(message.isEmpty())continue;
        if(message.value("event")=="error"){reason=message.value("reason").toString();stop();state="error";if(changed)changed();return;}
        if(message.value("event")=="stats" && message.value("decoder")=="vaapi")
            playback.insert("hardwareReady",true);
        if(message.contains("state"))state=message.value("state").toString();
        if(message.contains("loops"))loops=message.value("loops").toInt();
        for(const char *key:{"decoder","sourceFps","decodedFps","displayFps","frameDrops",
            "decoderDrops","delayedFrames","position","width","height","originalSource"})
            if(message.contains(key))playback.insert(key,message.value(key));
        if(changed)changed();
    }
}
void VideoWallpaperTrial::refreshGeometry(){lastGeometry.clear();scheduleGeometry();}
void VideoWallpaperTrial::scheduleGeometry(){if(enabled&&!geometryTimer.isActive())geometryTimer.start();}
void VideoWallpaperTrial::sendGeometry() {
    if(!enabled || process.state()!=QProcess::Running)return;
    const qreal dpr=canvas->devicePixelRatioF();QRegion free(canvas->rect());
    for(auto *child:canvas->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly)){
        if(!child->isVisible() || child->isWindow())continue;
        const auto mask=child->mask().isEmpty()?QRegion(child->rect()):child->mask();free-=mask.translated(child->pos());
    }
    if(overlay)free-=overlay();QJsonArray rectangles;
    for(const auto &rect:free){
        const int left=qFloor(rect.left()*dpr),top=qFloor(rect.top()*dpr);
        rectangles.append(QJsonArray{left,top,qCeil((rect.right()+1)*dpr)-left,qCeil((rect.bottom()+1)*dpr)-top});
    }
    const auto geometry=QJsonDocument(QJsonObject{{"command","geometry"},{"width",qRound(canvas->width()*dpr)},
        {"height",qRound(canvas->height()*dpr)},{"visible",canvas->isVisible()&&!canvas->isMinimized()},
        {"rects",rectangles}}).toJson(QJsonDocument::Compact)+'\n';
    if(geometry!=lastGeometry){lastGeometry=geometry;process.write(geometry);}
}
bool VideoWallpaperTrial::eventFilter(QObject *object,QEvent *event) {
    if(object==canvas && event->type()==QEvent::ChildPolished)
        if(auto *child=qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child()))child->installEventFilter(this);
    switch(event->type()){
    case QEvent::Move:case QEvent::Resize:case QEvent::Show:case QEvent::Hide:
    case QEvent::WindowStateChange:case QEvent::Paint:scheduleGeometry();break;
    default:break;
    }return false;
}
