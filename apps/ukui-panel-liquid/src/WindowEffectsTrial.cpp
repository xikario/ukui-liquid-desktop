// This process owns the deadline; no compositor request blocks its event loop.
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusPendingCallWatcher>
#include <QIcon>
#include <QDebug>
#include <functional>

namespace {
const QStringList effects={"magiclamp","kwin4_effect_squash","kwin4_effect_squashtablet"};
const QStringList keys={"Compositing/Backend","Plugins/magiclampEnabled",
    "Plugins/kwin4_effect_magiclampEnabled","Plugins/kwin4_effect_squashEnabled",
    "Plugins/kwin4_effect_squashtabletEnabled"};
QString config(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui-kwinrc";}
QString journal(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-effects-trial.ini";}
bool gl(const QString &s){return s.startsWith("gl") || s.contains("opengl",Qt::CaseInsensitive);}
bool success(const QDBusMessage &r){return r.type()==QDBusMessage::ReplyMessage;}
bool boolReply(const QDBusMessage &r){
    return success(r) && r.arguments().size()==1 && r.arguments().first().type()==QVariant::Bool;
}
using Done=std::function<void(bool)>;

class Trial final:public QObject {
public:
    void snapshot(Done done){
        ++generation;
        QSettings j(journal(),QSettings::IniFormat),s(config(),QSettings::IniFormat);
        j.sync();s.sync();
        if(j.status()!=QSettings::NoError || s.status()!=QSettings::NoError
            || !j.isWritable() || !s.isWritable() || j.value("pending").toBool()
            || j.value("saved").toBool()){done(false);return;}
        saved.clear();
        for(const auto &k:keys){
            saved.insert("exists/"+k,s.contains(k));
            if(s.contains(k))saved.insert("before/"+k,s.value(k));
        }
        readBackend([this,done](const QString &type){
            if(type!="xrender" && !gl(type)){done(false);return;}
            saved.insert("backend",gl(type)?"OpenGL":"XRender");
            snapshotEffect(0,done);
        });
    }
    void prepare(Done done){
        request("/Compositor","org.freedesktop.DBus.Properties","Set",
            {"org.ukui.kwin.Compositing","compositingType",QVariant::fromValue(QDBusVariant("OpenGL"))},
            [done](const QDBusMessage &r){done(success(r));});
    }
    void verify(Done done){
        readBackend([this,done](const QString &type){
            if(!gl(type)){done(false);return;}
            queryEffect("isEffectSupported","magiclamp",[this,done](bool ok,bool supported){
                if(!ok || !supported){done(false);return;}
                disableAlternative(1,done);
            });
        });
    }
    void commit(const std::function<bool()> &inTime,Done done){
        // Recheck without mutating competing effects again. Errors are never
        // interpreted as an unloaded effect, including during confirmation.
        readBackend([this,inTime,done](const QString &type){
            if(!gl(type)){done(false);return;}
            queryEffect("isEffectSupported","magiclamp",[this,inTime,done](bool ok,bool supported){
                if(!ok || !supported){done(false);return;}
                confirmEffect(0,inTime,done);
            });
        });
    }
    void restore(Done done){
        // Invalidates outstanding verification/commit callbacks before issuing
        // recovery requests on the same bus connection.
        const int serial=++generation;
        QSettings j(journal(),QSettings::IniFormat);j.sync();
        if(j.status()!=QSettings::NoError){done(false);return;}
        if(!j.value("pending").toBool() && !j.value("saved").toBool()){done(true);return;}
        saved.clear();for(const auto &key:j.allKeys())saved.insert(key,j.value(key));
        if(!validSnapshot()){qWarning()<<"Incomplete recovery journal; refusing to guess";done(false);return;}
        // Persist the baseline even if the compositor is unavailable. A failed
        // runtime recovery retains the complete journal for the next startup.
        QSettings s(config(),QSettings::IniFormat);
        for(const auto &key:keys){
            if(saved.value("exists/"+key).toBool())s.setValue(key,saved.value("before/"+key));
            else s.remove(key);
        }
        s.sync();const bool diskOk=s.status()==QSettings::NoError;
        // Recovery itself is bounded; an unresponsive KWin cannot trap the UI.
        QTimer::singleShot(12000,this,[this,serial,done]{
            if(serial!=generation)return;
            ++generation;qWarning()<<"Recovery timed out; journal retained";done(false);
        });
        const QString target=saved.value("backend").toString();
        request("/Compositor","org.freedesktop.DBus.Properties","Set",
            {"org.ukui.kwin.Compositing","compositingType",QVariant::fromValue(QDBusVariant(target))},
            [this,diskOk,done](const QDBusMessage &r){restoreEffect(0,diskOk && success(r),done);});
    }
private:
    using Reply=std::function<void(const QDBusMessage &)>;
    void request(const QString &path,const QString &iface,const QString &method,const QVariantList &args,Reply done){
        auto m=QDBusMessage::createMethodCall("org.ukui.KWin",path,iface,method);m.setArguments(args);
        const int serial=generation;
        auto *watch=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1500),this);
        connect(watch,&QDBusPendingCallWatcher::finished,this,[this,serial,watch,done]{
            const auto r=watch->reply();watch->deleteLater();
            if(serial==generation)done(r);
        });
    }
    void readBackend(std::function<void(QString)> done){
        request("/Compositor","org.freedesktop.DBus.Properties","Get",{"org.ukui.kwin.Compositing","compositingType"},
            [done](const QDBusMessage &r){
                done(success(r) && r.arguments().size()==1
                    ? qvariant_cast<QDBusVariant>(r.arguments().first()).variant().toString():QString());
            });
    }
    void queryEffect(const QString &method,const QString &id,std::function<void(bool,bool)> done){
        request("/Effects","org.ukui.kwin.Effects",method,{id},[done](const QDBusMessage &r){
            done(boolReply(r),boolReply(r) && r.arguments().first().toBool());
        });
    }
    void snapshotEffect(int i,Done done){
        if(i==effects.size()){
            saved.insert("pending",true);saved.insert("saved",false);
            QSettings j(journal(),QSettings::IniFormat);j.clear();
            for(auto it=saved.cbegin();it!=saved.cend();++it)j.setValue(it.key(),it.value());
            j.sync();done(j.status()==QSettings::NoError);return;
        }
        queryEffect("isEffectLoaded",effects[i],[this,i,done](bool ok,bool loaded){
            if(!ok){done(false);return;}
            saved.insert("loaded/"+effects[i],loaded);snapshotEffect(i+1,done);
        });
    }
    void disableAlternative(int i,Done done){
        if(i==effects.size()){
            queryEffect("isEffectLoaded","magiclamp",[this,done](bool ok,bool loaded){
                if(!ok){done(false);return;}
                if(loaded){done(true);return;}
                queryEffect("loadEffect","magiclamp",[this,done](bool ok,bool loaded){
                    if(!ok || !loaded){done(false);return;}
                    queryEffect("isEffectLoaded","magiclamp",[done](bool ok,bool loaded){done(ok && loaded);});
                });
            });return;
        }
        request("/Effects","org.ukui.kwin.Effects","unloadEffect",{effects[i]},[this,i,done](const QDBusMessage &r){
            if(!success(r)){done(false);return;}
            queryEffect("isEffectLoaded",effects[i],[this,i,done](bool ok,bool loaded){
                if(!ok || loaded){done(false);return;}
                disableAlternative(i+1,done);
            });
        });
    }
    void confirmEffect(int i,const std::function<bool()> &inTime,Done done){
        if(i<effects.size()){
            queryEffect("isEffectLoaded",effects[i],[this,i,inTime,done](bool ok,bool loaded){
                if(!ok || loaded!=(i==0)){done(false);return;}
                confirmEffect(i+1,inTime,done);
            });return;
        }
        if(!inTime()){done(false);return;}
        QSettings s(config(),QSettings::IniFormat);
        s.setValue("Compositing/Backend","OpenGL"); // Leave OEM AutoBackend untouched.
        s.setValue("Plugins/magiclampEnabled",true);s.setValue("Plugins/kwin4_effect_magiclampEnabled",true);
        for(int i=1;i<effects.size();++i)s.setValue("Plugins/"+effects[i]+"Enabled",false);
        s.sync();if(s.status()!=QSettings::NoError || !inTime()){done(false);return;}
        QSettings j(journal(),QSettings::IniFormat);j.setValue("pending",false);j.setValue("saved",true);j.sync();
        done(j.status()==QSettings::NoError);
    }
    bool validSnapshot()const{
        const QString target=saved.value("backend").toString();
        if(target!="OpenGL" && target!="XRender")return false;
        for(const auto &key:keys){
            if(!saved.contains("exists/"+key))return false;
            if(saved.value("exists/"+key).toBool() && !saved.contains("before/"+key))return false;
        }
        for(const auto &id:effects)if(!saved.contains("loaded/"+id))return false;
        return true;
    }
    void restoreEffect(int i,bool ok,Done done){
        if(i==effects.size()){
            readBackend([this,ok,done](const QString &type){
                const QString target=saved.value("backend").toString();
                bool restored=ok && (target=="OpenGL"?gl(type):type=="xrender");
                if(restored){
                    QSettings j(journal(),QSettings::IniFormat);j.clear();j.sync();
                    restored=j.status()==QSettings::NoError;
                }
                ++generation;qInfo()<<"RESTORE"<<restored<<type;done(restored);
            });return;
        }
        const bool wanted=saved.value("loaded/"+effects[i]).toBool();
        queryEffect("isEffectLoaded",effects[i],[this,i,ok,wanted,done](bool readOk,bool loaded){
            if(!readOk){restoreEffect(i+1,false,done);return;}
            if(loaded==wanted){restoreEffect(i+1,ok,done);return;}
            request("/Effects","org.ukui.kwin.Effects",wanted?"loadEffect":"unloadEffect",{effects[i]},
                [this,i,ok,wanted,done](const QDBusMessage &r){
                    const bool changed=success(r) && (!wanted || (boolReply(r) && r.arguments().first().toBool()));
                    queryEffect("isEffectLoaded",effects[i],[this,i,ok,wanted,changed,done](bool readOk,bool loaded){
                        restoreEffect(i+1,ok && changed && readOk && loaded==wanted,done);
                    });
                });
        });
    }
    int generation=0;
    QVariantMap saved;
};
}

