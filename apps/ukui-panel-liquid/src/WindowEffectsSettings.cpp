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
#include <functional>

namespace {
const QString magic=QStringLiteral("magiclamp"),maximize=QStringLiteral("kwin4_effect_maximize");
const QStringList alternatives={QStringLiteral("kwin4_effect_squash"),QStringLiteral("kwin4_effect_squashtablet")};
QString configPath() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui-kwinrc"; }
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
    Change(std::function<void(QString)> done):QObject(qApp),finished(std::move(done)) {}
    void toggle(const QString &effect,bool enabled) {
        QSettings settings(configPath(),QSettings::IniFormat);
        if(!settings.isWritable()){finish("无法保存系统窗口动画，请检查配置文件权限。");return;}
        target=effect;desired=enabled;
        ids=QStringList{effect};if(effect==magic && enabled)ids.append(alternatives);
        readNext(0);
    }
    void duration(int value) {
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
    void finish(const QString &error) {finished(error);deleteLater();}
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
    std::function<void(QString)> finished;
};
}

WindowEffectsSettings::WindowEffectsSettings(QWidget *parent):QWidget(parent) {
    auto *form=new QFormLayout(this);
    auto *intro=new QLabel("以下设置作用于整个桌面的应用窗口，由系统 UKUI KWin 执行动画。",this);
    intro->setWordWrap(true);form->addRow(intro);
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
    reload();
}
void WindowEffectsSettings::setBusy(bool busy) {
    m_magic->setEnabled(!busy && m_magicSupported);m_maximize->setEnabled(!busy && m_maximizeSupported);
    m_duration->setEnabled(!busy && m_magicSupported);m_reset->setEnabled(!busy && m_magicSupported);
}
void WindowEffectsSettings::reload() {
    setBusy(true);m_status->setText("正在读取系统窗口动画…");
    {QSignalBlocker block(m_duration);QSettings s(configPath(),QSettings::IniFormat);m_duration->setValue(s.value("Effect-MagicLamp/AnimationDuration",0).toInt());}
    call(this,"isEffectSupported",{magic},[this](const QDBusMessage &r){
        if(!ok(r)){m_status->setText(failure(r));return;}m_magicSupported=boolean(r);
        call(this,"isEffectSupported",{maximize},[this](const QDBusMessage &r){
            if(!ok(r)){m_status->setText(failure(r));return;}m_maximizeSupported=boolean(r);
            call(this,"isEffectLoaded",{magic},[this](const QDBusMessage &r){
                if(!ok(r)){m_status->setText(failure(r));return;}
                {QSignalBlocker block(m_magic);m_magic->setChecked(boolean(r));}
                call(this,"isEffectLoaded",{maximize},[this](const QDBusMessage &r){
                    if(!ok(r)){m_status->setText(failure(r));return;}
                    {QSignalBlocker block(m_maximize);m_maximize->setChecked(boolean(r));}
                    setBusy(false);m_status->setText(m_magicSupported && m_maximizeSupported?
                        "更改后立即生效并保存。":"当前系统不支持的动画已禁用。");
                });
            });
        });
    });
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
