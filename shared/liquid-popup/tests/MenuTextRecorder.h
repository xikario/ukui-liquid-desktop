#pragma once

#include <QFontMetricsF>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QWidget>

// Record the text actually submitted by QWidget/QStyle painting, rather than
// reconstructing its layout from menu size or our stylesheet constants.
class MenuTextRecorder final : public QPaintDevice {
public:
    struct Run { QString text; QRectF bounds; };
    class Engine final : public QPaintEngine {
    public:
        Engine() : QPaintEngine(AllFeatures) {}
        QList<Run> runs;
        QTransform transform;
        bool begin(QPaintDevice *) override { setActive(true); return true; }
        bool end() override { setActive(false); return true; }
        Type type() const override { return User; }
        void updateState(const QPaintEngineState &state) override { transform = state.transform(); }
        void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override {}
        void drawImage(const QRectF &, const QImage &, const QRectF &, Qt::ImageConversionFlags) override {}
        void drawPath(const QPainterPath &) override {}
        void drawPolygon(const QPointF *, int, PolygonDrawMode) override {}
        void drawTextItem(const QPointF &point, const QTextItem &item) override {
            const QRectF advance(point.x(), point.y()-item.ascent(), item.width(), item.ascent()+item.descent());
            const QRectF ink = QFontMetricsF(item.font()).boundingRect(item.text()).translated(point);
            runs.append({item.text(), transform.mapRect(advance.united(ink))});
        }
    };
    explicit MenuTextRecorder(QWidget &widget) : dpiX(widget.logicalDpiX()), dpiY(widget.logicalDpiY()) {
        widget.render(this);
    }
    QPaintEngine *paintEngine() const override { return &engine; }
    QRectF boundsFor(const QString &text, const QRect &row) const {
        QRectF bounds;
        QString recorded;
        for (const auto &run : engine.runs) {
            if (!run.text.isEmpty() && text.contains(run.text) && row.contains(run.bounds.center().toPoint())) {
                bounds = bounds.united(run.bounds);
                recorded += run.text;
            }
        }
        return recorded == text ? bounds : QRectF();
    }
private:
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmDpiX: case PdmPhysicalDpiX: return dpiX;
        case PdmDpiY: case PdmPhysicalDpiY: return dpiY;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return QPaintDevice::devicePixelRatioFScale();
        case PdmDepth: return 32;
        default: return 10000;
        }
    }
    int dpiX, dpiY;
    mutable Engine engine;
};
