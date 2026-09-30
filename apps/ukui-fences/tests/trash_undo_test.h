#pragma once
#include "DesktopIcon.h"
#include "FileClipboard.h"
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <unistd.h>

static int runTrashUndoTest(const QString &root)
{
    const QString desktop = root + "/desktop";
    QDir().mkpath(desktop);
    qputenv("XDG_DESKTOP_DIR", desktop.toUtf8());
    QSettings settings;
    for (const QString &name : {QString("smartSpace"), QString("systemMonitor"),
        QString("clockWidget"), QString("activityWidget"), QString("musicWidget"), QString("calendarWidget")})
        settings.setValue(name + "/autoStart", false);
    settings.sync();
    auto write = [](const QString &path, const QByteArray &data) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    auto read = [](const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray(); return file.readAll();
    };
    QStringList warnings;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget)) { warnings << box->text(); box->accept(); }
    });
    dismiss.start(10);
    QWidget owner;
    auto trash = [&](const QStringList &paths) {
        FileClipboard::PasteResult result; bool completed = false;
        check(FileClipboard::trashFilesAsync(paths, &owner, [&](const auto &r) { result = r; completed = true; }),
            "trash job accepted");
        QElapsedTimer timer; timer.start();
        while (!completed && timer.elapsed() < 8000) settle(20);
        check(completed, "trash job completed");
        return result;
    };
    const QString file = desktop + "/同名 % 文件.txt";
    write(file, "Fences generation");
    const auto first = trash({file});
    const auto receipt = first.trashEntries.value(file);
    check(first.failedPaths.isEmpty() && first.undoUnavailablePaths.isEmpty()
        && !receipt.infoPath.isEmpty() && receipt.infoPath.startsWith(root + "/data/Trash/"),
        "real GIO trash captures receipt in custom XDG data directory");
    write(file, "External generation");
    check(QProcess::execute("gio", {"trash", "--", file}) == 0, "external deletion of same pathname succeeds");
    check(FileClipboard::restoreTrashedEntry(receipt) == file && read(file) == "Fences generation",
        "undo restores exact original generation after external same-path deletion");
    check(!QFileInfo::exists(receipt.infoPath), "undo removes only consumed metadata");
    check(QDir(root + "/data/Trash/info").entryList({"*.trashinfo"}, QDir::Files).size() == 1,
        "external generation remains in trash");

    write(file, "first LIFO"); const auto a = trash({file}).trashEntries.value(file);
    write(file, "second LIFO"); const auto b = trash({file}).trashEntries.value(file);
    check(FileClipboard::restoreTrashedEntry(b) == file && read(file) == "second LIFO", "latest own receipt restores first");
    const QString conflict = FileClipboard::restoreTrashedEntry(a);
    check(!conflict.isEmpty() && conflict != file && read(conflict) == "first LIFO" && read(file) == "second LIFO",
        "older receipt restores to unique name without overwriting current file");

    const QString dangling = desktop + "/dangling-link";
    check(QFile::link(desktop + "/absent-target", dangling), "dangling symlink fixture created");
    const auto link = trash({dangling}).trashEntries.value(dangling);
    check(!link.infoPath.isEmpty() && FileClipboard::restoreTrashedEntry(link) == dangling
        && QFileInfo(dangling).isSymLink(), "dangling symlink receives and restores exact receipt");
    const QString folder = desktop + "/folder";
    QDir().mkpath(folder + "/nested"); write(folder + "/nested/content", "directory payload");
    const auto directory = trash({folder}).trashEntries.value(folder);
    check(FileClipboard::restoreTrashedEntry(directory) == folder && read(folder + "/nested/content") == "directory payload",
        "directory receipt restores complete nested content");

    QTemporaryDir volume("/dev/shm/fences-trash-volume-XXXXXX");
    if (volume.isValid()) {
        const QString volumeTrash = "/dev/shm/.Trash-" + QString::number(::getuid());
        const bool existed = QFileInfo::exists(volumeTrash);
        const QString name = QFileInfo(volume.path()).fileName() + ".txt";
        const QString volumeFile = volume.path() + '/' + name;
        write(volumeFile, "separate filesystem payload");
        const auto volumeResult = trash({volumeFile});
        const auto volumeEntry = volumeResult.trashEntries.value(volumeFile);
        if (volumeResult.failedPaths.contains(volumeFile)) {
            // Some OEM GIO builds try a cross-mount clone into home trash.
            check(read(volumeFile) == "separate filesystem payload" && volumeEntry.infoPath.isEmpty(),
                "backend rejection of cross-filesystem trash retains source without undo record");
        } else {
            check(!volumeEntry.infoPath.isEmpty() && volumeEntry.infoPath.startsWith("/dev/shm/.Trash"),
                "different filesystem records its volume trash receipt");
            check(FileClipboard::restoreTrashedEntry(volumeEntry) == volumeFile
                && read(volumeFile) == "separate filesystem payload", "volume-relative trash path restores correctly");
        }
        // Clean only this randomly named fixture if an assertion fails.
        for (const QString &info : QDir(volumeTrash + "/info").entryList({name + "*.trashinfo"}, QDir::Files)) {
            const QString infoPath = volumeTrash + "/info/" + info;
            if (!read(infoPath).contains(QFileInfo(volume.path()).fileName().toUtf8())) continue;
            QString payload = info; payload.chop(10);
            QFile::remove(volumeTrash + "/files/" + payload); QFile::remove(infoPath);
        }
        if (!existed) {
            QDir().rmdir(volumeTrash + "/info"); QDir().rmdir(volumeTrash + "/files"); QDir().rmdir(volumeTrash);
        }
    }

    write(file, "deleted receipt"); const auto missing = trash({file}).trashEntries.value(file);
    QFile::remove(missing.trashedPath);
    write(file, "newer live generation"); const auto newer = trash({file}).trashEntries.value(file);
    check(FileClipboard::restoreTrashedEntry(missing).isEmpty() && read(newer.trashedPath) == "newer live generation",
        "missing exact payload never falls back to another generation");
    const QByteArray metadata = read(newer.infoPath);
    check(QFile::rename(newer.infoPath, newer.infoPath + ".saved") && write(newer.infoPath, metadata), "replacement metadata fixture created");
    check(FileClipboard::restoreTrashedEntry(newer).isEmpty() && read(newer.trashedPath) == "newer live generation",
        "replacement metadata is refused even if its text is identical");

    const QString altered = desktop + "/altered.txt";
    write(altered, "unmodified payload"); const auto alteredEntry = trash({altered}).trashEntries.value(altered);
    write(alteredEntry.trashedPath, "modified inside trash");
    check(FileClipboard::restoreTrashedEntry(alteredEntry).isEmpty(), "changed payload identity is refused");

    // A successful backend with no usable receipt must not invent an undo.
    const QString bin = root + "/untracked-backend"; QDir().mkpath(bin);
    write(altered, "backend source without receipt");
    write(bin + "/gio", "#!/bin/sh\nexit 0\n");
    QFile::setPermissions(bin + "/gio", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QByteArray oldPath = qgetenv("PATH"); qputenv("PATH", bin.toUtf8() + ':' + oldPath);
    const auto untracked = trash({altered}); qputenv("PATH", oldPath);
    check(untracked.placedPaths.contains(altered) && untracked.trashEntries.isEmpty()
        && untracked.undoUnavailablePaths.contains(altered), "missing receipt is reported without guessing another record");

    const QString relative = root + "/data/relative-folder/relative.txt";
    QDir().mkpath(QFileInfo(relative).absolutePath()); write(relative, "relative metadata payload");
    write(bin + "/gio", R"(#!/bin/sh
printf '[Trash Info]\nPath=relative-folder/relative.txt\nDeletionDate=2026-10-01T00:00:00\n' > "$XDG_DATA_HOME/Trash/info/relative-fixture.trashinfo"
exec mv -- "$3" "$XDG_DATA_HOME/Trash/files/relative-fixture"
)");
    qputenv("PATH", bin.toUtf8() + ':' + oldPath);
    const auto relativeEntry = trash({relative}).trashEntries.value(relative); qputenv("PATH", oldPath);
    check(!relativeEntry.infoPath.isEmpty() && FileClipboard::restoreTrashedEntry(relativeEntry) == relative
        && read(relative) == "relative metadata payload", "standard relative metadata resolves against trash location base");

    // Exercise the real Fence -> Canvas signal and Ctrl+Z path, including partial failure.
    DesktopCanvas canvas;
    auto *fence = canvas.createFence("trash source", QRect(400, 150, 320, 260));
    const QString good = desktop + "/good.txt", bad = desktop + "/bad.txt";
    write(good, "good own generation"); write(bad, "bad own generation");
    fence->addItem(DesktopItem::fromPath(good)); fence->addItem(DesktopItem::fromPath(bad));
    const auto batch = trash({good, bad, desktop + "/does-not-exist"});
    check(batch.placedPaths.size() == 2 && batch.failedPaths.size() == 1 && batch.trashEntries.size() == 2,
        "partial trash batch captures only successful deletions");
    fence->filesTrashed(batch);
    write(good, "external good"); check(QProcess::execute("gio", {"trash", "--", good}) == 0, "external batch generation trashed");
    const auto badEntry = batch.trashEntries.value(bad);
    QFile::remove(badEntry.trashedPath);
    write(bad, "external bad"); const auto externalBad = trash({bad}).trashEntries.value(bad);
    auto undo = [&] {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(fence, &event);
        QElapsedTimer timer; timer.start();
        do { settle(20); } while (FileClipboard::busy() && timer.elapsed() < 8000);
        check(!FileClipboard::busy(), "Ctrl+Z operation completes");
    };
    undo();
    check(read(good) == "good own generation" && fence->hasItem(good), "real canvas undo restores exact file and source fence");
    check(!QFileInfo::exists(bad) && read(externalBad.trashedPath) == "external bad", "partial undo leaves unrelated generation untouched");
    undo();
    check(read(good) == "good own generation" && read(externalBad.trashedPath) == "external bad" && warnings.size() == 2,
        "retry retains failed exact receipt and does not repeat successful entries");
    return failures ? 1 : 0;
}
