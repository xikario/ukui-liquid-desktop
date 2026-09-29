#include "LiquidOpticsRenderer.h"
#include "WallpaperBackdrop.h"
#include <QApplication>
#include <QPainter>
#include <QDir>
#include <QDebug>
#include <cstdlib>
static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}qInfo()<<"PASS:"<<message;}
int main(int argc,char **argv){
 QApplication app(argc,argv);LiquidOpticsRenderer optics;
 const bool gpu=app.arguments().contains("--require-gpu");
 const QString output=argc>1 && !QString::fromLocal8Bit(argv[1]).startsWith("--")?QString::fromLocal8Bit(argv[1]):"artifacts";
 QDir().mkpath(output);
 for(qreal d:{1.,1.5,2.}){
  const QSize logical(800,64);
  QImage backdrop(QSize(qRound(logical.width()*d),qRound(logical.height()*d)),QImage::Format_RGB32);backdrop.setDevicePixelRatio(d);
  {QPainter p(&backdrop);QLinearGradient g(0,0,800,0);g.setColorAt(0,QColor("#e98d40"));g.setColorAt(.5,QColor("#536ab2"));g.setColorAt(1,QColor("#071222"));p.fillRect(QRect(QPoint(),logical),g);p.setPen(QPen(QColor("#c5e8f2"),3));for(int x=0;x<850;x+=37)p.drawLine(x,0,x-35,64);}
  optics.setWallpaper(backdrop);optics.setOptics(0,.58,.55);
  const QImage flat=optics.renderPanel(QRect(QPoint(),logical),18);
  if(gpu)check(optics.usedGpu(),"GPU Snell backend actually used");
  optics.setOptics(3.5,.58,.55);const QImage refracted=optics.renderPanel(QRect(QPoint(),logical),18);
  check(refracted.size()==backdrop.size() && refracted.devicePixelRatio()==d,"optical surface retains DPR");
  check(refracted.pixelColor(0,0).alpha()<5,"optical corners transparent");
  if(gpu)check(refracted!=flat,"refraction setting changes optical content");
  optics.setOptics(3.5,.58,.55,.48);
  const QImage restrained=optics.renderPanel(QRect(QPoint(),logical),18);
  if(gpu)check(restrained!=refracted,"chroma setting changes optical content");
  optics.setMaterial(0,1);
  check(optics.renderPanel(QRect(QPoint(),logical),18)==restrained,"default new material parameters preserve original rendering");
  const int prepared=optics.preparationCount();
  optics.setMaterial(1,1);const QImage crisp=optics.renderPanel(QRect(QPoint(),logical),18);
  check(crisp!=restrained,"clarity changes wallpaper detail on this backend");
  optics.setMaterial(1,1.9);const QImage strong=optics.renderPanel(QRect(QPoint(),logical),18);
  check(strong!=crisp,"liquid strength changes material on this backend");
  check(optics.preparationCount()==prepared,"material controls reuse prepared diffusion");
  check(strong.pixelColor(0,0).alpha()<5 && strong.size()==restrained.size(),"material controls preserve shape and physical size");
  optics.setMaterial(0,1);
  check(optics.renderPanel(QRect(QPoint(),logical),18)==restrained,"returning settings restores exact baseline");
  const int count=optics.preparationCount();optics.renderPanel(QRect(QPoint(),logical),18);
  check(optics.preparationCount()==count,"background diffusion cached");
  refracted.save(output+QString("/optics-%1.png").arg(d));
 }
 WallpaperBackdrop wallpaper;check(wallpaper.reload(),"wallpaper provider initializes");
 auto crop=wallpaper.sample(QRect(0,0,200,50),QRect(0,0,800,600),1.5);
 if(!crop.isNull())check(crop.size()==QSize(300,75),"wallpaper crop uses requested physical size");
 return 0;
}
