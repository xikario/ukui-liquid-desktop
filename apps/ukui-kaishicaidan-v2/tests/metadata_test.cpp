#include "RecentFiles.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include "NativeScreenMap.h"
int main(int argc,char **argv) {
    QTemporaryDir root; qputenv("XDG_DATA_HOME",root.path().toUtf8());
    QCoreApplication app(argc,argv);
    const QDateTime now=QDateTime::currentDateTime();
    if(RecentFiles::timeAgoString(now.addDays(-3))!=QStringLiteral("3天前") || RecentFiles::timeAgoString(now.addDays(-1))!=QStringLiteral("昨天") || RecentFiles::timeAgoString(now.addSecs(60))!=QStringLiteral("刚刚"))return 1;
    QStringList paths; for(const QString &name:{QStringLiteral("旧.txt"),QStringLiteral("新 文件.txt")}) { QString path=root.path()+"/"+name;QFile f(path);if(!f.open(QIODevice::WriteOnly))return 2;f.write("test");paths<<path; }
    QFile xml(root.path()+"/recently-used.xbel");if(!xml.open(QIODevice::WriteOnly))return 3;
    QByteArray text="<xbel xmlns:mime='http://www.freedesktop.org/standards/shared-mime-info'>";
    for(int i=0;i<2;++i)text+="<bookmark href='"+QUrl::fromLocalFile(paths[i]).toEncoded()+"' modified='"+now.addDays(i-3).toString(Qt::ISODate).toUtf8()+"'><info><metadata><mime:mime-type type='text/plain'/></metadata></info></bookmark>";
    xml.write(text+"</xbel>");xml.close();const auto recent=RecentFiles::recent(1);
    if(recent.size()!=1 || recent[0].path!=paths[1] || recent[0].mimeType!="text/plain" || !RecentFiles::recent(0).isEmpty())return 4;
    NativeScreenMap::Screen second{QRect(1920,0,2560,1440),QRect(1920,0,1280,720),2};
    if(NativeScreenMap::toLogical(QRect(2120,1340,1000,100),second)!=QRect(2020,670,500,50))return 5;
    NativeScreenMap::Screen negative{QRect(-1920,0,1920,1080),QRect(-1280,0,1280,720),1.5};
    if(NativeScreenMap::toLogical(QRect(-1920,1005,1920,75),negative)!=QRect(-1280,670,1280,50))return 6;
    qInfo()<<"PASS recent metadata, encoded paths, limits, local dates and mixed-screen mapping";return 0;
}
