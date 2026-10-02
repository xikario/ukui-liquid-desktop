#include "SettingsComboPopup.h"
#include "DesktopCanvas.h"
#include "LiquidPopup.h"
#include <QPointer>
namespace SettingsComboPopup {
void install(QWidget *form,DesktopCanvas *canvas) {
    const QPointer<DesktopCanvas> guard(canvas);
    LiquidPopup::installComboPopups(form,[guard](const QRect &area,qreal dpr){
        return guard?guard->wallpaperBackdrop(area,dpr):LiquidPopup::captureBackdrop(area,dpr);
    });
}
}
