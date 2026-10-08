#include "Lyrics.h"
#include <QRegularExpression>
#include <algorithm>

namespace Lyrics {
QVector<Line> parse(const QString &lrc)
{
    static const QRegularExpression stamp(QStringLiteral("^\\[(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]"));
    static const QRegularExpression offsetTag(QStringLiteral("^\\[offset:\\s*([+-]?\\d+)\\s*\\]"),
                                              QRegularExpression::CaseInsensitiveOption);
    QVector<Line> lines;
    qint64 offsetMs = 0;
    const QStringList rows = lrc.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    for (const QString &row : rows) {
        QString rest = row.trimmed();
        const auto offset = offsetTag.match(rest);
        if (offset.hasMatch()) {
            offsetMs = offset.captured(1).toLongLong();
            continue;
        }
        QVector<qint64> times;
        for (auto m = stamp.match(rest); m.hasMatch(); m = stamp.match(rest)) {
            const QString fraction = m.captured(3);
            // "xx" is hundredths, "xxx" milliseconds, "x" tenths.
            const qint64 ms = fraction.isEmpty() ? 0 : fraction.leftJustified(3, QLatin1Char('0')).toLongLong();
            times.append((m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 1000 + ms);
            rest = rest.mid(m.capturedLength());
        }
        const QString text = rest.trimmed();
        for (const qint64 ms : qAsConst(times))
            lines.append({qMax<qint64>(0, ms - offsetMs) * 1000, text});
    }
    // Stable: equal timestamps keep file order.
    std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.us < b.us; });
    return lines;
}

int lineAt(const QVector<Line> &lines, qint64 us)
{
    const auto after = std::upper_bound(lines.cbegin(), lines.cend(), us,
                                        [](qint64 value, const Line &line) { return value < line.us; });
    return int(after - lines.cbegin()) - 1;
}
}
