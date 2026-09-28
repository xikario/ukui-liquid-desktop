#pragma once
#include <QMouseEvent>

static int runFenceFeaturesTest(const QString &isolated)
{
    QSettings settings;
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    QDir().mkpath(isolated + "/config/kyfences");
    const QString layoutPath = isolated + "/config/kyfences/layout.json";
    QFile fixture(layoutPath);
    fixture.open(QIODevice::WriteOnly);
    fixture.write(QJsonDocument(QJsonObject{{"fences", QJsonArray{
        QJsonObject{{"id","industry"},{"title","行业资料"},{"x",100},{"y",150},
                    {"w",428},{"h",34},{"collapsed",false}},
        QJsonObject{{"id","shaped"},{"title","折叠轮廓"},{"x",700},{"y",150},
                    {"w",320},{"h",34},{"collapsed",true},{"expandedHeight",320},
                    {"magneticEdge",3},{"magneticContour",QJsonArray{
                        QJsonArray{0,60},QJsonArray{160,80},QJsonArray{320,60}}}}
    }}}).toJson());
    fixture.close();
    auto byId = [](DesktopCanvas &canvas, const QString &id) -> FenceWidget * {
        for (auto *f : canvas.findChildren<FenceWidget *>())
            if (f->fenceId() == id) return f;
        return nullptr;
    };
    auto mouse = [](FenceWidget *f, QEvent::Type type, QPoint pos, QPoint global) {
        QMouseEvent e(type, pos, global,
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(f, &e);
    };
    auto click = [&](FenceWidget *f, QPoint pos) {
        mouse(f,QEvent::MouseButtonPress,pos,f->mapToGlobal(pos));
        mouse(f,QEvent::MouseButtonRelease,pos,f->mapToGlobal(pos));
    };
    {
        DesktopCanvas canvas;
        settle(200);
        auto *industry=byId(canvas,"industry");
        auto *shaped=byId(canvas,"shaped");
        if (!industry || !shaped) return 2;
        check(industry->height()==240 && !industry->collapsed(),
              "legacy industry title-only expanded layout recovers a usable body");
        check(shaped->collapsed() && shaped->height()==34 && shaped->mask().isEmpty(),
              "collapsed magnetic fence has a fully clickable title");
        click(shaped,QPoint(160,17)); settle(220);
        check(!shaped->collapsed() && shaped->height()==320,
              "click expands collapsed magnetic fence to persisted height");
        click(industry,QPoint(100,17)); settle(220);
        canvas.setGlobalEditMode(true);
        const QPoint start(2,17), global=industry->mapToGlobal(start);
        mouse(industry,QEvent::MouseButtonPress,start,global);
        mouse(industry,QEvent::MouseMove,QPoint(-38,47),global+QPoint(-40,30));
        mouse(industry,QEvent::MouseButtonRelease,start,global+QPoint(-40,30));
        check(industry->collapsed() && industry->height()==34 && industry->width()>428,
              "resizing a collapsed fence changes width without losing expanded height");
        click(industry,QPoint(industry->width()-20,17)); settle(220);
        check(!industry->collapsed() && industry->height()==240,
              "title arrow still expands resized fence in edit mode");
        canvas.setGlobalEditMode(false);
        industry->setCollapsed(true); settle(40);
        industry->setCollapsed(false); settle(30);
        industry->setCollapsed(true); settle(220);
        industry->setCollapsed(false); settle(220);
        check(industry->height()==240 && !industry->collapsed(),
              "rapid animation reversal preserves full expanded height");
        industry->resize(industry->width(),360);
        industry->setCollapsed(true); // Save while animation is in flight.
        canvas.saveLayout();
        QFile saved(layoutPath); saved.open(QIODevice::ReadOnly);
        for (const auto &value: QJsonDocument::fromJson(saved.readAll()).object()["fences"].toArray()) {
            const auto obj=value.toObject();
            if (obj["id"].toString()=="industry")
                check(obj["h"].toInt()==34 && obj["expandedHeight"].toInt()==360,
                      "saving during animation stores stable target and expanded height");
        }
        settle(220);
        check(!shaped->mask().isEmpty(),"expanded magnetic fixture has an active contour");
        const QRect geometry=shaped->geometry();
        canvas.setWallpaperMagnetEnabled(false);
        shaped->refreshMagneticContour();
        check(shaped->mask().isEmpty() && shaped->geometry()==geometry,
              "disabling magnet clears hit mask without moving fences");
        check(!settings.value("appearance/wallpaperMagnetEnabled",true).toBool(),
              "disabled magnetic setting persists");
    }
    {
        DesktopCanvas canvas;
        auto *industry=byId(canvas,"industry");
        auto *shaped=byId(canvas,"shaped");
        if (!industry || !shaped) return 2;
        check(!canvas.wallpaperMagnetEnabled() && shaped->mask().isEmpty(),
              "restart respects disabled magnet");
        check(industry->collapsed() && industry->height()==34,
              "restart preserves collapsed state without a startup animation");
        click(industry,QPoint(100,17)); settle(220);
        check(industry->height()==360,"restart restores exact expanded height after resize");
        const QRect geometry=shaped->geometry();
        canvas.setWallpaperMagnetEnabled(true);
        check(canvas.wallpaperMagnetEnabled() && shaped->geometry()==geometry,
              "reenabling magnet applies to future drags without rearranging desktop");
    }
    return failures ? 1 : 0;
}
