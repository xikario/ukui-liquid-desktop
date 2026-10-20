#include "WindowEffectsSettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QStandardPaths>
#include <QPointer>
#include <QSignalBlocker>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusError>
#include <QDBusVariant>
#include <QDBusArgument>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QLockFile>
#include <QFileSystemWatcher>
#include <QTimer>
#include <dlfcn.h>
#include <functional>

namespace {
const QString magic=QStringLiteral("magiclamp"),maximize=QStringLiteral("kwin4_effect_maximize");
const QStringList alternatives={QStringLiteral("kwin4_effect_squash"),QStringLiteral("kwin4_effect_squashtablet")};
QString configPath() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui-kwinrc"; }
QString trialJournal() {return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-effects-trial.ini";}
QByteArray journalContents() {
    QFile file(trialJournal());return file.open(QIODevice::ReadOnly)?file.read(65536):QByteArray();
}
QString trialProgram() {
    const QString installed=QDir::homePath()+"/.local/bin/ukui-liquid-effects-trial";
    if(QFileInfo(installed).isExecutable())return installed;
    const QString beside=QCoreApplication::applicationDirPath()+"/ukui-liquid-effects-trial";
    if(QFileInfo(beside).isExecutable())return beside;
    Dl_info info{};
    if(dladdr(reinterpret_cast<void *>(&trialProgram),&info) && info.dli_fname) {
        const QString built=QFileInfo(QString::fromLocal8Bit(info.dli_fname)).dir().absoluteFilePath("../../ukui-liquid-effects-trial");
        if(QFileInfo(built).isExecutable())return built;
    }
    return QStandardPaths::findExecutable("ukui-liquid-effects-trial");
}
void configureTrialProcess(QProcess *process) {
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    const QString directory=QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)+"/ukui-panel-liquid";
    // A regular file survives the panel's exit. Never leave the helper writing
    // to the panel's QProcess pipes; a late log write could otherwise get EPIPE.
    process->setStandardErrorFile(QDir().mkpath(directory)
        ? directory+"/effects-trial.log":QProcess::nullDevice());
}
using Reply=std::function<void(const QDBusMessage &)>;
void call(QObject *context,const QString &method,const QVariantList &args,Reply done) {
    auto message=QDBusMessage::createMethodCall("org.ukui.KWin","/Effects","org.ukui.kwin.Effects",method);
    message.setArguments(args);
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1500),context);
    QObject::connect(watcher,&QDBusPendingCallWatcher::finished,context,[watcher,done]{
        const QDBusMessage reply=watcher->reply();watcher->deleteLater();done(reply);
    });
}
bool ok(const QDBusMessage &r) {return r.type()!=QDBusMessage::ErrorMessage;}
bool boolean(const QDBusMessage &r) {return ok(r) && r.arguments().size()==1 && r.arguments().first().toBool();}
QString failure(const QDBusMessage &r) {
    return "无法连接或更新系统窗口特效（"+r.errorName()+"）。请确认 UKUI KWin 正在运行后重新打开此页。";
}

