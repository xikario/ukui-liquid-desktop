#pragma once
#include "FenceIconPicker.h"
#include <QListView>
#include <QStackedWidget>
#include <QProcess>
#include <QWindow>
#include "MenuTextRecorder.h"

static int runFenceSettingsTest(const QString &root)
{
    QSettings settings;
    settings.setValue("smartSpace/autoStart",false);
    settings.setValue("systemMonitor/autoStart",false);
    for (const QString key : {"clock","activity","music","calendar"})
        LiquidDesklet::setAutoStart(key,false);
    const QString iconRoot = root + "/theme-icons";
    auto theme = [&](const QString &name, const QString &parents, const QColor &color) {
        const QString directory = iconRoot + '/' + name;
        QDir().mkpath(directory + "/32x32/places");
        QDir().mkpath(directory + "/64x64/places");
        QFile index(directory + "/index.theme");
        check(index.open(QIODevice::WriteOnly),"theme fixture opens");
        index.write(("[Icon Theme]\nName="+name+"\nComment=fixture\nInherits="+parents+
            "\nDirectories=32x32/places,64x64/places\n"
            "[32x32/places]\nSize=32\nType=Fixed\nContext=Places\n"
            "[64x64/places]\nSize=64\nType=Fixed\nContext=Places\n").toUtf8());
        index.close();
        QImage image(32,32,QImage::Format_ARGB32);image.fill(color);
        image.save(directory+"/32x32/places/fixture-shared.png");
        image.save(directory+"/64x64/places/fixture-shared.png");
    };
    theme("fixture-base","fixture-parent",QColor("#16b463"));
    theme("fixture-parent","",QColor("#e25025"));
    theme("fixture-other","",QColor("#285add"));
    theme("hicolor","",QColor("#555555"));
    QFile::copy(iconRoot+"/fixture-parent/32x32/places/fixture-shared.png",
                iconRoot+"/fixture-parent/32x32/places/fixture-parent-only.png");
    QFile::copy(iconRoot+"/hicolor/32x32/places/fixture-shared.png",
                iconRoot+"/hicolor/32x32/places/fixture-fallback-only.png");
    const auto oldRoots = QIcon::themeSearchPaths();
    const auto oldTheme = QIcon::themeName();
    const auto oldFallback = QIcon::fallbackThemeName();
    const auto oldFallbackRoots = QIcon::fallbackSearchPaths();
    QIcon::setThemeSearchPaths(QStringList{iconRoot});
    QIcon::setFallbackSearchPaths({});
    QIcon::setFallbackThemeName("hicolor");
    QIcon::setThemeName("fixture-base");
    QDir().mkpath(root+"/config/kyfences");
    QImage background(1000,700,QImage::Format_RGB32);background.fill(QColor("#26364b"));
    background.save(root+"/test-wallpaper.png");
    QFile fixture(root+"/config/kyfences/layout.json");fixture.open(QIODevice::WriteOnly);
    fixture.write(QJsonDocument(QJsonObject{{"wallpaperPath",root+"/test-wallpaper.png"},
        {"wallpaperMode",2},{"fences",QJsonArray{}}}).toJson());fixture.close();
    DesktopCanvas canvas;canvas.showAndActivate();
    QElapsedTimer startup;startup.start();
    while(!canvas.isVisible() && startup.elapsed()<5000)settle(20);
    check(canvas.isVisible(),"test desktop prepares its initial wallpaper before menu interaction");
    settle(100);
    auto *fence = canvas.createFence("合并设置回归",QRect(100,100,420,300));
    auto *other = canvas.createFence("另一个分区",QRect(650,100,420,300));
    check(!fence->titleIconPath().isEmpty() && !QIcon(fence->titleIconPath()).pixmap(24,24).isNull(),
          "new fence has a usable default icon without external assets");
    const auto byId=[](DesktopCanvas &desktop,const QString &id)->FenceWidget * {
        for(auto *item:desktop.findChildren<FenceWidget *>(QString(),Qt::FindDirectChildrenOnly))
            if(item->fenceId()==id)return item;
        return nullptr;
    };
    const QString filePath = root + "/保留的文件.txt";
    QFile file(filePath);file.open(QIODevice::WriteOnly);file.write("keep");file.close();
    fence->addItem(DesktopItem::fromPath(filePath));
    const QRect originalGeometry = fence->geometry();
    FencesSettingsWindow *window = nullptr;
    for (bool title : {true,false}) {
        bool inspected = false;
        QTimer::singleShot(50,&canvas,[&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            check(menu,"actual fence context menu opens");
            if(!menu)return;
            auto *entry = menu->findChild<QAction *>("fenceSettingsAction");
            check(entry && menu->actions().contains(entry),"title and body have a root settings entry");
            int entries = 0;
            for(auto *action:menu->actions()) {
                entries += action->text()=="分区设置…";
                check(!QStringList{"透明度","分区颜色","分区图标…","清除分区图标",
                    "标题字体设置…","内部图标字体设置…"}.contains(action->text()),
                    "appearance and font settings are consolidated");
            }
            check(entries==1,"each context menu has exactly one settings entry");
            inspected=true;
            menu->close();
            if(entry)entry->trigger();
        });
        const QPoint pos(80,title?15:100);
        QContextMenuEvent event(QContextMenuEvent::Mouse,pos,fence->mapToGlobal(pos));
        QApplication::sendEvent(fence,&event);
        check(inspected,"actual context menu was inspected");
        for(auto *w:QApplication::topLevelWidgets())
            if(auto *candidate=qobject_cast<FencesSettingsWindow *>(w))window=candidate;
        check(window && window->findChild<QLineEdit *>("fenceTitleEdit") && window->findChild<QLineEdit *>("fenceTitleEdit")->text()==fence->title(),
              "context entry opens the matching fence in the shared settings window");
    }
    if(!window)return 1;
    auto page = [&] {return window->findChild<QStackedWidget *>("settingsPages")->currentWidget();};
    check(page()->findChildren<QDialogButtonBox *>().size()==2,
          "both title and internal font forms remain in the consolidated page");
    check(page()->findChild<QSpinBox *>("fenceOpacity") &&
          page()->findChild<QCheckBox *>("fenceLocked"),"opacity and locking are available in settings");
    check(fence->geometry()==originalGeometry && !canvas.smartSpaceVisible() && !canvas.systemMonitorVisible(),
          "opening per-fence settings preserves layout and hidden components");
    auto *opacity=page()->findChild<QSpinBox *>("fenceOpacity");
    QColor chosenColor("#345678");chosenColor.setAlphaF(0.55);
    fence->setFenceColor(chosenColor);
    check(opacity->value()==55,"color changes keep the opacity control in sync");
    opacity->setValue(60);
    check(qRound(fence->fenceColor().alphaF()*100)==60,"opacity control changes the real fence color");
    // Run actual modal controls with a deadline instead of fixed timing.
    auto modal = [&](const std::function<bool(QDialog *)> &action, const std::function<void()> &open) {
        QElapsedTimer deadline;deadline.start();bool handled=false;
        QTimer timer;timer.setInterval(20);
        QObject::connect(&timer,&QTimer::timeout,[&] {
            timer.stop();
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if(dialog && action(dialog)){handled=true;timer.stop();}
            if(deadline.elapsed()>5000){check(false,"modal inspection completes within deadline");timer.stop();if(dialog)dialog->reject();}
            else if(!handled)timer.start();
        });
        timer.start();open();timer.stop();check(handled,"real modal input was delivered");
    };
    auto *clear = page()->findChild<QPushButton *>("fenceClearTitleIcon");
    const auto originalIcon = fence->titleIconPath();
    modal([&](QDialog *dialog) {
        if(dialog->objectName()!="liquidMessageDialog")return false;
        bool cancelDefault=false;
        for(auto *button:dialog->findChildren<QPushButton *>())
            cancelDefault |= button->property("messageChoice").toInt()==QMessageBox::Cancel && button->isDefault();
        check(cancelDefault,"clear confirmation defaults to Cancel");
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(dialog,&enter);return true;
    },[&]{clear->click();});
    check(fence->titleIconPath()==originalIcon,"default confirmation key does not clear an icon");
    modal([&](QDialog *dialog) {
        if(dialog->objectName()!="liquidMessageDialog")return false;
        for(auto *button:dialog->findChildren<QPushButton *>())
            if(button->property("messageChoice").toInt()==QMessageBox::Yes){button->click();return true;}
        return false;
    },[&]{clear->click();});
    check(!fence->hasTitleIcon() && !clear->isEnabled(),"confirmed clear updates icon and button state");
    check(fence->items().size()==1 && QFileInfo::exists(filePath) && other->hasTitleIcon(),
          "clearing title icon preserves files and other fences");
    auto *choose = page()->findChild<QPushButton *>("fenceChooseLibraryIcon");
    modal([&](QDialog *dialog) {
        if(dialog->objectName()!="fenceIconPicker")return false;
        check(!dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint)
            && dialog->windowHandle()->transientParent()==window->windowHandle()
            && !dialog->windowIcon().pixmap(64,64).isNull(),
            "icon picker belongs above its settings owner without pinning above other applications");
        QProcess identity;
        identity.start("xprop",{"-id",QString::number(dialog->winId()),"WM_CLASS","_KDE_NET_WM_DESKTOP_FILE"});
        check(identity.waitForFinished(2000) && identity.readAllStandardOutput().count("ukui-fences-settings")>=3,
            "icon picker native task identity maps to the Fences settings icon");
        auto *tabs=dialog->findChild<QTabWidget *>("fenceIconTabs");tabs->setCurrentIndex(1);
        auto *view=dialog->findChild<QListView *>("fenceDefaultIconList");
        check(view->model()->rowCount()==10,"ten built-in defaults are available");
        for(int i=0;i<view->model()->rowCount();++i)
            check(!qvariant_cast<QIcon>(view->model()->index(i,0).data(Qt::DecorationRole)).pixmap(24,24).isNull(),
                  "every default icon renders from embedded resources");
        view->setCurrentIndex(view->model()->index(2,0));
        settle(100);
        MenuTextRecorder text(*view->viewport());
        for(int i=0;i<view->model()->rowCount();++i) {
            const auto index=view->model()->index(i,0);const auto row=view->visualRect(index);
            check(!text.boundsFor(index.data().toString(),row).isEmpty(),
                  "every built-in icon paints its complete label");
        }
        const auto image=dialog->grab().toImage();
        int white=0,total=0;
        for(int y=4;y<image.height();y+=4)for(int x=4;x<image.width();x+=4) {
            const auto color=image.pixelColor(x,y);
            white+=color.red()>235 && color.green()>235 && color.blue()>235;++total;
        }
        check(white<total/10,"switching icon tabs does not expose a large opaque white page");
        QDir().mkpath("artifacts/fence-settings");
        dialog->grab().save(QString("artifacts/fence-settings/defaults-%1.png").arg(dialog->devicePixelRatioF()));
        dialog->findChild<QDialogButtonBox *>("fenceIconButtons")->button(QDialogButtonBox::Ok)->click();
        return true;
    },[&]{choose->click();});
    check(fence->titleIconPath()==":/fence-icons/industry.svg" && fence->titleIconThemeName().isEmpty(),
          "default selection applies only to the selected fence");
    bool searched=false;
    modal([&](QDialog *dialog) {
        if(dialog->objectName()!="fenceIconPicker")return false;
        auto *tabs=dialog->findChild<QTabWidget *>("fenceIconTabs");tabs->setCurrentIndex(0);
        auto *view=dialog->findChild<QListView *>("fenceSystemIconList");
        if(!searched) {
            if(view->model()->rowCount()<3)return false;
            QSet<QString> names;
            for(int i=0;i<view->model()->rowCount();++i)names.insert(view->model()->index(i,0).data().toString());
            check(names.contains("fixture-parent-only") && names.contains("fixture-fallback-only"),
                  "catalog includes inherited and hicolor icons");
            check(names.size()==view->model()->rowCount(),"catalog deduplicates icon sizes");
            dialog->findChild<QLineEdit *>("fenceIconSearch")->setText("FIXTURE-SHARED");
            searched=true;return false;
        }
        if(view->model()->rowCount()!=1)return false;
        view->setCurrentIndex(view->model()->index(0,0));
        settle(100);
        check(view->currentIndex().data().toString()=="fixture-shared","system icon search is case insensitive");
        dialog->grab().save(QString("artifacts/fence-settings/system-%1.png").arg(dialog->devicePixelRatioF()));
        dialog->findChild<QLineEdit *>("fenceIconSearch")->setText("pending-filter-no-match");
        dialog->findChild<QDialogButtonBox *>("fenceIconButtons")->button(QDialogButtonBox::Ok)->click();
        return true;
    },[&]{choose->click();});
    check(fence->titleIconThemeName()=="fixture-shared" && fence->titleIconPath().isEmpty(),
          "accepted system choice survives a pending search and persists its icon name");
    const auto sampleIcon = [&] {
        const auto image=fence->grab().toImage();const auto dpr=image.devicePixelRatio();
        return image.pixelColor(qRound(20*dpr),qRound(17*dpr));
    };
    check(sampleIcon()==QColor("#16b463"),"selected current-theme icon is painted on the fence");
    QIcon::setThemeName("fixture-other");fence->update();
    check(sampleIcon()==QColor("#285add"),"existing fence icon follows a system icon theme change");
    fence->setTitleIconThemeName("fixture-parent-only");
    QIcon::setThemeName("fixture-base");fence->update();
    check(sampleIcon()==QColor("#e25025"),"temporarily missing icon resolves again when a theme supplies it");
    fence->setTitleIconThemeName("fixture-shared");
    const auto layoutPath=root+"/config/kyfences/layout.json";
    QFile saved(layoutPath);saved.open(QIODevice::ReadOnly);
    bool persisted=false;
    for(const auto &value:QJsonDocument::fromJson(saved.readAll()).object()["fences"].toArray()) {
        const auto obj=value.toObject();
        if(obj["id"].toString()==fence->fenceId())
            persisted=obj["titleIconTheme"].toString()=="fixture-shared" && !obj.contains("titleIcon");
    }
    check(persisted,"layout stores mutually exclusive theme/file icon sources");
    {
        DesktopCanvas restored;
        auto *loaded=byId(restored,fence->fenceId());
        check(loaded && loaded->titleIconThemeName()=="fixture-shared",
              "saved system icon survives a desktop reload");
    }
    for(int i=0;i<4;++i)modal([](QDialog *dialog) {
        if(dialog->objectName()!="fenceIconPicker")return false;
        dialog->reject();return true;
    },[&]{choose->click();});
    check(fence->titleIconThemeName()=="fixture-shared","cancelled catalog openings preserve the current icon");
    fence->setTitleIconPath(QString());
    {
        DesktopCanvas restored;
        auto *loaded=byId(restored,fence->fenceId());
        check(loaded && !loaded->hasTitleIcon(),"cleared system icon stays cleared after reload");
    }
    QIcon::setThemeSearchPaths(oldRoots);QIcon::setFallbackSearchPaths(oldFallbackRoots);
    QIcon::setFallbackThemeName(oldFallback);QIcon::setThemeName(oldTheme);
    window->close();settle(200);
    return failures?1:0;
}
