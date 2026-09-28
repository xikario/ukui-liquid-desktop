#pragma once

#include <QString>
#include <QList>
#include <QDateTime>

struct RecentFileEntry {
    QString name;
    QString path;
    QString mimeType;
    QString timeAgo;
};

class RecentFiles {
public:
    static QList<RecentFileEntry> recent(int limit = 4);
    static QString timeAgoString(const QDateTime &dt);
};
