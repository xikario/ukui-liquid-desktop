#pragma once

#include "DesktopIcon.h"
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScreen>
#include <QProcess>
#include <QStandardPaths>

// Exercise the real QWidget backing store: screen grabs do not trigger a fresh
// paint, unlike QWidget::grab(), so missing damage and stale pixels are visible.
class SelectionPaintProbe : public DesktopCanvas
{
public:
    qint64 paintedPixels = 0;
    int paints = 0;
    int presses = 0;
    int releases = 0;
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        ++presses;
        DesktopCanvas::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        ++releases;
        DesktopCanvas::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent *event) override
    {
        ++paints;
        for (const QRect &rect : event->region())
            paintedPixels += qint64(rect.width()) * rect.height();
        DesktopCanvas::paintEvent(event);
    }
};

static int runSelectionDragTest(const QString &isolated)
{
    QSettings settings;
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    const QString wallPath = isolated + "/selection-wallpaper.png";
    wallpaper(QSize(1440, 900), 1).save(wallPath);
    QDir().mkpath(isolated + "/config/kyfences");
    QFile layout(isolated + "/config/kyfences/layout.json");
    layout.open(QIODevice::WriteOnly);
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath", wallPath},
        {"wallpaperMode", 2}, {"fences", QJsonArray{}}}).toJson());
    layout.close();

    SelectionPaintProbe canvas;
    const auto icons = canvas.findChildren<DesktopIcon *>();
    for (auto *icon : icons) icon->hide();
    settle(350);
    const QPoint origin(700, 450);
    auto sendMouse = [&](QEvent::Type type, QPoint pos) {
        const bool move = type == QEvent::MouseMove;
        QMouseEvent event(type, pos, canvas.mapToGlobal(pos),
                         move ? Qt::NoButton : Qt::LeftButton,
                         type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(&canvas, &event);
    };
    auto screenshot = [&] {
        return QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage();
    };
    auto checkBackingStore = [&] {
        const QImage partial = screenshot();
        canvas.update();
        settle(15);
        check(!partial.isNull() && partial == screenshot(),
              "incremental selection paint matches full repaint (no stale pixels)");
    };
    const QImage clean = screenshot();
    // Native input exercises the UKUI style's window-drag filter and its X11
    // synthetic releases; sendEvent(canvas) alone cannot detect this regression.
    const QString xdotool = QStandardPaths::findExecutable("xdotool");
    check(!xdotool.isEmpty(), "native selection regression requires xdotool");
    if (!xdotool.isEmpty()) {
        auto nativeMouse = [&](const QStringList &args) {
            check(QProcess::execute(xdotool, args) == 0, "native mouse command succeeds");
            settle(80);
        };
        auto nativeMove = [&](QPoint pos) {
            const QPoint global = canvas.mapToGlobal(pos);
            const qreal dpr = canvas.devicePixelRatioF();
            nativeMouse({"mousemove", QString::number(qRound(global.x() * dpr)),
                         QString::number(qRound(global.y() * dpr))});
        };
        for (int x : {10, canvas.width() / 2, canvas.width() - 160}) {
            for (int y : {0, 1, 10, 47, 48, 80}) {
                const QPoint start(x, y);
                const QPoint end(x + 100, y + 180);
                const QImage before = screenshot();
                nativeMove(start);
                canvas.presses = canvas.releases = 0;
                nativeMouse({"mousedown", "1"});
                nativeMove(end);
                qInfo() << "Native top selection" << start << "DPR" << canvas.devicePixelRatioF();
                check(canvas.presses == 1 && canvas.releases == 0,
                      "top drag is not cancelled by a synthetic mouse release");
                check(screenshot() != before, "top drag paints a visible selection");
                checkBackingStore();
                nativeMouse({"mouseup", "1"});
                check(canvas.releases == 1 && screenshot() == before,
                      "physical release clears top selection without moving desktop");
            }
        }
    }
    sendMouse(QEvent::MouseButtonPress, origin);
    for (const QPoint &pos : {QPoint(1000, 700), QPoint(1004, 703),
                              QPoint(850, 550), QPoint(450, 250),
                              QPoint(950, 250), QPoint(450, 700),
                              origin, QPoint(700, 700), QPoint(1000, 450)}) {
        sendMouse(QEvent::MouseMove, pos);
        settle(15);
        checkBackingStore();
    }
    sendMouse(QEvent::MouseMove, QPoint(1000, 700));
    settle(15);
    canvas.paints = 0;
    canvas.paintedPixels = 0;
    sendMouse(QEvent::MouseMove, QPoint(1004, 703));
    settle(15);
    const qint64 fullPixels = qint64(canvas.width()) * canvas.height();
    qInfo() << "Selection small-step painted pixels:" << canvas.paintedPixels
            << "of desktop" << fullPixels;
    check(canvas.paints > 0 && canvas.paintedPixels < fullPixels / 20,
          "small drag step repaints less than 5 percent of desktop");
    canvas.paints = 0;
    sendMouse(QEvent::MouseMove, QPoint(1004, 703));
    settle(15);
    check(canvas.paints == 0, "unchanged pointer position does not repaint");
    // Multiple mouse events may be coalesced into a single paint by Qt.
    for (const QPoint &pos : {QPoint(300, 200), QPoint(1100, 750), QPoint(500, 650)})
        sendMouse(QEvent::MouseMove, pos);
    settle(15);
    checkBackingStore();
    sendMouse(QEvent::MouseButtonRelease, QPoint(500, 650));
    settle(15);
    check(screenshot() == clean, "release removes the entire selection without residue");

    // A real loose icon must still toggle as the rectangle enters/leaves it.
    check(!icons.isEmpty(), "desktop provides a system icon for selection regression");
    if (!icons.isEmpty()) {
        DesktopIcon *icon = icons.first();
        icon->move(800, 550);
        icon->show();
        settle(15);
        sendMouse(QEvent::MouseButtonPress, origin);
        sendMouse(QEvent::MouseMove, QPoint(1100, 800));
        settle(15);
        check(icon->isSelected(), "drag selects an intersecting desktop icon");
        canvas.paints = 0;
        icon->setSelected(true);
        settle(15);
        check(canvas.paints == 0, "unchanged icon selection does not invalidate desktop");
        sendMouse(QEvent::MouseMove, QPoint(720, 470));
        settle(15);
        check(!icon->isSelected(), "shrinking selection deselects desktop icon");
        sendMouse(QEvent::MouseButtonRelease, QPoint(720, 470));
    }
    return failures ? 1 : 0;
}
