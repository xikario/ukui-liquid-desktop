#pragma once
#include <QWidget>

namespace LiquidButtons {
// Opt in owned glass hosts only. Buttons retain Qt's input, layout, menus and
// accessibility; their rim/fill is painted once with antialiasing. New children
// are adopted when polished/shown, without polling or an application filter.
// liquidButtonFlat preserves borderless cover/action controls.
// liquidButtonGlyph="close"/"minimize" uses a font-independent vector glyph.
void install(QWidget *root);
}
