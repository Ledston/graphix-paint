#pragma once
#include <QColor>
#include <QPainterPath>
#include <QSize>
#include <QTransform>

#include "core/Document.h"

namespace gx {

// All document editing operations. Each one records an undo step.
namespace ops {

// ---- layers ----
Layer* insertLayer(Document& doc, LayerPtr layer, Layer* above = nullptr, bool undo = true, const QString& undoName = {});
RasterLayer* addRasterLayer(Document& doc, const QString& name = {});
VectorLayer* addVectorLayer(Document& doc, const QString& name = {});
GroupLayer* addGroup(Document& doc, const QString& name = {});
AdjustmentLayer* addAdjustmentLayer(Document& doc, const Adjustment& adj);
TextLayer* addTextLayer(Document& doc, const TextParams& params);
void deleteLayer(Document& doc, quint32 id);
Layer* duplicateLayer(Document& doc, quint32 id);
bool mergeDown(Document& doc, quint32 id, QString* error = nullptr);
void mergeVisible(Document& doc);
void flatten(Document& doc);
Layer* rasterize(Document& doc, quint32 id);  // vector / text / group -> raster
void moveLayer(Document& doc, quint32 id, GroupLayer* newParent, int index);
void moveLayerUp(Document& doc, quint32 id);
void moveLayerDown(Document& doc, quint32 id);
GroupLayer* groupLayer(Document& doc, quint32 id);
void clearLayer(Document& doc, quint32 id);  // respects the selection
void fillLayer(Document& doc, quint32 id, const QColor& c);  // respects the selection
void flipLayer(Document& doc, quint32 id, bool horizontal);

// ---- several layers at once (one undo step each) ----
void deleteLayers(Document& doc, const QList<quint32>& ids);
void duplicateLayers(Document& doc, const QList<quint32>& ids);
GroupLayer* groupLayers(Document& doc, const QList<quint32>& ids);   // into one new folder
Layer* mergeLayers(Document& doc, const QList<quint32>& ids, QString* error = nullptr);  // into one pixel layer
void moveLayers(Document& doc, const QList<quint32>& ids, GroupLayer* newParent, int index);
void setLayersVisible(Document& doc, const QList<quint32>& ids, bool visible);

// ---- channels and pixels of one pixel layer (inside the selection if there is one) ----
// fn changes n premultiplied RGBA pixels in place.
bool mapLayerPixels(Document& doc, quint32 id, const QString& undoName, const std::function<void(float* px, int n)>& fn,
                    bool wholeCanvas = false);
GroupLayer* splitChannels(Document& doc, quint32 id);     // red, green and blue as layers in a folder
Layer* alphaAsLayer(Document& doc, quint32 id);           // the alpha channel as a gray layer
void transparencyToMask(Document& doc, quint32 id);       // alpha moves into a layer mask
void invertAlpha(Document& doc, quint32 id);
void removeWhite(Document& doc, quint32 id);              // white becomes transparent (scans, line art)
void recolorLayer(Document& doc, quint32 id, const QColor& c);  // keeps the shape, one color
Layer* layerFromSelection(Document& doc, bool cut);
void centerLayer(Document& doc, quint32 id);

// ---- masks ----
void addMask(Document& doc, quint32 id, bool fromSelection);
void deleteMask(Document& doc, quint32 id);
void applyMask(Document& doc, quint32 id);

// Renders a layer subtree (or several layers) on its own into a surface
RasterSurface renderLayers(Document& doc, const std::vector<LayerPtr>& layers, const QRect& bounds);

// ---- selection ----
enum class SelectMode { Replace, Add, Subtract, Intersect };
void selectAll(Document& doc);
void deselect(Document& doc);
void reselect(Document& doc);
void invertSelection(Document& doc);
void selectLayerAlpha(Document& doc, quint32 id, SelectMode mode = SelectMode::Replace);
// Combines a coverage mask (0..255, canvas space, default 0) with the selection.
void combineSelection(Document& doc, const MaskSurface& shape, SelectMode mode, const QString& undoName);
void selectPath(Document& doc, const QPainterPath& path, SelectMode mode, bool antialias, const QString& undoName);
void growSelection(Document& doc, int px);  // negative shrinks
void featherSelection(Document& doc, double radius);
void borderSelection(Document& doc, int px);
MaskSurface rasterizePath(const QPainterPath& path, const QRect& clip, bool antialias);

// ---- canvas ----
void resizeCanvas(Document& doc, const QSize& size, const QPoint& offset);  // offset = where old (0,0) goes
void scaleImage(Document& doc, const QSize& size, bool smooth);
void rotateCanvas(Document& doc, int quarterTurnsCw);
void flipCanvas(Document& doc, bool horizontal);
void cropToSelection(Document& doc);
void trimCanvas(Document& doc);

// Helpers used by tools
void transformSurface(RasterSurface& s, const QTransform& t, bool smooth, const QRect& srcBounds = {});
void transformMask(MaskSurface& s, const QTransform& t, bool smooth);
void transformLayerContent(Layer& l, const QTransform& t, bool smooth);
void applyToSelection(Document& doc, RasterSurface& target, const RasterSurface& original, const QRect& r);

}  // namespace ops
}  // namespace gx
