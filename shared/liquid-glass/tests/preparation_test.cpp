#include "LiquidMaterialPreparation.h"
#include "LiquidOpticsRenderer.h"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    BackgroundTask::ApplicationScope scope;
    QImage first(1600,900,QImage::Format_RGB32);first.fill(Qt::red);
    QImage latest(960,540,QImage::Format_RGB32);latest.fill(Qt::blue);latest.setDevicePixelRatio(1.5);
    int stale=0, delivered=0;
    QEventLoop loop;QTimer::singleShot(5000,&loop,&QEventLoop::quit);
    LiquidMaterial::Preparation preparation(&app);
    LiquidOpticsRenderer optics, reference;
    preparation.request(first,[&](const auto &){++stale;});
    preparation.invalidate();
    preparation.request(first,[&](const auto &){++stale;});
    preparation.request(latest,[&](const LiquidMaterial::Prepared &material){
        ++delivered;
        if(material.source.cacheKey()!=latest.cacheKey()){++stale;loop.quit();return;}
        optics.setPreparedWallpaper(material);
        loop.quit();
    });
    if(delivered){qCritical()<<"preparation ran inline";return 1;}
    auto *destroyed=new LiquidMaterial::Preparation(&app);
    destroyed->request(first,[&](const auto &){++stale;});delete destroyed;
    loop.exec();
    if(stale || delivered!=1){qCritical()<<"stale/deleted request delivered"<<stale<<delivered;return 2;}
    reference.setWallpaper(latest);
    const QRect panel(0,0,400,250);
    const int prepared=optics.preparationCount();
    if(optics.renderPanel(panel,16)!=reference.renderPanel(panel,16) || optics.preparationCount()!=prepared){
        qCritical()<<"prepared rendering changed pixels or repeated diffusion";return 3;
    }
    qInfo()<<"PASS: asynchronous coalescing, recipient destruction, pixel parity and no GUI-side re-preparation";
    return 0;
}
