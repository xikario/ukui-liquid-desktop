#pragma once
#include "LiquidIconRenderer.h"
#include <QSettings>

struct IconAppearance {
    IconVisualStyle style = IconVisualStyle::LiquidPlate;
    int strength = 62;
    bool wallpaperTint = true;
    bool fencePlates = false;
    static IconAppearance load() {
        QSettings s; IconAppearance a;
        a.style = s.value("appearance/desktopIconStyle","liquid").toString()=="native"
            ? IconVisualStyle::Native : IconVisualStyle::LiquidPlate;
        a.strength=qBound(0,s.value("appearance/desktopIconGlassStrength",62).toInt(),100);
        a.wallpaperTint=s.value("appearance/desktopIconTintMode","wallpaper").toString()=="wallpaper";
        a.fencePlates=s.value("appearance/fenceIconLiquidPlate",false).toBool();
        return a;
    }
    void save() const {
        QSettings s;
        s.setValue("appearance/desktopIconStyle",style==IconVisualStyle::Native?"native":"liquid");
        s.setValue("appearance/desktopIconGlassStrength",strength);
        s.setValue("appearance/desktopIconTintMode",wallpaperTint?"wallpaper":"neutral");
        s.setValue("appearance/fenceIconLiquidPlate",fencePlates); s.sync();
    }
};
