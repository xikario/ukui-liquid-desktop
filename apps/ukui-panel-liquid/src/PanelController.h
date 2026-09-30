#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QDialog>
#include "LiquidSurface.h"
#include "LiquidOpticsRenderer.h"
#include "WallpaperBackdrop.h"
#include <QFileSystemWatcher>
#include <QTimer>
class QWidget;
class QMenu;
namespace LiquidMaterial { class Preparation; }
class PanelController final : public QObject {
public:
    explicit PanelController(QObject *parent);
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    void attach(QWidget *);
    void addMenu(QMenu *);
    void settingsDialog(QWidget *);
    void apply();
    void refreshBackdrop(bool force = true);
    void updateWallpaperWatchers();
    void syncOutline(QWidget *);
    void updatePointer(QWidget *,const QPointF &);
    void invalidateMaterial(QWidget *);
    QString configFile() const;
    bool m_enabled=true, m_reducedMotion=false, m_followWallpaper=true;
    LiquidPopup::SurfaceStyle m_surface;
    qreal m_refraction=3.5;
    qreal m_clarity=0, m_liquidStrength=1, m_transparency=.35;
    bool m_seeThrough=false;
    qreal m_chroma=.48;
    WallpaperBackdrop m_wallpaper;
    bool m_wallpaperLoading = false, m_wallpaperPending = false, m_wallpaperForce = false;
    QFileSystemWatcher m_wallpaperWatcher;
    QTimer m_wallpaperRefresh;
    QTimer m_hoverRefresh;
    QHash<QWidget *,QPointF> m_pointers;
    LiquidOpticsRenderer m_optics;
    QHash<QWidget *,QImage> m_cache;
    QSet<QWidget *> m_materialDirty;
    QHash<QWidget *,LiquidMaterial::Preparation *> m_preparations;
    QHash<QWidget *,QRegion> m_originalMasks;
    QPointer<QDialog> m_dialog;
    quint64 m_paints=0, m_builds=0, m_dirtyPixels=0;
};