// A change outlives its view: closing settings while a D-Bus request is in
// flight must not leave a runtime-only change that disappears on next login.
class Change final : public QObject {
public:
    Change(std::function<void(QString)> done):QObject(qApp),operationLock(trialJournal()+".operation-lock"),finished(std::move(done)) {}
    void toggle(const QString &effect,bool enabled) {
        if(!acquire())return;
        QSettings settings(configPath(),QSettings::IniFormat);
        if(!settings.isWritable()){finish("无法保存系统窗口动画，请检查配置文件权限。");return;}
        target=effect;desired=enabled;
        ids=QStringList{effect};if(effect==magic && enabled)ids.append(alternatives);
        readNext(0);
    }
    void duration(int value) {
        if(!acquire())return;
        QSettings s(configPath(),QSettings::IniFormat);
        const QString key="Effect-MagicLamp/AnimationDuration";
        const QVariant before=s.value(key);const bool existed=s.contains(key);
        s.setValue(key,value);s.sync();
        if(s.status()!=QSettings::NoError){finish("无法保存系统动画时长，请检查配置文件权限。");return;}
        call(this,"reconfigureEffect",{magic},[this,key,before,existed](const QDBusMessage &r){
            if(ok(r)){finish({});return;}
            QSettings s(configPath(),QSettings::IniFormat);
            if(existed)s.setValue(key,before);else s.remove(key);s.sync();
            finish(failure(r));
        });
    }
private:
    bool acquire() {
        QDir().mkpath(QFileInfo(trialJournal()).absolutePath());
        QSettings j(trialJournal(),QSettings::IniFormat);
        if(!operationLock.tryLock(0) || j.value("pending").toBool()) {
            finish("窗口后端正在试用或等待恢复，请先完成试用或恢复原后端。");return false;
        }
        return true;
    }
    void finish(const QString &error) {operationLock.unlock();finished(error);deleteLater();}
    void readNext(int i) {
        if(i==ids.size()) {changeNext(0);return;}
        call(this,"isEffectLoaded",{ids[i]},[this,i](const QDBusMessage &r){
            if(!ok(r)){finish(failure(r));return;}
            previous.append(boolean(r));readNext(i+1);
        });
    }
    void changeNext(int i) {
        if(i==ids.size()) {persist();return;}
        const bool on=i==0?desired:false;
        if(on==previous[i]){changeNext(i+1);return;}
        // Include this entry in rollback even if the request times out after
        // the compositor has performed it.
        changed.append(i);
        call(this,on?"loadEffect":"unloadEffect",{ids[i]},[this,i,on](const QDBusMessage &r){
            if(!ok(r) || (on && !boolean(r))) {rollback("系统未能切换窗口动画，已尝试恢复原状态。",changed.size()-1);return;}
            call(this,"isEffectLoaded",{ids[i]},[this,i,on](const QDBusMessage &check){
                if(!ok(check) || boolean(check)!=on){rollback("系统未能确认动画状态，已尝试恢复原状态。",changed.size()-1);return;}
                changeNext(i+1);
            });
        });
    }
    void rollback(const QString &error,int at) {
        if(at<0){finish(error);return;}
        const int i=changed[at];
        call(this,previous[i]?"loadEffect":"unloadEffect",{ids[i]},[this,error,at](const QDBusMessage &){rollback(error,at-1);});
    }
    void persist() {
        QSettings s(configPath(),QSettings::IniFormat);
        for(int i=0;i<ids.size();++i)s.setValue("Plugins/"+ids[i]+"Enabled",i==0?desired:false);
        // Older OEM settings expose this alias; keep it consistent while the
        // actual built-in effect is identified by "magiclamp".
        if(target==magic)s.setValue("Plugins/kwin4_effect_magiclampEnabled",desired);
        s.sync();
        if(s.status()!=QSettings::NoError){rollback("无法保存窗口动画设置，已尝试恢复原状态。",changed.size()-1);return;}
        finish({});
    }
    QString target;bool desired=false;
    QStringList ids;QList<bool> previous;QList<int> changed;
    QLockFile operationLock;
    std::function<void(QString)> finished;
};
}

