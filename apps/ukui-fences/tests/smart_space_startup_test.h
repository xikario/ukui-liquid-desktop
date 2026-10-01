#pragma once
#include "SmartSpaceWidget.h"
#include <QLabel>
#include <QSemaphore>
#include <QStandardPaths>
#include <QtEndian>
#include <sys/stat.h>
#include <zlib.h>

// Drive publication and restored exclusions; assertions inspect displayed
// cards/status through the real widget, including its constructor's async read.
class SmartSpaceStartupTestAccess {
public:
    static bool publish(SmartSpaceWidget &widget) { return widget.loadIndex(); }
    static void restoreExcludedFolders(SmartSpaceWidget &widget) { widget.m_excludedFolders.clear(); }
};

static int runSmartSpaceStartupTest(const QString &root)
{
    qputenv("UKUI_FENCES_TEST_CONFIRM_EXCLUDE", "1");
    QSettings settings;
    settings.setValue("smartSpace/defaultHidden", false);
    settings.setValue("smartSpace/themeMode", 1);
    settings.setValue("smartSpace/indexMode", 0);
    settings.setValue("smartSpace/excludedFolders", QStringList()); settings.sync();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/smart-space";
    QDir().mkpath(cache);
    const QString canonical = cache + "/index.json", stream = canonical + ".ui.bin.gz";
    const QString oldPath = root + "/held/old.txt", newPath = root + "/new.txt";
    auto writeJson = [&](const QString &path) {
        QFile f(canonical);
        if (!f.open(QIODevice::WriteOnly)) return false;
        return f.write(QJsonDocument(QJsonObject{{"generatedAt", "2026-10-01T00:00:00.000Z"},
            {"items", QJsonArray{QJsonObject{{"path", path}, {"root", root},
                {"category", "document"}, {"name", QFileInfo(path).fileName()}}}}}).toJson()) > 0;
    };
    auto displayed = [](SmartSpaceWidget *widget, const QString &path) {
        for (auto *list : widget->findChildren<QListWidget *>())
            for (int i=0; i<list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole+1).toString() == path) return true;
        return false;
    };
    // A gzip stream whose writer is held by a semaphore gives deterministic
    // slow I/O, independent of CPU speed or a guessed sleep duration.
    QByteArray payload("UKFIDX1\n", 8);
    auto u32 = [&](quint32 v) { v=qToLittleEndian(v); payload.append(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto i64 = [&](qint64 v) { v=qToLittleEndian(v); payload.append(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto str = [&](const QString &v) { const auto b=v.toUtf8(); u32(b.size()); payload.append(b); };
    u32(1);
    const QByteArray metadata=QJsonDocument(QJsonObject{{"uiStreamItems", 1},
        {"generatedAt", "2026-10-01T00:00:00.000Z"}}).toJson(QJsonDocument::Compact);
    u32(metadata.size()); payload.append(metadata); u32(1); payload.append(char(0)); i64(0); i64(-1);
    for (const QString &v : {oldPath, root, QString("old.txt"), QString("document"),
        QString(), QString(), QString(), QString()}) str(v);
    const QString gzipPath=root+"/fixture.gz";
    gzFile gzip=gzopen(QFile::encodeName(gzipPath).constData(), "wb");
    check(gzip && gzwrite(gzip, payload.constData(), payload.size()) == payload.size(), "write held stream fixture");
    if (!gzip) return 1;
    check(gzclose(gzip) == Z_OK, "finish gzip fixture");
    QFile gzipFile(gzipPath); gzipFile.open(QIODevice::ReadOnly); const auto encoded=gzipFile.readAll();

    for (int scenario=0; scenario<5; ++scenario) {
        settings.setValue("smartSpace/excludedFolders", scenario==4 ? QStringList{root+"/held"} : QStringList()); settings.sync();
        check(writeJson(oldPath), "write canonical fallback fixture");
        QFile::remove(stream);
        check(::mkfifo(QFile::encodeName(stream).constData(), 0600) == 0, "create held index stream");
        QSemaphore opened, release, finished;
        auto *writer=QThread::create([&] {
            QFile pipe(stream);
            if (pipe.open(QIODevice::WriteOnly)) {
                opened.release();
                // A bounded fallback also lets a regression in the constructor
                // complete with failed assertions instead of hanging shutdown.
                release.tryAcquire(1, 5000);
                pipe.write(encoded); pipe.close();
            }
            finished.release();
        });
        writer->start();
        QElapsedTimer elapsed; elapsed.start();
        auto *widget=new SmartSpaceWidget;
        check(elapsed.elapsed()<2000 && widget->property("indexLoading").toBool(),
              "constructor returns with cached index I/O still pending");
        widget->show();
        int beats=0; QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start(5);
        elapsed.restart();
        while (!opened.available() && elapsed.elapsed()<2000) settle(10);
        settle(30);
        check(opened.available() && beats>0 && !displayed(widget, oldPath),
              "GUI paints and processes timers while the cached index reader is blocked");
        if (scenario==1) widget->excludeFolder(root+"/held");
        if (scenario==4) SmartSpaceStartupTestAccess::restoreExcludedFolders(*widget);
        if (scenario==2) {
            // The old reader has its FIFO open; a new publication replaces the
            // path while that reader remains held, then wins over its callback.
            QFile::remove(stream);
            check(writeJson(newPath) && SmartSpaceStartupTestAccess::publish(*widget),
                  "new publication applies while initial read is pending");
            check(displayed(widget, newPath), "new publication has a real file card");
        }
        QPointer<SmartSpaceWidget> guard(widget);
        if (scenario==3) { delete widget; widget=nullptr; }
        release.release();
        elapsed.restart();
        while ((!finished.available() || (widget && widget->property("indexLoading").toBool()))
               && elapsed.elapsed()<5000) settle(10);
        writer->wait(); delete writer; settle(80);
        if (scenario==0 || scenario==4)
            check(widget && !widget->property("indexLoading").toBool() && displayed(widget, oldPath),
                  "completed gzip read delivers the expected file card using the current scope");
        if (scenario==1)
            check(widget && !displayed(widget, oldPath)
                  && widget->findChild<QLabel *>("smartSpaceStatus")->text().contains("0 项"),
                  "exclusion changed during read also applies to its delivered snapshot");
        if (scenario==2)
            check(widget && displayed(widget, newPath) && !displayed(widget, oldPath),
                  "stale initial completion cannot replace the newer publication");
        if (scenario==3) check(!guard, "closing during index I/O safely drops completion delivery");
        delete widget; QFile::remove(stream);
    }
    return failures ? 1 : 0;
}
