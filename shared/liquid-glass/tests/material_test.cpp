#include "LiquidMaterial.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
static QImage reference(const QImage &source,QSize logical,int radius,int divisor) {
    QImage a=source.scaled(qMax(1,logical.width()/divisor),qMax(1,logical.height()/divisor),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32),b(a.size(),a.format());
    for(int pass=0;pass<6;++pass) {
        for(int y=0;y<a.height();++y) for(int x=0;x<a.width();++x) {
            int r=0,g=0,bl=0;
            for(int k=-radius;k<=radius;++k) {
                QRgb c=a.pixel(qBound(0,x+(pass%2?0:k),a.width()-1),qBound(0,y+(pass%2?k:0),a.height()-1));
                r+=qRed(c);g+=qGreen(c);bl+=qBlue(c);
            }
            b.setPixel(x,y,qRgb(r/(radius*2+1),g/(radius*2+1),bl/(radius*2+1)));
        }
        a.swap(b);
    }
    return a.scaled(source.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
static QImage legacyDiffuse(const QImage &source, QSize logical, int radius=2, int divisor=3)
{
    QImage a = source.scaled(qMax(1, logical.width()/divisor), qMax(1, logical.height()/divisor),
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(), a.format());
    for (int pass = 0; pass < 6; ++pass) {
        const bool horizontal = pass % 2 == 0;
        for (int y = 0; y < a.height(); ++y) {
            QRgb *out = reinterpret_cast<QRgb *>(b.scanLine(y));
            for (int x = 0; x < a.width(); ++x) {
                int r=0, g=0, blue=0;
                for (int k=-radius; k<=radius; ++k) {
                    const int sx = horizontal ? qBound(0,x+k,a.width()-1) : x;
                    const int sy = horizontal ? y : qBound(0,y+k,a.height()-1);
                    const QRgb c = reinterpret_cast<const QRgb *>(a.constScanLine(sy))[sx];
                    r += qRed(c); g += qGreen(c); blue += qBlue(c);
                }
                out[x] = qRgb(r/(radius*2+1),g/(radius*2+1),blue/(radius*2+1));
            }
        }
        a.swap(b);
    }
    return a.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    for(QSize size:{QSize(1,1),QSize(2,7),QSize(79,51),QSize(640,400)}) {
        QImage image(size,QImage::Format_RGB32);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)image.setPixel(x,y,qRgb((x*73+y*13)%256,(x*17+y*71)%256,(x*41+y*59)%256));
        for(qreal dpr:{1.,1.5,2.})for(int radius:{1,2}) {
            image.setDevicePixelRatio(dpr);QSize logical(qMax(1,qRound(size.width()/dpr)),qMax(1,qRound(size.height()/dpr)));
            if(LiquidMaterial::diffuse(image,logical,radius,radius==1?2:3)!=reference(image,logical,radius,radius==1?2:3)) {qCritical()<<"material pixel mismatch"<<size<<dpr<<radius;return 1;}
        }
    }
    QImage large(3840,2160,QImage::Format_RGB32);large.fill(QColor(130,180,220));
    QElapsedTimer timer;timer.start();const auto material=LiquidMaterial::prepare(large);
    qInfo()<<"PASS material parity; 4K CPU preparation ms"<<timer.elapsed();
    if(qEnvironmentVariableIsSet("UKUI_LIQUID_MATERIAL_BENCHMARK")) {
        QList<qint64> original,shared;
        for(int i=0;i<9;++i) {
            timer.restart();const QImage a=legacyDiffuse(large,large.size(),2,3),b=legacyDiffuse(large,large.size(),1,2);
            original<<timer.elapsed();
            timer.restart();const auto prepared=LiquidMaterial::prepare(large);shared<<timer.elapsed();
            if(prepared.body!=a || prepared.clear!=b)return 2;
        }
        std::sort(original.begin(),original.end());std::sort(shared.begin(),shared.end());
        qInfo()<<"4K body+clear, 9 samples, baseline p50/p95 ms"<<original[4]<<original[8]
               <<"shared p50/p95 ms"<<shared[4]<<shared[8];
    }
    return material.body.isNull()?1:0;
}
