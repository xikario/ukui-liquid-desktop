#pragma once
#include <QObject>
#include <QRect>
#include <QVector>
#include <functional>
#include <memory>

namespace DesktopCover {
struct Client {
    QRect frame;          // root coordinates, already corrected for frame/shadow extents
    bool dock = false;    // panels hide the strip a maximized window leaves free
    bool normal = true;
    bool hidden = false;
    bool maximized = false;
    bool fullscreen = false;
    bool translucent = false;
    bool onDesktop = true;
};
// Only maximized/fullscreen opaque windows count, so ordinary window drags never
// wake the desktop. Returns true when they leave less than 3% of it exposed.
bool covered(const QRect &desktop, const QVector<Client> &above);
// Expand by _NET_FRAME_EXTENTS (server decoration) and shrink by
// _GTK_FRAME_EXTENTS (client-side shadow), both ordered left,right,top,bottom.
QRect visibleFrame(const QRect &client, const QVector<unsigned long> &wmExtents,
                   const QVector<unsigned long> &shadowExtents);
}

// Event-driven X11 watcher: root and client property changes only, coalesced.
class DesktopCoverWatch final : public QObject {
public:
    DesktopCoverWatch(unsigned long window, std::function<void(bool)> changed, QObject *parent = nullptr);
    ~DesktopCoverWatch() override;
private:
    struct Private;
    std::unique_ptr<Private> d;
};
