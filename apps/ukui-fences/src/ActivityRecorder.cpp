#include "ActivityRecorder.h"
#include <QApplication>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

struct ActivityRecorder::Native {
    Display *display = nullptr; Atom active = 0, type = 0, desktop = 0, dock = 0, pid = 0;
    QSocketNotifier *notifier = nullptr;
    QHash<QString, QString> names;
    ~Native() { delete notifier; if (display) XCloseDisplay(display); }
};
namespace {
int ignoreDestroyedWindow(Display *, XErrorEvent *) { return 0; }
QByteArray fileBytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray(); }
unsigned long xProperty(Display *d, Window w, Atom a, Atom requested) {
    Atom actual; int format; unsigned long count, rest; unsigned char *bytes = nullptr;
    unsigned long value = 0;
    if (XGetWindowProperty(d,w,a,0,1,False,requested,&actual,&format,&count,&rest,&bytes)==Success && bytes && format==32 && count)
        value = *reinterpret_cast<unsigned long *>(bytes);
    if (bytes) XFree(bytes); return value;
}
}
QString ActivityRecorder::dataPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ukui-fences/widgets/activity.json";
}
ActivityRecorder::ActivityRecorder(QObject *parent) : QObject(parent), m_native(new Native) {
    QFile f(dataPath());
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject() && doc.object().value("schemaVersion").toInt() == 1)
            m_ledger = ActivityLedger::fromJson(doc.object());
        else {
            f.close();
            // Preserve an unreadable file before creating a replacement ledger.
            const QString backup = dataPath()+".invalid-"+QString::number(QDateTime::currentMSecsSinceEpoch());
            m_saveOk = QFile::copy(dataPath(), backup);
            if (!m_saveOk) qWarning() << "Activity ledger could not be backed up; writes disabled";
        }
    }
    m_recording = QSettings().value("desklets/activity/recording", true).toBool();
    for (const QString &root : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        for (const QString &file : QDir(root).entryList({"*.desktop"}, QDir::Files)) {
            QSettings desktop(root+"/"+file,QSettings::IniFormat); desktop.setIniCodec("UTF-8"); desktop.beginGroup("Desktop Entry");
            const QString key = desktop.value("StartupWMClass").toString().toLower();
            const QString name = desktop.value("Name[zh_CN]",desktop.value("Name")).toString();
            if (!name.isEmpty()) {
                if (!key.isEmpty()) m_native->names.insert(key,name);
                m_native->names.insert(file.left(file.size()-8).toLower(),name);
            }
        }
    }
    if (QGuiApplication::platformName() == "xcb") m_native->display = XOpenDisplay(nullptr);
    if (auto *d = m_native->display) {
        m_native->active = XInternAtom(d,"_NET_ACTIVE_WINDOW",False);
        m_native->type = XInternAtom(d,"_NET_WM_WINDOW_TYPE",False);
        m_native->desktop = XInternAtom(d,"_NET_WM_WINDOW_TYPE_DESKTOP",False);
        m_native->dock = XInternAtom(d,"_NET_WM_WINDOW_TYPE_DOCK",False);
        m_native->pid = XInternAtom(d,"_NET_WM_PID",False);
        XSelectInput(d,DefaultRootWindow(d),PropertyChangeMask); XFlush(d);
        m_native->notifier = new QSocketNotifier(ConnectionNumber(d),QSocketNotifier::Read,this);
        connect(m_native->notifier,&QSocketNotifier::activated,this,[this,d] {
            bool changed = false;
            while (XPending(d)) { XEvent e; XNextEvent(d,&e); if (e.type==PropertyNotify && e.xproperty.atom==m_native->active) changed=true; }
            if (changed) sample();
        });
    }
    for (const QString &service : {QStringLiteral("org.freedesktop.ScreenSaver"),QStringLiteral("org.ukui.ScreenSaver"),QStringLiteral("org.gnome.ScreenSaver")}) {
        const QString path = service == "org.freedesktop.ScreenSaver" ? "/ScreenSaver" : "/org/"+service.section('.',1,1)+"/ScreenSaver";
        QDBusConnection::sessionBus().connect(service,path,service,"ActiveChanged",this,SLOT(setLocked(bool)));
        QDBusInterface iface(service,path,service,QDBusConnection::sessionBus());
        if (iface.isValid()) { QDBusReply<bool> active = iface.call("GetActive"); if (active.isValid() && active.value()) m_locked=true; }
    }
    // This UKUI release exports its lock service at / with parameterless
    // lock/unlock signals, not the freedesktop ActiveChanged interface.
    auto bus = QDBusConnection::sessionBus();
    for (const QString &signal : {QStringLiteral("lock"), QStringLiteral("prepareForLock"), QStringLiteral("ScreenSaverLock")})
        bus.connect("org.ukui.ScreenSaver", "/", "org.ukui.ScreenSaver", signal, this, SLOT(sessionLocked()));
    bus.connect("org.ukui.ScreenSaver", "/", "org.ukui.ScreenSaver", "unlock", this, SLOT(sessionUnlocked()));
    QDBusInterface ukuiLock("org.ukui.ScreenSaver", "/", "org.ukui.ScreenSaver", bus);
    if (ukuiLock.isValid()) {
        QDBusReply<bool> locked = ukuiLock.call("GetLockState");
        if (locked.isValid()) m_locked = locked.value();
    }
    m_elapsed.start(); m_lastWall = QDateTime::currentDateTime();
    connect(&m_sampleTimer,&QTimer::timeout,this,&ActivityRecorder::sample); m_sampleTimer.start(5000);
    connect(&m_saveTimer,&QTimer::timeout,this,&ActivityRecorder::flush); m_saveTimer.start(60000);
    connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{sample();flush();});
    sample(); flush();
}
ActivityRecorder::~ActivityRecorder() { sample(); flush(); }
void ActivityRecorder::setRecording(bool enabled) {
    sample(); m_recording=enabled; QSettings().setValue("desklets/activity/recording",enabled);
    sample(); flush(); emit changed();
}
void ActivityRecorder::setLocked(bool locked) { sample(); m_locked=locked; sample(); }
void ActivityRecorder::sample() {
    const auto now = QDateTime::currentDateTime();
    const qint64 elapsed = m_elapsed.restart();
    const qint64 wall = m_lastWall.msecsTo(now); m_lastWall=now;
    // Never attribute suspend, event-loop stalls, or a clock jump to an app.
    if (m_recording && !m_locked && !m_appId.isEmpty() && elapsed > 0 && elapsed <= 10000 && qAbs(wall-elapsed)<2000)
        m_ledger.addInterval(now,elapsed,m_appId,m_appName);
    const QByteArray uptimeText = fileBytes("/proc/uptime").split(' ').value(0);
    bool ok=false; const double uptime=uptimeText.toDouble(&ok);
    if(ok) m_ledger.observeBoot(QString::fromUtf8(fileBytes("/proc/sys/kernel/random/boot_id")),qint64(uptime*1000),now);
    m_appId.clear(); m_appName.clear();
    if (m_recording && !m_locked && m_native->display) {
        auto *d=m_native->display;
        XSync(d,False); auto oldHandler=XSetErrorHandler(ignoreDestroyedWindow);
        const Window w=xProperty(d,DefaultRootWindow(d),m_native->active,XA_WINDOW);
        if(w) {
            const Atom t=xProperty(d,w,m_native->type,XA_ATOM);
            const auto pid=xProperty(d,w,m_native->pid,XA_CARDINAL);
            if(t!=m_native->desktop && t!=m_native->dock && pid!=static_cast<unsigned long>(QCoreApplication::applicationPid())) {
                XClassHint cls{};
                if(XGetClassHint(d,w,&cls)) {
                    const QString name=QString::fromUtf8(cls.res_class ? cls.res_class : "");
                    const QString id=name.toLower();
                    if(!id.isEmpty() && !id.contains("lock") && !id.contains("screensaver") && id!="ukui-fences") {
                        m_appId=id; m_appName=m_native->names.value(id,name);
                    }
                    if(cls.res_name)XFree(cls.res_name); if(cls.res_class)XFree(cls.res_class);
                }
            }
        }
        XSync(d,False); XSetErrorHandler(oldHandler);
    }
    emit changed();
}
void ActivityRecorder::flush() {
    if(!m_saveOk) return;
    QDir().mkpath(QFileInfo(dataPath()).absolutePath());
    QSaveFile f(dataPath());
    const auto data=QJsonDocument(m_ledger.toJson()).toJson(QJsonDocument::Compact);
    if(!f.open(QIODevice::WriteOnly) || f.write(data)!=data.size() || !f.commit()) {
        qWarning()<<"Could not save activity ledger"<<f.errorString();
        // Retry a transient failure on the next flush, while exposing the error.
        setProperty("saveError",true); emit changed();
    } else setProperty("saveError",false);
}
