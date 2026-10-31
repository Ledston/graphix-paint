#include "brush/BrushSettings.h"

#include <QHash>
#include <algorithm>

#include "core/Adjustments.h"
#include "core/Pixel.h"

namespace gx {

QString brushEngineName(BrushEngine e) {
    switch (e) {
    case BrushEngine::Paint: return "Paint";
    case BrushEngine::Mix: return "Mix (brush / watercolor)";
    case BrushEngine::Erase: return "Eraser";
    case BrushEngine::Blur: return "Blur";
    case BrushEngine::Smudge: return "Smudge";
    }
    return "Paint";
}

QJsonObject BrushSettings::toJson() const {
    QJsonObject o;
    o["name"] = name;
    o["icon"] = iconKey;
    o["group"] = group;
    o["engine"] = int(engine);
    o["size"] = size;
    o["minSize"] = minSize;
    o["sizePressure"] = sizePressure;
    o["opacity"] = opacity;
    o["density"] = density;
    o["minDensity"] = minDensity;
    o["densityPressure"] = densityPressure;
    o["buildup"] = buildup;
    o["airbrush"] = airbrush;
    o["airbrushRate"] = airbrushRate;
    o["hardness"] = hardness;
    o["antialias"] = antialias;
    o["spacing"] = spacing;
    o["roundness"] = roundness;
    o["angle"] = angle;
    o["angleFollowsStroke"] = angleFollowsStroke;
    o["angleFromTilt"] = angleFromTilt;
    o["shape"] = shape;
    o["shapeStrength"] = shapeStrength;
    o["shapeAsTip"] = shapeAsTip;
    o["texture"] = texture;
    o["textureStrength"] = textureStrength;
    o["textureScale"] = textureScale;
    o["textureInvert"] = textureInvert;
    o["textureEachDab"] = textureEachDab;
    o["textureDensity"] = textureDensity;
    o["textureFollowsStroke"] = textureFollowsStroke;
    o["bundleLines"] = bundleLines;
    o["bundleThickness"] = bundleThickness;
    o["bundleThicknessJitter"] = bundleThicknessJitter;
    o["bundleWobble"] = bundleWobble;
    o["bundleLengthJitter"] = bundleLengthJitter;
    o["particles"] = particles;
    o["particleSize"] = particleSize;
    o["subColorShare"] = subColorShare;
    o["stampSet"] = stampSet;
    o["ribbon"] = ribbon;
    o["ribbonLength"] = ribbonLength;
    o["blending"] = blending;
    o["dilution"] = dilution;
    o["persistence"] = persistence;
    o["blendingPressure"] = blendingPressure;
    o["sizeSoftness"] = sizeSoftness;
    o["densitySoftness"] = densitySoftness;
    o["blendSoftness"] = blendSoftness;
    o["fringe"] = fringe;
    o["sizeJitter"] = sizeJitter;
    o["angleJitter"] = angleJitter;
    o["scatter"] = scatter;
    o["opacityJitter"] = opacityJitter;
    o["hueJitter"] = hueJitter;
    o["saturationJitter"] = saturationJitter;
    o["valueJitter"] = valueJitter;
    o["taperIn"] = taperIn;
    o["taperOut"] = taperOut;
    o["keepOpacity"] = keepOpacity;
    o["paintMode"] = int(paintMode);
    o["customPressureCurve"] = customPressureCurve;
    o["pressureCurve"] = pointsToJson(pressureCurve);
    return o;
}

BrushSettings BrushSettings::fromJson(const QJsonObject& o) {
    BrushSettings b;
    auto d = [&](const char* k, double def) { return o.contains(k) ? o.value(k).toDouble(def) : def; };
    auto bl = [&](const char* k, bool def) { return o.contains(k) ? o.value(k).toBool(def) : def; };
    b.name = o.value("name").toString(b.name);
    b.iconKey = o.value("icon").toString(b.iconKey);
    b.group = o.value("group").toString();
    if (b.group.isEmpty()) b.group = defaultGroupFor(b.name);
    int e = o.value("engine").toInt(0);
    b.engine = (e >= 0 && e <= 4) ? BrushEngine(e) : BrushEngine::Paint;
    b.size = std::clamp(d("size", b.size), 0.5, 5000.0);
    b.minSize = std::clamp(d("minSize", b.minSize), 0.0, 1.0);
    b.sizePressure = bl("sizePressure", b.sizePressure);
    b.opacity = std::clamp(d("opacity", b.opacity), 0.0, 1.0);
    b.density = std::clamp(d("density", b.density), 0.0, 1.0);
    b.minDensity = std::clamp(d("minDensity", b.minDensity), 0.0, 1.0);
    b.densityPressure = bl("densityPressure", b.densityPressure);
    b.buildup = bl("buildup", b.buildup);
    b.airbrush = bl("airbrush", b.airbrush);
    b.airbrushRate = std::clamp(d("airbrushRate", b.airbrushRate), 1.0, 200.0);
    b.hardness = std::clamp(d("hardness", b.hardness), 0.0, 1.0);
    b.antialias = bl("antialias", b.antialias);
    b.spacing = std::clamp(d("spacing", b.spacing), 0.01, 5.0);
    b.roundness = std::clamp(d("roundness", b.roundness), 0.02, 1.0);
    b.angle = d("angle", b.angle);
    b.angleFollowsStroke = bl("angleFollowsStroke", b.angleFollowsStroke);
    b.angleFromTilt = bl("angleFromTilt", b.angleFromTilt);
    b.shape = o.value("shape").toString();
    b.shapeStrength = std::clamp(d("shapeStrength", b.shapeStrength), 0.0, 1.0);
    b.shapeAsTip = bl("shapeAsTip", b.shapeAsTip);
    b.texture = o.value("texture").toString();
    b.textureStrength = std::clamp(d("textureStrength", b.textureStrength), 0.0, 1.0);
    b.textureScale = std::clamp(d("textureScale", b.textureScale), 0.05, 20.0);
    b.textureInvert = bl("textureInvert", b.textureInvert);
    b.textureEachDab = bl("textureEachDab", b.textureEachDab);
    b.textureDensity = std::clamp(d("textureDensity", b.textureDensity), 0.0, 1.0);
    b.textureFollowsStroke = bl("textureFollowsStroke", b.textureFollowsStroke);
    b.bundleLines = std::clamp(int(d("bundleLines", b.bundleLines)), 1, 40);
    b.bundleThickness = std::clamp(d("bundleThickness", b.bundleThickness), 0.05, 1.0);
    b.bundleThicknessJitter = std::clamp(d("bundleThicknessJitter", b.bundleThicknessJitter), 0.0, 1.0);
    b.bundleWobble = std::clamp(d("bundleWobble", b.bundleWobble), 0.0, 1.0);
    b.bundleLengthJitter = std::clamp(d("bundleLengthJitter", b.bundleLengthJitter), 0.0, 1.0);
    b.particles = std::clamp(int(d("particles", b.particles)), 0, 60);
    b.particleSize = std::clamp(d("particleSize", b.particleSize), 0.01, 1.0);
    b.subColorShare = std::clamp(d("subColorShare", b.subColorShare), 0.0, 1.0);
    b.stampSet = o.value("stampSet").toString();
    b.ribbon = o.value("ribbon").toString();
    b.ribbonLength = std::clamp(d("ribbonLength", b.ribbonLength), 0.1, 10.0);
    b.blending = std::clamp(d("blending", b.blending), 0.0, 1.0);
    b.dilution = std::clamp(d("dilution", b.dilution), 0.0, 1.0);
    b.persistence = std::clamp(d("persistence", b.persistence), 0.0, 1.0);
    b.blendingPressure = bl("blendingPressure", b.blendingPressure);
    b.sizeSoftness = std::clamp(d("sizeSoftness", 100), 0.0, 200.0);
    b.densitySoftness = std::clamp(d("densitySoftness", 100), 0.0, 200.0);
    b.blendSoftness = std::clamp(d("blendSoftness", 100), 0.0, 200.0);
    b.fringe = std::clamp(d("fringe", b.fringe), 0.0, 1.0);
    b.sizeJitter = std::clamp(d("sizeJitter", 0), 0.0, 1.0);
    b.angleJitter = std::clamp(d("angleJitter", 0), 0.0, 1.0);
    b.scatter = std::clamp(d("scatter", 0), 0.0, 5.0);
    b.opacityJitter = std::clamp(d("opacityJitter", 0), 0.0, 1.0);
    b.hueJitter = std::clamp(d("hueJitter", 0), 0.0, 1.0);
    b.saturationJitter = std::clamp(d("saturationJitter", 0), 0.0, 1.0);
    b.valueJitter = std::clamp(d("valueJitter", 0), 0.0, 1.0);
    b.taperIn = std::max(0.0, d("taperIn", 0));
    b.taperOut = std::max(0.0, d("taperOut", 0));
    b.keepOpacity = bl("keepOpacity", false);
    int pm = o.value("paintMode").toInt(0);
    b.paintMode = (pm >= 0 && pm < int(BlendMode::Count)) ? BlendMode(pm) : BlendMode::Normal;
    b.customPressureCurve = bl("customPressureCurve", false);
    if (o.contains("pressureCurve")) b.pressureCurve = pointsFromJson(o.value("pressureCurve"));
    return b;
}

QVector<BrushSettings> BrushSettings::defaults() {
    QVector<BrushSettings> v;
    {
        BrushSettings b; b.name = "Pen"; b.iconKey = "pen";
        b.size = 6; b.minSize = 0.0; b.hardness = 0.92; b.spacing = 0.06;
        v << b;
    }
    {
        BrushSettings b; b.name = "Ink"; b.iconKey = "ink";
        b.size = 10; b.minSize = 0.05; b.hardness = 1.0; b.spacing = 0.05; b.taperIn = 12;
        v << b;
    }
    {
        BrushSettings b; b.name = "Pencil"; b.iconKey = "pencil";
        b.size = 4; b.minSize = 0.4; b.hardness = 0.55; b.densityPressure = true; b.minDensity = 0.15;
        b.density = 0.85; b.texture = "Fine paper"; b.textureStrength = 0.6; b.spacing = 0.1;
        v << b;
    }
    {
        BrushSettings b; b.name = "AirBrush"; b.iconKey = "airbrush";
        b.size = 140; b.sizePressure = false; b.hardness = 0.0; b.density = 0.06; b.densityPressure = true;
        b.minDensity = 0.0; b.buildup = true; b.airbrush = true; b.spacing = 0.06;
        v << b;
    }
    {
        BrushSettings b; b.name = "Brush"; b.iconKey = "brush"; b.engine = BrushEngine::Mix;
        b.size = 26; b.minSize = 0.25; b.hardness = 0.7; b.blending = 0.5; b.persistence = 0.8;
        b.density = 0.85; b.spacing = 0.05; b.blendingPressure = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Water Color"; b.iconKey = "watercolor"; b.engine = BrushEngine::Mix;
        b.size = 45; b.minSize = 0.5; b.hardness = 0.45; b.blending = 0.6; b.dilution = 0.35; b.persistence = 0.75;
        b.density = 0.75; b.fringe = 0.2; b.texture = "Watercolor paper"; b.textureStrength = 0.25; b.spacing = 0.05;
        b.blendingPressure = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Marker"; b.iconKey = "marker";
        b.size = 22; b.minSize = 0.6; b.hardness = 0.85; b.opacity = 0.55; b.roundness = 0.45; b.angle = 35;
        v << b;
    }
    {
        BrushSettings b; b.name = "Chalk"; b.iconKey = "chalk";
        b.size = 28; b.minSize = 0.5; b.shape = "Chalk tip"; b.shapeAsTip = true; b.texture = "Rough grain";
        b.textureStrength = 0.6; b.spacing = 0.12; b.angleJitter = 1.0; b.densityPressure = true; b.minDensity = 0.3;
        v << b;
    }
    {
        BrushSettings b; b.name = "Bristle"; b.iconKey = "bristle"; b.engine = BrushEngine::Mix;
        b.size = 34; b.minSize = 0.4; b.shape = "Bristle tip"; b.shapeAsTip = true; b.angleFollowsStroke = true;
        b.blending = 0.35; b.persistence = 0.85; b.spacing = 0.04; b.hardness = 0.6; b.blendingPressure = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Binary"; b.iconKey = "binary";
        b.size = 2; b.sizePressure = false; b.antialias = false; b.hardness = 1.0; b.spacing = 0.2;
        v << b;
    }
    {
        BrushSettings b; b.name = "Eraser"; b.iconKey = "eraser"; b.engine = BrushEngine::Erase;
        b.size = 30; b.minSize = 0.3; b.hardness = 0.85;
        v << b;
    }
    {
        BrushSettings b; b.name = "Soft Eraser"; b.iconKey = "softeraser"; b.engine = BrushEngine::Erase;
        b.size = 120; b.sizePressure = false; b.hardness = 0.0; b.density = 0.25; b.densityPressure = true; b.buildup = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Blur"; b.iconKey = "blur"; b.engine = BrushEngine::Blur;
        b.size = 60; b.sizePressure = false; b.hardness = 0.3; b.density = 0.5; b.densityPressure = true; b.spacing = 0.1;
        v << b;
    }
    {
        BrushSettings b; b.name = "Smudge"; b.iconKey = "smudge"; b.engine = BrushEngine::Smudge;
        b.size = 35; b.sizePressure = false; b.hardness = 0.4; b.persistence = 0.8; b.density = 0.85; b.spacing = 0.05;
        b.densityPressure = true;
        v << b;
    }
    v << defaultsSince(2);
    v << defaultsSince(3);
    v << defaultsSince(4);
    for (BrushSettings& b : v)
        if (b.group.isEmpty()) b.group = defaultGroupFor(b.name);
    return v;
}

QVector<BrushSettings> BrushSettings::defaultsSince(int version) {
    QVector<BrushSettings> v;
    if (version == 3) return decorDefaults();
    if (version == 4) return customDefaults();
    if (version > 4) return v;
    // ---- hatching: one stroke draws a bundle of lines
    {
        BrushSettings b; b.name = "Hatching"; b.iconKey = "hatching"; b.group = "Hatching";
        b.size = 34; b.minSize = 0.55; b.hardness = 0.9; b.spacing = 0.35;
        b.bundleLines = 7; b.bundleThickness = 0.4; b.bundleThicknessJitter = 0.45; b.bundleWobble = 0.2; b.bundleLengthJitter = 0.5;
        b.taperIn = 25; b.taperOut = 45;
        v << b;
    }
    {
        BrushSettings b; b.name = "Hatching with white"; b.iconKey = "hatching"; b.group = "Hatching";
        b.size = 34; b.minSize = 0.55; b.hardness = 0.9; b.spacing = 0.35;
        b.bundleLines = 9; b.bundleThickness = 0.3; b.bundleThicknessJitter = 0.5; b.bundleWobble = 0.2; b.bundleLengthJitter = 0.6;
        b.subColorShare = 0.35; b.taperIn = 25; b.taperOut = 45;
        v << b;
    }
    {
        // smears the colors under it along the lines, like a finger with a comb
        BrushSettings b; b.name = "Hatching blender"; b.iconKey = "hatching"; b.group = "Hatching"; b.engine = BrushEngine::Mix;
        b.size = 40; b.minSize = 0.5; b.hardness = 0.75; b.spacing = 0.3;
        b.bundleLines = 6; b.bundleThickness = 0.45; b.bundleThicknessJitter = 0.4; b.bundleWobble = 0.15; b.bundleLengthJitter = 0.3;
        b.blending = 0.7; b.persistence = 0.85; b.density = 0.8; b.blendingPressure = true; b.taperIn = 20; b.taperOut = 40;
        b.texture = "Pencil grain"; b.textureStrength = 0.35;
        v << b;
    }
    {
        BrushSettings b; b.name = "Textured hatching"; b.iconKey = "hatching"; b.group = "Hatching";
        b.size = 30; b.minSize = 0.4; b.hardness = 0.8; b.spacing = 0.06; b.densityPressure = true; b.minDensity = 0.3;
        b.texture = "Hatch lines"; b.textureStrength = 1.0; b.textureFollowsStroke = true; b.textureScale = 0.8;
        b.taperIn = 20; b.taperOut = 30;
        v << b;
    }
    {
        BrushSettings b; b.name = "Stipple"; b.iconKey = "stipple"; b.group = "Hatching";
        b.size = 50; b.sizePressure = false; b.hardness = 0.95; b.spacing = 0.22; b.particles = 7; b.particleSize = 0.09;
        b.sizeJitter = 0.6; b.densityPressure = true; b.minDensity = 0.1;
        v << b;
    }
    {
        BrushSettings b; b.name = "Glitter"; b.iconKey = "stipple"; b.group = "Effects";
        b.size = 60; b.sizePressure = false; b.hardness = 0.9; b.spacing = 0.45; b.particles = 2; b.particleSize = 0.22;
        b.sizeJitter = 0.8; b.densityPressure = true; b.minDensity = 0.1; b.subColorShare = 0.5;
        b.shape = "Sparkle"; b.shapeAsTip = true; b.angleJitter = 0.15;
        v << b;
    }
    // ---- painting
    {
        BrushSettings b; b.name = "Charcoal"; b.iconKey = "charcoal"; b.group = "Paint";
        b.size = 24; b.minSize = 0.6; b.hardness = 0.6; b.spacing = 0.08; b.densityPressure = true; b.minDensity = 0.2; b.density = 0.9;
        b.shape = "Chalk tip"; b.shapeStrength = 0.6; b.texture = "Charcoal"; b.textureStrength = 0.8; b.textureScale = 1.5;
        v << b;
    }
    {
        BrushSettings b; b.name = "Soft pencil"; b.iconKey = "pencil"; b.group = "Basic";
        b.size = 8; b.minSize = 0.5; b.hardness = 0.5; b.spacing = 0.08; b.densityPressure = true; b.minDensity = 0.1; b.density = 0.8;
        b.shape = "Pencil tip"; b.shapeStrength = 0.5; b.texture = "Pencil grain"; b.textureStrength = 0.7;
        v << b;
    }
    {
        // picks the color up and spreads it: blending soft shadows (the "finger" with paint)
        BrushSettings b; b.name = "Blender"; b.iconKey = "blender"; b.group = "Paint"; b.engine = BrushEngine::Mix;
        b.size = 40; b.minSize = 0.4; b.hardness = 0.35; b.blending = 1.0; b.persistence = 0.6; b.density = 0.7; b.spacing = 0.05;
        b.densityPressure = true; b.minDensity = 0.2;
        v << b;
    }
    {
        BrushSettings b; b.name = "Soft brush"; b.iconKey = "brush"; b.group = "Paint"; b.engine = BrushEngine::Mix;
        b.size = 60; b.minSize = 0.3; b.hardness = 0.1; b.blending = 0.3; b.persistence = 0.6; b.density = 0.5; b.spacing = 0.05;
        b.densityPressure = true; b.minDensity = 0.0;
        v << b;
    }
    {
        BrushSettings b; b.name = "Dry brush"; b.iconKey = "bristle"; b.group = "Paint"; b.engine = BrushEngine::Mix;
        b.size = 40; b.minSize = 0.6; b.hardness = 0.7; b.blending = 0.25; b.persistence = 0.7; b.density = 0.9; b.spacing = 0.04;
        b.texture = "Dry brush"; b.textureStrength = 0.9; b.textureFollowsStroke = true; b.blendingPressure = true;
        v << b;
    }
    // ---- effects (two colors: the second one is the background color of the color panel)
    {
        BrushSettings b; b.name = "Glow"; b.iconKey = "glow"; b.group = "Effects";
        b.size = 70; b.sizePressure = false; b.hardness = 0.0; b.density = 0.12; b.densityPressure = true; b.minDensity = 0.0;
        b.buildup = true; b.spacing = 0.06; b.paintMode = BlendMode::Shine;
        v << b;
    }
    {
        BrushSettings b; b.name = "Hair highlight"; b.iconKey = "highlight"; b.group = "Effects";
        b.size = 18; b.minSize = 0.2; b.hardness = 0.85; b.spacing = 0.3;
        b.bundleLines = 3; b.bundleThickness = 0.55; b.bundleThicknessJitter = 0.5; b.bundleWobble = 0.1; b.bundleLengthJitter = 0.8;
        b.subColorShare = 0.34; b.taperIn = 40; b.taperOut = 60;
        v << b;
    }
    {
        BrushSettings b; b.name = "Droplets"; b.iconKey = "droplet"; b.group = "Effects";
        b.size = 50; b.sizePressure = false; b.hardness = 0.95; b.spacing = 1.1; b.particles = 1; b.particleSize = 0.45;
        b.sizeJitter = 0.8; b.shape = "Droplet"; b.shapeAsTip = true; b.subColorShare = 0.2; b.densityPressure = true; b.minDensity = 0.3;
        v << b;
    }
    return v;
}

QVector<BrushSettings> BrushSettings::decorDefaults() {
    QVector<BrushSettings> v;
    // ---- ribbons: a pattern along the stroke, as wide as the brush
    auto ribbon = [&](const char* name, const char* pattern, double size, double length) {
        BrushSettings b; b.name = name; b.iconKey = "ribbon"; b.group = "Decor";
        b.size = size; b.minSize = 0.85; b.hardness = 1.0; b.spacing = 0.04; b.ribbon = pattern; b.ribbonLength = length;
        v << b;
    };
    ribbon("Lace", "Lace", 40, 1.0);
    ribbon("Chain", "Chain", 26, 1.0);
    ribbon("Fishnet", "Fishnet", 60, 1.0);
    ribbon("Stitches", "Stitches", 14, 1.0);
    ribbon("Beads", "Beads", 18, 1.0);
    ribbon("Rope", "Rope", 20, 1.0);
    ribbon("Bricks", "Bricks", 50, 1.0);
    ribbon("Roof tiles", "Roof tiles", 50, 1.0);
    // ---- stamps: every dab is one of the pictures of the set, with its own size, turn and shade
    auto stamp = [&](const char* name, const char* set, double size, double spacing, double scatter, double sizeJ, double angleJ, double hueJ,
                     double sub) {
        BrushSettings b; b.name = name; b.iconKey = "stamp"; b.group = "Decor"; b.engine = BrushEngine::Mix;
        b.size = size; b.sizePressure = false; b.hardness = 1.0; b.blending = 0.0; b.persistence = 0.0;
        b.spacing = spacing; b.scatter = scatter; b.sizeJitter = sizeJ; b.angleJitter = angleJ; b.hueJitter = hueJ;
        b.valueJitter = hueJ > 0 ? 0.15 : 0.0; b.subColorShare = sub; b.stampSet = set;
        v << b;
        return &v.last();
    };
    stamp("Leaves", "Leaves", 50, 0.9, 0.8, 0.5, 1.0, 0.06, 0.0);
    stamp("Flowers", "Flowers", 46, 1.3, 0.9, 0.5, 1.0, 0.08, 0.25);
    stamp("Stars", "Stars", 36, 1.5, 1.2, 0.7, 0.3, 0.0, 0.3);
    stamp("Clouds", "Clouds", 120, 0.6, 0.4, 0.4, 0.05, 0.0, 0.0);
    stamp("Grass", "Grass", 60, 0.35, 0.15, 0.4, 0.05, 0.05, 0.0);
    stamp("Snowflakes", "Snowflakes", 30, 1.6, 1.5, 0.7, 1.0, 0.0, 0.0);
    stamp("Bubbles", "Bubbles", 40, 1.4, 1.2, 0.7, 0.0, 0.0, 0.0);
    // ---- for backgrounds
    {
        BrushSettings* b = stamp("Foliage", "Leaves", 60, 0.25, 1.4, 0.6, 1.0, 0.1, 0.3);
        b->name = "Foliage"; b->group = "Backgrounds"; b->iconKey = "leaf";
    }
    {
        BrushSettings* b = stamp("Bush", "Bush", 110, 0.4, 0.6, 0.4, 1.0, 0.08, 0.35);
        b->group = "Backgrounds"; b->iconKey = "leaf";
    }
    {
        BrushSettings* b = stamp("Rain", "Rain", 70, 0.5, 2.5, 0.6, 0.0, 0.0, 0.0);
        b->group = "Backgrounds"; b->iconKey = "rain"; b->angle = -20;
    }
    {
        BrushSettings* b = stamp("Snow", "Snowflakes", 22, 0.8, 3.0, 0.8, 1.0, 0.0, 0.0);
        b->group = "Backgrounds"; b->iconKey = "snow";
    }
    {
        BrushSettings b; b.name = "Fur"; b.iconKey = "fur"; b.group = "Backgrounds";
        b.size = 40; b.minSize = 0.5; b.hardness = 0.8; b.spacing = 0.4;
        b.bundleLines = 12; b.bundleThickness = 0.35; b.bundleThicknessJitter = 0.6; b.bundleWobble = 0.5; b.bundleLengthJitter = 1.0;
        b.taperIn = 6; b.taperOut = 30; b.subColorShare = 0.15;
        v << b;
    }
    {
        BrushSettings b; b.name = "Hair strands"; b.iconKey = "fur"; b.group = "Backgrounds";
        b.size = 24; b.minSize = 0.3; b.hardness = 0.9; b.spacing = 0.35;
        b.bundleLines = 5; b.bundleThickness = 0.45; b.bundleThicknessJitter = 0.5; b.bundleWobble = 0.25; b.bundleLengthJitter = 0.6;
        b.taperIn = 30; b.taperOut = 80;
        v << b;
    }
    {
        BrushSettings b; b.name = "Dust"; b.iconKey = "stipple"; b.group = "Backgrounds";
        b.size = 90; b.sizePressure = false; b.hardness = 0.3; b.spacing = 0.3; b.particles = 5; b.particleSize = 0.05;
        b.sizeJitter = 0.9; b.density = 0.6; b.opacityJitter = 0.6; b.densityPressure = true; b.minDensity = 0.1;
        v << b;
    }
    {
        // a brick wall in one stroke, also as a background brush
        BrushSettings b; b.name = "Brick wall"; b.iconKey = "ribbon"; b.group = "Backgrounds";
        b.size = 120; b.sizePressure = false; b.hardness = 1.0; b.spacing = 0.04; b.ribbon = "Bricks"; b.ribbonLength = 1.0;
        v << b;
    }
    return v;
}

QStringList BrushSettings::groups() {
    return {customGroup(), "Basic", "Paint", "Hatching", "Effects", "Decor", "Backgrounds", "Erasers"};
}

// The owner's SAI 2 brushes, from the screenshots of SAI's brush and Tool Preference windows.
// SAI -> GraphiX: Density -> stroke opacity, Min Density -> the density at no pressure,
// the 5 edge shapes -> hardness 0 / .25 / .5 / .75 / 1, texture Intensity (plus half the
// paper Messiness) -> texture strength, Scale % -> texture scale, Blending / Dilution /
// Persistence -> the same (persistence 100 kept just under 1 so the brush still picks up color),
// "Prs. Hard <-> Soft" -> the softness of size, density and mixing.
QVector<BrushSettings> BrushSettings::customDefaults() {
    QVector<BrushSettings> v;
    auto sai = [](const char* name, const char* icon, BrushEngine e, double size, double minSize, double density, double edge) {
        BrushSettings b;
        b.name = name;
        b.iconKey = icon;
        b.group = customGroup();
        b.engine = e;
        b.size = size;
        b.minSize = minSize;
        b.sizePressure = minSize < 1.0;
        b.opacity = density;
        b.density = 1.0;
        b.minDensity = 0.0;
        b.densityPressure = true;
        b.hardness = edge;
        b.spacing = 0.05;
        return b;
    };
    auto texture = [](BrushSettings& b, const char* tex, double intensity, double mess, double scalePct, bool invert) {
        b.texture = tex;
        b.textureStrength = std::clamp((intensity + mess * 0.5) / 100.0, 0.0, 1.0);
        b.textureScale = scalePct / 100.0;
        b.textureInvert = invert;
    };
    auto mixing = [](BrushSettings& b, double blending, double dilution, double persistence) {
        b.blending = blending / 100.0;
        b.dilution = dilution / 100.0;
        b.persistence = std::min(0.97, persistence / 100.0);
        // full blending only moves the colors (pressure would let water in and thin them)
        b.blendingPressure = blending < 100;
    };
    {   // A: the main brush, line art and sketches
        BrushSettings b = sai("Pencil (SAI)", "pencil", BrushEngine::Paint, 9, 0.15, 1.0, 1.0);
        b.sizeSoftness = 100;
        b.densitySoftness = 100;
        b.spacing = 0.06;
        v << b;
    }
    {   // S: smears the colors
        BrushSettings b = sai("111", "blender", BrushEngine::Mix, 40, 0.51, 0.40, 0.5);
        texture(b, "Paper", 28, 20, 67, true);
        mixing(b, 100, 100, 100);
        b.sizeSoftness = 118;
        b.densitySoftness = 70;
        b.blendSoftness = 135;
        v << b;
    }
    {   // D: an ordinary hard brush for the base colors
        BrushSettings b = sai("333", "brush", BrushEngine::Mix, 10, 0.25, 0.60, 0.25);
        texture(b, "Paper", 40, 10, 59, true);
        mixing(b, 19, 32, 72);
        b.sizeSoftness = 129;
        b.densitySoftness = 38;
        b.blendSoftness = 10;
        v << b;
    }
    {   // Z: soft blur, softly melts the colors together
        BrushSettings b = sai("blur", "blur", BrushEngine::Mix, 350, 1.0, 0.20, 0.0);
        texture(b, "Paper", 30, 15, 51, true);
        mixing(b, 100, 100, 91);
        b.sizeSoftness = 200;
        b.densitySoftness = 99;
        b.blendSoftness = 171;
        v << b;
    }
    {   // C: airbrush to spread colors
        BrushSettings b = sai("AirBrush (SAI)", "airbrush", BrushEngine::Paint, 8, 0.35, 0.30, 0.25);
        texture(b, "Fabric 01", 20, 7, 70, false);
        b.sizeSoftness = 144;
        b.densitySoftness = 0;
        v << b;
    }
    {   // B: a little firmer than the soft blur, softer than the smearing brush
        BrushSettings b = sai("b blur", "blender", BrushEngine::Mix, 35, 1.0, 0.40, 0.25);
        texture(b, "Paper", 45, 20, 56, true);
        mixing(b, 100, 100, 100);
        b.sizeSoftness = 200;
        b.densitySoftness = 71;
        b.blendSoftness = 50;
        v << b;
    }
    return v;
}

QString BrushSettings::defaultGroupFor(const QString& name) {
    static const QHash<QString, QString> map = {
        {"Pen", "Basic"},          {"Ink", "Basic"},       {"Pencil", "Basic"},       {"Binary", "Basic"},
        {"Marker", "Basic"},       {"AirBrush", "Basic"},  {"Soft pencil", "Basic"},  {"Brush", "Paint"},
        {"Water Color", "Paint"},  {"Chalk", "Paint"},     {"Bristle", "Paint"},      {"Charcoal", "Paint"},
        {"Blender", "Paint"},      {"Soft brush", "Paint"}, {"Dry brush", "Paint"},   {"Hatching", "Hatching"},
        {"Hatching with white", "Hatching"}, {"Hatching blender", "Hatching"}, {"Textured hatching", "Hatching"},
        {"Stipple", "Hatching"},   {"Glitter", "Effects"}, {"Glow", "Effects"},       {"Hair highlight", "Effects"},
        {"Droplets", "Effects"},   {"Eraser", "Erasers"},  {"Soft Eraser", "Erasers"}, {"Blur", "Erasers"},
        {"Smudge", "Erasers"},     {"Lace", "Decor"},      {"Chain", "Decor"},        {"Fishnet", "Decor"},
        {"Stitches", "Decor"},     {"Beads", "Decor"},     {"Rope", "Decor"},         {"Bricks", "Decor"},
        {"Roof tiles", "Decor"},   {"Leaves", "Decor"},    {"Flowers", "Decor"},      {"Stars", "Decor"},
        {"Clouds", "Decor"},       {"Grass", "Decor"},     {"Snowflakes", "Decor"},   {"Bubbles", "Decor"},
        {"Foliage", "Backgrounds"}, {"Bush", "Backgrounds"}, {"Rain", "Backgrounds"},  {"Snow", "Backgrounds"},
        {"Fur", "Backgrounds"},    {"Hair strands", "Backgrounds"}, {"Dust", "Backgrounds"}, {"Brick wall", "Backgrounds"}};
    return map.value(name, "Basic");
}

void PressureCurve::set(const QVector<QPointF>& pts) {
    m_points = pts.size() >= 2 ? pts : QVector<QPointF>{QPointF(0, 0), QPointF(1, 1)};
    m_lut = curveLut(m_points, 1024);
}

float PressureCurve::map(float p) const {
    float f = clamp01(p) * 1023.f;
    int i = int(f);
    if (i >= 1023) return m_lut[1023];
    float t = f - float(i);
    return m_lut[size_t(i)] + (m_lut[size_t(i) + 1] - m_lut[size_t(i)]) * t;
}

}  // namespace gx
