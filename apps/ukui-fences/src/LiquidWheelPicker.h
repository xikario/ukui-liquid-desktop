#pragma once
#include <QString>
#include <functional>
class QWidget;
namespace LiquidWheelPicker {
// Five visible rows regardless of range size; outside click/Escape cancels.
int pick(QWidget *anchor, const QString &name, int current, int minimum, int maximum,
         const std::function<QString(int)> &label,
         const std::function<bool(int)> &enabled = {}, int step = 1);
}
