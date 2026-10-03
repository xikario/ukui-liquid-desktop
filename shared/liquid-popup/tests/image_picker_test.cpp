#include "LiquidDialog.h"
#include "../../async-work/BackgroundTask.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QListView>
#include <QTemporaryDir>
#include <QStyleFactory>
#include <QTimer>
#include <QTreeView>
#include <QDebug>
#include <cstdlib>

static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}}
static void settle(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
static bool until(const std::function<bool()> &done,int timeout=6000){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<timeout)settle(10);return done();}
static int colorPixels(const QImage &image,const QColor &color){int n=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){auto c=image.pixelColor(x,y);n+=qAbs(c.red()-color.red())<12&&qAbs(c.green()-color.green())<12&&qAbs(c.blue()-color.blue())<12;}return n;}
int main(int argc,char **argv){
    QTemporaryDir configuration;qputenv("XDG_CONFIG_HOME",configuration.path().toUtf8());
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication app(argc,argv);
    BackgroundTask::ApplicationScope workers;
    app.setStyle(QStyleFactory::create(qEnvironmentVariable("UKUI_IMAGE_TEST_STYLE","Fusion")));
    LiquidPopup::install(app);LiquidPopup::theme().reducedMotion=true;
    QTemporaryDir root;check(root.isValid(),"temporary fixture directory exists");
    const QColor red("#e73524"),blue("#234fe7");
    QImage image(1600,1000,QImage::Format_RGB32);image.fill(red);
    check(image.save(root.path()+"/00-picture.png"),"PNG fixture saved");
    image.fill(blue);check(image.save(root.path()+"/01-picture.jpg"),"JPEG fixture saved");
    QFile svg(root.path()+"/02-icon.svg");svg.open(QIODevice::WriteOnly);
    svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'><circle cx='50' cy='50' r='45' fill='#23bf69'/></svg>");svg.close();
    QFile broken(root.path()+"/03-broken.png");broken.open(QIODevice::WriteOnly);broken.write("invalid image");broken.close();
    for(int i=0;i<180;++i)QFile::copy(root.path()+"/01-picture.jpg",root.path()+QString("/image-%1.jpg").arg(i,3,10,QChar('0')));
    QDir().mkpath(root.path()+"/sub");image.fill(QColor("#cd23cf"));image.save(root.path()+"/sub/only.png");
    LiquidDialog::FileDialog picker(nullptr,"选择图片",root.path(),"图片 (*.png *.jpg *.svg)");
    picker.setOption(QFileDialog::DontUseNativeDialog);picker.setFileMode(QFileDialog::ExistingFile);
    LiquidDialog::install(&picker);LiquidDialog::installImageThumbnails(&picker);
    picker.findChild<QTreeView *>("treeView")->sortByColumn(0,Qt::AscendingOrder);
    LiquidDialog::installImageThumbnails(&picker);
    check(picker.findChildren<QObject *>("liquidImageThumbnails").size()==1,"installation is idempotent");
    picker.show();auto *view=picker.findChild<QListView *>("listView");
    check(view&&view->viewMode()==QListView::IconMode,"image chooser opens as a thumbnail grid");
    check(until([&]{const auto rendered=view->viewport()->grab().toImage();return colorPixels(rendered,red)>300&&colorPixels(rendered,blue)>300&&colorPixels(rendered,QColor("#23bf69"))>200;}),"visible images finish asynchronous decoding");
    auto pixels=view->viewport()->grab().toImage();
    check(colorPixels(pixels,red)>300&&colorPixels(pixels,blue)>300,
          "real PNG and JPEG content replaces generic icons in the view");
    check(colorPixels(pixels,QColor("#23bf69"))>200,"transparent SVG thumbnail renders");
    settle(300);const int loads=picker.property("liquidThumbnailLoads").toInt();
    check(loads<50,"large directory does not eagerly decode every image");
    settle(300);check(picker.property("liquidThumbnailLoads").toInt()==loads,"idle chooser submits no recurring work");
    QDir().mkpath("artifacts");picker.grab().save(QString("artifacts/image-picker-%1.png").arg(picker.devicePixelRatioF()));
    picker.setDirectory(root.path()+"/sub");
    check(until([&]{return colorPixels(view->viewport()->grab().toImage(),QColor("#cd23cf"))>300;}),
          "changing directories discards obsolete work and loads the new image");
    check(picker.property("liquidThumbnailCached").toInt()==1,"previous directory cache is released");
    auto *model=picker.findChild<QFileSystemModel *>();
    view->setCurrentIndex(model->index(root.path()+"/sub/only.png"));
    check(picker.selectedFiles()==QStringList{root.path()+"/sub/only.png"},"selecting a thumbnail supplies the native file path");
    picker.accept();
    check(until([&]{return !picker.isVisible();}),"native accept closes the chooser");
    check(picker.result()==QDialog::Accepted&&picker.selectedFiles().first()==root.path()+"/sub/only.png",
          "thumbnail mode preserves native selected path");
    // Close during a pending decode; completion is disconnected by ownership.
    auto *closing=new LiquidDialog::FileDialog(nullptr,"关闭测试",root.path(),"图片 (*.png *.jpg)");
    LiquidDialog::installImageThumbnails(closing);closing->show();settle(10);delete closing;settle(200);
    QTimer timer;timer.setInterval(10);QObject::connect(&timer,&QTimer::timeout,[&]{
        if(auto *dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))dialog->reject();
    });timer.start();QString filter="图片 (*.png)";
    check(LiquidDialog::getOpenImageName(nullptr,"取消",root.path(),filter,&filter).isEmpty(),"public image helper preserves cancellation");timer.stop();
    LiquidDialog::FileDialog plain(nullptr,"普通文件",root.path());LiquidDialog::install(&plain);
    check(!plain.findChild<QObject *>("liquidImageThumbnails"),"ordinary file dialogs do not decode thumbnails");
    return 0;
}
