#pragma once
#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
#include <vector>

#include "core/Adjustments.h"
#include "core/BlendModes.h"
#include "core/Tile.h"

namespace gx {

// Values are stored in files: append only.
enum class LayerType : int { Raster = 0, Group = 1, Vector = 2, Text = 3, Adjustment = 4 };

struct LayerEffects {
    bool outline = false;
    QColor outlineColor = Qt::white;
    float outlineSize = 3.f;
    float outlineOpacity = 1.f;

    bool shadow = false;
    QColor shadowColor = Qt::black;
    float shadowOpacity = 0.5f;
    QPointF shadowOffset{6, 6};
    float shadowBlur = 6.f;

    bool blur = false;
    float blurRadius = 4.f;

    bool overlay = false;
    QColor overlayColor = QColor(255, 90, 90);
    float overlayOpacity = 1.f;

    bool texture = false;
    QString textureName;
    float textureStrength = 0.5f;
    float textureScale = 1.f;

    bool innerShadow = false;
    QColor innerShadowColor = Qt::black;
    float innerShadowOpacity = 0.5f;
    QPointF innerShadowOffset{4, 4};
    float innerShadowBlur = 6.f;

    bool outerGlow = false;
    QColor outerGlowColor = QColor(255, 230, 140);
    float outerGlowOpacity = 0.8f;
    float outerGlowSize = 12.f;

    bool innerGlow = false;
    QColor innerGlowColor = QColor(255, 255, 220);
    float innerGlowOpacity = 0.7f;
    float innerGlowSize = 10.f;

    bool gradientOverlay = false;
    QColor gradientStart = QColor(255, 120, 60);
    QColor gradientEnd = QColor(90, 60, 200);
    float gradientAngle = 90.f;      // degrees, 90 = top to bottom
    float gradientOpacity = 1.f;
    int gradientBlend = 0;           // BlendMode

    bool grain = false;
    float grainAmount = 0.25f;
    float grainSize = 1.f;           // px
    bool grainMono = true;
    int grainBlend = 3;              // BlendMode::Overlay
    int grainSeed = 1;

    bool screentone = false;
    float toneCell = 8.f;            // px between dots
    float toneAngle = 45.f;
    int toneShape = 0;               // 0 dots, 1 lines, 2 squares
    QColor toneColor = Qt::black;
    bool toneKeepWhite = true;       // light areas become transparent

    bool chromatic = false;          // chromatic aberration: red and blue drift apart
    float chromaShift = 4.f;         // px
    float chromaAngle = 0.f;         // degrees

    bool anyActive() const {
        return outline || shadow || blur || overlay || texture || innerShadow || outerGlow || innerGlow || gradientOverlay || grain ||
               screentone || chromatic;
    }
    bool needsNeighborhood() const { return outline || shadow || blur || innerShadow || outerGlow || innerGlow || chromatic; }
    int margin() const;
    QJsonObject toJson() const;
    static LayerEffects fromJson(const QJsonObject& o);
    bool operator==(const LayerEffects& o) const;
    bool operator!=(const LayerEffects& o) const { return !(*this == o); }
};

struct TextParams {
    QString text = "Text";
    QString family;
    double sizePx = 48;
    bool bold = false, italic = false, underline = false;
    QColor color = Qt::black;
    int align = 0;  // 0 left, 1 center, 2 right
    double lineSpacing = 1.0;
    double letterSpacing = 0.0;  // percent
    bool vertical = false;
    QPointF origin;  // top-left in canvas coordinates
    double rotation = 0;  // degrees
    double scale = 1.0;
    QJsonObject toJson() const;
    static TextParams fromJson(const QJsonObject& o);
    QFont font() const;
};

struct VectorPoint {
    QPointF pos;
    float pressure = 1.f;
};

struct VectorStroke {
    quint32 id = 0;
    QVector<VectorPoint> points;
    float width = 4.f;       // diameter at full pressure
    float minWidth = 0.15f;  // fraction of width at zero pressure
    float hardness = 0.85f;
    float opacity = 1.f;
    QColor color = Qt::black;
    QRectF bounds() const;  // includes stroke width
    QJsonObject toJson() const;
    static VectorStroke fromJson(const QJsonObject& o);
};

class GroupLayer;

// Effects cache: rendered result of a layer with its effects, filled lazily per tile.
struct FxCache {
    RasterSurface tiles;
    QRect content;       // where the layer has pixels (for gradients spanning the layer)
    QSet<qint64> valid;  // tiles that are up to date
    LayerEffects params;
};

class Layer {
public:
    virtual ~Layer() = default;
    virtual LayerType type() const = 0;
    virtual std::shared_ptr<Layer> clone() const = 0;

