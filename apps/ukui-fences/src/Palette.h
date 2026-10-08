#pragma once
#include <QColor>
#include <QImage>
#include <QVector>

// Colour rules shared by wallpaper and artwork tinting. Everything here is
// computed once per source image and cached by the caller.
namespace Palette {
constexpr qreal HoverMix = 0.10;
constexpr qreal PressMix = 0.18;
constexpr qreal MinSecondaryDistance = 0.28;  // L1 over normalised RGB
constexpr qreal PrimaryTextContrast = 7.0;    // WCAG AAA body text
constexpr qreal SecondaryTextContrast = 4.5;  // WCAG AA body text

// WCAG 2.x relative luminance and contrast ratio (1..21).
qreal luminance(const QColor &color);
qreal contrast(const QColor &a, const QColor &b);
// Black or white, whichever contrasts more with `background`.
QColor readableText(const QColor &background);
// Moves `foreground` toward black or white until it reaches `ratio` against
// `background`; returns the closest extreme when the ratio is unreachable.
QColor ensureContrast(const QColor &foreground, const QColor &background, qreal ratio);
// Same, against every background at once (worst case over sampled cells).
qreal minContrast(const QColor &foreground, const QVector<QColor> &backgrounds);
QColor ensureContrast(const QColor &foreground, const QVector<QColor> &backgrounds, qreal ratio);
// When no text colour can reach `ratio` over `backgrounds`, the backdrop has
// to move instead: returns the amount (0..1) to mix every background toward
// the extreme opposite the readable text, and that text extreme in `text`.
qreal toneLift(const QVector<QColor> &backgrounds, qreal ratio, QColor *text);
// Linear RGB mix used for hover (10%) and press (18%) states.
QColor mix(const QColor &base, const QColor &overlay, qreal amount);

// A swatch is usable when it is not near-black (max > .16), not near-white
// (min < .90) and has visible chroma (max - min > .10).
bool usableSwatch(const QColor &color);
qreal distance(const QColor &a, const QColor &b);

struct Colors {
    QColor primary, secondary;
    bool isValid() const { return primary.isValid(); }
};
// Population-weighted swatches from a 4-bit-per-channel histogram of a
// downscaled copy. The secondary is the most populous swatch at least
// MinSecondaryDistance away from the primary, if any.
Colors extract(const QImage &image);
}
