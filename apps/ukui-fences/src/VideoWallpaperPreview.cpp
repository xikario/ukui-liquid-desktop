#include "VideoWallpaperPreview.h"
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
#include <cmath>

VideoWallpaperPreview::VideoWallpaperPreview(QWidget *parent) : QWidget(parent)
{
    setObjectName("videoWallpaperFrames");
    auto *layout = new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto *strip = new QHBoxLayout;strip->setSpacing(8);layout->addLayout(strip);
    for (int i=0;i<3;++i) {
        auto *column = new QVBoxLayout;
        auto *image = new QLabel("等待预览",this);
        image->setObjectName(QString("videoPreviewFrame%1").arg(i));
        image->setFixedSize(140,90);image->setAlignment(Qt::AlignCenter);
        image->setStyleSheet("background:#101827;border:1px solid #39445a;border-radius:6px;color:#cbd5e1;");
        auto *time = new QLabel(this);time->setAlignment(Qt::AlignCenter);
        column->addWidget(image);column->addWidget(time);strip->addLayout(column);
        m_images.append(image);m_times.append(time);
    }
    strip->addStretch();
    m_hint = new QLabel("选择视频后显示三帧画面。",this);m_hint->setWordWrap(true);layout->addWidget(m_hint);
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
    m_cancelled=true;
    if(m_process.state()!=QProcess::NotRunning)m_process.kill();
    setProperty("frameCount",0);setProperty("previewState",m_path.isEmpty()?"empty":"loading");
    for(int i=0;i<3;++i){m_images[i]->setText(m_path.isEmpty()?"未选择视频":"正在提取…");m_times[i]->clear();}
    m_hint->setText(m_path.isEmpty()?"选择视频后显示三帧画面。":"正在生成画面预览，壁纸不会立即生效。");
    if(!m_path.isEmpty())m_debounce.start();
}
void VideoWallpaperPreview::begin()
{
    if(m_process.state()!=QProcess::NotRunning){m_debounce.start();return;}
    if(m_path.isEmpty())return;
    if(!QFileInfo(m_path).isFile()){fail("视频文件不存在，无法预览。");return;}
    m_cancelled=false;m_timedOut=false;m_probing=true;m_frame=0;m_duration=0;
    m_process.readAllStandardOutput();
    m_process.start("ffprobe",{"-v","error","-show_entries","format=duration","-of","json",m_path});
    m_deadline.start();
}
void VideoWallpaperPreview::extract()
{
    m_probing=false;m_timedOut=false;
    const double position=m_duration*(0.1+0.4*m_frame);
    m_times[m_frame]->setText(QString::number(position,'f',1)+" 秒附近");
    m_process.start("ffmpeg",{"-hide_banner","-loglevel","error","-nostdin","-threads","1",
        // Seek to the preceding keyframe; passthrough timestamps avoid decoding
        // and discarding a whole 4K GOP just to create a small still preview.
        "-noaccurate_seek","-ss",QString::number(position,'f',3),"-i",m_path,"-an","-sn","-dn","-frames:v","1","-vsync","0",
        "-vf","scale=320:180:force_original_aspect_ratio=decrease","-filter_threads","1",
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
        extract();return;
    }
    const QImage image=QImage::fromData(output,"PNG");
    if(image.isNull()){fail("无法读取视频预览画面。");return;}
    m_images[m_frame]->setPixmap(QPixmap::fromImage(image).scaled(m_images[m_frame]->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    ++m_frame;setProperty("frameCount",m_frame);
    if(m_frame<3){extract();return;}
    setProperty("previewState","ready");
    m_hint->setText("前 / 中 / 后段附近的关键帧预览；点击应用后才切换壁纸。");
}
void VideoWallpaperPreview::fail(const QString &message)
{
    setProperty("previewState","error");m_hint->setText(message);
    for(int i=m_frame;i<3;++i)m_images[i]->setText("无预览");
}
