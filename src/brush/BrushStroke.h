#pragma once
#include <QColor>
#include <QRect>
#include <QTransform>
#include <QVector>
#include <deque>
#include <memory>
#include <random>

#include "brush/BrushSettings.h"
#include "brush/Dab.h"
#include "brush/Stabilizer.h"
#include "core/Tile.h"

namespace gx {

class Document;
class Layer;

struct StrokeContext {
    Document* doc = nullptr;
    Layer* layer = nullptr;
    bool targetMask = false;      // paint into the layer mask instead of the pixels
    BrushSettings brush;
    QColor color = Qt::black;
    bool transparentColor = false;  // SAI style "transparent" color: paints transparency
    const PressureCurve* pressureCurve = nullptr;
    QVector<QTransform> symmetry;  // extra mirrored copies
};

// One brush stroke from pen down to pen up.
class BrushStroke {
public:
    explicit BrushStroke(const StrokeContext& ctx);
    ~BrushStroke();

    void begin(const InputSample& s);
    void add(const InputSample& s);   // stabilized samples
    void airbrushTick();
    void end();                       // commits the stroke into the layer
    void cancel();                    // throws the stroke away (caller reverts undo)
    QRect bounds() const { return m_bounds; }

private:
    struct PathPoint {
        InputSample s;
        double taper = 1.0;
        double cum = 0;  // path length up to here
    };
    struct Emitter {
        QTransform xf;
        PathPoint last;
        bool started = false;
        double residual = 0;
        double traveled = 0;
        float carried[4] = {0, 0, 0, 0};
        bool hasCarried = false;
        QPointF lastDir{1, 0};
    };

    void feedPath(const PathPoint& p);
    void emitTo(Emitter& e, const PathPoint& p);
    void dab(Emitter& e, const PathPoint& p, const QPointF& dir);
    void bufferedDab(const DabParams& d, float density);
    void directDab(Emitter& e, const DabParams& d, float density, float pressure);
    void flushPreview();
    void notify();
    double diameterFor(double pressure, double taper) const;
    float mapPressure(double p) const;
    float textureAt(int x, int y) const;
    void jitterColor(float* rgb);

    StrokeContext m_ctx;
    BrushEngine m_engine;
    std::vector<Emitter> m_emitters;
    std::deque<PathPoint> m_pending;  // held back for taper out
    double m_pathLen = 0;
    PathPoint m_lastPoint;
    bool m_hasLast = false;

    FloatSurface m_mask{0.f};          // stroke coverage (buffered engines)
    QRect m_dirty;                     // since last notify
    QRect m_previewDirty;              // stroke mask changed, preview not rebuilt yet
    QRect m_bounds;
    float m_color[3] = {0, 0, 0};      // straight color for this stroke
    TexturePtr m_texture, m_shape;
    std::mt19937 m_rng;
    std::uniform_real_distribution<double> m_uni{0.0, 1.0};
    bool m_useSelection = false;
    bool m_alphaLock = false;
    bool m_ended = false;
};

}  // namespace gx
