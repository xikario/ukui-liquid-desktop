#include "../src/DesktopItem.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

static bool waitFor(const std::function<bool()> &ready, int timeout = 3000)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return ready();
}
static void write(const QString &path, const QByteArray &contents)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(contents) != contents.size()) std::abort();
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
int main(int argc, char **argv)
{
    qputenv("GIO_USE_VFS", "local");
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    const QString root = temp.path(), writer = root + "/writer.py", gio = root + "/gio";
    write(writer, "import os,signal,sys,time\nfrom pathlib import Path\n"
                  "signal.signal(signal.SIGPIPE,signal.SIG_DFL)\n"
                  "Path(sys.argv[1]+'.started').write_text('started')\n"
                  "time.sleep(.3)\n"
                  "os.write(int(sys.argv[2]),b'late application log\\n')\n"
                  "Path(sys.argv[1]).write_text('survived')\n");
    qputenv("PATH", root.toUtf8() + ':' + qgetenv("PATH"));
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        std::fprintf(stderr, "%s: %s\n", ok ? "PASS" : "FAIL", message);
        if (!ok) ++failures;
    };
    for (int stream : {1, 2}) {
        const QString marker = root + "/survived-" + QString::number(stream);
        const QString arguments = root + "/arguments";
        write(gio, ("#!/usr/bin/python3\nimport subprocess,sys\nfrom pathlib import Path\n"
                    "Path('" + arguments + "').write_text(sys.argv[2])\n"
                    "subprocess.Popen([sys.executable,'" + writer + "','" + marker + "','"
                    + QString::number(stream) + "'])\n").toUtf8());
        QObject recipient;
        DesktopItem item; item.filePath = root + "/file ' $(touch NEVER) ;.txt";
        bool completed = false, launched = false;
        item.openAsync(&recipient, [&](bool ok, const QString &) { completed = true; launched = ok; });
        check(waitFor([&] { return completed; }) && launched, "ordinary opener reports successful dispatch");
        QFile actualArguments(arguments); actualArguments.open(QIODevice::ReadOnly);
        check(QString::fromUtf8(actualArguments.readAll()) == item.filePath,
              "file path reaches opener literally, including shell metacharacters");
        check(waitFor([&] { return QFile::exists(marker); }),
              stream == 1 ? "application survives delayed stdout after opener exits"
                          : "application survives delayed stderr after opener exits");
    }
    const QString marker = root + "/survived-owner";
    write(gio, ("#!/bin/sh\nexec /usr/bin/python3 '" + writer + "' '" + marker + "' 1\n").toUtf8());
    auto *recipient = new QObject;
    DesktopItem item; item.filePath = root + "/another.txt";
    bool staleCallback = false;
    item.openAsync(recipient, [&](bool, const QString &) { staleCallback = true; });
    check(waitFor([&] { return QFile::exists(marker + ".started"); }), "long-lived opener fixture started");
    delete recipient;
    check(waitFor([&] { return QFile::exists(marker); }), "destroying desktop recipient leaves opened application alive");
    check(!staleCallback, "destroyed recipient never receives completion");
    write(gio, "#!/bin/sh\nexit 0\n");
    QPointer<QObject> selfDeleting = new QObject;
    item.openAsync(selfDeleting, [&](bool, const QString &) { delete selfDeleting.data(); });
    check(waitFor([&] { return selfDeleting.isNull(); }), "completion may destroy its recipient safely");
    const QString desktop = root + "/fixture.desktop", argsFile = root + "/desktop-arguments";
    write(writer, "import json,sys\nfrom pathlib import Path\nPath(sys.argv[1]).write_text(json.dumps(sys.argv[2:]))\n");
    write(desktop, ("[Desktop Entry]\nType=Application\nName=Fixture\nTerminal=false\nExec=/usr/bin/python3 \""
        + writer + "\" \"" + argsFile + "\" %U\n").toUtf8());
    const QString path = root + QString::fromUtf8("/中文 file #1.txt");
    QString error;
    check(launchDesktopFileCompat(desktop, &error, {path, QStringLiteral("https://example.invalid/a%20b")}),
          "shared GIO launch accepts file and URI arguments");
    check(waitFor([&] {
        QFile f(argsFile);
        return f.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(f.readAll()).array().size() == 2;
    }), "shared GIO launch executes actual fixture");
    QFile received(argsFile);received.open(QIODevice::ReadOnly);
    const auto args = QJsonDocument::fromJson(received.readAll()).array();
    // GIO may pass native paths for local file URIs, even with %U. Both
    // representations must identify exactly the original Unicode filename.
    check(args.size() == 2 && (args[0].toString() == path || QUrl(args[0].toString()).toLocalFile() == path)
          && args[1].toString() == QStringLiteral("https://example.invalid/a%20b"),
          "shared GIO preserves Unicode file targets and explicit URIs");
    check(!launchDesktopFileCompat(root + "/missing.desktop", &error) && !error.isEmpty(),
          "invalid desktop entry returns a useful error");
    return failures ? 1 : 0;
}
