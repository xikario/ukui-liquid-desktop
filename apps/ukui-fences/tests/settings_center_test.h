#pragma once
#include "FencesSettingsWindow.h"
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QSlider>
#include <QTabWidget>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QComboBox>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QStyle>
static int runSettingsCenterTest(const QString &root) {
    qputenv("UKUI_FENCES_SMARTSPACE_ROOTS",root.toUtf8());
    QSettings s;s.setValue("smartSpace/autoStart",false);s.setValue("systemMonitor/autoStart",false);
    for(const QString key:{"clock","activity","music","calendar"})LiquidDesklet::setAutoStart(key,false);
    DesktopCanvas canvas;canvas.showAndActivate();settle(250);
    auto settingsWindow=[]()->FencesSettingsWindow *{for(auto *w:QApplication::topLevelWidgets())if(auto *f=qobject_cast<FencesSettingsWindow *>(w))return f;return nullptr;};
    canvas.showSettingsPage("appearance");settle(400);QPointer<FencesSettingsWindow> window=settingsWindow();
    check(window,"settings window opens");if(!window)return 1;
    check(!window->windowIcon().isNull() && !window->windowIcon().pixmap(32,32).isNull(),"settings has an embedded taskbar icon");
    check(window->windowRole()=="fences-settings","settings has its own window role");
    check(!window->findChild<QPushButton *>("liquidPopupPreviewButton"),"unused liquid preview entry is removed");
    canvas.showUnifiedSettings();check(settingsWindow()==window,"repeated entry reuses the same window");
    check(!canvas.globalEditMode(),"opening settings does not enable layout editing");
    check(!canvas.smartSpaceVisible() && !canvas.systemMonitorVisible(),"opening settings does not instantiate hidden components");
    settle(700); // Initial asynchronous wallpaper delivery may invalidate the first surface.
    const int materialBuilds=window->property("materialBuilds").toInt();
    for(int i=0;i<5;++i)window->grab();settle(250);
    check(materialBuilds>0 && materialBuilds==window->property("materialBuilds").toInt(),"settings repaint reuses cached glass");
    const QPoint beforeMove=window->pos();
    for(int i=1;i<=24;++i){window->move(beforeMove+QPoint(i*2,i));settle(8);}
    window->resize(window->size()+QSize(10,10));settle(180);
    check(materialBuilds==1 && window->property("materialBuilds").toInt()==1,"drag and resize reuse the one opening capture");
    check(window->styleSheet().isEmpty() && window->palette()==QApplication::palette(),"native dialog parent retains default style and palette");
    check(window->windowFlags().testFlag(Qt::WindowStaysOnTopHint),"settings remains above desktop and external windows");
    {
        QFileDialog chooser(window,"选择 Fences 壁纸");
        check(!chooser.testOption(QFileDialog::DontUseNativeDialog),"wallpaper chooser permits the system native dialog");
        check(chooser.styleSheet().isEmpty() && chooser.palette()==QApplication::palette(),"native chooser does not inherit liquid content styling");
    }
    window->showMinimized();settle(60);check(window->isMinimized(),"pinned settings can still minimize explicitly");
    window->showNormal();settle(60);check(window->isVisible() && !window->isMinimized(),"settings restores after explicit minimize");
    window->move(beforeMove);
    QMouseEvent outside(QEvent::MouseButtonPress,QPoint(3,3),canvas.mapToGlobal(QPoint(3,3)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&outside);canvas.refreshAll();settle(1400);
    check(window->isVisible() && !window->isMinimized(),"outside desktop click and refresh do not hide settings");
    check(window->property("materialBuilds").toInt()==1,"wallpaper refresh does not replace frozen settings material");
    if(Display *display=XOpenDisplay(nullptr)){
        Window root,parent,*children=nullptr;unsigned int count=0;int desktopIndex=-1,settingsIndex=-1;
        XQueryTree(display,DefaultRootWindow(display),&root,&parent,&children,&count);
        for(unsigned int i=0;i<count;++i){if(children[i]==canvas.winId())desktopIndex=i;if(children[i]==window->winId())settingsIndex=i;}
        check(settingsIndex>desktopIndex && desktopIndex>=0,"desktop restacking stays below the settings window");
        if(children)XFree(children);XCloseDisplay(display);
    }
    for(const QString id:{"wallpaper","icons","layout","widgets","smart","monitor","clock","activity","music","calendar","sync","help"}){window->openPage(id);settle(30);}
    check(!canvas.smartSpaceVisible() && !canvas.systemMonitorVisible(),"browsing all pages does not start hidden models");
    window->openPage("wallpaper");
    auto *wallpage=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
    auto *combo=wallpage->findChildren<QComboBox *>().last();const int oldIndex=combo->currentIndex();
    check(!combo->style()->styleHint(QStyle::SH_ComboBox_Popup,nullptr,combo),"combo uses a normal scrolling list instead of menu scrollers");
    combo->showPopup();settle(100);
    auto *popup=combo->view()->window();
    check(popup->isVisible() && popup->property("settingsComboPopupSurface").toBool(),"ordinary combo list opens with liquid surface");
    check(!combo->findChild<QToolButton *>("liquidWheelButton"),"settings no longer intercepts controls with a wheel");
    QDir().mkpath("artifacts");popup->grab().save(QString("artifacts/settings-picker-%1.png").arg(window->devicePixelRatioF()));
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(combo->view(),&escape);
    combo->hidePopup();check(combo->currentIndex()==oldIndex && window->isVisible(),"Escape closes list without closing settings");
    QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);QApplication::sendEvent(combo,&down);
    check(combo->currentIndex()==qMin(oldIndex+1,combo->count()-1),"ordinary combo keeps native keyboard selection");
    window->openPage("help");
    auto *desktopToggle=window->findChild<QPushButton *>("desktopVisibilityButton");
    check(desktopToggle && desktopToggle->text()=="切换到系统桌面","desktop switch starts with its current target");
    if(desktopToggle){
        canvas.setClockWidgetVisible(true);settle(100);
        auto *clock=canvas.findChild<ClockDesklet *>();
        const QRect before=clock?clock->geometry():QRect();
        desktopToggle->click();settle(80);
        check(!canvas.fencesDesktopVisible() && window->isVisible() && desktopToggle->text()=="切换回 Fences 桌面","switching to system desktop keeps a visible return action");
        window->showMinimized();settle(40);canvas.showUnifiedSettings();settle(60);
        check(!canvas.fencesDesktopVisible() && desktopToggle->text()=="切换回 Fences 桌面","reopening settings retains the return-to-Fences action");
        desktopToggle->click();settle(150);
        check(canvas.fencesDesktopVisible() && clock && clock->isVisible() && clock->geometry()==before,"return switch restores desktop and existing widget geometry");
        canvas.setClockWidgetVisible(false);
        canvas.hideFences();check(desktopToggle->text()=="切换回 Fences 桌面","external hide updates settings immediately");
        canvas.showAndActivate();
    }
    window->openPage("icons");
    auto *iconForm=window->findChild<QWidget *>("iconAppearanceDialog");
    check(iconForm,"icon editor exists as embedded QWidget");
    const int original=IconAppearance::load().strength;
    iconForm->findChild<QSlider *>("iconGlassStrength")->setValue(original==70?71:70);
    window->openPage("help");window->openPage("icons");
    check(iconForm==window->findChild<QWidget *>("iconAppearanceDialog"),"page navigation retains an unapplied draft");
    QTimer::singleShot(30,window,[]{if(auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget()))box->reject();});
    window->close();check(window && window->isVisible(),"canceling close keeps the draft available");
    QTimer::singleShot(30,window,[]{if(auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget()))box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Discard)->click();});
    window->close();settle(100);
    // Model the outer event-loop boundary; nested settle() alone need not
    // deliver the WA_DeleteOnClose deferred deletion scheduled by close().
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(window.isNull(),"discard closes and destroys draft controls");
    check(IconAppearance::load().strength==original,"discard does not persist draft appearance");
    canvas.showSettingsPage("icons");window=settingsWindow();settle(80);
    iconForm=window->findChild<QWidget *>("iconAppearanceDialog");
    iconForm->findChild<QSlider *>("iconGlassStrength")->setValue(61);
    iconForm->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    check(IconAppearance::load().strength==61 && !iconForm->property("settingsDirty").toBool(),"apply persists appearance and clears draft state");
    window->openPage("widgets");
    QCheckBox *startup=nullptr,*visible=nullptr;
    for(auto *c:window->findChildren<QCheckBox *>()){
        if(c->property("startupKey").toString()=="clock")startup=c;
        if(c->property("visibilityKey").toString()=="clock")visible=c;
    }
    check(startup && visible,"component has distinct visibility and startup controls");
    startup->setChecked(true);check(LiquidDesklet::autoStartEnabled("clock") && !canvas.clockWidgetVisible(),"startup choice does not immediately show a component");
    visible->setChecked(true);check(canvas.clockWidgetVisible(),"visibility switch reveals component");
    visible->setChecked(false);check(!canvas.clockWidgetVisible() && LiquidDesklet::autoStartEnabled("clock"),"hiding does not erase startup choice");
    canvas.showSystemMonitorWidget();canvas.showSmartSpaceWidget();settle(400);
    for(const QString id:{"monitor","smart"}){
        window->openPage(id);settle(100);
        auto *current=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
        auto forms=current->findChildren<QDialogButtonBox *>();
        check(!forms.isEmpty(),"complex component form is embedded without a modal dialog");
        check(!QApplication::activeModalWidget(),"complex page leaves desktop interactive");
        if(id=="monitor" && !forms.isEmpty()){
            auto *interval=current->findChildren<QSpinBox *>().last();
            const int value=interval->value();interval->stepUp();
            check(interval->buttonSymbols()!=QAbstractSpinBox::NoButtons,"spinbox retains normal step buttons");
            check(interval->value()==qMin(value+1,interval->maximum()),"normal step button updates the existing spinbox");interval->setValue(3);
            forms.first()->button(QDialogButtonBox::Apply)->click();
            check(s.value("systemMonitor/statInterval").toInt()==3,"monitor apply keeps sampling configuration persistence");
        }
        if(id=="smart") {
            check(!canvas.smartSpaceIndexBusy(),"visiting smart settings does not start indexing");
            auto *tabs=current->findChild<QTabWidget *>();
            if(tabs)tabs->setCurrentIndex(3);settle(80);
            bool skinFound=false;
            for(auto *skin:current->findChildren<QComboBox *>())if(skin->findText("黑夜模式")>=0){
                skinFound=true;skin->showPopup();settle(80);
                QWidget *surface=skin->view()->window();
                const QRect lastRow=skin->view()->visualRect(skin->model()->index(skin->count()-1,0));
                check(lastRow.bottom()<skin->view()->viewport()->height(),"three-item skin selector shows the complete last row");
                for(auto *child:surface->findChildren<QWidget *>())
                    if(QByteArray(child->metaObject()->className())=="QComboBoxPrivateScroller")
                        check(!child->isVisible(),"skin list has no opaque private top or bottom scroller");
                const QImage image=surface->grab().toImage();int whiteRows=0;
                for(int y=0;y<image.height();++y){int white=0;
                    for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);white+=c.alpha()>220 && c.red()>235 && c.green()>235 && c.blue()>235;}
                    if(white>image.width()*0.8)++whiteRows;
                }
                check(whiteRows<3,"skin dropdown contains no full-width white band");
                surface->grab().save(QString("artifacts/settings-skin-popup-%1.png").arg(window->devicePixelRatioF()));
                skin->hidePopup();
            }
            check(skinFound,"smart-space skin selector was exercised");
            for(int i=0;tabs && i<tabs->count();++i){
                tabs->setCurrentIndex(i);settle(50);
                check(!tabs->widget(i)->autoFillBackground(),"every smart tab keeps its liquid background");
                const QImage tab=window->grab(QRect(tabs->mapTo(window,QPoint()),tabs->size())).toImage();int light=0,total=0;
                for(int y=2;y<tab.height();y+=4)for(int x=2;x<tab.width();x+=4){const auto c=tab.pixelColor(x,y);light+=c.red()>230 && c.green()>230 && c.blue()>230;++total;}
                check(light<total/5,"smart tab has no large white surface during navigation");
                QDir().mkpath("artifacts");window->grab().save(QString("artifacts/settings-smart-tab-%1-%2.png").arg(i).arg(window->devicePixelRatioF()));
            }
            if(tabs){
                tabs->setCurrentIndex(4);auto *chunk=tabs->currentWidget()->findChildren<QSpinBox *>().first();
                const int chunkBefore=chunk->value();chunk->stepUp();
                check(chunk->value()==chunkBefore+chunk->singleStep(),"spinbox respects configured chunk-size steps");
                chunk->setValue(chunkBefore);tabs->setCurrentIndex(0);
            }
            for(auto *c:current->findChildren<QCheckBox *>())if(c->text().contains("同步目录"))c->setChecked(false);
            if(!forms.isEmpty())forms.first()->button(QDialogButtonBox::Apply)->click();
            check(!canvas.smartSpaceIndexBusy(),"applying smart configuration without explicit indexing does not start a job");
        }
        QDir().mkpath("artifacts");window->grab().save(QString("artifacts/settings-%1-%2.png").arg(id).arg(window->devicePixelRatioF()));
    }
    window->openPage("monitor");canvas.toggleDesklet();settle(150);window->openPage("monitor");
    check(window && window->isVisible(),"removing component while its settings exist is safe");
    auto *fence=canvas.createFence("设置测试",QRect(200,200,350,280));
    window->openPage("fence:"+fence->fenceId());
    auto *fencePage=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
    auto fontForms=fencePage->findChildren<QDialogButtonBox *>();
    check(fontForms.size()==2,"both per-fence font editors are embedded");
    if(!fontForms.isEmpty())fontForms.first()->button(QDialogButtonBox::Apply)->click();
    check(fence->hasLocalFont(),"per-fence font apply updates the original model");
    window->openPage("appearance");settle(300);QDir().mkpath("artifacts");
    window->grab().save(QString("artifacts/settings-center-%1.png").arg(window->devicePixelRatioF()));
    window->close();settle(100);
    return failures?1:0;
}
