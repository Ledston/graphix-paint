#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <vector>

#include "core/BlendModes.h"

namespace gx {

// How a brush puts paint on the layer. Stored in files: append only.
enum class BrushEngine : int {
    Paint = 0,   // pen, airbrush, marker: stroke is built in a buffer, opacity never exceeds the stroke opacity
    Mix = 1,     // brush, watercolor: paints directly and mixes with the colors under it
    Erase = 2,   // eraser
    Blur = 3,    // blur tool
    Smudge = 4,  // smudge / finger
};

QString brushEngineName(BrushEngine e);

struct BrushSettings {
    QString name = "Pen";
    QString iconKey = "pen";  // which icon the tool palette shows
    QString group;            // tab of the brush palette ("Basic", "Paint", "Hatching", "Effects", "Erase")
    BrushEngine engine = BrushEngine::Paint;

    // Size
    double size = 8.0;          // diameter in pixels at full pressure
    double minSize = 0.0;       // fraction of size at zero pressure
    bool sizePressure = true;

    // Opacity
    double opacity = 1.0;       // stroke opacity (a stroke never gets more opaque than this)
    double density = 1.0;       // per-dab flow
    double minDensity = 1.0;    // fraction of density at zero pressure
    bool densityPressure = false;
    bool buildup = false;       // dabs accumulate (airbrush) instead of capping at density
    bool airbrush = false;      // keeps spraying while the pen does not move
    double airbrushRate = 40;   // dabs per second when still

    // Tip
    double hardness = 0.9;      // 0 soft .. 1 hard edge
    bool antialias = true;      // false = binary / pixel pen
    double spacing = 0.08;      // fraction of the current diameter
    double roundness = 1.0;
    double angle = 0.0;         // degrees
    bool angleFollowsStroke = false;
    bool angleFromTilt = false;
    QString shape;              // tip shape texture (empty = round)
    double shapeStrength = 1.0;
    bool shapeAsTip = false;    // use the shape as the whole tip instead of modulating the round tip

    // Grain texture (in canvas space, like SAI brush textures)
    QString texture;
    double textureStrength = 0.0;
    double textureScale = 1.0;
    bool textureInvert = false;
    bool textureEachDab = false;  // apply per dab (darker overlaps) instead of per stroke
    double textureDensity = 0.5;  // 0 = only the brightest grain takes paint, 1 = the grain fills in (0.5 = as drawn)
    bool textureFollowsStroke = false;  // the texture runs along the stroke (hatching, bristles) instead of lying on the paper

    // Mixing (Mix and Smudge engines)
    double blending = 0.0;      // how much of the canvas color is picked up
    double dilution = 0.0;      // water: makes the paint transparent
    double persistence = 0.5;   // how long the picked up color stays in the brush
    bool blendingPressure = false;  // less pressure = more blending
    double fringe = 0.0;        // watercolor edge darkening

    // Dynamics
    double sizeJitter = 0.0, angleJitter = 0.0, scatter = 0.0, opacityJitter = 0.0;
    double hueJitter = 0.0, saturationJitter = 0.0, valueJitter = 0.0;
    double taperIn = 0.0;       // pixels over which a stroke grows from zero
    double taperOut = 0.0;      // pixels over which a stroke ends thin (applied with the stabilizer tail)

    // Line bundle (hatching): one stroke draws several parallel lines across the brush size
    int bundleLines = 1;               // 1 = an ordinary brush
    double bundleThickness = 0.4;      // line width as a part of the space between lines
    double bundleThicknessJitter = 0.3;
    double bundleWobble = 0.15;        // lines drift a little, like drawn by hand
    double bundleLengthJitter = 0.4;   // lines start at different places
    // Particles (stipple): every dab scatters this many dots inside the brush size
    int particles = 0;                 // 0 = off
    double particleSize = 0.1;         // dot size as a part of the brush size
    // Second color (the background color of the color panel): this part of the lines,
    // dots or dabs is painted with it (white lines in hatching, highlights)
    double subColorShare = 0.0;
    // Decor: every dab is a random picture of a stamp set (leaves, stars...), or a pattern
    // runs along the stroke as wide as the brush (lace, chain...)
    QString stampSet;
    QString ribbon;
    double ribbonLength = 1.0;  // stretches the pattern along the stroke

    // Paint behaviour
    bool keepOpacity = false;   // only paint where the layer already has paint
    BlendMode paintMode = BlendMode::Normal;

    // Per-brush pressure curve (otherwise the global one is used)
    bool customPressureCurve = false;
    QVector<QPointF> pressureCurve{QPointF(0, 0), QPointF(1, 1)};

    QJsonObject toJson() const;
    static BrushSettings fromJson(const QJsonObject& o);
    static QVector<BrushSettings> defaults();
    // brushes added in later versions (given to existing brush palettes once)
    static QVector<BrushSettings> defaultsSince(int version);
    static QVector<BrushSettings> decorDefaults();
    static QStringList groups();                       // the built-in palette tabs, in order
    static QString defaultGroupFor(const QString& name);  // for palettes saved before tabs existed
};

class PressureCurve {
public:
    PressureCurve() { set({QPointF(0, 0), QPointF(1, 1)}); }
    void set(const QVector<QPointF>& pts);
    const QVector<QPointF>& points() const { return m_points; }
    float map(float p) const;

private:
    QVector<QPointF> m_points;
    std::vector<float> m_lut;
};

}  // namespace gx
