#include "LiquidDialog.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include "PanelController.h"
#include "LiquidMaterialPreparation.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QWidget>
#include <QMenu>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QScreen>
#include <QFile>
#include <QMouseEvent>
#include <QPushButton>
#include <QFileInfo>
#include <QStyle>
#include "PanelNative.h"

QString PanelController::configFile() const {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-panel.ini";
}
PanelController::PanelController(QObject *parent):QObject(parent) {
    QSettings s(configFile(),QSettings::IniFormat);
    m_refraction=qBound(0.,s.value("appearance/refraction",3.5).toDouble(),8.);
    m_clarity=qBound(0.,s.value("appearance/clarity",0.).toDouble(),1.);
    m_liquidStrength=qBound(0.,s.value("appearance/liquidStrength",1.).toDouble(),2.);
    m_transparency=qBound(0.,s.value("appearance/transparency",.35).toDouble(),.65);
    m_seeThrough=s.value("appearance/seeThrough",false).toBool();
    m_chroma=qBound(0.,s.value("appearance/chroma",.48).toDouble(),1.3);
    m_enabled=s.value("appearance/enabled",true).toBool();
    m_followWallpaper=s.value("appearance/followWallpaper",true).toBool();
    m_reducedMotion=s.value("appearance/reducedMotion",false).toBool();
    m_surface.opacity=qBound(.15,s.value("appearance/opacity",.58).toDouble(),.95);
    m_surface.radius=qBound(0.,s.value("appearance/radius",16.).toDouble(),24.);
    m_surface.highlight=qBound(0.,s.value("appearance/highlight",.55).toDouble(),1.);
    LiquidPopup::install(*qApp);
    LiquidPopup::setEnabled(m_enabled);
    LiquidPopup::theme().reducedMotion=m_reducedMotion;
    LiquidPopup::theme().highlight=m_surface.highlight;
    QTimer::singleShot(0, this, [this] { refreshBackdrop(true); });
    m_wallpaperRefresh.setSingleShot(true);m_wallpaperRefresh.setInterval(250);
    connect(&m_wallpaperRefresh,&QTimer::timeout,this,[this]{if(m_followWallpaper)refreshBackdrop(false);});
    connect(&m_wallpaperWatcher,&QFileSystemWatcher::fileChanged,this,[this]{m_wallpaperRefresh.start();});
    connect(&m_wallpaperWatcher,&QFileSystemWatcher::directoryChanged,this,[this]{m_wallpaperRefresh.start();});
    updateWallpaperWatchers();
    m_hoverRefresh.setSingleShot(true);m_hoverRefresh.setInterval(32);
    m_previewRefresh.setSingleShot(true);m_previewRefresh.setInterval(16);
    connect(&m_previewRefresh,&QTimer::timeout,this,&PanelController::flushPreview);
    m_configSave.setSingleShot(true);m_configSave.setInterval(180);
    connect(&m_configSave,&QTimer::timeout,this,&PanelController::saveConfiguration);
    connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{if(m_configSave.isActive())saveConfiguration();});
    connect(&m_hoverRefresh,&QTimer::timeout,this,[this]{
        const auto damage=m_pointerDamage;
        m_pointerDamage.clear();
        for(auto i=damage.cbegin();i!=damage.cend();++i)
            if(i.key()->isVisible()) i.key()->update(i.value());
    });
    qApp->installEventFilter(this);
    for(QWidget *w:QApplication::topLevelWidgets()) attach(w);
    if (qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_PROFILE")) {
        auto *stats=new QTimer(this);stats->setInterval(5000);
        connect(stats,&QTimer::timeout,this,[this,stats,samples=6]() mutable {
            qInfo()<<"[LiquidPanelProfile] 5s paints"<<m_paints<<"cache builds"<<m_builds<<"dirty pixels"<<m_dirtyPixels;
            m_paints=0;m_builds=0;m_dirtyPixels=0;
            if (--samples==0) stats->stop();
        });stats->start();
    }
    qInfo()<<"[LiquidPanel] v2 optics loaded on original executable; theme enabled:"<<m_enabled;
}
void PanelController::attach(QWidget *w) {
    if (!w || !w->inherits("UKUIPanel") || w->property("liquidPanelAttached").toBool()) return;
    w->setProperty("liquidPanelAttached",true);
    w->setProperty("liquidPanelEnabled",m_enabled);
    m_cache.insert(w,{});
    m_preparations.insert(w,new LiquidMaterial::Preparation(this));
    m_originalMasks.insert(w,w->mask());
    connect(w,&QObject::destroyed,this,[this,w]{delete m_preparations.take(w);m_cache.remove(w);m_materialDirty.remove(w);m_pointers.remove(w);m_pointerDamage.remove(w);m_originalMasks.remove(w);});
    syncOutline(w);
    w->setMouseTracking(true);
    for(QWidget *child:w->findChildren<QWidget *>())child->setMouseTracking(true);
    qInfo()<<"[LiquidPanel] attached"<<w->metaObject()->className()<<w->size()
           <<"translucent:"<<w->testAttribute(Qt::WA_TranslucentBackground);
    w->update();
}
void PanelController::apply(bool materialChanged) {
    LiquidPopup::theme().reducedMotion=m_reducedMotion;
    LiquidPopup::theme().highlight=m_surface.highlight;
    LiquidPopup::setEnabled(m_enabled);
    m_pendingMaterial|=materialChanged;
    if(!m_previewRefresh.isActive())m_previewRefresh.start();
    m_configSave.start();
}
void PanelController::flushPreview() {
    m_previewRefresh.stop();
    const bool rebuild=m_pendingMaterial;m_pendingMaterial=false;
    qApp->setProperty("liquidPanelPreviewBatches",qApp->property("liquidPanelPreviewBatches").toInt()+1);
    for(auto it=m_cache.begin();it!=m_cache.end();++it) {
        if(rebuild)invalidateMaterial(it.key());
        it.key()->setProperty("liquidPanelEnabled",m_enabled);syncOutline(it.key());it.key()->update();
    }
}
void PanelController::saveConfiguration() {
    m_configSave.stop();
    QSettings s(configFile(),QSettings::IniFormat);
    s.setValue("appearance/refraction",m_refraction);
    s.setValue("appearance/chroma",m_chroma);
    s.setValue("appearance/clarity",m_clarity);
    s.setValue("appearance/liquidStrength",m_liquidStrength);
    s.setValue("appearance/transparency",m_transparency);
    s.setValue("appearance/seeThrough",m_seeThrough);
    s.setValue("appearance/enabled",m_enabled);
    s.setValue("appearance/followWallpaper",m_followWallpaper);
    s.setValue("appearance/opacity",m_surface.opacity);
    s.setValue("appearance/radius",m_surface.radius);
    s.setValue("appearance/highlight",m_surface.highlight);
    s.setValue("appearance/reducedMotion",m_reducedMotion);s.sync();
    qApp->setProperty("liquidPanelConfigWrites",qApp->property("liquidPanelConfigWrites").toInt()+1);
}
void PanelController::addMenu(QMenu *menu) {
    if (!menu || menu->property("liquidPanelMenu").toBool()) return;
    auto *owner=menu->parentWidget();
    if (!owner || !owner->inherits("UKUIPanel")) return;
    menu->setProperty("liquidPanelMenu",true);
    menu->addSeparator();
    auto *appearance=menu->addMenu("外观与特效");appearance->setObjectName("liquidPanelAppearanceMenu");
    auto *toggle=appearance->addAction(m_enabled?"液态主题（已开启）":"液态主题（已关闭）");
    toggle->setObjectName("liquidPanelEnabledAction");toggle->setCheckable(true);toggle->setChecked(m_enabled);
    connect(toggle,&QAction::triggered,this,[this,toggle](bool on){
        m_enabled=on;toggle->setText(on?"液态主题（已开启）":"液态主题（已关闭）");
        apply();flushPreview();saveConfiguration();
    });
    auto *refresh=appearance->addAction("刷新背景材质");
    connect(refresh,&QAction::triggered,this,[this]{refreshBackdrop();});
    auto *settings=appearance->addAction("液态外观设置…");settings->setObjectName("liquidPanelSettingsAction");
    connect(settings,&QAction::triggered,this,[this,owner]{settingsDialog(owner);});
}
void PanelController::settingsDialog(QWidget *owner) {
    if(m_dialog){LiquidDialog::reopen(m_dialog);return;}
    auto *dialog=new LiquidDialog::Dialog(owner,Qt::Dialog);m_dialog=dialog;
    // The OEM dock can be kept above applications; its settings are an
    // ordinary task window and must follow the user's chosen foreground.
    dialog->setWindowFlag(Qt::WindowStaysOnTopHint,false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("面板 · 液态外观");
    dialog->setWindowRole("liquid-panel-settings");
    dialog->setWindowIcon(QIcon::fromTheme("preferences-desktop",
        dialog->style()->standardIcon(QStyle::SP_DesktopIcon)));
    dialog->setMinimumWidth(400);
    auto *form=new QFormLayout(dialog);
    auto *intro=new QLabel("以桌面壁纸生成液态材质。清晰度调节磨砂，液态强度调节弯折；修改后立即保存。",dialog);
    intro->setWordWrap(true);form->addRow(intro);
    auto *follow=new QCheckBox("自适应壁纸（更换壁纸后自动更新材质）",dialog);
    follow->setObjectName("liquidPanelFollowWallpaper");follow->setChecked(m_followWallpaper);form->addRow(follow);
    connect(follow,&QCheckBox::toggled,this,[this](bool on){
        m_followWallpaper=on;updateWallpaperWatchers();if(on)refreshBackdrop();apply(false);
    });
    auto *refresh=new QPushButton("立即刷新背景材质",dialog);form->addRow(refresh);
    connect(refresh,&QPushButton::clicked,this,[this]{refreshBackdrop();});
    auto slider=[&](const QString &name,const QString &id,int low,int high,int value,auto change,bool material=true){
        auto *row=new QWidget(dialog);auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        auto *bar=new QSlider(Qt::Horizontal,row);bar->setObjectName(id);bar->setRange(low,high);bar->setValue(value);
        auto *number=new QLabel(QString::number(value),row);number->setMinimumWidth(32);
        layout->addWidget(bar);layout->addWidget(number);form->addRow(name,row);
        connect(bar,&QSlider::valueChanged,this,[this,number,change,material](int v){number->setText(QString::number(v));change(v);apply(material);});
        connect(bar,&QSlider::sliderReleased,this,[this]{
            if(m_previewRefresh.isActive())flushPreview();
            if(m_configSave.isActive())saveConfiguration();
        });
        return bar;
    };
    slider("背景压暗（%）","liquidPanelShade",15,95,qRound(m_surface.opacity*100),[this](int v){m_surface.opacity=v/100.;});
    slider("底板圆角","liquidPanelRadius",0,24,qRound(m_surface.radius),[this](int v){m_surface.radius=v;});
    slider("边缘高光（%）","liquidPanelHighlight",0,100,qRound(m_surface.highlight*100),[this](int v){m_surface.highlight=v/100.;});
    slider("液态强度（%）","liquidPanelStrength",0,200,qRound(m_liquidStrength*100),[this](int v){m_liquidStrength=v/100.;});
    slider("背景清晰度（%）","liquidPanelClarity",0,100,qRound(m_clarity*100),[this](int v){m_clarity=v/100.;});
    slider("背景色彩（%）","liquidPanelChroma",0,130,qRound(m_chroma*100),[this](int v){m_chroma=v/100.;});
    auto *seeThrough=new QCheckBox("透视后方窗口",dialog);
    seeThrough->setObjectName("liquidPanelSeeThrough");seeThrough->setChecked(m_seeThrough);form->addRow(seeThrough);
    auto *transparency=slider("窗口透视程度（%）","liquidPanelTransparency",0,65,qRound(m_transparency*100),[this](int v){m_transparency=v/100.;},false);
    transparency->setEnabled(m_seeThrough);
    connect(seeThrough,&QCheckBox::toggled,this,[this,transparency](bool on){m_seeThrough=on;transparency->setEnabled(on);apply(false);});
    auto *hint=new QLabel("透视只影响底板，图标和文字保持清晰；后方窗口自然透出，不做实时折射。",dialog);
    hint->setWordWrap(true);form->addRow(hint);
    auto *reduced=new QCheckBox("减弱菜单动画",dialog);reduced->setChecked(m_reducedMotion);form->addRow(reduced);
    connect(reduced,&QCheckBox::toggled,this,[this](bool on){m_reducedMotion=on;apply(false);});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    connect(dialog,&QDialog::finished,this,[this]{if(m_previewRefresh.isActive())flushPreview();if(m_configSave.isActive())saveConfiguration();});
    dialog->show();dialog->raise();dialog->activateWindow();
}
bool PanelController::eventFilter(QObject *obj,QEvent *e) {
    auto *w=qobject_cast<QWidget *>(obj);if(!w)return false;
    if(e->type()==QEvent::Polish || e->type()==QEvent::Show) {
        attach(w);
        if(auto *menu=qobject_cast<QMenu *>(w))addMenu(menu);
    }
    if(m_enabled && (e->type()==QEvent::MouseMove || e->type()==QEvent::Enter || e->type()==QEvent::Leave)) {
        QWidget *panel=w;
        while(panel && !m_cache.contains(panel))panel=panel->parentWidget();
        if(panel && m_cache.contains(panel)) {
            const QPoint global = e->type()==QEvent::MouseMove
                ? static_cast<QMouseEvent *>(e)->globalPos() : QCursor::pos();
            updatePointer(panel,panel->mapFromGlobal(global));
        }
    }
    if (!w->property("liquidPanelAttached").toBool())return false;
    if(e->type()==QEvent::Hide) {
        m_pointers.remove(w);
        m_pointerDamage.remove(w);
        if(m_pointerDamage.isEmpty())m_hoverRefresh.stop();
    }
    if(e->type()==QEvent::Move || e->type()==QEvent::Resize || e->type()==QEvent::PaletteChange || e->type()==QEvent::StyleChange || e->type()==QEvent::Show) {
        invalidateMaterial(w);
        // OEM may update native effects inside its own event handler.
        QPointer<QWidget> guard(w);
        QTimer::singleShot(0,this,[this,guard]{if(guard)syncOutline(guard);});
    }
    if(e->type()==QEvent::Paint && m_enabled) {
        ++m_paints;
        const auto region=static_cast<QPaintEvent *>(e)->region();
        for (const QRect &r:region) m_dirtyPixels+=quint64(r.width())*r.height();
        QImage &image=m_cache[w];
        if(m_materialDirty.contains(w) || image.isNull() || image.devicePixelRatio()!=w->devicePixelRatioF()) {
            m_materialDirty.remove(w);
            ++m_builds;
            w->setProperty("liquidMaterialBuilds",w->property("liquidMaterialBuilds").toInt()+1);
            QScreen *screen=QGuiApplication::screenAt(w->mapToGlobal(w->rect().center()));
            if(!screen)screen=QGuiApplication::primaryScreen();
            const QImage source=screen?m_wallpaper.sample(QRect(w->mapToGlobal(QPoint()),w->size()),screen->geometry(),w->devicePixelRatioF()):QImage();
            auto style=m_surface;style.light=w->palette().color(QPalette::Base).lightness()>150;
            if(image.isNull() || image.size()!=source.size() || image.devicePixelRatio()!=w->devicePixelRatioF()){
                image=LiquidPopup::renderSurface(w->size(),w->devicePixelRatioF(),style);
                w->setProperty("liquidOpticalGpu",false);
            }
            if(!source.isNull()) {
                const QPointer<QWidget> guard(w);
                const QRect requested(w->mapToGlobal(QPoint()),w->size());
                const qreal dpr=w->devicePixelRatioF();
                m_preparations[w]->request(source,[this,guard,requested,dpr](const LiquidMaterial::Prepared &material){
                    if(!guard || !m_enabled)return;
                    if(requested!=QRect(guard->mapToGlobal(QPoint()),guard->size()) || !qFuzzyCompare(dpr,guard->devicePixelRatioF())){
                        invalidateMaterial(guard);guard->update();return;
                    }
                    m_optics.setOptics(m_refraction,m_surface.opacity,m_surface.highlight,m_chroma);
                    m_optics.setMaterial(m_clarity,m_liquidStrength);
                    m_optics.setPreparedWallpaper(material);
                    m_cache[guard]=m_optics.renderPanel(QRect(QPoint(),guard->size()),m_surface.radius);
                    guard->setProperty("liquidOpticalGpu",m_optics.usedGpu());
                    guard->setProperty("liquidMaterialReady",true);
                    syncOutline(guard);guard->update();
                });
            }
        }
        // Match the native outline to physical optical coverage, including AA.
        if(w->property("liquidNativeOutlineKey").toLongLong()!=image.cacheKey())
            setPanelNativeOutline(w,image);
        QPainter p(w);p.setRenderHint(QPainter::Antialiasing);p.setClipRegion(region);p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(w->rect(),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // Fade the premultiplied material only; child icons/text paint normally.
        // KWin composites windows behind us, with no screenshots or blur request.
        p.setOpacity(m_seeThrough?1.-m_transparency:1.);
        p.drawImage(QPoint(),image);
        if(m_pointers.contains(w)) {
            const QPointF pos=m_pointers.value(w);
            const QRectF body=QRectF(w->rect()).adjusted(.75,.75,-.75,-.75);
            const qreal radius=qMin(m_surface.radius,qMin(body.width(),body.height())/2);
            QPainterPath rim;rim.setFillRule(Qt::OddEvenFill);
            rim.addRoundedRect(body,radius,radius);
            rim.addRoundedRect(body.adjusted(1.8,1.8,-1.8,-1.8),qMax(0.,radius-1.8),qMax(0.,radius-1.8));
            QRadialGradient light(QPointF(pos.x(),pos.y()<w->height()/2.?0:w->height()),95);
            light.setColorAt(0,QColor(244,253,255,qRound(165*m_surface.highlight)));
            light.setColorAt(.3,QColor(176,220,255,qRound(75*m_surface.highlight)));
            light.setColorAt(1,Qt::transparent);p.fillPath(rim,light);
        }
        // QWidget's child traversal still paints original buttons and plugins.
        return true;
    }
    return false;
}

void PanelController::invalidateMaterial(QWidget *w) {
    m_materialDirty.insert(w);
    if(auto *preparation=m_preparations.value(w))preparation->invalidate();
    w->setProperty("liquidMaterialReady",false);
}

void PanelController::syncOutline(QWidget *w) {
    if(!m_enabled) {
        const QRegion original=m_originalMasks.value(w);
        if(original.isEmpty())w->clearMask();else w->setMask(original);
    } else {
        if(!m_cache.value(w).isNull()) {
            setPanelNativeOutline(w,m_cache.value(w));
            setPanelNativeBackdrop(w,true);
            return;
        }
        QPainterPath outline;
        const qreal radius=qMin(m_surface.radius,qMin(w->width(),w->height())/2.);
        // Temporary paint envelope until the physical coverage is rendered.
        // Keep AA pixels; the cached alpha surface supplies the exact outline.
        outline.addRoundedRect(QRectF(w->rect()).adjusted(-2,-2,2,2),radius+2,radius+2);
        const QRegion logical=QRegion(outline.toFillPolygon().toPolygon()) & w->rect();
        if(w->mask()!=logical)w->setMask(logical);
    }
    setPanelNativeBackdrop(w,m_enabled);
}
void PanelController::updateWallpaperWatchers() {
    if(!m_wallpaperWatcher.files().isEmpty())m_wallpaperWatcher.removePaths(m_wallpaperWatcher.files());
    if(!m_wallpaperWatcher.directories().isEmpty())m_wallpaperWatcher.removePaths(m_wallpaperWatcher.directories());
    if(!m_followWallpaper){m_wallpaperRefresh.stop();return;}
    const QString cfg=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    for(const QString &path:QStringList{m_wallpaper.path(),cfg+"/kyfences/layout.json",cfg+"/dconf/user"}) {
        if(path.isEmpty())continue;
        if(QFile::exists(path))m_wallpaperWatcher.addPath(path);
        const QString parent=QFileInfo(path).absolutePath();
        // Atomic rename replaces the watched inode. Its directory survives.
        if(QDir(parent).exists() && !m_wallpaperWatcher.directories().contains(parent))m_wallpaperWatcher.addPath(parent);
    }
}
void PanelController::refreshBackdrop(bool force) {
    m_wallpaperForce = m_wallpaperForce || force;
    if (m_wallpaperLoading) { m_wallpaperPending = true; return; }
    m_wallpaperLoading = true; m_wallpaperPending = false;
    auto old = m_wallpaper;
    BackgroundTask::run(this, [old]() mutable {
        const bool changed = old.reload();
        return qMakePair(old, changed);
    }, [this](const QPair<WallpaperBackdrop, bool> &result) {
        m_wallpaperLoading = false;
        if (m_wallpaperPending) { refreshBackdrop(m_wallpaperForce); return; }
        m_wallpaper = result.first; updateWallpaperWatchers();
        const bool invalidate = result.second || m_wallpaperForce;
        m_wallpaperForce = false;
        if (invalidate)
            for (auto i=m_cache.begin();i!=m_cache.end();++i) { invalidateMaterial(i.key()); i.key()->update(); }
    });
}
void PanelController::updatePointer(QWidget *panel,const QPointF &pos) {
    const QPointF old=m_pointers.value(panel,QPointF(-1000,-1000));
    const bool inside=panel->rect().contains(pos.toPoint()) && panel->isVisible();
    if((inside && old==pos) || (!inside && !m_pointers.contains(panel)))return;
    QRegion &damage=m_pointerDamage[panel];
    if(m_pointers.contains(panel))damage |= QRect(qRound(old.x())-120,0,240,panel->height());
    if(inside) {
        m_pointers.insert(panel,pos);
        damage |= QRect(qRound(pos.x())-120,0,240,panel->height());
    } else m_pointers.remove(panel);
    // Coalesce old and new footprints once per frame, including pointer leave.
    if(!m_hoverRefresh.isActive())m_hoverRefresh.start();
}
