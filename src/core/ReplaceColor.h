#pragma once
#include <QColor>

namespace gx {

class Document;

// Edit > Replace color: every pixel close to `from` becomes `to`.
struct ReplaceColorParams {
    QColor from = Qt::red, to = Qt::blue;
    double tolerance = 40;   // 0..255: how different a color may be and still be replaced fully
    double softness = 30;    // 0..255: beyond the tolerance, colors are replaced less and less
    bool keepShading = true; // lighter and darker shades of `from` become lighter / darker `to`
};

// n premultiplied RGBA float pixels, changed in place.
void replaceColorPixels(float* px, int n, const ReplaceColorParams& p);
// On the layer (inside the selection), as one undo step.
bool replaceColor(Document& doc, quint32 layerId, const ReplaceColorParams& p);
// Canvas > Offset by half (or any amount): every pixel layer and mask moves, what goes out comes back in.
void offsetWrap(Document& doc, int dx, int dy);

}  // namespace gx
