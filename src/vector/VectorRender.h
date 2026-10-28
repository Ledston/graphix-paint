#pragma once
#include <QRect>
#include <QVector>

#include "core/Layer.h"

namespace gx {

// Dense polyline through the control points (Catmull-Rom), pressure interpolated.
QVector<VectorPoint> sampleStroke(const VectorStroke& s, double step = 1.0);
// Reduces a dense input polyline to control points (Ramer-Douglas-Peucker).
QVector<VectorPoint> simplifyPoints(const QVector<VectorPoint>& pts, double tolerance);

// Re-renders the layer cache inside rect (whole layer when rect is empty).
// Returns the rect that changed.
QRect renderVectorLayer(VectorLayer& l, QRect rect = {});

// Index of the stroke under p (topmost first) or -1.
int hitTestStroke(const VectorLayer& l, const QPointF& p, double tolerance);
// Nearest control point: returns stroke index and fills pointIndex, or -1.
int hitTestControlPoint(const VectorLayer& l, const QPointF& p, double tolerance, int* pointIndex);

// Text layers
QRect renderTextLayer(TextLayer& l);
QRectF textBounds(const TextParams& t);

}  // namespace gx
