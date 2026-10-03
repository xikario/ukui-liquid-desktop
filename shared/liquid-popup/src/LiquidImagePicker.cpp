#include "LiquidDialog.h"
#include "../../async-work/BackgroundTask.h"
#include <QCache>
#include <QDateTime>
#include <QFileSystemModel>
#include <QImageReader>
#include <QListView>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QQueue>
#include <QSet>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTimer>
#include <QTreeView>

namespace LiquidDialog {
namespace {
// A single decode in flight per picker, a bounded queue, and at most 64 cached
// cards (<=16 MiB at 256 px). No directory-wide scan or timer runs when idle.
class Thumbnails final : public QObject {
public:
    explicit Thumbnails(QFileDialog *picker):QObject(picker),picker(picker),cache(64) {
        setObjectName("liquidImageThumbnails");
        size=QSize(qMin(256,qRound(96*picker->devicePixelRatioF())),
                   qMin(192,qRound(72*picker->devicePixelRatioF())));
        if(auto *model=picker->findChild<QFileSystemModel *>())
            connect(model,&QFileSystemModel::rootPathChanged,this,[this] {
                ++revision;queue.clear();queued.clear();cache.clear();
                this->picker->setProperty("liquidThumbnailCached",0);
            });
    }
    QIcon icon(const QModelIndex &index) {
        const QString path=index.data(QFileSystemModel::FilePathRole).toString();
        const QString suffix=QFileInfo(path).suffix().toLower();
        static const QSet<QString> images={"png","jpg","jpeg","bmp","webp","svg","ico","xpm","gif","tif","tiff"};
        if(path.isEmpty() || !images.contains(suffix))return {};
        if(const auto *hit=cache.object(path))return *hit;
        if(!queued.contains(path) && path!=activePath) {
            if(queue.size()>=48)queued.remove(queue.dequeue().path);
            queue.enqueue({path,QPersistentModelIndex(index)});queued.insert(path);
            if(!scheduled && !busy) {
                scheduled=true;QTimer::singleShot(0,this,[this]{scheduled=false;pump();});
            }
        }
        return {};
    }
private:
    struct Request {QString path;QPersistentModelIndex index;};
    bool visible(const QPersistentModelIndex &index) const {
        if(!picker->isVisible() || !index.isValid())return false;
        for(auto *view:picker->findChildren<QAbstractItemView *>()) {
            if(view->objectName()!="listView" && view->objectName()!="treeView")continue;
            if(view->isVisible() && view->model()==index.model()
                && view->visualRect(index).intersects(view->viewport()->rect()))return true;
        }
        return false;
    }
    void pump() {
        if(busy)return;
        Request request;
        while(!queue.isEmpty()) {
            request=queue.dequeue();queued.remove(request.path);
            if(visible(request.index))break;
            request={};
        }
        if(request.path.isEmpty())return;
        busy=true;activePath=request.path;
        const auto stamp=revision;const auto pixels=size;
        picker->setProperty("liquidThumbnailLoads",picker->property("liquidThumbnailLoads").toInt()+1);
        const bool submitted=BackgroundTask::run(this,[path=request.path,pixels] {
            const QFileInfo info(path);
            // Avoid directories, enormous unscaled images, and oversized files.
            // Readers use scaled decode where supported; other formats have a
            // conservative source pixel cap and never decode on the GUI thread.
            if(!info.isFile() || info.size()>64*1024*1024)return QImage();
            QImageReader reader(path);reader.setAutoTransform(true);
            const QSize source=reader.size();
            if(!source.isValid() || qint64(source.width())*source.height()>40000000)return QImage();
            reader.setScaledSize(source.scaled(pixels,Qt::KeepAspectRatio));
            QImage image=reader.read();if(image.isNull())return QImage();
            image=image.scaled(pixels,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            QImage card(pixels,QImage::Format_ARGB32_Premultiplied);card.fill(Qt::white);
            QPainter painter(&card);
            painter.drawImage(QPoint((pixels.width()-image.width())/2,(pixels.height()-image.height())/2),image);
            return card;
        },[this,request,stamp](const QImage &image) {
            busy=false;activePath.clear();
            if(stamp==revision && request.index.isValid()) {
                const QIcon thumbnail=image.isNull()?QIcon():QIcon(QPixmap::fromImage(image));
                cache.insert(request.path,new QIcon(thumbnail));
                picker->setProperty("liquidThumbnailCached",cache.size());
                for(auto *view:picker->findChildren<QAbstractItemView *>())
                    if(view->model()==request.index.model())view->viewport()->update(view->visualRect(request.index));
            }
            pump();
        });
        if(!submitted){busy=false;activePath.clear();queue.clear();queued.clear();}
    }
    QFileDialog *picker;
    QCache<QString,QIcon> cache;
    QQueue<Request> queue;
    QSet<QString> queued;
    QSize size;
    QString activePath;
    quint64 revision=0;
    bool busy=false,scheduled=false;
};
class ImageDelegate final : public QStyledItemDelegate {
public:
    ImageDelegate(Thumbnails *thumbnails,QObject *parent):QStyledItemDelegate(parent),thumbnails(thumbnails) {}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        QStyleOptionViewItem item(option);initStyleOption(&item,index);
        if(index.column()==0) {
            const auto icon=thumbnails->icon(index);
            if(!icon.isNull())item.icon=icon;
        }
        const auto *widget=item.widget;
        (widget?widget->style():QApplication::style())->drawControl(QStyle::CE_ItemViewItem,&item,p,widget);
    }
private:Thumbnails *thumbnails;
};
}
void installImageThumbnails(QFileDialog *picker) {
    if(!picker || picker->findChild<QObject *>("liquidImageThumbnails"))return;
    picker->setOption(QFileDialog::DontUseNativeDialog,true);
    picker->setViewMode(QFileDialog::List);
    auto *thumbnails=new Thumbnails(picker);
    for(auto *view:picker->findChildren<QAbstractItemView *>()) {
        if(view->objectName()!="listView" && view->objectName()!="treeView")continue;
        view->setItemDelegate(new ImageDelegate(thumbnails,view));
        view->setIconSize(QSize(96,72));
        if(auto *list=qobject_cast<QListView *>(view)) {
            list->setViewMode(QListView::IconMode);list->setMovement(QListView::Static);
            list->setResizeMode(QListView::Adjust);list->setUniformItemSizes(true);
            list->setGridSize(QSize(148,112));list->setWordWrap(true);
            list->setTextElideMode(Qt::ElideRight);
            list->setLayoutMode(QListView::Batched);list->setBatchSize(64);
        }
    }
    picker->resize(860,620);
}
}
