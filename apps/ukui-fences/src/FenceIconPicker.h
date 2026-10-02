#pragma once
#include <QStringList>

class QWidget;

namespace FenceIconPicker {
struct Choice {
    QString themeName;
    QString path;
    bool accepted = false;
};
QStringList defaultIconPaths();
QString defaultIconLabel(const QString &path);
Choice choose(QWidget *parent, const QString &themeName, const QString &path);
}
