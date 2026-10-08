#include "WallpaperBackdrop.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QPainter>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QImageReader>
#include <QDebug>
#include <QSettings>
#include <QCryptographicHash>

static QString fencesVideoPoster(const QString &path) {
    const QFileInfo video(path);
    if(!video.isFile())return {};
    // Match Fences' VideoWallpaperCache v2 identity. Use its explicit data
    // directory: AppLocalDataLocation here belongs to the OEM panel process.
    const QByteArray identity=video.canonicalFilePath().toUtf8()+'\n'
        +QByteArray::number(video.size())+'\n'
        +QByteArray::number(video.lastModified().toMSecsSinceEpoch());
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        +"/kylin/ukui-fences/video-previews/v2-"
        +QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex())+"/poster.png";
}

static QString setting(const QString &schema,const QString &key) {
    QProcess process;process.start("gsettings",{ "get",schema,key });
    if(!process.waitForFinished(1200)){process.kill();process.waitForFinished(500);return {};}
    if(process.exitCode()!=0)return {};
    QString value=QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if(value.startsWith('\'') && value.endsWith('\''))value=value.mid(1,value.size()-2);
    return value;
}
bool WallpaperBackdrop::reload() {
    const QString config=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString path=qEnvironmentVariable("UKUI_LIQUID_WALLPAPER");
    m_videoPath.clear();m_videoPoster.clear();
    int mode=0;
    if(path.isEmpty()) {
        // Fences owns the visible desktop on this installation, including its
        // crop convention. Read only its wallpaper fields, not unrelated data.
        QFile file(config+"/kyfences/layout.json");
        bool fences=file.open(QIODevice::ReadOnly);
        if(fences) {
            const auto root=QJsonDocument::fromJson(file.readAll()).object();
            mode=root.value("wallpaperMode").toInt(5);
            if(mode!=5)path=root.value("wallpaperPath").toString();
            if(mode==5)mode=0; // Fences renders system wallpaper as Fill.
        }
        QSettings videoSettings(config+"/kylin/ukui-fences.ini",QSettings::IniFormat);
        m_videoPath=videoSettings.value("wallpaper/videoPath").toString();
        m_videoPoster=fencesVideoPoster(m_videoPath);
        if(!m_videoPoster.isEmpty() && QFileInfo(m_videoPoster).isFile()) {
            path=m_videoPoster;mode=0; // Video and its glass use the first frame, filled.
        }
        if(path.isEmpty()) {
            path=setting("org.mate.background","picture-filename");
            if(path.isEmpty())path=setting("org.gnome.desktop.background","picture-uri");
            if(!fences) {
                const QString options=setting("org.mate.background","picture-options");
                mode=options=="scaled"?1:options=="stretched"?2:options=="wallpaper"?3:options=="centered"?4:0;
            }
        }
    }
    if(path.startsWith("file:"))path=QUrl(path).toLocalFile();
    QFileInfo info(path);
    const qint64 modified=info.exists()?info.lastModified().toMSecsSinceEpoch():0;
    const qint64 size=info.isFile()?info.size():0;
    if(path==m_path && modified==m_modified && size==m_size && mode==m_mode && !m_source.isNull())return false;
    m_path=path;m_modified=modified;m_size=size;m_mode=mode;
    // Downloaded wallpaper may have a .png name but contain JPEG data. Match
    // Fences' content-based decoding so the visible desktop and optics agree.
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    m_source=reader.read();
    if(m_source.isNull())
        qWarning()<<"[LiquidPanelWallpaper] Cannot decode wallpaper"<<path<<reader.errorString();
    return true;
}
QImage WallpaperBackdrop::sample(const QRect &area,const QRect &screen,qreal dpr) const {
    if(m_source.isNull() || area.isEmpty() || screen.isEmpty())return {};
    QImage crop(QSize(qRound(area.width()*dpr),qRound(area.height()*dpr)),QImage::Format_RGB32);
    crop.setDevicePixelRatio(dpr);crop.fill(Qt::black);
    QPainter p(&crop);p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF offset=screen.topLeft()-area.topLeft();
    QSizeF size=m_source.size();
    if(m_mode==2)size=screen.size();
    else if(m_mode==0 || m_mode==1)size.scale(screen.size(),m_mode==0?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);
    if(m_mode==3) {
        p.translate(offset);p.setBrushOrigin(0,0);p.fillRect(QRectF(QPointF(),screen.size()),QBrush(QPixmap::fromImage(m_source)));
    } else {
        const QPointF origin=offset+QPointF((screen.width()-size.width())/2,(screen.height()-size.height())/2);
        p.drawImage(QRectF(origin,size),m_source);
    }
    return crop;
}
