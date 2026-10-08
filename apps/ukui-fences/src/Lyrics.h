#pragma once
#include <QString>
#include <QVector>

// LRC lyrics parsed once per metadata revision. A line may carry several
// timestamps ("[00:12.00][01:30.50]chorus"); each becomes its own entry.
namespace Lyrics {
struct Line { qint64 us = 0; QString text; };
// Sorted by time; honours the [offset:+/-ms] tag. Empty when no timed line.
QVector<Line> parse(const QString &lrc);
// Index of the line showing at `us`, or -1 before the first line.
int lineAt(const QVector<Line> &lines, qint64 us);
}
