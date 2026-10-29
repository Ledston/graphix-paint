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
    QColor subColor = Qt::white;  // the second color (lines / dots / dabs given to it by subColorShare)
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
    // paint picked up by the brush (mixing engines)
    struct Carry {
        float c[4] = {0, 0, 0, 0};
        bool has = false;
    };
    struct Emitter {
        QTransform xf;
        PathPoint last;
        bool started = false;
        double residual = 0;
        double traveled = 0;
        Carry carry;
        std::vector<Carry> lineCarry;  // one per line of a bundle
        QPointF lastDir{1, 0};
        QPointF smoothDir{0, 0};  // the direction, smoothed over about half a brush (ribbons)
        double smoothAt = -1;     // where along the stroke it was last updated
        // the previous dab (ribbons are mapped onto the line between dab centers)
        bool hasDab = false;
        QPointF dabC;
        double dabAlong = 0, dabDiam = 0;
    };
    // one line of a hatching bundle, chosen when the stroke starts
    struct Line {
        double offset = 0;   // across the brush, -0.5..0.5 of the size
        double widthK = 1;
        double startPx = 0;  // starts this far into the stroke
        double phase = 0, freq = 0;
        bool sub = false;    // painted with the second color
    };
    // where a dab sits on the stroke, for textures that run along it
    struct TexFrame {
        bool on = false;
        double cx = 0, cy = 0, dx = 1, dy = 0, along = 0, across = 0;
        // ribbons: the previous dab center, so a pixel finds its place on the stroke's center line
        double px = 0, py = 0, palong = 0, pdiam = 0;
        bool hasPrev = false;
    };

    void feedPath(const PathPoint& p);
    void emitTo(Emitter& e, const PathPoint& p);
    void dab(Emitter& e, const PathPoint& p, const QPointF& dir);
    void put(const DabParams& d, float density, bool sub, const TexFrame& tf, Carry& carry, float pressure);
    void bufferedDab(const DabParams& d, float density, bool sub, const TexFrame& tf);
    void directDab(Carry& carry, const float* base, const DabParams& d, float density, float pressure, const TexFrame& tf);
    double stepFor(double pressure, double taper) const;
    void flushPreview();
    void notify();
    double diameterFor(double pressure, double taper) const;
    float mapPressure(double p) const;
    float textureAt(int x, int y) const;
    float textureAlong(int x, int y, const TexFrame& tf) const;
    float ribbonAt(int x, int y, const TexFrame& tf, double diameter, float* dist) const;
    float textureValue(float raw) const;
    void jitterColor(float* rgb);

    StrokeContext m_ctx;
    BrushEngine m_engine;
    std::unique_ptr<RasterSurface> m_origin;  // layer pixels at stroke start (shared tiles, no copy)
    std::vector<Emitter> m_emitters;
    std::deque<PathPoint> m_pending;  // held back for taper out
    double m_pathLen = 0;
    PathPoint m_lastPoint;
    bool m_hasLast = false;

    FloatSurface m_mask{0.f};          // stroke coverage (buffered engines)
    FloatSurface m_mask2{0.f};         // coverage of the second color
    FloatSurface m_ribbonDist{1e9f};   // ribbons: how far along the stroke the dab that painted a pixel was
    bool m_useSub = false;             // something was painted with the second color
    std::vector<Line> m_lines;
    float m_sub[3] = {1, 1, 1};
    QRect m_dirty;                     // since last notify
    QRect m_previewDirty;              // stroke mask changed, preview not rebuilt yet
    QRect m_bounds;
    float m_color[3] = {0, 0, 0};      // straight color for this stroke
    TexturePtr m_texture, m_shape, m_ribbon;
    std::vector<TexturePtr> m_stamps;
    double m_ribbonRepeat = 1;  // px along the stroke for one repeat of the ribbon pattern
    std::mt19937 m_rng;
    std::uniform_real_distribution<double> m_uni{0.0, 1.0};
    bool m_useSelection = false;
    bool m_alphaLock = false;
    bool m_ended = false;
};

}  // namespace gx
