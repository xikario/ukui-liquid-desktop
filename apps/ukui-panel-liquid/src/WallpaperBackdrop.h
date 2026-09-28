#pragma once
#include <QImage>
#include <QRect>
#include <QString>
class WallpaperBackdrop {
public:
    bool reload();
    QImage sample(const QRect &globalArea,const QRect &screen,qreal dpr) const;
    QString path() const { return m_path; }
private:
    QString m_path;
    qint64 m_modified=0;
    QImage m_source;
    int m_mode=0;
};
