#pragma once
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QElapsedTimer>

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
    auto finishTransitions = [] {
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed()<1000) {
            bool running=false;
            for(auto *w:QApplication::topLevelWidgets())
                for(auto *a:w->findChildren<QPropertyAnimation *>())
                    running |= a->state()==QAbstractAnimation::Running;
            if(!running) return;
            settle(10);
        }
        check(false,"partition transition finishes within one second");
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
        click(shaped,QPoint(160,17)); finishTransitions();
        check(!shaped->collapsed() && shaped->height()==320,
              "click expands collapsed magnetic fence to persisted height");
        click(industry,QPoint(100,17)); finishTransitions();
        canvas.setGlobalEditMode(true);
        const QPoint start(2,17), global=industry->mapToGlobal(start);
        mouse(industry,QEvent::MouseButtonPress,start,global);
        mouse(industry,QEvent::MouseMove,QPoint(-38,47),global+QPoint(-40,30));
        mouse(industry,QEvent::MouseButtonRelease,start,global+QPoint(-40,30));
        check(industry->collapsed() && industry->height()==34 && industry->width()>428,
              "resizing a collapsed fence changes width without losing expanded height");
        click(industry,QPoint(industry->width()-20,17)); finishTransitions();
        check(!industry->collapsed() && industry->height()==240,
              "title arrow still expands resized fence in edit mode");
        canvas.setGlobalEditMode(false);
        industry->addItem(DesktopItem::fromPath(layoutPath));
        industry->setLiquidGlassEnabled(true);
        const int captures=industry->property("collapseCaptures").toInt();
        industry->setCollapsed(true); settle(40);
        check(industry->icons().first()->parentWidget()->isHidden(),"live icons are hidden while the snapshot animates");
        const int builds=industry->property("glassBuilds").toInt();
        industry->grab();settle(30);industry->grab();
        check(industry->height()>34 && industry->height()<240,"collapse has intermediate reveal frames");
        check(industry->property("glassBuilds").toInt()==builds,"animation frames reuse captured glass without rebuilding");
        QDir().mkpath("artifacts");industry->grab().save("artifacts/fence-collapse.png");
        industry->setCollapsed(false); settle(30);
        check(industry->property("collapseCaptures").toInt()==captures+1,"rapid reversal reuses the same expanded snapshot");
        industry->setCollapsed(true); finishTransitions();
        industry->setCollapsed(false); finishTransitions();
        check(industry->height()==240 && !industry->collapsed(),
              "rapid animation reversal preserves full expanded height");
        check(!industry->icons().first()->isHidden() && !industry->icons().first()->parentWidget()->isHidden(),"expanded icons regain normal visibility and interaction");
        industry->setCollapsed(true);settle(40);canvas.setGlobalEditMode(true);
        click(industry,QPoint(60,17));finishTransitions();
        check(industry->collapsed() && industry->height()==34,"editing safely interrupts the collapse at its target");
        industry->setCollapsed(false);finishTransitions();canvas.setGlobalEditMode(false);
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
        finishTransitions();
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
        click(industry,QPoint(100,17)); finishTransitions();
        check(industry->height()==360,"restart restores exact expanded height after resize");
        const QRect geometry=shaped->geometry();
        canvas.setWallpaperMagnetEnabled(true);
        check(canvas.wallpaperMagnetEnabled() && shaped->geometry()==geometry,
              "reenabling magnet applies to future drags without rearranging desktop");
    }
    return failures ? 1 : 0;
}