    bool isGroup() const { return type() == LayerType::Group; }
    bool isRaster() const { return type() == LayerType::Raster; }
    bool hasPixels() const { return type() != LayerType::Group && type() != LayerType::Adjustment; }

    // Pixels shown by the compositor (raster pixels, preview, or rendered cache).
    virtual const RasterSurface* displaySurface() const { return nullptr; }
    const MaskSurface* displayMask() const {
        if (previewMask) return previewMask.get();
        return hasMask && maskEnabled ? &mask : nullptr;
    }
    bool isEffectivelyVisible() const;
    bool isEffectivelyLocked() const;
    int depth() const;

    quint32 id = 0;
    QString name;
    bool visible = true;
    float opacity = 1.f;
    BlendMode blend = BlendMode::Normal;
    bool clipping = false;
    bool locked = false;
    bool alphaLock = false;  // "Preserve opacity"
    QColor labelColor;       // invalid = none
    bool reference = false;  // fill tools can take their borders from this layer (line art)
    LayerEffects fx;

    // Layer mask (white reveals). Default value 255 so an empty mask shows everything.
    bool hasMask = false;
    bool maskEnabled = true;
    MaskSurface mask{255};

    GroupLayer* parent = nullptr;

    // ----- runtime state, not saved -----
    std::shared_ptr<MaskSurface> previewMask;  // tool preview of the mask
    QPoint previewOffset;                      // move tool preview
    std::shared_ptr<FxCache> fxCache;          // never shared between clones
    quint64 version = 0;                       // bumped on every pixel change (thumbnails)

protected:
    void copyBaseTo(Layer& o) const;
};

class RasterLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Raster; }
    std::shared_ptr<Layer> clone() const override;
    const RasterSurface* displaySurface() const override { return preview ? preview.get() : &pixels; }

    RasterSurface pixels;
    std::shared_ptr<RasterSurface> preview;  // tool preview (strokes, transforms, filters)
};

class VectorLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Vector; }
    std::shared_ptr<Layer> clone() const override;
    const RasterSurface* displaySurface() const override { return &cache; }
    VectorStroke* strokeById(quint32 id);

    QVector<VectorStroke> strokes;
    quint32 nextStrokeId = 1;
    RasterSurface cache;  // rendered strokes
};

class TextLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Text; }
    std::shared_ptr<Layer> clone() const override;
    const RasterSurface* displaySurface() const override { return &cache; }

    TextParams text;
    RasterSurface cache;
};

class AdjustmentLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Adjustment; }
    std::shared_ptr<Layer> clone() const override;
    Adjustment adjustment;

    // runtime: compiled processor, rebuilt by Compositor::prepare when params change
    std::shared_ptr<const AdjustmentProcessor> processor;
    QJsonObject processorKey;
};

class GroupLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Group; }
    std::shared_ptr<Layer> clone() const override;

    // children[0] is the bottom-most layer
    std::vector<std::shared_ptr<Layer>> children;
    bool expanded = true;
    bool passThrough = false;

    int indexOf(const Layer* l) const;
    void insert(int index, std::shared_ptr<Layer> l);
    std::shared_ptr<Layer> take(int index);
};

using LayerPtr = std::shared_ptr<Layer>;

// Depth-first walk, bottom to top. Return false from fn to stop.
bool forEachLayer(const GroupLayer* g, const std::function<bool(Layer*)>& fn);
Layer* findLayer(const GroupLayer* root, quint32 id);
// All layers in top-to-bottom display order (as shown in the layer panel)
QList<Layer*> layersTopToBottom(const GroupLayer* root, bool onlyExpanded = false);
bool isAncestor(const Layer* maybeAncestor, const Layer* l);

QString layerTypeName(LayerType t);

}  // namespace gx
