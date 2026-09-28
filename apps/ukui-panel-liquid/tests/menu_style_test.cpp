#include <QApplication>
#include <QMenu>
#include <QActionGroup>
#include <QTimer>
#include <QPainter>
#include <QStyleOption>
#include <QDebug>
#include <QDir>
#include <cstdlib>
static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    QMenu menu;
    auto *checked=menu.addAction("锁定任务栏");checked->setCheckable(true);checked->setChecked(true);
    auto *off=menu.addAction("未勾选");off->setCheckable(true);
    auto *disabled=menu.addAction("已勾选（不可用）");disabled->setCheckable(true);disabled->setChecked(true);disabled->setEnabled(false);
    auto *group=new QActionGroup(&menu);auto *radio=menu.addAction("单选项");group->addAction(radio);radio->setCheckable(true);radio->setChecked(true);
    menu.addSeparator();menu.addMenu("外观与特效")->addAction("子菜单项");
    QTimer::singleShot(80,&app,[&]{
        QEvent polish(QEvent::Polish);QApplication::sendEvent(&menu,&polish);
        menu.popup(QPoint(60,60));
        QTimer::singleShot(300,&app,[&]{
            QDir().mkpath("artifacts");const QImage normal=menu.grab().toImage();normal.save("artifacts/menu-style.png");
            check(menu.property("liquidPopupSkin").toBool(),"test exercises adapted menu");
            for(qreal d:{1.,1.5,2.}) {
                QImage symbols(QSize(qRound(160*d),qRound(32*d)),QImage::Format_ARGB32_Premultiplied);symbols.setDevicePixelRatio(d);symbols.fill(Qt::transparent);
                QPainter p(&symbols);int x=0;
                for(auto e:{QStyle::PE_IndicatorMenuCheckMark,QStyle::PE_IndicatorArrowLeft,QStyle::PE_IndicatorArrowRight,QStyle::PE_IndicatorArrowUp,QStyle::PE_IndicatorArrowDown}) {
                    QStyleOption opt;opt.initFrom(&menu);opt.rect=QRect(x,4,24,24);opt.state=QStyle::State_Enabled|QStyle::State_On;
                    opt.palette.setColor(QPalette::Text,Qt::white);opt.palette.setColor(QPalette::WindowText,Qt::white);
                    app.style()->drawPrimitive(e,&opt,&p,&menu);x+=32;
                }
                p.end();symbols.save(QString("artifacts/menu-symbols-%1.png").arg(d));
                for(int glyph=0;glyph<5;++glyph) {
                    int ink=0,partial=0;
                    for(int y=0;y<symbols.height();++y)for(int x=qRound(glyph*32*d);x<qRound((glyph*32+24)*d);++x) {
                        const int a=symbols.pixelColor(x,y).alpha();if(a>0)++ink;if(a>0 && a<255)++partial;
                    }
                    check(ink>10 && partial>5,"each vector menu symbol has antialiased coverage at each DPR");
                }
                for(int state=0;state<3;++state) {
                    QImage mark(QSize(qRound(20*d),qRound(20*d)),QImage::Format_ARGB32_Premultiplied);mark.setDevicePixelRatio(d);mark.fill(Qt::transparent);
                    QStyleOption opt;opt.initFrom(&menu);opt.rect=QRect(0,0,20,20);
                    opt.state=state==2?QStyle::State_None:QStyle::State_Enabled;
                    if(state==1)opt.state|=QStyle::State_Selected;
                    opt.palette.setColor(QPalette::Active,QPalette::Text,QColor(240,0,0));
                    opt.palette.setColor(QPalette::Active,QPalette::HighlightedText,QColor(0,240,0));
                    opt.palette.setColor(QPalette::Disabled,QPalette::Text,QColor(0,0,240));
                    {QPainter paint(&mark);app.style()->drawPrimitive(QStyle::PE_IndicatorMenuCheckMark,&opt,&paint,&menu);check(!paint.testRenderHint(QPainter::Antialiasing),"glyph restores caller painter state");}
                    int count=0;
                    for(int y=0;y<mark.height();++y)for(int x=0;x<mark.width();++x) {
                        const QColor c=mark.pixelColor(x,y);if(c.alpha()<100)continue;++count;
                        check((state==0?c.red():state==1?c.green():c.blue())>220,"checkmark follows normal selected and disabled palette");
                    }
                    check(count>5,"state variant remains visible");
                }
            }
            check(checked->isChecked() && !off->isChecked() && disabled->isChecked() && radio->isChecked(),"drawing preserves check and radio states");
            menu.setActiveAction(checked);app.processEvents();const QImage selected=menu.grab().toImage();selected.save("artifacts/menu-style-selected.png");
            const qreal scale=selected.devicePixelRatio();
            const QPoint blank(qRound((menu.width()-30)*scale),qRound(menu.actionGeometry(checked).center().y()*scale));
            check(normal.pixelColor(blank)!=selected.pixelColor(blank),"selected row has visible background beyond text and checkmark");
            menu.setLayoutDirection(Qt::RightToLeft);app.processEvents();menu.grab().save("artifacts/menu-style-rtl.png");
            menu.hide();app.quit();
        });
    });
    return app.exec();
}
