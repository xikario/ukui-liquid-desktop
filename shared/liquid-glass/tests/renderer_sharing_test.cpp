#include "LiquidOpticsRenderer.h"
#include <QApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const bool requireGpu = app.arguments().contains("--gpu");
    QImage red(320, 220, QImage::Format_RGB32), blue(500, 260, QImage::Format_RGB32);
    red.fill(QColor(210, 30, 30)); blue.fill(QColor(30, 30, 210));
    auto first = std::make_unique<LiquidOpticsRenderer>();
    auto second = std::make_unique<LiquidOpticsRenderer>();
    first->setWallpaper(red); second->setWallpaper(blue);
    second->setOptics(6, .8, .2, .7);
    const QImage baseline = first->renderPanel(QRect(0, 0, 320, 220), 16);
    if (requireGpu && !first->usedGpu()) return 77;
    const QImage other = second->renderControl(QRect(0, 0, 500, 120), 28, true);
    if (baseline.isNull() || other.isNull() || baseline.pixelColor(160, 110).red()
        <= baseline.pixelColor(160, 110).blue() + 40
        || other.pixelColor(250, 60).blue() <= other.pixelColor(250, 60).red() + 40) return 1;
    // Sizes, source textures, optics, radius and control mode all change on the
    // shared program. Rendering again must restore every uniform and FBO size.
    if (first->renderPanel(QRect(0, 0, 320, 220), 16) != baseline) return 2;
    first.reset();
    if (second->renderControl(QRect(0, 0, 500, 120), 28, true) != other) return 3;
    second.reset();
    // No strong global owner: resources can be released, then reacquired while
    // the application is alive without retaining a dead context or framebuffer.
    LiquidOpticsRenderer recreated;
    recreated.setWallpaper(red);
    if (recreated.renderPanel(QRect(0, 0, 320, 220), 16) != baseline) return 4;
    if (requireGpu && !recreated.usedGpu()) return 5;
    qInfo() << "PASS: renderer sources, uniforms, sizes and lifetime remain independent; GPU" << recreated.usedGpu();
    return 0;
}
