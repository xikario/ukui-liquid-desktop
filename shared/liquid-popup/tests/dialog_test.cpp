#include "LiquidDialog.h"
#include "MaterialReference.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QProgressDialog>
#include <QProgressBar>
#include <QToolButton>
#include <QShowEvent>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QDebug>
#include <cstdlib>
#include <memory>

static void check(bool ok,const char *message) {
    if(!ok){qCritical()<<message;std::exit(1);}
}
static void settle(int ms) {QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
static QPushButton *choice(QWidget *w,int value) {
    for(auto *b:w->findChildren<QPushButton *>())if(b->property("messageChoice").toInt()==value)return b;
    return nullptr;
}
class Paints : public QObject {
public: int count=0;
protected: bool eventFilter(QObject *,QEvent *e)override{if(e->type()==QEvent::Paint)++count;return false;}
};
class NativeVisibility : public QObject {
public:
    std::unique_ptr<QShowEvent> restored;
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Show && event->spontaneous())
            restored=std::make_unique<QShowEvent>(*static_cast<QShowEvent *>(event));
        return false;
    }
};
static QWidget *checkChrome(QWidget *window) {
    auto *header=window->findChild<QWidget *>("liquidDialogTitlebar");
    if(!window->windowFlags().testFlag(Qt::FramelessWindowHint) || !header || !header->isVisible())
        qCritical()<<"Unexpected frame state"<<window->windowFlags()<<header<<(header?header->isVisible():false);
    check(window->windowFlags().testFlag(Qt::FramelessWindowHint)
        && header && header->isVisible(),"owned dialog has an integrated title bar");
    auto *title=window->findChild<QLabel *>("liquidDialogTitle");
    check(title && title->textFormat()==Qt::PlainText
        && title->accessibleName()==window->windowTitle(),"title keeps plain text and full accessible name");
    check(window->findChild<QToolButton *>("liquidDialogClose"),"frame exposes an accessible close button");
    check(window->findChild<QToolButton *>("liquidDialogMinimize")
        && !window->findChild<QToolButton *>("liquidDialogMaximize")
        && window->windowFlags().testFlag(Qt::WindowMinimizeButtonHint)
        && !window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint),
        "owned frame exposes minimize and close only");
    check(window->windowType()==Qt::Window,"owned frame remains eligible for the task list");
    auto *icon=window->findChild<QLabel *>("liquidDialogIcon");
    check(!window->windowIcon().isNull() && icon && icon->pixmap() && !icon->pixmap()->isNull(),
        "owned frame has matching task-list and title-bar icons");
    return header;
}
int main(int argc,char **argv) {
    QTemporaryDir home;check(home.isValid(),"temporary home");qputenv("HOME",home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME",(home.path()+"/config").toUtf8());
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    const auto requestedStyle=qEnvironmentVariable("UKUI_DIALOG_TEST_STYLE","Fusion");
    check(app.setStyle(requestedStyle)!=nullptr,"requested Qt style is available");
    LiquidPopup::install(app);LiquidPopup::theme().reducedMotion=true;
    if(qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0") {
        LiquidDialog::Dialog native;native.show();settle(20);
        check(!native.property("liquidDialogInstalled").toBool(),"environment switch preserves native dialog surface");
        check(!native.windowFlags().testFlag(Qt::FramelessWindowHint)
            && !native.findChild<QWidget *>("liquidDialogTitlebar"),"disabled surface retains the native window frame");
        return 0;
    }

    QImage backdrop(480,360,QImage::Format_RGB32);
    for(int y=0;y<backdrop.height();++y)for(int x=0;x<backdrop.width();++x)
        backdrop.setPixel(x,y,qRgb((x*17+y*13)%256,(x*3+y*23)%256,(x*11+y*5)%256));
    // The reference contains the old per-pixel math, not the new cache algorithm.
    const auto originalTheme=LiquidPopup::theme();
    LiquidPopup::clearMaterialCache();
    for(qreal dpr:{1.,1.25,1.5,2.})for(bool light:{false,true})for(int variant=0;variant<3;++variant) {
        auto &theme=LiquidPopup::theme();theme.radius=variant*12;theme.refraction=variant*3.5;
        theme.tint=.17+variant*.21;theme.highlight=variant*.4;
        const QSize size(145+variant*13,97+variant*7);
        const QRectF body=variant==2?QRectF(8,12,size.width()-16,size.height()-24):QRectF();
        check(LiquidPopup::renderMaterial(backdrop,size,dpr,light,body)==MaterialReference::renderMaterial(backdrop,size,dpr,light,body),"cached optics preserve every pixel at all scales and tints");
        check(LiquidPopup::renderMenuMaterial(backdrop,size,dpr,light)==MaterialReference::renderMenuMaterial(backdrop,size,dpr,light),"rounded outline and alpha preserve every pixel");
    }
    LiquidPopup::theme()=originalTheme;
    LiquidPopup::clearMaterialCache();
    const auto first=LiquidPopup::renderMaterial(backdrop,QSize(220,130),1.5);
    auto stats=LiquidPopup::materialCacheStats();check(stats.builds==1,"first geometry computed once");
    QImage different=backdrop;different.fill(Qt::red);
    const auto second=LiquidPopup::renderMaterial(different,QSize(220,130),1.5);
    check(first!=second && LiquidPopup::materialCacheStats().hits==1,"geometry reuse never reuses an old backdrop");
    LiquidPopup::theme().refraction+=1;LiquidPopup::renderMaterial(backdrop,QSize(220,130),1.5);
    check(LiquidPopup::materialCacheStats().builds==2,"refraction change gets new geometry");
    for(int n=0;n<30;++n)LiquidPopup::renderMaterial(backdrop,QSize(220,180),1.,false,QRectF(80+n*.1,60,30,30));
    check(LiquidPopup::materialCacheStats().bytes<=2*1024*1024,"geometry cache has a strict two MiB bound");
    LiquidPopup::theme()=originalTheme;
    int captures=0;LiquidPopup::setBackdropProvider([&](const QRect &area,qreal dpr){
        ++captures;QImage img(area.size()*dpr,QImage::Format_RGB32);img.fill(QColor(40,85,125));img.setDevicePixelRatio(dpr);return img;
    });
    LiquidDialog::Dialog dialog(nullptr,Qt::Dialog|Qt::WindowMaximizeButtonHint|Qt::WindowCloseButtonHint);
    dialog.setWindowTitle("统一设置 · <真实标题>");dialog.resize(440,260);auto *layout=new QVBoxLayout(&dialog);
    auto *text=new QLabel("统一设置与确认 · 玻璃材质",&dialog);layout->addWidget(text);
    auto *combo=new QComboBox(&dialog);combo->addItems({"第一项","第二项","第三项"});layout->addWidget(combo);
    auto *fonts=new QFontComboBox(&dialog);layout->addWidget(fonts);
    auto *ok=new QPushButton("确定",&dialog);layout->addWidget(ok);
    dialog.show();settle(260);
    auto *header=checkChrome(&dialog);
    check(text->y()>header->geometry().bottom(),"body starts below the title bar");
    const int topMargin=layout->contentsMargins().top();
    const QPoint oldPosition=dialog.pos();
    const QPoint origin=header->rect().center(),global=header->mapToGlobal(origin);
    QMouseEvent dragPress(QEvent::MouseButtonPress,origin,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dragMove(QEvent::MouseMove,origin+QPoint(28,19),global+QPoint(28,19),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dragRelease(QEvent::MouseButtonRelease,origin+QPoint(28,19),global+QPoint(28,19),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(header,&dragPress);QApplication::sendEvent(header,&dragMove);QApplication::sendEvent(header,&dragRelease);
    check(dialog.pos()==oldPosition+QPoint(28,19),"dragging the header moves the actual dialog");
    check(dialog.property("liquidDialogMaterialBuilds").toInt()==1 && captures==1,"one current backdrop per opening");
    check(text->palette().color(QPalette::WindowText).lightness()>128 && ok->palette().color(QPalette::ButtonText).lightness()>128,"labels and buttons keep readable light text on dark glass");
    const auto snap=dialog.grab().toImage();QDir().mkpath("artifacts");snap.save("artifacts/unified-dialog.png");
    check(combo->property("settingsComboPopup").toBool() && fonts->property("settingsComboPopup").toBool(),"ordinary and font selectors use common popup skin");
    check(fonts->view()->itemDelegate()!=nullptr,"font delegate survives replacing the list view");
    const int before=captures;for(int i=0;i<6;++i){dialog.move(dialog.pos()+QPoint(2,1));dialog.resize(dialog.size()+QSize(1,1));dialog.grab();}
    check(captures==before && dialog.property("liquidDialogMaterialBuilds").toInt()==1,"moving resizing and repaint reuse opening material");
    combo->showPopup();settle(60);auto *popup=combo->view()->window();
    check(popup->property("liquidComboMaterialBuilds").toInt()==1,"combo computes once per opening");
    QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier),enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(combo->view(),&down);QApplication::sendEvent(combo->view(),&enter);settle(30);
    check(combo->currentIndex()==1 && !popup->isVisible(),"native combo keyboard selection preserved");
    check(popup->property("liquidComboMaterialBytes").toULongLong()==0,"hidden combo frees material");
    Paints paints;dialog.installEventFilter(&paints);settle(100);paints.count=0;settle(180);
    check(paints.count==0,"stationary surface stops painting after finite motion");
    dialog.hide();check(dialog.property("liquidDialogMaterialBytes").toULongLong()==0,"hidden dialog frees cached surface");
    dialog.show();settle(30);check(dialog.property("liquidDialogMaterialBuilds").toInt()==2,"reopening gets a fresh backdrop");dialog.hide();
    check(layout->contentsMargins().top()==topMargin,"reopening never accumulates title bar padding");
    dialog.show();settle(30);
    QMouseEvent titleDoubleClick(QEvent::MouseButtonDblClick,header->rect().center(),
        header->mapToGlobal(header->rect().center()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(header,&titleDoubleClick);
    check(!dialog.isMaximized(),"title double click cannot maximize an owned popup");
    const auto restoredPosition=dialog.pos();
    NativeVisibility visibility;dialog.installEventFilter(&visibility);
    const int restoredBuilds=dialog.property("liquidDialogMaterialBuilds").toInt();
    const int restoredCaptures=captures;
    dialog.findChild<QToolButton *>("liquidDialogMinimize")->click();settle(30);
    check(dialog.isMinimized(),"title minimize button works");
    check(dialog.property("liquidDialogMaterialBytes").toULongLong()>0,
        "native minimization retains the finite opening material");
    LiquidDialog::reopen(&dialog);settle(30);
    check(!dialog.isMinimized() && dialog.pos()==restoredPosition,"restoring a minimized window keeps its position");
    check(dialog.property("liquidDialogMaterialBuilds").toInt()==restoredBuilds && captures==restoredCaptures,
        "native restoration does not capture an already-mapped dialog into its own glass");
    check(visibility.restored && visibility.restored->spontaneous(),"native restoration sends a spontaneous show event");
    dialog.removeEventFilter(&visibility);
    {
        QWidget foreground;foreground.show();foreground.activateWindow();settle(30);
        check(QApplication::activeWindow()==&foreground,"unrelated foreground window is active");
        // Re-deliver a real native restore event without changing window state;
        // QApplication::notify preserves its spontaneous bit.
        app.notify(&dialog,visibility.restored.get());settle(30);
        check(QApplication::activeWindow()==&foreground,
            "window-system show events preserve the selected foreground window");
    }
    dialog.hide();
    {
        QWidget owner(nullptr,Qt::Window|Qt::WindowStaysOnTopHint);owner.show();
        QPixmap ownerPixmap(32,32);ownerPixmap.fill(QColor("#5bacdc"));owner.setWindowIcon(QIcon(ownerPixmap));
        QWidget page(&owner);
        auto *question=LiquidDialog::createMessage(&page,"层级与按钮容器","透明按钮区测试",QMessageBox::Question);
        auto *box=question->findChild<QDialogButtonBox *>();
        QPalette opaque=box->palette();opaque.setColor(QPalette::Window,Qt::black);
        box->setPalette(opaque);box->setAutoFillBackground(true);
        question->show();settle(40);
        check(question->windowFlags().testFlag(Qt::WindowStaysOnTopHint)
            && question->windowType()==Qt::Window,"normal dialog inherits owner top layer through a settings page");
        check(question->windowIcon().cacheKey()==owner.windowIcon().cacheKey(),
            "page-owned dialogs inherit the owning task window's icon");
        check(!box->autoFillBackground(),"button container cannot auto-fill an opaque plate");
        const auto image=question->grab().toImage();
        QImage source(image.size(),QImage::Format_RGB32);source.fill(QColor(40,85,125));
        source.setDevicePixelRatio(question->devicePixelRatioF());
        const auto reference=LiquidPopup::renderMenuMaterial(source,question->size(),question->devicePixelRatioF(),false);
        const QPoint probe=box->mapTo(question,QPoint(6,box->height()/2))*question->devicePixelRatioF();
        const auto actual=image.pixelColor(probe),expected=reference.pixelColor(probe);
        check(qAbs(actual.red()-expected.red())<8 && qAbs(actual.green()-expected.green())<8
            && qAbs(actual.blue()-expected.blue())<8,"empty button-row pixels show glass rather than a black rectangle");
        question->grab().save("artifacts/confirmation-transparent-"+requestedStyle+".png");
        delete question;
    }
    {
        QWidget owner;owner.show();QWidget page(&owner);
        LiquidDialog::Dialog child(&page);
        QPixmap ownPixmap(32,32);ownPixmap.fill(QColor("#e77785"));const QIcon ownIcon(ownPixmap);
        child.setWindowIcon(ownIcon);child.show();settle(30);
        check(!child.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
            "ordinary task owners keep children in the normal window layer");
        check(child.windowIcon().cacheKey()==ownIcon.cacheKey(),"explicit dialog icon remains authoritative");
    }
    {
        QWidget alreadyFramed;auto *body=new QVBoxLayout(&alreadyFramed);
        const int originalMargin=body->contentsMargins().top();
        LiquidDialog::installMotion(&alreadyFramed);alreadyFramed.show();settle(20);
        check(!alreadyFramed.findChild<QWidget *>("liquidDialogTitlebar")
            && body->contentsMargins().top()==originalMargin,"motion-only hosts retain their own title bar");
    }
    QTimer::singleShot(20,[]{auto *w=QApplication::activeModalWidget();check(w,"modal confirmation");
        check(choice(w,QMessageBox::No)->isDefault(),"destructive question defaults to No");
        QKeyEvent e(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(w,&e);
    });
    check(LiquidDialog::question(nullptr,"确认删除","<b>文件名</b>\n只供测试")==QMessageBox::No,"Escape returns No without confirming deletion");
    QTimer::singleShot(20,[]{
        auto *w=QApplication::activeModalWidget();checkChrome(w);
        w->findChild<QToolButton *>("liquidDialogClose")->click();
    });
    check(LiquidDialog::question(nullptr,"标题关闭测试","关闭保留安全取消")==QMessageBox::No,"title close returns No without confirming deletion");
    QTimer::singleShot(20,[]{auto *w=QApplication::activeModalWidget();check(w,"question shown");choice(w,QMessageBox::Yes)->click();});
    check(LiquidDialog::question(nullptr,"确认","测试确认",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes,"clicked Yes returns the exact standard result");
    bool accepted=false;
    QTimer::singleShot(20,[]{auto *w=QApplication::activeModalWidget();w->findChild<QLineEdit *>("liquidInputText")->setText("中文 % 新名字");qobject_cast<QDialog *>(w)->accept();});
    check(LiquidDialog::getText(nullptr,"名称","输入",QLineEdit::Normal,"旧名称",&accepted)=="中文 % 新名字" && accepted,"text entry preserves exact accepted value");
    QTimer::singleShot(20,[]{qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject();});
    check(LiquidDialog::getText(nullptr,"名称","输入",QLineEdit::Normal,"旧名称",&accepted).isEmpty() && !accepted,"input cancellation preserved");
    {
        QProgressDialog progress("正在处理测试项目","取消后续项目",0,0);LiquidDialog::install(&progress);
        progress.setMinimumDuration(0);progress.show();settle(100);
        auto *barHeader=checkChrome(&progress);
        auto *label=progress.findChild<QLabel *>(QString(),Qt::FindDirectChildrenOnly);
        auto *bar=progress.findChild<QProgressBar *>();
        check(label && bar && label->mapTo(&progress,label->contentsRect().topLeft()).y()>barHeader->geometry().bottom()
            && bar->y()>barHeader->geometry().bottom(),"Qt progress text and bar stay below the shared header");
        progress.resize(100,50);settle(20);
        check(bar->y()>barHeader->geometry().bottom(),"progress minimum size prevents frame overlap");
        check(progress.property("liquidDialogMaterialBuilds").toInt()==1,"progress uses one shared cached surface");
        progress.grab().save("artifacts/unified-progress.png");
        bool canceled=false;QObject::connect(&progress,&QProgressDialog::canceled,[&]{canceled=true;});
        progress.findChild<QToolButton *>("liquidDialogClose")->click();
        check(progress.wasCanceled() && progress.property("liquidDialogMaterialBytes").toULongLong()==0,"progress cancellation and surface release preserved");
        check(canceled,"title close emits the native progress canceled signal");
    }
    const QString fileName=home.path()+"/文件.txt";QFile file(fileName);check(file.open(QIODevice::WriteOnly),"fixture file");file.write("fixture");file.close();
    QTimer::singleShot(80,[&]{auto *picker=qobject_cast<QFileDialog *>(QApplication::activeModalWidget());check(picker && picker->testOption(QFileDialog::DontUseNativeDialog),"owned Qt file picker installed");auto *header=checkChrome(picker);check(picker->layout()->contentsMargins().top()>header->geometry().bottom(),"Qt file grid reserves header space");picker->selectFile(fileName);picker->grab().save("artifacts/unified-file.png");QMetaObject::invokeMethod(picker,"accept");});
    check(LiquidDialog::getOpenFileName(nullptr,"文件",home.path(),"文本 (*.txt)")==fileName,"file selection returns actual path");
    QTimer::singleShot(80,[]{qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject();});
    check(LiquidDialog::getSaveFileName(nullptr,"保存",home.path()).isEmpty(),"cancel save does not produce a target");
    {
        LiquidDialog::FileDialog save(nullptr,"覆盖测试",home.path());save.setOption(QFileDialog::DontUseNativeDialog);
        save.setAcceptMode(QFileDialog::AcceptSave);save.selectFile(fileName);LiquidDialog::install(&save);save.show();settle(80);
        QTimer::singleShot(30,[]{auto *w=QApplication::activeModalWidget();check(w && w->objectName()=="liquidMessageDialog","overwrite uses common confirmation");choice(w,QMessageBox::No)->click();});
        save.accept();check(save.isVisible(),"declining overwrite leaves the picker available");
        QTimer::singleShot(30,[]{choice(QApplication::activeModalWidget(),QMessageBox::Yes)->click();});
        save.accept();check(!save.isVisible() && save.result()==QDialog::Accepted,"confirming overwrite returns Accepted");
        QFile unchanged(fileName);unchanged.open(QIODevice::ReadOnly);check(unchanged.readAll()=="fixture","picker never writes the actual file");
    }
    QTimer::singleShot(80,[]{auto *picker=qobject_cast<QColorDialog *>(QApplication::activeModalWidget());check(picker,"color picker");auto *header=checkChrome(picker);check(picker->layout()->contentsMargins().top()>header->geometry().bottom(),"Qt color layout reserves header space");picker->setCurrentColor(QColor("#36a3c1"));picker->grab().save("artifacts/unified-color.png");QMetaObject::invokeMethod(picker,"accept");});
    check(LiquidDialog::getColor(Qt::red).name()=="#36a3c1","color selection preserved");
    QTimer::singleShot(80,[]{qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject();});
    check(!LiquidDialog::getColor(Qt::red).isValid(),"cancel color preserves invalid result");
    QMenu menu;auto *a=menu.addAction("第一项");auto *b=menu.addAction("第二项");auto *c=menu.addAction("第三项");menu.popup(QPoint(40,40));settle(60);
    int hoverCount=0;QObject::connect(&menu,&QMenu::hovered,[&](QAction *){++hoverCount;});
    for(int i=0;i<250;++i){QPoint p=menu.actionGeometry(i%2?a:b).center();QMouseEvent e(QEvent::MouseMove,p,menu.mapToGlobal(p),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&menu,&e);}
    QPoint last=menu.actionGeometry(c).center();QMouseEvent finalMove(QEvent::MouseMove,last,menu.mapToGlobal(last),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&menu,&finalMove);
    settle(50);check(menu.activeAction()==c && hoverCount<30,"hover burst merges while preserving the final row");
    bool correct=false;QObject::connect(b,&QAction::triggered,[&]{correct=true;});
    last=menu.actionGeometry(b).center();QMouseEvent pending(QEvent::MouseMove,last,menu.mapToGlobal(last),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&menu,&pending);
    QMouseEvent press(QEvent::MouseButtonPress,last,menu.mapToGlobal(last),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,last,menu.mapToGlobal(last),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&menu,&press);QApplication::sendEvent(&menu,&release);check(correct,"click flushes pending hover before activating a row");
    LiquidPopup::theme().reducedMotion=false;
    if(QGuiApplication::platformName()=="xcb") {
        for(int expected:{QDialog::Accepted,QDialog::Rejected})for(bool remap:{false,true}) {
            LiquidDialog::Dialog modal;
            int completions=0,completedResult=-1;bool timedOut=false;
            QObject::connect(&modal,&QDialog::finished,&modal,[&](int result){
                ++completions;completedResult=result;
            });
            QTimer watchdog;watchdog.setSingleShot(true);
            QObject::connect(&watchdog,&QTimer::timeout,&modal,[&]{
                timedOut=true;modal.QDialog::done(QDialog::Rejected);
            });
            watchdog.start(1000);
            QTimer::singleShot(30,&modal,[&,expected,remap]{
                modal.done(expected);
                check(modal.property("liquidDialogClosing").toBool(),"modal close has a pending animated result");
                modal.showMinimized();
                if(remap)modal.showNormal();
            });
            const int actual=modal.exec();watchdog.stop();settle(180);
            check(!timedOut && actual==expected && completedResult==expected && completions==1,
                "minimize/remap during closing completes the exact modal result once without blocking");
            check(!modal.isVisible() && !modal.property("liquidDialogClosing").toBool()
                && modal.isEnabled(),"interrupted close releases the dialog and restores its input state");
        }
        QPointer<LiquidDialog::Dialog> fading=LiquidDialog::createMessage(nullptr,"连续操作","动画结束后才释放",QMessageBox::Information);
        fading->setAttribute(Qt::WA_DeleteOnClose);fading->show();settle(240);fading->accept();
        check(fading && fading->property("liquidDialogClosing").toBool(),"close animation begins");LiquidDialog::reopen(fading);settle(200);
        check(fading && fading->isVisible() && !fading->property("liquidDialogClosing").toBool(),"rapid reopen cancels pending deletion");
        fading->accept();fading->reject();settle(200);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        check(!fading,"repeated close completes and deletes only once");
        auto *parent=new QWidget;auto *child=new LiquidDialog::Dialog(parent);child->show();child->accept();delete parent;settle(200);
    }
    qInfo()<<"Unified dialogs, optics parity and hover regression checks passed";return 0;
}
