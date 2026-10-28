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

}  // namespace gx
