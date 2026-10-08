#include "VideoWallpaperPreview.h"
#include "VideoWallpaperCache.h"
#include <QRandomGenerator>
#include <QSaveFile>
#include <QShowEvent>
#include <QHideEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QVariant>
#include <QLockFile>
#include <QSettings>
#include <cmath>

VideoWallpaperPreview::VideoWallpaperPreview(QWidget *parent) : QWidget(parent)
{
    setObjectName("videoWallpaperFrames");
    auto *layout = new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    m_animatedImage=new QLabel(this);m_animatedImage->setFixedSize(320,180);
    m_animatedImage->setObjectName("videoAnimatedPreview");
    m_animatedImage->setAlignment(Qt::AlignCenter);layout->addWidget(m_animatedImage);
    m_animation.setInterval(900);
    connect(&m_animation,&QTimer::timeout,this,[this]{
        if(!isVisible() || m_frames.size()!=3){m_animation.stop();return;}
        m_displayFrame=(m_displayFrame+1)%3;showFrame();
    });
    for (int i=0;i<3;++i) {

        auto *image = new QLabel("等待预览",this);
        image->setObjectName(QString("videoPreviewFrame%1").arg(i));
        image->setFixedSize(140,90);image->setAlignment(Qt::AlignCenter);
        image->setStyleSheet("background:#101827;border:1px solid #39445a;border-radius:6px;color:#cbd5e1;");
        image->hide();
        m_images.append(image);
    }

    m_hint = new QLabel(this);m_hint->setObjectName("videoPreviewStatus");
    m_hint->setWordWrap(true);layout->addWidget(m_hint);m_hint->hide();
    m_debounce.setSingleShot(true);m_debounce.setInterval(250);
    m_deadline.setSingleShot(true);m_deadline.setInterval(5000);
    connect(&m_debounce,&QTimer::timeout,this,&VideoWallpaperPreview::begin);
    connect(&m_deadline,&QTimer::timeout,this,[this]{m_timedOut=true;m_process.kill();});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&VideoWallpaperPreview::completed);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart) {m_deadline.stop();fail("无法生成预览，请检查 FFmpeg/ffprobe 是否可用。");}
    });
    connect(&m_process,&QProcess::readyReadStandardError,this,[this]{m_process.readAllStandardError();});
}
VideoWallpaperPreview::~VideoWallpaperPreview()
{
    m_process.disconnect(this);
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(500);}
}
void VideoWallpaperPreview::setFile(const QString &path)
{
    const QFileInfo info(path.trimmed());
    const QString identity=path.trimmed().isEmpty()?QString():info.absoluteFilePath()+":"+QString::number(info.size())+":"+QString::number(info.lastModified().toMSecsSinceEpoch());
    if(identity==m_identity)return;
    m_identity=identity;m_path=path.trimmed();m_debounce.stop();m_deadline.stop();m_frame=0;
    m_animation.stop();m_frames.clear();m_positions.clear();m_displayFrame=0;m_confirmed=false;
    m_animatedImage->setText(m_path.isEmpty()?"未选择视频":"正在生成预览…");
    m_cacheDirectory=VideoWallpaperCache::directory(m_path);setProperty("cacheHit",false);
    m_cancelled=true;
    if(m_process.state()!=QProcess::NotRunning)m_process.kill();
    setProperty("frameCount",0);setProperty("previewState",m_path.isEmpty()?"empty":"loading");
    for(int i=0;i<3;++i)m_images[i]->setText(m_path.isEmpty()?"未选择视频":"正在提取…");
    m_hint->clear();m_hint->hide();
    if(!m_path.isEmpty() && !loadCache())m_debounce.start();
}
void VideoWallpaperPreview::begin()
{
    if(m_process.state()!=QProcess::NotRunning){m_debounce.start();return;}
    if(m_path.isEmpty())return;
    if(loadCache())return; // Another page may have confirmed during the debounce.
    if(!QFileInfo(m_path).isFile()){fail("视频文件不存在，无法预览。");return;}
    m_cancelled=false;m_timedOut=false;m_probing=true;m_frame=0;m_duration=0;
    m_process.readAllStandardOutput();
    m_process.start("ffprobe",{"-v","error","-show_entries","format=duration","-of","json",m_path});
    m_deadline.start();
}
void VideoWallpaperPreview::extract()
{
    m_probing=false;m_timedOut=false;
    const double position=m_positions[m_frame];
    m_process.start("ffmpeg",{"-hide_banner","-loglevel","error","-nostdin","-threads","1",
        // Seek to the preceding keyframe; passthrough timestamps avoid decoding
        // and discarding a whole 4K GOP just to create a small still preview.
        "-noaccurate_seek","-ss",QString::number(position,'f',3),"-i",m_path,"-an","-sn","-dn","-frames:v","1","-vsync","0",
        "-vf","scale=640:360:force_original_aspect_ratio=decrease","-filter_threads","1",
        "-threads","1","-f","image2pipe","-vcodec","png","pipe:1"});
    m_deadline.start();
}
void VideoWallpaperPreview::completed(int code,QProcess::ExitStatus status)
{
    m_deadline.stop();const auto output=m_process.readAllStandardOutput();
    if(m_cancelled){if(!m_path.isEmpty())m_debounce.start();return;}
    if(m_timedOut || code!=0 || status!=QProcess::NormalExit){fail("无法生成预览，仍可通过“应用”检查此视频。");return;}
    if(m_probing){
        m_duration=QJsonDocument::fromJson(output).object().value("format").toObject().value("duration").toString().toDouble();
        if(!std::isfinite(m_duration) || m_duration<=0 || m_duration>60){fail("预览支持时长不超过 60 秒的本地视频。");return;}
        // One random target within each third; saved positions remain stable
        // across settings reopen/restarts for the same confirmed file identity.
        for(int i=0;i<3;++i)m_positions.append(m_duration*(i+0.15+QRandomGenerator::global()->generateDouble()*0.7)/3.0);
        extract();return;
    }
    const QImage image=QImage::fromData(output,"PNG");
    if(image.isNull()){fail("无法读取视频预览画面。");return;}
    m_images[m_frame]->setPixmap(QPixmap::fromImage(image).scaled(m_images[m_frame]->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    m_frames.append(image);
    ++m_frame;setProperty("frameCount",m_frame);
    if(m_frame<3){extract();return;}
    setProperty("previewState","ready");
    m_hint->clear();m_hint->hide();
    saveCache();showFrame();if(isVisible())m_animation.start();
}
void VideoWallpaperPreview::fail(const QString &message)
{
    setProperty("previewState","error");m_hint->setText(message);m_hint->show();
    for(int i=m_frame;i<3;++i)m_images[i]->setText("无预览");
}

void VideoWallpaperPreview::confirmFile(const QString &path)
{
    setFile(path);m_confirmed=true;saveCache();
}
void VideoWallpaperPreview::showFrame()
{
    if(m_frames.size()!=3)return;
    const qreal dpr=m_animatedImage->devicePixelRatioF();
    auto pixmap=QPixmap::fromImage(m_frames[m_displayFrame]).scaled(
        m_animatedImage->size()*dpr,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(dpr);m_animatedImage->setPixmap(pixmap);
    setProperty("animationFrame",m_displayFrame);
}
void VideoWallpaperPreview::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if(m_frames.size()==3){showFrame();m_animation.start();}
}
void VideoWallpaperPreview::hideEvent(QHideEvent *event)
{
    m_animation.stop();QWidget::hideEvent(event);
}
bool VideoWallpaperPreview::loadCache()
{
    if(m_cacheDirectory.isEmpty())return false;
    QFile file(m_cacheDirectory+"/preview.json");
    if(!file.open(QIODevice::ReadOnly) || file.size()>4096)return false;
    const auto object=QJsonDocument::fromJson(file.readAll()).object();
    const auto times=object.value("positions").toArray();
    const int version=object.value("version").toInt();
    if((version!=1 && version!=2) || times.size()!=3)return false;
    QVector<QImage> frames;QVector<double> positions;
    for(int i=0;i<3;++i){
        const QImage frame(m_cacheDirectory+QString(version==1?"/frame%1.png":"/frame640-%1.png").arg(i));
        if(frame.isNull() || frame.width()>640 || frame.height()>360 || !times[i].isDouble()
            || !std::isfinite(times[i].toDouble()) || times[i].toDouble()<0 || times[i].toDouble()>60)return false;
        frames.append(frame);positions.append(times[i].toDouble());
    }
    m_frames=frames;m_positions=positions;m_frame=3;
    for(int i=0;i<3;++i)m_images[i]->setPixmap(QPixmap::fromImage(frames[i]));
    setProperty("frameCount",3);setProperty("previewState","ready");setProperty("cacheHit",true);
    m_hint->clear();m_hint->hide();showFrame();if(isVisible())m_animation.start();
    return true;
}
void VideoWallpaperPreview::saveCache()
{
    if(!m_confirmed || m_frames.size()!=3 || m_cacheDirectory.isEmpty() || property("cacheHit").toBool())return;
    if(!QDir().mkpath(m_cacheDirectory))return;
    QLockFile lock(m_cacheDirectory+"/preview.lock");
    if(!lock.tryLock(0))return;
    if(loadCache())return; // A valid committed preview is immutable.
    for(int i=0;i<3;++i)
        if(!VideoWallpaperCache::saveImage(m_cacheDirectory,QString("frame640-%1.png").arg(i),m_frames[i]))return;
    QJsonArray positions;for(double value:m_positions)positions.append(value);
    QSaveFile file(m_cacheDirectory+"/preview.json");
    if(!file.open(QIODevice::WriteOnly))return;
    file.write(QJsonDocument(QJsonObject{{"version",2},{"positions",positions}}).toJson());
    if(file.commit()){
        setProperty("cacheHit",true);m_hint->clear();m_hint->hide();
        VideoWallpaperCache::prune(m_path,QSettings().value("wallpaper/previousVideoPath").toString());
    }
}
