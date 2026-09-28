#include "RecentFiles.h"

#include <QFile>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QDebug>

QList<RecentFileEntry> RecentFiles::recent(int limit)
{
    QList<RecentFileEntry> entries;

    const QString xbelPath = QDir::homePath() + "/.local/share/recently-used.xbel";
    QFile file(xbelPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entries;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("bookmark")) {
            RecentFileEntry entry;
            const QXmlStreamAttributes attrs = xml.attributes();
            entry.path = attrs.value("href").toString();
            if (entry.path.startsWith("file://"))
                entry.path = entry.path.mid(7);

            QDateTime modTime;
            while (!xml.atEnd() && !xml.hasError() && !(xml.isEndElement() && xml.name() == QStringLiteral("bookmark"))) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == QStringLiteral("mime-type")) {
                        entry.mimeType = xml.readElementText();
                    } else if (xml.name() == QStringLiteral("modified")) {
                        modTime = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
                    }
                }
            }

            if (entry.path.isEmpty()) continue;
            if (!QFile::exists(entry.path)) continue;

            QFileInfo fi(entry.path);
            entry.name = fi.fileName();
            entry.timeAgo = timeAgoString(modTime);

            entries.append(entry);

            if (entries.size() >= limit)
                break;
        }
    }

    file.close();
    return entries;
}

QString RecentFiles::timeAgoString(const QDateTime &dt)
{
    if (!dt.isValid())
        return QString::fromUtf8("\xE8\xBE\x83\xE6\x97\xA9");

    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QString::fromUtf8("\xE5\x88\x9A\xE5\x88\x9A");
    if (secs < 3600)
        return QString::fromUtf8("%1\xE5\x88\x86\xE9\x92\x9F\xE5\x89\x8D").arg(secs / 60);
    if (secs < 86400)
        return QString::fromUtf8("%1\xE5\xB0\x8F\xE6\x97\xB6\xE5\x89\x8D").arg(secs / 3600);
    if (secs < 604800)
        return QString::fromUtf8("\xE6\x98\xA8\xE5\xA4\xA9");
    return QString::fromUtf8("\xE6\x9B\xB4\xE6\x97\xA9");
}
