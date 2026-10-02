#include "LiquidDialog.h"
#include <QLabel>
#pragma once
#include "DesktopIcon.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>

static int runFolderDropUndoTest(const QString &root)
{
    QSettings settings;
    for (const QString &name : {QString("smartSpace"), QString("systemMonitor"),
                               QString("clockWidget"), QString("activityWidget"),
                               QString("musicWidget"), QString("calendarWidget")})
        settings.setValue(name + "/autoStart", false);
    settings.sync();
    const QString desktop = root + "/desktop", dest = root + "/destination";
    QDir().mkpath(desktop); QDir().mkpath(dest);
    qputenv("XDG_DESKTOP_DIR", desktop.toUtf8());
    QDir().mkpath(root + "/config/kyfences");
    QFile layout(root + "/config/kyfences/layout.json");
    layout.open(QIODevice::WriteOnly);
    layout.write("{\"fences\":[]}"); layout.close();
    auto write = [](const QString &path, const QByteArray &data) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    };
    auto read = [](const QString &path) {
        QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll();
    };
    auto wait = [] {
        QElapsedTimer timer; timer.start();
        do { settle(20); } while (FileClipboard::busy() && timer.elapsed() < 8000);
        check(!FileClipboard::busy(), "file operation completes within timeout");
        settle(60);
    };
    // Close expected conflict/partial-failure messages without blocking the test.
    QStringList messages;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if(widget->objectName()=="liquidMessageDialog" && widget->isVisible() && !widget->property("liquidDialogClosing").toBool()) {
                messages << widget->findChild<QLabel *>("liquidMessageText")->text(); qobject_cast<QDialog *>(widget)->accept();
            }
    });
    dismiss.start(10);
    DesktopCanvas canvas;
    auto *sourceFence = canvas.createFence("source", QRect(400, 150, 320, 260));
    auto *targetFence = canvas.createFence("target", QRect(760, 150, 320, 260));
    auto undo = [&](QWidget *receiver) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(receiver, &event); wait();
    };
    auto drop = [&](const QStringList &paths, bool move, bool inFence, bool destroyIcon = false) {
        DesktopIcon *icon = nullptr;
        if (inFence) {
            targetFence->addItem(DesktopItem::fromPath(dest));
            for (auto *candidate : targetFence->findChildren<DesktopIcon *>())
                if (candidate->item().filePath == dest) { icon = candidate; break; }
        } else icon = new DesktopIcon(DesktopItem::fromPath(dest), &canvas);
        check(icon != nullptr, "directory drop target exists");
        if (!icon) return;
        icon->show();
        QMimeData mime; QList<QUrl> urls;
        for (const QString &path : paths) urls << QUrl::fromLocalFile(path);
        mime.setUrls(urls);
        if (move) mime.setData("application/x-ukui-fences-file-drag", "1");
        const auto action = move ? Qt::MoveAction : Qt::CopyAction;
        QDragEnterEvent enter(QPoint(20, 20), action, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(icon, &enter);
        QDropEvent event(QPointF(20, 20), action, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(icon, &event);
        check(event.isAccepted(), "real directory icon accepts drop");
        if (destroyIcon) {
            if (inFence) targetFence->removeItem(dest);
            else delete icon;
        }
        wait();
        if (!inFence && !destroyIcon) delete icon;
    };
    const QString folder = desktop + "/folder";
    QDir().mkpath(folder + "/nested"); write(folder + "/nested/data.txt", "original payload");
    sourceFence->addItem(DesktopItem::fromPath(folder));
    drop({folder}, true, true, true);
    check(!QFileInfo::exists(folder) && read(dest + "/folder/nested/data.txt") == "original payload",
          "folder including nested content moves into target directory");
    check(!sourceFence->hasItem(folder), "source fence removes moved folder");
    undo(sourceFence);
    check(read(folder + "/nested/data.txt") == "original payload" && !QFileInfo::exists(dest + "/folder"),
          "Ctrl+Z restores folder after target icon destruction");
    check(sourceFence->hasItem(folder), "undo restores original fence membership");

    const QString loose = desktop + "/loose.txt";
    write(loose, "loose content"); canvas.refreshAll(); settle(100);
    auto looseIcon = [&](const QString &path) -> DesktopIcon * {
        for (auto *icon : canvas.findChildren<DesktopIcon *>(QString(), Qt::FindDirectChildrenOnly))
            if (icon->item().filePath == path && icon->isVisible()) return icon;
        return nullptr;
    };
    DesktopIcon *beforeLoose = looseIcon(loose);
    check(beforeLoose != nullptr, "loose source icon is present");
    const QPoint loosePosition = beforeLoose ? beforeLoose->pos() : QPoint();
    drop({loose}, true, false);
    undo(&canvas);
    check(read(loose) == "loose content" && !QFileInfo::exists(dest + "/loose.txt"),
          "desktop directory drop restores loose source");

    check(looseIcon(loose) && looseIcon(loose)->pos() == loosePosition,
          "undo restores original loose desktop position");

    drop({folder}, false, false);
    check(read(dest + "/folder/nested/data.txt") == "original payload", "external copy creates independent folder");
    undo(&canvas);
    check(read(folder + "/nested/data.txt") == "original payload" && !QFileInfo::exists(dest + "/folder"),
          "copy undo removes only copy and preserves original");

    QDir().mkpath(dest + "/folder"); write(dest + "/folder/keep.txt", "destination occupant");
    drop({folder}, true, false);
    check(read(dest + "/folder (1)/nested/data.txt") == "original payload",
          "same-name destination gets a separate unique folder");
    undo(&canvas);
    check(read(folder + "/nested/data.txt") == "original payload" &&
          read(dest + "/folder/keep.txt") == "destination occupant" && !QFileInfo::exists(dest + "/folder (1)"),
          "undo uses actual unique destination without touching existing folder");
    QDir(dest + "/folder").removeRecursively();

    const QString second = desktop + "/second.txt";
    write(second, "second payload"); sourceFence->addItem(DesktopItem::fromPath(second));
    drop({folder, second, desktop + "/missing.txt"}, true, true);
    check(!QFileInfo::exists(folder) && !QFileInfo::exists(second), "partial drop commits valid sources");
    QDir().mkpath(folder); write(folder + "/keep.txt", "new occupant");
    undo(&canvas);
    check(read(folder + "/keep.txt") == "new occupant" &&
          read(dest + "/folder/nested/data.txt") == "original payload", "undo never overwrites conflicting source");
    check(read(second) == "second payload" && sourceFence->hasItem(second), "partial undo restores independent item and fence");
    QDir(folder).removeRecursively();
    undo(&canvas);
    check(read(folder + "/nested/data.txt") == "original payload" && sourceFence->hasItem(folder),
          "failed undo remains retryable after resolving conflict");
    check(read(second) == "second payload", "retry does not repeat successful entries");

    // A same-directory drop must leave the previous meaningful undo at the top.
    drop({second}, true, false);
    drop({dest + "/second.txt"}, true, false);
    undo(&canvas);
    check(read(second) == "second payload" && !QFileInfo::exists(dest + "/second.txt"),
          "same-directory no-op does not add an undo record");
    check(messages.size() == 2, "only expected partial-drop and undo-conflict warnings shown");
    return failures ? 1 : 0;
}