WindowEffectsSettings::WindowEffectsSettings(QWidget *parent):QWidget(parent) {
    auto *form=new QFormLayout(this);
    auto *intro=new QLabel("以下设置作用于整个桌面的应用窗口，由系统 UKUI KWin 执行动画。",this);
    intro->setWordWrap(true);form->addRow(intro);
    m_backend=new QLabel("正在读取合成后端…",this);m_backend->setWordWrap(true);
    m_backend->setObjectName("liquidWindowBackendInfo");form->addRow(m_backend);
    auto *backendRow=new QWidget(this);auto *backendLayout=new QHBoxLayout(backendRow);backendLayout->setContentsMargins(0,0,0,0);
    m_trial=new QPushButton("试用 OpenGL 动画…",backendRow);m_trial->setObjectName("liquidWindowBackendTrial");m_trial->setEnabled(false);
    m_restoreBackend=new QPushButton("恢复切换前的后端",backendRow);m_restoreBackend->setObjectName("liquidWindowBackendRestore");m_restoreBackend->setEnabled(false);
    backendLayout->addWidget(m_trial);backendLayout->addWidget(m_restoreBackend);form->addRow(backendRow);
    connect(m_trial,&QPushButton::clicked,this,[this]{runBackendTrial(false);});
    connect(m_restoreBackend,&QPushButton::clicked,this,[this]{runBackendTrial(true);});
    m_magic=new QCheckBox("魔壶效果（最小化与恢复）",this);m_magic->setObjectName("liquidWindowMagicLamp");form->addRow(m_magic);
    auto *row=new QWidget(this);auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
    m_duration=new QSpinBox(row);m_duration->setObjectName("liquidWindowMagicDuration");
    m_duration->setRange(0,3000);m_duration->setSingleStep(50);m_duration->setSuffix(" ms");
    m_duration->setSpecialValueText("系统默认");m_duration->setKeyboardTracking(false);
    m_reset=new QPushButton("恢复默认时长",row);m_reset->setObjectName("liquidWindowMagicReset");
    layout->addWidget(m_duration);layout->addWidget(m_reset);form->addRow("魔壶动画时长",row);
    auto *hint=new QLabel("数值越大动作越慢；0 表示系统默认。实际速度还受系统全局动画速度影响。启用魔壶会关闭同类的挤压最小化动画。",this);
    hint->setWordWrap(true);form->addRow(hint);
    m_maximize=new QCheckBox("最大化与还原动画",this);m_maximize->setObjectName("liquidWindowMaximize");form->addRow(m_maximize);
    m_status=new QLabel(this);m_status->setWordWrap(true);m_status->setObjectName("liquidWindowEffectsStatus");form->addRow(m_status);
    connect(m_magic,&QCheckBox::toggled,this,[this](bool on){toggleEffect(magic,on);});
    connect(m_maximize,&QCheckBox::toggled,this,[this](bool on){toggleEffect(maximize,on);});
    connect(m_duration,qOverload<int>(&QSpinBox::valueChanged),this,&WindowEffectsSettings::saveDuration);
    connect(m_reset,&QPushButton::clicked,this,[this]{if(m_duration->value()!=0)m_duration->setValue(0);});
    auto *watch=new QFileSystemWatcher(this);
    auto *refresh=new QTimer(this);refresh->setSingleShot(true);refresh->setInterval(100);
    auto watchJournal=[watch]{
        for(const auto &path:{trialJournal(),QFileInfo(trialJournal()).absolutePath(),
                QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)})
            if(QFileInfo::exists(path) && !watch->files().contains(path) && !watch->directories().contains(path))watch->addPath(path);
    };
    watchJournal();
    connect(watch,&QFileSystemWatcher::fileChanged,refresh,qOverload<>(&QTimer::start));
    connect(watch,&QFileSystemWatcher::directoryChanged,refresh,qOverload<>(&QTimer::start));
    connect(refresh,&QTimer::timeout,this,[this,watchJournal]{
        watchJournal();if(!m_helperRunning && journalContents()!=m_journalContents)reload();
    });
    reload();
}
void WindowEffectsSettings::setBusy(bool busy) {
    m_busy=busy;
    m_trial->setEnabled(!busy && m_trialAvailable);m_restoreBackend->setEnabled(!busy && m_restoreAvailable);
    m_magic->setEnabled(!busy && !m_trialPending && m_magicSupported);m_maximize->setEnabled(!busy && !m_trialPending && m_maximizeSupported);
    m_duration->setEnabled(!busy && !m_trialPending && m_magicSupported);m_reset->setEnabled(!busy && !m_trialPending && m_magicSupported);
}
void WindowEffectsSettings::reload() {
    const int serial=++m_reloadGeneration;m_journalContents=journalContents();reloadBackend(serial);
    setBusy(true);m_status->setText("正在读取系统窗口动画…");
    {QSignalBlocker block(m_duration);QSettings s(configPath(),QSettings::IniFormat);m_duration->setValue(s.value("Effect-MagicLamp/AnimationDuration",0).toInt());}
    call(this,"isEffectSupported",{magic},[this,serial](const QDBusMessage &r){
        if(serial!=m_reloadGeneration)return;
        if(!ok(r)){readFailure(r);return;}m_magicSupported=boolean(r);
        call(this,"isEffectSupported",{maximize},[this,serial](const QDBusMessage &r){
            if(serial!=m_reloadGeneration)return;
            if(!ok(r)){readFailure(r);return;}m_maximizeSupported=boolean(r);
            call(this,"isEffectLoaded",{magic},[this,serial](const QDBusMessage &r){
                if(serial!=m_reloadGeneration)return;
                if(!ok(r)){readFailure(r);return;}
                {QSignalBlocker block(m_magic);m_magic->setChecked(boolean(r));}
                call(this,"isEffectLoaded",{maximize},[this,serial](const QDBusMessage &r){
                    if(serial!=m_reloadGeneration)return;
                    if(!ok(r)){readFailure(r);return;}
                    {QSignalBlocker block(m_maximize);m_maximize->setChecked(boolean(r));}
                    setBusy(false);m_status->setText(!m_operationError.isEmpty()?m_operationError:(m_magicSupported && m_maximizeSupported?
                        "更改后立即生效并保存。":"当前合成环境不支持的动画已禁用；可查看上方后端说明。"));
                });
            });
        });
    });
}
void WindowEffectsSettings::readFailure(const QDBusMessage &reply) {
    m_magicSupported=false;m_maximizeSupported=false;setBusy(false);m_status->setText(failure(reply));
}
void WindowEffectsSettings::recoverPendingTrial() {
    QSettings saved(trialJournal(),QSettings::IniFormat);
    if(!saved.value("pending").toBool())return;
    const QString program=trialProgram();
    if(!program.isEmpty()) {
        QProcess process;configureTrialProcess(&process);
        process.setProgram(program);process.setArguments({"--recover"});process.startDetached();
    }
}
void WindowEffectsSettings::reloadBackend(int serial) {
    m_trialAvailable=false;m_restoreAvailable=false;
    QSettings journal(trialJournal(),QSettings::IniFormat);m_trialPending=journal.value("pending").toBool();
    m_restoreAvailable=!trialProgram().isEmpty() && (m_trialPending || journal.value("saved").toBool());
    m_trial->setEnabled(false);m_restoreBackend->setEnabled(false);
    auto msg=QDBusMessage::createMethodCall("org.ukui.KWin","/Compositor","org.freedesktop.DBus.Properties","Get");
    msg.setArguments({"org.ukui.kwin.Compositing","compositingType"});
    auto *watch=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(watch,&QDBusPendingCallWatcher::finished,this,[this,watch,serial]{
        const QDBusMessage r=watch->reply();watch->deleteLater();
        if(serial!=m_reloadGeneration)return;
        if(!ok(r) || r.arguments().isEmpty()){m_backend->setText("无法读取当前合成后端；窗口特效仍按系统能力检测。");return;}
        const QString type=qvariant_cast<QDBusVariant>(r.arguments().first()).variant().toString();
        const bool openGL=type.startsWith("gl") || type.contains("opengl",Qt::CaseInsensitive);
        m_backend->setText(QString("当前后端：%1。%2\n独显与集显均按当前桌面显卡的驱动及合成能力判断；此处不切换应用所用显卡。试用后需在 20 秒内确认，否则自动恢复。").arg(type,
            openGL?"OpenGL 动画能力由系统特效接口确认。":"魔壶需要 OpenGL；XRender 不提供该动画。"));
        QSettings saved(trialJournal(),QSettings::IniFormat);
        const bool helper=!trialProgram().isEmpty();
        if(!helper)m_backend->setText(m_backend->text()+"\n后端试用程序未安装，请重新部署液态面板。");
        m_trialAvailable=helper && type=="xrender" && !saved.value("saved").toBool() && !saved.value("pending").toBool();
        m_restoreAvailable=helper && (saved.value("saved").toBool() || saved.value("pending").toBool());
        m_trial->setEnabled(!m_busy && m_trialAvailable);
        m_restoreBackend->setEnabled(!m_busy && m_restoreAvailable);
        auto query=QDBusMessage::createMethodCall("org.ukui.KWin","/Compositor","org.ukui.kwin.Compositing","getGPUStatus");
        auto *gpu=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(query,1500),this);
        connect(gpu,&QDBusPendingCallWatcher::finished,this,[this,gpu,serial]{
            const QDBusMessage result=gpu->reply();gpu->deleteLater();
            if(serial!=m_reloadGeneration)return;
            if(!ok(result) || result.arguments().isEmpty())return;
            const QVariantMap map=qdbus_cast<QVariantMap>(result.arguments().first());
            if(map.value("hasRestrictedDevice").toBool())
                m_backend->setText(m_backend->text()+QString("\n系统报告显卡兼容限制（%1:%2）。%3试用只设置用户后端，不修改系统显卡列表。").arg(map.value("vid").toString(),map.value("pid").toString(),
                    map.value("shouldBeXRenderBackend").toBool()?"自动策略倾向 XRender；":""));
        });
    });
}
void WindowEffectsSettings::runBackendTrial(bool restore) {
    const QString program=trialProgram();if(program.isEmpty()){m_status->setText("后端试用程序未安装，请重新部署液态面板。");return;}
    m_operationError.clear();m_helperRunning=true;setBusy(true);m_trial->setEnabled(false);m_restoreBackend->setEnabled(false);
    m_status->setText(restore?"正在恢复原后端…":"请在独立试用窗口中测试最小化与恢复，并确认保留或恢复。");
    // No QObject parent: the independent confirmation helper must outlive a
    // destroyed settings view or panel. Delete the process wrapper on exit.
    auto *process=new QProcess;configureTrialProcess(process);const QPointer<WindowEffectsSettings> guard(this);
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,[guard,process](int code,QProcess::ExitStatus status){
        if(guard){guard->m_helperRunning=false;if(code==3 && status==QProcess::NormalExit)
            guard->m_operationError="已有窗口后端操作正在运行，请先完成该操作。";
            else if(status!=QProcess::NormalExit || (code!=0 && code!=2))
            guard->m_operationError="后端试用或恢复未通过；已尝试恢复，请检查当前后端和恢复按钮。";
            guard->reload();}
        process->deleteLater();
    });
    connect(process,&QProcess::errorOccurred,process,[guard,process](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart)return;
        if(guard){guard->m_helperRunning=false;guard->m_operationError="无法启动后端试用程序。";guard->reload();}process->deleteLater();
    });
    process->start(program,restore?QStringList{"--restore"}:QStringList{});
}
void WindowEffectsSettings::toggleEffect(const QString &effect,bool on) {
    setBusy(true);m_status->setText("正在应用窗口动画…");
    const QPointer<WindowEffectsSettings> guard(this);
    auto *change=new Change([guard](const QString &error){
        if(!guard)return;
        if(error.isEmpty())guard->reload();
        else {guard->m_status->setText(error);/* leave disabled until reopened */}
    });
    change->toggle(effect,on);
}
void WindowEffectsSettings::saveDuration(int value) {
    setBusy(true);m_status->setText("正在保存动画时长…");
    const QPointer<WindowEffectsSettings> guard(this);
    auto *change=new Change([guard](const QString &error){
        if(!guard)return;
        if(error.isEmpty()){guard->setBusy(false);guard->m_status->setText("动画时长已保存并应用。");}
        else guard->m_status->setText(error);
    });
    change->duration(value);
}
