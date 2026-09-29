#pragma once
#include "FileClipboard.h"
#include <QElapsedTimer>
#include <QStandardPaths>

static int runClipboardTest(const QString &root)
{
    const auto write=[](const QString &path,const QByteArray &data) {
        QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;return f.write(data)==data.size();
    };
    const QString bin=root+"/bin",source=root+"/source",dest=root+"/dest";
    QDir().mkpath(bin);QDir().mkpath(source);QDir().mkpath(dest);
    const QByteArray oldPath=qgetenv("PATH");
    const QString cp=QStandardPaths::findExecutable("cp");
    check(!cp.isEmpty(),"copy backend available");
    check(write(bin+"/cp",("#!/bin/sh\nsleep 0.3\nexec "+cp+" \"$@\"\n").toUtf8()),"slow copy fixture created");
    QFile::setPermissions(bin+"/cp",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    check(write(bin+"/gio","#!/bin/sh\nexit 1\n"),"unavailable move backend fixture created");
    QFile::setPermissions(bin+"/gio",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    qputenv("PATH",bin.toUtf8()+":"+oldPath);
    const QString file=source+"/test.txt";check(write(file,"original payload"),"copy fixture created");
    QWidget owner;int ticks=0;bool completed=false;FileClipboard::PasteResult result;
    QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,[&]{++ticks;});heartbeat.start(20);
    FileClipboard::writeFiles({file},false);
    check(FileClipboard::pasteFilesToDirectoryAsync(dest,&owner,[&](const auto &r){result=r;completed=true;}),"asynchronous paste accepted");
    check(!FileClipboard::pasteFilesToDirectoryAsync(dest,&owner,[](const auto &){}),"concurrent paste serialized");
    FileClipboard::writeFiles({root+"/new-selection"},false);
    QElapsedTimer elapsed;elapsed.start();while(!completed && elapsed.elapsed()<5000)settle(20);
    check(completed && result.failedPaths.isEmpty() && result.placedPaths.size()==1,"asynchronous paste completes");
    check(ticks>=5,"event loop stays responsive during copy");
    check(FileClipboard::readFiles().paths==QStringList{root+"/new-selection"},"newer clipboard survives completion");
    check(QFileInfo::exists(file) && QFileInfo::exists(dest+"/test.txt"),"copy preserves original");
    check(!FileClipboard::transferPath(file,dest+"/test.txt",false),"existing target is never overwritten");
    QDir().mkpath(source+"/folder");write(source+"/folder/kept.txt","kept");
    check(!FileClipboard::transferPath(source+"/folder",source+"/folder/nested",false),"recursive self-copy rejected");
    QTemporaryDir external("/dev/shm/fences-transfer-XXXXXX");
    if(external.isValid()) {
        check(FileClipboard::transferPath(source+"/folder",external.path()+"/moved",true),"move fallback copies directory across filesystems");
        check(!QFileInfo::exists(source+"/folder") && QFileInfo::exists(external.path()+"/moved/kept.txt"),"source removed only after successful staged copy");
    }
    write(bin+"/cp","#!/bin/sh\nexit 1\n");
    check(!FileClipboard::transferPath(file,dest+"/failed",false) && QFileInfo::exists(file) && !QFileInfo::exists(dest+"/failed"),"copy failure retains source without partial destination");
    qputenv("PATH",oldPath);return failures?1:0;
}
