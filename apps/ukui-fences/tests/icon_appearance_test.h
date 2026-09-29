#pragma once
#include "DesktopIcon.h"
#include "FileClipboard.h"
#include <QComboBox>
#include <QDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSlider>

static int runIconAppearanceTest(const QString &root)
{
    QSettings s; s.setValue("smartSpace/autoStart",false);s.setValue("systemMonitor/autoStart",false);s.sync();
    const QString path=root+"/sample.txt";
    {QFile f(path); f.open(QIODevice::WriteOnly);f.write("sample");}
    DesktopItem item=DesktopItem::fromPath(path);
    QPixmap source(64,64);source.fill(QColor("#20c070"));item.icon=QIcon(source);
    DesktopIcon icon(item);icon.show();settle();
    check(icon.visualStyle()==IconVisualStyle::Native,"unconfigured icons retain native rendering");
    const qreal d=icon.devicePixelRatioF();
    QImage sheet(QSize(qRound(720*d),qRound(180*d)),QImage::Format_RGB32);sheet.setDevicePixelRatio(d);
    sheet.fill(QColor("#334a62"));QPainter gallery(&sheet);
    int column=0;
    for(qreal scale:{.75,1.,1.75}) {
        icon.setVisualScale(scale);
        icon.setIconAppearance(IconVisualStyle::LiquidPlate,IconSurface::Desktop,QColor("#7199df"),.62);
        const auto normal=icon.dragPixmap();const QImage im=normal.toImage();
        check(qAbs(normal.devicePixelRatioF()-d)<.01,"drag preview retains screen DPR");
        check(icon.rect().contains(icon.plateRect().adjusted(-3,-3,3,3).toAlignedRect()),"plate and shadow fit cell at every scale");
        check(im.pixelColor(im.width()/2,im.height()/2).green()>160,"source icon remains vivid and clear");
        check(im.pixelColor(0,0).alpha()==0,"plate exterior stays transparent");
        int partial=0;for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)
            if(im.pixelColor(x,y).alpha()>0 && im.pixelColor(x,y).alpha()<255) ++partial;
        check(partial>30,"glass and rounded edges have fractional alpha");
        check(icon.dragPixmap().cacheKey()==normal.cacheKey(),"repeated rendering reuses cached preview");
        icon.render(&gallery,QPoint(column*140,15),QRegion(),QWidget::DrawChildren);++column;
        icon.setCut(true);const auto cut=icon.dragPixmap().toImage();
        const int px=qRound(5*d),py=im.height()/2;
        check(cut.pixelColor(px,py).alpha()<im.pixelColor(px,py).alpha(),"cut fades plate as well as source icon");
        check(cut.pixelColor(cut.width()/2,cut.height()/2).alpha()<120,"cut source opacity retained");
        icon.setCut(false);
    }
    icon.setVisualScale(1.);icon.setSelected(true);icon.render(&gallery,QPoint(440,15),QRegion(),QWidget::DrawChildren);
    icon.setSelected(false);QEvent hover(QEvent::Enter);QApplication::sendEvent(&icon,&hover);
    icon.render(&gallery,QPoint(530,15),QRegion(),QWidget::DrawChildren);icon.setCut(true);icon.render(&gallery,QPoint(620,15),QRegion(),QWidget::DrawChildren);
    gallery.end();QDir().mkpath("artifacts");sheet.save(QString("artifacts/liquid-icons-%1.png").arg(d));
    icon.hide();
    {
        DesktopCanvas canvas;canvas.showAndActivate();settle(150);
        canvas.configureIconAppearance(&icon,IconSurface::Desktop);
        check(icon.visualStyle()==IconVisualStyle::LiquidPlate,"desktop defaults to liquid");
        canvas.configureIconAppearance(&icon,IconSurface::Fence);
        check(icon.visualStyle()==IconVisualStyle::Native,"fence defaults to native");
        QTimer::singleShot(50,&canvas,[&] {
            auto *dialog=canvas.findChild<QDialog *>("iconAppearanceDialog");
            check(dialog!=nullptr,"icon settings reachable");if(!dialog)return;
            dialog->findChild<QCheckBox *>("fenceIconPlates")->setChecked(true);
            dialog->findChild<QSlider *>("iconGlassStrength")->setValue(73);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        canvas.showIconAppearanceDialog();
        canvas.configureIconAppearance(&icon,IconSurface::Fence);
        check(icon.visualStyle()==IconVisualStyle::LiquidPlate,"fence opt-in takes effect immediately");
        check(IconAppearance::load().strength==73 && IconAppearance::load().fencePlates,"appearance persists independently of layout");
    }
    {DesktopCanvas next;next.configureIconAppearance(&icon,IconSurface::Fence);
     check(icon.visualStyle()==IconVisualStyle::LiquidPlate,"appearance survives canvas recreation");}
    {QFile f(root+"/.bashrc");f.open(QIODevice::WriteOnly);f.write("x");}
    check(FileClipboard::uniqueTargetPath(root,".bashrc").endsWith("/.bashrc (1)"),"hidden file collisions preserve basename without duplicate suffix");
    return failures?1:0;
}
