#pragma once
#include "SmartSpaceWidget.h"
#include <QScreen>
#include <QToolButton>
#include <QLayout>

static int runSmartSpacePositionTest()
{
    qputenv("GSETTINGS_BACKEND", "keyfile");
    QSettings settings;
    settings.setValue("systemMonitor/autoStart", false);
    for (const auto *key : {"clock", "activity", "music", "calendar"})
        settings.setValue(QString("desklets/%1/autoStart").arg(key), false);
    settings.setValue("smartSpace/autoStart", true);
    settings.setValue("smartSpace/defaultHidden", true);
    settings.setValue("smartSpace/themeMode", 3);
    settings.setValue("smartSpace/indexMode", 0);
    settings.sync();
    QSize restoredSize(763, 429);
    {
        SmartSpaceWidget layoutProbe;
        layoutProbe.ensurePolished();
        restoredSize = restoredSize.expandedTo(layoutProbe.minimumSizeHint());
    }
    // A saved rectangle below the style's minimum size or outside the scaled
    // screen is intentionally constrained by Qt. Test exact preservation with
    // a valid rectangle at every DPI, keeping the reported size where it fits.
    const QRect available = QApplication::primaryScreen()->availableGeometry();
    const QRect restored(QPoint(0, qMax(0, qMin(448, available.height() - restoredSize.height() - 48))),
                         restoredSize);
    qInfo() << "position fixture" << restored << "screen" << available;
    auto waitFor = [](const std::function<bool()> &ready) {
        QElapsedTimer clock; clock.start();
        while (!ready() && clock.elapsed() < 5000) settle(10);
        return ready();
    };
    for (bool pinned : {false, true}) {
        settings.setValue("smartSpace/alwaysOnTop", pinned);
        settings.setValue("smartSpace/position", restored.topLeft());
        settings.setValue("smartSpace/size", restored.size());
        settings.sync();
        DesktopCanvas canvas;
        canvas.activateOnSessionStartup();
        check(waitFor([&] {
            auto *smart = canvas.findChild<SmartSpaceWidget *>();
            return smart && smart->edgeHidden() && !smart->edgeTransitionActive()
                && smart->isVisible();
        }), "login restores a visible collapsed Smart Space entry");
        auto *smart = canvas.findChild<SmartSpaceWidget *>();
        if (!smart) continue;
        check(smart->isWindow() == pinned, "startup uses the saved desktop or pinned mode");
        const QRect firstEntry = smart->geometry();
        smart->revealFromEdge();
        check(waitFor([&] { return !smart->edgeTransitionActive() && smart->isVisible(); }),
              "first expansion completes");
        qInfo() << "expanded geometry" << smart->geometry() << "minimum" << smart->minimumSizeHint();
        check(smart->geometry() == restored, "startup retains the saved expanded geometry");
        QToolButton *hideButton = nullptr;
        for (auto *button : smart->findChildren<QToolButton *>())
            if (button->property("railIconKey").toString() == "hide") hideButton = button;
        check(hideButton != nullptr, "expanded Smart Space exposes its hide button");
        if (hideButton) {
            const QRect bounds = pinned ? QApplication::primaryScreen()->availableGeometry()
                                        : canvas.rect();
            const QPoint anchor = hideButton->mapToGlobal(hideButton->rect().center());
            const QPoint local = pinned ? anchor : canvas.mapFromGlobal(anchor);
            const QRect expected(QPoint(bounds.left(),
                qBound(bounds.top(), local.y() - 26, bounds.bottom() - 51)), QSize(52, 52));
            qInfo() << "startup entry" << firstEntry << "laid out anchor entry" << expected;
            check(firstEntry == expected,
                  "initial collapsed entry aligns with the fully laid out hide button");
        }
        for (int cycle = 0; cycle < 2; ++cycle) {
            smart->hideToNearestEdge();
            check(waitFor([&] { return !smart->edgeTransitionActive() && smart->isVisible(); }),
                  "manual retraction completes");
            check(smart->geometry() == firstEntry,
                  "expand and retract never changes the startup entry position");
            smart->revealFromEdge();
            waitFor([&] { return !smart->edgeTransitionActive() && smart->isVisible(); });
            check(smart->geometry() == restored, "repeated transitions preserve expanded geometry");
        }
        smart->hideToNearestEdge();
        waitFor([&] { return !smart->edgeTransitionActive(); });
    }
    // Login may request retraction before the parent is mapped and before Qt
    // delivers the first layout events. Exercise that ordering explicitly;
    // a normal, already-shown canvas does not reproduce the stale rail anchor.
    for (bool nearBottom : {false, true}) for (bool pinned : {false, true}) {
        const QPoint savedPosition = nearBottom
            ? QPoint(0, available.bottom() - restored.height() - 16 + 1)
            : restored.topLeft();
        settings.setValue("smartSpace/alwaysOnTop", pinned);
        settings.setValue("smartSpace/position", savedPosition);
        settings.setValue("smartSpace/size", restored.size());
        settings.setValue("smartSpace/autoStart", false);
        settings.sync();
        // Use the real canvas: a plain QWidget's backdrop grab implicitly
        // lays out its children and accidentally masks this startup ordering.
        DesktopCanvas host;
        const QRect modeBounds = pinned ? available : host.rect();
        const QRect expectedExpanded = nearBottom
            ? QRect(QPoint(0, modeBounds.bottom() - restored.height() + 1), restored.size())
            : restored;
        SmartSpaceWidget smart(false, &host);
        if (pinned) {
            smart.setParent(&host, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            smart.recreateNativeSurface(true);
        }
        smart.hideToNearestEdge();
        const QRect firstEntry = smart.geometry();
        check(!smart.isVisible(), "pre-map retraction does not expose the expanded surface");
        host.show();
        smart.show();
        settle(250); // Let the constructor's default-hide timer expire.
        smart.revealFromEdge();
        check(waitFor([&] { return !smart.edgeTransitionActive() && smart.isVisible(); }),
              "pre-map startup can expand normally");
        check(smart.geometry() == expectedExpanded, "pre-map startup preserves expanded geometry and existing edge snapping");
        smart.hideToNearestEdge();
        check(waitFor([&] { return !smart.edgeTransitionActive() && smart.isVisible(); }),
              "pre-map startup can retract normally");
        qInfo() << "pre-map entry" << firstEntry << "subsequent entry" << smart.geometry()
                << "pinned" << pinned << "near bottom" << nearBottom;
        check(smart.geometry() == firstEntry,
              "pre-map and fully laid out retraction use the same entry position");
    }
    return failures ? 1 : 0;
}
