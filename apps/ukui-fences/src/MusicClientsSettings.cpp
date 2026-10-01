#include "MusicClientsSettings.h"
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QRegularExpression>

namespace {
QString profileLabel(const MusicClientProfile &profile) {
    return profile.name+(profile.enabled?" · 已启用":" · 已停用")+"\n"+profile.service;
}
}

MusicClientsSettings::MusicClientsSettings(QWidget *parent)
    : QWidget(parent), m_profiles(MprisPlayer::loadProfiles()), m_discovery(new MprisPlayer(this,false)) {
    setObjectName("musicClientsForm");
    setProperty("settingsManagesDraft",true);
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(10);
    auto *hint=new QLabel("先手动打开一次播放器，再添加已运行的客户端，自动识别启动信息。多个同时运行时选择最后启动的，退出后回到仍在运行的客户端。仅支持提供 MPRIS 接口的播放器。",this);
    hint->setWordWrap(true); hint->setProperty("hint",true); layout->addWidget(hint);
    m_clients=new QListWidget(this); m_clients->setObjectName("musicClients");
    m_clients->setStyleSheet("QListWidget::indicator {width:15px;height:15px;border:1px solid #bbd9df;border-radius:3px;background:#203746;} QListWidget::indicator:checked {background:#52c7b8;border-color:#bbf1e5;}");
    m_clients->setMinimumHeight(145); m_clients->setMaximumHeight(210); layout->addWidget(m_clients);
    auto *discoveryRow=new QHBoxLayout;
    m_available=new QComboBox(this); m_available->setObjectName("musicDetectedClients");
    m_available->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_available->setMinimumContentsLength(12);
    discoveryRow->addWidget(m_available,1);
    auto *detected=new QPushButton("添加已运行的客户端",this); detected->setObjectName("musicAddDetected"); discoveryRow->addWidget(detected);
    layout->addLayout(discoveryRow);
    auto *editRow=new QHBoxLayout;
    auto *add=new QPushButton("手动添加",this); add->setObjectName("musicAddCustom"); editRow->addWidget(add);
    auto *remove=new QPushButton("移除所选",this); remove->setObjectName("musicRemoveClient"); editRow->addWidget(remove); editRow->addStretch(); layout->addLayout(editRow);
    auto *form=new QFormLayout; form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_name=new QLineEdit(this); m_name->setObjectName("musicClientName"); form->addRow("显示名称",m_name);
    m_service=new QLineEdit(this); m_service->setObjectName("musicClientService"); m_service->setPlaceholderText("org.mpris.MediaPlayer2.yesplaymusic"); form->addRow("MPRIS 名称",m_service);
    auto *programRow=new QHBoxLayout;
    m_program=new QLineEdit(this); m_program->setObjectName("musicClientProgram"); m_program->setPlaceholderText("可留空；程序名或可执行文件的完整路径"); programRow->addWidget(m_program,1);
    auto *identify=new QPushButton("从运行进程识别",this); identify->setObjectName("musicIdentifyProgram"); programRow->addWidget(identify); form->addRow("启动程序",programRow);
    m_arguments=new QPlainTextEdit(this); m_arguments->setObjectName("musicClientArguments");
    m_arguments->setPlaceholderText("可留空；每行一个参数，路径含空格也无需加引号"); m_arguments->setMaximumHeight(80); form->addRow("启动参数",m_arguments);
    layout->addLayout(form);
    m_launchHint=new QLabel(this);m_launchHint->setObjectName("musicLaunchHint");m_launchHint->setWordWrap(true);m_launchHint->setTextFormat(Qt::PlainText);m_launchHint->setProperty("hint",true);layout->addWidget(m_launchHint);
    auto *details=new QLabel("MPRIS 名称匹配客户端及其多个实例。自动切换只改变小组件的控制对象。启动程序与参数分开填写；运行中的客户端可不填启动程序。",this);
    details->setWordWrap(true); details->setProperty("hint",true); layout->addWidget(details);
    m_status=new QLabel(this); m_status->setObjectName("musicClientsStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply,this); buttons->setObjectName("musicClientsButtons"); layout->addWidget(buttons);
    connect(m_clients,&QListWidget::currentRowChanged,this,&MusicClientsSettings::showProfile);
    connect(m_clients,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){
        if(m_loading)return; const int row=m_clients->row(item); if(row<0 || row>=m_profiles.size())return;
        m_profiles[row].enabled=item->checkState()==Qt::Checked; setProperty("settingsDirty",true);
        const QSignalBlocker block(m_clients); item->setText(profileLabel(m_profiles[row]));
    });
    for(auto *editor:{m_name,m_service,m_program})connect(editor,&QLineEdit::textEdited,this,[this]{editProfile();});
    connect(m_arguments,&QPlainTextEdit::textChanged,this,&MusicClientsSettings::editProfile);
    connect(detected,&QPushButton::clicked,this,[this]{const QString service=m_available->currentData().toString(); if(!service.isEmpty())addProfile(service);});
    connect(add,&QPushButton::clicked,this,[this]{addProfile({});});
    connect(remove,&QPushButton::clicked,this,[this]{
        const int row=m_clients->currentRow(); if(row<0)return; m_profiles.removeAt(row); rebuild(qMin(row,m_profiles.size()-1)); setProperty("settingsDirty",true);
    });
    connect(identify,&QPushButton::clicked,this,[this]{
        if(identifyProfile(m_clients->currentRow()))m_status->setText("已从运行进程识别启动信息，点击应用保存。");
        else m_status->setText("请先手动打开该播放器，等待识别完成后重试。");
    });
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this]{
        QString error;
        if(!MprisPlayer::saveProfiles(m_profiles,&error)){m_status->setText(error);return;}
        setProperty("settingsDirty",false); m_status->setText("已应用。自动接入配置立即生效。"); emit configurationApplied();
    });
    connect(m_discovery,&MprisPlayer::clientsChanged,this,&MusicClientsSettings::refreshAvailable);
    rebuild(m_profiles.isEmpty()?-1:0); refreshAvailable();
}
void MusicClientsSettings::rebuild(int selected) {
    // Clearing/repopulating emits selection and item-change signals. Block the
    // whole rebuild so nested selection handlers cannot edit enabled profiles.
    const QSignalBlocker block(m_clients);
    m_loading=true; m_clients->clear();
    for(const auto &p:m_profiles) {
        auto *item=new QListWidgetItem(profileLabel(p),m_clients);
        item->setFlags(item->flags()|Qt::ItemIsUserCheckable); item->setCheckState(p.enabled?Qt::Checked:Qt::Unchecked);
    }
    m_loading=false; m_clients->setCurrentRow(selected); showProfile(selected);
}
void MusicClientsSettings::showProfile(int row) {
    m_loading=true; const bool exists=row>=0 && row<m_profiles.size();
    const MusicClientProfile p=exists?m_profiles[row]:MusicClientProfile{};
    m_name->setText(p.name); m_service->setText(p.service); m_program->setText(p.program); m_arguments->setPlainText(p.arguments.join('\n'));
    for(auto *editor:{m_name,m_service,m_program})editor->setEnabled(exists); m_arguments->setEnabled(exists);
    m_launchHint->setText(!p.desktopFile.isEmpty()?"启动时使用自动识别的桌面入口："+p.desktopFile:!p.workingDirectory.isEmpty()?"已识别运行进程与工作目录："+p.workingDirectory:QString());
    findChild<QPushButton *>("musicIdentifyProgram")->setEnabled(exists); findChild<QPushButton *>("musicRemoveClient")->setEnabled(exists); m_loading=false;
}
void MusicClientsSettings::editProfile() {
    const int row=m_clients->currentRow(); if(m_loading || row<0 || row>=m_profiles.size())return;
    auto &p=m_profiles[row]; p.name=m_name->text().trimmed(); p.service=m_service->text().trimmed(); p.program=m_program->text().trimmed();
    if(sender()==m_program || sender()==m_arguments || sender()==m_service){p.desktopFile.clear();p.workingDirectory.clear();m_launchHint->clear();}
    p.arguments=m_arguments->toPlainText().isEmpty()?QStringList():m_arguments->toPlainText().split('\n');
    const QSignalBlocker block(m_clients); m_clients->item(row)->setText(profileLabel(p)); setProperty("settingsDirty",true); m_status->clear();
}
void MusicClientsSettings::addProfile(const QString &detected) {
    QString service=detected;
    service.remove(QRegularExpression("\\.(?:instance[A-Za-z0-9_-]*|pid[0-9]+)$"));
    for(int i=0;i<m_profiles.size();++i) if(!service.isEmpty() && m_profiles[i].service==service){m_clients->setCurrentRow(i);identifyProfile(i);m_status->setText("该客户端已在列表中，已更新可识别的启动信息。点击应用保存。");return;}
    if(m_profiles.size()>=32){m_status->setText("最多配置 32 个客户端。");return;}
    m_profiles << MusicClientProfile{service.isEmpty()?QString("新客户端"):MprisPlayer::suggestedName(service),service,{}, {},true};
    rebuild(m_profiles.size()-1); setProperty("settingsDirty",true); m_status->clear();
    if(!service.isEmpty())identifyProfile(m_profiles.size()-1);
    if(service.isEmpty())m_service->setFocus();
}
void MusicClientsSettings::refreshAvailable() {
    const QString selected=m_available->currentData().toString();
    const QSignalBlocker block(m_available); m_available->clear();
    for(const auto &service:m_discovery->availableServices())m_available->addItem(MprisPlayer::suggestedName(service)+" · "+service,service);
    if(m_available->count()==0)m_available->addItem(m_discovery->discovering()?"正在检测…":"未检测到 MPRIS 客户端",QString());
    const int index=m_available->findData(selected); if(index>=0)m_available->setCurrentIndex(index);
    findChild<QPushButton *>("musicAddDetected")->setEnabled(!m_available->currentData().toString().isEmpty());
    for(int i=0;i<m_profiles.size();++i)
        if(m_profiles[i].program.isEmpty() && m_profiles[i].desktopFile.isEmpty())identifyProfile(i);
}
bool MusicClientsSettings::identifyProfile(int row) {
    if(row<0 || row>=m_profiles.size())return false;
    const auto launch=m_discovery->detectedProfile(m_profiles[row].service);
    if(launch.program.isEmpty() && launch.desktopFile.isEmpty())return false;
    auto &profile=m_profiles[row];
    if(profile.program==launch.program && profile.arguments==launch.arguments
       && profile.desktopFile==launch.desktopFile && profile.workingDirectory==launch.workingDirectory)return true;
    profile.program=launch.program;profile.arguments=launch.arguments;
    profile.desktopFile=launch.desktopFile;profile.workingDirectory=launch.workingDirectory;
    setProperty("settingsDirty",true);
    if(row==m_clients->currentRow())showProfile(row);
    return true;
}
