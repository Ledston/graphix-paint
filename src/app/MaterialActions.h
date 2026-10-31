#pragma once
#include <QColor>
#include <QPointF>
#include <QString>
#include <optional>

namespace gx {

class Document;
class Layer;

// What the materials panel and Edit > Fill with pattern / Stroke selection do.
// Each one is a single undo step; on failure they return null / false and set *error.
namespace materials {

// A picture as a new layer, centered on `center` (canvas coordinates) or on the picture.
Layer* placeImage(Document& d, const QString& file, std::optional<QPointF> center = {}, QString* error = nullptr);
// The selection (or the whole picture) filled with a pattern on a new layer.
Layer* placePattern(Document& d, const QString& name, const QColor& ink, QString* error = nullptr);
// A screentone layer from Materials::tones()[index].
Layer* placeTone(Document& d, int index, const QColor& ink, QString* error = nullptr);
// A gradient map adjustment layer from Materials::gradients()[index].
Layer* placeGradient(Document& d, int index, QString* error = nullptr);

// Edit > Fill with pattern: the current pattern on the current pixel layer, inside the selection.
bool fillWithPattern(Document& d, const QColor& ink, QString* error = nullptr);

// Edit > Stroke selection. position: 0 inside, 1 centered on the edge, 2 outside.
enum class StrokePosition { Inside = 0, Center = 1, Outside = 2 };
bool strokeSelection(Document& d, double width, StrokePosition pos, const QColor& color, bool pattern, QString* error = nullptr);

// The selected part of the current layer (or the whole layer) saved as a picture or a pattern.
// Returns the material name.
QString saveSelection(Document& d, bool asPattern, const QString& name, QString* error = nullptr);
// The whole picture (all visible layers) saved as a pattern (Canvas > Save as pattern).
QString savePictureAsPattern(Document& d, const QString& name, QString* error = nullptr);
// The gradient of the current gradient map layer saved into the gradient map set.
bool saveLayerGradient(Document& d, const QString& name, QString* error = nullptr);

}  // namespace materials
}  // namespace gx
