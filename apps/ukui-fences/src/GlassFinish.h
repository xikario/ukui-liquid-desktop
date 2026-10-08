#pragma once
#include <QImage>
#include <QPainterPath>
#include <QVector>

// Static finishing passes baked into glass caches once per rebuild. Nothing
// here runs per frame; callers store the returned image.
namespace GlassFinish {
enum class Scrim { Readable = 0, Balanced = 1, Transparent = 2, Subtle = 3 };
struct ScrimCurve { qreal strength; qreal cap; };

ScrimCurve curve(Scrim preset);
Scrim scrimFromIndex(int index);
// Process-wide preset, owned by DesktopCanvas settings.
Scrim scrim();
void setScrim(Scrim preset);

// Alpha-weighted luma of 16x16 device-pixel cells, smoothed with a 3x3 1:2:1
// kernel. Cells without coverage report 0.
QVector<qreal> cellLuma(const QImage &image, QSize *cells);
// Rational damage->alpha curve: bright backdrops get a stronger scrim so white
// text stays readable; dark ones keep a 6% floor of the cap.
qreal scrimAlpha(qreal luma, ScrimCurve curve);
// Returns a copy darkened by the local scrim, limited to existing coverage.
QImage withScrim(const QImage &image, Scrim preset);

// Superellipse (exponent 3) rounded rectangle. `radius` is the circular
// radius it replaces; the curve starts earlier but stays inside that arc, so
// existing masks and hit regions remain valid.
QPainterPath squirclePath(const QRectF &rect, qreal radius);
// Cuts the image to `outline` (logical coordinates) and bakes a 1 px inner
// highlight, brightest at the top-left. Pass an empty outline to keep coverage.
QImage withEdge(const QImage &image, const QPainterPath &outline, bool clip = true);

constexpr int GRAIN_TILE = 64;   // device pixels; the tile is generated once per process
constexpr int GRAIN_LEVELS = 3;  // max luma offset in 8-bit steps, before coverage
// Deterministic zero-mean noise tile, values in [-GRAIN_LEVELS, GRAIN_LEVELS].
const QVector<qint8> &grainTile();
// Returns a copy with the cached grain added in device pixels, scaled by
// coverage so transparent pixels and alpha stay unchanged.
QImage withGrain(const QImage &image);
}
