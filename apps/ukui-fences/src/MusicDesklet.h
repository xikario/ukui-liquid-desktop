#pragma once
#include "DesktopWidgets.h"
#include <QElapsedTimer>
class MprisPlayer;
class QSlider;
class MusicDesklet final : public LiquidDesklet {
    Q_OBJECT
    friend struct PointerEffectTestAccess;
public:
    explicit MusicDesklet(DesktopCanvas *canvas);
    MprisPlayer *player() const { return m_player; }
    bool notesAnimating() const { return m_notesTimer.isActive(); }
protected:
    void paintContent(QPainter &) override;
    void arrangeControls() override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void extendMenu(QMenu &) override;
private:
    void updateControls();
    void syncNotesAnimation();
    QRect notesArea() const;
    void paintFloatingNotes(QPainter &p);
    QRegion notesDamage(qreal seconds) const;
    qreal m_notesFrameSeconds=0;
    QTimer m_notesTimer;
    QElapsedTimer m_notesClock;
    MprisPlayer *m_player = nullptr;
    QPushButton *m_previous, *m_play, *m_next, *m_open, *m_cover;
    QSlider *m_seek, *m_volume;
    QString m_seekTrack, m_seekConnection, m_volumeConnection;
    bool m_updating=false;
};
