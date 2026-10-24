#pragma once
#include "DesktopIcon.h"
#include <QProcess>
#include <QScreen>
#include <QPushButton>
#include <QClipboard>
#include <QKeyEvent>
#include <QMessageBox>
#include <QLineEdit>

static int runExternalDragTest(const QString &root)
{
    QSettings settings;
    for (const QString &name : {QString("systemMonitor"), QString("clockWidget"), QString("activityWidget"), QString("musicWidget"), QString("calendarWidget")})
        settings.setValue(name + "/autoStart", false);
    settings.sync();
    const QString desktop=root+"/desktop", destination=root+"/destination";
    QDir().mkpath(desktop); QDir().mkpath(destination);
    qputenv("XDG_DESKTOP_DIR", desktop.toUtf8());
    DesktopCanvas canvas;
    settle(1500);
    auto *fence=canvas.createFence("drag-source",QRect(50,180,400,350));
    fence->hide();
    QProcess peony;
    peony.setProcessChannelMode(QProcess::MergedChannels);
    peony.start("/usr/bin/peony", {destination});
    settle(2200);
    QProcess search;
    search.start("xdotool", {"search","--onlyvisible","--pid",QString::number(peony.processId())});
    search.waitForFinished(3000);
    const QStringList windows=QString::fromUtf8(search.readAllStandardOutput()).trimmed().split('\n',QString::SkipEmptyParts);
    check(!windows.isEmpty(),"isolated real Peony window is visible");
    if(windows.isEmpty()) {qInfo()<<peony.readAll();peony.kill();peony.waitForFinished();return 1;}
    const QString window=windows.last();
    const qreal dpr=canvas.devicePixelRatioF();
    auto physical=[&](int n){return QString::number(qRound(n*dpr));};
    check(QProcess::execute("xdotool", {"windowmove",window,physical(600),physical(100),
        "windowsize",window,physical(780),physical(650),"windowraise",window})==0,"Peony target geometry set");
    settle(300);
    auto read=[](const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
    for(bool inside:{false,true}) for(bool copy:{false,true}) {
        const QString prefix=QString("%1-%2").arg(inside?"fence":"desktop").arg(copy?"copy":"move");
        const QString source=desktop+"/"+prefix+" 中文 % #.txt";
        const QString folder=desktop+"/"+prefix+" folder";
        QFile file(source);file.open(QIODevice::WriteOnly);file.write("external drag payload");file.close();
        if(inside){
            QDir().mkpath(folder+"/nested");QFile child(folder+"/nested/content");child.open(QIODevice::WriteOnly);child.write("nested payload");child.close();
            fence->show();fence->addItem(DesktopItem::fromPath(source));fence->addItem(DesktopItem::fromPath(folder));
        }
        canvas.refreshAll();settle(1400);
        DesktopIcon *icon=nullptr;
        for(auto *candidate:(inside?fence->findChildren<DesktopIcon *>():canvas.findChildren<DesktopIcon *>(QString(),Qt::FindDirectChildrenOnly)))
            if(candidate->item().filePath==source)icon=candidate;
        check(icon!=nullptr,"drag source icon exists");if(!icon)continue;
        if(inside){QKeyEvent all(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier);QApplication::sendEvent(fence,&all);}
        else icon->move(480,250);
        settle(100);
        const QPoint start=icon->mapToGlobal(QPoint(35,25));
        QProcess mouse;
        mouse.start("/usr/bin/python3", {"-c",QString(
            "import subprocess,time\ndef x(*a): subprocess.run(['/usr/bin/xdotool']+list(map(str,a)),check=True)\n"
            "try:\n x('mousemove',%1,%2);x('mousedown',1);time.sleep(.15)\n %5\n"
            " for i in range(1,31):\n  x('mousemove',int(%1+(%3-%1)*i/30),int(%2+(%4-%2)*i/30));time.sleep(.035)\n"
            " time.sleep(.3)\nfinally:\n x('mouseup',1);time.sleep(.4);x('keyup','ctrl')\n")
            .arg(qRound(start.x()*dpr)).arg(qRound(start.y()*dpr)).arg(qRound(1050*dpr)).arg(qRound(500*dpr))
            .arg(copy?"x('keydown','ctrl')":"pass")});
        QElapsedTimer timer;timer.start();
        auto transferred=[&]{
            return read(destination+"/"+QFileInfo(source).fileName())=="external drag payload"
                && QFileInfo::exists(source)==copy
                && (!inside || (read(destination+"/"+QFileInfo(folder).fileName()+"/nested/content")=="nested payload" && QFileInfo::exists(folder)==copy));
        };
        while(timer.elapsed()<8000 && (mouse.state()!=QProcess::NotRunning || !transferred()))settle(30);
        check(mouse.state()==QProcess::NotRunning && mouse.exitCode()==0,"native mouse driver completes and releases buttons");
        if(mouse.state()!=QProcess::NotRunning){mouse.kill();mouse.waitForFinished();QProcess::execute("xdotool",{"mouseup","1","keyup","ctrl"});}
        qInfo()<<"External drag case"<<prefix;
        check(read(destination+"/"+QFileInfo(source).fileName())=="external drag payload" && QFileInfo::exists(source)==copy,
            "native drag into real Peony preserves file content and move/copy semantics");
        if(inside)check(read(destination+"/"+QFileInfo(folder).fileName()+"/nested/content")=="nested payload" && QFileInfo::exists(folder)==copy,
            "multi-selection includes complete nested folder");
        settle(1600);
        if(inside) {fence->removeItem(source);fence->removeItem(folder);}
    }
    QDir().mkpath("artifacts/file-interaction");
    QApplication::primaryScreen()->grabWindow(0).save("artifacts/file-interaction/peony.png");
    QFile log("artifacts/file-interaction/peony.log");log.open(QIODevice::WriteOnly);log.write(peony.readAll());
    peony.terminate();peony.waitForFinished(3000);
    return failures?1:0;
}

static int runPermanentDeleteTest(const QString &root)
{
    QSettings settings;
    for(const QString &name:{QString("systemMonitor"),QString("clockWidget"),QString("activityWidget"),QString("musicWidget"),QString("calendarWidget")})
        settings.setValue(name+"/autoStart",false);
    settings.sync();
    const QString desktop=root+"/desktop";
    QDir().mkpath(desktop);qputenv("XDG_DESKTOP_DIR",desktop.toUtf8());
    auto write=[](const QString &path){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write("keep-or-delete")==14;};
    auto key=[](QWidget *widget,int code,Qt::KeyboardModifiers mods){QKeyEvent e(QEvent::KeyPress,code,mods);QApplication::sendEvent(widget,&e);};
    auto wait=[] {QElapsedTimer t;t.start();do{settle(20);}while(FileClipboard::busy()&&t.elapsed()<5000);check(!FileClipboard::busy(),"permanent delete job finished");settle(100);};
    DesktopCanvas canvas;settle(1000);
    auto *fence=canvas.createFence("permanent-delete",QRect(400,150,320,260));
    int answer=QMessageBox::No, confirmations=0;
    QTimer dismiss;
    QObject::connect(&dismiss,&QTimer::timeout,[&]{
        for(auto *widget:QApplication::topLevelWidgets()) {
            if(widget->objectName()!="liquidMessageDialog"||!widget->isVisible()||widget->property("liquidDialogClosing").toBool())continue;
            if(widget->windowTitle()=="永久删除") {
                ++confirmations;
                bool safeDefault=false;
                for(auto *button:widget->findChildren<QPushButton *>())
                    if(button->isDefault() && button->property("messageChoice").toInt()==QMessageBox::No)safeDefault=true;
                check(safeDefault,"permanent delete confirmation defaults to No");
                qobject_cast<QDialog *>(widget)->done(answer);
            } else if(widget->windowTitle()=="移到回收站") qobject_cast<QDialog *>(widget)->done(QMessageBox::Yes);
            else qobject_cast<QDialog *>(widget)->accept();
        }
    });dismiss.start(10);
    for(bool inside:{false,true}) {
        const QString file=desktop+(inside?"/fence-file":"/loose-file");
        const QString dir=file+"-dir",link=file+"-link";
        const QString protectedDir=root+(inside?"/protected-fence":"/protected-loose");
        QDir().mkpath(dir);QDir().mkpath(protectedDir);
        write(file);write(dir+"/nested");write(protectedDir+"/keep");
        QFile::link(protectedDir,link);
        const QStringList paths{file,dir,link};
        if(inside)for(const QString &path:paths)fence->addItem(DesktopItem::fromPath(path));
        canvas.refreshAll();settle(200);
        QWidget *target=inside?static_cast<QWidget *>(fence):static_cast<QWidget *>(&canvas);
        key(target,Qt::Key_A,Qt::ControlModifier);
        QApplication::clipboard()->setText("clipboard must survive");
        const int before=confirmations;
        answer=QMessageBox::No;key(target,Qt::Key_Delete,Qt::ShiftModifier);wait();
        check(confirmations==before+1 && QFileInfo::exists(file),"Shift+Delete asks and cancel preserves files");
        check(QApplication::clipboard()->text()=="clipboard must survive","Shift+Delete does not cut files");
        answer=QMessageBox::Yes;key(target,Qt::Key_Delete,Qt::ShiftModifier);wait();
        check(!QFileInfo::exists(file)&&!QFileInfo::exists(dir)&&!QFileInfo(link).isSymLink(),"confirmed Shift+Delete removes multi-selection including directory and links");
        check(QFileInfo::exists(protectedDir+"/keep"),"permanent deletion never follows directory symlink");
        if(inside)check(!fence->hasItem(file)&&!fence->hasItem(link),"fence removes deleted entries");
    }
    check(QDir(root+"/data/Trash/info").entryList({"*.trashinfo"},QDir::Files).isEmpty(),"permanent deletion creates no trash entries");
    const QString good=desktop+"/partial-good", dangling=root+"/dangling-link";write(good);
    check(QFile::link(root+"/missing",dangling),"dangling link fixture created");
    FileClipboard::PasteResult partial;bool completed=false;
    FileClipboard::deleteFilesPermanentlyAsync({good,dangling,root+"/does-not-exist"},&canvas,[&](const auto &r){partial=r;completed=true;});wait();
    check(completed&&!QFileInfo::exists(good)&&partial.placedPaths==QStringList({good,dangling})&&!QFileInfo(dangling).isSymLink()&&partial.failedPaths.size()==1,"partial permanent deletion reports only failed paths");
    const QString ordinary=desktop+"/ordinary-delete";write(ordinary);
    fence->addItem(DesktopItem::fromPath(ordinary));
    key(fence,Qt::Key_A,Qt::ControlModifier);
    key(fence,Qt::Key_X,Qt::ControlModifier);
    check(FileClipboard::currentCutPaths().contains(ordinary),"Ctrl+X still cuts selected files");
    DesktopIcon *renameIcon=nullptr;
    for(auto *candidate:fence->findChildren<DesktopIcon *>())
        if(candidate->item().filePath==ordinary)renameIcon=candidate;
    check(renameIcon!=nullptr,"rename test icon exists");
    if(renameIcon) {
        renameIcon->startInlineRename();settle(50);
        QLineEdit *edit=nullptr;
        for(auto *candidate:canvas.findChildren<QLineEdit *>())
            if(candidate->isVisible() && candidate->text()==QFileInfo(ordinary).fileName())edit=candidate;
        check(edit && edit->isVisible(),"inline rename is active");
        if(edit) {
            const int count=confirmations;edit->selectAll();key(edit,Qt::Key_Delete,Qt::ShiftModifier);
            check(confirmations==count && QFileInfo::exists(ordinary),"Shift+Delete while editing name remains text editing");
            key(edit,Qt::Key_Escape,Qt::NoModifier);
        }
    }
    key(fence,Qt::Key_Delete,Qt::NoModifier);wait();
    check(!QFileInfo::exists(ordinary),"ordinary Delete moves selected file to trash");
    key(fence,Qt::Key_Z,Qt::ControlModifier);wait();
    check(QFileInfo::exists(ordinary),"Ctrl+Z still restores ordinary Delete");
    return failures?1:0;
}
