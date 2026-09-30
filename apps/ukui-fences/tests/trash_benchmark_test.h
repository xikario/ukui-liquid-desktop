#pragma once
#include "FileClipboard.h"

// Manual benchmark: isolated HOME/data, 5,000 old records and 200 real GIO deletions.
static int runTrashBenchmarkTest(const QString &root)
{
    auto write = [](const QString &path, const QByteArray &data) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    const QString info = root + "/data/Trash/info", files = root + "/data/Trash/files";
    QDir().mkpath(info); QDir().mkpath(files); QDir().mkpath(root + "/sources");
    for (int i = 0; i < 5000; ++i)
        if (!write(info + QString("/old-%1.trashinfo").arg(i), "[Trash Info]\nPath=/old-unrelated-file\n")) return 2;
    QStringList paths;
    for (int i = 0; i < 200; ++i) {
        const QString path = root + QString("/sources/current-%1.txt").arg(i);
        if (!write(path, "benchmark payload")) return 2;
        paths << path;
    }
    QWidget owner; bool completed = false; FileClipboard::PasteResult result;
    QElapsedTimer elapsed; elapsed.start();
    check(FileClipboard::trashFilesAsync(paths, &owner, [&](const auto &r) { result = r; completed = true; }),
        "benchmark batch accepted");
    while (!completed && elapsed.elapsed() < 120000) settle(10);
    qInfo() << "TRASH_BENCHMARK" << "records=5000 files=200 elapsed_ms=" << elapsed.elapsed()
            << "receipts=" << result.trashEntries.size();
    check(completed && result.placedPaths.size() == 200 && result.trashEntries.size() == 200
        && result.failedPaths.isEmpty() && result.undoUnavailablePaths.isEmpty(), "benchmark retains every exact receipt");
    return failures ? 1 : 0;
}
