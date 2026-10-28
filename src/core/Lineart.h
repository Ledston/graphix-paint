#pragma once
#include <QColor>
#include <QPoint>
#include <QVector>

#include "core/Layer.h"

namespace gx {

// Turns any picture (photo, scan of a sketch, anime frame, 3D render) into line art:
// dark lines on a transparent background that can be painted under.
struct LineartParams {
    enum Mode { Auto = 0, Photo = 1, Sketch = 2, Anime = 3 };
    int mode = Auto;
    double detail = 50;        // 0..100, more = more lines
    double thickness = 2.0;    // px
    double cleanup = 4;        // spots smaller than this (px across) are removed
    double smooth = 1.0;       // 0..5, smoother and calmer lines
    int closeGaps = 0;         // px, joins small breaks in lines
    bool fillDarks = false;    // keep big dark areas (hair, shadows) as solid ink
    int colorMode = 0;         // 0 one color, 1 colors from the picture
    QColor color = Qt::black;

    // hand-drawn look
    double wobble = 0;         // px, lines drift like a hand-drawn line
    double wobbleScale = 30;   // px, how long the drifts are
    double pressure = 0;       // 0..1, thickness changes along the lines
    int texture = 0;           // 0 none, 1 pencil, 2 charcoal, 3 ink bleed
    double textureStrength = 0.5;
    double sketchy = 0;        // 0..1, a second, offset pass like a quick sketch
    unsigned seed = 1;
};

// rgba: premultiplied W*H*4. out: premultiplied W*H*4 line image.
// pxScale: size of one output pixel compared to the real picture (previews use smaller copies),
// so sizes in px stay the same. Returns the mode that was used (Auto picks one).
int makeLineart(const float* rgba, int W, int H, const LineartParams& p, float* out, double pxScale = 1.0, const QPoint& origin = {});

// Traces line art (alpha, W*H) into editable linework strokes (canvas offset 'at').
QVector<VectorStroke> traceLineart(const float* alpha, int W, int H, const QPoint& at, const QColor& color);

}  // namespace gx
