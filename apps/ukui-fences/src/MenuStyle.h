#pragma once

#include <QMenu>
#include <QString>

namespace MenuStyle {

inline QString venturaContextMenuStyleSheet()
{
    return QStringLiteral(
        "QMenu {"
        "  background-color: rgb(30,30,30);"
        "  color: rgba(241,245,249,245);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 12px;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  background-color: transparent;"
        "  padding: 4px 24px 4px 12px;"
        "  border-radius: 5px;"
        "  min-height: 16px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: rgba(59,130,246,217);"
        "  color: white;"
        "}"
        "QMenu::item:disabled {"
        "  color: rgba(241,245,249,90);"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background-color: rgba(255,255,255,20);"
        "  margin: 3px 6px;"
        "}"
        "QMenu::indicator {"
        "  width: 14px;"
        "  height: 14px;"
        "}"
    );
}

inline void applyVenturaContextMenu(QMenu *menu)
{
    if (!menu) {
        return;
    }

    menu->setStyleSheet(venturaContextMenuStyleSheet());
}

} // namespace MenuStyle
