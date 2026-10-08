#include "WallpaperBackdrop.h"
#include "../../ukui-fences/src/VideoWallpaperCache.h"
#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdlib>

static void check(bool ok,const char *message) {
    if(!ok){qCritical()<<message;std::exit(1);}qInfo()<<"PASS:"<<message;
}
int main(int argc,char **argv) {
    QTemporaryDir root;
    qputenv("XDG_CONFIG_HOME",(root.path()+"/config").toUtf8());
    qputenv("XDG_DATA_HOME",(root.path()+"/data").toUtf8());
    qunsetenv("UKUI_LIQUID_WALLPAPER");
    QApplication app(argc,argv);app.setOrganizationName("kylin");app.setApplicationName("ukui-fences");
    const QString cfg=root.path()+"/config";
    QDir().mkpath(cfg+"/kyfences");
    const auto image=[&](const QString &path,QColor color) {
        QImage pixels(160,90,QImage::Format_RGB32);pixels.fill(color);
        check(pixels.save(path),"wallpaper fixture saved");return pixels;
    };
    const QString still=root.path()+"/static.png";image(still,QColor(220,50,40));
    QFile layout(cfg+"/kyfences/layout.json");check(layout.open(QIODevice::WriteOnly),"layout opened");
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",still},{"wallpaperMode",1}}).toJson());layout.close();
    QSettings settings(cfg+"/kylin/ukui-fences.ini",QSettings::IniFormat);
    auto video=[&](const QString &name) {
        const QString path=root.path()+name;QFile file(path);
        check(file.open(QIODevice::WriteOnly),"video identity fixture opened");file.write("identity fixture");return path;
    };
    auto center=[&](const WallpaperBackdrop &source) {
        return source.sample(QRect(300,550,200,50),QRect(0,0,800,600),1.5).pixelColor(150,37);
    };
    WallpaperBackdrop source;check(source.reload() && source.path()==still,"static source initially selected");
    const QString first=video("/甲.mp4");settings.setValue("wallpaper/videoPath",first);settings.sync();
    check(!source.reload() && source.path()==still,"missing video poster preserves the static fallback");
    const QString poster=VideoWallpaperCache::directory(first)+"/poster.png";
    check(source.sourceWatchPaths().contains(first) && source.sourceWatchPaths().contains(poster),
          "missing poster and source video remain observable for later cache creation");
    QImage blue(160,90,QImage::Format_RGB32);blue.fill(QColor(30,70,220));
    check(VideoWallpaperCache::saveImage(VideoWallpaperCache::directory(first),"poster.png",blue),"real Fences cache producer writes the poster");
    check(source.reload() && source.path()==poster && center(source).blue()>200,
          "panel reads the exact Fences poster and uses Fill despite saved static Fit mode");
    check(!source.reload(),"unchanged poster does not rebuild the material");
    settings.setValue("systemMonitor/unrelatedFixture",true);settings.sync();
    check(!source.reload(),"unrelated Fences settings do not rebuild the material");
    const QString second=video("/乙.mp4");QImage green(160,90,QImage::Format_RGB32);green.fill(QColor(20,210,50));
    check(VideoWallpaperCache::saveImage(VideoWallpaperCache::directory(second),"poster.png",green),"second poster cached");
    settings.setValue("wallpaper/videoPath",second);settings.sync();
    check(source.reload() && center(source).green()>190,"video switch updates pixels without changing static layout");
    QFile changed(second);changed.open(QIODevice::Append);changed.write("changed");changed.close();
    check(source.reload() && source.path()==still,"changed video identity never reuses stale first-frame pixels");
    check(VideoWallpaperCache::saveImage(VideoWallpaperCache::directory(second),"poster.png",blue),"changed video's first frame regenerated");
    check(source.reload() && center(source).blue()>200,"new cache becomes available after identity change");
    settings.remove("wallpaper/videoPath");settings.sync();
    check(source.reload() && source.path()==still,"disabling video restores the saved static source");
    qputenv("UKUI_LIQUID_WALLPAPER",still.toUtf8());settings.setValue("wallpaper/videoPath",second);settings.sync();
    source.reload();check(source.path()==still,"explicit wallpaper override keeps priority");
    qunsetenv("UKUI_LIQUID_WALLPAPER");check(source.reload() && center(source).blue()>200,"automatic source resumes after clearing override");
    return 0;
}
