#pragma once
#include <QObject>
#include <functional>
#include <memory>

// Monitor only desktop stacking changes. Never activate or remap the canvas.
class DesktopLayerWatch final : public QObject {
public:
    DesktopLayerWatch(unsigned long window, std::function<bool()> visible, QObject *parent=nullptr);
    ~DesktopLayerWatch() override;
    static void requestPlacement(unsigned long window);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
