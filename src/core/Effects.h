#pragma once
#include <QRect>

#include "core/Layer.h"

namespace gx {

// Layer effects (outline, drop shadow, blur, color overlay, paper texture) are
// non-destructive: the layer keeps its pixels and the effect result is cached per tile.

// Makes sure the effect cache of a layer covers the given rect (canvas space).
// Must be called from the GUI thread before compositing.
void ensureEffects(Layer& l, const QRect& canvasRect);
// Marks cached effect tiles affected by a pixel change (canvas space).
void invalidateEffects(Layer& l, const QRect& canvasRect);
// Renders one tile (layer space rect) of the layer with its effects.
void renderEffectsTile(const Layer& l, const QRect& tileRect, quint32* out);

// Effects as a filter: the pixels of a layer with 'fx' burned in, inside 'area'
// (and the selection, if given). The layer's own mask and effects are not used.
// Thread safe: works on a copy of the layer.
RasterSurface bakeEffects(const Layer& layer, const LayerEffects& fx, const QRect& area, const MaskSurface* selection);

}  // namespace gx
