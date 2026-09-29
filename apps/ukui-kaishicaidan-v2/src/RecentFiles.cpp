#include <QUrl>
#include <algorithm>
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
    if (limit <= 0) return entries;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/recently-used.xbel");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return entries;
    QList<QPair<QDateTime, RecentFileEntry>> dated;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("bookmark")) continue;
        const auto attrs = xml.attributes();
        const QUrl url(attrs.value("href").toString());
        const QDateTime modified = QDateTime::fromString(attrs.value("modified").toString(), Qt::ISODate);
        RecentFileEntry entry;
        entry.path = url.isLocalFile() ? url.toLocalFile() : QString();
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QStringLiteral("bookmark")) break;
            if (xml.isStartElement() && xml.name() == QStringLiteral("mime-type"))
                entry.mimeType = xml.attributes().value("type").toString();
        }
        if (entry.path.isEmpty() || !QFileInfo::exists(entry.path)) continue;
        entry.name = QFileInfo(entry.path).fileName();
        entry.timeAgo = timeAgoString(modified);
        dated.append(qMakePair(modified, entry));
    }
    if (xml.hasError()) return {};
    std::stable_sort(dated.begin(), dated.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &item : dated) {
        entries << item.second;
        if (entries.size() >= limit) break;
    }
    return entries;
}

QString RecentFiles::timeAgoString(const QDateTime &dt)
{
    if (!dt.isValid()) return QStringLiteral("较早");
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = dt.secsTo(now);
    if (secs < 60) return QStringLiteral("刚刚");
    const qint64 days = dt.toLocalTime().date().daysTo(now.date());
    if (days == 1) return QStringLiteral("昨天");
    if (days > 1) return QStringLiteral("%1天前").arg(days);
    if (secs < 3600) return QStringLiteral("%1分钟前").arg(secs / 60);
    return QStringLiteral("%1小时前").arg(secs / 3600);
}
