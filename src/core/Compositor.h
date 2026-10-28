#pragma once
#include <QImage>
#include <QRect>

#include "core/Layer.h"

namespace gx {

struct CompositeOptions {
    // When set, only this layer is rendered (respecting its own opacity, mask and effects).
    const Layer* only = nullptr;
};

class Compositor {
public:
    // Must run on the GUI thread before compositing a region: builds effect caches
    // and adjustment processors for layers in that region.
    static void prepare(GroupLayer* root, const QRect& r);

    // Composites the visible image for r into out (premultiplied RGBA floats, r.width()*r.height()*4).
    static void composite(const GroupLayer* root, const QRect& r, float* out, const CompositeOptions& opt = {});

    // Convenience: whole region as an image (prepares and composites in parallel tiles).
    static QImage renderImage(GroupLayer* root, const QRect& r, const CompositeOptions& opt = {});
    // Merged color at one pixel (prepare must have run for that area, or effects are ignored).
    static void samplePixel(const GroupLayer* root, int x, int y, float* rgba);
};

}  // namespace gx
