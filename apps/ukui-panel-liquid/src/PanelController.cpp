#include "../../../shared/async-work/BackgroundTask.h"
#include "PanelController.h"
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
    connect(&m_hoverRefresh,&QTimer::timeout,this,[this]{
        for(auto i=m_pointers.cbegin();i!=m_pointers.cend();++i)
            i.key()->update(QRect(qRound(i.value().x())-120,0,240,i.key()->height()));
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
    m_originalMasks.insert(w,w->mask());
    connect(w,&QObject::destroyed,this,[this,w]{m_cache.remove(w);m_pointers.remove(w);m_originalMasks.remove(w);});
    syncOutline(w);
    w->setMouseTracking(true);
    for(QWidget *child:w->findChildren<QWidget *>())child->setMouseTracking(true);
    qInfo()<<"[LiquidPanel] attached"<<w->metaObject()->className()<<w->size()
           <<"translucent:"<<w->testAttribute(Qt::WA_TranslucentBackground);
    w->update();
}
void PanelController::apply() {
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
    LiquidPopup::theme().reducedMotion=m_reducedMotion;
    LiquidPopup::theme().highlight=m_surface.highlight;
    LiquidPopup::setEnabled(m_enabled);
    for(auto it=m_cache.begin();it!=m_cache.end();++it) {
        it.value()={};it.key()->setProperty("liquidPanelEnabled",m_enabled);syncOutline(it.key());it.key()->update();
    }
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
        m_enabled=on;toggle->setText(on?"液态主题（已开启）":"液态主题（已关闭）");apply();
    });
    auto *refresh=appearance->addAction("刷新背景材质");
    connect(refresh,&QAction::triggered,this,[this]{refreshBackdrop();});
    auto *settings=appearance->addAction("液态外观设置…");settings->setObjectName("liquidPanelSettingsAction");
    connect(settings,&QAction::triggered,this,[this,owner]{settingsDialog(owner);});
}
void PanelController::settingsDialog(QWidget *owner) {
    if(m_dialog){m_dialog->show();m_dialog->raise();m_dialog->activateWindow();return;}
    auto *dialog=new QDialog(owner,Qt::Dialog);m_dialog=dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("面板 · 液态外观");
    dialog->setMinimumWidth(400);
    auto *form=new QFormLayout(dialog);
    auto *intro=new QLabel("以桌面壁纸生成液态材质。清晰度调节磨砂，液态强度调节弯折；修改后立即保存。",dialog);
    intro->setWordWrap(true);form->addRow(intro);
    auto *enabled=new QCheckBox("启用液态主题",dialog);enabled->setChecked(m_enabled);form->addRow(enabled);
    connect(enabled,&QCheckBox::toggled,this,[this](bool on){m_enabled=on;apply();});
    auto *follow=new QCheckBox("自适应壁纸（更换壁纸后自动更新材质）",dialog);
    follow->setObjectName("liquidPanelFollowWallpaper");follow->setChecked(m_followWallpaper);form->addRow(follow);
    connect(follow,&QCheckBox::toggled,this,[this](bool on){
        m_followWallpaper=on;updateWallpaperWatchers();if(on)refreshBackdrop();apply();
    });
    auto *refresh=new QPushButton("立即刷新背景材质",dialog);form->addRow(refresh);
    connect(refresh,&QPushButton::clicked,this,[this]{refreshBackdrop();});
    auto slider=[&](const QString &name,const QString &id,int low,int high,int value,auto change){
        auto *row=new QWidget(dialog);auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        auto *bar=new QSlider(Qt::Horizontal,row);bar->setObjectName(id);bar->setRange(low,high);bar->setValue(value);
        auto *number=new QLabel(QString::number(value),row);number->setMinimumWidth(32);
        layout->addWidget(bar);layout->addWidget(number);form->addRow(name,row);
        connect(bar,&QSlider::valueChanged,this,[this,number,change](int v){number->setText(QString::number(v));change(v);apply();});
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
    auto *transparency=slider("窗口透视程度（%）","liquidPanelTransparency",0,65,qRound(m_transparency*100),[this](int v){m_transparency=v/100.;});
    transparency->setEnabled(m_seeThrough);
    connect(seeThrough,&QCheckBox::toggled,this,[this,transparency](bool on){m_seeThrough=on;transparency->setEnabled(on);apply();});
    auto *hint=new QLabel("透视只影响底板，图标和文字保持清晰；后方窗口自然透出，不做实时折射。",dialog);
    hint->setWordWrap(true);form->addRow(hint);
    auto *reduced=new QCheckBox("减弱菜单动画",dialog);reduced->setChecked(m_reducedMotion);form->addRow(reduced);
    connect(reduced,&QCheckBox::toggled,this,[this](bool on){m_reducedMotion=on;apply();});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
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
        if(panel && m_cache.contains(panel))updatePointer(panel,panel->mapFromGlobal(QCursor::pos()));
    }
    if (!w->property("liquidPanelAttached").toBool())return false;
    if(e->type()==QEvent::Move || e->type()==QEvent::Resize || e->type()==QEvent::PaletteChange || e->type()==QEvent::StyleChange || e->type()==QEvent::Show) {
        m_cache[w]={};
        // OEM may update native effects inside its own event handler.
        QPointer<QWidget> guard(w);
        QTimer::singleShot(0,this,[this,guard]{if(guard)syncOutline(guard);});
    }
    if(e->type()==QEvent::Paint && m_enabled) {
        ++m_paints;
        const auto region=static_cast<QPaintEvent *>(e)->region();
        for (const QRect &r:region) m_dirtyPixels+=quint64(r.width())*r.height();
        QImage &image=m_cache[w];
        if(image.isNull() || image.devicePixelRatio()!=w->devicePixelRatioF()) {
            ++m_builds;
            QScreen *screen=QGuiApplication::screenAt(w->mapToGlobal(w->rect().center()));
            if(!screen)screen=QGuiApplication::primaryScreen();
            const QImage source=screen?m_wallpaper.sample(QRect(w->mapToGlobal(QPoint()),w->size()),screen->geometry(),w->devicePixelRatioF()):QImage();
            if(!source.isNull()) {
                m_optics.setOptics(m_refraction,m_surface.opacity,m_surface.highlight,m_chroma);
                m_optics.setMaterial(m_clarity,m_liquidStrength);
                m_optics.setWallpaper(source);
                image=m_optics.renderPanel(QRect(QPoint(),w->size()),m_surface.radius);
                w->setProperty("liquidOpticalGpu",m_optics.usedGpu());
                qInfo()<<"[LiquidPanelOptics]"<<(m_optics.usedGpu()?"Snell GPU":"blur fallback")<<w->size();
            } else {
                auto style=m_surface;style.light=w->palette().color(QPalette::Base).lightness()>150;
                image=LiquidPopup::renderSurface(w->size(),w->devicePixelRatioF(),style);
                w->setProperty("liquidOpticalGpu",false);
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
            for (auto i=m_cache.begin();i!=m_cache.end();++i) { i.value()={}; i.key()->update(); }
    });
}
void PanelController::updatePointer(QWidget *panel,const QPointF &pos) {
    const QPointF old=m_pointers.value(panel,QPointF(-1000,-1000));
    if(!panel->rect().contains(pos.toPoint())) {
        if(m_pointers.remove(panel))panel->update(QRect(qRound(old.x())-120,0,240,panel->height()));
        return;
    }
    if(old==pos)return;
    m_pointers.insert(panel,pos);
    panel->update(QRect(qRound(old.x())-120,0,240,panel->height()));
    if(!m_hoverRefresh.isActive())m_hoverRefresh.start();
}
