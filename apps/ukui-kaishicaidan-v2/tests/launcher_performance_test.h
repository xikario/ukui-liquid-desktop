#pragma once
#include <sys/stat.h>

// Uses the real opening/refresh paths, isolated files and a gated FIFO. No
// sleeps in a fake backend and no wall-clock performance pass/fail threshold.
class StartMenuPerformanceTest {
public:
    static int run() {
        int failures = 0;
        const auto check = [&failures](bool ok, const char *message) {
            if (ok) qInfo() << "PASS:" << message;
            else { ++failures; qCritical() << "FAIL:" << message; }
        };
        const auto wait = [](const std::function<bool()> &ready) {
            QElapsedTimer timer; timer.start();
            while (!ready() && timer.elapsed() < 5000) settle(10);
            return ready();
        };
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QString xbel = data + "/recently-used.xbel";
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto xml = [](const char *count) {
            return QByteArray("<xbel><application exec='cache-alpha' name='Cache Alpha' modified='2026-10-15T08:00:00Z' count='")
                + count + "'/></xbel>";
        };
        AppEntry app; app.name="Cache Alpha"; app.exec="/opt/cache-alpha";
        app.desktopPath=data+"/applications/cache-alpha.desktop";
        QList<AppEntry> apps{app};
        check(write(xbel,xml("3")),"usage fixture is in XDG_DATA_HOME");
        AppRegistry::refreshUsageMetadata(apps);
        check(apps[0].launchCount==3 && apps[0].lastUsed.isValid(),"XDG usage metadata reaches matching apps");
        AppRegistry::refreshUsageMetadata(apps);
        check(apps[0].launchCount==3,"cached refresh does not accumulate launch counts");
        const auto mtime=QFileInfo(xbel).lastModified();
        check(write(xbel+".new",xml("7")),"same-size replacement prepared");
        { QFile file(xbel+".new"); file.open(QIODevice::ReadWrite); file.setFileTime(mtime,QFileDevice::FileModificationTime); }
        QFile::remove(xbel); QFile::rename(xbel+".new",xbel);
        AppRegistry::refreshUsageMetadata(apps);
        check(apps[0].launchCount==7,"same-size same-mtime atomic replacement invalidates cached usage");
        QFile::remove(xbel); AppRegistry::refreshUsageMetadata(apps);
        check(apps[0].launchCount==0 && !apps[0].lastUsed.isValid(),"deleting XBEL clears previous usage metadata");

        StartMenu menu;
        check(wait([&]{return !menu.m_appScanBusy && !menu.m_avatarPending;}),"initial background scan and bounded avatar lookup finish");
        menu.refreshUserAvatar();
        check(!menu.m_avatarPending && menu.m_avatarRefreshTime.isValid(),"adjacent openings reuse the account/avatar result without new I/O");
        menu.applySkin(Skin::EcoLiquid);
        check(wait([&]{return !menu.m_appScanBusy;}),"skin refresh completes");
        menu.show(); settle(60);
        auto buttons=menu.m_recentContainer->findChildren<QPushButton *>(QString(),Qt::FindDirectChildrenOnly);
        check(!buttons.isEmpty(),"real recent application tiles are present");
        QList<QPointer<QPushButton>> original;
        for (auto *button:buttons) original << button;
        menu.setupRecentFiles(); menu.setupRecentFiles();
        bool reused=true; for (const auto &button:original) reused=reused && button;
        check(reused,"unchanged recent entries retain actual buttons, animations and connections");
        if (!buttons.isEmpty()) {
            auto *tile=buttons.first(); tile->grab();
            const int iconBuilds=tile->property("iconSpriteBuilds").toInt();
            const int labelBuilds=tile->property("labelSpriteBuilds").toInt();
            for (int i=0;i<20;++i) tile->grab();
            check(iconBuilds>0 && labelBuilds>0 && tile->property("iconSpriteBuilds").toInt()==iconBuilds
                && tile->property("labelSpriteBuilds").toInt()==labelBuilds,"repeated paints reuse rasterized icons and outlined labels");
            const QImage oldIcon = tile->grab().toImage();
            QImage replacement(72,80,QImage::Format_RGB32); replacement.fill(QColor("#df4040"));
            replacement.save(data+"/applications/good-icon.png"); menu.setupRecentFiles();
            check(tile->grab().toImage()!=oldIcon,"reused recent tile notices an icon file replaced at the same path");
            const QImage before=tile->grab().toImage();
            QFont font=tile->font(); font.setBold(!font.bold()); tile->setFont(font); tile->grab();
            check(tile->property("labelSpriteBuilds").toInt()>labelBuilds && tile->grab().toImage()!=before,
                "font changes invalidate label pixels");
            const auto afterFont=tile->grab().toImage();
            menu.m_palette.textSecondary=QColor("#ff8090"); tile->grab();
            check(tile->grab().toImage()!=afterFont,"palette changes invalidate label pixels");
            tile->setDown(true);
            check(tile->grab().toImage()!=afterFont,"pressed feedback is retained with cached sprites");
            tile->setDown(false);
        }
        menu.refreshPalette();
        const int beforeBright = buttons.isEmpty() ? 0 : buttons.first()->property("labelSpriteBuilds").toInt();
        QImage bright(QSize(qRound(menu.width()*menu.devicePixelRatioF()),qRound(menu.height()*menu.devicePixelRatioF())),QImage::Format_RGB32);
        bright.setDevicePixelRatio(menu.devicePixelRatioF()); bright.fill(QColor("#eee8df"));
        menu.m_nextKdeGlassView->setBackdropFast(bright); menu.update(); settle(30);
        if (!buttons.isEmpty()) { buttons.first()->grab();
            check(buttons.first()->property("labelSpriteBuilds").toInt()>beforeBright,"bright backdrop refreshes the local outlined-text halo"); }

        const QString artifacts=QCoreApplication::applicationDirPath()+"/artifacts/optimization-20261015";
        QDir().mkpath(artifacts);
        menu.grab().save(artifacts+QString("/menu-%1.png").arg(menu.devicePixelRatioF()));
        menu.hideMenu();
        menu.prewarmBackdrop();
        check(!menu.isVisible() && !menu.m_showPending,"hover prewarm never maps or activates the launcher");
        menu.showMenu();
        check(wait([&]{return !menu.m_showPending;}),"opening joins or replaces hover preparation");
        check(menu.isVisible(),"opening after hover preparation still shows the launcher");
        menu.hideMenu();
        // Stall the actual usage file open. GUI input must still be delivered.
        const QString recent=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            +"/ukui-kaishicaidan-v2/recent-apps.conf";
        QFile originalRecent(recent); originalRecent.open(QIODevice::ReadOnly);
        const QByteArray saved=originalRecent.readAll(); originalRecent.close();
        QFile::remove(recent);
        const bool fifo=::mkfifo(QFile::encodeName(recent).constData(),0600)==0;
        check(fifo,"gated usage reader created");
        if (fifo) {
            menu.refreshUsageMetadata();
            bool delivered=false; QTimer::singleShot(0,&menu,[&]{delivered=true;});
            settle(40);
            check(delivered && menu.m_usageRefreshBusy,"GUI events continue while real usage I/O is blocked");
            menu.refreshUsageMetadata(); menu.refreshUsageMetadata();
            check(menu.m_usageRefreshPending,"repeated refreshes coalesce behind the active worker");
            // Supersede the snapshot as if the application catalog changed.
            ++menu.m_appDataRevision;
            AppEntry newest; newest.name="Newest Catalog Entry"; newest.exec="/bin/true";
            newest.desktopPath=data+"/applications/newest-entry.desktop";
            menu.m_allApps.append(newest);
            menu.m_searchEdit->setText("Newest");
            QFile release(recent); release.open(QIODevice::WriteOnly);
            release.write(saved); release.close(); QFile::remove(recent); write(recent,saved);
            check(wait([&]{return !menu.m_usageRefreshBusy;}),"superseded usage snapshot completes using latest catalog");
            bool retained=false; for (const auto &entry:menu.m_allApps) retained=retained || entry.desktopPath==newest.desktopPath;
            check(retained && menu.m_searchEdit->text()=="Newest","stale usage completion cannot replace newer catalog or typed search");
            menu.m_searchEdit->clear();
        }
        const auto watched=menu.m_appWatcher->files();
        menu.refreshAppWatcher();
        check(menu.m_appWatcher->files()==watched && !watched.isEmpty(),"refresh retains unchanged desktop-file watches");
        if (!watched.isEmpty()) {
            const QString file=watched.first(); QFile input(file); input.open(QIODevice::ReadOnly);
            QByteArray contents=input.readAll(); input.close();
            const auto found=contents.indexOf("Name="); const auto end=contents.indexOf('\n',found);
            if (found>=0 && end>found) contents.replace(found,end-found,"Name=Changed In Place");
            write(file,contents);
            check(wait([&]{for (const auto &entry:menu.m_allApps) if (entry.desktopPath==file && entry.name=="Changed In Place") return true; return false;}),
                "in-place desktop-file edits still refresh the actual catalog");
        }
        return failures ? 1 : 0;
    }
};
