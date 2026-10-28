#pragma once
#include <QRect>
#include <vector>

#include "core/Textures.h"

namespace gx {

struct DabParams {
    double cx = 0, cy = 0;      // center in canvas pixels
    double diameter = 1;
    double hardness = 1;        // 0..1
    double roundness = 1;       // 0..1
    double angleRad = 0;
    bool antialias = true;
    const Texture* shape = nullptr;
    double shapeStrength = 1;
    bool shapeAsTip = false;
    double fringe = 0;
};

QRect dabBounds(const DabParams& d);
// Writes coverage (0..1) for every pixel of 'box' (row-major, box.width() stride).
// Returns false if nothing is covered.
bool rasterizeDab(const DabParams& d, const QRect& box, float* cov);

}  // namespace gx
