#pragma once
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QPropertyAnimation>
#include <QMimeData>
#include <QUrl>

static int runFenceReorderTest(const QString &root)
{
    QWidget host;host.resize(900,700);host.show();
    FenceWidget fence("Reorder",QRect(20,20,420,500),&host);fence.show();
    QStringList paths;
    for(int i=0;i<7;++i) {
        const QString path=root+QString("/folder-%1").arg(i);QDir().mkpath(path);
        QFile payload(path+"/payload.txt");payload.open(QIODevice::WriteOnly);payload.write("keep");payload.close();
        paths.append(path);fence.addItem(DesktopItem::fromPath(path));
    }
    settle(100);
    auto order=[&]{QStringList result;for(const auto &item:fence.items())result.append(item.filePath);return result;};
    auto mimeFor=[](const QStringList &paths,QMimeData &mime){
        QList<QUrl> urls;for(const auto &path:paths)urls.append(QUrl::fromLocalFile(path));
        mime.setUrls(urls);mime.setData("application/x-ukui-fences-file-drag","1");
    };
    int saves=0;QObject::connect(&fence,&FenceWidget::geometryChanged,&host,[&]{++saves;});
    auto *first=fence.icons().first();auto *last=fence.icons().last();
    const QPoint firstOriginal=first->pos();const QPoint firstPoint=first->mapTo(&fence,QPoint(2,20));
    QMimeData mime;mimeFor({paths.last()},mime);
    QDragEnterEvent enter(firstPoint,Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&fence,&enter);
    settle(70);
    check(first->pos()==firstOriginal && order()==paths,"brief hover does not reorder or persist");
    settle(150);
    auto *anim=first->findChild<QPropertyAnimation *>("fenceReorderAnimation");
    check(anim && anim->state()==QAbstractAnimation::Running,"confirmed hover animates displaced icons");
    settle(180);
    check(first->pos()!=firstOriginal && !last->isVisible(),"preview leaves a gap for the dragged icon");
    check(order()==paths && saves==0 && !FileClipboard::busy(),"hover preview never mutates saved order or files");
    fence.cancelIconReorder();settle(200);
    check(order()==paths && first->pos()==firstOriginal && last->isVisible() && saves==0,
          "cancel restores visual order without saving");

    // Route through a folder icon: same-fence sorting must beat folder transfer.
    QDragEnterEvent iconEnter(QPoint(2,20),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(first,&iconEnter);
    QDropEvent drop(QPointF(2,20),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(first,&drop);settle(200);
    QStringList expected=paths;expected.move(6,0);
    check(drop.isAccepted() && order()==expected && saves==1,"folder-target drop commits insertion once");
    check(!FileClipboard::busy() && QFileInfo::exists(paths.last()+"/payload.txt")
        && !QFileInfo::exists(paths.first()+"/folder-6"),"same-fence folder reorder does not move directory contents");

    auto *self=fence.icons().first();const int saved=saves;
    QDragEnterEvent selfEnter(QPoint(2,20),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(self,&selfEnter);
    QDropEvent selfDrop(QPointF(2,20),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(self,&selfDrop);settle(50);
    check(selfDrop.isAccepted() && saves==saved && order()==expected && !FileClipboard::busy(),
          "same-position drop is a silent no-op");

    QMimeData multiple;mimeFor({expected[0],expected[1]},multiple);
    fence.commitIconReorder(&multiple,QPoint(fence.width()-2,fence.height()-2));settle(200);
    QStringList moved=expected.mid(2);moved<<expected[0]<<expected[1];
    check(order()==moved,"multi-selection moves to end preserving relative order across rows");
    // Previously decrementing an already adjusted index misplaced adjacent selections.
    fence.commitIconReorder(&multiple,QPoint(0,0));settle(200);
    check(order()==expected,"multi-selection moves back to front without index drift");
    fence.previewIconReorder(&mime,QPoint(0,0));fence.removeItem(paths[3]);settle(400);
    check(!order().contains(paths[3]),"file removal cancels pending preview without stale icon pointers");

    DesktopIcon loose(DesktopItem::fromPath(paths.last()),&host);loose.show();
    QDragEnterEvent looseEnter(QPoint(2,20),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&loose,&looseEnter);
    QDropEvent looseDrop(QPointF(2,20),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&loose,&looseDrop);
    check(looseDrop.isAccepted() && !FileClipboard::busy(),"loose folder dropped onto itself starts no file job or warning");
    for(const auto &path:paths)check(QFileInfo::exists(path+"/payload.txt"),"all original folder payloads preserved");
    loose.hide(); // Do not cover the first slot during the native drag test.
    // Exercise a real QDrag event loop too: synthetic drop events have no source.
    const QString xdotool=QStandardPaths::findExecutable("xdotool");
    check(!xdotool.isEmpty(),"native reorder regression requires xdotool");
    if(!xdotool.isEmpty()) {
        auto nativeMove=[&](QPoint point){
            const qreal dpr=host.devicePixelRatioF();
            QProcess::execute(xdotool,{"mousemove",QString::number(qRound(point.x()*dpr)),QString::number(qRound(point.y()*dpr))});
        };
        for(int mode:{0,1,2}) {
            const bool cancel=mode!=1;
            const auto before=order();auto *source=fence.icons().last();auto *target=fence.icons().first();
            const QPoint start=source->mapToGlobal(QPoint(source->width()/2,20));
            const QPoint end=target->mapToGlobal(QPoint(3,20));
            nativeMove(start);QProcess::execute(xdotool,{"mousedown","1"});
            QTimer::singleShot(80,&host,[&,start]{nativeMove(start+QPoint(25,0));});
            QTimer::singleShot(230,&host,[&,end]{nativeMove(end);});
            QTimer::singleShot(330,&host,[&,end]{nativeMove(end+QPoint(1,0));});
            bool previewed=false;
            QTimer::singleShot(700,&host,[&]{
                previewed=order()==before && !source->isVisible();
                QDir().mkpath("artifacts");host.grab().save("artifacts/fence-reorder-hover.png");
            });
            if(mode==2)QTimer::singleShot(760,&host,[&]{nativeMove(host.mapToGlobal(QPoint(850,630)));});
            QTimer::singleShot(850,&host,[&,mode]{
                if(mode==0)QProcess::execute(xdotool,{"key","Escape"});
                QProcess::execute(xdotool,{"mouseup","1"});
            });
            settle(1250);
            check(previewed,"native QDrag shows a non-persistent insertion preview");
            auto after=before;if(!cancel)after.move(after.size()-1,0);
            if(order()!=after || !source->isVisible())qInfo()<<"native result"<<cancel<<order()<<after<<source->isVisible();
            check(order()==after && source->isVisible(),cancel
                ? "native Escape or leaving fence restores source icon and original order"
                : "native release inserts last folder at front without a file transfer");
            check(!FileClipboard::busy(),"native reorder does not submit filesystem work");
        }
    }
    QDir().mkpath("artifacts");host.grab().save("artifacts/fence-reorder-final.png");
    return failures==0?0:1;
}