int main(int argc,char **argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("ukui-liquid-effects-trial");app.setApplicationDisplayName("窗口动画试用");
    app.setWindowIcon(QIcon::fromTheme("preferences-desktop-effects",QIcon::fromTheme("preferences-system")));
    if(!QDir().mkpath(QFileInfo(journal()).absolutePath()))return 1;
    QLockFile lock(journal()+".operation-lock");if(!lock.tryLock(0))return 3;
    Trial trial;const auto args=app.arguments();
    const bool restoreOnly=args.contains("--restore"),recoverOnly=args.contains("--recover");
    if(restoreOnly || recoverOnly){
        QTimer::singleShot(0,&app,[&]{
            QSettings j(journal(),QSettings::IniFormat);j.sync();
            if(j.status()!=QSettings::NoError){app.exit(1);return;}
            if(recoverOnly && !j.value("pending").toBool()){app.exit(0);return;}
            trial.restore([&](bool ok){app.exit(ok?0:1);});
        });return app.exec();
    }
    QDialog dialog(nullptr,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowSystemMenuHint
        |Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint);
    dialog.setWindowTitle("OpenGL 窗口动画试用");dialog.setMinimumWidth(440);
    auto *layout=new QVBoxLayout(&dialog);
    auto *label=new QLabel("正在读取切换前状态；尚未修改后端…",&dialog);label->setWordWrap(true);layout->addWidget(label);
    auto *buttons=new QDialogButtonBox(&dialog);
    auto *keep=buttons->addButton("保留 OpenGL 与魔壶",QDialogButtonBox::AcceptRole);keep->setEnabled(false);
    auto *revert=buttons->addButton("立即恢复",QDialogButtonBox::RejectRole);layout->addWidget(buttons);
    bool finished=false,ready=false,failed=false,restoring=false;
    QElapsedTimer elapsed;QTimer deadline;deadline.setInterval(100);
    auto restore=[&]{
        if(finished || restoring)return;
        restoring=true;deadline.stop();keep->setEnabled(false);revert->setEnabled(false);
        label->setText("正在恢复切换前状态…");
        trial.restore([&](bool ok){finished=true;app.exit(ok?(failed?1:2):1);});
    };
    QObject::connect(revert,&QPushButton::clicked,&dialog,restore);
    QObject::connect(&dialog,&QDialog::rejected,&dialog,restore);
    QObject::connect(keep,&QPushButton::clicked,&dialog,[&]{
        if(restoring || finished)return;
        if(!elapsed.isValid() || elapsed.elapsed()>=20000){restore();return;}
        keep->setEnabled(false);
        trial.commit([&]{return elapsed.elapsed()<20000;},[&](bool ok){
            if(ok){finished=true;deadline.stop();qInfo()<<"COMMITTED";app.exit(0);}
            else {failed=true;restore();}
        });
    });
    QObject::connect(&deadline,&QTimer::timeout,&dialog,[&]{
        const int remaining=20000-int(elapsed.elapsed());
        if(remaining<=0){restore();return;}
        label->setText((ready?QString("已试用 OpenGL 与魔壶动画。请测试窗口最小化和恢复；%1 秒内未确认将恢复原后端。"):
            QString("正在检查 OpenGL；%1 秒内未确认将恢复原后端。")).arg((remaining+999)/1000));
    });
    auto start=[&]{
        trial.snapshot([&](bool ok){
            if(!ok){qWarning()<<"Cannot snapshot KWin or a saved trial already exists";app.exit(1);return;}
            elapsed.start();deadline.start();
            trial.prepare([&](bool ok){
                if(!ok){failed=true;restore();return;}
                QTimer::singleShot(1800,&dialog,[&]{
                    if(restoring || finished)return;
                    trial.verify([&](bool ok){
                        if(!ok){failed=true;restore();return;}
                        if(restoring || finished || elapsed.elapsed()>=20000){restore();return;}
                        qInfo()<<"READY";ready=true;keep->setEnabled(true);
                        if(args.contains("--verify-only"))QTimer::singleShot(1000,&dialog,restore);
                        if(args.contains("--enable"))keep->click();
                    });
                });
            });
        });
    };
    dialog.show();dialog.raise();dialog.activateWindow();
    QTimer::singleShot(0,&dialog,[&]{
        QSettings previous(journal(),QSettings::IniFormat);
        if(previous.value("pending").toBool()){
            trial.restore([&,start](bool ok){if(ok)start();else app.exit(1);});
        }else start();
    });
    return app.exec();
}
