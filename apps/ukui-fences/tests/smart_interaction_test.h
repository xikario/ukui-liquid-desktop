#include <QMessageBox>
#include <QLabel>
#include <QAbstractButton>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QComboBox>
#include <QAbstractItemView>
#include <QScrollArea>
#include <QKeyEvent>

class SmartTransitionVisibilityProbe final : public QObject
{
public:
    bool recording = false;
    int visibleGeometryChanges = 0;
    int expandedShows = 0;
    int transientEntryShows = 0;
    SmartSpaceWidget *smart = nullptr;
    QToolButton *entry = nullptr;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!recording) return false;
        if (watched == smart) {
            if ((event->type()==QEvent::Resize || event->type()==QEvent::Move)
                && smart->isVisible()) ++visibleGeometryChanges;
            if (event->type()==QEvent::Show && smart->width()>52)
                ++expandedShows;
        }
        if (watched==entry && event->type()==QEvent::Show && smart->width()>52)
            ++transientEntryShows;
        return false;
    }
};

static int runSmartInteractionTest(const QString &root)
{
    QSettings settings;
    settings.setValue("smartSpace/themeMode", 3);
    settings.setValue("smartSpace/defaultHidden", false);
    settings.setValue("smartSpace/alwaysOnTop", false);
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    // Exercise the populated card hierarchy, not just an empty search pane.
    QJsonArray items;
    for (int i=0; i<120; ++i) {
        QString folder = root + QString("/folder-%1").arg(i % 9);
        if (i % 9 == 0)
            folder += i % 2 == 0 ? "/branch-a" : "/branch-b";
        QDir().mkpath(folder);
        const QString path = folder + (i == 0
            ? QString("/000-%1.txt").arg(QString(72, QChar(0x6587)))
            : QString("/document-%1.txt").arg(i));
        QFile file(path); file.open(QIODevice::WriteOnly); file.write("fixture");
        items.append(QJsonObject{{"path", path}, {"root", root},
            {"category", "text"}, {"content", "animation fixture"}});
    }
    const QString indexDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + "/smart-space";
    QDir().mkpath(indexDir);
    QFile index(indexDir + "/index.json"); index.open(QIODevice::WriteOnly);
    index.write(QJsonDocument(QJsonObject{{"items", items}}).toJson()); index.close();
    DesktopCanvas canvas;
    canvas.show(); canvas.showSmartSpaceWidget();
    canvas.moveSmartSpace(100,100); canvas.resizeSmartSpace(800,500);
    settle(600);
    auto *smart=canvas.findChild<SmartSpaceWidget *>();
    check(smart!=nullptr,"Smart Space exists");
    if (!smart) return 1;
    auto *folderScroll=smart->findChild<QScrollArea *>("smartFolderScroll");
    auto *rootFolders=smart->findChild<QListWidget *>("smartRootFolderList");
    check(folderScroll && rootFolders && rootFolders->count()==9,
          "folder navigation contains all nine synthetic root groups");
    if(folderScroll && rootFolders) {
        check(rootFolders->viewport()->height()>folderScroll->viewport()->height()*9/10,
              "root folder list uses the whole pane instead of sharing it with a blank stretch");
        const QPoint local=rootFolders->visualItemRect(rootFolders->item(0)).center();
        const QPoint global=rootFolders->viewport()->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(rootFolders->viewport(),&press);
        QApplication::sendEvent(rootFolders->viewport(),&release);settle(80);
        auto *children=smart->findChild<QListWidget *>("smartChildFolderList");
        check(children && children->isVisible() && children->count()==2,
              "clicking a root folder still displays its two child directories below");
        const QPoint blank(2,folderScroll->viewport()->height()-2);
        QMouseEvent reset(QEvent::MouseButtonPress,blank,folderScroll->viewport()->mapToGlobal(blank),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(folderScroll->viewport(),&reset);settle(80);
        rootFolders=smart->findChild<QListWidget *>("smartRootFolderList");
        check(!smart->findChild<QListWidget *>("smartChildFolderList") && rootFolders && rootFolders->count()==9,
              "blank navigation click restores all roots and removes the drilldown level");
    }
    auto *category=smart->findChild<QComboBox *>("smartCategoryFilter");
    check(category && !category->style()->styleHint(QStyle::SH_ComboBox_Popup,nullptr,category),
          "type picker uses a normal scrolling list without UKUI menu scrollers");
    if(category) {
        category->showPopup();settle(100);
        auto *popup=category->view()->window();
        check(popup->isVisible() && popup->property("settingsComboPopupSurface").toBool()
              && popup->property("liquidComboMaterialBytes").toLongLong()>0,
              "actual type picker opens with the shared liquid material");
        const QImage frame=popup->grab().toImage();
        bool whiteBand=false;
        int whiteRows=0;
        const int edge=qMax(1,qRound(5*frame.devicePixelRatio()));
        for(int y=0;y<frame.height();++y) {
            if(y>=edge && y<frame.height()-edge) {whiteRows=0;continue;}
            int bright=0;
            for(int x=edge;x<frame.width()-edge;++x) {
                const QColor color=frame.pixelColor(x,y);
                if(color.red()>240 && color.green()>240 && color.blue()>240)++bright;
            }
            whiteRows=bright>(frame.width()-edge*2)*.85 ? whiteRows+1 : 0;
            // The shared material has a fine bright rim. A native scroller
            // produces an opaque band several pixels thick, not that outline.
            if(whiteRows>=qMax(2,qCeil(1.5*frame.devicePixelRatio())))whiteBand=true;
        }
        check(!whiteBand,"type popup has no solid white top or bottom stripe");
        const QRect last=category->view()->visualRect(category->model()->index(category->count()-1,0));
        check(category->view()->viewport()->rect().contains(last),
              "all seven types fit, including the full final row");
        QDir().mkpath("artifacts");
        frame.save(QString("artifacts/smart-type-popup-%1.png").arg(smart->devicePixelRatioF()));
        const int builds=popup->property("liquidComboMaterialBuilds").toInt();
        QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier),enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(category->view(),&down);QApplication::sendEvent(category->view(),&down);
        QApplication::sendEvent(category->view(),&enter);settle(50);
        auto *results=smart->findChild<QWidget *>("smartResultPage")->findChild<QListWidget *>();
        check(category->currentData().toString()=="pdf" && !popup->isVisible()
              && results && results->count()==1 && results->item(0)->flags()==Qt::NoItemFlags
              && results->item(0)->text()==QString::fromUtf8("没有匹配文件"),
              "native keyboard type activation closes the popup and filters actual results");
        check(popup->property("liquidComboMaterialBytes").toLongLong()==0
              && popup->property("liquidComboMaterialBuilds").toInt()==builds,
              "selection releases popup material without rebuilding it for each row");
        category->showPopup();settle(60);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(category->view(),&escape);settle(40);
        check(!popup->isVisible() && category->currentData().toString()=="pdf" && smart->isVisible(),
              "Escape cancels the type picker without changing the filter or hiding Smart Space");
        category->setCurrentIndex(0);settle(50);
    }
    // Send real grip events for both desktop-child and pinned native-window modes.
    auto *resizePeer=canvas.createFence("resize peer",QRect(910,100,200,400));
    for (bool pinned : {false,true}) {
        canvas.setSmartSpaceAlwaysOnTop(pinned);settle(100);
        canvas.moveSmartSpace(100,100);canvas.resizeSmartSpace(800,500);settle(100);
        auto *grip=smart->findChild<QWidget *>("smartSpaceResizeGrip");
        check(grip!=nullptr,"smart space exposes its resize grip");
        if (grip) {
            const QPoint local=grip->rect().center(),global=grip->mapToGlobal(local);
            QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(grip,&press);
            QMouseEvent move(QEvent::MouseMove,local+QPoint(4,0),global+QPoint(4,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(grip,&move);
            QMouseEvent release(QEvent::MouseButtonRelease,local,global+QPoint(4,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(grip,&release);
            check(smart->size()==QSize(810,506),"smart space resize snaps to fence with its aspect ratio preserved");
        }
    }
    resizePeer->hide();canvas.setSmartSpaceAlwaysOnTop(false);
    canvas.moveSmartSpace(100,100);canvas.resizeSmartSpace(800,500);settle(100);
    SmartTransitionVisibilityProbe visibility;
    visibility.smart = smart;
    visibility.entry = smart->findChild<QToolButton *>("smartEdgeReveal");
    smart->installEventFilter(&visibility);
    visibility.entry->installEventFilter(&visibility);
    check(smart->findChildren<QWidget *>("fileResultCard").size() >= 100,
          "transition regression includes a full page of file cards");
    auto *resultStack=smart->findChild<QStackedWidget *>("smartResultStack");
    auto *resultPage=smart->findChild<QWidget *>("smartResultPage");
    auto *previewPage=smart->findChild<QFrame *>("smartPreviewPanel");
    auto *resultList=resultPage ? resultPage->findChild<QListWidget *>() : nullptr;
    check(resultStack && resultPage && previewPage && resultList,
          "results and preview share one content stack");
    if(resultStack && resultList) {
        QToolButton *preview=nullptr;
        for(auto *button:resultPage->findChildren<QToolButton *>())
            if(button->property("previewAction").toBool()) {preview=button;break;}
        check(preview!=nullptr,"file results expose the real preview action");
        const int resultCount=resultList->count();
        const QRect originalGeometry=smart->geometry();
        if(preview) {
            preview->click();settle(100);
            auto *text=smart->findChild<QTextBrowser *>("smartPreviewText");
            check(resultStack->currentWidget()==previewPage && !resultList->isVisible()
                && text && text->isVisible() && text->toPlainText().contains("fixture"),
                "preview replaces painted results and displays the selected file text");
            check(smart->geometry()==originalGeometry,
                "long preview filenames do not resize or displace smart space");
            auto closePreview=[&] {
                for(auto *button:previewPage->findChildren<QToolButton *>())
                    if(button->toolTip()==QString::fromUtf8("关闭预览")) {button->click();return;}
            };
            closePreview();settle(30);
            check(resultStack->currentWidget()==resultPage && resultList->isVisible()
                && resultList->count()==resultCount,
                "closing preview restores the existing paginated results");
            canvas.resizeSmartSpace(650,410);settle(80);
            preview->click();settle(80);
            check(resultStack->currentWidget()==previewPage && !resultList->isVisible(),
                "compact layout keeps preview and results mutually exclusive");
            closePreview();canvas.resizeSmartSpace(originalGeometry.width(),originalGeometry.height());settle(80);
        }
    }
    for (bool pinned : {false,true}) {
        canvas.setSmartSpaceAlwaysOnTop(pinned); settle(100);
        const QRect before=smart->geometry();
        for (int i=0;i<3;++i) {
            visibility.recording = true;
            smart->hideToNearestEdge();
            visibility.recording = false;
            check(visibility.visibleGeometryChanges==0 && visibility.transientEntryShows==0,
                  "collapse preparation never exposes resized content or a transient entry window");
            check(smart->edgeHidden(),"collapse remains available during rapid toggles");
            auto *retract=canvas.findChild<QWidget *>("smartSpaceRetractFrame");
            check(retract && retract->isVisible(),"collapse animates a cached frame");
            check(retract && retract->geometry()==before,"retraction stays within expanded footprint");
            check(retract && (pinned || retract->testAttribute(Qt::WA_OpaquePaintEvent)),
                  "desktop transition does not repaint transparent ancestors every frame");
            check(smart->isVisible() && smart->size()==QSize(52,52),"edge entry remains clickable during retraction");
            if (retract) {
                auto *animation=retract->findChild<QVariantAnimation *>();
                check(animation && animation->startValue().toReal()>=0.0
                    && animation->startValue().toReal()<=1.0
                    && animation->endValue().toReal()==0.0 && animation->loopCount()==1
                    && animation->duration()<=200,"retraction is a short one-shot reverse transition");
            }
            settle(30);
            auto *closingAnimation = retract ? retract->findChild<QVariantAnimation *>() : nullptr;
            const qreal closingProgress = closingAnimation ? closingAnimation->currentValue().toReal() : -1;
            auto *edge = smart->findChild<QToolButton *>("smartEdgeReveal");
            auto *opacity = edge ? qobject_cast<QGraphicsOpacityEffect *>(edge->graphicsEffect()) : nullptr;
            check(opacity && qAbs(opacity->opacity() - (1-closingProgress)) < 0.001,
                  "edge entry fades in with the retracting content");
            visibility.recording = true;
            smart->revealFromEdge();
            visibility.recording = false;
            check(visibility.visibleGeometryChanges==0 && visibility.expandedShows==0,
                  "reveal preparation never maps the expanded content before its animation");
            check(!canvas.findChild<QWidget *>("smartSpaceRetractFrame"),"reopen cancels retraction immediately");
            auto *frame=canvas.findChild<QWidget *>("smartSpaceRevealFrame");
            check(frame && frame->isVisible(),"reveal animates a visible cached frame");
            auto *openingAnimation = frame ? frame->findChild<QVariantAnimation *>() : nullptr;
            check(frame == retract && openingAnimation
                      && qAbs(openingAnimation->startValue().toReal()-closingProgress)<0.001,
                  "reversal reuses the frame and continues at exactly the current progress");
            check(smart->geometry()==before,"animation does not resize/reflow live content");
            settle(30);
        }
        settle(250);
        check(smart->isVisible() && !smart->edgeHidden(),"live content restored after reveal");
        check(!canvas.findChild<QWidget *>("smartSpaceRevealFrame"),"transition frame released after completion");
        check(!smart->findChild<QToolButton *>("smartEdgeReveal")->graphicsEffect(),
              "entry effect is released when animation is idle");
        check(smart->geometry()==before,"rapid toggles retain expanded geometry");
        smart->hideToNearestEdge();
        const QRect collapsed=smart->geometry();
        settle(250);
        check(smart->isVisible() && smart->edgeHidden() && smart->geometry()==collapsed,
            "completed retraction leaves the edge entry at its final location");
        check(!canvas.findChild<QWidget *>("smartSpaceRetractFrame"),
            "retraction releases its frame and animation when idle");
        check(smart->property("edgePaintFrames").toInt() > 1,
              "completed transition records actual paint frames");
        smart->revealFromEdge(); settle(250);
        check(smart->geometry()==before,"reveal after completed retraction preserves original geometry");
    }
    canvas.setSmartSpaceAlwaysOnTop(false); settle(100);
    auto *toggle=smart->findChild<QToolButton *>("smartThemeToggle");
    for (int mode=0;mode<3;++mode) {
        bool inspected=false;
        QTimer::singleShot(150,smart,[&] {
            auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(box!=nullptr,"fast full index shows real confirmation");
            if (!box) return;
            auto *label=box->findChild<QLabel *>("smartMessageText");
            check(label && !label->text().isEmpty(),"confirmation contains explanatory text");
            // The palette's Window brush is transparent for a glass surface;
            // inspect the actual cached background in the gap above the buttons.
            const QImage frame=box->grab().toImage();
            auto *buttonBox=box->findChild<QDialogButtonBox *>();
            const qreal ratio=frame.devicePixelRatio();
            const QColor bg=frame.pixelColor(qRound(box->width()/2.*ratio),qRound((buttonBox->geometry().top()-8)*ratio));
            const QColor fg=label ? label->palette().color(QPalette::WindowText) : bg;
            auto luma=[](QColor c) { return .2126*c.red()+.7152*c.green()+.0722*c.blue(); };
            check(qAbs(luma(bg)-luma(fg))>120,"confirmation text has readable contrast in each skin");
            QPushButton *cancel=nullptr;
            for (auto *b:box->findChildren<QPushButton *>())
                if (b->property("messageChoice").toInt()==QMessageBox::No) cancel=b;
            check(cancel!=nullptr,"cancel button is present");
            box->grab().save(root+QString("/confirm-%1.png").arg(mode));
            inspected=true;
            if(cancel) cancel->click(); else box->reject();
        });
        smart->startFastFullIndex();
        check(inspected && !smart->indexBusy(),"canceling confirmation does not start an index");
        if (toggle) toggle->click();
        settle(60);
    }
    return failures ? 1 : 0;
}
